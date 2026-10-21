// NexiumEngine — camera.h
// Cameras are plain value types. The engine never stores one; you pass it to
// n3d::BeginScene() each frame, which keeps camera logic in your code.
#pragma once

#include "nexium/input.h"
#include "nexium/math.h"

namespace n3d {

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
    // (y up). Handy for mouse picking: n3d::GetMouseNdc() feeds straight in.
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

    // Ray through a pixel of the render target (origin top-left, y down). The
    // counterpart of Camera2D::ScreenToWorld for a 3D scene, and what a "click to
    // select" needs.
    Ray RayThroughScreen(Vec2 pixel, float width, float height) const {
        const Vec2 ndc{(pixel.x / width) * 2.0f - 1.0f, 1.0f - (pixel.y / height) * 2.0f};
        return RayThroughNdc(ndc, width / height);
    }

    // Projects a world point to a pixel (origin top-left, y down). The returned
    // z is the normalized depth when the point is in front of the camera, and -1
    // when it is behind it (or on the eye plane), so callers can reject those
    // without having to reason about clip space.
    Vec3 WorldToScreen(Vec3 world, float width, float height) const {
        const Vec4 clip = ViewProjection(width / height) * Vec4{world.x, world.y, world.z, 1.0f};
        if (clip.w <= 1e-8f) return Vec3{0.0f, 0.0f, -1.0f};
        const float invW = 1.0f / clip.w;
        const float ndcX = clip.x * invW;
        const float ndcY = clip.y * invW;
        const float ndcZ = clip.z * invW;
        return Vec3{(ndcX * 0.5f + 0.5f) * width, (0.5f - ndcY * 0.5f) * height, ndcZ};
    }

    // The camera's orthonormal basis. Right/Up come straight from the view matrix
    // so they can never disagree with what is drawn.
    Vec3 Right() const { return Normalize(Cross(Forward(), up)); }
    Vec3 Up() const { return Normalize(Cross(Right(), Forward())); }

    // Moves the eye and the look-at point together, i.e. slides the camera.
    void Move(Vec3 delta) { position = position + delta; target = target + delta; }

    // Moves along the camera's own axes. `delta` is in world units: x = right,
    // y = up, z = forward.
    void MoveLocal(Vec3 delta) {
        Move(Right() * delta.x + Up() * delta.y + Forward() * delta.z);
    }

    // Orbits the eye around `target` by the given angles (radians), keeping the
    // distance. Pitch is clamped just short of the poles so `up` stays usable.
    void Orbit(float yawRadians, float pitchRadians) {
        Vec3 offset = position - target;
        const float radius = Length(offset);
        if (radius < 1e-5f) return;

        // Current spherical coordinates of the offset, then add the deltas.
        float currentYaw = std::atan2(offset.x, offset.z);
        float currentPitch = std::asin(Clamp(offset.y / radius, -1.0f, 1.0f));
        currentYaw += yawRadians;
        currentPitch = Clamp(currentPitch + pitchRadians, -1.5533f, 1.5533f);   // +-89 degrees

        const float cosPitch = std::cos(currentPitch);
        offset = Vec3{cosPitch * std::sin(currentYaw), std::sin(currentPitch),
                      cosPitch * std::cos(currentYaw)} * radius;
        position = target + offset;
    }

    // Moves the eye toward or away from the target by `amount` world units.
    void Zoom(float amount) {
        const Vec3 direction = Forward();
        const float distance = Length(target - position);
        const float wanted = Clamp(distance - amount, 0.01f, distance);
        position = target - direction * wanted;
    }

    // Camera parameters that make the field of view match `other`, so a smooth
    // transition between two cameras is a Lerp over this struct.
    struct Snapshot {
        Vec3 position{};
        Vec3 target{};
        Vec3 up{};
        float fovY = 60.0f;
        float nearZ = 0.05f;
        float farZ = 2000.0f;
    };

    Snapshot Capture() const { return Snapshot{position, target, up, fovY, nearZ, farZ}; }

    void Apply(const Snapshot& snapshot) {
        position = snapshot.position;
        target = snapshot.target;
        up = snapshot.up;
        fovY = snapshot.fovY;
        nearZ = snapshot.nearZ;
        farZ = snapshot.farZ;
    }
};

