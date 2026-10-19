#!/usr/bin/env bash
# Cross-compiles every Aurora3D source and example with MinGW-w64.
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
if ! x86_64-w64-mingw32-ar rcs "$BUILD/libaurora3d.a" "${objects[@]}"; then
    echo "  FAILED  ar"
    status=1
else
    echo "  ok      libaurora3d.a"
fi

echo "== examples =="
for source in examples/*.cpp; do
    [ -e "$source" ] || continue
    binary="$BUILD/$(basename "$source" .cpp).exe"
    if ! $CXX $STD $WARNINGS $INCLUDES "$source" "$BUILD/libaurora3d.a" $LIBS -o "$binary"; then
        echo "  FAILED  $source"
        status=1
        continue
    fi
    echo "  ok      $source"
done

echo "== api surface =="
# Not a runnable test: compiling it proves every function the documentation
# advertises still exists with the promised signature.
if ! $CXX $STD $WARNINGS $INCLUDES tests/test_public_api.cpp "$BUILD/libaurora3d.a" $LIBS \
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
        tests/test_math.cpp|tests/test_geometry.cpp|tests/test_2d.cpp) ;;
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
