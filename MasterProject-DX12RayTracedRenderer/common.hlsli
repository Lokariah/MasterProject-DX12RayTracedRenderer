cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    
    float3 gEyePos;
    float padding1;
    
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gFrameTime;
};

//cbuffer cbRtPerFrame : register(b0)
//{
//    float3 gTriangleColour1[3];
//    float3 gTriangleColour2[3];
//    float3 gTriangleColour3[3];
//}

struct vertexIn
{
    float3 posL : POSITION;
    float4 colour : COLOUR;
};

struct vertexOut
{
    float4 posH : SV_Position;
    float4 colour : COLOUR;
};