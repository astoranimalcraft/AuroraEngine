// NexiumEngine — Win32 platform backend.
//
// The only translation unit that talks to the Win32 API for windowing and input.
// It implements PlatformBackend and translates native messages into
// PlatformEvent values, which the window layer drains. Keeping the HWND and the
// message loop inside this file is what lets the rest of the engine be written
// against an OS-neutral interface.
#include "internal/platform.h"

#include "nexium/log.h"

#include "internal/common.h"
#include "internal/icon_data.h"

#include <windowsx.h>
#include <dbt.h>


#include <vector>

namespace n3d::internal {
namespace {

constexpr wchar_t kWindowClassName[] = L"NexiumEngineWindowClass";

// Generic-desktop usage page and its mouse/keyboard usages, from hidusage.h.
// Spelled out rather than including that header, which MinGW ships under a name
// that varies by version.
constexpr USHORT kUsagePageGenericDesktop = 0x01;
constexpr USHORT kUsageMouse = 0x02;

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

HCURSOR SystemCursor(MouseCursor cursor) {
    LPCWSTR id = IDC_ARROW;
    switch (cursor) {
        case MouseCursor::IBeam: id = IDC_IBEAM; break;
        case MouseCursor::Crosshair: id = IDC_CROSS; break;
        case MouseCursor::PointingHand: id = IDC_HAND; break;
        case MouseCursor::ResizeEW: id = IDC_SIZEWE; break;
        case MouseCursor::ResizeNS: id = IDC_SIZENS; break;
        case MouseCursor::ResizeNWSE: id = IDC_SIZENWSE; break;
        case MouseCursor::ResizeNESW: id = IDC_SIZENESW; break;
        case MouseCursor::ResizeAll: id = IDC_SIZEALL; break;
        case MouseCursor::NotAllowed: id = IDC_NO; break;
        case MouseCursor::Default:
        case MouseCursor::Arrow:
        default: break;
    }
    return LoadCursorW(nullptr, id);
}

// Builds an HICON from the embedded logo. Windows wants an icon as a bitmap pair:
// a 32-bit BGRA colour bitmap and a 1-bit AND mask. The mask must be all zeros
// (meaning "use the colour bitmap's alpha everywhere") because a non-zero mask
// bit forces that pixel transparent and would punch holes in the logo. `smallSize`
// asks for the 16x16 size Windows uses in the title bar; the class registers both
// so the shell can pick per context.
//
// The parameter is not named `small`: that is a legacy Windows macro
// (`#define small char`, from rpcndr.h / the MSVC SDK headers) which would expand
// in the declaration and turn `bool small` into `bool char`.
HICON BuildDefaultIcon(bool smallSize) {
    const int size = smallSize ? 16 : 32;

    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    // A negative height requests a top-down DIB, which is the row order the logo
    // is stored in; with a positive height the rows would come out mirrored.
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    HDC screen = GetDC(nullptr);
    void* dibBits = nullptr;
    HBITMAP colorBitmap =
        CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS,
                         &dibBits, nullptr, 0);
    HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, nullptr);
    ReleaseDC(nullptr, screen);

    if (!colorBitmap || !maskBitmap || !dibBits) {
        if (colorBitmap) DeleteObject(colorBitmap);
        if (maskBitmap) DeleteObject(maskBitmap);
        return nullptr;
    }

    auto* destination = static_cast<uint32_t*>(dibBits);
    for (int y = 0; y < size; ++y) {
        const int sourceY = y * internal::kIconHeight / size;
        for (int x = 0; x < size; ++x) {
            const int sourceX = x * internal::kIconWidth / size;
            const size_t index = (static_cast<size_t>(sourceY) * internal::kIconWidth + sourceX) * 4u;
            const uint8_t r = internal::kIconPixels[index + 0];
            const uint8_t g = internal::kIconPixels[index + 1];
            const uint8_t b = internal::kIconPixels[index + 2];
            const uint8_t a = internal::kIconPixels[index + 3];
            destination[y * size + x] =
                (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
                (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = colorBitmap;
    info.hbmMask = maskBitmap;
    HICON icon = CreateIconIndirect(&info);

    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return icon;
}

std::wstring Widen(const char* utf8) {
    if (!utf8) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);
    return wide;
}

std::string Narrow(const wchar_t* wide) {
    if (!wide) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string utf8(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), length, nullptr, nullptr);
    return utf8;
}

HMONITOR WindowMonitor(HWND hwnd) {
    return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
}

// Fills `info` for the display at `index` (-1 = the window's own). Returns false
// when the index is out of range.
bool DisplayInfoAt(HWND hwnd, int index, MONITORINFOEXW& info) {
    info = MONITORINFOEXW{};
    info.cbSize = sizeof(info);
    if (index < 0) return GetMonitorInfoW(WindowMonitor(hwnd), &info) != FALSE;

    struct Search {
        int wanted;
        int seen;
        MONITORINFOEXW* found;
    } search{index, 0, &info};

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* state = reinterpret_cast<Search*>(data);
            if (state->seen++ != state->wanted) return TRUE;
            state->found->cbSize = sizeof(MONITORINFOEXW);
            GetMonitorInfoW(monitor, state->found);
            return FALSE;   // stop enumerating
        },
        reinterpret_cast<LPARAM>(&search));

