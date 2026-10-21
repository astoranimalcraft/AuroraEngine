// Geometry generation extracted so it can be unit-tested on any host.
//
// The built-in primitives are the only place where winding order and normals are
// hand-authored, and getting them wrong shows up as invisible or inside-out
// meshes. Keeping the builders free of any D3D12 dependency means the Linux CI
// box can verify them, and mesh.cpp stays concerned only with uploading.
#pragma once

#include "nexium/math.h"
#include "nexium/mesh.h"

#include <cstdint>
#include <vector>

namespace n3d::geometry {

void BuildCube(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, float size);

void BuildSphere(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                 float radius, int rings, int slices);

// Shared by cylinders and cones: `topRadius` of 0 produces a cone. Caps are
// emitted only when `capped` is set.
void BuildTube(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
               float radius, float height, int slices, bool capped, float topRadius);

void BuildPlane(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                float width, float depth, int subdivisions);

// A ring with `majorRadius` from the centre to the middle of the tube and
// `minorRadius` as the tube's own radius, lying in the XZ plane.
void BuildTorus(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                float majorRadius, float minorRadius, int majorSegments, int minorSegments);

// A square base of `baseSize` in XZ with the apex on +Y, centred on the origin
// (base at -height/2, apex at +height/2) to match the other primitives.
void BuildPyramid(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                  float baseSize, float height);

// A flat disc in the XZ plane facing +Y, built as a triangle fan.
void BuildDisk(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
               float radius, int segments);

// A capsule: a cylinder of `height` capped by two hemispheres of `radius`, all
// centred on the origin. The total extent along Y is height + 2 * radius.
void BuildCapsule(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                  float radius, float height, int slices, int rings);

} // namespace n3d::geometry
