// Aurora3D — audio backend (XAudio2).
//
// Sound and Music share one decode path: a RIFF/WAVE file is parsed and its PCM
// payload is handed to XAudio2 as a single buffer in its native format (8/16/24/
// 32-bit integer or 32-bit float). XAudio2 mixes and resamples, so there is no
// conversion on our side.
//
// Each Sound owns one reusable voice whose source buffer points at the Sound's
// own PCM, so the address is stable for as long as the Sound lives. Music does
// the same but keeps a play cursor so GetMusicTime() works, and can loop.
//
// The whole subsystem is optional at runtime. XAudio2Create() can fail on a
// machine with no audio endpoint, and every entry point below tolerates that by
// reporting silence rather than aborting the program.
#include "aurora/audio.h"

#include "aurora/log.h"

#include "internal/common.h"

#include <xaudio2.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace a3d {

// The public headers keep these opaque; the definitions live here because only
// this file touches XAudio2.
struct Sound {
    IXAudio2SourceVoice* voice = nullptr;
    std::vector<uint8_t> pcm;      // owned copy of the file's sample data
    WAVEFORMATEX format{};
    float volume = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;
    bool looping = false;
    std::string name;
};

struct Music {
    IXAudio2SourceVoice* voice = nullptr;
    std::vector<uint8_t> pcm;
    WAVEFORMATEX format{};
    float volume = 1.0f;
    float pitch = 1.0f;
    bool looping = false;
    std::string name;
    uint64_t framesPerSecond = 0;
    // Frame count the current playback started from, so a restart can be detected.
    uint64_t startFrames = 0;
    uint64_t playedFrames = 0;
};

namespace {

IXAudio2* gEngine = nullptr;
IXAudio2MasteringVoice* gMaster = nullptr;
float gMasterVolume = 1.0f;
bool gDevicePaused = false;
bool gTriedInit = false;

uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) |
                                 (static_cast<uint32_t>(p[3]) << 24));
}

// Parsed WAV: format plus a copy of the sample data.
struct WavData {
    WAVEFORMATEX format{};
    std::vector<uint8_t> samples;
};

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;

bool IsFloatSubFormat(const uint8_t* guid) {
    // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = 00000003-0000-0010-8000-00aa00389b71
    static const uint8_t kFloat[16] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                       0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    return std::memcmp(guid, kFloat, 16) == 0;
}

bool ReadWholeFile(const char* path, std::vector<uint8_t>& out) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return false;
    }
    out.resize(static_cast<size_t>(size));
    const size_t read = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    return read == out.size();
}

// Minimal RIFF/WAVE parser: walks the chunk list looking for "fmt " and "data".
bool ParseWav(const std::vector<uint8_t>& bytes, WavData& out, const char* path) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        LogError("audio: '%s' is not a RIFF/WAVE file", path);
        return false;
    }

    bool haveFormat = false;
    bool haveData = false;
    size_t cursor = 12;

    while (cursor + 8 <= bytes.size()) {
        const uint8_t* chunk = bytes.data() + cursor;
        const uint32_t chunkSize = ReadU32(chunk + 4);
        const uint8_t* body = chunk + 8;
        // A truncated or lying chunk size would run past the buffer; clamp it.
        const size_t available = bytes.size() - cursor - 8;
        const size_t bodySize = chunkSize < available ? chunkSize : available;

        if (std::memcmp(chunk, "fmt ", 4) == 0 && bodySize >= 16) {
            out.format.wFormatTag = ReadU16(body + 0);
            out.format.nChannels = ReadU16(body + 2);
            out.format.nSamplesPerSec = ReadU32(body + 4);
            out.format.nAvgBytesPerSec = ReadU32(body + 8);
            out.format.nBlockAlign = ReadU16(body + 12);
            out.format.wBitsPerSample = ReadU16(body + 14);
            out.format.cbSize = 0;

            if (out.format.wFormatTag == kFormatExtensible && bodySize >= 40) {
                // The real format is in the SubFormat GUID.
                out.format.wFormatTag = IsFloatSubFormat(body + 24) ? kFormatFloat : kFormatPcm;
            }
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            out.samples.assign(body, body + bodySize);
            haveData = true;
        }

        // Chunks are word aligned: an odd size is followed by a pad byte.
        cursor += 8 + bodySize + (bodySize & 1u);
    }

    if (!haveFormat || !haveData || out.samples.empty()) {
        LogError("audio: '%s' has no usable fmt/data chunks", path);
        return false;
    }
    if (out.format.wFormatTag != kFormatPcm && out.format.wFormatTag != kFormatFloat) {
        LogError("audio: '%s' uses unsupported format tag %u (PCM and IEEE float only)",
                 path, out.format.wFormatTag);
        return false;
    }
    return true;
}

