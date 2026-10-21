// NexiumEngine — game: Nexium Heights.
//
// A third-person 3D platformer built only on the NexiumEngine public API. Where the
// other game in this folder is a first-person shooter, this one leans on the
// parts of the library that a platformer needs: gravity, axis-separated box
// collision, moving platforms that carry the player, a follow camera, spinning
// pickups and a health/lives loop.
//
// The level is a course of floating platforms climbing into the sky. Jump from
// one to the next, grab every gem, and don't fall off. Some platforms move, and
// red orbs patrol the higher ones.
//
// No asset files: the audio cues are synthesised at startup and loaded back in.
//
// Controls
//   W A S D   move (relative to the camera)
//   space     jump
//   mouse     drag to orbit the camera   wheel to zoom
//   R         restart        esc to quit
#include <nexium/NexiumEngine.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

using namespace n3d;

namespace {

// ----------------------------------------------------------------- tuning ----

constexpr float kGravity      = -26.0f;
constexpr float kMoveSpeed    = 7.5f;
constexpr float kJumpSpeed    = 10.5f;
constexpr float kAirControl   = 0.65f;
constexpr float kPlayerRadius = 0.4f;
constexpr float kPlayerHeight = 1.8f;     // feet to top of the capsule
constexpr float kCoyoteTime   = 0.12f;    // jump grace after leaving a ledge
constexpr float kJumpBuffer   = 0.15f;    // jump pressed slightly before landing
constexpr float kFallLimit    = -12.0f;   // below this the player is out
constexpr float kRoundSeconds = 90.0f;
constexpr int   kLives        = 3;
constexpr int   kSampleRate   = 44100;

enum class State { Playing, Won, Lost };

// ------------------------------------------------------------------- audio ---

void WriteWav16(const char* path, const std::vector<float>& samples) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;

    const uint32_t dataSize = static_cast<uint32_t>(samples.size() * 2u);
    const auto u32 = [&out](uint32_t value) {
        const char b[4] = {static_cast<char>(value & 0xFFu), static_cast<char>((value >> 8) & 0xFFu),
                           static_cast<char>((value >> 16) & 0xFFu),
                           static_cast<char>((value >> 24) & 0xFFu)};
        out.write(b, 4);
    };
    const auto u16 = [&out](uint16_t value) {
        const char b[2] = {static_cast<char>(value & 0xFFu), static_cast<char>((value >> 8) & 0xFFu)};
        out.write(b, 2);
    };

    out.write("RIFF", 4);
    u32(36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16u);
    u16(1u);
    u16(1u);
    u32(static_cast<uint32_t>(kSampleRate));
    u32(static_cast<uint32_t>(kSampleRate * 2));
    u16(2u);
    u16(16u);
    out.write("data", 4);
    u32(dataSize);
    for (const float sample : samples) {
        u16(static_cast<uint16_t>(static_cast<int>(n3d::Clamp(sample, -1.0f, 1.0f) * 32767.0f)));
    }
}

std::vector<float> Tone(float fromHz, float toHz, float seconds, float gain) {
    const int length = static_cast<int>(static_cast<float>(kSampleRate) * seconds);
    std::vector<float> out(static_cast<size_t>(length));
    float phase = 0.0f;
    for (int i = 0; i < length; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float u = t / seconds;
        phase += n3d::Lerp(fromHz, toHz, u) / static_cast<float>(kSampleRate);
        const float envelope = 1.0f - u;
        out[static_cast<size_t>(i)] = std::sin(phase * n3d::TAU) * envelope * envelope * gain;
    }
    return out;
}

// ----------------------------------------------------------------- entities --

struct Platform {
    Vec3  base{};          // where it sits when not moving
    Vec3  size{1, 1, 1};
    Vec3  center{};        // animated centre, recomputed each frame
    Vec3  previous{};      // last frame's centre, for carrying the player
    Vec3  moveAxis{};
    float range = 0.0f;    // 0 means static
    float speed = 0.0f;
    float phase = 0.0f;
    Color color = n3d::colors::Gray;

    n3d::Bounds Bounds() const {
        const Vec3 half = size * 0.5f;
        return n3d::Bounds{center - half, center + half};
    }
};

