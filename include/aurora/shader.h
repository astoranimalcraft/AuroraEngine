// Aurora3D — shader.h
// Custom HLSL support. A Shader wraps a compiled vertex/pixel pair plus the
// pipeline state object built for it.
//
// Requirements for user shaders: include "aurora.hlsli" and declare entry points
// named VSMain / PSMain that follow the A3D_VertexInput / A3D_VertexOutput
// contract. Because the root signature is fixed, that is all the engine needs.
#pragma once

#include <cstdint>

#include "aurora/color.h"

namespace a3d {

struct Shader;

enum class BlendMode {
    Opaque,      // depth write on, no blending
    AlphaBlend,  // src-alpha / one-minus-src-alpha, depth write off
    Additive,    // src-alpha / one, depth write off
};

enum class CullMode {
    Back,
    Front,
    None,
};

// Primitive topology a shader's pipeline is built for. Fixed per shader, so it is
// not part of the render-state key.
enum class PrimitiveTopology {
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip,
    PointList,
};

enum class DepthTest {
    Less,        // standard opaque
    LessEqual,
    Always,      // overlay / debug geometry
};

struct ShaderDesc {
    // Source text. When `vertexPath` / `pixelPath` are set instead, the files are
    // read from disk and `source` is ignored.
    const char* source = nullptr;
    const char* vertexPath = nullptr;
    const char* pixelPath = nullptr;
    const char* vertexEntry = "VSMain";
    const char* pixelEntry = "PSMain";
    // Directory prepended to #include "..." lookups. Defaults to the directory of
    // the shader file, or the engine's shader directory for inline source.
    const char* includeDir = nullptr;
    bool watchForChanges = false;   // recompile when the file's mtime changes
    const char* debugName = nullptr;
};

// Compiles and creates the pipeline. Returns nullptr on failure (errors logged
// with file/line information from the HLSL compiler).
Shader* LoadShaderFromMemory(const char* hlslSource, const char* debugName = nullptr);
Shader* LoadShader(const ShaderDesc& desc);
Shader* LoadShaderFromFile(const char* path, bool watchForChanges = false);
Shader* LoadShaderFromFiles(const char* vertexPath, const char* pixelPath,
                            bool watchForChanges = false);

void UnloadShader(Shader* shader);

// Binds the shader for subsequent draws until changed. Passing nullptr restores
// the built-in default shader.
void SetShader(Shader* shader);
Shader* GetCurrentShader();
Shader* GetDefaultShader();

// Renders with the given state until changed. These map onto pipeline state
// objects, so switching is cheap but not free — batch draws that share state.
void SetBlendMode(BlendMode mode);
void SetCullMode(CullMode mode);
void SetDepthTest(DepthTest test);
BlendMode GetBlendMode();
CullMode GetCullMode();
DepthTest GetDepthTest();

// ------------------------------------------------------------ state blocks --
// BeginShader() saves the current shader and render state, then makes `shader`
// active (nullptr restores the built-in default). EndShader() restores exactly
// what was saved. Nesting is supported and each pair must be balanced.
//
//     a3d::BeginShader(myShader);
//     a3d::SetBlendMode(a3d::BlendMode::Additive);
//     a3d::DrawMesh(sphere, pos, rot, scale, a3d::colors::White);
//     a3d::EndShader();
//
// This is the Aurora3D spelling of "start using my own HLSL here".
void BeginShader(Shader* shader);
void EndShader();
// Number of BeginShader() blocks currently open, for assertions.
int GetShaderStackDepth();

// Recompiles every shader created with watchForChanges whose source file changed.
// Called automatically once per frame; exposed for manual control.
void ReloadChangedShaders();

} // namespace a3d
