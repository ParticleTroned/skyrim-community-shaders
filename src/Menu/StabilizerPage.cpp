#include "StabilizerPage.h"
#include "Features/Upscaling.h"
#include "Features/VR/StabilizerIntegration.h"
#include "Globals.h"
#include "SettingsPage.h"
#include "Utils/Game.h"
#include "Utils/UI.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <tuple>

namespace
{
	bool vrFpsStabilizerProfilesDirty = false;
	constexpr std::array<const char*, 4> kVRFpsStabilizerMethodNames{ "None", "TAA", "AMD FSR", "NVIDIA DLSS" };
	constexpr std::array<const char*, 7> kVRFpsStabilizerPresetNames{
		"Native AA",
		"Hoshipa",
		"Ultra Quality",
		"Quality",
		"Balanced",
		"Performance",
		"Ultra Performance"
	};
	constexpr std::array<const char*, 5> kVRFpsStabilizerDLSSProfileNames{ "J", "K", "L", "M", "F" };

	struct VRFpsStabilizerUIState
	{
		bool initialized = false;
		bool dirty = false;
		bool loadFailed = false;
		bool messageIsError = false;
		bool profilesDefinedInIni = false;
		std::string message;
		VRFpsStabilizer::IniDocument document;
		uint64_t revision = 0;
		Upscaling::VRFpsStabilizerConfig config;
		Upscaling::VRFpsStabilizerConfig baselineConfig;
	};

	bool HasVRFpsStabilizerProfileRows(const Upscaling::VRFpsStabilizerConfig& config)
	{
		return config.HasAnyProfile() ||
		       config.interior.invalidSettingCount > 0 ||
		       config.exterior.invalidSettingCount > 0;
	}

	bool MatchesVRFpsStabilizerEditableProfile(
		const Upscaling::VRFpsStabilizerProfile& lhs,
		const Upscaling::VRFpsStabilizerProfile& rhs)
	{
		return lhs.upscaleMethod == rhs.upscaleMethod &&
		       lhs.qualityMode == rhs.qualityMode &&
		       lhs.dlssPreset == rhs.dlssPreset &&
		       lhs.renderScaleMode == rhs.renderScaleMode &&
		       lhs.screenSpaceShadowsEnabled == rhs.screenSpaceShadowsEnabled &&
		       lhs.screenSpaceGIEnabled == rhs.screenSpaceGIEnabled &&
		       lhs.volumetricLightingExteriorEnabled == rhs.volumetricLightingExteriorEnabled &&
		       lhs.contactShadowsEnabled == rhs.contactShadowsEnabled;
	}

	bool HasVRFpsStabilizerEditableChanges(const VRFpsStabilizerUIState& state)
	{
		const bool startedNewProfileDefinition =
			!state.profilesDefinedInIni && state.config.HasAnyProfile();
		return startedNewProfileDefinition ||
		       state.config.upscalingSwitchingEnabled != state.baselineConfig.upscalingSwitchingEnabled ||
		       state.config.fadeDuration != state.baselineConfig.fadeDuration ||
		       !MatchesVRFpsStabilizerEditableProfile(state.config.interior, state.baselineConfig.interior) ||
		       !MatchesVRFpsStabilizerEditableProfile(state.config.exterior, state.baselineConfig.exterior);
	}

	void RefreshVRFpsStabilizerUIStateDirty(VRFpsStabilizerUIState& state)
	{
		state.dirty = HasVRFpsStabilizerEditableChanges(state);
		vrFpsStabilizerProfilesDirty = state.dirty;
	}

	void LoadVRFpsStabilizerUIState(VRFpsStabilizerUIState& state)
	{
		state.message.clear();
		state.loadFailed = !VRFpsStabilizer::Load(VRFpsStabilizer::ConfigFile::Main, state.document, state.message) ||
		                   !globals::features::upscaling.LoadVRFpsStabilizerConfig(state.config, state.message);
		state.revision = VRFpsStabilizer::Status().revision;
		state.messageIsError = state.loadFailed;
		state.profilesDefinedInIni = !state.loadFailed && HasVRFpsStabilizerProfileRows(state.config);
		if (!state.profilesDefinedInIni)
			state.config.upscalingSwitchingEnabled = false;
		state.initialized = true;
		state.baselineConfig = state.config;
		RefreshVRFpsStabilizerUIStateDirty(state);
	}

