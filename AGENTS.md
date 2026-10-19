# AGENTS.md — Aurora3D

## What this repository is

Aurora3D is a small, self-contained 3D library/engine for C++20 built directly on
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
      -o /tmp/a3d.exe
  WINEPREFIX=$HOME/wineprefix XDG_RUNTIME_DIR=$HOME/xdgr DISPLAY=:99 wine /tmp/a3d.exe
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
  VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json wine /tmp/a3d.exe   # lavapipe (CPU)
  ```
  With that in place the adapter reports as a real GPU and the engine reaches
  "Aurora3D ready". vkd3d is more permissive than the Windows runtime, though: it tolerates
  some invalid calls that Windows rejects with `DXGI_ERROR_INVALID_CALL`, so a clean Wine run
  is necessary but not sufficient. Confirm anything version- or flag-sensitive against the
  DXGI/D3D12 documentation as well. The D3D12 debug layer is unavailable under Wine, so
  validation-layer-only messages do not appear here.
- **XAudio2 does not survive Wine here.** `XAudio2Create` and `CreateMasteringVoice`
  both succeed, then Wine's audio mixer thread faults a few hundred ms later (ALSA
  cannot open a card in this container). The fault is inside Wine, not the engine: a
  ~15-line program that only calls `XAudio2Create`, `CreateMasteringVoice`, `SetVolume`
  and `Sleep(3000)` crashes identically, and the crash is asynchronous, so it lands in
  whatever code is running at the time. Do not chase this as an Aurora3D bug — test the
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
- Raw-string HLSL literals use the `)A3D` delimiter so `tools/embed_shaders.py` can
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

## Conventions

- Namespace `a3d`. Public headers live in `include/aurora/`, one header per
  subsystem, plus the umbrella `aurora.h`.
- Implementation in `src/`, with Windows/D3D12-only code under `src/internal/`.
- Matrices are **column-major** in memory (`m[col*4 + row]`) and use the
  **column-vector** convention, so `M * v` in C++ maps exactly to
  `mul(M, v)` in HLSL with default (column-major) packing. Do not change this.
- Right-handed coordinate system, depth range `[0, 1]` (native D3D convention).
- Errors are reported through `a3d::Log` and by returning `false`/`nullptr`; the
  library never throws across its public boundary.
- No exceptions, no RTTI-dependent design, no external dependencies beyond the
  Windows SDK / MinGW headers and the C++ standard library.

## Style

- `PascalCase` for types and public functions, `camelCase` for locals/members.
- Public API is `a3d::` free functions + small POD structs (raylib-ish ergonomics).
- Keep comments for genuinely non-obvious invariants (e.g. resource-state
  transitions, barrier ordering, alignment rules), not for restating code.
