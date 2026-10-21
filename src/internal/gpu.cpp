// NexiumEngine — GPU context implementation.
#include "internal/gpu.h"

#include "nexium/log.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdio>

namespace n3d::internal {
namespace {

constexpr D3D_FEATURE_LEVEL kMinFeatureLevel = D3D_FEATURE_LEVEL_11_0;

// Description must be kept alive for the lifetime of the debug callback, so it is
// leaked deliberately. Only active when the debug layer is enabled.
ID3D12InfoQueue* gInfoQueue = nullptr;

void ConfigureInfoQueue(ID3D12Device* device) {
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&gInfoQueue))) || !gInfoQueue) return;

    // Corruption and errors break into the debugger; warnings are filtered below.
    gInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    gInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);

    // The debug layer warns about the intentionally minimal resource-state usage
    // and about Present() on a non-visible window; neither is actionable here.
    D3D12_MESSAGE_ID hidden[] = {
        D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
        D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
        D3D12_MESSAGE_ID_UNKNOWN,
    };
    D3D12_INFO_QUEUE_FILTER filter{};
    filter.DenyList.NumIDs = _countof(hidden);
    filter.DenyList.pIDList = hidden;
    gInfoQueue->AddStorageFilterEntries(&filter);
}

void ReportInfoQueueErrors() {
    if (!gInfoQueue) return;
    UINT64 count = gInfoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T len = 0;
        if (FAILED(gInfoQueue->GetMessage(i, nullptr, &len)) || len == 0) continue;
        auto* message = static_cast<D3D12_MESSAGE*>(std::malloc(len));
        if (!message) continue;
        if (SUCCEEDED(gInfoQueue->GetMessage(i, message, &len)))
            LogError("[d3d12] %s", message->pDescription);
        std::free(message);
    }
    gInfoQueue->ClearStoredMessages();
}

} // namespace

// ----------------------------------------------------------- DynamicBuffer ---
bool DynamicBuffer::Create(ID3D12Device* device, uint64_t bytes, const wchar_t* name) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ,
                                                 nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(hr)) {
        LogError("failed to create dynamic buffer (%llu bytes): 0x%08lX",
                 static_cast<unsigned long long>(bytes), static_cast<unsigned long>(hr));
        return false;
    }

    resource->SetName(name);
    D3D12_RANGE readRange{0, 0};
    if (FAILED(resource->Map(0, &readRange, reinterpret_cast<void**>(&mapped)))) {
        LogError("failed to map dynamic buffer");
        return false;
    }

    capacity = bytes;
    offset = 0;
    return true;
}

void DynamicBuffer::Destroy() {
    if (resource && mapped) resource->Unmap(0, nullptr);
    resource.Reset();
    mapped = nullptr;
    capacity = 0;
    offset = 0;
}

bool DynamicBuffer::Push(const void* data, uint64_t bytes, uint64_t alignment,
                         uint64_t* outOffset) {
    const uint64_t aligned = (offset + alignment - 1) & ~(alignment - 1);
    if (aligned + bytes > capacity) return false;

    std::memcpy(mapped + aligned, data, bytes);
    if (outOffset) *outOffset = aligned;
    offset = aligned + bytes;
    return true;
}

// ------------------------------------------------------------- GpuContext ---
GpuContext& GpuContext::Get() {
    static GpuContext instance;
    return instance;
}

bool GpuContext::Initialize(HWND hwnd, uint32_t width, uint32_t height, bool enableDebugLayer) {
    width_ = width;
    height_ = height;
    debugLayer_ = enableDebugLayer;

    if (!CreateDevice(enableDebugLayer)) return false;
    if (!CreateSwapChain(hwnd)) return false;
    if (!CreateDescriptorHeaps()) return false;

    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        LogError("failed to create fence event");
        return false;
    }
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        LogError("failed to create fence");
        return false;
    }

    if (!CreateDepthBuffer()) return false;
    CreateBackBufferViews();
    if (!CreateFrameResources()) return false;

    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&immediateAllocator_)))) {
        LogError("failed to create immediate command allocator");
        return false;
    }
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          immediateAllocator_.Get(), nullptr,
                                          IID_PPV_ARGS(&immediateList_)))) {
        LogError("failed to create immediate command list");
        return false;
    }
    immediateList_->Close();

    LogInfo("NexiumEngine ready — %ux%u, %s", width_, height_, adapterName_.c_str());
    return true;
}

