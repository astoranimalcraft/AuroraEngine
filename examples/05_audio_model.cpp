// Aurora3D — example: a 3D scene with sound, a loaded model and shader states.
//
// Exercises the subsystems added on top of the core: an OBJ model with a material,
// a looping music track, a sound effect triggered by a key, and BeginShader() to
// swap the pixel shader for one draw without disturbing the rest of the frame.
//
// Every resource is optional: if the files are missing, or there is no audio
// device, the example still runs and just draws the fallback cube.
#include <aurora/aurora.h>

#include <cmath>

namespace {

a3d::OrbitCamera gOrbit;

const char* kOutlineHlsl = R"HLSL(
#include "aurora.hlsli"

A3D_VertexOutput VSMain(A3D_VertexInput input)
{
    return A3D_TransformVertex(input);
}

float4 PSMain(A3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    // Rim light: bright at grazing angles, dark head-on. Reads as an outline.
    float3 view = normalize(cameraPosTime.xyz - input.worldPos);
    float rim = pow(1.0f - saturate(dot(normalize(input.normal), view)), 2.0f);
    return float4(rim * input.color.rgb, input.color.a);
}
)HLSL";

} // namespace

int main() {
    a3d::Config config;
    config.title = "Aurora3D — Model, Sound & Shaders";
    config.width = 1280;
    config.height = 720;
    if (!a3d::Init(config)) return 1;

    gOrbit.distance = 8.0f;
    a3d::SetLightDirection(a3d::Vec3{-0.4f, -1.0f, -0.3f});
    a3d::SetAmbient(a3d::colors::White, 0.25f);

    // A model, if one is around. The built-in loader reads OBJ/MTL with no extra
    // dependency; any other format needs AURORA_HAS_ASSIMP.
    a3d::Model* model = a3d::LoadModel("assets/robot.obj");
    if (!model) a3d::LogInfo("no assets/robot.obj — drawing the fallback cube");

    a3d::Sound* beep = a3d::LoadSound("assets/beep.wav");
    a3d::Music* music = a3d::LoadMusic("assets/music.wav");
    if (music) {
        a3d::SetMusicVolume(music, 0.5f);
        a3d::SetMusicLooping(music, true);
        a3d::PlayMusic(music);
    }

    a3d::Shader* outline = a3d::LoadShaderFromMemory(kOutlineHlsl, "outline");
    const a3d::Mesh* cube = a3d::GetCubeMesh(1.0f);

    while (!a3d::WindowShouldClose()) {
        a3d::UpdateOrbitCamera(gOrbit);

        if (a3d::IsKeyPressed(a3d::Key::Space) && beep) a3d::PlaySound(beep);
        if (a3d::IsKeyPressed(a3d::Key::P) && music) {
            if (a3d::IsMusicPlaying(music)) a3d::PauseMusic(music);
            else a3d::ResumeMusic(music);
        }

        a3d::BeginScene(gOrbit.ToCamera(), a3d::Color{0.18f, 0.20f, 0.24f, 1.0f});
        a3d::DrawGrid(12.0f, 24, a3d::colors::DarkGray);

        if (model) {
            a3d::DrawModel(model, a3d::Vec3{0.0f, 0.0f, 0.0f}, a3d::Vec3{0.0f, a3d::GetTime() * 20.0f, 0.0f},
                           a3d::Vec3{1.0f, 1.0f, 1.0f});
        } else {
            a3d::DrawCube(a3d::Vec3{0.0f, 1.0f, 0.0f}, a3d::Vec3{2.0f, 2.0f, 2.0f},
                          a3d::colors::Orange);
        }

        // A glowing shell around the model, drawn with the rim shader only for
        // this one call. BeginShader/EndShader keep the state change local.
        if (outline) {
            a3d::BeginShader(outline);
            a3d::SetBlendMode(a3d::BlendMode::Additive);
            a3d::SetDepthTest(a3d::DepthTest::LessEqual);
            a3d::DrawMesh(cube, a3d::Vec3{0.0f, 1.5f, 0.0f}, a3d::Vec3{0.0f, a3d::GetTime() * 40.0f, 0.0f},
                          a3d::Vec3{2.6f, 2.6f, 2.6f}, a3d::colors::Cyan);
            a3d::EndShader();
        }

        // HUD.
        a3d::DrawText("space: beep   p: pause music", 16, 16, 20.0f, a3d::colors::White);
        if (music) {
            a3d::DrawTextFormat(16, 42, 20.0f, a3d::colors::Lime, "music %.1f / %.1f s",
                                static_cast<double>(a3d::GetMusicTime(music)),
                                static_cast<double>(a3d::GetMusicLength(music)));
        }

        a3d::EndScene();
    }

    if (music) a3d::UnloadMusic(music);
    if (beep) a3d::UnloadSound(beep);
    if (model) a3d::UnloadModel(model);
    a3d::Close();
    return 0;
}
