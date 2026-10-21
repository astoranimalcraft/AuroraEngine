// NexiumEngine — internal shared definitions.
// Only included by .cpp files under src/. Never exposed to applications.
#pragma once

// The engine uses the wide-character Win32 API exclusively, so force UNICODE
// before windows.h is pulled in by any translation unit.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wrl/client.h>

// windows.h defines DrawText and PlaySound as A/W aliases (DrawTextW,
// PlaySoundW). The engine has functions with those exact names, and the macro
// would rewrite the engine's own declarations and definitions into something
// that does not exist. Undo them here, once, for every translation unit.
#undef DrawText
#undef DrawTextEx
#undef PlaySound

// The same trap for the platform backend: CreateWindow/CreateWindowEx and
// DestroyWindow are macros expanding to their A/W forms, and PlatformBackend
// declares methods with those names.
#undef CreateWindow
#undef CreateWindowEx
#undef DestroyWindow

#include <cstdint>
#include <cstring>

#include "nexium/color.h"
#include "nexium/math.h"
#include "nexium/mesh.h"

namespace n3d::internal {

using Microsoft::WRL::ComPtr;

// ------------------------------------------------------------- constants -----
// Frame and object constant blocks are bound as 32-bit root constants, so their
// sizes must match the HLSL cbuffer declarations in shaders/nexium.hlsli
// exactly (both are 28 DWORDs = 112 bytes).
inline constexpr uint32_t kFrameConstantCount = 28;
inline constexpr uint32_t kObjectConstantCount = 28;

// Number of frames the CPU may run ahead of the GPU.
inline constexpr uint32_t kMaxFramesInFlight = 2;

// Swap chain buffers are kept at 2 to stay in the flip-model fast path.
inline constexpr uint32_t kSwapChainBufferCount = 2;

// Per-frame CPU-side dynamic vertex/index capacity. Buffers grow if exceeded.
inline constexpr uint32_t kInitialVertexCapacity = 1u << 16;
inline constexpr uint32_t kInitialIndexCapacity = 1u << 17;

// Descriptor heap sizing. One shader-visible heap holds an SRV per texture; the
// slots are stable for the lifetime of the texture that owns them.
inline constexpr uint32_t kSrvHeapCapacity = 1024;
inline constexpr uint32_t kRtvHeapCapacity = 8;
// Slot 0 is the swap chain depth buffer; slot 1 is the shadow map.
inline constexpr uint32_t kDsvHeapCapacity = 4;

// ------------------------------------------------------ GPU-side structs -----
// Layout must match shaders/nexium.hlsli.
struct FrameConstants {
    Mat4 viewProj;
    float cameraPosTime[4];
    float lightDir[4];
    float ambient[4];
};
static_assert(sizeof(FrameConstants) == kFrameConstantCount * sizeof(uint32_t));

struct ObjectConstants {
    Mat4 world;
    float baseColor[4];
    float uvScaleOffset[4];
    float params[3];
    uint32_t flags;
};
static_assert(sizeof(ObjectConstants) == kObjectConstantCount * sizeof(uint32_t));

// A vertex as uploaded to the GPU. The engine uses the public n3d::Vertex (the
// same type applications pass to CreateMesh) so the two layouts can never drift
// apart. Mirrors N3D_VertexInput in shaders/nexium.hlsli.
using Vertex = n3d::Vertex;
static_assert(sizeof(Vertex) == 48);

// Object flags shared with HLSL (N3D_FLAG_*).
enum ObjectFlags : uint32_t {
    kFlagUnlit    = 1u << 0,
    kFlagTextured = 1u << 1,
    kFlagTwoSided = 1u << 2,
    kFlagNoShadow = 1u << 3,   // excluded from the shadow pass
};

// --------------------------------------------------------------- shadows -----
// Bound as a root CBV at b2 rather than root constants: the light matrix alone is
// 16 DWORDs, and the root signature only has 64 DWORDs to spend across every
// parameter. A root CBV costs 2 DWORDs no matter how large the block is, so this
// keeps the signature inside the limit while leaving room to grow.
struct ShadowConstants {
    Mat4 lightViewProj;
    float lightParams[4];   // x = enabled, y = depth bias, z = normal bias, w = texel size
    float shadowParams[4];  // x = 1/width, y = 1/height, z = pcf radius, w = unused
};
static_assert(sizeof(ShadowConstants) == 24 * sizeof(uint32_t));

inline constexpr uint32_t kShadowConstantBytes = 256;   // CBV size must be 256-aligned

inline uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace n3d::internal
