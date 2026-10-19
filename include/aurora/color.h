// Aurora3D — color.h
// Small RGBA color type plus a raylib-flavoured palette of named colors.
#pragma once

#include <cstdint>

namespace a3d {

struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    // 0-255 constructor, matching how colors are usually written by hand.
    static constexpr Color FromBytes(int r_, int g_, int b_, int a_ = 255) {
        return Color(static_cast<float>(r_) / 255.0f, static_cast<float>(g_) / 255.0f,
                     static_cast<float>(b_) / 255.0f, static_cast<float>(a_) / 255.0f);
    }

    static constexpr Color Hex(uint32_t rgba) {
        return FromBytes(static_cast<int>((rgba >> 24) & 0xFF), static_cast<int>((rgba >> 16) & 0xFF),
                         static_cast<int>((rgba >> 8) & 0xFF), static_cast<int>(rgba & 0xFF));
    }

    Color WithAlpha(float alpha) const { return Color(r, g, b, alpha); }
    Color Scaled(float s) const { return Color(r * s, g * s, b * s, a); }
};

inline constexpr Color operator*(Color c, float s) { return Color(c.r * s, c.g * s, c.b * s, c.a); }
inline constexpr Color operator*(float s, Color c) { return c * s; }

inline Color Lerp(Color a, Color b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                 a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

namespace colors {
constexpr Color White      {1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Black      {0.0f, 0.0f, 0.0f, 1.0f};
constexpr Color Clear      {0.0f, 0.0f, 0.0f, 0.0f};
constexpr Color Red        {1.0f, 0.0f, 0.0f, 1.0f};
constexpr Color Green      {0.0f, 1.0f, 0.0f, 1.0f};
constexpr Color Blue       {0.0f, 0.0f, 1.0f, 1.0f};
constexpr Color Yellow     {1.0f, 1.0f, 0.0f, 1.0f};
constexpr Color Magenta    {1.0f, 0.0f, 1.0f, 1.0f};
constexpr Color Cyan       {0.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Orange     {1.0f, 0.647f, 0.0f, 1.0f};
constexpr Color Purple     {0.5f, 0.0f, 0.5f, 1.0f};
constexpr Color Pink       {1.0f, 0.753f, 0.796f, 1.0f};
constexpr Color Gray       {0.5f, 0.5f, 0.5f, 1.0f};
constexpr Color DarkGray   {0.25f, 0.25f, 0.25f, 1.0f};
constexpr Color LightGray  {0.75f, 0.75f, 0.75f, 1.0f};
constexpr Color SkyBlue    {0.529f, 0.808f, 0.922f, 1.0f};
constexpr Color DarkBlue   {0.0f, 0.0f, 0.545f, 1.0f};
constexpr Color Lime       {0.0f, 1.0f, 0.0f, 1.0f};
constexpr Color Gold       {1.0f, 0.843f, 0.0f, 1.0f};
constexpr Color Violet     {0.933f, 0.510f, 0.933f, 1.0f};
constexpr Color Teal       {0.0f, 0.5f, 0.5f, 1.0f};
constexpr Color Brown      {0.588f, 0.294f, 0.0f, 1.0f};
constexpr Color DarkGreen  {0.0f, 0.392f, 0.0f, 1.0f};
constexpr Color Coral      {1.0f, 0.498f, 0.314f, 1.0f};
constexpr Color SlateGray  {0.439f, 0.502f, 0.565f, 1.0f};
} // namespace colors

} // namespace a3d
