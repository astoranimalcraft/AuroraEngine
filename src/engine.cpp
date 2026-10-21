// NexiumEngine — engine lifecycle: window, device, frame loop.
#include "nexium/NexiumEngine.h"

#include "internal/gpu.h"
#include "internal/renderer.h"
#include "internal/resources.h"
#include "internal/window.h"

#include <string>

namespace n3d {
namespace {

bool gInitialized = false;
bool gDebugLayerRequested = false;
std::string gTitle;

} // namespace

void SetDebugLayerEnabled(bool enabled) { gDebugLayerRequested = enabled; }

bool IsInitialized() { return gInitialized; }

bool Init(const Config& config) {
    if (gInitialized) return true;

    internal::WindowDesc desc{};
    desc.width = config.width;
    desc.height = config.height;
    desc.title = config.title ? config.title : "NexiumEngine";
    desc.resizable = config.resizable;
    desc.vsync = config.vsync;
    gTitle = desc.title;

    if (!internal::Window::Get().Create(desc)) {
        LogError("Init: failed to create the window");
        return false;
    }

    auto& gpu = internal::GpuContext::Get();
    // The one place the DirectX dependency is allowed to assume Win32: the
    // backend hands the window over as an opaque pointer and the GPU layer, which
    // is DirectX 12 and therefore Windows by construction, casts it back to HWND.
    // A backend for another platform would need a renderer that is not D3D12.
    if (!gpu.Initialize(static_cast<HWND>(internal::Window::Get().Handle()),
                        static_cast<uint32_t>(config.width), static_cast<uint32_t>(config.height),
                        config.debugLayer || gDebugLayerRequested)) {
        LogError("Init: failed to initialize Direct3D 12");
        internal::Window::Get().Destroy();
        return false;
    }

    gpu.SetVsync(config.vsync);
    if (!internal::RootSignature::Instance().Create(gpu.Device())) {
        LogError("Init: failed to create the root signature");
        gpu.Shutdown();
        internal::Window::Get().Destroy();
        return false;
    }

    // Built-in shaders and the default texture are created up front so the first
    // draw call never has to allocate a pipeline.
    if (!internal::CreateBuiltinShaders()) {
        LogError("Init: failed to create the built-in shaders");
        internal::RootSignature::Instance().Destroy();
        gpu.Shutdown();
        internal::Window::Get().Destroy();
        return false;
    }

    if (!GetDefaultTexture()) {
        LogError("Init: failed to create the default texture");
        internal::DestroyBuiltinShaders();
        internal::RootSignature::Instance().Destroy();
        gpu.Shutdown();
        internal::Window::Get().Destroy();
        return false;
    }

    // The built-in bitmap font so a program can call n3d::DrawText() with no
    // asset files at all.
    if (!internal::CreateBuiltinFont()) {
        LogError("Init: failed to create the built-in font");
        internal::DestroyBuiltinFont();
        internal::DestroyBuiltinShaders();
        internal::RootSignature::Instance().Destroy();
        gpu.Shutdown();
        internal::Window::Get().Destroy();
        return false;
    }

    LogInfo("NexiumEngine ready — adapter '%s', %ux%u", gpu.AdapterName(), gpu.Width(), gpu.Height());
    // Storage entries can now be loaded, so give them a way to release handles.
    internal::InstallStorageOps();
    gInitialized = true;
    return true;
}

void Close() {
    if (!gInitialized) return;

    auto& gpu = internal::GpuContext::Get();
    gpu.WaitForIdle();

    internal::Renderer::Get().Reset();
    internal::DestroyBuiltinFont();
    internal::DestroyBuiltinShaders();
    internal::RootSignature::Instance().Destroy();
    gpu.Shutdown();
    internal::Window::Get().Destroy();

    gInitialized = false;
}

// ---------------------------------------------------------------- window ---
bool WindowShouldClose() {
    return !gInitialized || internal::Window::Get().ShouldClose();
}

void RequestClose() { internal::Window::Get().RequestClose(); }

void SetExitKey(Key key) { internal::Window::Get().SetExitKey(key); }
Key GetExitKey() { return internal::Window::Get().ExitKey(); }

int GetScreenWidth() { return internal::Window::Get().Width(); }
int GetScreenHeight() { return internal::Window::Get().Height(); }

float GetAspectRatio() { return internal::GpuContext::Get().Aspect(); }

void SetWindowTitle(const char* title) {
    gTitle = title ? title : "";
    internal::Window::Get().SetTitle(gTitle.c_str());
}

void SetVsync(bool enabled) {
    internal::Window::Get().SetVsync(enabled);
    internal::GpuContext::Get().SetVsync(enabled);
}
bool IsVsyncEnabled() { return internal::GpuContext::Get().Vsync(); }

void SetWindowSize(int width, int height) {
    internal::Window::Get().SetClientSize(width, height);
}

void SetFullscreen(bool fullscreen) { internal::Window::Get().SetFullscreen(fullscreen); }
bool IsFullscreen() { return internal::Window::Get().Fullscreen(); }
void ToggleFullscreen() { SetFullscreen(!IsFullscreen()); }

// Note: this deliberately shadows the Win32 ::ShowCursor, which is why the
// engine's own cursor handling goes through Window::SetMouseVisible().
void ShowCursor(bool visible) { internal::Window::Get().SetMouseVisible(visible); }

bool IsWindowFocused() { return internal::Window::Get().Focused(); }
bool IsWindowResized() { return internal::Window::Get().ConsumeResized(); }
void MinimizeWindow() { internal::Window::Get().Minimize(); }
void MaximizeWindow() { internal::Window::Get().Maximize(); }
void RestoreWindow() { internal::Window::Get().Restore(); }
bool IsWindowMaximized() { return internal::Window::Get().Maximized(); }
void SetWindowResizable(bool resizable) { internal::Window::Get().SetResizable(resizable); }
void SetWindowMinSize(int width, int height) { internal::Window::Get().SetMinSize(width, height); }
void CenterWindow(int monitor) { internal::Window::Get().CenterOnMonitor(monitor); }
void SetWindowAlwaysOnTop(bool onTop) { internal::Window::Get().SetAlwaysOnTop(onTop); }
bool IsWindowAlwaysOnTop() { return internal::Window::Get().AlwaysOnTop(); }

Vec2 GetWindowPosition() { return internal::Window::Get().WindowPosition(); }
void SetWindowPosition(Vec2 position) { internal::Window::Get().SetWindowPosition(position); }

int GetMonitorCount() { return internal::Window::Get().MonitorCount(); }
Vec2 GetMonitorPosition(int index) { return internal::Window::Get().MonitorPosition(index); }
Vec2 GetMonitorSize(int index) { return internal::Window::Get().MonitorSize(index); }
float GetMonitorRefreshRate(int index) {
    return internal::Window::Get().MonitorRefreshRate(index);
}
const char* GetMonitorName(int index) { return internal::Window::Get().MonitorName(index); }
float GetWindowDpiScale() { return internal::Window::Get().DpiScale(); }

void SetWindowOpacity(float opacity) { internal::Window::Get().SetOpacity(opacity); }
bool SetWindowIcon(const char* path) { return internal::Window::Get().SetIconFromFile(path); }

void SetClipboardText(const char* text) { internal::Window::Get().SetClipboardText(text); }
const char* GetClipboardText() { return internal::Window::Get().GetClipboardText(); }

// ------------------------------------------------------- platform/backend ---
const char* GetPlatformName() {
    const char* name = internal::Window::Get().BackendName();
    return name ? name : "none";
}

PlatformCapabilities GetPlatformCapabilities() {
    const internal::PlatformCaps caps = internal::Window::Get().Caps();
    PlatformCapabilities out;
    out.fullscreen = caps.fullscreen;
    out.customCursors = caps.customCursors;
    out.clipboard = caps.clipboard;
    out.windowOpacity = caps.opacity;
    out.alwaysOnTop = caps.alwaysOnTop;
    out.monitorQuery = caps.monitorQuery;
    out.dpiAware = caps.dpiAware;
    out.textInput = caps.textInput;
    out.gamepad = caps.gamepad;
    out.rawMouse = caps.rawMouse;
    out.touch = caps.touch;
    out.displayOrientation = caps.orientation;
    out.vibration = caps.vibration;
    return out;
}

void ShowWindow() { internal::Window::Get().Show(); }
void HideWindow() { internal::Window::Get().Hide(); }
bool IsWindowVisible() { return internal::Window::Get().Visible(); }
bool IsWindowMaximizedChanged() { return internal::Window::Get().ConsumeMaximizedChanged(); }
int GetCurrentMonitor() { return internal::Window::Get().CurrentMonitor(); }

// -------------------------------------------------------------- cursor ----
bool SetMouseCursorFromFile(const char* path) {
    return internal::Window::Get().SetCursorFromFile(path);
}
void SetRawMouseInput(bool enabled) { internal::Window::Get().SetRawMouse(enabled); }
bool IsRawMouseInputEnabled() { return internal::Window::Get().RawMouse(); }
void SetMouseRect(Vec2 topLeft, Vec2 size) { internal::Window::Get().SetMouseRect(topLeft, size); }

Vec2 GetMousePositionNormalized() {
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();
    if (width <= 0 || height <= 0) return Vec2{};
    const Vec2 position = internal::Window::Get().MousePosition();
    return Vec2{position.x / static_cast<float>(width), position.y / static_cast<float>(height)};
}

void Wait(float seconds) {
    if (seconds <= 0.0f) return;
    Sleep(static_cast<DWORD>(seconds * 1000.0f));
}

// ----------------------------------------------------------------- frame ---
bool IsWindowMinimized() { return internal::Window::Get().IsMinimized(); }

float GetTime() { return static_cast<float>(internal::Window::Get().Time()); }
float GetFrameTime() { return internal::Window::Get().DeltaTime(); }
float GetFps() { return internal::Window::Get().Fps(); }
uint64_t GetFrameCount() { return internal::Window::Get().FrameCount(); }

void BeginScene(const Camera3D& camera, Color clear) {
    if (!gInitialized) return;

    auto& window = internal::Window::Get();
    auto& gpu = internal::GpuContext::Get();

    // A minimized window has a zero-sized client area; D3D12 forbids that, so the
    // frame is skipped entirely rather than drawing into nothing.
    if (window.IsMinimized()) return;
    if (!gpu.Resize(static_cast<uint32_t>(window.Width()), static_cast<uint32_t>(window.Height())))
        return;

    internal::Renderer::Get().BeginScene(camera, true, clear);
}

void BeginSceneOverlay(const Camera3D& camera) {
    if (!gInitialized) return;

    auto& window = internal::Window::Get();
    auto& gpu = internal::GpuContext::Get();

    if (window.IsMinimized()) return;
    if (!gpu.Resize(static_cast<uint32_t>(window.Width()), static_cast<uint32_t>(window.Height())))
        return;

    internal::Renderer::Get().BeginScene(camera, false, colors::Clear);
}

void EndScene() {
    if (!gInitialized) return;

    // Submitting is skipped when BeginScene() decided not to open a frame (a
    // minimized window), but the message pump below must still run or the window
    // would stop responding for as long as it stays minimized.
    if (internal::Renderer::Get().InScene()) internal::Renderer::Get().EndScene();

    internal::Window::Get().FramePacing();
    internal::Window::Get().PumpMessages();
    PollGamepads();
    UpdateMusic();
    UpdateVideo();
    ReloadChangedShaders();
}

// ---------------------------------------------------------------- lights ---
void SetLightDirection(Vec3 direction) { internal::Renderer::Get().SetLightDirection(direction); }
void SetAmbient(Color color, float strength) { internal::Renderer::Get().SetAmbient(color, strength); }

// --------------------------------------------------------------- shadows ---
void SetShadowsEnabled(bool enabled) { internal::Renderer::Get().SetShadowsEnabled(enabled); }
bool ShadowsEnabled() { return internal::Renderer::Get().ShadowsEnabled(); }
void SetShadowResolution(uint32_t resolution) {
    internal::Renderer::Get().SetShadowResolution(resolution);
}
uint32_t GetShadowResolution() { return internal::Renderer::Get().ShadowResolution(); }
void SetShadowDistance(float distance) { internal::Renderer::Get().SetShadowDistance(distance); }
float GetShadowDistance() { return internal::Renderer::Get().ShadowDistance(); }
void SetShadowBias(float depthBias, float normalBias) {
    internal::Renderer::Get().SetShadowBias(depthBias, normalBias);
}
float GetShadowDepthBias() { return internal::Renderer::Get().ShadowDepthBias(); }
float GetShadowNormalBias() { return internal::Renderer::Get().ShadowNormalBias(); }

void SetMeshCastsShadow(Mesh* mesh, bool casts) {
    if (!mesh) return;
    // Toggling the flag on the mesh itself means every draw of it agrees, without
    // the caller having to track the state per draw call.
    if (casts) {
        mesh->flags &= ~MeshNoShadow;
    } else {
        mesh->flags |= MeshNoShadow;
    }
}

// ----------------------------------------------------------- environment ---
void SetSky(Texture* equirectangular) { internal::Renderer::Get().SetSky(equirectangular); }
Texture* GetSky() { return internal::Renderer::Get().Sky(); }
void SetSkyTint(Color tint) { internal::Renderer::Get().SetSkyTint(tint); }
void DrawSky() { internal::Renderer::Get().DrawSky(); }

bool UpdateLightingFromSky(float strength) {
    Texture* sky = internal::Renderer::Get().Sky();
    if (!sky || !sky->envValid) return false;

    // The average colour becomes the ambient. A sky is mostly blue, so this alone
    // is what stops shadows from being neutral grey.
    SetAmbient(Color{sky->envAverage[0], sky->envAverage[1], sky->envAverage[2]}, strength);

    // The brightest direction becomes the sun. SetLightDirection takes the
    // direction the light *travels*, which is the opposite of where the sun is.
    const Vec3 toSun = sky->envSunDirection;
    SetLightDirection(Vec3{-toSun.x, -toSun.y, -toSun.z});
    return true;
}

RenderStats GetRenderStats() {
    const internal::FrameStats& stats = internal::Renderer::Get().Stats();
    RenderStats out;
    out.drawCalls = stats.drawCalls;
    out.triangles = stats.triangles;
    out.lines = stats.lines;
    out.stateChanges = stats.stateChanges;
    return out;
}

} // namespace n3d
