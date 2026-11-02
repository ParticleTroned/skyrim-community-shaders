#include "Features/Upscaling/FoveatedMaskCalibration.h"
#include "Features/Upscaling/NeuralRendering/MemoryRetirementPolicy.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"
#include "Features/Upscaling/NeuralRendering/Runtime.h"
#include <cstdio>
#include <nlohmann/json.hpp>

#include <optional>

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

using uint32_t = std::uint32_t;
using json = nlohmann::json;
bool nrRuntimeInstalled = true;
bool NeuralRendering::Runtime::IsInstalled() noexcept { return nrRuntimeInstalled; }
const NeuralRendering::DriverSupport& NeuralRendering::Runtime::GetDriverSupport() noexcept
{
	static const DriverSupport support{ kMinimumNvidiaDriverRelease };
	return support;
}
namespace globals
{
	struct State
	{
		uint32_t frameCount = 4;
	};
	State* state = nullptr;
	namespace game
	{
		bool isVR = true;
	}
}
namespace logger
{
	template <class... T>
	void error(const char*, T&&...)
	{}
}
namespace NeuralRendering
{
	struct CharacterRendering
	{
		unsigned invalidations = 0;
		static CharacterRendering& Instance()
		{
			static CharacterRendering instance;
			return instance;
		}
		void Invalidate() { ++invalidations; }
	};
	struct Renderer
	{
		bool resetSucceeds = true, failed = false, quarantined = false;
		bool resourcesRetained = true;
		unsigned resets = 0;
		BackendRetirementPolicy lastPolicy = BackendRetirementPolicy::ReleaseBackend;
		static Renderer& Instance()
		{
			static Renderer instance;
			return instance;
		}
		bool IsFailureLatched() const { return failed; }
		bool IsQuarantined() const { return quarantined; }
		bool Reset(bool = false, BackendRetirementPolicy policy = BackendRetirementPolicy::ReleaseBackend)
		{
			lastPolicy = policy;
			++resets;
			if (!resetSucceeds) {
				failed = true;
				return false;
			}
			resourcesRetained = failed = quarantined = false;
			return true;
		}
	};
}
bool headsetProjectionAvailable = true;
std::optional<int> GetFoveatedHeadsetGeometry(bool) { return headsetProjectionAvailable ? std::optional{ 1 } : std::nullopt; }
constexpr int ImGuiHoveredFlags_AllowWhenDisabled = 1;
namespace ImGui
{
	int disabled = 0, selectedMode = -1;
	bool taaDisabled = false;
	std::string tooltip, renderedMessage, unformattedText;
	bool captureCalibration = false, captureHeadset = false, finishSetup = false;
	bool Button(const char* label) { return disabled ? false : std::string(label) == "Automatic Headset Setup" ? captureHeadset :
		                                                   std::string(label) == "Mask setup finished"         ? finishSetup :
		                                                                                                         captureCalibration; }
	bool SliderFloat(const char*, float*, float, float, const char*) { return false; }
	bool IsItemActive() { return false; }
	void TextWrapped(const char*, const char* text) { renderedMessage = text; }
	template <class... T>
	void TextWrapped(const char*, T&&...)
	{}
	void TextUnformatted(const char* text) { unformattedText = text; }
	struct ComboBox
	{
		ComboBox(const char*, const char*) {}
		explicit operator bool() const { return true; }
	};
	bool Selectable(const char* label, bool)
	{
		const bool taa = std::string(label) == "FOV + TAA";
		if (taa)
			taaDisabled = disabled != 0;
		return disabled == 0 && selectedMode == (taa ? 1 : 0);
	}
	void SetItemDefaultFocus() {}
	bool IsItemHovered(int) { return true; }
	void SetTooltip(const char*, const char* text) { tooltip = text; }
}
namespace Util
{
	bool HoverTooltipWrapper() { return false; }
	struct DisableGuard
	{
		bool disabled;
		explicit DisableGuard(bool value) : disabled(value) { ImGui::disabled += disabled; }
		~DisableGuard() { ImGui::disabled -= disabled; }
	};
	namespace Text
	{
		std::string warning;
		void WrappedError(const char* text) { warning = text; }
		void WrappedWarning(const char* text) { warning = text; }
		void WrappedWarning(const char*, const char* text) { warning = ImGui::renderedMessage = text; }
	}
}
namespace globals::features
{
	struct AdapterFixture
	{
		bool IsNeuralRenderingHardwareSupported() const noexcept { return true; }
	} upscaling;
}

template <class F>
struct ScopeExit
{
	F callback;
	~ScopeExit() { callback(); }
};

