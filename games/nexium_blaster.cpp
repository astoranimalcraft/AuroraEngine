// NexiumEngine — game: Nexium Blaster.
//
// A complete, self-contained first-person shooting gallery built only on the
// NexiumEngine public API. It exists to show what a real game loop looks like on top
// of the library: a first-person camera, mouse picking with a ray, collision
// tests, a HUD in the 2D overlay pass, particles, camera shake and sound.
//
// There are no asset files. The audio cues are synthesised into WAV files in the
// working directory at startup and loaded from there, so running the game next to
// the executable is enough. If there is no audio device the sounds are silently
// skipped and everything else keeps working.
//
// Controls
//   W A S D   walk          mouse   look
//   left      fire          R       reload
//   P         pause         space   start
//   esc       quit
//
// Build it with ./scripts/verify.sh, or by hand:
//   x86_64-w64-mingw32-g++ -std=c++20 -Iinclude -Isrc games/nexium_blaster.cpp
//       src/*.cpp src/internal/*.cpp -ld3d12 -ldxgi -ld3dcompiler -lwindowscodecs
//       -lole32 -luuid -luser32 -lgdi32 -lshcore -lxinput -lxaudio2_8 -lmfplat
//       -lmfreadwrite -lmfuuid -o nexium_blaster.exe
#include <nexium/NexiumEngine.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

// Application code, not a header: importing the namespace keeps the game loop
// readable. The library's own headers stay explicit about n3d::.
using namespace n3d;

namespace {

// ----------------------------------------------------------------- tuning ----

constexpr int   kTargetCount   = 6;       // simultaneous floating targets
constexpr float kRoundSeconds  = 60.0f;   // length of a round
constexpr int   kMagSize       = 12;      // rounds per magazine
constexpr float kReloadSeconds = 0.85f;
constexpr float kFireInterval  = 0.11f;   // minimum time between shots
constexpr float kArenaRadius   = 14.0f;   // the ring wall the player stays inside
constexpr float kEyeHeight     = 1.7f;
constexpr int   kSampleRate    = 44100;

// Colours for the floating targets, so a fresh one is easy to tell from an old.
constexpr Color kTargetColors[6] = {
    colors::Red, colors::Orange, colors::Gold, colors::Lime, colors::Cyan, colors::Magenta,
};

enum class State { Menu, Playing, Paused, GameOver };

// ------------------------------------------------------------------- audio ---
// NexiumEngine only loads RIFF/WAVE from disk, so the cues are generated into small
// files once. LoadSound() decodes the whole clip into memory, so the files can be
// removed immediately afterwards.

void WriteWav16(const char* path, const std::vector<float>& samples) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;

    const uint32_t dataSize = static_cast<uint32_t>(samples.size() * 2u);
    const auto u32 = [&out](uint32_t value) {
        const char bytes[4] = {static_cast<char>(value & 0xFFu),
                               static_cast<char>((value >> 8) & 0xFFu),
                               static_cast<char>((value >> 16) & 0xFFu),
                               static_cast<char>((value >> 24) & 0xFFu)};
        out.write(bytes, 4);
    };
    const auto u16 = [&out](uint16_t value) {
        const char bytes[2] = {static_cast<char>(value & 0xFFu),
                               static_cast<char>((value >> 8) & 0xFFu)};
        out.write(bytes, 2);
    };

    out.write("RIFF", 4);
    u32(36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16u);                       // PCM header size
    u16(1u);                        // format = PCM
    u16(1u);                        // mono
    u32(static_cast<uint32_t>(kSampleRate));
    u32(static_cast<uint32_t>(kSampleRate * 2));   // byte rate
    u16(2u);                        // block align
    u16(16u);                       // bits per sample
    out.write("data", 4);
    u32(dataSize);

    for (const float sample : samples) {
        const int value = static_cast<int>(Clamp(sample, -1.0f, 1.0f) * 32767.0f);
        u16(static_cast<uint16_t>(value & 0xFFFF));
    }
}

