// NexiumEngine — video playback backend (Media Foundation).
//
// A Video wraps an IMFSourceReader configured to deliver RGB32 (BGRA) frames.
// UpdateVideo() pulls one frame and copies it into a texture through a staging
// buffer, on an upload command list. DrawVideo() then queues a screen-space quad,
// so video needs no special render path.
//
// Decoding is synchronous: frames are pulled during UpdateVideo(), which the
// engine calls once per frame. Playback is driven by the wall clock, so a clip
// runs at its own frame rate no matter what the program renders at. That keeps
// the subsystem free of threads and shared state at the cost of a short stall
// when a frame is expensive to decode; a background decoder can be added later
// without changing this interface.
//
// Everything is platform gated and best-effort: without Media Foundation, or for
// a file it cannot open, LoadVideo() returns nullptr and the program continues.
#include "nexium/video.h"

#include "nexium/draw.h"
#include "nexium/log.h"
#include "nexium/texture.h"

#include "internal/gpu.h"
#include "internal/resources.h"
#include "internal/window.h"

#include <d3d12.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace n3d {

struct Video {
    IMFSourceReader* reader = nullptr;
    // Default-heap texture holding the frame, sampled like any other texture. It
    // is never written directly: each new frame is copied into it from a staging
    // buffer on the frame's command list.
    Texture* texture = nullptr;
    uint32_t width = 0;              // frame width, as the video reports it
    uint32_t height = 0;
    // Staging buffer the decoder's frame is written into, and the footprint that
    // describes how the device wants the rows laid out inside it.
    ComPtr<ID3D12Resource> staging;
    uint8_t* stagingMapped = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint32_t rowPitch = 0;           // bytes per row in the staging buffer
    float fps = 30.0f;
    float duration = 0.0f;           // seconds
    float position = 0.0f;           // seconds, from the last decoded frame
    // Playback clock, in seconds, advanced by real elapsed time while playing.
    // Frames are pulled to keep `clock` ahead of `position`; without this the
    // decoder is advanced once per UpdateVideo() call, which makes a 30 fps clip
    // play at the frame rate instead of at 30 fps.
    double clock = 0.0;
    float frameInterval = 1.0f / 30.0f;   // seconds between frames, from the stream
    bool playing = false;
    bool looping = false;
    bool ended = false;
    bool ready = false;              // a frame has reached the texture
    std::string name;
};

namespace {

bool gMediaFoundationStarted = false;
bool gMediaFoundationTried = false;

// Wall clock of the previous UpdateVideo(), so playback can be advanced by real
// elapsed time instead of by frame count.
double gLastUpdateTime = 0.0;

// Every live Video, so the per-frame UpdateVideo() can advance them without the
// caller having to. A program owns its handles; this list only mirrors them.
std::vector<Video*> gVideos;

// Defined further down, next to the other decode helpers; PlayVideo() needs it to
// put a picture on screen before the first UpdateVideo() tick.
bool ReadAndUploadFrame(Video* video);

// Starts Media Foundation once. Returns false when unavailable, which makes every
// video load fail cleanly.
//
// Media Foundation requires an initialised COM apartment before it can create any
// object, and the WIC path in texture.cpp is the only other place that calls
// CoInitializeEx — so a program that only plays video never initialises COM and
// every call here fails with CO_E_NOTINITIALIZED. MFSTARTUP_FULL (the documented
// default) rather than LITE, which only skips the socket layer.
bool EnsureMediaFoundation() {
    if (gMediaFoundationStarted) return true;
    if (gMediaFoundationTried) return false;
    gMediaFoundationTried = true;

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // RPC_E_CHANGED_MODE means COM is already up in another apartment; that is
    // fine, Media Foundation works from either.
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        LogWarn("video: CoInitializeEx failed (0x%08lX); running without video",
                static_cast<unsigned long>(comResult));
        return false;
    }

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) {
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
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
    return wide;
}

// Creates the frame texture as an ordinary engine texture. It goes through the
// same default-heap + staging upload path as every other texture rather than being
// an upload-heap resource written directly: an upload-heap texture is not valid on
// every implementation (vkd3d, used for D3D12 on Linux, rejects it outright with
// E_INVALIDARG), and the copy path is the one the rest of the engine already uses.
//
// The initial contents are black so the texture is valid before the first frame.
Texture* CreateVideoTexture(uint32_t width, uint32_t height) {
    std::vector<uint8_t> black(static_cast<size_t>(width) * height * 4u, 0u);
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.pixels = black.data();
    desc.generateMips = false;
    desc.debugName = "(video frame)";
    return LoadTexture(desc);
}