struct Upscaling
{
	enum class UpscaleMethod
	{
		kDLSS,
		kFSR
	};
	bool foveatedDispatch = true;
	bool foveatedMaskBinocularGapVisualization = false;
	bool IsFoveatedVendorDispatchEnabled(UpscaleMethod) const { return foveatedDispatch; }
	bool IsPeripheryTAAEnabled(UpscaleMethod) const;
	struct Settings
	{
		bool neuralRenderingEnabled = false;
		bool neuralCharacterProviderBlending = false;
		bool neuralCharacterPackedCrops = false;
		bool neuralRenderingRenderscaleFov = false;
		bool neuralRenderingFovOnly = false, periphery_taa_enable = false, foveatedVendorDispatch = true;
		uint32_t neuralRenderingInsertionPoint = 0, neuralRenderingMode = 0;
		uint32_t neuralRenderingConfidenceRejectionPercent = 0;
		float foveatedCenterArea = 0.8f, periphery_taa_center_area = 0.3f;
		float foveatedOuterBlendFeather = 0.05f, periphery_taa_outer_scale = 0.9f, periphery_taa_center_blend_feather = 0.05f;
		bool foveatedDisableOuterFeather = false;
		FoveatedMaskCalibration::Reference foveatedCalibrationReference;
		FoveatedMaskCalibration::SetupConfirmation foveatedMaskSetup;
		float foveatedAutomaticMaskScaling = 100.0f, foveatedCenterHorizontalScale = 1.0f;
		float foveatedLeftEyeMaskOffsetX = 0, foveatedLeftEyeMaskOffsetY = 0;
		float foveatedRightEyeMaskOffsetX = 0, foveatedRightEyeMaskOffsetY = 0;
		float neuralRenderingBlendFeather = 0.05f;
		bool foveatedPeripheryMaskVisualization = false;
		bool operator==(const Settings&) const = default;
	} settings;
	struct Cached
	{
		int value = 0;
	} mainFinalLdrNeuralState{ 17 }, mainFinalLdrPresentationState{ 18 };
	uint32_t neuralInsertionPointTransitionFrame = 0;
	std::atomic_uint32_t neuralTemporalAdmissionLatch{ 7 };
	unsigned historyResets = 0, invalidations = 0;
	void RequestHistoryReset() { ++historyResets; }
	void InvalidateFrameScopedUpscalingState() { ++invalidations; }
	static bool HasSameNeuralRenderingSettingsKey(const Settings& a, const Settings& b)
	{
		if (!a.neuralRenderingEnabled && !b.neuralRenderingEnabled)
			return a.neuralRenderingMode == b.neuralRenderingMode && a.neuralRenderingFovOnly == b.neuralRenderingFovOnly &&
			       a.neuralRenderingRenderscaleFov == b.neuralRenderingRenderscaleFov;
		return a == b;
	}
	static bool ApplyNeuralRenderingFovConstraint(Settings&) noexcept;
	static bool IsNeuralRenderingEnabled(const Settings&) noexcept;
	std::optional<float> pendingFoveatedMaskScaling;
	bool neuralRenderingReplacedFovTaa = false;
	bool fovAvailable = true;
	NeuralRendering::RenderingMode GetNeuralRenderingMode() const { return NeuralRendering::ConfiguredRenderingMode(settings.neuralRenderingMode); }
	UpscaleMethod GetUpscaleMethod() const { return UpscaleMethod::kDLSS; }
	bool IsNeuralRenderingFovConfigurationAvailable(UpscaleMethod) const { return fovAvailable; }
	void DrawNeuralRenderingFovWarning(bool) const;
	void DrawFovSettingsLink() const {}
	void DrawPeripheryTAAControl();
	std::string foveatedCalibrationMessage;
	bool foveatedCalibrationWarning = false;
	void DrawFoveatedCalibration();
	bool setupContextAvailable = true;
	bool ValidateFoveatedMaskSetupContext(const Settings&, std::string& error) const
	{
		error = setupContextAvailable ? "" : "FOV setup requires enabled VR FOV in a loaded scene.";
		return setupContextAvailable;
	}
	bool ConfirmFoveatedMaskSetup(Settings&, std::string&) const;
	bool coverageAvailable = true, coverageGap = false;
	FoveatedMaskCalibration::CoverageCheck GetFoveatedBinocularCoverage(const Settings&) const { return { .available = coverageAvailable, .hasGap = coverageGap }; }
	void EditOuterFeatherFromMenu(bool disabled, bool fullEye = false);
	mutable FoveatedMaskCalibration::CaptureMode capturedMode = FoveatedMaskCalibration::CaptureMode::Reuse;
	bool calibrationSucceeds = true;
	bool PrepareFoveatedMaskCalibration(Settings& candidate, FoveatedMaskCalibration::CaptureMode mode, float percent, std::string& error) const
	{
		capturedMode = mode;
		if (!calibrationSucceeds) {
			error = "Calibration unavailable; previous fit retained.";
			return false;
		}
		candidate.foveatedCenterArea = 0.6f;
		candidate.foveatedAutomaticMaskScaling = percent;
		return true;
	}
	bool HandleNeuralRenderingSettingsTransition(const Settings&, const char*, bool* = nullptr);
};

float GetNormalFoveatedBlendFeather(const Upscaling::Settings& settings, bool)
{
	return FoveatedCommon::GetOuterFeather(settings.foveatedOuterBlendFeather, settings.foveatedDisableOuterFeather);
}
json NeuralRenderingStatusJson(const Upscaling& upscaling)
{
	return { { "fovOnly", upscaling.settings.neuralRenderingFovOnly },
		{ "fovScale", upscaling.settings.foveatedCenterArea } };
}

#include "neural_rendering_controls_under_test.h"

void Require(bool value, const char* reason)
{
	if (!value) {
		std::fprintf(stderr, "%s\n", reason);
		throw std::runtime_error(reason);
	}
}

void SetFovOnlySetup(Upscaling& upscaling)
{
	auto& reference = upscaling.settings.foveatedCalibrationReference;
	reference.version = FoveatedMaskCalibration::kReferenceVersion;
	reference.leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	reference.feather = 0.05f;
	upscaling.settings.foveatedMaskSetup = FoveatedMaskCalibration::CaptureSetupConfirmation(upscaling.settings);
}

