// Aurora3D — example: a custom HLSL shader.
//
// The engine binds a fixed root signature, so a user shader only has to include
// "aurora.hlsli" and provide VSMain / PSMain. Here the pixel stage is replaced
// with a flat two-colour gradient, which needs no texture and no lighting.
//
// The shader is compiled at runtime by the engine, so there is no build step and
// no .cso to ship. Set `watchForChanges` to edit the HLSL and see it reload live.
#include <aurora/aurora.h>

#include <cmath>

namespace {

const char* kGradientHlsl = R"HLSL(
#include "aurora.hlsli"

A3D_VertexOutput VSMain(A3D_VertexInput input)
{
    return A3D_TransformVertex(input);
}

float4 PSMain(A3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    // Vertical gradient in world space, tinted by the object colour.
    float t = saturate(input.worldPos.y * 0.5f + 0.5f);
    float3 a = float3(0.10f, 0.35f, 0.85f);
    float3 b = float3(1.00f, 0.45f, 0.10f);
    float3 albedo = lerp(a, b, t) * input.color.rgb;

    if (flags & A3D_FLAG_TWOSIDED)
        albedo *= isFrontFace ? 1.0f : 0.6f;

    return float4(albedo, input.color.a);
}
)HLSL";

a3d::OrbitCamera gOrbit;

} // namespace

int main() {
    a3d::Config config;
    config.title = "Aurora3D — Custom Shader";
    if (!a3d::Init(config)) return 1;

    gOrbit.distance = 7.0f;
    a3d::Shader* gradient = a3d::LoadShaderFromMemory(kGradientHlsl, "gradient");
    if (!gradient) {
        a3d::LogError("could not compile the gradient shader; falling back to the default");
    }

    const a3d::Mesh* sphere = a3d::GetSphereMesh(1.0f);
    const a3d::Mesh* cube = a3d::GetCubeMesh(1.0f);

    while (!a3d::WindowShouldClose()) {
        a3d::UpdateOrbitCamera(gOrbit);

        a3d::BeginScene(gOrbit.ToCamera(), a3d::colors::Black);
        a3d::DrawGrid(8.0f, 16, a3d::colors::DarkGray);

        a3d::SetShader(gradient);
        a3d::DrawMesh(sphere, a3d::Vec3{-1.6f, 1.0f, 0.0f}, a3d::Vec3{},
                      a3d::Vec3{1.4f, 1.4f, 1.4f}, a3d::colors::White);
        a3d::DrawMesh(cube, a3d::Vec3{1.6f, 1.0f, 0.0f},
                      a3d::Vec3{0.0f, a3d::GetTime() * 40.0f, 0.0f}, a3d::Vec3{1.6f, 1.6f, 1.6f},
                      a3d::colors::White);

        // nullptr restores the built-in lit shader.
        a3d::SetShader(nullptr);
        a3d::DrawCube(a3d::Vec3{0.0f, 0.5f, 2.5f}, a3d::Vec3{1, 1, 1}, a3d::colors::Gray);
        a3d::EndScene();
    }

    a3d::UnloadShader(gradient);
    a3d::Close();
    return 0;
}