// Allocates the staging buffer a frame is copied through, sized from the device's
// own footprint for the texture so the row pitch is whatever the device requires.
bool CreateVideoStaging(Video* video) {
    auto& gpu = internal::GpuContext::Get();
    ID3D12Device* device = gpu.Device();
    if (!device || !video->texture) return false;

    const D3D12_RESOURCE_DESC textureDesc = video->texture->resource->GetDesc();
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &video->footprint, &numRows, &rowSize,
                                  &totalBytes);
    video->rowPitch = video->footprint.Footprint.RowPitch;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = totalBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&video->staging)))) {
        LogError("video: failed to create the frame staging buffer");
        return false;
    }

    D3D12_RANGE readRange{0, 0};
    if (FAILED(video->staging->Map(0, &readRange,
                                   reinterpret_cast<void**>(&video->stagingMapped)))) {
        LogError("video: failed to map the frame staging buffer");
        video->staging.Reset();
        return false;
    }
    return true;
}

// Copies the frame that ReadAndUploadFrame() just decoded into the texture. Runs
// on an upload command list, which waits for the GPU first; that stall is the price
// of keeping video free of threading and of any per-frame GPU allocation.
bool UploadVideoFrame(Video* video) {
    if (!video->texture || !video->staging || !video->stagingMapped) return false;

    auto& gpu = internal::GpuContext::Get();
    ID3D12GraphicsCommandList* list = gpu.BeginUpload();
    if (!list) return false;

    D3D12_RESOURCE_BARRIER toCopy{};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = video->texture->resource.Get();
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(1, &toCopy);

    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = video->staging.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = video->footprint;

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = video->texture->resource.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;

    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

    D3D12_RESOURCE_BARRIER toShader{};
    toShader.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShader.Transition.pResource = video->texture->resource.Get();
    toShader.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toShader.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toShader.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &toShader);

    gpu.EndUpload();
    return true;
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
            fpsDen != 0 && fpsNum != 0) {
            video->fps = static_cast<float>(fpsNum) / static_cast<float>(fpsDen);
            video->frameInterval = static_cast<float>(fpsDen) / static_cast<float>(fpsNum);
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

// Moves the reader to `seconds` and drops the frame currently on screen, so the
// next decoded frame is the one at the new position. `position` is pushed forward
// as well: the sync loop in UpdateVideo() only decodes while the clock is ahead of
// it, so leaving it at the old value would stall playback until the clock caught
// up with the old position again.
void SeekVideoInternal(Video* video, double seconds) {
    if (!video || !video->reader) return;

    const double clamped = seconds < 0.0 ? 0.0 : seconds;

    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = static_cast<LONGLONG>(clamped * 10000000.0);
    video->reader->SetCurrentPosition(GUID_NULL, position);
    PropVariantClear(&position);

    video->position = static_cast<float>(clamped);
    video->clock = clamped;
    video->ended = false;
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

    // MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING lets the reader convert the native
    // YUV format to RGB32 through the video processor MFT. Without it, asking for
    // RGB32 on a stream that is not already RGB fails with MF_E_INVALIDMEDIATYPE
    // for every ordinary H.264/MP4 file, because the decoder only emits NV12.
    IMFAttributes* attributes = nullptr;
    if (SUCCEEDED(MFCreateAttributes(&attributes, 1))) {
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }
    const HRESULT openResult =
        MFCreateSourceReaderFromURL(widePath.c_str(), attributes, &video->reader);
    if (attributes) attributes->Release();
    if (FAILED(openResult)) {
        LogError("video: cannot open '%s' (0x%08lX)", path,
                 static_cast<unsigned long>(openResult));
        delete video;
        return nullptr;
    }

    // Only the first video stream.
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

    video->texture = CreateVideoTexture(video->width, video->height);
    if (!video->texture) {
        video->reader->Release();
        delete video;
        return nullptr;
    }
    if (!CreateVideoStaging(video)) {
        UnloadTexture(video->texture);
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
    if (video->staging) {
        if (video->stagingMapped) video->staging->Unmap(0, nullptr);
        // The GPU may still be reading the staging buffer, so hand it to the
        // deferred queue rather than releasing it here.
        auto& gpu = internal::GpuContext::Get();
        ComPtr<ID3D12Resource> staging = video->staging;
        gpu.Defer([staging]() mutable { staging.Reset(); });
    }
    if (video->texture) UnloadTexture(video->texture);
    if (video->reader) video->reader->Release();
    delete video;
}

void PlayVideo(Video* video) {
    if (!video) return;
    // Restarting from the top only makes sense when the clip is not already at
    // its start; calling Play() on a paused video should resume, not rewind.
    if (video->position > 0.0f || video->ended) SeekVideoInternal(video, 0.0);
    video->playing = true;
    video->ended = false;
    // Decode one frame straight away so the first DrawVideo() after Play() has a
    // picture instead of a black texture.
    if (!video->ready) {
        ReadAndUploadFrame(video);
        video->clock = 0.0;
    }
}

void StopVideo(Video* video) {
    if (!video) return;
    video->playing = false;
    video->ended = false;
    SeekVideoInternal(video, 0.0);
}

void PauseVideo(Video* video) {
    if (video) video->playing = false;
}

void ResumeVideo(Video* video) {
    if (!video) return;
    if (video->ended) {
        PlayVideo(video);
        return;
    }
    // The clock is kept at the paused position, so resuming does not jump.
    video->playing = true;
}

void SeekVideo(Video* video, float seconds) {
    if (!video) return;
    SeekVideoInternal(video, seconds);
    // Show the new position right away rather than waiting for the next tick.
    if (video->playing) ReadAndUploadFrame(video);
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
            // Rewind through the same path as a seek, so the playback clock is
            // reset with the reader; leaving the clock behind would make the next
            // tick decode the whole clip at once trying to catch up.
            SeekVideoInternal(video, 0.0);
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
    bool decoded = false;
    if (SUCCEEDED(buffer->Lock(&data, &maxLength, &currentLength))) {
        // The decoder's frame is tightly packed at `width * 4` bytes per row while
        // the staging buffer's rows follow the device's own pitch, so the copy walks
        // one row at a time. A single memcpy would run past the end of the decoder's
        // buffer on the last row whenever the two pitches differ.
        const size_t sourcePitch = static_cast<size_t>(video->width) * 4u;
        const size_t needed = sourcePitch * video->height;

        if (currentLength >= needed) {
            for (uint32_t y = 0; y < video->height; ++y) {
                uint8_t* destination = video->stagingMapped + video->footprint.Offset +
                                       static_cast<size_t>(y) * video->rowPitch;
                std::memcpy(destination, data + static_cast<size_t>(y) * sourcePitch, sourcePitch);
                // RGB32 is BGRA in memory; the engine samples RGBA8.
                for (uint32_t x = 0; x < video->width; ++x) {
                    std::swap(destination[x * 4 + 0], destination[x * 4 + 2]);
                }
            }
            decoded = true;
        } else {
            LogError("video: decoded frame is short (%lu bytes, wanted %zu)",
                     static_cast<unsigned long>(currentLength), needed);
        }
        buffer->Unlock();
    }
    buffer->Release();
    sample->Release();

    if (decoded) {
        video->position = static_cast<float>(timestamp) / 10000000.0f;
        // Sticky: once a frame has reached the texture, a later upload failure
        // should not make the video look unloaded again.
        if (UploadVideoFrame(video)) video->ready = true;
    }
    return decoded;
}

} // namespace

bool DrawVideo(Video* video, float x, float y, Color tint) {
    return DrawVideoV(video, Vec2{x, y}, tint);
}

bool DrawVideoV(Video* video, Vec2 position, Color tint) {
    if (!video || !video->texture || !video->ready) return false;
    const float w = static_cast<float>(video->width);
    const float h = static_cast<float>(video->height);
    DrawTexturePro(video->texture, Rect{0.0f, 0.0f, w, h},
                   Rect{position.x, position.y, w, h}, Vec2{}, 0.0f, tint);
    return true;
}

Texture* GetVideoTexture(Video* video) { return video ? video->texture : nullptr; }

void UpdateVideo() {
    // Advance every playing video by however many frames the wall clock says are
    // due. Pulling exactly one frame per call, as an earlier version did, made
    // playback speed a function of the frame rate: a 30 fps clip ran fast at
    // 120 fps and slow at 15 fps. The loop is bounded so a long stall (a
    // debugger break, a window drag) cannot turn into an unbounded decode burst.
    auto& window = internal::Window::Get();
    const double now = window.Time();
    const double delta = now - gLastUpdateTime;
    gLastUpdateTime = now;
    // A negative or absurd delta means the clock was reset or the process was
    // suspended; skip this tick rather than fast-forwarding the whole clip.
    const double elapsed = (delta > 0.0 && delta < 1.0) ? delta : 0.0;

    for (Video* video : gVideos) {
        if (!video || !video->playing) continue;

        video->clock += elapsed;

        const int maxFrames = 4;   // decode at most this many per tick
        for (int i = 0; i < maxFrames; ++i) {
            // Decode while the next frame is already due. `position` is the
            // timestamp of the frame in the texture, so this keeps the picture
            // no more than one frame behind the clock.
            if (video->position + video->frameInterval > video->clock) break;
            if (!ReadAndUploadFrame(video)) break;
            if (!video->playing) break;   // hit the end without looping
        }
    }
}

} // namespace n3d
