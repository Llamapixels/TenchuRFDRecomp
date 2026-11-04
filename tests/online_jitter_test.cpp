#include "port/online_jitter.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace tenchu::online::jitter;

// A queue fed in batches of `batch` frames, with a packet held back now and then,
// emptied one frame per frame with the native catch-up past 8 frames.
struct Result { double average = 0; unsigned underflows = 0, skipped = 0, worst_late = 0; };
static Result Run(bool trim, unsigned batch, unsigned spike_every, unsigned frames = 36000) {
    QueueTrim q; Result r; unsigned backlog = 0, late = 0; std::vector<unsigned> held; double total = 0; unsigned counted = 0;
    unsigned rng = 12345;
    for (unsigned f = 0; f < frames; ++f) {
        if (f % batch == 0) held.push_back(batch);
        rng = rng * 1103515245u + 12345u;
        const bool delayed = late == 0 && (rng >> 16) % spike_every < batch;  // a late packet every ~spike_every frames
        if (delayed) late = 1 + (rng >> 8) % 6;
        if (late) --late; else { for (auto n : held) backlog += n; held.clear(); }
        if (trim && q.Step(backlog, true)) { --backlog; ++r.skipped; }
        if (backlog > 8) { --backlog; ++r.skipped; }       // native catch-up
        if (backlog) --backlog; else ++r.underflows;      // native holds on empty
        if (f > frames / 2) { total += backlog; ++counted; }
    }
    r.average = total / counted; return r;
}

int main() { try {
    auto check = [](bool value, const char* why) { if (!value) throw std::runtime_error(why); };
    for (unsigned batch : {1u, 4u}) {
        // A steady connection with a hiccup every ~10 seconds: delay comes right down.
        const auto stock = Run(false, batch, 600), trimmed = Run(true, batch, 600);
        std::printf("batch %u, steady: stock waits %.2f frames (%u empty), trimmed %.2f frames (%u empty, %u skipped)\n", batch,
                    stock.average, stock.underflows, trimmed.average, trimmed.underflows, trimmed.skipped);
        check(stock.average > 5, "stock queue settles near the 8 frame catch-up");
        check(trimmed.average + 1.5 < stock.average, "trimming removes the built-up delay");
        check(trimmed.underflows < 36000 / 100, "a steady connection rarely runs dry");
        // A bad connection with a late packet every second: trimming backs off.
        const auto bad = Run(false, batch, 60), bad_trimmed = Run(true, batch, 60);
        std::printf("batch %u, bad: stock waits %.2f frames (%u empty), trimmed %.2f frames (%u empty, %u skipped)\n", batch,
                    bad.average, bad.underflows, bad_trimmed.average, bad_trimmed.underflows, bad_trimmed.skipped);
        check(bad_trimmed.underflows < 36000 / 40, "a bad connection keeps most of its cushion");
    }
    // Never skip a frame that is not marked skippable (a button press or release).
    QueueTrim q; unsigned skipped = 0;
    for (unsigned f = 0; f < 1000; ++f) skipped += q.Step(8, false);
    check(skipped == 0, "frames with button changes are never skipped");
    // Never trim into the cushion.
    q.Reset(); skipped = 0;
    for (unsigned f = 0; f < 1000; ++f) skipped += q.Step(3, true);
    check(skipped == 0, "a queue at its cushion is left alone");

    uint8_t a[28]{}, b[28]{};
    a[0] = b[0] = 0x10; b[12] = 0x7F;  // same held buttons, stick moved
    check(SameButtons(b, a), "stick movement alone can be skipped");
    b[8] = 1; check(!SameButtons(b, a), "a press this frame is kept");
    b[8] = 0; b[0] = 0; check(!SameButtons(b, a), "a release is kept");
    b[0] = 0x10; b[16] = 2; check(!SameButtons(b, a), "button word changes are kept");
    a[8] = 1; b[16] = 0; std::memcpy(b, a, 28); check(!SameButtons(b, a), "a repeated press flag is kept");
    std::puts("PASS: online queues drop built-up delay, keep a cushion and never skip button changes.");
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; } }
