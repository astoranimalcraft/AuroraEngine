// NexiumEngine — renderer implementation.
#include "internal/renderer.h"

#include "nexium/log.h"

#include "internal/gpu.h"
#include "internal/resources.h"
#include "internal/window.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace n3d::internal {
namespace {

// Packs the ObjectConstants block. Kept in one place so the layout is described
// exactly once and always matches shaders/nexium.hlsli.
void FillObjectConstants(ObjectConstants* out, const Mat4& world, Color tint,
                         const float uvScale[2], const float uvOffset[2],
                         float metallic, float roughness, uint32_t flags) {
    out->world = world;
    out->baseColor[0] = tint.r;
    out->baseColor[1] = tint.g;
    out->baseColor[2] = tint.b;
    out->baseColor[3] = tint.a;
    out->uvScaleOffset[0] = uvScale[0];
    out->uvScaleOffset[1] = uvScale[1];
    out->uvScaleOffset[2] = uvOffset[0];
    out->uvScaleOffset[3] = uvOffset[1];
    out->params[0] = metallic;
    out->params[1] = roughness;
    out->params[2] = 0.0f;
    out->flags = flags;
}

} // namespace

Renderer& Renderer::Get() {
    static Renderer instance;
    return instance;
}

// ------------------------------------------------------------ scene life ---
void Renderer::BeginScene(const Camera3D& camera, bool clearColor, Color clearValue) {
    if (inScene_) {
        LogWarn("BeginScene called while a scene is already open");
        return;
    }

    camera_ = camera;
    opaque_.clear();
    transparent_.clear();
    lineVertices_.clear();
    blendedLineVertices_.clear();
    overlayVertices_.clear();
    overlayIndices_.clear();
    overlayBatches_.clear();
    dynamic_.clear();
    stats_ = FrameStats{};

    if (!GpuContext::Get().BeginFrame(clearValue, clearColor, true)) return;
    inScene_ = true;
}

void Renderer::EndScene() {
    if (!inScene_) return;

    // The shadow pass re-renders the opaque queue from the light. It runs first so
    // the map is complete before anything samples it.
    shadowActive_ = false;
    if (shadowsEnabled_ && !opaque_.empty()) {
        RenderShadowPass();
    }
    // The shader always reads lightParams.x, so a block must be bound every frame.
    // The shadow pass already pushed one; when it did not run, push a disabled
    // block here so the shader still has valid constants to read.
    if (!shadowActive_) PushDisabledShadowConstants();

    FlushOpaque();
    FlushTransparent();
    FlushDynamic();
    FlushLines();
    FlushOverlay();

    GpuContext::Get().EndFrame();

    opaque_.clear();
    transparent_.clear();
    lineVertices_.clear();
    blendedLineVertices_.clear();
    overlayVertices_.clear();
    overlayIndices_.clear();
    overlayBatches_.clear();
    dynamic_.clear();
    inScene_ = false;
}

void Renderer::Reset() {
    opaque_.clear();
    transparent_.clear();
    lineVertices_.clear();
    blendedLineVertices_.clear();
    overlayVertices_.clear();
    overlayIndices_.clear();
    overlayBatches_.clear();
    dynamic_.clear();
    inScene_ = false;
    stats_ = FrameStats{};
}

void Renderer::SetLightDirection(Vec3 direction) { lightDirection_ = Normalize(direction); }
void Renderer::SetAmbient(Color color, float strength) {
    ambientColor_ = color;
    ambientStrength_ = Clamp(strength, 0.0f, 1.0f);
}

void Renderer::SetShadowResolution(uint32_t resolution) {
    // Clamp to a sane range and round up to a power of two, which is what the
    // depth target and the texel-snapping math assume.
    if (resolution < 256) resolution = 256;
    if (resolution > 8192) resolution = 8192;
    uint32_t power = 256;
    while (power < resolution) power <<= 1;
    shadowResolution_ = power;
}

