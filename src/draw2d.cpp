// Aurora3D — 2D drawing: shapes, sprites, text and the 2D camera.
//
// Every function here builds geometry in *pixel* space with the origin at the
// top-left and hands it to the renderer's overlay pass, which draws it last with
// depth testing off. When a 2D camera is active the geometry is built in world
// units instead and the camera's matrix is baked into the vertices, so the
// overlay pass itself stays a single orthographic projection.
#include "aurora/draw.h"

#include "aurora/log.h"
#include "aurora/shader.h"
#include "aurora/texture.h"

#include "internal/font_data.h"
#include "internal/gpu.h"
#include "internal/renderer.h"
#include "internal/resources.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace a3d {
namespace {

// ------------------------------------------------------------- 2D camera ----
// The active camera and whether a 2D block is open. A single instance is enough
// because the overlay is a single ordered pass.
bool gMode2DActive = false;
Camera2D gMode2DCamera;

// Applies the active camera to a pixel-space point, or passes it through when no
// 2D mode is active. World-to-pixel uses the same matrix the vertices are baked
// with, so hit testing lines up exactly with what is drawn.
Vec2 ToDrawSpace(Vec2 point) {
    if (!gMode2DActive) return point;
    const float width = static_cast<float>(internal::GpuContext::Get().Width());
    const float height = static_cast<float>(internal::GpuContext::Get().Height());
    return gMode2DCamera.WorldToScreen(point, width, height);
}

Vertex MakeVertex(Vec2 position, Color color, Vec2 uv) {
    Vertex vertex;
    vertex.position = Vec3{position.x, position.y, 0.0f};
    vertex.normal = Vec3{0.0f, 0.0f, -1.0f};
    vertex.color = color;
    vertex.uv = uv;
    return vertex;
}

// Submits a run of triangles already built in draw space.
void SubmitTriangles(std::vector<Vertex>& vertices, Texture* texture, bool textured,
                     Shader* shader = nullptr) {
    if (vertices.size() < 3) return;
    internal::Renderer::Get().DrawOverlayTriangles(vertices.data(),
                                                   static_cast<uint32_t>(vertices.size()), texture,
                                                   colors::White, textured, shader);
}

// Appends the six vertices of one triangle to `out`.
void PushTriangle(std::vector<Vertex>& out, Vec2 a, Vec2 b, Vec2 c, Color color, Color colorB,
                  Color colorC) {
    out.push_back(MakeVertex(a, color, Vec2{}));
    out.push_back(MakeVertex(b, colorB, Vec2{}));
    out.push_back(MakeVertex(c, colorC, Vec2{}));
}

// ------------------------------------------------------------- font store ----
std::vector<Font*> gFonts;
Font* gDefaultFont = nullptr;
Font* gCurrentFont = nullptr;

} // namespace

void BeginMode2D(const Camera2D& camera) {
    gMode2DActive = true;
    gMode2DCamera = camera;
}

void EndMode2D() { gMode2DActive = false; }

bool IsMode2DActive() { return gMode2DActive; }
Camera2D GetMode2DCamera() { return gMode2DCamera; }

Rect GetScreenRect() {
    return Rect{0.0f, 0.0f, static_cast<float>(internal::GpuContext::Get().Width()),
                static_cast<float>(internal::GpuContext::Get().Height())};
}

