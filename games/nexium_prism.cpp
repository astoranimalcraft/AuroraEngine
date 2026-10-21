// NexiumEngine — game: Nexium Prism.
//
// A 3D shader gallery. It exists to show the HLSL half of the engine: several
// custom shaders written as source strings, each compiled at runtime, switched
// from the keyboard, and applied to engine primitives. There is no build step for
// the shaders and no .cso to ship.
//
// It also uses a Storage drawer as the single place its asset paths live, so the
// scene and the shader sources are named once and referred to by name afterwards.
//
// Controls
//   1 2 3 4 5   pick a shader
//   mouse drag  orbit
//   wheel       zoom
//   esc         quit
//
// Build: see ./scripts/verify.sh, or compile games/nexium_prism.cpp against the
// library the same way the other games are.
#include <nexium/NexiumEngine.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace n3d;

namespace {

// --------------------------------------------------------------- shaders ---
// Each shader includes "nexium.hlsli" and provides VSMain / PSMain, which is all
// the engine's fixed root signature requires. N3D_TransformVertex() is the shared
// vertex path, so a shader only has to think about the pixel stage.

// A plasma: two scrolling sine fields combined, then run through a warm/cool ramp.
const char* kPlasma = R"HLSL(
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float2 p = input.worldPos.xz * 0.6f;
    float t = N3D_Time() * 0.8f;

    float v = sin(p.x + t) + sin(p.y * 1.3f - t * 0.7f);
    v += sin((p.x + p.y) * 0.7f + t * 1.2f);
    v += sin(length(p) * 2.2f - t * 1.6f);
    v *= 0.25f;

    float3 cool = float3(0.05f, 0.25f, 0.75f);
    float3 warm = float3(1.00f, 0.35f, 0.15f);
    float3 albedo = lerp(cool, warm, saturate(v * 0.5f + 0.5f));

    return float4(albedo, input.color.a);
}
)HLSL";

// A fresnel rim: dark in the middle, bright at the silhouette, with a pulsing
// inner glow. Reads as glass or a shield.
const char* kFresnel = R"HLSL(
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float3 viewDir = normalize(N3D_CameraPosition() - input.worldPos);
    float rim = 1.0f - saturate(dot(input.normal, viewDir));
    rim = pow(rim, 2.5f);

    float pulse = 0.6f + 0.4f * sin(N3D_Time() * 3.0f);
    float3 core = float3(0.10f, 0.45f, 0.90f);
    float3 edge = float3(0.60f, 0.95f, 1.00f);
    float3 albedo = lerp(core, edge, rim) * pulse;

    return float4(albedo, saturate(0.35f + rim));
}
)HLSL";

// A hologram: horizontal scanlines and a vertical sweep, additive-friendly.
const char* kHologram = R"HLSL(
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float scan = 0.5f + 0.5f * sin(input.worldPos.y * 90.0f - N3D_Time() * 8.0f);
    float sweep = saturate(1.0f - abs(frac(input.worldPos.y * 0.35f - N3D_Time() * 0.4f) - 0.5f) * 6.0f);

    float3 albedo = float3(0.15f, 0.95f, 0.75f);
    float alpha = 0.25f + 0.35f * scan + 0.6f * sweep;

    return float4(albedo * (0.5f + scan * 0.5f + sweep), saturate(alpha) * input.color.a);
}
)HLSL";

// A grid: world-space lines on the XZ plane, fading with distance. Useful to see
// the geometry move.
const char* kGridGlow = R"HLSL(
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float gridLine(float2 p, float spacing, float thickness)
{
    float2 cell = abs(frac(p / spacing) - 0.5f) * spacing;
    float d = min(cell.x, cell.y);
    return 1.0f - smoothstep(0.0f, thickness, d);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float lines = gridLine(input.worldPos.xz, 1.0f, 0.03f)
                + gridLine(input.worldPos.xz, 0.25f, 0.01f) * 0.4f;

    float3 albedo = float3(0.20f, 0.60f, 1.00f) * lines;
    float alpha = saturate(lines) * input.color.a;

    return float4(albedo, alpha);
}
)HLSL";

// A normal-visualisation: maps the world-space normal into colour. The classic
// "is the geometry right" shader.
const char* kNormals = R"HLSL(
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float3 n = input.normal * 0.5f + 0.5f;
    return float4(n, input.color.a);
}
)HLSL";

struct ShaderChoice {
    const char* name;
    const char* source;
};

