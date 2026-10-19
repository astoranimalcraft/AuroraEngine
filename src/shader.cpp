// Aurora3D — HLSL compilation, root signature and pipeline state caching.
#include "aurora/shader.h"

#include "aurora/log.h"

#include "internal/gpu.h"
#include "internal/resources.h"
#include "internal/shader_sources.h"

#include <d3d12.h>

#include <cstddef>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace a3d {
namespace {

// --------------------------------------------------------- state tracking ----
Shader* gCurrentShader = nullptr;
Shader* gDefaultShader = nullptr;
Shader* gLineShader = nullptr;
Shader* gTextShader = nullptr;
BlendMode gBlendMode = BlendMode::Opaque;
CullMode gCullMode = CullMode::Back;
DepthTest gDepthTest = DepthTest::Less;

std::mutex gShaderMutex;
std::vector<Shader*> gWatchedShaders;

D3D12_BLEND_DESC MakeBlendDesc(BlendMode mode) {
    D3D12_BLEND_DESC desc{};
    D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    switch (mode) {
        case BlendMode::Opaque:
            rt.BlendEnable = FALSE;
            break;
        case BlendMode::AlphaBlend:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            break;
        case BlendMode::Additive:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_ONE;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ONE;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            break;
    }
    return desc;
}

D3D12_RASTERIZER_DESC MakeRasterizerDesc(CullMode mode) {
    D3D12_RASTERIZER_DESC desc{};
    desc.FillMode = D3D12_FILL_MODE_SOLID;
    desc.CullMode = mode == CullMode::Back   ? D3D12_CULL_MODE_BACK
                    : mode == CullMode::Front ? D3D12_CULL_MODE_FRONT
                                              : D3D12_CULL_MODE_NONE;
    // Right-handed geometry with a counter-clockwise front face, which is what
    // the built-in primitives and LookAt() produce.
    desc.FrontCounterClockwise = TRUE;
    desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.DepthClipEnable = TRUE;
    desc.MultisampleEnable = FALSE;
    desc.AntialiasedLineEnable = FALSE;
    desc.ForcedSampleCount = 0;
    desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return desc;
}

D3D12_DEPTH_STENCIL_DESC MakeDepthDesc(DepthTest test, bool depthWrite) {
    D3D12_DEPTH_STENCIL_DESC desc{};
    desc.DepthEnable = TRUE;
    desc.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = test == DepthTest::Less        ? D3D12_COMPARISON_FUNC_LESS
                     : test == DepthTest::LessEqual ? D3D12_COMPARISON_FUNC_LESS_EQUAL
                                                    : D3D12_COMPARISON_FUNC_ALWAYS;
    desc.StencilEnable = FALSE;
    return desc;
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE ToTopologyType(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::TriangleList:
        case PrimitiveTopology::TriangleStrip:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        case PrimitiveTopology::LineList:
        case PrimitiveTopology::LineStrip:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case PrimitiveTopology::PointList:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

const char* TopologyName(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::TriangleList: return "TriangleList";
        case PrimitiveTopology::TriangleStrip: return "TriangleStrip";
        case PrimitiveTopology::LineList: return "LineList";
        case PrimitiveTopology::LineStrip: return "LineStrip";
        case PrimitiveTopology::PointList: return "PointList";
    }
    return "?";
}

// The vertex format every Aurora3D pipeline consumes. Without this the input
// assembler feeds the vertex shader nothing and every draw collapses to a point,
// so it is not optional even for a fixed layout.
const D3D12_INPUT_ELEMENT_DESC kVertexLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position),
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal),
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv),
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color),
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

} // namespace

