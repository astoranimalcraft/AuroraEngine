// NexiumEngine — storage.h
// Storage: a named drawer of assets.
//
// A game tends to grow a scatter of paths — a music track here, a model there, a
// shader somewhere else — and those paths end up duplicated across files with no
// single place to look. A Storage collects them: you declare the drawer once, give
// every file a short name, and from then on refer to the name, never the path.
//
//   // The simple form: a drawer you build at run time.
//   n3d::Storage Game = {
//       {"music", "assets/overworld.ogg"},
//       {"hero",  "assets/hero.obj"},
//       {"sky",   "assets/sky.hdr"},
//   };
//
//   Game["music"].AsMusic();   // loads on first use, then cached
//   Game.Count();              // how many files the drawer holds
//
//   // The named form: every file becomes a static member, so the compiler checks
//   // the name and the editor autocompletes it. Define the list once (the real
//   // definition continues each line with a backslash, omitted here for clarity):
//
//   #define GAME_FILES(X)
//       X(music, "assets/overworld.ogg")
//       X(hero,  "assets/hero.obj")
//       X(sky,   "assets/sky.hdr")
//
//   // ...then build the drawer from it. Placing this inside `namespace n3d`
//   // (or `namespace n2d`) is what makes n3d::Game::music work.
//   N3D_DEFINE_STORAGE(Game, GAME_FILES)
//
//   n3d::Game::music.AsMusic();
//   n3d::Game::Drawer().LoadAll();
//
// Loading is lazy and cached: the first As*() call for a file loads it, every
// later call returns the same handle, and Unload()/UnloadAll() release it. A file
// that fails to load returns nullptr (logged) rather than throwing, matching the
// rest of the engine.
//
// The registry half of this header is deliberately free of GPU and Windows types,
// so it is exercised by the native tests; the loaders live in src/storage.cpp.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace n3d {

struct Texture;
struct Sound;
struct Music;
struct Video;
struct Model;
struct Shader;
struct Font;

// What a file is, inferred from its extension. It is only a hint: the typed
// accessors (AsSound()/AsMusic(), ...) decide the loader, so a file the heuristic
// guesses wrong still loads correctly through the accessor you call.
enum class AssetKind : uint32_t {
    Unknown = 0,
    Texture,   // png, jpg, bmp, tga, hdr, dds
    Sound,     // short clip: wav
    Music,     // streamed track: ogg, mp3, flac
    Video,     // mp4, mkv, avi, mov, wmv
    Model,     // obj, fbx, gltf, glb, dae, 3ds
    Shader,    // hlsl, fx, hlsli
    Text,      // txt, json, csv, ini, cfg
    Binary,    // anything else
};

