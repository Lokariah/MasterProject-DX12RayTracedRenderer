cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
};

struct vertexIn
{
    float3 posL : POSITION;
    float4 colour : COLOR;
};

struct vertexOut
{
    float4 posH : SV_Position;
    float4 colour : COLOR;
};