// Interpolates between two camera snapshots. Both ends must describe the same
// kind of motion or the intermediate frames swing; it is meant for short blends.
inline Camera3D::Snapshot LerpCamera(const Camera3D::Snapshot& a, const Camera3D::Snapshot& b,
                                     float t) {
    Camera3D::Snapshot out;
    out.position = Lerp(a.position, b.position, t);
    out.target = Lerp(a.target, b.target, t);
    out.up = Normalize(Lerp(a.up, b.up, t));
    out.fovY = Lerp(a.fovY, b.fovY, t);
    out.nearZ = Lerp(a.nearZ, b.nearZ, t);
    out.farZ = Lerp(a.farZ, b.farZ, t);
    return out;
}

// Blends one camera toward another. `t` is clamped to [0, 1]. Use it for a
// cinematic "cut to" or a follow camera that lags behind its target.
inline Camera3D LerpCamera(const Camera3D& a, const Camera3D& b, float t) {
    Camera3D out;
    out.Apply(LerpCamera(a.Capture(), b.Capture(), Clamp(t, 0.0f, 1.0f)));
    return out;
}

// A six-plane view frustum, for culling. Planes are stored as (a, b, c, d) with
// the normal (a, b, c) pointing *into* the frustum, so a point is inside when
// Dot(normal, point) + d >= 0 for every plane.
struct Frustum {
    Vec4 planes[6];

    // Builds the frustum from a view-projection matrix (right-handed, depth [0,1]).
    static Frustum FromViewProjection(const Mat4& vp) {
        // Gribb-Hartmann extraction. Rows of the matrix, since the engine stores
        // matrices column-major and multiplies column vectors on the left.
        const auto row = [&vp](int r) {
            return Vec4{vp.At(0, r), vp.At(1, r), vp.At(2, r), vp.At(3, r)};
        };
        const Vec4 x = row(0), y = row(1), z = row(2), w = row(3);

        Frustum f;
        f.planes[0] = w + x;   // left
        f.planes[1] = w - x;   // right
        f.planes[2] = w + y;   // bottom
        f.planes[3] = w - y;   // top
        f.planes[4] = z;       // near (depth 0 at the near plane)
        f.planes[5] = w - z;   // far
        for (Vec4& plane : f.planes) {
            const Vec3 normal{plane.x, plane.y, plane.z};
            const float length = Length(normal);
            if (length > 1e-8f) plane = plane * (1.0f / length);
        }
        return f;
    }

    bool ContainsPoint(Vec3 point) const {
        for (const Vec4& plane : planes) {
            if (plane.x * point.x + plane.y * point.y + plane.z * point.z + plane.w < 0.0f) {
                return false;
            }
        }
        return true;
    }

    // Conservative sphere test: true when the sphere is not entirely outside any
    // single plane. May report a false positive at the corners, which is the safe
    // direction for culling.
    bool ContainsSphere(Vec3 centre, float radius) const {
        for (const Vec4& plane : planes) {
            if (plane.x * centre.x + plane.y * centre.y + plane.z * centre.z + plane.w < -radius) {
                return false;
            }
        }
        return true;
    }

    bool ContainsBounds(Bounds bounds) const {
        if (!bounds.Valid()) return false;
        // Test each of the eight corners; a box is visible if any corner is
        // inside or if it straddles the frustum, which the per-plane test below
        // detects by keeping the box when it is not fully on the outside.
        for (const Vec4& plane : planes) {
            const Vec3 positive{plane.x >= 0.0f ? bounds.max.x : bounds.min.x,
                                plane.y >= 0.0f ? bounds.max.y : bounds.min.y,
                                plane.z >= 0.0f ? bounds.max.z : bounds.min.z};
            if (plane.x * positive.x + plane.y * positive.y + plane.z * positive.z + plane.w <
                0.0f) {
                return false;
            }
        }
        return true;
    }
};

// Builds the frustum for a camera at the given aspect ratio.
inline Frustum GetCameraFrustum(const Camera3D& camera, float aspect) {
    return Frustum::FromViewProjection(camera.ViewProjection(aspect));
}

