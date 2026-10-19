// Aurora3D — aurora.h
// Umbrella header. Include this one and you have the whole library.
//
//   #include <aurora/aurora.h>
//
//   int main() {
//       a3d::Config config;
//       config.title = "Hello Aurora3D";
//       if (!a3d::Init(config)) return 1;
//
//       a3d::OrbitCamera orbit;
//
//       while (!a3d::WindowShouldClose()) {
//           a3d::UpdateOrbitCamera(orbit);            // mouse + WASD
//           a3d::BeginScene(orbit.ToCamera());
//           a3d::DrawGrid();
//           a3d::DrawCube({0, 0.5f, 0}, {1, 1, 1}, a3d::colors::Orange);
//           a3d::EndScene();
//       }
//
//       a3d::Close();
//       return 0;
//   }
#pragma once

#include "aurora/audio.h"
#include "aurora/camera.h"
#include "aurora/color.h"
#include "aurora/draw.h"
#include "aurora/input.h"
#include "aurora/log.h"
#include "aurora/math.h"
#include "aurora/mesh.h"
#include "aurora/model.h"
#include "aurora/shader.h"
#include "aurora/texture.h"
#include "aurora/video.h"

namespace a3d {

struct Config {
    int width = 1280;
    int height = 720;
    const char* title = "Aurora3D";
    bool resizable = true;
    bool vsync = true;
    // Enables the D3D12 debug layer and GPU-based validation. Debug builds only:
    // it needs the Graphics Tools optional feature installed on Windows.
    bool debugLayer = false;
};

// Creates the window, the D3D12 device and the built-in resources. Returns false
// on failure (details in the log). Calling it twice is a no-op that returns true.
bool Init(const Config& config = {});

// Releases everything Init() created. Safe to call when not initialized.
void Close();

bool IsInitialized();

// --------------------------------------------------------------- window ----
// True once the user closes the window or presses Escape.
bool WindowShouldClose();
void RequestClose();

int GetScreenWidth();
int GetScreenHeight();
float GetAspectRatio();

void SetWindowTitle(const char* title);
void SetVsync(bool enabled);
bool IsVsyncEnabled();

// Resizes the client area. The swap chain follows on the next frame.
void SetWindowSize(int width, int height);
// Switches between windowed and borderless fullscreen at the desktop resolution.
void SetFullscreen(bool fullscreen);
bool IsFullscreen();
// Shows or hides the OS cursor (independent of mouse capture).
void ShowCursor(bool visible);
// True while the window owns the keyboard focus; pause on `false`.
bool IsWindowFocused();
bool IsWindowResized();     // true for the frame in which the client size changed
void MinimizeWindow();
void MaximizeWindow();
void RestoreWindow();
bool IsWindowMaximized();
// Enables or disables resizing by the user at runtime.
void SetWindowResizable(bool resizable);

// Blocks the calling thread for `seconds`. A convenience so a simple program
// never needs <thread>.
void Wait(float seconds);

// ---------------------------------------------------------------- frame ----
// True while the window is minimized, in which case the renderer skips drawing.
bool IsWindowMinimized();

// Seconds since Init(). Simulation time: it advances with DeltaTime().
float GetTime();
// Seconds the previous frame took.
float GetFrameTime();
// Frames per second, averaged over the last second.
float GetFps();
uint64_t GetFrameCount();

// BeginScene / EndScene / BeginSceneOverlay live in aurora/draw.h, next to the
// drawing calls they bracket.

// ---------------------------------------------------------------- lights ----
// Direction the light travels; normalized internally. Defaults to a soft key
// light from above and to the left.
void SetLightDirection(Vec3 direction);
// Ambient colour and its intensity in [0, 1]. Defaults to white at 0.25.
void SetAmbient(Color color, float strength);

// ------------------------------------------------------------ statistics ----
struct RenderStats {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t lines = 0;
    uint32_t stateChanges = 0;
};

// Counters for the frame that was drawn last.
RenderStats GetRenderStats();

// ------------------------------------------------------------ debugging ----
// Enables or disables the D3D12 debug layer. Must be called before Init().
void SetDebugLayerEnabled(bool enabled);

} // namespace a3d
