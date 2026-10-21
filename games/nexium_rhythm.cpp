// NexiumEngine — game: Nexium Rhythm.
//
// A 2D rhythm game that exists to show two things: the `n2d` namespace, and a
// Storage drawer as the single place every asset path lives.
//
// The drawer is declared with N3D_DEFINE_STORAGE inside `namespace n3d`, so the
// files are reachable both as n3d::Audio::music and, through the n2d alias, as
// n2d::Audio::music. Nothing in the game loop ever writes a path: it asks the
// drawer for a file by name and gets a handle back, loaded once and cached.
//
// There are no asset files in the repository. The track and the hit/error cues
// are synthesised into WAV files at startup and then loaded *through the drawer*,
// which is the same code path a real .wav on disk would take.
//
// Controls
//   D F J K   hit the four lanes when a note reaches its line
//   space     start / restart
//   esc       quit
//
// Build: see ./scripts/verify.sh, or compile games/nexium_rhythm.cpp against the
// library the same way the other games are.
#include <nexium/n2d.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace n2d;

namespace {

// ------------------------------------------------------------------ tuning ---

constexpr int   kLanes        = 4;
constexpr float kLaneWidth    = 110.0f;
constexpr float kNoteSpeed    = 620.0f;    // pixels per second
constexpr float kHitWindow    = 26.0f;     // pixels; how close counts as a hit
constexpr float kSpawnEvery   = 0.42f;     // seconds between notes
constexpr int   kSampleRate   = 44100;

const Color kLaneColors[kLanes] = {
    Color::FromBytes(255, 90, 120, 255),
    Color::FromBytes(255, 190, 80, 255),
    Color::FromBytes(90, 220, 160, 255),
    Color::FromBytes(110, 160, 255, 255),
};

const Key kLaneKeys[kLanes] = {Key::D, Key::F, Key::J, Key::K};
const char* const kLaneLabels[kLanes] = {"D", "F", "J", "K"};

// ------------------------------------------------------------- the drawer ---
// Every path the game uses. Placing this in namespace n3d is what lets both
// n3d:: and n2d:: name these files; the n2d alias makes the 2D spelling work.
#define NEXIUM_RHYTHM_FILES(X)                        \
    X(music, "nexium_rhythm_music.wav")               \
    X(hit,   "nexium_rhythm_hit.wav")                 \
    X(miss,  "nexium_rhythm_miss.wav")                \
    X(logo,  "nexium_rhythm_logo.png")

} // namespace

namespace n3d {
N3D_DEFINE_STORAGE(Audio, NEXIUM_RHYTHM_FILES)
} // namespace n3d

namespace {

// ------------------------------------------------------------------- audio ---
// The engine loads RIFF/WAVE only, so the track and cues are written to disk once
// and then loaded through the drawer above.

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
    u32(16u);
    u16(1u);   // PCM
    u16(1u);   // mono
    u32(kSampleRate);
    u32(kSampleRate * 2u);
    u16(2u);
    u16(16u);
    out.write("data", 4);
    u32(dataSize);
    for (const float sample : samples) {
        const int value = static_cast<int>(Clamp(sample, -1.0f, 1.0f) * 32767.0f);
        u16(static_cast<uint16_t>(value & 0xFFFF));
    }
}

// A short percussive blip, used for the hit and miss cues.
std::vector<float> MakeBlip(float frequency, float seconds, float decay) {
    const int count = static_cast<int>(kSampleRate * seconds);
    std::vector<float> samples(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float envelope = std::exp(-t * decay);
        samples[static_cast<size_t>(i)] =
            std::sin(2.0f * PI * frequency * t) * envelope * 0.45f;
    }
    return samples;
}

// A short looping track: a bass pulse and a four-note arpeggio over eight bars, so
// there is something musical to play the notes against.
std::vector<float> MakeTrack(float seconds) {
    const int count = static_cast<int>(kSampleRate * seconds);
    std::vector<float> samples(static_cast<size_t>(count));
    

    const float beat = 0.5f;                       // 120 bpm
    const float arp[kLanes] = {220.0f, 277.18f, 329.63f, 440.0f};

    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;

        // Bass: a sine an octave down, gated on every beat.
        const float beatPhase = std::fmod(t, beat) / beat;
        const float bassEnv = std::exp(-beatPhase * 4.0f);
        float value = std::sin(2.0f * PI * 55.0f * t) * bassEnv * 0.30f;

        // Arpeggio: step through the four notes, one per half beat.
        const int step = static_cast<int>(t / (beat * 0.5f)) % kLanes;
        const float notePhase = std::fmod(t, beat * 0.5f) / (beat * 0.5f);
        const float noteEnv = std::exp(-notePhase * 6.0f);
        value += std::sin(2.0f * PI * arp[step] * t) * noteEnv * 0.18f;

        samples[static_cast<size_t>(i)] = value;
    }
    return samples;
}

