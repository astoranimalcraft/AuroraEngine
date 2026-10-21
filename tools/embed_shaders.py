#!/usr/bin/env python3
"""Generates src/internal/shader_sources.h from the .hlsl files in shaders/.

The engine embeds its own shaders so a program links against NexiumEngine and runs
with no asset files next to the executable. The .hlsl files stay on disk because
they are the readable, editable source of truth (and the reference users copy
when writing custom shaders); this script turns them into string literals.

Run from the repository root:

    python3 tools/embed_shaders.py

The generated header is committed so the build needs no Python.
"""
import os
import re
import sys

SHADER_DIR = "shaders"
OUTPUT = os.path.join("src", "internal", "shader_sources.h")

# Embedded in this order; the include handler resolves "nexium.hlsli" from here.
FILES = ["nexium.hlsli", "nexium_basic.hlsl", "nexium_line.hlsl", "nexium_text.hlsl",
         "nexium_sky.hlsl"]


def identifier(name):
    return re.sub(r"[^A-Za-z0-9]", "_", name)


def main():
    # Two calling conventions are supported:
    #   python3 tools/embed_shaders.py                      (from the repo root)
    #   python3 tools/embed_shaders.py OUT.h IN1 [IN2 ...]   (explicit, used by CMake)
    argv = sys.argv[1:]
    if argv:
        if len(argv) < 2:
            print("usage: embed_shaders.py OUTPUT.h INPUT.hlsl [INPUT2.hlsl ...]",
                  file=sys.stderr)
            return 1
        output = argv[0]
        inputs = argv[1:]
    else:
        if not os.path.isdir(SHADER_DIR):
            print(f"error: run this from the repository root ({SHADER_DIR}/ not found)",
                  file=sys.stderr)
            return 1
        output = OUTPUT
        inputs = [os.path.join(SHADER_DIR, name) for name in FILES]

    entries = []
    for path in inputs:
        with open(path, "r", encoding="utf-8") as handle:
            source = handle.read()

        # A raw string literal cannot contain its own delimiter sequence.
        if ")N3D" in source:
            print(f"error: {path} contains the raw-string delimiter )N3D", file=sys.stderr)
            return 1
        entries.append((os.path.basename(path), source))

    lines = [
        "// NexiumEngine — generated file. DO NOT EDIT.",
        "//",
        "// Regenerate with:  python3 tools/embed_shaders.py",
        "//",
        "// The engine compiles these copies of shaders/*.hlsl at startup so that an",
        "// NexiumEngine program runs with no shader files next to the executable. Files on",
        "// disk take precedence, so dropping a modified copy beside the binary (or",
        "// pointing LoadShader() at your own) overrides the built-in one.",
        "#pragma once",
        "",
        "namespace n3d::internal {",
        "",
        "struct EmbeddedShader {",
        "    const char* name;",
        "    const char* source;",
        "};",
        "",
    ]

    for name, source in entries:
        lines.append(
            f'inline constexpr const char* kShaderSource_{identifier(name)} = R"N3D({source})N3D";'
        )
        lines.append("")

    lines.append("inline constexpr EmbeddedShader kEmbeddedShaders[] = {")
    for name, _ in entries:
        lines.append(f'    {{"{name}", kShaderSource_{identifier(name)}}},')
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr int kEmbeddedShaderCount =")
    lines.append("    static_cast<int>(sizeof(kEmbeddedShaders) / sizeof(kEmbeddedShaders[0]));")
    lines.append("")
    lines.append("// Looks up an embedded shader by file name (no directory). Returns nullptr when")
    lines.append("// the name is not one of the engine's own shaders.")
    lines.append("inline const char* FindEmbeddedShader(const char* name) {")
    lines.append("    if (!name) return nullptr;")
    lines.append("    for (int i = 0; i < kEmbeddedShaderCount; ++i) {")
    lines.append("        const char* a = kEmbeddedShaders[i].name;")
    lines.append("        const char* b = name;")
    lines.append("        while (*a && *a == *b) { ++a; ++b; }")
    lines.append("        if (*a == '\\0' && *b == '\\0') return kEmbeddedShaders[i].source;")
    lines.append("    }")
    lines.append("    return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace n3d::internal")
    lines.append("")

    directory = os.path.dirname(output)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))

    total = sum(len(source) for _, source in entries)
    print(f"wrote {output} ({len(entries)} shaders, {total} bytes of source)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