struct Gem {
    Vec3  position{};
    bool  collected = false;
    float bob = 0.0f;
    float radius = 0.45f;
};

struct Hazard {
    Vec3  center{};
    float radius = 0.0f;   // orbit radius
    float speed = 0.0f;
    float phase = 0.0f;
    float height = 1.4f;
    float size = 0.55f;
    Vec3  position{};
};

struct Spark {
    Vec3  position{};
    Vec3  velocity{};
    float life = 0.0f;
    n3d::Color color = n3d::colors::White;
};

struct Player {
    Vec3  position{0.0f, 0.1f, 0.0f};   // the feet
    Vec3  velocity{};
    bool  grounded = false;
    float coyote = 0.0f;
    float jumpQueued = 0.0f;
    float facing = 0.0f;                // radians, for the pointer
    float squash = 0.0f;                // landing/jump deformation, decays
};

// --------------------------------------------------------------- game state --

struct Game {
    State state = State::Playing;

    Player player;
    std::vector<Platform> platforms;
    std::vector<Gem> gems;
    std::vector<Hazard> hazards;
    std::vector<Spark> sparks;
    n3d::Random random{0x51D2A7u};
    n3d::CameraShake shake;

    float camYaw = 0.0f;
    float camPitch = 0.42f;
    float camDistance = 11.0f;

    float timeLeft = kRoundSeconds;
    int lives = kLives;
    int collected = 0;
    int total = 0;

    n3d::Sound* jumpSound = nullptr;
    n3d::Sound* gemSound = nullptr;
    n3d::Sound* hurtSound = nullptr;
};

// ---------------------------------------------------------------- the level --

void AddPlatform(Game& game, Vec3 center, Vec3 size, n3d::Color color) {
    Platform platform;
    platform.base = center;
    platform.center = center;
    platform.previous = center;
    platform.size = size;
    platform.color = color;
    game.platforms.push_back(platform);
}

void AddMovingPlatform(Game& game, Vec3 center, Vec3 size, Vec3 axis, float range, float speed,
                       float phase, n3d::Color color) {
    Platform platform;
    platform.base = center;
    platform.center = center;
    platform.previous = center;
    platform.size = size;
    platform.moveAxis = axis;
    platform.range = range;
    platform.speed = speed;
    platform.phase = phase;
    platform.color = color;
    game.platforms.push_back(platform);
}

void BuildLevel(Game& game) {
    game.platforms.clear();
    game.gems.clear();
    game.hazards.clear();
    game.sparks.clear();

    // Each step is reachable with a single jump: about 5 units across and 1.5 up.
    AddPlatform(game, {0.0f, -0.5f, 0.0f}, {10.0f, 1.0f, 10.0f}, n3d::colors::DarkGreen);

    const Vec3 steps[] = {
        {0.0f, 1.0f, -8.0f},    {5.0f, 2.5f, -13.0f},  {10.0f, 4.0f, -17.0f},
        {14.0f, 5.5f, -10.0f},  {10.0f, 7.0f, -4.0f},   {5.0f, 8.5f, 2.0f},
        {-4.0f, 10.0f, 5.0f},   {-10.0f, 11.5f, 1.0f}, {-14.0f, 13.0f, -5.0f},
        {-10.0f, 14.5f, -12.0f}, {-3.0f, 16.0f, -15.0f}, {4.0f, 17.5f, -12.0f},
    };
    for (const Vec3& step : steps) {
        AddPlatform(game, step, {3.5f, 1.0f, 3.5f}, n3d::colors::SlateGray);
    }

    // Three of them drift, so standing still is not always safe.
    AddMovingPlatform(game, {16.0f, 3.2f, -14.0f}, {3.0f, 1.0f, 3.0f}, {1, 0, 0}, 3.5f, 0.8f, 0.0f,
                      n3d::colors::Teal);
    AddMovingPlatform(game, {-7.0f, 9.0f, -2.0f}, {3.0f, 1.0f, 3.0f}, {0, 0, 1}, 4.0f, 0.7f, 1.5f,
                      n3d::colors::Teal);
    AddMovingPlatform(game, {0.0f, 19.0f, -15.0f}, {3.0f, 1.0f, 3.0f}, {1, 0, 0}, 4.5f, 1.0f, 3.0f,
                      n3d::colors::Teal);

    // The summit, where the last gem sits.
    AddPlatform(game, {0.0f, 20.5f, -6.0f}, {7.0f, 1.0f, 7.0f}, n3d::colors::Gold);

    // A gem floats above every platform except the starting slab.
    for (size_t i = 1; i < game.platforms.size(); ++i) {
        Gem gem;
        gem.position = game.platforms[i].base + Vec3{0.0f, 1.9f, 0.0f};
        gem.bob = game.random.Range(0.0f, n3d::TAU);
        game.gems.push_back(gem);
    }
    game.total = static_cast<int>(game.gems.size());

    // Orbs circling the upper platforms.
    const struct { Vec3 center; float radius; float speed; float phase; } orbits[] = {
        {{10.0f, 8.1f, -4.0f}, 2.2f, 1.6f, 0.0f},
        {{-4.0f, 11.1f, 5.0f}, 2.4f, -1.4f, 1.0f},
        {{-10.0f, 12.6f, -12.0f}, 2.0f, 1.8f, 2.0f},
        {{0.0f, 21.6f, -6.0f}, 2.8f, 1.2f, 0.5f},
    };
    for (const auto& orbit : orbits) {
        Hazard hazard;
        hazard.center = orbit.center;
        hazard.radius = orbit.radius;
        hazard.speed = orbit.speed;
        hazard.phase = orbit.phase;
        game.hazards.push_back(hazard);
    }
}

