// NexiumEngine — window state, event folding and the frame clock.
//
// The OS work lives in a PlatformBackend; this file turns its event stream into
// the engine's input state. Nothing here includes <windows.h>, so the file
// compiles for any backend the build provides.
#include "internal/window.h"

#include "nexium/log.h"

#include <chrono>

namespace n3d::internal {

Window& Window::Get() {
    static Window instance;
    return instance;
}

bool Window::Create(const WindowDesc& desc) {
    width_ = desc.width;
    height_ = desc.height;
    resizable_ = desc.resizable;
    vsync_ = desc.vsync;

    backend_ = GetPlatformBackend();
    if (!backend_) {
        LogError("Window::Create: no platform backend available");
        return false;
    }
    if (!backend_->Init()) {
        LogError("Window::Create: backend '%s' failed to initialise", backend_->Name());
        return false;
    }

    PlatformWindowDesc platformDesc;
    platformDesc.width = desc.width;
    platformDesc.height = desc.height;
    platformDesc.title = desc.title;
    platformDesc.resizable = desc.resizable;
    platformDesc.vsync = desc.vsync;
    if (!backend_->CreateWindow(platformDesc)) {
        backend_->Shutdown();
        return false;
    }

    frameStart_ = std::chrono::steady_clock::now();
    mousePosition_ = Vec2{static_cast<float>(width_) * 0.5f, static_cast<float>(height_) * 0.5f};
    mousePrevPosition_ = mousePosition_;
    return true;
}

void Window::Destroy() {
    if (!backend_) return;
    backend_->DestroyWindow();
    backend_->Shutdown();
    backend_ = nullptr;
}

void Window::SetTitle(const char* title) {
    if (backend_ && title) backend_->SetTitle(title);
}

void Window::SetClientSize(int width, int height) {
    if (!backend_ || width <= 0 || height <= 0) return;
    backend_->SetSize(width, height);
}

void Window::SetFullscreen(bool fullscreen) {
    if (backend_) backend_->SetFullscreen(fullscreen);
}

void Window::Minimize() { if (backend_) backend_->Minimize(); }
void Window::Maximize() { if (backend_) backend_->Maximize(); }
void Window::Restore() { if (backend_) backend_->Restore(); }
bool Window::Maximized() const { return backend_ && backend_->Maximized(); }
void Window::Show() { if (backend_) backend_->Show(); }
void Window::Hide() { if (backend_) backend_->Hide(); }

void Window::SetResizable(bool resizable) {
    if (!backend_) return;
    resizable_ = resizable;
    backend_->SetResizable(resizable);
}

Vec2 Window::WindowPosition() const { return backend_ ? backend_->Position() : Vec2{}; }
void Window::SetWindowPosition(Vec2 position) { if (backend_) backend_->SetPosition(position); }

int Window::CurrentMonitor() const { return backend_ ? backend_->CurrentDisplay() : 0; }

int Window::MonitorCount() const { return backend_ ? backend_->DisplayCount() : 0; }
Vec2 Window::MonitorPosition(int index) const {
    return backend_ ? backend_->Display(index).position : Vec2{};
}
Vec2 Window::MonitorSize(int index) const { return backend_ ? backend_->Display(index).size : Vec2{}; }
float Window::MonitorRefreshRate(int index) const {
    return backend_ ? backend_->Display(index).refreshRate : 0.0f;
}

const char* Window::MonitorName(int index) const {
    if (!backend_) return "";
    // The backend returns the display by value, so the name has to be copied into
    // storage that outlives the call. A function-local static keeps the returned
    // pointer valid without the caller owning anything.
    static std::string name;
    name = backend_->Display(index).name;
    return name.c_str();
}

float Window::DpiScale() const { return backend_ ? backend_->DpiScale() : 1.0f; }
void Window::SetOpacity(float opacity) { if (backend_) backend_->SetOpacity(opacity); }
bool Window::SetIconFromFile(const char* path) {
    return backend_ && backend_->SetIconFromFile(path);
}

void Window::SetClipboardText(const char* text) { if (backend_) backend_->SetClipboardText(text); }
const char* Window::GetClipboardText() { return backend_ ? backend_->ClipboardText() : ""; }

void Window::SetMinSize(int width, int height) {
    minWidth_ = width < 0 ? 0 : width;
    minHeight_ = height < 0 ? 0 : height;
    if (backend_) backend_->SetMinSize(width, height);
}

void Window::CenterOnMonitor(int monitor) { if (backend_) backend_->CenterOnDisplay(monitor); }

void Window::SetAlwaysOnTop(bool onTop) {
    alwaysOnTop_ = onTop;
    if (backend_) backend_->SetAlwaysOnTop(onTop);
}

void Window::SetMouseCaptured(bool captured) {
    if (backend_) backend_->SetMouseCaptured(captured);
}

void Window::SetMouseVisible(bool visible) {
    mouseVisible_ = visible;
    if (backend_) backend_->SetCursorVisible(visible);
}

void Window::SetMouseCursor(MouseCursor cursor) {
    mouseCursor_ = cursor;
    if (backend_) backend_->SetCursorShape(cursor);
}

bool Window::SetCursorFromFile(const char* path) {
    return backend_ && backend_->SetCursorFromFile(path);
}

void Window::SetRawMouse(bool enabled) { if (backend_) backend_->SetRawMouse(enabled); }

void Window::SetCursorClipped(bool clipped) {
    cursorClipped_ = clipped;
    if (backend_) backend_->SetCursorClipped(clipped);
}

void Window::SetMouseRect(Vec2 topLeft, Vec2 size) {
    mouseRectOrigin_ = topLeft;
    mouseRectSize_ = size;
    if (backend_) backend_->SetMouseRect(topLeft, size);
}

void Window::SetMousePosition(Vec2 position) {
    if (!backend_ || backend_->MouseCaptured()) return;
    backend_->SetCursorPosition(position);
}

void Window::HideCursor(bool hide) { SetMouseVisible(!hide); }

bool Window::PumpMessages() {
    // Snapshot the previous frame's state *before* pumping, so that the
    // IsKeyPressed()/IsMouseButtonPressed() queries during this frame compare the
    // incoming state against the state at the end of the last frame.
    keyPrev_ = keyDown_;
    mousePrev_ = mouseDown_;
    keyRepeat_.fill(false);
    mouseWheel_ = 0.0f;
    mouseWheelVector_ = Vec2{};
    charCount_ = 0;
    // Movement is accumulated by the events below; start from zero so the delta
    // reported this frame is this frame's movement.
    mouseDelta_ = Vec2{};

    if (backend_) {
        if (!backend_->PumpEvents()) shouldClose_ = true;

        PlatformEvent event;
        while (backend_->PollEvent(event)) ApplyEvent(event);
    }

    // The exit key closes the window at the *end* of the frame, after the game has
    // had its chance to react (a paused game may consume Escape first). Games that
    // want a different key call SetExitKey(); Unknown disables the behaviour.
    if (exitKey_ != Key::Unknown && KeyPressed(exitKey_)) RequestClose();

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - frameStart_).count();
    frameStart_ = now;

