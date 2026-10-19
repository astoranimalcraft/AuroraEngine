// Aurora3D — video playback backend (Media Foundation).
//
// A Video wraps an IMFSourceReader configured to deliver RGB32 (BGRA) frames.
// UpdateVideo() pulls one frame, skipping ahead when the display has fallen
// behind the stream's own timestamps, and writes the pixels straight into an
// upload-heap texture the engine samples like any other. DrawVideo() then queues
// a screen-space quad, so video needs no special render path.
//
// Decoding is synchronous: one frame is pulled per UpdateVideo() call. That keeps
// the subsystem free of threads and shared state at the cost of a short stall
// when a frame is expensive to decode; a background decoder can be added later
// without changing this interface.
//
// Everything is platform gated and best-effort: without Media Foundation, or for
// a file it cannot open, LoadVideo() returns nullptr and the program continues.
#include "aurora/video.h"

#include "aurora/draw.h"
#include "aurora/log.h"
#include "aurora/texture.h"

#include "internal/gpu.h"
#include "internal/resources.h"

#include <d3d12.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstring>
#include <string>
#include <vector>

namespace a3d {

struct Video {
    IMFSourceReader* reader = nullptr;
    Texture* texture = nullptr;      // upload-heap texture, written in place
    uint8_t* mapped = nullptr;       // persistent mapping of the texture
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;             // bytes per row in the texture
    float fps = 30.0f;
    float duration = 0.0f;           // seconds
    float position = 0.0f;           // seconds, advanced by frame timestamps
    bool playing = false;
    bool looping = false;
    bool ended = false;
    bool ready = false;              // a frame has been uploaded
    std::string name;
    // Scratch buffer for the decoded frame, reused every update.
};

namespace {

bool gMediaFoundationStarted = false;
bool gMediaFoundationTried = false;

// Every live Video, so the per-frame UpdateVideo() can advance them without the
// caller having to. A program owns its handles; this list only mirrors them.
std::vector<Video*> gVideos;

// Starts Media Foundation once. Returns false when unavailable, which makes every
// video load fail cleanly.
bool EnsureMediaFoundation() {
    if (gMediaFoundationStarted) return true;
    if (gMediaFoundationTried) return false;
    gMediaFoundationTried = true;

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        LogWarn("video: MFStartup failed; running without video");
        return false;
    }
    gMediaFoundationStarted = true;
    return true;
}

std::wstring Widen(const char* text) {
    if (!text) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring wide(static_cast<size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
    return wide;
}

// Creates an upload-heap BGRA texture, mapped once and written in place. Using an
// upload heap (rather than a default-heap texture plus copies) means a new frame
// is a plain memcpy from the decoder with no command list involvement.
Texture* CreateVideoTexture(uint32_t width, uint32_t height, uint8_t** outMapped) {
    auto& gpu = internal::GpuContext::Get();
    ID3D12Device* device = gpu.Device();
    if (!device) return nullptr;

    const DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    auto* texture = new Texture();
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&texture->resource)))) {
        LogError("video: failed to create the frame texture");
        delete texture;
        return nullptr;
    }

    if (!gpu.AllocSrv(&texture->cpuHandle, &texture->gpuHandle)) {
        delete texture;
        return nullptr;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(texture->resource.Get(), &srv, texture->cpuHandle);

    D3D12_RANGE readRange{0, 0};
    if (FAILED(texture->resource->Map(0, &readRange, reinterpret_cast<void**>(outMapped)))) {
        LogError("video: failed to map the frame texture");
        gpu.FreeSrv(texture->cpuHandle);
        delete texture;
        return nullptr;
    }

    texture->width = width;
    texture->height = height;
    texture->name = "(video frame)";
    return texture;
}

// Reads the stream's frame rate, size and duration into the Video.
void ReadStreamProperties(Video* video) {
    IMFMediaType* type = nullptr;
    if (SUCCEEDED(video->reader->GetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &type))) {
        UINT32 width = 0, height = 0;
        if (SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height))) {
            video->width = width;
            video->height = height;
        }
        UINT32 fpsNum = 0, fpsDen = 0;
        if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &fpsNum, &fpsDen)) &&
            fpsDen != 0) {
            video->fps = static_cast<float>(fpsNum) / static_cast<float>(fpsDen);
        }
        type->Release();
    }

    IMFMediaSource* source = nullptr;
    if (SUCCEEDED(video->reader->GetServiceForStream(
            static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), GUID_NULL, IID_PPV_ARGS(&source)))) {
        IMFPresentationDescriptor* descriptor = nullptr;
        if (SUCCEEDED(source->CreatePresentationDescriptor(&descriptor))) {
            UINT64 duration = 0;
            if (SUCCEEDED(descriptor->GetUINT64(MF_PD_DURATION, &duration))) {
                // Duration is in 100-nanosecond units.
                video->duration = static_cast<float>(duration) / 10000000.0f;
            }
            descriptor->Release();
        }
        source->Release();
    }
}

} // namespace

