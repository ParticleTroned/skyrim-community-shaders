#define main UtilityPixelMain
#include "Utility.hlsl"
#undef main

cbuffer TestParameters : register(b9)
{
	float4 Cases[8];
};

float4 VertexMain(uint vertex : SV_VertexID) : SV_POSITION
{
	return float4(float2((vertex << 1) & 2, vertex & 2) * float2(2, -2) + float2(-1, 1), 0, 1);
}

#ifdef PSHADER
PS_OUTPUT PixelMain(float4 position : SV_POSITION)
{
	PS_INPUT input = (PS_INPUT)0;
	input.PositionCS = position;
	input.TexCoord1 = Cases[uint(position.x)];
	return UtilityPixelMain(input);
}
#endif