bool GpuContext::CreateDevice(bool enableDebugLayer) {
    if (enableDebugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            // GPU-based validation is far too slow for interactive use, so it is
            // left off; the CPU-side layer still catches state and barrier bugs.
        } else {
            LogWarn("D3D12 debug layer requested but unavailable; continuing without it");
            debugLayer_ = false;
        }
    }

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(enableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0,
                                  IID_PPV_ARGS(&factory)))) {
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
            LogError("failed to create DXGI factory");
            return false;
        }
    }

    // Pick the highest-performance adapter that can actually run our feature level.
    ComPtr<IDXGIAdapter4> chosen;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter4> adapter;
        if (factory->EnumAdapterByGpuPreference(
                i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
            break;
        if (!adapter) continue;

        DXGI_ADAPTER_DESC3 desc{};
        if (FAILED(adapter->GetDesc3(&desc))) continue;
        if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) continue;

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), kMinFeatureLevel,
                                        __uuidof(ID3D12Device), nullptr))) {
            chosen = adapter;
            char name[256]{};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
            adapterName_ = name;
            break;
        }
    }

    HRESULT hr = D3D12CreateDevice(chosen.Get(), kMinFeatureLevel, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) {
        LogError("no D3D12-capable adapter found (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }

    if (debugLayer_) {
        ConfigureInfoQueue(device_.Get());
    } else {
        // Silence the debug layer's own "device removed" spam when it is absent.
        ComPtr<ID3D12InfoQueue> queue;
        if (SUCCEEDED(device_.As(&queue))) queue->SetMuteDebugOutput(FALSE);
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    if (FAILED(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)))) {
        LogError("failed to create command queue");
        return false;
    }

    return true;
}

bool GpuContext::CreateSwapChain(HWND hwnd) {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        LogError("failed to create DXGI factory for swap chain");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kSwapChainBufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    // Tearing is only advertised when the OS and the driver both support it, and a
    // swap chain that requests the flag without support fails to create. Probe it
    // and fall back to vsync, which is always legal.
    ComPtr<IDXGIFactory5> factory5;
    BOOL tearingSupported = FALSE;
    if (SUCCEEDED(factory.As(&factory5))) {
        factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported,
                                      sizeof(tearingSupported));
    }
    tearingSupported_ = tearingSupported == TRUE;
    desc.Flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
                                   : static_cast<DXGI_SWAP_CHAIN_FLAG>(0);
    if (!tearingSupported_) vsync_ = true;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = factory->CreateSwapChainForHwnd(queue_.Get(), hwnd, &desc, nullptr, nullptr, &swapChain1);
    if (FAILED(hr)) {
        LogError("failed to create swap chain (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    if (FAILED(swapChain1.As(&swapChain_))) {
        LogError("swap chain does not support IDXGISwapChain4");
        return false;
    }

    // Alt+Enter fullscreen is handled by the window layer instead.
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool GpuContext::CreateDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = kSrvHeapCapacity;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_)))) {
        LogError("failed to create SRV descriptor heap");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kRtvHeapCapacity;
    if (FAILED(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_)))) {
        LogError("failed to create RTV descriptor heap");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = kDsvHeapCapacity;
    if (FAILED(device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_)))) {
        LogError("failed to create DSV descriptor heap");
        return false;
    }

    srvIncrement_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    rtvIncrement_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsvIncrement_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    return true;
}

void GpuContext::CreateBackBufferViews() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < kSwapChainBufferCount; ++i) {
        if (FAILED(swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])))) {
            LogError("failed to get swap chain buffer %u", i);
            continue;
        }
        device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, rtv);
        rtv.ptr += static_cast<SIZE_T>(rtvIncrement_);
    }
}