// --------------------------------------------------------------- simulation --

// The player is an axis-aligned box: radius wide, height tall, feet at position.
n3d::Bounds PlayerBounds(const Player& player) {
    return n3d::Bounds{{player.position.x - kPlayerRadius, player.position.y,
                        player.position.z - kPlayerRadius},
                       {player.position.x + kPlayerRadius, player.position.y + kPlayerHeight,
                        player.position.z + kPlayerRadius}};
}

void Respawn(Game& game) {
    game.player.position = Vec3{0.0f, 0.1f, 0.0f};
    game.player.velocity = Vec3{};
    game.player.grounded = false;
    game.player.coyote = 0.0f;
    game.camYaw = 0.0f;
    game.camPitch = 0.42f;
    game.shake.Add(0.5f);
}

// Moves the player along one axis and pushes it out of any platform it entered.
// Resolving the axes separately is what keeps a corner from eating the motion.
void ResolveAxis(Game& game, int axis, float direction) {
    Player& player = game.player;
    for (const Platform& platform : game.platforms) {
        const n3d::Bounds box = platform.Bounds();
        const n3d::Bounds me = PlayerBounds(player);
        if (!(me.min.x < box.max.x && me.max.x > box.min.x && me.min.y < box.max.y &&
              me.max.y > box.min.y && me.min.z < box.max.z && me.max.z > box.min.z)) {
            continue;
        }

        if (axis == 0) {
            player.position.x = direction > 0.0f ? box.min.x - kPlayerRadius
                                                 : box.max.x + kPlayerRadius;
            player.velocity.x = 0.0f;
        } else if (axis == 2) {
            player.position.z = direction > 0.0f ? box.min.z - kPlayerRadius
                                                 : box.max.z + kPlayerRadius;
            player.velocity.z = 0.0f;
        } else if (direction <= 0.0f) {
            // Coming down onto the top face.
            player.position.y = box.max.y;
            player.velocity.y = 0.0f;
            player.grounded = true;
        } else {
            // Hitting the underside: stop, do not stick.
            player.position.y = box.min.y - kPlayerHeight;
            player.velocity.y = 0.0f;
        }
    }
}

void MovePlayer(Game& game, Vec3 delta) {
    Player& player = game.player;
    if (delta.x != 0.0f) {
        player.position.x += delta.x;
        ResolveAxis(game, 0, delta.x);
    }
    if (delta.z != 0.0f) {
        player.position.z += delta.z;
        ResolveAxis(game, 2, delta.z);
    }
    if (delta.y != 0.0f) {
        player.position.y += delta.y;
        ResolveAxis(game, 1, delta.y);
    }
}

