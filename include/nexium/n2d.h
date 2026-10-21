// NexiumEngine — n2d.h
// `n2d` is the 2D spelling of the same library.
//
// The engine has one namespace, `n3d`, because it has one API: the 2D calls
// (DrawRectangle, DrawTexture, BeginMode2D, ...) and the 3D calls (DrawCube,
// BeginScene, ...) live side by side and a game mixes them freely. Rather than
// duplicate two thousand symbols under a second name — which would drift the
// moment either side changed — `n2d` is an alias for `n3d`.
//
//   #include <nexium/n2d.h>
//
//   n2d::Storage Sprites = {{"hero", "assets/hero.png"}};
//   n2d::BeginMode2D(camera2d);
//   n2d::DrawTexture(Sprites["hero"].AsTexture(), 10, 10);
//   n2d::EndMode2D();
//
// Everything declared in `n3d` — including a drawer built with
// N3D_DEFINE_STORAGE inside `namespace n3d` — is reachable as `n2d::`.
//
// The convention, then, is about intent rather than capability: write `n2d::` in
// a 2D game so a reader knows the camera is 2D and the world is a screen, and
// `n3d::` in a 3D one. Both compile to exactly the same code.
#pragma once

#include "nexium/NexiumEngine.h"

namespace n2d = n3d;
