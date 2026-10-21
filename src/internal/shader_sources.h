// NexiumEngine — generated file. DO NOT EDIT.
//
// Regenerate with:  python3 tools/embed_shaders.py
//
// The engine compiles these copies of shaders/*.hlsl at startup so that an
// NexiumEngine program runs with no shader files next to the executable. Files on
// disk take precedence, so dropping a modified copy beside the binary (or
// pointing LoadShader() at your own) overrides the built-in one.
#pragma once

namespace n3d::internal {

struct EmbeddedShader {
    const char* name;
    const char* source;
};

inline constexpr const char* kShaderSource_nexium_hlsli = R"N3D(// NexiumEngine — shared HLSL binding layout.
//
// NexiumEngine uses a FIXED root signature so any user-authored HLSL vertex / pixel
// shader can be plugged in without touching the engine. Include this file to get
// the exact constant blocks and resources the engine binds.
//
//   Root signature layout
//   ---------------------
//   b0 : FrameConstants    28 x 32-bit root constants, set once per camera
//   b1 : ObjectConstants   28 x 32-bit root constants, set once per draw
//   t0 : albedo texture    (descriptor table, 1 SRV, pixel visibility)
//   s0 : static sampler    (linear / wrap / mip-linear)
//   s1 : static sampler    (point / clamp), for crisp bitmap-font glyphs
//
//   Register space 0 only. Constants are pushed as root constants rather than
//   CBVs, which is the cheapest binding path in D3D12 and avoids per-frame
//   upload buffers entirely.
//
// Matrix convention: column-vector, `mul(M, v)`. Matrices arrive column-major,
// exactly as stored in C++ (see include/nexium/math.h), so neither side needs a
// transpose.

#ifndef NEXIUM_HLSLI
#define NEXIUM_HLSLI

#define N3D_FLAG_UNLIT    1u   // shade with baseColor * texture only
#define N3D_FLAG_TEXTURED 2u   // sample t0
#define N3D_FLAG_TWOSIDED 4u   // flip the normal on back faces
#define N3D_FLAG_NOSHADOW 8u   // excluded from the shadow pass

cbuffer FrameConstants : register(b0)
{
    float4x4 viewProj;      // world -> clip
    float4   cameraPosTime; // xyz = camera position, w = seconds since init
    float4   lightDir;      // xyz = direction light travels (normalized), w unused
    float4   ambient;       // rgb = ambient color, a = ambient intensity
};

cbuffer ObjectConstants : register(b1)
{
    float4x4 world;         // local -> world
    float4   baseColor;     // tint; alpha = opacity
    float4   uvScaleOffset; // xy = uv scale, zw = uv offset
    float3   params;        // x = metallic, y = roughness, z = unused
    uint     flags;         // N3D_FLAG_* bitset
};

Texture2D    albedoTexture : register(t0);
SamplerState albedoSampler : register(s0);
// Point/clamp sampler for bitmap fonts: nearest filtering keeps glyph pixels
// sharp, and clamp stops the sampling from bleeding between atlas cells.
SamplerState glyphSampler : register(s1);

// ---------------------------------------------------------------- shadows ----
// Bound as a root CBV at b2. The engine fills it once per frame; a shader only
// reads it, and a program that never enables shadows can ignore it entirely.
cbuffer ShadowConstants : register(b2)
{
    float4x4 lightViewProj;  // world -> light clip
    // x = 1 when shadows are on, y = depth bias, z = normal bias, w = texel size
    float4   lightParams;
    // x,y = 1 / map size, z = PCF radius in texels, w = unused
    float4   shadowParams;
};

Texture2D        shadowMap       : register(t1);
// Comparison sampler: SampleCmp compares the reference depth against the map and
// filters the 0/1 results, which gives 2x2 PCF in one instruction.
SamplerComparisonState shadowSampler : register(s2);

// Returns 1.0 when the fragment is lit and 0.0 when it is in shadow.
// `N` is the (normalized) surface normal, used for the normal-offset bias that
// keeps a surface from shadowing itself.
float N3D_ShadowFactor(float3 worldPos, float3 N)
{
    if (lightParams.x < 0.5f)
        return 1.0f;   // shadows off

    // Offset along the normal before projecting: this is what removes the acne on
    // surfaces that are nearly parallel to the light.
    float3 biasedPos = worldPos + N * lightParams.z;

    float4 lightClip = mul(lightViewProj, float4(biasedPos, 1.0f));
    if (lightClip.w <= 0.0f)
        return 1.0f;   // behind the light

    // Clip space (-1..1, with Y up) to texture space (0..1, with Y down).
    float3 ndc = lightClip.xyz / lightClip.w;
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);