    return search.seen > search.wanted;
}

} // namespace

// ============================================================================
// Win32Backend
// ============================================================================
class Win32Backend final : public PlatformBackend {
public:
    const char* Name() const override { return "win32"; }

    PlatformCaps Caps() const override {
        PlatformCaps caps;
        caps.fullscreen = true;
        caps.customCursors = true;
        caps.clipboard = true;
        caps.opacity = true;
        caps.alwaysOnTop = true;
        caps.monitorQuery = true;
        caps.dpiAware = true;
        caps.textInput = true;
        caps.gamepad = true;
        caps.rawMouse = true;
        caps.touch = false;
        caps.orientation = false;
        caps.vibration = true;
        return caps;
    }

    bool Init() override {
        // Opt into per-monitor DPI v2 so the client area is reported in physical
        // pixels and the window is not bitmap-stretched by the system. Resolved
        // dynamically because the entry point is Windows 10 1703+ and the library
        // still runs on older systems; failure is not fatal, it just means the
        // window is DPI-scaled by the OS.
        using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        static auto setContext = reinterpret_cast<SetContextFn>(
            reinterpret_cast<void (*)()>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                        "SetProcessDpiAwarenessContext")));
        if (setContext) {
            setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }

        HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = &Win32Backend::WndProcThunk;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;   // the engine clears the window itself
        wc.lpszClassName = kWindowClassName;
        wc.hIcon = BuildDefaultIcon(false);
        wc.hIconSm = BuildDefaultIcon(true);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            LogError("RegisterClassEx failed (%lu)", GetLastError());
            return false;
        }
        return true;
    }

    void Shutdown() override {
        DestroyWindow();
        // Custom cursors are ours to destroy; the system ones are not.
        for (HCURSOR cursor : ownedCursors_) DestroyCursor(cursor);
        ownedCursors_.clear();
        customCursor_ = nullptr;
        UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    }

    bool CreateWindow(const PlatformWindowDesc& desc) override {
        width_ = desc.width;
        height_ = desc.height;
        resizable_ = desc.resizable;

        const DWORD style = WS_OVERLAPPEDWINDOW & ~(resizable_ ? 0u : (WS_THICKFRAME | WS_MAXIMIZEBOX));
        RECT rect{0, 0, width_, height_};
        AdjustWindowRect(&rect, style, FALSE);

        hwnd_ = CreateWindowExW(0, kWindowClassName, Widen(desc.title.c_str()).c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top,
                                nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd_) {
            LogError("CreateWindowEx failed (%lu)", GetLastError());
            return false;
        }

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        visible_ = true;
        return true;
    }

    void DestroyWindow() override {
        // Hand the system cursor back before the window goes away: a hidden or
        // confined cursor would otherwise stay that way on the desktop.
        if (mouseCaptured_) SetMouseCaptured(false);
        if (!cursorVisible_) SetCursorVisible(true);
        SetCursorClipped(false);

        if (hwnd_) {
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            ::DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void* NativeHandle() const override { return hwnd_; }

    // --------------------------------------------------------------- events --
    bool PumpEvents() override {
        events_.clear();
        eventCursor_ = 0;
        // Movement accumulated by WM_INPUT / WM_MOUSEMOVE during this pump. Raw
        // input and the pointer path are kept apart so a backend that gets both
        // does not double-count.
        pointerDelta_ = Vec2{};
        rawDelta_ = Vec2{};

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit_ = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return !quit_;
    }

    bool PollEvent(PlatformEvent& out) override {
        if (eventCursor_ >= events_.size()) return false;
        out = events_[eventCursor_++];
        return true;
    }

    // -------------------------------------------------------------- window --
    void SetTitle(const char* title) override {
        if (hwnd_ && title) SetWindowTextW(hwnd_, Widen(title).c_str());
    }

    void SetSize(int width, int height) override {
        if (!hwnd_ || width <= 0 || height <= 0) return;
        RECT rect{0, 0, width, height};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);
        SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void SetFullscreen(bool fullscreen) override {
        if (!hwnd_ || fullscreen == fullscreen_) return;

        if (fullscreen) {
            // Remember where we were so leaving fullscreen puts the window back
            // exactly, instead of guessing a position.
            savedPlacement_.length = sizeof(savedPlacement_);
            GetWindowPlacement(hwnd_, &savedPlacement_);
            savedStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);

            // Borderless fullscreen at the monitor's full resolution: the modern
            // approach that avoids a display mode switch and its black flicker.
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            GetMonitorInfoW(WindowMonitor(hwnd_), &monitorInfo);
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

    bool Fullscreen() const override { return fullscreen_; }
    void Minimize() override { if (hwnd_) ShowWindow(hwnd_, SW_MINIMIZE); }
    void Maximize() override { if (hwnd_) ShowWindow(hwnd_, SW_MAXIMIZE); }
    void Restore() override { if (hwnd_) ShowWindow(hwnd_, SW_RESTORE); }
    bool Maximized() const override { return hwnd_ && IsZoomed(hwnd_) != FALSE; }
    bool Focused() const override { return focused_; }
    bool Visible() const override { return visible_; }
    void Show() override { if (hwnd_) { ShowWindow(hwnd_, SW_SHOW); visible_ = true; } }
    void Hide() override { if (hwnd_) { ShowWindow(hwnd_, SW_HIDE); visible_ = false; } }

    void SetResizable(bool resizable) override {
        if (!hwnd_ || resizable == resizable_) return;
        resizable_ = resizable;
        LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        if (resizable) style |= (WS_THICKFRAME | WS_MAXIMIZEBOX);
        else style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    Vec2 Position() const override {
        if (!hwnd_) return Vec2{};
        RECT rect{};
        if (!GetWindowRect(hwnd_, &rect)) return Vec2{};
        return Vec2{static_cast<float>(rect.left), static_cast<float>(rect.top)};
    }

    void SetPosition(Vec2 position) override {
        if (!hwnd_ || fullscreen_) return;
        SetWindowPos(hwnd_, nullptr, static_cast<int>(position.x), static_cast<int>(position.y), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void SetMinSize(int width, int height) override {
        minWidth_ = width < 0 ? 0 : width;
        minHeight_ = height < 0 ? 0 : height;
    }

    void SetAlwaysOnTop(bool onTop) override {
        if (!hwnd_ || onTop == alwaysOnTop_) return;
        alwaysOnTop_ = onTop;
        SetWindowPos(hwnd_, onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void SetOpacity(float opacity) override {
        if (!hwnd_) return;
        const float clamped = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);

        LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        if (clamped >= 1.0f) {
            SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
            return;
        }
        // SetLayeredWindowAttributes only works on a layered window, so the style
        // is added on first use and removed again when opacity returns to 1.
        if (!(exStyle & WS_EX_LAYERED)) {
            SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        }
        SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(clamped * 255.0f + 0.5f), LWA_ALPHA);
    }

    bool SetIconFromFile(const char* path) override {
        if (!hwnd_ || !path) return false;
        HANDLE icon = LoadImageW(nullptr, Widen(path).c_str(), IMAGE_ICON, 0, 0,
                                 LR_LOADFROMFILE | LR_DEFAULTSIZE);
        if (!icon) {
            LogError("SetIconFromFile: could not load '%s'", path);
            return false;
        }
        // The window takes ownership once WM_SETICON succeeds; both sizes are set
        // so the taskbar and the title bar agree.
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        return true;
    }

    void CenterOnDisplay(int index) override {
        if (!hwnd_) return;
        RECT window{};
        GetWindowRect(hwnd_, &window);
        const int windowWidth = window.right - window.left;
        const int windowHeight = window.bottom - window.top;

        const DisplayInfo display = Display(index);
        const int x = static_cast<int>(display.position.x + (display.size.x - windowWidth) * 0.5f);
        const int y = static_cast<int>(display.position.y + (display.size.y - windowHeight) * 0.5f);
        SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // ------------------------------------------------------------ displays --
    int DisplayCount() const override {
        int count = 0;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR, HDC, LPRECT, LPARAM data) -> BOOL {
                ++*reinterpret_cast<int*>(data);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&count));
        return count;
    }

    DisplayInfo Display(int index) const override {
        DisplayInfo result;
        MONITORINFOEXW info{};
        if (!DisplayInfoAt(hwnd_, index, info)) return result;

        result.position = Vec2{static_cast<float>(info.rcMonitor.left),
                               static_cast<float>(info.rcMonitor.top)};
        result.size = Vec2{static_cast<float>(info.rcMonitor.right - info.rcMonitor.left),
                           static_cast<float>(info.rcMonitor.bottom - info.rcMonitor.top)};
        result.name = Narrow(info.szDevice);

        // Refresh rate is a property of the display mode, not the monitor rect,
        // so it has to come from the display settings of the device.
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
            result.refreshRate = static_cast<float>(mode.dmDisplayFrequency);
        }
        result.dpiScale = DpiScale();
        return result;
    }

    int CurrentDisplay() const override {
        // Walk the monitor list until the window's own monitor comes up. The
        // callback gets a plain struct rather than a std::pair so the intent is
        // readable and no temporary has to be taken by address.
        struct Search {
            HMONITOR target;
            int index = 0;
            int found = -1;
        } search{WindowMonitor(hwnd_), 0, -1};

        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
                auto* state = reinterpret_cast<Search*>(data);
                if (monitor == state->target) {
                    state->found = state->index;
                    return FALSE;
                }
                ++state->index;
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));

        return search.found >= 0 ? search.found : 0;
    }

    float DpiScale() const override {
        if (!hwnd_) return 1.0f;
        // GetDpiForWindow is Windows 10 1607+, so it is resolved dynamically to
        // keep the library usable on older systems.
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        static auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
            reinterpret_cast<void (*)()>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                        "GetDpiForWindow")));

        UINT dpi = 96;
        if (getDpiForWindow) {
            dpi = getDpiForWindow(hwnd_);
        } else {
            HDC screen = GetDC(nullptr);
            if (screen) {
                dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
                ReleaseDC(nullptr, screen);
            }
        }
        return dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
    }

    // ----------------------------------------------------------- clipboard --
    void SetClipboardText(const char* text) override {
        if (!hwnd_ || !text) return;
        const std::wstring wide = Widen(text);
        if (wide.empty()) return;

        if (!OpenClipboard(hwnd_)) return;
        EmptyClipboard();

        // The clipboard owns the block after SetClipboardData, so it must come
        // from GlobalAlloc rather than the C++ allocator.
        const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
        HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (block) {
            void* destination = GlobalLock(block);
            if (destination) {
                memcpy(destination, wide.c_str(), bytes);
                GlobalUnlock(block);
                SetClipboardData(CF_UNICODETEXT, block);
            } else {
                GlobalFree(block);
            }
        }
        CloseClipboard();
    }

    const char* ClipboardText() override {
        clipboardBuffer_.clear();
        if (!hwnd_ || !IsClipboardFormatAvailable(CF_UNICODETEXT)) return clipboardBuffer_.c_str();
        if (!OpenClipboard(hwnd_)) return clipboardBuffer_.c_str();

        if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(handle))) {
                clipboardBuffer_ = Narrow(wide);
                GlobalUnlock(handle);
            }
        }
        CloseClipboard();
        return clipboardBuffer_.c_str();
    }

    // -------------------------------------------------------------- cursor --
    void SetCursorVisible(bool visible) override {
        cursorVisible_ = visible;
        // ::ShowCursor is the Win32 one; n3d::ShowCursor has the same name.
        if (!visible) {
            // Hide until the cursor is moved, otherwise it reappears on
            // WM_SETCURSOR.
            while (::ShowCursor(FALSE) >= 0) {}
        } else {
            while (::ShowCursor(TRUE) < 0) {}
            if (hwnd_) SetCursor(CurrentCursorHandle());
        }
    }

    void SetCursorShape(MouseCursor cursor) override {
        if (cursor == cursorShape_) return;
        cursorShape_ = cursor;
        // Choosing a system shape drops any custom cursor loaded from a file.
        customCursor_ = nullptr;
        if (cursorVisible_ && hwnd_) SetCursor(CurrentCursorHandle());
    }

    bool SetCursorFromFile(const char* path) override {
        if (!hwnd_ || !path) return false;
        // .cur and .ani both load through LoadImageW with IMAGE_CURSOR.
        HCURSOR cursor = static_cast<HCURSOR>(
            LoadImageW(nullptr, Widen(path).c_str(), IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE));
        if (!cursor) {
            LogError("SetCursorFromFile: could not load '%s'", path);
            return false;
        }
        ownedCursors_.push_back(cursor);
        customCursor_ = cursor;
        if (cursorVisible_) SetCursor(cursor);
        return true;
    }

    void SetCursorClipped(bool clipped) override {
        cursorClipped_ = clipped;
        ApplyClip();
    }

    void SetCursorPosition(Vec2 position) override {
        if (!hwnd_ || mouseCaptured_) return;
        POINT screen{static_cast<LONG>(position.x), static_cast<LONG>(position.y)};
        ClientToScreen(hwnd_, &screen);
        SetCursorPos(screen.x, screen.y);
    }

    void SetMouseCaptured(bool captured) override {
        if (captured == mouseCaptured_) return;
        mouseCaptured_ = captured;
        SetCursorVisible(!captured);
        if (captured) {
            RECT client{};
            GetClientRect(hwnd_, &client);
            POINT center{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
            ClientToScreen(hwnd_, &center);
            SetCursorPos(center.x, center.y);
            SetCursorClipped(true);
        } else {
            SetCursorClipped(false);
        }
    }

    bool MouseCaptured() const override { return mouseCaptured_; }
    bool MouseInWindow() const override { return mouseInWindow_; }

    void SetRawMouse(bool enabled) override {
        if (!hwnd_ || enabled == rawMouse_) return;

        RAWINPUTDEVICE device{};
        device.usUsagePage = kUsagePageGenericDesktop;
        device.usUsage = kUsageMouse;
        device.dwFlags = enabled ? RIDEV_INPUTSINK : RIDEV_REMOVE;
        device.hwndTarget = enabled ? hwnd_ : nullptr;

        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            LogError("SetRawMouse: RegisterRawInputDevices failed (%lu)", GetLastError());
            return;
        }
        rawMouse_ = enabled;
    }

    bool RawMouse() const override { return rawMouse_; }

    void SetMouseRect(Vec2 topLeft, Vec2 size) override {
        mouseRectOrigin_ = topLeft;
        mouseRectSize_ = size;
    }

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        Win32Backend* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<Win32Backend*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Win32Backend*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (self) return self->WndProc(hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void Push(const PlatformEvent& event) { events_.push_back(event); }

    // X1/X2 are only tracked so the capture release below knows whether a button
    // is still held; they index the same array as the first three.
    void SetButtonDown(MouseButton button, bool down) {
        const int index = static_cast<int>(button);
        if (index >= 0 && index < static_cast<int>(MouseButton::Count)) buttonDown_[index] = down;
    }

    HCURSOR CurrentCursorHandle() const {
        if (customCursor_) return customCursor_;
        return SystemCursor(cursorShape_);
    }

    void ApplyClip() {
        if (cursorClipped_ && focused_) {
            // The rectangle is in screen coordinates and only meaningful while the
            // window is foreground; re-applied on WM_SETFOCUS for that reason.
            RECT client{};
            GetClientRect(hwnd_, &client);
            POINT topLeft{client.left, client.top};
            POINT bottomRight{client.right, client.bottom};
            ClientToScreen(hwnd_, &topLeft);
            ClientToScreen(hwnd_, &bottomRight);
            RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
            ClipCursor(&screen);
        } else {
            ClipCursor(nullptr);
        }
    }

    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_CLOSE:
                Push(PlatformEvent{PlatformEventType::CloseRequested});
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;

            case WM_SIZE: {
                PlatformEvent event;
                if (wParam == SIZE_MINIMIZED) {
                    minimized_ = true;
                    event.type = PlatformEventType::Minimized;
                } else {
                    if (wParam == SIZE_MAXIMIZED) {
                        maximized_ = true;
                        event.type = PlatformEventType::Maximized;
                    } else if (maximized_) {
                        maximized_ = false;
                        event.type = PlatformEventType::Restored;
                    } else {
                        event.type = PlatformEventType::Resized;
                    }
                    minimized_ = false;
                    width_ = LOWORD(lParam);
                    height_ = HIWORD(lParam);
                    event.width = width_;
                    event.height = height_;
                    if (event.type == PlatformEventType::Maximized ||
                        event.type == PlatformEventType::Restored) {
                        // A maximize also changes the client size; report it so the
                        // swap chain is resized either way.
                        PlatformEvent resized;
                        resized.type = PlatformEventType::Resized;
                        resized.width = width_;
                        resized.height = height_;
                        Push(resized);
                    }
                }
                Push(event);
                return 0;
            }

            case WM_MOVE: {
                PlatformEvent event;
                event.type = PlatformEventType::Moved;
                event.position = Vec2{static_cast<float>(GET_X_LPARAM(lParam)),
                                      static_cast<float>(GET_Y_LPARAM(lParam))};
                Push(event);
                return 0;
            }

            case WM_PAINT: {
                PlatformEvent event;
                event.type = PlatformEventType::Exposed;
                Push(event);
                ValidateRect(hwnd, nullptr);
                return 0;
            }

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                const Key key = TranslateKey(wParam);
                if (key != Key::Unknown) {
                    PlatformEvent event;
                    event.type = PlatformEventType::KeyDown;
                    event.key = key;
                    // Bit 30 of lParam is set when the key was already down, i.e.
                    // this is an auto-repeat rather than the initial press.
                    event.repeat = (lParam & (1 << 30)) != 0;
                    Push(event);
                }
                // System keys (Alt combos) are reported above so input queries see
                // them, but they must still reach DefWindowProc: Alt+F4 closes the
                // window and Alt+Space opens the system menu.
                if (msg == WM_SYSKEYDOWN) break;
                return 0;
            }

            case WM_KEYUP:
            case WM_SYSKEYUP: {
                const Key key = TranslateKey(wParam);
                if (key != Key::Unknown) {
                    PlatformEvent event;
                    event.type = PlatformEventType::KeyUp;
                    event.key = key;
                    Push(event);
                }
                if (msg == WM_SYSKEYUP) break;
                return 0;
            }

            case WM_CHAR:
            case WM_UNICHAR: {
                if (msg == WM_UNICHAR && wParam == UNICODE_NOCHAR) return TRUE;
                const int code = static_cast<int>(wParam);
                // Filter control codes except backspace/tab/enter, which games use
                // for text entry.
                const bool printable = code >= 32 && code != 127;
                const bool control = code == '\b' || code == '\t' || code == '\r';
                if (printable || control) {
                    PlatformEvent event;
                    event.type = PlatformEventType::TextInput;
                    event.codepoint = code;
                    Push(event);
                }
                return 0;
            }

            case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
            case WM_XBUTTONDOWN: {
                MouseButton button = MouseButton::Left;
                if (msg == WM_RBUTTONDOWN) button = MouseButton::Right;
                else if (msg == WM_MBUTTONDOWN) button = MouseButton::Middle;
                else if (msg == WM_XBUTTONDOWN)
                    button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? MouseButton::X1 : MouseButton::X2;

                PlatformEvent event;
                event.type = PlatformEventType::MouseButtonDown;
                event.button = button;
                event.position = Vec2{static_cast<float>(GET_X_LPARAM(lParam)),
                                      static_cast<float>(GET_Y_LPARAM(lParam))};
                Push(event);
                SetButtonDown(button, true);
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

                PlatformEvent event;
                event.type = PlatformEventType::MouseButtonUp;
                event.button = button;
                event.position = Vec2{static_cast<float>(GET_X_LPARAM(lParam)),
                                      static_cast<float>(GET_Y_LPARAM(lParam))};
                Push(event);
                SetButtonDown(button, false);

                // Only hand back the capture that a button-down installed.
                // Releasing unconditionally would also drop a capture the game
                // asked for with SetMouseCaptured(true).
                if (!mouseCaptured_ && !buttonDown_[0] && !buttonDown_[1] && !buttonDown_[2]) {
                    ReleaseCapture();
                }
                return 0;
            }

            case WM_MOUSEMOVE: {
                const Vec2 position{static_cast<float>(GET_X_LPARAM(lParam)),
                                    static_cast<float>(GET_Y_LPARAM(lParam))};
                HandlePointerMove(position, hwnd);
                return 0;
            }

            case WM_INPUT: {
                HandleRawInput(lParam);
                break;   // DefWindowProc must still see WM_INPUT for cleanup
            }

            case WM_MOUSELEAVE: {
                trackingMouseLeave_ = false;
                mouseInWindow_ = false;
                Push(PlatformEvent{PlatformEventType::MouseLeft});
                return 0;
            }

            case WM_MOUSEHWHEEL: {
                PlatformEvent event;
                event.type = PlatformEventType::MouseWheel;
                event.wheelX = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
                Push(event);
                return 0;
            }

            case WM_MOUSEWHEEL: {
                PlatformEvent event;
                event.type = PlatformEventType::MouseWheel;
                event.wheelY = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
                Push(event);
                return 0;
            }

            case WM_SETFOCUS: {
                focused_ = true;
                // ClipCursor only takes effect for the foreground window, so a clip
                // requested earlier has to be re-applied when focus comes back.
                ApplyClip();
                Push(PlatformEvent{PlatformEventType::FocusGained});
                return 0;
            }

            case WM_KILLFOCUS: {
                focused_ = false;
                // A captured or confined cursor must be handed back: otherwise the
                // mouse stays recentred and invisible while the window is in the
                // background, so Alt+Tab leaves the user with no usable pointer.
                if (mouseCaptured_) SetMouseCaptured(false);
                if (cursorClipped_) ClipCursor(nullptr);
                ReleaseCapture();
                Push(PlatformEvent{PlatformEventType::FocusLost});
                return 0;
            }

            case WM_CAPTURECHANGED:
                // Something else took the mouse capture (Alt+Tab, a UAC prompt,
                // the debugger). Drop capture instead of fighting the other window.
                if (mouseCaptured_) SetMouseCaptured(false);
                return 0;

            case WM_DPICHANGED: {
                // The window moved to a monitor with a different scale. Adopt the
                // size the system suggests so the client area keeps its logical
                // size, and report it as a resize.
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }

            case WM_DISPLAYCHANGE:
                Push(PlatformEvent{PlatformEventType::DisplayChanged});
                return 0;

            case WM_DEVICECHANGE:
                // DBT_DEVNODES_CHANGED: something was plugged in or removed. The
                // engine re-polls the controllers; a false positive just costs a
                // poll.
                if (wParam == DBT_DEVNODES_CHANGED) {
                    Push(PlatformEvent{PlatformEventType::GamepadChanged});
                }
                return 0;

            case WM_GETMINMAXINFO: {
                if (minWidth_ > 0 || minHeight_ > 0) {
                    auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                    // ptMinTrackSize is the size of the whole window frame, so the
                    // client minimum is grown by the frame's non-client margins.
                    RECT rect{0, 0, minWidth_, minHeight_};
                    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
                    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
                    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
                    if (minWidth_ > 0) info->ptMinTrackSize.x = rect.right - rect.left;
                    if (minHeight_ > 0) info->ptMinTrackSize.y = rect.bottom - rect.top;
                }
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;   // the GPU owns the client area

            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT) {
                    if (!cursorVisible_) {
                        SetCursor(nullptr);
                        return TRUE;
                    }
                    // Re-assert the chosen shape: Windows resets the cursor to the
                    // class cursor on every WM_SETCURSOR, so without this the shape
                    // chosen by SetCursorShape() would only survive until the next
                    // mouse move.
                    SetCursor(CurrentCursorHandle());
                    return TRUE;
                }
                break;

            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void HandlePointerMove(Vec2 position, HWND hwnd) {
        // Raw input, when enabled, is the authoritative movement source; the
        // pointer path still runs so that position and hover state stay correct,
        // but its delta is not reported.
        if (mouseCaptured_) {
            // Recentre the cursor so it can never leave the client area, and
            // derive the delta from the reported movement instead. The delta is
            // *accumulated*: several WM_MOUSEMOVE can arrive in one frame, and
            // assigning would keep only the last, dropping most of a fast movement.
            RECT client{};
            GetClientRect(hwnd, &client);
            POINT center{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
            POINT raw{static_cast<LONG>(position.x), static_cast<LONG>(position.y)};
            pointerDelta_.x += static_cast<float>(raw.x - center.x);
            pointerDelta_.y += static_cast<float>(raw.y - center.y);
            if (raw.x != center.x || raw.y != center.y) {
                ClientToScreen(hwnd, &center);
                SetCursorPos(center.x, center.y);
            }
            mousePosition_ = position;
            return;
        }

        Vec2 clamped = position;
        if (mouseRectSize_.x > 0.0f && mouseRectSize_.y > 0.0f) {
            // Confine to the sub-rectangle: letterboxed rendering and split-screen
            // want the pointer kept inside the active viewport.
            clamped.x = Clamp(position.x, mouseRectOrigin_.x, mouseRectOrigin_.x + mouseRectSize_.x - 1);
            clamped.y = Clamp(position.y, mouseRectOrigin_.y, mouseRectOrigin_.y + mouseRectSize_.y - 1);
            if (clamped.x != position.x || clamped.y != position.y) {
                POINT screen{static_cast<LONG>(clamped.x), static_cast<LONG>(clamped.y)};
                ClientToScreen(hwnd, &screen);
                SetCursorPos(screen.x, screen.y);
            }
        }

        PlatformEvent event;
        event.type = PlatformEventType::MouseMoved;
        event.position = clamped;
        event.delta = clamped - mousePosition_;
        Push(event);
        mousePosition_ = clamped;

        if (!mouseInWindow_) {
            mouseInWindow_ = true;
            Push(PlatformEvent{PlatformEventType::MouseEntered});
        }
        if (!trackingMouseLeave_) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            trackingMouseLeave_ = true;
        }
    }

    void HandleRawInput(LPARAM lParam) {
        if (!rawMouse_) return;

        UINT size = 0;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size,
                            sizeof(RAWINPUTHEADER)) != 0 || size == 0) {
            return;
        }
        if (size > sizeof(rawBuffer_)) return;

        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawBuffer_, &size,
                            sizeof(RAWINPUTHEADER)) != size) {
            return;
        }

        const auto* raw = reinterpret_cast<const RAWINPUT*>(rawBuffer_);
        if (raw->header.dwType != RIM_TYPEMOUSE) return;
        // Absolute coordinates arrive from tablets and remote sessions; they are
        // already pointer positions, so mixing them into a delta would be wrong.
        if (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) return;

        rawDelta_.x += static_cast<float>(raw->data.mouse.lLastX);
        rawDelta_.y += static_cast<float>(raw->data.mouse.lLastY);
    }

