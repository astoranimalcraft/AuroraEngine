# AGENTS.md — NexiumEngine

## What this repository is

NexiumEngine is a small, self-contained 3D library/engine for C++20 built directly on
**DirectX 12** with first-class **HLSL** support. Its programming model is
immediate-mode and deliberately close in *spirit* to raylib (short verb-like
functions, no boilerplate), but the API surface, naming and internals are its own.

## Environment notes (important for agents)

- The dev sandbox is **Linux**. DirectX 12 is Windows-only, so programs **cannot be
  executed here**. They *can* be fully type-checked and linked by cross-compiling.
- Toolchain installed for verification:
  - `x86_64-w64-mingw32-g++` (MinGW-w64, GCC 14) — has `d3d12.h`, `dxgi1_6.h`,
    `d3dcompiler.h`, `wincodec.h`, `directxmath.h` and the matching import libs
    (`-ld3d12 -ldxgi -ld3dcompiler -lwindowscodecs -lole32 -luuid -luser32`).
- **Always verify a change** with the cross-compile script:
  ```sh
  ./scripts/verify.sh          # syntax + link every source, example and the API check
  ./scripts/verify.sh --warnings   # same, with -Werror
  ```
- The math/color headers are deliberately platform-independent. They are covered by
  a native test that **does** run on Linux:
  ```sh
  ./scripts/test-math.sh
  ```
- The CMake build can also be exercised on Linux, which catches CMakeLists mistakes
  that the shell script cannot see:
  ```sh
  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
  cmake --build build
  ```
  Keep it working. `verify.sh` does **not** run CMake, so a broken `CMakeLists.txt`
  goes unnoticed unless you build it.
- `tests/test_public_api.cpp` is a compile-only check of the API surface documented
  in `README.md`. It never runs; it exists so a renamed or removed function breaks
  the build instead of leaving the documentation wrong.
- **Running the binary catches what compiling cannot.** Wine plus a virtual display
  executes the real Win32 path, so window/message-pump bugs surface here. Cross-compile
  statically, or the MinGW runtime DLLs are missing:
  ```sh
  sudo apt-get install -y wine64 xvfb
  Xvfb :99 -screen 0 1280x720x24 &
  x86_64-w64-mingw32-g++ -std=c++20 -static -static-libgcc -static-libstdc++ \
      -Iinclude -Isrc examples/01_hello_cube.cpp src/*.cpp src/internal/*.cpp \
      -ld3d12 -ldxgi -ld3dcompiler -lwindowscodecs -lole32 -luuid -luser32 -lgdi32 -lshcore \
      -o /tmp/n3d.exe
  WINEPREFIX=$HOME/wineprefix XDG_RUNTIME_DIR=$HOME/xdgr DISPLAY=:99 wine /tmp/n3d.exe
  ```
  Initialize the prefix once with `WINEARCH=win64 wineboot -i`, and run under `DISPLAY=:99`:
  with `DISPLAY` unset, `CreateWindowEx` fails for lack of an X server, which looks exactly
  like a library bug.

  To exercise the **whole** pipeline (device, root signature, PSOs, draws, present) and not
  just the Win32 layer, install vkd3d and a software Vulkan driver, then force the software
  ICD. Wine's D3D12 implementation is vkd3d, which translates to Vulkan; without it
  `D3D12CreateDevice` fails with `0x80004005`:
  ```sh
  sudo apt-get install -y libvkd3d1 libvkd3d-shader1 mesa-vulkan-drivers libvulkan1 vulkan-tools
  VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json wine /tmp/n3d.exe   # lavapipe (CPU)
  ```
  With that in place the adapter reports as a real GPU and the engine reaches
  "NexiumEngine ready". vkd3d is more permissive than the Windows runtime, though: it tolerates
  some invalid calls that Windows rejects with `DXGI_ERROR_INVALID_CALL`, so a clean Wine run
  is necessary but not sufficient. Confirm anything version- or flag-sensitive against the
  DXGI/D3D12 documentation as well. The D3D12 debug layer is unavailable under Wine, so
  validation-layer-only messages do not appear here.
- **XAudio2 does not survive Wine here.** `XAudio2Create` and `CreateMasteringVoice`
  both succeed, then Wine's audio mixer thread faults a few hundred ms later (ALSA
  cannot open a card in this container). The fault is inside Wine, not the engine: a
  ~15-line program that only calls `XAudio2Create`, `CreateMasteringVoice`, `SetVolume`
  and `Sleep(3000)` crashes identically, and the crash is asynchronous, so it lands in
  whatever code is running at the time. Do not chase this as an NexiumEngine bug — test the
  audio path by cross-compiling only, or on real Windows.

