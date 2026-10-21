// NexiumEngine — built-in primitive geometry.
//
// Kept free of any D3D12 dependency so the builders can be unit-tested on any
// host: winding order and normals are hand-authored here, and getting them wrong
// shows up as invisible or inside-out meshes.
#include "internal/geometry.h"

#include <cmath>

namespace n3d::geometry {

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

void BuildTorus(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                float majorRadius, float minorRadius, int majorSegments, int minorSegments) {
    majorSegments = majorSegments < 3 ? 3 : majorSegments;
    minorSegments = minorSegments < 3 ? 3 : minorSegments;

    // Sweep a small circle around the Y axis. Each vertex normal points away from
    // the centre of the tube it belongs to, which is what makes the ring shade
    // smoothly instead of looking like a flat band.
    for (int major = 0; major <= majorSegments; ++major) {
        const float u = static_cast<float>(major) / static_cast<float>(majorSegments);
        const float theta = u * TAU;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);

        // Centre of the tube for this slice.
        const Vec3 tubeCentre{majorRadius * cosTheta, 0.0f, majorRadius * sinTheta};

        for (int minor = 0; minor <= minorSegments; ++minor) {
            const float v = static_cast<float>(minor) / static_cast<float>(minorSegments);
            const float phi = v * TAU;
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);

            // Outward direction in the tube's cross-section: radially out from the
            // Y axis, then tilted by the minor angle.
            const Vec3 normal{cosPhi * cosTheta, sinPhi, cosPhi * sinTheta};

            Vertex vertex;
            vertex.position = tubeCentre + normal * minorRadius;
            vertex.normal = normal;
            vertex.uv = Vec2{u, v};
            vertex.color = colors::White;
            vertices.push_back(vertex);
        }
    }

    const uint32_t stride = static_cast<uint32_t>(minorSegments) + 1;
    for (int major = 0; major < majorSegments; ++major) {
        for (int minor = 0; minor < minorSegments; ++minor) {
            const uint32_t a = static_cast<uint32_t>(major) * stride + static_cast<uint32_t>(minor);
            const uint32_t b = a + stride;

            // Wound so the outside faces the camera; verified by the geometry test.
            indices.push_back(a);
            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(a + 1);
            indices.push_back(b + 1);
            indices.push_back(b);
        }
    }
}

void BuildPyramid(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                  float baseSize, float height) {
    const float half = baseSize * 0.5f;
    const float halfHeight = height * 0.5f;

    const Vec3 apex{0.0f, halfHeight, 0.0f};
    // Counter-clockwise seen from above, so the side faces wind outwards.
    const Vec3 base[4] = {
        {-half, -halfHeight, -half},
        { half, -halfHeight, -half},
        { half, -halfHeight,  half},
        {-half, -halfHeight,  half},
    };

    // Four triangular sides, each with a flat normal.
    for (int i = 0; i < 4; ++i) {
        const Vec3& a = base[i];
        const Vec3& b = base[(i + 1) % 4];
        // The base ring runs clockwise seen from above, so (apex - a) x (b - a)
        // is the outward normal rather than the inward one.
        const Vec3 normal = Normalize(Cross(apex - a, b - a));

        const uint32_t start = static_cast<uint32_t>(vertices.size());
        const Vec3 positions[3] = {a, apex, b};
        const Vec2 uvs[3] = {Vec2{0.0f, 1.0f}, Vec2{0.5f, 0.0f}, Vec2{1.0f, 1.0f}};
        for (int v = 0; v < 3; ++v) {
            Vertex vertex;
            vertex.position = positions[v];
            vertex.normal = normal;
            vertex.uv = uvs[v];
            vertex.color = colors::White;
            vertices.push_back(vertex);
        }
        indices.push_back(start);
        indices.push_back(start + 1);
        indices.push_back(start + 2);
    }

    // Base quad, facing -Y. In this ring order the quad 0-1-2 already faces down,
    // so it needs no reversal.
    const uint32_t start = static_cast<uint32_t>(vertices.size());
    for (int i = 0; i < 4; ++i) {
        Vertex vertex;
        vertex.position = base[i];
        vertex.normal = Vec3{0, -1, 0};
        vertex.uv = Vec2{base[i].x / baseSize + 0.5f, base[i].z / baseSize + 0.5f};
        vertex.color = colors::White;
        vertices.push_back(vertex);
    }
    indices.push_back(start + 0);
    indices.push_back(start + 1);
    indices.push_back(start + 2);
    indices.push_back(start + 0);
    indices.push_back(start + 2);
    indices.push_back(start + 3);
}

