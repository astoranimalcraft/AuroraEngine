// Aurora3D — audio.h
// Sound and Music, backed by XAudio2.
//
// A Sound is a short clip decoded into memory at load time and mixed by the
// engine; a Music is a longer track that is decoded and streamed in the
// background so a multi-minute file does not sit in RAM. Both are opaque handles
// owned by the engine.
//
// Audio is best-effort: if XAudio2 is unavailable (or there is no output device,
// as in a headless test), loading returns nullptr, every query reports silence,
// and the program keeps running. Nothing here can crash a build.
#pragma once

#include "aurora/math.h"

// windows.h defines PlaySound as an alias for PlaySoundW/PlaySoundA. Undo it so
// an application that includes <windows.h> first still gets the engine's
// PlaySound() declaration.
#ifdef PlaySound
#undef PlaySound
#endif

namespace a3d {

struct Sound;
struct Music;

// ---------------------------------------------------------------- device ----
// True when an audio device was opened and playback is possible.
bool IsAudioDeviceReady();
// Global output level, 0 to 1. Applies to everything already playing.
void SetMasterVolume(float volume);
float GetMasterVolume();
// Suspends or resumes all audio output without changing per-source state.
void PauseAudioDevice(bool paused);
bool IsAudioDevicePaused();

// ----------------------------------------------------------------- sound ----
// Supported today: RIFF/WAVE (PCM 8/16/24/32-bit and IEEE float), decoded fully
// into memory.
Sound* LoadSound(const char* path);
void UnloadSound(Sound* sound);

// Plays a sound. Returns false when it could not start (no device, or the sound
// is null). Starting a sound that is already playing restarts it.
bool PlaySound(Sound* sound);
// Plays on a background voice so several copies can overlap, and pitches the
// copy randomly within `pitchRange` (in semitones) — the usual trick for making
// repeated footsteps and gunshots sound varied. Returns the voice handle, or 0.
uint64_t PlaySoundMulti(Sound* sound, float pitchRange = 0.0f);
void StopSoundMulti(uint64_t voice);
bool IsSoundMultiPlaying(uint64_t voice);

void StopSound(Sound* sound);
void PauseSound(Sound* sound);
void ResumeSound(Sound* sound);
bool IsSoundPlaying(const Sound* sound);

void SetSoundVolume(Sound* sound, float volume);   // 0 to 1
float GetSoundVolume(const Sound* sound);
void SetSoundPitch(Sound* sound, float pitch);     // 1 = normal, 2 = one octave up
float GetSoundPitch(const Sound* sound);
// -1 left, 0 centre, 1 right.
void SetSoundPan(Sound* sound, float pan);
bool IsSoundLooping(const Sound* sound);
void SetSoundLooping(Sound* sound, bool looping);
float GetSoundLength(const Sound* sound);          // seconds

// ----------------------------------------------------------------- music ----
// Music streams from disk: LoadMusic opens the file, and the engine decodes
// ahead of the play cursor on a worker thread. Call UpdateMusic() once per frame
// to keep the stream fed.
Music* LoadMusic(const char* path);
void UnloadMusic(Music* music);

void PlayMusic(Music* music);          // restarts from the beginning
void StopMusic(Music* music);
void PauseMusic(Music* music);
void ResumeMusic(Music* music);
bool IsMusicPlaying(const Music* music);

void SetMusicVolume(Music* music, float volume);
float GetMusicVolume(const Music* music);
void SetMusicPitch(Music* music, float pitch);
void SetMusicLooping(Music* music, bool looping);

// Playback position in seconds, and total length in seconds.
float GetMusicTime(Music* music);
float GetMusicLength(const Music* music);
void SeekMusic(Music* music, float seconds);

// Feeds the streaming decoder. Called automatically once per frame by
// a3d::EndDrawing(); exposed for hosts that drive their own loop.
void UpdateMusic();

} // namespace a3d