// ------------------------------------------------------------ Shader::PSO ---
ID3D12PipelineState* Shader::GetPipeline(BlendMode blend, CullMode cull, DepthTest depth) {
    const int b = static_cast<int>(blend);
    const int c = static_cast<int>(cull);
    const int d = static_cast<int>(depth);
    if (pipelineCache[b][c][d]) return pipelineCache[b][c][d].Get();

    ID3D12Device* device = internal::GpuContext::Get().Device();
    if (!device || !vertexBlob || !pixelBlob) return nullptr;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = internal::RootSignature::Instance().Get();
    desc.VS = {vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize()};
    desc.PS = {pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize()};
    desc.BlendState = MakeBlendDesc(blend);
    desc.RasterizerState = MakeRasterizerDesc(cull);
    desc.InputLayout = {kVertexLayout, _countof(kVertexLayout)};
    // Transparent geometry must not occlude itself, so depth writes are disabled
    // for every blended mode; depth testing stays on.
    desc.DepthStencilState = MakeDepthDesc(depth, blend == BlendMode::Opaque);
    desc.PrimitiveTopologyType = ToTopologyType(topology);
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.NodeMask = 0;
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineCache[b][c][d]));
    if (FAILED(hr)) {
        LogError("shader '%s': pipeline creation failed for %s/%d/%d (0x%08lX)",
                 name.c_str(), TopologyName(topology), c, d, static_cast<unsigned long>(hr));
        return nullptr;
    }

    pipelineCache[b][c][d]->SetName(L"Aurora3D Pipeline");
    return pipelineCache[b][c][d].Get();
}

