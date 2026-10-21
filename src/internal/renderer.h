// NexiumEngine — renderer (internal).
//
// Immediate-mode calls (n3d::DrawMesh, n3d::DrawLine, ...) never touch the GPU
// directly. They append to per-frame lists which EndScene() flushes in ordered
// passes:
//
//   1. opaque 3D       sorted by pipeline state to minimise PSO and descriptor
//                      churn, which is what actually costs on a CPU-bound frame
//   2. transparent 3D  sorted back-to-front, because blending is order dependent
//   3. debug lines     batched into the frame's dynamic vertex stream
//   4. 2D overlay      orthographic, depth test off, drawn last
//
// Static mesh geometry is read straight from the mesh's own buffers. Dynamic
// geometry (lines, overlay quads) is streamed through the frame's upload buffers,
// so a DrawItem only ever needs a GPU address plus counts.
#pragma once

#include "nexium/camera.h"
#include "nexium/color.h"
#include "nexium/math.h"
#include "nexium/mesh.h"
#include "nexium/shader.h"
#include "nexium/texture.h"

#include "internal/common.h"

#include <d3d12.h>

#include <vector>

namespace n3d::internal {

struct FrameStats {
    uint32_t drawCalls = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
    uint32_t lines = 0;
    uint32_t stateChanges = 0;   // pipeline or descriptor rebinds across the frame
};

class Renderer {
public:
    static Renderer& Get();

    // `clearColor` false keeps the colour buffer, which is how an overlay pass is
    // layered on top of a 3D scene (the depth buffer is always cleared).
    void BeginScene(const Camera3D& camera, bool clearColor, Color clearValue);
    void EndScene();

    void DrawMesh(const Mesh* mesh, const Mat4& world, Color tint, Texture* texture,
                  float metallic, float roughness);

    // Texture used by the next mesh draw that does not name one; cleared once
    // consumed. This is what n3d::SetTexture() drives.
    void SetPendingTexture(Texture* texture) { pendingTexture_ = texture; }

    void DrawLine(Vec3 from, Vec3 to, Color color);
    void DrawScreenLine(Vec2 from, Vec2 to, Color color, float thickness);
    void DrawScreenRect(Vec2 position, Vec2 size, Color color);

    // Adds a screen-space quad (already in pixel coordinates, origin top-left).
    // Quads that share a texture and arrive consecutively are merged into a single
    // draw, which is what keeps a page of text to one call per font.
    void DrawOverlayQuad(const Vertex corners[4], Texture* texture, Color tint, bool textured,
                         Shader* shader = nullptr);

    // Adds a triangle list to the screen-space overlay. `vertexCount` must be a
    // multiple of three and the vertices are taken in order (0-1-2, 3-4-5, ...).
    // This is what the 2D shape helpers build their geometry with.
    void DrawOverlayTriangles(const Vertex* vertices, uint32_t vertexCount, Texture* texture,
                              Color tint, bool textured, Shader* shader = nullptr);

    // Streams a one-off indexed triangle mesh in world space. Used for billboarded
    // text and any other dynamic geometry that does not live in a Mesh.
    void DrawDynamicMesh(const Vertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                         uint32_t indexCount, Texture* texture, BlendMode blend, CullMode cull,
                         DepthTest depth);

    void SetLightDirection(Vec3 direction);
    void SetAmbient(Color color, float strength);

    // ---- environment -------------------------------------------------------
    void SetSky(Texture* sky) { skyTexture_ = sky; }
    Texture* Sky() const { return skyTexture_; }
    void SetSkyTint(Color tint) { skyTint_ = tint; }
    // Draws the sky immediately. Called from within the scene so the depth buffer
    // is already available and the sky is occluded by real geometry.
    void DrawSky();

    // ---- shadows -----------------------------------------------------------
    void SetShadowsEnabled(bool enabled) { shadowsEnabled_ = enabled; }
    bool ShadowsEnabled() const { return shadowsEnabled_; }
    void SetShadowResolution(uint32_t resolution);
    uint32_t ShadowResolution() const { return shadowResolution_; }
    void SetShadowDistance(float distance) { shadowDistance_ = distance > 0.0f ? distance : 1.0f; }
    float ShadowDistance() const { return shadowDistance_; }
    void SetShadowBias(float depthBias, float normalBias) {
        shadowDepthBias_ = depthBias;
        shadowNormalBias_ = normalBias;
    }
    float ShadowDepthBias() const { return shadowDepthBias_; }
    float ShadowNormalBias() const { return shadowNormalBias_; }

    bool InScene() const { return inScene_; }
    const Camera3D& Camera() const { return camera_; }
    const FrameStats& Stats() const { return stats_; }

