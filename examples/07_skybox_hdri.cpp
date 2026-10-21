// NexiumEngine — example: an environment sky (HDRI).
//
// The engine now has a first-class environment path: a float texture format, a
// Radiance (.hdr) loader, and a built-in sky shader. So this example does the real
// thing rather than an 8-bit imitation.
//
// It does three things:
//
//   1. Builds an equirectangular radiance map in memory — a full-sphere image,
//      2:1, with a gradient, clouds and a sun that is *brighter than white*.
//      That last part is the whole point of HDR: the sun is stored as a value of
//      ~24, so it can light the scene instead of merely being drawn.
//   2. Hands it to the engine with SetSky() and calls DrawSky() inside the scene.
//      The engine draws it as a cube around the camera; the application no longer
//      needs its own sky shader.
//   3. Calls UpdateLightingFromSky(), which reads the map's average colour and
//      brightest direction and drives the ambient and the directional light.
//
// To use a real photograph, replace the generated map with
// n3d::LoadHDRFromFile("sky.hdr") — that is the whole change.
#include <nexium/NexiumEngine.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

// ------------------------------------------------------------------ sky art --

// Sun position in the world. Azimuth 0 looks along +Z, and the elevation is
// measured up from the horizon, so this is a low afternoon sun.
const float kSunAzimuth   = n3d::Radians(40.0f);
const float kSunElevation = n3d::Radians(18.0f);

constexpr int kMapWidth  = 1024;   // 2:1, the standard equirectangular ratio
constexpr int kMapHeight = 512;

// How much brighter than white the sun is stored. An 8-bit texture cannot hold
// this at all; a float one holds it exactly, which is what lets the sky light the
// scene instead of just decorating it.
constexpr float kSunIntensity = 24.0f;

// Direction from the origin toward the sun, matching the azimuth/elevation above.
n3d::Vec3 SunDirection() {
    const float ce = std::cos(kSunElevation);
    return n3d::Vec3{ce * std::sin(kSunAzimuth), std::sin(kSunElevation),
                     ce * std::cos(kSunAzimuth)};
}

// Integer hash and value noise. Cheap and good enough for cloud shapes; the
// point is a lumpy sky, not weather simulation.
float Hash(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h ^ (h >> 16)) / 4294967295.0f;
}

float ValueNoise(float x, float y) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi);
    const float fy = y - static_cast<float>(yi);
    // Smoothstep the interpolant so the lattice does not show through.
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);

    const float a = Hash(xi, yi);
    const float b = Hash(xi + 1, yi);
    const float c = Hash(xi, yi + 1);
    const float d = Hash(xi + 1, yi + 1);
    return n3d::Lerp(n3d::Lerp(a, b, ux), n3d::Lerp(c, d, ux), uy);
}

float Fbm(float x, float y) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    for (int octave = 0; octave < 5; ++octave) {
        sum += ValueNoise(x, y) * amplitude;
        x *= 2.03f;   // a non-integer step keeps the octaves from lining up
        y *= 2.03f;
        amplitude *= 0.5f;
    }
    return sum;
}

// Renders the whole sphere of sky into a tightly packed float RGBA buffer.
std::vector<float> BuildEnvironmentPixels() {
    std::vector<float> pixels(static_cast<size_t>(kMapWidth) * kMapHeight * 4u);

    const n3d::Vec3 sun = SunDirection();
    const float sunCos = std::cos(n3d::Radians(2.2f));    // angular radius of the disc
    const float glowCos = std::cos(n3d::Radians(28.0f));  // reach of the halo

    // Three anchors for the vertical gradient, and the ground under the horizon.
    const n3d::Color zenith  = n3d::Color::FromBytes(38, 76, 150);
    const n3d::Color horizon = n3d::Color::FromBytes(232, 176, 128);
    const n3d::Color ground  = n3d::Color::FromBytes(30, 28, 34);

    for (int y = 0; y < kMapHeight; ++y) {
        // v runs 0 at the top of the image (zenith) to 1 at the bottom (nadir).
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kMapHeight);
        const float elevation = (0.5f - v) * n3d::PI;   // +90 deg at the top

        for (int x = 0; x < kMapWidth; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kMapWidth);
            const float azimuth = (u - 0.5f) * n3d::TAU;

            // The direction this texel represents. This is the exact inverse of
            // the mapping the engine's sky shader uses, which is what keeps the sun
            // in the image aligned with the directional light.
            const float ce = std::cos(elevation);
            const n3d::Vec3 dir{ce * std::sin(azimuth), std::sin(elevation),
                                ce * std::cos(azimuth)};

            // Vertical gradient. The horizon band is squeezed so the warm colour
            // stays low in the sky instead of washing out the whole dome.
            n3d::Color color;
            if (elevation >= 0.0f) {
                const float t = std::pow(std::sin(elevation), 0.45f);
                color = n3d::Lerp(horizon, zenith, t);
            } else {
                const float t = std::pow(-std::sin(elevation), 0.35f);
                color = n3d::Lerp(horizon, ground, t);
            }

            // Clouds, only above the horizon, thinning out as they approach it.
            if (elevation > 0.0f) {
                // Sample the noise on the sphere so the clouds do not pinch at the
                // pole: scale the direction, not the raw UV.
                const float cloud = Fbm(dir.x * 3.1f + 11.0f, dir.z * 3.1f + 7.0f);
                const float cover = n3d::Clamp((cloud - 0.48f) * 3.2f, 0.0f, 1.0f);
                const float fade = n3d::Clamp(elevation / n3d::Radians(12.0f), 0.0f, 1.0f);
                const float density = cover * fade;

                // Clouds catch the sun, so tint them warmer near it.
                const float towardSun = n3d::Clamp(n3d::Dot(dir, sun), 0.0f, 1.0f);
                const n3d::Color lit = n3d::Lerp(n3d::colors::White, horizon, towardSun * 0.7f);
                color = n3d::Lerp(color, lit, density * 0.85f);
            }

            // Sun halo and disc. The disc is written as an HDR value: no clamp to
            // 1, so the stored pixel really is kSunIntensity times white.
            float r = color.r, g = color.g, b = color.b;
            const float alignment = n3d::Dot(dir, sun);
            if (alignment > glowCos) {
                const float t = (alignment - glowCos) / (1.0f - glowCos);
                const float glow = n3d::Clamp(std::pow(t, 2.5f), 0.0f, 1.0f) * 0.55f;
                r = n3d::Lerp(r, 1.0f, glow);
                g = n3d::Lerp(g, 0.886f, glow);
                b = n3d::Lerp(b, 0.706f, glow);
            }
            if (alignment > sunCos) {
                r = 1.0f * kSunIntensity;
                g = 0.973f * kSunIntensity;
                b = 0.910f * kSunIntensity;
            }

            const size_t index = (static_cast<size_t>(y) * kMapWidth + x) * 4u;
            pixels[index + 0] = r;
            pixels[index + 1] = g;
            pixels[index + 2] = b;
            pixels[index + 3] = 1.0f;
        }
    }

    return pixels;
}

