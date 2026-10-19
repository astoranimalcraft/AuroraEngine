// Aurora3D — engine lifecycle: window, device, frame loop.
#include "aurora/aurora.h"

#include "internal/gpu.h"
#include "internal/renderer.h"
#include "internal/resources.h"
#include "internal/window.h"

#include <string>

namespace a3d {
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
    desc.title = config.title ? config.title : "Aurora3D";
    desc.resizable = config.resizable;
    desc.vsync = config.vsync;
    gTitle = desc.title;

    if (!internal::Window::Get().Create(desc)) {
        LogError("Init: failed to create the window");
        return false;
    }

    auto& gpu = internal::GpuContext::Get();
    if (!gpu.Initialize(internal::Window::Get().Handle(), static_cast<uint32_t>(config.width),
                        static_cast<uint32_t>(config.height),
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

    // The built-in bitmap font so a program can call a3d::DrawText() with no
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

    LogInfo("Aurora3D ready — adapter '%s', %ux%u", gpu.AdapterName(), gpu.Width(), gpu.Height());
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

RenderStats GetRenderStats() {
    const internal::FrameStats& stats = internal::Renderer::Get().Stats();
    RenderStats out;
    out.drawCalls = stats.drawCalls;
    out.triangles = stats.triangles;
    out.lines = stats.lines;
    out.stateChanges = stats.stateChanges;
    return out;
}

} // namespace a3d
