#include "/Shaders/Upscaling/NeuralRendering/OutputResolve.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

static const float3 kTestNeuralOutputLuminanceWeights =
	float3(0.2126f, 0.7152f, 0.0722f);

float TestAnchoredLuminanceRatio(float3 resolved, float3 original)
{
	const float resolvedLuminance =
		max(dot(resolved, kTestNeuralOutputLuminanceWeights), 0.0f);
	const float originalLuminance =
		max(dot(original, kTestNeuralOutputLuminanceWeights), 0.0f);
	return (resolvedLuminance + kNeuralOutputLuminanceFloor) /
		(originalLuminance + kNeuralOutputLuminanceFloor);
}

/// @tags neural-rendering, output-resolve
[numthreads(1, 1, 1)] void TestNeuralOutputResolvePreservesSafeModelOutput()
{
	const float4 original = float4(0.25f, 0.25f, 0.25f, 0.35f);
	const float4 model = float4(0.4f, 0.2f, 0.1f, 0.9f);
	const float4 resolved = ResolveNeuralOutput(original, model);

	ASSERT(IsTrue, all(abs(resolved.rgb - model.rgb) < 1e-6f));
	ASSERT(IsTrue, resolved.a == original.a);
}

/// @tags neural-rendering, output-resolve
[numthreads(1, 1, 1)] void TestNeuralOutputResolveBoundsBrightModelOutput()
{
	const float4 original = float4(0.0f, 0.0f, 0.0f, 0.4f);
	const float4 model = float4(4.0f, 2.0f, 0.5f, 1.0f);
	const float4 resolved = ResolveNeuralOutput(original, model);
	const float ratio = TestAnchoredLuminanceRatio(resolved.rgb, original.rgb);

	ASSERT(IsTrue, ratio <= kNeuralOutputMaximumLuminanceRatio + 1e-4f);
	ASSERT(IsTrue, abs(resolved.r / model.r - resolved.g / model.g) < 1e-5f);
	ASSERT(IsTrue, abs(resolved.g / model.g - resolved.b / model.b) < 1e-5f);
	ASSERT(IsTrue, resolved.a == original.a);
}

/// @tags neural-rendering, output-resolve
[numthreads(1, 1, 1)] void TestNeuralOutputResolveBoundsDarkModelOutput()
{
	const float4 original = float4(0.8f, 0.4f, 0.2f, 0.6f);
	const float4 model = float4(0.04f, 0.02f, 0.01f, 1.0f);
	const float4 resolved = ResolveNeuralOutput(original, model);
	const float ratio = TestAnchoredLuminanceRatio(resolved.rgb, original.rgb);

	ASSERT(IsTrue, ratio >=
		1.0f / kNeuralOutputMaximumLuminanceRatio - 1e-4f);
	ASSERT(IsTrue, abs(resolved.r / model.r - resolved.g / model.g) < 1e-5f);
	ASSERT(IsTrue, abs(resolved.g / model.g - resolved.b / model.b) < 1e-5f);
	ASSERT(IsTrue, resolved.a == original.a);
}

/// @tags neural-rendering, output-resolve
[numthreads(1, 1, 1)] void TestNeuralOutputResolveRejectsInvalidOrEmptyModelOutput()
{
	const float4 original = float4(0.3f, 0.2f, 0.1f, 0.7f);
	const float nanValue = asfloat(0x7FC00000u);
	const float positiveInfinity = asfloat(0x7F800000u);
	const float negativeInfinity = asfloat(0xFF800000u);
	const float4 nanResolved = ResolveNeuralOutput(
		original, float4(nanValue, 0.2f, 0.1f, 1.0f));
	const float4 positiveInfinityResolved = ResolveNeuralOutput(
		original, float4(0.3f, positiveInfinity, 0.1f, 1.0f));
	const float4 negativeInfinityResolved = ResolveNeuralOutput(
		original, float4(0.3f, 0.2f, negativeInfinity, 1.0f));
	const float4 emptyResolved = ResolveNeuralOutput(
		original, float4(0.0f, 0.0f, 0.0f, 1.0f));
	const float4 blackResolved = ResolveNeuralOutput(
		float4(0.0f, 0.0f, 0.0f, 0.25f),
		float4(0.0f, 0.0f, 0.0f, 0.9f));

	ASSERT(IsTrue, all(nanResolved == original));
	ASSERT(IsTrue, all(positiveInfinityResolved == original));
	ASSERT(IsTrue, all(negativeInfinityResolved == original));
	ASSERT(IsTrue, all(emptyResolved == original));
	ASSERT(IsTrue, all(blackResolved == float4(0.0f, 0.0f, 0.0f, 0.25f)));
}

/// @tags neural-rendering, output-resolve
[numthreads(1, 1, 1)] void TestNeuralOutputResolveSanitizesInvalidOriginal()
{
	const float nanValue = asfloat(0x7FC00000u);
	const float4 invalidColorResolved = ResolveNeuralOutput(
		float4(0.3f, nanValue, 0.1f, 0.7f),
		float4(0.4f, 0.2f, 0.1f, 1.0f));
	const float4 invalidAlphaResolved = ResolveNeuralOutput(
		float4(0.3f, 0.2f, 0.1f, nanValue),
		float4(0.4f, 0.2f, 0.1f, 1.0f));

	ASSERT(IsTrue, all(invalidColorResolved == float4(0.0f, 0.0f, 0.0f, 0.7f)));
	ASSERT(IsTrue, all(invalidAlphaResolved.rgb == float3(0.4f, 0.2f, 0.1f)));
	ASSERT(IsTrue, invalidAlphaResolved.a == 1.0f);
}

/// @tags neural-rendering, output-resolve
[numthreads(1, 1, 1)] void TestNeuralOutputResolvePreservesExactRatioBoundaries()
{
	const float originalLuminance = 0.25f;
	const float maximumLuminance =
		kNeuralOutputMaximumLuminanceRatio *
			(originalLuminance + kNeuralOutputLuminanceFloor) -
		kNeuralOutputLuminanceFloor;
	const float minimumLuminance =
		(originalLuminance + kNeuralOutputLuminanceFloor) /
			kNeuralOutputMaximumLuminanceRatio -
		kNeuralOutputLuminanceFloor;
	const float4 original = float4(originalLuminance.xxx, 0.45f);
	const float4 maximumModel = float4(maximumLuminance.xxx, 0.9f);
	const float4 minimumModel = float4(minimumLuminance.xxx, 0.9f);
	const float4 maximumResolved = ResolveNeuralOutput(original, maximumModel);
	const float4 minimumResolved = ResolveNeuralOutput(original, minimumModel);

	ASSERT(IsTrue, all(abs(maximumResolved.rgb - maximumModel.rgb) < 1e-6f));
	ASSERT(IsTrue, all(abs(minimumResolved.rgb - minimumModel.rgb) < 1e-6f));
	ASSERT(IsTrue, maximumResolved.a == original.a);
	ASSERT(IsTrue, minimumResolved.a == original.a);
}
