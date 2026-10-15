struct float2
{
	float x = 0.0f, y = 0.0f;
};
using uint = unsigned;

#include "Features/Upscaling/FoveatedMaskCalibration.h"
#include "Features/Upscaling/FoveatedRegionPlan.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"
#include "Features/Upscaling/NeuralRendering/Runtime.h"

#include <iostream>
#include <stdexcept>

namespace globals::game
{
	bool isVR = true;
}
bool NeuralRendering::Runtime::IsInstalled() noexcept { return true; }
namespace globals
{
	struct State
	{
		bool pendingPostLoadRuntimeReset = false;
	} storage;
	State* state = &storage;
}
struct FoveatedHeadsetSnapshot;
std::optional<FoveatedHeadsetSnapshot> GetFoveatedHeadsetGeometry(bool includeVisibleBounds = true);
bool loadingMenu = false, mainMenu = false, menuPresentation = false, loadingProtection = false;
bool IsLoadingMenuContextActive() { return loadingMenu; }
bool IsMainMenuContextActive() { return mainMenu; }
bool IsVRMenuPresentationContextActive() { return menuPresentation; }

namespace globals::features
{
	struct AdapterFixture
	{
		bool IsNeuralRenderingHardwareSupported() const noexcept { return true; }
	} upscaling;
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
		bool foveatedVendorDispatch = true;
		FoveatedMaskCalibration::Reference foveatedCalibrationReference;
		FoveatedMaskCalibration::SetupConfirmation foveatedMaskSetup;
		float foveatedAutomaticMaskScaling = 100;
		bool operator==(const Settings&) const = default;
		bool neuralRenderingEnabled = true, neuralRenderingFovOnly = true;
		bool neuralRenderingRenderscaleFov = false;
		bool foveatedPeripheryMaskVisualization = false, periphery_taa_enable = false;
		uint neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::FullResolution);
		uint neuralRenderingInsertionPoint = static_cast<uint>(NeuralRendering::InsertionPoint::FinalLdrPreUi);
		float foveatedCenterArea = 0.50f, periphery_taa_center_area = 0.35f, periphery_taa_outer_scale = 0.8f;
		float foveatedCenterHorizontalScale = 1.1f;
		float foveatedOuterBlendFeather = FoveatedCommon::kCenterFeather;
		bool foveatedDisableOuterFeather = false;
		float foveatedLeftEyeMaskOffsetX = 0.0f, foveatedLeftEyeMaskOffsetY = 0.0f;
		float foveatedRightEyeMaskOffsetX = 0.0f, foveatedRightEyeMaskOffsetY = 0.0f;
		float periphery_taa_center_blend_feather = 0.03f;
		float neuralRenderingBlendFeather = 0.10f;
	} settings;
	static bool IsNeuralRenderingEnabled(const Settings&) noexcept;
#include "neural_full_resolution_fov_types.h"
	bool available = true, loaded = true;
	struct
	{
		bool knownMenuContextActive = false, menuContextActive = false, loadingMenuActive = false;
	} runtimeResolutionPlan;
	FoveatedMaskCalibration::CoverageCheck GetFoveatedBinocularCoverage(const Settings&) const;
	bool ConfirmFoveatedMaskSetup(Settings&, std::string&) const;
	bool ValidateFoveatedMaskSetupContext(const Settings&, std::string&) const;
	bool PrepareFoveatedMaskCalibration(Settings&, FoveatedMaskCalibration::CaptureMode, float, std::string&) const;
	std::array<float2, 2> fovOffsets{ float2{ -0.03f, 0.02f }, float2{ 0.04f, -0.01f } };
	bool IsNeuralRenderingRequested() const { return settings.neuralRenderingEnabled; }
	auto GetNeuralRenderingMode() const { return NeuralRendering::ClampRenderingMode(settings.neuralRenderingMode); }
	UpscaleMethod method = UpscaleMethod::kDLSS;
	UpscaleMethod GetRuntimeUpscaleMethod() const { return method; }
	UpscaleMethod GetUpscaleMethod() const { return method; }
	bool IsActiveUpscalingFoveatedProfileAvailable() const { return available; }
	bool IsPeripheryTAAEnabled(UpscaleMethod) const { return available && settings.periphery_taa_enable; }
	auto GetResolvedFoveatedMaskCenterOffsets(bool) const { return fovOffsets; }
	ActiveUpscalingFoveatedProfile GetActiveUpscalingFoveatedProfile() const;
	bool BuildFoveatedDispatchRects(uint32_t, uint32_t, uint32_t, uint32_t, bool,
		float, float, float, UpscaleMethod, bool, const ActiveUpscalingFoveatedProfile* = nullptr);
};