## Gotchas found the hard way

- A D3D12 index buffer must be transitioned to `D3D12_RESOURCE_STATE_INDEX_BUFFER`,
  **not** `VERTEX_AND_CONSTANT_BUFFER`. The wrong state does not fail validation
  loudly, it just misbehaves.
- `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` makes swap-chain creation *fail* on hardware
  that does not support it, and tearing is only legal with `Present(0, ...)`. Probe
  with `IDXGIFactory5::CheckFeatureSupport` and fall back to vsync.
- `ResizeBuffers` must be passed the **same** flags the swap chain was created with.
  The DXGI docs state it cannot add or remove `ALLOW_TEARING`; passing it on a chain
  created without it returns `DXGI_ERROR_INVALID_CALL` on every `WM_SIZE`.
- `D3D12SerializeRootSignature` only serializes version 1.0. Passing it
  `D3D_ROOT_SIGNATURE_VERSION_1_1` fails with "unsupported root signature version (2)".
  Query the device with `CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, ...)` and
  use `D3D12SerializeVersionedRootSignature` with the matching `D3D12_ROOT_PARAMETER1`
  / `D3D12_DESCRIPTOR_RANGE1` form (the 1.1 descriptor range adds a `Flags` field).
- A graphics PSO without an `InputLayout` silently draws nothing. Set it.
- `WIN32` is undefined in CMake before `project()`, so a Windows-only guard placed
  above `project()` fires on Windows too.
- Raw-string HLSL literals use the `)N3D` delimiter so `tools/embed_shaders.py` can
  detect a collision. Do not use that sequence inside a shader.
- A `WndProc` must answer with the `HWND` **passed to it**, never a member. Windows
  calls the procedure with `WM_NCCREATE` during `CreateWindowEx`, before the member is
  assigned, so `DefWindowProcW(nullptr, ...)` returns `FALSE` and window creation dies
  with `ERROR_INVALID_WINDOW_HANDLE` (1400). Compiling cleanly proves nothing here.
- `windows.h` defines `DrawText` and `PlaySound` as A/W aliases. The engine has
  functions with those exact names, so any translation unit that pulls in
  `windows.h` mangles them into `DrawTextW`/`PlaySoundA` and the link fails with an
  "undefined reference" to the real name. `internal/common.h` `#undef`s them, and
  `draw.h` / `audio.h` do the same defensively so an application that includes
  `windows.h` first still sees the engine's declarations. The failure mode is a
  *link* error in the examples only, never in the library build.
- Media Foundation needs a COM apartment, and `video.cpp` is the only other place
  that calls `CoInitializeEx`. A program that plays a video without ever loading a
  texture therefore had COM uninitialised, and `MFCreateSourceReaderFromURL` failed
  with `CO_E_NOTINITIALIZED` — `LoadVideo` returned `nullptr` and the example just
  showed its "no video" hint. `EnsureMediaFoundation()` now calls `CoInitializeEx`
  itself and tolerates `RPC_E_CHANGED_MODE`. Note `MFSTARTUP_LITE` is only
  `MFSTARTUP_NOSOCKET`, so it was never the problem.
- `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING` must be set on the source reader's
  attributes for `SetCurrentMediaType(MFVideoFormat_RGB32)` to succeed. Without it
  a decoder that only emits NV12 (i.e. every ordinary H.264/MP4) rejects RGB32 with
  `MF_E_INVALIDMEDIATYPE`, and the reader cannot be created with `pAttributes == NULL`.
- Do **not** create a texture in a `D3D12_HEAP_TYPE_UPLOAD` heap. Real D3D12 allows
  it, but vkd3d (D3D12 over Vulkan, the only way to run this on Linux) rejects it
  with `E_INVALIDARG` for every size, aligned row pitch or not. Video frames go
  through a default-heap texture plus a staging buffer, the same path
  `LoadTextureFromMemory` uses, so the same code works on both.
- A video frame's *colour channels* are the other half of the same trap.
  `MFVideoFormat_RGB32` is BGRA in memory, not RGBA, and the engine samples RGBA8,
  so writing a decoded frame straight into a texture swaps red and blue. The frame
  is channel-swapped as it is copied into the staging buffer.