void BuildDisk(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
               float radius, int segments) {
    segments = segments < 3 ? 3 : segments;

    // Centre vertex, then the rim. A fan around +Y.
    Vertex centre;
    centre.position = Vec3{0, 0, 0};
    centre.normal = Vec3{0, 1, 0};
    centre.uv = Vec2{0.5f, 0.5f};
    centre.color = colors::White;
    vertices.push_back(centre);

    for (int i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float theta = u * TAU;
        Vertex vertex;
        vertex.position = Vec3{radius * std::cos(theta), 0.0f, radius * std::sin(theta)};
        vertex.normal = Vec3{0, 1, 0};
        vertex.uv = Vec2{std::cos(theta) * 0.5f + 0.5f, std::sin(theta) * 0.5f + 0.5f};
        vertex.color = colors::White;
        vertices.push_back(vertex);
    }

    // Increasing theta sweeps counter-clockwise seen from +Y, so the fan is
    // front-facing as viewed from above.
    for (int i = 0; i < segments; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<uint32_t>(i + 2));
        indices.push_back(static_cast<uint32_t>(i + 1));
    }
}

void BuildCapsule(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                  float radius, float height, int slices, int rings) {
    slices = slices < 3 ? 3 : slices;
    rings = rings < 1 ? 1 : rings;
    const float half = height * 0.5f;

    // Revolve a single profile around Y: bottom pole, lower hemisphere, the
    // straight side, upper hemisphere, top pole. Building it as one lathe (rather
    // than a tube plus two caps) means there is no seam where the pieces meet, so
    // the normals stay continuous around the whole silhouette.
    struct ProfilePoint {
        float radial;   // distance from the Y axis
        float y;
    };
    std::vector<ProfilePoint> profile;
    profile.push_back({0.0f, -half - radius});   // bottom pole

    for (int k = 1; k <= rings; ++k) {
        const float a = (static_cast<float>(k) / static_cast<float>(rings)) * (PI * 0.5f);
        profile.push_back({radius * std::sin(a), -half - radius * std::cos(a)});
    }
    for (int k = 1; k <= rings; ++k) {
        const float b = (static_cast<float>(k) / static_cast<float>(rings)) * (PI * 0.5f);
        profile.push_back({radius * std::cos(b), half + radius * std::sin(b)});
    }

    // Normals come from the profile's own tangent, so they follow the curve
    // exactly instead of being approximated per section.
    const size_t count = profile.size();
    for (size_t j = 0; j < count; ++j) {
        const ProfilePoint& here = profile[j];
        const ProfilePoint& next = profile[j + 1 < count ? j + 1 : j];
        const ProfilePoint& prev = profile[j > 0 ? j - 1 : j];
        const float dRadial = next.radial - prev.radial;
        const float dY = next.y - prev.y;

        // Rotating the profile tangent by -90 degrees gives the outward normal.
        const float normalRadial = dY;
        const float normalY = -dRadial;
        const float length = std::sqrt(normalRadial * normalRadial + normalY * normalY);
        const float nr = length > 1e-8f ? normalRadial / length : 1.0f;
        const float ny = length > 1e-8f ? normalY / length : 0.0f;

        for (int slice = 0; slice <= slices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * TAU;
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);

            Vertex vertex;
            vertex.position = Vec3{here.radial * cosTheta, here.y, here.radial * sinTheta};
            vertex.normal = Vec3{nr * cosTheta, ny, nr * sinTheta};
            vertex.uv = Vec2{u, static_cast<float>(j) / static_cast<float>(count - 1)};
            vertex.color = colors::White;
            vertices.push_back(vertex);
        }
    }

    const uint32_t stride = static_cast<uint32_t>(slices) + 1;
    for (uint32_t j = 0; j + 1 < static_cast<uint32_t>(count); ++j) {
        for (int slice = 0; slice < slices; ++slice) {
            const uint32_t a = j * stride + static_cast<uint32_t>(slice);
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


} // namespace n3d::geometry
