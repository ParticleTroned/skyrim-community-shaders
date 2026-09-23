#include "Features/FoveatedCommon.h"
#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace spdlog::level
{
	enum level_enum
	{
		trace,
		debug,
		info,
		warn,
		err,
		critical,
		off
	};
}
struct State
{
	spdlog::level::level_enum level = spdlog::level::info;
	spdlog::level::level_enum GetLogLevel() const { return level; }
	bool IsDeveloperMode();
};
namespace globals
{
	State* state = nullptr;
	namespace game
	{
		bool isVR = true;
	}
	namespace features
	{
		struct Upscaling
		{
			enum class UpscaleMethod
			{
				kNONE,
				kTAA,
				kFSR,
				kDLSS
			};
			unsigned draws = 0;
			bool loaded = true;
			struct Settings
			{
				bool neuralRenderingFovOnly = false, neuralRenderingEnabled = false, neuralCharacterRenderingEnabled = false;
				bool neuralRenderingRenderscaleFov = false;
				bool neuralCharacterFacesEnabled = true, neuralCharacterSkinEnabled = true, neuralCharacterHairEnabled = true;
				unsigned neuralRenderingMode = 0;
				bool foveatedVendorDispatch = true, periphery_taa_enable = false;
				float periphery_taa_center_area = 0.3f, foveatedCenterArea = 0.3f;
				float foveatedCenterHorizontalScale = 1.0f;
				float foveatedLeftEyeMaskOffsetX = 0.0f, foveatedLeftEyeMaskOffsetY = 0.0f;
				float foveatedRightEyeMaskOffsetX = 0.0f, foveatedRightEyeMaskOffsetY = 0.0f;
			} settings;
			bool neuralRenderingFeatureAvailable = true;
			UpscaleMethod method = UpscaleMethod::kDLSS;
			std::optional<UpscaleMethod> runtimeMethod;
			UpscaleMethod lastDrawMethod = UpscaleMethod::kNONE;
			NeuralRendering::RenderingMode GetNeuralRenderingMode() const { return NeuralRendering::ClampRenderingMode(settings.neuralRenderingMode); }
			bool IsNeuralRenderingFovConfigurationAvailable() const;
			bool IsNeuralRenderingFovConfigurationAvailable(UpscaleMethod a_upscaleMethod) const;
			bool renderScaleRequested = true, renderScaleLatched = true, renderScaleActive = true;
			bool GetVRRenderScaleModeRequested() const { return renderScaleRequested; }
			bool IsVRRenderScaleModeLatched() const { return renderScaleLatched; }
			bool IsVRRenderScaleModeActive() const { return renderScaleActive; }
			bool IsNeuralRenderingRenderScaleRequired() const noexcept;
			bool IsNeuralRenderingRenderScaleAvailable() const noexcept;
			bool IsNeuralRenderingRequested() const noexcept;
			bool IsFoveatedVendorDispatchEnabled(UpscaleMethod) const;
			bool IsActiveUpscalingFoveatedProfileAvailable() const;
			void DrawSelectionControls(bool a_essentialsOnly = true);
			void DrawNeuralRenderingFovWarning(bool) const {}
			static bool ApplyNeuralRenderingFovConstraint(Settings& value)
			{
				if (!value.neuralRenderingEnabled || !value.periphery_taa_enable)
					return false;
				value.periphery_taa_enable = false;
				return true;
			}
			UpscaleMethod GetUpscaleMethod() const { return method; }
			UpscaleMethod GetRuntimeUpscaleMethod() const { return runtimeMethod.value_or(method); }
			void DrawNeuralRenderingSettings(UpscaleMethod value, bool = false)
			{
				++draws;
				lastDrawMethod = value;
			}
		} upscaling;
	}
}
using Upscaling = globals::features::Upscaling;
using uint = unsigned;
float ClampFoveatedCenterScale(float value) { return FoveatedCommon::ClampCenterScale(value); }
float ClampFoveatedCenterHorizontalScale(float value) { return FoveatedCommon::ClampCenterHorizontalScale(value); }
float ClampFoveatedMaskOffsetAdjustment(float value) { return value; }
constexpr int ImGuiSliderFlags_AlwaysClamp = 1;
namespace ImGui
{
	std::vector<std::string> items;
	std::vector<bool> disabledItems;
	std::string clicked;
	int treeDepth = 0, comboDepth = 0;
	std::string colourPreview;
	unsigned disableDepth = 0;
	bool openTrees = true;
	float sliderEditValue = 50.0f;
	int comboEditValue = 0;
	int lightingSliderFlags = 0;
	void Record(const char* label)
	{
		items.emplace_back(label);
		disabledItems.push_back(disableDepth != 0);
	}
	bool Seen(std::string_view label)
	{
		return std::find(items.begin(), items.end(), label) != items.end();
	}
	bool Disabled(std::string_view label)
	{
		const auto item = std::find(items.begin(), items.end(), label);
		return item != items.end() && disabledItems[static_cast<std::size_t>(item - items.begin())];
	}
	void Clear(std::string_view click = {})
	{
		items.clear();
		disabledItems.clear();
		clicked = click;
		treeDepth = comboDepth = 0;
		colourPreview.clear();
	}
	template <class... Args>
	void TextWrapped(const char* label, Args&&...)
	{
		Record(label);
	}
	template <class... Args>
	void TextDisabled(const char* label, Args&&...)
	{
		Record(label);
	}
	void TextUnformatted(const char* label) { Record(label); }
	void SeparatorText(const char* label) { Record(label); }
	void Separator() {}
	void SameLine() {}
	void PushID(int) {}
	void PopID() {}
	bool Button(const char* label)
	{
		Record(label);
		return disableDepth == 0 && clicked == label;
	}
	bool Checkbox(const char* label, bool* value)
	{
		if (!Button(label))
			return false;
		*value = !*value;
		return true;
	}
	template <class... Args>
	bool Combo(const char* label, int* value, Args&&...)
	{
		if (!Button(label))
			return false;
		*value = comboEditValue;
		return true;
	}
	bool BeginCombo(const char* label, const char* preview)
	{
		Record(label);
		colourPreview = preview;
		if (disableDepth != 0)
			return false;
		++comboDepth;
		return true;
	}
	bool Selectable(const char* label, bool) { return Button(label); }
	void SetItemDefaultFocus() {}
	void EndCombo() { --comboDepth; }
	bool SliderFloat(const char* label, float* value, float minimum, float maximum, const char* = "%.3f", int flags = 0)
	{
		if (std::string_view(label) == "Lighting preservation")
			lightingSliderFlags = flags;
		if (!Button(label))
			return false;
		*value = (flags & ImGuiSliderFlags_AlwaysClamp) ? std::clamp(sliderEditValue, minimum, maximum) : sliderEditValue;
		return true;
	}
	bool TreeNode(const char* label)
	{
		Record(label);
		if (openTrees)
			++treeDepth;
		return openTrees;
	}
	void TreePop() { --treeDepth; }
}
namespace Util
{
	bool HoverTooltipWrapper() { return false; }
	class DisableGuard
	{
		bool disabled_;

