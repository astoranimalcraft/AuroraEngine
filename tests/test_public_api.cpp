// Aurora3D — public API surface check.
//
// This file is never executed: it exists to be compiled and linked against the
// library. Every call it makes is one the documentation promises, so a renamed or
// removed function breaks the build here instead of silently rotting in README.md.
// Kept out of examples/ because it is not a program worth running, and out of the
// native test loop because it needs the Windows library to link.
#include <aurora/aurora.h>

int main() {
    // --- README "quick start" ---
    a3d::Config config;
    config.title = "Meu jogo";
    if (!a3d::Init(config)) return 1;

    a3d::OrbitCamera orbit;

    while (!a3d::WindowShouldClose()) {
        a3d::UpdateOrbitCamera(orbit);

        a3d::BeginScene(orbit.ToCamera());
        a3d::DrawGrid();
        a3d::DrawCube({0, 0.5f, 0}, {1, 1, 1}, a3d::colors::Orange);
        a3d::EndScene();
    }
    a3d::Close();

    // --- README "a câmera é sua" ---
    a3d::Camera3D camera;
    camera.position = {0, 3, -8};
    camera.target = {0, 1, 0};
    camera.fovY = 60.0f;
    orbit.distance = 10.0f;

    // --- README "desenho immediate-mode" ---
    const a3d::Mesh* mesh = a3d::GetSphereMesh(1.0f);
    a3d::Mat4 world = a3d::Compose(a3d::Vec3{0, 1, 0}, a3d::Vec3{0, 1, 0}, a3d::Vec3{1, 1, 1});

    // --- README "estado de renderização" ---
    a3d::SetBlendMode(a3d::BlendMode::AlphaBlend);
    a3d::SetCullMode(a3d::CullMode::Back);
    a3d::SetDepthTest(a3d::DepthTest::Less);
    a3d::SetLightDirection({-0.5f, -1.0f, -0.4f});
    a3d::SetAmbient(a3d::colors::White, 0.3f);

    // --- README "malhas e texturas próprias" ---
    a3d::Vertex vertices[3] = {};
    uint32_t indices[3] = {0, 1, 2};
    a3d::Mesh* triangulo = a3d::CreateMesh(vertices, 3, indices, 3);

    // --- README draw table ---
    a3d::Texture* tex = a3d::LoadTextureFromFile("chao.png");
    a3d::DrawMeshMaterial(mesh, {0, 0, 0}, tex, {}, {}, a3d::colors::White, 0.8f, 0.2f);
    a3d::DrawMeshEx(mesh, world, a3d::colors::White);
    a3d::DrawMesh(mesh, {0, 2, 0}, a3d::Vec3{0, 45, 0}, a3d::Vec3{2, 2, 2});
    a3d::DrawSphere({3, 1, 0}, 1.0f, a3d::colors::Green);
    a3d::DrawCylinder({-3, 1, 0}, 0.5f, 2.0f, a3d::colors::Blue);
    a3d::DrawPlane({0, 0, 0}, {20, 20}, a3d::colors::DarkGray);
    a3d::DrawCone({0, 2, 5}, 0.5f, 1.5f, a3d::colors::Red);
    a3d::DrawAxes({}, 2.0f);
    a3d::DrawLine({0, 0, 0}, {0, 5, 0}, a3d::colors::Yellow);
    a3d::DrawBounds(a3d::MeshBounds(mesh), a3d::colors::Green);
    a3d::DrawScreenRect({10, 10}, {200, 40}, a3d::colors::Black);
    a3d::DrawScreenLine({10, 60}, {210, 60}, a3d::colors::White, 2.0f);
    a3d::UnloadMesh(triangulo);
    a3d::UnloadTexture(tex);

    // --- README "overlay" ---
    a3d::BeginSceneOverlay(camera);
    a3d::EndScene();

    // --- README "hot reload" ---
    a3d::Shader* s = a3d::LoadShaderFromFile("meu.hlsl", true);
    a3d::UnloadShader(s);

    // --- README "debug layer" ---
    a3d::SetDebugLayerEnabled(true);

    // --- README "diagnóstico" ---
    a3d::RenderStats stats = a3d::GetRenderStats();
    a3d::LogInfo("%u draw calls, %u triângulos, %u trocas de estado", stats.drawCalls,
                 stats.triangles, stats.stateChanges);

    // --- README "shaders HLSL" ---
    const char* fonte = R"HLSL(
#include "aurora.hlsli"
A3D_VertexOutput VSMain(A3D_VertexInput input) { return A3D_TransformVertex(input); }
float4 PSMain(A3D_VertexOutput input, bool frente : SV_IsFrontFace) : SV_TARGET {
    return float4(input.color.rgb * input.uv.x, input.color.a);
}
)HLSL";
    a3d::Shader* meu = a3d::LoadShaderFromMemory(fonte);
    a3d::SetShader(meu);
    a3d::SetShader(nullptr);

    // --- README "2D" ---
    a3d::Camera2D camera2d;
    camera2d.offset = {640.0f, 360.0f};
    camera2d.target = {0.0f, 0.0f};
    camera2d.zoom = 1.5f;
    camera2d.rotation = 10.0f;
    a3d::BeginMode2D(camera2d);
    a3d::DrawRectangle(0.0f, 0.0f, 100.0f, 50.0f, a3d::colors::Red);
    a3d::DrawRectangleV({0, 0}, {10, 10}, a3d::colors::Green);
    a3d::DrawRectangleRec(a3d::Rect{0, 0, 10, 10}, a3d::colors::Blue);
    a3d::DrawRectangleLines(0.0f, 0.0f, 10.0f, 10.0f, a3d::colors::White);
    a3d::DrawRectangleLinesEx(a3d::Rect{0, 0, 10, 10}, 2.0f, a3d::colors::White);
    a3d::DrawRectangleGradient(a3d::Rect{0, 0, 10, 10}, a3d::colors::Red, a3d::colors::Blue);
    a3d::DrawCircle(0.0f, 0.0f, 10.0f, a3d::colors::Yellow);
    a3d::DrawCircleV({0, 0}, 10.0f, a3d::colors::Yellow);
    a3d::DrawCircleLines({0, 0}, 10.0f, a3d::colors::Yellow);
    a3d::DrawCircleSector({0, 0}, 10.0f, 0.0f, 90.0f, a3d::colors::Yellow);
    a3d::DrawRing({0, 0}, 5.0f, 10.0f, 0.0f, 180.0f, a3d::colors::Yellow);
    a3d::DrawTriangle2D({0, 0}, {10, 0}, {0, 10}, a3d::colors::Cyan);
    a3d::DrawTriangleLines2D({0, 0}, {10, 0}, {0, 10}, a3d::colors::Cyan);
    a3d::DrawPolygon({0, 0}, 6, 20.0f, 0.0f, a3d::colors::Magenta);
    a3d::DrawPolygonLines({0, 0}, 6, 20.0f, 0.0f, a3d::colors::Magenta);
    const a3d::Vec2 points[3] = {{0, 0}, {10, 0}, {0, 10}};
    a3d::DrawPolygonV(points, 3, a3d::colors::Orange);
    a3d::DrawPolyline(points, 3, 2.0f, a3d::colors::Orange);
    a3d::EndMode2D();

    // --- README "texto" ---
    a3d::Font* font = a3d::GetDefaultFont();
    a3d::SetFont(font);
    a3d::DrawText("olá", 10, 10, 20.0f, a3d::colors::White);
    a3d::DrawTextV("olá", {10, 10}, 20.0f, a3d::colors::White);
    a3d::DrawTextCentered("olá", 100.0f, 50.0f, 20.0f, a3d::colors::White);
    a3d::DrawTextWithBackground("olá", 10, 10, 20.0f, a3d::colors::White, a3d::colors::Black);
    a3d::DrawTextFormat(10, 10, 20.0f, a3d::colors::White, "%d", 1);
    const a3d::Vec2 measured = a3d::MeasureText("olá", 20.0f);
    a3d::DrawText3D("olá", {0, 1, 0}, 0.05f, a3d::colors::White);
    a3d::UnloadFont(nullptr);   // null is a no-op

    // --- README "texturas / sprites" ---
    a3d::DrawTexture(tex, 0.0f, 0.0f, a3d::colors::White);
    a3d::DrawTextureV(tex, {0, 0}, a3d::colors::White);
    a3d::DrawTextureEx(tex, {0, 0}, 45.0f, 2.0f, a3d::colors::White);
    a3d::DrawTextureRec(tex, a3d::Rect{0, 0, 8, 8}, {0, 0}, a3d::colors::White);
    a3d::DrawTexturePro(tex, a3d::Rect{0, 0, 8, 8}, a3d::Rect{0, 0, 32, 32}, {0.5f, 0.5f}, 0.0f,
                        a3d::colors::White);

    // --- README "estado de shader" ---
    a3d::BeginShader(meu);
    a3d::DrawSphere({0, 1, 0}, 1.0f, a3d::colors::White);
    a3d::EndShader();

    // --- README "áudio" ---
    a3d::Sound* beep = a3d::LoadSound("beep.wav");
    a3d::Music* track = a3d::LoadMusic("music.wav");
    if (beep) {
        a3d::SetSoundVolume(beep, 0.5f);
        a3d::PlaySound(beep);
        const uint64_t voice = a3d::PlaySoundMulti(beep, 0.5f);
        a3d::StopSoundMulti(voice);
    }
    if (track) {
        a3d::SetMusicVolume(track, 0.4f);
        a3d::SetMusicLooping(track, true);
        a3d::PlayMusic(track);
        a3d::SeekMusic(track, 5.0f);
        (void)a3d::GetMusicTime(track);
    }
    a3d::SetMasterVolume(0.8f);
    a3d::UnloadMusic(track);
    a3d::UnloadSound(beep);

    // --- README "vídeo" ---
    a3d::Video* clip = a3d::LoadVideo("clip.mp4");
    if (clip) {
        a3d::SetVideoLooping(clip, true);
        a3d::PlayVideo(clip);
        a3d::DrawVideoV(clip, {0, 0}, a3d::colors::White);
        (void)a3d::GetVideoWidth(clip);
    }
    a3d::UnloadVideo(clip);

    // --- README "modelos" ---
    a3d::Model* modelo = a3d::LoadModel("robot.obj");
    if (modelo) {
        a3d::DrawModel(modelo, {0, 0, 0});
        a3d::DrawModelEx(modelo, world, a3d::colors::White);
    }
    a3d::UnloadModel(modelo);

    (void)camera;
    (void)world;
    (void)mesh;
    (void)triangulo;
    (void)measured;
    return 0;
}