void CheckOuterFeatherMenuTransactions()
{
	auto& renderer = NeuralRendering::Renderer::Instance();
	for (bool taa : { false, true }) {
		for (float feather : { 0.0f, 0.0001f, 0.05f }) {
			renderer = {};
			Upscaling upscaling;
			SetFovOnlySetup(upscaling);
			upscaling.settings.foveatedOuterBlendFeather = feather;
			upscaling.settings.foveatedCalibrationReference.feather = FoveatedCommon::GetOuterFeather(feather);
			upscaling.settings.foveatedMaskSetup = FoveatedMaskCalibration::CaptureSetupConfirmation(upscaling.settings);
			upscaling.settings.periphery_taa_enable = taa;
			NeuralRendering::NormalizeRenderingCoverage(upscaling.settings, true);
			const auto reference = upscaling.settings.foveatedCalibrationReference;
			for (bool disabled : { true, false }) {
				const auto resets = upscaling.historyResets;
				upscaling.EditOuterFeatherFromMenu(disabled);
				Require(upscaling.historyResets == resets + 1,
					"Menu outer-edge changes must explicitly reset history even at the minimum width with NR off");
				Require(upscaling.settings.foveatedDisableOuterFeather == disabled && upscaling.settings.periphery_taa_enable == taa &&
							upscaling.settings.foveatedCalibrationReference == reference,
					"Menu toggle must preserve calibration and the selected TAA mode");
				upscaling.EditOuterFeatherFromMenu(disabled);
				Require(upscaling.historyResets == resets + 1, "Unchanged menu controls must not reset history every frame");
			}
		}
	}
	Upscaling rejected;
	SetFovOnlySetup(rejected);
	rejected.settings.neuralRenderingEnabled = true;
	NeuralRendering::NormalizeRenderingCoverage(rejected.settings, true);
	const auto saved = rejected.settings;
	renderer = {};
	renderer.quarantined = true;
	renderer.resetSucceeds = false;
	rejected.EditOuterFeatherFromMenu(true, true);
	Require(rejected.settings == saved && !Util::Text::warning.empty(),
		"Unsafe menu transitions must retain the toggle and the entire saved fit");
	renderer = {};
}

