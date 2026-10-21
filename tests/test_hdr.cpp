// Native (host-independent) tests for the Radiance RGBE decoder.
// Build & run with scripts/test-math.sh — no DirectX and no GPU required.
//
// The decoder is the half of HDR loading that can actually be wrong in a way a
// compiler will not catch, so it is tested here with hand-built byte streams:
// one flat scanline, one run-length encoded scanline, and the malformed cases.
#include "internal/hdr.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

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

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Builds the standard ASCII header for a `width` x `height` image.
static std::string Header(uint32_t width, uint32_t height) {
    return std::string("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y ") + std::to_string(height) +
           " +X " + std::to_string(width) + "\n";
}

// A flat (non-RLE) scanline: one raw RGBE pixel per texel.
static void AppendFlatRow(std::string& out, uint8_t r, uint8_t g, uint8_t b, uint8_t e,
                          uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        out.push_back(static_cast<char>(r));
        out.push_back(static_cast<char>(g));
        out.push_back(static_cast<char>(b));
        out.push_back(static_cast<char>(e));
    }
}

// A new-style RLE scanline, encoded the way the format specifies: the 2,2,w,h
// marker, then four independently encoded channel planes.
static void AppendRleRow(std::string& out, uint8_t r, uint8_t g, uint8_t b, uint8_t e,
                         uint32_t width) {
    out.push_back(2);
    out.push_back(2);
    out.push_back(static_cast<char>((width >> 8) & 0xFF));
    out.push_back(static_cast<char>(width & 0xFF));
    for (uint8_t value : {r, g, b, e}) {
        // One literal block holding the whole row: count must be <= 128.
        CHECK(width <= 128);
        out.push_back(static_cast<char>(width));
        for (uint32_t i = 0; i < width; ++i) out.push_back(static_cast<char>(value));
    }
}

static bool Decode(const std::string& bytes, uint32_t* width, uint32_t* height,
                   std::vector<float>* pixels) {
    std::istringstream stream(bytes, std::ios::binary);
    return n3d::internal::DecodeHdr(stream, width, height, pixels);
}

static void TestRgbeDecode() {
    std::printf("rgbe -> float\n");
    float out[4];
    // Exponent 128 is the neutral scale, so 128,128,128 with e=128 is ~1.0.
    n3d::internal::RgbeToFloat(128, 128, 128, 128, out);
    CHECK(Near(out[0], (128.0f + 0.5f) / 256.0f));
    CHECK(Near(out[3], 1.0f));

    // A zero exponent means a zero pixel, whatever the mantissas hold.
    n3d::internal::RgbeToFloat(200, 100, 50, 0, out);
    CHECK(Near(out[0], 0.0f) && Near(out[1], 0.0f) && Near(out[2], 0.0f));

    // A large exponent must produce a value far above 1: this is the property an
    // 8-bit format cannot express, and the reason the whole feature exists.
    n3d::internal::RgbeToFloat(128, 128, 128, 150, out);
    CHECK(out[0] > 100.0f);
}

static void TestFlatImage() {
    std::printf("flat scanline\n");
    std::string bytes = Header(4, 2);
    AppendFlatRow(bytes, 128, 64, 32, 128, 4);   // row 0
    AppendFlatRow(bytes, 10, 20, 30, 129, 4);    // row 1

    uint32_t width = 0, height = 0;
    std::vector<float> pixels;
    CHECK(Decode(bytes, &width, &height, &pixels));
    CHECK(width == 4 && height == 2);
    CHECK(pixels.size() == 4u * 2u * 4u);

    // Row 0, pixel 0.
    CHECK(Near(pixels[0], (128.0f + 0.5f) / 256.0f));
    // Row 1 must not overwrite row 0: index 4 texels in is (1, 0).
    const size_t row1 = 4u * 4u;
    CHECK(Near(pixels[row1 + 0], (10.0f + 0.5f) * std::ldexp(1.0f, 129 - 136)));
    CHECK(Near(pixels[row1 + 3], 1.0f));   // alpha is always opaque
}

static void TestRleImage() {
    std::printf("run-length scanline\n");
    std::string bytes = Header(16, 1);
    AppendRleRow(bytes, 200, 100, 50, 128, 16);

    uint32_t width = 0, height = 0;
    std::vector<float> pixels;
    CHECK(Decode(bytes, &width, &height, &pixels));
    CHECK(width == 16 && height == 1);
    CHECK(pixels.size() == 16u * 4u);

    // Every texel in the row came from the same RGBE quadruple.
    const float expected = (200.0f + 0.5f) / 256.0f;
    CHECK(Near(pixels[0], expected));
    CHECK(Near(pixels[15 * 4 + 0], expected));

    // A run must decode to the same thing as the literal it replaces.
    std::string runBytes = Header(16, 1);
    runBytes.push_back(2);
    runBytes.push_back(2);
    runBytes.push_back(0);
    runBytes.push_back(16);
    for (uint8_t value : {static_cast<uint8_t>(200), static_cast<uint8_t>(100),
                          static_cast<uint8_t>(50), static_cast<uint8_t>(128)}) {
        runBytes.push_back(static_cast<char>(128 + 16));   // repeat the next byte 16 times
        runBytes.push_back(static_cast<char>(value));
    }
    uint32_t runWidth = 0, runHeight = 0;
    std::vector<float> runPixels;
    CHECK(Decode(runBytes, &runWidth, &runHeight, &runPixels));
    CHECK(runWidth == 16);
    CHECK(runPixels.size() == pixels.size());
    bool same = true;
    for (size_t i = 0; i < pixels.size(); ++i) {
        if (!Near(runPixels[i], pixels[i])) same = false;
    }
    CHECK(same);
}

static void TestRejectsMalformed() {
    std::printf("malformed input\n");
    uint32_t width = 0, height = 0;
    std::vector<float> pixels;

    // Wrong signature.
    CHECK(!Decode("not an hdr file at all\n", &width, &height, &pixels));
    // Right signature, no FORMAT line.
    CHECK(!Decode("#?RADIANCE\n\n-Y 2 +X 2\n", &width, &height, &pixels));
    // A non-RGBE format must be refused rather than misread.
    CHECK(!Decode("#?RADIANCE\nFORMAT=32-bit_rle_xyze\n\n-Y 2 +X 2\n", &width, &height, &pixels));
    // Zero size.
    CHECK(!Decode("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 0 +X 4\n", &width, &height, &pixels));
    // A flipped axis is not supported, and must fail loudly rather than quietly
    // producing an upside-down image.
    CHECK(!Decode("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n+Y 2 +X 2\n", &width, &height, &pixels));
    // Header promises two rows but the stream ends after one.
    std::string truncated = Header(4, 2);
    AppendFlatRow(truncated, 1, 2, 3, 128, 4);
    CHECK(!Decode(truncated, &width, &height, &pixels));
}

int main() {
    TestRgbeDecode();
    TestFlatImage();
    TestRleImage();
    TestRejectsMalformed();

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
