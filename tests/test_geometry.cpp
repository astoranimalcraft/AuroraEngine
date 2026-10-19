// Native tests for the built-in primitive geometry.
//
// These run on any host (no DirectX needed). They exist because winding order and
// normal direction are the two mistakes that turn a mesh invisible or inside-out,
// and neither is visible from reading the builder code.
#include "internal/geometry.h"

#include <cmath>
#include <cstdio>
#include <vector>

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


// Every triangle must be front-facing (its geometric normal agrees with the
// interpolated vertex normals) and no index may be out of range. Zero-area
// triangles are skipped: the sphere poles and the cone apex legitimately
// collapse to a point, and such a triangle has no facing at all.
static void CheckMeshIntegrity(const char* name, const std::vector<Vertex>& vertices,
                               const std::vector<uint32_t>& indices, bool expectOutward) {
    CHECK(!vertices.empty());
    CHECK(!indices.empty());
    CHECK(indices.size() % 3 == 0);

    uint32_t flipped = 0;
    uint32_t degenerate = 0;
    for (size_t i = 0; i < indices.size(); i += 3) {
        const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) {
            CHECK(false && "index out of range");
            return;
        }

        const Vec3 cross = Cross(vertices[b].position - vertices[a].position,
                                 vertices[c].position - vertices[a].position);
        if (LengthSq(cross) < 1e-12f) {
            ++degenerate;
            continue;
        }

        const Vec3 geometric = Normalize(cross);
        const Vec3 shading = Normalize(vertices[a].normal + vertices[b].normal + vertices[c].normal);
        if (Dot(geometric, shading) < 0.0f) ++flipped;
    }

    if (expectOutward) {
        if (flipped > 0)
            std::printf("  %s: %u of %zu triangles are wound inside-out\n", name, flipped,
                        indices.size() / 3);
        CHECK(flipped == 0);
    }
    // A cap that is all slivers would mean the winding check never ran.
    CHECK(degenerate < indices.size() / 3);
}

static void CheckUvRange(const char* name, const std::vector<Vertex>& vertices) {
    for (const Vertex& v : vertices) {
        if (v.uv.x < -1e-3f || v.uv.x > 1.0f + 1e-3f || v.uv.y < -1e-3f || v.uv.y > 1.0f + 1e-3f) {
            std::printf("  %s: UV out of [0,1]: (%f, %f)\n", name, v.uv.x, v.uv.y);
            CHECK(false);
            return;
        }
    }
}

static void TestCube() {
    std::printf("cube\n");
    std::vector<Vertex> v;
    std::vector<uint32_t> i;
    geometry::BuildCube(v, i, 2.0f);

    CHECK(v.size() == 24);          // 6 faces x 4 vertices
    CHECK(i.size() == 36);          // 6 faces x 2 triangles x 3
    CheckMeshIntegrity("cube", v, i, true);
    CheckUvRange("cube", v);

    // Half-extent must be exactly 1 for size = 2.
    for (const Vertex& vertex : v) {
        CHECK(Near(Abs(vertex.position.x), 1.0f) || Near(Abs(vertex.position.y), 1.0f) ||
              Near(Abs(vertex.position.z), 1.0f));
    }

    // Face normals must be axis-aligned unit vectors.
    for (const Vertex& vertex : v) CHECK(Near(Length(vertex.normal), 1.0f));
}

static void TestSphere() {
    std::printf("sphere\n");
    std::vector<Vertex> v;
    std::vector<uint32_t> i;
    geometry::BuildSphere(v, i, 2.5f, 16, 24);

    CHECK(v.size() == static_cast<size_t>(17 * 25));
    CheckMeshIntegrity("sphere", v, i, true);
    CheckUvRange("sphere", v);

    // Every vertex sits exactly on the sphere and its normal is the outward
    // radial direction.
    for (const Vertex& vertex : v) {
        CHECK(Near(Length(vertex.position), 2.5f));
        CHECK(Near(Dot(Normalize(vertex.position), vertex.normal), 1.0f));
    }
}

static void TestCylinder() {
    std::printf("cylinder\n");
    std::vector<Vertex> v;
    std::vector<uint32_t> i;
    geometry::BuildTube(v, i, 1.0f, 2.0f, 24, true, 1.0f);

    CheckMeshIntegrity("cylinder", v, i, true);
    CheckUvRange("cylinder", v);

    // A straight wall means the side normals have no vertical component.
    int sideNormals = 0;
    for (const Vertex& vertex : v) {
        if (Near(Abs(vertex.normal.y), 0.0f)) ++sideNormals;
    }
    CHECK(sideNormals == 2 * 25);   // two rings of slices + 1

    // The caps must face straight up and straight down.
    int upCaps = 0, downCaps = 0;
    for (const Vertex& vertex : v) {
        if (Near(vertex.normal.y, 1.0f)) ++upCaps;
        if (Near(vertex.normal.y, -1.0f)) ++downCaps;
    }
    CHECK(upCaps > 0);
    CHECK(downCaps > 0);
}

static void TestCone() {
    std::printf("cone\n");
    std::vector<Vertex> v;
    std::vector<uint32_t> i;
    geometry::BuildTube(v, i, 1.0f, 2.0f, 24, true, 0.0f);

    CheckMeshIntegrity("cone", v, i, true);
    CheckUvRange("cone", v);

    // The apex ring collapses to a point, so those vertices are at (0, h/2, 0).
    int apex = 0;
    for (const Vertex& vertex : v) {
        if (Near(vertex.position.x, 0.0f) && Near(vertex.position.z, 0.0f) &&
            Near(vertex.position.y, 1.0f))
            ++apex;
    }
    CHECK(apex == 25);

    // Cone side normals must tilt upward.
    for (const Vertex& vertex : v) {
        if (!Near(Abs(vertex.normal.y), 1.0f)) CHECK(vertex.normal.y > 0.0f);
    }
}

static void TestPlane() {
    std::printf("plane\n");
    std::vector<Vertex> v;
    std::vector<uint32_t> i;
    geometry::BuildPlane(v, i, 4.0f, 6.0f, 2);

    CHECK(v.size() == 9);           // 3x3 grid
    CHECK(i.size() == 24);          // 4 quads x 2 triangles x 3
    CheckMeshIntegrity("plane", v, i, true);
    CheckUvRange("plane", v);

    for (const Vertex& vertex : v) {
        CHECK(Near(vertex.position.y, 0.0f));
        CHECK(Near(vertex.normal.y, 1.0f));
    }
}

static void TestDegenerateInputs() {
    std::printf("degenerate parameters\n");
    std::vector<Vertex> v;
    std::vector<uint32_t> i;

    // Out-of-range tessellation must be clamped rather than looping forever.
    geometry::BuildSphere(v, i, 1.0f, 0, 0);
    CHECK(!v.empty());
    CheckMeshIntegrity("sphere(clamped)", v, i, true);

    v.clear();
    i.clear();
    geometry::BuildTube(v, i, 1.0f, 1.0f, 0, true, 1.0f);
    CHECK(!v.empty());
    CheckMeshIntegrity("cylinder(clamped)", v, i, true);

    v.clear();
    i.clear();
    geometry::BuildPlane(v, i, 1.0f, 1.0f, 0);
    CHECK(!v.empty());
    CheckMeshIntegrity("plane(clamped)", v, i, true);
}

int main() {
    TestCube();
    TestSphere();
    TestCylinder();
    TestCone();
    TestPlane();
    TestDegenerateInputs();

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
