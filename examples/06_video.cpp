// Aurora3D — example: video playback.
//
// Loads a video and draws it full-screen as a textured quad. Playback is
// best-effort: if the file is missing, or Media Foundation cannot decode it, the
// example keeps running and just shows a hint on screen.
//
// Space pauses/resumes, left/right seek, V toggles looping.
#include <aurora/aurora.h>

namespace {

const char* kPath = "VideoTeste.mp4";

} // namespace

int main() {
    a3d::Config config;
    config.title = "Aurora3D — Video";
    config.width = 1280;
    config.height = 720;
    if (!a3d::Init(config)) return 1;

    a3d::Video* video = a3d::LoadVideo(kPath);
    if (video) {
        a3d::SetVideoLooping(video, true);
        a3d::PlayVideo(video);
    }

    while (!a3d::WindowShouldClose()) {
        if (video) {
            if (a3d::IsKeyPressed(a3d::Key::Space)) {
                if (a3d::IsVideoPlaying(video)) a3d::PauseVideo(video);
                else a3d::ResumeVideo(video);
            }
            if (a3d::IsKeyPressed(a3d::Key::V)) a3d::SetVideoLooping(video, !a3d::IsVideoLooping(video));
        }

        a3d::BeginSceneOverlay(a3d::Camera3D{});
        a3d::DrawRectangle(0.0f, 0.0f, static_cast<float>(a3d::GetScreenWidth()),
                           static_cast<float>(a3d::GetScreenHeight()), a3d::colors::Black);

        // Fit the frame inside the window while preserving its aspect ratio.
        a3d::Texture* frame = a3d::GetVideoTexture(video);
        if (video && a3d::IsVideoReady(video) && frame) {
            const float screenW = static_cast<float>(a3d::GetScreenWidth());
            const float screenH = static_cast<float>(a3d::GetScreenHeight());
            const float videoW = static_cast<float>(a3d::GetVideoWidth(video));
            const float videoH = static_cast<float>(a3d::GetVideoHeight(video));
            const float scale = a3d::Min(screenW / videoW, screenH / videoH);
            const float drawW = videoW * scale;
            const float drawH = videoH * scale;
            a3d::DrawTexturePro(frame, a3d::Rect{0, 0, videoW, videoH},
                                a3d::Rect{(screenW - drawW) * 0.5f, (screenH - drawH) * 0.5f, drawW,
                                          drawH},
                                a3d::Vec2{0.0f, 0.0f}, 0.0f, a3d::colors::White);
            a3d::DrawTextFormat(16, 16, 20.0f, a3d::colors::White, "%s  %.1f / %.1f s",
                                a3d::IsVideoPlaying(video) ? "playing" : "paused",
                                static_cast<double>(a3d::GetVideoTime(video)),
                                static_cast<double>(a3d::GetVideoLength(video)));
        } else {
            a3d::DrawText("no video — put a clip at assets/clip.mp4", 16, 16, 20.0f,
                          a3d::colors::Yellow);
        }

        a3d::EndScene();
    }

    if (video) a3d::UnloadVideo(video);
    a3d::Close();
    return 0;
}