bool EnsureAudio() {
    if (gEngine) return true;
    if (gTriedInit) return false;   // one attempt is enough; do not spam the log
    gTriedInit = true;

    if (FAILED(XAudio2Create(&gEngine, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        LogWarn("audio: XAudio2Create failed; running without sound");
        gEngine = nullptr;
        return false;
    }
    if (FAILED(gEngine->CreateMasteringVoice(&gMaster))) {
        LogWarn("audio: no audio output device; running without sound");
        gEngine->Release();
        gEngine = nullptr;
        return false;
    }
    gMaster->SetVolume(gMasterVolume);
    LogInfo("audio: XAudio2 device ready");
    return true;
}

// Creates a source voice whose single submitted buffer points at `pcm`, which
// must outlive the voice.
IXAudio2SourceVoice* CreateVoice(const WAVEFORMATEX& format, const std::vector<uint8_t>& pcm,
                                 bool looping) {
    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(gEngine->CreateSourceVoice(&voice, &format))) {
        LogError("audio: CreateSourceVoice failed");
        return nullptr;
    }

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(pcm.size());
    buffer.pAudioData = pcm.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = looping ? XAUDIO2_LOOP_INFINITE : 0;
    if (FAILED(voice->SubmitSourceBuffer(&buffer))) {
        LogError("audio: SubmitSourceBuffer failed");
        voice->DestroyVoice();
        return nullptr;
    }
    return voice;
}

// (Re)submits a Sound's buffer. Used by PlaySound so looping can change between
// plays without recreating the voice.
bool Resubmit(Sound* sound) {
    sound->voice->FlushSourceBuffers();
    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(sound->pcm.size());
    buffer.pAudioData = sound->pcm.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = sound->looping ? XAUDIO2_LOOP_INFINITE : 0;
    return SUCCEEDED(sound->voice->SubmitSourceBuffer(&buffer));
}

void ApplyVolume(IXAudio2SourceVoice* voice, float volume) {
    if (voice) voice->SetVolume(Clamp(volume, 0.0f, 1.0f));
}

void ApplyPitch(IXAudio2SourceVoice* voice, float pitch) {
    if (voice) voice->SetFrequencyRatio(Clamp(pitch, 0.05f, 8.0f));
}

bool VoicePlaying(IXAudio2SourceVoice* voice) {
    if (!voice) return false;
    XAUDIO2_VOICE_STATE state{};
    voice->GetState(&state);
    return state.BuffersQueued > 0;
}

// Tracks the voices started by PlaySoundMulti() so they can be stopped and so a
// finished one can be reclaimed.
struct MultiVoice {
    uint64_t id = 0;
    IXAudio2SourceVoice* voice = nullptr;
};
std::vector<MultiVoice> gMultiVoices;
uint64_t gNextVoiceId = 1;

void ReapMultiVoices() {
    for (auto it = gMultiVoices.begin(); it != gMultiVoices.end();) {
        if (!VoicePlaying(it->voice)) {
            it->voice->DestroyVoice();
            it = gMultiVoices.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

// ---------------------------------------------------------------- device ----
bool IsAudioDeviceReady() { return EnsureAudio(); }

void SetMasterVolume(float volume) {
    gMasterVolume = Clamp(volume, 0.0f, 1.0f);
    if (gMaster) gMaster->SetVolume(gMasterVolume);
}

float GetMasterVolume() { return gMasterVolume; }

void PauseAudioDevice(bool paused) {
    if (!gEngine) return;
    gDevicePaused = paused;
    if (paused) gEngine->StopEngine();
    else gEngine->StartEngine();
}

bool IsAudioDevicePaused() { return gDevicePaused; }

// ----------------------------------------------------------------- sound ----
Sound* LoadSound(const char* path) {
    if (!path || !EnsureAudio()) return nullptr;

    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes)) {
        LogError("audio: cannot read '%s'", path);
        return nullptr;
    }

    WavData wav;
    if (!ParseWav(bytes, wav, path)) return nullptr;

    auto* sound = new Sound();
    sound->pcm = std::move(wav.samples);
    sound->format = wav.format;
    sound->name = path;
    // The voice's buffer points at sound->pcm, which never moves again.
    sound->voice = CreateVoice(sound->format, sound->pcm, false);
    if (!sound->voice) {
        delete sound;
        return nullptr;
    }
    return sound;
}

void UnloadSound(Sound* sound) {
    if (!sound) return;
    if (sound->voice) {
        sound->voice->Stop(0);
        sound->voice->FlushSourceBuffers();
        sound->voice->DestroyVoice();
    }
    delete sound;
}

bool PlaySound(Sound* sound) {
    if (!sound || !sound->voice) return false;
    sound->voice->Stop(0);
    if (!Resubmit(sound)) return false;
    sound->voice->Start(0);
    return true;
}

uint64_t PlaySoundMulti(Sound* sound, float pitchRange) {
    if (!sound || !EnsureAudio()) return 0;
    ReapMultiVoices();

    // A dedicated voice reusing the Sound's PCM, so several can overlap.
    IXAudio2SourceVoice* voice = CreateVoice(sound->format, sound->pcm, sound->looping);
    if (!voice) return 0;

    if (pitchRange > 0.0f) {
        // pitchRange is in semitones; a symmetric offset becomes a frequency
        // ratio of 2^(semitones/12).
        const float semitones = RandomFloat(-pitchRange, pitchRange);
        voice->SetFrequencyRatio(std::pow(2.0f, semitones / 12.0f));
    }
    voice->SetVolume(Clamp(sound->volume, 0.0f, 1.0f));
    voice->Start(0);

    const uint64_t id = gNextVoiceId++;
    gMultiVoices.push_back(MultiVoice{id, voice});
    return id;
}

void StopSoundMulti(uint64_t voice) {
    for (auto it = gMultiVoices.begin(); it != gMultiVoices.end(); ++it) {
        if (it->id == voice) {
            it->voice->Stop(0);
            it->voice->FlushSourceBuffers();
            it->voice->DestroyVoice();
            gMultiVoices.erase(it);
            return;
        }
    }
}

bool IsSoundMultiPlaying(uint64_t voice) {
    for (const MultiVoice& entry : gMultiVoices) {
        if (entry.id == voice) return VoicePlaying(entry.voice);
    }
    return false;
}

void StopSound(Sound* sound) {
    if (!sound || !sound->voice) return;
    sound->voice->Stop(0);
    sound->voice->FlushSourceBuffers();
}

void PauseSound(Sound* sound) {
    if (sound && sound->voice) sound->voice->Stop(0);
}

void ResumeSound(Sound* sound) {
    if (sound && sound->voice) sound->voice->Start(0);
}

bool IsSoundPlaying(const Sound* sound) {
    return sound && sound->voice && VoicePlaying(sound->voice);
}

void SetSoundVolume(Sound* sound, float volume) {
    if (!sound) return;
    sound->volume = Clamp(volume, 0.0f, 1.0f);
    ApplyVolume(sound->voice, sound->volume);
}

float GetSoundVolume(const Sound* sound) { return sound ? sound->volume : 0.0f; }

void SetSoundPitch(Sound* sound, float pitch) {
    if (!sound) return;
    sound->pitch = pitch;
    ApplyPitch(sound->voice, pitch);
}

float GetSoundPitch(const Sound* sound) { return sound ? sound->pitch : 1.0f; }

void SetSoundPan(Sound* sound, float pan) {
    if (!sound || !sound->voice) return;
    sound->pan = Clamp(pan, -1.0f, 1.0f);

    // XAudio2 pans through an output matrix. Only mono and stereo sources are
    // handled; anything wider is left alone.
    const int channels = sound->format.nChannels;
    if (channels < 1 || channels > 2) return;
    float matrix[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float p = sound->pan;
    matrix[0] = (1.0f - p) * 0.5f;   // left input -> left output
    matrix[1] = (1.0f + p) * 0.5f;   // left input -> right output
    sound->voice->SetOutputMatrix(nullptr, static_cast<UINT32>(channels), 2, matrix);
}

bool IsSoundLooping(const Sound* sound) { return sound && sound->looping; }

void SetSoundLooping(Sound* sound, bool looping) {
    // Loop behaviour is baked into the submitted buffer, so take effect on the
    // next PlaySound().
    if (sound) sound->looping = looping;
}

float GetSoundLength(const Sound* sound) {
    if (!sound || sound->format.nAvgBytesPerSec == 0) return 0.0f;
    return static_cast<float>(sound->pcm.size()) /
           static_cast<float>(sound->format.nAvgBytesPerSec);
}

// ----------------------------------------------------------------- music ----
Music* LoadMusic(const char* path) {
    if (!path || !EnsureAudio()) return nullptr;

    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes)) {
        LogError("audio: cannot read '%s'", path);
        return nullptr;
    }

    WavData wav;
    if (!ParseWav(bytes, wav, path)) return nullptr;

    auto* music = new Music();
    music->pcm = std::move(wav.samples);
    music->format = wav.format;
    music->name = path;
    music->framesPerSecond = music->format.nSamplesPerSec;
    music->voice = CreateVoice(music->format, music->pcm, false);
    if (!music->voice) {
        delete music;
        return nullptr;
    }
    return music;
}

void UnloadMusic(Music* music) {
    if (!music) return;
    if (music->voice) {
        music->voice->Stop(0);
        music->voice->FlushSourceBuffers();
        music->voice->DestroyVoice();
    }
    delete music;
}

void PlayMusic(Music* music) {
    if (!music || !music->voice) return;
    music->voice->Stop(0);
    music->voice->FlushSourceBuffers();

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(music->pcm.size());
    buffer.pAudioData = music->pcm.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = music->looping ? XAUDIO2_LOOP_INFINITE : 0;
    if (FAILED(music->voice->SubmitSourceBuffer(&buffer))) return;

    music->startFrames = 0;
    music->playedFrames = 0;
    music->voice->Start(0);
}

void StopMusic(Music* music) {
    if (!music || !music->voice) return;
    music->voice->Stop(0);
    music->voice->FlushSourceBuffers();
    music->playedFrames = 0;
}

void PauseMusic(Music* music) {
    if (music && music->voice) music->voice->Stop(0);
}

void ResumeMusic(Music* music) {
    if (music && music->voice) music->voice->Start(0);
}

bool IsMusicPlaying(const Music* music) {
    return music && music->voice && VoicePlaying(music->voice);
}

void SetMusicVolume(Music* music, float volume) {
    if (!music) return;
    music->volume = Clamp(volume, 0.0f, 1.0f);
    ApplyVolume(music->voice, music->volume);
}

float GetMusicVolume(const Music* music) { return music ? music->volume : 0.0f; }

void SetMusicPitch(Music* music, float pitch) {
    if (!music) return;
    music->pitch = pitch;
    ApplyPitch(music->voice, pitch);
}

void SetMusicLooping(Music* music, bool looping) {
    if (music) music->looping = looping;
}

float GetMusicLength(const Music* music) {
    if (!music || music->format.nAvgBytesPerSec == 0) return 0.0f;
    return static_cast<float>(music->pcm.size()) /
           static_cast<float>(music->format.nAvgBytesPerSec);
}

float GetMusicTime(Music* music) {
    if (!music || !music->voice || music->framesPerSecond == 0) return 0.0f;
    // Ask XAudio2 for the running sample count, which is monotonic while playing.
    XAUDIO2_VOICE_STATE state{};
    music->voice->GetState(&state);
    if (state.SamplesPlayed < music->startFrames) {
        // The voice was restarted, so the counter reset.
        music->startFrames = 0;
    }
    music->playedFrames = state.SamplesPlayed - music->startFrames;
    return static_cast<float>(music->playedFrames) /
           static_cast<float>(music->framesPerSecond);
}

void SeekMusic(Music* music, float seconds) {
    // A single submitted buffer cannot be seeked, so restart. The reported time
    // then starts from zero; sample-accurate seeking would need a chunked stream.
    if (!music) return;
    (void)seconds;
    PlayMusic(music);
}

void UpdateMusic() { ReapMultiVoices(); }

} // namespace a3d