Mat4 Renderer::ComputeLightViewProj(Vec3 eye, float halfExtent, float* outTexelSize) const {
    // The light looks at the camera from `halfExtent * 2` away along the light
    // direction, so nothing between the light and the scene is clipped.
    const Vec3 toLight = Normalize(-lightDirection_);
    const float distance = halfExtent * 2.0f;
    const Vec3 lightEye = eye + toLight * distance;

    // Any up vector works as long as it is not parallel to the light direction.
    Vec3 up{0.0f, 1.0f, 0.0f};
    if (std::fabs(Dot(toLight, up)) > 0.99f) up = Vec3{0.0f, 0.0f, 1.0f};

    Mat4 view = LookAt(lightEye, eye, up);

    // Snap the centre to whole texels in light space. Without this the map slides
    // under the geometry as the camera moves and shadow edges shimmer.
    const float texelSize = (halfExtent * 2.0f) / static_cast<float>(shadowResolution_);
    if (outTexelSize) *outTexelSize = texelSize;

    Vec3 center = eye;
    Vec3 centerLightSpace{
        view.At(0, 0) * center.x + view.At(0, 1) * center.y + view.At(0, 2) * center.z + view.At(0, 3),
        view.At(1, 0) * center.x + view.At(1, 1) * center.y + view.At(1, 2) * center.z + view.At(1, 3),
        view.At(2, 0) * center.x + view.At(2, 1) * center.y + view.At(2, 2) * center.z + view.At(2, 3)};
    centerLightSpace.x = std::floor(centerLightSpace.x / texelSize) * texelSize;
    centerLightSpace.y = std::floor(centerLightSpace.y / texelSize) * texelSize;

    // Undo the view rotation to get the snapped world-space centre back.
    Mat4 inverseView = Inverse(view);
    Vec3 snapped{
        inverseView.At(0, 0) * centerLightSpace.x + inverseView.At(0, 1) * centerLightSpace.y +
            inverseView.At(0, 2) * centerLightSpace.z + inverseView.At(0, 3),
        inverseView.At(1, 0) * centerLightSpace.x + inverseView.At(1, 1) * centerLightSpace.y +
            inverseView.At(1, 2) * centerLightSpace.z + inverseView.At(1, 3),
        inverseView.At(2, 0) * centerLightSpace.x + inverseView.At(2, 1) * centerLightSpace.y +
            inverseView.At(2, 2) * centerLightSpace.z + inverseView.At(2, 3)};

    view = LookAt(snapped + toLight * distance, snapped, up);

    // An orthographic box just wide enough to hold the snapped area. The depth
    // range is generous so casters far behind the camera still make the map.
    const float radius = halfExtent * 1.5f;
    Mat4 projection = Ortho(-radius, radius, -radius, radius, 0.01f, distance * 3.0f);
    return projection * view;
}

void Renderer::PushDisabledShadowConstants() {
    ShadowConstants constants{};
    constants.lightViewProj = Mat4::Identity();
    constants.lightParams[0] = 0.0f;   // shadows off
    shadowConstantAddress_ = GpuContext::Get().PushShadowConstants(constants);
}