	public:
		explicit DisableGuard(bool disabled) : disabled_(disabled)
		{
			if (disabled_)
				++ImGui::disableDepth;
		}
		~DisableGuard()
		{
			if (disabled_)
				--ImGui::disableDepth;
		}
	};
}
#define IM_ARRAYSIZE(value) (sizeof(value) / sizeof((value)[0]))

namespace NeuralRendering::Color
{
	struct Registry
	{
		Configuration configuration{};
		bool accept = true;
		static Registry& Instance()
		{
			static Registry instance;
			return instance;
		}
		Configuration Snapshot() const { return configuration; }
		bool Configure(const Settings& settings, const Experiments& experiments, std::uint64_t revision)
		{
			if (!accept || revision != configuration.revision || !Valid(settings) || !Valid(experiments))
				return false;
			if (settings == configuration.settings && experiments == configuration.experiments)
				return true;
			configuration.settings = settings;
			configuration.experiments = experiments;
			++configuration.revision;
			return true;
		}
	};
}
using namespace NeuralRendering::Color;
unsigned statusReads = 0, assetReads = 0;
nlohmann::json StatusJson()
{
	++statusReads;
	return nlohmann::json::object();
}
nlohmann::json AssetsJson()
{
	++assetReads;
	return nlohmann::json::object();
}
struct NeuralRenderingFeature
{
	void DrawSettings();
	void DrawEssentialSettings();
};
#include "neural_rendering_ui_under_test.h"