// ---------------------------------------------------------------- shapes ----
namespace {

// Builds a fan of `segments` triangles approximating a filled arc / disc / ring
// between `innerRadius` and `outerRadius`. A full circle (a 360-degree sweep) is
// closed with a centre vertex so the fan does not leave a wedge missing.
void BuildArc(std::vector<Vertex>& out, Vec2 center, float innerRadius, float outerRadius,
              float startDegrees, float endDegrees, Color color) {
    const bool fullCircle = Abs(endDegrees - startDegrees) >= 359.999f;
    float sweep = endDegrees - startDegrees;
    if (fullCircle) sweep = 360.0f;

    // One segment per ~4 degrees, clamped so tiny arcs stay cheap and a full
    // circle stays smooth.
    int segments = static_cast<int>(Abs(sweep) / 4.0f);
    segments = segments < 3 ? 3 : (segments > 128 ? 128 : segments);

    const float startRadians = Radians(startDegrees);
    const float step = Radians(sweep) / static_cast<float>(segments);

    Vec2 prevOuter = center + Vec2{std::cos(startRadians), std::sin(startRadians)} * outerRadius;
    Vec2 prevInner = center + Vec2{std::cos(startRadians), std::sin(startRadians)} * innerRadius;

    for (int i = 1; i <= segments; ++i) {
        const float angle = startRadians + step * static_cast<float>(i);
        const Vec2 dir{std::cos(angle), std::sin(angle)};
        const Vec2 outer = center + dir * outerRadius;
        const Vec2 inner = center + dir * innerRadius;

        if (innerRadius <= 0.0f) {
            PushTriangle(out, center, prevOuter, outer, color, color, color);
        } else {
            PushTriangle(out, prevInner, prevOuter, outer, color, color, color);
            PushTriangle(out, prevInner, outer, inner, color, color, color);
        }
        prevOuter = outer;
        prevInner = inner;
    }
}

// Evenly spaced points on a circle, used by the polygon helpers.
std::vector<Vec2> PolygonPoints(Vec2 center, int sides, float radius, float rotationDegrees) {
    std::vector<Vec2> points;
    if (sides < 3) sides = 3;
    points.reserve(static_cast<size_t>(sides));
    const float step = TAU / static_cast<float>(sides);
    const float start = Radians(rotationDegrees);
    for (int i = 0; i < sides; ++i) {
        const float angle = start + step * static_cast<float>(i);
        points.push_back(center + Vec2{std::cos(angle), std::sin(angle)} * radius);
    }
    return points;
}

} // namespace

void DrawRectangle(float x, float y, float width, float height, Color color) {
    DrawRectangleV(Vec2{x, y}, Vec2{width, height}, color);
}

void DrawRectangleV(Vec2 position, Vec2 size, Color color) {
    if (size.x == 0.0f || size.y == 0.0f) return;

    const Vec2 topLeft = ToDrawSpace(position);
    const Vec2 topRight = ToDrawSpace(Vec2{position.x + size.x, position.y});
    const Vec2 bottomRight = ToDrawSpace(Vec2{position.x + size.x, position.y + size.y});
    const Vec2 bottomLeft = ToDrawSpace(Vec2{position.x, position.y + size.y});

    Vertex corners[4] = {
        MakeVertex(topLeft, color, Vec2{}),
        MakeVertex(topRight, color, Vec2{}),
        MakeVertex(bottomRight, color, Vec2{}),
        MakeVertex(bottomLeft, color, Vec2{}),
    };
    internal::Renderer::Get().DrawOverlayQuad(corners, nullptr, colors::White, false);
}

void DrawRectangleRec(Rect rect, Color color) {
    DrawRectangleV(rect.Position(), rect.Size(), color);
}

void DrawRectangleLines(float x, float y, float width, float height, Color color) {
    DrawRectangleLinesEx(Rect{x, y, width, height}, 1.0f, color);
}

void DrawRectangleLinesEx(Rect rect, float thickness, Color color) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;

    // Four thin filled rectangles. Corners overlap by design so no seam shows.
    DrawRectangleV(Vec2{rect.x, rect.y}, Vec2{rect.width, thickness}, color);
    DrawRectangleV(Vec2{rect.x, rect.y + rect.height - thickness}, Vec2{rect.width, thickness}, color);
    DrawRectangleV(Vec2{rect.x, rect.y + thickness}, Vec2{thickness, rect.height - thickness * 2.0f},
                   color);
    DrawRectangleV(Vec2{rect.x + rect.width - thickness, rect.y + thickness},
                   Vec2{thickness, rect.height - thickness * 2.0f}, color);
}

void DrawRectangleGradient(Rect rect, Color topLeft, Color bottomRight) {
    if (rect.width == 0.0f || rect.height == 0.0f) return;

    const Vec2 tl = ToDrawSpace(rect.Position());
    const Vec2 tr = ToDrawSpace(Vec2{rect.x + rect.width, rect.y});
    const Vec2 br = ToDrawSpace(Vec2{rect.x + rect.width, rect.y + rect.height});
    const Vec2 bl = ToDrawSpace(Vec2{rect.x, rect.y + rect.height});

    // The four corners carry their own colour, so the interpolated interior is a
    // bilinear blend of the two supplied colours.
    Vertex corners[4] = {
        MakeVertex(tl, topLeft, Vec2{}),
        MakeVertex(tr, Lerp(topLeft, bottomRight, 0.5f), Vec2{}),
        MakeVertex(br, bottomRight, Vec2{}),
        MakeVertex(bl, Lerp(topLeft, bottomRight, 0.5f), Vec2{}),
    };
    internal::Renderer::Get().DrawOverlayQuad(corners, nullptr, colors::White, false);
}

