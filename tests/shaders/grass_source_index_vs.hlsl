#include "Common/Math.hlsli"
#include "GrassOptimizations/GrassBatch.hlsli"

struct Input
{
	float4 a: TEXCOORD4;
	float4 b: TEXCOORD5;
	float4 c: TEXCOORD6;
	float4 d: TEXCOORD7;
};

struct Output
{
	float4 a: TEXCOORD0;
	float4 b: TEXCOORD1;
	float4 c: TEXCOORD2;
	float4 d: TEXCOORD3;
};

Output main(Input input)
{
	LoadGrassBatchInstance(input.a, input.b, input.c, input.d);
	Output output;
	output.a = input.a;
	output.b = input.b;
	output.c = input.c;
	output.d = input.d;
	return output;
}
