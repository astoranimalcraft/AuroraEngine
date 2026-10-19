// Aurora3D — default shader pair.
// Compiled at runtime by the engine (D3DCompile) and used by every draw call
// unless the application installs its own HLSL via a3d::SetShader().
#include "aurora.hlsli"

A3D_VertexOutput VSMain(A3D_VertexInput input)
{
    return A3D_TransformVertex(input);
}

float4 PSMain(A3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    return A3D_ShadePixel(input, isFrontFace);
}