void Renderer::RenderShadowPass() {
    if (!shadowsEnabled_ || opaque_.empty()) return;

    auto& gpu = GpuContext::Get();
    if (!gpu.EnsureShadowMap(shadowResolution_)) return;

    float texelSize = 0.0f;
    const Mat4 lightViewProj =
        ComputeLightViewProj(camera_.position, shadowDistance_, &texelSize);
    shadowViewProj_ = lightViewProj;

    ShadowConstants constants{};
    constants.lightViewProj = lightViewProj;
    constants.lightParams[0] = 1.0f;                  // enabled
    constants.lightParams[1] = shadowDepthBias_;
    constants.lightParams[2] = shadowNormalBias_;
    constants.lightParams[3] = texelSize;
    constants.shadowParams[0] = 1.0f / static_cast<float>(shadowResolution_);
    constants.shadowParams[1] = 1.0f / static_cast<float>(shadowResolution_);
    constants.shadowParams[2] = 1.5f;                 // PCF tap radius, in texels
    constants.shadowParams[3] = 0.0f;

    const D3D12_GPU_VIRTUAL_ADDRESS shadowAddress = gpu.PushShadowConstants(constants);
    if (shadowAddress == 0) return;

    // Remembered so the main pass binds the same block: a fragment needs the light
    // matrix to project itself into the map, and it must be the exact matrix the
    // pass rendered with or the comparison would be meaningless.
    shadowConstantAddress_ = shadowAddress;

    gpu.BeginShadowPass();

    ID3D12GraphicsCommandList* list = gpu.CommandList();
    list->SetGraphicsRootSignature(RootSignature::Instance().Get());

    // The built-in vertex shader already transforms by frameConstants.viewProj, so
    // pushing the light's matrix there renders the scene from the light with no
    // second shader. The camera matrix is restored by the scene pass, which calls
    // BindFrame() again.
    FrameConstants shadowFrame = frameConstants_;
    shadowFrame.viewProj = lightViewProj;
    list->SetGraphicsRoot32BitConstants(0, kFrameConstantCount, &shadowFrame, 0);
    // The shadow block is bound here too, in case a custom vertex shader wants the
    // light matrix. The map itself (t1) is not: a depth-only pipeline cannot sample
    // the target it is writing.
    list->SetGraphicsRootConstantBufferView(2, shadowAddress);

    for (const DrawItem& item : opaque_) {
        if (item.flags & kFlagNoShadow) continue;
        if (!item.shader || !item.vertexAddress) continue;

        ID3D12PipelineState* pipeline = item.shader->GetShadowPipeline(item.cull);
        if (!pipeline) continue;

        list->SetPipelineState(pipeline);
        list->IASetPrimitiveTopology(item.topology);

        // The object block is still needed for `world`; the light matrix comes from
        // the shadow block, so the frame's viewProj is unused here.
        ObjectConstants object{};
        FillObjectConstants(&object, item.world, item.tint, item.uvScale, item.uvOffset,
                            item.metallic, item.roughness, item.flags);
        list->SetGraphicsRoot32BitConstants(1, kObjectConstantCount, &object, 0);

        const D3D12_VERTEX_BUFFER_VIEW vertexView{
            item.vertexAddress, static_cast<UINT>(item.vertexCount * sizeof(Vertex)),
            static_cast<UINT>(sizeof(Vertex))};
        list->IASetVertexBuffers(0, 1, &vertexView);

        if (item.indexAddress && item.indexCount > 0) {
            const D3D12_INDEX_BUFFER_VIEW indexView{
                item.indexAddress, static_cast<UINT>(item.indexCount * sizeof(uint32_t)),
                DXGI_FORMAT_R32_UINT};
            list->IASetIndexBuffer(&indexView);
            list->DrawIndexedInstanced(item.indexCount, 1, 0, 0, 0);
        } else {
            list->DrawInstanced(item.vertexCount, 1, 0, 0);
        }
    }

    gpu.EndShadowPass();

    // Only now is the map complete, so the main pass may sample it.
    shadowActive_ = true;
}

// ---------------------------------------------------------------- meshes ---
void Renderer::DrawMesh(const Mesh* mesh, const Mat4& world, Color tint, Texture* texture,
                        float metallic, float roughness) {
    if (!inScene_) {
        LogWarn("DrawMesh called outside BeginScene/EndScene; ignoring");
        return;
    }
    QueueMesh(mesh, world, tint, texture, metallic, roughness);
}

void Renderer::QueueMesh(const Mesh* mesh, const Mat4& world, Color tint, Texture* texture,
                         float metallic, float roughness) {
    if (!mesh || mesh->vertexCount == 0 || !mesh->vertexBuffer) return;

    // A texture set with n3d::SetTexture() applies to the next mesh draw only.
    if (!texture && pendingTexture_) {
        texture = pendingTexture_;
        pendingTexture_ = nullptr;
    }

    const bool wantsTexture = texture && !texture->isDefault;
    if (!wantsTexture) texture = GetDefaultTexture();
    if (!texture) return;

    DrawItem item;
    item.vertexAddress = mesh->vertexBuffer->GetGPUVirtualAddress();
    item.vertexCount = mesh->vertexCount;
    if (mesh->indexBuffer && mesh->indexCount > 0) {
        item.indexAddress = mesh->indexBuffer->GetGPUVirtualAddress();
        item.indexCount = mesh->indexCount;
    }

    item.shader = GetCurrentShader() ? GetCurrentShader() : GetDefaultShader();
    item.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    item.world = world;
    item.tint = tint;
    item.metallic = metallic;
    item.roughness = roughness;
    item.texture = texture->gpuHandle;

    if (wantsTexture) item.flags |= kFlagTextured;
    if (mesh->flags & MeshUnlit) item.flags |= kFlagUnlit;
    if (mesh->flags & MeshNoCull) item.flags |= kFlagTwoSided;
    if (mesh->flags & MeshNoShadow) item.flags |= kFlagNoShadow;

    item.cull = (mesh->flags & MeshNoCull) ? CullMode::None : CurrentCullMode();
    item.depth = CurrentDepthTest();

    const BlendMode requested = CurrentBlendMode();
    if (tint.a < 1.0f || requested != BlendMode::Opaque) {
        item.blend = requested == BlendMode::Opaque ? BlendMode::AlphaBlend : requested;
        if (item.flags & kFlagTwoSided) item.cull = CullMode::None;

        const Vec3 origin{world.At(0, 3), world.At(1, 3), world.At(2, 3)};
        item.sortKey = LengthSq(origin - camera_.position);
        transparent_.push_back(item);
    } else {
        item.blend = BlendMode::Opaque;
        opaque_.push_back(item);
    }
}

