cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld; //used
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gViewProj; //used
    
    float3 gLight1Dir;
    float gPadding1;
    float3 gLight1Colour;
    float gPadding2;
    
    //float4x4 gInvProj;
    //float4x4 gInvViewProj;
    
    float3 gCameraPos;
    float padding3;
    
    //float2 gRenderTargetSize;
    //float2 gInvRenderTargetSize;
    
    //float gNearZ;
    //float gFarZ;
    //float gTotalTime;
    //float gFrameTime;
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
    float3 posN : NORMAL;
    float4 colour : COLOUR;
};

struct vertexOut
{
    float4 posH : SV_Position;
    //float4 colour : COLOUR;
};

struct blinnPhongLightingPixelShaderInput
{
    float4 clipPos : SV_Position;
    float3 worldPos : worldPosition;
    float3 worldNormal : worldNormal;
    float4 colour : COLOUR;
};

struct blinnPhongLightingPixelShaderOutput
{
    float4 colourOutput : SV_Target0;
};
