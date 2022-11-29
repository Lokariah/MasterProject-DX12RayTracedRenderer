cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj = float4x4(
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1);
};

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