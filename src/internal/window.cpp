// Aurora3D — Win32 window, message pump, input and frame timing.
#include "internal/window.h"

#include "aurora/log.h"

#include <windowsx.h>

#include <cmath>

namespace a3d::internal {
namespace {

constexpr wchar_t kWindowClassName[] = L"Aurora3DWindowClass";

// Maps Win32 virtual-key codes onto a3d::Key. Returns Key::Unknown for anything
// the engine does not model.
Key TranslateKey(WPARAM vk) {
    switch (vk) {
        case VK_SPACE: return Key::Space;
        case VK_OEM_7: return Key::Apostrophe;
        case VK_OEM_COMMA: return Key::Comma;
        case VK_OEM_MINUS: return Key::Minus;
        case VK_OEM_PERIOD: return Key::Period;
        case VK_OEM_2: return Key::Slash;
        case VK_OEM_1: return Key::Semicolon;
        case VK_OEM_PLUS: return Key::Equal;
        case VK_OEM_4: return Key::LeftBracket;
        case VK_OEM_5: return Key::Backslash;
        case VK_OEM_6: return Key::RightBracket;
        case VK_OEM_3: return Key::Grave;
        case VK_ESCAPE: return Key::Escape;
        case VK_RETURN: return Key::Enter;
        case VK_TAB: return Key::Tab;
        case VK_BACK: return Key::Backspace;
        case VK_INSERT: return Key::Insert;
        case VK_DELETE: return Key::Delete;
        case VK_RIGHT: return Key::Right;
        case VK_LEFT: return Key::Left;
        case VK_DOWN: return Key::Down;
        case VK_UP: return Key::Up;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        case VK_CAPITAL: return Key::CapsLock;
        case VK_SCROLL: return Key::ScrollLock;
        case VK_NUMLOCK: return Key::NumLock;
        case VK_SNAPSHOT: return Key::PrintScreen;
        case VK_PAUSE: return Key::Pause;
        case VK_LSHIFT: return Key::LeftShift;
        case VK_LCONTROL: return Key::LeftControl;
        case VK_LMENU: return Key::LeftAlt;
        case VK_LWIN: return Key::LeftSuper;
        case VK_RSHIFT: return Key::RightShift;
        case VK_RCONTROL: return Key::RightControl;
        case VK_RMENU: return Key::RightAlt;
        case VK_RWIN: return Key::RightSuper;
        case VK_APPS: return Key::Menu;
        case VK_DECIMAL: return Key::KeypadDecimal;
        case VK_DIVIDE: return Key::KeypadDivide;
        case VK_MULTIPLY: return Key::KeypadMultiply;
        case VK_SUBTRACT: return Key::KeypadSubtract;
        case VK_ADD: return Key::KeypadAdd;
        default: break;
    }

    if (vk >= '0' && vk <= '9') return static_cast<Key>(static_cast<int>(Key::Num0) + (vk - '0'));
    if (vk >= 'A' && vk <= 'Z') return static_cast<Key>(static_cast<int>(Key::A) + (vk - 'A'));
    if (vk >= VK_F1 && vk <= VK_F12) return static_cast<Key>(static_cast<int>(Key::F1) + (vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return static_cast<Key>(static_cast<int>(Key::Keypad0) + (vk - VK_NUMPAD0));
    return Key::Unknown;
}

} // namespace

Window& Window::Get() {
    static Window instance;
    return instance;
}

bool Window::Create(const WindowDesc& desc) {
    width_ = desc.width;
    height_ = desc.height;
    resizable_ = desc.resizable;
    vsync_ = desc.vsync;

    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = &Window::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // the engine clears the window itself
    wc.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LogError("RegisterClassEx failed (%lu)", GetLastError());
        return false;
    }

    // Size the window so the *client* area matches the requested resolution.
    const DWORD style = WS_OVERLAPPEDWINDOW & ~(resizable_ ? 0u : (WS_THICKFRAME | WS_MAXIMIZEBOX));
    RECT rect{0, 0, width_, height_};
    AdjustWindowRect(&rect, style, FALSE);

    int titleLength = MultiByteToWideChar(CP_UTF8, 0, desc.title.c_str(), -1, nullptr, 0);
    std::wstring title(static_cast<size_t>(titleLength > 0 ? titleLength : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, desc.title.c_str(), -1, title.data(), titleLength);

    hwnd_ = CreateWindowExW(0, kWindowClassName, title.c_str(), style,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            rect.right - rect.left, rect.bottom - rect.top,
                            nullptr, nullptr, instance, this);
    if (!hwnd_) {
        LogError("CreateWindowEx failed (%lu)", GetLastError());
        return false;
    }

    QueryPerformanceFrequency(&frequency_);
    QueryPerformanceCounter(&lastCounter_);
    frameStart_ = lastCounter_;

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void Window::Destroy() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
}

void Window::SetTitle(const char* title) {
    if (!hwnd_ || !title) return;

    const int length = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
    if (length <= 0) return;

    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wide.data(), length);
    SetWindowTextW(hwnd_, wide.c_str());
}

LRESULT CALLBACK Window::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) return self->WndProc(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            shouldClose_ = true;
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                minimized_ = true;
            } else {
                minimized_ = false;
                const int newWidth = LOWORD(lParam);
                const int newHeight = HIWORD(lParam);
                if (newWidth != width_ || newHeight != height_) resized_ = true;
                width_ = newWidth;
                height_ = newHeight;
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const Key key = TranslateKey(wParam);
            if (key != Key::Unknown) {
                const int index = static_cast<int>(key);
                // Bit 30 of lParam is set when the key was already down, i.e. this
                // is an auto-repeat rather than the initial press.
                if (lParam & (1 << 30)) keyRepeat_[index] = true;
                keyDown_[index] = true;
            }
            if (msg == WM_SYSKEYDOWN && (wParam == VK_F10 || wParam == VK_MENU)) break;
            return 0;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const Key key = TranslateKey(wParam);
            if (key != Key::Unknown) keyDown_[static_cast<int>(key)] = false;
            return 0;
        }

        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN: {
            MouseButton button = MouseButton::Left;
            if (msg == WM_RBUTTONDOWN) button = MouseButton::Right;
            else if (msg == WM_MBUTTONDOWN) button = MouseButton::Middle;
            else if (msg == WM_XBUTTONDOWN)
                button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? MouseButton::X1 : MouseButton::X2;
            mouseDown_[static_cast<int>(button)] = true;
            SetCapture(hwnd);
            return 0;
        }

        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
        case WM_XBUTTONUP: {
            MouseButton button = MouseButton::Left;
            if (msg == WM_RBUTTONUP) button = MouseButton::Right;
            else if (msg == WM_MBUTTONUP) button = MouseButton::Middle;
            else if (msg == WM_XBUTTONUP)
                button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? MouseButton::X1 : MouseButton::X2;
            mouseDown_[static_cast<int>(button)] = false;
            if (!mouseDown_[0] && !mouseDown_[1] && !mouseDown_[2]) ReleaseCapture();
            return 0;
        }

