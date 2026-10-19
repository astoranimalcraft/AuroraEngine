// Aurora3D — public input queries.
//
// Every function forwards to the Win32 backend, which tracks the current and
// previous state of each key so "pressed" and "released" are exact one-frame
// edges rather than a guess based on a timer.
#include "aurora/input.h"

#include "internal/gpu.h"
#include "internal/window.h"

namespace a3d {

bool IsKeyDown(Key key) { return internal::Window::Get().KeyDown(key); }
bool IsKeyPressed(Key key) { return internal::Window::Get().KeyPressed(key); }
bool IsKeyReleased(Key key) { return internal::Window::Get().KeyReleased(key); }
bool IsKeyRepeated(Key key) { return internal::Window::Get().KeyRepeated(key); }

bool IsAnyKeyPressed() {
    for (int i = 0; i < static_cast<int>(Key::Count); ++i) {
        if (IsKeyPressed(static_cast<Key>(i))) return true;
    }
    return false;
}

int GetCharPressed() { return internal::Window::Get().NextChar(); }

bool IsMouseButtonDown(MouseButton button) {
    return internal::Window::Get().MouseDown(button);
}
bool IsMouseButtonPressed(MouseButton button) {
    return internal::Window::Get().MousePressed(button);
}
bool IsMouseButtonReleased(MouseButton button) {
    return internal::Window::Get().MouseReleased(button);
}

Vec2 GetMousePosition() { return internal::Window::Get().MousePosition(); }
float GetMouseX() { return internal::Window::Get().MousePosition().x; }
float GetMouseY() { return internal::Window::Get().MousePosition().y; }
Vec2 GetMouseDelta() { return internal::Window::Get().MouseDelta(); }
float GetMouseWheelDelta() { return internal::Window::Get().MouseWheel(); }
bool IsMouseInWindow() { return internal::Window::Get().MouseInWindow(); }

void SetMouseCaptured(bool captured) { internal::Window::Get().SetMouseCaptured(captured); }
bool IsMouseCaptured() { return internal::Window::Get().MouseCaptured(); }
void SetMouseVisible(bool visible) { internal::Window::Get().SetMouseVisible(visible); }
bool IsMouseVisible() { return internal::Window::Get().MouseVisible(); }
void SetMousePosition(Vec2 position) { internal::Window::Get().SetMousePosition(position); }
void SetMouseLockedInWindow(bool locked) { internal::Window::Get().SetMouseLockedInWindow(locked); }

Vec2 GetMouseNdc() {
    const Vec2 position = internal::Window::Get().MousePosition();
    const float width = static_cast<float>(internal::GpuContext::Get().Width());
    const float height = static_cast<float>(internal::GpuContext::Get().Height());
    if (width <= 0.0f || height <= 0.0f) return Vec2{};

    // Pixels (origin top-left, y down) to NDC (origin centre, y up).
    return Vec2{(position.x / width) * 2.0f - 1.0f, 1.0f - (position.y / height) * 2.0f};
}

} // namespace a3d
