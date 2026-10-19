// Aurora3D — built-in primitive geometry.
//
// Kept free of any D3D12 dependency so the builders can be unit-tested on any
// host: winding order and normals are hand-authored here, and getting them wrong
// shows up as invisible or inside-out meshes.
#include "internal/geometry.h"

#include <cmath>

namespace a3d::geometry {

void BuildCube(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, float size) {
    const float h = size * 0.5f;

    // Six faces, each with its own vertices so the normals and UVs are correct.
    struct Face {
        Vec3 normal, tangent, bitangent;
    };
    const Face faces[6] = {
        {{ 0,  0, -1}, { 1, 0, 0}, {0, 1, 0}},   // front (-Z)
        {{ 0,  0,  1}, {-1, 0, 0}, {0, 1, 0}},   // back  (+Z)
        {{-1,  0,  0}, { 0, 0,-1}, {0, 1, 0}},   // left
        {{ 1,  0,  0}, { 0, 0, 1}, {0, 1, 0}},   // right
        {{ 0,  1,  0}, { 1, 0, 0}, {0, 0,-1}},   // top
        {{ 0, -1,  0}, { 1, 0, 0}, {0, 0, 1}},   // bottom
    };

    for (const Face& face : faces) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        const Vec3 center = face.normal * h;

        const Vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const Vec2& corner : corners) {
            Vertex vertex;
            vertex.position = center + face.tangent * (corner.x * h) + face.bitangent * (corner.y * h);
            vertex.normal = face.normal;
            vertex.uv = Vec2{(corner.x + 1.0f) * 0.5f, 1.0f - (corner.y + 1.0f) * 0.5f};
            vertex.color = colors::White;
            vertices.push_back(vertex);
        }

        // The corner order is counter-clockwise as seen from outside only when
        // (tangent x bitangent) points along the face normal. Rather than relying
        // on every entry of the table above being hand-correct, the winding is
        // derived from that product, which makes the builder self-checking.
        const bool counterClockwise =
            Dot(Cross(face.tangent, face.bitangent), face.normal) > 0.0f;

        if (counterClockwise) {
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        } else {
            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 1);
            indices.push_back(base + 0);
            indices.push_back(base + 3);
            indices.push_back(base + 2);
        }
    }
}

void BuildSphere(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                 float radius, int rings, int slices) {
    rings = rings < 2 ? 2 : rings;
    slices = slices < 3 ? 3 : slices;

    for (int ring = 0; ring <= rings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float phi = v * PI;                 // 0 at the north pole
        const float y = std::cos(phi);
        const float r = std::sin(phi);

        for (int slice = 0; slice <= slices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * TAU;

            Vertex vertex;
            const Vec3 normal{r * std::sin(theta), y, r * std::cos(theta)};
            vertex.position = normal * radius;
            vertex.normal = normal;
            vertex.uv = Vec2{u, v};
            vertex.color = colors::White;
            vertices.push_back(vertex);
        }
    }

    const uint32_t stride = static_cast<uint32_t>(slices) + 1;
    for (int ring = 0; ring < rings; ++ring) {
        for (int slice = 0; slice < slices; ++slice) {
            const uint32_t a = static_cast<uint32_t>(ring) * stride + static_cast<uint32_t>(slice);
            const uint32_t b = a + stride;

            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(a + 1);
            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(b + 1);
        }
    }
}