// ----------------------------------------------------------------- lines ---
void Renderer::DrawLine(Vec3 from, Vec3 to, Color color) {
    if (!inScene_) return;

    Vertex a;
    a.position = from;
    a.color = color;
    Vertex b;
    b.position = to;
    b.color = color;

    if (color.a < 1.0f) {
        blendedLineVertices_.push_back(a);
        blendedLineVertices_.push_back(b);
    } else {
        lineVertices_.push_back(a);
        lineVertices_.push_back(b);
    }
    stats_.lines += 1;
}

// --------------------------------------------------------------- overlay ---
void Renderer::DrawOverlayTriangles(const Vertex* vertices, uint32_t vertexCount, Texture* texture,
                                    Color tint, bool textured, Shader* shader) {
    if (!inScene_ || !vertices || vertexCount < 3) return;
    if (!texture) texture = GetDefaultTexture();
    if (!texture) return;
    if (!shader) shader = GetDefaultShader();
    if (!shader) return;

    // Extend the run in progress when the texture, shader and mode all match,
    // otherwise start a new one. This is what makes a paragraph of text one draw
    // call and a stack of same-coloured shapes a single batch.
    if (overlayBatches_.empty() || overlayBatches_.back().texture != texture ||
        overlayBatches_.back().shader != shader ||
        overlayBatches_.back().textured != textured) {
        OverlayBatch batch;
        batch.texture = texture;
        batch.shader = shader;
        batch.textured = textured;
        batch.firstIndex = static_cast<uint32_t>(overlayIndices_.size());
        overlayBatches_.push_back(batch);
    }

    const uint32_t base = static_cast<uint32_t>(overlayVertices_.size());
    for (uint32_t i = 0; i < vertexCount; ++i) {
        Vertex vertex = vertices[i];
        vertex.color = Color{vertices[i].color.r * tint.r, vertices[i].color.g * tint.g,
                             vertices[i].color.b * tint.b, vertices[i].color.a * tint.a};
        overlayVertices_.push_back(vertex);
    }

    // A triangle list indexes straight through; the six indices of a quad are the
    // same pattern, so the caller never has to supply indices.
    for (uint32_t i = 0; i < vertexCount; ++i) overlayIndices_.push_back(base + i);
    overlayBatches_.back().indexCount += vertexCount;
}

void Renderer::DrawOverlayQuad(const Vertex corners[4], Texture* texture, Color tint,
                               bool textured, Shader* shader) {
    // Expand the quad into the two triangles a triangle list needs.
    const Vertex triangles[6] = {corners[0], corners[1], corners[2],
                                 corners[0], corners[2], corners[3]};
    DrawOverlayTriangles(triangles, 6, texture, tint, textured, shader);
}