    // Outside the map means the fragment is not covered by it, so treat it as lit.
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
        return 1.0f;

    float reference = ndc.z - lightParams.y;

    // A small 4-tap box around the sample point. The comparison sampler already
    // filters within a tap, so this widens the penumbra without a real PCF loop.
    float radius = shadowParams.z;
    float2 texel = float2(shadowParams.x, shadowParams.y) * radius;
    float sum = 0.0f;
    sum += shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2(-texel.x, -texel.y), reference);
    sum += shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2( texel.x, -texel.y), reference);
    sum += shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2(-texel.x,  texel.y), reference);
    sum += shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2( texel.x,  texel.y), reference);
    return sum * 0.25f;
}

// ---------------------------------------------------------------- helpers ----
float3 N3D_LightDirection() { return normalize(-lightDir.xyz); } // toward the light
float3 N3D_CameraPosition() { return cameraPosTime.xyz; }
float  N3D_Time()           { return cameraPosTime.w; }

// Lambert diffuse + Blinn-Phong specular. `N` must be normalized, `worldPos` is
// the interpolated world position of the fragment.
float3 N3D_Shade(float3 N, float3 worldPos, float3 albedo)
{
    float3 L = N3D_LightDirection();
    float3 V = normalize(N3D_CameraPosition() - worldPos);
    float3 H = normalize(L + V);

    float ndl  = saturate(dot(N, L));
    float shin = exp2(lerp(1.0f, 9.0f, 1.0f - saturate(params.y))); // roughness -> exponent
    float spec = pow(saturate(dot(N, H)), shin) * saturate(params.x);
    float amb  = ambient.a;

    // The shadow only attenuates the direct term; ambient light still reaches a
    // surface in shadow, which is what keeps shadows from going pure black.
    float shadow = N3D_ShadowFactor(worldPos, N);

    return albedo * (ambient.rgb * amb + ndl * shadow) + spec * ndl * shadow;
}

// ------------------------------------------------------------ VS / PS I/O ---
struct N3D_VertexInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct N3D_VertexOutput
{
    float4 clipPos  : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 color    : COLOR0;
};

N3D_VertexOutput N3D_TransformVertex(N3D_VertexInput input)
{
    N3D_VertexOutput o;
    float4 worldPos = mul(world, float4(input.position, 1.0f));

    o.worldPos = worldPos.xyz;
    o.clipPos  = mul(viewProj, worldPos);
    // The 3x3 of `world` is a valid normal matrix for uniformly scaled objects,
    // which is what the built-in meshes and Compose() produce.
    o.normal   = mul((float3x3)world, input.normal);
    o.uv       = input.uv * uvScaleOffset.xy + uvScaleOffset.zw;
    o.color    = input.color * baseColor;
    return o;
}

// Shared pixel stage. `isFrontFace` comes from SV_IsFrontFace in the PS entry.
float4 N3D_ShadePixel(N3D_VertexOutput input, bool isFrontFace)
{
    float4 albedo = input.color;

    if (flags & N3D_FLAG_TEXTURED)
        albedo *= albedoTexture.Sample(albedoSampler, input.uv);

    if (flags & N3D_FLAG_UNLIT)
        return albedo;

    float3 N = normalize(input.normal);
    if ((flags & N3D_FLAG_TWOSIDED) && !isFrontFace)
        N = -N;

    return float4(N3D_Shade(N, input.worldPos, albedo.rgb), albedo.a);
}

