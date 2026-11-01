Texture2D<float4> Scene : register(t0);
float4 VSMain(uint vertex : SV_VertexID) : SV_Position
{
	const float2 position = float2((vertex << 1u) & 2u, vertex & 2u);
	return float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 PSMain(float4 position : SV_Position) : SV_Target0
{
	return Scene.Load(int3(uint2(position.xy), 0));
}