// ---------------------------------------------------------------- FPS --------
// A first-person camera driven the way a shooter expects: the mouse looks
// around, WASD walks, Space/Ctrl fly up and down. Unlike OrbitCamera this one
// *is* the eye: `position` is the camera, `yaw`/`pitch` are its orientation.
struct FpsCamera {
    Vec3  position{0.0f, 1.7f, 0.0f};
    float yaw   = 0.0f;        // radians, 0 looks toward +Z
    float pitch = 0.0f;        // radians, positive looks up
    float fovY  = 60.0f;       // degrees
    float nearZ = 0.05f;
    float farZ  = 2000.0f;

    float moveSpeed   = 6.0f;      // units/second
    float lookSpeed   = 0.0025f;   // radians per pixel dragged
    float maxPitch    = 1.5533f;   // ~89 degrees

    Vec3 Forward() const {
        const float cp = std::cos(pitch);
        return Vec3{cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)};
    }
    // Horizontal forward, for walking without flying.
    Vec3 ForwardFlat() const {
        const Vec3 f = Forward();
        const float length = std::sqrt(f.x * f.x + f.z * f.z);
        return length > 1e-5f ? Vec3{f.x / length, 0.0f, f.z / length} : Vec3{0, 0, 1};
    }
    Vec3 Right() const { return Normalize(Cross(Forward(), Vec3{0, 1, 0})); }

    Camera3D ToCamera() const {
        Camera3D c;
        c.position = position;
        c.target = position + Forward();
        c.fovY = fovY;
        c.nearZ = nearZ;
        c.farZ = farZ;
        return c;
    }

    // Applies a mouse delta (pixels) to yaw/pitch. Moving the mouse right turns
    // the view right, and moving it up looks up.
    void Look(Vec2 mouseDelta) {
        yaw -= mouseDelta.x * lookSpeed;
        pitch = Clamp(pitch - mouseDelta.y * lookSpeed, -maxPitch, maxPitch);
    }

    // Walks in the horizontal plane by `delta` (x = strafe right, y = forward).
    void Walk(Vec2 delta, float speedScale = 1.0f) {
        const Vec3 move = Right() * delta.x + ForwardFlat() * delta.y;
        position = position + move * (moveSpeed * speedScale);
    }
};

// A decaying positional/rotational shake, for impacts and explosions. Add the
// offset to a camera before drawing and call Update() once per frame.
struct CameraShake {
    float trauma = 0.0f;      // 0 = still, 1 = maximum
    float decay = 1.6f;       // trauma lost per second
    float frequency = 22.0f;  // oscillations per second
    float maxOffset = 0.35f;  // world units at trauma 1
    float maxRoll = 0.06f;    // radians at trauma 1
    float time = 0.0f;

    // Adds shake. Values add up, so several hits in one frame shake harder.
    void Add(float amount) { trauma = Clamp(trauma + amount, 0.0f, 1.0f); }

    void Update(float deltaTime) {
        time += deltaTime;
        trauma = Clamp(trauma - decay * deltaTime, 0.0f, 1.0f);
    }

    // Shake amplitude is squared so the tail settles smoothly instead of cutting
    // off abruptly.
    float Amplitude() const { return trauma * trauma; }

    // Small offset to add to the camera position. Deterministic in `time` rather
    // than random, so it stays smooth from frame to frame.
    Vec3 Offset() const {
        const float a = Amplitude();
        if (a <= 0.0f) return Vec3{};
        const float t = time * frequency;
        return Vec3{std::sin(t * 1.1f), std::sin(t * 1.7f + 1.3f), std::sin(t * 0.9f + 2.1f)} *
               (maxOffset * a);
    }

    // Roll angle (radians) to add to the camera's orientation.
    float Roll() const { return std::sin(time * frequency * 1.3f) * maxRoll * Amplitude(); }
};

// Drives an FPS camera from the standard controls: mouse look (holding
// `lookButton`, left by default, or automatically when the mouse is captured),
// WASD to walk, Space/Ctrl to fly. Call once per frame before BeginScene().
void UpdateFpsCamera(FpsCamera& camera, MouseButton lookButton = MouseButton::Left);

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

} // namespace n3d