void CheckFoveationTransactions()
{
	auto& renderer = NeuralRendering::Renderer::Instance();
	globals::game::isVR = true;
	for (const uint32_t mode : { 0u, 2u }) {
		for (const bool succeeds : { false, true }) {
			Upscaling upscaling;
			SetFovOnlySetup(upscaling);
			upscaling.settings.foveatedCenterArea = 1.0f;
			upscaling.settings.foveatedAutomaticMaskScaling = 80.0f;
			upscaling.settings.neuralRenderingMode = mode;
			upscaling.settings.neuralRenderingEnabled = true;
			NeuralRendering::NormalizeRenderingCoverage(upscaling.settings, true);
			const auto previous = upscaling.settings;
			renderer = {};
			renderer.quarantined = true;
			renderer.resetSucceeds = succeeds;
			ImGui::captureCalibration = true;
			upscaling.DrawFoveatedCalibration();
			ImGui::captureCalibration = false;
			Require(upscaling.capturedMode == FoveatedMaskCalibration::CaptureMode::Manual, "Manual calibration must retain its own capture action");
			Require(succeeds ? upscaling.settings.foveatedCenterArea == 0.6f && upscaling.settings.foveatedAutomaticMaskScaling == 80.0f : upscaling.settings == previous,
				"Manual calibration must retain Outer Coverage and commit only after safe NR retirement");
			Require(succeeds || upscaling.foveatedCalibrationMessage.find("previous calibration retained") != std::string::npos,
				"Rejected calibration must explain that the previous fit is retained");
			upscaling.settings = previous;
			renderer = {};
			renderer.quarantined = true;
			renderer.resetSucceeds = succeeds;
			FoveationConfigurationRequest manualRequest;
			manualRequest.calibrateMasks = true;
			const auto manualResponse = ApplyFoveationConfiguration(upscaling, manualRequest, "foveation_configure", true);
			Require(upscaling.capturedMode == FoveatedMaskCalibration::CaptureMode::Manual &&
						manualResponse.at("ok") == succeeds && upscaling.settings.foveatedAutomaticMaskScaling == 80.0f &&
						(succeeds || upscaling.settings == previous),
				"DevBench manual calibration must retain the same percentage as the menu and roll back on failure");
			upscaling.settings = previous;
			renderer = {};
			renderer.quarantined = true;
			renderer.resetSucceeds = succeeds;
			ImGui::captureHeadset = true;
			upscaling.DrawFoveatedCalibration();
			ImGui::captureHeadset = false;
			Require(upscaling.capturedMode == FoveatedMaskCalibration::CaptureMode::Headset &&
						(succeeds ? upscaling.settings.foveatedCenterArea == .6f && upscaling.settings.foveatedAutomaticMaskScaling == 100.0f : upscaling.settings == previous),
				"Headset setup must use its own action and retain all settings when NR refuses the transition");
			upscaling.settings = previous;
			renderer = {};
			renderer.quarantined = true;
			renderer.resetSucceeds = succeeds;
			FoveationConfigurationRequest headsetRequest;
			headsetRequest.automaticHeadsetSetup = true;
			const auto headsetResponse = ApplyFoveationConfiguration(upscaling, headsetRequest, "foveation_configure", true);
			Require(upscaling.capturedMode == FoveatedMaskCalibration::CaptureMode::Headset &&
						headsetResponse.at("ok") == succeeds && headsetResponse.at("mutationApplied") == succeeds &&
						(succeeds ? upscaling.settings.foveatedCenterArea == .6f && upscaling.settings.foveatedAutomaticMaskScaling == 100.0f : upscaling.settings == previous),
				"DevBench headset setup must use the same transactional fit and report the retained state");
			upscaling.settings = previous;
			renderer = {};
			renderer.quarantined = true;
			renderer.resetSucceeds = succeeds;
			FoveationConfigurationRequest request;
			request.fovOnlyCenterScale = 0.6f;
			request.maskBinocularGapVisualization = true;
			const auto response = ApplyFoveationConfiguration(upscaling, request, "foveation_configure", true);
			Require(response.at("ok") == succeeds && response.at("settingsChanged") == succeeds &&
						response.at("mutationApplied") == succeeds,
				"API receipts must reflect accepted or rolled-back FOV settings");
			Require(succeeds ? upscaling.settings.foveatedCenterArea == 0.6f : upscaling.settings == previous,
				"Failed API transitions must roll back all FOV controls and inherited NR coverage");
			Require(upscaling.foveatedMaskBinocularGapVisualization == succeeds && response.at("previewChanged") == succeeds,
				"Mixed preview and geometry changes must commit or roll back atomically");
			Require(response.at("neuralRendering").at("fovScale") == upscaling.settings.foveatedCenterArea,
				"API status must describe the actual retained geometry");
			if (!succeeds)
				Require(response.at("effectiveNotBeforeFrame").is_null() && response.at("measurementSafeFromFrame").is_null(),
					"Rejected geometry has no effective or measurement-ready frame");
		}
	}
	renderer = {};
	Upscaling preview;
	SetFovOnlySetup(preview);
	preview.settings.neuralRenderingEnabled = true;
	NeuralRendering::NormalizeRenderingCoverage(preview.settings, true);
	const auto saved = preview.settings;
	renderer.quarantined = true;
	renderer.resetSucceeds = false;
	for (bool check : { true, false }) {
		FoveationConfigurationRequest diagnostic;
		diagnostic.maskBinocularGapVisualization = check;
		Require(diagnostic.HasAnyControl(), "Both true and false are explicit preview requests");
		const auto response = ApplyFoveationConfiguration(preview, diagnostic, "foveation_configure", true);
		Require(response.at("ok") == true && response.at("mutationApplied") == true && response.at("noOp") == false &&
					response.at("previewChanged") == true && response.at("settingsChanged") == false && preview.settings == saved &&
					preview.foveatedMaskBinocularGapVisualization == check,
			"Standalone preview changes must retain every calibrated and saved setting");
		Require(!response.at("historyResetRequested").get<bool>() && !response.at("frameScopedStateInvalidated").get<bool>() &&
					!response.at("neuralSettingsTransitionAttempted").get<bool>() && preview.historyResets == 0 && preview.invalidations == 0 && renderer.resets == 0,
			"Diagnostic toggles must not touch temporal history or retire a faulty NR backend");
		const auto repeated = ApplyFoveationConfiguration(preview, diagnostic, "foveation_configure", true);
		Require(repeated.at("noOp") == true && repeated.at("previewChanged") == false &&
					repeated.at("errorCode") == "foveation_configure_noop",
			"An unchanged preview request must retain the normal no-op contract");
	}
	renderer = {};
	Upscaling hardBoundary;
	SetFovOnlySetup(hardBoundary);
	hardBoundary.settings.periphery_taa_enable = true;
	for (bool disabled : { true, false }) {
		FoveationConfigurationRequest toggle;
		toggle.disableOuterFeather = disabled;
		const auto savedReference = hardBoundary.settings.foveatedCalibrationReference;
		const auto response = ApplyFoveationConfiguration(hardBoundary, toggle, "foveation_configure", true);
		Require(response.at("ok") == true && response.at("settingsChanged") == true &&
					hardBoundary.settings.foveatedDisableOuterFeather == disabled &&
					hardBoundary.settings.periphery_taa_enable && hardBoundary.settings.foveatedCalibrationReference == savedReference,
			"The saved outer-fade toggle must retain TAA and calibration in both directions");
	}
	renderer = {};
	Upscaling uncalibrated;
	FoveationConfigurationRequest request;
	request.peripheryTaaEnabled = true;
	const auto previous = uncalibrated.settings;
	const auto response = ApplyFoveationConfiguration(uncalibrated, request, "foveation_configure", true);
	Require(response.at("errorCode") == "foveation_fov_only_setup_required" && uncalibrated.settings == previous,
		"The API must reject uncalibrated TAA without changing controls");
}

