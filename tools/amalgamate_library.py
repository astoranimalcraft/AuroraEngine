#!/usr/bin/env python3
"""Builds the single-header NexiumEngine: nexium.h, library + implementation.

Two ways to ship a C++ library are common. The repository keeps the engine split
into one .cpp per subsystem, which is the right layout to develop and debug. The
other is one header a project can drop in and compile: `#define
NEXIUM_IMPLEMENTATION` in exactly one translation unit, then include everywhere.

This script produces the second form. It writes:

    nexium.h                 the whole engine: public API, then the
                             implementation guarded by NEXIUM_IMPLEMENTATION
    nexium_impl.cpp          one line that defines the macro and includes it,
                             so a project that does not want the macro in its
                             own source can just compile this file

The layout follows the stb convention on purpose: it is the one people already
know, and it keeps the header usable in both modes from the same text.

Usage:  python3 tools/amalgamate_library.py [output_dir]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Public headers, dependency first. These are what a user includes.
PUBLIC = [
    "include/nexium/color.h",
    "include/nexium/math.h",
    "include/nexium/log.h",
    "include/nexium/input.h",
    "include/nexium/camera.h",
    "include/nexium/mesh.h",
    "include/nexium/texture.h",
    "include/nexium/shader.h",
    "include/nexium/storage.h",
    "include/nexium/draw.h",
    "include/nexium/audio.h",
    "include/nexium/model.h",
    "include/nexium/video.h",
    "include/nexium/NexiumEngine.h",
]

# Implementation, dependency first. Emitted only under NEXIUM_IMPLEMENTATION.
IMPLEMENTATION = [
    "src/internal/common.h",
    "src/internal/geometry.h",
    "src/internal/gpu.h",
    "src/internal/hdr.h",
    "src/internal/envmap.h",
    "src/internal/resources.h",
    "src/internal/renderer.h",
    "src/internal/platform.h",
    "src/internal/window.h",
    "src/internal/font_data.h",
    "src/internal/shader_sources.h",
    "src/internal/icon_data.h",
    "src/log.cpp",
    "src/random.cpp",
    "src/geometry.cpp",
    "src/mesh.cpp",
    "src/texture.cpp",
    "src/shader.cpp",
    "src/renderer.cpp",
    "src/draw.cpp",
    "src/draw2d.cpp",
    "src/camera.cpp",
    "src/input.cpp",
    "src/audio.cpp",
    "src/model.cpp",
    "src/storage.cpp",
    "src/video.cpp",
    "src/engine.cpp",
    "src/internal/gamepad.cpp",
    "src/internal/gpu.cpp",
    "src/internal/window.cpp",
    "src/internal/platform_win32.cpp",
]

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
CONDITIONAL_RE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b')

# Generated files whose contents are embedded data. The HLSL blob contains
# `#include "nexium.hlsli"` lines *inside raw string literals*, so they must not
# be mistaken for C++ includes. These are emitted verbatim.
OPAQUE = {
    "src/internal/shader_sources.h",
    "src/internal/font_data.h",
}


def resolve(source, name):
    """Classifies an include as local (returns its path) or system (returns None).

    Shader sources pulled in by name are neither: they are compiled at runtime by
    the engine, so the amalgamated C++ must simply leave them alone.
    """
    if name.endswith(".hlsli"):
        return "skip"
    for candidate in (
        os.path.join(ROOT, "include", name),
        os.path.join(ROOT, "src", name),
        os.path.join(ROOT, "src", os.path.dirname(source), name),
    ):
        if os.path.isfile(candidate):
            return os.path.relpath(candidate, ROOT)
    if name.startswith("nexium/") or name.startswith("internal/"):
        raise SystemExit("amalgamate: cannot resolve %r from %r" % (name, source))
    return None


def flatten(path, seen, order, system):
    if path in seen:
        return
    seen.add(path)

    with open(os.path.join(ROOT, path), "r", encoding="utf-8") as handle:
        text = handle.read()

    if path not in OPAQUE:
        depth = 0
        for line in text.splitlines():
            if CONDITIONAL_RE.match(line):
                token = CONDITIONAL_RE.match(line).group(1)
                depth += -1 if token == "endif" else 1
                continue
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            target = resolve(path, match.group(1))
            if target == "skip":
                continue
            if target is None:
                if depth == 0 and match.group(1) not in system:
                    system.append(match.group(1))
            else:
                flatten(target, seen, order, system)

    order.append(path)


def strip_includes(path, text):
    if path in OPAQUE:
        return "\n".join(l for l in text.splitlines() if l.strip() != "#pragma once")
    out = []
    depth = 0
    for line in text.splitlines():
        if CONDITIONAL_RE.match(line):
            token = CONDITIONAL_RE.match(line).group(1)
            depth += -1 if token == "endif" else 1
            out.append(line)
            continue
        if line.strip() == "#pragma once":
            continue
        if INCLUDE_RE.match(line):
            match = INCLUDE_RE.match(line)
            target = resolve(path, match.group(1))
            if target == "skip":
                continue
            if target is None and depth > 0:
                out.append(line)      # guarded system include stays put
            continue
        out.append(line)
    return "\n".join(out)


def collect(modules, skip=None):
    order, system = [], []
    seen = set(skip or ())
    for module in modules:
        flatten(module, seen, order, system)
    return order, system


def render(order, system, banner):
    parts = [banner, "\n// ------------------------------------------------- system headers --\n"]
    # These normally live in src/internal/common.h, which the engine's .cpp files
    # include before <windows.h>. Here every system header is hoisted above that,
    # so the defines have to come first or windows.h is pulled in without UNICODE
    # and the wide-character entry points are not declared.
    parts.append(
        "#ifndef UNICODE\n"
        "#define UNICODE\n"
        "#endif\n"
        "#ifndef _UNICODE\n"
        "#define _UNICODE\n"
        "#endif\n"
        "#ifndef WIN32_LEAN_AND_MEAN\n"
        "#define WIN32_LEAN_AND_MEAN\n"
        "#endif\n"
        "#ifndef NOMINMAX\n"
        "#define NOMINMAX\n"
        "#endif\n"
    )
    parts.extend("#include <%s>" % name for name in system)

    for path in order:
        with open(os.path.join(ROOT, path), "r", encoding="utf-8") as handle:
            text = handle.read()
        header = "// " + "-" * 20 + " %s " % path
        header = header.ljust(79, "-")
        parts.append("\n%s\n\n%s\n" % (header, strip_includes(path, text).strip()))
    return "\n".join(parts)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "dist"
    os.makedirs(out_dir, exist_ok=True)

    public_order, public_system = collect(PUBLIC)
    # The implementation section must not re-emit the public headers: they are
    # already above, outside the NEXIUM_IMPLEMENTATION guard, so repeating them
    # inside would redefine every type in the translation unit that defines the
    # macro.
    impl_order, impl_system = collect(IMPLEMENTATION, skip=set(public_order))

    # System headers are the union: the public section needs the few the headers
    # pull in, the implementation section needs the rest.
    system = list(public_system)
    for name in impl_system:
        if name not in system:
            system.append(name)

    banner = (
        "// NexiumEngine — single-header build.\n"
        "//\n"
        "// GENERATED FILE, do not edit by hand. Regenerate with:\n"
        "//     python3 tools/amalgamate_library.py\n"
        "//\n"
        "// Usage, exactly like stb:\n"
        "//\n"
        "//   In ONE translation unit, before including this file:\n"
        "//       #define NEXIUM_IMPLEMENTATION\n"
        "//       #include \"nexium.h\"\n"
        "//\n"
        "//   Everywhere else:\n"
        "//       #include \"nexium.h\"\n"
        "//\n"
        "// Or compile the companion nexium_impl.cpp, which does the first form for\n"
        "// you, and include this header normally in the rest of the project.\n"
        "//\n"
        "// Windows and DirectX 12 only. Link against:\n"
        "//     d3d12 dxgi d3dcompiler windowscodecs ole32 uuid user32 gdi32 shcore\n"
        "//     xinput xaudio2_8 mfplat mfreadwrite mfuuid\n"
        "//\n"
        "// MinGW-w64:\n"
        "//   g++ -std=c++20 -O2 my_game.cpp nexium_impl.cpp -ld3d12 -ldxgi\n"
        "//       -ld3dcompiler -lwindowscodecs -lole32 -luuid -luser32 -lgdi32\n"
        "//       -lshcore -lxinput -lxaudio2_8 -lmfplat -lmfreadwrite -lmfuuid\n"
        "//       -o my_game.exe\n"
        "//\n"
        "// MSVC:\n"
        "//   cl /std:c++20 /O2 /EHsc my_game.cpp nexium_impl.cpp d3d12.lib dxgi.lib\n"
        "//       d3dcompiler.lib windowscodecs.lib ole32.lib uuid.lib user32.lib\n"
        "//       gdi32.lib shcore.lib xinput.lib xaudio2_8.lib mfplat.lib\n"
        "//       mfreadwrite.lib mfuuid.lib\n"
    )

    text = render(public_order, system, banner)
    text += (
        "\n\n// ============================================================================\n"
        "// Implementation\n"
        "// ============================================================================\n"
        "#ifdef NEXIUM_IMPLEMENTATION\n"
        "#ifndef NEXIUM_IMPLEMENTATION_INCLUDED\n"
        "#define NEXIUM_IMPLEMENTATION_INCLUDED\n\n"
    )
    text += render(impl_order, [], "")
    text += "\n#endif // NEXIUM_IMPLEMENTATION_INCLUDED\n"
    text += "#endif // NEXIUM_IMPLEMENTATION\n"

    header_path = os.path.join(out_dir, "nexium.h")
    with open(header_path, "w", encoding="utf-8") as handle:
        handle.write(text)

    impl_path = os.path.join(out_dir, "nexium_impl.cpp")
    with open(impl_path, "w", encoding="utf-8") as handle:
        handle.write(
            "// NexiumEngine — single-header build, implementation unit.\n"
            "//\n"
            "// GENERATED FILE, do not edit by hand.\n"
            "//\n"
            "// Defines NEXIUM_IMPLEMENTATION and includes nexium.h so the header can\n"
            "// stay include-only in the rest of the project. Compile this file once.\n"
            "#define NEXIUM_IMPLEMENTATION\n"
            '#include "nexium.h"\n'
        )

    print("wrote %s (%d public + %d implementation modules, %d system headers)"
          % (header_path, len(public_order), len(impl_order), len(system)))
    print("wrote %s" % impl_path)


if __name__ == "__main__":
    main()
