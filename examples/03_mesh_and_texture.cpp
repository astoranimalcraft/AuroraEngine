// NexiumEngine — example: procedural mesh plus a texture, drawn with a lit material.
//
// Shows the lower-level path: build a Vertex/Index buffer by hand, upload it with
// n3d::CreateMesh(), generate a texture from memory and draw it with
// n3d::DrawMeshMaterial(). Everything is released explicitly at the end.
#include <nexium/NexiumEngine.h>

#include <cmath>
#include <vector>

namespace {

// A flat, subdivided quad in the XZ plane with a checkerboard texture on top.
n3d::Mesh* BuildGround(float extent, int divisions) {
    std::vector<n3d::Vertex> vertices;
    std::vector<uint32_t> indices;

    const float step = (extent * 2.0f) / static_cast<float>(divisions);
    for (int z = 0; z <= divisions; ++z) {
        for (int x = 0; x <= divisions; ++x) {
            n3d::Vertex vertex;
            vertex.position = n3d::Vec3{-extent + step * static_cast<float>(x), 0.0f,
                                        -extent + step * static_cast<float>(z)};
            vertex.normal = n3d::Vec3{0, 1, 0};
            vertex.uv = n3d::Vec2{static_cast<float>(x) / static_cast<float>(divisions),
                                  static_cast<float>(z) / static_cast<float>(divisions)};
            vertex.color = n3d::colors::White;
            vertices.push_back(vertex);
        }
    }

    const uint32_t stride = static_cast<uint32_t>(divisions + 1);
    for (int z = 0; z < divisions; ++z) {
        for (int x = 0; x < divisions; ++x) {
            const uint32_t i = static_cast<uint32_t>(z) * stride + static_cast<uint32_t>(x);
            // Counter-clockwise when seen from above (+y), matching the engine's
            // right-handed, counter-clockwise front-face convention.
            indices.push_back(i);
            indices.push_back(i + stride);
            indices.push_back(i + 1);
            indices.push_back(i + 1);
            indices.push_back(i + stride);
            indices.push_back(i + stride + 1);
        }
    }

    return n3d::CreateMesh(vertices.data(), static_cast<uint32_t>(vertices.size()),
                           indices.data(), static_cast<uint32_t>(indices.size()));
}

// A 64x64 checkerboard generated in code, so the example needs no asset files.
n3d::Texture* BuildCheckerTexture(int size, int cells) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    const int cellSize = size / cells;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool light = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            const uint8_t value = light ? 230 : 40;
            const size_t index = (static_cast<size_t>(y) * size + x) * 4;
            pixels[index + 0] = value;
            pixels[index + 1] = static_cast<uint8_t>(value * 0.9f);
            pixels[index + 2] = static_cast<uint8_t>(value * 0.75f);
            pixels[index + 3] = 255;
        }
    }

    n3d::TextureDesc desc;
    desc.width = static_cast<uint32_t>(size);
    desc.height = static_cast<uint32_t>(size);
    desc.pixels = pixels.data();
    desc.generateMips = true;
    desc.debugName = "checker";
    return n3d::LoadTexture(desc);
}

n3d::OrbitCamera gOrbit;

} // namespace

int main() {
    n3d::Config config;
    config.title = "NexiumEngine — Mesh and Texture";
    if (!n3d::Init(config)) return 1;

    gOrbit.distance = 12.0f;
    gOrbit.pitch = n3d::Radians(28.0f);
    n3d::SetLightDirection(n3d::Vec3{-0.5f, -1.0f, -0.4f});
    n3d::SetAmbient(n3d::colors::White, 0.35f);

    n3d::Mesh* ground = BuildGround(8.0f, 24);
    n3d::Texture* checker = BuildCheckerTexture(64, 8);
    if (!ground || !checker) {
        n3d::LogError("failed to build the example assets");
        n3d::Close();
        return 1;
    }

    const n3d::Mesh* sphere = n3d::GetSphereMesh(1.0f);

    while (!n3d::WindowShouldClose()) {
        n3d::UpdateOrbitCamera(gOrbit);

        n3d::BeginScene(gOrbit.ToCamera(), n3d::colors::SkyBlue);

        n3d::DrawMeshMaterial(ground, n3d::Vec3{}, checker, n3d::Vec3{}, n3d::Vec3{},
                              n3d::colors::White, 0.0f, 0.9f);

        // A shiny metallic sphere bouncing above the ground.
        const float t = n3d::GetTime();
        const float y = 1.5f + std::sin(t * 1.5f) * 0.6f;
        n3d::DrawMeshMaterial(sphere, n3d::Vec3{0.0f, y, 0.0f}, n3d::GetDefaultTexture(),
                              n3d::Vec3{}, n3d::Vec3{1.2f, 1.2f, 1.2f}, n3d::colors::Orange,
                              0.85f, 0.15f);

        n3d::DrawBounds(n3d::MeshBounds(sphere), n3d::colors::Yellow);
        n3d::EndScene();
    }

    n3d::UnloadMesh(ground);
    n3d::UnloadTexture(checker);
    n3d::Close();
    return 0;
}
