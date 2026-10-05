struct Input
{
	float4 position: POSITION0;
	float2 uv: TEXCOORD0;
	float4 normal: NORMAL0;
	float4 color: COLOR0;
	float4 a: TEXCOORD4;
	float4 b: TEXCOORD5;
	float4 c: TEXCOORD6;
	float4 d: TEXCOORD7;
};
float4 main(Input input) : SV_POSITION
{
	return input.position + float4(input.uv, 0, 0) + input.normal + input.color + input.a + input.b + input.c + input.d;
}
