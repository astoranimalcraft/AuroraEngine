// NexiumEngine — draw.h
// The immediate-mode drawing API. Everything between BeginScene() and EndScene()
// is queued into per-frame vertex/index streams and submitted in one go, sorted
// only when it has to be (state changes and transparency).
#pragma once

#include "nexium/camera.h"
#include "nexium/color.h"
#include "nexium/math.h"
#include "nexium/mesh.h"
#include "nexium/texture.h"

// windows.h defines DrawText as an alias for DrawTextW/DrawTextA. An application
// that includes <windows.h> before this header would otherwise see the engine's
// DrawText() declaration rewritten into a call to the Win32 function.
#ifdef DrawText
#undef DrawText
#endif
#ifdef DrawTextEx
#undef DrawTextEx
#endif

namespace n3d {

// --------------------------------------------------------------- scene -------
// Starts a 3D frame. Clears the render target and depth buffer to `clear`.
// Must be paired with EndScene().
void BeginScene(const Camera3D& camera, Color clear = colors::SkyBlue);

// Same, but keeps whatever is already in the colour buffer (depth is still
// cleared) so 2D/overlay passes can be layered on top of a 3D scene.
void BeginSceneOverlay(const Camera3D& camera);

void EndScene();

// Aspect ratio of the current render target, for ad-hoc matrix math.
float GetAspectRatio();

// --------------------------------------------------------------- meshes ------
// Draw a mesh with a full transform.
void DrawMesh(const Mesh* mesh, Vec3 position,
              Vec3 rotationDegrees = Vec3{},
              Vec3 scale = Vec3{1, 1, 1},
              Color tint = colors::White);

// Draw a mesh with an explicit world matrix (bypasses the euler/scale helpers).
void DrawMeshEx(const Mesh* mesh, const Mat4& world, Color tint = colors::White);

// Draw a mesh with a texture and PBR-ish material parameters.
// `metallic` and `roughness` are in [0, 1].
void DrawMeshMaterial(const Mesh* mesh, Vec3 position, Texture* texture,
                      Vec3 rotationDegrees = Vec3{},
                      Vec3 scale = Vec3{1, 1, 1},
                      Color tint = colors::White,
                      float metallic = 0.0f,
                      float roughness = 0.5f);

// Per-draw texture binding for DrawMesh / DrawMeshEx, reset to the default white
// texture by the next draw call. Cheap, but prefer DrawMeshMaterial for clarity.
void SetTexture(Texture* texture);

// ---------------------------------------------------------------- solids ----
// One-liner primitive draws. Each is equivalent to the matching Get*Mesh() plus
// DrawMesh(), and exists so simple scenes read like raylib. Sizes are exact: a
// DrawSphere of radius 1 spans 2 units, a DrawCylinder of height 2 is 2 tall.
void DrawCube(Vec3 position, Vec3 size, Color color = colors::White);
void DrawSphere(Vec3 center, float radius, Color color = colors::White);
void DrawSphereEx(Vec3 center, float radius, int rings, int slices, Color color = colors::White);
void DrawCylinder(Vec3 center, float radius, float height, Color color = colors::White);
void DrawCylinderEx(Vec3 start, Vec3 end, float radius, Color color = colors::White);
void DrawPlane(Vec3 center, Vec2 size, Color color = colors::White);
void DrawCone(Vec3 baseCenter, float radius, float height, Color color = colors::White);
void DrawCapsule(Vec3 center, float radius, float height, Color color = colors::White);
void DrawTorus(Vec3 center, float majorRadius, float minorRadius, Color color = colors::White);
void DrawPyramid(Vec3 baseCenter, float baseSize, float height, Color color = colors::White);
void DrawDisk(Vec3 center, float radius, Color color = colors::White);
void DrawQuad(Vec3 center, Vec2 size, Vec3 normal, Color color = colors::White);

// Axis-aligned box given by its bounds.
void DrawBox(Bounds bounds, Color color = colors::White);

// ----------------------------------------------------------------- lines -----
// Debug line drawing. All lines are batched into a single draw call per frame and
// rendered depth-tested (SetDepthTest(DepthTest::Always) for an overlay pass).
void DrawLine(Vec3 from, Vec3 to, Color color = colors::White);
void DrawRay(Ray ray, float length, Color color = colors::White);
void DrawTriangle(Vec3 a, Vec3 b, Vec3 c, Color color = colors::White);
void DrawGrid(float extent = 10.0f, int divisions = 10, Color color = colors::Gray);
void DrawAxes(Vec3 origin = Vec3{}, float length = 1.0f);
void DrawBounds(Bounds bounds, Color color = colors::Yellow);
void DrawWireSphere(Vec3 center, float radius, Color color = colors::Yellow,
                    int rings = 8, int slices = 16);

// ---------------------------------------------------------------- 2D -------
// Screen-space overlay drawing. These are queued and drawn as the last pass of
// the frame, so they can be issued anywhere between BeginScene() and EndScene();
// coordinates are pixels with the origin at the top-left. Use
// BeginSceneOverlay() for a frame that is *only* 2D.
void DrawScreenLine(Vec2 from, Vec2 to, Color color = colors::White, float thickness = 1.0f);
void DrawScreenRect(Vec2 position, Vec2 size, Color color = colors::White);

// -------------------------------------------------------------- 2D mode -----
// A 2D camera. `offset` is the screen position the `target` point is drawn at and
// `zoom` scales everything, so a camera is: what to look at, where to put it, how
// big. Rotation is in degrees, clockwise on screen.
//
// Unlike n3d::Camera3D this is not required to draw 2D: with no 2D mode active,
// the Draw*2D calls take raw pixel coordinates with the origin at the top-left.
struct Camera2D {
    Vec2  offset{0.0f, 0.0f};      // screen-space pivot, usually the screen centre
    Vec2  target{0.0f, 0.0f};      // world point that lands on `offset`
    float rotation = 0.0f;         // degrees
    float zoom = 1.0f;