const ShaderChoice kShaders[] = {
    {"plasma",   kPlasma},
    {"fresnel",  kFresnel},
    {"hologram", kHologram},
    {"grid",     kGridGlow},
    {"normals",  kNormals},
};
constexpr int kShaderCount = static_cast<int>(sizeof(kShaders) / sizeof(kShaders[0]));

// ------------------------------------------------------------- the drawer ---
// The shader sources live in files so they can be edited and reloaded live; the
// drawer is what names them. Placed in namespace n3d so n3d::Assets::plasma works.
#define NEXIUM_PRISM_FILES(X)                                     \
    X(plasma,   "nexium_prism_plasma.hlsl")                       \
    X(fresnel,  "nexium_prism_fresnel.hlsl")                      \
    X(hologram, "nexium_prism_hologram.hlsl")                     \
    X(grid,     "nexium_prism_grid.hlsl")                         \
    X(normals,  "nexium_prism_normals.hlsl")                      \
    X(music,    "nexium_prism_music.wav")

constexpr int kSampleRate = 44100;

void WriteFileText(const char* path, const char* text) {
    std::ofstream out(path, std::ios::binary);
    if (out) out << text;
}

void WriteWav16(const char* path, const std::vector<float>& samples) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;

    const uint32_t dataSize = static_cast<uint32_t>(samples.size() * 2u);
    const auto u32 = [&out](uint32_t value) {
        const char bytes[4] = {static_cast<char>(value & 0xFFu),
                               static_cast<char>((value >> 8) & 0xFFu),
                               static_cast<char>((value >> 16) & 0xFFu),
                               static_cast<char>((value >> 24) & 0xFFu)};
        out.write(bytes, 4);
    };
    const auto u16 = [&out](uint16_t value) {
        const char bytes[2] = {static_cast<char>(value & 0xFFu),
                               static_cast<char>((value >> 8) & 0xFFu)};
        out.write(bytes, 2);
    };

    out.write("RIFF", 4);
    u32(36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16u);
    u16(1u);
    u16(1u);
    u32(kSampleRate);
    u32(kSampleRate * 2u);
    u16(2u);
    u16(16u);
    out.write("data", 4);
    u32(dataSize);
    for (const float sample : samples) {
        const int value = static_cast<int>(Clamp(sample, -1.0f, 1.0f) * 32767.0f);
        u16(static_cast<uint16_t>(value & 0xFFFF));
    }
}

// A slow drone with a wandering filter-ish shimmer, long enough to loop.
std::vector<float> MakeDrone(float seconds) {
    const int count = static_cast<int>(kSampleRate * seconds);
    std::vector<float> samples(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        float value = std::sin(2.0f * PI * 55.0f * t) * 0.16f;
        value += std::sin(2.0f * PI * 82.5f * t + std::sin(t * 0.3f)) * 0.10f;
        value += std::sin(2.0f * PI * 220.0f * t + std::sin(t * 0.7f) * 2.0f) * 0.05f;
        const float swell = 0.7f + 0.3f * std::sin(t * 0.5f);
        samples[static_cast<size_t>(i)] = value * swell;
    }
    return samples;
}

} // namespace

namespace n3d {
N3D_DEFINE_STORAGE(Assets, NEXIUM_PRISM_FILES)
} // namespace n3d

namespace {

struct Scene {
    OrbitCamera orbit;
    Shader* shaders[kShaderCount] = {};
    int active = 0;
    float rotation = 0.0f;
};

} // namespace

