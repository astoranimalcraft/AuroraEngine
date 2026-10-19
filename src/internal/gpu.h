// Aurora3D — GPU context (internal).
//
// Owns everything that is a singleton on the device: the D3D12 device, the direct
// command queue, the swap chain and its back buffers, the depth buffer, the
// descriptor heaps, the fence and the per-frame upload buffers.
//
// Frame lifecycle
// ---------------
//   BeginFrame()  waits for the frame slot's fence, resets its allocator and
//                 command list, opens the render pass (barrier + RTV/DSV clear)
//   ... draw calls record into the command list ...
//   EndFrame()    closes the list, executes it, presents, and signals the fence
//
// Because the CPU may run kMaxFramesInFlight frames ahead, every per-frame
// resource (allocator, command list, upload buffer, descriptor ring region) is
// duplicated per slot and only touched after that slot's fence has signalled.
#pragma once

#include "aurora/color.h"
#include "aurora/math.h"

#include "internal/common.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <functional>
#include <string>
#include <vector>

namespace a3d::internal {

// A persistently-mapped upload-heap buffer used for per-frame dynamic geometry.
struct DynamicBuffer {
    ComPtr<ID3D12Resource> resource;
    uint8_t* mapped = nullptr;
    uint64_t capacity = 0;   // bytes
    uint64_t offset = 0;     // current write cursor, bytes

    bool Create(ID3D12Device* device, uint64_t bytes, const wchar_t* name);
    void Destroy();

    // Reserves `bytes` (aligned), copies `data` in and reports the byte offset of
    // the copy. Returns false when the buffer is full.
    bool Push(const void* data, uint64_t bytes, uint64_t alignment, uint64_t* outOffset);

    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(uint64_t offsetBytes) const {
        return resource->GetGPUVirtualAddress() + offsetBytes;
    }
};

struct FrameResources {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    DynamicBuffer vertices;
    DynamicBuffer indices;
    uint64_t fenceValue = 0;
};

class GpuContext {
public:
    static GpuContext& Get();

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height, bool enableDebugLayer);
    void Shutdown();

    // Recreates the swap chain buffers and depth target. Safe to call every frame
    // with the current client size; it is a no-op when nothing changed.
    bool Resize(uint32_t width, uint32_t height);

    bool BeginFrame(Color clearColorValue, bool clearColorBuffer, bool clearDepthBuffer);
    void EndFrame();

    // Present interval and tearing flag for the swap chain. Set from
    // a3d::SetVsync(); 1 blocks on the vertical blank, 0 presents immediately.
    void SetVsync(bool enabled) { vsync_ = enabled; }
    bool Vsync() const { return vsync_; }

    // ---- accessors used by the renderer ------------------------------------
    ID3D12Device* Device() const { return device_.Get(); }
    ID3D12GraphicsCommandList* CommandList() const { return frames_[frameIndex_].commandList.Get(); }
    ID3D12DescriptorHeap* SrvHeap() const { return srvHeap_.Get(); }
    uint32_t SrvIncrement() const { return srvIncrement_; }
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    float Aspect() const { return height_ ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f; }
    bool InFrame() const { return inFrame_; }

    // Appends to this frame's dynamic streams, reporting the byte offset of the
    // copy. Returns false when the stream is full.
    bool UploadVertices(const void* data, uint64_t bytes, uint64_t* outOffset);
    bool UploadIndices(const void* data, uint64_t bytes, uint64_t* outOffset);

    // Base addresses of this frame's dynamic streams, for buffer views.
    D3D12_GPU_VIRTUAL_ADDRESS FrameVertexBase() const;
    D3D12_GPU_VIRTUAL_ADDRESS FrameIndexBase() const;

    // Allocates an SRV slot. Slots are stable for the resource's lifetime and
    // recycled through a free list, so they must be freed explicitly.
    bool AllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu);
    void FreeSrv(D3D12_CPU_DESCRIPTOR_HANDLE handle);

    // Blocks until every in-flight frame has finished on the GPU.
    void WaitForIdle();

    // One-shot command list for resource uploads performed outside a frame
    // (textures, meshes). BeginImmediate() resets a dedicated allocator and
    // returns an open list; EndImmediate() closes, submits and blocks until the
    // GPU has finished, so the caller may safely release staging memory after.
    ID3D12GraphicsCommandList* BeginImmediate();
    void EndImmediate();

    // Convenience for the common case: run `record` on an immediate list and
    // wait for completion.
    template <typename Fn>
    void ExecuteImmediate(Fn&& record) {
        ID3D12GraphicsCommandList* list = BeginImmediate();
        if (!list) return;
        record(list);
        EndImmediate();
    }

    // ---- resource upload ---------------------------------------------------
    // Uploads may happen either between frames or in the middle of one (a game
    // creating a mesh from gameplay code). BeginUpload() returns a list to record
    // into: a dedicated one outside a frame, or the frame's own list inside one.
    // EndUpload() submits and waits only in the first case.
    ID3D12GraphicsCommandList* BeginUpload();
    void EndUpload();

    // Runs `action` once the GPU has finished the submission that is currently
    // being recorded (or the last one submitted, outside a frame). Used to release
    // staging buffers, textures and descriptor slots without stalling the CPU.
    void Defer(std::function<void()> action);

    // Fills a debug name on a resource; no-op unless the debug layer is active.
    void SetDebugName(ID3D12Object* object, const char* name);

    const char* AdapterName() const { return adapterName_.c_str(); }

private:
    GpuContext() = default;
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    bool CreateDevice(bool enableDebugLayer);
    bool CreateSwapChain(HWND hwnd);
    bool CreateDescriptorHeaps();
    bool CreateFrameResources();
    bool CreateDepthBuffer();
    void CreateBackBufferViews();
    void ReleaseBackBuffers();
    void MoveToNextFrame();
    void CloseAndExecute();

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain4> swapChain_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    ComPtr<ID3D12Resource> backBuffers_[kSwapChainBufferCount];
    ComPtr<ID3D12Resource> depthBuffer_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;

    FrameResources frames_[kMaxFramesInFlight];
    uint32_t frameIndex_ = 0;
    uint32_t srvIncrement_ = 0;
    uint32_t rtvIncrement_ = 0;

    // SRV slots are bump-allocated and recycled through a free list, so a slot
    // stays valid for the lifetime of the resource that owns it.
    uint32_t srvCursor_ = 0;
    std::vector<uint32_t> freeSrvSlots_;

    // Dedicated allocator/list pair for out-of-frame uploads.
    ComPtr<ID3D12CommandAllocator> immediateAllocator_;
    ComPtr<ID3D12GraphicsCommandList> immediateList_;
    bool inUpload_ = false;

    // Work queued to run once the GPU has consumed the submission currently being
    // recorded. Each entry is tagged with the fence value to wait for.
    struct DeferredAction {
        uint64_t fenceValue = 0;
        std::function<void()> action;
    };
    std::vector<DeferredAction> deferredActions_;
    void SweepDeferredActions();

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint64_t fenceValue_ = 0;
    bool inFrame_ = false;
    bool debugLayer_ = false;
    bool vsync_ = true;
    bool tearingSupported_ = false;
    std::string adapterName_;
};

} // namespace a3d::internal
