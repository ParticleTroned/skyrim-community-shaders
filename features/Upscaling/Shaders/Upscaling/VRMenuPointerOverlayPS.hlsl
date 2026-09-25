struct PointerInput
{
	float4 position: SV_POSITION0;
	float4 texCoord: TEXCOORD0;
	float4 worldPosition: POSITION1;
	float4 colour: COLOR0;
};

float4 main(PointerInput input) : SV_Target
{
	return float4(saturate(input.colour.rgb), 1.0);
}
