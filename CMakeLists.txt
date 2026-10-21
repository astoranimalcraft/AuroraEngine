cmake_minimum_required(VERSION 3.20)

project(NexiumEngine VERSION 0.1.0 LANGUAGES CXX)

# NexiumEngine only targets Windows: DirectX 12 has no other implementation. Checked
# after project() because WIN32 is only defined once a platform has been loaded.
if(NOT WIN32)
    message(FATAL_ERROR
        "NexiumEngine is Windows-only (DirectX 12). On other platforms use ./scripts/verify.sh, "
        "which cross-compiles and links everything with MinGW-w64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra)
endif()

option(NEXIUM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(NEXIUM_WARNINGS_AS_ERRORS)
    if(MSVC)
        add_compile_options(/WX)
    else()
        add_compile_options(-Werror)
    endif()
endif()

# ----------------------------------------------------------------- library ---
add_library(nexiumengine STATIC
    src/audio.cpp
    src/camera.cpp
    src/engine.cpp
    src/draw.cpp
    src/draw2d.cpp
    src/geometry.cpp
    src/input.cpp
    src/log.cpp
    src/mesh.cpp
    src/model.cpp
    src/random.cpp
    src/renderer.cpp
    src/shader.cpp
    src/storage.cpp
    src/texture.cpp
    src/video.cpp
    src/internal/gpu.cpp
    src/internal/gamepad.cpp
    src/internal/window.cpp
    src/internal/platform_win32.cpp
)

target_include_directories(nexiumengine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    # The installed headers live under <prefix>/include, and a consumer only needs
    # the public ones; src/ is not installed.
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nexiumengine PUBLIC
    d3d12 dxgi d3dcompiler windowscodecs ole32 uuid user32 gdi32 shcore
    xinput xaudio2_8 mfplat mfreadwrite mfuuid
)

# The shaders are embedded in the binary by tools/embed_shaders.py, so the library
# is self-contained: no runtime files to ship next to the executable. The generated
# header is committed, so regenerating it is only possible, never required - the
# build works with no Python installed.
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    set(N3D_SHADER_OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/internal/shader_sources.h)
    set(N3D_SHADER_INPUTS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nexium.hlsli
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nexium_basic.hlsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nexium_line.hlsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nexium_text.hlsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nexium_sky.hlsl
    )

    add_custom_command(
        OUTPUT ${N3D_SHADER_OUTPUT}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.py
                ${N3D_SHADER_OUTPUT}
                ${N3D_SHADER_INPUTS}
        DEPENDS ${N3D_SHADER_INPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.py
        COMMENT "Embedding HLSL sources into shader_sources.h"
        VERBATIM
    )

    add_custom_target(nexiumengine_shaders DEPENDS ${N3D_SHADER_OUTPUT})
    add_dependencies(nexiumengine nexiumengine_shaders)
else()
    message(STATUS "Python3 not found: using the committed src/internal/shader_sources.h")
endif()

# ------------------------------------------------------------------ install --
# GNUInstallDirs supplies CMAKE_INSTALL_LIBDIR, which is "lib" on most systems
# but "lib64" or "lib/<triplet>" on others. Without it the path below would be
# empty and the package would try to install into "/cmake/NexiumEngine".
include(GNUInstallDirs)

# The export set is what makes the library consumable with find_package(): without
# it CMake can copy the files into place but has no package metadata to describe
# them, so a downstream project has to hardcode the include and library paths.
install(TARGETS nexiumengine
    EXPORT NexiumEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/nexium DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY shaders/ DESTINATION ${CMAKE_INSTALL_DATADIR}/nexiumengine/shaders)

# Generates NexiumEngineConfig.cmake and NexiumEngineConfigVersion.cmake from the
# template below, plus the targets file that carries the link libraries.
include(CMakePackageConfigHelpers)

set(N3D_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/NexiumEngine")

install(EXPORT NexiumEngineTargets
    FILE NexiumEngineTargets.cmake
    NAMESPACE NexiumEngine::
    DESTINATION ${N3D_CMAKE_INSTALL_DIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NexiumEngineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NexiumEngineConfig.cmake
    INSTALL_DESTINATION ${N3D_CMAKE_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/NexiumEngineConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/NexiumEngineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/NexiumEngineConfigVersion.cmake
    DESTINATION ${N3D_CMAKE_INSTALL_DIR}
)

# ----------------------------------------------------------------- examples --
option(NEXIUM_BUILD_EXAMPLES "Build the NexiumEngine examples" ON)
if(NEXIUM_BUILD_EXAMPLES)
    file(GLOB N3D_EXAMPLE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp)
    foreach(source ${N3D_EXAMPLE_SOURCES})
        get_filename_component(name ${source} NAME_WE)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE nexiumengine)
    endforeach()
endif()

# ------------------------------------------------------------------- games ---
# Complete applications built on the public API, useful as worked examples of a
# real game loop. Same shape as the examples targets.
option(NEXIUM_BUILD_GAMES "Build the NexiumEngine games" ON)
if(NEXIUM_BUILD_GAMES)
    file(GLOB N3D_GAME_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/games/*.cpp)
    foreach(source ${N3D_GAME_SOURCES})
        get_filename_component(name ${source} NAME_WE)
        # The *_single.cpp files are amalgamated: they already contain the whole
        # engine, so linking nexiumengine into them would duplicate every symbol. They
        # are generated by tools/amalgamate_game.py and built by hand instead.
        string(REGEX MATCH "_single$" is_single "${name}")
        if(is_single)
            continue()
        endif()
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE nexiumengine)
    endforeach()
endif()
