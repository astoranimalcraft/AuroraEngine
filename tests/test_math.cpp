// Native (host-independent) tests for the portable math core.
// Build & run with scripts/test-math.sh — no DirectX required.
#include "nexium/math.h"

#include <cstdio>
#include <cstring>

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

static bool Near(float a, float b, float eps = 1e-4f) { return Abs(a - b) <= eps; }
static bool Near(Vec3 a, Vec3 b, float eps = 1e-4f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

static void TestVectors() {
    std::printf("vectors\n");
    Vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK(Near(Dot(a, b), 32.0f));
    CHECK(Near(Cross(a, b), Vec3{-3, 6, -3}));
    CHECK(Near(Length(Vec3{3, 4, 0}), 5.0f));
    CHECK(Near(Length(Normalize(Vec3{0, 0, 7})), 1.0f));
    CHECK(Near(Normalize(Vec3{0, 0, 0}), Vec3{0, 0, 0}));  // degenerate stays finite
    CHECK(Near(Reflect(Vec3{1, -1, 0}, Vec3{0, 1, 0}), Vec3{1, 1, 0}));
    CHECK(Near(Distance(Vec3{0, 0, 0}, Vec3{1, 1, 1}), std::sqrt(3.0f)));
}

static void TestMatrixLayout() {
    std::printf("matrix layout / storage\n");
    // Column-major: (row, col) at m[col*4+row]. Translation lives in column 3.
    Mat4 t = Translation({10, 20, 30});
    CHECK(Near(t.m[12], 10.0f));
    CHECK(Near(t.m[13], 20.0f));
    CHECK(Near(t.m[14], 30.0f));
    CHECK(Near(t.At(0, 3), 10.0f));
    CHECK(Near(t.At(3, 0), 0.0f));

    // Verify memcpy-ability: a Mat4 is exactly 16 tightly packed floats.
    CHECK(sizeof(Mat4) == 16 * sizeof(float));
    float raw[16];
    std::memcpy(raw, &t, sizeof(Mat4));
    CHECK(Near(raw[12], 10.0f));
}

static void TestTransformComposition() {
    std::printf("transform composition\n");
    Mat4 model = Compose({1, 0, 0}, {0, 0, 0}, {2, 2, 2});
    CHECK(Near(TransformPoint(model, {1, 0, 0}), Vec3{3, 0, 0}));

    // Rotation order is Y * X * Z: a 90° yaw sends +X to -Z.
    Mat4 yaw = RotationEuler({0, Radians(90), 0});
    CHECK(Near(TransformPoint(yaw, {1, 0, 0}), Vec3{0, 0, -1}));

    Mat4 pitch = RotationEuler({Radians(90), 0, 0});
    CHECK(Near(TransformPoint(pitch, {0, 1, 0}), Vec3{0, 0, 1}));

    // Direction vectors ignore translation.
    CHECK(Near(TransformDirection(Translation({100, 100, 100}), {1, 0, 0}), Vec3{1, 0, 0}));
}

static void TestMatrixMultiply() {
    std::printf("matrix multiply / associativity\n");
    Mat4 a = Translation({1, 2, 3});
    Mat4 b = Scale({2, 2, 2});
    // (T * S) applied to origin gives translation; scale must not affect it.
    CHECK(Near(TransformPoint(a * b, {0, 0, 0}), Vec3{1, 2, 3}));
    // (S * T) scales the translation.
    CHECK(Near(TransformPoint(b * a, {0, 0, 0}), Vec3{2, 4, 6}));

    Mat4 c = RotationY(Radians(37));
    Mat4 lhs = (a * b) * c;
    Mat4 rhs = a * (b * c);
    for (int i = 0; i < 16; ++i) CHECK(Near(lhs.m[i], rhs.m[i]));
}

static void TestInverse() {
    std::printf("inverse\n");
    Mat4 m = Compose({3, -2, 5}, {Radians(20), Radians(-40), Radians(10)}, {1.5f, 0.5f, 2.0f});
    Mat4 inv = Inverse(m);
    Mat4 id = m * inv;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            CHECK(Near(id.At(r, c), r == c ? 1.0f : 0.0f, 1e-3f));

    // Singular matrix must not produce NaNs.
    Mat4 singular = Scale({1, 0, 1});
    Mat4 si = Inverse(singular);
    for (int i = 0; i < 16; ++i) CHECK(si.m[i] == si.m[i]);
}

static void TestProjection() {
    std::printf("projection\n");
    Mat4 p = Perspective(Radians(60), 16.0f / 9.0f, 0.1f, 100.0f);
    // A point on the near plane maps to z = 0 in NDC (D3D depth range).
    Vec4 nearPt = p * Vec4(0, 0, -0.1f, 1.0f);
    CHECK(Near(nearPt.z / nearPt.w, 0.0f, 1e-3f));
    // A point on the far plane maps to z = 1.
    Vec4 farPt = p * Vec4(0, 0, -100.0f, 1.0f);
    CHECK(Near(farPt.z / farPt.w, 1.0f, 1e-3f));
    // w = -z (right-handed, camera looks down -Z).
    CHECK(Near(nearPt.w, 0.1f, 1e-4f));

    Mat4 o = Ortho(-1, 1, -1, 1, 0.1f, 10.0f);
    Vec4 oNear = o * Vec4(0, 0, -0.1f, 1.0f);
    CHECK(Near(oNear.z, 0.0f, 1e-3f));
}

static void TestLookAt() {
    std::printf("look at\n");
    Mat4 v = LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    // The origin sits 5 units in front of the camera, i.e. at view-space (0,0,-5).
    CHECK(Near(TransformPoint(v, {0, 0, 0}), Vec3{0, 0, -5}));
    CHECK(Near(TransformPoint(v, {0, 0, 5}), Vec3{0, 0, 0}));
}

static void TestQuat() {
    std::printf("quaternion\n");
    Quat q = QuatFromEuler({0, Radians(90), 0});
    CHECK(Near(Length(Vec3{q.x, q.y, q.z}), std::sin(Radians(45)), 1e-3f));
    CHECK(Near(q.w, std::cos(Radians(45)), 1e-3f));

    Quat a = QuatFromEuler({0, 0, 0});
    Quat b = QuatFromEuler({0, Radians(90), 0});
    Quat mid = Slerp(a, b, 0.5f);
    CHECK(Near(mid.y, std::sin(Radians(22.5f)), 1e-3f));
}

static void TestBounds() {
    std::printf("bounds\n");
    Bounds b;
    CHECK(!b.Valid());
    b.Grow({-1, -1, -1});
    b.Grow({1, 1, 1});
    CHECK(b.Valid());
    CHECK(Near(b.Center(), Vec3{0, 0, 0}));
    CHECK(Near(b.Extents(), Vec3{1, 1, 1}));
    CHECK(Near(b.Radius(), std::sqrt(3.0f)));
}

static void TestToEuler() {
    std::printf("to euler (inverse of RotationEuler)\n");

    // ToEuler() must invert RotationEuler() exactly, for any angle triple. An
    // earlier version used the wrong element order and only round-tripped when
    // one of the three angles was zero, so an arbitrary triple is the real test.
    const Vec3 cases[] = {
        {Radians(20), Radians(35), Radians(10)},
        {Radians(-40), Radians(120), Radians(-25)},
        {Radians(15), Radians(-70), Radians(80)},
        {0, 0, 0},
        {Radians(30), 0, 0},
        {0, Radians(90), 0},
        {0, 0, Radians(45)},
        {Radians(12), Radians(200), Radians(-160)},
    };

    for (const Vec3& euler : cases) {
        const Mat4 original = RotationEuler(euler);
        const Mat4 rebuilt = RotationEuler(ToEuler(original));
        for (int i = 0; i < 16; ++i) CHECK(Near(original.m[i], rebuilt.m[i], 1e-4f));
    }

    // The extracted angles must also match the input when it is already canonical
    // (pitch within +-90 degrees).
    const Vec3 euler{Radians(20), Radians(35), Radians(10)};
    const Vec3 back = ToEuler(RotationEuler(euler));
    CHECK(Near(back.x, euler.x, 1e-4f));
    CHECK(Near(back.y, euler.y, 1e-4f));
    CHECK(Near(back.z, euler.z, 1e-4f));

    // At the gimbal-lock pole the extraction must still produce a valid rotation
    // rather than NaNs.
    const Mat4 locked = RotationEuler({Radians(90), Radians(45), 0});
    const Mat4 lockedBack = RotationEuler(ToEuler(locked));
    for (int i = 0; i < 16; ++i) {
        CHECK(lockedBack.m[i] == lockedBack.m[i]);   // not NaN
        CHECK(Near(locked.m[i], lockedBack.m[i], 1e-4f));
    }
}

static void TestCollision() {
    std::printf("collision\n");

    // Spheres: touching counts, a gap does not.
    CHECK(CheckCollisionSpheres({0, 0, 0}, 1.0f, {1.5f, 0, 0}, 1.0f));
    CHECK(!CheckCollisionSpheres({0, 0, 0}, 1.0f, {2.5f, 0, 0}, 1.0f));

    // Boxes.
    Bounds a, b;
    a.Grow({0, 0, 0});
    a.Grow({2, 2, 2});
    b.Grow({1, 1, 1});
    b.Grow({3, 3, 3});
    CHECK(CheckCollisionBoxes(a, b));
    Bounds farAway;
    farAway.Grow({10, 10, 10});
    farAway.Grow({11, 11, 11});
    CHECK(!CheckCollisionBoxes(a, farAway));
    CHECK(!CheckCollisionBoxes(a, Bounds{}));   // invalid box never collides
    CHECK(CheckCollisionPointBox({1, 1, 1}, a));
    CHECK(!CheckCollisionPointBox({3, 1, 1}, a));

    // Ray vs. sphere: a forward hit reports the near intersection.
    Ray ray;
    ray.origin = {0, 0, -5};
    ray.direction = {0, 0, 1};
    float distance = -1.0f;
    CHECK(CheckCollisionRaySphere(ray, {0, 0, 0}, 1.0f, &distance));
    CHECK(Near(distance, 4.0f, 0.01f));
    CHECK(!CheckCollisionRaySphere(ray, {0.5f, 0, 0}, 0.4f));         // passes beside it
    CHECK(!CheckCollisionRaySphere(ray, {0, 0, -100}, 1.0f));         // behind the origin

    // Starting inside the sphere reports distance 0.
    ray.origin = {0, 0, 0};
    CHECK(CheckCollisionRaySphere(ray, {0, 0, 0}, 1.0f, &distance));
    CHECK(Near(distance, 0.0f));

    // Ray vs. box.
    ray.origin = {0, 0, -5};
    ray.direction = {0, 0, 1};
    CHECK(RayHitsBounds(ray, a, &distance));
    CHECK(Near(distance, 5.0f, 0.01f));
    CHECK(!RayHitsBounds(ray, farAway));

    // 2D circles and rectangles.
    CHECK(CheckCollisionCircles({0, 0}, 1.0f, {1.5f, 0}, 1.0f));
    CHECK(!CheckCollisionCircles({0, 0}, 1.0f, {3, 0}, 1.0f));
    CHECK(CheckCollisionPointCircle({0.5f, 0.5f}, {0, 0}, 1.0f));
    Rect rect{0, 0, 10, 10};
    CHECK(CheckCollisionPointRect({5, 5}, rect));
    CHECK(!CheckCollisionPointRect({15, 5}, rect));
    CHECK(CheckCollisionRecs(rect, Rect{5, 5, 10, 10}));
    CHECK(!CheckCollisionRecs(rect, Rect{20, 20, 5, 5}));

    // The overlap rectangle.
    const Rect overlap = GetCollisionRec(rect, Rect{5, 5, 10, 10});
    CHECK(Near(overlap.x, 5.0f));
    CHECK(Near(overlap.y, 5.0f));
    CHECK(Near(overlap.width, 5.0f));
    CHECK(Near(overlap.height, 5.0f));
    CHECK(GetCollisionRec(rect, Rect{20, 20, 5, 5}).width == 0.0f);

    // Segment-segment crossing.
    Vec2 point;
    CHECK(CheckCollisionLines({0, 0}, {10, 10}, {0, 10}, {10, 0}, &point));
    CHECK(Near(point.x, 5.0f, 0.01f) && Near(point.y, 5.0f, 0.01f));
    CHECK(!CheckCollisionLines({0, 0}, {1, 1}, {2, 2}, {3, 3}));        // parallel
    CHECK(!CheckCollisionLines({0, 0}, {1, 0}, {5, 5}, {6, 5}));        // apart

    // Point in a triangle, both windings.
    CHECK(CheckCollisionPointTriangle({0.25f, 0.25f}, {0, 0}, {1, 0}, {0, 1}));
    CHECK(CheckCollisionPointTriangle({0.25f, 0.25f}, {0, 0}, {0, 1}, {1, 0}));
    CHECK(!CheckCollisionPointTriangle({2, 2}, {0, 0}, {1, 0}, {0, 1}));

    // Closest point on a box, and the distance between two segments.
    CHECK(Near(ClosestPointOnBox({5, 1, 1}, a), Vec3{2, 1, 1}));
    CHECK(Near(ClosestPointOnBox({-5, -5, -5}, a), Vec3{0, 0, 0}));
    CHECK(Near(ClosestPointOnRect({-5, 5}, rect).x, 0.0f));
    CHECK(Near(ClosestPointOnRect({-5, 5}, rect).y, 5.0f));

    float s = -1.0f, t = -1.0f;
    // Two skew segments: the closest approach is 1 unit apart.
    const float distanceSq = ClosestPointsBetweenSegments({0, 0, 0}, {10, 0, 0},
                                                          {0, 0, 1}, {10, 0, 1}, &s, &t);
    CHECK(Near(distanceSq, 1.0f, 0.01f));
    CHECK(s >= 0.0f && s <= 1.0f && t >= 0.0f && t <= 1.0f);
}

int main() {
    TestVectors();
    TestMatrixLayout();
    TestTransformComposition();
    TestMatrixMultiply();
    TestInverse();
    TestProjection();
    TestLookAt();
    TestQuat();
    TestToEuler();
    TestBounds();
    TestCollision();

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