        case WM_MOUSEMOVE: {
            Vec2 position{static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam))};

            if (mouseCaptured_) {
                // Recentre the cursor every frame so it can never leave the client
                // area, and derive the delta from the reported movement instead.
                RECT client{};
                GetClientRect(hwnd, &client);
                POINT center{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
                POINT raw{static_cast<LONG>(position.x), static_cast<LONG>(position.y)};
                mouseDelta_ = Vec2{static_cast<float>(raw.x - center.x), static_cast<float>(raw.y - center.y)};
                if (mouseDelta_.x != 0.0f || mouseDelta_.y != 0.0f) {
                    ClientToScreen(hwnd, &center);
                    SetCursorPos(center.x, center.y);
                }
                return 0;
            }

            if (mouseLocked_) {
                RECT client{};
                GetClientRect(hwnd, &client);
                const float maxX = static_cast<float>(client.right - client.left - 1);
                const float maxY = static_cast<float>(client.bottom - client.top - 1);
                const Vec2 clamped{Clamp(position.x, 0.0f, maxX), Clamp(position.y, 0.0f, maxY)};
                if (clamped.x != position.x || clamped.y != position.y) {
                    POINT screen{static_cast<LONG>(clamped.x), static_cast<LONG>(clamped.y)};
                    ClientToScreen(hwnd, &screen);
                    SetCursorPos(screen.x, screen.y);
                }
                position = clamped;
            }

            mousePosition_ = position;
            if (!trackingMouseLeave_) {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                trackingMouseLeave_ = true;
            }
            mouseInWindow_ = true;
            return 0;
        }

        case WM_CHAR: {
            // Filter control codes except backspace/tab/enter, which games use for
            // text entry. Everything else the message pump already handled.
            const int code = static_cast<int>(wParam);
            const bool printable = code >= 32 && code < 127;
            const bool control = code == '\b' || code == '\t' || code == '\r';
            if ((printable || control) && charCount_ < kMaxCharQueue) {
                charQueue_[charCount_++] = code;
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            trackingMouseLeave_ = false;
            mouseInWindow_ = false;
            return 0;

        case WM_MOUSEWHEEL: {
            const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            mouseWheel_ += notches;
            return 0;
        }

        case WM_SETFOCUS:
            focused_ = true;
            return 0;

        case WM_KILLFOCUS:
            // Dropping focus while keys are held would otherwise leave them stuck.
            focused_ = false;
            keyDown_.fill(false);
            mouseDown_.fill(false);
            return 0;

        case WM_ERASEBKGND:
            return 1;   // the GPU owns the client area

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && !mouseVisible_) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Window::PumpMessages() {
    // Snapshot the previous frame's state *before* pumping, so that the
    // IsKeyPressed()/IsMouseButtonPressed() queries during this frame compare the
    // incoming state against the state at the end of the last frame.
    keyPrev_ = keyDown_;
    mousePrev_ = mouseDown_;
    keyRepeat_.fill(false);
    mouseWheel_ = 0.0f;
    charCount_ = 0;

    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) shouldClose_ = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Fold the frame's raw mouse movement into the per-frame delta. When the
    // cursor is captured the delta was already computed in WM_MOUSEMOVE.
    if (!mouseCaptured_) {
        mouseDelta_ = mousePosition_ - mousePrevPosition_;
        mousePrevPosition_ = mousePosition_;
    } else {
        mousePrevPosition_ = mousePosition_;
    }

    QueryPerformanceCounter(&lastCounter_);
    const double elapsed = static_cast<double>(lastCounter_.QuadPart - frameStart_.QuadPart) /
                           static_cast<double>(frequency_.QuadPart);
    frameStart_ = lastCounter_;

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

    UpdateInputEdges();
    return !shouldClose_;
}