void Renderer::DrawScreenLine(Vec2 from, Vec2 to, Color color, float thickness) {
    if (!inScene_) return;

    const Vec2 delta = to - from;
    const float length = Length(delta);
    if (length < 1e-4f) return;

    // Expand the segment into a quad perpendicular to its direction. A minimum
    // width of one pixel keeps a hairline visible on every display.
    const Vec2 direction = delta / length;
    const Vec2 normal = Perp(direction) * (Max(thickness, 1.0f) * 0.5f);

    Vertex corners[4];
    const Vec2 points[4] = {from + normal, to + normal, to - normal, from - normal};
    for (int i = 0; i < 4; ++i) {
        corners[i].position = Vec3{points[i].x, points[i].y, 0.0f};
        corners[i].color = colors::White;
    }
    DrawOverlayQuad(corners, nullptr, color, false);
}

void Renderer::DrawScreenRect(Vec2 position, Vec2 size, Color color) {
    if (!inScene_) return;

    Vertex corners[4];
    const Vec2 points[4] = {
        position,
        Vec2{position.x + size.x, position.y},
        Vec2{position.x + size.x, position.y + size.y},
        Vec2{position.x, position.y + size.y},
    };
    for (int i = 0; i < 4; ++i) {
        corners[i].position = Vec3{points[i].x, points[i].y, 0.0f};
        corners[i].color = colors::White;
    }
    DrawOverlayQuad(corners, nullptr, color, false);
}

void Renderer::DrawDynamicMesh(const Vertex* vertices, uint32_t vertexCount,
                               const uint32_t* indices, uint32_t indexCount, Texture* texture,
                               BlendMode blend, CullMode cull, DepthTest depth) {
    if (!inScene_ || !vertices || vertexCount == 0) return;

    DynamicItem item;
    item.vertices.assign(vertices, vertices + vertexCount);
    if (indices && indexCount > 0) item.indices.assign(indices, indices + indexCount);
    item.texture = texture ? texture : GetDefaultTexture();
    item.blend = blend;
    item.cull = cull;
    item.depth = depth;

    // Billboards and similar are camera-facing, so order them back to front like
    // the transparent pass.
    Vec3 centre{};
    for (uint32_t i = 0; i < vertexCount; ++i) centre = centre + vertices[i].position;
    centre = centre / static_cast<float>(vertexCount);
    item.sortKey = LengthSq(centre - camera_.position);
    dynamic_.push_back(std::move(item));
}

// ----------------------------------------------------------------- flush ---
void Renderer::BindFrame(const Mat4& viewProj) {
    FrameConstants constants{};
    constants.viewProj = viewProj;
    constants.cameraPosTime[0] = camera_.position.x;
    constants.cameraPosTime[1] = camera_.position.y;
    constants.cameraPosTime[2] = camera_.position.z;
    constants.cameraPosTime[3] = static_cast<float>(Window::Get().Time());
    constants.lightDir[0] = lightDirection_.x;
    constants.lightDir[1] = lightDirection_.y;
    constants.lightDir[2] = lightDirection_.z;
    constants.lightDir[3] = 0.0f;
    constants.ambient[0] = ambientColor_.r;
    constants.ambient[1] = ambientColor_.g;
    constants.ambient[2] = ambientColor_.b;
    constants.ambient[3] = ambientStrength_;
    frameConstants_ = constants;

    ID3D12GraphicsCommandList* list = GpuContext::Get().CommandList();
    list->SetGraphicsRootSignature(RootSignature::Instance().Get());
    list->SetGraphicsRoot32BitConstants(0, kFrameConstantCount, &frameConstants_, 0);

    // The shadow block and the shadow map are bound once per pass, not per draw, so
    // the cost does not grow with the number of objects. They are bound even when
    // shadows are off: the shader always reads lightParams.x, and an unbound root
    // CBV would be undefined. When off the block says so and the shader returns
    // immediately, so the fetch never happens.
    if (shadowConstantAddress_ != 0) {
        list->SetGraphicsRootConstantBufferView(2, shadowConstantAddress_);
        D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv = GpuContext::Get().ShadowMapSrv();
        if (shadowActive_ && shadowSrv.ptr != 0) {
            list->SetGraphicsRootDescriptorTable(4, shadowSrv);
        } else {
            // Any valid SRV keeps the descriptor table well formed; the shader will
            // not sample it while lightParams.x is zero.
            Texture* fallback = GetDefaultTexture();
            list->SetGraphicsRootDescriptorTable(4, fallback->gpuHandle);
        }
    }
}