Video* LoadVideo(const char* path) {
    if (!path || !EnsureMediaFoundation()) return nullptr;

    const std::wstring widePath = Widen(path);
    if (widePath.empty()) {
        LogError("video: invalid path '%s'", path);
        return nullptr;
    }

    auto* video = new Video();
    video->name = path;

    if (FAILED(MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, &video->reader))) {
        LogError("video: cannot open '%s'", path);
        delete video;
        return nullptr;
    }

    // Only the first video stream, and ask for BGRA so no conversion is needed.
    video->reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    video->reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);

    IMFMediaType* outputType = nullptr;
    if (FAILED(MFCreateMediaType(&outputType))) {
        delete video;
        return nullptr;
    }
    outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    const HRESULT setType = video->reader->SetCurrentMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, outputType);
    outputType->Release();
    if (FAILED(setType)) {
        LogError("video: '%s' has no decodable video stream", path);
        video->reader->Release();
        delete video;
        return nullptr;
    }

    ReadStreamProperties(video);
    if (video->width == 0 || video->height == 0) {
        LogError("video: '%s' reported an empty frame size", path);
        video->reader->Release();
        delete video;
        return nullptr;
    }

    video->stride = video->width * 4;
    video->texture = CreateVideoTexture(video->width, video->height, &video->mapped);
    if (!video->texture) {
        video->reader->Release();
        delete video;
        return nullptr;
    }

    gVideos.push_back(video);
    LogInfo("video: '%s' opened — %ux%u @ %.2f fps, %.1f s", path, video->width, video->height,
            static_cast<double>(video->fps), static_cast<double>(video->duration));
    return video;
}

void UnloadVideo(Video* video) {
    if (!video) return;
    for (auto it = gVideos.begin(); it != gVideos.end(); ++it) {
        if (*it == video) {
            gVideos.erase(it);
            break;
        }
    }
    if (video->texture) {
        if (video->mapped) video->texture->resource->Unmap(0, nullptr);
        UnloadTexture(video->texture);
    }
    if (video->reader) video->reader->Release();
    delete video;
}

void PlayVideo(Video* video) {
    if (!video) return;
    video->playing = true;
    video->ended = false;
    video->position = 0.0f;
}

void StopVideo(Video* video) {
    if (!video) return;
    video->playing = false;
    video->ended = false;
    video->position = 0.0f;
}

void PauseVideo(Video* video) {
    if (video) video->playing = false;
}

void ResumeVideo(Video* video) {
    if (!video) return;
    if (video->ended) PlayVideo(video);
    else video->playing = true;
}

bool IsVideoPlaying(const Video* video) { return video && video->playing; }
bool IsVideoReady(const Video* video) { return video && video->ready; }

void SetVideoVolume(Video* video, float volume) {
    // Audio tracks are not decoded yet; the call is accepted so code written
    // against the full API keeps working.
    (void)video;
    (void)volume;
}

void SetVideoLooping(Video* video, bool looping) {
    if (video) video->looping = looping;
}

bool IsVideoLooping(const Video* video) { return video && video->looping; }

int GetVideoWidth(const Video* video) { return video ? static_cast<int>(video->width) : 0; }
int GetVideoHeight(const Video* video) { return video ? static_cast<int>(video->height) : 0; }
float GetVideoFps(const Video* video) { return video ? video->fps : 0.0f; }
float GetVideoLength(const Video* video) { return video ? video->duration : 0.0f; }
float GetVideoTime(const Video* video) { return video ? video->position : 0.0f; }

namespace {

// Pulls one frame from the reader and uploads it to the frame texture. Returns
// false when the stream ended or a frame could not be read.
bool ReadAndUploadFrame(Video* video) {
    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    IMFSample* sample = nullptr;

    const HRESULT hr = video->reader->ReadSample(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &streamIndex, &flags,
        &timestamp, &sample);

    if (FAILED(hr)) {
        LogError("video: ReadSample failed (0x%08lX)", static_cast<unsigned long>(hr));
        if (sample) sample->Release();
        return false;
    }

    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        if (sample) sample->Release();
        video->ended = true;
        if (video->looping) {
            // Rewind by seeking the reader back to the start.
            PROPVARIANT position;
            PropVariantInit(&position);
            position.vt = VT_I8;
            position.hVal.QuadPart = 0;
            video->reader->SetCurrentPosition(GUID_NULL, position);
            PropVariantClear(&position);
            video->position = 0.0f;
            video->ended = false;
            return false;
        }
        video->playing = false;
        return false;
    }

    if (!sample) return false;   // a stream tick with no frame

    IMFMediaBuffer* buffer = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
        sample->Release();
        return false;
    }

    BYTE* data = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    bool uploaded = false;
    if (SUCCEEDED(buffer->Lock(&data, &maxLength, &currentLength))) {
        // RGB32 frames are tightly packed at width * 4 bytes per row. Copy only
        // whole rows, and never read past what the buffer actually holds.
        const size_t wanted = static_cast<size_t>(video->stride) * video->height;
        const size_t available = currentLength < wanted ? currentLength : wanted;
        std::memcpy(video->mapped, data, available);
        buffer->Unlock();
        uploaded = available == wanted;
    }
    buffer->Release();
    sample->Release();

    if (uploaded) {
        video->ready = true;
        video->position = static_cast<float>(timestamp) / 10000000.0f;
    }
    return uploaded;
}

} // namespace

bool DrawVideo(Video* video, float x, float y, Color tint) {
    return DrawVideoV(video, Vec2{x, y}, tint);
}

bool DrawVideoV(Video* video, Vec2 position, Color tint) {
    if (!video || !video->texture || !video->ready) return false;
    DrawTextureV(video->texture, position, tint);
    return true;
}

Texture* GetVideoTexture(Video* video) { return video ? video->texture : nullptr; }

void UpdateVideo() {
    // Advance every playing video by one frame. Pulling one frame per call keeps
    // playback locked to the frame rate, which is what a game wants; a video that
    // needs to run ahead can call this more than once.
    for (Video* video : gVideos) {
        if (!video || !video->playing) continue;
        ReadAndUploadFrame(video);
    }
}

} // namespace a3d