    // Move the target to the origin, rotate, scale, then move the result to the
    // screen-space offset. Rotation is negated because the camera rotates opposite
    // to the world it looks at.
    Mat4 View() const {
        return Translation(Vec3{offset.x, offset.y, 0.0f}) * Scale(Vec3{zoom, zoom, 1.0f}) *
               RotationZ(Radians(-rotation)) * Translation(Vec3{-target.x, -target.y, 0.0f});
    }

    // Pixel-space orthographic projection with the origin at the top-left (+y down).
    Mat4 Projection(float width, float height) const {
        return Ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
    }

    Mat4 ViewProjection(float width, float height) const {
        return Projection(width, height) * View();
    }

    // Screen (pixels) <-> world conversions, for mouse picking in 2D.
    Vec2 WorldToScreen(Vec2 world, float width, float height) const {
        const Mat4 viewProj = ViewProjection(width, height);
        const Vec4 clip = viewProj * Vec4{world.x, world.y, 0.0f, 1.0f};
        // The projection flips y, so convert straight to pixels.
        return Vec2{(clip.x * 0.5f + 0.5f) * width, (0.5f - clip.y * 0.5f) * height};
    }

    Vec2 ScreenToWorld(Vec2 screen, float width, float height) const {
        const Mat4 inverse = Inverse(ViewProjection(width, height));
        // Pixels -> NDC (y down), then through the inverse.
        const float ndcX = (screen.x / width) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (screen.y / height) * 2.0f;
        const Vec4 world = inverse * Vec4{ndcX, ndcY, 0.0f, 1.0f};
        return Vec2{world.x / world.w, world.y / world.w};
    }
};

// Starts a 2D drawing block. Everything drawn until EndMode2D() is transformed by
// the camera and clipped to the screen. Nesting is not supported; the renderer
// keeps a single 2D pass.
void BeginMode2D(const Camera2D& camera);
void EndMode2D();
// True between BeginMode2D() and EndMode2D().
bool IsMode2DActive();
Camera2D GetMode2DCamera();

// Screen rectangle in pixels, for layout code.
Rect GetScreenRect();

// ---------------------------------------------------------------- shapes ----
// Filled and outlined shapes in pixel coordinates. If a 2D camera is active the
// coordinates are world coordinates instead, transformed by the camera.
void DrawRectangle(float x, float y, float width, float height, Color color);
void DrawRectangleV(Vec2 position, Vec2 size, Color color);
void DrawRectangleRec(Rect rect, Color color);
void DrawRectangleLines(float x, float y, float width, float height, Color color);
void DrawRectangleLinesEx(Rect rect, float thickness, Color color);
void DrawRectangleGradient(Rect rect, Color topLeft, Color bottomRight);

void DrawCircle(float centerX, float centerY, float radius, Color color);
void DrawCircleV(Vec2 center, float radius, Color color);
void DrawCircleLines(Vec2 center, float radius, Color color);
void DrawCircleSector(Vec2 center, float radius, float startDegrees, float endDegrees, Color color);
void DrawRing(Vec2 center, float innerRadius, float outerRadius, float startDegrees,
              float endDegrees, Color color);

void DrawTriangle2D(Vec2 a, Vec2 b, Vec2 c, Color color);
void DrawTriangleLines2D(Vec2 a, Vec2 b, Vec2 c, Color color);
void DrawPolygon(Vec2 center, int sides, float radius, float rotationDegrees, Color color);
void DrawPolygonLines(Vec2 center, int sides, float radius, float rotationDegrees, Color color);
// Free-form convex polygon, vertices in order. Triangles are fanned from vertex 0.
void DrawPolygonV(const Vec2* points, int pointCount, Color color);
void DrawPolyline(const Vec2* points, int pointCount, float thickness, Color color);

// ----------------------------------------------------------------- text ----
// A bitmap font. Fonts own a texture and a glyph table; the built-in font is
// created by n3d::Init() and must not be freed.
struct Font;

// Loads a font from an image laid out as an ASCII grid: 96 glyphs (space through
// '~') across `columns` columns, each cell `glyphWidth` x `glyphHeight` pixels.
Font* LoadFontFromImage(const char* path, int glyphWidth, int glyphHeight, int columns);
Font* LoadFontFromTexture(Texture* texture, int glyphWidth, int glyphHeight, int columns);
void UnloadFont(Font* font);

// The engine's own font, usable immediately after Init().
Font* GetDefaultFont();
void SetFont(Font* font);
Font* GetFont();

// Draws `text` with its top-left at (x, y). Newlines are honoured.
void DrawText(const char* text, int x, int y, float fontSize, Color color);
void DrawTextV(const char* text, Vec2 position, float fontSize, Color color);
// Centres the text horizontally and vertically on (centerX, centerY).
void DrawTextCentered(const char* text, float centerX, float centerY, float fontSize, Color color);
// Draws with a background rectangle, useful for debug HUDs.
void DrawTextWithBackground(const char* text, int x, int y, float fontSize, Color textColor,
                            Color backgroundColor);
// Measures what DrawText would occupy, for layout and hit testing.
Vec2 MeasureText(const char* text, float fontSize);
float MeasureTextWidth(const char* text, float fontSize);
// Formats with printf and draws. The buffer is capped at 1024 characters.
void DrawTextFormat(int x, int y, float fontSize, Color color, const char* fmt, ...);

// -------------------------------------------------------------- textures ----
// Draws a texture. `position` is the top-left corner. With no 2D camera active
// the coordinates are pixels; `rotation` is in degrees and pivots on `origin`
// (0, 0 = top-left, 0.5, 0.5 = centre).
void DrawTexture(Texture* texture, float x, float y, Color tint = colors::White);
void DrawTextureV(Texture* texture, Vec2 position, Color tint = colors::White);
void DrawTextureEx(Texture* texture, Vec2 position, float rotationDegrees, float scale,
                   Color tint = colors::White);
void DrawTextureRec(Texture* texture, Rect source, Vec2 position, Color tint = colors::White);
void DrawTexturePro(Texture* texture, Rect source, Rect dest, Vec2 origin,
                    float rotationDegrees, Color tint = colors::White);

// --------------------------------------------------------------- 3D text ----
// Draws text as billboarded quads facing the camera, at a world position. The
// text is scaled so one font pixel is `worldSize` world units.
void DrawText3D(const char* text, Vec3 position, float worldSize, Color color);

} // namespace n3d