bool GpuContext::CreateDepthBuffer() {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                                IID_PPV_ARGS(&depthBuffer_)))) {
        LogError("failed to create depth buffer");
        return false;
    }
    depthBuffer_->SetName(L"NexiumEngine Depth Buffer");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depthBuffer_.Get(), &dsv,
                                    dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool GpuContext::CreateFrameResources() {
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        FrameResources& frame = frames_[i];

        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&frame.allocator)))) {
            LogError("failed to create command allocator %u", i);
            return false;
        }
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              frame.allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&frame.commandList)))) {
            LogError("failed to create command list %u", i);
            return false;
        }
        frame.commandList->Close();

        if (!frame.vertices.Create(device_.Get(), 4ull * 1024 * 1024, L"NexiumEngine Vertices")) return false;
        if (!frame.indices.Create(device_.Get(), 4ull * 1024 * 1024, L"NexiumEngine Indices")) return false;
        // Only a handful of shadow constant blocks per frame, so a small buffer is
        // plenty; it exists so the block never has to be inlined in the root
        // signature, where its 24 DWORDs would blow the 64-DWORD budget.
        if (!frame.shadowConstants.Create(device_.Get(), 64ull * 1024, L"NexiumEngine Shadow Constants")) return false;
    }
    return true;
}

bool GpuContext::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;   // minimised; nothing to do
    if (width == width_ && height == height_) return true;

    WaitForIdle();

    ReleaseBackBuffers();
    depthBuffer_.Reset();

    width_ = width;
    height_ = height;

    // ResizeBuffers requires the same flags the swap chain was created with; the
    // tearing flag is only legal when the OS and driver both reported support.
    const DXGI_SWAP_CHAIN_FLAG flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
                                                         : static_cast<DXGI_SWAP_CHAIN_FLAG>(0);
    if (FAILED(swapChain_->ResizeBuffers(kSwapChainBufferCount, width_, height_,
                                         DXGI_FORMAT_R8G8B8A8_UNORM, flags))) {
        LogError("swap chain resize failed");
        return false;
    }

    CreateBackBufferViews();
    return CreateDepthBuffer();
}

void GpuContext::ReleaseBackBuffers() {
    for (auto& buffer : backBuffers_) buffer.Reset();
}