// ------------------------------------------------------------------- state ---

struct Note {
    float laneY = 0.0f;     // distance travelled, in pixels
    int lane = 0;
    bool alive = false;
    bool scored = false;
};

constexpr int kMaxNotes = 96;

struct Game {
    Note notes[kMaxNotes];
    float spawnTimer = 0.0f;
    float hitLineY = 0.0f;
    int score = 0;
    int combo = 0;
    int bestCombo = 0;
    int misses = 0;
    float hitFlash[kLanes] = {};
    float shake = 0.0f;
    bool started = false;
    bool musicPlaying = false;
};

void SpawnNote(Game& game, float laneWidth, int lanes) {
    for (Note& note : game.notes) {
        if (note.alive) continue;
        note.alive = true;
        note.scored = false;
        note.lane = RandomInt(0, lanes - 1);
        note.laneY = 0.0f;
        (void)laneWidth;
        return;
    }
}

void ResetGame(Game& game) {
    game = Game{};
    for (Note& note : game.notes) note = Note{};
}

} // namespace

int main() {
    Config config;
    config.title = "NexiumEngine — Rhythm (n2d + Storage)";
    config.width = 900;
    config.height = 720;
    if (!Init(config)) return 1;

    // Generate the audio once, then load it through the drawer. The paths live in
    // the drawer, so the load below is the same call a shipped .wav would use.
    WriteWav16(Audio::music.Path(), MakeTrack(24.0f));
    WriteWav16(Audio::hit.Path(), MakeBlip(880.0f, 0.16f, 30.0f));
    WriteWav16(Audio::miss.Path(), MakeBlip(150.0f, 0.22f, 18.0f));

    Music* music = Audio::music.AsMusic();
    Sound* hitSound = Audio::hit.AsSound();
    Sound* missSound = Audio::miss.AsSound();
    if (music) SetMusicLooping(music, true);
    if (hitSound) SetSoundVolume(hitSound, 0.5f);
    if (missSound) SetSoundVolume(missSound, 0.5f);

    // A small logo texture drawn in the corner, built in memory and registered in
    // the drawer's texture slot by hand so the file list stays honest about intent.
    const int logoSize = 64;
    std::vector<uint8_t> logoPixels(static_cast<size_t>(logoSize) * logoSize * 4);
    for (int y = 0; y < logoSize; ++y) {
        for (int x = 0; x < logoSize; ++x) {
            const float dx = static_cast<float>(x) - 31.5f;
            const float dy = static_cast<float>(y) - 31.5f;
            const float radius = std::sqrt(dx * dx + dy * dy);
            const bool inside = radius < 30.0f;
            uint8_t* texel = &logoPixels[(static_cast<size_t>(y) * logoSize + x) * 4];
            texel[0] = inside ? 255 : 0;
            texel[1] = inside ? 200 : 0;
            texel[2] = inside ? 90 : 0;
            texel[3] = inside ? 255 : 0;
        }
    }
    TextureDesc logoDesc;
    logoDesc.width = logoSize;
    logoDesc.height = logoSize;
    logoDesc.pixels = logoPixels.data();
    Texture* logo = LoadTexture(logoDesc);

    Game game;
    ResetGame(game);

    while (!WindowShouldClose()) {
        const float deltaTime = Min(GetFrameTime(), 0.05f);
        const float width = static_cast<float>(GetScreenWidth());
        const float height = static_cast<float>(GetScreenHeight());
        game.hitLineY = height - 110.0f;

        // --- input ---
        if (!game.started) {
            if (IsKeyPressed(Key::Space) || IsKeyPressed(Key::Enter)) {
                ResetGame(game);
                game.started = true;
                if (music) {
                    PlayMusic(music);
                    game.musicPlaying = true;
                }
            }
        } else {
            for (int lane = 0; lane < kLanes; ++lane) {
                if (!IsKeyPressed(kLaneKeys[lane])) continue;

                // The closest unscored note in this lane, if any, decides hit or miss.
                Note* best = nullptr;
                float bestDistance = kHitWindow;
                for (Note& note : game.notes) {
                    if (!note.alive || note.scored || note.lane != lane) continue;
                    const float distance = std::fabs(note.laneY - game.hitLineY);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best = &note;
                    }
                }

                if (best) {
                    best->scored = true;
                    best->alive = false;
                    ++game.combo;
                    game.bestCombo = Max(game.bestCombo, game.combo);
                    game.score += 10 + game.combo;
                    game.hitFlash[lane] = 1.0f;
                    if (hitSound) PlaySound(hitSound);
                } else {
                    game.combo = 0;
                    game.shake = 6.0f;
                    if (missSound) PlaySound(missSound);
                }
            }
        }

        // --- notes ---
        if (game.started) {
            game.spawnTimer -= deltaTime;
            if (game.spawnTimer <= 0.0f) {
                game.spawnTimer = kSpawnEvery;
                SpawnNote(game, kLaneWidth, kLanes);
            }

            for (Note& note : game.notes) {
                if (!note.alive) continue;
                note.laneY += kNoteSpeed * deltaTime;
                if (note.laneY > game.hitLineY + kHitWindow) {
                    note.alive = false;
                    game.combo = 0;
                    ++game.misses;
                    game.shake = 4.0f;
                }
            }
        }

        for (float& flash : game.hitFlash) flash = Max(0.0f, flash - deltaTime * 4.0f);
        game.shake = Max(0.0f, game.shake - deltaTime * 30.0f);

        // --- draw ---
        BeginSceneOverlay(Camera3D{});

        // A little screen shake on a hit or a miss.
        Camera2D camera;
        camera.offset = Vec2{width * 0.5f, height * 0.5f};
        camera.target = Vec2{};
        if (game.shake > 0.0f) {
            camera.offset.x += RandomFloat(-game.shake, game.shake);
            camera.offset.y += RandomFloat(-game.shake, game.shake);
        }
        BeginMode2D(camera);

        const float boardWidth = kLaneWidth * kLanes;
        const float boardLeft = -boardWidth * 0.5f;
        const float boardTop = -height * 0.5f;
        const float boardBottom = height * 0.5f;

        DrawRectangleRec(Rect{boardLeft, boardTop, boardWidth, boardBottom - boardTop},
                         Color::FromBytes(18, 18, 26, 255));

        // Lane guides and the hit line.
        for (int lane = 0; lane < kLanes; ++lane) {
            const float x = boardLeft + lane * kLaneWidth;
            if (lane % 2 == 0) {
                DrawRectangleRec(Rect{x, boardTop, kLaneWidth, boardBottom - boardTop},
                                 Color::FromBytes(26, 26, 38, 255));
            }
            DrawRectangleRec(Rect{x, game.hitLineY - height * 0.5f, kLaneWidth, 4.0f},
                             kLaneColors[lane].WithAlpha(0.55f));
            if (game.hitFlash[lane] > 0.0f) {
                DrawRectangleRec(Rect{x, game.hitLineY - height * 0.5f, kLaneWidth, 4.0f},
                                 kLaneColors[lane].WithAlpha(game.hitFlash[lane]));
            }
        }

        // Notes, as rounded bars falling toward the hit line.
        for (const Note& note : game.notes) {
            if (!note.alive) continue;
            const float x = boardLeft + note.lane * kLaneWidth + 10.0f;
            const float y = game.hitLineY - height * 0.5f - note.laneY;
            DrawRectangleRec(Rect{x, y - 14.0f, kLaneWidth - 20.0f, 28.0f},
                             kLaneColors[note.lane]);
            DrawRectangleLinesEx(Rect{x, y - 14.0f, kLaneWidth - 20.0f, 28.0f}, 2.0f,
                                 colors::White.WithAlpha(0.7f));
        }

        // Key labels under each lane.
        for (int lane = 0; lane < kLanes; ++lane) {
            const float x = boardLeft + lane * kLaneWidth + kLaneWidth * 0.5f;
            DrawTextCentered(kLaneLabels[lane], x, game.hitLineY - height * 0.5f + 34.0f, 22.0f,
                             kLaneColors[lane]);
        }

        EndMode2D();

        // HUD in plain screen space.
        DrawTextFormat(16, 14, 22.0f, colors::White, "score %d", game.score);
        DrawTextFormat(16, 42, 18.0f, colors::LightGray, "combo %d  best %d  misses %d",
                       game.combo, game.bestCombo, game.misses);
        if (logo) DrawTexture(logo, static_cast<float>(GetScreenWidth()) - 80.0f, 16.0f);

        if (!game.started) {
            DrawTextCentered("NEXIUM RHYTHM", width * 0.5f, height * 0.5f - 40.0f, 44.0f,
                             colors::White);
            DrawTextCentered("space para comecar", width * 0.5f, height * 0.5f + 16.0f, 22.0f,
                             colors::LightGray);
            DrawTextCentered("D F J K na linha de baixo", width * 0.5f, height * 0.5f + 46.0f,
                             18.0f, colors::Gray);
        }

        EndScene();
    }

    // The drawer releases the assets; the files were only a delivery mechanism.
    Audio::UnloadAll();
    UnloadTexture(logo);
    std::remove(Audio::music.Path());
    std::remove(Audio::hit.Path());
    std::remove(Audio::miss.Path());

    Close();
    return 0;
}
