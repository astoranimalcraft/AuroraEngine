// NexiumEngine — platform backend interface (internal).
//
// This is the seam between the engine and the operating system. Everything the
// engine needs from the host — a window, an event stream, monitors, the cursor,
// the clipboard — is declared here and nothing else. The Win32 implementation
// lives in platform_win32.cpp; a different backend only has to implement this
// header.
//
// The header deliberately does not include <windows.h> or any other OS header.
// Keeping it clean is what makes a second backend possible at all: a backend
// that had to be compiled against the same translation unit as the engine could
// never be swapped.
//
// The event model follows SDL3: the backend translates native messages into
// PlatformEvent values and pushes them onto a queue; the window layer drains the
// queue and turns it into the engine's input state. The backend never writes
// into the engine's input arrays directly, so a new backend cannot desynchronise
// them.
#pragma once

#include "nexium/input.h"
#include "nexium/math.h"

#include <cstdint>
#include <memory>
#include <string>

namespace n3d::internal {

// --------------------------------------------------------------- events -----
// A flattened event. A tagged union would be tighter, but every payload is a
// small POD and the flat form keeps the queue trivially copyable and free of
// placement-new, which matters because the queue is drained every frame.
enum class PlatformEventType : uint8_t {
    None = 0,
    Quit,               // the process should end
    CloseRequested,     // the user asked to close this window
    Resized,
    Moved,
    Exposed,            // the client area needs a repaint
    Minimized,
    Maximized,
    Restored,
    FocusGained,
    FocusLost,
    KeyDown,
    KeyUp,
    TextInput,          // a printable character or an editing control code
    MouseMoved,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    MouseEntered,
    MouseLeft,
    DisplayChanged,     // a monitor was added, removed or reconfigured
    GamepadChanged,     // a controller was plugged in or unplugged
};

struct PlatformEvent {
    PlatformEventType type = PlatformEventType::None;

    // KeyDown / KeyUp
    Key key = Key::Unknown;
    bool repeat = false;

    // TextInput. `codepoint` is a Unicode scalar value, not a byte.
    int codepoint = 0;

    // MouseButtonDown / MouseButtonUp
    MouseButton button = MouseButton::Left;

    // MouseMoved / MouseButtonDown / MouseButtonUp. `position` is in client
    // pixels. `delta` is the movement since the previous event and is the only
    // usable reading while the cursor is captured, because then `position` is
    // pinned to the centre.
    Vec2 position{};
    Vec2 delta{};

    // MouseWheel, in notches. Separate axes because a tilt wheel or a touchpad
    // can scroll sideways.
    float wheelX = 0.0f;
    float wheelY = 0.0f;

    // Resized
    int width = 0;
    int height = 0;
};

// ----------------------------------------------------------- capabilities ---
// What the backend can actually do. A game that wants to run on more than one
// backend checks this instead of assuming, so a missing feature degrades instead
// of failing at the call.
struct PlatformCaps {
    bool fullscreen = false;
    bool customCursors = false;
    bool clipboard = false;
    bool opacity = false;
    bool alwaysOnTop = false;
    bool monitorQuery = false;
    bool dpiAware = false;
    bool textInput = false;
    bool gamepad = false;
    bool touch = false;
    bool rawMouse = false;
    bool orientation = false;
    bool vibration = false;
};

// --------------------------------------------------------------- displays ---
struct DisplayInfo {
    Vec2 position{};        // top-left in virtual-desktop coordinates
    Vec2 size{};            // in pixels
    float refreshRate = 0.0f;
    float dpiScale = 1.0f;
    std::string name;
};

// ----------------------------------------------------------------- window ---
struct PlatformWindowDesc {
    int width = 1280;
    int height = 720;
    std::string title = "NexiumEngine";
    bool resizable = true;
    bool vsync = true;
};

// ---------------------------------------------------------------- backend ---
// One process-wide instance. The engine owns it and talks to it only through
// this interface, so the Win32 types never leak into the rest of the engine.
class PlatformBackend {
public:
    virtual ~PlatformBackend() = default;

