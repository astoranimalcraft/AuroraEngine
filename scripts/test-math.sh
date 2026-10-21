#!/usr/bin/env bash
# Runs the platform-independent tests natively on this machine.
#
# The math, colour and geometry layers have no Windows dependency, so unlike the
# rest of NexiumEngine they can be compiled with the host compiler and executed for
# real. This is the fastest feedback loop while working on them.
#
#   ./scripts/test-math.sh            run the tests
#   ./scripts/test-math.sh --warnings treat warnings as errors
set -u

cd "$(dirname "$0")/.."

CXX=${CXX:-g++}
STD=-std=c++20
INCLUDES="-Iinclude -Isrc"
WARNINGS="-Wall -Wextra"
if [ "${1:-}" = "--warnings" ]; then
    WARNINGS="$WARNINGS -Werror"
fi

BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

status=0
for source in tests/test_*.cpp; do
    [ -e "$source" ] || continue

    # The public-API check needs the Windows library to link; verify.sh compiles it.
    case "$source" in
        *public_api*) continue ;;
    esac

    binary="$BUILD/$(basename "$source" .cpp)"

    # test_geometry also exercises the primitive builders, which live in
    # src/geometry.cpp; the other tests are header-only.
    extra=""
    case "$source" in
        *geometry*) extra="src/geometry.cpp" ;;
    esac

    if ! $CXX $STD $WARNINGS $INCLUDES "$source" $extra -o "$binary"; then
        echo "  FAILED  $source (compile)"
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
    echo "tests: all passed"
else
    echo
    echo "tests: FAILURES above"
fi
exit $status
