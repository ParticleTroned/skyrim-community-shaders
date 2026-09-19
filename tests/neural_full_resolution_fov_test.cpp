struct float2
{
	float x = 0.0f, y = 0.0f;
};
using uint = unsigned;

#include "Features/Upscaling/FoveatedRegionPlan.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <iostream>
#include <stdexcept>

namespace globals::game
{
	bool isVR = true;
}

struct Upscaling
{
	enum class UpscaleMethod
	{
		kNONE,
		kDLSS,
		kFSR
	};
	struct Settings
	{
		bool neuralRenderingEnabled = true, neuralRenderingFovOnly = true;
		bool neuralRenderingRenderscaleFov = false;
		bool foveatedPeripheryMaskVisualization = false, periphery_taa_enable = false;
		uint neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::FullResolution);
		uint neuralRenderingInsertionPoint = static_cast<uint>(NeuralRendering::InsertionPoint::FinalLdrPreUi);
		float foveatedCenterArea = 0.50f, periphery_taa_center_area = 0.35f, periphery_taa_outer_scale = 0.8f;
		float foveatedCenterHorizontalScale = 1.1f;
		float foveatedLeftEyeMaskOffsetX = 0.0f, foveatedLeftEyeMaskOffsetY = 0.0f;
		float foveatedRightEyeMaskOffsetX = 0.0f, foveatedRightEyeMaskOffsetY = 0.0f;
		float periphery_taa_center_blend_feather = 0.03f;
		float neuralRenderingBlendFeather = 0.10f;
	} settings;
#include "neural_full_resolution_fov_types.h"
	bool available = true;
	std::array<float2, 2> fovOffsets{ float2{ -0.03f, 0.02f }, float2{ 0.04f, -0.01f } };
	std::array<float2, 2> taaOffsets{ float2{ -0.06f, 0.03f }, float2{ 0.07f, -0.02f } };
	bool IsNeuralRenderingRequested() const { return settings.neuralRenderingEnabled; }
	auto GetNeuralRenderingMode() const { return NeuralRendering::ClampRenderingMode(settings.neuralRenderingMode); }
	UpscaleMethod GetRuntimeUpscaleMethod() const { return UpscaleMethod::kDLSS; }
	bool IsActiveUpscalingFoveatedProfileAvailable() const { return available; }
	bool IsPeripheryTAAEnabled(UpscaleMethod) const { return available && settings.periphery_taa_enable; }
	auto GetResolvedFoveatedMaskCenterOffsets(bool taa) const { return taa ? taaOffsets : fovOffsets; }
	ActiveUpscalingFoveatedProfile GetActiveUpscalingFoveatedProfile() const;
	bool BuildFoveatedDispatchRects(uint32_t, uint32_t, uint32_t, uint32_t, bool,
		float, float, float, UpscaleMethod, bool, const ActiveUpscalingFoveatedProfile* = nullptr);
};

float ClampFoveatedCenterScale(float value) { return FoveatedCommon::ClampCenterScale(value); }
float ClampFoveatedCenterHorizontalScale(float value) { return FoveatedCommon::ClampCenterHorizontalScale(value); }
float ClampFoveatedMaskOffsetAdjustment(float value) { return value; }

#include "neural_full_resolution_fov_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

bool Equal(const FoveatedRegionPlan::Rect& left, const FoveatedRegionPlan::Rect& right)
{
	return left.minX == right.minX && left.minY == right.minY && left.maxX == right.maxX && left.maxY == right.maxY;
}

