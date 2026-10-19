// Aurora3D — wireframe / debug-line shader pair.
// Uses the exact same binding layout as aurora_basic.hlsl, so the engine can swap
// between them without rebuilding the root signature.
#include "aurora.hlsli"

A3D_VertexOutput VSMain(A3D_VertexInput input)
{
    return A3D_TransformVertex(input);
}

float4 PSMain(A3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    // Vertex colors carry the line color; no lighting, no texture.
    return input.color;
}