float ClampFoveatedCenterScale(float value) { return FoveatedCommon::ClampCenterScale(value); }
float ClampFoveatedCenterHorizontalScale(float value) { return FoveatedCommon::ClampCenterHorizontalScale(value); }
float ClampFoveatedMaskOffsetAdjustment(float value) { return std::clamp(value, FoveatedCommon::kMaskOffsetMin, FoveatedCommon::kMaskOffsetMax); }
bool SupportsFoveatedVendorDispatch(Upscaling::UpscaleMethod method) { return method != Upscaling::UpscaleMethod::kNONE; }
bool IsVRLoadingSubmitProtectionContextActive(const Upscaling&, const globals::State*) { return loadingProtection; }

#include "neural_full_resolution_fov_under_test.h"
std::optional<FoveatedHeadsetSnapshot> liveHeadset;
std::optional<FoveatedHeadsetSnapshot> GetFoveatedHeadsetGeometry(bool) { return liveHeadset; }

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
				0.26f, 0.10f, 1.7f, upscaling.GetRuntimeUpscaleMethod(), true, &profile),
		"Shared mask planning failed");
	const auto& cache = upscaling.foveatedRectCache;
	Require(cache.plan.IsValid(), "Both eye plans must be valid");
	Require(cache.centerScale == profile.sharedVisibleScale && cache.centerFeather == profile.sharedVisibleFeather &&
				cache.centerHorizontalScale == profile.centerHorizontalScale,
		"NR must borrow exact shared geometry and feather");
	Require(cache.peripheryTAAOuterScale == 0.0f, "Shared outer boundary must become the NR center mask");
	for (uint32_t eye = 0; eye < 2; ++eye) {
		const auto offset = profile.centerOffsets[eye];
		Require(cache.plan.eyes[eye].centerOffset.x == offset.x && cache.plan.eyes[eye].centerOffset.y == offset.y,
			"Active profile eye offsets must remain pinned");
		const auto bounds = FoveatedCommon::BuildCenteredDispatchBounds(0, 1511, 1217,
			profile.sharedVisibleScale, offset.x, offset.y, profile.sharedVisibleFeather, profile.centerHorizontalScale);
		Require(Equal(cache.plan.eyes[eye].output,
					{ uint(bounds.minX), uint(bounds.minY), uint(bounds.maxX), uint(bounds.maxY) }),
			"Visible NR support must equal shared FOV support for each eye");
		Require(cache.rects[eye].outputOffsetX == cache.plan.eyes[eye].output.minX &&
					cache.rects[eye].inputWidth == cache.plan.eyes[eye].input.Width(),
			"Guide crop must use the common region plan");
	}
}

