// Native test for the Storage drawer. Exercises the platform-independent half:
// kinds, path resolution, name lookup, the X-macro drawer, and manifest parsing.
//
// It deliberately never calls a typed As*() accessor, because those reach the GPU
// and audio subsystems and this test runs on the host without DirectX. What is
// checked here is exactly the part a game gets wrong: the names and the paths.
#include "nexium/storage.h"

#include <cstdio>
#include <fstream>
#include <string>

static int gChecks = 0;
static int gFailures = 0;

static void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("  FAIL: %s\n", what);
    }
}

static void CheckStr(const char* got, const char* want, const char* what) {
    ++gChecks;
    if (std::strcmp(got ? got : "", want ? want : "") != 0) {
        ++gFailures;
        std::printf("  FAIL: %s (got '%s', want '%s')\n", what, got ? got : "(null)",
                    want ? want : "(null)");
    }
}

// The X-macro form. Declared in namespace n3d, which is what makes n3d::Game::music
// and (through the n2d alias) n2d::Game::music name the same file.
#define GAME_FILES(X)                            \
    X(music, "assets/overworld.ogg")             \
    X(hero, "assets/hero.obj")                   \
    X(sky, "assets/sky.hdr")                     \
    X(click, "assets/click.wav")

namespace n3d {
N3D_DEFINE_STORAGE(Game, GAME_FILES)
} // namespace n3d

namespace n2d = n3d;

int main() {
    // ------------------------------------------------------------- kinds ---
    Check(n3d::KindFromPath("a.png") == n3d::AssetKind::Texture, "png is a texture");
    Check(n3d::KindFromPath("a.HDR") == n3d::AssetKind::Texture, "HDR is a texture");
    Check(n3d::KindFromPath("a.wav") == n3d::AssetKind::Sound, "wav is a sound");
    Check(n3d::KindFromPath("a.ogg") == n3d::AssetKind::Music, "ogg is music");
    Check(n3d::KindFromPath("a.mp4") == n3d::AssetKind::Video, "mp4 is video");
    Check(n3d::KindFromPath("a.OBJ") == n3d::AssetKind::Model, "OBJ is a model");
    Check(n3d::KindFromPath("a.hlsl") == n3d::AssetKind::Shader, "hlsl is a shader");
    Check(n3d::KindFromPath("a.json") == n3d::AssetKind::Text, "json is text");
    Check(n3d::KindFromPath("noext") == n3d::AssetKind::Binary, "no extension is binary");
    Check(n3d::KindFromPath("") == n3d::AssetKind::Unknown, "empty path is unknown");

    // Extension matching must not be fooled by a prefix or a missing dot.
    Check(!n3d::PathHasExtension("a.pngx", "png"), "pngx is not png");
    Check(n3d::PathHasExtension("dir/a.b.png", "png"), "last extension wins");

    // ----------------------------------------------------------- join path ---
    CheckStr(n3d::JoinPath("assets", "a.png").c_str(), "assets/a.png", "join adds a slash");
    CheckStr(n3d::JoinPath("assets/", "a.png").c_str(), "assets/a.png", "join avoids a double slash");
    CheckStr(n3d::JoinPath("", "a.png").c_str(), "a.png", "empty base is a no-op");
    CheckStr(n3d::JoinPath("assets", "/abs/a.png").c_str(), "/abs/a.png", "absolute path wins");
    CheckStr(n3d::JoinPath("assets", "C:/a.png").c_str(), "C:/a.png", "drive path wins");

    // --------------------------------------------------- runtime drawer ---
    n3d::Storage drawer = {
        {"music", "assets/overworld.ogg"},
        {"hero", "assets/hero.obj"},
    };
    Check(drawer.Count() == 2, "initializer list adds two files");
    Check(drawer.Contains("music"), "contains finds a file");
    Check(!drawer.Contains("nope"), "contains misses an absent file");
    CheckStr(drawer["hero"].Path(), "assets/hero.obj", "operator[] reads the path");
    Check(drawer["hero"].Kind() == n3d::AssetKind::Model, "hero is a model");
    Check(drawer.Find("music") != nullptr, "find returns an entry");
    Check(drawer.Find("ghost") == nullptr, "find returns null for a miss");
    Check(drawer.At(0) != nullptr && drawer.At(1) != nullptr, "at() indexes entries");
    Check(drawer.At(2) == nullptr, "at() past the end is null");
    Check(drawer.At(-1) == nullptr, "at() before the start is null");

    // operator[] on a miss creates a slot, which is what makes map-style writes work.
    const int before = drawer.Count();
    drawer["late"] = n3d::StorageFile("late", "assets/late.bin");
    Check(drawer.Count() == before + 1, "operator[] on a miss adds an entry");

    // Base directory applies to relative paths and not to absolute ones.
    drawer.SetBaseDirectory("data");
    CheckStr(drawer["hero"].ResolvedPath().c_str(), "data/assets/hero.obj", "base dir is applied");
    drawer.Add("abs", "/tmp/abs.png");
    CheckStr(drawer["abs"].ResolvedPath().c_str(), "/tmp/abs.png", "base dir skips absolute paths");

    // A file with no path is not valid and must not be loadable.
    n3d::Storage empty;
    empty.Add("blank", "");
    Check(!empty["blank"].IsValid(), "a pathless file is invalid");
    Check(empty["blank"].Load() == nullptr, "loading a pathless file returns null");
    Check(empty["blank"].IsLoaded() == false, "a failed load stays unloaded");

    // ------------------------------------------------------ X-macro form ---
    Check(n3d::Game::Count() == 4, "the macro drawer has four files");
    CheckStr(n3d::Game::music.Path(), "assets/overworld.ogg", "Game::music path");
    CheckStr(n3d::Game::hero.Name(), "hero", "Game::hero name");
    Check(n3d::Game::sky.Kind() == n3d::AssetKind::Texture, "Game::sky is a texture");
    Check(n3d::Game::click.Kind() == n3d::AssetKind::Sound, "Game::click is a sound");
    Check(n3d::Game::Get("hero") == &n3d::Game::hero, "Get(name) finds the static member");
    Check(n3d::Game::Get("ghost") == nullptr, "Get(miss) is null");
    // The whole point of the alias: the same file under the 2D spelling.
    Check(n2d::Game::music.Path() == n3d::Game::music.Path(), "n2d:: reaches the same drawer");

    // The drawer is a singleton: two calls hand back the same object.
    Check(&n3d::Game::Drawer() == &n3d::Game::Drawer(), "Drawer() is stable");

    // --------------------------------------------------------- manifest ---
    const char* manifestPath = "storage_test_manifest.txt";
    {
        std::ofstream out(manifestPath);
        out << "# a comment\n";
        out << "\n";
        out << "  title  =  assets/title.png  \n";
        out << "not a pair\n";
        out << "song=assets/song.ogg\n";
        out << "trailing = assets/x.png # inline comment\n";
    }

    n3d::Storage fromFile;
    const int added = n3d::LoadStorageManifest(fromFile, manifestPath);
    Check(added == 3, "manifest parses three pairs");
    CheckStr(fromFile["title"].Path(), "assets/title.png", "manifest trims whitespace");
    CheckStr(fromFile["song"].Path(), "assets/song.ogg", "manifest reads a tight pair");
    CheckStr(fromFile["trailing"].Path(), "assets/x.png", "manifest strips a trailing comment");

    std::remove(manifestPath);
    Check(n3d::LoadStorageManifest(fromFile, "does_not_exist.txt") == 0,
          "a missing manifest adds nothing");

    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