void DrawCircle(float centerX, float centerY, float radius, Color color) {
    DrawCircleV(Vec2{centerX, centerY}, radius, color);
}

void DrawCircleV(Vec2 center, float radius, Color color) {
    if (radius <= 0.0f) return;
    std::vector<Vertex> vertices;
    BuildArc(vertices, ToDrawSpace(center), 0.0f, radius, 0.0f, 360.0f, color);
    SubmitTriangles(vertices, nullptr, false);
}

void DrawCircleLines(Vec2 center, float radius, Color color) {
    if (radius <= 0.0f) return;

    const Vec2 screenCenter = ToDrawSpace(center);
    const int segments = radius < 8.0f ? 12 : 48;
    const float step = TAU / static_cast<float>(segments);
    Vec2 previous = screenCenter + Vec2{radius, 0.0f};
    for (int i = 1; i <= segments; ++i) {
        const float angle = step * static_cast<float>(i);
        const Vec2 current = screenCenter + Vec2{std::cos(angle), std::sin(angle)} * radius;
        internal::Renderer::Get().DrawScreenLine(previous, current, color, 1.0f);
        previous = current;
    }
}

void DrawCircleSector(Vec2 center, float radius, float startDegrees, float endDegrees, Color color) {
    if (radius <= 0.0f) return;
    std::vector<Vertex> vertices;
    BuildArc(vertices, ToDrawSpace(center), 0.0f, radius, startDegrees, endDegrees, color);
    SubmitTriangles(vertices, nullptr, false);
}

void DrawRing(Vec2 center, float innerRadius, float outerRadius, float startDegrees,
              float endDegrees, Color color) {
    if (outerRadius <= 0.0f || innerRadius >= outerRadius) return;
    std::vector<Vertex> vertices;
    BuildArc(vertices, ToDrawSpace(center), Max(innerRadius, 0.0f), outerRadius, startDegrees,
             endDegrees, color);
    SubmitTriangles(vertices, nullptr, false);
}

void DrawTriangle2D(Vec2 a, Vec2 b, Vec2 c, Color color) {
    std::vector<Vertex> vertices;
    PushTriangle(vertices, ToDrawSpace(a), ToDrawSpace(b), ToDrawSpace(c), color, color, color);
    SubmitTriangles(vertices, nullptr, false);
}

void DrawTriangleLines2D(Vec2 a, Vec2 b, Vec2 c, Color color) {
    internal::Renderer::Get().DrawScreenLine(ToDrawSpace(a), ToDrawSpace(b), color, 1.0f);
    internal::Renderer::Get().DrawScreenLine(ToDrawSpace(b), ToDrawSpace(c), color, 1.0f);
    internal::Renderer::Get().DrawScreenLine(ToDrawSpace(c), ToDrawSpace(a), color, 1.0f);
}

void DrawPolygon(Vec2 center, int sides, float radius, float rotationDegrees, Color color) {
    if (sides < 3 || radius <= 0.0f) return;

    const std::vector<Vec2> points = PolygonPoints(ToDrawSpace(center), sides, radius,
                                                   rotationDegrees);
    std::vector<Vertex> vertices;
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec2& next = points[(i + 1) % points.size()];
        PushTriangle(vertices, ToDrawSpace(center), points[i], next, color, color, color);
    }
    SubmitTriangles(vertices, nullptr, false);
}

void DrawPolygonLines(Vec2 center, int sides, float radius, float rotationDegrees, Color color) {
    if (sides < 3 || radius <= 0.0f) return;

    const std::vector<Vec2> points = PolygonPoints(ToDrawSpace(center), sides, radius,
                                                   rotationDegrees);
    for (size_t i = 0; i < points.size(); ++i) {
        internal::Renderer::Get().DrawScreenLine(points[i], points[(i + 1) % points.size()], color,
                                                 1.0f);
    }
}

void DrawPolygonV(const Vec2* points, int pointCount, Color color) {
    if (!points || pointCount < 3) return;

    std::vector<Vertex> vertices;
    const Vec2 first = ToDrawSpace(points[0]);
    for (int i = 1; i + 1 < pointCount; ++i) {
        PushTriangle(vertices, first, ToDrawSpace(points[i]), ToDrawSpace(points[i + 1]), color,
                     color, color);
    }
    SubmitTriangles(vertices, nullptr, false);
}

