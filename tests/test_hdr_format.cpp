// Native (host-independent) tests for the float/half conversion and the
// environment-map summary. Build & run with scripts/test-math.sh.
//
// Both are part of HDR support and both are easy to get subtly wrong: the half
// conversion has subnormal and rounding paths, and the summary has to find a sun
// that occupies a handful of texels in a large image.
#include "internal/envmap.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace n3d;
using namespace n3d::internal;

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++gChecks;                                                         \
        if (!(cond)) {                                                     \
            ++gFailures;                                                   \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                  \
    } while (0)

static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// A round trip through half is lossy, so it is compared against the value the
// half format can actually represent, not the original.
static void TestHalfRoundTrip() {
    std::printf("half <-> float\n");
    for (float value : {0.0f, 0.5f, 1.0f, 2.0f, 100.0f, 1000.0f, 0.25f, -3.5f}) {
        const float back = HalfToFloat(FloatToHalf(value));
        CHECK(Near(back, value, std::fabs(value) * 0.001f + 1e-6f));
    }

    // 1.0 is the canonical half bit pattern: exponent 15, mantissa 0.
    CHECK(FloatToHalf(1.0f) == 0x3C00u);
    CHECK(HalfToFloat(0x3C00u) == 1.0f);
    CHECK(HalfToFloat(0x0000u) == 0.0f);
    CHECK(HalfToFloat(0x8000u) == 0.0f);   // negative zero is still zero

    // The whole point of the format: values far above 1 survive.
    CHECK(HalfToFloat(FloatToHalf(2048.0f)) == 2048.0f);
    CHECK(FloatToHalf(65504.0f) == 0x7BFFu);   // largest finite half

    // Overflow saturates to infinity rather than wrapping.
    const uint16_t inf = FloatToHalf(1.0e9f);
    CHECK((inf & 0x7C00u) == 0x7C00u && (inf & 0x03FFu) == 0u);

    // Subnormals: the smallest positive half is 2^-24, and it must decode, not
    // flush to zero.
    CHECK(Near(HalfToFloat(0x0001u), std::ldexp(1.0f, -24), 1e-9f));
    CHECK(HalfToFloat(0x0001u) > 0.0f);

    // A tiny float underflows to zero.
    CHECK(FloatToHalf(1.0e-30f) == 0u);
}

static void TestEnvSummaryFindsSmallSun() {
    std::printf("environment summary\n");

    // A 512x256 map: dark blue everywhere, with a 2x2 white sun in the upper half.
    // The size matters: it is large enough that a stride-based summary would step
    // straight over a disc this small, which is the regression being guarded.
    const uint32_t width = 512, height = 256;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = 20;
        pixels[i + 1] = 30;
        pixels[i + 2] = 90;
        pixels[i + 3] = 255;
    }
    // u = 0.25 means azimuth = -PI/2, i.e. -X. v = 0.25 means elevation = +45 deg.
    // The coordinates are deliberately not multiples of a power of two.
    const uint32_t sunX = 131, sunY = 65;
    for (uint32_t dy = 0; dy < 2; ++dy) {
        for (uint32_t dx = 0; dx < 2; ++dx) {
            const size_t index = (static_cast<size_t>(sunY + dy) * width + (sunX + dx)) * 4;
            pixels[index + 0] = 255;
            pixels[index + 1] = 255;
            pixels[index + 2] = 255;
        }
    }

    const EnvironmentSummary summary =
        SummarizeEnvironment(pixels.data(), width, height, TextureFormat::RGBA8);
    CHECK(summary.valid);

    // The sun direction must point at the sun, not at whatever a stride happened
    // to land on. This is the regression the stride-based version had.
    const float u = (static_cast<float>(sunX) + 1.0f) / static_cast<float>(width);
    const float v = (static_cast<float>(sunY) + 1.0f) / static_cast<float>(height);
    const float azimuth = (u - 0.5f) * 6.28318530717958647692f;
    const float elevation = (0.5f - v) * 3.14159265358979323846f;
    const float ce = std::cos(elevation);
    const Vec3 expected{ce * std::sin(azimuth), std::sin(elevation), ce * std::cos(azimuth)};
    CHECK(Near(Dot(summary.sunDirection, expected), 1.0f, 0.02f));

    // The mean is of the upper half only, so it sits between the dark sky and the
    // white sun: strictly above the background, strictly below 1.
    CHECK(summary.average[2] > 90.0f / 255.0f);
    CHECK(summary.average[2] < 1.0f);
    CHECK(summary.average[0] < summary.average[2]);   // still blue-dominant
    CHECK(summary.peakLuminance > 0.9f);
}

static void TestEnvSummaryHalfInput() {
    std::printf("environment summary (RGBA16F)\n");

    // The same idea with half-float input, to cover the other decode branch. The
    // sun here is 8x white, a value an 8-bit texture cannot represent at all.
    const uint32_t width = 64, height = 32;
    std::vector<uint16_t> halves(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < halves.size(); i += 4) {
        halves[i + 0] = FloatToHalf(0.1f);
        halves[i + 1] = FloatToHalf(0.1f);
        halves[i + 2] = FloatToHalf(0.4f);
        halves[i + 3] = FloatToHalf(1.0f);
    }
    const uint32_t sunX = 48, sunY = 8;
    const size_t sunIndex = (static_cast<size_t>(sunY) * width + sunX) * 4;
    halves[sunIndex + 0] = FloatToHalf(8.0f);
    halves[sunIndex + 1] = FloatToHalf(8.0f);
    halves[sunIndex + 2] = FloatToHalf(8.0f);

    const EnvironmentSummary summary = SummarizeEnvironment(
        reinterpret_cast<const uint8_t*>(halves.data()), width, height, TextureFormat::RGBA16F);
    CHECK(summary.valid);
    CHECK(summary.peakLuminance > 7.0f);   // the 8x value survived the decode

    const float u = (static_cast<float>(sunX) + 0.5f) / static_cast<float>(width);
    const float v = (static_cast<float>(sunY) + 0.5f) / static_cast<float>(height);
    const float azimuth = (u - 0.5f) * 6.28318530717958647692f;
    const float elevation = (0.5f - v) * 3.14159265358979323846f;
    const float ce = std::cos(elevation);
    const Vec3 expected{ce * std::sin(azimuth), std::sin(elevation), ce * std::cos(azimuth)};
    CHECK(Near(Dot(summary.sunDirection, expected), 1.0f, 0.02f));
}

static void TestEnvSummaryRejectsDegenerate() {
    std::printf("environment summary (degenerate)\n");
    std::vector<uint8_t> pixels(4, 255);
    CHECK(!SummarizeEnvironment(pixels.data(), 1, 1, TextureFormat::RGBA8).valid);
    CHECK(!SummarizeEnvironment(nullptr, 4, 4, TextureFormat::RGBA8).valid);
}

int main() {
    TestHalfRoundTrip();
    TestEnvSummaryFindsSmallSun();
    TestEnvSummaryHalfInput();
    TestEnvSummaryRejectsDegenerate();

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
