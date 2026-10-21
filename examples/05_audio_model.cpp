// NexiumEngine — example: a 3D scene with sound, a loaded model and shader states.
//
// Exercises the subsystems added on top of the core: an OBJ model with a material,
// a looping music track, a sound effect triggered by a key, and BeginShader() to
// swap the pixel shader for one draw without disturbing the rest of the frame.
//
// Every resource is optional: if the files are missing, or there is no audio
// device, the example still runs and just draws the fallback cube.
#include <nexium/NexiumEngine.h>

#include <cmath>

namespace {

n3d::OrbitCamera gOrbit;

const char* kOutlineHlsl = R"HLSL(
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    // Rim light: bright at grazing angles, dark head-on. Reads as an outline.
    float3 view = normalize(cameraPosTime.xyz - input.worldPos);
    float rim = pow(1.0f - saturate(dot(normalize(input.normal), view)), 2.0f);
    return float4(rim * input.color.rgb, input.color.a);
}
)HLSL";

} // namespace

int main() {
    n3d::Config config;
    config.title = "NexiumEngine — Model, Sound & Shaders";
    config.width = 1280;
    config.height = 720;
    if (!n3d::Init(config)) return 1;

    gOrbit.distance = 8.0f;
    n3d::SetLightDirection(n3d::Vec3{-0.4f, -1.0f, -0.3f});
    n3d::SetAmbient(n3d::colors::White, 0.25f);

    // A model, if one is around. The built-in loader reads OBJ/MTL with no extra
    // dependency; any other format needs NEXIUM_HAS_ASSIMP.
    n3d::Model* model = n3d::LoadModel("assets/robot.obj");
    if (!model) n3d::LogInfo("no assets/robot.obj — drawing the fallback cube");

    n3d::Sound* beep = n3d::LoadSound("assets/beep.wav");
    n3d::Music* music = n3d::LoadMusic("assets/music.wav");
    if (music) {
        n3d::SetMusicVolume(music, 0.5f);
        n3d::SetMusicLooping(music, true);
        n3d::PlayMusic(music);
    }

    n3d::Shader* outline = n3d::LoadShaderFromMemory(kOutlineHlsl, "outline");
    const n3d::Mesh* cube = n3d::GetCubeMesh(1.0f);

    while (!n3d::WindowShouldClose()) {
        n3d::UpdateOrbitCamera(gOrbit);

        if (n3d::IsKeyPressed(n3d::Key::Space) && beep) n3d::PlaySound(beep);
        if (n3d::IsKeyPressed(n3d::Key::P) && music) {
            if (n3d::IsMusicPlaying(music)) n3d::PauseMusic(music);
            else n3d::ResumeMusic(music);
        }

        n3d::BeginScene(gOrbit.ToCamera(), n3d::Color{0.18f, 0.20f, 0.24f, 1.0f});
        n3d::DrawGrid(12.0f, 24, n3d::colors::DarkGray);

        if (model) {
            n3d::DrawModel(model, n3d::Vec3{0.0f, 0.0f, 0.0f}, n3d::Vec3{0.0f, n3d::GetTime() * 20.0f, 0.0f},
                           n3d::Vec3{1.0f, 1.0f, 1.0f});
        } else {
            n3d::DrawCube(n3d::Vec3{0.0f, 1.0f, 0.0f}, n3d::Vec3{2.0f, 2.0f, 2.0f},
                          n3d::colors::Orange);
        }

        // A glowing shell around the model, drawn with the rim shader only for
        // this one call. BeginShader/EndShader keep the state change local.
        if (outline) {
            n3d::BeginShader(outline);
            n3d::SetBlendMode(n3d::BlendMode::Additive);
            n3d::SetDepthTest(n3d::DepthTest::LessEqual);
            n3d::DrawMesh(cube, n3d::Vec3{0.0f, 1.5f, 0.0f}, n3d::Vec3{0.0f, n3d::GetTime() * 40.0f, 0.0f},
                          n3d::Vec3{2.6f, 2.6f, 2.6f}, n3d::colors::Cyan);
            n3d::EndShader();
        }

        // HUD.
        n3d::DrawText("space: beep   p: pause music", 16, 16, 20.0f, n3d::colors::White);
        if (music) {
            n3d::DrawTextFormat(16, 42, 20.0f, n3d::colors::Lime, "music %.1f / %.1f s",
                                static_cast<double>(n3d::GetMusicTime(music)),
                                static_cast<double>(n3d::GetMusicLength(music)));
        }

        n3d::EndScene();
    }

    if (music) n3d::UnloadMusic(music);
    if (beep) n3d::UnloadSound(beep);
    if (model) n3d::UnloadModel(model);
    n3d::Close();
    return 0;
}