- `IsVideoReady()` is driven by a flag set when a frame reaches the texture. When
  the texture upload was refactored, the assignment was dropped, so the flag stayed
  `false` forever and the example kept drawing its "no video" hint even though
  loading had succeeded. A *silent* failure: nothing was logged, and the only
  symptom was the fallback text.
- Under Wine, `EnumDisplaySettingsW(..., ENUM_CURRENT_SETTINGS, ...)` reports
  `dmDisplayFrequency == 0`, so `GetMonitorRefreshRate()` returns 0 there while it
  returns 60/120/... on real Windows. That is Wine's virtual display, not an engine
  bug; do not chase it. The same run is still the best check that the monitor
  enumeration, clipboard and cursor calls link and execute.
- The `DrawText`/`PlaySound` macro trap has two more members: `CreateWindow`,
  `CreateWindowEx` and `DestroyWindow` are also A/W macros. `PlatformBackend`
  declares methods with those names, so `internal/common.h` `#undef`s them too. Same
  symptom as the others: a *link* error or a macro-expansion error, never a clean
  build, and only in the translation unit that included `windows.h`.
- `CMAKE_INSTALL_LIBDIR` is empty until `include(GNUInstallDirs)` runs. Referencing it
  earlier silently produces a path like `/cmake/NexiumEngine`, and the failure only
  shows up at `cmake --install` time with a confusing "cannot create directory".
- The single-header amalgamation must emit the public headers **outside** the
  `NEXIUM_IMPLEMENTATION` guard and must not re-emit them inside it. `collect()` in
  `tools/amalgamate_library.py` takes the public list as `skip` for exactly that
  reason; without it the implementation section redefines every type and the build
  fails with "redefinition of struct n3d::Color" at the first header.
- `scripts/verify.sh` globs `src/*.cpp`, so it compiles a new source whether or not
  anyone added it to `CMakeLists.txt`. The CMake build does not glob, so the omission
  is invisible until a user of the *installed* package hits an undefined reference at
  link time — which is exactly what happened with `src/storage.cpp`. The script now
  compares the glob against both `CMakeLists.txt` and the amalgamator's source list.
