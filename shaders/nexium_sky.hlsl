// NexiumEngine — built-in environment skybox shader.
//
// Draws an equirectangular environment map as a cube centred on the camera. The
// vertex stage is the stock transform; the pixel stage turns the direction to the
// fragment back into the map's longitude/latitude UV. Nothing is lit here — the
// sky is the light.
#include "nexium.hlsli"

static const float N3D_PI  = 3.14159265358979323846f;
static const float N3D_TAU = 6.28318530717958647692f;

N3D_VertexOutput VSMain(N3D_VertexInput input)
{
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input) : SV_TARGET
{
    // The cube is centred on the eye, so the direction is just the normalised
    // offset from the camera.
    float3 dir = normalize(input.worldPos - N3D_CameraPosition());

    // Longitude from the azimuth, latitude from the elevation. This is the inverse
    // of the mapping an equirectangular image is generated with.
    float u = atan2(dir.x, dir.z) / N3D_TAU + 0.5f;
    float v = acos(clamp(dir.y, -1.0f, 1.0f)) / N3D_PI;

    // s0 wraps in U, so the longitude seam between u = 0 and u = 1 is invisible.
    // The tint scales the whole sky, which is how an application dims it without
    // touching the image.
    float3 sky = albedoTexture.Sample(albedoSampler, float2(u, v)).rgb;
    return float4(sky * baseColor.rgb, 1.0f);
}
