// NexiumEngine — XInput gamepad backend.
//
// Controllers are polled once per frame into a small snapshot per slot, so the
// public queries are cheap and the pressed/released edges are computed the same
// way the keyboard's are. XInput can be absent at runtime (an old Windows without
// the redistributable), so every entry point degrades to "no controller" instead
// of failing to load.
#include "nexium/input.h"

#include "nexium/log.h"

#include "internal/common.h"

#include <xinput.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace n3d {
namespace {

// One slot's worth of state: the raw XInput snapshot plus the previous frame's
// buttons, from which the edges are derived.
struct PadState {
    bool connected = false;
    XINPUT_STATE current{};
    XINPUT_GAMEPAD previous{};
    bool previousValid = false;
    std::string name;
};

std::array<PadState, kMaxGamepads> gPads;

// Applies the dead zone XInput recommends, so a resting stick reports exactly 0
// rather than drifting.
float ApplyThumbDeadZone(short value, short deadZone) {
    const float f = static_cast<float>(value);
    if (std::fabs(f) < static_cast<float>(deadZone)) return 0.0f;
    // Rescale so the response starts from zero at the edge of the dead zone.
    const float sign = f < 0.0f ? -1.0f : 1.0f;
    const float magnitude = (std::fabs(f) - static_cast<float>(deadZone)) /
                            (32767.0f - static_cast<float>(deadZone));
    return sign * Clamp(magnitude, 0.0f, 1.0f);
}

float ApplyTriggerDeadZone(unsigned char value) {
    return value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD
               ? static_cast<float>(value) / 255.0f
               : 0.0f;
}

WORD ButtonMask(GamepadButton button) {
    switch (button) {
        case GamepadButton::DpadUp:        return XINPUT_GAMEPAD_DPAD_UP;
        case GamepadButton::DpadDown:      return XINPUT_GAMEPAD_DPAD_DOWN;
        case GamepadButton::DpadLeft:      return XINPUT_GAMEPAD_DPAD_LEFT;
        case GamepadButton::DpadRight:     return XINPUT_GAMEPAD_DPAD_RIGHT;
        case GamepadButton::Start:         return XINPUT_GAMEPAD_START;
        case GamepadButton::Back:          return XINPUT_GAMEPAD_BACK;
        case GamepadButton::LeftThumb:     return XINPUT_GAMEPAD_LEFT_THUMB;
        case GamepadButton::RightThumb:    return XINPUT_GAMEPAD_RIGHT_THUMB;
        case GamepadButton::LeftShoulder:  return XINPUT_GAMEPAD_LEFT_SHOULDER;
        case GamepadButton::RightShoulder: return XINPUT_GAMEPAD_RIGHT_SHOULDER;
        case GamepadButton::A:             return XINPUT_GAMEPAD_A;
        case GamepadButton::B:             return XINPUT_GAMEPAD_B;
        case GamepadButton::X:             return XINPUT_GAMEPAD_X;
        case GamepadButton::Y:             return XINPUT_GAMEPAD_Y;
        case GamepadButton::Count:         break;
    }
    return 0;
}

bool ValidSlot(int gamepad) { return gamepad >= 0 && gamepad < kMaxGamepads; }

bool Down(const PadState& pad, GamepadButton button) {
    if (!pad.connected) return false;
    const WORD mask = ButtonMask(button);
    return mask != 0 && (pad.current.Gamepad.wButtons & mask) != 0;
}

bool PrevDown(const PadState& pad, GamepadButton button) {
    if (!pad.previousValid) return false;
    const WORD mask = ButtonMask(button);
    return mask != 0 && (pad.previous.wButtons & mask) != 0;
}

} // namespace

void PollGamepads() {
    for (int slot = 0; slot < kMaxGamepads; ++slot) {
        PadState& pad = gPads[slot];
        XINPUT_STATE state{};
        const DWORD result = XInputGetState(static_cast<DWORD>(slot), &state);

        if (result == ERROR_SUCCESS) {
            pad.previous = pad.current.Gamepad;
            pad.previousValid = pad.connected;
            pad.current = state;
            pad.connected = true;
            if (pad.name.empty()) {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "XInput Controller %d", slot);
                pad.name = buffer;
            }
        } else {
            pad.connected = false;
            pad.previousValid = false;
            pad.previous = XINPUT_GAMEPAD{};
            pad.current = XINPUT_STATE{};
        }
    }
}

bool IsGamepadAvailable(int gamepad) {
    if (!ValidSlot(gamepad)) return false;
    return gPads[gamepad].connected;
}

const char* GetGamepadName(int gamepad) {
    if (!ValidSlot(gamepad) || !gPads[gamepad].connected) return nullptr;
    return gPads[gamepad].name.c_str();
}

bool IsGamepadButtonDown(GamepadButton button, int gamepad) {
    if (!ValidSlot(gamepad)) return false;
    return Down(gPads[gamepad], button);
}

bool IsGamepadButtonPressed(GamepadButton button, int gamepad) {
    if (!ValidSlot(gamepad)) return false;
    return Down(gPads[gamepad], button) && !PrevDown(gPads[gamepad], button);
}

bool IsGamepadButtonReleased(GamepadButton button, int gamepad) {
    if (!ValidSlot(gamepad)) return false;
    return !Down(gPads[gamepad], button) && PrevDown(gPads[gamepad], button);
}

Vec2 GetGamepadStick(int stick, int gamepad) {
    if (!ValidSlot(gamepad) || !gPads[gamepad].connected) return Vec2{};
    const XINPUT_GAMEPAD& pad = gPads[gamepad].current.Gamepad;

    const bool right = stick != 0;
    const short x = right ? pad.sThumbRX : pad.sThumbLX;
    const short y = right ? pad.sThumbRY : pad.sThumbLY;
    // XInput reports +y as up, which matches the engine's y-up convention.
    return Vec2{ApplyThumbDeadZone(x, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
                ApplyThumbDeadZone(y, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)};
}

float GetGamepadTriggerLeft(int gamepad) {
    if (!ValidSlot(gamepad) || !gPads[gamepad].connected) return 0.0f;
    return ApplyTriggerDeadZone(gPads[gamepad].current.Gamepad.bLeftTrigger);
}

float GetGamepadTriggerRight(int gamepad) {
    if (!ValidSlot(gamepad) || !gPads[gamepad].connected) return 0.0f;
    return ApplyTriggerDeadZone(gPads[gamepad].current.Gamepad.bRightTrigger);
}

void SetGamepadVibration(float leftMotor, float rightMotor, int gamepad) {
    if (!ValidSlot(gamepad) || !gPads[gamepad].connected) return;

    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = static_cast<WORD>(Clamp(leftMotor, 0.0f, 1.0f) * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(Clamp(rightMotor, 0.0f, 1.0f) * 65535.0f);
    XInputSetState(static_cast<DWORD>(gamepad), &vibration);
}

} // namespace n3d