// A short, pitched "pew": a square wave that falls in pitch, gated by an envelope
// so it clicks off instead of ending abruptly.
std::vector<float> MakeShootSound() {
    const int length = kSampleRate * 10 / 100;   // 0.10 s
    std::vector<float> out(static_cast<size_t>(length));
    float phase = 0.0f;
    for (int i = 0; i < length; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float frequency = Lerp(900.0f, 220.0f, t / 0.10f);
        phase += frequency / static_cast<float>(kSampleRate);
        const float square = std::sin(phase * TAU) >= 0.0f ? 1.0f : -1.0f;
        const float envelope = 1.0f - t / 0.10f;
        out[static_cast<size_t>(i)] = square * envelope * envelope * 0.35f;
    }
    return out;
}

// A rising two-note "ding" for a hit.
std::vector<float> MakeHitSound() {
    const int length = kSampleRate * 14 / 100;   // 0.14 s
    std::vector<float> out(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float frequency = t < 0.05f ? 660.0f : 990.0f;
        const float envelope = 1.0f - t / 0.14f;
        out[static_cast<size_t>(i)] = std::sin(t * frequency * TAU) * envelope * 0.4f;
    }
    return out;
}

// A dry click, used for the reload.
std::vector<float> MakeReloadSound() {
    const int length = kSampleRate * 12 / 100;   // 0.12 s
    std::vector<float> out(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        // Two ticks, at the ends of the clip.
        const float tickA = std::exp(-t * 120.0f);
        const float tickB = std::exp(-std::fabs(t - 0.07f) * 160.0f);
        const float noise = std::sin(t * 3100.0f * TAU);
        out[static_cast<size_t>(i)] = noise * (tickA + tickB) * 0.18f;
    }
    return out;
}

// A dull thud when the trigger is pulled on an empty magazine.
std::vector<float> MakeEmptySound() {
    const int length = kSampleRate * 7 / 100;   // 0.07 s
    std::vector<float> out(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float envelope = std::exp(-t * 70.0f);
        out[static_cast<size_t>(i)] = std::sin(t * 150.0f * TAU) * envelope * 0.3f;
    }
    return out;
}

// ----------------------------------------------------------------- entities --

struct Target {
    Vec3  anchor{};          // bob-free home position
    Vec3  position{};        // animated position, what is drawn and hit-tested
    float radius = 0.5f;
    Color color = colors::Red;
    float bobPhase = 0.0f;
    float bobAmplitude = 0.25f;
    float spin = 0.0f;
    float life = 0.0f;       // seconds until it relocates on its own
    float dying = 0.0f;      // > 0 while the pop animation plays
    bool  alive = false;
};

struct Shard {
    Vec3  position{};
    Vec3  velocity{};
    float life = 0.0f;
    Color color = colors::White;
};

struct Tracer {
    Vec3  from{};
    Vec3  to{};
    float life = 0.0f;
};

// --------------------------------------------------------------- game state --

struct Game {
    State state = State::Menu;

    FpsCamera camera;
    CameraShake shake;

    Target targets[kTargetCount];
    std::vector<Shard> shards;
    std::vector<Tracer> tracers;
    Random random{0xA0B0C0D0u};

    Timer fireCooldown{kFireInterval};
    Timer reloadTimer{kReloadSeconds};

    float roundTime = kRoundSeconds;
    float muzzleFlash = 0.0f;   // seconds the flash stays lit
    float recoil = 0.0f;        // 0..1, decays after each shot

    int score = 0;
    int best = 0;
    int shots = 0;
    int hits = 0;
    int combo = 0;
    int ammo = kMagSize;
    bool reloading = false;

    Sound* shootSound = nullptr;
    Sound* hitSound = nullptr;
    Sound* reloadSound = nullptr;
    Sound* emptySound = nullptr;
};

// A random point in front of the player, inside the arena and above the floor.
Vec3 RandomTargetAnchor(Game& game) {
    const float angle = game.random.Range(-1.20f, 1.20f);        // +-69 degrees around +Z
    const float distance = game.random.Range(7.0f, 13.0f);
    const float height = game.random.Range(1.2f, 4.0f);
    return Vec3{std::sin(angle) * distance, height, std::cos(angle) * distance};
}