void CheckMaskSetupConfirmation()
{
	NeuralRendering::Renderer::Instance() = {};
	Upscaling upscaling;
	ImGui::selectedMode = 1;
	upscaling.DrawPeripheryTAAControl();
	Require(ImGui::taaDisabled && !upscaling.settings.periphery_taa_enable, "TAA must start locked");
	const auto before = upscaling.settings;
	ImGui::finishSetup = true;
	upscaling.setupContextAvailable = false;
	upscaling.DrawFoveatedCalibration();
	Require(upscaling.settings == before, "An unavailable scene must not confirm the mask");
	upscaling.setupContextAvailable = true;
	upscaling.coverageGap = true;
	upscaling.DrawFoveatedCalibration();
	Require(upscaling.settings == before, "A detected shared-view gap must not confirm the mask");
	upscaling.coverageGap = false;
	upscaling.DrawFoveatedCalibration();
	ImGui::finishSetup = false;
	Require(FoveatedMaskCalibration::HasFovOnlySetup(upscaling.settings), "Finish button must unlock a checked manual fit");
	upscaling.DrawPeripheryTAAControl();
	Require(!ImGui::taaDisabled && upscaling.settings.periphery_taa_enable, "Confirmed setup must enable the TAA dropdown");
	for (bool hardEdge : { true, false }) {
		FoveationConfigurationRequest inner;
		inner.disableOuterFeather = hardEdge;
		inner.peripheryTaaCenterScale = 0.4f;
		inner.peripheryTaaBlendFeather = 0.07f;
		const auto confirmed = upscaling.settings.foveatedMaskSetup;
		Require(ApplyFoveationConfiguration(upscaling, inner, "foveation_configure", false).at("ok") == true &&
					upscaling.settings.foveatedMaskSetup == confirmed && upscaling.settings.periphery_taa_center_blend_feather == 0.07f,
			"Inner feather and size edits must preserve the outer fit with or without outer feather");
		const auto locked = upscaling.settings;
		FoveationConfigurationRequest outer;
		outer.leftEyeOffsetX = 0.1f;
		Require(ApplyFoveationConfiguration(upscaling, outer, "foveation_configure", false).at("errorCode") == "foveation_outer_mask_locked" &&
					upscaling.settings == locked,
			"TAA must reject outer edits atomically");
	}
	FoveationConfigurationRequest edit;
	edit.peripheryTaaEnabled = false;
	edit.leftEyeOffsetX = 0.1f;
	Require(ApplyFoveationConfiguration(upscaling, edit, "foveation_configure", false).at("ok") == true &&
				!FoveatedMaskCalibration::HasFovOnlySetup(upscaling.settings),
		"FOV-only edits must clear confirmation");
	FoveationConfigurationRequest finish;
	finish.maskSetupFinished = true;
	Require(ApplyFoveationConfiguration(upscaling, finish, "foveation_configure", false).at("ok") == true &&
				FoveatedMaskCalibration::HasFovOnlySetup(upscaling.settings),
		"DevBench must share the finish-button policy");
	ImGui::selectedMode = -1;
}

void CheckManualSetupConfirmation()
{
	NeuralRendering::Renderer::Instance() = {};
	headsetProjectionAvailable = false;
	for (const bool hardEdge : { false, true }) {
		Upscaling upscaling;
		upscaling.coverageAvailable = false;
		upscaling.settings.foveatedDisableOuterFeather = hardEdge;
		const auto before = upscaling.settings;
		ImGui::finishSetup = true;
		upscaling.DrawFoveatedCalibration();
		ImGui::finishSetup = false;
		auto expected = before;
		expected.foveatedMaskSetup = FoveatedMaskCalibration::CaptureSetupConfirmation(expected);
		Require(upscaling.settings == expected && FoveatedMaskCalibration::HasFovOnlySetup(upscaling.settings),
			"Explicit manual confirmation must accept unavailable headset checks without changing the mask");
		Require(!upscaling.GetFoveatedBinocularCoverage(upscaling.settings).available,
			"Manual acceptance must not invent an automatic coverage result");
		ImGui::selectedMode = 1;
		upscaling.DrawPeripheryTAAControl();
		ImGui::selectedMode = -1;
		Require(!ImGui::taaDisabled && upscaling.settings.periphery_taa_enable,
			"A manually confirmed fit must unlock TAA with feathered or hard outer boundaries");
		upscaling.settings.neuralRenderingEnabled = true;
		upscaling.settings.periphery_taa_enable = false;
		ImGui::selectedMode = 1;
		upscaling.DrawPeripheryTAAControl();
		upscaling.DrawFoveatedCalibration();
		ImGui::selectedMode = -1;
		Require(ImGui::taaDisabled && !upscaling.settings.periphery_taa_enable &&
					ImGui::unformattedText.find("unavailable while NR") != std::string::npos,
			"Manual confirmation must keep NR incompatible with TAA and explain its unavailability");
		upscaling.settings = before;
		FoveationConfigurationRequest finish;
		finish.maskSetupFinished = true;
		upscaling.setupContextAvailable = false;
		Require(ApplyFoveationConfiguration(upscaling, finish, "foveation_configure", false).at("ok") == false && upscaling.settings == before,
			"DevBench confirmation must retain settings when the setup context is unavailable");
		upscaling.setupContextAvailable = true;
		Require(ApplyFoveationConfiguration(upscaling, finish, "foveation_configure", false).at("ok") == true &&
					FoveatedMaskCalibration::HasFovOnlySetup(upscaling.settings) && upscaling.settings.foveatedMaskSetup == expected.foveatedMaskSetup &&
					upscaling.settings.foveatedDisableOuterFeather == hardEdge && upscaling.settings.foveatedCalibrationReference == before.foveatedCalibrationReference &&
					upscaling.settings.foveatedAutomaticMaskScaling == before.foveatedAutomaticMaskScaling,
			"DevBench must share the manual-confirmation path without changing the outer fit or inventing calibration data");
	}
	headsetProjectionAvailable = true;
}

void CheckConfirmedCalibrationFailure()
{
	Upscaling upscaling;
	upscaling.settings.foveatedMaskSetup = FoveatedMaskCalibration::CaptureSetupConfirmation(upscaling.settings);
	const auto confirmed = upscaling.settings;
	upscaling.calibrationSucceeds = false;
	for (const bool automatic : { false, true }) {
		ImGui::captureHeadset = automatic;
		ImGui::captureCalibration = !automatic;
		ImGui::renderedMessage.clear();
		upscaling.DrawFoveatedCalibration();
		Require(upscaling.settings == confirmed && FoveatedMaskCalibration::HasFovOnlySetup(upscaling.settings),
			"Failed calibration must retain a previously confirmed fit");
		Require(upscaling.foveatedCalibrationWarning && Util::Text::warning == upscaling.foveatedCalibrationMessage,
			"Failed calibration must display its message using warning styling");
		Require(ImGui::renderedMessage == "Calibration unavailable; previous fit retained.",
			"A confirmed fit must not hide a later calibration error");
	}
	upscaling.calibrationSucceeds = true;
	Util::Text::warning.clear();
	upscaling.DrawFoveatedCalibration();
	Require(!upscaling.foveatedCalibrationWarning && Util::Text::warning.empty(),
		"A successful retry must return calibration messages to normal styling");
	upscaling.settings = confirmed;
	upscaling.calibrationSucceeds = false;
	ImGui::captureHeadset = ImGui::captureCalibration = false;
	FoveationConfigurationRequest request;
	request.automaticHeadsetSetup = true;
	const auto response = ApplyFoveationConfiguration(upscaling, request, "foveation_configure", true);
	Require(response.at("errorCode") == "foveation_calibration_failed" && response.at("ok") == false &&
				response.at("mutationApplied") == false && response.at("settingsChanged") == false && upscaling.settings == confirmed,
		"DevBench calibration failure must retain the fit and report no mutation");
}