void DrawPolyline(const Vec2* points, int pointCount, float thickness, Color color) {
    if (!points || pointCount < 2) return;
    for (int i = 0; i + 1 < pointCount; ++i) {
        internal::Renderer::Get().DrawScreenLine(ToDrawSpace(points[i]), ToDrawSpace(points[i + 1]),
                                                 color, thickness);
    }
}

// ------------------------------------------------------------------ text ----
namespace internal {

// Builds the atlas texture for the built-in 5x7 font: a 16-column grid of 95
// glyphs, each cell padded to 6x8 so a neighbouring glyph can never bleed in even
// with linear filtering.
bool CreateBuiltinFont() {
    if (gDefaultFont) return true;

    constexpr int kColumns = 16;
    constexpr int kCellWidth = kFontGlyphWidth + 1;    // 1px padding
    constexpr int kCellHeight = kFontGlyphHeight + 1;
    constexpr int kGlyphCount = kFontLastChar - kFontFirstChar + 1;   // 95
    constexpr int kRows = (kGlyphCount + kColumns - 1) / kColumns;    // 6

    const int atlasWidth = kColumns * kCellWidth;
    const int atlasHeight = kRows * kCellHeight;

    std::vector<uint8_t> pixels(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);
    for (int glyph = 0; glyph < kGlyphCount; ++glyph) {
        const int cellX = (glyph % kColumns) * kCellWidth;
        const int cellY = (glyph / kColumns) * kCellHeight;
        for (int row = 0; row < kFontGlyphHeight; ++row) {
            const uint8_t bits = kFontRows[glyph * kFontGlyphHeight + row];
            for (int column = 0; column < kFontGlyphWidth; ++column) {
                // Bit 4 is the leftmost pixel; test it with a shift.
                const bool ink = (bits >> (kFontGlyphWidth - 1 - column)) & 1;
                if (!ink) continue;
                const int x = cellX + column;
                const int y = cellY + row;
                uint8_t* texel = &pixels[(static_cast<size_t>(y) * atlasWidth + x) * 4];
                texel[0] = 255;
                texel[1] = 255;
                texel[2] = 255;
                texel[3] = 255;
            }
        }
    }

    Texture* texture = LoadTextureFromMemory(pixels.data(), static_cast<uint32_t>(atlasWidth),
                                             static_cast<uint32_t>(atlasHeight), false);
    if (!texture) return false;

    auto* font = new Font();
    font->texture = texture;
    font->glyphWidth = kCellWidth;
    font->glyphHeight = kCellHeight;
    font->columns = kColumns;
    font->firstChar = kFontFirstChar;
    font->lastChar = kFontLastChar;
    font->spacing = 0.0f;
    font->lineHeight = static_cast<float>(kCellHeight);
    font->name = "(builtin)";
    font->isDefault = true;
    font->ownsTexture = true;

    gDefaultFont = font;
    gCurrentFont = font;
    gFonts.push_back(font);
    return true;
}

void DestroyBuiltinFont() {
    // UnloadFont() skips the default font, so free it directly here.
    if (!gDefaultFont) return;
    if (gDefaultFont->texture) UnloadTexture(gDefaultFont->texture);
    for (auto it = gFonts.begin(); it != gFonts.end(); ++it) {
        if (*it == gDefaultFont) {
            gFonts.erase(it);
            break;
        }
    }
    delete gDefaultFont;
    gDefaultFont = nullptr;
    gCurrentFont = nullptr;
}

} // namespace internal

Font* LoadFontFromTexture(Texture* texture, int glyphWidth, int glyphHeight, int columns) {
    if (!texture || glyphWidth <= 0 || glyphHeight <= 0 || columns <= 0) {
        LogError("LoadFontFromTexture: invalid arguments");
        return nullptr;
    }

    auto* font = new Font();
    font->texture = texture;
    font->glyphWidth = glyphWidth;
    font->glyphHeight = glyphHeight;
    font->columns = columns;
    font->firstChar = 32;
    font->lastChar = 126;
    font->spacing = 0.0f;
    font->lineHeight = static_cast<float>(glyphHeight);
    font->name = TextureName(texture);
    font->isDefault = false;
    font->ownsTexture = false;

    gFonts.push_back(font);
    if (!gCurrentFont) gCurrentFont = font;
    return font;
}