void SpawnTarget(Game& game, int index) {
    Target& target = game.targets[index];
    target.anchor = RandomTargetAnchor(game);
    target.position = target.anchor;
    target.radius = game.random.Range(0.35f, 0.7f);
    target.color = kTargetColors[game.random.RangeInt(0, 5)];
    target.bobPhase = game.random.Range(0.0f, TAU);
    target.bobAmplitude = game.random.Range(0.15f, 0.4f);
    target.spin = game.random.Range(20.0f, 90.0f) * game.random.Sign();
    target.life = game.random.Range(5.0f, 9.0f);
    target.dying = 0.0f;
    target.alive = true;
}

void SpawnShards(Game& game, Vec3 origin, Color color) {
    constexpr int count = 14;
    for (int i = 0; i < count; ++i) {
        Shard shard;
        shard.position = origin;
        shard.velocity = game.random.OnSphere(game.random.Range(2.5f, 6.5f));
        shard.velocity.y = std::fabs(shard.velocity.y) + 1.5f;   // always pop upwards
        shard.life = game.random.Range(0.35f, 0.75f);
        shard.color = color;
        game.shards.push_back(shard);
    }
}

void AddTracer(Game& game, Vec3 from, Vec3 to, float life) {
    Tracer tracer;
    tracer.from = from;
    tracer.to = to;
    tracer.life = life;
    game.tracers.push_back(tracer);
}

void ResetRound(Game& game) {
    game.roundTime = kRoundSeconds;
    game.score = 0;
    game.shots = 0;
    game.hits = 0;
    game.combo = 0;
    game.ammo = kMagSize;
    game.reloading = false;
    game.shards.clear();
    game.tracers.clear();
    game.camera = FpsCamera{};
    game.camera.position = Vec3{0.0f, kEyeHeight, 0.0f};
    game.camera.yaw = 0.0f;
    game.shake = CameraShake{};
    game.fireCooldown.Stop();
    game.reloadTimer.Stop();
    for (int i = 0; i < kTargetCount; ++i) SpawnTarget(game, i);
}

void StartReload(Game& game) {
    if (game.reloading || game.ammo == kMagSize) return;
    game.reloading = true;
    game.reloadTimer.Restart();
    if (game.reloadSound) PlaySound(game.reloadSound);
}

