// NexiumEngine — the immediate-mode drawing API.
//
// Everything here is a thin translation into a Renderer call: convert friendly
// arguments (euler degrees, sizes, colors) into a world matrix plus material and
// queue it. Keeping the conversion in one file means the queuing rules — winding,
// culling, transparency — are stated exactly once.
#include "nexium/draw.h"

#include "nexium/log.h"

#include "internal/gpu.h"
#include "internal/renderer.h"
#include "internal/window.h"

#include <cmath>

namespace n3d {
namespace {

// Euler degrees to a world matrix, the way n3d::DrawMesh is documented.
Mat4 MakeWorld(Vec3 position, Vec3 rotationDegrees, Vec3 scale) {
    return Compose(position, Vec3{Radians(rotationDegrees.x), Radians(rotationDegrees.y),
                                  Radians(rotationDegrees.z)},
                   scale);
}

// Shared by every Draw* solid: resolve the mesh, then queue it.
void DrawPrimitive(const Mesh* mesh, Vec3 position, Vec3 rotationDegrees, Vec3 scale, Color color) {
    if (!mesh) return;
    internal::Renderer::Get().DrawMesh(mesh, MakeWorld(position, rotationDegrees, scale), color,
                                       GetDefaultTexture(), 0.0f, 0.5f);
}

} // namespace

// BeginScene / BeginSceneOverlay / EndScene / GetAspectRatio are defined in
// engine.cpp, next to the rest of the frame lifecycle. 2D overlay calls such as
// n3d::DrawScreenRect() are queued here and drawn by EndScene(), so they may be
// issued anywhere between BeginScene() and EndScene().

// --------------------------------------------------------------- meshes ----
void DrawMesh(const Mesh* mesh, Vec3 position, Vec3 rotationDegrees, Vec3 scale, Color tint) {
    if (!mesh) return;
    internal::Renderer::Get().DrawMesh(mesh, MakeWorld(position, rotationDegrees, scale), tint,
                                       GetDefaultTexture(), 0.0f, 0.5f);
}

void DrawMeshEx(const Mesh* mesh, const Mat4& world, Color tint) {
    if (!mesh) return;
    internal::Renderer::Get().DrawMesh(mesh, world, tint, GetDefaultTexture(), 0.0f, 0.5f);
}

void DrawMeshMaterial(const Mesh* mesh, Vec3 position, Texture* texture, Vec3 rotationDegrees,
                      Vec3 scale, Color tint, float metallic, float roughness) {
    if (!mesh) return;
    internal::Renderer::Get().DrawMesh(mesh, MakeWorld(position, rotationDegrees, scale), tint,
                                       texture, metallic, roughness);
}

void SetTexture(Texture* texture) {
    // Textures bind per draw rather than as sticky state, so this covers the case
    // of wanting the next mesh draw textured without naming a texture in it.
    internal::Renderer::Get().SetPendingTexture(texture);
}

// ---------------------------------------------------------------- solids ----
void DrawCube(Vec3 position, Vec3 size, Color color) {
    // The unit cube spans -0.5..0.5, so the scale is the size itself.
    DrawPrimitive(GetCubeMesh(1.0f), position, Vec3{}, size, color);
}

void DrawSphere(Vec3 center, float radius, Color color) {
    // The mesh is built at the requested radius and drawn unscaled, so the sphere
    // is exactly as wide as asked. (Scaling a radius-1 mesh by radius * 2, as an
    // earlier version did, drew it twice the size.)
    DrawPrimitive(GetSphereMesh(radius), center, Vec3{}, Vec3{1, 1, 1}, color);
}

void DrawSphereEx(Vec3 center, float radius, int rings, int slices, Color color) {
    DrawPrimitive(GetSphereMesh(radius, rings, slices), center, Vec3{}, Vec3{1, 1, 1}, color);
}

void DrawCylinder(Vec3 center, float radius, float height, Color color) {
    DrawPrimitive(GetCylinderMesh(radius, height), center, Vec3{}, Vec3{1, 1, 1}, color);
}

void DrawCylinderEx(Vec3 start, Vec3 end, float radius, Color color) {
    const Vec3 axis = end - start;
    const float length = Length(axis);
    if (length < 1e-6f) return;

    // The mesh is built along +Y, so rotate +Y onto the segment's direction.
    const Vec3 up{0.0f, 1.0f, 0.0f};
    const Vec3 direction = axis / length;
    Mat4 rotation;
    const float alignment = Dot(up, direction);
    if (alignment > 0.99999f) {
        rotation = Mat4::Identity();
    } else if (alignment < -0.99999f) {
        rotation = RotationX(PI);   // pointing straight down
    } else {
        rotation = RotationAxis(Cross(up, direction), std::acos(Clamp(alignment, -1.0f, 1.0f)));
    }

    const Mat4 world = Translation((start + end) * 0.5f) * rotation;
    internal::Renderer::Get().DrawMesh(GetCylinderMesh(radius, length), world, color,
                                       GetDefaultTexture(), 0.0f, 0.5f);
}

void DrawPlane(Vec3 center, Vec2 size, Color color) {
    // The unit plane is 1x1 in XZ, so the scale is the requested extent.
    DrawPrimitive(GetPlaneMesh(1.0f, 1.0f), center, Vec3{}, Vec3{size.x, 1.0f, size.y}, color);
}

void DrawCone(Vec3 baseCenter, float radius, float height, Color color) {
    // The cone mesh is built with its base at -height/2, so the centre is offset.
    const Vec3 center{baseCenter.x, baseCenter.y + height * 0.5f, baseCenter.z};
    DrawPrimitive(GetConeMesh(radius, height), center, Vec3{}, Vec3{1, 1, 1}, color);
}

void DrawCapsule(Vec3 center, float radius, float height, Color color) {
    DrawPrimitive(GetCapsuleMesh(radius, height), center, Vec3{}, Vec3{1, 1, 1}, color);
}

void DrawTorus(Vec3 center, float majorRadius, float minorRadius, Color color) {
    DrawPrimitive(GetTorusMesh(majorRadius, minorRadius), center, Vec3{}, Vec3{1, 1, 1}, color);
}

void DrawPyramid(Vec3 baseCenter, float baseSize, float height, Color color) {
    // The pyramid mesh is built with its base at -height/2.
    const Vec3 center{baseCenter.x, baseCenter.y + height * 0.5f, baseCenter.z};
    DrawPrimitive(GetPyramidMesh(baseSize, height), center, Vec3{}, Vec3{1, 1, 1}, color);
}

void DrawDisk(Vec3 center, float radius, Color color) {
    DrawPrimitive(GetDiskMesh(radius), center, Vec3{}, Vec3{1, 1, 1}, color);
}

void DrawQuad(Vec3 center, Vec2 size, Vec3 normal, Color color) {
    const float lengthSq = LengthSq(normal);
    if (lengthSq < 1e-8f) return;

    // A quad is the unit plane oriented so its +Y normal points where the caller
    // asked, then scaled to the requested extent.
    const Vec3 target = normal / std::sqrt(lengthSq);
    const Vec3 up{0.0f, 1.0f, 0.0f};
    const float alignment = Dot(up, target);
    Mat4 rotation;
    if (alignment > 0.99999f) {
        rotation = Mat4::Identity();
    } else if (alignment < -0.99999f) {
        rotation = RotationX(PI);
    } else {
        rotation = RotationAxis(Cross(up, target), std::acos(Clamp(alignment, -1.0f, 1.0f)));
    }

    const Mat4 world = Translation(center) * rotation * Scale(Vec3{size.x, 1.0f, size.y});
    internal::Renderer::Get().DrawMesh(GetPlaneMesh(1.0f, 1.0f), world, color, GetDefaultTexture(),
                                       0.0f, 0.5f);
}

void DrawBox(Bounds bounds, Color color) {
    if (!bounds.Valid()) return;
    DrawPrimitive(GetCubeMesh(1.0f), bounds.Center(), Vec3{}, bounds.Extents() * 2.0f, color);
}

// ----------------------------------------------------------------- lines ----
void DrawLine(Vec3 from, Vec3 to, Color color) {
    internal::Renderer::Get().DrawLine(from, to, color);
}

void DrawRay(Ray ray, float length, Color color) {
    DrawLine(ray.origin, ray.origin + Normalize(ray.direction) * length, color);
}

void DrawTriangle(Vec3 a, Vec3 b, Vec3 c, Color color) {
    DrawLine(a, b, color);
    DrawLine(b, c, color);
    DrawLine(c, a, color);
}

void DrawGrid(float extent, int divisions, Color color) {
    if (divisions < 1) divisions = 1;
    const float step = (extent * 2.0f) / static_cast<float>(divisions);
    for (int i = 0; i <= divisions; ++i) {
        const float offset = -extent + step * static_cast<float>(i);
        DrawLine(Vec3{offset, 0.0f, -extent}, Vec3{offset, 0.0f, extent}, color);
        DrawLine(Vec3{-extent, 0.0f, offset}, Vec3{extent, 0.0f, offset}, color);
    }
}

void DrawAxes(Vec3 origin, float length) {
    DrawLine(origin, origin + Vec3{length, 0, 0}, colors::Red);
    DrawLine(origin, origin + Vec3{0, length, 0}, colors::Green);
    DrawLine(origin, origin + Vec3{0, 0, length}, colors::Blue);
}

void DrawBounds(Bounds bounds, Color color) {
    if (!bounds.Valid()) return;

    const Vec3& lo = bounds.min;
    const Vec3& hi = bounds.max;
    const Vec3 corners[8] = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
    };

