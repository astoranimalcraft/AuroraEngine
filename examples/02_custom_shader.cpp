// NexiumEngine — example: a custom HLSL shader.
//
// The engine binds a fixed root signature, so a user shader only has to include
// "nexium.hlsli" and provide VSMain / PSMain. Here the pixel stage is replaced
// with a flat two-colour gradient, which needs no texture and no lighting.
//
// The shader is compiled at runtime by the engine, so there is no build step and
// no .cso to ship. Set `watchForChanges` to edit the HLSL and see it reload live.
#include <nexium/NexiumEngine.h>

#include <cmath>

namespace {

const char* kGradientHlsl = R"HLSL(
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    // Vertical gradient in world space, tinted by the object colour.
    float t = saturate(input.worldPos.y * 0.5f + 0.5f);
    float3 a = float3(0.10f, 0.35f, 0.85f);
    float3 b = float3(1.00f, 0.45f, 0.10f);
    float3 albedo = lerp(a, b, t) * input.color.rgb;

    if (flags & N3D_FLAG_TWOSIDED)
        albedo *= isFrontFace ? 1.0f : 0.6f;

    return float4(albedo, input.color.a);
}
)HLSL";

n3d::OrbitCamera gOrbit;

} // namespace

int main() {
    n3d::Config config;
    config.title = "NexiumEngine — Custom Shader";
    if (!n3d::Init(config)) return 1;

    gOrbit.distance = 7.0f;
    n3d::Shader* gradient = n3d::LoadShaderFromMemory(kGradientHlsl, "gradient");
    if (!gradient) {
        n3d::LogError("could not compile the gradient shader; falling back to the default");
    }

    const n3d::Mesh* sphere = n3d::GetSphereMesh(1.0f);
    const n3d::Mesh* cube = n3d::GetCubeMesh(1.0f);

    while (!n3d::WindowShouldClose()) {
        n3d::UpdateOrbitCamera(gOrbit);

        n3d::BeginScene(gOrbit.ToCamera(), n3d::colors::Black);
        n3d::DrawGrid(8.0f, 16, n3d::colors::DarkGray);

        n3d::SetShader(gradient);
        n3d::DrawMesh(sphere, n3d::Vec3{-1.6f, 1.0f, 0.0f}, n3d::Vec3{},
                      n3d::Vec3{1.4f, 1.4f, 1.4f}, n3d::colors::White);
        n3d::DrawMesh(cube, n3d::Vec3{1.6f, 1.0f, 0.0f},
                      n3d::Vec3{0.0f, n3d::GetTime() * 40.0f, 0.0f}, n3d::Vec3{1.6f, 1.6f, 1.6f},
                      n3d::colors::White);

        // nullptr restores the built-in lit shader.
        n3d::SetShader(nullptr);
        n3d::DrawCube(n3d::Vec3{0.0f, 0.5f, 2.5f}, n3d::Vec3{1, 1, 1}, n3d::colors::Gray);
        n3d::EndScene();
    }

    n3d::UnloadShader(gradient);
    n3d::Close();
    return 0;
}
