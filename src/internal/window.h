// Aurora3D — Win32 window, message pump and input state (internal).
#pragma once

#include "aurora/input.h"
#include "aurora/math.h"

#include "internal/common.h"

#include <array>
#include <cstdint>
#include <string>

namespace a3d::internal {

struct WindowDesc {
    int width = 1280;
    int height = 720;
    std::string title = "Aurora3D";
    bool resizable = true;
    bool vsync = true;
};

// Owns the HWND, translates Win32 messages into the engine's input state, and
// drives the fixed-step frame clock.
class Window {
public:
    static Window& Get();

    bool Create(const WindowDesc& desc);
    void Destroy();

    // Pumps messages and updates per-frame input edges and the timer.
    // Returns false once the user has asked to close the window.
    bool PumpMessages();

    // Blocks until the next frame is due. No-op when vsync is enabled, because
    // Present() already paces the loop.
    void FramePacing();

    HWND Handle() const { return hwnd_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsMinimized() const { return minimized_; }
    bool ShouldClose() const { return shouldClose_; }
    void RequestClose() { shouldClose_ = true; }

    // UTF-8 title; ignored before Create() or after Destroy().
    void SetTitle(const char* title);

    // Window geometry and state, driven by the public a3d:: window API.
    void SetClientSize(int width, int height);
    void SetFullscreen(bool fullscreen);
    bool Fullscreen() const { return fullscreen_; }
    void Minimize();
    void Maximize();
    void Restore();
    bool Maximized() const;
    void SetResizable(bool resizable);
    bool Focused() const { return focused_; }
    // True for the single frame after the client size changed.
    bool ConsumeResized() { const bool r = resized_; resized_ = false; return r; }
    void HideCursor(bool hide);

    // Timing, in seconds.
    double Time() const { return time_; }
    float DeltaTime() const { return deltaTime_; }
    uint64_t FrameCount() const { return frameCount_; }
    float Fps() const { return fps_; }

    void SetVsync(bool enabled) { vsync_ = enabled; }
    bool Vsync() const { return vsync_; }

    // Input queries used by the public input.h API.
    bool KeyDown(Key key) const;
    bool KeyPressed(Key key) const;
    bool KeyReleased(Key key) const;
    bool KeyRepeated(Key key) const;
    bool MouseDown(MouseButton button) const;
    bool MousePressed(MouseButton button) const;
    bool MouseReleased(MouseButton button) const;

    Vec2 MousePosition() const { return mousePosition_; }
    Vec2 MouseDelta() const { return mouseDelta_; }
    float MouseWheel() const { return mouseWheel_; }
    bool MouseInWindow() const { return mouseInWindow_; }
    bool MouseVisible() const { return mouseVisible_; }
    bool MouseLocked() const { return mouseLocked_; }

    void SetMouseCaptured(bool captured);
    bool MouseCaptured() const { return mouseCaptured_; }
    void SetMouseVisible(bool visible);
    void SetMousePosition(Vec2 position);
    void SetMouseLockedInWindow(bool locked) { mouseLocked_ = locked; }

    // Typed text, drained one character at a time by the public API.
    int NextChar();

private:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void UpdateInputEdges();

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool resizable_ = true;
    bool vsync_ = true;
    bool minimized_ = false;
    bool shouldClose_ = false;
    bool focused_ = true;
    bool fullscreen_ = false;
    bool resized_ = false;
    // Saved window placement, so leaving fullscreen restores exactly where the
    // window was rather than a guessed position.
    WINDOWPLACEMENT savedPlacement_{};
    LONG_PTR savedStyle_ = 0;
    bool trackingMouseLeave_ = false;
    bool mouseCaptured_ = false;
    bool mouseVisible_ = true;
    bool mouseLocked_ = false;

    static constexpr int kKeyCount = static_cast<int>(Key::Count);
    std::array<bool, kKeyCount> keyDown_{};
    std::array<bool, kKeyCount> keyPrev_{};
    std::array<bool, kKeyCount> keyRepeat_{};
    std::array<bool, static_cast<int>(MouseButton::Count)> mouseDown_{};
    std::array<bool, static_cast<int>(MouseButton::Count)> mousePrev_{};

    // Printable characters typed this frame, in arrival order. Bounded so a key
    // held down while the frame is slow cannot grow it without limit.
    static constexpr size_t kMaxCharQueue = 64;
    std::array<int, kMaxCharQueue> charQueue_{};
    size_t charCount_ = 0;

    Vec2 mousePosition_{};
    Vec2 mousePrevPosition_{};
    Vec2 mouseDelta_{};
    float mouseWheel_ = 0.0f;
    bool mouseInWindow_ = false;

    // Frame clock, advanced once per pump by the measured frame time.
    double time_ = 0.0;
    float deltaTime_ = 0.0f;
    uint64_t frameCount_ = 0;
    double fpsTimer_ = 0.0;
    uint32_t fpsFrames_ = 0;
    float fps_ = 0.0f;
    LARGE_INTEGER frequency_{};
    LARGE_INTEGER lastCounter_{};
    LARGE_INTEGER frameStart_{};
};

} // namespace a3d::internal
