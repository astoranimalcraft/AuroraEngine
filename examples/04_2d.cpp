// NexiumEngine — example: a pure 2D frame.
//
// Shows the 2D half of the API: a Camera2D you can pan and zoom, the shape
// helpers, the built-in bitmap font, and DrawTexturePro for a rotated sprite.
// The whole frame is 2D, so it uses BeginSceneOverlay() and never touches 3D.
#include <nexium/NexiumEngine.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

n3d::Camera2D gCamera;

} // namespace

int main() {
    n3d::Config config;
    config.title = "NexiumEngine — 2D";
    config.width = 1280;
    config.height = 720;
    if (!n3d::Init(config)) return 1;

    // A texture to draw as a sprite: a small checkerboard built in memory.
    const int size = 64;
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4, 255);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            const uint8_t value = light ? 255 : 90;
            uint8_t* texel = &pixels[(static_cast<size_t>(y) * size + x) * 4];
            texel[0] = value;
            texel[1] = value;
            texel[2] = value;
            texel[3] = 255;
        }
    }
    n3d::TextureDesc spriteDesc;
    spriteDesc.width = size;
    spriteDesc.height = size;
    spriteDesc.pixels = pixels.data();
    n3d::Texture* sprite = n3d::LoadTexture(spriteDesc);

    // Centre the camera on the middle of the screen and let it zoom.
    gCamera.target = n3d::Vec2{640.0f, 360.0f};
    gCamera.offset = n3d::Vec2{640.0f, 360.0f};

    while (!n3d::WindowShouldClose()) {
        // Drag with the right mouse button to pan; the wheel zooms.
        if (n3d::IsMouseButtonDown(n3d::MouseButton::Right)) {
            const n3d::Vec2 delta = n3d::GetMouseDelta();
            gCamera.target.x -= delta.x / gCamera.zoom;
            gCamera.target.y -= delta.y / gCamera.zoom;
        }
        gCamera.zoom += n3d::GetMouseWheelDelta() * 0.1f;
        gCamera.zoom = n3d::Clamp(gCamera.zoom, 0.25f, 4.0f);
        gCamera.rotation += 20.0f * n3d::GetFrameTime();

        n3d::BeginSceneOverlay(n3d::Camera3D{});
        n3d::BeginMode2D(gCamera);

        // A ring of rotating shapes around the origin.
        for (int i = 0; i < 12; ++i) {
            const float angle = n3d::GetTime() * 0.6f + static_cast<float>(i) * n3d::TAU / 12.0f;
            const n3d::Vec2 centre{std::cos(angle) * 220.0f, std::sin(angle) * 160.0f};
            const n3d::Color color = n3d::Color::FromBytes(80 + i * 12, 200 - i * 10, 255, 255);
            n3d::DrawPolygon(centre, 3 + i % 5, 26.0f, n3d::Degrees(angle), color);
            n3d::DrawCircleLines(centre, 34.0f, color.WithAlpha(0.5f));
        }

        n3d::DrawRectangleGradient(n3d::Rect{-320.0f, -60.0f, 640.0f, 120.0f},
                                   n3d::colors::DarkBlue, n3d::colors::Magenta);
        n3d::DrawRectangleLinesEx(n3d::Rect{-320.0f, -60.0f, 640.0f, 120.0f}, 3.0f,
                                  n3d::colors::White);
        n3d::DrawRing(n3d::Vec2{}, 90.0f, 120.0f, 0.0f, 300.0f, n3d::colors::Gold);

        n3d::DrawTextCentered("NexiumEngine", 0.0f, -20.0f, 34.0f, n3d::colors::White);
        n3d::DrawTextFormat(-150, 10, 18.0f, n3d::colors::LightGray,
                            "zoom %.2fx   fps %.0f", gCamera.zoom, n3d::GetFps());

        // A sprite under the 2D camera: position and size are world units.
        if (sprite) {
            n3d::DrawTexturePro(sprite, n3d::Rect{0, 0, 64, 64}, n3d::Rect{-32, -260, 64, 64},
                                n3d::Vec2{0.5f, 0.5f}, n3d::GetTime() * 90.0f,
                                n3d::colors::White);
        }

        n3d::EndMode2D();

        // Screen-space HUD, drawn in pixels on top of the 2D world.
        n3d::DrawRectangle(10.0f, 10.0f, 260.0f, 46.0f, n3d::colors::Black.WithAlpha(0.4f));
        n3d::DrawText("right-drag pan / wheel zoom", 18, 18, 16.0f, n3d::colors::White);
        n3d::DrawTextFormat(18, 36, 16.0f, n3d::colors::Lime, "mouse %.0f, %.0f",
                            static_cast<double>(n3d::GetMouseX()),
                            static_cast<double>(n3d::GetMouseY()));

        n3d::EndScene();
    }

    n3d::UnloadTexture(sprite);
    n3d::Close();
    return 0;
}
