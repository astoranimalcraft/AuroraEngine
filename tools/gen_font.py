#!/usr/bin/env python3
"""Generates src/internal/font_data.h — the built-in bitmap font.

The engine ships a tiny ASCII bitmap font so a program can call n3d::DrawText()
with no asset files at all. The glyphs are authored here as readable ASCII art
(7 rows of 5 pixels, '#' = ink) and packed into one byte per row, low 5 bits,
most-significant bit leftmost.

Run from the repository root:

    python3 tools/gen_font.py

The generated header is committed, so the build needs no Python.
"""
import os
import sys

GLYPH_W = 5
GLYPH_H = 7

# ASCII 32..126, in order. Each entry is 7 rows of exactly 5 characters.
GLYPHS = {
    " ": """
.....
.....
.....
.....
.....
.....
.....""",
    "!": """
..#..
..#..
..#..
..#..
..#..
.....
..#..""",
    '"': """
.#.#.
.#.#.
.....
.....
.....
.....
.....""",
    "#": """
.#.#.
.#.#.
#####
.#.#.
#####
.#.#.
.#.#.""",
    "$": """
..#..
.####
#.#..
.###.
..#.#
####.
..#..""",
    "%": """
##...
##..#
...#.
..#..
.#...
#..##
...##""",
    "&": """
.##..
#..#.
#.#..
.#...
#.#.#
#..#.
.##.#""",
    "'": """
..#..
..#..
.....
.....
.....
.....
.....""",
    "(": """
...#.
..#..
.#...
.#...
.#...
..#..
...#.""",
    ")": """
.#...
..#..
...#.
...#.
...#.
..#..
.#...""",
    "*": """
.....
#.#.#
.###.
#####
.###.
#.#.#
.....""",
    "+": """
.....
..#..
..#..
#####
..#..
..#..
.....""",
    ",": """
.....
.....
.....
.....
..##.
..#..
.#...""",
    "-": """
.....
.....
.....
#####
.....
.....
.....""",
    ".": """
.....
.....
.....
.....
.....
.##..
.##..""",
    "/": """
....#
...#.
...#.
..#..
.#...
.#...
#....""",
    "0": """
.###.
#...#
#..##
#.#.#
##..#
#...#
.###.""",
    "1": """
..#..
.##..
..#..
..#..
..#..
..#..
.###.""",
    "2": """
.###.
#...#
....#
...#.
..#..
.#...
#####""",
    "3": """
#####
...#.
..#..
...#.
....#
#...#
.###.""",
    "4": """
...#.
..##.
.#.#.
#..#.
#####
...#.
...#.""",
    "5": """
#####
#....
####.
....#
....#
#...#
.###.""",
    "6": """
..##.
.#...
#....
####.
#...#
#...#
.###.""",
    "7": """
#####
....#
...#.
..#..
.#...
.#...
.#...""",
    "8": """
.###.
#...#
#...#
.###.
#...#
#...#
.###.""",
    "9": """
.###.
#...#
#...#
.####
....#
...#.
.##..""",
    ":": """
.....
.##..
.##..
.....
.##..
.##..
.....""",
    ";": """
.....
.##..
.##..
.....
.##..
..#..
.#...""",
    "<": """
...#.
..#..
.#...
#....
.#...
..#..
...#.""",
    "=": """
.....
.....
#####
.....
#####
.....
.....""",
    ">": """
.#...
..#..
...#.
....#
...#.
..#..
.#...""",
    "?": """
.###.
#...#
....#
...#.
..#..
.....
..#..""",
    "@": """
.###.
#...#
#.###
#.#.#
#.###
#....
.###.""",
    "A": """
..#..
.#.#.
#...#
#...#
#####
#...#
#...#""",
    "B": """
####.
#...#
#...#
####.
#...#
#...#
####.""",
    "C": """
.###.
#...#
#....
#....
#....
#...#
.###.""",
    "D": """
###..
#..#.
#...#
#...#
#...#
#..#.
###..""",
    "E": """
#####
#....
#....
####.
#....
#....
#####""",
    "F": """
#####
#....
#....
####.
#....
#....
#....""",
    "G": """
.###.
#...#
#....
#.###
#...#
#...#
.####""",
    "H": """
#...#
#...#
#...#
#####
#...#
#...#
#...#""",
    "I": """
.###.
..#..
..#..
..#..
..#..
..#..
.###.""",
    "J": """
..###
...#.
...#.
...#.
...#.
#..#.
.##..""",
    "K": """
#...#
#..#.
#.#..
##...
#.#..
#..#.
#...#""",
    "L": """
#....
#....
#....
#....
#....
#....
#####""",
    "M": """
#...#
##.##
#.#.#
#.#.#
#...#
#...#
#...#""",
    "N": """
#...#
#...#
##..#
#.#.#
#..##
#...#
#...#""",
    "O": """
.###.
#...#
#...#
#...#
#...#
#...#
.###.""",
    "P": """
####.
#...#
#...#
####.
#....
#....
#....""",
    "Q": """
.###.
#...#
#...#
#...#
#.#.#
#..#.
.##.#""",
    "R": """
####.
#...#
#...#
####.
#.#..
#..#.
#...#""",
    "S": """
.####
#....
#....
.###.
....#
....#
####.""",
    "T": """
#####
..#..
..#..
..#..
..#..
..#..
..#..""",
    "U": """
#...#
#...#
#...#
#...#
#...#
#...#
.###.""",
    "V": """
#...#
#...#
#...#
#...#
#...#
.#.#.
..#..""",
    "W": """
#...#
#...#
#...#
#.#.#
#.#.#
##.##
#...#""",
    "X": """
#...#
#...#
.#.#.
..#..
.#.#.
#...#
#...#""",
    "Y": """
#...#
#...#
.#.#.
..#..
..#..
..#..
..#..""",
    "Z": """
#####
....#
...#.
..#..
.#...
#....
#####""",
    "[": """
.###.
.#...
.#...
.#...
.#...
.#...
.###.""",
    "\\": """
#....
.#...
.#...
..#..
...#.
...#.
....#""",
    "]": """
.###.
...#.
...#.
...#.
...#.
...#.
.###.""",
    "^": """
..#..
.#.#.
#...#
.....
.....
.....
.....""",
    "_": """
.....
.....
.....
.....
.....
.....
#####""",
    "`": """
.#...
..#..
.....
.....
.....
.....
.....""",
    "a": """
.....
.....
.###.
....#
.####
#...#
.####""",
    "b": """
#....
#....
####.
#...#
#...#
#...#
####.""",
    "c": """
.....
.....
.###.
#....
#....
#....
.###.""",
    "d": """
....#
....#
.####
#...#
#...#
#...#
.####""",
    "e": """
.....
.....
.###.
#...#
#####
#....
.###.""",
    "f": """
..##.
.#..#
.#...
####.
.#...
.#...
.#...""",
    "g": """
.....
.####
#...#
#...#
.####
....#
.###.""",
    "h": """
#....
#....
####.
#...#
#...#
#...#
#...#""",
    "i": """
..#..
.....
.##..
..#..
..#..
..#..
.###.""",
    "j": """
...#.
.....
..##.
...#.
...#.
#..#.
.##..""",
    "k": """
#....
#....
#..#.
#.#..
##...
#.#..
#..#.""",
    "l": """
.##..
..#..
..#..
..#..
..#..
..#..
.###.""",
    "m": """
.....
.....
##.#.
#.#.#
#.#.#
#...#
#...#""",
    "n": """
.....
.....
####.
#...#
#...#
#...#
#...#""",
    "o": """
.....
.....
.###.
#...#
#...#
#...#
.###.""",
    "p": """
.....
####.
#...#
#...#
####.
#....
#....""",
    "q": """
.....
.####
#...#
#...#
.####
....#
....#""",
    "r": """
.....
.....
#.##.
##..#
#....
#....
#....""",
    "s": """
.....
.....
.####
#....
.###.
....#
####.""",
    "t": """
.#...
.#...
####.
.#...
.#...
.#..#
..##.""",
    "u": """
.....
.....
#...#
#...#
#...#
#..##
.##.#""",
    "v": """
.....
.....
#...#
#...#
#...#
.#.#.
..#..""",
    "w": """
.....
.....
#...#
#.#.#
#.#.#
#.#.#
.#.#.""",
    "x": """
.....
.....
#...#
.#.#.
..#..
.#.#.
#...#""",
    "y": """
.....
#...#
#...#
#...#
.####
....#
.###.""",
    "z": """
.....
.....
#####
...#.
..#..
.#...
#####""",
    "{": """
...##
..#..
..#..
.#...
..#..
..#..
...##""",
    "|": """
..#..
..#..
..#..
..#..
..#..
..#..
..#..""",
    "}": """
##...
..#..
..#..
...#.
..#..
..#..
##...""",
    "~": """
.....
.....
.#...
#.#.#
...#.
.....
.....""",
}


