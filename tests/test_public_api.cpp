// NexiumEngine — public API surface check.
//
// This file is never executed: it exists to be compiled and linked against the
// library. Every call it makes is one the documentation promises, so a renamed or
// removed function breaks the build here instead of silently rotting in README.md.
// Kept out of examples/ because it is not a program worth running, and out of the
// native test loop because it needs the Windows library to link.
#include <nexium/NexiumEngine.h>
#include <nexium/n2d.h>

// The named drawer used by the storage section below. It has to be at namespace
// scope: a local class cannot have static data members, so N3D_DEFINE_STORAGE
// never belongs inside a function.
#define API_TEST_FILES(X)              \
    X(music, "assets/overworld.ogg")   \
    X(hero, "assets/hero.obj")
namespace n3d {
N3D_DEFINE_STORAGE(Game, API_TEST_FILES)
}
#undef API_TEST_FILES

int main() {
    // --- README "quick start" ---
    n3d::Config config;
    config.title = "Meu jogo";
    if (!n3d::Init(config)) return 1;

    n3d::OrbitCamera orbit;

    while (!n3d::WindowShouldClose()) {
        n3d::UpdateOrbitCamera(orbit);

        n3d::BeginScene(orbit.ToCamera());
        n3d::DrawGrid();
        n3d::DrawCube({0, 0.5f, 0}, {1, 1, 1}, n3d::colors::Orange);
        n3d::EndScene();
    }
    n3d::Close();

    // --- README "a câmera é sua" ---
    n3d::Camera3D camera;
    camera.position = {0, 3, -8};
    camera.target = {0, 1, 0};
    camera.fovY = 60.0f;
    orbit.distance = 10.0f;

    // --- README "desenho immediate-mode" ---
    const n3d::Mesh* mesh = n3d::GetSphereMesh(1.0f);
    n3d::Mat4 world = n3d::Compose(n3d::Vec3{0, 1, 0}, n3d::Vec3{0, 1, 0}, n3d::Vec3{1, 1, 1});

    // --- README "estado de renderização" ---
    n3d::SetBlendMode(n3d::BlendMode::AlphaBlend);
    n3d::SetCullMode(n3d::CullMode::Back);
    n3d::SetDepthTest(n3d::DepthTest::Less);
    n3d::SetLightDirection({-0.5f, -1.0f, -0.4f});
    n3d::SetAmbient(n3d::colors::White, 0.3f);

    // --- README "malhas e texturas próprias" ---
    n3d::Vertex vertices[3] = {};
    uint32_t indices[3] = {0, 1, 2};
    n3d::Mesh* triangulo = n3d::CreateMesh(vertices, 3, indices, 3);

    // --- README draw table ---
    n3d::Texture* tex = n3d::LoadTextureFromFile("chao.png");
    n3d::DrawMeshMaterial(mesh, {0, 0, 0}, tex, {}, {}, n3d::colors::White, 0.8f, 0.2f);
    n3d::DrawMeshEx(mesh, world, n3d::colors::White);
    n3d::DrawMesh(mesh, {0, 2, 0}, n3d::Vec3{0, 45, 0}, n3d::Vec3{2, 2, 2});
    n3d::DrawSphere({3, 1, 0}, 1.0f, n3d::colors::Green);
    n3d::DrawCylinder({-3, 1, 0}, 0.5f, 2.0f, n3d::colors::Blue);
    n3d::DrawPlane({0, 0, 0}, {20, 20}, n3d::colors::DarkGray);
    n3d::DrawCone({0, 2, 5}, 0.5f, 1.5f, n3d::colors::Red);
    n3d::DrawAxes({}, 2.0f);
    n3d::DrawLine({0, 0, 0}, {0, 5, 0}, n3d::colors::Yellow);
    n3d::DrawBounds(n3d::MeshBounds(mesh), n3d::colors::Green);
    n3d::DrawScreenRect({10, 10}, {200, 40}, n3d::colors::Black);
    n3d::DrawScreenLine({10, 60}, {210, 60}, n3d::colors::White, 2.0f);
    n3d::UnloadMesh(triangulo);
    n3d::UnloadTexture(tex);

    // --- README "overlay" ---
    n3d::BeginSceneOverlay(camera);
    n3d::EndScene();

    // --- README "hot reload" ---
    n3d::Shader* s = n3d::LoadShaderFromFile("meu.hlsl", true);
    n3d::UnloadShader(s);

    // --- README "debug layer" ---
    n3d::SetDebugLayerEnabled(true);

    // --- README "câmeras" (novas APIs) ---
    n3d::FpsCamera fps;
    fps.position = {0, 1.7f, 0};
    fps.yaw = 0.5f;
    n3d::UpdateFpsCamera(fps);
    n3d::Camera3D fpsCamera = fps.ToCamera();
    const n3d::Vec3 projected = fpsCamera.WorldToScreen({0, 1, 0}, 1280.0f, 720.0f);
    const n3d::Ray pickRay = fpsCamera.RayThroughScreen({640.0f, 360.0f}, 1280.0f, 720.0f);
    const n3d::Frustum frustum = n3d::GetCameraFrustum(fpsCamera, 16.0f / 9.0f);
    (void)frustum.ContainsPoint({0, 0, 0});
    n3d::CameraShake shake;
    shake.Add(0.5f);
    shake.Update(n3d::GetFrameTime());
    const n3d::Camera3D blended = n3d::LerpCamera(camera, fpsCamera, 0.5f);
    camera.Orbit(0.1f, 0.0f);
    camera.Zoom(1.0f);
    camera.MoveLocal({1, 0, 0});

    // --- README "janela e monitor" (novas APIs) ---
    n3d::SetWindowPosition({100, 100});
    (void)n3d::GetWindowPosition();
    (void)n3d::GetMonitorCount();
    (void)n3d::GetMonitorSize();
    (void)n3d::GetMonitorRefreshRate();
    (void)n3d::GetMonitorName();
    (void)n3d::GetWindowDpiScale();
    n3d::SetWindowOpacity(0.9f);
    n3d::SetWindowIcon("icone.ico");
    n3d::SetClipboardText("texto");
    (void)n3d::GetClipboardText();

    // Platform and window additions.
    (void)n3d::GetPlatformName();
    const n3d::PlatformCapabilities caps = n3d::GetPlatformCapabilities();
    (void)caps.rawMouse;
    (void)caps.customCursors;
    (void)caps.textInput;
    n3d::ShowWindow();
    n3d::HideWindow();
    (void)n3d::IsWindowVisible();
    (void)n3d::IsWindowMaximizedChanged();
    (void)n3d::GetCurrentMonitor();

    // Cursor additions.
    (void)n3d::SetMouseCursorFromFile("cursors/crosshair.cur");
    n3d::SetRawMouseInput(true);
    (void)n3d::IsRawMouseInputEnabled();
    n3d::SetMouseRect(n3d::Vec2{0.0f, 0.0f}, n3d::Vec2{640.0f, 480.0f});
    (void)n3d::GetMousePositionNormalized();

    // --- README "entrada" (novas APIs) ---
    n3d::SetMouseCursor(n3d::MouseCursor::Crosshair);
    (void)n3d::GetMouseCursor();
    (void)n3d::GetMouseWheelVector();

    // --- README "áudio" (novas APIs) ---
    (void)projected;
    (void)pickRay;
    (void)blended;
    (void)shake;

    // --- README "diagnóstico" ---
    n3d::RenderStats stats = n3d::GetRenderStats();
    n3d::LogInfo("%u draw calls, %u triângulos, %u trocas de estado", stats.drawCalls,
                 stats.triangles, stats.stateChanges);

    // --- README "shaders HLSL" ---
    const char* fonte = R"HLSL(
#include "nexium.hlsli"
N3D_VertexOutput VSMain(N3D_VertexInput input) { return N3D_TransformVertex(input); }
float4 PSMain(N3D_VertexOutput input, bool frente : SV_IsFrontFace) : SV_TARGET {
    return float4(input.color.rgb * input.uv.x, input.color.a);
}
)HLSL";
    n3d::Shader* meu = n3d::LoadShaderFromMemory(fonte);
    n3d::SetShader(meu);
    n3d::SetShader(nullptr);

    // --- README "sombras" ---
    n3d::SetShadowsEnabled(true);
    (void)n3d::ShadowsEnabled();
    n3d::SetShadowResolution(2048);
    (void)n3d::GetShadowResolution();
    n3d::SetShadowDistance(40.0f);
    (void)n3d::GetShadowDistance();
    n3d::SetShadowBias(0.0015f, 0.02f);
    (void)n3d::GetShadowDepthBias();
    (void)n3d::GetShadowNormalBias();
    n3d::SetMeshCastsShadow(triangulo, false);
    n3d::SetMeshCastsShadow(triangulo, true);

    // --- README "ambiente / HDRI" ---
    n3d::Texture* hdr = n3d::LoadHDRFromFile("ceu.hdr");
    n3d::SetSky(hdr);
    n3d::SetSkyTint(n3d::colors::White);
    n3d::DrawSky();
    (void)n3d::UpdateLightingFromSky(0.5f);
    (void)n3d::GetSky();
    (void)n3d::TexturePixelFormat(hdr);
    n3d::SetSky(nullptr);
    n3d::UnloadTexture(hdr);

    // --- README "texturas float" ---
    const float pixelsFloat[16] = {};
    n3d::Texture* floatTex = n3d::LoadTextureFromFloats(pixelsFloat, 2, 2);
    n3d::UnloadTexture(floatTex);

    // --- README "2D" ---
    n3d::Camera2D camera2d;
    camera2d.offset = {640.0f, 360.0f};
    camera2d.target = {0.0f, 0.0f};
    camera2d.zoom = 1.5f;
    camera2d.rotation = 10.0f;
    n3d::BeginMode2D(camera2d);
    n3d::DrawRectangle(0.0f, 0.0f, 100.0f, 50.0f, n3d::colors::Red);
    n3d::DrawRectangleV({0, 0}, {10, 10}, n3d::colors::Green);
    n3d::DrawRectangleRec(n3d::Rect{0, 0, 10, 10}, n3d::colors::Blue);
    n3d::DrawRectangleLines(0.0f, 0.0f, 10.0f, 10.0f, n3d::colors::White);
    n3d::DrawRectangleLinesEx(n3d::Rect{0, 0, 10, 10}, 2.0f, n3d::colors::White);
    n3d::DrawRectangleGradient(n3d::Rect{0, 0, 10, 10}, n3d::colors::Red, n3d::colors::Blue);
    n3d::DrawCircle(0.0f, 0.0f, 10.0f, n3d::colors::Yellow);
    n3d::DrawCircleV({0, 0}, 10.0f, n3d::colors::Yellow);
    n3d::DrawCircleLines({0, 0}, 10.0f, n3d::colors::Yellow);
    n3d::DrawCircleSector({0, 0}, 10.0f, 0.0f, 90.0f, n3d::colors::Yellow);
    n3d::DrawRing({0, 0}, 5.0f, 10.0f, 0.0f, 180.0f, n3d::colors::Yellow);
    n3d::DrawTriangle2D({0, 0}, {10, 0}, {0, 10}, n3d::colors::Cyan);
    n3d::DrawTriangleLines2D({0, 0}, {10, 0}, {0, 10}, n3d::colors::Cyan);
    n3d::DrawPolygon({0, 0}, 6, 20.0f, 0.0f, n3d::colors::Magenta);
    n3d::DrawPolygonLines({0, 0}, 6, 20.0f, 0.0f, n3d::colors::Magenta);
    const n3d::Vec2 points[3] = {{0, 0}, {10, 0}, {0, 10}};
    n3d::DrawPolygonV(points, 3, n3d::colors::Orange);
    n3d::DrawPolyline(points, 3, 2.0f, n3d::colors::Orange);
    n3d::EndMode2D();

    // --- README "texto" ---
    n3d::Font* font = n3d::GetDefaultFont();
    n3d::SetFont(font);
    n3d::DrawText("olá", 10, 10, 20.0f, n3d::colors::White);
    n3d::DrawTextV("olá", {10, 10}, 20.0f, n3d::colors::White);
    n3d::DrawTextCentered("olá", 100.0f, 50.0f, 20.0f, n3d::colors::White);
    n3d::DrawTextWithBackground("olá", 10, 10, 20.0f, n3d::colors::White, n3d::colors::Black);
    n3d::DrawTextFormat(10, 10, 20.0f, n3d::colors::White, "%d", 1);
    const n3d::Vec2 measured = n3d::MeasureText("olá", 20.0f);
    n3d::DrawText3D("olá", {0, 1, 0}, 0.05f, n3d::colors::White);
    n3d::UnloadFont(nullptr);   // null is a no-op

    // --- README "texturas / sprites" ---
    n3d::DrawTexture(tex, 0.0f, 0.0f, n3d::colors::White);
    n3d::DrawTextureV(tex, {0, 0}, n3d::colors::White);
    n3d::DrawTextureEx(tex, {0, 0}, 45.0f, 2.0f, n3d::colors::White);
    n3d::DrawTextureRec(tex, n3d::Rect{0, 0, 8, 8}, {0, 0}, n3d::colors::White);
    n3d::DrawTexturePro(tex, n3d::Rect{0, 0, 8, 8}, n3d::Rect{0, 0, 32, 32}, {0.5f, 0.5f}, 0.0f,
                        n3d::colors::White);

    // --- README "estado de shader" ---
    n3d::BeginShader(meu);
    n3d::DrawSphere({0, 1, 0}, 1.0f, n3d::colors::White);
    n3d::EndShader();

    // --- README "áudio" ---
    n3d::Sound* beep = n3d::LoadSound("beep.wav");
    n3d::Music* track = n3d::LoadMusic("music.wav");
    if (beep) {
        n3d::SetSoundVolume(beep, 0.5f);
        n3d::PlaySound(beep);
        const uint64_t voice = n3d::PlaySoundMulti(beep, 0.5f);
        n3d::StopSoundMulti(voice);
    }
    if (track) {
        n3d::SetMusicVolume(track, 0.4f);
        n3d::SetMusicLooping(track, true);
        n3d::SetMusicPan(track, -0.5f);
        n3d::PlayMusic(track);
        n3d::SeekMusic(track, 5.0f);
        (void)n3d::GetMusicTime(track);
        (void)n3d::GetMusicPan(track);
        (void)n3d::GetMusicPitch(track);
        (void)n3d::IsMusicLooping(track);
    }
    n3d::SetMasterVolume(0.8f);
    n3d::UnloadMusic(track);
    n3d::UnloadSound(beep);

    // --- README "vídeo" ---
    n3d::Video* clip = n3d::LoadVideo("clip.mp4");
    if (clip) {
        n3d::SetVideoLooping(clip, true);
        n3d::PlayVideo(clip);
        n3d::DrawVideoV(clip, {0, 0}, n3d::colors::White);
        (void)n3d::GetVideoWidth(clip);
    }
    n3d::UnloadVideo(clip);

    // --- README "modelos" ---
    n3d::Model* modelo = n3d::LoadModel("robot.obj");
    if (modelo) {
        n3d::DrawModel(modelo, {0, 0, 0});
        n3d::DrawModelEx(modelo, world, n3d::colors::White);
    }
    n3d::UnloadModel(modelo);

    // --- README "storage: a gaveta de dados" ---
    // The runtime form: paths declared once, referred to by name afterwards.
    n3d::Storage assets = {
        {"music", "assets/overworld.ogg"},
        {"hero", "assets/hero.obj"},
        {"sky", "assets/sky.hdr"},
    };
    assets.SetBaseDirectory("assets");
    (void)assets.Count();
    (void)assets.Contains("hero");
    (void)assets.Find("music");
    (void)assets.At(0);
    (void)assets["hero"].Path();
    (void)assets["sky"].Kind();
    (void)assets["sky"].ResolvedPath();

    // The named form: every file a static member, reachable as Game::music. The
    // drawer itself is declared at namespace scope, above main().
    (void)n3d::Game::music.Path();
    (void)n3d::Game::Get("hero");
    (void)n3d::Game::Count();
    n3d::Game::Drawer();
    n3d::Game::UnloadAll();

    n3d::Texture* gavetaTex = assets["sky"].AsTexture();
    n3d::Model* gavetaModel = assets["hero"].AsModel();
    n3d::Music* gavetaMusic = assets["music"].AsMusic();
    (void)gavetaTex;
    (void)gavetaModel;
    (void)gavetaMusic;
    assets.UnloadAll();
    n3d::LoadStorageManifest(assets, "assets/manifest.txt");
    (void)n3d::KindFromPath("a.png");
    (void)n3d::PathHasExtension("a.obj", "obj");
    (void)n3d::JoinPath("assets", "a.png");

    // --- README "n2d: o mesmo motor, grafia 2D" ---
    // n2d is an alias for n3d, so this is the same drawer under the 2D name.
    (void)n2d::Game::music.Path();

    (void)camera;
    (void)world;
    (void)mesh;
    (void)triangulo;
    (void)measured;
    return 0;
}