// Carries the player with a moving platform it is standing on.
void CarryOnPlatforms(Game& game) {
    Player& player = game.player;
    if (!player.grounded) return;
    for (const Platform& platform : game.platforms) {
        if (platform.range <= 0.0f) continue;
        const n3d::Bounds box = platform.Bounds();
        const n3d::Bounds me = PlayerBounds(player);
        const bool onTop = std::fabs(me.min.y - box.max.y) < 0.12f;
        const bool within = me.min.x < box.max.x && me.max.x > box.min.x &&
                            me.min.z < box.max.z && me.max.z > box.min.z;
        if (onTop && within) {
            const Vec3 carried = platform.center - platform.previous;
            player.position += carried;
        }
    }
}

void UpdatePlatforms(Game& game, float time) {
    for (Platform& platform : game.platforms) {
        platform.previous = platform.center;
        if (platform.range <= 0.0f) continue;
        const float offset = std::sin(time * platform.speed + platform.phase) * platform.range;
        platform.center = platform.base + platform.moveAxis * offset;
    }
}

Vec3 CameraForward(const Game& game) {
    return Vec3{-std::sin(game.camYaw), 0.0f, -std::cos(game.camYaw)};
}

Vec3 CameraRight(const Game& game) {
    return Vec3{std::cos(game.camYaw), 0.0f, -std::sin(game.camYaw)};
}

void UpdatePlayer(Game& game, float deltaTime) {
    Player& player = game.player;

    // Desired horizontal direction, relative to where the camera looks.
    Vec3 wish{};
    if (n3d::IsKeyDown(n3d::Key::W)) wish += CameraForward(game);
    if (n3d::IsKeyDown(n3d::Key::S)) wish -= CameraForward(game);
    if (n3d::IsKeyDown(n3d::Key::D)) wish += CameraRight(game);
    if (n3d::IsKeyDown(n3d::Key::A)) wish -= CameraRight(game);
    if (n3d::LengthSq(wish) > 1e-6f) {
        wish = n3d::Normalize(wish);
        player.facing = std::atan2(wish.x, wish.z);
    }

    // Full speed on the ground, a bit less in the air, so jumps commit.
    const float control = player.grounded ? 1.0f : kAirControl;
    const Vec3 target = wish * (kMoveSpeed * control);
    player.velocity.x = n3d::Damp(player.velocity.x, target.x, player.grounded ? 18.0f : 6.0f,
                                  deltaTime);
    player.velocity.z = n3d::Damp(player.velocity.z, target.z, player.grounded ? 18.0f : 6.0f,
                                  deltaTime);

    // Jump: buffered input plus coyote time, so near misses still jump.
    if (n3d::IsKeyPressed(n3d::Key::Space)) player.jumpQueued = kJumpBuffer;
    player.jumpQueued = n3d::Max(0.0f, player.jumpQueued - deltaTime);
    player.coyote = player.grounded ? kCoyoteTime : n3d::Max(0.0f, player.coyote - deltaTime);

    if (player.jumpQueued > 0.0f && player.coyote > 0.0f) {
        player.velocity.y = kJumpSpeed;
        player.grounded = false;
        player.coyote = 0.0f;
        player.jumpQueued = 0.0f;
        player.squash = -0.25f;
        if (game.jumpSound) n3d::PlaySoundMulti(game.jumpSound, 1.2f);
    }
    // Variable jump height: releasing early cuts the rise.
    if (player.velocity.y > 0.0f && !n3d::IsKeyDown(n3d::Key::Space)) {
        player.velocity.y -= 22.0f * deltaTime;
    }

    player.velocity.y += kGravity * deltaTime;
    player.velocity.y = n3d::Max(player.velocity.y, -32.0f);

    player.grounded = false;
    MovePlayer(game, player.velocity * deltaTime);
    CarryOnPlatforms(game);

    player.squash = n3d::Damp(player.squash, 0.0f, 10.0f, deltaTime);

    // Out of bounds: a life, and back to the start.
    if (player.position.y < kFallLimit) {
        game.lives--;
        if (game.hurtSound) n3d::PlaySound(game.hurtSound);
        if (game.lives <= 0) {
            game.state = State::Lost;
        } else {
            Respawn(game);
        }
    }
}

