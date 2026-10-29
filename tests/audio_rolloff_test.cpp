#include "audio/rolloff_state.h"
#include <cstdio>
#include <limits>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++failures; } };
    tenchu::RolloffState state;
    check(state.Attenuate(0.25f) == 0.25f, "default preserves normal attenuation");
    check(state.Set(0, 1) == 0 && state.Attenuate(0.25f) == 0.25f, "deferred setting waits for commit");
    state.Commit();
    check(state.Attenuate(0.25f) == 1, "zero disables distance attenuation after commit");
    check(state.Set(1, 0) == 0 && state.Attenuate(0.25f) == 0.25f, "immediate default restores attenuation");
    check(state.Set(0, 1) == 0 && state.Set(1, 0) == 0, "immediate update supersedes pending value");
    state.Commit();
    check(state.active == 1, "superseded pending setting cannot reappear");
    check(state.Set(-1, 0) == 0x80070057u && state.Set(11, 0) == 0x80070057u, "invalid factor rejected");
    check(state.Set(std::numeric_limits<float>::quiet_NaN(), 0) == 0x80070057u, "NaN rejected");
    check(state.Set(0, 2) == 0x80070057u, "invalid apply mode rejected");
    check(state.Set(0.5f, 0) == 0x80004001u && state.active == 1, "unvalidated factor fails explicitly without state change");
    if (!failures) std::puts("PASS: rolloff endpoints, deferred commit and invalid/unsupported requests");
    return failures ? 1 : 0;
}
