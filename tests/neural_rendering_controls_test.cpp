#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

using uint32_t = std::uint32_t;
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
	struct Renderer
	{
		bool resetSucceeds = true, failed = false, quarantined = false;
		bool resourcesRetained = true;
		unsigned resets = 0;
		static Renderer& Instance()
		{
			static Renderer instance;
			return instance;
		}
		bool IsFailureLatched() const { return failed; }
		bool IsQuarantined() const { return quarantined; }
		bool Reset()
		{
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
namespace ImGui
{
	int disabled = 0;
	bool checkboxDisabled = false;
	void TextUnformatted(const char*) {}
	void Checkbox(const char*, bool* value)
	{
		checkboxDisabled = disabled != 0;
		if (!checkboxDisabled)
			*value = !*value;
	}
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
	}
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
		bool neuralRenderingEnabled = false, neuralCharacterMultiRoiEnabled = false;
		bool neuralRenderingFovOnly = false, periphery_taa_enable = false, foveatedVendorDispatch = true;
		uint32_t neuralRenderingInsertionPoint = 0, neuralRenderingMode = 0;
		float foveatedCenterArea = 0.8f, periphery_taa_center_area = 0.3f;
		bool operator==(const Settings&) const = default;
	} settings;
	struct Cached
	{
		int value = 0;
	} mainFinalLdrNeuralState{ 17 }, mainFinalLdrPresentationState{ 18 };
	uint32_t neuralInsertionPointTransitionFrame = 0;
	unsigned historyResets = 0, invalidations = 0;
	void RequestHistoryReset() { ++historyResets; }
	void InvalidateFrameScopedUpscalingState() { ++invalidations; }
	static bool HasSameNeuralRenderingSettingsKey(const Settings& a, const Settings& b) { return a == b; }
	static bool ApplyNeuralRenderingFovConstraint(Settings&) noexcept;
	bool neuralRenderingReplacedFovTaa = false;
	bool fovAvailable = true;
	NeuralRendering::RenderingMode GetNeuralRenderingMode() const { return NeuralRendering::ClampRenderingMode(settings.neuralRenderingMode); }
	UpscaleMethod GetUpscaleMethod() const { return UpscaleMethod::kDLSS; }
	bool IsNeuralRenderingFovConfigurationAvailable(UpscaleMethod) const { return fovAvailable; }
	void DrawNeuralRenderingFovWarning(bool) const;
	void DrawPeripheryTAAControl();
	bool HandleNeuralRenderingSettingsTransition(const Settings&, const char*, bool* = nullptr);
};

#include "neural_rendering_controls_under_test.h"

void Require(bool value, const char* reason)
{
	if (!value)
		throw std::runtime_error(reason);
}

int main()
{
	auto& renderer = NeuralRendering::Renderer::Instance();
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
			const bool accepted = upscaling.HandleNeuralRenderingSettingsTransition(previous, "NR off", &resetSucceeded);
			if (!accepted)
				upscaling.settings = previous;
			Require(accepted && !upscaling.settings.neuralRenderingEnabled, "Off must remain accepted even when retirement fails or no world frame exists");
			Require(resetSucceeded == retirementSucceeds && renderer.resets == 1, "Retirement outcome must remain independent of configuration acceptance");
			Require(renderer.resourcesRetained == !retirementSucceeds, "Off must not release resources retained by a failed reset");
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
		upscaling.DrawPeripheryTAAControl();
		Require(ImGui::checkboxDisabled == nrEnabled && ImGui::disabled == 0, "FOV+TAA UI must be greyed out only while NR is enabled");
		Require(upscaling.settings.periphery_taa_enable == (nrEnabled ? taaBeforeDraw : !taaBeforeDraw), "Disabled FOV+TAA control must not change its value");
		Require(!Util::Text::warning.empty() == nrEnabled, "NR FOV must display the shared red mask warning");
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
						upscaling.settings.neuralRenderingFovOnly = fovOnly;
						Util::Text::warning.clear();
						upscaling.DrawNeuralRenderingFovWarning(true);
						Require(!Util::Text::warning.empty() == (isVR && priorTaa && fovAvailable && (mode != 0 || fovOnly)),
							"NR menu requires prior TAA and a configured FOV-dependent selection");
						Util::Text::warning.clear();
						upscaling.DrawNeuralRenderingFovWarning(false);
						Require(!Util::Text::warning.empty() == isVR, "Upscaling must retain its NR-on warning regardless of prior TAA or NR mode");
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
