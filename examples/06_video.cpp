// NexiumEngine — example: video playback.
//
// Loads a video and draws it full-screen as a textured quad. Playback is
// best-effort: if the file is missing, or Media Foundation cannot decode it, the
// example keeps running and just shows a hint on screen.
//
// Space pauses/resumes, left/right seek, V toggles looping.
#include <nexium/NexiumEngine.h>

namespace {

const char* kPath = "assets/clip.mp4";

} // namespace

int main() {
    n3d::Config config;
    config.title = "NexiumEngine — Video";
    config.width = 1280;
    config.height = 720;
    if (!n3d::Init(config)) return 1;

    n3d::Video* video = n3d::LoadVideo(kPath);
    if (video) {
        n3d::SetVideoLooping(video, true);
        n3d::PlayVideo(video);
    }

    while (!n3d::WindowShouldClose()) {
        if (video) {
            if (n3d::IsKeyPressed(n3d::Key::Space)) {
                if (n3d::IsVideoPlaying(video)) n3d::PauseVideo(video);
                else n3d::ResumeVideo(video);
            }
            if (n3d::IsKeyPressed(n3d::Key::V)) n3d::SetVideoLooping(video, !n3d::IsVideoLooping(video));
        }

        n3d::BeginSceneOverlay(n3d::Camera3D{});
        n3d::DrawRectangle(0.0f, 0.0f, static_cast<float>(n3d::GetScreenWidth()),
                           static_cast<float>(n3d::GetScreenHeight()), n3d::colors::Black);

        // Fit the frame inside the window while preserving its aspect ratio.
        n3d::Texture* frame = n3d::GetVideoTexture(video);
        if (video && n3d::IsVideoReady(video) && frame) {
            const float screenW = static_cast<float>(n3d::GetScreenWidth());
            const float screenH = static_cast<float>(n3d::GetScreenHeight());
            const float videoW = static_cast<float>(n3d::GetVideoWidth(video));
            const float videoH = static_cast<float>(n3d::GetVideoHeight(video));
            const float scale = n3d::Min(screenW / videoW, screenH / videoH);
            const float drawW = videoW * scale;
            const float drawH = videoH * scale;
            n3d::DrawTexturePro(frame, n3d::Rect{0, 0, videoW, videoH},
                                n3d::Rect{(screenW - drawW) * 0.5f, (screenH - drawH) * 0.5f, drawW,
                                          drawH},
                                n3d::Vec2{0.0f, 0.0f}, 0.0f, n3d::colors::White);
            n3d::DrawTextFormat(16, 16, 20.0f, n3d::colors::White, "%s  %.1f / %.1f s",
                                n3d::IsVideoPlaying(video) ? "playing" : "paused",
                                static_cast<double>(n3d::GetVideoTime(video)),
                                static_cast<double>(n3d::GetVideoLength(video)));
        } else {
            n3d::DrawText("no video — put a clip at assets/clip.mp4", 16, 16, 20.0f,
                          n3d::colors::Yellow);
        }

        n3d::EndScene();
    }

    if (video) n3d::UnloadVideo(video);
    n3d::Close();
    return 0;
}
