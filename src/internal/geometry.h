// Geometry generation extracted so it can be unit-tested on any host.
//
// The built-in primitives are the only place where winding order and normals are
// hand-authored, and getting them wrong shows up as invisible or inside-out
// meshes. Keeping the builders free of any D3D12 dependency means the Linux CI
// box can verify them, and mesh.cpp stays concerned only with uploading.
#pragma once

#include "aurora/math.h"
#include "aurora/mesh.h"

#include <cstdint>
#include <vector>

namespace a3d::geometry {

void BuildCube(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, float size);

void BuildSphere(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                 float radius, int rings, int slices);

// Shared by cylinders and cones: `topRadius` of 0 produces a cone. Caps are
// emitted only when `capped` is set.
void BuildTube(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
               float radius, float height, int slices, bool capped, float topRadius);

void BuildPlane(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                float width, float depth, int subdivisions);

} // namespace a3d::geometry
