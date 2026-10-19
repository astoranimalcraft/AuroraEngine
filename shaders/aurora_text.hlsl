// Aurora3D — bitmap-font text shader.
// Like aurora_basic but samples the glyph atlas with the point/clamp sampler so
// the built-in font stays sharp at any scale.
#include "aurora.hlsli"

A3D_VertexOutput VSMain(A3D_VertexInput input)
{
    return A3D_TransformVertex(input);
}

float4 PSMain(A3D_VertexOutput input) : SV_TARGET
{
    return A3D_SampleGlyph(input);
}