// Case-insensitive comparison of a path's extension against `extension`, which is
// given without the dot ("obj", not ".obj").
inline bool PathHasExtension(const char* path, const char* extension) {
    if (!path || !extension) return false;
    const char* dot = std::strrchr(path, '.');
    if (!dot) return false;
    const char* a = dot + 1;
    const char* b = extension;
    for (; *a && *b; ++a, ++b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

inline AssetKind KindFromPath(const char* path) {
    static const char* const textures[] = {"png", "jpg", "jpeg", "bmp", "tga", "gif",
                                           "dds", "hdr",  "ktx", nullptr};
    static const char* const music[] = {"ogg", "mp3", "flac", "m4a", nullptr};
    static const char* const videos[] = {"mp4", "mkv", "avi", "mov", "wmv", "webm", nullptr};
    static const char* const models[] = {"obj", "fbx", "gltf", "glb", "dae", "3ds", "ply", nullptr};
    static const char* const shaders[] = {"hlsl", "fx", "hlsli", "glsl", nullptr};
    static const char* const texts[] = {"txt", "json", "csv", "ini", "cfg", "md", "xml", nullptr};

    for (const char* const* e = textures; *e; ++e)
        if (PathHasExtension(path, *e)) return AssetKind::Texture;
    if (PathHasExtension(path, "wav")) return AssetKind::Sound;
    for (const char* const* e = music; *e; ++e)
        if (PathHasExtension(path, *e)) return AssetKind::Music;
    for (const char* const* e = videos; *e; ++e)
        if (PathHasExtension(path, *e)) return AssetKind::Video;
    for (const char* const* e = models; *e; ++e)
        if (PathHasExtension(path, *e)) return AssetKind::Model;
    for (const char* const* e = shaders; *e; ++e)
        if (PathHasExtension(path, *e)) return AssetKind::Shader;
    for (const char* const* e = texts; *e; ++e)
        if (PathHasExtension(path, *e)) return AssetKind::Text;
    if (!path || !*path) return AssetKind::Unknown;
    return AssetKind::Binary;
}

inline const char* AssetKindName(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return "texture";
        case AssetKind::Sound: return "sound";
        case AssetKind::Music: return "music";
        case AssetKind::Video: return "video";
        case AssetKind::Model: return "model";
        case AssetKind::Shader: return "shader";
        case AssetKind::Text: return "text";
        case AssetKind::Binary: return "binary";
        default: return "unknown";
    }
}

// Joins a base directory and a relative path with a single separator. An absolute
// path, or an empty base, is returned unchanged, so a file may always opt out of
// the drawer's base directory.
inline std::string JoinPath(const std::string& base, const char* path) {
    if (!path) return {};
    const bool absolute = path[0] == '/' || path[0] == '\\' ||
                          (path[0] && path[1] == ':');
    if (base.empty() || absolute) return path;
    std::string out = base;
    if (out.back() != '/' && out.back() != '\\') out += '/';
    out += path;
    return out;
}

namespace internal {

// The engine installs these during Init(), so a StorageFile can load and release a
// handle without storage.h depending on the GPU, audio or media subsystems — which
// is what keeps the registry compilable and testable off Windows. A null slot
// means the subsystem is not up, so the load reports failure instead of crashing.
struct AssetOps {
    void* (*loadTexture)(const char*) = nullptr;
    void* (*loadSound)(const char*) = nullptr;
    void* (*loadMusic)(const char*) = nullptr;
    void* (*loadVideo)(const char*) = nullptr;
    void* (*loadModel)(const char*) = nullptr;
    void* (*loadShader)(const char*) = nullptr;
    void* (*loadFont)(const char*, int, int, int) = nullptr;
    void (*unloadTexture)(void*) = nullptr;
    void (*unloadSound)(void*) = nullptr;
    void (*unloadMusic)(void*) = nullptr;
    void (*unloadVideo)(void*) = nullptr;
    void (*unloadModel)(void*) = nullptr;
    void (*unloadShader)(void*) = nullptr;
    // Formatting a warning needs the engine logger, which is not portable; the
    // header formats the text and the backend routes it.
    void (*logWarn)(const char*) = nullptr;
};

// Null until Init() runs; a null table makes every load report failure, which is
// the correct answer before there is a device.
inline AssetOps gAssetOps{};

// Routes a storage warning through the engine logger when the backend is up. The
// header cannot call LogWarn() itself because log.cpp is not portable.
inline void WarnStorage(const char* message) {
    if (gAssetOps.logWarn) gAssetOps.logWarn(message);
}

// Defined in src/storage.cpp; called by n3d::Init().
void InstallStorageOps();

} // namespace internal

// One file inside a Storage: a short name, a path, and (once asked for) a loaded
// handle. Copies are shallow in the sense that they never own the handle; a
// Storage is the single owner.
class StorageFile {
public:
    StorageFile() = default;
    StorageFile(const char* name, const char* path) { Reset(name, path); }

    void Reset(const char* name, const char* path) {
        name_ = name ? name : "";
        path_ = path ? path : "";
        kind_ = KindFromPath(path_.c_str());
        Unload();
    }

    const char* Name() const { return name_.c_str(); }
    const char* Path() const { return path_.c_str(); }
    AssetKind Kind() const { return kind_; }
    const char* KindName() const { return AssetKindName(kind_); }
    // True once the file has been loaded and the handle is live.
    bool IsLoaded() const { return loaded_; }
    // True when a name and a path are both set.
    bool IsValid() const { return !name_.empty() && !path_.empty(); }

    // Overrides the extension-derived kind. Only affects Load(); the typed
    // accessors still pick their own loader.
    void SetKind(AssetKind kind) { kind_ = kind; }

    // Resolves the path against `base` before loading. Called by Storage when the
    // entry is added; not usually needed directly.
    void SetBaseDirectory(const std::string& base) { base_ = base; }
    // The path that will actually be opened: base directory applied.
    std::string ResolvedPath() const { return JoinPath(base_, path_.c_str()); }

    // ------------------------------------------------------- typed access ---
    // Each loads on first use and caches the result. Returns nullptr on failure
    // (logged), so a missing file degrades instead of crashing.
    Texture* AsTexture() { return static_cast<Texture*>(Load()); }
    Sound* AsSound() { return static_cast<Sound*>(Load()); }
    Music* AsMusic() { return static_cast<Music*>(Load()); }
    Video* AsVideo() { return static_cast<Video*>(Load()); }
    Model* AsModel() { return static_cast<Model*>(Load()); }
    Shader* AsShader() { return static_cast<Shader*>(Load()); }
    // Fonts need the glyph grid the image was laid out with, which cannot be
    // guessed; this overload is the only one that can load.
    Font* AsFont(int glyphWidth, int glyphHeight, int columns);

    // Whole-file contents, for text and data files. Cached, so repeated calls do
    // not re-read the disk. Returns empty on failure.
    const std::string& AsText() {
        if (!attempted_) ReadBytes();
        if (text_.empty() && !bytes_.empty()) text_.assign(bytes_.begin(), bytes_.end());
        return text_;
    }
    const std::vector<uint8_t>& AsBytes() {
        if (!attempted_) ReadBytes();
        return bytes_;
    }

    // Loads using Kind(), returning the handle as void*. Prefer the typed calls.
    void* Load();
    // Releases whatever was loaded. Safe to call when nothing is.
    void Unload();

    // The handle from the last Load()/As*(), or nullptr.
    void* Raw() const { return handle_; }

private:
    // Reads the whole file into bytes_. Defined here because it needs no engine
    // subsystem, only the standard library.
    void ReadBytes() {
        attempted_ = true;
        bytes_.clear();
        if (!IsValid()) return;

        const std::string resolved = ResolvedPath();
        std::ifstream stream(resolved.c_str(), std::ios::binary);
        if (!stream) return;

        bytes_.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        loaded_ = !bytes_.empty();
    }

    std::string name_;
    std::string path_;
    std::string base_;
    AssetKind kind_ = AssetKind::Unknown;
    bool loaded_ = false;
    // A failed load must not be retried every frame, so the attempt is recorded
    // separately from the result.
    bool attempted_ = false;
    void* handle_ = nullptr;
    std::string text_;
    std::vector<uint8_t> bytes_;
};

// ---------------------------------------------------------------------------
// StorageFile::Load / Unload / AsFont. Inline in the header so the drawer works
// with no engine .cpp linked: the actual subsystems arrive through gAssetOps,
// which Init() fills in. Before that the table is empty and every load fails,
// which is the right answer when there is no device yet.
// ---------------------------------------------------------------------------
inline void StorageFile::Unload() {
    if (handle_) {
        internal::AssetOps& ops = internal::gAssetOps;
        void (*release)(void*) = nullptr;
        switch (kind_) {
            case AssetKind::Texture: release = ops.unloadTexture; break;
            case AssetKind::Sound: release = ops.unloadSound; break;
            case AssetKind::Music: release = ops.unloadMusic; break;
            case AssetKind::Video: release = ops.unloadVideo; break;
            case AssetKind::Model: release = ops.unloadModel; break;
            case AssetKind::Shader: release = ops.unloadShader; break;
            default: break;   // text/binary hold no engine handle
        }
        if (release) release(handle_);
    }
    handle_ = nullptr;
    loaded_ = false;
    attempted_ = false;
    text_.clear();
    bytes_.clear();
}

inline void* StorageFile::Load() {
    if (attempted_) return handle_;
    attempted_ = true;

    if (!IsValid()) {
        internal::WarnStorage("a storage file has no path");
        return nullptr;
    }

    const std::string resolved = ResolvedPath();
    const char* path = resolved.c_str();
    internal::AssetOps& ops = internal::gAssetOps;

    switch (kind_) {
        case AssetKind::Texture:
            // A radiance map has to go through the HDR loader or its range is lost,
            // and that choice lives in the backend.
            handle_ = ops.loadTexture ? ops.loadTexture(path) : nullptr;
            break;
        case AssetKind::Sound: handle_ = ops.loadSound ? ops.loadSound(path) : nullptr; break;
        case AssetKind::Music: handle_ = ops.loadMusic ? ops.loadMusic(path) : nullptr; break;
        case AssetKind::Video: handle_ = ops.loadVideo ? ops.loadVideo(path) : nullptr; break;
        case AssetKind::Model: handle_ = ops.loadModel ? ops.loadModel(path) : nullptr; break;
        case AssetKind::Shader: handle_ = ops.loadShader ? ops.loadShader(path) : nullptr; break;
        case AssetKind::Text:
        case AssetKind::Binary:
        default:
            // Raw files are read whole; AsText()/AsBytes() serve them from here.
            attempted_ = false;
            ReadBytes();
            return handle_;
    }

    if (!handle_) {
        char message[512];
        std::snprintf(message, sizeof(message), "could not load '%s' (%s) from %s", name_.c_str(),
                      KindName(), path);
        internal::WarnStorage(message);
    }
    loaded_ = handle_ != nullptr;
    return handle_;
}

inline Font* StorageFile::AsFont(int glyphWidth, int glyphHeight, int columns) {
    if (handle_) {
        internal::WarnStorage("this file was already loaded with a different accessor");
        return nullptr;
    }
    if (!IsValid()) return nullptr;

    internal::AssetOps& ops = internal::gAssetOps;
    const std::string resolved = ResolvedPath();
    handle_ = ops.loadFont ? ops.loadFont(resolved.c_str(), glyphWidth, glyphHeight, columns)
                           : nullptr;
    kind_ = AssetKind::Texture;
    attempted_ = true;
    loaded_ = handle_ != nullptr;
    return static_cast<Font*>(handle_);
}

// A drawer of StorageFile. Owns the entries added with Add()/operator[] and may
// also reference entries it does not own (the static members of a N3D_DEFINE_STORAGE
// drawer), so the two forms can coexist.
//
// Move-only: a drawer is a single object, not a value to copy around.
class Storage {
public:
    Storage() = default;
    Storage(std::initializer_list<StorageFile> files) {
        for (const StorageFile& file : files) {
            StorageFile* copy = Own(new StorageFile(file));
            files_.push_back(copy);
        }
    }

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) = default;
    Storage& operator=(Storage&&) = default;

    // Directory prepended to every relative path in the drawer.
    Storage& SetBaseDirectory(const char* directory) {
        base_ = directory ? directory : "";
        for (StorageFile* file : files_) file->SetBaseDirectory(base_);
        return *this;
    }
    const char* BaseDirectory() const { return base_.c_str(); }

    // Adds a file the drawer owns. Returns the stored entry.
    StorageFile& Add(const char* name, const char* path) {
        StorageFile* file = Own(new StorageFile(name, path));
        file->SetBaseDirectory(base_);
        files_.push_back(file);
        return *file;
    }

    // References a file the drawer does not own (a static member, typically).
    StorageFile& Attach(StorageFile& file) {
        file.SetBaseDirectory(base_);
        files_.push_back(&file);
        return file;
    }

    // Number of files in the drawer.
    int Count() const { return static_cast<int>(files_.size()); }

    // The entry at `index`, or nullptr when out of range.
    StorageFile* At(int index) {
        if (index < 0 || index >= Count()) return nullptr;
        return files_[static_cast<size_t>(index)];
    }

    // Looks up by name; nullptr when the drawer has no such file.
    StorageFile* Find(const char* name) {
        if (!name) return nullptr;
        for (StorageFile* file : files_) {
            if (std::strcmp(file->Name(), name) == 0) return file;
        }
        return nullptr;
    }
    const StorageFile* Find(const char* name) const {
        return const_cast<Storage*>(this)->Find(name);
    }

    bool Contains(const char* name) const { return Find(name) != nullptr; }

    // Looks up by name, creating an empty entry when it is missing (map-like).
    // Use Find() when a miss should stay a miss.
    StorageFile& operator[](const char* name) {
        if (StorageFile* file = Find(name)) return *file;
        return Add(name, "");
    }

    // Loads / releases every file in the drawer.
    void LoadAll() {
        for (StorageFile* file : files_) file->Load();
    }
    void UnloadAll() {
        for (StorageFile* file : files_) file->Unload();
    }

    // Iteration, so `for (StorageFile* f : drawer)` works.
    StorageFile** begin() { return files_.data(); }
    StorageFile** end() { return files_.data() + files_.size(); }

private:
    StorageFile* Own(StorageFile* file) {
        owned_.emplace_back(file);
        return file;
    }

    std::vector<StorageFile*> files_;
    std::vector<std::unique_ptr<StorageFile>> owned_;
    std::string base_;
};

