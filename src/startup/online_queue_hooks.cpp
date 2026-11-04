#include "cpu/x86_ops.h"
#include "kernel/memory.h"
#include "port/online.h"
#include "port/online_jitter.h"
#include "core/log.h"
#include <cstdlib>
#include <cstring>

// Keeps the two native online queues short. The host replays P2's inputs from
// a queue (00021910) and the guest replays the host's world from another
// (00017B50). Both only catch up past 8 frames, so delay builds up after every
// late packet and stays. See runtime/port/online_jitter.h.
X86_EXTERN_FUNC(__imp__sub_00021910);
X86_EXTERN_FUNC(__imp__sub_00017B50);
namespace {
using tenchu::online::jitter::QueueTrim;
using tenchu::online::jitter::SameButtons;
constexpr uint32_t kRing = 200;
uint32_t Read(const uint8_t* b, uint32_t p) { uint32_t n = 0; std::memcpy(&n, b + p, 4); return n; }
void Write(uint8_t* b, uint32_t p, uint32_t n) { std::memcpy(b + p, &n, 4); }
uint32_t Gap(uint32_t writer, uint32_t reader) { return (writer + kRing - reader) % kRing; }
bool Enabled() {
    static const bool on = [] { const char* v = std::getenv("TENCHU_ONLINE_TRIM"); return !v || std::strcmp(v, "0") != 0; }();
    return on;
}
bool Active(const uint8_t* b) { return tenchu::online::enabled.load() && Read(b, 0x5A97D0) && Read(b, 0x5A97CC); }

struct Report {
    const char* what; unsigned frames = 0, low = UINT_MAX, high = 0, skipped = 0, reports = 0;
    void Note(unsigned backlog) { ++frames; low = std::min(low, backlog); high = std::max(high, backlog); }
    void Flush() {
        if (frames < 600 || reports >= 30) return;
        LOGR("Online queue: %s waiting %u..%u frames over the last %u, %u skipped to cut delay", what, low, high, frames, skipped);
        ++reports; frames = 0; low = UINT_MAX; high = 0; skipped = 0;
    }
};
QueueTrim host_trim, guest_trim;
Report host_report{"host P2 inputs"}, guest_report{"guest world playback"};
}

X86_FUNC(sub_00021910) {
    // Same guard as the native consumer: in a mission with P2's actor present.
    if (Enabled() && Active(base) && Read(base, 0x5A97B8) && (Read(base, 0x8E38BC) & 0xFFFF) == 3 && Read(base, 0x1BAB114)) {
        const uint32_t queued = Read(base, 0x5A97EC), reader = Read(base, 0x5A8E7C), writer = Read(base, 0x5A8758);
        if (reader < kRing && writer < kRing) {
            const uint32_t backlog = queued ? std::min(queued, Gap(writer, reader)) : 0;
            const uint32_t next = (reader + 1) % kRing, previous = (reader + kRing - 1) % kRing;
            const bool skippable = backlog && SameButtons(base + 0x59CD80 + reader * 28, base + 0x59CD80 + previous * 28);
            host_report.Note(backlog);
            if (host_trim.Step(backlog, skippable) && next != writer) {
                Write(base, 0x5A8E7C, next); Write(base, 0x5A97EC, queued - 1); ++host_report.skipped;
            }
            host_report.Flush();
        }
    } else if (!Read(base, 0x5A97EC)) host_trim.Reset();
    __imp__sub_00021910(ctx, base);
}

X86_FUNC(sub_00017B50) {
    // Same conditions as the guest's native playback step at 00017D3C..00017D6F.
    if (Enabled() && Active(base) && !Read(base, 0x5A97B8) && !Read(base, 0x5A97E0) && !Read(base, 0x5A97E4) &&
        !Read(base, 0x5A97E8) && Read(base, 0x5A97D4) && int32_t(Read(base, 0x5A8750)) >= 2) {
        const uint32_t queued = Read(base, 0x5A97EC);
        const uint32_t play = Read(base, 0x59E370), latest = Read(base, 0x5A1640);
        const uint32_t second = Read(base, 0x58FEE8), second_end = Read(base, 0x59E36C);
        const uint32_t third = Read(base, 0x5A8748), third_end = Read(base, 0x5A4784);
        if (play < kRing && latest < kRing && second < kRing && second_end < kRing && third < kRing && third_end < kRing) {
            const uint32_t backlog = queued ? std::min({queued, Gap(latest, play), Gap(second_end, second), Gap(third_end, third)}) : 0;
            guest_report.Note(backlog);
            // Skipping a world frame drops one picture, like the native catch-up.
            if (guest_trim.Step(backlog, backlog > 0)) {
                Write(base, 0x59E370, (play + 1) % kRing); Write(base, 0x58FEE8, (second + 1) % kRing);
                Write(base, 0x5A8748, (third + 1) % kRing); Write(base, 0x5A97EC, queued - 1); ++guest_report.skipped;
            }
            guest_report.Flush();
        }
    } else if (!Read(base, 0x5A97EC)) guest_trim.Reset();
    __imp__sub_00017B50(ctx, base);
}