	void HandleVRFpsStabilizerUIEdit(VRFpsStabilizerUIState& state)
	{
		RefreshVRFpsStabilizerUIStateDirty(state);
		state.message.clear();
		state.messageIsError = false;
	}

	bool DrawVRFpsStabilizerUpscaleMethod(Upscaling::VRFpsStabilizerProfile& profile)
	{
		bool changed = false;
		const int method = std::clamp(
			static_cast<int>(profile.upscaleMethod),
			static_cast<int>(Upscaling::UpscaleMethod::kNONE),
			static_cast<int>(Upscaling::UpscaleMethod::kDLSS));
		const bool methodConfigured = profile.hasUpscaleMethod || profile.hasLegacyMethodSelection;
		const char* preview = methodConfigured ? kVRFpsStabilizerMethodNames[method] : "Not configured";
		ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
		if (auto combo = Util::Widgets::ComboBox("##UpscaleMethod", preview)) {
			for (int option = 0; option < static_cast<int>(kVRFpsStabilizerMethodNames.size()); ++option) {
				const bool selected = methodConfigured && option == method;
				auto disabledGuard = Util::DisableGuard(!globals::features::upscaling.IsNeuralRenderingUpscalingProfileAllowed(static_cast<Upscaling::UpscaleMethod>(option), Upscaling::kQualityModeMaxIndex, true));
				if (ImGui::Selectable(kVRFpsStabilizerMethodNames[option], selected)) {
					changed = !selected || profile.hasLegacyMethodSelection;
					profile.upscaleMethod = static_cast<Upscaling::UpscaleMethod>(option);
					profile.hasUpscaleMethod = true;
					profile.hasLegacyMethodSelection = false;
				}
				if (auto tooltip = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted("Selects this profile's upscaling method. Choices incompatible with enabled NR are unavailable.");
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Selects the profile's upscaling method. Renderscale NR requires DLSS; turn NR off or change its rendering mode to use another method.");
		}

		return changed;
	}

	bool DrawVRFpsStabilizerUpscalePreset(Upscaling::VRFpsStabilizerProfile& profile)
	{
		if (!profile.hasUpscaleMethod && !profile.hasLegacyMethodSelection) {
			ImGui::TextDisabled("Not configured");
			return false;
		}

		bool changed = false;
		const bool vendorUpscaling =
			profile.upscaleMethod == Upscaling::UpscaleMethod::kFSR ||
			profile.upscaleMethod == Upscaling::UpscaleMethod::kDLSS;
		const char* nativePresetName = profile.upscaleMethod == Upscaling::UpscaleMethod::kDLSS ? "DLAA" : "Native AA";
		auto presetNames = kVRFpsStabilizerPresetNames;
		presetNames[0] = nativePresetName;
		int qualityMode = static_cast<int>(std::min(profile.qualityMode, Upscaling::kQualityModeMaxIndex));
		{
			auto disabledGuard = Util::DisableGuard(!vendorUpscaling);
			ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
			auto combo = Util::Widgets::ComboBox("##UpscalePreset", presetNames[qualityMode]);
			const bool open = static_cast<bool>(combo);
			if (!open) {
				if (auto tooltip = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted("Chooses image quality and performance. Renderscale NR requires a below-native DLSS preset.");
			}
			if (open) {
				for (int option = 0; option < static_cast<int>(presetNames.size()); ++option) {
					auto optionGuard = Util::DisableGuard(option == 0 && globals::features::upscaling.IsNeuralRenderingRenderScaleRequired());
					if (ImGui::Selectable(presetNames[option], option == qualityMode)) {
						profile.qualityMode = static_cast<uint32_t>(option);
						profile.hasQualityMode = true;
						changed = true;
					}
					if (auto tooltip = Util::HoverTooltipWrapper())
						ImGui::TextUnformatted(option == 0 ? "Uses native resolution. Unavailable with Renderscale NR." : "Uses a smaller render image to improve performance.");
					if (option == qualityMode)
						ImGui::SetItemDefaultFocus();
				}
			}
		}
		return changed;
	}

	bool DrawVRFpsStabilizerDLSSProfile(Upscaling::VRFpsStabilizerProfile& profile)
	{
		if (!profile.hasUpscaleMethod && !profile.hasLegacyMethodSelection) {
			ImGui::TextDisabled("Not configured");
			return false;
		}

		bool changed = false;
		int dlssPreset = static_cast<int>(std::min(profile.dlssPreset, Upscaling::kDLSSPresetF));
		{
			auto disabledGuard = Util::DisableGuard(profile.upscaleMethod != Upscaling::UpscaleMethod::kDLSS);
			ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
			if (Util::Widgets::Combo("##DLSSProfile", &dlssPreset, kVRFpsStabilizerDLSSProfileNames.data(), static_cast<int>(kVRFpsStabilizerDLSSProfileNames.size()))) {
				profile.dlssPreset = static_cast<uint32_t>(dlssPreset);
				profile.hasDLSSPreset = true;
				changed = true;
			}
		}
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Chooses the DLSS image reconstruction preset for this profile.");
		return changed;
	}

	bool DrawVRFpsStabilizerRenderScale(Upscaling::VRFpsStabilizerProfile& profile)
	{
		bool changed = false;
		const bool vendorUpscaling =
			profile.upscaleMethod == Upscaling::UpscaleMethod::kFSR ||
			profile.upscaleMethod == Upscaling::UpscaleMethod::kDLSS;
		const bool renderScaleEligible = vendorUpscaling && profile.qualityMode > 0;
		if (!renderScaleEligible && profile.renderScaleMode) {
			profile.renderScaleMode = false;
			profile.hasRenderScaleMode = true;
			changed = true;
		}
		{
			auto disabledGuard = Util::DisableGuard(!renderScaleEligible ||
													(profile.renderScaleMode && globals::features::upscaling.IsNeuralRenderingRenderScaleRequired()));
			if (Util::Widgets::Checkbox("Enabled##RenderScale", &profile.renderScaleMode)) {
				profile.hasRenderScaleMode = true;
				changed = true;
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses a smaller render image. Renderscale NR requires this on in both profiles. Turn NR off or change its rendering mode to turn this off.");
		}
		return changed;
	}

	bool DrawVRFpsStabilizerFeatureToggle(
		const char* id,
		const char* label,
		bool& enabled,
		bool& hasSetting)
	{
		ImGui::PushID(id);
		const bool changed = Util::Widgets::Checkbox(label, &enabled);
		ImGui::PopID();
		if (changed)
			hasSetting = true;
		return changed;
	}

	bool DrawVRFpsStabilizerNotConfiguredToggle(const char* id, const char* label)
	{
		bool enabled = false;
		auto disabledGuard = Util::DisableGuard(true);
		ImGui::PushID(id);
		Util::Widgets::Checkbox(label, &enabled);
		ImGui::PopID();
		return false;
	}

	bool DrawVRFpsStabilizerNotConfiguredText()
	{
		ImGui::TextDisabled("Not configured");
		return false;
	}

	void SetupVRFpsStabilizerProfileTableColumns(bool currentCellIsInterior)
	{
		const char* interiorHeader = currentCellIsInterior ?
		                                 "Interior Profile (Current Location)###InteriorProfile" :
		                                 "Interior Profile###InteriorProfile";
		const char* exteriorHeader = currentCellIsInterior ?
		                                 "Exterior Profile###ExteriorProfile" :
		                                 "Exterior Profile (Current Location)###ExteriorProfile";
		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.85f);
		ImGui::TableSetupColumn(interiorHeader, ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn(exteriorHeader, ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableHeadersRow();
	}

	void DrawVRFpsStabilizerProfileRowLabel(const char* label)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
	}

	bool DrawVRFpsStabilizerProfileEditors(
		Upscaling::VRFpsStabilizerConfig& config,
		bool currentCellIsInterior,
		bool showNotConfigured)
	{
		bool changed = false;
		constexpr auto tableFlags =
			ImGuiTableFlags_Borders |
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_PadOuterX |
			ImGuiTableFlags_NoSavedSettings;

		const auto drawProfileCells = [&](auto&& drawControl) {
			ImGui::TableSetColumnIndex(1);
			ImGui::PushID("InteriorProfile");
			changed |= drawControl(config.interior);
			ImGui::PopID();
			ImGui::TableSetColumnIndex(2);
			ImGui::PushID("ExteriorProfile");
			changed |= drawControl(config.exterior);
			ImGui::PopID();
		};

		ImGui::SeparatorText("Upscaling");
		if (ImGui::BeginTable("##VRFpsStabilizerUpscalingProfiles", 3, tableFlags)) {
			SetupVRFpsStabilizerProfileTableColumns(currentCellIsInterior);

			DrawVRFpsStabilizerProfileRowLabel("Method");
			if (showNotConfigured)
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredText(); });
			else
				drawProfileCells([](auto& profile) { return DrawVRFpsStabilizerUpscaleMethod(profile); });

			DrawVRFpsStabilizerProfileRowLabel("Upscale Preset");
			if (showNotConfigured)
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredText(); });
			else
				drawProfileCells([](auto& profile) { return DrawVRFpsStabilizerUpscalePreset(profile); });

			DrawVRFpsStabilizerProfileRowLabel("DLSS Profile");
			if (showNotConfigured)
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredText(); });
			else
				drawProfileCells([](auto& profile) { return DrawVRFpsStabilizerDLSSProfile(profile); });

