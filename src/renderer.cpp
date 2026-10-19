// Aurora3D — renderer implementation.
#include "internal/renderer.h"

#include "aurora/log.h"

#include "internal/gpu.h"
#include "internal/resources.h"
#include "internal/window.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace a3d::internal {
namespace {

// Packs the ObjectConstants block. Kept in one place so the layout is described
// exactly once and always matches shaders/aurora.hlsli.
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

    // A texture set with a3d::SetTexture() applies to the next mesh draw only.
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
    list->SetGraphicsRootDescriptorTable(2, item.texture);

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

} // namespace a3d::internal
