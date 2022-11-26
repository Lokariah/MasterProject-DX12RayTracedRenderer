#include "common.hlsli"

vertexOut VS(vertexIn vIn)
{
    vertexOut vOut;
    vOut.posH = mul(float4(vIn.posL, 1.0f), gWorldViewProj);
    vOut.colour = vIn.colour;
    return vOut;
}