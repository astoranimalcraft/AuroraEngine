// Aurora3D — shared HLSL binding layout.
//
// Aurora3D uses a FIXED root signature so any user-authored HLSL vertex / pixel
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
// exactly as stored in C++ (see include/aurora/math.h), so neither side needs a
// transpose.

#ifndef AURORA_HLSLI
#define AURORA_HLSLI

#define A3D_FLAG_UNLIT    1u   // shade with baseColor * texture only
#define A3D_FLAG_TEXTURED 2u   // sample t0
#define A3D_FLAG_TWOSIDED 4u   // flip the normal on back faces

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
    uint     flags;         // A3D_FLAG_* bitset
};

Texture2D    albedoTexture : register(t0);
SamplerState albedoSampler : register(s0);
// Point/clamp sampler for bitmap fonts: nearest filtering keeps glyph pixels
// sharp, and clamp stops the sampling from bleeding between atlas cells.
SamplerState glyphSampler : register(s1);

// ---------------------------------------------------------------- helpers ----
float3 A3D_LightDirection() { return normalize(-lightDir.xyz); } // toward the light
float3 A3D_CameraPosition() { return cameraPosTime.xyz; }
float  A3D_Time()           { return cameraPosTime.w; }

// Lambert diffuse + Blinn-Phong specular. `N` must be normalized, `worldPos` is
// the interpolated world position of the fragment.
float3 A3D_Shade(float3 N, float3 worldPos, float3 albedo)
{
    float3 L = A3D_LightDirection();
    float3 V = normalize(A3D_CameraPosition() - worldPos);
    float3 H = normalize(L + V);

    float ndl  = saturate(dot(N, L));
    float shin = exp2(lerp(1.0f, 9.0f, 1.0f - saturate(params.y))); // roughness -> exponent
    float spec = pow(saturate(dot(N, H)), shin) * saturate(params.x);
    float amb  = ambient.a;

    return albedo * (ambient.rgb * amb + ndl) + spec * ndl;
}

// ------------------------------------------------------------ VS / PS I/O ---
struct A3D_VertexInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct A3D_VertexOutput
{
    float4 clipPos  : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 color    : COLOR0;
};

A3D_VertexOutput A3D_TransformVertex(A3D_VertexInput input)
{
    A3D_VertexOutput o;
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
float4 A3D_ShadePixel(A3D_VertexOutput input, bool isFrontFace)
{
    float4 albedo = input.color;

    if (flags & A3D_FLAG_TEXTURED)
        albedo *= albedoTexture.Sample(albedoSampler, input.uv);

    if (flags & A3D_FLAG_UNLIT)
        return albedo;

    float3 N = normalize(input.normal);
    if ((flags & A3D_FLAG_TWOSIDED) && !isFrontFace)
        N = -N;

    return float4(A3D_Shade(N, input.worldPos, albedo.rgb), albedo.a);
}

// Unlit sampling for bitmap text: the vertex colour tints the glyph and the
// point sampler keeps the atlas pixels crisp.
float4 A3D_SampleGlyph(A3D_VertexOutput input)
{
    return input.color * albedoTexture.Sample(glyphSampler, input.uv);
}

#endif // AURORA_HLSLI
