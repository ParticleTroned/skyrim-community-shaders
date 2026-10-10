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
constexpr int ImGuiHoveredFlags_AllowWhenDisabled = 1;
namespace ImGui
{
	int disabled = 0, selectedMode = -1;
	bool taaDisabled = false;
	std::string tooltip;
	bool captureCalibration = false;
	bool Button(const char*) { return captureCalibration; }
	bool SliderFloat(const char*, float*, float, float, const char*) { return false; }
	bool IsItemActive() { return false; }
	template <class... T>
	void TextWrapped(const char*, T&&...)
	{}
	void TextUnformatted(const char*) {}
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
	}
}
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
		kDLSS,
		kFSR
	};
	bool foveatedDispatch = true;
	bool IsFoveatedVendorDispatchEnabled(UpscaleMethod) const { return foveatedDispatch; }
	bool IsPeripheryTAAEnabled(UpscaleMethod) const;
	struct Settings
	{
		bool neuralRenderingEnabled = false;
		bool neuralCharacterProviderBlending = false;
		bool neuralRenderingRenderscaleFov = false;
		bool neuralRenderingFovOnly = false, periphery_taa_enable = false, foveatedVendorDispatch = true;
		uint32_t neuralRenderingInsertionPoint = 0, neuralRenderingMode = 0;
		float foveatedCenterArea = 0.8f, periphery_taa_center_area = 0.3f;
		float foveatedOuterBlendFeather = 0.05f, periphery_taa_outer_scale = 0.9f, periphery_taa_center_blend_feather = 0.05f;
		FoveatedMaskCalibration::Reference foveatedCalibrationReference;
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
	static bool HasSameNeuralRenderingSettingsKey(const Settings& a, const Settings& b) { return a == b; }
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
	void DrawFoveatedCalibration();
	bool PrepareFoveatedMaskCalibration(Settings& candidate, bool, float percent, std::string&) const
	{
		candidate.foveatedCenterArea = 0.6f;
		candidate.foveatedAutomaticMaskScaling = percent;
		return true;
	}
	bool HandleNeuralRenderingSettingsTransition(const Settings&, const char*, bool* = nullptr);
};

float GetNormalFoveatedBlendFeather(const Upscaling::Settings& settings, bool)
{
	return std::max(settings.foveatedOuterBlendFeather, FoveatedCommon::kMinimumFeather);
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
	reference.version = 1;
	reference.leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	reference.feather = 0.05f;
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
			Require(succeeds ? upscaling.settings.foveatedCenterArea == 0.6f : upscaling.settings == previous,
				"Calibration must commit only after safe NR retirement, preserving all prior settings on failure");
			Require(succeeds || upscaling.foveatedCalibrationMessage.find("previous calibration retained") != std::string::npos,
				"Rejected calibration must explain that the previous fit is retained");
			upscaling.settings = previous;
			renderer = {};
			renderer.quarantined = true;
			renderer.resetSucceeds = succeeds;
			FoveationConfigurationRequest request;
			request.fovOnlyCenterScale = 0.6f;
			const auto response = ApplyFoveationConfiguration(upscaling, request, "foveation_configure", true);
			Require(response.at("ok") == succeeds && response.at("settingsChanged") == succeeds &&
						response.at("mutationApplied") == succeeds,
				"API receipts must reflect accepted or rolled-back FOV settings");
			Require(succeeds ? upscaling.settings.foveatedCenterArea == 0.6f : upscaling.settings == previous,
				"Failed API transitions must roll back all FOV controls and inherited NR coverage");
			Require(response.at("neuralRendering").at("fovScale") == upscaling.settings.foveatedCenterArea,
				"API status must describe the actual retained geometry");
			if (!succeeds)
				Require(response.at("effectiveNotBeforeFrame").is_null() && response.at("measurementSafeFromFrame").is_null(),
					"Rejected geometry has no effective or measurement-ready frame");
		}
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

int main()
{
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
		Require(ImGui::tooltip.find("Calibrate Masks") != std::string::npos,
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
