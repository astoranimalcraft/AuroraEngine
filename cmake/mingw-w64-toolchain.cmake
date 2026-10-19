# CMake toolchain for cross-compiling Aurora3D from Linux with MinGW-w64.
#
# DirectX 12 is Windows-only, so this is not how you build for release - it exists
# so that the CMake build itself can be exercised (and the examples linked) on a
# non-Windows machine, alongside scripts/verify.sh.
#
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
#   cmake --build build
#
# Requires the MinGW-w64 cross compiler, e.g. on Debian/Ubuntu:
#   sudo apt install g++-mingw-w64-x86-64
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Search the cross sysroot for libraries and headers, but the host PATH for tools.
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
