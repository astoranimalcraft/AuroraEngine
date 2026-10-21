// NexiumEngine — NexiumEngine.h
// Umbrella header. Include this one and you have the whole library.
//
//   #include <nexium/NexiumEngine.h>
//
//   int main() {
//       n3d::Config config;
//       config.title = "Hello NexiumEngine";
//       if (!n3d::Init(config)) return 1;
//
//       n3d::OrbitCamera orbit;
//
//       while (!n3d::WindowShouldClose()) {
//           n3d::UpdateOrbitCamera(orbit);            // mouse + WASD
//           n3d::BeginScene(orbit.ToCamera());
//           n3d::DrawGrid();
//           n3d::DrawCube({0, 0.5f, 0}, {1, 1, 1}, n3d::colors::Orange);
//           n3d::EndScene();
//       }
//
//       n3d::Close();
//       return 0;
//   }
#pragma once

#include "nexium/audio.h"
#include "nexium/camera.h"
#include "nexium/color.h"
#include "nexium/draw.h"
#include "nexium/input.h"
#include "nexium/log.h"
#include "nexium/math.h"
#include "nexium/mesh.h"
#include "nexium/model.h"
#include "nexium/shader.h"
#include "nexium/storage.h"
#include "nexium/texture.h"
#include "nexium/video.h"

