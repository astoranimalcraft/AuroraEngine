// Aurora3D — small runtime helpers shared by the whole library.
//
// The header-side game helpers (Transform, Random, Timer, easing) are pure math.
// The only thing that needs a translation unit is the process-wide RNG, which is
// seeded from the clock so a program that never calls SetRandomSeed() still gets
// a different sequence every run.
#include "aurora/math.h"

#include "internal/common.h"

namespace a3d {

Random& GlobalRandom() {
    // Function-local static: constructed on first use, so nothing runs before
    // main() and the seed can come from the clock.
    static Random instance = [] {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        Random rng(static_cast<uint32_t>(counter.QuadPart ^ (counter.QuadPart >> 32)));
        return rng;
    }();
    return instance;
}

void SetRandomSeed(uint32_t seed) { GlobalRandom().Seed(seed); }

float RandomFloat(float minValue, float maxValue) {
    return GlobalRandom().Range(minValue, maxValue);
}

int RandomInt(int minValue, int maxValue) { return GlobalRandom().RangeInt(minValue, maxValue); }

bool RandomChance(float probability) { return GlobalRandom().Chance(probability); }

} // namespace a3d
