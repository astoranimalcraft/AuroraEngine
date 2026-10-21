// NexiumEngine — video.h
// Video playback, backed by Media Foundation.
//
// A Video decodes to BGRA frames on a worker thread and hands the newest one to
// DrawVideo(), which uploads it as a texture and draws it as a screen-space quad.
// Audio tracks are ignored for now.
//
// Playback is best-effort and platform gated: on a build without Media
// Foundation, or for a file MF cannot open, LoadVideo() returns nullptr and the
// program keeps running.
#pragma once

#include "nexium/color.h"
#include "nexium/math.h"
#include "nexium/texture.h"

namespace n3d {

struct Video;

// Opens `path` and prepares the decoder. Does not start playback.
Video* LoadVideo(const char* path);
void UnloadVideo(Video* video);

void PlayVideo(Video* video);
void StopVideo(Video* video);
void PauseVideo(Video* video);
void ResumeVideo(Video* video);

// Jumps to `seconds` from the start. Clamped at zero. The next frame decoded is
// the one at the new position, so the picture follows the seek on the next tick.
void SeekVideo(Video* video, float seconds);

bool IsVideoPlaying(const Video* video);
bool IsVideoReady(const Video* video);   // first frame decoded and drawable

void SetVideoVolume(Video* video, float volume);   // reserved; audio is not decoded yet
void SetVideoLooping(Video* video, bool looping);
bool IsVideoLooping(const Video* video);

// Frame size in pixels.
int GetVideoWidth(const Video* video);
int GetVideoHeight(const Video* video);
// Frame rate, frames per second.
float GetVideoFps(const Video* video);
// Total length in seconds.
float GetVideoLength(const Video* video);
// Current position in seconds.
float GetVideoTime(const Video* video);

// Draws the current frame. Coordinates are pixels (or world units under a 2D
// camera), `position` being the top-left corner. Returns false when no frame is
// available yet.
bool DrawVideo(Video* video, float x, float y, Color tint = colors::White);
bool DrawVideoV(Video* video, Vec2 position, Color tint = colors::White);
// The frame texture, for callers that want to size or transform the quad
// themselves. Null until the first frame arrives. It is a normal texture the width
// and height reported by GetVideoWidth()/GetVideoHeight(), so it can be drawn like
// any other.
Texture* GetVideoTexture(Video* video);

// Advances the decoder and uploads the newest frame to the GPU. Called
// automatically once per frame by n3d::EndScene(); exposed for custom loops.
void UpdateVideo();

} // namespace n3d
