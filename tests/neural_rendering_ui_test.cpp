#include "Features/FoveatedCommon.h"
#include "Features/Upscaling/NeuralRendering/CharacterSettings.h"
#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"
#include "Features/Upscaling/NeuralRendering/Runtime.h"

#include <cstdio>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace logger
{
	template <class... Args>
	void warn(Args&&...)
	{}
}
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
	struct MenuStub
	{
		unsigned dirtyChecks = 0;
		void RequestSettingsDirtyCheck() { ++dirtyChecks; }
	} menuInstance;
	MenuStub* menu = &menuInstance;
	namespace features
	{
		struct Upscaling
		{
			static constexpr unsigned kQualityModeMaxIndex = 6, kDLSSPresetF = 4;
			enum class UpscaleMethod
			{
				kNONE,
				kTAA,
				kFSR,
				kDLSS
			};
			unsigned draws = 0;
			unsigned historyResets = 0;
			bool loaded = true;
			struct Settings
			{
				bool neuralRenderingFovOnly = false, neuralRenderingEnabled = false, neuralCharacterRenderingEnabled = false;
				bool neuralRenderingRenderscaleFov = false;
				bool neuralCharacterSceneStrengthsEnabled = false;
				bool neuralCharacterProviderBlending = false;
				bool neuralCharacterFacesEnabled = true, neuralCharacterSkinEnabled = true, neuralCharacterHairEnabled = true;
				bool neuralCharacterArmorEnabled = false, neuralCharacterWeaponsEnabled = false;
				float neuralCharacterFaceStrength = 1.0f, neuralCharacterSkinStrength = 1.0f, neuralCharacterHairStrength = 0.65f;
				float neuralCharacterArmorStrength = 1.0f, neuralCharacterWeaponsStrength = 1.0f;
				bool neuralCharacterHumansEnabled = true, neuralCharacterOtherHumanoidsEnabled = true;
				bool neuralCharacterCreaturesEnabled = true, neuralCharacterAnimalsEnabled = true, neuralCharacterOtherActorsEnabled = true;
				unsigned neuralRenderingMode = 0;
				unsigned neuralRenderingPreset = 1, neuralRenderingStyle = 0;
				float neuralRenderingIntensity = 1.0f, neuralRenderingLocalTone = 1.0f;
				float neuralRenderingLocalStructure = 1.0f, neuralRenderingSkinStructure = 1.0f;
				unsigned neuralCharacterCropMode = 1;
				bool foveatedVendorDispatch = true, periphery_taa_enable = false;
				float periphery_taa_center_area = 0.3f, foveatedCenterArea = 0.3f;
				float foveatedCenterHorizontalScale = 1.0f;
				float foveatedLeftEyeMaskOffsetX = 0.0f, foveatedLeftEyeMaskOffsetY = 0.0f;
				float foveatedRightEyeMaskOffsetX = 0.0f, foveatedRightEyeMaskOffsetY = 0.0f;
			} settings;
			struct AdapterFixture
			{
				bool nvidia = true;
				bool IsNvidiaAdapterDetected() const { return nvidia; }
			} fidelityFX;
			bool IsNeuralRenderingHardwareSupported() const noexcept;
			bool IsNeuralRenderingEnabled() const noexcept;
			bool IsNeuralRenderingUpscalingProfileAllowed(UpscaleMethod, uint32_t, bool) const noexcept;

			bool neuralRenderingFeatureAvailable = true;
			UpscaleMethod method = UpscaleMethod::kDLSS;
			std::optional<UpscaleMethod> runtimeMethod;
			std::optional<UpscaleMethod> pendingMethod;
			UpscaleMethod lastDrawMethod = UpscaleMethod::kNONE;
			NeuralRendering::RenderingMode GetNeuralRenderingMode() const { return NeuralRendering::ClampRenderingMode(settings.neuralRenderingMode); }
			bool IsNeuralRenderingFovConfigurationAvailable() const;
			bool IsNeuralRenderingFovConfigurationAvailable(UpscaleMethod a_upscaleMethod) const;
			bool renderScaleRequested = true, renderScaleLatched = true, renderScaleActive = true;
			bool GetVRRenderScaleModeRequested() const { return renderScaleRequested; }
			bool GetVRRenderScaleModePreference() const { return renderScaleRequested; }
			bool GetVRRenderScalePreferenceForSelection(UpscaleMethod) const { return renderScaleRequested; }
			bool IsVRRenderScaleModeLatched() const { return renderScaleLatched; }
			bool IsVRRenderScaleModeActive() const { return renderScaleActive; }
			bool IsNeuralRenderingRenderScaleRequired() const noexcept;
			const char* GetNeuralRenderingUpscalingProfileBlocker(NeuralRendering::RenderingMode, UpscaleMethod, uint32_t, bool) const noexcept;
			struct VRFpsStabilizerProfile
			{
				UpscaleMethod upscaleMethod = UpscaleMethod::kDLSS;
				unsigned qualityMode = 3;
				bool renderScaleMode = true;
				bool configured = true;
				bool hasUpscaleMethod = true, hasLegacyMethodSelection = false, hasQualityMode = true;
				bool hasRenderScaleMode = true, hasDLSSPreset = true;
				unsigned dlssPreset = 1;
				bool HasAnyUpscalingSetting() const { return configured; }
			};
			struct VRFpsStabilizerConfig
			{
				VRFpsStabilizerProfile interior, exterior;
				bool upscalingSwitchingEnabled = true;
			} stabilizerConfig;
			bool stabilizerSyncActive = false;
			bool IsVRFpsStabilizerSyncActive() const { return stabilizerSyncActive; }
			VRFpsStabilizerConfig GetVRFpsStabilizerSessionConfig() const { return stabilizerConfig; }
			bool IsNeuralRenderingUpscalingAvailable() const noexcept;
			bool IsNeuralRenderingUpscalingAvailable(NeuralRendering::RenderingMode) const noexcept;
			bool IsNeuralRenderingRequested() const noexcept;
			bool IsFoveatedVendorDispatchEnabled(UpscaleMethod) const;
			bool IsActiveUpscalingFoveatedProfileAvailable() const;
			void DrawSelectionControls(bool a_essentialsOnly = true);
			void DrawNeuralRenderingMasterControl(bool a_showDiagnostics);
			bool ToggleNeuralRendering(std::string* a_error = nullptr);
			void DrawNeuralRenderingCropControl(bool);
			bool acceptConfiguration = true;
			nlohmann::json GetNeuralRenderingConfiguration() const
			{
				return { { "neuralRenderingEnabled", settings.neuralRenderingEnabled }, { "neuralRenderingMode", settings.neuralRenderingMode } };
			}
			bool ApplyNeuralRenderingConfiguration(const nlohmann::json& config, std::string& error)
			{
				if (!acceptConfiguration) {
					error = "blocked";
					return false;
				}
				settings.neuralRenderingEnabled = config.at("neuralRenderingEnabled");
				return true;
			}
			void RequestHistoryReset() { ++historyResets; }
			void DrawNeuralRenderingFovWarning(bool) const {}
			static bool IsNeuralRenderingEnabled(const Settings&) noexcept;
			static bool ApplyNeuralRenderingFovConstraint(Settings&) noexcept;
			static bool ApplyNeuralRenderingPreset(Settings&, uint32_t) noexcept;
			UpscaleMethod GetUpscaleMethod() const { return method; }
			UpscaleMethod GetRuntimeUpscaleMethod() const { return runtimeMethod.value_or(method); }
			unsigned runtimeQualityMode = 3;
			unsigned GetRuntimeQualityMode() const { return runtimeQualityMode; }
			std::optional<unsigned> configuredQualityMode;
			unsigned GetEffectiveUpscalingQualityMode() const { return configuredQualityMode.value_or(runtimeQualityMode); }
			struct DesiredProfile
			{
				UpscaleMethod method;
				unsigned qualityMode;
				bool renderScaleModeEnabled, renderScaleModePreference;
			};
			DesiredProfile GetPendingVRRenderScaleDesiredProfile() const
			{
				return { pendingMethod.value_or(method), GetEffectiveUpscalingQualityMode(), renderScaleRequested, renderScaleRequested };
			}
			void DrawNeuralRenderingSettings(UpscaleMethod value, bool = false, const std::function<void()>& drawColourSettings = {})
			{
				++draws;
				lastDrawMethod = value;
				if (drawColourSettings)
					drawColourSettings();
			}
		} upscaling;
	}
}
using Upscaling = globals::features::Upscaling;
using uint = unsigned;
bool IsRenderScaleQualityMode(unsigned quality) { return quality > 0; }
struct StabilizerTarget
{
	Upscaling::UpscaleMethod method;
	unsigned qualityMode;
	bool renderScaleModePreference;
};
StabilizerTarget ResolveVRFpsStabilizerTransitionTarget(const Upscaling&, const Upscaling::VRFpsStabilizerProfile& profile)
{
	return { profile.upscaleMethod, profile.qualityMode, profile.renderScaleMode };
}
float ClampFoveatedCenterScale(float value) { return FoveatedCommon::ClampCenterScale(value); }
float ClampFoveatedCenterHorizontalScale(float value) { return FoveatedCommon::ClampCenterHorizontalScale(value); }
float ClampFoveatedMaskOffsetAdjustment(float value) { return value; }
constexpr int ImGuiSliderFlags_AlwaysClamp = 1;
namespace ImGui
{
	std::vector<std::string> items;
	std::vector<bool> disabledItems;
	std::vector<unsigned> rows;
	std::vector<std::pair<std::string, std::string>> tooltips;
	std::vector<std::pair<std::string, bool>> checkboxValues;
	bool drawingTooltip = false;
	unsigned row = 0;
	bool nextOnSameLine = false;
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
		if (drawingTooltip) {
			tooltips.back().second += label;
			return;
		}
		items.emplace_back(label);
		disabledItems.push_back(disableDepth != 0);
		if (!nextOnSameLine)
			++row;
		rows.push_back(row);
		nextOnSameLine = false;
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
		rows.clear();
		tooltips.clear();
		checkboxValues.clear();
		row = 0;
		nextOnSameLine = false;
		clicked = click;
		treeDepth = comboDepth = 0;
		colourPreview.clear();
	}
	template <class... Args>
	void TextWrapped(const char* label, Args&&...)
	{
		Record(label);
	}
	void TextWrapped(const char* label, const char* value)
	{
		char text[1024]{};
		std::snprintf(text, sizeof(text), label, value);
		Record(text);
	}
	template <class... Args>
	void TextDisabled(const char* label, Args&&...)
	{
		Record(label);
	}
	void TextUnformatted(const char* label) { Record(label); }
	void SeparatorText(const char* label) { Record(label); }
	void Separator() {}
	void Spacing() {}
	void SetNextItemWidth(float) {}
	void SameLine() { nextOnSameLine = true; }
	void PushID(int) {}
	void PushID(const char*) {}
	void PopID() {}
	bool Button(const char* label)
	{
		Record(label);
		return disableDepth == 0 && clicked == label;
	}
	bool Checkbox(const char* label, bool* value)
	{
		checkboxValues.emplace_back(label, *value);
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
	struct HoverTooltipWrapper
	{
		HoverTooltipWrapper()
		{
			ImGui::tooltips.emplace_back(ImGui::items.empty() ? "" : ImGui::items.back(), "");
			ImGui::drawingTooltip = true;
		}
		explicit operator bool() const { return true; }
		~HoverTooltipWrapper() { ImGui::drawingTooltip = false; }
	};
	namespace Text
	{
		void WrappedError(const char* label) { ImGui::Record(label); }
		template <class... Args>
		void WrappedWarning(const char* label, Args... args)
		{
			char text[1024]{};
			std::snprintf(text, sizeof(text), label, args...);
			ImGui::Record(text);
		}
	}
	bool interior = true;
	bool IsInterior() { return interior; }
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

namespace NeuralRendering
{
	bool runtimeInstalled = true;
	bool Runtime::IsInstalled() noexcept { return runtimeInstalled; }
	struct Renderer
	{
		struct Snapshot
		{
			bool quarantined = false, failureLatched = false;
			std::string detail;
		} snapshot;
		bool resetSucceeded = true;
		unsigned resetCalls = 0, snapshotReads = 0;
		static Renderer& Instance()
		{
			static Renderer instance;
			return instance;
		}
		Snapshot GetSnapshot()
		{
			++snapshotReads;
			return snapshot;
		}
		bool Reset()
		{
			++resetCalls;
			if (!resetSucceeded || snapshot.quarantined)
				return false;
			snapshot = {};
			return true;
		}
	};
	struct CharacterRendering
	{
		unsigned resetCalls = 0;
		static CharacterRendering& Instance()
		{
			static CharacterRendering instance;
			return instance;
		}
		void Reset() { ++resetCalls; }
	};
}

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
	void DrawColourSettings();
};
#include "neural_rendering_ui_under_test.h"

