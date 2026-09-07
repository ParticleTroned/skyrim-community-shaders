static const float3 kNeuralOutputLuminanceWeights =
	float3(0.2126f, 0.7152f, 0.0722f);
static const float kNeuralOutputLuminanceFloor = 1.0f / 512.0f;
static const float kNeuralOutputMaximumLuminanceRatio = 2.0f;
static const float kNeuralOutputMinimumModelLuminance = 1e-5f;

float4 ResolveNeuralOutput(float4 originalSample, float4 modelSample)
{
	const float originalAlpha = isfinite(originalSample.a) ? originalSample.a : 1.0f;
	if (!all(isfinite(originalSample.rgb)))
		return float4(0.0f, 0.0f, 0.0f, originalAlpha);
	if (!all(isfinite(modelSample.rgb)))
		return float4(originalSample.rgb, originalAlpha);

	const float originalLuminance =
		max(dot(originalSample.rgb, kNeuralOutputLuminanceWeights), 0.0f);
	const float modelLuminance =
		max(dot(modelSample.rgb, kNeuralOutputLuminanceWeights), 0.0f);
	if (!isfinite(originalLuminance) ||
		!isfinite(modelLuminance) ||
		modelLuminance <= kNeuralOutputMinimumModelLuminance) {
		return float4(originalSample.rgb, originalAlpha);
	}

	const float luminanceRatio =
		(modelLuminance + kNeuralOutputLuminanceFloor) /
		(originalLuminance + kNeuralOutputLuminanceFloor);
	if (!isfinite(luminanceRatio) || luminanceRatio <= 0.0f)
		return float4(originalSample.rgb, originalAlpha);

	const float minimumLuminanceRatio =
		1.0f / kNeuralOutputMaximumLuminanceRatio;
	if (luminanceRatio >= minimumLuminanceRatio &&
		luminanceRatio <= kNeuralOutputMaximumLuminanceRatio) {
		return float4(modelSample.rgb, originalAlpha);
	}

	const float boundedLuminanceRatio = clamp(
		luminanceRatio,
		minimumLuminanceRatio,
		kNeuralOutputMaximumLuminanceRatio);
	const float boundedLuminance = max(
		boundedLuminanceRatio *
			(originalLuminance + kNeuralOutputLuminanceFloor) -
			kNeuralOutputLuminanceFloor,
		0.0f);
	const float correctionScale = boundedLuminance / modelLuminance;
	const float3 resolvedColor = modelSample.rgb * correctionScale;
	if (!isfinite(correctionScale) || !all(isfinite(resolvedColor)))
		return float4(originalSample.rgb, originalAlpha);

	// One scalar retains the model's hue while anchoring its brightness to the
	// untouched input. This is a general output guard, not a fire/content mask.
	return float4(resolvedColor, originalAlpha);
}