// -------------------------------------------------------------------- scene --

n3d::OrbitCamera gOrbit;
bool gShowSky = true;

void DrawScene() {
    n3d::DrawGrid(30.0f, 30, n3d::colors::DarkGray);

    // Objects with different roughness, so the sky's effect on the shading is
    // visible: a rough ball next to a polished one.
    n3d::DrawSphere(n3d::Vec3{-3.0f, 1.0f, 0.0f}, 1.0f, n3d::colors::Coral);
    n3d::DrawMeshMaterial(n3d::GetSphereMesh(1.0f), n3d::Vec3{0.0f, 1.0f, 0.0f}, nullptr,
                          n3d::Vec3{}, n3d::Vec3{1, 1, 1}, n3d::colors::SlateGray, 1.0f, 0.15f);
    n3d::DrawCube(n3d::Vec3{3.0f, 1.0f, 0.0f}, n3d::Vec3{2, 2, 2}, n3d::colors::Teal);
    n3d::DrawCylinder(n3d::Vec3{0.0f, 1.5f, 4.0f}, 0.6f, 3.0f, n3d::colors::Gold);
}

} // namespace

int main() {
    n3d::Config config;
    config.title = "NexiumEngine — Sky Environment (HDRI)";
    if (!n3d::Init(config)) return 1;

    // 1. Build the environment map and upload it as a float texture. This is the
    //    step that could not be done before: the sun is stored at 24x white.
    const std::vector<float> pixels = BuildEnvironmentPixels();
    n3d::Texture* environment = n3d::LoadTextureFromFloats(pixels.data(), kMapWidth, kMapHeight,
                                                           true);
    if (!environment) {
        n3d::LogError("could not create the environment texture");
        n3d::Close();
        return 1;
    }

    // 2. Hand it to the engine. From here DrawSky() draws it, with no shader of the
    //    application's own and no manual cull-mode juggling.
    n3d::SetSky(environment);

    // 3. Make the sky the light. UpdateLightingFromSky() reads the map's average
    //    colour into the ambient and its brightest direction into the directional
    //    light — the cheap version of image-based lighting.
    if (!n3d::UpdateLightingFromSky(0.55f)) {
        n3d::LogWarn("the environment map produced no lighting summary; using the defaults");
    }
    n3d::SetShadowsEnabled(true);

    gOrbit.distance = 12.0f;
    gOrbit.pitch = n3d::Radians(12.0f);

    while (!n3d::WindowShouldClose()) {
        n3d::UpdateOrbitCamera(gOrbit);
        if (n3d::IsKeyPressed(n3d::Key::Space)) gShowSky = !gShowSky;

        n3d::BeginScene(gOrbit.ToCamera(), n3d::Color::FromBytes(232, 176, 128));

        // Draw the sky first: everything after it is occluded correctly, because
        // the sky writes depth at the far edge of the scene.
        if (gShowSky) n3d::DrawSky();
        DrawScene();

        n3d::DrawTextFormat(20, 20, 20.0f, n3d::colors::White,
                            "Sky environment: %s   (space toggles)", gShowSky ? "on" : "off");
        n3d::DrawTextFormat(20, 46, 16.0f, n3d::colors::LightGray,
                            "%dx%d equirectangular, RGBA16F   FPS %.0f", kMapWidth, kMapHeight,
                            static_cast<double>(n3d::GetFps()));
        n3d::EndScene();
    }

    // The sky must be cleared before the texture it points at is freed, or the
    // renderer would hold a dangling pointer for the next frame.
    n3d::SetSky(nullptr);
    n3d::UnloadTexture(environment);
    n3d::Close();
    return 0;
}