void Window::UpdateInputEdges() {
    // Nothing left to do here: PumpMessages() snapshots the previous state before
    // dispatching, which is what the pressed/released queries compare against.
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

void Window::SetMouseCaptured(bool captured) {
    if (captured == mouseCaptured_) return;
    mouseCaptured_ = captured;
    SetMouseVisible(!captured);
    if (captured) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        POINT center{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
        ClientToScreen(hwnd_, &center);
        SetCursorPos(center.x, center.y);
    }
}

void Window::SetMouseVisible(bool visible) {
    mouseVisible_ = visible;
    if (!visible) {
        // Hide until the cursor is moved, otherwise it reappears on WM_SETCURSOR.
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        while (ShowCursor(TRUE) < 0) {}
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
}

void Window::SetMousePosition(Vec2 position) {
    if (!hwnd_ || mouseCaptured_) return;

    POINT screen{static_cast<LONG>(position.x), static_cast<LONG>(position.y)};
    ClientToScreen(hwnd_, &screen);
    SetCursorPos(screen.x, screen.y);
    mousePosition_ = position;
}

int Window::NextChar() {
    if (charCount_ == 0) return 0;
    const int code = charQueue_[0];
    for (size_t i = 1; i < charCount_; ++i) charQueue_[i - 1] = charQueue_[i];
    --charCount_;
    return code;
}

void Window::SetClientSize(int width, int height) {
    if (!hwnd_ || width <= 0 || height <= 0) return;

    RECT rect{0, 0, width, height};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::SetFullscreen(bool fullscreen) {
    if (!hwnd_ || fullscreen == fullscreen_) return;

    if (fullscreen) {
        // Remember where we were so Restore() puts the window back exactly.
        savedPlacement_.length = sizeof(savedPlacement_);
        GetWindowPlacement(hwnd_, &savedPlacement_);
        savedStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);

        // Borderless fullscreen at the monitor's full resolution: the modern
        // approach that avoids a display mode switch and its black flicker.
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitorInfo);
        const RECT& bounds = monitorInfo.rcMonitor;

        SetWindowLongPtrW(hwnd_, GWL_STYLE, savedStyle_ & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowPos(hwnd_, HWND_TOP, bounds.left, bounds.top,
                     bounds.right - bounds.left, bounds.bottom - bounds.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, savedStyle_);
        SetWindowPlacement(hwnd_, &savedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                         SWP_FRAMECHANGED);
    }
    fullscreen_ = fullscreen;
}

void Window::Minimize() { if (hwnd_) ShowWindow(hwnd_, SW_MINIMIZE); }
void Window::Maximize() { if (hwnd_) ShowWindow(hwnd_, SW_MAXIMIZE); }
void Window::Restore() { if (hwnd_) ShowWindow(hwnd_, SW_RESTORE); }

bool Window::Maximized() const {
    return hwnd_ && IsZoomed(hwnd_) != FALSE;
}

void Window::SetResizable(bool resizable) {
    if (!hwnd_ || resizable == resizable_) return;
    resizable_ = resizable;

    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    if (resizable) style |= (WS_THICKFRAME | WS_MAXIMIZEBOX);
    else style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void Window::HideCursor(bool hide) { SetMouseVisible(!hide); }

} // namespace a3d::internal
