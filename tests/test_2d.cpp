// Native (host-independent) tests for the 2D camera math.
//
// Camera2D is header-only and free of DirectX, so its view/projection matrices and
// the screen<->world conversions can be checked on any host. Getting these wrong is
// invisible until a sprite lands in the wrong place, so they are worth a test.
#include "aurora/draw.h"

#include <cmath>
#include <cstdio>

using namespace a3d;

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++gChecks;                                                         \
        if (!(cond)) {                                                     \
            ++gFailures;                                                   \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                  \
    } while (0)

static bool Near(float a, float b, float eps = 1e-3f) { return Abs(a - b) <= eps; }
static bool Near(Vec2 a, Vec2 b, float eps = 1e-3f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps);
}

static void TestIdentityCamera() {
    std::printf("identity camera\n");
    // Target and offset both at the origin, no rotation, unit zoom: the camera
    // should be a no-op, so a world point maps to the same pixel.
    Camera2D camera;
    const float width = 1280.0f, height = 720.0f;

    CHECK(Near(camera.WorldToScreen(Vec2{0.0f, 0.0f}, width, height), Vec2{0.0f, 0.0f}));
    CHECK(Near(camera.WorldToScreen(Vec2{640.0f, 360.0f}, width, height), Vec2{640.0f, 360.0f}));
    CHECK(Near(camera.ScreenToWorld(Vec2{640.0f, 360.0f}, width, height), Vec2{640.0f, 360.0f}));
    CHECK(Near(camera.ScreenToWorld(Vec2{0.0f, 0.0f}, width, height), Vec2{0.0f, 0.0f}));
}

static void TestOffsetAndTarget() {
    std::printf("offset / target\n");
    const float width = 800.0f, height = 600.0f;

    // Target (100, 50) drawn at the screen centre.
    Camera2D camera;
    camera.target = Vec2{100.0f, 50.0f};
    camera.offset = Vec2{400.0f, 300.0f};
    CHECK(Near(camera.WorldToScreen(Vec2{100.0f, 50.0f}, width, height), Vec2{400.0f, 300.0f}));
    // A point 10 to the right and 20 down lands 10 right, 20 down on screen.
    CHECK(Near(camera.WorldToScreen(Vec2{110.0f, 70.0f}, width, height), Vec2{410.0f, 320.0f}));
    CHECK(Near(camera.ScreenToWorld(Vec2{410.0f, 320.0f}, width, height), Vec2{110.0f, 70.0f}));
}

static void TestZoom() {
    std::printf("zoom\n");
    const float width = 800.0f, height = 600.0f;
    Camera2D camera;
    camera.target = Vec2{0.0f, 0.0f};
    camera.offset = Vec2{400.0f, 300.0f};
    camera.zoom = 2.0f;

    // At 2x, 50 world units right of the target is 100 pixels right of the offset.
    CHECK(Near(camera.WorldToScreen(Vec2{50.0f, 0.0f}, width, height), Vec2{500.0f, 300.0f}));
    CHECK(Near(camera.ScreenToWorld(Vec2{500.0f, 300.0f}, width, height), Vec2{50.0f, 0.0f}));
}

static void TestRoundTrip() {
    std::printf("round trip\n");
    const float width = 1024.0f, height = 768.0f;

    // Arbitrary camera: a rotation, a zoom and an off-centre pivot.
    Camera2D camera;
    camera.target = Vec2{12.0f, -7.0f};
    camera.offset = Vec2{300.0f, 400.0f};
    camera.rotation = 33.0f;
    camera.zoom = 1.7f;

    const Vec2 samples[5] = {
        Vec2{0.0f, 0.0f}, Vec2{100.0f, -50.0f}, Vec2{-250.0f, 90.0f},
        Vec2{12.0f, -7.0f}, Vec2{3.5f, 22.25f},
    };
    for (const Vec2& world : samples) {
        const Vec2 screen = camera.WorldToScreen(world, width, height);
        CHECK(Near(camera.ScreenToWorld(screen, width, height), world, 5e-3f));
    }
}

int main() {
    TestIdentityCamera();
    TestOffsetAndTarget();
    TestZoom();
    TestRoundTrip();

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