int main()
{
	const auto require = [](bool condition, const char* message) {
		if (!condition) {
			std::fprintf(stderr, "NeuralRenderingUI: %s\n", message);
			throw std::runtime_error(message);
		}
	};
	ImGui::Clear();
	DrawNeuralRenderingActorCategories(globals::features::upscaling.settings);
	const auto rowFor = [&](const char* label) {
		const auto item = std::find(ImGui::items.begin(), ImGui::items.end(), label);
		require(item != ImGui::items.end(), "Every actor and material selection must remain available");
		return ImGui::rows[static_cast<std::size_t>(item - ImGui::items.begin())];
	};
	for (const auto* label : { "Humans", "Other humanoids", "Beasts / creatures", "Animals", "Other / unknown" })
		require(rowFor(label) == rowFor("Humans"), "Actor toggles must share one row");
	for (const auto* label : { "Faces", "Skin", "Hair", "Armour / clothing", "Weapons" })
		require(rowFor(label) == rowFor("Faces"), "Material toggles must share one row");
	DrawNeuralRenderingMaterialStrengths(globals::features::upscaling.settings);
	for (const auto* label : { "Humans", "Other humanoids", "Beasts / creatures", "Animals", "Other / unknown",
			 "Faces", "Skin", "Hair", "Armour / clothing", "Weapons", "Face Strength", "Skin Strength", "Hair Strength",
			 "Armour / Clothing Strength", "Weapon Strength" }) {
		const auto tip = std::find_if(ImGui::tooltips.begin(), ImGui::tooltips.end(), [label](const auto& entry) { return entry.first == label; });
		require(tip != ImGui::tooltips.end() && !tip->second.empty(), "Each toggle and strength slider must have its own tooltip");
	}
	for (unsigned mode = 0; mode < 3; ++mode) {
		auto& settings = globals::features::upscaling.settings;
		settings = {};
		settings.neuralRenderingMode = mode;
		ImGui::Clear("Adjust categories in scene NR");
		DrawNeuralRenderingCategoryControls(settings, false);
		require(settings.neuralCharacterSceneStrengthsEnabled && !settings.neuralCharacterRenderingEnabled &&
					!ImGui::Disabled("Humans") && !ImGui::Disabled("Face Strength"),
			"All pipelines must allow ordinary scene category strengths without Actors only");
		const auto category = std::find(ImGui::items.begin(), ImGui::items.end(), "Category strengths");
		const auto actors = std::find(ImGui::items.begin(), ImGui::items.end(), "Actors only");
		require(category < actors, "Shared category choices must precede actor-only coverage");
		ImGui::Clear("Actors only");
		DrawNeuralRenderingCategoryControls(settings, false);
		require(settings.neuralCharacterRenderingEnabled && settings.neuralCharacterSceneStrengthsEnabled,
			"Actor-only selection must retain the ordinary scene preference");
	}
	globals::features::upscaling.settings = {};
	for (unsigned mode = 0; mode < 3; ++mode) {
		for (const bool essentials : { true, false }) {
			auto& upscaling = globals::features::upscaling;
			upscaling.settings = {};
			upscaling.settings.neuralRenderingMode = mode;
			upscaling.settings.neuralCharacterHumansEnabled = false;
			upscaling.settings.neuralCharacterArmorEnabled = true;
			ImGui::Clear();
			upscaling.DrawSelectionControls(essentials);
			for (const char* label : { "Full resolution", "Foveated", "Renderscale NR before DLSS", "Shared image settings",
					 "Preset", "Intensity", "Local Tone", "Local Structure", "Skin Structure", "Style", "Actors only" })
				require(ImGui::Seen(label), "Essentials and Advanced must expose modes, shared image settings and Actors only");
			require(ImGui::Seen("Restrict to FOV mask") == (mode == 0) &&
						ImGui::Seen("Use FOV mask for Renderscale NR") == (mode == 2),
				"Both UI levels must expose the FOV toggle for the selected rendering mode");
			for (const char* label : { "Strength application", "Category strengths", "Humans", "Armour / clothing", "Face Strength", "Weapon Strength" })
				require(ImGui::Seen(label) == !essentials, "Category and compositor tuning must stay in Advanced");
			const auto image = std::find(ImGui::items.begin(), ImGui::items.end(), "Shared image settings");
			const auto actors = std::find(ImGui::items.begin(), ImGui::items.end(), "Actors only");
			require(image < actors, "Shared image settings must precede actor-only selection");
			for (const char* label : { "Preset", "Intensity", "Local Tone", "Local Structure", "Skin Structure", "Style", "Actors only" })
				require(std::any_of(ImGui::tooltips.begin(), ImGui::tooltips.end(),
							[label](const auto& tip) { return tip.first == label && !tip.second.empty(); }),
					"Each essential image control and actor toggle must have its own tooltip");
			require(!upscaling.settings.neuralCharacterHumansEnabled && upscaling.settings.neuralCharacterArmorEnabled,
				"Changing UI levels must preserve hidden actor and material selections");
			ImGui::comboEditValue = 3;
			ImGui::Clear("Preset");
			upscaling.DrawSelectionControls(essentials);
			require(upscaling.settings.neuralRenderingPreset == 3 && upscaling.settings.neuralRenderingIntensity == 0.8f,
				"Shared presets must apply from both UI levels");
			ImGui::sliderEditValue = 0.375f;
			ImGui::Clear("Intensity");
			upscaling.DrawSelectionControls(essentials);
			require(upscaling.settings.neuralRenderingPreset == 0 && upscaling.settings.neuralRenderingIntensity == 0.375f,
				"Shared slider edits must select Custom from both UI levels");
		}
	}
	globals::features::upscaling.settings = {};
	ImGui::comboEditValue = 0;
	ImGui::sliderEditValue = 0.375f;
	for (const auto& [label, selection, strength] : {
			 std::tuple{ "Face Strength", &Upscaling::Settings::neuralCharacterFacesEnabled, &Upscaling::Settings::neuralCharacterFaceStrength },
			 std::tuple{ "Skin Strength", &Upscaling::Settings::neuralCharacterSkinEnabled, &Upscaling::Settings::neuralCharacterSkinStrength },
			 std::tuple{ "Hair Strength", &Upscaling::Settings::neuralCharacterHairEnabled, &Upscaling::Settings::neuralCharacterHairStrength },
			 std::tuple{ "Armour / Clothing Strength", &Upscaling::Settings::neuralCharacterArmorEnabled, &Upscaling::Settings::neuralCharacterArmorStrength },
			 std::tuple{ "Weapon Strength", &Upscaling::Settings::neuralCharacterWeaponsEnabled, &Upscaling::Settings::neuralCharacterWeaponsStrength } }) {
		auto& settings = globals::features::upscaling.settings;
		settings.*strength = 1.0f;
		settings.*selection = false;
		ImGui::Clear(label);
		DrawNeuralRenderingMaterialStrengths(settings);
		require(ImGui::Disabled(label) && settings.*strength == 1.0f,
			"Deselected equipment must retain its strength without allowing edits");
		settings.*selection = true;
		ImGui::Clear(label);
		DrawNeuralRenderingMaterialStrengths(settings);
		require(!ImGui::Disabled(label) && settings.*strength == 0.375f,
			"Selected equipment must expose an independent editable strength");
		require(ImGui::disableDepth == 0, "Material strength disable scopes must be balanced");
		for (const auto value : { -1.0f, 2.0f }) {
			ImGui::sliderEditValue = value;
			ImGui::Clear(label);
			DrawNeuralRenderingMaterialStrengths(settings);
			require(settings.*strength == std::clamp(value, 0.0f, 1.0f),
				"Typed category strengths must remain inside the slider limits");
		}
		ImGui::sliderEditValue = 0.375f;
	}
	globals::features::upscaling.settings = {};
	ImGui::sliderEditValue = 50.0f;
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
	require(!ImGui::Seen("Restore normal colour processing") && !ImGui::Seen("Colour experiments and diagnostics"),
		"Assessment controls stay together in Debug");
	state.level = spdlog::level::debug;
	registry.accept = false;
	draw("Reset assessment overrides");
	require(registry.configuration.experiments == before.experiments, "Rejected reset must retain active settings");
	require(ImGui::Seen("Settings changed concurrently; retry after the next UI refresh."), "Rejected reset must be visible");
	registry.accept = true;
	draw("Reset assessment overrides");
	require(registry.configuration.experiments == Experiments{} && registry.configuration.settings == before.settings,
		"Recovery clears only experiments, preserving colour mode and tuning");
	registry.configuration.experiments.captureFrameEvidence = true;
	registry.configuration.experiments.diagnostics = true;
	state.level = spdlog::level::info;
	draw();
	require(!ImGui::Seen("Restore normal colour processing"), "Capture-only diagnostics do not claim an image override");
	require(!ImGui::Seen("Stop diagnostic captures"), "Capture controls stay in Debug");
	state.level = spdlog::level::debug;
	draw();
	require(ImGui::Seen("Stop diagnostic captures"), "Debug can stop active captures");
	const auto beforeStop = registry.Snapshot();
	draw("Stop diagnostic captures");
	require(!registry.configuration.experiments.captureFrameEvidence && !registry.configuration.experiments.diagnostics &&
				registry.configuration.settings == beforeStop.settings &&
				registry.configuration.experiments.applyModelEdit == beforeStop.experiments.applyModelEdit,
		"Stopping diagnostics preserves image settings and model edits");
	state.level = spdlog::level::info;
	registry.configuration.experiments.profiles[0].domain = Domain::Linear;
	registry.configuration.experiments.profiles[0].transform = Transform::LinearToSRGB;
	draw();
	require(!ImGui::Seen("Restore normal colour processing"), "Calibration recovery stays in Debug");
	registry.configuration.settings.enabled = false;
	draw();
	require(!ImGui::Seen("Restore normal colour processing"), "Disabled processing cannot claim active profile overrides");
	globals::state = nullptr;
	draw();
	require(!ImGui::Seen("Colour experiments and diagnostics"), "Missing state fails closed for diagnostics");
	require(!ImGui::Seen("Managed (experimental)"), "Missing state must not offer experimental colour selection");
	require(globals::features::upscaling.draws > 0, "Feature must retain the main NR controls");

	auto& upscaling = globals::features::upscaling;
	for (const unsigned crop : { 0u, 1u, 2u }) {
		upscaling.settings.neuralCharacterCropMode = crop;
		ImGui::Clear();
		upscaling.DrawNeuralRenderingCropControl(false);
		require(ImGui::colourPreview == (crop == 0 ? "Calibrated (session)" : crop == 1 ? "Cropped (default)" :
																						  "Uncropped"),
			"Info preview must describe the active crop policy without silently changing it");
		require(!ImGui::Seen("Calibrated (session)") && upscaling.settings.neuralCharacterCropMode == crop,
			"Info must not offer the Debug-only calibration choice");
		ImGui::Clear("Cropped (default)");
		upscaling.DrawNeuralRenderingCropControl(false);
		require(upscaling.settings.neuralCharacterCropMode == 1, "Info must allow leaving calibration");
	}
	ImGui::Clear("Calibrated (session)");
	upscaling.DrawNeuralRenderingCropControl(true);
	require(upscaling.settings.neuralCharacterCropMode == 0, "Debug must retain calibration selection");
	auto& renderer = NeuralRendering::Renderer::Instance();
	auto& characterRenderer = NeuralRendering::CharacterRendering::Instance();
	const auto drawMaster = [&](std::string_view click = {}, bool diagnostics = false) {
		const auto reads = renderer.snapshotReads;
		ImGui::Clear(click);
		upscaling.DrawNeuralRenderingMasterControl(diagnostics);
		require(renderer.snapshotReads == reads + 1, "Master control must use one coherent renderer snapshot per redraw");
		require(ImGui::disableDepth == 0 && ImGui::treeDepth == 0 && ImGui::comboDepth == 0,
			"Master control must restore every UI scope");
	};
	NeuralRendering::runtimeInstalled = false;
	require(std::string_view(NeuralRendering::Runtime::kMissingRuntimeNotice) ==
				"Missing DLL: Shaders/Upscaling/Streamline/nvngx_dlssnr.dll",
		"Missing-provider notice must identify the exact game and mod-relative paths");
	upscaling.settings.neuralRenderingEnabled = false;
	drawMaster("Enabled");
	require(ImGui::Disabled("Enabled") && !upscaling.settings.neuralRenderingEnabled &&
				ImGui::Seen(NeuralRendering::Runtime::kMissingRuntimeNotice),
		"Missing runtime must disable NR and name the DLL and installation path");
	upscaling.settings.neuralRenderingEnabled = true;
	upscaling.settings.periphery_taa_enable = true;
	require(!Upscaling::ApplyNeuralRenderingFovConstraint(upscaling.settings) && upscaling.settings.periphery_taa_enable,
		"Missing provider must preserve a saved FOV + TAA preference");
	require(!upscaling.IsNeuralRenderingRequested() && !upscaling.IsNeuralRenderingRenderScaleRequired(),
		"Saved enabled NR cannot render or require render scale without its DLL");
	draw("Enable colour processing");
	require(ImGui::Disabled("Enable colour processing"), "Missing runtime must grey out colour settings");
	NeuralRendering::runtimeInstalled = true;
	upscaling.settings.periphery_taa_enable = false;
	upscaling.settings.neuralRenderingEnabled = false;
	drawMaster();
	require(!ImGui::Disabled("Enabled") && !ImGui::Seen(NeuralRendering::Runtime::kMissingRuntimeNotice),
		"Installed runtime must unlock the feature controls");
	const auto requirePreferences = [&](const Upscaling::Settings& expected) {
		require(upscaling.settings.neuralRenderingFovOnly == expected.neuralRenderingFovOnly &&
					upscaling.settings.neuralRenderingRenderscaleFov == expected.neuralRenderingRenderscaleFov &&
					upscaling.settings.neuralCharacterRenderingEnabled == expected.neuralCharacterRenderingEnabled &&
					upscaling.settings.neuralCharacterFacesEnabled == expected.neuralCharacterFacesEnabled &&
					upscaling.settings.neuralCharacterSkinEnabled == expected.neuralCharacterSkinEnabled &&
					upscaling.settings.neuralCharacterHairEnabled == expected.neuralCharacterHairEnabled &&
					upscaling.settings.neuralCharacterHumansEnabled == expected.neuralCharacterHumansEnabled &&
					upscaling.settings.neuralCharacterOtherHumanoidsEnabled == expected.neuralCharacterOtherHumanoidsEnabled &&
					upscaling.settings.neuralCharacterCreaturesEnabled == expected.neuralCharacterCreaturesEnabled &&
					upscaling.settings.neuralCharacterAnimalsEnabled == expected.neuralCharacterAnimalsEnabled &&
					upscaling.settings.neuralCharacterOtherActorsEnabled == expected.neuralCharacterOtherActorsEnabled &&
					upscaling.settings.neuralCharacterArmorEnabled == expected.neuralCharacterArmorEnabled &&
					upscaling.settings.neuralCharacterWeaponsEnabled == expected.neuralCharacterWeaponsEnabled,
			"Master and runtime recovery controls must retain all character and FOV preferences");
	};
	upscaling.settings = {};
	upscaling.settings.neuralCharacterRenderingEnabled = true;
	upscaling.settings.neuralCharacterFacesEnabled = false;
	upscaling.settings.neuralCharacterHairEnabled = false;
	upscaling.settings.neuralCharacterArmorEnabled = true;
	upscaling.settings.neuralCharacterWeaponsEnabled = true;
	upscaling.settings.neuralRenderingFovOnly = true;
	const auto rememberedPreferences = upscaling.settings;
	for (const bool enabled : { true, false, true, false }) {
		drawMaster("Enabled");
		require(upscaling.settings.neuralRenderingEnabled == enabled && !ImGui::Disabled("Enabled"),
			"Healthy master must remain editable through repeated off/on cycles");
		requirePreferences(rememberedPreferences);
		drawMaster();
		require(upscaling.settings.neuralRenderingEnabled == enabled,
			"Passive redraw must preserve the master selection");
		requirePreferences(rememberedPreferences);
		require(ImGui::Seen("NR is off. Actor and colour preferences are retained.") == !enabled,
			"Disabled master must explain the retained child settings");
	}
	for (const auto& [label, member] : {
			 std::pair{ "Humans", &Upscaling::Settings::neuralCharacterHumansEnabled },
			 std::pair{ "Other humanoids", &Upscaling::Settings::neuralCharacterOtherHumanoidsEnabled },
			 std::pair{ "Beasts / creatures", &Upscaling::Settings::neuralCharacterCreaturesEnabled },
			 std::pair{ "Animals", &Upscaling::Settings::neuralCharacterAnimalsEnabled },
			 std::pair{ "Other / unknown", &Upscaling::Settings::neuralCharacterOtherActorsEnabled },
			 std::pair{ "Faces", &Upscaling::Settings::neuralCharacterFacesEnabled },
			 std::pair{ "Skin", &Upscaling::Settings::neuralCharacterSkinEnabled },
			 std::pair{ "Hair", &Upscaling::Settings::neuralCharacterHairEnabled },
			 std::pair{ "Armour / clothing", &Upscaling::Settings::neuralCharacterArmorEnabled },
			 std::pair{ "Weapons", &Upscaling::Settings::neuralCharacterWeaponsEnabled } }) {
		for (const bool masterEnabled : { false, true }) {
			upscaling.settings.neuralRenderingEnabled = masterEnabled;
			const bool previous = upscaling.settings.*member;
			ImGui::Clear(label);
			upscaling.DrawSelectionControls(false);
			require(!ImGui::Disabled(label) && upscaling.settings.*member == !previous &&
						upscaling.settings.neuralRenderingEnabled == masterEnabled,
				"Actor category preferences must remain editable independently of the healthy master");
			const auto editedPreferences = upscaling.settings;
			drawMaster("Enabled");
			requirePreferences(editedPreferences);
			drawMaster("Enabled");
			requirePreferences(editedPreferences);
			drawMaster();
			require(upscaling.settings.neuralRenderingEnabled == masterEnabled,
				"Editing character categories must not leave a stale master value on redraw");
			requirePreferences(editedPreferences);
			require(ImGui::disableDepth == 0 && ImGui::comboDepth == 0,
				"Category edits and subsequent master cycles must restore UI scopes");
		}
	}
	upscaling.settings.neuralRenderingEnabled = false;
	const auto recoveryPreferences = upscaling.settings;
	renderer.snapshot = { .failureLatched = true, .detail = "Recoverable NR failure" };
	renderer.resetSucceeded = false;
	const auto resetCalls = renderer.resetCalls;
	const auto characterResets = characterRenderer.resetCalls;
	const auto historyResets = upscaling.historyResets;
	for (unsigned redraw = 0; redraw < 2; ++redraw) {
		drawMaster();
		require(ImGui::Seen("Neural Rendering is unavailable. Reset its runtime before enabling it again.") &&
					ImGui::Seen("Reason: Recoverable NR failure") && ImGui::Seen("Reset Neural Rendering Runtime"),
			"Recoverable failures must retain their reason and reset action while NR is off");
		require(!upscaling.settings.neuralRenderingEnabled && ImGui::Disabled("Enabled"),
			"Recoverable failures must not silently change the stored master preference");
		requirePreferences(recoveryPreferences);
	}
	drawMaster("Enabled");
	require(!upscaling.settings.neuralRenderingEnabled && ImGui::Disabled("Enabled"),
		"A failed runtime must reject enabling until explicit recovery succeeds");
	require(renderer.resetCalls == resetCalls && characterRenderer.resetCalls == characterResets && upscaling.historyResets == historyResets,
		"Passive failure redraw must not reset runtime, character resources or history");
	drawMaster("Reset Neural Rendering Runtime");
	require(renderer.resetCalls == resetCalls + 1 && characterRenderer.resetCalls == characterResets && upscaling.historyResets == historyResets,
		"Failed runtime reset must retain character resources and history");
	require(ImGui::Seen("Neural Rendering could not reset safely. Its resources have been retained.") && renderer.snapshot.failureLatched,
		"Rejected reset must visibly preserve the original latched failure");
	requirePreferences(recoveryPreferences);
	renderer.resetSucceeded = true;
	drawMaster("Reset Neural Rendering Runtime");
	require(renderer.resetCalls == resetCalls + 2 && characterRenderer.resetCalls == characterResets + 1 && upscaling.historyResets == historyResets + 1 &&
				!renderer.snapshot.failureLatched && !upscaling.settings.neuralRenderingEnabled,
		"Successful recovery must clear runtime failure and reset character history without enabling NR");
	requirePreferences(recoveryPreferences);
	drawMaster();
	require(!ImGui::Seen("Reset Neural Rendering Runtime") && !ImGui::Seen("Reason: Recoverable NR failure"),
		"Recovered renderer must clear stale failure feedback on the next redraw");
	drawMaster("Enabled");
	require(upscaling.settings.neuralRenderingEnabled && !ImGui::Disabled("Enabled"),
		"Recovered renderer must allow enabling NR again");
	drawMaster("Reset Neural Rendering Runtime", true);
	require(upscaling.settings.neuralRenderingEnabled && characterRenderer.resetCalls == characterResets + 2 && upscaling.historyResets == historyResets + 2,
		"Explicit diagnostic reset must preserve an enabled master and reset its history");
	requirePreferences(recoveryPreferences);
	upscaling.settings.neuralRenderingEnabled = false;
	renderer.snapshot = { .quarantined = true, .failureLatched = true, .detail = "Pending GPU resources" };
	const auto quarantinedResetCalls = renderer.resetCalls;
	for (const bool diagnostics : { false, true, false }) {
		drawMaster("Enabled", diagnostics);
		require(ImGui::Disabled("Enabled") && !upscaling.settings.neuralRenderingEnabled,
			"Quarantine must refuse unsafe re-enabling while NR is off");
		require(ImGui::Seen("Neural Rendering cannot be re-enabled safely in this session. Restart the game to try again.") &&
					ImGui::Seen("Reason: Pending GPU resources") && !ImGui::Seen("Reset Neural Rendering Runtime"),
			"Quarantine must retain its reason without offering an unsafe reset at any UI level");
		requirePreferences(recoveryPreferences);
	}
	require(renderer.resetCalls == quarantinedResetCalls,
		"Quarantined redraw and blocked enabling must not call runtime reset");
	upscaling.settings.neuralRenderingEnabled = true;
	drawMaster("Enabled");
	require(!ImGui::Disabled("Enabled") && !upscaling.settings.neuralRenderingEnabled &&
				ImGui::Seen("Reason: Pending GPU resources"),
		"Master OFF must remain available even when the renderer is quarantined");
	requirePreferences(recoveryPreferences);
	drawMaster();
	require(ImGui::Disabled("Enabled") && !upscaling.settings.neuralRenderingEnabled,
		"After quarantined OFF, passive redraw must preserve OFF and refuse re-enabling");
	renderer.snapshot = {};
	upscaling.settings = {};
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
								ImGui::Clear("Actors only");
								upscaling.DrawSelectionControls();
								require(!ImGui::Disabled("Actors only") && upscaling.settings.neuralCharacterRenderingEnabled,
									"Both flat modes must allow character selection without VR FOV");
								ImGui::Clear("Enabled");
								upscaling.DrawSelectionControls();
								require(upscaling.IsNeuralRenderingRequested(), "Enabled flat character selection must reach the shared mono route");
							}
							if (characters) {
								ImGui::Clear("Actors only");
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
			upscaling.DrawSelectionControls(false);
			require(ImGui::Seen(category) && !ImGui::Disabled(category), "Both flat modes must expose editable Advanced character categories");
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
		const bool dlss = method == Upscaling::UpscaleMethod::kDLSS;
		require(ImGui::Disabled("Renderscale NR before DLSS") == !dlss &&
					upscaling.GetNeuralRenderingMode() == (dlss ? ModeChoice::ReducedResolution : ModeChoice::FullResolution),
			"Flat reduced mode requires DLSS; unavailable choices preserve Full resolution");
		for (const bool enabled : { true, false }) {
			ImGui::Clear("Enabled");
			upscaling.DrawSelectionControls();
			require(!ImGui::Disabled("Enabled") && upscaling.settings.neuralRenderingEnabled == enabled,
				"Full resolution remains editable without DLSS; scaled DLSS permits reduced NR");
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
			require(ImGui::Seen("Neural lighting allows lighting changes. Lighting preservation and Neural appearance mix do not apply."),
				"Neural lighting must explain which controls apply in plain language");
			require(ImGui::Seen("Detail contribution") && ImGui::Seen("Maximum detail gain (stops)"),
				"Neural Lighting must expose its shared bounded detail controls");
			require(!ImGui::Seen("Neural appearance mix"),
				"Neural Lighting must not expose an inapplicable appearance control");
		}
	}
	state.level = spdlog::level::info;
	registry.configuration.settings.lightingPreservation = 0.3737f;
	for (const auto& [label, field, maximum] : {
			 std::tuple{ "Detail contribution", &Settings::detailStrength, 2.0f },
			 std::tuple{ "Neural appearance mix", &Settings::appearanceMix, 1.0f },
			 std::tuple{ "Maximum detail gain (stops)", &Settings::maximumDetailStops, 2.0f } }) {
		for (const float value : { -1.0f, 3.0f }) {
			ImGui::sliderEditValue = value;
			draw(label);
			require(registry.configuration.settings.*field == std::clamp(value, 0.0f, maximum),
				"Typed colour controls must stay valid and accept the bounded edit");
		}
	}
	registry.configuration.settings = { .mode = Mode::PreserveSource, .lightingPreservation = 0.3737f };
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
				ImGui::Clear("Actors only");
				upscaling.DrawSelectionControls();
				require(ImGui::Seen("FOV is configured; this NR route waits until runtime upscaling is available."),
					"Pending runtime admission must not be reported as missing FOV configuration");
				require(ImGui::Disabled("Foveated") == (configured != Upscaling::UpscaleMethod::kDLSS) && !ImGui::Disabled("Actors only") &&
							upscaling.settings.neuralCharacterRenderingEnabled,
					"A temporarily masked vendor must not disable configured FOV or character selection");
				ImGui::Clear();
				upscaling.DrawSelectionControls(false);
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
					const bool unavailable = mode == ModeChoice::ReducedResolution || (mode == ModeChoice::Foveated && configured == Upscaling::UpscaleMethod::kFSR);
					require(ImGui::Disabled("Enabled") == unavailable && upscaling.settings.neuralRenderingEnabled == (unavailable || enabled) &&
								!upscaling.IsNeuralRenderingRequested(),
						"Full/Foveated remain editable; reduced NR waits for actual scaled DLSS without admitting work");
				}
				upscaling.runtimeMethod = configured;
				require(upscaling.IsNeuralRenderingRequested() == (configured == Upscaling::UpscaleMethod::kDLSS || mode == ModeChoice::FullResolution),
					"Full resolution resumes with either vendor; Foveated and Reduced require DLSS");
				upscaling.runtimeMethod = effective;
				globals::game::isVR = false;
				require(!upscaling.IsNeuralRenderingFovConfigurationAvailable(configured) &&
							upscaling.IsNeuralRenderingRequested() == (mode == ModeChoice::ReducedResolution && configured == Upscaling::UpscaleMethod::kDLSS && effective == Upscaling::UpscaleMethod::kDLSS),
					"Configured vendor readiness cannot expose VR FOV on flat runtimes");
			}
		}
	}
	upscaling.runtimeMethod.reset();
	upscaling.method = Upscaling::UpscaleMethod::kDLSS;
	for (const bool isVR : { false, true }) {
		globals::game::isVR = isVR;
		upscaling.runtimeQualityMode = 3;
		upscaling.renderScaleRequested = upscaling.renderScaleLatched = upscaling.renderScaleActive = true;
		for (const auto pending : { Upscaling::UpscaleMethod::kNONE, Upscaling::UpscaleMethod::kTAA, Upscaling::UpscaleMethod::kFSR }) {
			upscaling.pendingMethod = pending;
			for (const auto mode : { ModeChoice::Foveated, ModeChoice::ReducedResolution }) {
				upscaling.settings = {};
				upscaling.settings.neuralRenderingMode = static_cast<unsigned>(mode);
				require(!upscaling.IsNeuralRenderingUpscalingAvailable() && !upscaling.ToggleNeuralRendering() &&
							!upscaling.settings.neuralRenderingEnabled,
					"Pending incompatible methods must block activation even while the old DLSS profile still renders");
				upscaling.settings.neuralRenderingEnabled = true;
				require(!upscaling.IsNeuralRenderingRequested(), "A saved NR request cannot race a queued incompatible profile");
				ImGui::Clear();
				upscaling.DrawSelectionControls();
				require(ImGui::Disabled("Enabled") && !ImGui::Disabled("Full resolution"),
					"Pending incompatibility greys activation without preventing Full resolution selection");
				if (pending == Upscaling::UpscaleMethod::kFSR)
					require(ImGui::Seen(mode == ModeChoice::ReducedResolution ? "Renderscale NR paused: FSR." : "NR paused: FSR."),
						"The warning must name the pending incompatible setting, not the previous DLSS profile");
			}
		}
		upscaling.pendingMethod.reset();
		upscaling.settings = {};
		upscaling.settings.neuralRenderingMode = 2;
		upscaling.configuredQualityMode = 0;
		require(!upscaling.ToggleNeuralRendering(), "Pending DLAA must block Renderscale NR while scaled DLSS still renders");
		upscaling.configuredQualityMode.reset();
		require(upscaling.ToggleNeuralRendering(), "Cancelling the incompatible profile restores NR activation");
	}
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
						const bool required = mode == ModeChoice::ReducedResolution;
						const bool physicalRequired = isVR && required;
						const bool supported = isVR || mode != ModeChoice::Foveated;
						require(upscaling.IsNeuralRenderingRenderScaleRequired() == required, "Enabled Renderscale NR requires compatible scaled DLSS profiles");
						require(upscaling.IsNeuralRenderingRequested() == (supported && (!physicalRequired || (requested && latched && active))),
							"VR renderscale NR requires requested and physical scaling; Full/Foveated and flat remain independent");
						ImGui::Clear("Enabled");
						upscaling.DrawSelectionControls();
						const bool unavailable = physicalRequired && !(requested && latched && active);
						require(ImGui::Disabled("Enabled") == unavailable &&
									upscaling.settings.neuralRenderingEnabled == unavailable,
							"Unavailable reduced NR cannot be enabled; redraw preserves the saved preference");
						upscaling.settings.neuralRenderingEnabled = true;
						upscaling.neuralRenderingFeatureAvailable = false;
						require(!upscaling.IsNeuralRenderingRenderScaleRequired() && !upscaling.IsNeuralRenderingRequested(),
							"Unloaded NR cannot require Render Scale or run the model");
						upscaling.neuralRenderingFeatureAvailable = true;
					}
				}
			}
		}
	}
	upscaling.settings.neuralRenderingEnabled = false;
	upscaling.settings.neuralRenderingMode = 2;
	upscaling.renderScaleRequested = upscaling.renderScaleLatched = upscaling.renderScaleActive = true;
	for (const bool isVR : { false, true }) {
		globals::game::isVR = isVR;
		for (const auto method : { Upscaling::UpscaleMethod::kNONE, Upscaling::UpscaleMethod::kTAA,
				 Upscaling::UpscaleMethod::kFSR, Upscaling::UpscaleMethod::kDLSS }) {
			upscaling.method = method;
			for (const unsigned quality : { 0u, 1u, 3u, 6u }) {
				upscaling.runtimeQualityMode = quality;
				const bool available = method == Upscaling::UpscaleMethod::kDLSS && quality != 0;
				for (const bool fov : { false, true }) {
					upscaling.settings = {};
					upscaling.settings.neuralRenderingMode = 2;
					upscaling.settings.neuralRenderingRenderscaleFov = fov;
					upscaling.settings.foveatedVendorDispatch = false;
					ImGui::Clear("Enabled");
					upscaling.DrawSelectionControls();
					require(ImGui::Disabled("Enabled") == !available && upscaling.settings.neuralRenderingEnabled == available,
						"Reduced NR requires scaled DLSS with either FOV preference");
					upscaling.settings.neuralRenderingEnabled = false;
					require(upscaling.ToggleNeuralRendering() == available && upscaling.settings.neuralRenderingEnabled == available,
						"Controller toggles obey the reduced NR prerequisite");
					upscaling.settings.neuralRenderingEnabled = true;
					ImGui::Clear();
					upscaling.DrawSelectionControls();
					const auto master = std::find_if(ImGui::checkboxValues.begin(), ImGui::checkboxValues.end(),
						[](const auto& value) { return value.first == "Enabled"; });
					require(master != ImGui::checkboxValues.end() && master->second == available,
						"A saved incompatible request must never appear active in the master checkbox");
					if (!available) {
						ImGui::Clear("Turn off NR");
						upscaling.DrawSelectionControls();
						require(!upscaling.settings.neuralRenderingEnabled && !ImGui::Disabled("Turn off NR"),
							"A saved inactive NR request must remain removable without changing upscaling");
						upscaling.settings.neuralRenderingEnabled = true;
					}
					require(upscaling.ToggleNeuralRendering() && !upscaling.settings.neuralRenderingEnabled,
						"Controller toggles can always remove a saved incompatible request");
					for (const auto mode : { ModeChoice::FullResolution, ModeChoice::Foveated }) {
						require(upscaling.IsNeuralRenderingUpscalingAvailable(mode) ==
									(mode == ModeChoice::FullResolution || method == Upscaling::UpscaleMethod::kDLSS),
							"Full resolution supports ordinary upscaling; Foveated NR requires DLSS");
					}
					ImGui::Clear("Full resolution");
					upscaling.DrawSelectionControls();
					require(!ImGui::Disabled("Full resolution") && upscaling.GetNeuralRenderingMode() == ModeChoice::FullResolution,
						"A saved incompatible reduced mode always permits Full resolution");
					ImGui::Clear("Renderscale NR before DLSS");
					upscaling.DrawSelectionControls();
					require(ImGui::Disabled("Renderscale NR before DLSS") == !available &&
								upscaling.GetNeuralRenderingMode() == (available ? ModeChoice::ReducedResolution : ModeChoice::FullResolution),
						"The mode list must reject reduced NR with native AA, TAA, FSR or None");
				}
			}
		}
	}
	globals::game::isVR = true;
	upscaling.method = Upscaling::UpscaleMethod::kDLSS;
	upscaling.runtimeQualityMode = 0;
	upscaling.renderScaleRequested = upscaling.renderScaleLatched = upscaling.renderScaleActive = false;
	for (const auto mode : { ModeChoice::FullResolution, ModeChoice::Foveated }) {
		upscaling.settings = {};
		upscaling.settings.neuralRenderingMode = static_cast<unsigned>(mode);
		upscaling.settings.neuralRenderingEnabled = true;
		require(upscaling.IsNeuralRenderingRequested(), "Full resolution and Foveated must still run with DLAA and Render Scale off");
		ImGui::Clear();
		upscaling.DrawSelectionControls();
		require(!ImGui::Disabled("Enabled") && !ImGui::Disabled("Foveated") && !ImGui::Seen("Turn off NR"),
			"Full resolution and Foveated keep their normal master and mode controls");
	}
	upscaling.renderScaleRequested = upscaling.renderScaleLatched = upscaling.renderScaleActive = true;
	upscaling.runtimeQualityMode = 3;
	upscaling.settings = {};
	upscaling.settings.neuralRenderingMode = 2;
	for (const bool fov : { false, true }) {
		upscaling.settings.neuralRenderingEnabled = true;
		upscaling.settings.neuralRenderingRenderscaleFov = fov;
		upscaling.settings.neuralCharacterFaceStrength = 0.42f;
		for (const auto& [method, quality, scaled] : {
				 std::tuple{ Upscaling::UpscaleMethod::kDLSS, 3u, true },
				 std::tuple{ Upscaling::UpscaleMethod::kDLSS, 0u, false },
				 std::tuple{ Upscaling::UpscaleMethod::kTAA, 0u, false },
				 std::tuple{ Upscaling::UpscaleMethod::kNONE, 0u, false },
				 std::tuple{ Upscaling::UpscaleMethod::kDLSS, 3u, false },
				 std::tuple{ Upscaling::UpscaleMethod::kDLSS, 3u, true } }) {
			upscaling.method = method;
			upscaling.runtimeQualityMode = quality;
			upscaling.renderScaleRequested = upscaling.renderScaleLatched = upscaling.renderScaleActive = scaled;
			require(upscaling.IsNeuralRenderingRequested() == scaled,
				"Externally loaded incompatible settings keep NR inactive until compatible scaled DLSS returns");
			ImGui::Clear();
			upscaling.DrawSelectionControls();
			require(upscaling.settings.neuralRenderingEnabled && upscaling.settings.neuralRenderingMode == 2 &&
						upscaling.settings.neuralRenderingRenderscaleFov == fov && upscaling.settings.neuralCharacterFaceStrength == 0.42f,
				"Profile changes must preserve NR preferences and never switch to Full resolution");
		}
	}
	upscaling.settings.neuralRenderingRenderscaleFov = false;
	for (const auto& [method, quality, reason] : {
			 std::tuple{ Upscaling::UpscaleMethod::kDLSS, 0u, "DLAA" },
			 std::tuple{ Upscaling::UpscaleMethod::kFSR, 0u, "Native AA" },
			 std::tuple{ Upscaling::UpscaleMethod::kFSR, 3u, "FSR" },
			 std::tuple{ Upscaling::UpscaleMethod::kTAA, 0u, "TAA" },
			 std::tuple{ Upscaling::UpscaleMethod::kNONE, 0u, "None" },
			 std::tuple{ Upscaling::UpscaleMethod::kDLSS, 3u, "Render Scale off" } }) {
		upscaling.method = method;
		upscaling.runtimeQualityMode = quality;
		upscaling.renderScaleRequested = upscaling.renderScaleLatched = upscaling.renderScaleActive = false;
		ImGui::Clear();
		upscaling.DrawSelectionControls();
		require(ImGui::Seen(std::string("Renderscale NR paused: ") + reason + "."),
			"The NR panel must name the setting that pauses NR");
		upscaling.stabilizerSyncActive = true;
		for (const bool interior : { false, true }) {
			Util::interior = interior;
			auto& profile = interior ? upscaling.stabilizerConfig.interior : upscaling.stabilizerConfig.exterior;
			profile = { method, quality, false };
			ImGui::Clear();
			upscaling.DrawSelectionControls();
			require(ImGui::Seen(std::string("Renderscale NR paused: VR FPS Stabilizer ") +
								(interior ? "Interior" : "Exterior") + " profile, " + reason + "."),
				"A matching active Stabilizer profile must be named with its incompatible setting");
			profile.qualityMode = quality + 1;
			ImGui::Clear();
			upscaling.DrawSelectionControls();
			require(ImGui::Seen(std::string("Renderscale NR paused: ") + reason + "."),
				"Manual settings must not be blamed on a different Stabilizer profile");
		}
		upscaling.stabilizerSyncActive = false;
	}
	upscaling.method = Upscaling::UpscaleMethod::kDLSS;
	upscaling.runtimeQualityMode = 3;
	upscaling.renderScaleRequested = upscaling.renderScaleLatched = true;
	upscaling.renderScaleActive = false;
	upscaling.runtimeMethod = Upscaling::UpscaleMethod::kNONE;
	ImGui::Clear();
	upscaling.DrawSelectionControls();
	require(ImGui::Seen("Renderscale NR is waiting for scaling to become active."),
		"Pending targets must not be mistaken for an incompatible configured profile");
	upscaling.runtimeMethod.reset();
	upscaling.renderScaleActive = true;
	upscaling.stabilizerConfig.interior = { Upscaling::UpscaleMethod::kDLSS, 0u, false };
	upscaling.stabilizerConfig.exterior = { Upscaling::UpscaleMethod::kDLSS, 3u, false };
	ImGui::Clear();
	DrawStabilizerNRWarnings(upscaling.stabilizerConfig, false);
	require(ImGui::Seen("Interior profile is incompatible with Renderscale NR: DLAA.") &&
				ImGui::Seen("Exterior profile is incompatible with Renderscale NR: Render Scale off.") &&
				upscaling.stabilizerConfig.interior.qualityMode == 0 && !upscaling.stabilizerConfig.exterior.renderScaleMode,
		"Stabilizer warnings must name both incompatible profiles without changing the user's selection");
	for (const bool enabled : { false, true }) {
		upscaling.settings.neuralRenderingEnabled = enabled;
		for (const unsigned mode : { 0u, 1u, 2u }) {
			upscaling.settings.neuralRenderingMode = mode;
			for (const bool switching : { false, true }) {
				upscaling.stabilizerConfig.upscalingSwitchingEnabled = switching;
				for (const bool unconfigured : { false, true }) {
					ImGui::Clear();
					DrawStabilizerNRWarnings(upscaling.stabilizerConfig, unconfigured);
					require(ImGui::Seen("Interior profile is incompatible with Renderscale NR: DLAA.") ==
								(enabled && mode == 2 && switching && !unconfigured),
						"Stabilizer warnings apply only to enabled Renderscale NR and active profile switching");
				}
			}
		}
	}
	upscaling.stabilizerConfig = {};
	upscaling.settings.foveatedVendorDispatch = false;
	ImGui::Clear();
	DrawStabilizerNRWarnings(upscaling.stabilizerConfig, false);
	require(ImGui::items.empty(), "Compatible Stabilizer profiles must not show an incompatibility warning");
	for (const bool enabled : { false, true }) {
		upscaling.settings.neuralRenderingEnabled = enabled;
		for (const unsigned mode : { 0u, 1u, 2u }) {
			upscaling.settings.neuralRenderingMode = mode;
			for (const auto method : { Upscaling::UpscaleMethod::kNONE, Upscaling::UpscaleMethod::kTAA,
					 Upscaling::UpscaleMethod::kFSR, Upscaling::UpscaleMethod::kDLSS }) {
				for (const unsigned quality : { 0u, 1u, 3u, 6u }) {
					for (const bool renderScale : { false, true }) {
						const bool compatible = mode == 0 ||
						                        (method == Upscaling::UpscaleMethod::kDLSS && (mode == 1 || (quality != 0 && renderScale)));
						require(upscaling.IsNeuralRenderingUpscalingProfileAllowed(method, quality, renderScale) == (!enabled || compatible),
							"Enabled NR blocks incompatible profiles; disabled NR preserves all normal upscaling choices");
					}
				}
			}
			upscaling.method = Upscaling::UpscaleMethod::kDLSS;
			upscaling.runtimeQualityMode = 3;
			upscaling.renderScaleRequested = true;
			Upscaling::VRFpsStabilizerProfile profile{ Upscaling::UpscaleMethod::kDLSS, 3, true };
			for (const char* method : { "None", "TAA", "AMD FSR", "NVIDIA DLSS" }) {
				ImGui::Clear(method);
				DrawVRFpsStabilizerUpscaleMethod(profile);
				const bool blocked = enabled && mode != 0 && std::string_view(method) != "NVIDIA DLSS";
				require(ImGui::Disabled(method) == blocked, "Stabilizer disables each incompatible method choice");
				if (blocked)
					require(profile.upscaleMethod == Upscaling::UpscaleMethod::kDLSS, "A disabled profile method cannot mutate the saved selection");
				profile.upscaleMethod = Upscaling::UpscaleMethod::kDLSS;
			}
			ImGui::Clear("DLAA");
			DrawVRFpsStabilizerUpscalePreset(profile);
			require(ImGui::Disabled("DLAA") == (enabled && mode == 2) && profile.qualityMode == (enabled && mode == 2 ? 3u : 0u),
				"Stabilizer forbids DLAA only while Renderscale NR is enabled");
			profile.qualityMode = 3;
			ImGui::Clear("Enable##RenderScale");
			DrawVRFpsStabilizerRenderScale(profile);
			require(ImGui::Disabled("Enable##RenderScale") == (enabled && mode == 2) && profile.renderScaleMode == (enabled && mode == 2),
				"Stabilizer cannot turn Render Scale off while Renderscale NR is enabled");
			ImGui::Clear("##DLSSProfile");
			DrawVRFpsStabilizerDLSSProfile(profile);
			require(!ImGui::Disabled("##DLSSProfile"), "Compatible DLSS appearance profiles remain editable");
			struct MethodChoice
			{
				Upscaling::UpscaleMethod method;
				const char* label;
			};
			const std::array choices{ MethodChoice{ Upscaling::UpscaleMethod::kNONE, "None" },
				MethodChoice{ Upscaling::UpscaleMethod::kTAA, "TAA" }, MethodChoice{ Upscaling::UpscaleMethod::kFSR, "FSR" },
				MethodChoice{ Upscaling::UpscaleMethod::kDLSS, "DLSS" } };
			for (const auto& choice : choices) {
				int selected = 3;
				ImGui::Clear(choice.label);
				DrawUpscalingMethodSelection("Method", selected, choices, upscaling);
				const bool allowed = upscaling.IsNeuralRenderingUpscalingProfileAllowed(choice.method, 3, true);
				require(ImGui::Disabled(choice.label) == !allowed && (allowed || selected == 3),
					"Both upscaling views use the same disabled-choice policy without mutating rejected selections");
			}
		}
	}
	upscaling.method = Upscaling::UpscaleMethod::kFSR;
	upscaling.runtimeQualityMode = 3;
	for (const bool enabled : { false, true }) {
		for (const bool upscalingFov : { false, true }) {
			for (const bool nrFov : { false, true }) {
				upscaling.settings = {};
				upscaling.settings.neuralRenderingEnabled = enabled;
				upscaling.settings.foveatedVendorDispatch = upscalingFov;
				upscaling.settings.neuralRenderingFovOnly = nrFov;
				require(upscaling.IsNeuralRenderingUpscalingAvailable() &&
							upscaling.IsNeuralRenderingRequested() == (enabled && (!nrFov || upscalingFov)),
					"FSR Full resolution NR supports optional configured FOV");
				require(upscaling.IsNeuralRenderingUpscalingProfileAllowed(Upscaling::UpscaleMethod::kFSR, 3, true),
					"FSR profiles retain Full resolution NR with either FOV setting");
				for (const char* mode : { "Foveated", "Renderscale NR before DLSS" }) {
					ImGui::Clear(mode);
					upscaling.DrawSelectionControls();
					require(ImGui::Disabled(mode) && upscaling.settings.neuralRenderingMode == 0,
						"FSR cannot select Foveated or Renderscale NR");
				}
			}
		}
	}
	upscaling.method = Upscaling::UpscaleMethod::kDLSS;
	upscaling.fidelityFX.nvidia = false;
	for (const unsigned mode : { 0u, 1u, 2u }) {
		upscaling.settings = {};
		upscaling.settings.neuralRenderingMode = mode;
		upscaling.settings.neuralRenderingEnabled = true;
		upscaling.settings.periphery_taa_enable = true;
		for (const bool essentials : { false, true }) {
			ImGui::Clear("Intensity");
			upscaling.DrawSelectionControls(essentials);
			for (const char* label : { "Enabled", "Rendering mode", "Preset", "Intensity", "Actors only" })
				require(ImGui::Disabled(label), "Unsupported rendering GPUs grey out all NR controls in both views");
			require(!upscaling.IsNeuralRenderingRequested() && !upscaling.IsNeuralRenderingRenderScaleRequired() &&
						upscaling.settings.neuralRenderingEnabled && upscaling.settings.periphery_taa_enable,
				"Unsupported hardware preserves saved NR and ordinary FOV preferences without dispatching or locking upscaling");
			require(ImGui::Seen(NeuralRendering::Runtime::kUnsupportedHardwareNotice) && ImGui::disableDepth == 0,
				"The hardware requirement is explained and UI scopes remain balanced");
		}
		upscaling.settings.neuralRenderingEnabled = false;
		std::string error;
		require(!upscaling.ToggleNeuralRendering(&error) && error == NeuralRendering::Runtime::kUnsupportedHardwareNotice,
			"Bindings reject activation on unsupported rendering GPUs");
	}
	upscaling.fidelityFX.nvidia = true;
	upscaling.settings = {};
	upscaling.settings.neuralRenderingMode = 2;
	renderer.snapshot = {};
	NeuralRendering::runtimeInstalled = false;
	require(!upscaling.ToggleNeuralRendering() && !upscaling.settings.neuralRenderingEnabled,
		"Bindings cannot enable a missing provider");
	NeuralRendering::runtimeInstalled = true;
	renderer.snapshot.quarantined = true;
	require(!upscaling.ToggleNeuralRendering(), "Bindings cannot enable quarantined NR");
	renderer.snapshot = {};
	upscaling.acceptConfiguration = false;
	require(!upscaling.ToggleNeuralRendering() && !upscaling.settings.neuralRenderingEnabled,
		"Rejected binding transitions preserve the switch");
	upscaling.acceptConfiguration = true;
	const auto dirtyChecks = globals::menuInstance.dirtyChecks;
	require(upscaling.ToggleNeuralRendering() && upscaling.settings.neuralRenderingEnabled,
		"Bindings enable NR through the configuration transition");
	renderer.snapshot.quarantined = true;
	require(upscaling.ToggleNeuralRendering() && !upscaling.settings.neuralRenderingEnabled,
		"Bindings can disable quarantined NR");
	require(upscaling.settings.neuralRenderingMode == 2 && globals::menuInstance.dirtyChecks == dirtyChecks + 2,
		"Bindings retain the chosen route and mark successful edits dirty");
}
