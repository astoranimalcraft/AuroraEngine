// NexiumEngine — storage backend.
//
// The drawer itself (Storage, StorageFile, the X-macro) is header-only so it works
// without the GPU and is covered by the native tests. What cannot live in a header
// is the dependency on the actual subsystems, so this file is the one place that
// knows how to load each kind and routes those through internal::gAssetOps.
//
// Init() calls InstallStorageOps(); before that the table is empty and a load
// simply reports failure.
#include "nexium/storage.h"

#include "nexium/audio.h"
#include "nexium/draw.h"
#include "nexium/log.h"
#include "nexium/model.h"
#include "nexium/shader.h"
#include "nexium/texture.h"
#include "nexium/video.h"

namespace n3d {
namespace internal {
namespace {

void* LoadTextureAsset(const char* path) {
    // A radiance map has to go through the HDR loader or its range is lost.
    if (PathHasExtension(path, "hdr")) return LoadHDRFromFile(path);
    return LoadTextureFromFile(path);
}

void* LoadFontAsset(const char* path, int glyphWidth, int glyphHeight, int columns) {
    return LoadFontFromImage(path, glyphWidth, glyphHeight, columns);
}

// Bridges the subsystem's typed unload to the void* the table carries.
template <typename T, void (*Unload)(T*)>
void UnloadAsset(void* handle) {
    Unload(static_cast<T*>(handle));
}

void LogStorageWarn(const char* message) { LogWarn("storage: %s", message); }

} // namespace

void InstallStorageOps() {
    gAssetOps.loadTexture = &LoadTextureAsset;
    gAssetOps.loadSound = [](const char* path) -> void* { return LoadSound(path); };
    gAssetOps.loadMusic = [](const char* path) -> void* { return LoadMusic(path); };
    gAssetOps.loadVideo = [](const char* path) -> void* { return LoadVideo(path); };
    gAssetOps.loadModel = [](const char* path) -> void* { return LoadModel(path); };
    gAssetOps.loadShader = [](const char* path) -> void* { return LoadShaderFromFile(path); };
    gAssetOps.loadFont = &LoadFontAsset;

    gAssetOps.unloadTexture = &UnloadAsset<Texture, UnloadTexture>;
    gAssetOps.unloadSound = &UnloadAsset<Sound, UnloadSound>;
    gAssetOps.unloadMusic = &UnloadAsset<Music, UnloadMusic>;
    gAssetOps.unloadVideo = &UnloadAsset<Video, UnloadVideo>;
    gAssetOps.unloadModel = &UnloadAsset<Model, UnloadModel>;
    gAssetOps.unloadShader = &UnloadAsset<Shader, UnloadShader>;

    gAssetOps.logWarn = &LogStorageWarn;
}

} // namespace internal
} // namespace n3d
