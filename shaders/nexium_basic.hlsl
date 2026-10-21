// NexiumEngine — default shader pair.
// Compiled at runtime by the engine (D3DCompile) and used by every draw call
// unless the application installs its own HLSL via n3d::SetShader().
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    return N3D_ShadePixel(input, isFrontFace);
}
