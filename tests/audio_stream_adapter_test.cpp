#include "xdk/hle.h"
#include "core/log.h"
#include <array>
#include <cstdio>
#include <stdexcept>

X86_EXTERN_FUNC(sub_0036F5EF);
X86_EXTERN_FUNC(sub_0036DEEC);
namespace kernel { uint8_t* g_base = nullptr; }
namespace logx { void Write(Level, const char*, ...) {} }
[[noreturn]] void x86_trap(X86Context&, uint32_t, const char* message) { throw std::runtime_error(message); }

static unsigned calls = 0;
uint32_t kernel::CallGuest(X86Context& ctx, uint32_t fn, const uint32_t* args, size_t count) {
    ++calls;
    if (fn == SYM(CDirectSoundStream_Pause)) {
        if (count != 2 || args[0] != 0x500 || args[1] > 2)
            throw std::runtime_error("wrong pause arguments");
        ctx.ebx = ctx.esi = 0xBAD;
        return 0;
    }
    if (fn != SYM(DirectSoundCreateStream) || count != 2 || args[0] != 0x600 || args[1] != 0x700)
        throw std::runtime_error("wrong stream target or arguments");
    W32(args[1], 0x800);
    // Stress the real hle::Call helper's preservation of caller registers.
    ctx.eax = ctx.ebx = ctx.ecx = ctx.edx = ctx.esi = ctx.edi = ctx.ebp = 0xBAD;
    return 0;
}

int main() {
    std::array<uint8_t, 4096> memory{};
    kernel::g_base = memory.data();
    X86Context ctx{};
    auto reset = [&] {
        ctx = {};
        ctx.esp = 0x400;
        ctx.ebx=11; ctx.ecx=12; ctx.edx=13; ctx.esi=14; ctx.edi=15; ctx.ebp=16;
        kernel::W32(0x400, 0x12345678);
        kernel::W32(0x404, 0x500);
        kernel::W32(0x408, 0x600);
        kernel::W32(0x40C, 0x700);
        kernel::W32(0x410, 0);
    };
    auto check = [](bool ok, const char* what) { if (!ok) throw std::runtime_error(what); };
    try {
        reset();
        sub_0036F5EF(ctx, memory.data());
        check(calls==1 && ctx.eax==0 && kernel::R32(0x700)==0x800, "creation must return the runtime-backed stream");
        check(ctx.esp==0x414 && kernel::R32(0x400)==0x12345678, "four-argument stdcall must preserve return address and pop 20 bytes");
        check(ctx.ebx==11 && ctx.ecx==12 && ctx.edx==13 && ctx.esi==14 && ctx.edi==15 && ctx.ebp==16, "adapter must preserve registers across bridge call");
        reset(); kernel::W32(0x408,0);
        sub_0036F5EF(ctx,memory.data());
        check(calls==1 && ctx.eax==0x80070057u && ctx.esp==0x414, "invalid descriptor must fail without calling the stream backend");
        reset(); kernel::W32(0x410,1);
        bool trapped=false;
        try { sub_0036F5EF(ctx,memory.data()); } catch (const std::runtime_error&) { trapped=true; }
        check(trapped && calls==1, "unsupported aggregation must trap before backend invocation");
        for (unsigned mode=0; mode<3; ++mode) {
            reset(); kernel::W32(0x408,0); kernel::W32(0x40C,0); kernel::W32(0x410,mode);
            sub_0036DEEC(ctx,memory.data());
            check(ctx.eax==0 && ctx.esp==0x414 && ctx.ebx==11 && ctx.esi==14,
                  "immediate PauseEx must preserve registers and pop four arguments");
        }
        reset(); kernel::W32(0x408,1);
        trapped=false;
        try { sub_0036DEEC(ctx,memory.data()); } catch (const std::runtime_error&) { trapped=true; }
        check(trapped,"scheduled pause must fail explicitly");
        std::puts("PASS: stream creation and pause ABI, argument forwarding, registers, stack and explicit failures");
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    return 0;
}
