// NexiumEngine — float/half conversion and environment-map summary (internal).
//
// Free of any D3D12 dependency, so the parts of HDR support that can actually be
// wrong — the half-float rounding, and the search for the brightest texel — are
// compiled and executed by the native test suite on a machine with no GPU.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "nexium/math.h"
#include "nexium/texture.h"

namespace n3d {
namespace internal {

inline uint32_t BytesPerPixel(TextureFormat format) {
    return format == TextureFormat::RGBA16F ? 8u : 4u;
}

// IEEE 754 binary16 conversion. The engine stores HDR pixels as half floats
// because a float32 texture is twice the bandwidth for detail an environment map
// does not need: half still carries 5 bits of exponent, which is the whole point
// of HDR, and 10 bits of mantissa, more than 8-bit sRGB ever had.
inline uint16_t FloatToHalf(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));

    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = bits & 0x007FFFFFu;

    if (exponent <= 0) {
        // Subnormal or underflow to zero. The implicit leading 1 is shifted into
        // the mantissa, with a rounding term derived from the bits being dropped.
        if (exponent < -10) return static_cast<uint16_t>(sign);
        mantissa |= 0x00800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exponent);
        uint32_t half = mantissa >> shift;
        if (mantissa & (1u << (shift - 1))) ++half;   // round to nearest
        return static_cast<uint16_t>(sign | half);
    }
    if (exponent >= 31) {
        // Overflow: infinity, or NaN when the source was NaN.
        if (exponent == 31 + 127 - 15 && mantissa != 0) {
            return static_cast<uint16_t>(sign | 0x7E00u);
        }
        return static_cast<uint16_t>(sign | 0x7C00u);
    }

    uint32_t half = sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
    if (mantissa & 0x00001000u) ++half;   // round to nearest
    return static_cast<uint16_t>(half);
}

inline float HalfToFloat(uint16_t half) {
    const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16;
    const uint32_t exponent = (half >> 10) & 0x1Fu;
    uint32_t mantissa = half & 0x03FFu;

    uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;   // signed zero
        } else {
            // Subnormal: normalise it into a float32.
            int32_t e = -1;
            do {
                ++e;
                mantissa <<= 1;
            } while ((mantissa & 0x0400u) == 0);
            mantissa &= 0x03FFu;
            bits = sign | (static_cast<uint32_t>(127 - 15 - e) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13);   // infinity / NaN
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }

    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// What the lighting needs from an environment map, computed once at upload time.
struct EnvironmentSummary {
    bool valid = false;
    float average[3] = {0.0f, 0.0f, 0.0f};
    Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    float peakLuminance = 0.0f;
};

// Walks an equirectangular map and records the mean colour and the direction of
// the brightest region. A full irradiance solve this is not; it is the cheapest
// summary that still makes a sky light a scene.
//
// Every texel is visited. Sampling a subset would be cheaper, but the brightest
// thing in a sky is usually a small disc: in a 4K map the sun spans a handful of
// texels, so a stride would step over it and aim the light at the wrong part of
// the sky. The whole scan costs ~20 ms for 4096x2048, once, at load.
inline EnvironmentSummary SummarizeEnvironment(const uint8_t* pixels, uint32_t width,
                                               uint32_t height, TextureFormat format) {
    EnvironmentSummary summary;
    if (!pixels || width < 2 || height < 2) return summary;

    double sum[3] = {0.0, 0.0, 0.0};
    uint64_t count = 0;
    double peak = -1.0;
    Vec3 sunDirection{0.0f, 1.0f, 0.0f};

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * width + x;
            float rgb[3];
            if (format == TextureFormat::RGBA16F) {
                const uint16_t* half = reinterpret_cast<const uint16_t*>(pixels) + index * 4;
                for (int c = 0; c < 3; ++c) rgb[c] = HalfToFloat(half[c]);
            } else {
                for (int c = 0; c < 3; ++c) rgb[c] = pixels[index * 4 + c] / 255.0f;
            }

            // The mean covers the upper hemisphere only. Folding the ground in
            // would drag the ambient toward black, and the point of the average is
            // to tint the shadows with the colour of the sky above them.
            if (y < height / 2) {
                for (int c = 0; c < 3; ++c) sum[c] += rgb[c];
                ++count;
            }

            // The brightest texel is searched over the whole sphere, though: an
            // environment map lit from below still has a "sun" somewhere.
            // Rec. 709 luma, the same weighting the eye uses, so the "sun" is the
            // brightest thing visually rather than the most saturated.
            const double luminance = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
            if (luminance > peak) {
                peak = luminance;
                // Invert the equirectangular mapping: u is the azimuth around Y,
                // v the elevation from +Y down to -Y.
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
                const float azimuth = (u - 0.5f) * 6.28318530717958647692f;
                const float elevation = (0.5f - v) * 3.14159265358979323846f;
                const float ce = std::cos(elevation);
                sunDirection = Vec3{ce * std::sin(azimuth), std::sin(elevation),
                                    ce * std::cos(azimuth)};
            }
        }
    }

    if (count == 0) return summary;
    for (int c = 0; c < 3; ++c) summary.average[c] = static_cast<float>(sum[c] / count);
    summary.sunDirection = Normalize(sunDirection);
    summary.peakLuminance = static_cast<float>(peak);
    summary.valid = true;
    return summary;
}

} // namespace internal
} // namespace n3d
