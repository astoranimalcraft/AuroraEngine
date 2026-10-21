// NexiumEngine — mesh.h
// Meshes own GPU vertex/index buffers. Once uploaded they are immutable; create
// a new mesh to change geometry. Built-in primitives are cached internally and
// must not be freed by the application (UnloadMesh on them is a no-op).
#pragma once

#include <cstdint>

#include "nexium/color.h"
#include "nexium/math.h"

namespace n3d {

struct Vertex {
    Vec3  position{};
    Vec3  normal{0, 1, 0};
    Vec2  uv{};
    Color color{1, 1, 1, 1};
};

// `indices` may be null for non-indexed meshes. `flags` are the n3d::MeshFlags bits.
enum MeshFlags : uint32_t {
    MeshDefault  = 0,
    MeshNoCull   = 1u << 0,   // render both sides
    MeshUnlit    = 1u << 1,   // skip lighting
    MeshDynamic  = 1u << 2,   // hint: recreated often
    // Skips the shadow pass, so the mesh casts no shadow. The usual choice for a
    // sky box or a ground plane, whose self-shadowing would only add artefacts.
    MeshNoShadow = 1u << 3,
    // Engine-managed flag marking a cached built-in primitive. Set by the engine,
    // never by the application; UnloadMesh() refuses to free such a mesh.
    MeshBuiltin  = 1u << 31,
};

struct Mesh;

struct MeshDesc {
    const Vertex* vertices = nullptr;
    uint32_t vertexCount = 0;
    const uint32_t* indices = nullptr;
    uint32_t indexCount = 0;
    uint32_t flags = MeshDefault;
    const char* debugName = nullptr;
};

Mesh* CreateMesh(const MeshDesc& desc);
Mesh* CreateMesh(const Vertex* vertices, uint32_t vertexCount,
                 const uint32_t* indices = nullptr, uint32_t indexCount = 0);

void UnloadMesh(Mesh* mesh);

uint32_t MeshVertexCount(const Mesh* mesh);
uint32_t MeshIndexCount(const Mesh* mesh);
uint32_t MeshFlags(const Mesh* mesh);
Bounds MeshBounds(const Mesh* mesh);

// ------------------------------------------------------------- primitives ----
// Cached and shared: the same primitive requested twice returns the same mesh,
// so these must never be passed to UnloadMesh().
Mesh* GetCubeMesh(float size = 1.0f);
Mesh* GetSphereMesh(float radius = 0.5f, int rings = 16, int slices = 32);
Mesh* GetCylinderMesh(float radius = 0.5f, float height = 1.0f, int slices = 32);
Mesh* GetPlaneMesh(float width = 1.0f, float depth = 1.0f, int subdivisions = 1);
Mesh* GetConeMesh(float radius = 0.5f, float height = 1.0f, int slices = 32);
Mesh* GetTorusMesh(float majorRadius = 0.5f, float minorRadius = 0.2f,
                   int majorSegments = 32, int minorSegments = 16);
Mesh* GetPyramidMesh(float baseSize = 1.0f, float height = 1.0f);
Mesh* GetDiskMesh(float radius = 0.5f, int segments = 32);
// A capsule: a cylinder with a hemisphere on each end. `height` is the length of
// the straight section, so the total height is height + 2 * radius.
Mesh* GetCapsuleMesh(float radius = 0.5f, float height = 1.0f,
                     int slices = 24, int rings = 8);

} // namespace n3d