bool GpuContext::BeginFrame(Color clearColor, bool clearColorBuffer, bool clearDepthBuffer) {
    if (inFrame_) {
        LogWarn("BeginScene called while a frame is already open");
        return false;
    }

    FrameResources& frame = frames_[frameIndex_];

    // Wait until this slot's previous submission has retired before reusing its
    // allocator, command list and upload buffers.
    if (frame.fenceValue != 0 && fence_->GetCompletedValue() < frame.fenceValue) {
        fence_->SetEventOnCompletion(frame.fenceValue, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    if (FAILED(frame.allocator->Reset())) return false;
    if (FAILED(frame.commandList->Reset(frame.allocator.Get(), nullptr))) return false;

    frame.vertices.offset = 0;
    frame.indices.offset = 0;
    frame.shadowConstants.offset = 0;

    const uint32_t backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // PRESENT -> RENDER_TARGET, then open the pass.
    D3D12_RESOURCE_BARRIER toRenderTarget{};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = backBuffers_[backBufferIndex].Get();
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    frame.commandList->ResourceBarrier(1, &toRenderTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(backBufferIndex) * rtvIncrement_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    frame.commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    if (clearColorBuffer) {
        const FLOAT rgba[4] = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};
        frame.commandList->ClearRenderTargetView(rtv, rgba, 0, nullptr);
    }
    if (clearDepthBuffer) {
        frame.commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }

    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    frame.commandList->RSSetViewports(1, &viewport);
    frame.commandList->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    frame.commandList->SetDescriptorHeaps(1, heaps);

    inFrame_ = true;
    return true;
}

void GpuContext::EndFrame() {
    if (!inFrame_) return;

    const uint32_t backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // RENDER_TARGET -> PRESENT so the flip-model swap chain can read it.
    D3D12_RESOURCE_BARRIER toPresent{};
    toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toPresent.Transition.pResource = backBuffers_[backBufferIndex].Get();
    toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

    FrameResources& frame = frames_[frameIndex_];
    frame.commandList->ResourceBarrier(1, &toPresent);
    frame.commandList->Close();

    ID3D12CommandList* lists[] = {frame.commandList.Get()};
    queue_->ExecuteCommandLists(1, lists);

    // Tearing is only legal together with a sync interval of 0, so the two always
    // move together: vsync on presents at interval 1 and must not ask for tearing.
    const UINT syncInterval = vsync_ ? 1u : 0u;
    const UINT presentFlags = (!vsync_ && tearingSupported_) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    swapChain_->Present(syncInterval, presentFlags);

    MoveToNextFrame();
    inFrame_ = false;

    SweepDeferredActions();

    if (debugLayer_) ReportInfoQueueErrors();
}

void GpuContext::MoveToNextFrame() {
    const uint64_t signalValue = ++fenceValue_;
    queue_->Signal(fence_.Get(), signalValue);
    frames_[frameIndex_].fenceValue = signalValue;
    frameIndex_ = (frameIndex_ + 1) % kMaxFramesInFlight;
}

void GpuContext::WaitForIdle() {
    if (!fence_ || !queue_) return;
    const uint64_t signalValue = ++fenceValue_;
    queue_->Signal(fence_.Get(), signalValue);
    if (fence_->GetCompletedValue() < signalValue) {
        fence_->SetEventOnCompletion(signalValue, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    // The GPU is now idle, so every deferred release can run. Without this the
    // staging buffers and meshes queued since the last EndFrame() would only be
    // reclaimed if another frame is drawn.
    SweepDeferredActions();
}

// ----------------------------------------------------------------- shadows ---

bool GpuContext::EnsureShadowMap(uint32_t size) {
    if (size == 0) return false;
    if (shadowMap_ && shadowMapSize_ == size) return true;

    if (!device_) return false;

    // Replacing the map means the old one may still be referenced by a command
    // list in flight, so drain the queue first.
    if (shadowMap_) {
        WaitForIdle();
        shadowMap_.Reset();
        FreeSrv(shadowMapSrvCpu_);
        shadowMapSrv_ = {};
        shadowMapSrvCpu_ = {};
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    // Typeless storage with a D32_TYPELESS view pair: one resource serves as both
    // a depth attachment and a sampled texture, which a concrete D32_FLOAT format
    // would not allow.
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                                IID_PPV_ARGS(&shadowMap_)))) {
        LogError("failed to create the shadow map (%ux%u)", size, size);
        return false;
    }
    SetDebugName(shadowMap_.Get(), "NexiumEngine Shadow Map");

    // DSV in slot 1 of the dedicated heap, next to the scene depth buffer, which
    // owns slot 0.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    shadowMapDsv_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    shadowMapDsv_.ptr += static_cast<SIZE_T>(1) * dsvIncrement_;
    device_->CreateDepthStencilView(shadowMap_.Get(), &dsv, shadowMapDsv_);

    // SRV in the shader-visible heap. Sample with a comparison sampler, so the
    // shader can use SampleCmp for hardware PCF.
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!AllocSrv(&cpu, &gpu)) {
        shadowMap_.Reset();
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(shadowMap_.Get(), &srv, cpu);

    shadowMapSrvCpu_ = cpu;
    shadowMapSrv_ = gpu;
    shadowMapSize_ = size;
    // CreateCommittedResource placed it in DEPTH_WRITE, so the first BeginShadowPass
    // needs no transition.
    shadowMapState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    LogInfo("shadow map: %ux%u ready", size, size);
    return true;
}

D3D12_GPU_VIRTUAL_ADDRESS GpuContext::PushShadowConstants(const ShadowConstants& constants) {
    FrameResources& frame = frames_[frameIndex_];
    uint64_t offset = 0;
    // 256-byte alignment is the D3D12 requirement for a constant buffer view.
    if (!frame.shadowConstants.Push(&constants, sizeof(constants), 256, &offset)) {
        return 0;
    }
    return frame.shadowConstants.GpuAddress(offset);
}

void GpuContext::BeginShadowPass() {
    if (!inFrame_ || !shadowMap_) return;

    ID3D12GraphicsCommandList* list = CommandList();

    // The map may be in shader-read state from the previous frame's scene pass, so
    // transition from whatever it actually is rather than assuming. A freshly
    // created map is already DEPTH_WRITE, in which case no barrier is needed.
    if (shadowMapState_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER toDepth{};
        toDepth.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toDepth.Transition.pResource = shadowMap_.Get();
        toDepth.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toDepth.Transition.StateBefore = shadowMapState_;
        toDepth.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        list->ResourceBarrier(1, &toDepth);
        shadowMapState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    // Depth only: no colour target, so the shadow pass writes nothing but depth.
    list->OMSetRenderTargets(0, nullptr, FALSE, &shadowMapDsv_);
    list->ClearDepthStencilView(shadowMapDsv_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(shadowMapSize_),
                            static_cast<float>(shadowMapSize_), 0.0f, 1.0f};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(shadowMapSize_), static_cast<LONG>(shadowMapSize_)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);

    inShadowPass_ = true;
}

void GpuContext::EndShadowPass() {
    if (!inShadowPass_) return;

    ID3D12GraphicsCommandList* list = CommandList();

    // Back to a sampled texture for the scene pass that follows.
    D3D12_RESOURCE_BARRIER toShader{};
    toShader.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShader.Transition.pResource = shadowMap_.Get();
    toShader.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toShader.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    toShader.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &toShader);
    shadowMapState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Restore the swap chain target and the scene viewport for the main pass.
    const uint32_t backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(backBufferIndex) * rtvIncrement_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_),
                            0.0f, 1.0f};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);

    inShadowPass_ = false;
}

