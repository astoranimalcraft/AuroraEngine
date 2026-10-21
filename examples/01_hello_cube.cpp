// NexiumEngine — example: an orbiting cube over a grid, with a translucent sphere to
// exercise the transparency pass and a 2D overlay for the frame statistics.
//
// Build it with ./scripts/verify.sh, or by hand:
//   x86_64-w64-mingw32-g++ -std=c++20 -Iinclude -Isrc examples/01_hello_cube.cpp
//       src/*.cpp src/internal/*.cpp -ld3d12 -ldxgi -ld3dcompiler
//       -lwindowscodecs -lole32 -luuid -luser32 -lgdi32 -lshcore -o hello_cube.exe
#include <nexium/NexiumEngine.h>

#include <cmath>

namespace {

n3d::OrbitCamera gOrbit;

} // namespace

int main() {
    n3d::Config config;
    config.title = "NexiumEngine — Hello Cube";
    config.width = 1280;
    config.height = 720;
    if (!n3d::Init(config)) return 1;

    gOrbit.distance = 9.0f;
    n3d::SetLightDirection(n3d::Vec3{-0.4f, -1.0f, -0.3f});
    n3d::SetAmbient(n3d::colors::White, 0.3f);

    const n3d::Mesh* cube = n3d::GetCubeMesh(1.0f);
    const n3d::Mesh* sphere = n3d::GetSphereMesh(1.0f);

    while (!n3d::WindowShouldClose()) {
        n3d::UpdateOrbitCamera(gOrbit);
        const n3d::Camera3D camera = gOrbit.ToCamera();

        n3d::BeginScene(camera, n3d::colors::SkyBlue);

        n3d::DrawGrid(10.0f, 20, n3d::colors::DarkGray);
        n3d::DrawAxes(n3d::Vec3{}, 2.0f);

        // A spinning cube, drawn through the mesh API to show the matrix form.
        const float angle = n3d::GetTime() * 45.0f;
        const n3d::Mat4 world = n3d::Compose(n3d::Vec3{0.0f, 1.0f, 0.0f},
                                             n3d::Vec3{0.0f, n3d::Radians(angle), 0.0f},
                                             n3d::Vec3{1.0f, 1.0f, 1.0f});
        n3d::DrawMeshEx((cube), world, n3d::colors::Orange);

        // A translucent sphere, so the back-to-front transparent pass is visible.
        n3d::SetBlendMode(n3d::BlendMode::AlphaBlend);
        n3d::DrawMeshMaterial((sphere), n3d::Vec3{2.5f, 1.0f, 0.0f},
                              n3d::GetDefaultTexture(), n3d::Vec3{},
                              n3d::Vec3{1.5f, 1.5f, 1.5f},
                              n3d::colors::Cyan.WithAlpha(0.45f), 0.4f, 0.2f);
        n3d::SetBlendMode(n3d::BlendMode::Opaque);

        n3d::DrawCone(n3d::Vec3{-2.5f, 0.0f, 0.0f}, 0.8f, 2.0f, n3d::colors::Lime);
        n3d::DrawCylinder(n3d::Vec3{0.0f, 1.0f, -2.5f}, 0.6f, 2.0f, n3d::colors::Purple);

        // Debug overlay: a couple of lines and a translucent panel.
        n3d::DrawWireSphere(n3d::Vec3{0.0f, 1.0f, 0.0f}, 1.5f, n3d::colors::Yellow);

        // 2D calls are queued into the overlay pass, which EndScene() draws last,
        // so they can sit anywhere inside the frame.
        n3d::DrawScreenRect(n3d::Vec2{12.0f, 12.0f}, n3d::Vec2{220.0f, 64.0f},
                            n3d::colors::Black.WithAlpha(0.35f));
        n3d::DrawScreenLine(n3d::Vec2{20.0f, 30.0f}, n3d::Vec2{220.0f, 30.0f},
                            n3d::colors::White.WithAlpha(0.8f), 2.0f);
        n3d::DrawScreenLine(n3d::Vec2{20.0f, 50.0f},
                            n3d::Vec2{20.0f + n3d::GetFps() * 2.0f, 50.0f}, n3d::colors::Lime,
                            4.0f);
        n3d::EndScene();
    }

    n3d::Close();
    return 0;
}