namespace n3d {

struct Config {
    int width = 1280;
    int height = 720;
    const char* title = "NexiumEngine";
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
// True once the user closes the window, presses the exit key (Escape by default)
// or presses Alt+F4.
bool WindowShouldClose();
void RequestClose();

// The key that closes the window. Escape by default; pass Key::Unknown to make
// the game handle its own exit. Checked once per frame after the game's update.
void SetExitKey(Key key);
Key GetExitKey();

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
// Toggles between windowed and fullscreen. Handy on an F11 key.
void ToggleFullscreen();
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
// Smallest client size the user may resize to. Zero means no limit on that axis.
void SetWindowMinSize(int width, int height);
// Moves the window to the centre of the monitor it is on (-1) or of `monitor`.
void CenterWindow(int monitor = -1);
// Keeps the window above normal windows.
void SetWindowAlwaysOnTop(bool onTop);
bool IsWindowAlwaysOnTop();

// Position of the window frame in virtual-screen coordinates (multi-monitor aware).
Vec2 GetWindowPosition();
void SetWindowPosition(Vec2 position);

// Monitor geometry. `index` selects a monitor; -1 (the default) means the one the
// window is currently on. Indices run from 0 to GetMonitorCount() - 1.
int GetMonitorCount();
Vec2 GetMonitorPosition(int index = -1);
Vec2 GetMonitorSize(int index = -1);
// Current refresh rate in Hz, or 0 when it cannot be determined.
float GetMonitorRefreshRate(int index = -1);
// Device name of the monitor, e.g. "\\\\.\\DISPLAY1".
const char* GetMonitorName(int index = -1);
// Scale factor of the window's monitor: 1.0 at 96 DPI, 1.5 at 144 DPI. Multiply
// pixel sizes by this to stay physically consistent across displays.
float GetWindowDpiScale();

// Overall window opacity, 0 (invisible) to 1 (opaque).
void SetWindowOpacity(float opacity);
// Loads a .ico file and uses it as the window and taskbar icon.
bool SetWindowIcon(const char* path);

// UTF-8 text on the system clipboard. The pointer from GetClipboardText() is owned
// by the engine and stays valid until the next call.
void SetClipboardText(const char* text);
const char* GetClipboardText();

// ------------------------------------------------------- platform/backend ---
// Which platform backend is driving the window, e.g. "win32". Useful in a log
// line when the same game runs on more than one build.
const char* GetPlatformName();

// What the current backend can do. A game that runs on more than one backend
// checks this instead of assuming a feature exists, so a missing one degrades
// instead of failing at the call. Every field is false before Init().
struct PlatformCapabilities {
    bool fullscreen = false;
    bool customCursors = false;
    bool clipboard = false;
    bool windowOpacity = false;
    bool alwaysOnTop = false;
    bool monitorQuery = false;
    bool dpiAware = false;
    bool textInput = false;
    bool gamepad = false;
    bool rawMouse = false;
    bool touch = false;
    bool displayOrientation = false;
    bool vibration = false;
};
PlatformCapabilities GetPlatformCapabilities();

// Shows or hides the window after it exists, without recreating it.
void ShowWindow();
void HideWindow();
bool IsWindowVisible();

// True for the frame in which the window was maximized or restored by the user.
bool IsWindowMaximizedChanged();

// Index of the monitor the window is currently on, for use with the GetMonitor*
// calls above. Returns 0 when it cannot be determined.
int GetCurrentMonitor();

// -------------------------------------------------------------- cursor ----
// Loads a .cur or .ani file and uses it as the cursor until another shape or file
// is selected. Returns false if the backend has no custom cursors or the file
// cannot be read.
bool SetMouseCursorFromFile(const char* path);

// Reads the mouse from raw input rather than the pointer: every device movement
// arrives at full resolution, unaffected by cursor acceleration or the pointer
// reaching the screen edge. The right mode for a mouse-look camera. Best-effort;
// a backend without raw input keeps the normal path.
void SetRawMouseInput(bool enabled);
bool IsRawMouseInputEnabled();

// Restricts the pointer to a sub-rectangle of the client area, in pixels. Used
// for letterboxed rendering and split-screen, where the mouse should not wander
// into the black bars. Pass a zero size to clear it.
void SetMouseRect(Vec2 topLeft, Vec2 size);

// Cursor position inside the client area as a fraction of the client size, in
// [0, 1]. Handy for shaders and for UI that scales with the window.
Vec2 GetMousePositionNormalized();

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

// BeginScene / EndScene / BeginSceneOverlay live in nexium/draw.h, next to the
// drawing calls they bracket.

// ---------------------------------------------------------------- lights ----
// Direction the light travels; normalized internally. Defaults to a soft key
// light from above and to the left.
void SetLightDirection(Vec3 direction);
// Ambient colour and its intensity in [0, 1]. Defaults to white at 0.25.
void SetAmbient(Color color, float strength);

// ----------------------------------------------------------------- shadows ---
// A single directional shadow map, cast by the light set with SetLightDirection().
// Off by default, so a program that never asks for shadows pays nothing.
//
// When on, EndScene() renders the scene once more from the light's point of view
// into a depth-only target, then the main pass samples it with hardware PCF. The
// shadow follows the camera, so its resolution stays useful over a large world.
//
// Cost is one extra geometry pass. Raise `resolution` for crisper edges and lower
// it for speed; 1024 and 2048 are the useful range, and 2048 is the default.
void SetShadowsEnabled(bool enabled);
bool ShadowsEnabled();

// Size of the shadow map, a power of two. Changing it reallocates the target on
// the next frame.
void SetShadowResolution(uint32_t resolution);
uint32_t GetShadowResolution();

// Half-extent, in world units, of the box around the camera that casts and
// receives shadows. Smaller means sharper shadows over a smaller area.
void SetShadowDistance(float distance);
float GetShadowDistance();

// Depth bias, in light-space depth units, and normal bias, in world units. Raise
// them if shadow acne (self-shadowing stripes) appears; lower them if shadows
// detach from their casters (peter-panning).
void SetShadowBias(float depthBias, float normalBias);
float GetShadowDepthBias();
float GetShadowNormalBias();

// Excludes a mesh from the shadow pass, so it casts no shadow. Use it for the sky
// box, for ground planes that should not self-shadow, and for anything that only
// exists to be seen.
void SetMeshCastsShadow(struct Mesh* mesh, bool casts);

// ----------------------------------------------------------- environment ----
// A background sky drawn from an equirectangular (2:1) texture. The image is
// drawn as a cube centred on the camera, so it never shifts as the camera moves.
//
// The texture may be an RGBA8 image loaded with LoadTextureFromFile(), or a real
// Radiance HDR loaded with LoadHDRFromFile(). Only the HDR path preserves values
// above 1, so only it can be used as a light; an 8-bit sky is a backdrop and
// nothing more. For a physically lit scene, use the float path and let the sky
// drive both the ambient and the reflections.
void SetSky(Texture* equirectangular);
Texture* GetSky();
// Tints and dims the sky without touching the image. Defaults to white.
void SetSkyTint(Color tint);
// Draws the sky immediately, inside the current scene. Call it before the rest of
// the scene so the depth buffer rejects everything the sky does not cover.
void DrawSky();

// Averages the environment into the engine's ambient colour and takes the
// brightest direction as the sun. `strength` is the ambient intensity in [0, 1].
// This is the cheap "the sky lights the scene" step: one pass over the image, no
// irradiance map, no BRDF integration. Returns false when no sky is set.
bool UpdateLightingFromSky(float strength = 0.5f);

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

} // namespace n3d

// 2D games read better against a shorter namespace. n2d is the same library, not
// a separate one: every symbol above is reachable through either name, and both
// spellings can be mixed freely in one translation unit. Prefer n2d in a program
// that never leaves the XY plane, n3d when it works in three dimensions.
namespace n2d = n3d;