			DrawVRFpsStabilizerProfileRowLabel("Render Scale");
			if (showNotConfigured)
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredToggle("RenderScale", "Enabled"); });
			else
				drawProfileCells([](auto& profile) { return DrawVRFpsStabilizerRenderScale(profile); });

			ImGui::EndTable();
		}

		const auto& upscaling = globals::features::upscaling;
		if (config.upscalingSwitchingEnabled && !showNotConfigured && upscaling.IsNeuralRenderingEnabled()) {
			const auto blocker = [&](const auto& profile) {
				return upscaling.GetNeuralRenderingUpscalingProfileBlocker(upscaling.GetNeuralRenderingMode(), profile.upscaleMethod, profile.qualityMode, profile.renderScaleMode);
			};
			const char* modeName = upscaling.IsNeuralRenderingRenderScaleRequired() ? "Renderscale NR" : "NR";
			const char* interiorBlocker = blocker(config.interior);
			const char* exteriorBlocker = blocker(config.exterior);
			if (interiorBlocker)
				Util::Text::WrappedWarning("Interior profile is incompatible with %s: %s.", modeName, interiorBlocker);
			if (exteriorBlocker)
				Util::Text::WrappedWarning("Exterior profile is incompatible with %s: %s.", modeName, exteriorBlocker);
			if (interiorBlocker || exteriorBlocker)
				ImGui::TextWrapped("These profiles cannot be applied with the selected NR mode. Choose compatible upscaling in both profiles, turn NR off, or change its rendering mode. Renderscale NR requires scaled DLSS with Render Scale.");
		}

		ImGui::Spacing();
		ImGui::SeparatorText("Features");
		if (ImGui::BeginTable("##VRFpsStabilizerFeatureProfiles", 3, tableFlags)) {
			SetupVRFpsStabilizerProfileTableColumns(currentCellIsInterior);

			DrawVRFpsStabilizerProfileRowLabel("Screen Space Shadows");
			if (showNotConfigured) {
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredToggle("ScreenSpaceShadows", "Enabled"); });
			} else {
				drawProfileCells([](auto& profile) {
					return DrawVRFpsStabilizerFeatureToggle(
						"ScreenSpaceShadows",
						"Enabled",
						profile.screenSpaceShadowsEnabled,
						profile.hasScreenSpaceShadows);
				});
			}

			DrawVRFpsStabilizerProfileRowLabel("Screen Space GI");
			if (showNotConfigured) {
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredToggle("ScreenSpaceGI", "Enabled"); });
			} else {
				drawProfileCells([](auto& profile) {
					return DrawVRFpsStabilizerFeatureToggle(
						"ScreenSpaceGI",
						"Enabled",
						profile.screenSpaceGIEnabled,
						profile.hasScreenSpaceGI);
				});
			}

			DrawVRFpsStabilizerProfileRowLabel("Point Light Contact Shadows");
			if (showNotConfigured) {
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredToggle("PointLightContactShadows", "Enabled"); });
			} else {
				drawProfileCells([](auto& profile) {
					return DrawVRFpsStabilizerFeatureToggle(
						"PointLightContactShadows",
						"Enabled",
						profile.contactShadowsEnabled,
						profile.hasContactShadows);
				});
			}

			DrawVRFpsStabilizerProfileRowLabel("Volumetric Lighting");
			ImGui::TableSetColumnIndex(2);
			ImGui::PushID("ExteriorProfile");
			if (showNotConfigured) {
				changed |= DrawVRFpsStabilizerNotConfiguredToggle("VolumetricLighting", "Enable in Exteriors");
			} else {
				changed |= DrawVRFpsStabilizerFeatureToggle(
					"VolumetricLighting",
					"Enable in Exteriors",
					config.exterior.volumetricLightingExteriorEnabled,
					config.exterior.hasVolumetricLightingExterior);
			}
			ImGui::PopID();

			ImGui::EndTable();
		}

		return changed;
	}

	VRFpsStabilizerUIState& ProfileEditor()
	{
		static VRFpsStabilizerUIState state;
		if (!state.initialized || (!state.dirty && state.revision != VRFpsStabilizer::Status().revision))
			LoadVRFpsStabilizerUIState(state);
		return state;
	}

	void DrawVRFpsStabilizerSettings()
	{
		auto& upscaling = globals::features::upscaling;
		auto& uiState = ProfileEditor();

		ImGui::TextUnformatted("Interior and Exterior Profiles");
		ImGui::TextWrapped("Choose the VR FPS Stabilizer settings for each location type.");
		ImGui::Spacing();
		const bool currentCellIsInterior = Util::IsInterior();
		ImGui::TextDisabled(
			"Current location: %s. The matching profile column is marked below.",
			currentCellIsInterior ? "Interior" : "Exterior");
		if (!uiState.config.path.empty())
			ImGui::TextDisabled("INI file: %s", uiState.config.path.string().c_str());

		if (!uiState.loadFailed && !uiState.profilesDefinedInIni) {
			ImGui::Spacing();
			if (uiState.config.upscalingSwitchingEnabled) {
				Util::Text::WrappedWarning(
					"No VR FPS Stabilizer Interior/Exterior profile settings are defined in this INI yet. Choose a Method for both profiles and configure the remaining settings, then use Save INI. Save & Apply reloads the profiles when the live interface is available.");
			} else {
				Util::Text::WrappedWarning(
					"No VR FPS Stabilizer Interior/Exterior profile settings are defined in this INI. Switching remains inactive and no profile values are being applied. Enable switching to begin configuring them.");
			}
		}

		ImGui::Spacing();
		{
			auto disabledGuard = Util::DisableGuard(uiState.loadFailed);
			if (Util::Widgets::Checkbox(
					"Enable VR FPS Stabilizer Interior/Exterior switching",
					&uiState.config.upscalingSwitchingEnabled)) {
				HandleVRFpsStabilizerUIEdit(uiState);
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls switching between the Interior and Exterior profiles shown below.");
			ImGui::TextUnformatted("It does not disable VR FPS Stabilizer or edit its other settings and conditional profiles.");
		}
		if (!uiState.config.upscalingSwitchingEnabled && uiState.profilesDefinedInIni) {
			Util::Text::WrappedWarning(
				"Interior/Exterior switching is off. Saving disables the managed profile group; other VR FPS Stabilizer settings are preserved.");
		}

		if (uiState.loadFailed) {
			ImGui::Spacing();
			Util::Text::WrappedError("%s", uiState.message.c_str());
			ImGui::Spacing();
			if (ImGui::Button("Reload INI"))
				LoadVRFpsStabilizerUIState(uiState);
			return;
		}

		ImGui::Spacing();
		{
			auto disabledGuard = Util::DisableGuard(!uiState.config.upscalingSwitchingEnabled);
			const bool showNotConfigured =
				!uiState.profilesDefinedInIni &&
				!uiState.config.upscalingSwitchingEnabled &&
				!uiState.config.HasAnyProfile();
			if (DrawVRFpsStabilizerProfileEditors(uiState.config, currentCellIsInterior, showNotConfigured))
				HandleVRFpsStabilizerUIEdit(uiState);
		}

		ImGui::Spacing();
		const bool openCompositeBlocksUpscaling = upscaling.IsOpenCompositeUpscalingBlocked();
		const auto& sessionConfig = upscaling.GetVRFpsStabilizerSessionConfig();
		if (!VRFpsStabilizer::IsLoaded()) {
			ImGui::TextDisabled("VR FPS Stabilizer profile sync: Inactive because the plugin is not loaded.");
		} else if (openCompositeBlocksUpscaling) {
			Util::Text::WrappedWarning(
				"VR FPS Stabilizer profile sync: Inactive because Open Composite owns upscaling for this session.");
		} else if (upscaling.IsVRFpsStabilizerSyncActive()) {
			ImGui::TextColored(
				Util::Colors::GetSuccess(),
				"VR FPS Stabilizer profile sync: Active for this session.");
		} else if (!sessionConfig.fileExists) {
			ImGui::TextDisabled("VR FPS Stabilizer profile sync: Inactive; VRFpsStabilizer.ini was not found when profiles were loaded.");
		} else if (!sessionConfig.fileReadable) {
			ImGui::TextDisabled("VR FPS Stabilizer profile sync: Inactive; VRFpsStabilizer.ini was not readable when profiles were loaded.");
		} else if (!sessionConfig.upscalingSwitchingEnabled) {
			ImGui::TextDisabled("VR FPS Stabilizer profile sync: Inactive because Interior/Exterior switching is off in the loaded profiles.");
		} else {
			ImGui::TextDisabled("VR FPS Stabilizer profile sync: Inactive; no Interior or Exterior upscaling profile is configured in the loaded INI.");
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Profile sync activates automatically when Stabilizer is loaded and its INI contains an active Interior or Exterior upscaling profile.");
			ImGui::TextUnformatted("Save & Apply refreshes both Stabilizer and CSX. Older Stabilizer versions require restarting Skyrim VR.");
		}

		const bool completeProfiles = uiState.config.HasCompleteProfiles();
		const uint32_t invalidSettingCount = uiState.config.GetInvalidSettingCount();
		if (uiState.config.hasMixedUpscalingSwitchingActivation) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				"The managed Interior/Exterior profile rows and transition fade contain a mix of active and UI-disabled entries. Active entries take precedence; saving will make the whole group match this toggle.");
		}
		if (invalidSettingCount > 0) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				"%u recognized profile value(s) or combination(s) are invalid or outside the supported range. Safe resolved values are shown; saving will normalize those rows.",
				invalidSettingCount);
		}
		if (!completeProfiles && uiState.profilesDefinedInIni) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				"Missing profile values inherit the current runtime settings. Saving will write complete upscaling and feature settings for both profiles.");
		}
		if (!uiState.config.hasFadeDuration && uiState.profilesDefinedInIni) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				"The Render Scale transition fade duration is missing. CSX owns transition coverage; 0 seconds is recommended and will be written when saved.");
		}

		ImGui::Spacing();
		ImGui::SeparatorText("Render Scale Transition Fade");
		{
			auto disabledGuard = Util::DisableGuard(!uiState.config.upscalingSwitchingEnabled);
			double fadeDuration = uiState.config.fadeDuration;
			if (Util::Widgets::InputDouble("Fade-to-black duration (seconds)", &fadeDuration, 0.25, 1.0, "%.2f")) {
				if (std::isfinite(fadeDuration)) {
					uiState.config.fadeDuration = static_cast<float>(std::clamp(fadeDuration, 0.0, 1000000.0));
					uiState.config.hasFadeDuration = true;
					HandleVRFpsStabilizerUIEdit(uiState);
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("VR FPS Stabilizer's separate timed fade for Interior/Exterior Render Scale profile changes.");
			ImGui::TextUnformatted("Set this to 0 seconds because CSX owns transition coverage and releases Skyrim's loading fade when stereo presentation is coherent.");
		}

		if (!uiState.message.empty()) {
			ImGui::Spacing();
			if (uiState.messageIsError) {
				Util::Text::WrappedError("%s", uiState.message.c_str());
			} else {
				ImGui::TextColored(Util::Colors::GetSuccess(), "%s", uiState.message.c_str());
			}
		}
		Util::Text::WrappedDisabled(
			"This page saves Interior/Exterior profiles and the transition fade. Use the tabs above for performance, LOD, quality levels, locations and commands.");
	}

	MenuUI::SettingsFooter ProfileFooter()
	{
		auto& editor = ProfileEditor();
		const auto runtime = VRFpsStabilizer::Status();
		const bool normalize = !editor.config.HasCompleteSettings() || editor.config.GetInvalidSettingCount() > 0 || editor.config.hasMixedUpscalingSwitchingActivation;
		const bool newProfilesReady =
			(editor.config.interior.hasUpscaleMethod || editor.config.interior.hasLegacyMethodSelection) &&
			(editor.config.exterior.hasUpscaleMethod || editor.config.exterior.hasLegacyMethodSelection);
		const bool canSave = !editor.loadFailed && (editor.profilesDefinedInIni ? (editor.dirty || normalize) : (editor.dirty && newProfilesReady));
		return {
			editor.dirty, editor.messageIsError,
			editor.messageIsError ? "Check profile settings" : editor.dirty ? "Unsaved profiles" :
																			  "Profiles saved",
			editor.message.empty() ? "Profiles are stored in VRFpsStabilizer.ini." : editor.message,
			{ MenuUI::SettingsAction{ runtime.available ? "Save & Apply" : "Save INI", "Save profiles and reload when supported. Older Stabilizer versions need a restart.", canSave && !runtime.pending,
				  [&editor] {
					  if (globals::features::upscaling.SaveVRFpsStabilizerConfig(editor.config, editor.document.original, editor.message))
						  LoadVRFpsStabilizerUIState(editor);
					  else
						  editor.messageIsError = true;
				  },
				  nullptr, "Save" },
				{ editor.dirty ? "Discard edits" : "Read INI", "Read the saved profiles without applying them to the game.", !runtime.pending,
					[&editor] { LoadVRFpsStabilizerUIState(editor); }, editor.dirty ? "Discard unsaved profile changes and read the installed INI?" : nullptr, editor.dirty ? "Discard" : "Read" },
				{ "Apply saved INI", "Reload the saved main INI in Stabilizer and CSX; startup commands still require their event.", !editor.dirty && !editor.loadFailed && runtime.available && !runtime.pending,
					[&editor] { editor.messageIsError = !VRFpsStabilizer::RequestReload(VRFpsStabilizer::ConfigFile::Main, editor.message); }, nullptr, "Apply" } }
		};
	}

	class StabilizerAvailabilityCache
	{
	public:
		const VRFpsStabilizer::Availability& Get(std::chrono::steady_clock::time_point now)
		{
			if (now < nextCheck)
				return state;
			nextCheck = now + std::chrono::seconds(1);
			const auto path = VRFpsStabilizer::ConfigPath();
			std::error_code error;
			const auto type = std::filesystem::status(path, error).type();
			uintmax_t size = 0;
			std::filesystem::file_time_type modified{};
			if (type == std::filesystem::file_type::regular && !error) {
				size = std::filesystem::file_size(path, error);
				if (!error)
					modified = std::filesystem::last_write_time(path, error);
			}
			const Stamp current{ path, REL::Module::IsVR(), VRFpsStabilizer::IsLoaded(), VRFpsStabilizer::Status().revision, type, size, modified, error };
			// Stable files need only metadata checks; failed reads retry for permission recovery.
			if (!stamp || *stamp != current || (!state.iniReadable && state.iniDetected && now >= retryRead)) {
				state = VRFpsStabilizer::InspectAvailability();
				stamp = current;
				retryRead = now + std::chrono::seconds(5);
			}
			return state;
		}

	private:
		using Stamp = std::tuple<std::filesystem::path, bool, bool, uint64_t, std::filesystem::file_type, uintmax_t, std::filesystem::file_time_type, std::error_code>;
		VRFpsStabilizer::Availability state;
		std::optional<Stamp> stamp;
		std::chrono::steady_clock::time_point nextCheck = std::chrono::steady_clock::time_point::min();
		std::chrono::steady_clock::time_point retryRead = std::chrono::steady_clock::time_point::min();
	};

	const VRFpsStabilizer::Availability& Availability()
	{
		static StabilizerAvailabilityCache cache;
		return cache.Get(std::chrono::steady_clock::now());
	}

	std::string editorGroup = "profiles";
	bool blockedDraftNavigation = false;

	bool CanSelectEditor(std::string_view requested)
	{
		if (requested == "overview")
			return true;
		constexpr std::array editors{ "profiles", "targets", "lod", "levels", "locations", "commands" };
		if (std::ranges::find(editors, requested) == editors.end())
			return false;
		const bool sameMainDraft = editorGroup != "profiles" && editorGroup != "locations" && requested != "profiles" && requested != "locations";
		blockedDraftNavigation = requested != editorGroup && !sameMainDraft && MenuUI::StabilizerPage::Get().HasUnsavedChanges();
		if (!blockedDraftNavigation)
			editorGroup = requested;
		return !blockedDraftNavigation;
	}
}

