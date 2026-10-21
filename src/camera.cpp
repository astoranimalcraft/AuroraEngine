// NexiumEngine — camera.cpp
// The orbit controller needs input and frame time, which live in other headers,
// so it is implemented here rather than inline in camera.h. Keeping it in the
// library means an example does not have to re-derive the same camera maths.
#include "nexium/camera.h"

#include "nexium/NexiumEngine.h"   // GetFrameTime()
#include "nexium/input.h"

#include <cmath>

namespace n3d {

void UpdateFpsCamera(FpsCamera& camera, MouseButton lookButton) {
    // The mouse looks around either while `lookButton` is held or whenever the
    // cursor is captured, which is the usual arrangement in a shooter: dragging is
    // for a windowed editor, capture is for play.
    if (IsMouseButtonDown(lookButton) || IsMouseCaptured()) camera.Look(GetMouseDelta());

    const float deltaTime = GetFrameTime();
    const float speedScale = deltaTime;

    // Walk in the horizontal plane, then fly with Space/Ctrl.
    Vec2 walk{};
    if (IsKeyDown(Key::W)) walk.y += 1.0f;
    if (IsKeyDown(Key::S)) walk.y -= 1.0f;
    if (IsKeyDown(Key::D)) walk.x += 1.0f;
    if (IsKeyDown(Key::A)) walk.x -= 1.0f;
    if (walk.x != 0.0f || walk.y != 0.0f) camera.Walk(walk, speedScale);

    float rise = 0.0f;
    if (IsKeyDown(Key::Space)) rise += 1.0f;
    if (IsKeyDown(Key::LeftControl) || IsKeyDown(Key::C)) rise -= 1.0f;
    if (rise != 0.0f) camera.position.y += rise * camera.moveSpeed * speedScale;
}

void UpdateOrbitCamera(OrbitCamera& camera, MouseButton rotateButton) {
    if (IsMouseButtonDown(rotateButton)) {
        const Vec2 delta = GetMouseDelta();
        camera.yaw -= delta.x * camera.rotateSpeed;
        camera.pitch = Clamp(camera.pitch + delta.y * camera.rotateSpeed, camera.minPitch,
                             camera.maxPitch);
    }

    const float wheel = GetMouseWheelDelta();
    if (wheel != 0.0f) {
        // A notch multiplies the distance, so zooming feels the same near and far.
        camera.distance = Clamp(camera.distance * std::pow(camera.zoomSpeed, -wheel),
                                camera.minDistance, camera.maxDistance);
    }

    // WASD (or the arrow keys) pans the orbit target. The movement is applied in
    // the camera's own horizontal frame, so it stays intuitive after a rotation.
    float forward = 0.0f;
    float right = 0.0f;
    if (IsKeyDown(Key::W) || IsKeyDown(Key::Up)) forward += 1.0f;
    if (IsKeyDown(Key::S) || IsKeyDown(Key::Down)) forward -= 1.0f;
    if (IsKeyDown(Key::D) || IsKeyDown(Key::Right)) right += 1.0f;
    if (IsKeyDown(Key::A) || IsKeyDown(Key::Left)) right -= 1.0f;

    if (forward == 0.0f && right == 0.0f) return;

    const float speed = camera.moveSpeed * GetFrameTime();
    // Position() places the eye at target + (sin yaw, ., cos yaw) * distance, so
    // "forward" for the camera is the opposite of that horizontal direction.
    const Vec3 forwardAxis{-std::sin(camera.yaw), 0.0f, -std::cos(camera.yaw)};
    const Vec3 rightAxis{std::cos(camera.yaw), 0.0f, -std::sin(camera.yaw)};
    camera.target = camera.target + (forwardAxis * forward + rightAxis * right) * speed;
}

} // namespace n3d
