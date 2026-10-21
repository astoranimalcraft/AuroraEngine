// NexiumEngine — mesh creation, GPU upload and built-in primitives.
//
// Geometry is uploaded into DEFAULT-heap buffers through a staging copy, so draw
// calls read from fast memory. Primitives are built once, cached by their
// parameters and shared; they carry MeshBuiltin in `flags` so UnloadMesh() can
// refuse to free them.
#include "nexium/mesh.h"

#include "nexium/log.h"

#include "internal/gpu.h"
#include "internal/geometry.h"
#include "internal/resources.h"

#include <d3d12.h>

#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace n3d {
namespace {

std::mutex gMeshMutex;
std::map<std::string, Mesh*> gPrimitiveCache;

// Uploads `data` into a new DEFAULT-heap buffer of `bytes`, using a staging copy.
// Returns false on failure, having logged the reason.
bool UploadBuffer(const void* data, uint64_t bytes, const wchar_t* name,
                  D3D12_RESOURCE_STATES finalState, ComPtr<ID3D12Resource>* outResource) {
    auto& gpu = internal::GpuContext::Get();
    ID3D12Device* device = gpu.Device();
    if (!device || bytes == 0) return false;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = bytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> destination;
    HRESULT hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&destination));
    if (FAILED(hr)) {
        LogError("failed to create vertex/index buffer (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    destination->SetName(name);

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    ComPtr<ID3D12Resource> staging;
    hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                         IID_PPV_ARGS(&staging));
    if (FAILED(hr)) {
        LogError("failed to create staging buffer (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(staging->Map(0, &readRange, &mapped))) {
        LogError("failed to map staging buffer");
        return false;
    }
    std::memcpy(mapped, data, static_cast<size_t>(bytes));
    staging->Unmap(0, nullptr);

    ID3D12GraphicsCommandList* list = gpu.BeginUpload();
    if (!list) return false;

    list->CopyBufferRegion(destination.Get(), 0, staging.Get(), 0, bytes);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = destination.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = finalState;
    list->ResourceBarrier(1, &barrier);

    // Hand the staging buffer to the GPU until the submission retires.
    gpu.Defer([staging]() mutable { staging.Reset(); });
    gpu.EndUpload();

    *outResource = destination;
    return true;
}

void ComputeBounds(Mesh* mesh, const Vertex* vertices, uint32_t vertexCount) {
    for (uint32_t i = 0; i < vertexCount; ++i) mesh->bounds.Grow(vertices[i].position);
}

std::string PrimitiveKey(const char* kind, float a, float b, int c, int d) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s_%.4f_%.4f_%d_%d", kind, a, b, c, d);
    return buffer;
}

Mesh* CreateMeshInternal(const MeshDesc& desc, bool builtin) {
    if (!desc.vertices || desc.vertexCount == 0) {
        LogError("CreateMesh: at least one vertex is required");
        return nullptr;
    }

    auto* mesh = new Mesh();
    mesh->vertexCount = desc.vertexCount;
    mesh->indexCount = desc.indexCount;
    mesh->flags = desc.flags | (builtin ? MeshBuiltin : 0u);
    mesh->name = desc.debugName ? desc.debugName : "(mesh)";
    ComputeBounds(mesh, desc.vertices, desc.vertexCount);

    const uint64_t vertexBytes = static_cast<uint64_t>(desc.vertexCount) * sizeof(Vertex);
    if (!UploadBuffer(desc.vertices, vertexBytes, L"NexiumEngine Mesh VB",
                      D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, &mesh->vertexBuffer)) {
        delete mesh;
        return nullptr;
    }

    if (desc.indices && desc.indexCount > 0) {
        const uint64_t indexBytes = static_cast<uint64_t>(desc.indexCount) * sizeof(uint32_t);
        if (!UploadBuffer(desc.indices, indexBytes, L"NexiumEngine Mesh IB",
                          D3D12_RESOURCE_STATE_INDEX_BUFFER, &mesh->indexBuffer)) {
            delete mesh;
            return nullptr;
        }
    }

    return mesh;
}

} // namespace

Mesh* CreateMesh(const MeshDesc& desc) { return CreateMeshInternal(desc, false); }

Mesh* CreateMesh(const Vertex* vertices, uint32_t vertexCount,
                 const uint32_t* indices, uint32_t indexCount) {
    MeshDesc desc{};
    desc.vertices = vertices;
    desc.vertexCount = vertexCount;
    desc.indices = indices;
    desc.indexCount = indexCount;
    return CreateMeshInternal(desc, false);
}

