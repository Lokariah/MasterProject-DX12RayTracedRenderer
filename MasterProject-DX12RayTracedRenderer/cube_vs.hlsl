#include "common.hlsli"

vertexOut VS(vertexIn vIn)
{
    vertexOut vOut;
    float4 posW = mul(float4(vIn.posL, 1.0f), gWorld);
    vOut.posH = mul(posW, gViewProj);
    vOut.colour = vIn.colour;
    return vOut;
}