void GpuContext::DestroyShadowMap() {
    if (!shadowMap_) return;
    WaitForIdle();
    shadowMap_.Reset();
    if (shadowMapSrvCpu_.ptr) FreeSrv(shadowMapSrvCpu_);
    shadowMapSrv_ = {};
    shadowMapSrvCpu_ = {};
    shadowMapSize_ = 0;
}

ID3D12GraphicsCommandList* GpuContext::BeginImmediate() {
    if (!immediateList_) return nullptr;

    // An upload must not interleave with a frame being recorded, because both
    // would touch the shared descriptor heap and the queue's submission order.
    if (inFrame_) {
        LogError("BeginImmediate() called while a scene is being recorded; "
                 "upload resources before BeginScene() or after EndScene()");
        return nullptr;
    }

    WaitForIdle();
    if (FAILED(immediateAllocator_->Reset())) return nullptr;
    if (FAILED(immediateList_->Reset(immediateAllocator_.Get(), nullptr))) return nullptr;

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    immediateList_->SetDescriptorHeaps(1, heaps);
    return immediateList_.Get();
}

void GpuContext::EndImmediate() {
    if (!immediateList_) return;
    if (FAILED(immediateList_->Close())) return;

    ID3D12CommandList* lists[] = {immediateList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForIdle();
}

ID3D12GraphicsCommandList* GpuContext::BeginUpload() {
    if (inUpload_) {
        LogError("BeginUpload() called while another upload is already open");
        return nullptr;
    }

    if (inFrame_) {
        // Inside a frame the frame's own list is used, so the upload is recorded
        // in submission order and no extra synchronisation is needed.
        inUpload_ = true;
        return frames_[frameIndex_].commandList.Get();
    }

    ID3D12GraphicsCommandList* list = BeginImmediate();
    if (!list) return nullptr;
    inUpload_ = true;
    return list;
}

void GpuContext::EndUpload() {
    if (!inUpload_) return;
    inUpload_ = false;
    if (inFrame_) return;   // the frame's EndFrame() submits it

    EndImmediate();
    SweepDeferredActions();
}

void GpuContext::Defer(std::function<void()> action) {
    if (!action) return;

    // Tag with the fence value of the submission that will consume whatever the
    // caller is releasing: EndFrame()'s signal when recording, otherwise the one
    // EndImmediate() just waited on.
    DeferredAction deferred;
    deferred.fenceValue = inFrame_ ? fenceValue_ + 1 : fenceValue_;
    deferred.action = std::move(action);
    deferredActions_.push_back(std::move(deferred));
}

void GpuContext::SweepDeferredActions() {
    if (deferredActions_.empty() || !fence_) return;
    const uint64_t completed = fence_->GetCompletedValue();

    for (auto it = deferredActions_.begin(); it != deferredActions_.end();) {
        if (it->fenceValue <= completed) {
            it->action();
            it = deferredActions_.erase(it);
        } else {
            ++it;
        }
    }
}

bool GpuContext::UploadVertices(const void* data, uint64_t bytes, uint64_t* outOffset) {
    return frames_[frameIndex_].vertices.Push(data, bytes, sizeof(float), outOffset);
}

bool GpuContext::UploadIndices(const void* data, uint64_t bytes, uint64_t* outOffset) {
    return frames_[frameIndex_].indices.Push(data, bytes, sizeof(uint32_t), outOffset);
}

D3D12_GPU_VIRTUAL_ADDRESS GpuContext::FrameVertexBase() const {
    return frames_[frameIndex_].vertices.resource->GetGPUVirtualAddress();
}

D3D12_GPU_VIRTUAL_ADDRESS GpuContext::FrameIndexBase() const {
    return frames_[frameIndex_].indices.resource->GetGPUVirtualAddress();
}

bool GpuContext::AllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    uint32_t slot = 0;
    if (!freeSrvSlots_.empty()) {
        slot = freeSrvSlots_.back();
        freeSrvSlots_.pop_back();
    } else {
        if (srvCursor_ >= kSrvHeapCapacity) {
            LogError("SRV descriptor heap exhausted (%u slots); increase kSrvHeapCapacity",
                     kSrvHeapCapacity);
            return false;
        }
        slot = srvCursor_++;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE base = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE baseGpu = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    base.ptr += static_cast<SIZE_T>(slot) * srvIncrement_;
    baseGpu.ptr += static_cast<UINT64>(slot) * srvIncrement_;
    *cpu = base;
    *gpu = baseGpu;
    return true;
}

void GpuContext::FreeSrv(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    D3D12_CPU_DESCRIPTOR_HANDLE base = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    const SIZE_T delta = handle.ptr - base.ptr;
    const uint32_t slot = static_cast<uint32_t>(delta / srvIncrement_);
    if (slot < srvCursor_) freeSrvSlots_.push_back(slot);
}

void GpuContext::SetDebugName(ID3D12Object* object, const char* name) {
    if (!debugLayer_ || !object || !name) return;
    wchar_t wide[256];
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 256) > 0) object->SetName(wide);
}

void GpuContext::Shutdown() {
    if (device_) WaitForIdle();

    for (auto& frame : frames_) {
        frame.vertices.Destroy();
        frame.indices.Destroy();
        frame.commandList.Reset();
        frame.allocator.Reset();
    }

    immediateList_.Reset();
    immediateAllocator_.Reset();
    deferredActions_.clear();

    ReleaseBackBuffers();
    depthBuffer_.Reset();
    fence_.Reset();
    srvHeap_.Reset();
    rtvHeap_.Reset();
    dsvHeap_.Reset();
    swapChain_.Reset();
    queue_.Reset();

    if (gInfoQueue) {
        gInfoQueue->Release();
        gInfoQueue = nullptr;
    }

    device_.Reset();

    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

} // namespace n3d::internal