void CheckSharedPlan(Upscaling& upscaling)
{
	const auto profile = upscaling.GetActiveUpscalingFoveatedProfile();
	Require(profile.available, "Shared profile must be active");
	Require(upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, true,
				0.26f, 0.10f, 1.7f, Upscaling::UpscaleMethod::kDLSS, true, &profile),
		"Shared mask planning failed");
	const auto& cache = upscaling.foveatedRectCache;
	Require(cache.plan.IsValid(), "Both eye plans must be valid");
	Require(cache.centerScale == profile.sharedVisibleScale && cache.centerFeather == FoveatedCommon::kCenterFeather &&
				cache.centerHorizontalScale == profile.centerHorizontalScale,
		"NR must borrow exact shared geometry and feather");
	Require(cache.peripheryTAAOuterScale == 0.0f, "Shared outer boundary must become the NR center mask");
	for (uint32_t eye = 0; eye < 2; ++eye) {
		const auto offset = profile.centerOffsets[eye];
		Require(cache.plan.eyes[eye].centerOffset.x == offset.x && cache.plan.eyes[eye].centerOffset.y == offset.y,
			"Active profile eye offsets must remain pinned");
		const auto bounds = FoveatedCommon::BuildCenteredDispatchBounds(0, 1511, 1217,
			profile.sharedVisibleScale, offset.x, offset.y, FoveatedCommon::kCenterFeather, profile.centerHorizontalScale);
		Require(Equal(cache.plan.eyes[eye].output,
					{ uint(bounds.minX), uint(bounds.minY), uint(bounds.maxX), uint(bounds.maxY) }),
			"Visible NR support must equal shared FOV support for each eye");
		Require(cache.rects[eye].outputOffsetX == cache.plan.eyes[eye].output.minX &&
					cache.rects[eye].inputWidth == cache.plan.eyes[eye].input.Width(),
			"Guide crop must use the common region plan");
	}
}

