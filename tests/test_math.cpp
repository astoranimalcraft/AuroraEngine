// Native (host-independent) tests for the portable math core.
// Build & run with scripts/test-math.sh — no DirectX required.
#include "aurora/math.h"

#include <cstdio>
#include <cstring>

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

int main() {
    TestVectors();
    TestMatrixLayout();
    TestTransformComposition();
    TestMatrixMultiply();
    TestInverse();
    TestProjection();
    TestLookAt();
    TestQuat();
    TestBounds();

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