// ---------------------------------------------------------- RootSignature ---
namespace internal {

RootSignature& RootSignature::Instance() {
    static RootSignature instance;
    return instance;
}

bool RootSignature::Create(ID3D12Device* device) {
    if (signature_) return true;

    // Negotiate the highest version the device supports. D3D12SerializeRootSignature
    // only serializes 1.0, so asking it for anything newer fails outright; the
    // versioned entry point below accepts both and needs the version up front.
    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature{};
    feature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature,
                                           sizeof(feature)))) {
        feature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // b0: frame constants, vertex-visible (viewProj is read there).
    // b1: object constants, read by both stages (world in the VS, the rest in the PS).
    // t0: albedo texture, bound as a single-descriptor table.
    //
    // Root constants share a layout across both versions, so they are filled once
    // in the 1.1 form and copied down. Descriptor ranges do not: 1.1 adds a Flags
    // field, so each version needs its own range and table parameter.
    D3D12_DESCRIPTOR_RANGE srvRange10{};
    D3D12_DESCRIPTOR_RANGE1 srvRange11{};
    D3D12_ROOT_PARAMETER parameters10[3]{};
    D3D12_ROOT_PARAMETER1 parameters11[3]{};

    parameters11[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters11[0].Constants.ShaderRegister = 0;
    parameters11[0].Constants.RegisterSpace = 0;
    parameters11[0].Constants.Num32BitValues = kFrameConstantCount;
    parameters11[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters11[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters11[1].Constants.ShaderRegister = 1;
    parameters11[1].Constants.RegisterSpace = 0;
    parameters11[1].Constants.Num32BitValues = kObjectConstantCount;
    parameters11[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    srvRange11.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange11.NumDescriptors = 1;
    srvRange11.BaseShaderRegister = 0;
    srvRange11.RegisterSpace = 0;
    // Descriptors are written once when a texture is created and never rewritten
    // while a command list referencing them is open, so the default (non-volatile)
    // range flags are correct and let the driver cache the table.
    srvRange11.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    srvRange11.OffsetInDescriptorsFromTableStart = 0;

    parameters11[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters11[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters11[2].DescriptorTable.pDescriptorRanges = &srvRange11;
    parameters11[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    srvRange10.RangeType = srvRange11.RangeType;
    srvRange10.NumDescriptors = srvRange11.NumDescriptors;
    srvRange10.BaseShaderRegister = srvRange11.BaseShaderRegister;
    srvRange10.RegisterSpace = srvRange11.RegisterSpace;
    srvRange10.OffsetInDescriptorsFromTableStart = srvRange11.OffsetInDescriptorsFromTableStart;

    for (int i = 0; i < 2; ++i) {
        parameters10[i].ParameterType = parameters11[i].ParameterType;
        parameters10[i].Constants = parameters11[i].Constants;
        parameters10[i].ShaderVisibility = parameters11[i].ShaderVisibility;
    }
    parameters10[2].ParameterType = parameters11[2].ParameterType;
    parameters10[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters10[2].DescriptorTable.pDescriptorRanges = &srvRange10;
    parameters10[2].ShaderVisibility = parameters11[2].ShaderVisibility;

    // s0: one static sampler shared by every shader, so no sampler descriptors
    // are ever needed and the sampler is guaranteed to be in the static path.
    // s1: a point/clamp sampler used by the bitmap-font text shader.
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MipLODBias = 0.0f;
    samplers[0].MaxAnisotropy = 1;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[0].MinLOD = 0.0f;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MipLODBias = 0.0f;
    samplers[1].MaxAnisotropy = 1;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplers[1].MinLOD = 0.0f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc10{};
    desc10.NumParameters = _countof(parameters10);
    desc10.pParameters = parameters10;
    desc10.NumStaticSamplers = _countof(samplers);
    desc10.pStaticSamplers = samplers;
    // Root constants only, so no root-signature flag is needed for CBVs.
    desc10.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    D3D12_ROOT_SIGNATURE_DESC1 desc11{};
    desc11.NumParameters = _countof(parameters11);
    desc11.pParameters = parameters11;
    desc11.NumStaticSamplers = _countof(samplers);
    desc11.pStaticSamplers = samplers;
    desc11.Flags = desc10.Flags;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned{};
    versioned.Version = feature.HighestVersion;
    if (feature.HighestVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1) {
        versioned.Desc_1_1 = desc11;
    } else {
        versioned.Desc_1_0 = desc10;
    }

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&versioned, &blob, &error);
    if (FAILED(hr)) {
        LogError("root signature serialization failed (version %u): %s",
                 static_cast<unsigned>(feature.HighestVersion),
                 error ? static_cast<const char*>(error->GetBufferPointer()) : "(no message)");
        return false;
    }

    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                     IID_PPV_ARGS(&signature_));
    if (FAILED(hr)) {
        LogError("root signature creation failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

// -------------------------------------------------------------- file utils --
bool ReadFileToString(const char* path, std::string* out) {
    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) return false;

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return false;
    }

    out->resize(static_cast<size_t>(size));
    const size_t read = size > 0 ? fread(out->data(), 1, static_cast<size_t>(size), file) : 0;
    fclose(file);
    out->resize(read);
    return true;
}

int64_t FileWriteTime(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return 0;
    return (static_cast<int64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
           data.ftLastWriteTime.dwLowDateTime;
}

// HLSL #include resolution. Relative to the including file first, then the
// engine's own shader directory, then the embedded sources, so
// `#include "aurora.hlsli"` resolves even when nothing is on disk.
class IncludeHandler : public ID3DInclude {
public:
    explicit IncludeHandler(std::string includeDir) : includeDir_(std::move(includeDir)) {}

    HRESULT Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID parentData,
                 LPCVOID* data, UINT* bytes) override {
        // The parent's path is passed as a synthetic name; resolve next to it.
        std::string base;
        if (parentData) base = static_cast<const char*>(parentData);

        std::vector<std::string> candidates;
        const size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos) candidates.push_back(base.substr(0, slash + 1) + fileName);
        if (!includeDir_.empty()) {
            candidates.push_back(includeDir_ + "/" + fileName);
            candidates.push_back(includeDir_ + "\\" + fileName);
        }
        candidates.push_back(fileName);

        for (const std::string& candidate : candidates) {
            std::string source;
            if (!ReadFileToString(candidate.c_str(), &source)) continue;
            return Publish(source, data, bytes);
        }

        // Fall back to the embedded copy, so an application that only ships the
        // executable still compiles its shaders.
        const char* baseName = fileName;
        if (const char* lastSlash = strrchr(fileName, '/')) baseName = lastSlash + 1;
        if (const char* lastBackslash = strrchr(fileName, '\\')) {
            if (lastBackslash + 1 > baseName) baseName = lastBackslash + 1;
        }
        if (const char* embedded = FindEmbeddedShader(baseName)) {
            return Publish(std::string(embedded), data, bytes);
        }
        return E_FAIL;
    }

    HRESULT Close(LPCVOID data) override {
        std::free(const_cast<void*>(data));
        return S_OK;
    }

private:
    HRESULT Publish(const std::string& source, LPCVOID* data, UINT* bytes) {
        char* buffer = static_cast<char*>(std::malloc(source.size() + 1));
        if (!buffer) return E_OUTOFMEMORY;
        std::memcpy(buffer, source.data(), source.size());
        buffer[source.size()] = '\0';
        *data = buffer;
        *bytes = static_cast<UINT>(source.size());
        return S_OK;
    }

    std::string includeDir_;
};

bool CompileShader(const char* source, size_t sourceLength, const char* path,
                   const char* entryPoint, const char* target, const char* includeDir,
                   ID3DBlob** outBlob) {
    IncludeHandler includeHandler(includeDir ? includeDir : "");
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(source, sourceLength, path, nullptr, &includeHandler, entryPoint,
                            target, flags, 0, &blob, &error);
    if (FAILED(hr)) {
        LogError("HLSL compile failed (%s, entry '%s', target %s):\n%s", path, entryPoint, target,
                 error ? static_cast<const char*>(error->GetBufferPointer()) : "(no message)");
        return false;
    }

    if (error && error->GetBufferSize() > 0) {
        LogWarn("HLSL warnings (%s, entry '%s'):\n%s", path, entryPoint,
                static_cast<const char*>(error->GetBufferPointer()));
    }

    *outBlob = blob.Detach();
    return true;
}

} // namespace internal

// -------------------------------------------------------------- public API ---
namespace {

// A stage's source text, resolved once at load time. Either it was read from a
// file (and is then watched for changes) or it is an engine-embedded shader.
struct StageSource {
    std::string text;
    std::string path;          // empty when embedded
    int64_t writeTime = 0;
    const char* entryPoint = nullptr;
};

// Resolves one stage. `path` may be null, in which case `embeddedName` is looked
// up in the built-in shader set.
bool ResolveStage(const char* path, const char* embeddedName, const char* entryPoint,
                  StageSource* out) {
    out->entryPoint = entryPoint;

    if (path) {
        if (!internal::ReadFileToString(path, &out->text)) {
            LogError("cannot read shader file '%s'", path);
            return false;
        }
        out->path = path;
        out->writeTime = internal::FileWriteTime(path);
        return true;
    }

    const char* embedded = internal::FindEmbeddedShader(embeddedName);
    if (!embedded) {
        LogError("no shader named '%s' is embedded in the engine", embeddedName);
        return false;
    }
    out->text = embedded;
    return true;
}

// Derives the #include search directory from the file that contains the entry
// point, falling back to the engine's own "shaders" directory for embedded ones.
std::string IncludeDirFor(const StageSource& vertex, const StageSource& pixel,
                          const char* explicitDir) {
    if (explicitDir) return explicitDir;
    const std::string& source = vertex.path.empty() ? pixel.path : vertex.path;
    if (source.empty()) return "shaders";
    const size_t slash = source.find_last_of("/\\");
    return slash == std::string::npos ? "." : source.substr(0, slash);
}

// Compiles both stages, builds one PSO as a smoke test, and registers the shader
// with the hot-reload watcher when asked. Ownership passes to the caller on
// success; the shader is deleted and nullptr returned on failure.
Shader* FinishShader(Shader* shader, const StageSource& vertex, const StageSource& pixel,
                     const char* debugName) {
    shader->name = debugName ? debugName : "(unnamed)";
    shader->vertexEntry = vertex.entryPoint;
    shader->pixelEntry = pixel.entryPoint;
    shader->vertexSource = vertex.text;
    shader->pixelSource = pixel.text;
    shader->vertexPath = vertex.path;
    shader->pixelPath = pixel.path;
    shader->vertexWriteTime = vertex.writeTime;
    shader->pixelWriteTime = pixel.writeTime;

    const char* includeDir = shader->includeDir.empty() ? nullptr : shader->includeDir.c_str();
    // The synthetic name the compiler reports diagnostics against and uses as the
    // parent for relative includes.
    const char* vertexName = vertex.path.empty() ? shader->name.c_str() : vertex.path.c_str();
    const char* pixelName = pixel.path.empty() ? shader->name.c_str() : pixel.path.c_str();

    if (!internal::CompileShader(shader->vertexSource.c_str(), shader->vertexSource.size(),
                                 vertexName, shader->vertexEntry.c_str(), "vs_5_1", includeDir,
                                 &shader->vertexBlob)) {
        delete shader;
        return nullptr;
    }
    if (!internal::CompileShader(shader->pixelSource.c_str(), shader->pixelSource.size(),
                                 pixelName, shader->pixelEntry.c_str(), "ps_5_1", includeDir,
                                 &shader->pixelBlob)) {
        delete shader;
        return nullptr;
    }

    // Build one PSO up front so a shader that cannot form a pipeline is rejected
    // at load time rather than at the first draw call.
    if (!shader->GetPipeline(BlendMode::Opaque, CullMode::Back, DepthTest::Less)) {
        delete shader;
        return nullptr;
    }

    if (shader->watchForChanges) {
        std::lock_guard<std::mutex> lock(gShaderMutex);
        gWatchedShaders.push_back(shader);
    }

    LogInfo("shader '%s' ready (%s)", shader->name.c_str(), TopologyName(shader->topology));
    return shader;
}

Shader* BuildShader(const StageSource& vertex, const StageSource& pixel, const char* includeDir,
                    bool watchForChanges, const char* debugName) {
    auto* shader = new Shader();
    shader->includeDir = IncludeDirFor(vertex, pixel, includeDir);
    // Only a shader whose stages both live on disk can be hot-reloaded.
    shader->watchForChanges = watchForChanges && !vertex.path.empty() && !pixel.path.empty();
    return FinishShader(shader, vertex, pixel, debugName);
}

} // namespace

Shader* LoadShaderFromMemory(const char* hlslSource, const char* debugName) {
    if (!hlslSource) {
        LogError("LoadShaderFromMemory: null source");
        return nullptr;
    }

    StageSource stage;
    stage.text = hlslSource;
    stage.entryPoint = "VSMain";
    StageSource pixelStage;
    pixelStage.text = hlslSource;
    pixelStage.entryPoint = "PSMain";

    return BuildShader(stage, pixelStage, "shaders", false, debugName ? debugName : "memory");
}

Shader* LoadShader(const ShaderDesc& desc) {
    if (desc.source) return LoadShaderFromMemory(desc.source, desc.debugName);

    StageSource vertex;
    StageSource pixel;

    if (desc.vertexPath && desc.pixelPath) {
        if (!ResolveStage(desc.vertexPath, nullptr, desc.vertexEntry, &vertex)) return nullptr;
        if (!ResolveStage(desc.pixelPath, nullptr, desc.pixelEntry, &pixel)) return nullptr;
        return BuildShader(vertex, pixel, desc.includeDir, desc.watchForChanges,
                           desc.debugName ? desc.debugName : desc.vertexPath);
    }

    if (desc.vertexPath) {
        // One file holding both stages.
        if (!ResolveStage(desc.vertexPath, nullptr, desc.vertexEntry, &vertex)) return nullptr;
        pixel = vertex;
        pixel.entryPoint = desc.pixelEntry;
        return BuildShader(vertex, pixel, desc.includeDir, desc.watchForChanges,
                           desc.debugName ? desc.debugName : desc.vertexPath);
    }

    LogError("LoadShader: no source and no paths supplied");
    return nullptr;
}

Shader* LoadShaderFromFile(const char* path, bool watchForChanges) {
    if (!path) return nullptr;
    ShaderDesc desc{};
    desc.vertexPath = path;
    desc.watchForChanges = watchForChanges;
    desc.debugName = path;
    return LoadShader(desc);
}

Shader* LoadShaderFromFiles(const char* vertexPath, const char* pixelPath, bool watchForChanges) {
    ShaderDesc desc{};
    desc.vertexPath = vertexPath;
    desc.pixelPath = pixelPath;
    desc.watchForChanges = watchForChanges;
    desc.debugName = vertexPath;
    return LoadShader(desc);
}

void UnloadShader(Shader* shader) {
    if (!shader) return;
    if (shader == gDefaultShader || shader == gLineShader || shader == gTextShader) return;   // owned by the engine

    {
        std::lock_guard<std::mutex> lock(gShaderMutex);
        for (auto it = gWatchedShaders.begin(); it != gWatchedShaders.end(); ++it) {
            if (*it == shader) {
                gWatchedShaders.erase(it);
                break;
            }
        }
    }

    if (gCurrentShader == shader) gCurrentShader = gDefaultShader;

    // Pipeline states reference the shader's blobs, so make sure the GPU is done
    // with any in-flight frame before the object disappears.
    internal::GpuContext::Get().WaitForIdle();
    delete shader;
}

void SetShader(Shader* shader) { gCurrentShader = shader ? shader : gDefaultShader; }
Shader* GetCurrentShader() { return gCurrentShader; }
Shader* GetDefaultShader() { return gDefaultShader; }

void SetBlendMode(BlendMode mode) { gBlendMode = mode; }
void SetCullMode(CullMode mode) { gCullMode = mode; }
void SetDepthTest(DepthTest test) { gDepthTest = test; }

BlendMode GetBlendMode() { return gBlendMode; }
CullMode GetCullMode() { return gCullMode; }
DepthTest GetDepthTest() { return gDepthTest; }

// ------------------------------------------------------- state blocks -------
// BeginShader() / EndShader() and the push/pop pairs save the four pieces of
// draw state, so a block can be entered and left without the caller having to
// remember what was set before it.
namespace {
struct StateFrame {
    Shader* shader = nullptr;
    BlendMode blend = BlendMode::Opaque;
    CullMode cull = CullMode::Back;
    DepthTest depth = DepthTest::Less;
};
std::vector<StateFrame> gStateStack;
} // namespace

void BeginShader(Shader* shader) {
    gStateStack.push_back(StateFrame{gCurrentShader, gBlendMode, gCullMode, gDepthTest});
    gCurrentShader = shader ? shader : gDefaultShader;
}

void EndShader() {
    if (gStateStack.empty()) return;
    const StateFrame frame = gStateStack.back();
    gStateStack.pop_back();
    gCurrentShader = frame.shader ? frame.shader : gDefaultShader;
    gBlendMode = frame.blend;
    gCullMode = frame.cull;
    gDepthTest = frame.depth;
}

int GetShaderStackDepth() { return static_cast<int>(gStateStack.size()); }

// Used by the renderer and the engine's own initialization.
namespace internal {
BlendMode CurrentBlendMode() { return gBlendMode; }
CullMode CurrentCullMode() { return gCullMode; }
DepthTest CurrentDepthTest() { return gDepthTest; }

Shader* LineShader() { return gLineShader; }
Shader* TextShader() { return gTextShader; }

// Loads one of the engine's own shaders. A copy next to the executable wins over
// the embedded source, so a user can drop in a modified aurora_basic.hlsl without
// rebuilding the engine.
Shader* LoadBuiltin(const char* fileName, const char* debugName) {
    const std::string inShadersDir = std::string("shaders/") + fileName;
    const char* search[] = {fileName, inShadersDir.c_str()};

    for (const char* path : search) {
        if (FileWriteTime(path) == 0) continue;   // not present on disk

        StageSource vertex;
        StageSource pixel;
        if (!ResolveStage(path, fileName, "VSMain", &vertex)) return nullptr;
        pixel = vertex;
        pixel.entryPoint = "PSMain";

        const std::string full = vertex.path;
        const size_t slash = full.find_last_of("/\\");
        const std::string directory = slash == std::string::npos ? "." : full.substr(0, slash);
        return BuildShader(vertex, pixel, directory.c_str(), false, debugName);
    }

    // Nothing on disk; use the embedded copy.
    StageSource vertex;
    StageSource pixel;
    if (!ResolveStage(nullptr, fileName, "VSMain", &vertex)) return nullptr;
    pixel = vertex;
    pixel.entryPoint = "PSMain";
    return BuildShader(vertex, pixel, "shaders", false, debugName);
}

bool CreateBuiltinShaders() {
    gDefaultShader = LoadBuiltin("aurora_basic.hlsl", "aurora_basic");
    gLineShader = LoadBuiltin("aurora_line.hlsl", "aurora_line");
    gTextShader = LoadBuiltin("aurora_text.hlsl", "aurora_text");
    if (!gDefaultShader || !gLineShader || !gTextShader) {
        LogError("failed to create the built-in shaders");
        return false;
    }

    gLineShader->topology = PrimitiveTopology::LineList;
    gCurrentShader = gDefaultShader;
    return true;
}

void DestroyBuiltinShaders() {
    gCurrentShader = nullptr;
    delete gDefaultShader;
    delete gLineShader;
    delete gTextShader;
    gDefaultShader = nullptr;
    gLineShader = nullptr;
    gTextShader = nullptr;
}
} // namespace internal

void ReloadChangedShaders() {
    std::vector<Shader*> changed;
    {
        std::lock_guard<std::mutex> lock(gShaderMutex);
        for (Shader* shader : gWatchedShaders) {
            if (!shader || shader->vertexPath.empty()) continue;
            const int64_t vertexTime = internal::FileWriteTime(shader->vertexPath.c_str());
            const int64_t pixelTime = shader->pixelPath.empty()
                                          ? vertexTime
                                          : internal::FileWriteTime(shader->pixelPath.c_str());
            if (vertexTime != shader->vertexWriteTime || pixelTime != shader->pixelWriteTime)
                changed.push_back(shader);
        }
    }
    if (changed.empty()) return;

    for (Shader* shader : changed) {
        // Hot reload: recompile both stages and rebuild the PSO cache. The old
        // pipeline states are released only after the GPU is idle, because they
        // may still be referenced by an in-flight command list.
        std::string vertexSource;
        std::string pixelSource;
        if (!internal::ReadFileToString(shader->vertexPath.c_str(), &vertexSource)) continue;
        if (!internal::ReadFileToString(shader->pixelPath.c_str(), &pixelSource)) continue;

        const char* includeDir = shader->includeDir.empty() ? nullptr : shader->includeDir.c_str();
        ComPtr<ID3DBlob> newVertex;
        ComPtr<ID3DBlob> newPixel;
        if (!internal::CompileShader(vertexSource.c_str(), vertexSource.size(),
                                     shader->vertexPath.c_str(), shader->vertexEntry.c_str(),
                                     "vs_5_1", includeDir, &newVertex))
            continue;
        if (!internal::CompileShader(pixelSource.c_str(), pixelSource.size(),
                                     shader->pixelPath.c_str(), shader->pixelEntry.c_str(),
                                     "ps_5_1", includeDir, &newPixel))
            continue;

        internal::GpuContext::Get().WaitForIdle();

        for (auto& b : shader->pipelineCache)
            for (auto& c : b)
                for (auto& d : c) d.Reset();

        shader->vertexBlob = newVertex;
        shader->pixelBlob = newPixel;
        shader->vertexSource = vertexSource;
        shader->pixelSource = pixelSource;
        shader->vertexWriteTime = internal::FileWriteTime(shader->vertexPath.c_str());
        shader->pixelWriteTime = internal::FileWriteTime(shader->pixelPath.c_str());

        if (shader->GetPipeline(gBlendMode, gCullMode, gDepthTest))
            LogInfo("hot-reloaded shader '%s'", shader->name.c_str());
    }
}

} // namespace a3d