void CheckReconstructionHistoryTransition()
{
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		for (bool enabled : { false, true }) {
			for (unsigned mode : { 0u, 2u }) {
				auto& renderer = NeuralRendering::Renderer::Instance();
				renderer = {};
				Upscaling upscaling;
				upscaling.settings.neuralRenderingEnabled = enabled;
				upscaling.settings.neuralRenderingMode = mode;
				NeuralRendering::NormalizeRenderingCoverage(upscaling.settings, vr);
				const auto previous = upscaling.settings;
				upscaling.settings.neuralRenderingConfidenceRejectionPercent = 50;
				bool retired = true;
				Require(upscaling.HandleNeuralRenderingSettingsTransition(previous, "reconstruction edit", &retired), "Reconstruction edit was rejected");
				Require(upscaling.historyResets == unsigned(enabled) && upscaling.invalidations == unsigned(enabled), "Reconstruction edit did not reset history and cached frame state exactly once");
				Require(upscaling.mainFinalLdrNeuralState.value == (enabled ? 0 : 17) &&
							upscaling.mainFinalLdrPresentationState.value == (enabled ? 0 : 18),
					"Reconstruction edit retained stale presentation or invalidated disabled NR");
				Require(!retired && renderer.resets == 0 && renderer.resourcesRetained, "Reconstruction edit retired healthy native storage");
				Require(upscaling.HandleNeuralRenderingSettingsTransition(upscaling.settings, "unchanged reconstruction") &&
							upscaling.historyResets == unsigned(enabled),
					"Unchanged reconstruction repeated the history reset");
			}
		}
	}
	globals::game::isVR = true;
}

