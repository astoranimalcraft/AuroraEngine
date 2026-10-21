// Native tests for the camera helpers.
//
// Camera3D, FpsCamera, Frustum and CameraShake are header-only and free of
// DirectX, so the projection, culling and orientation maths can be checked on any
// host. These are the pieces where a sign or an axis error stays invisible until
// something is culled that should be drawn, or a click picks the wrong object.

#include "nexium/camera.h"

#include <cstdio>

using namespace n3d;

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
static bool Near(Vec3 a, Vec3 b, float eps = 1e-3f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

int main() {
    const float width = 1280.0f, height = 720.0f;

    // --- projection round trip: a point on the centre line projects to the centre
    {
        std::printf("world<->screen\n");
        Camera3D cam;
        cam.position = {0, 0, -5};
        cam.target = {0, 0, 0};
        const Vec3 centre = cam.WorldToScreen({0, 0, 0}, width, height);
        CHECK(Near(centre.x, width * 0.5f, 0.01f));
        CHECK(Near(centre.y, height * 0.5f, 0.01f));

        // Right-handed engine: the camera at -Z looking at +Z sees world +X on
        // its LEFT (screen x decreases). Up is unaffected.
        const Vec3 plusX = cam.WorldToScreen({1, 0, 0}, width, height);
        CHECK(plusX.x < centre.x);
        CHECK(Near(plusX.y, centre.y, 0.01f));

        const Vec3 above = cam.WorldToScreen({0, 1, 0}, width, height);
        CHECK(above.y < centre.y);
        CHECK(Near(above.x, centre.x, 0.01f));

        // A point behind the camera is rejected with z = -1.
        const Vec3 behind = cam.WorldToScreen({0, 0, -10}, width, height);
        CHECK(Near(behind.z, -1.0f));
    }

    // --- ray through a screen pixel hits what the pixel shows
    {
        std::printf("ray through screen\n");
        Camera3D cam;
        cam.position = {0, 0, -5};
        cam.target = {0, 0, 0};

        // Centre pixel -> ray down +Z from the near plane, pointing at the origin.
        const Ray centreRay = cam.RayThroughScreen({width * 0.5f, height * 0.5f}, width, height);
        CHECK(Near(centreRay.direction, Vec3{0, 0, 1}, 0.01f));

        // A pixel to the right of centre aims toward world -X, because the
        // camera at -Z looking at +Z has world +X on its left.
        const Ray rightRay = cam.RayThroughScreen({width * 0.9f, height * 0.5f}, width, height);
        CHECK(rightRay.direction.x < -0.1f);

        // A pixel above centre must aim up (+y).
        const Ray upRay = cam.RayThroughScreen({width * 0.5f, height * 0.1f}, width, height);
        CHECK(upRay.direction.y > 0.1f);

        // The ray must pass through the sphere at the origin.
        float distance = 0.0f;
        CHECK(CheckCollisionRaySphere(centreRay, Vec3{0, 0, 0}, 0.5f, &distance));
        CHECK(distance > 4.0f && distance < 5.0f);
    }

    // --- frustum culling
    {
        std::printf("frustum\n");
        Camera3D cam;
        cam.position = {0, 0, -5};
        cam.target = {0, 0, 0};
        const Frustum frustum = GetCameraFrustum(cam, width / height);

        CHECK(frustum.ContainsPoint({0, 0, 0}));            // straight ahead
        CHECK(frustum.ContainsPoint({0, 0, 5}));            // further ahead
        CHECK(!frustum.ContainsPoint({0, 0, -10}));         // behind
        CHECK(!frustum.ContainsPoint({100, 0, 0}));         // far to the side
        CHECK(!frustum.ContainsPoint({0, 100, 0}));         // far above
        CHECK(!frustum.ContainsPoint({0, 0, -10000}));      // beyond the far plane

        CHECK(frustum.ContainsSphere({0, 0, 0}, 0.5f));
        CHECK(!frustum.ContainsSphere({100, 0, 0}, 0.5f));
        // A sphere whose centre is outside the right plane but which reaches in
        // must still be kept: the test is conservative on purpose.
        const float reach = -6.0f;   // beyond the right plane in world space
        CHECK(frustum.ContainsSphere({reach, 0, 0}, 2.0f));
        CHECK(!frustum.ContainsSphere({reach, 0, 0}, 0.1f));

        Bounds visible;
        visible.Grow({-1, -1, -1});
        visible.Grow({1, 1, 1});
        CHECK(frustum.ContainsBounds(visible));

        Bounds hidden;
        hidden.Grow({99, -1, -1});
        hidden.Grow({101, 1, 1});
        CHECK(!frustum.ContainsBounds(hidden));
    }

    // --- FPS camera
    {
        std::printf("fps camera\n");
        FpsCamera fps;
        fps.yaw = 0.0f;
        fps.pitch = 0.0f;
        CHECK(Near(fps.Forward(), Vec3{0, 0, 1}, 0.01f));   // yaw 0 looks toward +Z

        // Turning yaw by +90 degrees must look toward +X.
        fps.yaw = Radians(90.0f);
        CHECK(Near(fps.Forward(), Vec3{1, 0, 0}, 0.01f));

        // Pitch up must raise the forward vector's y.
        fps.yaw = 0.0f;
        fps.pitch = Radians(45.0f);
        CHECK(fps.Forward().y > 0.6f);

        // Look() clamps pitch so the camera can never flip over.
        fps.pitch = 0.0f;
        for (int i = 0; i < 1000; ++i) fps.Look(Vec2{0.0f, -100.0f});
        CHECK(fps.pitch <= fps.maxPitch + 1e-5f);

        // Walking forward at yaw 90 must move along +X and never change y.
        fps.yaw = Radians(90.0f);
        fps.pitch = 0.0f;
        const Vec3 before = fps.position;
        fps.Walk(Vec2{0.0f, 1.0f}, 1.0f);
        CHECK(fps.position.x > before.x);
        CHECK(Near(fps.position.y, before.y));
    }

    // --- mouse look directions, checked against what the screen actually shows
    {
        std::printf("fps look axes\n");

        // Mouse right must turn the view right, which slides a fixed world point
        // left across the screen. Reading the sign of yaw is not enough: the
        // engine is right-handed, so "look right" is a *decreasing* yaw. Getting
        // this backwards is the classic inverted-axis bug, and it looks fine
        // until someone plays it.
        FpsCamera fps;
        fps.position = {0, 0, 0};
        const Vec3 point{2, 0, 10};
        const float x0 = fps.ToCamera().WorldToScreen(point, width, height).x;
        fps.Look(Vec2{100.0f, 0.0f});
        const float x1 = fps.ToCamera().WorldToScreen(point, width, height).x;
        CHECK(x1 < x0);

        // Mouse up (negative delta.y) must look up, dropping a point that sits
        // above the horizon further down the screen.
        FpsCamera up;
        up.position = {0, 0, 0};
        const Vec3 high{0, 5, 10};
        const float y0 = up.ToCamera().WorldToScreen(high, width, height).y;
        up.Look(Vec2{0.0f, -100.0f});
        const float y1 = up.ToCamera().WorldToScreen(high, width, height).y;
        CHECK(y1 > y0);

        // Right() must agree with the screen: a point offset along it lands on
        // the right half of the viewport, and D walks along it.
        FpsCamera strafe;
        strafe.position = {0, 0, 0};
        const Camera3D cam = strafe.ToCamera();
        const Vec3 probe = strafe.Right() * 2.0f + strafe.Forward() * 10.0f;
        CHECK(cam.WorldToScreen(probe, width, height).x > width * 0.5f);

        const Vec3 start = strafe.position;
        strafe.Walk(Vec2{1.0f, 0.0f}, 1.0f);
        CHECK(Dot(strafe.position - start, strafe.Right()) > 0.0f);
    }

    // --- orbit keeps the distance and moves the eye
    {
        std::printf("camera orbit/zoom/move\n");
        Camera3D cam;
        cam.position = {0, 0, -10};
        cam.target = {0, 0, 0};
        const float radius = Length(cam.position - cam.target);

        cam.Orbit(Radians(90.0f), 0.0f);
        CHECK(Near(Length(cam.position - cam.target), radius, 0.01f));
        CHECK(!Near(cam.position, Vec3{0, 0, -10}, 0.5f));   // it actually moved

        // Orbit pitch is clamped, so repeated pitches never reach the pole.
        for (int i = 0; i < 200; ++i) cam.Orbit(0.0f, Radians(30.0f));
        const Vec3 offset = Normalize(cam.position - cam.target);
        CHECK(Abs(offset.y) < 0.9999f);

        cam.position = {0, 0, -10};
        cam.target = {0, 0, 0};
        cam.Zoom(2.0f);
        CHECK(Near(Length(cam.position - cam.target), 8.0f, 0.01f));

        cam.Move({1, 2, 3});
        CHECK(Near(cam.target, Vec3{1, 2, 3}));
    }

    // --- blending two cameras
    {
        std::printf("camera lerp\n");
        Camera3D a;
        a.position = {0, 0, -10};
        a.target = {0, 0, 0};
        Camera3D b;
        b.position = {0, 10, -10};
        b.target = {0, 5, 0};

        const Camera3D mid = LerpCamera(a, b, 0.5f);
        CHECK(Near(mid.position, Vec3{0, 5, -10}));
        CHECK(Near(mid.target, Vec3{0, 2.5f, 0}));

        CHECK(Near(LerpCamera(a, b, 0.0f).position, a.position));
        CHECK(Near(LerpCamera(a, b, 1.0f).position, b.position));
    }

    // --- camera shake decays to nothing
    {
        std::printf("camera shake\n");
        CameraShake shake;
        CHECK(Near(shake.Offset().x, 0.0f));
        shake.Add(1.0f);
        CHECK(shake.Amplitude() > 0.9f);
        for (int i = 0; i < 100; ++i) shake.Update(0.1f);
        CHECK(Near(shake.trauma, 0.0f));
        CHECK(Near(shake.Offset().x, 0.0f));
    }

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