void Renderer::DrawSky() {
    if (!inScene_ || !skyTexture_) return;

    Shader* shader = internal::SkyShader();
    if (!shader) return;

    // A cube centred on the eye, drawn with front-face culling because the camera
    // sits inside it: the faces we see are the ones whose winding points away.
    // The radius stays inside the far plane, so no clip is needed.
    const float radius = camera_.farZ * 0.5f;
    const Mat4 world = Translation(camera_.position) * Scale(Vec3{radius, radius, radius});

    DrawItem item{};
    const Mesh* cube = GetCubeMesh(1.0f);
    if (!cube || !cube->vertexBuffer) return;

    item.vertexAddress = cube->vertexBuffer->GetGPUVirtualAddress();
    item.vertexCount = cube->vertexCount;
    item.indexAddress = cube->indexBuffer ? cube->indexBuffer->GetGPUVirtualAddress() : 0;
    item.indexCount = cube->indexCount;
    item.shader = shader;
    item.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    item.blend = BlendMode::Opaque;
    item.cull = CullMode::Front;
    item.depth = DepthTest::LessEqual;
    item.texture = skyTexture_->gpuHandle;
    item.world = world;
    item.tint = skyTint_;
    item.roughness = 1.0f;
    // The sky is a backdrop: it must not be lit, must not cast a shadow, and must
    // not be culled from behind.
    item.flags |= kFlagUnlit | kFlagNoShadow | kFlagTwoSided;

    // Drawn straight away, not queued: the caller places DrawSky() at the point in
    // the scene where it belongs, and running it now keeps the depth buffer state
    // exactly as the application expects. Issue() assumes the frame is bound, which
    // the flush passes normally do; here nothing has run yet, so bind it first.
    BindFrame(camera_.ViewProjection(GpuContext::Get().Aspect()));
    Issue(item);
}

void Renderer::Issue(const DrawItem& item) {
    ID3D12GraphicsCommandList* list = GpuContext::Get().CommandList();
    if (!list || !item.shader || !item.vertexAddress) return;

    ID3D12PipelineState* pipeline = item.shader->GetPipeline(item.blend, item.cull, item.depth);
    if (!pipeline) return;

    list->SetPipelineState(pipeline);
    list->IASetPrimitiveTopology(item.topology);

    ObjectConstants constants{};
    FillObjectConstants(&constants, item.world, item.tint, item.uvScale, item.uvOffset,
                        item.metallic, item.roughness, item.flags);
    list->SetGraphicsRoot32BitConstants(1, kObjectConstantCount, &constants, 0);
    // t0's table is parameter 3 and t1's is parameter 4; b2 sits between the two
    // constant blocks and the tables.
    list->SetGraphicsRootDescriptorTable(3, item.texture);

    const D3D12_VERTEX_BUFFER_VIEW vertexView{
        item.vertexAddress, static_cast<UINT>(item.vertexCount * sizeof(Vertex)),
        static_cast<UINT>(sizeof(Vertex))};
    list->IASetVertexBuffers(0, 1, &vertexView);

    if (item.indexAddress && item.indexCount > 0) {
        const D3D12_INDEX_BUFFER_VIEW indexView{
            item.indexAddress, static_cast<UINT>(item.indexCount * sizeof(uint32_t)),
            DXGI_FORMAT_R32_UINT};
        list->IASetIndexBuffer(&indexView);
        list->DrawIndexedInstanced(item.indexCount, 1, 0, 0, 0);
    } else {
        list->DrawInstanced(item.vertexCount, 1, 0, 0);
    }

    ++stats_.drawCalls;
    stats_.vertices += item.vertexCount;

    // Only triangle topologies contribute triangles; a line batch must not inflate
    // the counter with a bogus vertexCount / 3.
    const bool triangles = item.topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST ||
                           item.topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    if (triangles) {
        stats_.triangles += item.indexCount > 0 ? item.indexCount / 3 : item.vertexCount / 3;
    }
}