void UpdateCamera(Game& game, float deltaTime) {
    if (n3d::IsMouseButtonDown(n3d::MouseButton::Right) ||
        n3d::IsMouseButtonDown(n3d::MouseButton::Left)) {
        const n3d::Vec2 delta = n3d::GetMouseDelta();
        game.camYaw -= delta.x * 0.005f;
        game.camPitch = n3d::Clamp(game.camPitch + delta.y * 0.005f, -0.15f, 1.25f);
    }
    // Arrow keys and Q/E orbit too, for a mouse-free machine.
    if (n3d::IsKeyDown(n3d::Key::Q) || n3d::IsKeyDown(n3d::Key::Left)) game.camYaw += 1.8f * deltaTime;
    if (n3d::IsKeyDown(n3d::Key::E) || n3d::IsKeyDown(n3d::Key::Right)) game.camYaw -= 1.8f * deltaTime;

    game.camDistance = n3d::Clamp(game.camDistance - n3d::GetMouseWheelDelta() * 0.8f, 5.0f, 20.0f);
}

void UpdateGems(Game& game, float deltaTime) {
    Player& player = game.player;
    const Vec3 chest = player.position + Vec3{0.0f, kPlayerHeight * 0.5f, 0.0f};
    for (Gem& gem : game.gems) {
        if (gem.collected) continue;
        gem.bob += deltaTime * 2.0f;
        const Vec3 at = gem.position + Vec3{0.0f, std::sin(gem.bob) * 0.15f, 0.0f};
        if (n3d::CheckCollisionSpheres(chest, kPlayerRadius + 0.2f, at, gem.radius)) {
            gem.collected = true;
            game.collected++;
            game.shake.Add(0.12f);
            if (game.gemSound) n3d::PlaySoundMulti(game.gemSound, 3.0f);
            for (int i = 0; i < 12; ++i) {
                Spark spark;
                spark.position = at;
                spark.velocity = game.random.OnSphere(game.random.Range(2.0f, 5.0f));
                spark.life = game.random.Range(0.3f, 0.6f);
                spark.color = n3d::colors::Gold;
                game.sparks.push_back(spark);
            }
        }
    }
    if (game.collected >= game.total) game.state = State::Won;
}

void UpdateHazards(Game& game, float time) {
    Player& player = game.player;
    const Vec3 chest = player.position + Vec3{0.0f, kPlayerHeight * 0.5f, 0.0f};
    for (Hazard& hazard : game.hazards) {
        const float angle = time * hazard.speed + hazard.phase;
        hazard.position = hazard.center +
                          Vec3{std::cos(angle) * hazard.radius, 0.0f, std::sin(angle) * hazard.radius};
        if (game.state != State::Playing) continue;
        if (n3d::CheckCollisionSpheres(chest, kPlayerRadius + 0.15f, hazard.position, hazard.size)) {
            game.lives--;
            game.shake.Add(0.7f);
            if (game.hurtSound) n3d::PlaySound(game.hurtSound);
            const Vec3 away = n3d::Normalize(chest - hazard.position);
            player.velocity = away * 9.0f + Vec3{0.0f, 7.0f, 0.0f};
            if (game.lives <= 0) game.state = State::Lost;
        }
    }
}

void UpdateSparks(Game& game, float deltaTime) {
    for (size_t i = 0; i < game.sparks.size();) {
        Spark& spark = game.sparks[i];
        spark.life -= deltaTime;
        if (spark.life <= 0.0f) {
            game.sparks[i] = game.sparks.back();
            game.sparks.pop_back();
            continue;
        }
        spark.velocity.y -= 9.0f * deltaTime;
        spark.position += spark.velocity * deltaTime;
        ++i;
    }
}

void Restart(Game& game) {
    game.state = State::Playing;
    game.timeLeft = kRoundSeconds;
    game.lives = kLives;
    game.collected = 0;
    game.shake = n3d::CameraShake{};
    BuildLevel(game);
    Respawn(game);
}

// ---------------------------------------------------------------- rendering --

