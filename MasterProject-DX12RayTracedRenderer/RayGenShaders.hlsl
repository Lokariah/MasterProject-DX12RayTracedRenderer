RaytracingAccelerationStructure gRtScene : register(t0);
RWTexture2D<float4> gOutput : register(u0);


cbuffer cbRtPerFrame : register(b0)
{
    float3 gTriangleColour1;
    float3 gTriangleColour2;
    float3 gTriangleColour3;
}
//#include "common.hlsli"

float3 linearToSrgb(float3 c)
{
    // Based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * c;
    return srgb;
}

struct RayPayload
{
    float3 color;
};

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    float2 crd = float2(launchIndex.xy);
    float2 dims = float2(launchDim.xy);

    float2 d = ((crd / dims) * 2.f - 1.f);
    float aspectRatio = dims.x / dims.y;

    RayDesc ray;
    ray.Origin = float3(0, 0, -2);
    ray.Direction = normalize(float3(d.x * aspectRatio, -d.y, 1));

    ray.TMin = 0;
    ray.TMax = 100000;

    RayPayload payload;
    TraceRay(gRtScene, 0 /*rayFlags*/, 0xFF, 0 /* ray index*/, 2, 0, ray, payload);
    float3 col = linearToSrgb(payload.color);
    gOutput[launchIndex.xy] = float4(col, 1);
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.color = float3(0.4, 0.6, 0.2);
}

[shader("closesthit")]
void Hit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);

    payload.color = gTriangleColour1 * barycentrics.x + gTriangleColour2 * barycentrics.y + gTriangleColour3 * barycentrics.z;
}

struct ShadowPayload
{
    bool hit;
};

[shader("closesthit")]
void PlaneHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float hitT = RayTCurrent();
    float3 rayDirWorld = WorldRayDirection();
    float3 rayOriginWorld = WorldRayOrigin();
    
    float3 posW = rayOriginWorld + hitT * rayDirWorld;
    
    RayDesc ray;
    ray.Origin = posW;
    ray.Direction = normalize(float3(0.5f, 0.5f, -0.5f));
    ray.TMin = 0.01f;
    ray.TMax = 100000.0f;
    ShadowPayload shadowPayload;
    TraceRay(gRtScene, 0 /*rayFlags*/, 0xFF, 1 /* ray index*/, 0, 1, ray, shadowPayload);
    
    float factor = shadowPayload.hit ? 0.1f : 1.0f;
    payload.color = float4(0.9f, 0.9f, 0.9f, 1.0f) * factor;
}

[shader("closesthit")]
void ShadowHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.hit = true;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.hit = false;
}