public:
    // Movement accumulated since the last PumpEvents(), for the window layer.
    Vec2 TakePointerDelta() {
        return rawMouse_ ? rawDelta_ : pointerDelta_;
    }

private:
    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool resizable_ = true;
    bool minimized_ = false;
    bool maximized_ = false;
    bool quit_ = false;
    bool focused_ = true;
    bool visible_ = false;
    bool fullscreen_ = false;
    bool alwaysOnTop_ = false;
    bool mouseCaptured_ = false;
    bool cursorVisible_ = true;
    bool cursorClipped_ = false;
    bool rawMouse_ = false;
    bool mouseInWindow_ = false;
    bool trackingMouseLeave_ = false;
    int minWidth_ = 0;
    int minHeight_ = 0;

    WINDOWPLACEMENT savedPlacement_{};
    LONG_PTR savedStyle_ = 0;

    MouseCursor cursorShape_ = MouseCursor::Default;
    HCURSOR customCursor_ = nullptr;
    std::vector<HCURSOR> ownedCursors_;

    Vec2 mousePosition_{};
    Vec2 mouseRectOrigin_{};
    Vec2 mouseRectSize_{};
    Vec2 pointerDelta_{};
    Vec2 rawDelta_{};

    bool buttonDown_[static_cast<int>(MouseButton::Count)] = {};

    std::vector<PlatformEvent> events_;
    size_t eventCursor_ = 0;
    std::string clipboardBuffer_;
    alignas(RAWINPUT) unsigned char rawBuffer_[sizeof(RAWINPUT) + 64]{};
};

// ============================================================================
// Registry
// ============================================================================
namespace {
std::unique_ptr<PlatformBackend> gBackend;
}

PlatformBackend* GetPlatformBackend() {
    if (!gBackend) gBackend = CreateDefaultPlatformBackend();
    return gBackend.get();
}

void SetPlatformBackend(std::unique_ptr<PlatformBackend> backend) {
    gBackend = std::move(backend);
}

std::unique_ptr<PlatformBackend> CreateDefaultPlatformBackend() {
    return std::make_unique<Win32Backend>();
}

} // namespace n3d::internal