void Renderer::FlushOpaque() {
    if (opaque_.empty()) return;

    // Group equal states together; std::stable_sort keeps the submission order
    // within a group, so the scene still looks the way it was written.
    std::stable_sort(opaque_.begin(), opaque_.end(), [](const DrawItem& a, const DrawItem& b) {
        if (a.shader != b.shader) return a.shader < b.shader;
        if (a.blend != b.blend) return a.blend < b.blend;
        if (a.cull != b.cull) return a.cull < b.cull;
        if (a.depth != b.depth) return a.depth < b.depth;
        if (a.flags != b.flags) return a.flags < b.flags;
        return a.texture.ptr < b.texture.ptr;
    });

    BindFrame(camera_.ViewProjection(GpuContext::Get().Aspect()));

    BlendMode lastBlend = static_cast<BlendMode>(-1);
    CullMode lastCull = static_cast<CullMode>(-1);
    DepthTest lastDepth = static_cast<DepthTest>(-1);
    uint64_t lastTexture = ~0ull;
    Shader* lastShader = nullptr;

    for (const DrawItem& item : opaque_) {
        if (item.shader != lastShader || item.blend != lastBlend || item.cull != lastCull ||
            item.depth != lastDepth || item.texture.ptr != lastTexture) {
            ++stats_.stateChanges;
            lastShader = item.shader;
            lastBlend = item.blend;
            lastCull = item.cull;
            lastDepth = item.depth;
            lastTexture = item.texture.ptr;
        }
        Issue(item);
    }
}

void Renderer::FlushTransparent() {
    if (transparent_.empty()) return;

    // Blending is order dependent: farthest first.
    std::stable_sort(transparent_.begin(), transparent_.end(),
                     [](const DrawItem& a, const DrawItem& b) { return a.sortKey > b.sortKey; });

    BindFrame(camera_.ViewProjection(GpuContext::Get().Aspect()));
    for (const DrawItem& item : transparent_) {
        ++stats_.stateChanges;
        Issue(item);
    }
}

bool Renderer::StreamVertices(const Vertex* vertices, uint32_t vertexCount, BlendMode blend,
                              CullMode cull, DepthTest depth, DrawItem* out) {
    uint64_t offset = 0;
    if (!GpuContext::Get().UploadVertices(vertices, static_cast<uint64_t>(vertexCount) * sizeof(Vertex),
                                          &offset)) {
        return false;
    }

    out->vertexAddress = GpuContext::Get().FrameVertexBase() + offset;
    out->vertexCount = vertexCount;
    out->shader = LineShader();
    out->topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    out->blend = blend;
    out->cull = cull;
    out->depth = depth;
    out->world = Mat4::Identity();
    out->tint = colors::White;
    out->flags = kFlagUnlit;
    out->texture = GetDefaultTexture() ? GetDefaultTexture()->gpuHandle
                                      : D3D12_GPU_DESCRIPTOR_HANDLE{};
    return out->shader != nullptr;
}

void Renderer::FlushLines() {
    if (lineVertices_.empty() && blendedLineVertices_.empty()) return;

    BindFrame(camera_.ViewProjection(GpuContext::Get().Aspect()));

    auto emit = [&](std::vector<Vertex>& vertices, BlendMode blend) {
        if (vertices.empty()) return;
        DrawItem item;
        if (!StreamVertices(vertices.data(), static_cast<uint32_t>(vertices.size()), blend,
                            CullMode::None, CurrentDepthTest(), &item)) {
            LogError("line vertex stream overflowed; increase the frame vertex budget");
            vertices.clear();
            return;
        }
        Issue(item);
        vertices.clear();
    };

    emit(lineVertices_, BlendMode::Opaque);
    emit(blendedLineVertices_, BlendMode::AlphaBlend);
}