Font* LoadFontFromImage(const char* path, int glyphWidth, int glyphHeight, int columns) {
    Texture* texture = LoadTextureFromFile(path, false);
    if (!texture) return nullptr;

    Font* font = LoadFontFromTexture(texture, glyphWidth, glyphHeight, columns);
    if (font) font->ownsTexture = true;   // the font created this texture
    return font;
}

void UnloadFont(Font* font) {
    if (!font || font->isDefault) return;   // the built-in font is owned by the engine
    if (font->ownsTexture && font->texture) UnloadTexture(font->texture);
    for (auto it = gFonts.begin(); it != gFonts.end(); ++it) {
        if (*it == font) {
            gFonts.erase(it);
            break;
        }
    }
    if (gCurrentFont == font) gCurrentFont = gDefaultFont;
    delete font;
}

Font* GetDefaultFont() { return gDefaultFont; }

void SetFont(Font* font) { gCurrentFont = font ? font : gDefaultFont; }
Font* GetFont() { return gCurrentFont ? gCurrentFont : gDefaultFont; }

namespace {

// Source rectangle of one glyph in the font atlas, in texels.
Rect GlyphSource(const Font& font, char character) {
    int code = static_cast<unsigned char>(character);
    if (code < font.firstChar || code > font.lastChar) code = '?';
    const int index = code - font.firstChar;
    const int column = index % font.columns;
    const int row = index / font.columns;
    return Rect{static_cast<float>(column * font.glyphWidth),
                static_cast<float>(row * font.glyphHeight),
                static_cast<float>(font.glyphWidth), static_cast<float>(font.glyphHeight)};
}

Vec2 GlyphUv0(const Font& font, Rect source) {
    const float width = static_cast<float>(TextureWidth(font.texture));
    const float height = static_cast<float>(TextureHeight(font.texture));
    return Vec2{source.x / width, source.y / height};
}

Vec2 GlyphUv1(const Font& font, Rect source) {
    const float width = static_cast<float>(TextureWidth(font.texture));
    const float height = static_cast<float>(TextureHeight(font.texture));
    return Vec2{(source.x + source.width) / width, (source.y + source.height) / height};
}

// Advances one glyph, in pixels, for the given font size.
float GlyphAdvance(const Font& font, float scale) {
    return (static_cast<float>(font.glyphWidth) + font.spacing) * scale;
}

// Emits one glyph quad at `pen` (pixel space, top-left of the cell).
void EmitGlyph(std::vector<Vertex>& out, const Font& font, char character, Vec2 pen,
               float scale, Color color) {
    const Rect source = GlyphSource(font, character);
    const Vec2 uv0 = GlyphUv0(font, source);
    const Vec2 uv1 = GlyphUv1(font, source);

    const float width = static_cast<float>(font.glyphWidth) * scale;
    const float height = static_cast<float>(font.glyphHeight) * scale;

    const Vec2 tl{pen.x, pen.y};
    const Vec2 tr{pen.x + width, pen.y};
    const Vec2 br{pen.x + width, pen.y + height};
    const Vec2 bl{pen.x, pen.y + height};

    // Two triangles, each with its own uv corner.
    out.push_back(MakeVertex(tl, color, uv0));
    out.push_back(MakeVertex(tr, color, Vec2{uv1.x, uv0.y}));
    out.push_back(MakeVertex(br, color, uv1));
    out.push_back(MakeVertex(tl, color, uv0));
    out.push_back(MakeVertex(br, color, uv1));
    out.push_back(MakeVertex(bl, color, Vec2{uv0.x, uv1.y}));
}

// Builds the glyph quads for a whole string, starting at `origin` in pixel space.
std::vector<Vertex> BuildTextQuads(const Font& font, const char* text, Vec2 origin,
                                   float scale, Color color) {
    std::vector<Vertex> vertices;
    if (!text) return vertices;

    const float advance = GlyphAdvance(font, scale);
    const float lineStep = font.lineHeight * scale;
    Vec2 pen = origin;

    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const char character = *cursor;
        if (character == '\n') {
            pen.x = origin.x;
            pen.y += lineStep;
            continue;
        }
        if (character != ' ') EmitGlyph(vertices, font, character, pen, scale, color);
        pen.x += advance;
    }
    return vertices;
}