void CheckHeadsetSetupTransaction()
{
	using namespace FoveatedMaskCalibration;
	const Polygon rectangle{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	liveHeadset = FoveatedHeadsetSnapshot{ .leftToRight = { 1, 0, -.19, 0, 1, 0, 0, 0, 1 }, .innerLimits = { 1, 0 }, .outlines = { rectangle, rectangle } };
	Upscaling upscaling;
	auto candidate = upscaling.settings;
	candidate.periphery_taa_enable = false;
	candidate.foveatedLeftEyeMaskOffsetX = .2f;
	candidate.foveatedRightEyeMaskOffsetY = -.2f;
	std::string error;
	Require(upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Headset, 100, error), "Automatic headset setup must accept prior manual geometry in FOV only");
	Require(!candidate.periphery_taa_enable && candidate.foveatedCenterArea == 1 && UsesHeadsetOutline(candidate.foveatedCalibrationReference),
		"Headset setup must select a conservative FOV-only baseline and save both outlines");
	const auto baseline = candidate;
	Require(upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Reuse, 50, error), "Outer coverage must reuse the saved headset setup");
	Require(candidate.foveatedCenterArea < 1 && candidate.foveatedAutomaticMaskScaling == 50 && candidate.foveatedCalibrationReference == baseline.foveatedCalibrationReference,
		"Outer scaling must reduce coverage, update sliders and preserve the full headset reference");
	const auto applied = CaptureFoveatedMaskReference(candidate, liveHeadset->leftToRight, liveHeadset->innerLimits);
	auto fitting = candidate.foveatedCalibrationReference;
	fitting.feather = GetNormalFoveatedBlendFeather(candidate, false);
	Require(!CheckCoverage(fitting, 50, applied.outer, applied.centerScale, applied.fullImage, false).hasGap,
		"Applied slider values must retain verified full-strength headset coverage");
	const auto scaled = candidate;
	for (float percent : { kMinimumPercent - 1, kMaximumPercent + 1, kMaximumReferencePercent }) {
		auto invalid = scaled;
		Require(!upscaling.PrepareFoveatedMaskCalibration(invalid, CaptureMode::Reuse, percent, error) && invalid == scaled,
			"Unsupported coverage requests must fail without changing the selected fit");
	}
	for (bool hardEdge : { false, true }) {
		for (float percent : { 50.0f, 65.0f, 80.0f, 90.0f, kMaximumPercent }) {
			auto manual = baseline;
			manual.foveatedDisableOuterFeather = hardEdge;
			Require(upscaling.PrepareFoveatedMaskCalibration(manual, CaptureMode::Reuse, percent, error),
				"Outer Coverage must establish the selected fit before manual fine-tuning");
			const auto automaticFit = manual;
			Require(upscaling.PrepareFoveatedMaskCalibration(manual, CaptureMode::Manual, 100, error) && manual == automaticFit,
				"Calibration after automatic outer scaling must retain the headset reference and fit when no manual controls changed");
			manual.foveatedLeftEyeMaskOffsetY += 0.01f;
			auto outer = CaptureFoveatedMaskReference(manual, liveHeadset->leftToRight, liveHeadset->innerLimits);
			outer.feather = GetNormalFoveatedBlendFeather(manual, false);
			Require(upscaling.PrepareFoveatedMaskCalibration(manual, CaptureMode::Manual, 100, error) &&
						manual.foveatedAutomaticMaskScaling == percent && manual.foveatedCalibrationReference.coveragePercent == percent &&
						!UsesHeadsetOutline(manual.foveatedCalibrationReference),
				"Calibrate Masks must retain the selected Outer Coverage percentage");
			const auto fitted = CaptureFoveatedMaskReference(manual, liveHeadset->leftToRight, liveHeadset->innerLimits);
			Require(!CheckCoverage(outer, 100, fitted.outer, fitted.centerScale, fitted.fullImage, false).hasGap,
				"Manual recalibration must preserve the current outer contour without scaling it a second time");
			const auto calibrated = manual;
			Require(upscaling.PrepareFoveatedMaskCalibration(manual, CaptureMode::Reuse, percent, error) && manual == calibrated,
				"Reapplying the retained percentage must reproduce the calibrated fit");
			for (unsigned repeat = 0; repeat < 3; ++repeat)
				Require(upscaling.PrepareFoveatedMaskCalibration(manual, CaptureMode::Manual, percent, error) && manual == calibrated,
					"Calibrating an unchanged fit must not progressively expand or reposition the masks");
			const float otherPercent = percent == 50.0f ? 55.0f : percent - 5;
			Require(upscaling.PrepareFoveatedMaskCalibration(manual, CaptureMode::Reuse, otherPercent, error) &&
						upscaling.PrepareFoveatedMaskCalibration(manual, CaptureMode::Reuse, percent, error) &&
						manual.foveatedAutomaticMaskScaling == percent && manual.foveatedCalibrationReference == calibrated.foveatedCalibrationReference &&
						CaptureFoveatedMaskReference(manual, liveHeadset->leftToRight, liveHeadset->innerLimits) == fitted,
				"Outer Coverage must scale from and return to the same calibrated outer geometry and percentage");
			Require(upscaling.PrepareFoveatedMaskCalibration(manual, CaptureMode::Headset, percent, error) &&
						manual.foveatedAutomaticMaskScaling == 100 && manual.foveatedCalibrationReference == baseline.foveatedCalibrationReference &&
						CaptureFoveatedMaskReference(manual, liveHeadset->leftToRight, liveHeadset->innerLimits) == CaptureFoveatedMaskReference(baseline, liveHeadset->leftToRight, liveHeadset->innerLimits),
				"Automatic Headset Setup must deliberately restore the headset baseline at 100 percent");
		}
	}
	Require(upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Reuse, 100, error) && candidate == baseline,
		"Returning to 100 percent must restore every fitted baseline control exactly");
	for (bool hardEdge : { false, true }) {
		candidate = scaled;
		candidate.periphery_taa_enable = true;
		candidate.foveatedDisableOuterFeather = hardEdge;
		const auto locked = candidate;
		Require(!upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Reuse, 65, error) && candidate == locked,
			"Outer coverage must remain locked in TAA mode with or without an outer feather");
	}
	candidate = scaled;
	liveHeadset->outlines[0][0].y = .01;
	Require(!upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Reuse, 70, error) && candidate == scaled,
		"Changed headset outline must invalidate reuse even when projection and nasal extent are unchanged");
	Require(!upscaling.ConfirmFoveatedMaskSetup(candidate, error) && candidate == scaled && !error.empty(),
		"Valid headset optics with stale calibration must require recalibration instead of manual fallback");
	Require(upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Headset, 100, error) &&
				candidate.foveatedCalibrationReference.headsetOutlines == liveHeadset->outlines && error.empty(),
		"Fresh headset setup must replace a stale saved outline");
	Require(upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Reuse, 70, error),
		"Outer Coverage must work after replacing a stale saved reference");
	liveHeadset->outlines[0][0].y = 0;
	candidate = scaled;
	for (unsigned condition = 0; condition < 10; ++condition) {
		globals::game::isVR = condition != 0;
		upscaling.loaded = condition != 1;
		loadingMenu = condition == 2;
		globals::storage.pendingPostLoadRuntimeReset = condition == 3;
		upscaling.method = condition == 4 ? Upscaling::UpscaleMethod::kNONE : Upscaling::UpscaleMethod::kDLSS;
		mainMenu = condition == 5;
		candidate.foveatedVendorDispatch = condition != 6;
		loadingProtection = condition == 7;
		globals::state = condition == 8 ? nullptr : &globals::storage;
		if (condition == 9)
			liveHeadset.reset();
		const auto rejected = candidate;
		Require(!upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Headset, 100, error) && candidate == rejected && !error.empty(),
			"Unavailable runtime, loading or missing optics must leave all mask settings untouched");
		if (condition != 9)
			Require(!upscaling.ConfirmFoveatedMaskSetup(candidate, error) && candidate == rejected,
				"Manual confirmation must retain the calibration runtime and loaded-scene guards");
		if (condition == 9)
			Require(error.find("valid viewing angles for both eyes") != std::string::npos &&
						error.find("manually in FOV only") != std::string::npos && error.find("X/Y sliders") != std::string::npos &&
						error.find("Mask setup finished, then Save settings") != std::string::npos,
				"Missing headset data must explain the failure and direct users to manual FOV setup");
	}
	globals::game::isVR = upscaling.loaded = true;
	globals::state = &globals::storage;
	loadingMenu = mainMenu = menuPresentation = loadingProtection = globals::storage.pendingPostLoadRuntimeReset = false;
	auto expected = scaled;
	expected.foveatedMaskSetup = CaptureSetupConfirmation(expected);
	Require(upscaling.ConfirmFoveatedMaskSetup(candidate, error) && candidate == expected && error.empty() &&
				!upscaling.GetFoveatedBinocularCoverage(candidate).available,
		"Missing headset optics must allow explicit acceptance without changing the fit or inventing a coverage result");
	liveHeadset = FoveatedHeadsetSnapshot{ .leftToRight = { 1, 0, -.19, 0, 1, 0, 0, 0, 1 }, .innerLimits = { 1, 0 }, .outlines = { rectangle, rectangle } };
	menuPresentation = true;
	for (unsigned staleFlag = 0; staleFlag < 4; ++staleFlag) {
		upscaling.runtimeResolutionPlan = { staleFlag == 0, staleFlag == 1, staleFlag == 2 };
		candidate = scaled;
		Require(upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Headset, 100, error) && candidate == baseline && error.empty(),
			"An in-game menu or stale plan must not block headset setup in a loaded scene");
		Require(upscaling.PrepareFoveatedMaskCalibration(candidate, CaptureMode::Reuse, 50, error) && candidate == scaled,
			"An in-game menu or stale plan must not block Outer Coverage");
		Require(upscaling.ConfirmFoveatedMaskSetup(candidate, error) && HasFovOnlySetup(candidate),
			"An in-game menu or stale plan must not block confirmation after live setup");
	}
}

