#!/usr/bin/env python3
"""Builds the single-file Nexium Blaster: engine + game in one .cpp.

The repository keeps the engine split into one .cpp per subsystem, which is the
right layout for the library. This script produces the other thing people ask
for: one translation unit that compiles with no other file present.

It flattens the include graph depth-first, hoists every system include to the
top, drops the engine's own headers into the body in dependency order, and
appends the game. Because everything lands in one unnamed namespace, duplicate
helper names across subsystems would clash - the build in verify is what proves
they do not.

Usage:  python3 tools/amalgamate_game.py <output.cpp> [game.cpp]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The source lists come from the library amalgamator, which is the single place
# they are maintained. Duplicating them here is how a newly added engine file ends
# up in one amalgamation and not the other; the game build then links against a
# stale copy of the engine.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from amalgamate_library import PUBLIC, IMPLEMENTATION  # noqa: E402

MODULES = PUBLIC + IMPLEMENTATION

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


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else "nexium_blaster_single.cpp"
    game = sys.argv[2] if len(sys.argv) > 2 else "games/nexium_blaster.cpp"

    order, system = [], []
    seen = set()
    for module in MODULES:
        flatten(module, seen, order, system)
    flatten(game, seen, order, system)

    parts = []
    parts.append(
        "// Nexium Blaster - single-file build.\n"
        "//\n"
        "// GENERATED FILE, do not edit by hand. Regenerate with:\n"
        "//     python3 tools/amalgamate_game.py nexium_blaster_single.cpp\n"
        "//\n"
        "// The whole NexiumEngine engine plus the game, in one translation unit. It\n"
        "// needs nothing but the Windows SDK and DirectX 12.\n"
        "//\n"
        "//   MinGW-w64:\n"
        "//     g++ -std=c++20 -O2 nexium_blaster_single.cpp -ld3d12 -ldxgi\n"
        "//         -ld3dcompiler -lwindowscodecs -lole32 -luuid -luser32 -lgdi32\n"
        "//         -lshcore -lxinput -lxaudio2_8 -lmfplat -lmfreadwrite -lmfuuid\n"
        "//         -o nexium_blaster.exe\n"
        "//\n"
        "//   MSVC:\n"
        "//     cl /std:c++20 /O2 /EHsc nexium_blaster_single.cpp d3d12.lib dxgi.lib\n"
        "//         d3dcompiler.lib windowscodecs.lib ole32.lib uuid.lib user32.lib\n"
        "//         gdi32.lib shcore.lib xinput.lib xaudio2_8.lib mfplat.lib\n"
        "//         mfreadwrite.lib mfuuid.lib\n"
    )

    parts.append("\n// ------------------------------------------------------- system headers --\n")
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
        banner = "// " + "-" * 20 + " %s " % path
        banner = banner.ljust(79, "-")
        parts.append("\n%s\n\n%s\n" % (banner, strip_includes(path, text).strip()))

    with open(output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(parts) + "\n")

    print("wrote %s (%d modules, %d system headers)" % (output, len(order), len(system)))


if __name__ == "__main__":
    main()
