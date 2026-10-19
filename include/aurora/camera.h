// Aurora3D — camera.h
// Cameras are plain value types. The engine never stores one; you pass it to
// a3d::BeginScene() each frame, which keeps camera logic in your code.
#pragma once

#include "aurora/input.h"
#include "aurora/math.h"

namespace a3d {

struct Camera3D {
    Vec3  position{0.0f, 2.0f, -5.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    Vec3  up{0.0f, 1.0f, 0.0f};

    float fovY  = 60.0f;   // vertical field of view, degrees
    float nearZ = 0.05f;
    float farZ  = 2000.0f;

    Mat4 View() const { return LookAt(position, target, up); }

    Mat4 Projection(float aspect) const {
        return Perspective(Radians(fovY), aspect, nearZ, farZ);
    }

    Mat4 ViewProjection(float aspect) const { return Projection(aspect) * View(); }

    // Direction the camera looks along.
    Vec3 Forward() const { return Normalize(target - position); }

    // Ray through a normalized device coordinate in [-1, 1] x [-1, 1]
    // (y up). Handy for mouse picking: a3d::GetMouseNdc() feeds straight in.
    Ray RayThroughNdc(Vec2 ndc, float aspect) const {
        Mat4 invVP = Inverse(ViewProjection(aspect));
        Vec4 nearH = invVP * Vec4(ndc.x, ndc.y, 0.0f, 1.0f);
        Vec4 farH  = invVP * Vec4(ndc.x, ndc.y, 1.0f, 1.0f);
        Vec3 nearP{nearH.x / nearH.w, nearH.y / nearH.w, nearH.z / nearH.w};
        Vec3 farP{farH.x / farH.w, farH.y / farH.w, farH.z / farH.w};
        Ray r;
        r.origin = nearP;
        r.direction = Normalize(farP - nearP);
        return r;
    }
};

// Spherical orbit controller — yaw around +Y, pitch clamped to avoid the poles.
struct OrbitCamera {
    Vec3  target{0.0f, 0.0f, 0.0f};
    float distance = 8.0f;
    float yaw      = Radians(35.0f);   // radians
    float pitch    = Radians(25.0f);   // radians
    float minPitch = Radians(-85.0f);
    float maxPitch = Radians(85.0f);
    float minDistance = 0.5f;
    float maxDistance = 500.0f;
    float moveSpeed   = 6.0f;          // units/second for WASD panning
    float rotateSpeed = 0.35f;         // radians per pixel dragged
    float zoomSpeed   = 0.9f;          // multiplier per wheel notch

    Vec3 Position() const {
        float cp = std::cos(pitch);
        return target + Vec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)) * distance;
    }

    Camera3D ToCamera() const {
        Camera3D c;
        c.position = Position();
        c.target = target;
        return c;
    }
};

// Drives an orbit camera from the standard controls: drag with `rotateButton`
// (right by default) to look around, the wheel to zoom, and WASD or the arrow
// keys to pan the target. Call it once per frame, before BeginScene().
void UpdateOrbitCamera(OrbitCamera& camera, MouseButton rotateButton = MouseButton::Right);

} // namespace a3d
