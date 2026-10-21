// NexiumEngine — texture.h
// Textures are opaque handles owned by the engine. Textures created from memory
// or from file are freed with n3d::UnloadTexture(); the built-in white texture
// returned by n3d::GetDefaultTexture() must not be freed.
#pragma once

#include <cstdint>

#include "nexium/color.h"

namespace n3d {

// Pixel layout of a texture's source data. The GPU format follows from this:
// RGBA8 becomes R8G8B8A8_UNORM, RGBA16F becomes R16G16B16A16_FLOAT.
//
// RGBA16F is what makes a real HDRI possible. An 8-bit texture cannot hold the
// range of a radiance map: a sun is thousands of times brighter than the sky
// around it, and clamping that to 0..1 throws away exactly the information the
// lighting and reflections need.
enum class TextureFormat : uint32_t {
    RGBA8,     // 4 bytes/pixel, sRGB-encodable
    RGBA16F,   // 8 bytes/pixel, half floats, linear
};

struct TextureDesc {
    uint32_t width  = 0;
    uint32_t height = 0;
    // Tightly packed, row-major, top-left origin. The component type depends on
    // `format`: 4 bytes/pixel for RGBA8, 8 bytes/pixel (half) for RGBA16F.
    const uint8_t* pixels = nullptr;
    bool generateMips = true;
    bool srgb = false;
    TextureFormat format = TextureFormat::RGBA8;
    const char* debugName = nullptr;
};

// Opaque handle. `nullptr` is the canonical "no texture" value; passing it to a
// draw call is legal and falls back to the default white texture.
struct Texture;

// Uploads pixels to the GPU. Returns nullptr on failure (logged).
Texture* LoadTexture(const TextureDesc& desc);
Texture* LoadTextureFromMemory(const uint8_t* pixels, uint32_t width, uint32_t height,
                               bool generateMips = true);
Texture* LoadTextureFromFile(const char* path, bool generateMips = true);

// Same as LoadTextureFromMemory, but from linear float RGBA. Convenient for a
// procedurally generated environment map, which is naturally float.
Texture* LoadTextureFromFloats(const float* rgba, uint32_t width, uint32_t height,
                               bool generateMips = true);

// Loads a Radiance (.hdr / RGBE) file into an RGBA16F texture. This is the
// standard high-dynamic-range format, and the one most environment maps ship as.
// Unlike LoadTextureFromFile the values are not clamped, so a bright sun stays
// bright and can drive lighting and reflections.
Texture* LoadHDRFromFile(const char* path, bool generateMips = true);

void UnloadTexture(Texture* texture);

uint32_t TextureWidth(const Texture* texture);
uint32_t TextureHeight(const Texture* texture);
const char* TextureName(const Texture* texture);
TextureFormat TexturePixelFormat(const Texture* texture);

// 1x1 opaque white texture used whenever a draw has no texture bound.
Texture* GetDefaultTexture();

} // namespace n3d
