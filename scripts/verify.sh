#!/usr/bin/env bash
# Cross-compiles every NexiumEngine source and example with MinGW-w64.
#
# DirectX 12 is Windows-only, so nothing here can run in this Linux sandbox. This
# script is the verification step: it type-checks and links the whole library and
# its examples, which is what catches API and ABI mistakes without a GPU.
#
#   ./scripts/verify.sh            library + examples
#   ./scripts/verify.sh --warnings treat warnings as errors
set -u

cd "$(dirname "$0")/.."

CXX=${CXX:-x86_64-w64-mingw32-g++}
STD=-std=c++20
INCLUDES="-Iinclude -Isrc"
WARNINGS="-Wall -Wextra"
if [ "${1:-}" = "--warnings" ]; then
    WARNINGS="$WARNINGS -Werror"
fi
LIBS="-ld3d12 -ldxgi -ld3dcompiler -lwindowscodecs -lole32 -luuid -luser32 -lgdi32 -lshcore -lxinput -lxaudio2_8 -lmfplat -lmfreadwrite -lmfuuid"

BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

status=0

echo "== build files =="
# The verify script globs src/*.cpp, so it compiles a file whether or not the
# build systems know about it. The CMake build does not glob, so a new source that
# nobody added to CMakeLists.txt links fine here and fails only for a user of the
# installed package. Compare the two lists instead of trusting the glob.
missing=""
for source in src/*.cpp src/internal/*.cpp; do
    [ -e "$source" ] || continue
    if ! grep -qF "$source" CMakeLists.txt; then
        missing="$missing $source"
    fi
done
if [ -n "$missing" ]; then
    echo "  FAILED  not in CMakeLists.txt:$missing"
    status=1
else
    echo "  ok      every source is listed in CMakeLists.txt"
fi

# The single-header amalgamation has its own source list for the same reason.
for source in src/*.cpp src/internal/*.cpp; do
    [ -e "$source" ] || continue
    if ! grep -qF "\"$source\"" tools/amalgamate_library.py; then
        echo "  FAILED  not in tools/amalgamate_library.py: $source"
        status=1
    fi
done

echo "== library =="
objects=()
for source in src/*.cpp src/internal/*.cpp; do
    object="$BUILD/$(echo "$source" | tr '/' '_').o"
    if ! $CXX $STD $WARNINGS $INCLUDES -c "$source" -o "$object"; then
        echo "  FAILED  $source"
        status=1
        continue
    fi
    echo "  ok      $source"
    objects+=("$object")
done

if [ ${#objects[@]} -eq 0 ]; then
    echo "no sources compiled"
    exit 1
fi

echo "== archive =="
if ! x86_64-w64-mingw32-ar rcs "$BUILD/libnexium.a" "${objects[@]}"; then
    echo "  FAILED  ar"
    status=1
else
    echo "  ok      libnexium.a"
fi

echo "== examples =="
for source in examples/*.cpp; do
    [ -e "$source" ] || continue
    binary="$BUILD/$(basename "$source" .cpp).exe"
    if ! $CXX $STD $WARNINGS $INCLUDES "$source" "$BUILD/libnexium.a" $LIBS -o "$binary"; then
        echo "  FAILED  $source"
        status=1
        continue
    fi
    echo "  ok      $source"
done

echo "== games =="
# The games are full applications built on the public API, verified the same way
# the examples are: cross-compiled and linked against the library. The *_single
# files already contain the whole engine, so they are handled on their own below.
for source in games/*.cpp; do
    [ -e "$source" ] || continue
    case "$source" in
        *_single.cpp) continue ;;
    esac
    binary="$BUILD/$(basename "$source" .cpp).exe"
    if ! $CXX $STD $WARNINGS $INCLUDES "$source" "$BUILD/libnexium.a" $LIBS -o "$binary"; then
        echo "  FAILED  $source"
        status=1
        continue
    fi
    echo "  ok      $source"
done

echo "== single-file builds =="
# The amalgamated builds must compile with no -I at all and no library: that is
# the whole point of them. Compiled only, since they are the same program.
for source in games/*_single.cpp; do
    [ -e "$source" ] || continue
    binary="$BUILD/$(basename "$source" .cpp).exe"
    if ! $CXX $STD $WARNINGS "$source" $LIBS -o "$binary"; then
        echo "  FAILED  $source"
        status=1
        continue
    fi
    echo "  ok      $source"
done

echo "== single-header build =="
# The library ships as one header too. It is generated, so a stale or broken
# amalgamation is only caught by actually compiling it: once with the macro in
# the game's own translation unit, once through the companion implementation
# unit. Both modes must link with no -I.
if command -v python3 >/dev/null 2>&1; then
    SH_DIR="$BUILD/single_header"
    mkdir -p "$SH_DIR"
    if ! python3 tools/amalgamate_library.py "$SH_DIR" >/dev/null; then
        echo "  FAILED  tools/amalgamate_library.py"
        status=1
    else
        cat > "$SH_DIR/macro_mode.cpp" <<'EOF'
#define NEXIUM_IMPLEMENTATION
#include "nexium.h"
int main() {
    n3d::Config config;
    if (!n3d::Init(config)) return 1;
    while (!n3d::WindowShouldClose()) {
        n3d::BeginScene(n3d::Camera3D{}, n3d::colors::Clear);
        n3d::EndScene();
    }
    n3d::Close();
    return 0;
}
EOF
        if ! $CXX $STD $WARNINGS -I"$SH_DIR" "$SH_DIR/macro_mode.cpp" $LIBS \
                -o "$SH_DIR/macro_mode.exe"; then
            echo "  FAILED  nexium.h (NEXIUM_IMPLEMENTATION mode)"
            status=1
        else
            echo "  ok      nexium.h (NEXIUM_IMPLEMENTATION mode)"
        fi

        cat > "$SH_DIR/separate_mode.cpp" <<'EOF'
#include "nexium.h"
int main() {
    n3d::Config config;
    if (!n3d::Init(config)) return 1;
    n3d::BeginScene(n3d::Camera3D{}, n3d::colors::Clear);
    n3d::EndScene();
    n3d::Close();
    return 0;
}
EOF
        if ! $CXX $STD $WARNINGS -I"$SH_DIR" "$SH_DIR/separate_mode.cpp" \
                "$SH_DIR/nexium_impl.cpp" $LIBS -o "$SH_DIR/separate_mode.exe"; then
            echo "  FAILED  nexium.h + nexium_impl.cpp"
            status=1
        else
            echo "  ok      nexium.h + nexium_impl.cpp"
        fi
    fi
else
    echo "  skip    python3 not found"
fi

echo "== api surface =="
# Not a runnable test: compiling it proves every function the documentation
# advertises still exists with the promised signature.
if ! $CXX $STD $WARNINGS $INCLUDES tests/test_public_api.cpp "$BUILD/libnexium.a" $LIBS \
        -o "$BUILD/test_public_api.exe"; then
    echo "  FAILED  tests/test_public_api.cpp"
    status=1
else
    echo "  ok      tests/test_public_api.cpp"
fi

echo "== native tests (run on this machine) =="
for source in tests/test_*.cpp; do
    [ -e "$source" ] || continue
    case "$source" in
        # Tests that only touch the platform-independent headers build natively.
        tests/test_math.cpp|tests/test_geometry.cpp|tests/test_2d.cpp|tests/test_camera.cpp|tests/test_hdr.cpp|tests/test_hdr_format.cpp|tests/test_storage.cpp) ;;
        *) continue ;;
    esac
    binary="$BUILD/$(basename "$source" .cpp)"
    if ! g++ $STD $WARNINGS $INCLUDES "$source" src/geometry.cpp -o "$binary"; then
        echo "  FAILED  $source"
        status=1
        continue
    fi
    if ! "$binary"; then
        echo "  FAILED  $source (runtime)"
        status=1
        continue
    fi
    echo "  ok      $source"
done

if [ $status -eq 0 ]; then
    echo
    echo "verify: all targets built"
else
    echo
    echo "verify: FAILURES above"
fi
exit $status