int main()
{
	try {
		Upscaling upscaling;
		CheckSharedPlan(upscaling);
		const auto centerOnly = upscaling.foveatedRectCache.plan;
		upscaling.settings.periphery_taa_enable = true;
		const auto profile = upscaling.GetActiveUpscalingFoveatedProfile();
		Require(profile.vendorCenterScale == 0.35f && profile.sharedVisibleScale == 0.8f && profile.usesPeripheryTAAOuterMask,
			"FOV+TAA must use its outer visible mask, not either vendor center");
		CheckSharedPlan(upscaling);
		Require(!Equal(centerOnly.eyes[0].output, upscaling.foveatedRectCache.plan.eyes[0].output),
			"Switching active profiles must rebuild the prepared guide crop");
		const auto taaPlan = upscaling.foveatedRectCache.plan;
		upscaling.settings.neuralRenderingBlendFeather = 0.0f;
		upscaling.settings.periphery_taa_center_blend_feather = 0.10f;
		CheckSharedPlan(upscaling);
		Require(Equal(taaPlan.eyes[1].output, upscaling.foveatedRectCache.plan.eyes[1].output),
			"Independent NR/vendor feathers cannot alter the shared mask");
		upscaling.taaOffsets[1].x -= 0.03f;
		CheckSharedPlan(upscaling);
		Require(!Equal(taaPlan.eyes[1].output, upscaling.foveatedRectCache.plan.eyes[1].output),
			"Eye offset changes must invalidate prepared geometry");
		upscaling.settings.periphery_taa_outer_scale = 1.0f;
		const auto fullSharedProfile = upscaling.GetActiveUpscalingFoveatedProfile();
		Require(fullSharedProfile.available && fullSharedProfile.vendorCenterScale < 0.999f &&
					fullSharedProfile.sharedVisibleScale == 1.0f,
			"Full shared coverage can retain an active vendor center");
		Require(upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, true,
					0.35f, 0.10f, 1.1f, Upscaling::UpscaleMethod::kDLSS, true, &fullSharedProfile),
			"Full shared coverage must be supported");
		for (uint32_t eye = 0; eye < 2; ++eye) {
			Require(upscaling.foveatedRectCache.plan.eyes[eye].output.CoversExtent(1511, 1217) &&
						upscaling.foveatedRectCache.centerOffsets[eye].x == 0.0f &&
						upscaling.foveatedRectCache.centerOffsets[eye].y == 0.0f,
				"Inactive shared foveation must include both full eyes, including corners");
		}
		upscaling.available = false;
		const auto unavailable = upscaling.GetActiveUpscalingFoveatedProfile();
		Require(!upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, true,
					0.5f, 0.1f, 1.0f, Upscaling::UpscaleMethod::kDLSS, false, &unavailable),
			"Unavailable shared mask must fail closed");
		upscaling.settings.neuralRenderingFovOnly = false;
		Require(upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, true,
					0.3f, 0.1f, 1.4f, Upscaling::UpscaleMethod::kDLSS, true),
			"Unrestricted full NR must remain available without FOV");
		Require(upscaling.foveatedRectCache.plan.eyes[0].output.CoversExtent(1511, 1217) &&
					upscaling.foveatedRectCache.plan.eyes[1].output.CoversExtent(1511, 1217),
			"Full NR must cover both complete eyes");
		upscaling.settings.neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::Foveated);
		upscaling.settings.neuralRenderingBlendFeather = 0.08f;
		Require(upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, true,
					0.5f, 0.02f, 1.0f, Upscaling::UpscaleMethod::kDLSS, false),
			"Existing vendor planning must remain available");
		Require(upscaling.foveatedRectCache.centerFeather == 0.08f,
			"Ordinary foveated reconstruction must retain its independent NR support feather");
		upscaling.settings.neuralRenderingInsertionPoint = static_cast<uint>(NeuralRendering::InsertionPoint::UpscaledCenter);
		Require(UsesFinalLdrNeuralBlend(upscaling.settings),
			"Legacy early placement must retain final-LDR support for Foveated");
		// Match support widths when comparing crop geometry across different placements.
		upscaling.settings.neuralRenderingBlendFeather = 0.02f;
		for (const float scale : { 0.26f, 0.60f, 0.95f }) {
			for (const float offset : { -0.25f, 0.0f, 0.25f }) {
				upscaling.fovOffsets = { float2{ offset, -offset }, float2{ -offset, offset } };
				upscaling.settings.neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::Foveated);
				Require(upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, true,
							scale, 0.02f, 1.1f, Upscaling::UpscaleMethod::kDLSS, false),
					"Foveated reference plan failed");
				const auto reference = upscaling.foveatedRectCache;
				upscaling.settings.neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::ReducedResolution);
				for (const bool savedRestriction : { false, true }) {
					upscaling.settings.neuralRenderingFovOnly = savedRestriction;
					upscaling.settings.neuralRenderingRenderscaleFov = true;
					Require(upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, true,
								scale, 0.02f, 1.1f, Upscaling::UpscaleMethod::kDLSS, false),
						"Renderscale FOV plan failed");
					const auto& actual = upscaling.foveatedRectCache;
					Require(actual.centerScale == reference.centerScale && actual.centerFeather == reference.centerFeather,
						"Renderscale and Foveated must share mask scale and support feather");
					for (uint eye = 0; eye < 2; ++eye) {
						Require(Equal(actual.plan.eyes[eye].input, reference.plan.eyes[eye].input) &&
									Equal(actual.plan.eyes[eye].output, reference.plan.eyes[eye].output),
							"Renderscale must preserve Foveated input/output coverage at odd dimensions and screen edges");
						Require(actual.centerOffsets[eye].x == reference.centerOffsets[eye].x &&
									actual.centerOffsets[eye].y == reference.centerOffsets[eye].y,
							"Renderscale must retain independent eye offsets");
					}
					upscaling.settings.neuralRenderingRenderscaleFov = false;
					Require(upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, true,
								scale, 0.02f, 1.1f, Upscaling::UpscaleMethod::kDLSS, false),
						"Disabling renderscale FOV must restore full-eye planning");
					for (const auto& eye : upscaling.foveatedRectCache.plan.eyes) {
						Require(eye.input.CoversExtent(1007, 811) && eye.output.CoversExtent(1511, 1217) &&
									eye.centerOffset.x == 0.0f && eye.centerOffset.y == 0.0f,
							"Unmasked renderscale must include every input/output pixel despite saved masks and offsets");
					}
				}
			}
		}
		globals::game::isVR = false;
		upscaling.settings.neuralRenderingFovOnly = true;
		upscaling.settings.neuralRenderingRenderscaleFov = true;
		Require(upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, false,
					0.6f, 0.02f, 1.1f, Upscaling::UpscaleMethod::kDLSS, false) &&
					upscaling.foveatedRectCache.plan.eyes[0].input.CoversExtent(1007, 811) &&
					upscaling.foveatedRectCache.plan.eyes[0].output.CoversExtent(1511, 1217),
			"Flat renderscale NR must retain complete mono coverage");
		std::cout << "Shared FOV geometry passed, including 18 VR renderscale on/off comparisons and flat coverage\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
