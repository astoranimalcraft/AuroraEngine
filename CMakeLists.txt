cmake_minimum_required(VERSION 3.20)

project(Aurora3D VERSION 0.1.0 LANGUAGES CXX)

# Aurora3D only targets Windows: DirectX 12 has no other implementation. Checked
# after project() because WIN32 is only defined once a platform has been loaded.
if(NOT WIN32)
    message(FATAL_ERROR
        "Aurora3D is Windows-only (DirectX 12). On other platforms use ./scripts/verify.sh, "
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

option(AURORA3D_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(AURORA3D_WARNINGS_AS_ERRORS)
    if(MSVC)
        add_compile_options(/WX)
    else()
        add_compile_options(-Werror)
    endif()
endif()

# ----------------------------------------------------------------- library ---
add_library(aurora3d STATIC
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
    src/texture.cpp
    src/video.cpp
    src/internal/gpu.cpp
    src/internal/gamepad.cpp
    src/internal/window.cpp
)

target_include_directories(aurora3d PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(aurora3d PUBLIC
    d3d12 dxgi d3dcompiler windowscodecs ole32 uuid user32 gdi32 shcore
    xinput xaudio2_8 mfplat mfreadwrite mfuuid
)

# The shaders are embedded in the binary by tools/embed_shaders.py, so the library
# is self-contained: no runtime files to ship next to the executable. The generated
# header is committed, so regenerating it is only possible, never required - the
# build works with no Python installed.
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    set(A3D_SHADER_OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/internal/shader_sources.h)
    set(A3D_SHADER_INPUTS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/aurora.hlsli
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/aurora_basic.hlsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/aurora_line.hlsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/aurora_text.hlsl
    )

    add_custom_command(
        OUTPUT ${A3D_SHADER_OUTPUT}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.py
                ${A3D_SHADER_OUTPUT}
                ${A3D_SHADER_INPUTS}
        DEPENDS ${A3D_SHADER_INPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.py
        COMMENT "Embedding HLSL sources into shader_sources.h"
        VERBATIM
    )

    add_custom_target(aurora3d_shaders DEPENDS ${A3D_SHADER_OUTPUT})
    add_dependencies(aurora3d aurora3d_shaders)
else()
    message(STATUS "Python3 not found: using the committed src/internal/shader_sources.h")
endif()

# ------------------------------------------------------------------ install --
install(TARGETS aurora3d ARCHIVE DESTINATION lib)
install(DIRECTORY include/aurora DESTINATION include)
install(DIRECTORY shaders/ DESTINATION share/aurora3d/shaders)

# ----------------------------------------------------------------- examples --
option(AURORA3D_BUILD_EXAMPLES "Build the Aurora3D examples" ON)
if(AURORA3D_BUILD_EXAMPLES)
    file(GLOB A3D_EXAMPLE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp)
    foreach(source ${A3D_EXAMPLE_SOURCES})
        get_filename_component(name ${source} NAME_WE)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE aurora3d)
    endforeach()
endif()
