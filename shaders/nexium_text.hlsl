// NexiumEngine — bitmap-font text shader.
// Like nexium_basic but samples the glyph atlas with the point/clamp sampler so
// the built-in font stays sharp at any scale.
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input) : SV_TARGET
{
    return N3D_SampleGlyph(input);
}
