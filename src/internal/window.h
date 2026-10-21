// NexiumEngine — window and input state (internal).
//
// This class no longer talks to the OS. It owns the engine's input state and the
// frame clock, and drives a PlatformBackend for everything that needs the
// operating system: window creation, the message pump, monitors, the cursor and
// the clipboard. The backend reports events; this class folds them into the
// key/mouse arrays the public API queries.
//
// Keeping the state here rather than in the backend is what makes a second
// backend safe: the pressed/released edge detection, the text queue and the
// timing live in one place and cannot be reimplemented differently per platform.
#pragma once

#include "nexium/input.h"
#include "nexium/math.h"

#include "internal/platform.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace n3d::internal {

struct WindowDesc {
    int width = 1280;
    int height = 720;
    std::string title = "NexiumEngine";
    bool resizable = true;
    bool vsync = true;
};

class Window {
public:
    static Window& Get();

    bool Create(const WindowDesc& desc);
    void Destroy();

    // Pumps the backend, folds the events into input state, and advances the
    // frame clock. Returns false once the user has asked to close the window.
    bool PumpMessages();

    // Blocks until the next frame is due. No-op when vsync is enabled, because
    // Present() already paces the loop.
    void FramePacing();

    void* Handle() const { return backend_->NativeHandle(); }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsMinimized() const { return minimized_; }
    bool ShouldClose() const { return shouldClose_; }
    void RequestClose() { shouldClose_ = true; }

    // UTF-8 title; ignored before Create() or after Destroy().
    void SetTitle(const char* title);

    // Window geometry and state, driven by the public n3d:: window API.
    void SetClientSize(int width, int height);
    void SetFullscreen(bool fullscreen);
    bool Fullscreen() const { return backend_->Fullscreen(); }
    void Minimize();
    void Maximize();
    void Restore();
    bool Maximized() const;
    // True for the single frame in which the user maximized or restored the
    // window, mirroring ConsumeResized().
    bool ConsumeMaximizedChanged() { const bool r = maximizedChanged_; maximizedChanged_ = false; return r; }
    // Index of the monitor the window currently sits on.
    int CurrentMonitor() const;
    void SetResizable(bool resizable);
    bool Focused() const { return backend_->Focused(); }
    bool Visible() const { return backend_->Visible(); }
    void Show();
    void Hide();

    // Position of the window frame in virtual-screen coordinates.
    Vec2 WindowPosition() const;
    void SetWindowPosition(Vec2 position);

    // Monitor geometry and mode. `index` counts the monitors as the OS reports
    // them; the window's own monitor is the default everywhere it matters.
    int MonitorCount() const;
    Vec2 MonitorPosition(int index = -1) const;
    Vec2 MonitorSize(int index = -1) const;
    float MonitorRefreshRate(int index = -1) const;
    const char* MonitorName(int index = -1) const;
    // Dots-per-inch scale of the window's monitor (1.0 at 96 DPI).
    float DpiScale() const;

    // Overall window opacity, 1.0 being fully opaque.
    void SetOpacity(float opacity);

    // Loads a .ico file and sets it as the window (and taskbar) icon.
    bool SetIconFromFile(const char* path);

    // UTF-8 clipboard access. GetClipboardText() returns a pointer to a buffer
    // owned by the engine that stays valid until the next call.
    void SetClipboardText(const char* text);
    const char* GetClipboardText();

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
    Vec2 MouseWheelVector() const { return mouseWheelVector_; }
    bool MouseInWindow() const { return backend_->MouseInWindow(); }
    bool MouseVisible() const { return mouseVisible_; }
    bool MouseLocked() const { return mouseLocked_; }

    void SetMouseCaptured(bool captured);
    bool MouseCaptured() const { return backend_->MouseCaptured(); }
    void SetMouseVisible(bool visible);
    void SetMousePosition(Vec2 position);
    void SetMouseLockedInWindow(bool locked) { mouseLocked_ = locked; }
    void SetMouseCursor(MouseCursor cursor);
    MouseCursor CursorShape() const { return mouseCursor_; }
    bool SetCursorFromFile(const char* path);
    void SetRawMouse(bool enabled);
    bool RawMouse() const { return backend_->RawMouse(); }

    // Confines the OS cursor to the client area while the window is focused. Unlike
    // capture this leaves the cursor visible and does not recentre it, so it is the
    // right tool for a windowed game that just wants the pointer kept inside.
    void SetCursorClipped(bool clipped);
    bool CursorClipped() const { return cursorClipped_; }

    // Restricts the mouse to a sub-rectangle of the client area, in pixels. Used
    // for letterboxed rendering and split-screen. Zero size clears it.
    void SetMouseRect(Vec2 topLeft, Vec2 size);
    Vec2 MouseRectOrigin() const { return mouseRectOrigin_; }
    Vec2 MouseRectSize() const { return mouseRectSize_; }

    // Smallest client size the user may resize the window to. Zero means no limit.
    void SetMinSize(int width, int height);
    int MinWidth() const { return minWidth_; }
    int MinHeight() const { return minHeight_; }

    // Moves the window to the centre of the monitor it is on (or `monitor`).
    void CenterOnMonitor(int monitor = -1);

    // Keeps the window above normal windows.
    void SetAlwaysOnTop(bool onTop);
    bool AlwaysOnTop() const { return alwaysOnTop_; }

    // The key that closes the window, checked once per frame by PumpMessages().
    // `Key::Unknown` disables it.
    void SetExitKey(Key key) { exitKey_ = key; }
    Key ExitKey() const { return exitKey_; }

    // Typed text, drained one character at a time by the public API.
    int NextChar();

    // What the backend can do. Checked by the public API before it promises a
    // feature, so a call on a backend without it degrades instead of lying.
    PlatformCaps Caps() const { return backend_->Caps(); }
    const char* BackendName() const { return backend_->Name(); }

private:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void ApplyEvent(const PlatformEvent& event);

    PlatformBackend* backend_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    bool resizable_ = true;
    bool vsync_ = true;
    bool minimized_ = false;
    bool shouldClose_ = false;
    bool resized_ = false;
    bool maximizedChanged_ = false;
    bool mouseVisible_ = true;
    bool mouseLocked_ = false;
    bool cursorClipped_ = false;
    bool alwaysOnTop_ = false;
    int minWidth_ = 0;
    int minHeight_ = 0;
    Key exitKey_ = Key::Escape;

    Vec2 mouseRectOrigin_{};
    Vec2 mouseRectSize_{};

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
    Vec2 mouseWheelVector_{};
    MouseCursor mouseCursor_ = MouseCursor::Default;

    // Frame clock, advanced once per pump by the measured frame time.
    double time_ = 0.0;
    float deltaTime_ = 0.0f;
    uint64_t frameCount_ = 0;
    double fpsTimer_ = 0.0;
    uint32_t fpsFrames_ = 0;
    float fps_ = 0.0f;
    std::chrono::steady_clock::time_point frameStart_{};
};

} // namespace n3d::internal