namespace MenuUI
{
	StabilizerPage& StabilizerPage::Get()
	{
		static StabilizerPage page;
		return page;
	}

	std::pair<std::string, std::vector<std::string>> StabilizerPage::GetFeatureSummary()
	{
		return { "Balance frame times with automatic quality and location profiles.", { "Interior and exterior profiles", "Frame-time targets and quality levels", "LOD, grass, location rules and commands" } };
	}

	bool StabilizerPage::IsAvailable() const { return Availability().CanEdit(); }

	Feature::SettingsHeaderStatus StabilizerPage::GetSettingsHeaderStatus() const
	{
		if (!IsAvailable())
			return { "Inactive", true };
		const auto runtime = VRFpsStabilizer::Status();
		return runtime.pending         ? SettingsHeaderStatus{ "Applying", true } :
		       runtime.restartRequired ? SettingsHeaderStatus{ "Restart required", true } :
		                                 SettingsHeaderStatus{};
	}

	void StabilizerPage::DrawSettingsEnabledControl()
	{
		bool available = IsAvailable();
		const auto disabled = Util::DisableGuard(true);
		Util::Widgets::Checkbox("Enabled", &available);
		Util::AddTooltip("Detection status. Enable or disable VR FPS Stabilizer in your mod manager, then restart Skyrim VR.");
	}

