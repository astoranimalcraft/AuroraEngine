// Aurora3D — internal resource definitions.
//
// The public headers expose Texture / Mesh / Shader as opaque forward-declared
// types. Their real definitions live here so that the renderer, the resource
// loaders and the shader system can all reach them without leaking D3D12 into
// application code.
#pragma once

#include "aurora/shader.h"

#include "internal/common.h"

#include <d3d12.h>

#include <string>
#include <vector>

namespace a3d {

// The public headers keep Texture/Mesh/Shader opaque, so the COM smart pointer
// alias has to be visible here too.
using Microsoft::WRL::ComPtr;

// ------------------------------------------------------------------ Texture --
struct Texture {
    ComPtr<ID3D12Resource> resource;
    uint32_t width = 0;
    uint32_t height = 0;
    // The SRV lives in a dedicated slot in the global heap, allocated once at
    // creation and stable for the texture's lifetime.
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
    std::string name;
    bool isDefault = false;
};

// --------------------------------------------------------------------- Font --
// A bitmap font: one texture holding an ASCII grid of glyphs plus the cell size.
// The engine's built-in font uses a procedurally generated atlas (see
// internal/font_data.h); a user font loads the same grid from an image.
struct Font {
    Texture* texture = nullptr;
    int glyphWidth = 5;
    int glyphHeight = 7;
    int columns = 16;
    int firstChar = 32;
    int lastChar = 126;
    float spacing = 1.0f;      // extra pixels between glyphs, in font pixels
    float lineHeight = 9.0f;   // pixels between baselines, in font pixels
    std::string name;
    bool isDefault = false;
    bool ownsTexture = false;
};

// --------------------------------------------------------------------- Mesh --
struct Mesh {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t flags = 0;
    Bounds bounds;
    std::string name;
    // Primitives are cached and shared; the application must not free them.
    bool isBuiltin = false;
};

// ------------------------------------------------------------------- Shader --
// A compiled shader owns its pipeline states. Because the root signature is
// fixed, a PSO only varies with render state and primitive topology, so the
// variants are built lazily and cached.
struct Shader {
    ComPtr<ID3DBlob> vertexBlob;
    ComPtr<ID3DBlob> pixelBlob;

    // Source location, kept so watchForChanges can recompile on file edits. When a
    // stage came from an embedded shader instead of a file its path stays empty.
    std::string vertexPath;
    std::string pixelPath;

    // Source text of each stage, retained for hot reload.
    std::string vertexSource;
    std::string pixelSource;
    std::string includeDir;
    std::string vertexEntry = "VSMain";
    std::string pixelEntry = "PSMain";
    int64_t vertexWriteTime = 0;
    int64_t pixelWriteTime = 0;
    bool watchForChanges = false;

    PrimitiveTopology topology = PrimitiveTopology::TriangleList;

    // PSO cache indexed by [blend][cull][depth].
    ComPtr<ID3D12PipelineState> pipelineCache[3][3][3];
    std::string name;

    // Creates (or fetches from the cache) the PSO for the given render state.
    ID3D12PipelineState* GetPipeline(BlendMode blend, CullMode cull, DepthTest depth);
};

namespace internal {

// The root signature every Aurora3D shader is built against. Created once.
class RootSignature {
public:
    static RootSignature& Instance();
    bool Create(ID3D12Device* device);
    ID3D12RootSignature* Get() const { return signature_.Get(); }
    void Destroy() { signature_.Reset(); }

private:
    ComPtr<ID3D12RootSignature> signature_;
};

// Compiles HLSL source into a shader blob. `path` is used for #include
// resolution and for compiler diagnostics; it may be a synthetic name.
bool CompileShader(const char* source, size_t sourceLength, const char* path,
                   const char* entryPoint, const char* target, const char* includeDir,
                   ID3DBlob** outBlob);

// Reads a file into memory. Returns false and logs on failure.
bool ReadFileToString(const char* path, std::string* out);

// Last-write time of a file, or 0 when it does not exist.
int64_t FileWriteTime(const char* path);

// ------------------------------------------------------------------ shaders --
// The renderer needs the render state the application last selected, plus the
// engine's own shaders, none of which belong in the public header.
BlendMode CurrentBlendMode();
CullMode CurrentCullMode();
DepthTest CurrentDepthTest();

// Built-in shaders, created once during a3d::Init().
bool CreateBuiltinShaders();
void DestroyBuiltinShaders();
Shader* LineShader();
Shader* TextShader();

// Built-in bitmap font, created once during a3d::Init().
bool CreateBuiltinFont();
void DestroyBuiltinFont();

} // namespace internal
} // namespace a3d