// Fires one round: raycast from the crosshair, award points on a hit, spawn the
// effects either way. This is the heart of the game and uses three library
// features at once — camera picking, sphere collision and the shake.
void Fire(Game& game, float width, float height) {
    game.shots++;
    game.ammo--;
    game.fireCooldown.Restart();
    game.recoil = 1.0f;
    game.muzzleFlash = 0.05f;

    const Ray ray = game.camera.ToCamera().RayThroughScreen(
        Vec2{width * 0.5f, height * 0.5f}, width, height);

    const Vec3 muzzle = game.camera.position + game.camera.Forward() * 0.8f +
                        game.camera.Right() * 0.18f -
                        Cross(game.camera.Right(), game.camera.Forward()) * 0.14f;

    // Nearest target the ray passes through.
    int bestIndex = -1;
    float bestDistance = 1e30f;
    for (int i = 0; i < kTargetCount; ++i) {
        const Target& target = game.targets[i];
        if (!target.alive || target.dying > 0.0f) continue;
        float distance = 0.0f;
        if (CheckCollisionRaySphere(ray, target.position, target.radius, &distance) &&
            distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    if (bestIndex >= 0) {
        Target& target = game.targets[bestIndex];
        game.hits++;
        game.combo++;

        // Farther targets are worth more; a combo multiplies the whole thing.
        const int base = 50 + static_cast<int>(bestDistance * 8.0f);
        const float multiplier = 1.0f + 0.25f * static_cast<float>(Min(game.combo, 8) - 1);
        game.score += static_cast<int>(static_cast<float>(base) * multiplier);

        AddTracer(game, muzzle, target.position, 0.09f);
        SpawnShards(game, target.position, target.color);
        game.shake.Add(0.22f);
        target.dying = 0.22f;
        if (game.hitSound) PlaySoundMulti(game.hitSound, 2.0f);
    } else {
        game.combo = 0;
        const Vec3 end = ray.origin + ray.direction * 40.0f;
        AddTracer(game, muzzle, end, 0.05f);
        game.shake.Add(0.08f);
    }

    if (game.shootSound) PlaySoundMulti(game.shootSound, 1.5f);
}

void UpdateTargets(Game& game, float time, float deltaTime) {
    for (int i = 0; i < kTargetCount; ++i) {
        Target& target = game.targets[i];
        if (!target.alive) continue;

        if (target.dying > 0.0f) {
            target.dying -= deltaTime;
            if (target.dying <= 0.0f) {
                SpawnTarget(game, i);
            }
            continue;
        }

        // Bob around the anchor and relocate when the lifetime runs out.
        target.position = target.anchor +
                          Vec3{0.0f, std::sin(time * 1.7f + target.bobPhase) * target.bobAmplitude,
                               0.0f};
        target.life -= deltaTime;
        if (target.life <= 0.0f) SpawnTarget(game, i);
    }
}

void UpdateShards(Game& game, float deltaTime) {
    for (size_t i = 0; i < game.shards.size();) {
        Shard& shard = game.shards[i];
        shard.life -= deltaTime;
        if (shard.life <= 0.0f) {
            game.shards[i] = game.shards.back();
            game.shards.pop_back();
            continue;
        }
        shard.velocity.y -= 12.0f * deltaTime;
        shard.position += shard.velocity * deltaTime;
        ++i;
    }
}

void UpdateTracers(Game& game, float deltaTime) {
    for (size_t i = 0; i < game.tracers.size();) {
        game.tracers[i].life -= deltaTime;
        if (game.tracers[i].life <= 0.0f) {
            game.tracers[i] = game.tracers.back();
            game.tracers.pop_back();
            continue;
        }
        ++i;
    }
}

// Keeps the player inside the ring and on the floor, whatever the camera did.
void ClampPlayer(Game& game) {
    Vec3 position = game.camera.position;
    const float radius = std::sqrt(position.x * position.x + position.z * position.z);
    const float limit = kArenaRadius - 1.5f;
    if (radius > limit) {
        position.x = position.x / radius * limit;
        position.z = position.z / radius * limit;
    }
    position.y = kEyeHeight;
    game.camera.position = position;
}

// ---------------------------------------------------------------- rendering --

void DrawArena() {
    DrawPlane(Vec3{0.0f, 0.0f, 0.0f}, Vec2{kArenaRadius * 2.0f, kArenaRadius * 2.0f},
              Color::FromBytes(28, 42, 36));
    DrawGrid(kArenaRadius, kArenaRadius, colors::DarkGray);
    DrawTorus(Vec3{0.0f, 0.02f, 0.0f}, kArenaRadius, 0.1f, colors::SlateGray);

    // Eight pillars with pyramid caps, evenly spaced on the ring.
    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * TAU / 8.0f;
        const Vec3 base{std::sin(angle) * kArenaRadius, 0.0f, std::cos(angle) * kArenaRadius};
        DrawCylinder(Vec3{base.x, 1.6f, base.z}, 0.35f, 3.2f, colors::DarkGray);
        DrawPyramid(Vec3{base.x, 3.2f, base.z}, 0.7f, 0.7f, colors::Gray);
    }
}

void DrawTargets(const Game& game, float time) {
    for (int i = 0; i < kTargetCount; ++i) {
        const Target& target = game.targets[i];
        if (!target.alive) continue;

        if (target.dying > 0.0f) {
            // A shrinking core plus an expanding wire ring: the "pop".
            const float progress = 1.0f - Clamp(target.dying / 0.22f, 0.0f, 1.0f);
            DrawSphere(target.position, target.radius * (1.0f - progress) * 0.5f,
                       target.color.WithAlpha(1.0f - progress));
            DrawWireSphere(target.position, target.radius * (1.0f + progress * 2.0f),
                           target.color.WithAlpha(0.7f * (1.0f - progress)), 6, 12);
            continue;
        }

        DrawSphere(target.position, target.radius, target.color);
        // A ring that spins around the target, so motion is obvious at a glance.
        const float spin = Radians(time * target.spin);
        const Vec3 offset{std::cos(spin) * (target.radius + 0.25f), 0.0f,
                          std::sin(spin) * (target.radius + 0.25f)};
        DrawSphere(target.position + offset, target.radius * 0.18f, colors::White);
    }
}

void DrawShards(const Game& game) {
    for (const Shard& shard : game.shards) {
        const float alpha = Clamp(shard.life / 0.5f, 0.0f, 1.0f);
        const Vec3 tail = shard.position - Normalize(shard.velocity) * 0.25f;
        DrawLine(shard.position, tail, shard.color.WithAlpha(alpha));
    }
}

void DrawTracers(const Game& game) {
    for (const Tracer& tracer : game.tracers) {
        const float alpha = Clamp(tracer.life / 0.09f, 0.0f, 1.0f);
        DrawLine(tracer.from, tracer.to, colors::Yellow.WithAlpha(alpha));
    }
}

// A simple two-piece gun glued to the camera, with a recoil kick.
void DrawWeapon(const Game& game) {
    const Vec3 forward = game.camera.Forward();
    const Vec3 right = game.camera.Right();
    const Vec3 up = Cross(right, forward);
    const float kick = game.recoil * 0.08f;

    const Vec3 grip = game.camera.position + forward * (0.55f - kick) + right * 0.24f -
                      up * 0.26f;
    const Vec3 muzzle = game.camera.position + forward * (1.05f - kick) + right * 0.17f -
                        up * 0.14f;

    DrawCylinderEx(grip, muzzle, 0.045f, colors::DarkGray);
    DrawCube(muzzle, Vec3{0.09f, 0.09f, 0.09f}, colors::Gray);

    if (game.muzzleFlash > 0.0f) {
        SetBlendMode(BlendMode::Additive);
        DrawSphere(muzzle + forward * 0.12f, 0.18f, colors::Yellow.WithAlpha(0.8f));
        SetBlendMode(BlendMode::Opaque);
    }
}

void DrawHud(const Game& game, float width, float height) {
    // Top-left status panel.
    DrawRectangle(14.0f, 14.0f, 300.0f, 156.0f, colors::Black.WithAlpha(0.45f));
    DrawTextFormat(26, 24, 26.0f, colors::White, "SCORE  %d", game.score);
    DrawTextFormat(26, 58, 18.0f, colors::Gold, "BEST   %d", game.best);

    const float accuracy = game.shots > 0
                               ? 100.0f * static_cast<float>(game.hits) /
                                     static_cast<float>(game.shots)
                               : 0.0f;
    DrawTextFormat(26, 84, 18.0f, colors::LightGray, "ACC %.0f%%   COMBO x%d",
                   static_cast<double>(accuracy), game.combo);

    // Round timer, red when it is nearly up.
    const float fraction = Clamp(game.roundTime / kRoundSeconds, 0.0f, 1.0f);
    const Color timeColor = game.roundTime < 10.0f ? colors::Red : colors::White;
    DrawRectangle(26.0f, 110.0f, 276.0f, 14.0f, colors::DarkGray);
    DrawRectangle(26.0f, 110.0f, 276.0f * fraction, 14.0f, timeColor);
    DrawTextFormat(26, 132, 16.0f, colors::LightGray, "%.1f s",
                   static_cast<double>(game.roundTime));

    // Ammunition pips, bottom-centre.
    constexpr float pipWidth = 10.0f;
    const float totalWidth = static_cast<float>(kMagSize) * (pipWidth + 4.0f);
    const float startX = (width - totalWidth) * 0.5f;
    for (int i = 0; i < kMagSize; ++i) {
        const Color color = i < game.ammo ? colors::Gold : colors::DarkGray;
        DrawRectangle(startX + static_cast<float>(i) * (pipWidth + 4.0f), height - 40.0f,
                      pipWidth, 22.0f, color);
    }
    if (game.reloading) {
        DrawTextCentered("RELOADING", width * 0.5f, height - 66.0f, 20.0f, colors::Orange);
    } else if (game.ammo == 0) {
        DrawTextCentered("PRESS R TO RELOAD", width * 0.5f, height - 66.0f, 20.0f, colors::Red);
    }

    // Crosshair: four ticks and a ring, at the screen centre.
    const float cx = width * 0.5f;
    const float cy = height * 0.5f;
    const Color crosshair = colors::White.WithAlpha(0.85f);
    DrawScreenLine(Vec2{cx - 16.0f, cy}, Vec2{cx - 5.0f, cy}, crosshair, 2.0f);
    DrawScreenLine(Vec2{cx + 5.0f, cy}, Vec2{cx + 16.0f, cy}, crosshair, 2.0f);
    DrawScreenLine(Vec2{cx, cy - 16.0f}, Vec2{cx, cy - 5.0f}, crosshair, 2.0f);
    DrawScreenLine(Vec2{cx, cy + 5.0f}, Vec2{cx, cy + 16.0f}, crosshair, 2.0f);
    DrawCircleLines(Vec2{cx, cy}, 3.0f, colors::Gold);

    DrawTextFormat(static_cast<int>(width) - 150, 24, 16.0f, colors::LightGray, "FPS %.0f",
                   static_cast<double>(GetFps()));
}

void DrawMenu(float width, float height) {
    DrawRectangle(0.0f, 0.0f, width, height, colors::Black.WithAlpha(0.55f));
    DrawTextCentered("NEXIUM BLASTER", width * 0.5f, height * 0.32f, 52.0f, colors::Gold);
    DrawTextCentered("a first-person shooting gallery on NexiumEngine", width * 0.5f,
                     height * 0.32f + 46.0f, 20.0f, colors::LightGray);
    DrawTextCentered("WASD walk   mouse look   left click fire", width * 0.5f,
                     height * 0.52f, 22.0f, colors::White);
    DrawTextCentered("R reload   P pause   ESC quit", width * 0.5f, height * 0.52f + 30.0f,
                     22.0f, colors::White);
    DrawTextCentered("60 seconds. Hit the floating targets. Longer shots score more.",
                     width * 0.5f, height * 0.66f, 18.0f, colors::LightGray);
    DrawTextCentered("PRESS SPACE TO START", width * 0.5f, height * 0.78f, 26.0f, colors::Lime);
}

void DrawGameOver(const Game& game, float width, float height) {
    DrawRectangle(0.0f, 0.0f, width, height, colors::Black.WithAlpha(0.6f));
    DrawTextCentered("TIME UP", width * 0.5f, height * 0.30f, 52.0f, colors::Orange);

    const float accuracy = game.shots > 0
                               ? 100.0f * static_cast<float>(game.hits) /
                                     static_cast<float>(game.shots)
                               : 0.0f;
    DrawTextFormat(static_cast<int>(width * 0.5f) - 130, static_cast<int>(height * 0.44f), 28.0f,
                   colors::White, "SCORE   %d", game.score);
    DrawTextFormat(static_cast<int>(width * 0.5f) - 130, static_cast<int>(height * 0.44f) + 40,
                   24.0f, colors::LightGray, "HITS    %d / %d", game.hits, game.shots);
    DrawTextFormat(static_cast<int>(width * 0.5f) - 130, static_cast<int>(height * 0.44f) + 74,
                   24.0f, colors::LightGray, "ACCURACY %.0f%%", static_cast<double>(accuracy));

    if (game.score >= game.best && game.score > 0) {
        DrawTextCentered("NEW BEST!", width * 0.5f, height * 0.70f, 30.0f, colors::Gold);
    }
    DrawTextCentered("PRESS R TO PLAY AGAIN   ESC TO QUIT", width * 0.5f, height * 0.82f, 24.0f,
                     colors::White);
}

} // namespace

