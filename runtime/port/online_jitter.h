#pragma once
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>

// The native online queues (host: P2's inputs, guest: the host's world) hand
// out one frame per frame and only catch up once more than 8 frames are
// waiting. Every late packet leaves them that much further behind for good, so
// over the internet both settle near 8 frames of delay that nothing needs.
// QueueTrim watches the smallest backlog over a window: frames that were never
// needed as a cushion are skipped, one at a time, until only the cushion is left.
namespace tenchu::online::jitter {
struct QueueTrim {
    unsigned window = 120, cushion = 2, limit = 30;
    unsigned frames = 0, low = UINT_MAX, budget = 0;
    // Call once per consumed frame with the frames waiting before it is used.
    // Returns true when one extra frame may be skipped now.
    bool Step(unsigned backlog, bool skippable) {
        low = std::min(low, backlog);
        bool skip = false;
        if (budget && skippable && backlog > cushion + 1) { --budget; skip = true; }
        if (++frames >= window) {
            budget = low > cushion ? std::min(low - cushion, limit) : 0;
            frames = 0; low = UINT_MAX;
        }
        return skip;
    }
    void Reset() { frames = 0; low = UINT_MAX; budget = 0; }
};

// A P2 input record (28 bytes): +0/+4 held buttons, +8 pressed this frame,
// +12 analog stick, +16 button word, then camera values. Skipping a record is
// only safe when its digital fields repeat the record before it, so presses,
// releases and attacks always reach the host.
inline bool SameButtons(const uint8_t* record, const uint8_t* previous) {
    return std::memcmp(record, previous, 12) == 0 && std::memcmp(record + 16, previous + 16, 2) == 0 &&
           record[8] == 0 && record[9] == 0 && record[10] == 0 && record[11] == 0;
}
}
