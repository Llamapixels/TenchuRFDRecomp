#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "graphics/renderer_selection.h"
#include <atomic>
#include <thread>

// A freeze leaves nothing in the log: the game keeps running its threads but stops
// presenting. When no frame reaches the screen for kStall while the window is
// neither minimized nor being resized, every guest thread's registers and stack
// go to the log once, so a hang reported from any menu can be traced afterwards.
extern std::atomic<uint64_t> g_tenchu_presents;

namespace {
constexpr uint64_t kStall = 8000;  // ms; loading screens still present frames

void Threads() {
    for (auto& t : kernel::Threads()) {
        if (!t->ctx) continue;
        LOGR("thread %X, last indirect call %08X", t->id, t->ctx->last_icall);
        kernel::LogGuestStack(*t->ctx, 48);
    }
    logx::Flush();
}

void Dump(uint64_t stalled_ms) {
    LOGR("Hang watchdog: no frame presented for %.1f s; guest threads follow", stalled_ms / 1000.0);
    Threads();
}

void Watch() {
    uint64_t seen = g_tenchu_presents.load(), since = GetTickCount64();
    bool dumped = false;
    for (;;) {
        Sleep(500);
        const uint64_t now = GetTickCount64(), presents = g_tenchu_presents.load();
        HWND window = static_cast<HWND>(app::Window());
        if (presents != seen || !presents || TenchuPresentationPaused() || (window && IsIconic(window))) {
            if (presents != seen && dumped) LOGR("Hang watchdog: frames resumed");
            seen = presents;
            since = now;
            dumped = false;
            continue;
        }
        if (!dumped && now - since >= kStall) {
            Dump(now - since);
            dumped = true;
        }
    }
}
}  // namespace

// Ctrl+F3 and F6 > Tools: a game stuck on one screen keeps presenting frames, so
// the watchdog never sees it; this dumps the same thread state on request.
void TenchuDumpThreads(const char* why) {
    LOGR("Thread dump requested (%s); guest threads follow", why);
    Threads();
}

void TenchuStartHangWatchdog() {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;
    std::thread(Watch).detach();
}
