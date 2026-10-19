// Aurora3D — example: an orbiting cube over a grid, with a translucent sphere to
// exercise the transparency pass and a 2D overlay for the frame statistics.
//
// Build it with ./scripts/verify.sh, or by hand:
//   x86_64-w64-mingw32-g++ -std=c++20 -Iinclude -Isrc examples/01_hello_cube.cpp
//       src/*.cpp src/internal/*.cpp -ld3d12 -ldxgi -ld3dcompiler
//       -lwindowscodecs -lole32 -luuid -luser32 -lgdi32 -lshcore -o hello_cube.exe
#include <aurora/aurora.h>

#include <cmath>

namespace {

a3d::OrbitCamera gOrbit;

} // namespace

int main() {
    a3d::Config config;
    config.title = "Aurora3D — Hello Cube";
    config.width = 1280;
    config.height = 720;
    if (!a3d::Init(config)) return 1;

    gOrbit.distance = 9.0f;
    a3d::SetLightDirection(a3d::Vec3{-0.4f, -1.0f, -0.3f});
    a3d::SetAmbient(a3d::colors::White, 0.3f);

    const a3d::Mesh* cube = a3d::GetCubeMesh(1.0f);
    const a3d::Mesh* sphere = a3d::GetSphereMesh(1.0f);

    while (!a3d::WindowShouldClose()) {
        a3d::UpdateOrbitCamera(gOrbit);
        const a3d::Camera3D camera = gOrbit.ToCamera();

        a3d::BeginScene(camera, a3d::colors::SkyBlue);

        a3d::DrawGrid(10.0f, 20, a3d::colors::DarkGray);
        a3d::DrawAxes(a3d::Vec3{}, 2.0f);

        // A spinning cube, drawn through the mesh API to show the matrix form.
        const float angle = a3d::GetTime() * 45.0f;
        const a3d::Mat4 world = a3d::Compose(a3d::Vec3{0.0f, 1.0f, 0.0f},
                                             a3d::Vec3{0.0f, a3d::Radians(angle), 0.0f},
                                             a3d::Vec3{1.0f, 1.0f, 1.0f});
        a3d::DrawMeshEx((cube), world, a3d::colors::Orange);

        // A translucent sphere, so the back-to-front transparent pass is visible.
        a3d::SetBlendMode(a3d::BlendMode::AlphaBlend);
        a3d::DrawMeshMaterial((sphere), a3d::Vec3{2.5f, 1.0f, 0.0f},
                              a3d::GetDefaultTexture(), a3d::Vec3{},
                              a3d::Vec3{1.5f, 1.5f, 1.5f},
                              a3d::colors::Cyan.WithAlpha(0.45f), 0.4f, 0.2f);
        a3d::SetBlendMode(a3d::BlendMode::Opaque);

        a3d::DrawCone(a3d::Vec3{-2.5f, 0.0f, 0.0f}, 0.8f, 2.0f, a3d::colors::Lime);
        a3d::DrawCylinder(a3d::Vec3{0.0f, 1.0f, -2.5f}, 0.6f, 2.0f, a3d::colors::Purple);

        // Debug overlay: a couple of lines and a translucent panel.
        a3d::DrawWireSphere(a3d::Vec3{0.0f, 1.0f, 0.0f}, 1.5f, a3d::colors::Yellow);

        // 2D calls are queued into the overlay pass, which EndScene() draws last,
        // so they can sit anywhere inside the frame.
        a3d::DrawScreenRect(a3d::Vec2{12.0f, 12.0f}, a3d::Vec2{220.0f, 64.0f},
                            a3d::colors::Black.WithAlpha(0.35f));
        a3d::DrawScreenLine(a3d::Vec2{20.0f, 30.0f}, a3d::Vec2{220.0f, 30.0f},
                            a3d::colors::White.WithAlpha(0.8f), 2.0f);
        a3d::DrawScreenLine(a3d::Vec2{20.0f, 50.0f},
                            a3d::Vec2{20.0f + a3d::GetFps() * 2.0f, 50.0f}, a3d::colors::Lime,
                            4.0f);
        a3d::EndScene();
    }

    a3d::Close();
    return 0;
}