void UnloadMesh(Mesh* mesh) {
    if (!mesh) return;
    if (mesh->flags & MeshBuiltin) {
        LogWarn("UnloadMesh called on the built-in mesh '%s'; ignoring", mesh->name.c_str());
        return;
    }

    // Buffers may still be in use by an in-flight submission.
    Mesh* doomed = mesh;
    internal::GpuContext::Get().Defer([doomed]() { delete doomed; });
}

uint32_t MeshVertexCount(const Mesh* mesh) { return mesh ? mesh->vertexCount : 0; }
uint32_t MeshIndexCount(const Mesh* mesh) { return mesh ? mesh->indexCount : 0; }
uint32_t MeshFlags(const Mesh* mesh) { return mesh ? mesh->flags : 0; }
Bounds MeshBounds(const Mesh* mesh) { return mesh ? mesh->bounds : Bounds{}; }

// Every primitive follows the same shape: derive a cache key from the parameters,
// return the cached mesh if there is one, otherwise build the geometry, upload it
// and remember it. `build` fills the vertex/index vectors.
Mesh* GetOrBuildPrimitive(const std::string& key,
                          const std::function<void(std::vector<Vertex>&,
                                                   std::vector<uint32_t>&)>& build) {
    std::lock_guard<std::mutex> lock(gMeshMutex);
    if (auto it = gPrimitiveCache.find(key); it != gPrimitiveCache.end()) return it->second;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    build(vertices, indices);

    MeshDesc desc{};
    desc.vertices = vertices.data();
    desc.vertexCount = static_cast<uint32_t>(vertices.size());
    desc.indices = indices.data();
    desc.indexCount = static_cast<uint32_t>(indices.size());
    desc.debugName = key.c_str();

    Mesh* mesh = CreateMeshInternal(desc, true);
    if (mesh) gPrimitiveCache[key] = mesh;
    return mesh;
}

Mesh* GetCubeMesh(float size) {
    return GetOrBuildPrimitive(PrimitiveKey("cube", size, 0, 0, 0),
                               [size](auto& v, auto& i) { geometry::BuildCube(v, i, size); });
}

Mesh* GetSphereMesh(float radius, int rings, int slices) {
    return GetOrBuildPrimitive(
        PrimitiveKey("sphere", radius, 0, rings, slices),
        [=](auto& v, auto& i) { geometry::BuildSphere(v, i, radius, rings, slices); });
}

Mesh* GetCylinderMesh(float radius, float height, int slices) {
    return GetOrBuildPrimitive(
        PrimitiveKey("cylinder", radius, height, slices, 0),
        [=](auto& v, auto& i) { geometry::BuildTube(v, i, radius, height, slices, true, radius); });
}

Mesh* GetConeMesh(float radius, float height, int slices) {
    // A cone is a tube whose top radius collapses to zero, which produces the apex
    // and drops the redundant top cap in one step.
    return GetOrBuildPrimitive(
        PrimitiveKey("cone", radius, height, slices, 0),
        [=](auto& v, auto& i) { geometry::BuildTube(v, i, radius, height, slices, true, 0.0f); });
}

Mesh* GetPlaneMesh(float width, float depth, int subdivisions) {
    return GetOrBuildPrimitive(
        PrimitiveKey("plane", width, depth, subdivisions, 0),
        [=](auto& v, auto& i) { geometry::BuildPlane(v, i, width, depth, subdivisions); });
}

Mesh* GetTorusMesh(float majorRadius, float minorRadius, int majorSegments, int minorSegments) {
    return GetOrBuildPrimitive(
        PrimitiveKey("torus", majorRadius, minorRadius, majorSegments, minorSegments),
        [=](auto& v, auto& i) {
            geometry::BuildTorus(v, i, majorRadius, minorRadius, majorSegments, minorSegments);
        });
}

Mesh* GetPyramidMesh(float baseSize, float height) {
    return GetOrBuildPrimitive(
        PrimitiveKey("pyramid", baseSize, height, 0, 0),
        [=](auto& v, auto& i) { geometry::BuildPyramid(v, i, baseSize, height); });
}

Mesh* GetDiskMesh(float radius, int segments) {
    return GetOrBuildPrimitive(
        PrimitiveKey("disk", radius, 0, segments, 0),
        [=](auto& v, auto& i) { geometry::BuildDisk(v, i, radius, segments); });
}

Mesh* GetCapsuleMesh(float radius, float height, int slices, int rings) {
    return GetOrBuildPrimitive(
        PrimitiveKey("capsule", radius, height, slices, rings),
        [=](auto& v, auto& i) { geometry::BuildCapsule(v, i, radius, height, slices, rings); });
}

} // namespace n3d