int main()
{
	CheckReconstructionHistoryTransition();
	CheckConfirmedCalibrationFailure();
	CheckMaskSetupConfirmation();
	CheckManualSetupConfirmation();
	CheckOuterFeatherMenuTransactions();
	CheckFoveationTransactions();
	auto& renderer = NeuralRendering::Renderer::Instance();
	for (const bool isVR : { false, true }) {
		globals::game::isVR = isVR;
		for (const bool menuWithoutFrame : { false, true }) {
			globals::State frame;
			globals::state = menuWithoutFrame ? nullptr : &frame;
			for (uint32_t previousMode = 0; previousMode < 3; ++previousMode) {
				for (uint32_t nextMode = 0; nextMode < 3; ++nextMode) {
					for (const bool retirementSucceeds : { false, true }) {
						Upscaling upscaling;
						upscaling.settings.neuralRenderingEnabled = true;
						upscaling.settings.neuralRenderingMode = previousMode;
						NeuralRendering::NormalizeRenderingCoverage(upscaling.settings, isVR);
						const auto previous = upscaling.settings;
						upscaling.settings.neuralRenderingMode = nextMode;
						renderer = {};
						renderer.resetSucceeds = retirementSucceeds;
						bool resetSucceeded = false;
						const bool domainChanged = (previousMode == 2) != (nextMode == 2);
						const bool accepted = upscaling.HandleNeuralRenderingSettingsTransition(previous, "mode transition", &resetSucceeded);
						if (!accepted)
							upscaling.settings = previous;
						Require(renderer.resets == (domainChanged ? 1u : 0u), "Pre/post-DLSS mode switches must retire all native ownership, including a healthy backend");
						Require(accepted == (!domainChanged || retirementSucceeds), "An enabled input-domain switch requires proven retirement");
						Require(resetSucceeded == (domainChanged && retirementSucceeds), "The transition must expose the actual retirement result");
						Require(renderer.resourcesRetained == !(domainChanged && retirementSucceeds), "Failed retirement must retain native resources; A/B switches may reuse them");
						Require(upscaling.settings.neuralRenderingMode == (accepted ? static_cast<uint32_t>(NeuralRendering::ConfiguredRenderingMode(nextMode)) : previous.neuralRenderingMode), "Failed retirement must preserve the prior mode");
						if (NeuralRendering::ConfiguredRenderingMode(previousMode) != NeuralRendering::ConfiguredRenderingMode(nextMode)) {
							Require(upscaling.historyResets == 1 && upscaling.invalidations == 1, "A mode transition must invalidate prepared frame state and history");
							Require(upscaling.neuralInsertionPointTransitionFrame == (menuWithoutFrame ? std::numeric_limits<uint32_t>::max() : frame.frameCount), "No transition-frame evaluation may consume mixed input domains");
						}
					}
				}
			}
		}
	}
	globals::game::isVR = true;
	globals::state = nullptr;
	{
		globals::State frame;
		globals::state = &frame;
		Upscaling upscaling;
		upscaling.settings.neuralRenderingEnabled = true;
		upscaling.settings.neuralRenderingMode = 2;
		upscaling.settings.neuralRenderingFovOnly = true;
		renderer = {};
		for (const bool masked : { true, false, true, false }) {
			const auto before = upscaling.settings;
			upscaling.settings.foveatedVendorDispatch = masked;
			const auto resets = upscaling.historyResets;
			Require(upscaling.HandleNeuralRenderingSettingsTransition(before, "renderscale FOV"), "Both renderscale routes must accept transitions");
			Require(upscaling.historyResets == resets + 1 && upscaling.neuralInsertionPointTransitionFrame == frame.frameCount,
				"Mask changes must invalidate history and block the current transition frame");
			Require(renderer.resets == 0 && upscaling.settings.neuralRenderingFovOnly == masked && upscaling.settings.neuralRenderingRenderscaleFov == masked,
				"Healthy mask transitions must retain backend ownership and the separate Full resolution preference");
			++frame.frameCount;
		}
	}
	for (const auto mode : NeuralRendering::kSelectableRenderingModes) {
		for (const bool succeeds : { false, true }) {
			Upscaling upscaling;
			upscaling.settings.neuralRenderingEnabled = true;
			upscaling.settings.neuralRenderingMode = static_cast<uint32_t>(mode);
			upscaling.settings.foveatedVendorDispatch = false;
			NeuralRendering::NormalizeRenderingCoverage(upscaling.settings, true);
			const auto before = upscaling.settings;
			upscaling.settings.foveatedVendorDispatch = true;
			renderer = {};
			renderer.quarantined = true;
			renderer.resetSucceeds = succeeds;
			const bool accepted = upscaling.HandleNeuralRenderingSettingsTransition(before, "shared FOV coverage");
			if (!accepted)
				upscaling.settings = before;
			Require(accepted == succeeds && renderer.resets == 1 && renderer.resourcesRetained == !succeeds,
				"A quarantined mask transition requires proven retirement and retains unsafe resources on failure");
			Require(upscaling.settings.neuralRenderingMode == before.neuralRenderingMode &&
						upscaling.settings.foveatedVendorDispatch == succeeds && upscaling.settings.neuralRenderingFovOnly == succeeds &&
						upscaling.settings.neuralRenderingRenderscaleFov == succeeds,
				"Failed shared FOV transitions roll back coverage without changing either NR placement");
		}
	}
	for (const bool menuWithoutFrame : { true, false }) {
		globals::State frame;
		globals::state = menuWithoutFrame ? nullptr : &frame;
		for (const bool retirementSucceeds : { false, true }) {
			Upscaling upscaling;
			renderer = {};
			renderer.resetSucceeds = retirementSucceeds;
			renderer.quarantined = !retirementSucceeds;
			auto previous = upscaling.settings;
			previous.neuralRenderingEnabled = true;
			bool resetSucceeded = true;
			const auto characterInvalidations = NeuralRendering::CharacterRendering::Instance().invalidations;
			const bool accepted = upscaling.HandleNeuralRenderingSettingsTransition(previous, "NR off", &resetSucceeded);
			Require(NeuralRendering::CharacterRendering::Instance().invalidations == characterInvalidations + 1 &&
						upscaling.neuralTemporalAdmissionLatch.load() == 0,
				"Off must invalidate character state and reset temporal admission once");
			if (!accepted)
				upscaling.settings = previous;
			Require(accepted && !upscaling.settings.neuralRenderingEnabled, "Off must remain accepted even when retirement fails or no world frame exists");
			Require(resetSucceeded == retirementSucceeds && renderer.resets == 1, "Retirement outcome must remain independent of configuration acceptance");
			Require(renderer.resourcesRetained == !retirementSucceeds, "Off must not release resources retained by a failed reset");
			Require(renderer.lastPolicy == NeuralRendering::BackendRetirementPolicy::RetainHealthyBackend,
				"Settings transitions must request healthy resource-only retirement");
			Require(upscaling.historyResets == 1 && upscaling.invalidations == 1, "Off must invalidate frame state and history");
			Require(upscaling.mainFinalLdrNeuralState.value == 0 && upscaling.mainFinalLdrPresentationState.value == 0, "Off must drop pending neural presentation");
			previous = upscaling.settings;
			upscaling.settings.neuralRenderingEnabled = true;
			const bool reenabled = upscaling.HandleNeuralRenderingSettingsTransition(previous, "NR on", &resetSucceeded);
			if (!reenabled)
				upscaling.settings = previous;
			Require(reenabled == retirementSucceeds && upscaling.settings.neuralRenderingEnabled == retirementSucceeds, "On must still require successful backend retirement");
		}
	}
	globals::state = nullptr;
	for (const bool nrEnabled : { false, true }) {
		Upscaling upscaling;
		upscaling.settings.neuralRenderingEnabled = nrEnabled;
		SetFovOnlySetup(upscaling);
		upscaling.settings.periphery_taa_enable = true;
		for (const auto method : { Upscaling::UpscaleMethod::kDLSS, Upscaling::UpscaleMethod::kFSR }) {
			Require(upscaling.IsPeripheryTAAEnabled(method) == !nrEnabled, "Runtime must block TAA while NR is enabled even before settings normalization");
			upscaling.foveatedDispatch = false;
			Require(!upscaling.IsPeripheryTAAEnabled(method), "Disabled foveation must never enable periphery TAA");
			upscaling.foveatedDispatch = true;
		}
		const auto prior = upscaling.settings;
		Require(Upscaling::ApplyNeuralRenderingFovConstraint(upscaling.settings) == nrEnabled, "Only enabled NR must replace FOV+TAA");
		Require(upscaling.settings.periphery_taa_enable == !nrEnabled, "NR must select centre-only FOV");
		Require(upscaling.settings.foveatedCenterArea == prior.foveatedCenterArea && upscaling.settings.periphery_taa_center_area == prior.periphery_taa_center_area, "Fallback must retain both saved mask profiles");
		Require(!Upscaling::ApplyNeuralRenderingFovConstraint(upscaling.settings), "FOV normalization must be idempotent");
		Util::Text::warning.clear();
		const bool taaBeforeDraw = upscaling.settings.periphery_taa_enable;
		ImGui::selectedMode = taaBeforeDraw ? 0 : 1;
		upscaling.DrawPeripheryTAAControl();
		Require(ImGui::taaDisabled == nrEnabled && ImGui::disabled == 0, "Calibrated TAA dropdown option must be greyed out while NR is enabled");
		Require(upscaling.settings.periphery_taa_enable == (nrEnabled ? taaBeforeDraw : !taaBeforeDraw), "Disabled TAA option must not activate; FOV only stays selectable");
		Require(!Util::Text::warning.empty() == nrEnabled, "NR FOV must display the shared red mask warning");
	}
	nrRuntimeInstalled = false;
	{
		Upscaling upscaling;
		SetFovOnlySetup(upscaling);
		ImGui::selectedMode = 0;
		upscaling.settings.neuralRenderingEnabled = true;
		upscaling.settings.periphery_taa_enable = true;
		Require(!Upscaling::ApplyNeuralRenderingFovConstraint(upscaling.settings) && upscaling.settings.periphery_taa_enable,
			"Missing NR must preserve the saved FOV + TAA preference");
		Require(upscaling.IsPeripheryTAAEnabled(Upscaling::UpscaleMethod::kDLSS), "Missing NR must retain normal periphery TAA rendering");
		Util::Text::warning.clear();
		upscaling.DrawPeripheryTAAControl();
		Require(!ImGui::taaDisabled && !upscaling.settings.periphery_taa_enable && Util::Text::warning.empty(),
			"Missing NR must leave FOV + TAA editable without NR warnings");
	}
	nrRuntimeInstalled = true;
	{
		Upscaling unconfigured;
		ImGui::selectedMode = 1;
		ImGui::tooltip.clear();
		unconfigured.DrawPeripheryTAAControl();
		Require(ImGui::taaDisabled && ImGui::disabled == 0 && !unconfigured.settings.periphery_taa_enable,
			"Uncalibrated TAA must be unavailable without leaking ImGui state");
		Require(ImGui::tooltip.find("Mask setup finished") != std::string::npos,
			"Disabled TAA must explain the FOV-only setup prerequisite");
		unconfigured.settings.periphery_taa_enable = true;
		Require(!unconfigured.IsPeripheryTAAEnabled(Upscaling::UpscaleMethod::kDLSS),
			"Uncalibrated saved TAA must never become active");
	}
	for (const bool priorTaa : { false, true }) {
		Upscaling upscaling;
		renderer = {};
		upscaling.settings.periphery_taa_enable = priorTaa;
		auto previous = upscaling.settings;
		upscaling.settings.neuralRenderingEnabled = true;
		Require(upscaling.HandleNeuralRenderingSettingsTransition(previous, "enable NR"), "Enabling NR must succeed");
		Require(upscaling.neuralRenderingReplacedFovTaa == priorTaa, "Warning must remember an actual TAA fallback only");
		for (const bool isVR : { false, true }) {
			globals::game::isVR = isVR;
			for (const bool fovAvailable : { false, true }) {
				upscaling.fovAvailable = fovAvailable;
				for (uint32_t mode = 0; mode < 3; ++mode) {
					upscaling.settings.neuralRenderingMode = mode;
					for (const bool fovOnly : { false, true }) {
						upscaling.settings.foveatedVendorDispatch = fovOnly;
						NeuralRendering::NormalizeRenderingCoverage(upscaling.settings, isVR);
						Util::Text::warning.clear();
						upscaling.DrawNeuralRenderingFovWarning(true);
						Require(!Util::Text::warning.empty() == (isVR && fovOnly && (!fovAvailable || priorTaa)),
							"NR menu explains missing FOV or the active FOV+TAA replacement");
						Util::Text::warning.clear();
						upscaling.DrawNeuralRenderingFovWarning(false);
						Require(!Util::Text::warning.empty() == (isVR && fovOnly), "Upscaling must retain its NR-on warning regardless of prior TAA or NR mode");
					}
				}
			}
		}
		globals::game::isVR = true;
		previous = upscaling.settings;
		Require(upscaling.HandleNeuralRenderingSettingsTransition(previous, "redraw"), "Passive redraw must succeed");
		Require(upscaling.neuralRenderingReplacedFovTaa == priorTaa, "Passive redraw must retain the actual fallback notice");
		upscaling.settings.neuralRenderingEnabled = false;
		renderer.resetSucceeds = false;
		Require(upscaling.HandleNeuralRenderingSettingsTransition(previous, "disable NR"), "Disabling must remain accepted after failed retirement");
		Require(!upscaling.neuralRenderingReplacedFovTaa, "Disabling NR must clear the fallback notice");
		for (const bool nrMenu : { false, true }) {
			Util::Text::warning.clear();
			upscaling.DrawNeuralRenderingFovWarning(nrMenu);
			Require(Util::Text::warning.empty(), "Neither menu may warn while NR is off");
		}
		previous = upscaling.settings;
		previous.periphery_taa_enable = true;
		upscaling.settings.neuralRenderingEnabled = true;
		Require(!upscaling.HandleNeuralRenderingSettingsTransition(previous, "rejected enable"), "Unsafe enabling must still fail");
		Require(!upscaling.neuralRenderingReplacedFovTaa, "Rejected transitions must not create a fallback notice");
	}
}