bool Renderer::StreamOverlay(DrawItem* out) {
    uint64_t vertexOffset = 0;
    uint64_t indexOffset = 0;
    if (!GpuContext::Get().UploadVertices(overlayVertices_.data(),
                                          overlayVertices_.size() * sizeof(Vertex),
                                          &vertexOffset)) {
        LogError("overlay vertex stream overflowed; increase the frame vertex budget");
        return false;
    }
    if (!GpuContext::Get().UploadIndices(overlayIndices_.data(),
                                         overlayIndices_.size() * sizeof(uint32_t),
                                         &indexOffset)) {
        LogError("overlay index stream overflowed; increase the frame index budget");
        return false;
    }

    out->vertexAddress = GpuContext::Get().FrameVertexBase() + vertexOffset;
    out->indexAddress = GpuContext::Get().FrameIndexBase() + indexOffset;
    overlayIndexBase_ = out->indexAddress;
    out->vertexCount = static_cast<uint32_t>(overlayVertices_.size());
    out->indexCount = static_cast<uint32_t>(overlayIndices_.size());
    out->shader = GetDefaultShader();
    out->topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    out->blend = BlendMode::AlphaBlend;
    out->cull = CullMode::None;
    out->depth = DepthTest::Always;
    out->world = Mat4::Identity();
    out->tint = colors::White;
    out->flags = 0;   // set per batch below
    out->texture = GetDefaultTexture() ? GetDefaultTexture()->gpuHandle
                                      : D3D12_GPU_DESCRIPTOR_HANDLE{};
    return out->shader != nullptr;
}

void Renderer::FlushOverlay() {
    if (overlayVertices_.empty() || overlayIndices_.empty() || overlayBatches_.empty()) return;

    // Pixel space, origin top-left: Ortho() with bottom/top swapped maps +y down.
    const float width = static_cast<float>(GpuContext::Get().Width());
    const float height = static_cast<float>(GpuContext::Get().Height());
    BindFrame(Ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f));

    DrawItem item;
    if (!StreamOverlay(&item)) return;

    // One draw per run of quads sharing a texture. Vertex colours carry the tint,
    // so an untextured run can ignore t0 and a textured one multiplies by it.
    for (const OverlayBatch& batch : overlayBatches_) {
        if (batch.indexCount == 0) continue;
        item.indexCount = batch.indexCount;
        item.texture = batch.texture->gpuHandle;
        item.shader = batch.shader ? batch.shader : GetDefaultShader();
        item.flags = kFlagUnlit | (batch.textured ? kFlagTextured : 0u);
        item.blend = batch.additive ? BlendMode::Additive : BlendMode::AlphaBlend;
        // The upload above addressed the start of the whole stream; advance to this
        // batch's first index.
        item.indexAddress = overlayIndexBase_ + batch.firstIndex * sizeof(uint32_t);
        Issue(item);
    }
}

void Renderer::FlushDynamic() {
    if (dynamic_.empty()) return;

    // Farthest first: these are blended billboards and decals.
    std::stable_sort(dynamic_.begin(), dynamic_.end(),
                     [](const DynamicItem& a, const DynamicItem& b) { return a.sortKey > b.sortKey; });

    BindFrame(camera_.ViewProjection(GpuContext::Get().Aspect()));
    for (const DynamicItem& entry : dynamic_) {
        uint64_t vertexOffset = 0;
        uint64_t indexOffset = 0;
        if (!GpuContext::Get().UploadVertices(entry.vertices.data(),
                                              entry.vertices.size() * sizeof(Vertex),
                                              &vertexOffset)) {
            LogError("dynamic vertex stream overflowed; increase the frame vertex budget");
            continue;
        }
        if (!entry.indices.empty() &&
            !GpuContext::Get().UploadIndices(entry.indices.data(),
                                             entry.indices.size() * sizeof(uint32_t),
                                             &indexOffset)) {
            LogError("dynamic index stream overflowed; increase the frame index budget");
            continue;
        }

        DrawItem item;
        item.vertexAddress = GpuContext::Get().FrameVertexBase() + vertexOffset;
        item.vertexCount = static_cast<uint32_t>(entry.vertices.size());
        if (!entry.indices.empty()) {
            item.indexAddress = GpuContext::Get().FrameIndexBase() + indexOffset;
            item.indexCount = static_cast<uint32_t>(entry.indices.size());
        }
        item.shader = GetCurrentShader() ? GetCurrentShader() : GetDefaultShader();
        item.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        item.blend = entry.blend;
        item.cull = entry.cull;
        item.depth = entry.depth;
        item.world = entry.world;
        item.tint = colors::White;
        item.texture = entry.texture ? entry.texture->gpuHandle
                                     : (GetDefaultTexture() ? GetDefaultTexture()->gpuHandle
                                                            : D3D12_GPU_DESCRIPTOR_HANDLE{});
        item.flags = kFlagUnlit | kFlagTextured;
        Issue(item);
    }
}

} // namespace n3d::internal