n3d::Camera3D FollowCamera(const Game& game) {
    const Vec3 target = game.player.position + Vec3{0.0f, 1.2f, 0.0f};
    const float cp = std::cos(game.camPitch);
    const Vec3 eye = target + Vec3{cp * std::sin(game.camYaw), std::sin(game.camPitch),
                                   cp * std::cos(game.camYaw)} * game.camDistance;
    n3d::Camera3D camera;
    camera.position = eye;
    camera.target = target;
    camera.fovY = 60.0f;
    camera.farZ = 400.0f;
    return camera;
}

void DrawLevel(const Game& game) {
    for (const Platform& platform : game.platforms) {
        n3d::DrawCube(platform.center, platform.size, platform.color);
        // A brighter rim so the edge of every platform reads at a distance.
        n3d::DrawBox(n3d::Bounds{platform.center - platform.size * 0.5f,
                                 platform.center + platform.size * 0.5f},
                     platform.color.Scaled(0.6f));
    }
}

void DrawGems(const Game& game, float time) {
    for (const Gem& gem : game.gems) {
        if (gem.collected) continue;
        const Vec3 at = gem.position + Vec3{0.0f, std::sin(gem.bob) * 0.15f, 0.0f};
        const n3d::Mat4 world = n3d::Compose(
            at, n3d::Vec3{time * 1.2f, time * 1.9f, 0.0f}, n3d::Vec3{1.4f, 1.4f, 1.4f});
        n3d::DrawMeshEx(n3d::GetTorusMesh(0.5f, 0.18f), world, n3d::colors::Gold);
        n3d::DrawSphere(at, 0.16f, n3d::colors::Yellow);
    }
}

void DrawHazards(const Game& game) {
    for (const Hazard& hazard : game.hazards) {
        n3d::DrawSphere(hazard.position, hazard.size, n3d::colors::Red);
        n3d::DrawWireSphere(hazard.position, hazard.size * 1.35f,
                            n3d::colors::Red.WithAlpha(0.5f), 6, 12);
    }
}

void DrawPlayer(const Game& game) {
    const Player& player = game.player;
    const float half = kPlayerHeight * 0.5f;
    const Vec3 center = player.position + Vec3{0.0f, half, 0.0f};

    // Squash and stretch on jump and landing.
    const float sx = 1.0f - player.squash;
    const float sy = 1.0f + player.squash;
    n3d::DrawCapsule(center, kPlayerRadius, kPlayerHeight - kPlayerRadius * 2.0f,
                     n3d::colors::SkyBlue);
    n3d::DrawSphere(center + Vec3{0.0f, half * sy * 0.55f, 0.0f}, kPlayerRadius * 1.02f * sx,
                    n3d::colors::White);

    // A nose cone, so which way the player faces is never a guess.
    const Vec3 nose = center + Vec3{std::sin(player.facing), 0.0f, std::cos(player.facing)} *
                                  0.55f;
    n3d::DrawCone(nose - Vec3{0.0f, 0.2f, 0.0f}, 0.22f, 0.5f, n3d::colors::Orange);
}

void DrawSparks(const Game& game) {
    for (const Spark& spark : game.sparks) {
        const float alpha = n3d::Clamp(spark.life / 0.5f, 0.0f, 1.0f);
        n3d::DrawLine(spark.position, spark.position - n3d::Normalize(spark.velocity) * 0.2f,
                      spark.color.WithAlpha(alpha));
    }
}

void DrawHud(const Game& game, float width, float height) {
    n3d::DrawRectangle(14.0f, 14.0f, 320.0f, 118.0f, n3d::colors::Black.WithAlpha(0.45f));
    n3d::DrawTextFormat(26, 24, 24.0f, n3d::colors::White, "GEMS  %d / %d", game.collected,
                        game.total);
    n3d::DrawTextFormat(26, 56, 20.0f, n3d::colors::Lime, "LIVES  %d", game.lives);

    const float fraction = n3d::Clamp(game.timeLeft / kRoundSeconds, 0.0f, 1.0f);
    const n3d::Color timeColor = game.timeLeft < 15.0f ? n3d::colors::Red : n3d::colors::White;
    n3d::DrawRectangle(26.0f, 88.0f, 292.0f, 14.0f, n3d::colors::DarkGray);
    n3d::DrawRectangle(26.0f, 88.0f, 292.0f * fraction, 14.0f, timeColor);
    n3d::DrawTextFormat(26, 108, 16.0f, n3d::colors::LightGray, "%.1f s",
                        static_cast<double>(game.timeLeft));

    n3d::DrawTextFormat(static_cast<int>(width) - 150, 24, 16.0f, n3d::colors::LightGray,
                        "FPS %.0f", static_cast<double>(n3d::GetFps()));
    n3d::DrawTextCentered("WASD move   space jump   drag to look   wheel zoom", width * 0.5f,
                          height - 30.0f, 16.0f, n3d::colors::LightGray.WithAlpha(0.8f));
}