    // Bottom and top quads, then the four vertical edges.
    for (int i = 0; i < 4; ++i) {
        DrawLine(corners[i], corners[(i + 1) % 4], color);
        DrawLine(corners[4 + i], corners[4 + (i + 1) % 4], color);
        DrawLine(corners[i], corners[4 + i], color);
    }
}

void DrawWireSphere(Vec3 center, float radius, Color color, int rings, int slices) {
    if (rings < 2) rings = 2;
    if (slices < 3) slices = 3;

    const float ringStep = PI / static_cast<float>(rings);
    const float sliceStep = TAU / static_cast<float>(slices);

    for (int r = 1; r < rings; ++r) {
        const float phi = ringStep * static_cast<float>(r);
        const float y = std::cos(phi) * radius;
        const float ringRadius = std::sin(phi) * radius;
        Vec3 previous{};
        for (int s = 0; s <= slices; ++s) {
            const float theta = sliceStep * static_cast<float>(s);
            const Vec3 point{center.x + std::cos(theta) * ringRadius, center.y + y,
                             center.z + std::sin(theta) * ringRadius};
            if (s > 0) DrawLine(previous, point, color);
            previous = point;
        }
    }

    // Meridians.
    for (int s = 0; s < slices; ++s) {
        const float theta = sliceStep * static_cast<float>(s);
        const Vec3 axis{std::cos(theta), 0.0f, std::sin(theta)};
        Vec3 previous{};
        for (int r = 0; r <= rings; ++r) {
            const float phi = ringStep * static_cast<float>(r);
            const Vec3 point = center + (axis * std::sin(phi) + Vec3{0, std::cos(phi), 0}) * radius;
            if (r > 0) DrawLine(previous, point, color);
            previous = point;
        }
    }
}

// ------------------------------------------------------------------- 2D ----
void DrawScreenLine(Vec2 from, Vec2 to, Color color, float thickness) {
    internal::Renderer::Get().DrawScreenLine(from, to, color, thickness);
}

void DrawScreenRect(Vec2 position, Vec2 size, Color color) {
    internal::Renderer::Get().DrawScreenRect(position, size, color);
}

} // namespace n3d
