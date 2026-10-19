// Aurora3D — texture creation, upload and image loading.
#include "aurora/texture.h"

#include "aurora/log.h"

#include "internal/gpu.h"
#include "internal/resources.h"

#include <d3d12.h>
#include <wincodec.h>

#include <cstring>
#include <string>
#include <vector>

namespace a3d {
namespace {

ComPtr<IWICImagingFactory> gWicFactory;
bool gComInitialized = false;

// COM is initialised once, lazily, because loading an image is the only thing in
// the engine that needs it and a host application may already have done it.
bool EnsureWic() {
    if (gWicFactory) return true;

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(comResult)) gComInitialized = true;
    // RPC_E_CHANGED_MODE means COM is already up in a different apartment; that
    // is fine, the factory still works.

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&gWicFactory)))) {
        LogError("failed to create the WIC imaging factory");
        return false;
    }
    return true;
}

uint32_t MipCountFor(uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    uint32_t size = width > height ? width : height;
    while (size > 1) {
        size >>= 1;
        ++levels;
    }
    return levels;
}

// Uploads `pixels` into a freshly created default-heap texture, generating mips
// when requested. Runs on an immediate command list and blocks until done, so the
// caller can free the source pixels immediately afterwards.
Texture* CreateTextureFromPixels(const uint8_t* pixels, uint32_t width, uint32_t height,
                                 bool generateMips, const char* debugName) {
    auto& gpu = internal::GpuContext::Get();
    ID3D12Device* device = gpu.Device();
    if (!device || !pixels || width == 0 || height == 0) {
        LogError("invalid texture description (%ux%u, pixels=%p)", width, height,
                 static_cast<const void*>(pixels));
        return nullptr;
    }

    const uint32_t mipLevels = generateMips ? MipCountFor(width, height) : 1;
    const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    const uint32_t bytesPerPixel = 4;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = static_cast<UINT16>(mipLevels);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> resource;
    HRESULT hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&resource));
    if (FAILED(hr)) {
        LogError("failed to create texture resource (0x%08lX)", static_cast<unsigned long>(hr));
        return nullptr;
    }

    // Row pitch must be 256-byte aligned in the upload buffer, so the staging
    // buffer is laid out per-subresource using the device's own footprints.
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipLevels);
    std::vector<UINT> numRows(mipLevels);
    std::vector<UINT64> rowSizes(mipLevels);
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, mipLevels, 0, footprints.data(), numRows.data(),
                                  rowSizes.data(), &totalBytes);

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> staging;
    hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                         IID_PPV_ARGS(&staging));
    if (FAILED(hr)) {
        LogError("failed to create texture staging buffer (0x%08lX)", static_cast<unsigned long>(hr));
        return nullptr;
    }

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped)))) {
        LogError("failed to map texture staging buffer");
        return nullptr;
    }

    // Mip 0 comes from the caller; the remaining levels are a simple 2x2 box
    // filter computed on the CPU, which keeps the engine free of compute shaders.
    std::vector<std::vector<uint8_t>> levels(static_cast<size_t>(mipLevels));
    levels[0].assign(pixels, pixels + static_cast<size_t>(width) * height * bytesPerPixel);

    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    for (uint32_t level = 1; level < mipLevels; ++level) {
        const uint32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        const uint32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;
        std::vector<uint8_t>& src = levels[level - 1];
        std::vector<uint8_t>& dst = levels[level];
        dst.resize(static_cast<size_t>(nextWidth) * nextHeight * bytesPerPixel);

        for (uint32_t y = 0; y < nextHeight; ++y) {
            for (uint32_t x = 0; x < nextWidth; ++x) {
                const uint32_t x0 = x * 2 < mipWidth ? x * 2 : mipWidth - 1;
                const uint32_t y0 = y * 2 < mipHeight ? y * 2 : mipHeight - 1;
                const uint32_t x1 = (x * 2 + 1) < mipWidth ? x * 2 + 1 : mipWidth - 1;
                const uint32_t y1 = (y * 2 + 1) < mipHeight ? y * 2 + 1 : mipHeight - 1;

                for (uint32_t c = 0; c < bytesPerPixel; ++c) {
                    const uint32_t sum =
                        src[(static_cast<size_t>(y0) * mipWidth + x0) * bytesPerPixel + c] +
                        src[(static_cast<size_t>(y0) * mipWidth + x1) * bytesPerPixel + c] +
                        src[(static_cast<size_t>(y1) * mipWidth + x0) * bytesPerPixel + c] +
                        src[(static_cast<size_t>(y1) * mipWidth + x1) * bytesPerPixel + c];
                    dst[(static_cast<size_t>(y) * nextWidth + x) * bytesPerPixel + c] =
                        static_cast<uint8_t>(sum / 4);
                }
            }
        }
        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    for (uint32_t level = 0; level < mipLevels; ++level) {
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = footprints[level];
        uint8_t* destination = mapped + footprint.Offset;
        const uint32_t sourcePitch = (level == 0 ? width : (width >> level ? width >> level : 1)) * bytesPerPixel;

        for (UINT row = 0; row < numRows[level]; ++row) {
            std::memcpy(destination + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                        levels[level].data() + static_cast<size_t>(row) * sourcePitch,
                        rowSizes[level]);
        }
    }

    staging->Unmap(0, nullptr);

    auto* texture = new Texture();
    texture->resource = resource;
    texture->width = width;
    texture->height = height;
    texture->name = debugName ? debugName : "(texture)";

    if (!gpu.AllocSrv(&texture->cpuHandle, &texture->gpuHandle)) {
        delete texture;
        return nullptr;
    }

    // The upload may run inside a frame (a game loading a texture mid-loop), in
    // which case the frame's own command list is used and the staging buffer must
    // outlive the submission. Defer() hands ownership over until the GPU is done.
    ID3D12GraphicsCommandList* list = gpu.BeginUpload();
    if (!list) {
        delete texture;
        return nullptr;
    }

    for (uint32_t level = 0; level < mipLevels; ++level) {
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = staging.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprints[level];

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = resource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = level;

        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }

    // COPY_DEST -> PIXEL_SHADER_RESOURCE for every subresource at once.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &barrier);

    gpu.Defer([staging]() mutable { staging.Reset(); });
    gpu.EndUpload();

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = mipLevels;
    srv.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(resource.Get(), &srv, texture->cpuHandle);

    gpu.SetDebugName(resource.Get(), texture->name.c_str());
    return texture;
}