void DrawEndScreen(const Game& game, float width, float height, bool won) {
    n3d::DrawRectangle(0.0f, 0.0f, width, height, n3d::colors::Black.WithAlpha(0.6f));
    n3d::DrawTextCentered(won ? "YOU MADE IT!" : "OUT OF LIVES", width * 0.5f, height * 0.34f,
                          50.0f, won ? n3d::colors::Gold : n3d::colors::Red);
    n3d::DrawTextFormat(static_cast<int>(width * 0.5f) - 110,
                        static_cast<int>(height * 0.48f), 26.0f, n3d::colors::White,
                        "GEMS  %d / %d", game.collected, game.total);
    n3d::DrawTextCentered("PRESS R TO PLAY AGAIN   ESC TO QUIT", width * 0.5f, height * 0.72f,
                          22.0f, n3d::colors::White);
}

} // namespace

// --------------------------------------------------------------------- main --

int main() {
    n3d::Config config;
    config.title = "Nexium Heights";
    config.width = 1280;
    config.height = 720;
    if (!n3d::Init(config)) return 1;

    n3d::SetLightDirection(n3d::Vec3{-0.3f, -1.0f, -0.35f});
    n3d::SetAmbient(n3d::colors::White, 0.38f);

    Game game;

    WriteWav16("nexium_jump.wav", Tone(320.0f, 640.0f, 0.12f, 0.3f));
    WriteWav16("nexium_gem.wav", Tone(880.0f, 1320.0f, 0.16f, 0.3f));
    WriteWav16("nexium_hurt.wav", Tone(400.0f, 120.0f, 0.25f, 0.35f));
    game.jumpSound = n3d::LoadSound("nexium_jump.wav");
    game.gemSound = n3d::LoadSound("nexium_gem.wav");
    game.hurtSound = n3d::LoadSound("nexium_hurt.wav");
    std::remove("nexium_jump.wav");
    std::remove("nexium_gem.wav");
    std::remove("nexium_hurt.wav");

    Restart(game);

    while (!n3d::WindowShouldClose()) {
        const float deltaTime = n3d::Min(n3d::GetFrameTime(), 0.05f);
        const float time = n3d::GetTime();
        const float width = static_cast<float>(n3d::GetScreenWidth());
        const float height = static_cast<float>(n3d::GetScreenHeight());

        if (game.state == State::Playing) {
            game.timeLeft -= deltaTime;
            UpdatePlatforms(game, time);
            UpdatePlayer(game, deltaTime);
            UpdateGems(game, deltaTime);
            UpdateHazards(game, time);
            UpdateSparks(game, deltaTime);
            game.shake.Update(deltaTime);

            if (game.timeLeft <= 0.0f) {
                game.timeLeft = 0.0f;
                game.state = State::Lost;
            }
        } else {
            game.shake.Update(deltaTime * 0.5f);
            if (n3d::IsKeyPressed(n3d::Key::R)) Restart(game);
        }
        UpdateCamera(game, deltaTime);

        n3d::Camera3D camera = FollowCamera(game);
        camera.position += game.shake.Offset();

        n3d::BeginScene(camera, n3d::Color::FromBytes(22, 28, 46));
        DrawLevel(game);
        DrawGems(game, time);
        DrawHazards(game);
        DrawSparks(game);
        DrawPlayer(game);

        DrawHud(game, width, height);
        if (game.state == State::Won) DrawEndScreen(game, width, height, true);
        if (game.state == State::Lost) DrawEndScreen(game, width, height, false);
        n3d::EndScene();
    }

    n3d::UnloadSound(game.jumpSound);
    n3d::UnloadSound(game.gemSound);
    n3d::UnloadSound(game.hurtSound);
    n3d::Close();
    return 0;
}