    // Identifies the backend in logs, e.g. "win32".
    virtual const char* Name() const = 0;
    virtual PlatformCaps Caps() const = 0;

    virtual bool Init() = 0;
    virtual void Shutdown() = 0;

    virtual bool CreateWindow(const PlatformWindowDesc& desc) = 0;
    virtual void DestroyWindow() = 0;

    // The native window handle, as an opaque pointer. The GPU layer casts it to
    // HWND; that cast is the one place the DirectX dependency is allowed to
    // assume Win32, and it is confined to gpu.cpp.
    virtual void* NativeHandle() const = 0;

    // Drains the OS message queue and appends the translated events. Returns
    // false when the process should terminate.
    virtual bool PumpEvents() = 0;
    // Pops one event. Returns false when the queue is empty.
    virtual bool PollEvent(PlatformEvent& out) = 0;

    virtual void SetTitle(const char* title) = 0;
    virtual void SetSize(int width, int height) = 0;
    virtual void SetFullscreen(bool fullscreen) = 0;
    virtual bool Fullscreen() const = 0;
    virtual void Minimize() = 0;
    virtual void Maximize() = 0;
    virtual void Restore() = 0;
    virtual bool Maximized() const = 0;
    virtual void SetResizable(bool resizable) = 0;
    virtual bool Focused() const = 0;
    virtual bool Visible() const = 0;
    virtual void Show() = 0;
    virtual void Hide() = 0;

    virtual Vec2 Position() const = 0;
    virtual void SetPosition(Vec2 position) = 0;
    virtual void SetMinSize(int width, int height) = 0;
    virtual void SetAlwaysOnTop(bool onTop) = 0;
    virtual void SetOpacity(float opacity) = 0;
    virtual bool SetIconFromFile(const char* path) = 0;
    virtual void CenterOnDisplay(int index) = 0;

    virtual int DisplayCount() const = 0;
    virtual DisplayInfo Display(int index) const = 0;
    virtual int CurrentDisplay() const = 0;
    virtual float DpiScale() const = 0;

    // A UTF-8 string owned by the backend, valid until the next call.
    virtual void SetClipboardText(const char* text) = 0;
    virtual const char* ClipboardText() = 0;

    // Cursor. `position` is in client pixels. Capturing hides the cursor and
    // confines it, and makes MouseMoved events carry a usable `delta`.
    virtual void SetCursorVisible(bool visible) = 0;
    virtual void SetCursorShape(MouseCursor cursor) = 0;
    virtual void SetCursorClipped(bool clipped) = 0;
    virtual void SetCursorPosition(Vec2 position) = 0;
    virtual void SetMouseCaptured(bool captured) = 0;
    virtual bool MouseCaptured() const = 0;
    virtual bool MouseInWindow() const = 0;
    // Loads a .cur/.ani file as the cursor. Returns false if the file cannot be
    // read or the backend has no custom cursors.
    virtual bool SetCursorFromFile(const char* path) = 0;
    // Reads the mouse from raw input rather than the pointer. Raw input reports
    // every device movement at full resolution and is unaffected by cursor
    // acceleration or the pointer hitting the screen edge, which is what a mouse
    // look wants. Best-effort: a backend without it keeps the normal path.
    virtual void SetRawMouse(bool enabled) = 0;
    virtual bool RawMouse() const = 0;

    // Restricts the mouse to a sub-rectangle of the client area, in pixels. Used
    // for letterboxed rendering and split-screen. Zero size clears it.
    virtual void SetMouseRect(Vec2 topLeft, Vec2 size) = 0;
};

// --------------------------------------------------------------- registry ---
// The backend the engine uses. Win32 is installed by default; SetPlatformBackend
// exists so a host can substitute one before n3d::Init().
PlatformBackend* GetPlatformBackend();
void SetPlatformBackend(std::unique_ptr<PlatformBackend> backend);
// Creates the default backend for the current build. Returns nullptr when the
// platform is not compiled in.
std::unique_ptr<PlatformBackend> CreateDefaultPlatformBackend();

} // namespace n3d::internal
