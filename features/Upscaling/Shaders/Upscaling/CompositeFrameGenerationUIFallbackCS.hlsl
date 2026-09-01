Texture2D<float4> UITexture : register(t0);
RWTexture2D<float4> SceneTexture : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint width;
	uint height;
	SceneTexture.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	const float4 scene = SceneTexture[dispatchID.xy];
	const float4 ui = UITexture[dispatchID.xy];
	SceneTexture[dispatchID.xy] = float4(
		ui.rgb + scene.rgb * (1.0 - saturate(ui.a)),
		ui.a + scene.a * (1.0 - saturate(ui.a)));
}