def parse_art(char, art):
    rows = art.split("\n")
    # A leading newline from the triple-quoted literal produces an empty first
    # element; drop it, then keep exactly GLYPH_H rows of GLYPH_W characters.
    if rows and rows[0] == "":
        rows = rows[1:]
    if len(rows) != GLYPH_H:
        raise ValueError(f"glyph {char!r}: expected {GLYPH_H} rows, got {len(rows)}")
    packed = []
    for index, row in enumerate(rows):
        if len(row) != GLYPH_W:
            raise ValueError(f"glyph {char!r} row {index}: expected {GLYPH_W} columns, got {len(row)}")
        value = 0
        for column, cell in enumerate(row):
            if cell == "#":
                value |= 1 << (GLYPH_W - 1 - column)
            elif cell != ".":
                raise ValueError(f"glyph {char!r} row {index}: bad cell {cell!r}")
        packed.append(value)
    return packed


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else os.path.join("src", "internal", "font_data.h")

    expected = [chr(c) for c in range(32, 127)]
    missing = [c for c in expected if c not in GLYPHS]
    if missing:
        print(f"error: missing glyphs: {missing}", file=sys.stderr)
        return 1
    extra = [c for c in GLYPHS if c not in expected]
    if extra:
        print(f"error: unexpected glyphs: {extra}", file=sys.stderr)
        return 1

    rows = []
    for char in expected:
        rows.extend(parse_art(char, GLYPHS[char]))

    lines = [
        "// NexiumEngine — generated file. DO NOT EDIT.",
        "//",
        "// Regenerate with:  python3 tools/gen_font.py",
        "//",
        "// The built-in 5x7 bitmap font, one byte per row, low 5 bits used, the",
        "// leftmost pixel in bit 4. Glyph order is ASCII 32 (' ') through 126 ('~').",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace n3d::internal {",
        "",
        f"inline constexpr int kFontGlyphWidth = {GLYPH_W};",
        f"inline constexpr int kFontGlyphHeight = {GLYPH_H};",
        "inline constexpr int kFontFirstChar = 32;",
        "inline constexpr int kFontLastChar = 126;",
        "",
        "inline constexpr uint8_t kFontRows[] = {",
    ]

    for start in range(0, len(rows), GLYPH_H):
        glyph_index = start // GLYPH_H
        char = expected[glyph_index]
        label = "' '" if char == " " else char
        values = ", ".join(f"0x{value:02X}" for value in rows[start:start + GLYPH_H])
        lines.append(f"    {values}, // {label!r}")
    lines.append("};")
    lines.append("")
    lines.append("} // namespace n3d::internal")
    lines.append("")

    directory = os.path.dirname(output)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))

    print(f"wrote {output} ({len(expected)} glyphs, {len(rows)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
