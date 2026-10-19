// Aurora3D — texture.h
// Textures are opaque handles owned by the engine. Textures created from memory
// or from file are freed with a3d::UnloadTexture(); the built-in white texture
// returned by a3d::GetDefaultTexture() must not be freed.
#pragma once

#include <cstdint>

#include "aurora/color.h"

namespace a3d {

struct TextureDesc {
    uint32_t width  = 0;
    uint32_t height = 0;
    // Tightly packed RGBA8, row-major, top-left origin, `width * 4` bytes/row.
    const uint8_t* pixels = nullptr;
    bool generateMips = true;
    bool srgb = false;
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

void UnloadTexture(Texture* texture);

uint32_t TextureWidth(const Texture* texture);
uint32_t TextureHeight(const Texture* texture);
const char* TextureName(const Texture* texture);

// 1x1 opaque white texture used whenever a draw has no texture bound.
Texture* GetDefaultTexture();

} // namespace a3d
