struct VS_OUTPUT
{
	float4 position : SV_POSITION;
};

VS_OUTPUT main(uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	output.position.x = (float)(vertexID / 2) * 4.0 - 1.0;
	output.position.y = (float)(vertexID % 2) * 4.0 - 1.0;
	output.position.z = 0.0;
	output.position.w = 1.0;

	return output;
}