    // Clamp to avoid a huge step after the window was dragged or stalled.
    const double clamped = elapsed > 0.25 ? 0.25 : elapsed;
    deltaTime_ = static_cast<float>(clamped);
    time_ += clamped;
    ++frameCount_;

    fpsTimer_ += clamped;
    ++fpsFrames_;
    if (fpsTimer_ >= 0.5) {
        fps_ = static_cast<float>(fpsFrames_ / fpsTimer_);
        fpsTimer_ = 0.0;
        fpsFrames_ = 0;
    }

    return !shouldClose_;
}

void Window::ApplyEvent(const PlatformEvent& event) {
    switch (event.type) {
        case PlatformEventType::CloseRequested:
            shouldClose_ = true;
            break;

        case PlatformEventType::Resized:
            if (event.width > 0 && event.height > 0) {
                if (event.width != width_ || event.height != height_) resized_ = true;
                width_ = event.width;
                height_ = event.height;
            }
            break;

        case PlatformEventType::Minimized:
            minimized_ = true;
            break;
        case PlatformEventType::Maximized:
            minimized_ = false;
            maximizedChanged_ = true;
            break;
        case PlatformEventType::Restored:
            minimized_ = false;
            maximizedChanged_ = true;
            break;

        case PlatformEventType::KeyDown: {
            const int index = static_cast<int>(event.key);
            if (index >= 0 && index < kKeyCount) {
                if (event.repeat) keyRepeat_[index] = true;
                keyDown_[index] = true;
            }
            break;
        }
        case PlatformEventType::KeyUp: {
            const int index = static_cast<int>(event.key);
            if (index >= 0 && index < kKeyCount) keyDown_[index] = false;
            break;
        }

        case PlatformEventType::TextInput:
            if (charCount_ < kMaxCharQueue) charQueue_[charCount_++] = event.codepoint;
            break;

        case PlatformEventType::MouseMoved:
            // The event carries the movement since the previous event, so summing
            // keeps every sample of a fast movement instead of only the last.
            mouseDelta_ += event.delta;
            mousePosition_ = event.position;
            mousePrevPosition_ = mousePosition_;
            break;

        case PlatformEventType::MouseButtonDown: {
            const int index = static_cast<int>(event.button);
            if (index >= 0 && index < static_cast<int>(MouseButton::Count)) mouseDown_[index] = true;
            break;
        }
        case PlatformEventType::MouseButtonUp: {
            const int index = static_cast<int>(event.button);
            if (index >= 0 && index < static_cast<int>(MouseButton::Count)) mouseDown_[index] = false;
            break;
        }

        case PlatformEventType::MouseWheel:
            mouseWheel_ += event.wheelY;
            mouseWheelVector_.x += event.wheelX;
            mouseWheelVector_.y += event.wheelY;
            break;

        default:
            // Focus, hover and device notifications do not change the input arrays;
            // the backend already handles the side effects (dropping capture on
            // focus loss, for instance).
            break;
    }
}

