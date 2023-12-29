#include "common.hlsli"

float4 PS(blinnPhongLightingPixelShaderInput pIn) : SV_TARGET
{
	// Renormalise pixel normal because interpolation from the vertex shader can introduce scaling
    float3 worldNormal = normalize(pIn.worldNormal);
	 
	// Blinn-Phong lighting using a single light
    float3 lightVector = gLight1Dir - pIn.worldPos; // Vector to light
    float lightDistance = length(lightVector); // Distance to light
    float3 lightNormal = lightVector / lightDistance; // Normal to light
    float3 cameraNormal = normalize(gCameraPos - pIn.worldPos); // Normal to camera
    float3 halfwayNormal = normalize(cameraNormal + lightNormal); // Halfway normal is halfway between camera and light normal - used for specular lighting

	// Attenuate light colour (reduce strength based on its distance)
    float3 attenuatedLightColour = gLight1Colour.rgb / lightDistance;

	// Diffuse lighting
    float lightDiffuseLevel = saturate(dot(worldNormal, lightNormal));
    float3 lightDiffuseColour = attenuatedLightColour * lightDiffuseLevel;

	// Specular lighting
    float specPow = 256;
    float lightSpecularLevel = pow(saturate(dot(worldNormal, halfwayNormal)), specPow);
    float3 lightSpecularColour = lightDiffuseColour * lightSpecularLevel; // Using diffuse light colour rather than plain attenuated colour - own adjustment to Blinn-Phong model


	// Diffuse colour combines material colour with per-model colour - alpha will also be combined here and used for output pixel alpha
    float4 materialDiffuseColour = pIn.colour;
    float3 specColour = { 1.0f, 1.0f, 1.0f };
    float3 ambColour = { 0.4f, 0.4f, 0.4f };
	// Combmine all colours, adding ambient light as part of the process
    float3 finalColour = materialDiffuseColour.rgb * (ambColour + lightDiffuseColour) + specColour * lightSpecularColour;

    return float4(finalColour, materialDiffuseColour.a);
}

//DontTryToDoPointLights,ConvertPointLightIntoDirectionalLight