- A banner line in a generated header that ends with a backslash is a multi-line
  comment, and `-Wcomment -Werror` rejects it. Keep the usage examples in
  `tools/amalgamate_library.py` free of trailing `\` continuations.

## Pitfalls

- Never use `git checkout -- <path>` / `git restore` to undo an accidental write to
  a tracked file: it restores the *index* version and silently discards uncommitted
  work. In this repo the tracked `games/*.cpp` and `examples/*.cpp` files are
  frequently ahead of the index (renamed + ported), so a checkout there loses real
  work with no reflog entry and no dangling blob to recover from. Prefer editing the
  file back, or `git stash`, and verify with `git status --short` first.
- The games are mechanically ported from the `Aurora3D`/`a3d` names with the same
  rename applied to the examples, so `tools/amalgamate_game.py` and the committed
  examples are the source of truth for the mapping:
  `Aurora3D`->`NexiumEngine`, `aurora3d`->`nexium`, `aurora/aurora.h`->
  `nexium/NexiumEngine.h`, `aurora/*.h`->`nexium/*.h`, `aurora.hlsli`->
  `nexium.hlsli`, `A3D_`->`N3D_`, `a3d`->`n3d`, `Aurora`->`Nexium`,
  `aurora`->`nexium`. Applying it to `git show HEAD:games/aurora_*.cpp`
  reproduces the ported games byte-for-byte.
- Never name a variable, parameter or member with a legacy Windows macro. `small`
  is `#define small char` (`rpcndr.h` / the MSVC SDK headers), so `bool small`
  expands to `bool char` and MSVC reports C2226 (unexpected `char`) plus C2632
  (`'bool'` followed by `char` is illegal). `near`, `far` and `interface` are
  similar traps. The `BuildDefaultIcon` parameter is `smallSize` for this reason.
  MinGW hides the bug because its `small` is guarded by `RC_INVOKED`, so it only
  shows up on a real MSVC build — keep identifiers clear of these names.

## Storage and n2d

- **`storage.h` is header-only and must stay that way.** The `Storage` drawer is
  covered by `tests/test_storage.cpp`, which compiles and *runs* natively on Linux
  (it is in the `verify.sh` native list). That only works because the header does
  not include the GPU/audio/media subsystems. It reaches them through
  `internal::AssetOps`, a table of function pointers that `src/storage.cpp` fills
  in from `n3d::Init()` via `InstallStorageOps()`. Adding a subsystem include to
  `storage.h`, or moving a loader body into it, breaks the native test. The same
  applies to logging: the header routes through `ops.logWarn` because
  `src/log.cpp` pulls in `windows.h`.
- **`N3D_DEFINE_STORAGE` cannot appear inside a function.** It generates a struct
  with `inline static` data members, and a local class cannot have static data
  members. Put it at namespace scope; wrap it in `namespace n3d { ... }` so the
  files read as `n3d::Drawer::file` (and `n2d::Drawer::file` through the alias).
- **`n2d` is an alias, not a second namespace.** `include/nexium/n2d.h` is
  `namespace n2d = n3d;`. Do not fork the API into a parallel `n2d` namespace:
  the 2D and 3D entry points share one implementation, and a copy would drift.
- **Regenerate the amalgamated games** after touching the engine or a game:
  `python3 tools/amalgamate_game.py games/<name>_single.cpp games/<name>.cpp`.
  `verify.sh` builds them with no `-I`, so a stale one fails the build.


## Platform layer, packaging and the single header

- **The window layer does not call Win32 directly.** `src/internal/platform.h`
  declares `PlatformBackend`; `src/internal/platform_win32.cpp` implements it and is
  the only file that includes `<windows.h>` for windowing/input. `src/internal/window.h`
  is a facade that owns the input state and the frame clock and folds the backend's
  `PlatformEvent` queue into it. The backend never writes the key/mouse arrays, so a
  second backend cannot desynchronise them.
- **Only the window layer is portable.** `n3d::Init()` casts the backend's opaque
  handle to `HWND` in one place (`src/engine.cpp`), because D3D12 is Windows and Xbox
  by construction. A non-Windows backend would need a renderer that is not D3D12. Do
  not claim the renderer is portable; it is not, and the README says so explicitly.
- **Adding a source file means three edits, not one.** `CMakeLists.txt` (does not
  glob), the `IMPLEMENTATION` list in `tools/amalgamate_library.py`, and — if it is a
  public header — the `PUBLIC` list there too. `verify.sh` now fails the build if a
  `src/*.cpp` is missing from either list, which is what caught `src/storage.cpp`.
- **The single header is `dist/nexium.h`**, generated, stb-style: public API above,
  implementation below behind `NEXIUM_IMPLEMENTATION`. `dist/nexium_impl.cpp` defines
  the macro for projects that do not want it in their own source. Regenerate with
  `python3 tools/amalgamate_library.py dist`. The public headers are emitted once,
  outside the guard; `collect()` is passed the public list as `skip` so the
  implementation section does not redefine them.
- **The library is installable.** `install(TARGETS ... EXPORT NexiumEngineTargets)`
  plus `cmake/NexiumEngineConfig.cmake.in` give a `NexiumEngine::nexiumengine` target
  that carries the include dirs and the DirectX link libraries, so a consumer needs
  only `find_package(NexiumEngine 0.1 REQUIRED)`. The export is only correct if
  `target_include_directories` has an `INSTALL_INTERFACE` entry; the `BUILD_INTERFACE`
  paths do not exist in an installed tree.
- **`windows.h` name traps.** `DrawText`, `DrawTextEx`, `PlaySound`, `CreateWindow`,
  `CreateWindowEx` and `DestroyWindow` are all A/W macros. `internal/common.h`
  `#undef`s every one of them; add to that list if a new public name collides.

## Conventions

- Namespace `n3d`. Public headers live in `include/nexium/`, one header per
  subsystem, plus the umbrella `NexiumEngine.h`.
- Implementation in `src/`, with Windows/D3D12-only code under `src/internal/`.
- Matrices are **column-major** in memory (`m[col*4 + row]`) and use the
  **column-vector** convention, so `M * v` in C++ maps exactly to
  `mul(M, v)` in HLSL with default (column-major) packing. Do not change this.
- Right-handed coordinate system, depth range `[0, 1]` (native D3D convention).
- Errors are reported through `n3d::Log` and by returning `false`/`nullptr`; the
  library never throws across its public boundary.
- No exceptions, no RTTI-dependent design, no external dependencies beyond the
  Windows SDK / MinGW headers and the C++ standard library.

## Style

- `PascalCase` for types and public functions, `camelCase` for locals/members.
- Public API is `n3d::` free functions + small POD structs (raylib-ish ergonomics).
- Keep comments for genuinely non-obvious invariants (e.g. resource-state
  transitions, barrier ordering, alignment rules), not for restating code.