// Loads a drawer from a text manifest, one `name = path` per line ('#' comments
// and blank lines ignored). Merges into an existing drawer and returns how many
// entries were read, so a game can keep its code-declared files and let a
// designer add more from disk.
inline int LoadStorageManifest(Storage& storage, const char* manifestPath) {
    std::ifstream stream(manifestPath);
    if (!stream) return 0;

    auto trim = [](const std::string& text) {
        const char* spaces = " \t\r\n";
        const size_t first = text.find_first_not_of(spaces);
        if (first == std::string::npos) return std::string{};
        const size_t last = text.find_last_not_of(spaces);
        return text.substr(first, last - first + 1);
    };

    int added = 0;
    std::string line;
    while (std::getline(stream, line)) {
        const size_t comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);

        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string name = trim(line.substr(0, equals));
        const std::string path = trim(line.substr(equals + 1));
        if (name.empty() || path.empty()) continue;

        if (StorageFile* existing = storage.Find(name.c_str())) {
            existing->Reset(name.c_str(), path.c_str());
        } else {
            storage.Add(name.c_str(), path.c_str());
        }
        ++added;
    }
    return added;
}

} // namespace n3d

// ------------------------------------------------------------ X-macro form ---
// N3D_DEFINE_STORAGE(NAME, LIST) builds a drawer whose files are static members:
//
//   #define FILES(X) X(music, "a.ogg") X(hero, "hero.obj")
//   N3D_DEFINE_STORAGE(Game, FILES)
//
// Then `Game::music` is the StorageFile, and `Game::Drawer()` is the Storage that
// holds them all. Put the definition inside a namespace to qualify it: with
// `namespace n3d { ... }` around it, `n3d::Game::music` is exactly the
// "drawer :: file" spelling.
#define N3D_STORAGE_MEMBER(id, path) inline static ::n3d::StorageFile id{#id, path};
#define N3D_STORAGE_ATTACH(id, path) storage.Attach(id);
#define N3D_DEFINE_STORAGE(NAME, LIST)                                          \
    struct NAME {                                                               \
        LIST(N3D_STORAGE_MEMBER)                                                \
        static ::n3d::Storage& Drawer() {                                       \
            static ::n3d::Storage storage = Build();                            \
            return storage;                                                     \
        }                                                                       \
        static ::n3d::Storage Build() {                                         \
            ::n3d::Storage storage;                                             \
            LIST(N3D_STORAGE_ATTACH)                                            \
            return storage;                                                     \
        }                                                                       \
        static ::n3d::StorageFile* Get(const char* name) {                      \
            return Drawer().Find(name);                                         \
        }                                                                       \
        static int Count() { return Drawer().Count(); }                         \
        static void LoadAll() { Drawer().LoadAll(); }                           \
        static void UnloadAll() { Drawer().UnloadAll(); }                       \
    };