void Window::FramePacing() {
    // Present() already blocks for the vertical blank when vsync is on, so there
    // is nothing to pace here.
}

bool Window::KeyDown(Key key) const {
    const int index = static_cast<int>(key);
    return index >= 0 && index < kKeyCount && keyDown_[index];
}

bool Window::KeyPressed(Key key) const {
    const int index = static_cast<int>(key);
    return index >= 0 && index < kKeyCount && keyDown_[index] && !keyPrev_[index];
}

bool Window::KeyReleased(Key key) const {
    const int index = static_cast<int>(key);
    return index >= 0 && index < kKeyCount && !keyDown_[index] && keyPrev_[index];
}

bool Window::KeyRepeated(Key key) const {
    const int index = static_cast<int>(key);
    return index >= 0 && index < kKeyCount && keyRepeat_[index];
}

bool Window::MouseDown(MouseButton button) const {
    const int index = static_cast<int>(button);
    return index >= 0 && index < static_cast<int>(MouseButton::Count) && mouseDown_[index];
}

bool Window::MousePressed(MouseButton button) const {
    const int index = static_cast<int>(button);
    return index >= 0 && index < static_cast<int>(MouseButton::Count) &&
           mouseDown_[index] && !mousePrev_[index];
}

bool Window::MouseReleased(MouseButton button) const {
    const int index = static_cast<int>(button);
    return index >= 0 && index < static_cast<int>(MouseButton::Count) &&
           !mouseDown_[index] && mousePrev_[index];
}

int Window::NextChar() {
    if (charCount_ == 0) return 0;
    const int code = charQueue_[0];
    for (size_t i = 1; i < charCount_; ++i) charQueue_[i - 1] = charQueue_[i];
    --charCount_;
    return code;
}

} // namespace n3d::internal