void BuildTube(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                   float radius, float height, int slices, bool capped, float topRadius) {
    slices = slices < 3 ? 3 : slices;
    const float halfHeight = height * 0.5f;

    // Side wall.
    for (int slice = 0; slice <= slices; ++slice) {
        const float u = static_cast<float>(slice) / static_cast<float>(slices);
        const float theta = u * TAU;
        const float cosTheta = std::cos(theta), sinTheta = std::sin(theta);

        // The normal follows the slope of the wall so cones shade correctly.
        const float slope = (radius - topRadius) / height;
        Vec3 normal = Normalize(Vec3{cosTheta, slope, sinTheta});

        Vertex bottom;
        bottom.position = Vec3{radius * cosTheta, -halfHeight, radius * sinTheta};
        bottom.normal = normal;
        bottom.uv = Vec2{u, 1.0f};
        bottom.color = colors::White;
        vertices.push_back(bottom);

        Vertex top;
        top.position = Vec3{topRadius * cosTheta, halfHeight, topRadius * sinTheta};
        top.normal = normal;
        top.uv = Vec2{u, 0.0f};
        top.color = colors::White;
        vertices.push_back(top);
    }

    for (int slice = 0; slice < slices; ++slice) {
        const uint32_t a = static_cast<uint32_t>(slice) * 2;
        indices.push_back(a);
        indices.push_back(a + 1);
        indices.push_back(a + 2);
        indices.push_back(a + 1);
        indices.push_back(a + 3);
        indices.push_back(a + 2);
    }

    if (!capped) return;

    // Caps, built as triangle fans with their own vertices for flat normals.
    // The winding is derived from the cap normal so both ends come out
    // front-facing when seen from outside the solid.
    auto addCap = [&](float capRadius, float y, Vec3 normal, float uvY) {
        if (capRadius <= 0.0f) return;
        const uint32_t center = static_cast<uint32_t>(vertices.size());

        Vertex centerVertex;
        centerVertex.position = Vec3{0, y, 0};
        centerVertex.normal = normal;
        centerVertex.uv = Vec2{0.5f, uvY};
        centerVertex.color = colors::White;
        vertices.push_back(centerVertex);

        for (int slice = 0; slice <= slices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * TAU;
            Vertex vertex;
            vertex.position = Vec3{capRadius * std::cos(theta), y, capRadius * std::sin(theta)};
            vertex.normal = normal;
            vertex.uv = Vec2{std::cos(theta) * 0.5f + 0.5f, std::sin(theta) * 0.5f + 0.5f};
            vertex.color = colors::White;
            vertices.push_back(vertex);
        }

        // A fan advancing by increasing theta sweeps counter-clockwise around
        // +Y, so it is front-facing as seen from +Y and must be reversed for the
        // bottom cap.
        const bool topFacing = normal.y > 0.0f;
        for (int slice = 0; slice < slices; ++slice) {
            const uint32_t a = center + 1 + static_cast<uint32_t>(slice);
            if (topFacing) {
                indices.push_back(center);
                indices.push_back(a + 1);
                indices.push_back(a);
            } else {
                indices.push_back(center);
                indices.push_back(a);
                indices.push_back(a + 1);
            }
        }
    };

    addCap(radius, -halfHeight, Vec3{0, -1, 0}, 1.0f);
    addCap(topRadius, halfHeight, Vec3{0, 1, 0}, 0.0f);
}

void BuildPlane(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                float width, float depth, int subdivisions) {
    subdivisions = subdivisions < 1 ? 1 : subdivisions;
    const float halfWidth = width * 0.5f;
    const float halfDepth = depth * 0.5f;

    // Y-up plane facing +Y, spanning XZ.
    for (int z = 0; z <= subdivisions; ++z) {
        for (int x = 0; x <= subdivisions; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(subdivisions);
            const float v = static_cast<float>(z) / static_cast<float>(subdivisions);

            Vertex vertex;
            vertex.position = Vec3{-halfWidth + u * width, 0.0f, -halfDepth + v * depth};
            vertex.normal = Vec3{0, 1, 0};
            vertex.uv = Vec2{u, v};
            vertex.color = colors::White;
            vertices.push_back(vertex);
        }
    }

    const uint32_t stride = static_cast<uint32_t>(subdivisions) + 1;
    for (int z = 0; z < subdivisions; ++z) {
        for (int x = 0; x < subdivisions; ++x) {
            const uint32_t a = static_cast<uint32_t>(z) * stride + static_cast<uint32_t>(x);
            const uint32_t b = a + stride;
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(a + 1);
            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(b + 1);
        }
    }
}


} // namespace a3d::geometry
