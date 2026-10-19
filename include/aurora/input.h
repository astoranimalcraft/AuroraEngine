// Aurora3D — input.h
// Key and mouse-button identifiers plus the input query functions. Values are
// stable; they are deliberately not tied to Win32 virtual-key codes so the enum
// can outlive the current backend.
#pragma once

#include "aurora/math.h"

namespace a3d {

enum class Key {
    Unknown = 0,
    Space, Apostrophe, Comma, Minus, Period, Slash,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Semicolon, Equal,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket, Backslash, RightBracket, Grave,
    Escape, Enter, Tab, Backspace, Insert, Delete,
    Right, Left, Down, Up,
    PageUp, PageDown, Home, End,
    CapsLock, ScrollLock, NumLock, PrintScreen, Pause,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4,
    Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal, KeypadDivide, KeypadMultiply, KeypadSubtract, KeypadAdd,
    KeypadEnter, KeypadEqual,
    LeftShift, LeftControl, LeftAlt, LeftSuper,
    RightShift, RightControl, RightAlt, RightSuper,
    Menu,
    Count
};

enum class MouseButton {
    Left = 0,
    Right,
    Middle,
    X1,
    X2,
    Count
};

// Gamepad buttons, matching the XInput layout. `Gamepad::None` is returned when
// no controller is connected.
enum class GamepadButton {
    DpadUp = 0, DpadDown, DpadLeft, DpadRight,
    Start, Back, LeftThumb, RightThumb,
    LeftShoulder, RightShoulder,
    A, B, X, Y,
    Count
};

// Which controller slot an input call refers to. Slot 0 is the usual one.
inline constexpr int kMaxGamepads = 4;

// True while the key is held down.
bool IsKeyDown(Key key);
// True only on the frame the key transitioned to down.
bool IsKeyPressed(Key key);
// True only on the frame the key transitioned to up.
bool IsKeyReleased(Key key);
// True on the frame the key auto-repeated.
bool IsKeyRepeated(Key key);
// Any key that was pressed this frame; useful for "press any key" screens.
bool IsAnyKeyPressed();

// Text typed this frame, in the order it arrived, already filtered to printable
// characters. Backspace arrives as '\b'. Call it once per frame from the frame
// loop; it drains the queue.
int GetCharPressed();

bool IsMouseButtonDown(MouseButton button);
bool IsMouseButtonPressed(MouseButton button);
bool IsMouseButtonReleased(MouseButton button);

Vec2 GetMousePosition();          // pixels, relative to the client area
float GetMouseX();                // GetMousePosition().x
float GetMouseY();                // GetMousePosition().y
Vec2 GetMouseDelta();             // pixels moved since the previous frame
float GetMouseWheelDelta();       // notches this frame (positive = scroll up)
bool IsMouseInWindow();
void SetMouseCaptured(bool captured);
bool IsMouseCaptured();
void SetMouseVisible(bool visible);
bool IsMouseVisible();
// Moves the cursor inside the client area, in pixels. No-op while captured.
void SetMousePosition(Vec2 position);
// Clamps the cursor to the window while it is focused.
void SetMouseLockedInWindow(bool locked);

// Normalized device coordinates of the cursor in [-1, 1], y up.
// Feed directly into Camera3D::RayThroughNdc() for picking.
Vec2 GetMouseNdc();

// --------------------------------------------------------------- gamepad ----
// XInput controllers. All calls are safe with no controller plugged in: they
// report "not available" and zeroed values instead of failing.
bool IsGamepadAvailable(int gamepad = 0);
const char* GetGamepadName(int gamepad = 0);
bool IsGamepadButtonDown(GamepadButton button, int gamepad = 0);
bool IsGamepadButtonPressed(GamepadButton button, int gamepad = 0);
bool IsGamepadButtonReleased(GamepadButton button, int gamepad = 0);
// Stick axes in [-1, 1], y up. `stick` is 0 for the left stick, 1 for the right.
Vec2 GetGamepadStick(int stick = 0, int gamepad = 0);
// Trigger values in [0, 1].
float GetGamepadTriggerLeft(int gamepad = 0);
float GetGamepadTriggerRight(int gamepad = 0);
// Motor rumble, 0 to 1.
void SetGamepadVibration(float leftMotor, float rightMotor, int gamepad = 0);
// Polls the controllers; called automatically once per frame, exposed for hosts
// that drive their own loop.
void PollGamepads();

} // namespace a3d