int main()
{
	try {
		CheckHeadsetSetupTransaction();
		Upscaling upscaling;
		const auto baseline = upscaling.GetActiveUpscalingFoveatedProfile();
		for (float feather : { 0.0f, 0.001f, 0.01f, 0.0100005f, 0.05f, 0.10f }) {
			upscaling.settings.foveatedOuterBlendFeather = feather;
			Require(GetNormalFoveatedBlendFeather(upscaling.settings, false) == std::max(FoveatedCommon::kMinimumFeather, feather),
				"FOV-only blend must use the selected outer feather");
			Require(GetNormalFoveatedBlendFeather(upscaling.settings, true) == upscaling.settings.periphery_taa_center_blend_feather,
				"Outer feather experiments must preserve the TAA transition");
			Require(GetFoveatedReconstructionSupportFeather(upscaling.settings, feather, true) == 0.10f,
				"Final-LDR NR must retain its wider independent support when needed");
			CheckSharedPlan(upscaling);
		}
		upscaling.settings.foveatedOuterBlendFeather = 0.05f;
		upscaling.settings.foveatedDisableOuterFeather = true;
		Require(GetNormalFoveatedBlendFeather(upscaling.settings, false) == 0.0f &&
					GetNormalFoveatedBlendFeather(upscaling.settings, true) == 0.03f,
			"Disabling the outer fade must preserve an inner FOV-to-TAA transition");
		Require(GetFoveatedReconstructionSupportFeather(upscaling.settings, 0.0f, true) == 0.0f,
			"Shared FOV NR cannot restore an outer fade during the hard-boundary experiment");
		for (bool taa : { false, true }) {
			upscaling.settings.periphery_taa_enable = taa;
			CheckSharedPlan(upscaling);
			Require(upscaling.foveatedRectCache.centerFeather == 0.0f,
				"Both shared profiles must plan an exact hard outer boundary");
		}
		const FoveatedMaskCalibration::Projection identity{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		const auto reference = CaptureFoveatedMaskReference(upscaling.settings, identity);
		Require(reference.outer.scale == upscaling.settings.foveatedCenterArea && reference.feather == 0.05f,
			"Hard-edge capture must use the full-strength core while preserving configured-fade compatibility");
		auto fittingReference = reference;
		fittingReference.feather = GetNormalFoveatedBlendFeather(upscaling.settings, false);
		for (float percent : { 70.0f, 85.0f, FoveatedMaskCalibration::kMaximumPercent }) {
			const auto fit = FoveatedMaskCalibration::Solve(fittingReference, percent);
			Require(fit.has_value(), "Saved hard-edge references must support automatic scaling");
			const auto coverage = FoveatedMaskCalibration::CheckCoverage(reference, percent, fit->geometry,
				fit->geometry.scale, fit->fullImage, false);
			Require(coverage.available && !coverage.hasGap, "A scaled hard-edge fit must retain complete full-strength coverage");
		}
		upscaling.settings.periphery_taa_enable = false;
		upscaling.settings.foveatedDisableOuterFeather = false;
		upscaling.settings.foveatedOuterBlendFeather = 0.01f;
		const auto featheredReference = CaptureFoveatedMaskReference(upscaling.settings, identity, { .953f, .047f });
		Require(featheredReference.outer.scale == upscaling.settings.foveatedCenterArea &&
					featheredReference.innerLimits == std::array<float, 2>{ .953f, .047f },
			"Feathered capture must retain the full-strength core separately from outer fade and headset nasal limits");
		const auto narrow = upscaling.GetActiveUpscalingFoveatedProfile();
		Require(narrow.sharedVisibleFeather < baseline.sharedVisibleFeather,
			"Shared FOV geometry must expose narrower feather support");
		upscaling.settings.foveatedOuterBlendFeather = FoveatedCommon::kCenterFeather;

		CheckSharedPlan(upscaling);
		const auto fullMasked = upscaling.foveatedRectCache.plan;
		upscaling.method = Upscaling::UpscaleMethod::kFSR;
		upscaling.settings.neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::Foveated);
		for (const bool fullFovPreference : { false, true }) {
			upscaling.settings.neuralRenderingFovOnly = fullFovPreference;
			Require(NeuralRendering::UsesSharedFinalLdrFovMask(upscaling.GetNeuralRenderingMode(), fullFovPreference, true),
				"FSR Foveated must select the shared mask independently of the Full mode preference");
			CheckSharedPlan(upscaling);
			for (uint eye = 0; eye < 2; ++eye)
				Require(Equal(fullMasked.eyes[eye].input, upscaling.foveatedRectCache.plan.eyes[eye].input) &&
							Equal(fullMasked.eyes[eye].output, upscaling.foveatedRectCache.plan.eyes[eye].output),
					"FSR Foveated and Full plus FOV must use identical eye crops");
		}
		upscaling.method = Upscaling::UpscaleMethod::kDLSS;
		upscaling.settings.neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::FullResolution);
		upscaling.settings.neuralRenderingFovOnly = true;
		const auto centerOnly = upscaling.foveatedRectCache.plan;
		upscaling.settings.periphery_taa_enable = true;
		const auto profile = upscaling.GetActiveUpscalingFoveatedProfile();
		Require(profile.vendorCenterScale == 0.35f && profile.sharedVisibleScale == upscaling.settings.foveatedCenterArea && profile.usesPeripheryTAAOuterMask,
			"FOV+TAA must preserve the FOV-only visible boundary independently of the vendor centre");
		CheckSharedPlan(upscaling);
		Require(Equal(centerOnly.eyes[0].output, upscaling.foveatedRectCache.plan.eyes[0].output),
			"Switching to TAA must preserve the inherited visible guide crop");
		const auto taaPlan = upscaling.foveatedRectCache.plan;
		upscaling.settings.neuralRenderingBlendFeather = 0.0f;
		upscaling.settings.periphery_taa_center_blend_feather = 0.10f;
		CheckSharedPlan(upscaling);
		Require(Equal(taaPlan.eyes[1].output, upscaling.foveatedRectCache.plan.eyes[1].output),
			"Independent NR/vendor feathers cannot alter the shared mask");
		upscaling.fovOffsets[1].x -= 0.03f;
		CheckSharedPlan(upscaling);
		Require(!Equal(taaPlan.eyes[1].output, upscaling.foveatedRectCache.plan.eyes[1].output),
			"Eye offset changes must invalidate prepared geometry");
		upscaling.settings.foveatedCenterArea = 1.0f;
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
				const auto referencePlan = upscaling.foveatedRectCache;
				upscaling.settings.neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::ReducedResolution);
				for (const bool savedRestriction : { false, true }) {
					upscaling.settings.neuralRenderingFovOnly = savedRestriction;
					upscaling.settings.neuralRenderingRenderscaleFov = true;
					Require(upscaling.BuildFoveatedDispatchRects(1007, 811, 1511, 1217, true,
								scale, 0.02f, 1.1f, Upscaling::UpscaleMethod::kDLSS, false),
						"Renderscale FOV plan failed");
					const auto& actual = upscaling.foveatedRectCache;
					Require(actual.centerScale == referencePlan.centerScale && actual.centerFeather == referencePlan.centerFeather,
						"Renderscale and Foveated must share mask scale and support feather");
					for (uint eye = 0; eye < 2; ++eye) {
						Require(Equal(actual.plan.eyes[eye].input, referencePlan.plan.eyes[eye].input) &&
									Equal(actual.plan.eyes[eye].output, referencePlan.plan.eyes[eye].output),
							"Renderscale must preserve Foveated input/output coverage at odd dimensions and screen edges");
						Require(actual.centerOffsets[eye].x == referencePlan.centerOffsets[eye].x &&
									actual.centerOffsets[eye].y == referencePlan.centerOffsets[eye].y,
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