int main()
{
	const auto require = [](bool condition, const char* message) {
		if (!condition)
			throw std::runtime_error(message);
	};
	State state;
	globals::state = &state;
	NeuralRenderingFeature feature;
	auto& registry = Registry::Instance();
	registry.configuration.settings.mode = Mode::PreserveSource;
	registry.configuration.settings.detailStrength = 1.25f;
	registry.configuration.settings.lightingPreservation = 0.37f;
	const auto draw = [&](std::string_view click = {}) {
		ImGui::Clear(click);
		feature.DrawSettings();
		require(ImGui::treeDepth == 0 && ImGui::comboDepth == 0, "UI tree and combo scopes must be balanced");
		require(ImGui::disableDepth == 0, "UI disable scopes must be balanced");
	};
	for (auto level : { spdlog::level::info, spdlog::level::warn, spdlog::level::err,
			 spdlog::level::critical, spdlog::level::off }) {
		state.level = level;
		draw();
		require(ImGui::Seen("Enable colour processing") && ImGui::Seen("Colour mode") &&
					ImGui::Seen("Detail contribution"),
			"Production colour controls remain available");
		require(!ImGui::Seen("Colour experiments and diagnostics") && statusReads == 0 && assetReads == 0,
			"Info and quieter levels cannot expose or generate diagnostics");
		require(ImGui::Seen("Original") && ImGui::Seen("Preserve source") && !ImGui::Seen("Managed (experimental)"),
			"Normal mode choices must exclude experimental Managed");
	}
	for (auto level : { spdlog::level::debug, spdlog::level::trace }) {
		state.level = level;
		draw();
		for (const auto* control : { "Apply neural edit (A/B; inference stays running)", "Capture engine HDR exposure",
				 "Capture HMD frame provenance", "Source-domain candidate", "Transport bypass (skip neural evaluation)",
				 "Bounded asynchronous colour samples", "Colour diagnostics", "Check installed colour shaders" })
			require(ImGui::Seen(control), "Debug and Trace expose the complete assessment controls");
		require(ImGui::Seen("Managed (experimental)"), "Developer mode must explicitly label the Managed choice experimental");
	}
	require(statusReads == 2 && assetReads == 0, "Asset checks run only on explicit request");
	draw("Check installed colour shaders");
	require(assetReads == 1, "Explicit shader check must run");
	state.level = spdlog::level::info;
	registry.configuration.experiments.transportBypass = true;
	registry.configuration.experiments.applyModelEdit = false;
	const auto before = registry.Snapshot();
	draw();
	require(registry.configuration.experiments == before.experiments && registry.configuration.revision == before.revision,
		"Hiding controls must not silently cancel active experiments");
	require(ImGui::Seen("Restore normal colour processing") && !ImGui::Seen("Colour experiments and diagnostics"),
		"Hidden output overrides retain a production recovery action");
	registry.accept = false;
	draw("Restore normal colour processing");
	require(registry.configuration.experiments == before.experiments, "Rejected reset must retain active settings");
	require(ImGui::Seen("Settings changed concurrently; retry after the next UI refresh."), "Rejected reset must be visible");
	registry.accept = true;
	draw("Restore normal colour processing");
	require(registry.configuration.experiments == Experiments{} && registry.configuration.settings == before.settings,
		"Recovery clears only experiments, preserving colour mode and tuning");
	registry.configuration.experiments.captureFrameEvidence = true;
	registry.configuration.experiments.diagnostics = true;
	draw();
	require(!ImGui::Seen("Restore normal colour processing"), "Capture-only diagnostics do not claim an image override");
	registry.configuration.experiments.profiles[0].domain = Domain::Linear;
	registry.configuration.experiments.profiles[0].transform = Transform::LinearToSRGB;
	draw();
	require(ImGui::Seen("Restore normal colour processing"), "Hidden colour calibration must remain recoverable");
	registry.configuration.settings.enabled = false;
	draw();
	require(!ImGui::Seen("Restore normal colour processing"), "Disabled processing cannot claim active profile overrides");
	globals::state = nullptr;
	draw();
	require(!ImGui::Seen("Colour experiments and diagnostics"), "Missing state fails closed for diagnostics");
	require(!ImGui::Seen("Managed (experimental)"), "Missing state must not offer experimental colour selection");
	require(globals::features::upscaling.draws > 0, "Feature must retain the main NR controls");

	auto& upscaling = globals::features::upscaling;
	// Exercise the actual routing controls independently of world-frame availability.
	for (const bool haveWorldState : { false, true }) {
		globals::state = haveWorldState ? &state : nullptr;
		for (const bool isVR : { false, true }) {
			globals::game::isVR = isVR;
			for (unsigned mode = 0; mode < 3; ++mode) {
				for (const bool fovAvailable : { false, true }) {
					for (const bool fovOnly : { false, true }) {
						for (const bool characters : { false, true }) {
							upscaling.settings = {};
							upscaling.settings.neuralRenderingMode = mode;
							upscaling.settings.foveatedVendorDispatch = fovAvailable;
							upscaling.settings.neuralRenderingFovOnly = fovOnly;
							upscaling.settings.neuralCharacterRenderingEnabled = characters;
							for (const bool enabled : { true, false, true, false }) {
								ImGui::Clear("Enabled");
								upscaling.DrawSelectionControls();
								require(!ImGui::Disabled("Enabled") && upscaling.settings.neuralRenderingEnabled == enabled,
									"Master must remain editable across repeated toggles, saved child settings and the main menu");
								require(upscaling.settings.neuralRenderingFovOnly == fovOnly && upscaling.settings.neuralCharacterRenderingEnabled == characters,
									"Master must preserve child preferences");
								const auto route = upscaling.GetNeuralRenderingMode();
								const bool executable = enabled && NeuralRendering::IsRenderingConfigurationSupported(isVR, route) &&
								                        (!NeuralRendering::RequiresFoveatedMask(route, fovOnly, isVR, upscaling.settings.neuralRenderingRenderscaleFov) || upscaling.IsNeuralRenderingFovConfigurationAvailable());
								require(upscaling.IsNeuralRenderingRequested() == executable, "Editable master must not bypass execution prerequisites");
								upscaling.neuralRenderingFeatureAvailable = false;
								require(!upscaling.IsNeuralRenderingRequested(), "Unloaded NR must not execute even with valid preferences");
								upscaling.neuralRenderingFeatureAvailable = true;
								require(ImGui::disableDepth == 0 && ImGui::comboDepth == 0, "Routing controls must restore UI state");
							}
							if (!isVR && mode != 1 && !fovOnly && !characters) {
								ImGui::Clear("Characters only");
								upscaling.DrawSelectionControls();
								require(!ImGui::Disabled("Characters only") && upscaling.settings.neuralCharacterRenderingEnabled,
									"Both flat modes must allow character selection without VR FOV");
								ImGui::Clear("Enabled");
								upscaling.DrawSelectionControls();
								require(upscaling.IsNeuralRenderingRequested(), "Enabled flat character selection must reach the shared mono route");
							}
							if (characters) {
								ImGui::Clear("Characters only");
								upscaling.DrawSelectionControls();
								require(!upscaling.settings.neuralCharacterRenderingEnabled, "Selected character restriction must always be removable");
							}
							if (mode == 1 || (isVR && mode == 2)) {
								ImGui::Clear("Full resolution");
								upscaling.DrawSelectionControls();
								require(upscaling.settings.neuralRenderingMode == 0, "Unavailable Foveated mode must allow return to Full resolution");
							}
							if (fovOnly && upscaling.GetNeuralRenderingMode() == NeuralRendering::RenderingMode::FullResolution) {
								ImGui::Clear("Restrict to FOV mask");
								upscaling.DrawSelectionControls();
								require(!upscaling.settings.neuralRenderingFovOnly, "Selected FOV restriction must always be removable");
							}
						}
					}
				}
			}
		}
	}
	globals::game::isVR = true;
	upscaling.settings = {};
	globals::state = &state;
	registry.configuration = {};
	using ModeChoice = NeuralRendering::RenderingMode;
	require(!NeuralRendering::IsRenderingModeSelectable(true, ModeChoice::Foveated, false), "Unavailable FOV cannot be newly selected");
	require(NeuralRendering::IsRenderingModeSelectable(true, ModeChoice::Foveated, true), "Configured FOV enables the foveated choice");
	require(NeuralRendering::IsRenderingModeSelectable(true, ModeChoice::FullResolution, false), "Full resolution allows escape from unavailable automatic FOV modes");
	require(NeuralRendering::IsRenderingModeSelectable(true, ModeChoice::ReducedResolution, false), "VR renderscale mode remains selectable without FOV");
	require(NeuralRendering::IsRenderingModeSelectable(false, ModeChoice::ReducedResolution, false) &&
				!NeuralRendering::IsRenderingModeSelectable(false, ModeChoice::Foveated, true) &&
				NeuralRendering::IsRenderingModeSelectable(false, ModeChoice::FullResolution, false),
		"Mode availability preserves flat-runtime support");
	upscaling.settings.neuralRenderingEnabled = true;
	upscaling.settings.foveatedVendorDispatch = true;
	ImGui::Clear("Renderscale NR before DLSS");
	upscaling.DrawSelectionControls();
	require(upscaling.GetNeuralRenderingMode() == ModeChoice::ReducedResolution && upscaling.IsNeuralRenderingRequested(),
		"VR renderscale selection defaults to the unrestricted route");
	require(ImGui::Seen("Use FOV mask for Renderscale NR") && !upscaling.settings.neuralRenderingRenderscaleFov,
		"Renderscale must expose an independent, default-off FOV switch");
	upscaling.settings.neuralRenderingFovOnly = true;
	for (const bool masked : { true, false, true }) {
		ImGui::Clear("Use FOV mask for Renderscale NR");
		upscaling.DrawSelectionControls();
		require(upscaling.settings.neuralRenderingRenderscaleFov == masked && upscaling.settings.neuralRenderingFovOnly,
			"Repeated renderscale comparisons must preserve the Full resolution restriction");
	}
	upscaling.settings.foveatedVendorDispatch = false;
	ImGui::Clear();
	upscaling.DrawSelectionControls();
	require(!upscaling.IsNeuralRenderingRequested() && !ImGui::Disabled("Enabled"),
		"An enabled renderscale FOV restriction must wait for configured FOV without locking its master");
	ImGui::Clear("Use FOV mask for Renderscale NR");
	upscaling.DrawSelectionControls();
	require(!upscaling.settings.neuralRenderingRenderscaleFov && upscaling.IsNeuralRenderingRequested() &&
				upscaling.IsFoveatedVendorDispatchEnabled(Upscaling::UpscaleMethod::kDLSS),
		"Removing an unavailable FOV restriction must restore the full-eye NR dispatch adapter");
	require(!upscaling.IsActiveUpscalingFoveatedProfileAvailable(),
		"Full-eye NR dispatch must not enable shared shader foveation while global FOV is off");
	ImGui::Clear("Use FOV mask for Renderscale NR");
	upscaling.DrawSelectionControls();
	require(ImGui::Disabled("Use FOV mask for Renderscale NR") && !upscaling.settings.neuralRenderingRenderscaleFov,
		"Missing configured FOV must prevent newly enabling the restriction");
	ImGui::Clear("Full resolution");
	upscaling.DrawSelectionControls();
	require(!upscaling.IsNeuralRenderingRequested() && upscaling.settings.neuralRenderingFovOnly,
		"Returning to Full resolution retains its independent saved restriction");
	globals::game::isVR = false;
	for (const auto mode : { ModeChoice::FullResolution, ModeChoice::ReducedResolution }) {
		upscaling.settings = {};
		upscaling.settings.neuralRenderingMode = static_cast<unsigned>(mode);
		upscaling.settings.foveatedVendorDispatch = false;
		upscaling.settings.neuralCharacterRenderingEnabled = true;
		for (const auto* category : { "Faces", "Skin", "Hair" }) {
			ImGui::Clear(category);
			upscaling.DrawSelectionControls();
			require(ImGui::Seen(category) && !ImGui::Disabled(category), "Both flat modes must expose editable essential character categories");
		}
		require(!upscaling.settings.neuralCharacterFacesEnabled && !upscaling.settings.neuralCharacterSkinEnabled &&
					!upscaling.settings.neuralCharacterHairEnabled,
			"Flat category edits must update their independent selections");
		ImGui::Clear("Foveated");
		upscaling.DrawSelectionControls();
		require(ImGui::Disabled("Foveated") && upscaling.GetNeuralRenderingMode() == mode, "Flat modes cannot select the VR Foveated route");
		ImGui::Clear("Restrict to FOV mask");
		upscaling.DrawSelectionControls();
		require((mode == ModeChoice::ReducedResolution ? !ImGui::Seen("Use FOV mask for Renderscale NR") :
														 ImGui::Disabled("Restrict to FOV mask")) &&
					!upscaling.settings.neuralRenderingFovOnly,
			"Flat modes cannot newly enable VR FOV restriction");
	}
	for (const auto method : { Upscaling::UpscaleMethod::kNONE, Upscaling::UpscaleMethod::kTAA,
			 Upscaling::UpscaleMethod::kFSR, Upscaling::UpscaleMethod::kDLSS }) {
		upscaling.settings = {};
		upscaling.method = method;
		upscaling.settings.neuralCharacterRenderingEnabled = true;
		ImGui::Clear("Renderscale NR before DLSS");
		upscaling.DrawSelectionControls();
		require(!ImGui::Disabled("Renderscale NR before DLSS") && upscaling.GetNeuralRenderingMode() == ModeChoice::ReducedResolution,
			"Flat reduced mode remains selectable independently of backend readiness");
		for (const bool enabled : { true, false }) {
			ImGui::Clear("Enabled");
			upscaling.DrawSelectionControls();
			require(!ImGui::Disabled("Enabled") && upscaling.settings.neuralRenderingEnabled == enabled,
				"Flat reduced mode cannot make the master stale when the upscaler changes");
		}
		ImGui::Clear("Full resolution");
		upscaling.DrawSelectionControls();
		require(upscaling.GetNeuralRenderingMode() == ModeChoice::FullResolution && upscaling.settings.neuralCharacterRenderingEnabled,
			"Full resolution remains a selectable fallback without losing character selection");
	}
	upscaling.method = Upscaling::UpscaleMethod::kDLSS;
	globals::game::isVR = true;
	for (auto mode : { NeuralRendering::RenderingMode::FullResolution, NeuralRendering::RenderingMode::Foveated,
			 NeuralRendering::RenderingMode::ReducedResolution }) {
		for (const bool fovOnly : { false, true }) {
			for (const bool available : { false, true }) {
				upscaling.settings.neuralRenderingMode = static_cast<unsigned>(mode);
				upscaling.settings.neuralRenderingFovOnly = fovOnly;
				upscaling.settings.foveatedVendorDispatch = available;
				const bool blocked = !available && (mode == NeuralRendering::RenderingMode::Foveated ||
													   (mode == NeuralRendering::RenderingMode::FullResolution && fovOnly));
				const auto beforeFovEdit = registry.Snapshot();
				draw("Enable colour processing");
				require(ImGui::Disabled("Colour mode") == blocked && ImGui::Disabled("Enable colour processing") == blocked,
					"FOV-dependent colour options stay grey until the shared mask is available");
				require((registry.configuration.settings.enabled == beforeFovEdit.settings.enabled) == blocked,
					"Unavailable FOV must prevent mutations, without blocking ordinary full-image NR");
				registry.configuration.settings = { .mode = Mode::PreserveSource, .lightingPreservation = 0.37f };
				const auto beforeRedraw = registry.Snapshot();
				draw();
				require(registry.configuration.settings == beforeRedraw.settings &&
							registry.configuration.revision == beforeRedraw.revision,
					"Route availability and passive redraw must retain nondefault colour preferences");
				for (const float percent : { 0.0f, 50.0f, 100.0f, -10.0f, 110.0f }) {
					ImGui::sliderEditValue = percent;
					const auto priorSlider = registry.Snapshot();
					draw("Lighting preservation");
					require(ImGui::Seen("Lighting preservation") && ImGui::Disabled("Lighting preservation") == blocked,
						"The production slider must follow the same FOV prerequisites in every NR route");
					require(ImGui::lightingSliderFlags == ImGuiSliderFlags_AlwaysClamp,
						"Typed slider input must stay within the percentage range");
					require(registry.configuration.settings.lightingPreservation ==
								(blocked ? priorSlider.settings.lightingPreservation : std::clamp(percent, 0.0f, 100.0f) / 100.0f),
						"Every NR route must write the shared lighting-preservation setting");
					require(registry.configuration.revision == priorSlider.revision + (blocked ? 0 : 1),
						"Blocked slider edits must not mutate the colour registry");
				}
			}
		}
	}
	upscaling.settings.foveatedVendorDispatch = true;
	upscaling.settings.neuralRenderingFovOnly = false;
	upscaling.settings.neuralRenderingMode = static_cast<unsigned>(ModeChoice::FullResolution);
	const auto checkInactivePreservation = [&]() {
		const auto prior = registry.Snapshot();
		for (const auto level : { spdlog::level::info, spdlog::level::debug }) {
			state.level = level;
			draw("Lighting preservation");
			require(ImGui::Disabled("Lighting preservation") && registry.configuration.settings == prior.settings &&
						registry.configuration.experiments == prior.experiments && registry.configuration.revision == prior.revision,
				"Inactive slider draws and edits must retain the complete nondefault configuration at every UI level");
		}
	};
	for (const auto inactiveMode : { Mode::LegacyRaw, Mode::Managed }) {
		registry.configuration.settings = { .mode = inactiveMode, .lightingPreservation = 0.37f };
		checkInactivePreservation();
		require(ImGui::Seen("Choose Preserve source to adjust lighting preservation."),
			"Inactive colour modes must explain the disabled slider and retain its value");
	}
	for (const auto zeroControl : { &Settings::detailStrength, &Settings::maximumDetailStops }) {
		registry.configuration.settings = { .mode = Mode::PreserveSource, .lightingPreservation = 0.37f };
		registry.configuration.settings.*zeroControl = 0.0f;
		checkInactivePreservation();
		require(ImGui::Seen("Raise Detail contribution and Maximum detail gain above zero to use lighting preservation."),
			"Zero-strength reconstruction must explain the ineffective slider");
	}
	registry.configuration.settings = { .mode = Mode::PreserveSource, .appearanceMix = 1.0f, .lightingPreservation = 0.37f };
	checkInactivePreservation();
	require(ImGui::Seen("Lower Neural appearance mix below 1 to use lighting preservation."),
		"Full appearance mixing must explain why preservation is bypassed");
	registry.configuration.settings = { .mode = Mode::PreserveSource, .enabled = false, .lightingPreservation = 0.37f };
	checkInactivePreservation();
	require(ImGui::Seen("Enable colour processing to apply lighting preservation. Your settings are retained."),
		"Disabled colour processing must retain and explain the slider");
	state.level = spdlog::level::info;
	for (const bool enabled : { true, false, true }) {
		const auto prior = registry.Snapshot();
		draw("Enable colour processing");
		auto expected = prior.settings;
		expected.enabled = enabled;
		require(registry.configuration.settings == expected && registry.configuration.revision == prior.revision + 1,
			"Switching colour processing off and on must retain all remembered tuning");
		require(ImGui::Disabled("Lighting preservation") == !enabled,
			"The preservation control must follow the effective colour mode immediately");
	}
	registry.configuration.settings.mode = Mode::Managed;
	const auto savedManaged = registry.Snapshot();
	draw();
	require(ImGui::colourPreview == "Managed (experimental)" && !ImGui::Seen("Managed (experimental)") &&
				registry.configuration.settings == savedManaged.settings && registry.configuration.revision == savedManaged.revision,
		"A saved Managed mode must remain visible without passive migration or a normal-menu selection");
	for (const auto* choice : { "Preserve source", "Neural lighting", "Original" }) {
		draw(choice);
		const auto expectedMode = std::string_view(choice) == "Original"        ? Mode::LegacyRaw :
		                          std::string_view(choice) == "Neural lighting" ? Mode::NeuralLighting :
		                                                                          Mode::PreserveSource;
		require(registry.configuration.settings.mode == expectedMode,
			"Users must be able to leave a saved Managed mode without enabling developer mode");
	}
	const auto normalMode = registry.Snapshot();
	draw("Managed (experimental)");
	require(registry.configuration.settings == normalMode.settings && registry.configuration.revision == normalMode.revision,
		"Normal UI must not accept an experimental Managed selection");
	state.level = spdlog::level::debug;
	for (const auto* choice : { "Preserve source", "Neural lighting", "Managed (experimental)", "Original", "Preserve source" }) {
		const auto mode = std::string_view(choice) == "Original"        ? Mode::LegacyRaw :
		                  std::string_view(choice) == "Preserve source" ? Mode::PreserveSource :
		                  std::string_view(choice) == "Neural lighting" ? Mode::NeuralLighting :
		                                                                  Mode::Managed;
		const auto prior = registry.Snapshot();
		draw(choice);
		auto expected = prior.settings;
		expected.mode = mode;
		require(registry.configuration.settings == expected && registry.configuration.revision == prior.revision + 1,
			"Actual colour-mode selections must preserve nondefault lighting preferences");
		require(ImGui::Disabled("Lighting preservation") == (mode != Mode::PreserveSource),
			"Changing colour mode must update slider availability in the same draw");
		if (mode == Mode::NeuralLighting) {
			require(ImGui::Seen("Neural lighting admits the full bounded neural brightness field. Lighting preservation and Neural appearance mix do not apply."),
				"Neural Lighting must explain its fixed full-tone reconstruction");
			require(ImGui::Seen("Detail contribution") && ImGui::Seen("Maximum detail gain (stops)"),
				"Neural Lighting must expose its shared bounded detail controls");
			require(!ImGui::Seen("Neural appearance mix"),
				"Neural Lighting must not expose an inapplicable appearance control");
		}
	}
	state.level = spdlog::level::info;
	registry.configuration.settings.lightingPreservation = 0.3737f;
	const auto beforePassiveDraw = registry.Snapshot();
	draw();
	require(registry.configuration.settings == beforePassiveDraw.settings &&
				registry.configuration.revision == beforePassiveDraw.revision,
		"Whole-percent display must not round a stored fractional percentage during redraw");
	registry.accept = false;
	ImGui::sliderEditValue = 83.0f;
	draw("Lighting preservation");
	require(registry.configuration.settings == beforePassiveDraw.settings &&
				registry.configuration.experiments == beforePassiveDraw.experiments &&
				registry.configuration.revision == beforePassiveDraw.revision,
		"Rejected slider updates must not mutate any live configuration");
	require(ImGui::Seen("Settings changed concurrently; retry after the next UI refresh."),
		"Rejected slider updates must expose their retry path");
	registry.accept = true;
	registry.configuration.settings.detailStrength = 1.6f;
	++registry.configuration.revision;
	const auto beforeRetry = registry.Snapshot();
	draw("Lighting preservation");
	auto expectedRetry = beforeRetry.settings;
	expectedRetry.lightingPreservation = 0.83f;
	require(registry.configuration.settings == expectedRetry && registry.configuration.experiments == beforeRetry.experiments &&
				registry.configuration.revision == beforeRetry.revision + 1,
		"Retry must apply the slider to the refreshed configuration without losing intervening changes");
	upscaling.loaded = false;
	require(!upscaling.IsNeuralRenderingFovConfigurationAvailable(), "Unloaded upscaler cannot supply a shared FOV mask");
	upscaling.loaded = true;
	for (auto method : { Upscaling::UpscaleMethod::kNONE, Upscaling::UpscaleMethod::kTAA }) {
		upscaling.method = method;
		require(!upscaling.IsNeuralRenderingFovConfigurationAvailable(), "Unsupported methods cannot supply an active FOV mask");
	}
	for (auto method : { Upscaling::UpscaleMethod::kFSR, Upscaling::UpscaleMethod::kDLSS }) {
		upscaling.method = method;
		require(upscaling.IsNeuralRenderingFovConfigurationAvailable(), "A configured DLSS or FSR mask must remain available");
	}
	upscaling.settings.foveatedCenterArea = 1.0f;
	require(!upscaling.IsNeuralRenderingFovConfigurationAvailable(), "Full coverage is not an active FOV mask");
	upscaling.settings.periphery_taa_enable = true;
	require(!upscaling.IsNeuralRenderingFovConfigurationAvailable(), "NR readiness requires the centre-only profile even before enabling NR");
	upscaling.settings.foveatedCenterArea = 0.6f;
	require(upscaling.IsNeuralRenderingFovConfigurationAvailable(), "NR readiness must use the saved centre-only mask");
	upscaling.settings.periphery_taa_center_area = 1.0f;
	require(upscaling.IsNeuralRenderingFovConfigurationAvailable(), "The incompatible TAA profile must not block a valid NR centre-only mask");
	globals::game::isVR = false;
	upscaling.settings.periphery_taa_center_area = 0.3f;
	require(!upscaling.IsNeuralRenderingFovConfigurationAvailable(), "Flat runtimes cannot supply the VR shared FOV mask");

	globals::game::isVR = true;
	upscaling.settings = {};
	upscaling.method = Upscaling::UpscaleMethod::kDLSS;
	upscaling.settings.foveatedCenterArea = 1.0f;
	upscaling.settings.periphery_taa_center_area = 0.3f;
	require(!IsFoveatedMaskConfigured(upscaling.settings, upscaling.method, false) &&
				IsFoveatedMaskConfigured(upscaling.settings, upscaling.method, true) &&
				!upscaling.IsNeuralRenderingFovConfigurationAvailable(upscaling.method),
		"FOV status may use the TAA profile, while NR requires its saved centre-only mask");
	upscaling.settings.foveatedCenterArea = 0.6f;
	upscaling.settings.periphery_taa_center_area = 1.0f;
	require(IsFoveatedMaskConfigured(upscaling.settings, upscaling.method, false) &&
				!IsFoveatedMaskConfigured(upscaling.settings, upscaling.method, true) &&
				upscaling.IsNeuralRenderingFovConfigurationAvailable(upscaling.method),
		"The NR prerequisite must not borrow full coverage from the independent TAA profile");
	upscaling.settings.periphery_taa_center_area = 0.3f;
	for (const bool taaProfile : { false, true }) {
		for (const auto unsupported : { Upscaling::UpscaleMethod::kNONE, Upscaling::UpscaleMethod::kTAA })
			require(!IsFoveatedMaskConfigured(upscaling.settings, unsupported, taaProfile), "Neither mask profile admits a nonvendor upscaler");
		upscaling.settings.foveatedVendorDispatch = false;
		require(!IsFoveatedMaskConfigured(upscaling.settings, upscaling.method, taaProfile), "Unchecked FOV cannot be reported as configured");
		upscaling.settings.foveatedVendorDispatch = true;
		globals::game::isVR = false;
		require(!IsFoveatedMaskConfigured(upscaling.settings, upscaling.method, taaProfile), "Both configured mask profiles remain VR-only");
		globals::game::isVR = true;
	}
	upscaling.loaded = false;
	require(IsFoveatedMaskConfigured(upscaling.settings, upscaling.method, false) &&
				!upscaling.IsNeuralRenderingFovConfigurationAvailable(upscaling.method) && !upscaling.IsNeuralRenderingFovConfigurationAvailable(),
		"Saved mask geometry remains configured while an unloaded upscaler blocks both NR prerequisite overloads");
	upscaling.loaded = true;

	// Main/loading menus can temporarily mask the selected vendor with NONE or TAA.
	for (const auto configured : { Upscaling::UpscaleMethod::kDLSS, Upscaling::UpscaleMethod::kFSR }) {
		for (const auto effective : { Upscaling::UpscaleMethod::kNONE, Upscaling::UpscaleMethod::kTAA }) {
			for (const auto mode : { ModeChoice::FullResolution, ModeChoice::Foveated, ModeChoice::ReducedResolution }) {
				globals::game::isVR = true;
				upscaling.settings = {};
				upscaling.method = configured;
				upscaling.runtimeMethod = effective;
				upscaling.settings.foveatedCenterArea = 0.6f;
				upscaling.settings.neuralRenderingMode = static_cast<unsigned>(mode);
				upscaling.settings.neuralRenderingFovOnly = true;
				upscaling.settings.neuralRenderingRenderscaleFov = true;
				upscaling.settings.neuralRenderingEnabled = true;
				require(upscaling.IsNeuralRenderingFovConfigurationAvailable(configured) &&
							!upscaling.IsNeuralRenderingFovConfigurationAvailable() && !upscaling.IsNeuralRenderingRequested(),
					"Configured FOV remains editable while effective runtime admission waits for the vendor");
				ImGui::Clear("Characters only");
				upscaling.DrawSelectionControls();
				require(ImGui::Seen("FOV is configured; this NR route waits until runtime upscaling is available."),
					"Pending runtime admission must not be reported as missing FOV configuration");
				require(!ImGui::Disabled("Foveated") && !ImGui::Disabled("Characters only") &&
							upscaling.settings.neuralCharacterRenderingEnabled,
					"A temporarily masked vendor must not disable configured FOV or character selection");
				for (const auto* category : { "Faces", "Skin", "Hair" })
					require(ImGui::Seen(category) && !ImGui::Disabled(category), "Pending FOV must retain editable character categories");
				if (mode == ModeChoice::FullResolution)
					require(!ImGui::Disabled("Restrict to FOV mask"), "Configured FOV restriction remains editable in main/loading menus");
				registry.configuration = {};
				registry.configuration.settings.mode = Mode::PreserveSource;
				registry.configuration.settings.lightingPreservation = 0.25f;
				ImGui::sliderEditValue = 61.0f;
				draw("Lighting preservation");
				require(upscaling.lastDrawMethod == configured && !ImGui::Disabled("Enable colour processing") &&
							!ImGui::Disabled("Colour mode") && !ImGui::Disabled("Lighting preservation") &&
							registry.configuration.settings.lightingPreservation == 0.61f,
					"Actual NR feature UI must use configured readiness for pending FOV colour edits");
				feature.DrawEssentialSettings();
				require(upscaling.lastDrawMethod == configured, "Essential NR UI must use the configured vendor too");
				for (const bool enabled : { false, true }) {
					ImGui::Clear("Enabled");
					upscaling.DrawSelectionControls();
					require(!ImGui::Disabled("Enabled") && upscaling.settings.neuralRenderingEnabled == enabled &&
								!upscaling.IsNeuralRenderingRequested(),
						"Pending FOV must leave the master editable without admitting runtime work");
				}
				upscaling.runtimeMethod = configured;
				require(upscaling.IsNeuralRenderingRequested(), "Restoring the effective vendor must admit the configured FOV request");
				upscaling.runtimeMethod = effective;
				globals::game::isVR = false;
				require(!upscaling.IsNeuralRenderingFovConfigurationAvailable(configured) &&
							upscaling.IsNeuralRenderingRequested() == (mode == ModeChoice::ReducedResolution),
					"Configured vendor readiness cannot expose VR FOV on flat runtimes");
			}
		}
	}
	upscaling.runtimeMethod.reset();
	upscaling.method = Upscaling::UpscaleMethod::kDLSS;
	// Saved off/native settings and an in-flight relatch must never admit pre-DLSS VR NR.
	for (const bool isVR : { false, true }) {
		globals::game::isVR = isVR;
		for (const auto mode : { ModeChoice::FullResolution, ModeChoice::Foveated, ModeChoice::ReducedResolution }) {
			for (const bool requested : { false, true }) {
				for (const bool latched : { false, true }) {
					for (const bool active : { false, true }) {
						upscaling.settings = {};
						upscaling.settings.neuralRenderingEnabled = true;
						upscaling.settings.neuralRenderingMode = static_cast<unsigned>(mode);
						upscaling.renderScaleRequested = requested;
						upscaling.renderScaleLatched = latched;
						upscaling.renderScaleActive = active;
						const bool required = isVR && mode == ModeChoice::ReducedResolution;
						const bool supported = isVR || mode != ModeChoice::Foveated;
						require(upscaling.IsNeuralRenderingRenderScaleRequired() == required, "Only enabled VR renderscale NR locks Render Scale");
						require(upscaling.IsNeuralRenderingRequested() == (supported && (!required || (requested && latched && active))),
							"VR renderscale NR requires requested and physical scaling; Full/Foveated and flat remain independent");
						ImGui::Clear("Enabled");
						upscaling.DrawSelectionControls();
						require(!ImGui::Disabled("Enabled") && !upscaling.IsNeuralRenderingRenderScaleRequired(),
							"Master remains editable and disabling NR releases the dependency");
						upscaling.settings.neuralRenderingEnabled = true;
						upscaling.neuralRenderingFeatureAvailable = false;
						require(!upscaling.IsNeuralRenderingRenderScaleRequired() && !upscaling.IsNeuralRenderingRequested(),
							"Unloaded NR cannot lock Render Scale or run the model");
						upscaling.neuralRenderingFeatureAvailable = true;
					}
				}
			}
		}
	}
}