// Measures a string the same way BuildTextQuads advances it.
Vec2 MeasureString(const Font& font, const char* text, float scale) {
    if (!text) return Vec2{};

    const float advance = GlyphAdvance(font, scale);
    const float lineStep = font.lineHeight * scale;
    float widest = 0.0f;
    float lineWidth = 0.0f;
    float lines = 1.0f;

    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            widest = Max(widest, lineWidth);
            lineWidth = 0.0f;
            lines += 1.0f;
            continue;
        }
        lineWidth += advance;
    }
    widest = Max(widest, lineWidth);
    // Trailing advance belongs to the space after the last glyph, so trim it.
    widest = Max(0.0f, widest - advance);
    return Vec2{widest, lines * lineStep};
}

} // namespace

void DrawText(const char* text, int x, int y, float fontSize, Color color) {
    DrawTextV(text, Vec2{static_cast<float>(x), static_cast<float>(y)}, fontSize, color);
}

void DrawTextV(const char* text, Vec2 position, float fontSize, Color color) {
    Font* font = GetFont();
    if (!font || !text) return;

    // Font size is expressed in "font pixels": the 5x7 built-in font is 1:1 at
    // fontSize 7, so a scale of fontSize / glyphHeight keeps the glyphs crisp.
    const float scale = Max(fontSize, 1.0f) / static_cast<float>(font->glyphHeight);
    const Vec2 origin = ToDrawSpace(position);

    std::vector<Vertex> vertices = BuildTextQuads(*font, text, origin, scale, color);
    if (vertices.empty()) return;
    internal::Renderer::Get().DrawOverlayTriangles(vertices.data(),
                                                   static_cast<uint32_t>(vertices.size()),
                                                   font->texture, colors::White, true,
                                                   internal::TextShader());
}

void DrawTextCentered(const char* text, float centerX, float centerY, float fontSize, Color color) {
    Font* font = GetFont();
    if (!font || !text) return;

    const float scale = Max(fontSize, 1.0f) / static_cast<float>(font->glyphHeight);
    const Vec2 size = MeasureString(*font, text, scale);
    DrawTextV(text, Vec2{centerX - size.x * 0.5f, centerY - size.y * 0.5f}, fontSize, color);
}

void DrawTextWithBackground(const char* text, int x, int y, float fontSize, Color textColor,
                            Color backgroundColor) {
    const Vec2 size = MeasureText(text, fontSize);
    const float padding = Max(2.0f, fontSize * 0.25f);
    DrawRectangleV(Vec2{static_cast<float>(x) - padding, static_cast<float>(y) - padding},
                   Vec2{size.x + padding * 2.0f, size.y + padding * 2.0f}, backgroundColor);
    DrawText(text, x, y, fontSize, textColor);
}

Vec2 MeasureText(const char* text, float fontSize) {
    Font* font = GetFont();
    if (!font || !text) return Vec2{};
    const float scale = Max(fontSize, 1.0f) / static_cast<float>(font->glyphHeight);
    return MeasureString(*font, text, scale);
}

float MeasureTextWidth(const char* text, float fontSize) {
    return MeasureText(text, fontSize).x;
}

void DrawTextFormat(int x, int y, float fontSize, Color color, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    DrawText(buffer, x, y, fontSize, color);
}

// -------------------------------------------------------------- textures ----
void DrawTexture(Texture* texture, float x, float y, Color tint) {
    DrawTextureV(texture, Vec2{x, y}, tint);
}

void DrawTextureV(Texture* texture, Vec2 position, Color tint) {
    if (!texture) return;
    DrawTexturePro(texture,
                   Rect{0.0f, 0.0f, static_cast<float>(TextureWidth(texture)),
                        static_cast<float>(TextureHeight(texture))},
                   Rect{position.x, position.y, static_cast<float>(TextureWidth(texture)),
                        static_cast<float>(TextureHeight(texture))},
                   Vec2{}, 0.0f, tint);
}

void DrawTextureEx(Texture* texture, Vec2 position, float rotationDegrees, float scale, Color tint) {
    if (!texture) return;
    const float width = static_cast<float>(TextureWidth(texture));
    const float height = static_cast<float>(TextureHeight(texture));
    // Rotate about the top-left corner and scale the destination size, which is
    // what raylib's DrawTextureEx does.
    DrawTexturePro(texture, Rect{0.0f, 0.0f, width, height},
                   Rect{position.x, position.y, width * scale, height * scale}, Vec2{},
                   rotationDegrees, tint);
}