// Unlit sampling for bitmap text: the vertex colour tints the glyph and the
// point sampler keeps the atlas pixels crisp.
float4 N3D_SampleGlyph(N3D_VertexOutput input)
{
    return input.color * albedoTexture.Sample(glyphSampler, input.uv);
}

#endif // NEXIUM_HLSLI
)N3D";

inline constexpr const char* kShaderSource_nexium_basic_hlsl = R"N3D(// NexiumEngine — default shader pair.
// Compiled at runtime by the engine (D3DCompile) and used by every draw call
// unless the application installs its own HLSL via n3d::SetShader().
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    return N3D_ShadePixel(input, isFrontFace);
}
)N3D";

inline constexpr const char* kShaderSource_nexium_line_hlsl = R"N3D(// NexiumEngine — wireframe / debug-line shader pair.
// Uses the exact same binding layout as nexium_basic.hlsl, so the engine can swap
// between them without rebuilding the root signature.
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    // Vertex colors carry the line color; no lighting, no texture.
    return input.color;
}
)N3D";

inline constexpr const char* kShaderSource_nexium_text_hlsl = R"N3D(// NexiumEngine — bitmap-font text shader.
// Like nexium_basic but samples the glyph atlas with the point/clamp sampler so
// the built-in font stays sharp at any scale.
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input) : SV_TARGET
{
    return N3D_SampleGlyph(input);
}
)N3D";

inline constexpr const char* kShaderSource_nexium_sky_hlsl = R"N3D(// NexiumEngine — built-in environment skybox shader.
//
// Draws an equirectangular environment map as a cube centred on the camera. The
// vertex stage is the stock transform; the pixel stage turns the direction to the
// fragment back into the map's longitude/latitude UV. Nothing is lit here — the
// sky is the light.
#include "nexium.hlsli"

static const float N3D_PI  = 3.14159265358979323846f;
static const float N3D_TAU = 6.28318530717958647692f;

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input) : SV_TARGET
{
    // The cube is centred on the eye, so the direction is just the normalised
    // offset from the camera.
    float3 dir = normalize(input.worldPos - N3D_CameraPosition());

    // Longitude from the azimuth, latitude from the elevation. This is the inverse
    // of the mapping an equirectangular image is generated with.
    float u = atan2(dir.x, dir.z) / N3D_TAU + 0.5f;
    float v = acos(clamp(dir.y, -1.0f, 1.0f)) / N3D_PI;

    // s0 wraps in U, so the longitude seam between u = 0 and u = 1 is invisible.
    // The tint scales the whole sky, which is how an application dims it without
    // touching the image.
    float3 sky = albedoTexture.Sample(albedoSampler, float2(u, v)).rgb;
    return float4(sky * baseColor.rgb, 1.0f);
}
)N3D";

inline constexpr EmbeddedShader kEmbeddedShaders[] = {
    {"nexium.hlsli", kShaderSource_nexium_hlsli},
    {"nexium_basic.hlsl", kShaderSource_nexium_basic_hlsl},
    {"nexium_line.hlsl", kShaderSource_nexium_line_hlsl},
    {"nexium_text.hlsl", kShaderSource_nexium_text_hlsl},
    {"nexium_sky.hlsl", kShaderSource_nexium_sky_hlsl},
};

inline constexpr int kEmbeddedShaderCount =
    static_cast<int>(sizeof(kEmbeddedShaders) / sizeof(kEmbeddedShaders[0]));

// Looks up an embedded shader by file name (no directory). Returns nullptr when
// the name is not one of the engine's own shaders.
inline const char* FindEmbeddedShader(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < kEmbeddedShaderCount; ++i) {
        const char* a = kEmbeddedShaders[i].name;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return kEmbeddedShaders[i].source;
    }
    return nullptr;
}

} // namespace n3d::internal