// --------------------------------------------------------------------- main --

int main() {
    Config config;
    config.title = "Nexium Blaster";
    config.width = 1280;
    config.height = 720;
    if (!Init(config)) return 1;

    SetLightDirection(Vec3{-0.35f, -1.0f, 0.2f});
    SetAmbient(colors::White, 0.35f);

    Game game;

    // Synthesise the audio cues, load them, then drop the files.
    WriteWav16("nexium_shoot.wav", MakeShootSound());
    WriteWav16("nexium_hit.wav", MakeHitSound());
    WriteWav16("nexium_reload.wav", MakeReloadSound());
    WriteWav16("nexium_empty.wav", MakeEmptySound());
    game.shootSound = LoadSound("nexium_shoot.wav");
    game.hitSound = LoadSound("nexium_hit.wav");
    game.reloadSound = LoadSound("nexium_reload.wav");
    game.emptySound = LoadSound("nexium_empty.wav");
    std::remove("nexium_shoot.wav");
    std::remove("nexium_hit.wav");
    std::remove("nexium_reload.wav");
    std::remove("nexium_empty.wav");

    ResetRound(game);
    game.state = State::Menu;

    while (!WindowShouldClose()) {
        const float deltaTime = Min(GetFrameTime(), 0.05f);
        const float time = GetTime();
        const float width = static_cast<float>(GetScreenWidth());
        const float height = static_cast<float>(GetScreenHeight());

        // --- transition between the screens ---
        if (game.state == State::Menu) {
            if (IsKeyPressed(Key::Space) || IsKeyPressed(Key::Enter)) {
                ResetRound(game);
                game.state = State::Playing;
                SetMouseCaptured(true);
            }
        } else if (game.state == State::GameOver) {
            if (IsKeyPressed(Key::R) || IsKeyPressed(Key::Enter)) {
                ResetRound(game);
                game.state = State::Playing;
                SetMouseCaptured(true);
            }
        } else if (game.state == State::Playing) {
            if (IsKeyPressed(Key::P)) {
                game.state = State::Paused;
                SetMouseCaptured(false);
            }
        } else if (game.state == State::Paused) {
            if (IsKeyPressed(Key::P)) {
                game.state = State::Playing;
                SetMouseCaptured(true);
            }
        }

        // --- simulation, only while playing ---
        if (game.state == State::Playing) {
            UpdateFpsCamera(game.camera);
            ClampPlayer(game);

            game.roundTime -= deltaTime;
            game.fireCooldown.Tick(deltaTime);
            if (game.reloadTimer.Tick(deltaTime)) {
                game.reloading = false;
                game.ammo = kMagSize;
            }

            if (IsMouseButtonPressed(MouseButton::Left)) {
                if (!game.reloading && game.ammo > 0 && !game.fireCooldown.Running()) {
                    Fire(game, width, height);
                } else if (game.ammo == 0) {
                    StartReload(game);
                    if (game.emptySound) PlaySound(game.emptySound);
                }
            }
            if (IsKeyPressed(Key::R)) StartReload(game);

            if (game.ammo == 0 && !game.reloading) StartReload(game);

            UpdateTargets(game, time, deltaTime);
            UpdateShards(game, deltaTime);
            UpdateTracers(game, deltaTime);

            game.muzzleFlash = Max(0.0f, game.muzzleFlash - deltaTime);
            game.recoil = Max(0.0f, game.recoil - deltaTime * 6.0f);
            game.shake.Update(deltaTime);

            if (game.roundTime <= 0.0f) {
                game.roundTime = 0.0f;
                game.best = Max(game.best, game.score);
                game.state = State::GameOver;
                SetMouseCaptured(false);
            }
        } else {
            game.shake.Update(deltaTime * 0.5f);
        }

        // --- render ---
        Camera3D camera = game.camera.ToCamera();
        camera.position += game.shake.Offset();

        BeginScene(camera, Color::FromBytes(20, 24, 38));

        DrawArena();
        DrawTargets(game, time);
        DrawShards(game);
        DrawTracers(game);
        if (game.state != State::Menu) DrawWeapon(game);

        // 2D overlay, drawn last by EndScene().
        if (game.state == State::Playing || game.state == State::Paused) {
            DrawHud(game, width, height);
        }
        if (game.state == State::Menu) DrawMenu(width, height);
        if (game.state == State::GameOver) DrawGameOver(game, width, height);
        if (game.state == State::Paused) {
            DrawRectangle(0.0f, 0.0f, width, height, colors::Black.WithAlpha(0.5f));
            DrawTextCentered("PAUSED", width * 0.5f, height * 0.45f, 48.0f, colors::White);
            DrawTextCentered("PRESS P TO RESUME", width * 0.5f, height * 0.55f, 22.0f,
                             colors::LightGray);
        }

        EndScene();
    }

    SetMouseCaptured(false);
    UnloadSound(game.shootSound);
    UnloadSound(game.hitSound);
    UnloadSound(game.reloadSound);
    UnloadSound(game.emptySound);
    Close();
    return 0;
}