	std::string_view StabilizerPage::GetSettingsFooterText() const
	{
		return "Requires VR FPS Stabilizer and its installed INI. Settings are saved separately from CSX.";
	}

	bool StabilizerPage::HasUnsavedChanges() const
	{
		return vrFpsStabilizerProfilesDirty || VRFpsStabilizer::HasUnsavedSettings(VRFpsStabilizer::ConfigFile::Main) || VRFpsStabilizer::HasUnsavedSettings(VRFpsStabilizer::ConfigFile::Locations);
	}

	void StabilizerPage::DrawSettings()
	{
		const bool available = IsAvailable();
		if (!available)
			Util::Text::WrappedWarning("%s", Availability().message.c_str());
		else
			VRFpsStabilizer::DrawStatus();
		if (!HasUnsavedChanges())
			blockedDraftNavigation = false;
		if (blockedDraftNavigation)
			Util::Text::WrappedWarning("Save or discard the current draft before switching to another INI editor.");
		SettingsPage page("VRFpsStabilizer", { { "profiles", "Profiles", "Choose compatible interior and exterior quality profiles.", "Upscaling and CSX feature profiles", true, true, "Choose automatic behavior", nullptr, nullptr, available }, { "targets", "Frame targets", "Choose frame-time targets and automatic quality behavior.", "Frame-time targets and response", true, true, nullptr, nullptr, nullptr, available }, { "lod", "LOD & Grass", "Balance distance and density as quality changes.", "Grass, distant LOD and fade distances", true, true, "Refine scene detail", nullptr, nullptr, available }, { "levels", "Quality Levels", "Configure the settings used by each automatic quality level.", "Level 0 through level 9", true, true, nullptr, nullptr, nullptr, available }, { "locations", "Locations", "Choose quality rules for individual locations.", "Location tiers and interior rules", true, true, "Refine location and event rules", nullptr, nullptr, available }, { "commands", "Commands", "Edit commands that run on game events and conditions.", "Startup, loading and conditional commands", true, true, nullptr, nullptr, nullptr, available } }, "Your Stabilizer setup", "Choose a step, then save its INI changes.", {}, CanSelectEditor);
		if (!available)
			return;
		if (page.Is("profiles"))
			DrawVRFpsStabilizerSettings();
		else if (page.Is("targets"))
			VRFpsStabilizer::DrawSettings("Performance");
		else if (page.Is("lod"))
			VRFpsStabilizer::DrawSettings("LOD & Grass");
		else if (page.Is("levels"))
			VRFpsStabilizer::DrawSettings("Quality Levels");
		else if (page.Is("locations"))
			VRFpsStabilizer::DrawSettings("Locations");
		else if (page.Is("commands"))
			VRFpsStabilizer::DrawSettings("Commands");
	}

	SettingsFooter StabilizerPage::GetSettingsFooter()
	{
		if (!IsAvailable())
			return { HasUnsavedChanges(), true, "Stabilizer inactive", Availability().message,
				{ SettingsAction{ "Save INI", "Requires the installed INI and loaded mod.", false, {}, nullptr, "Save" },
					{ "Read INI", "Requires the installed INI and loaded mod.", false, {}, nullptr, "Read" },
					{ "Apply saved INI", "Requires the installed INI and loaded mod.", false, {}, nullptr, "Apply" } } };
		return editorGroup == "profiles" ? ProfileFooter() : VRFpsStabilizer::GetSettingsFooter(editorGroup == "locations" ? VRFpsStabilizer::ConfigFile::Locations : VRFpsStabilizer::ConfigFile::Main);
	}
}