void DrawTextureRec(Texture* texture, Rect source, Vec2 position, Color tint) {
    if (!texture) return;
    DrawTexturePro(texture, source, Rect{position.x, position.y, source.width, source.height},
                   Vec2{}, 0.0f, tint);
}

void DrawTexturePro(Texture* texture, Rect source, Rect dest, Vec2 origin, float rotationDegrees,
                    Color tint) {
    if (!texture || dest.width == 0.0f || dest.height == 0.0f) return;

    // Normalized source uvs.
    const float textureWidth = static_cast<float>(TextureWidth(texture));
    const float textureHeight = static_cast<float>(TextureHeight(texture));
    const Vec2 uv0{source.x / textureWidth, source.y / textureHeight};
    const Vec2 uv1{(source.x + source.width) / textureWidth,
                   (source.y + source.height) / textureHeight};

    // `origin` is given as a fraction of the destination size.
    const Vec2 pivot{dest.x + dest.width * origin.x, dest.y + dest.height * origin.y};

    // Corner offsets from the pivot, rotated about it.
    const float radians = Radians(rotationDegrees);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const Vec2 local[4] = {
        Vec2{-dest.width * origin.x, -dest.height * origin.y},
        Vec2{dest.width * (1.0f - origin.x), -dest.height * origin.y},
        Vec2{dest.width * (1.0f - origin.x), dest.height * (1.0f - origin.y)},
        Vec2{-dest.width * origin.x, dest.height * (1.0f - origin.y)},
    };

    Vec2 corners[4];
    for (int i = 0; i < 4; ++i) {
        const Vec2 rotated{local[i].x * c - local[i].y * s, local[i].x * s + local[i].y * c};
        corners[i] = ToDrawSpace(Vec2{pivot.x + rotated.x, pivot.y + rotated.y});
    }

    Vertex quad[4] = {
        MakeVertex(corners[0], tint, uv0),
        MakeVertex(corners[1], tint, Vec2{uv1.x, uv0.y}),
        MakeVertex(corners[2], tint, uv1),
        MakeVertex(corners[3], tint, Vec2{uv0.x, uv1.y}),
    };
    internal::Renderer::Get().DrawOverlayQuad(quad, texture, colors::White, true);
}

// --------------------------------------------------------------- 3D text ----
void DrawText3D(const char* text, Vec3 position, float worldSize, Color color) {
    Font* font = GetFont();
    if (!font || !text) return;

    const float scale = Max(worldSize, 0.0001f);
    const float advance = GlyphAdvance(*font, scale);
    const float lineStep = font->lineHeight * scale;

    // Billboard the text so it always faces the camera: build the quad in view
    // space and transform by the inverse view, which is exactly a screen-aligned
    // quad anchored at `position`.
    const Camera3D& camera = internal::Renderer::Get().Camera();
    const Vec3 forward = camera.Forward();
    Vec3 right = Normalize(Cross(Vec3{0.0f, 1.0f, 0.0f}, forward));
    if (LengthSq(right) < 1e-6f) right = Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 up = Cross(forward, right);

    Vec3 pen = position;
    Vec3 lineStart = position;

    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            pen = lineStart - up * lineStep;
            lineStart = pen;
            continue;
        }
        if (*cursor != ' ') {
            const Rect source = GlyphSource(*font, *cursor);
            const Vec2 uv0 = GlyphUv0(*font, source);
            const Vec2 uv1 = GlyphUv1(*font, source);
            const float width = static_cast<float>(font->glyphWidth) * scale;
            const float height = static_cast<float>(font->glyphHeight) * scale;

            const Vec3 tl = pen;
            const Vec3 tr = pen + right * width;
            const Vec3 br = pen + right * width - up * height;
            const Vec3 bl = pen - up * height;

            Vertex quad[6];
            const Vec3 positions[6] = {tl, tr, br, tl, br, bl};
            const Vec2 uvs[6] = {uv0, Vec2{uv1.x, uv0.y}, uv1, uv0, uv1, Vec2{uv0.x, uv1.y}};
            for (int i = 0; i < 6; ++i) {
                quad[i].position = positions[i];
                quad[i].normal = -forward;
                quad[i].uv = uvs[i];
                quad[i].color = color;
            }
            internal::Renderer::Get().DrawDynamicMesh(quad, 6, nullptr, 0, font->texture,
                                                      BlendMode::AlphaBlend, CullMode::None,
                                                      DepthTest::LessEqual);
        }
        pen = pen + right * advance;
    }
}

} // namespace a3d
