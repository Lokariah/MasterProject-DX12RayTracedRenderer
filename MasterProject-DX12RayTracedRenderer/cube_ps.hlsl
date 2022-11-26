#include "common.hlsli"

float4 PS(vertexOut pIn) : SV_TARGET
{
    return pIn.colour;
}