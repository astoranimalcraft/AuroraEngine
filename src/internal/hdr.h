// NexiumEngine — Radiance (.hdr / RGBE) decoding (internal).
//
// Kept separate from texture.cpp, and free of any D3D12 dependency, so the decoder
// can be compiled and executed by the native test suite. The GPU upload path and
// the file parsing are the two halves of HDR loading, and only the second one is
// worth testing on a machine with no GPU.
#pragma once

#include <cmath>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace n3d {
namespace internal {

// Reads the ASCII header up to and including the blank line that ends it. Returns
// false when the signature or the resolution cannot be understood.
inline bool ReadHdrHeader(std::istream& in, uint32_t* outWidth, uint32_t* outHeight) {
    std::string line;
    if (!std::getline(in, line)) return false;
    // The signature may be followed by a CR, so accept both endings.
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line != "#?RADIANCE" && line != "#?RGBE") return false;

    bool sawFormat = false;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) break;   // blank line ends the header
        if (line.rfind("FORMAT=", 0) == 0) {
            if (line != "FORMAT=32-bit_rle_rgbe") return false;
            sawFormat = true;
        }
        // EXPOSURE, GAMMA, PRIMARIES and comments are all ignorable for our use.
    }
    if (!sawFormat) return false;

    // The resolution line is "-Y height +X width" for the standard orientation.
    if (!std::getline(in, line)) return false;
    int height = 0, width = 0;
    char ySign = 0, xSign = 0;
    if (std::sscanf(line.c_str(), " %cY %d %cX %d", &ySign, &height, &xSign, &width) != 4) {
        return false;
    }
    if (width <= 0 || height <= 0) return false;
    // A flipped axis would need the rows reversed; rather than silently produce an
    // upside-down sky, refuse it and let the caller notice.
    if (ySign != '-' || xSign != '+') return false;

    *outWidth = static_cast<uint32_t>(width);
    *outHeight = static_cast<uint32_t>(height);
    return true;
}

// Undoes the RGBE encoding: one shared exponent byte scales three mantissas, which
// is what gives the format its range for only 4 bytes per pixel.
inline void RgbeToFloat(uint8_t r, uint8_t g, uint8_t b, uint8_t e, float* out) {
    if (e == 0) {
        out[0] = out[1] = out[2] = 0.0f;
    } else {
        // The +0.5 lands the value in the middle of the stored bucket, which keeps
        // the decode from biasing every pixel slightly dark.
        const float scale = std::ldexp(1.0f, static_cast<int>(e) - (128 + 8));
        out[0] = (static_cast<float>(r) + 0.5f) * scale;
        out[1] = (static_cast<float>(g) + 0.5f) * scale;
        out[2] = (static_cast<float>(b) + 0.5f) * scale;
    }
    out[3] = 1.0f;
}

// Reads one scanline, which may be run-length encoded or stored flat. Old files
// write flat rows, and a row narrower than 8 or wider than 32767 is always flat.
inline bool ReadHdrScanline(std::istream& in, uint32_t width, uint8_t* rgbe) {
    uint8_t header[4];
    if (!in.read(reinterpret_cast<char*>(header), 4)) return false;

    const bool isNewRle = header[0] == 2 && header[1] == 2 &&
                          (header[2] & 0x80) == 0 &&
                          ((static_cast<uint32_t>(header[2]) << 8) | header[3]) == width;
    if (!isNewRle) {
        // Flat row: the four bytes already read are the first pixel.
        rgbe[0] = header[0];
        rgbe[1] = header[1];
        rgbe[2] = header[2];
        rgbe[3] = header[3];
        if (width == 1) return true;
        in.read(reinterpret_cast<char*>(rgbe + 4), static_cast<std::streamsize>(width - 1) * 4);
        return static_cast<bool>(in);
    }

    // RLE row: each of the four channels is encoded on its own, so the plane has to
    // be decoded into a temporary and then interleaved back into RGBA order.
    std::vector<uint8_t> planes[4];
    for (int channel = 0; channel < 4; ++channel) {
        std::vector<uint8_t>& plane = planes[channel];
        plane.resize(width);
        for (uint32_t x = 0; x < width;) {
            uint8_t count = 0;
            if (!in.read(reinterpret_cast<char*>(&count), 1)) return false;
            if (count > 128) {
                // A run: the next byte repeats (count - 128) times.
                uint8_t value = 0;
                if (!in.read(reinterpret_cast<char*>(&value), 1)) return false;
                const uint32_t run = static_cast<uint32_t>(count - 128);
                if (x + run > width) return false;
                for (uint32_t i = 0; i < run; ++i) plane[x + i] = value;
                x += run;
            } else {
                // A literal block of `count` bytes.
                if (count == 0 || x + count > width) return false;
                if (!in.read(reinterpret_cast<char*>(plane.data() + x), count)) return false;
                x += count;
            }
        }
    }

    for (uint32_t x = 0; x < width; ++x) {
        for (int channel = 0; channel < 4; ++channel) {
            rgbe[x * 4 + channel] = planes[channel][x];
        }
    }
    return true;
}

// Decodes a whole stream into linear float RGBA. On failure `out` is left in an
// unspecified state and false is returned.
inline bool DecodeHdr(std::istream& in, uint32_t* outWidth, uint32_t* outHeight,
                      std::vector<float>* out) {
    uint32_t width = 0, height = 0;
    if (!ReadHdrHeader(in, &width, &height)) return false;

    // Decode to float: the whole point of HDR is that the values are not clamped,
    // and keeping them in float until the final half conversion preserves the sun's
    // intensity through the mip chain.
    std::vector<float> pixels(static_cast<size_t>(width) * height * 4);
    std::vector<uint8_t> row(static_cast<size_t>(width) * 4);

    for (uint32_t y = 0; y < height; ++y) {
        if (!ReadHdrScanline(in, width, row.data())) return false;
        for (uint32_t x = 0; x < width; ++x) {
            const size_t src = static_cast<size_t>(x) * 4;
            const size_t dst = (static_cast<size_t>(y) * width + x) * 4;
            RgbeToFloat(row[src + 0], row[src + 1], row[src + 2], row[src + 3],
                        pixels.data() + dst);
        }
    }

    *outWidth = width;
    *outHeight = height;
    *out = std::move(pixels);
    return true;
}

} // namespace internal
} // namespace n3d
