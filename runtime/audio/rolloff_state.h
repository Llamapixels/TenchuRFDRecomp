#pragma once
#include <cmath>
#include <cstdint>

namespace tenchu {
// Support the two documented endpoints needed by startup. Other factors stay
// explicit until the Xbox attenuation curve can be validated against reference.
struct RolloffState {
    float active = 1, pending = 1;
    bool dirty = false;
    uint32_t Set(float value, uint32_t apply) {
        if (!std::isfinite(value) || value < 0 || value > 10 || apply > 1) return 0x80070057u;
        if (value != 0 && value != 1) return 0x80004001u;
        pending = value;
        dirty = apply != 0;
        if (!dirty) active = value;
        return 0;
    }
    void Commit() { if (dirty) { active = pending; dirty = false; } }
    float Attenuate(float normal) const { return active == 0 ? 1.0f : normal; }
};
}