Texture* gDefaultTexture = nullptr;

} // namespace

Texture* LoadTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || !desc.pixels) {
        LogError("LoadTexture: width, height and pixels are all required");
        return nullptr;
    }
    return CreateTextureFromPixels(desc.pixels, desc.width, desc.height, desc.generateMips,
                                   desc.debugName);
}

Texture* LoadTextureFromMemory(const uint8_t* pixels, uint32_t width, uint32_t height,
                               bool generateMips) {
    return CreateTextureFromPixels(pixels, width, height, generateMips, "(memory)");
}

Texture* LoadTextureFromFile(const char* path, bool generateMips) {
    if (!path) return nullptr;
    if (!EnsureWic()) return nullptr;

    // WIC speaks UTF-16, so widen the path first.
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wideLength <= 0) {
        LogError("LoadTextureFromFile: invalid path '%s'", path);
        return nullptr;
    }
    std::wstring widePath(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath.data(), wideLength);

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = gWicFactory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        LogError("LoadTextureFromFile: cannot decode '%s' (0x%08lX)", path,
                 static_cast<unsigned long>(hr));
        return nullptr;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        LogError("LoadTextureFromFile: '%s' has no frames", path);
        return nullptr;
    }

    // Normalise every source format to straight RGBA8 so the D3D format and the
    // mip generation code only ever deal with one layout.
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(gWicFactory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        LogError("LoadTextureFromFile: cannot convert '%s' to RGBA8", path);
        return nullptr;
    }

    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);
    if (width == 0 || height == 0) {
        LogError("LoadTextureFromFile: '%s' has zero size", path);
        return nullptr;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()),
                                     pixels.data()))) {
        LogError("LoadTextureFromFile: cannot read pixels from '%s'", path);
        return nullptr;
    }

    return CreateTextureFromPixels(pixels.data(), width, height, generateMips, path);
}

void UnloadTexture(Texture* texture) {
    if (!texture) return;
    if (texture->isDefault) {
        LogWarn("UnloadTexture called on the built-in default texture; ignoring");
        return;
    }

    // The GPU may still be reading the resource and its descriptor slot, so both
    // are released only once the in-flight submission has retired.
    auto& gpu = internal::GpuContext::Get();
    Texture* doomed = texture;
    gpu.Defer([doomed]() {
        internal::GpuContext::Get().FreeSrv(doomed->cpuHandle);
        delete doomed;
    });
}

uint32_t TextureWidth(const Texture* texture) { return texture ? texture->width : 0; }
uint32_t TextureHeight(const Texture* texture) { return texture ? texture->height : 0; }
const char* TextureName(const Texture* texture) { return texture ? texture->name.c_str() : ""; }

Texture* GetDefaultTexture() {
    if (gDefaultTexture) return gDefaultTexture;

    // 1x1 opaque white, so an untextured draw multiplies by 1 and the shader can
    // take the same path with or without a texture bound.
    const uint8_t white[4] = {255, 255, 255, 255};
    gDefaultTexture = CreateTextureFromPixels(white, 1, 1, false, "(default white)");
    if (gDefaultTexture) gDefaultTexture->isDefault = true;
    return gDefaultTexture;
}

} // namespace a3d
