#include "common.hlsli"

blinnPhongLightingPixelShaderInput VS(vertexIn vIn)
{
    blinnPhongLightingPixelShaderInput vOut;
    float4 posW = mul(float4(vIn.posL, 1.0f), gWorld);
    vOut.worldPos = posW;
    vOut.clipPos = mul(posW, gViewProj);
    float4 posN = mul(float4(vIn.posN, 1.0f), gWorld);
    vOut.worldNormal = posN;
    vOut.colour = vIn.colour;
    return vOut;
}