int main() {
    Config config;
    config.title = "NexiumEngine — Prism (HLSL shaders)";
    config.width = 1280;
    config.height = 720;
    if (!Init(config)) return 1;

    // Write the shader sources and the music, then load everything through the
    // drawer. watchForChanges means editing one of these .hlsl files reloads the
    // shader while the game runs.
    WriteFileText(Assets::plasma.Path(), kPlasma);
    WriteFileText(Assets::fresnel.Path(), kFresnel);
    WriteFileText(Assets::hologram.Path(), kHologram);
    WriteFileText(Assets::grid.Path(), kGridGlow);
    WriteFileText(Assets::normals.Path(), kNormals);
    WriteWav16(Assets::music.Path(), MakeDrone(20.0f));

    Scene scene;
    scene.orbit.distance = 11.0f;
    scene.orbit.yaw = Radians(35.0f);
    scene.orbit.pitch = Radians(22.0f);

    Shader* loaded[kShaderCount] = {
        Assets::plasma.AsShader(),   Assets::fresnel.AsShader(),
        Assets::hologram.AsShader(), Assets::grid.AsShader(),
        Assets::normals.AsShader(),
    };
    for (int i = 0; i < kShaderCount; ++i) {
        scene.shaders[i] = loaded[i];
        if (!scene.shaders[i]) LogError("prism: shader '%s' failed to compile", kShaders[i].name);
    }

    Music* music = Assets::music.AsMusic();
    if (music) {
        SetMusicLooping(music, true);
        SetMusicVolume(music, 0.6f);
        PlayMusic(music);
    }

    const Mesh* sphere = GetSphereMesh(1.0f);
    const Mesh* cube = GetCubeMesh(1.0f);
    const Mesh* cylinder = GetCylinderMesh(0.7f, 2.2f, 24);

    while (!WindowShouldClose()) {
        const float deltaTime = Min(GetFrameTime(), 0.05f);
        const float width = static_cast<float>(GetScreenWidth());

        // --- shader selection ---
        for (int i = 0; i < kShaderCount; ++i) {
            const Key key = static_cast<Key>(static_cast<int>(Key::Num1) + i);
            if (IsKeyPressed(key)) scene.active = i;
        }
        if (IsKeyPressed(Key::Right)) scene.active = (scene.active + 1) % kShaderCount;
        if (IsKeyPressed(Key::Left)) scene.active = (scene.active + kShaderCount - 1) % kShaderCount;

        UpdateOrbitCamera(scene.orbit);
        scene.rotation += deltaTime * 30.0f;

        Shader* shader = scene.shaders[scene.active];
        const char* shaderName = kShaders[scene.active].name;

        BeginScene(scene.orbit.ToCamera(), Color::FromBytes(12, 12, 18, 255));
        SetLightDirection(Vec3{-0.4f, -1.0f, -0.3f});

        // The lit reference geometry, drawn with the built-in shader.
        DrawGrid(12.0f, 24, colors::DarkGray);
        DrawCube(Vec3{-6.5f, 0.5f, 0.0f}, Vec3{1, 1, 1}, colors::Gray);

        // The showcase, drawn with the selected custom shader. BeginShader saves
        // the state and EndShader restores it, so the two never leak into each
        // other.
        BeginShader(shader);
        // The grid and hologram shaders output partial alpha, so they need a blend
        // mode; the opaque ones would waste a transparent pass.
        if (scene.active == 2) {
            SetBlendMode(BlendMode::Additive);
        } else if (scene.active == 3) {
            SetBlendMode(BlendMode::AlphaBlend);
        }

        DrawMesh(sphere, Vec3{-1.8f, 2.2f, 0.0f}, Vec3{}, Vec3{2.0f, 2.0f, 2.0f},
                 colors::White);
        DrawMesh(cube, Vec3{1.8f, 1.6f, 0.0f}, Vec3{0.0f, scene.rotation, 0.0f},
                 Vec3{2.6f, 2.6f, 2.6f}, colors::White);
        DrawMesh(cylinder, Vec3{4.6f, 1.6f, 0.0f}, Vec3{0.0f, 0.0f, 90.0f},
                 Vec3{1.6f, 1.6f, 1.6f}, colors::White);

        EndShader();

        // A ring of small cubes, lit normally, to keep a sense of scale.
        for (int i = 0; i < 10; ++i) {
            const float angle = scene.rotation * 0.4f + static_cast<float>(i) * TAU / 10.0f;
            const Vec3 position{std::cos(angle) * 8.0f, 0.4f, std::sin(angle) * 8.0f};
            DrawCube(position, Vec3{0.5f, 0.5f, 0.5f},
                     Color::FromBytes(60 + i * 15, 80, 160 - i * 8, 255));
        }

        EndScene();

        // --- HUD ---
        DrawTextFormat(16, 14, 20.0f, colors::White, "shader: %s", shaderName);
        DrawTextFormat(16, 40, 16.0f, colors::LightGray,
                       "1-5 ou setas trocam   mouse orbita   esc sai");
        if (!shader) {
            DrawTextFormat(16, 66, 16.0f, colors::Red,
                           "este shader falhou; usando o padrao");
        }
        if (music) {
            DrawTextFormat(16, 66, 16.0f, colors::LightGray, "musica %.1fs / %.1fs",
                           GetMusicTime(music), GetMusicLength(music));
        }
        (void)width;
    }

    Assets::UnloadAll();
    std::remove(Assets::plasma.Path());
    std::remove(Assets::fresnel.Path());
    std::remove(Assets::hologram.Path());
    std::remove(Assets::grid.Path());
    std::remove(Assets::normals.Path());
    std::remove(Assets::music.Path());

    Close();
    return 0;
}
