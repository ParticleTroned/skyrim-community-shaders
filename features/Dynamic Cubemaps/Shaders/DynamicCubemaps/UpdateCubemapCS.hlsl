#include "DynamicCubemaps/CaptureCommon.hlsli"

[numthreads(8, 8, 1)] void main(uint3 texel : SV_DispatchThreadID) {
	uint width, height, faces;
	DynamicCubemap.GetDimensions(width, height, faces);
	if (texel.x >= width || texel.y >= height || texel.z >= faces)
		return;

	float4 history = 0.0;
	float4 historyPosition = 0.0;
	float4 capture = 0.0;
	if (LightingState[0].Reset == 0) {
		history = DynamicCubemapRaw[texel];
		historyPosition = DynamicCubemapPosition[texel];
		historyPosition.xyz = AdjustCapturePosition(historyPosition.xyz);
		capture = DynamicCubemap[texel];
		if (CaptureHistoryExpired(historyPosition.w) || !all(isfinite(historyPosition)) ||
			!all(isfinite(history)) || !all(isfinite(capture))) {
			history = 0.0;
			historyPosition = 0.0;
			capture = 0.0;
		}
	}

	float3 position, color;
	float2 uv;
	if (SampleCapture(texel, position, color, uv)) {
		float blend = history.a >= 0.9 && CaptureGeometryMatches(historyPosition.xyz, position) ? 0.5 : 1.0;
		float4 colorFinal = float4(color, 1.0);
		history = lerp(history, colorFinal, blend);
		historyPosition = float4(lerp(historyPosition.xyz, position, blend), GetCaptureTime());
		colorFinal *= sqrt(saturate(500.0 * length(position)));
		capture = lerp(capture, colorFinal, blend);
	} else {
		// Preserve the soft unseen-sample weighting without radial player-centred rings.
		float historyFade = 0.94;
#if defined(FAKEREFLECTIONS)
		historyFade = 0.97;
#endif
		if ((CaptureFlags & 0x2u) != 0) {
#if defined(REFLECTIONS)
			historyFade = min(historyFade, 0.86);
#else
			historyFade = min(historyFade, 0.90);
#endif
		}
		capture = history * historyFade;
	}

	DynamicCubemapPosition[texel] = historyPosition;
	DynamicCubemapRaw[texel] = max(0.0, history);
	DynamicCubemap[texel] = max(0.0, capture);
}