    // Drops queued work and cached pointers on device teardown.
    void Reset();

private:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // A queued draw. Geometry is addressed uniformly, whether it lives in a mesh
    // buffer or in this frame's upload stream.
    struct DrawItem {
        D3D12_GPU_VIRTUAL_ADDRESS vertexAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS indexAddress = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        Shader* shader = nullptr;
        D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        BlendMode blend = BlendMode::Opaque;
        CullMode cull = CullMode::Back;
        DepthTest depth = DepthTest::Less;
        D3D12_GPU_DESCRIPTOR_HANDLE texture{};

        Mat4 world;
        Color tint{1, 1, 1, 1};
        float uvScale[2] = {1.0f, 1.0f};
        float uvOffset[2] = {0.0f, 0.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        uint32_t flags = 0;

        // Squared distance to the camera, used to order transparent draws.
        float sortKey = 0.0f;
    };

    void QueueMesh(const Mesh* mesh, const Mat4& world, Color tint, Texture* texture,
                   float metallic, float roughness);

    // Appends `vertices` to the frame's dynamic vertex stream and fills a DrawItem
    // addressing the copy. Returns false if the stream is full.
    bool StreamVertices(const Vertex* vertices, uint32_t vertexCount, BlendMode blend,
                        CullMode cull, DepthTest depth, DrawItem* out);
    bool StreamOverlay(DrawItem* out);

    void FlushOpaque();
    void FlushTransparent();
    void FlushLines();
    void FlushOverlay();
    void FlushDynamic();
    void Issue(const DrawItem& item);
    void BindFrame(const Mat4& viewProj);

    // Renders the opaque queue again from the light's point of view, into the
    // shadow map, and records whether the result is usable by the main pass.
    void RenderShadowPass();
    // Pushes a shadow block with `enabled` at zero, so the shader's shadow lookup
    // short-circuits on a frame that has no shadow pass.
    void PushDisabledShadowConstants();
    // Fits an orthographic box around the camera so the map covers the visible
    // area. The box follows the camera but is snapped to texel-sized steps, which
    // stops the shadow edges from crawling as the camera moves.
    Mat4 ComputeLightViewProj(Vec3 eye, float halfExtent, float* outTexelSize) const;

    Camera3D camera_{};
    FrameConstants frameConstants_{};

    bool shadowsEnabled_ = false;
    uint32_t shadowResolution_ = 2048;
    float shadowDistance_ = 40.0f;
    float shadowDepthBias_ = 0.0015f;
    float shadowNormalBias_ = 0.02f;

    Texture* skyTexture_ = nullptr;
    Color skyTint_{1, 1, 1, 1};
    // The built-in sky shader, compiled lazily on the first DrawSky() so a program
    // that never draws a sky never pays for it.
    Shader* skyShader_ = nullptr;

    // Set by the shadow pass and consumed by the main pass, so both agree on the
    // light matrix and on whether the map holds anything worth sampling.
    Mat4 shadowViewProj_ = Mat4::Identity();
    bool shadowActive_ = false;
    D3D12_GPU_VIRTUAL_ADDRESS shadowConstantAddress_ = 0;

    std::vector<DrawItem> opaque_;
    std::vector<DrawItem> transparent_;
    std::vector<Vertex> lineVertices_;          // opaque lines
    std::vector<Vertex> blendedLineVertices_;   // translucent lines
    std::vector<Vertex> overlayVertices_;
    std::vector<uint32_t> overlayIndices_;

    // Overlay draws are collected as runs that share a texture, so text (one font)
    // and a stack of shapes collapse to a handful of draw calls instead of one per
    // quad. Each batch indexes into the two overlay streams above.
    struct OverlayBatch {
        Texture* texture = nullptr;
        Shader* shader = nullptr;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        bool textured = false;
        bool additive = false;
    };
    std::vector<OverlayBatch> overlayBatches_;
    // GPU address the overlay index stream started at, so each batch can point at
    // its own slice of it.
    D3D12_GPU_VIRTUAL_ADDRESS overlayIndexBase_ = 0;

    // Runs accumulated for one frame of DrawText3D / dynamic geometry, submitted
    // after the line pass so they sort against the rest of the scene.
    struct DynamicItem {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        Texture* texture = nullptr;
        BlendMode blend = BlendMode::AlphaBlend;
        CullMode cull = CullMode::None;
        DepthTest depth = DepthTest::Less;
        Mat4 world;
        float sortKey = 0.0f;
    };
    std::vector<DynamicItem> dynamic_;

    Vec3 lightDirection_{0.35f, -0.8f, 0.45f};   // direction light travels
    Color ambientColor_{1.0f, 1.0f, 1.0f, 1.0f};
    float ambientStrength_ = 0.25f;

    FrameStats stats_{};
    bool inScene_ = false;
    Texture* pendingTexture_ = nullptr;
};

} // namespace n3d::internal
