// NexiumEngine — wireframe / debug-line shader pair.
// Uses the exact same binding layout as nexium_basic.hlsl, so the engine can swap
// between them without rebuilding the root signature.
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    // Vertex colors carry the line color; no lighting, no texture.
    return input.color;
}
