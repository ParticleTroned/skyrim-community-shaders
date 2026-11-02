#include "ScreenSpaceGI.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include <cmath>

#include <DirectXTex.h>

#include "../I18n/I18n.h"
#include "Deferred.h"
#include "Globals.h"
#include "Profiler.h"
#include "State.h"
#include "LocationContext.h"
#include "Util.h"

#define I18N_KEY_PREFIX "feature.screen_space_gi."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceGI::Settings,
	Enabled,
	EnableGI,
	EnableExperimentalSpecularGI,
	EnableVanillaSSAO,
	AOInteriorsOnly,
	ILInteriorsOnly,
	EnableAdaptiveSampling,
	ResourceProfile,
	NumSlices,
	NumSteps,
	ResolutionMode,
	MinScreenRadius,
	AORadius,
	GIRadius,
	Thickness,
	DepthFadeRange,
	GISaturation,
	GIDistanceCompensation,
	AOPower,
	GIStrength,
	EnableTemporalDenoiser,
	EnableBlur,
	DepthDisocclusion,
	NormalDisocclusion,
	MaxAccumFrames,
	BlurRadius,
	DistanceNormalisation)

namespace
{
	void ClearScreenSpaceGIProfilerTimers()
	{
		if (globals::profiler) {
			globals::profiler->ClearTimersForFeature("ScreenSpaceGI");
		}
	}

	void ClearScreenSpaceGIProfilerTimersIfDisabled(bool a_wasEnabled, const ScreenSpaceGI::Settings& a_settings)
	{
		if (a_wasEnabled && !a_settings.Enabled) {
			ClearScreenSpaceGIProfilerTimers();
		}
	}

	void SetScreenSpaceGIEnabled(ScreenSpaceGI& a_feature, bool a_enabled)
	{
		a_feature.SetEnabled(a_enabled);
	}

	bool DrawScreenSpaceGIEnabledCheckbox(ScreenSpaceGI& a_feature, const char* a_label)
	{
		bool enabled = a_feature.IsEnabledRequested();
		if (!ImGui::Checkbox(a_label, &enabled))
			return false;

		SetScreenSpaceGIEnabled(a_feature, enabled);
		return true;
	}

	void ApplyAOOnlyPreset(ScreenSpaceGI::Settings& a_settings)
	{
		a_settings.NumSlices = 1;
		a_settings.NumSteps = 6;
		a_settings.ResolutionMode = 0;
		a_settings.EnableBlur = true;
		a_settings.EnableGI = false;
		a_settings.EnableExperimentalSpecularGI = false;
	}

	bool IsAOOnlyPreset(const ScreenSpaceGI::Settings& a_settings)
	{
		return a_settings.NumSlices == 1 &&
		       a_settings.NumSteps == 6 &&
		       a_settings.ResolutionMode == 0 &&
		       a_settings.EnableBlur &&
		       !a_settings.EnableGI &&
		       !a_settings.EnableExperimentalSpecularGI;
	}

	void DrawPresetChoices(ScreenSpaceGI::Settings& a_settings, bool& a_recompileFlag, const char* a_id)
	{
		const auto matches = [&](uint slices, uint steps, int resolution) {
			return a_settings.NumSlices == slices && a_settings.NumSteps == steps &&
			       a_settings.ResolutionMode == resolution && a_settings.EnableBlur &&
			       a_settings.EnableGI && !a_settings.EnableExperimentalSpecularGI;
		};
		const int selected = IsAOOnlyPreset(a_settings) ? 0 :
		                     matches(4, 6, 0)           ? 1 :
		                     matches(8, 10, 0)          ? 2 :
		                     matches(10, 12, 2)         ? 3 :
		                     matches(4, 8, 1)           ? 4 :
		                     matches(4, 8, 0)           ? 5 :
		                                                  6;
		const MenuUI::Choice choices[] = {
			{ "ao", T("feature.screen_space_gi.ao_only", "AO only"), T("feature.screen_space_gi.ui_full_resolution_ambient_shadows", "Full resolution ambient shadows"), T("feature.screen_space_gi.ui_full_resolution_1_slice_and_6_steps_indirect_lighting_is_disabled", "Full resolution, 1 slice and 6 steps; indirect lighting is disabled.") },
			{ "gi", T("feature.screen_space_gi.ui_ao_gi", "AO + GI"), T("feature.screen_space_gi.ui_ambient_shadows_and_bounced_light", "Ambient shadows and bounced light"), T("feature.screen_space_gi.ui_full_resolution_4_slices_and_6_steps", "Full resolution, 4 slices and 6 steps.") },
			{ "reference", T("feature.screen_space_gi.reference", "Reference"), T("feature.screen_space_gi.ui_higher_quality_higher_cost", "Higher quality, higher cost"), T("feature.screen_space_gi.ui_full_resolution_with_gi_and_blur_8_slices_and_10_steps", "Full resolution with GI and blur, 8 slices and 10 steps.") },
			{ "low", T("feature.screen_space_gi.low", "Low"), T("feature.screen_space_gi.ui_reduced_resolution_for_lower_cost", "Reduced resolution for lower cost"), T("feature.screen_space_gi.ui_quarter_resolution_with_gi_and_blur_10_slices_and_12_steps", "Quarter resolution with GI and blur, 10 slices and 12 steps.") },
			{ "standard", T("feature.screen_space_gi.standard", "Standard"), T("feature.screen_space_gi.ui_balance_detail_and_cost", "Balance detail and cost"), T("feature.screen_space_gi.ui_half_resolution_with_gi_and_blur_4_slices_and_8_steps", "Half resolution with GI and blur, 4 slices and 8 steps.") },
			{ "extreme", T("feature.screen_space_gi.extreme", "Extreme"), T("feature.screen_space_gi.ui_full_resolution_with_more_samples", "Full resolution with more samples"), T("feature.screen_space_gi.ui_full_resolution_with_gi_and_blur_4_slices_and_8_steps", "Full resolution with GI and blur, 4 slices and 8 steps.") },
			{ "user", T("feature.screen_space_gi.ui_user", "User"), T("feature.screen_space_gi.ui_start_from_ao_gi_and_customise", "Start from AO + GI and customise"), T("feature.screen_space_gi.ui_copies_ao_gi_custom_settings_remain_unrestricted", "Copies AO + GI. Custom settings remain unrestricted.") }
		};
		const auto disabled = Util::DisableGuard(!a_settings.Enabled);
		const int chosen = MenuUI::ChoiceCards(a_id, selected, choices);
		if (chosen < 0)
			return;
		if (chosen == 0) {
			ApplyAOOnlyPreset(a_settings);
		} else {
			a_settings.NumSlices = chosen == 2 ? 8u : chosen == 3 ? 10u :
			                                                        4u;
			a_settings.NumSteps = chosen == 2 ? 10u : chosen == 3            ? 12u :
			                                      chosen == 4 || chosen == 5 ? 8u :
			                                                                   6u;
			a_settings.ResolutionMode = chosen == 3 ? 2 : chosen == 4 ? 1 :
			                                                            0;
			a_settings.EnableBlur = true;
			a_settings.EnableGI = true;
			a_settings.EnableExperimentalSpecularGI = false;
			a_settings.ResourceProfile = ScreenSpaceGI::kResourceProfileFullGI;
		}
		a_recompileFlag = true;
	}

	bool DrawResolutionModeSelector(const char* a_tableId, int& a_resolutionMode)
	{
		bool clickedResolutionMode = false;
		if (ImGui::BeginTable(a_tableId, 3, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn(T("feature.screen_space_gi.fullres", "FullRes"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn(T("feature.screen_space_gi.halfres", "HalfRes"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn(T("feature.screen_space_gi.quarterres", "QuarterRes"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			clickedResolutionMode |= ImGui::RadioButton(T(TKEY("full_res"), "Full Res"), &a_resolutionMode, 0);
			ImGui::TableNextColumn();
			clickedResolutionMode |= ImGui::RadioButton(T(TKEY("half_res"), "Half Res"), &a_resolutionMode, 1);
			ImGui::TableNextColumn();
			clickedResolutionMode |= ImGui::RadioButton(T(TKEY("quarter_res"), "Quarter Res"), &a_resolutionMode, 2);
			ImGui::EndTable();
		}
		return clickedResolutionMode;
	}

	void DrawQualityPerformanceControls(
		ScreenSpaceGI::Settings& a_settings,
		bool& a_recompileFlag,
		bool a_showAdvanced,
		const char* a_presetsTableId,
		const char* a_resolutionTableId)
	{
		auto qualityGuard = Util::DisableGuard(!a_settings.Enabled);
		a_settings.NumSlices = std::clamp(a_settings.NumSlices, 1u, 10u);
		a_settings.NumSteps = std::clamp(a_settings.NumSteps, 1u, 20u);

		DrawPresetChoices(a_settings, a_recompileFlag, a_presetsTableId);

		if (a_showAdvanced) {
			int numSlices = static_cast<int>(a_settings.NumSlices);
			if (ImGui::SliderInt(T(TKEY("slices"), "Slices"), &numSlices, 1, 10))
				a_settings.NumSlices = static_cast<uint>(std::clamp(numSlices, 1, 10));
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("slices_tooltip"),
									  "How many directions do the samples take.\n"
									  "Controls noise."));

			int numSteps = static_cast<int>(a_settings.NumSteps);
			if (ImGui::SliderInt(T(TKEY("steps_per_slice"), "Steps Per Slice"), &numSteps, 1, 20))
				a_settings.NumSteps = static_cast<uint>(std::clamp(numSteps, 1, 20));
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("steps_per_slice_tooltip"),
									  "How many samples does it take in one direction.\n"
									  "Controls accuracy of lighting, and noise when effect radius is large."));
		}

		a_settings.ResolutionMode = std::clamp(a_settings.ResolutionMode, 0, 2);
		a_recompileFlag |= DrawResolutionModeSelector(a_resolutionTableId, a_settings.ResolutionMode);
	}
}

////////////////////////////////////////////////////////////////////////////////////

void ScreenSpaceGI::RestoreDefaultSettings()
{
	const bool wasEnabled = settings.Enabled;
	runtimeToggle.ReplaceSettings(settings, Settings{}, &Settings::Enabled);
	ClearScreenSpaceGIProfilerTimersIfDisabled(wasEnabled, settings);
	if (!wasEnabled && settings.Enabled)
		QueueHistoryReset();
	recompileFlag = true;
}

void ScreenSpaceGI::DrawSettings()
{
	settings.NumSlices = std::clamp(settings.NumSlices, 1u, 10u);
	settings.NumSteps = std::clamp(settings.NumSteps, 1u, 20u);
	settings.ResourceProfile = std::clamp(settings.ResourceProfile, kResourceProfileFullGI, kResourceProfileAOOnly);
	static bool showAdvanced;

	MenuUI::SettingsPage page("ScreenSpaceGI", {
												   { "preset", T("feature.screen_space_gi.ui_presets", "Presets"), T("feature.screen_space_gi.ui_choose_the_shadow_and_indirect_light_baseline", "Choose the shadow and indirect-light baseline."), T("feature.screen_space_gi.ui_ao_and_indirect_light_baselines", "AO and indirect-light baselines"), true, true, T("feature.screen_space_gi.ui_choose_the_rendering_foundation", "Choose the rendering foundation") },
												   { "coverage", T("feature.screen_space_gi.ui_coverage", "Coverage"), T("feature.screen_space_gi.ui_choose_the_effects_and_area_to_process", "Choose the effects and area to process."), T("feature.screen_space_gi.ui_effects_and_processing_area", "Effects and processing area"), true, true, nullptr },
												   { "quality", T("feature.screen_space_gi.ui_quality", "Quality"), T("feature.screen_space_gi.ui_choose_the_detail_and_processing_budget", "Choose the detail and processing budget."), T("feature.screen_space_gi.ui_resolution_and_sample_budget", "Resolution and sample budget"), true, true, T("feature.screen_space_gi.ui_balance_detail_and_appearance", "Balance detail and appearance") },
												   { "look", T("feature.screen_space_gi.ui_look", "Look"), T("feature.screen_space_gi.ui_refine_shadows_and_lighting", "Refine shadows and lighting."), T("feature.screen_space_gi.ui_shadow_strength_and_lighting", "Shadow strength and lighting"), true, true, nullptr },
												   { "indirect", T("feature.screen_space_gi.ui_indirect_light", "Indirect light"), T("feature.screen_space_gi.ui_refine_bounced_light_colour_and_intensity", "Refine bounced-light colour and intensity."), T("feature.screen_space_gi.ui_bounced_light_colour", "Bounced-light colour"), true, true, T("feature.screen_space_gi.ui_refine_indirect_light", "Refine indirect light"), nullptr, nullptr, settings.EnableGI },
												   { "smoothing", T("feature.screen_space_gi.ui_smoothing", "Smoothing"), T("feature.screen_space_gi.ui_reduce_noise_after_choosing_the_appearance", "Reduce noise after choosing the appearance."), T("feature.screen_space_gi.ui_temporal_and_spatial_smoothing", "Temporal and spatial smoothing"), true, true, nullptr },
												   { "diagnostics", T("feature.screen_space_gi.ui_diagnostics", "Diagnostics"), T("feature.screen_space_gi.ui_inspect_diagnostic_buffer_views", "Inspect diagnostic buffer views."), T("feature.screen_space_gi.ui_diagnostic_buffer_views", "Diagnostic buffer views"), true, false, nullptr },
											   });

	if (!page.Is("overview") && !page.Is("performance") && !page.Is("profiling")) {
		if (!ShadersOK())
			Util::Text::Error("%s", T("feature.screen_space_gi.shader_compile_error", "Compute shaders failed to compile!"));
	}
	if (page.Is("coverage") || page.Is("quality") || page.Is("look") || page.Is("indirect") || page.Is("smoothing"))
		Util::Widgets::Checkbox(T("feature.screen_space_gi.ui_advanced_options", "Advanced options"), &showAdvanced);
	if (page.Is("coverage")) {
		auto guard = Util::DisableGuard(!settings.Enabled);
		Util::Widgets::Checkbox(T("feature.screen_space_gi.vanilla_ssao", "Vanilla SSAO"), &settings.EnableVanillaSSAO);
		Util::AddTooltip(T("feature.screen_space_gi.ui_enable_skyrim_s_built_in_ssao_usually_disabled_with_ss_c22f9e16", "Enable Skyrim's built-in SSAO. Usually disabled with SSGI to avoid double-darkening."));
	}

	///////////////////////////////

	if (page.Is("preset")) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
		ImGui::TextWrapped("%s", T("feature.screen_space_gi.ui_these_presets_keep_ssgi_baselines_close_to_the_regular_46414a07", "These presets keep SSGI baselines close to the regular quality and resource controls."));
		ImGui::PopStyleColor();
	}

	{
		auto presetsAndQualityGuard = Util::DisableGuard(!settings.Enabled);

		if (page.Is("preset"))
			DrawPresetChoices(settings, recompileFlag, "SSGIPreset");
		if (page.Is("coverage")) {
			MenuUI::SectionHeading(T("feature.screen_space_gi.ui_effects_and_coverage", "Effects and coverage"));

			const int previousResourceProfile = settings.ResourceProfile;
			{
				MenuUI::DetailGrid controls("SSGIEffectsResources");
				controls.Next();
				bool enableGI = settings.EnableGI;
				if (Util::Widgets::Checkbox(T("feature.screen_space_gi.ui_enable_indirect_lighting", "Enable indirect lighting"), &enableGI)) {
					if (enableGI) {
						settings.ResourceProfile = kResourceProfileFullGI;
						settings.EnableGI = true;
					} else {
						settings.EnableGI = false;
						settings.EnableExperimentalSpecularGI = false;
					}
					recompileFlag = true;
				}
				Util::AddTooltip(T("feature.screen_space_gi.ui_adds_bounced_light_to_ambient_shadows_turning_it_off_k_bbfe18ee", "Adds bounced light to ambient shadows. Turning it off keeps ambient shadows enabled."));
				controls.Next();
				bool keepGIResources = settings.ResourceProfile == kResourceProfileFullGI;
				if (Util::Widgets::Checkbox(T("feature.screen_space_gi.ui_keep_gi_resources_resident", "Keep GI resources resident"), &keepGIResources))
					settings.ResourceProfile = keepGIResources ? kResourceProfileFullGI : kResourceProfileAOOnly;
				Util::AddTooltip(T("feature.screen_space_gi.ui_keep_gi_buffers_for_runtime_switching_turning_this_off_72e4c669", "Keep GI buffers for runtime switching. Turning this off disables GI and frees buffers when render targets are recreated or the game restarts."));
				controls.Next();
				Util::Widgets::Checkbox(T("feature.screen_space_gi.ui_ao_interiors_only", "AO Interiors Only"), &settings.AOInteriorsOnly);
				Util::AddTooltip(T("feature.screen_space_gi.ui_run_ambient_shadows_only_in_interiors", "Run ambient shadows only in interiors."));
				controls.Next();
				const auto indirectDisabled = Util::DisableGuard(!settings.EnableGI);
				Util::Widgets::Checkbox(T("feature.screen_space_gi.ui_gi_interiors_only", "GI Interiors Only"), &settings.ILInteriorsOnly);
				Util::AddTooltip(T("feature.screen_space_gi.ui_run_indirect_lighting_only_in_interiors", "Run indirect lighting only in interiors."));
			}

			if (showAdvanced) {
				auto hqSpecGuard = Util::DisableGuard(!settings.Enabled || !settings.EnableGI);
				recompileFlag |= Util::Widgets::Checkbox(T("feature.screen_space_gi.hq_specular_il", "(Experimental) HQ Specular IL"), &settings.EnableExperimentalSpecularGI);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(T("feature.screen_space_gi.hq_specular_il_tooltip", "An experimental specular GI that is more accurate but requires more samples. Won't be blurred."));
			}

			settings.ResourceProfile = std::clamp(settings.ResourceProfile, kResourceProfileFullGI, kResourceProfileAOOnly);
			if (settings.ResourceProfile != previousResourceProfile) {
				if (settings.ResourceProfile == kResourceProfileAOOnly) {
					settings.EnableGI = false;
					settings.EnableExperimentalSpecularGI = false;
				}
				recompileFlag = true;
			}
			if (settings.EnableGI && !HasGIResources())
				Util::Text::Warning("%s", T("feature.screen_space_gi.ui_full_gi_resources_are_not_allocated_recreate_render_ta_d91b7f9e", "Full GI resources are not allocated. Recreate render targets or restart to allocate resources and compile GI shaders."));
			if (IsResourceProfileRestartPending()) {
				Util::Text::Warning("%s", T("feature.screen_space_gi.resource_profile_restart", "The resource profile changes when render targets are recreated or the game restarts. The AO-only profile omits indirect-lighting textures and shaders."));
			}
		}
		if (page.Is("quality")) {
			MenuUI::SectionHeading(T("feature.screen_space_gi.ui_resolution_and_samples", "Resolution and samples"));

			if (showAdvanced) {
				int numSlices = static_cast<int>(settings.NumSlices);
				if (Util::Widgets::SliderInt(T("feature.screen_space_gi.slices", "Slices"), &numSlices, 1, 10))
					settings.NumSlices = static_cast<uint>(std::clamp(numSlices, 1, 10));
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(T("feature.screen_space_gi.slices_tooltip", "How many directions do the samples take.\nControls noise."));

				int numSteps = static_cast<int>(settings.NumSteps);
				if (Util::Widgets::SliderInt(T("feature.screen_space_gi.steps_per_slice", "Steps Per Slice"), &numSteps, 1, 20))
					settings.NumSteps = static_cast<uint>(std::clamp(numSteps, 1, 20));
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(T("feature.screen_space_gi.ui_samples_per_direction_controls_lighting_accuracy_and_n_f9603fdd", "Samples per direction. Controls lighting accuracy and noise at large effect radii."));
			}

			recompileFlag |= Util::Widgets::Checkbox(T("feature.screen_space_gi.ui_adaptive_sampling", "Adaptive Sampling"), &settings.EnableAdaptiveSampling);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.screen_space_gi.ui_reduces_ao_sample_count_in_far_distance_and_low_varian_76f0878f", "Reduces AO sample count in far distance and low-variance regions to improve performance."));
			}

			const int previousResolutionMode = settings.ResolutionMode;
			settings.ResolutionMode = std::clamp(settings.ResolutionMode, 0, 2);

			const MenuUI::Choice resolutions[] = {
				{ "full", T("feature.screen_space_gi.ui_full_resolution", "Full resolution"), T("feature.screen_space_gi.ui_maximum_spatial_detail", "Maximum spatial detail"), T("feature.screen_space_gi.ui_processes_every_pixel", "Processes every pixel.") },
				{ "half", T("feature.screen_space_gi.ui_half_resolution", "Half resolution"), T("feature.screen_space_gi.ui_balance_detail_and_cost", "Balance detail and cost"), T("feature.screen_space_gi.ui_processes_a_reduced_resolution_image", "Processes a reduced-resolution image.") },
				{ "quarter", T("feature.screen_space_gi.ui_quarter_resolution", "Quarter resolution"), T("feature.screen_space_gi.ui_lowest_processing_cost", "Lowest processing cost"), T("feature.screen_space_gi.ui_processes_the_smallest_image", "Processes the smallest image.") }
			};
			const int chosen = MenuUI::ChoiceCards("SSGIResolutionMode", settings.ResolutionMode, resolutions);
			if (chosen >= 0) {
				settings.ResolutionMode = chosen;
			}
			recompileFlag |= (settings.ResolutionMode != previousResolutionMode);
		}
	}

	///////////////////////////////
	if (page.Is("look")) {
		MenuUI::SectionHeading(T("feature.screen_space_gi.ui_shadow_appearance", "Shadow appearance"));

		{
			auto visualGuard = Util::DisableGuard(!settings.Enabled);

			Util::Widgets::SliderFloat(T("feature.screen_space_gi.ao_power", "AO Power"), &settings.AOPower, 0.f, 6.f, "%.2f");

			{
				auto ilGuard = Util::DisableGuard(!settings.EnableGI);
				Util::Widgets::SliderFloat(T("feature.screen_space_gi.il_source_brightness", "IL Source Brightness"), &settings.GIStrength, 0.f, 6.f, "%.2f");
			}

			ImGui::Separator();

			Util::Widgets::SliderFloat(T("feature.screen_space_gi.ao_radius", "AO radius"), &settings.AORadius, 10.f, 1024.0f, T("feature.screen_space_gi.ui_1f_units", "%.1f units"));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				std::vector<std::string> tooltipLines = {
					T("feature.screen_space_gi.ao_radius_tooltip", "A smaller radius produces tighter AO."),
					Util::Units::FormatDistance(settings.AORadius)
				};
				Util::DrawMultiLineTooltip(tooltipLines);
			}

			{
				auto ilRadiusGuard = Util::DisableGuard(!settings.EnableGI);

				Util::Widgets::SliderFloat(T("feature.screen_space_gi.il_radius", "IL radius"), &settings.GIRadius, 10.f, 1024.0f, T("feature.screen_space_gi.ui_1f_units", "%.1f units"));
				if (auto _tt = Util::HoverTooltipWrapper()) {
					std::vector<std::string> tooltipLines = {
						T("feature.screen_space_gi.il_radius_tooltip", "A larger radius produces wider IL."),
						Util::Units::FormatDistance(settings.GIRadius)
					};
					Util::DrawMultiLineTooltip(tooltipLines);
				}
			}

			if (showAdvanced) {
				Util::Widgets::SliderFloat(T("feature.screen_space_gi.min_screen_radius", "Min Screen Radius"), &settings.MinScreenRadius, 0.f, 0.05f, "%.3f");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(T("feature.screen_space_gi.min_screen_radius_tooltip", "The minimum screen-space effect radius as proportion of display width, to prevent far field AO being too small."));
			}

			Util::Widgets::SliderFloat2(T("feature.screen_space_gi.depth_fade_range", "Depth Fade Range"), &settings.DepthFadeRange.x, 1e4, 5e4, T("feature.screen_space_gi.ui_0f_units", "%.0f units"));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				std::vector<std::string> tooltipLines = {
					T("feature.screen_space_gi.depth_fade_range_tooltip", "Distance range where depth-based effects fade out."),
					T("feature.screen_space_gi.ui_near", "Near: ") + Util::Units::FormatDistance(settings.DepthFadeRange.x),
					T("feature.screen_space_gi.ui_far", "Far: ") + Util::Units::FormatDistance(settings.DepthFadeRange.y)
				};
				Util::DrawMultiLineTooltip(tooltipLines);
			}

			if (showAdvanced) {
				ImGui::Separator();

				Util::Widgets::SliderFloat(T("feature.screen_space_gi.thickness", "Thickness"), &settings.Thickness, 0.f, 128.0f, T("feature.screen_space_gi.ui_1f_units", "%.1f units"));
				if (auto _tt = Util::HoverTooltipWrapper()) {
					std::vector<std::string> tooltipLines = {
						T("feature.screen_space_gi.thickness_tooltip", "How thick the occluders are. Only affects AO."),
						Util::Units::FormatDistance(settings.Thickness)
					};
					Util::DrawMultiLineTooltip(tooltipLines);
				}
			}
		}

		///////////////////////////////
	}
	if (page.Is("indirect")) {
		if (!settings.EnableGI && MenuUI::DetailNote(T("feature.screen_space_gi.ui_indirect_light_tuning_is_inactive_while_gi_is_off_enab_0ffeae32", "Indirect-light tuning is inactive while GI is off. Enable indirect lighting in Coverage to use it."), T("feature.screen_space_gi.ui_open_coverage", "Open Coverage")))
			MenuUI::SettingsPage::Select("ScreenSpaceGI", "coverage");
		MenuUI::SectionHeading(T("feature.screen_space_gi.ui_indirect_light", "Indirect light"));

		{
			auto visualILGuard = Util::DisableGuard(!settings.Enabled || !settings.EnableGI);

			if (showAdvanced) {
				Util::Widgets::SliderFloat(T("feature.screen_space_gi.il_distance_compensation", "IL Distance Compensation"), &settings.GIDistanceCompensation, -5.0f, 5.0f, "%.1f");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(T("feature.screen_space_gi.il_distance_compensation_tooltip", "Brighten/Dimming further radiance samples."));

				ImGui::Separator();
			}

			Util::PercentageSlider(T("feature.screen_space_gi.il_saturation", "IL Saturation"), &settings.GISaturation);
		}

		///////////////////////////////
	}
	if (page.Is("smoothing")) {
		MenuUI::SectionHeading(T("feature.screen_space_gi.ui_smoothing", "Smoothing"));

		{
			auto denoiseGuard = Util::DisableGuard(!settings.Enabled);

			{
				MenuUI::DetailGrid denoisers("denoisers");
				denoisers.Next();
				recompileFlag |= Util::Widgets::Checkbox(T("feature.screen_space_gi.temporal_denoiser", "Temporal Denoiser"), &settings.EnableTemporalDenoiser);
				denoisers.Next();
				Util::Widgets::Checkbox(T("feature.screen_space_gi.blur", "Blur"), &settings.EnableBlur);
			}

			if (showAdvanced) {
				ImGui::Separator();

				{
					auto temporalGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser);
					Util::Widgets::SliderInt(T("feature.screen_space_gi.max_frame_accumulation", "Max Frame Accumulation"), (int*)&settings.MaxAccumFrames, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextUnformatted(T("feature.screen_space_gi.max_frame_accumulation_tooltip", "How many past frames to accumulate results with. Higher values are less noisy but potentially cause ghosting."));
				}

				ImGui::Separator();

				{
					auto disocclusionGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser && !settings.EnableGI);

					Util::PercentageSlider(T("feature.screen_space_gi.movement_disocclusion", "Movement Disocclusion"), &settings.DepthDisocclusion, 0.f, 20.f);
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextUnformatted(T("feature.screen_space_gi.ui_reject_previous_frame_radiance_when_a_pixel_moves_too_7731564c", "Reject previous-frame radiance when a pixel moves too far. Lower values reject more strictly."));

					ImGui::Separator();
				}

				{
					auto blurGuard = Util::DisableGuard(!settings.EnableBlur);
					Util::Widgets::SliderFloat(T("feature.screen_space_gi.blur_radius", "Blur Radius"), &settings.BlurRadius, 0.f, 30.f, T("feature.screen_space_gi.ui_1f_px", "%.1f px"));

					Util::Widgets::SliderFloat(T("feature.screen_space_gi.geometry_weight", "Geometry Weight"), &settings.DistanceNormalisation, 0.f, 5.f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextUnformatted(T("feature.screen_space_gi.geometry_weight_tooltip", "Higher value makes the blur more sensitive to differences in geometry."));
				}
			}
		}

		///////////////////////////////
	}
	if (page.Is("diagnostics")) {
		MenuUI::SectionHeading(T("feature.screen_space_gi.ui_diagnostics", "Diagnostics"));

		if (ImGui::TreeNode(T("feature.screen_space_gi.buffer_viewer", "Buffer Viewer"))) {
			static float debugRescale = .3f;
			Util::Widgets::SliderFloat(T("feature.screen_space_gi.view_resize", "View Resize"), &debugRescale, 0.f, 1.f);

			BUFFER_VIEWER_NODE(texNoise, debugRescale)
			BUFFER_VIEWER_NODE(texWorkingDepth, debugRescale)
			BUFFER_VIEWER_NODE(texPrevGeo, debugRescale)
			BUFFER_VIEWER_NODE(texRadiance, debugRescale)
			BUFFER_VIEWER_NODE(texAo[0], debugRescale)
			BUFFER_VIEWER_NODE(texAo[1], debugRescale)
			BUFFER_VIEWER_NODE(texIlY[0], debugRescale)
			BUFFER_VIEWER_NODE(texIlY[1], debugRescale)
			BUFFER_VIEWER_NODE(texIlCoCg[0], debugRescale)
			BUFFER_VIEWER_NODE(texIlCoCg[1], debugRescale)

			ImGui::TreePop();
		}
	}
}

void ScreenSpaceGI::DrawPerformanceSettings(bool a_advanced)
{
	if (!ShadersOK())
		Util::Text::Error("%s", T(TKEY("shader_compile_error"), "Compute shaders failed to compile!"));

	DrawScreenSpaceGIEnabledCheckbox(*this, "Enable");
	{
		auto settingsGuard = Util::DisableGuard(!settings.Enabled);
		recompileFlag |= ImGui::Checkbox(T(TKEY("indirect_lighting"), "Indirect Lighting (IL)"), &settings.EnableGI);
		if (a_advanced)
			recompileFlag |= ImGui::Checkbox(T(TKEY("hq_specular_il"), "(Experimental) HQ Specular IL"), &settings.EnableExperimentalSpecularGI);
	}

	ImGui::SeparatorText(T(TKEY("quality_performance"), "Quality/Performance"));
	DrawQualityPerformanceControls(settings, recompileFlag, a_advanced, "SSGIPerformancePresets", "SSGIPerformanceResolutionMode");

	if (a_advanced) {
		ImGui::SeparatorText(T(TKEY("denoising"), "Denoising"));
		auto denoiseGuard = Util::DisableGuard(!settings.Enabled);
		recompileFlag |= ImGui::Checkbox(T(TKEY("temporal_denoiser"), "Temporal Denoiser"), &settings.EnableTemporalDenoiser);
		ImGui::Checkbox(T(TKEY("blur"), "Blur"), &settings.EnableBlur);
	}
}

void ScreenSpaceGI::SetPerformanceCostMeasurementEnabled(bool a_enabled)
{
	SetEnabled(a_enabled);
}

void ScreenSpaceGI::RestorePerformanceCostMeasurementState(const json& a_state)
{
	if (!a_state.is_object())
		return;

	auto state = a_state;
	LoadSettings(state);
}

void ScreenSpaceGI::LoadSettings(json& o_json)
{
	const bool wasEnabled = settings.Enabled;
	runtimeToggle.ReplaceSettings(settings, o_json.get<Settings>(), &Settings::Enabled);
	settings.ResourceProfile = std::clamp(settings.ResourceProfile, 0, 1);
	settings.ResolutionMode = std::clamp(settings.ResolutionMode, 0, 2);
	settings.NumSlices = std::clamp(settings.NumSlices, 1u, 10u);
	settings.NumSteps = std::clamp(settings.NumSteps, 1u, 20u);
	ClearScreenSpaceGIProfilerTimersIfDisabled(wasEnabled, settings);
	if (!wasEnabled && settings.Enabled)
		QueueHistoryReset();

	const Settings defaults{};
	const auto finiteClamp = [](float value, float fallback, float low, float high) {
		return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
	};
	settings.MinScreenRadius = finiteClamp(settings.MinScreenRadius, defaults.MinScreenRadius, 0.0f, 1.0f);
	settings.AORadius = finiteClamp(settings.AORadius, defaults.AORadius, 1e-3f, 8192.0f);
	settings.GIRadius = finiteClamp(settings.GIRadius, defaults.GIRadius, 1e-3f, 8192.0f);
	settings.Thickness = finiteClamp(settings.Thickness, defaults.Thickness, 1e-3f, 8192.0f);
	settings.DepthFadeRange.x = finiteClamp(settings.DepthFadeRange.x, defaults.DepthFadeRange.x, 0.0f, 1e7f);
	settings.DepthFadeRange.y = finiteClamp(settings.DepthFadeRange.y, defaults.DepthFadeRange.y, 0.0f, 1e7f);
	settings.GISaturation = finiteClamp(settings.GISaturation, defaults.GISaturation, 0.0f, 4.0f);
	settings.GIDistanceCompensation = finiteClamp(settings.GIDistanceCompensation, defaults.GIDistanceCompensation, 0.0f, 4.0f);
	settings.AOPower = finiteClamp(settings.AOPower, defaults.AOPower, 0.0f, 4.0f);
	settings.GIStrength = finiteClamp(settings.GIStrength, defaults.GIStrength, 0.0f, 16.0f);
	settings.DepthDisocclusion = finiteClamp(settings.DepthDisocclusion, defaults.DepthDisocclusion, 1e-5f, 1.0f);
	settings.NormalDisocclusion = finiteClamp(settings.NormalDisocclusion, defaults.NormalDisocclusion, 1e-5f, 1.0f);
	settings.MaxAccumFrames = std::clamp(settings.MaxAccumFrames, 1u, 255u);
	settings.BlurRadius = finiteClamp(settings.BlurRadius, defaults.BlurRadius, 0.0f, 8.0f);
	settings.DistanceNormalisation = finiteClamp(settings.DistanceNormalisation, defaults.DistanceNormalisation, 1e-3f, 64.0f);
	recompileFlag = true;
}

void ScreenSpaceGI::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["Enabled"] = IsEnabledRequested();
}

void ScreenSpaceGI::QueueHistoryReset() noexcept
{
	queuedResetHistory.store(true, std::memory_order_release);
}

RE::BSEventNotifyControl ScreenSpaceGI::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening)
		globals::features::screenSpaceGI.QueueHistoryReset();

	return RE::BSEventNotifyControl::kContinue;
}

bool ScreenSpaceGI::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	static bool registered = false;

	if (registered)
		return true;

	const auto ui = globals::game::ui;
	if (!ui) {
		logger::error("[Screen Space GI] UI event source not found");
		return false;
	}

	const auto source = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!source) {
		logger::error("[Screen Space GI] MenuOpenCloseEvent source not found");
		return false;
	}

	source->AddEventSink(&singleton);
	registered = true;
	logger::info("[Screen Space GI] Registered MenuOpenCloseEventHandler");
	return true;
}

void ScreenSpaceGI::PostPostLoad()
{
	MenuOpenCloseEventHandler::Register();
}

bool ScreenSpaceGI::AllowsAO() const
{
	return LocationContext::AllowsInteriorOnly(settings.AOInteriorsOnly);
}

bool ScreenSpaceGI::AllowsGI() const
{
	return IsGIActive() && LocationContext::AllowsInteriorOnly(settings.ILInteriorsOnly);
}

void ScreenSpaceGI::SetupResources()
{
	if (!globals::game::renderer || !globals::d3d::device)
		return;
	auto* mainTarget = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture;
	if (!mainTarget)
		return;
	D3D11_TEXTURE2D_DESC mainDesc{};
	mainTarget->GetDesc(&mainDesc);
	if (mainDesc.Width < 16 || mainDesc.Height < 16 || mainDesc.SampleDesc.Count != 1)
		return;
	activeResourceProfile = std::clamp(settings.ResourceProfile, 0, 1);
	texRadiance.reset();
	texRadianceTemp.reset();
	for (auto& view : uavRadiance) view = nullptr;
	for (auto& view : uavWorkingDepth) view = nullptr;
	for (auto& view : uavNormal) view = nullptr;
	for (auto& texture : texIlY) texture.reset();
	for (auto& texture : texIlCoCg) texture.reset();
	for (auto& texture : texGiSpecular) texture.reset();
	linearClampSampler = nullptr;
	pointClampSampler = nullptr;
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	logger::debug("Creating buffers...");
	{
		ssgiCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSGICB>(), "SSGI::CB");
	}

	logger::debug("Creating textures...");
	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 64,
			.Height = 64,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_UINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 5;

		if (HasGIResources()) {
			texRadiance = eastl::make_unique<Texture2D>(texDesc, "SSGI::Radiance");
			texRadiance->CreateSRV(srvDesc);
			// No default UAV needed: prefilterRadiance binds per-mip UAVs via uavRadiance[].

			// Create individual UAVs for each mip level for prefiltering
			for (uint i = 0; i < 5; ++i) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {
					.Format = DXGI_FORMAT_R11G11B10_FLOAT,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texRadiance->resource.get(), &mipUavDesc, uavRadiance[i].put()));
				Util::SetResourceName(uavRadiance[i].get(), "SSGI::Radiance UAV mip%u", i);
			}

			// Staging texture for mip 0 radiance. radianceDisocc writes it directly,
			// prefilterRadiance reads it as SRV and writes the mip chain back to texRadiance.
			// Avoids a full-texture CopySubresourceRegion each frame.
			D3D11_TEXTURE2D_DESC tempTexDesc = texDesc;
			tempTexDesc.MipLevels = 1;
			tempTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			D3D11_SHADER_RESOURCE_VIEW_DESC tempSrvDesc = {
				.Format = DXGI_FORMAT_R11G11B10_FLOAT,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = {
					.MostDetailedMip = 0,
					.MipLevels = 1 }
			};

			D3D11_UNORDERED_ACCESS_VIEW_DESC tempUavDesc = {
				.Format = DXGI_FORMAT_R11G11B10_FLOAT,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = 0 }
			};

			texRadianceTemp = eastl::make_unique<Texture2D>(tempTexDesc, "SSGI::RadianceTemp");
			texRadianceTemp->CreateSRV(tempSrvDesc);
			texRadianceTemp->CreateUAV(tempUavDesc);
		}

		texDesc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
		texDesc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;

		{
			texWorkingDepth = eastl::make_unique<Texture2D>(texDesc, "SSGI::WorkingDepth");
			texWorkingDepth->CreateSRV(srvDesc);
			for (int i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texWorkingDepth->resource.get(), &uavDesc, uavWorkingDepth[i].put()));
				Util::SetResourceName(uavWorkingDepth[i].get(), "SSGI::WorkingDepth UAV mip%d", i);
			}
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		{
			texNormal = eastl::make_unique<Texture2D>(texDesc, "SSGI::Normal");
			texNormal->CreateSRV(srvDesc);
			for (uint i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texNormal->resource.get(), &uavDesc, uavNormal[i].put()));
				Util::SetResourceName(uavNormal[i].get(), "SSGI::Normal UAV mip%u", i);
			}
		}

		uavDesc.Texture2D.MipSlice = 0;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		if (HasGIResources()) {
			texIlY[0] = eastl::make_unique<Texture2D>(texDesc, "SSGI::IlY[0]");
			texIlY[0]->CreateSRV(srvDesc);
			texIlY[0]->CreateUAV(uavDesc);

			texIlY[1] = eastl::make_unique<Texture2D>(texDesc, "SSGI::IlY[1]");
			texIlY[1]->CreateSRV(srvDesc);
			texIlY[1]->CreateUAV(uavDesc);

			texGiSpecular[0] = eastl::make_unique<Texture2D>(texDesc, "SSGI::GiSpecular[0]");
			texGiSpecular[0]->CreateSRV(srvDesc);
			texGiSpecular[0]->CreateUAV(uavDesc);

			texGiSpecular[1] = eastl::make_unique<Texture2D>(texDesc, "SSGI::GiSpecular[1]");
			texGiSpecular[1]->CreateSRV(srvDesc);
			texGiSpecular[1]->CreateUAV(uavDesc);
		}
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		if (HasGIResources()) {
			texIlCoCg[0] = eastl::make_unique<Texture2D>(texDesc, "SSGI::IlCoCg[0]");
			texIlCoCg[0]->CreateSRV(srvDesc);
			texIlCoCg[0]->CreateUAV(uavDesc);

			texIlCoCg[1] = eastl::make_unique<Texture2D>(texDesc, "SSGI::IlCoCg[1]");
			texIlCoCg[1]->CreateSRV(srvDesc);
			texIlCoCg[1]->CreateUAV(uavDesc);
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8_UNORM;
		{
			texAo[0] = eastl::make_unique<Texture2D>(texDesc, "SSGI::AO[0]");
			texAo[0]->CreateSRV(srvDesc);
			texAo[0]->CreateUAV(uavDesc);

			texAo[1] = eastl::make_unique<Texture2D>(texDesc, "SSGI::AO[1]");
			texAo[1]->CreateSRV(srvDesc);
			texAo[1]->CreateUAV(uavDesc);

			texAccumFrames[0] = eastl::make_unique<Texture2D>(texDesc, "SSGI::AccumFrames[0]");
			texAccumFrames[0]->CreateSRV(srvDesc);
			texAccumFrames[0]->CreateUAV(uavDesc);

			texAccumFrames[1] = eastl::make_unique<Texture2D>(texDesc, "SSGI::AccumFrames[1]");
			texAccumFrames[1]->CreateSRV(srvDesc);
			texAccumFrames[1]->CreateUAV(uavDesc);
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		{
			texPrevGeo = eastl::make_unique<Texture2D>(texDesc, "SSGI::PrevGeo");
			texPrevGeo->CreateSRV(srvDesc);
			texPrevGeo->CreateUAV(uavDesc);
		}
	}

	logger::debug("Loading noise texture...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path{ "Data\\Shaders\\ScreenSpaceGI\\fast_2uges.dds" };

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texNoise = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "SSGI::Noise");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texNoise->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texNoise->CreateSRV(srvDesc);
	}

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearClampSampler.put()));
		Util::SetResourceName(linearClampSampler.get(), "SSGI::LinearClampSampler");

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, pointClampSampler.put()));
		Util::SetResourceName(pointClampSampler.get(), "SSGI::PointClampSampler");
	}

	CompileComputeShaders();
	QueueHistoryReset();
}

void ScreenSpaceGI::ClearShaderCache()
{
	static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
		&radianceDisoccAOOnlyCompute, &giAOOnlyCompute, &upsampleAOOnlyCompute, &prefilterDepthsCompute, &prefilterRadianceCompute, &prefilterNormalCompute, &radianceDisoccCompute, &giCompute, &blurCompute, &upsampleCompute
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
	QueueHistoryReset();
}

void ScreenSpaceGI::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* program;
		std::string_view filename;
		bool gi = false;
		bool adaptive = false;
	};
	std::vector<ShaderCompileInfo> infos{
		{ &prefilterDepthsCompute, "prefilterDepths.cs.hlsl" },
		{ &prefilterNormalCompute, "prefilterNormal.cs.hlsl" },
		{ &radianceDisoccAOOnlyCompute, "radianceDisocc.cs.hlsl" },
		{ &giAOOnlyCompute, "gi.cs.hlsl", false, true },
		{ &upsampleAOOnlyCompute, "upsample.cs.hlsl" }
	};
	if (HasGIResources()) {
		infos.push_back({ &prefilterRadianceCompute, "prefilterRadiance.cs.hlsl", true });
		infos.push_back({ &radianceDisoccCompute, "radianceDisocc.cs.hlsl", true });
		infos.push_back({ &giCompute, "gi.cs.hlsl", true, true });
		infos.push_back({ &blurCompute, "blur.cs.hlsl", true });
		infos.push_back({ &upsampleCompute, "upsample.cs.hlsl", true });
	}
	std::vector<winrt::com_ptr<ID3D11ComputeShader>> replacements(infos.size());
	bool complete = true;
	for (size_t i = 0; i < infos.size(); ++i) {
		const auto& info = infos[i];
		std::vector<std::pair<const char*, const char*>> defines;
		if (info.program == &prefilterDepthsCompute) defines.push_back({ "LINEAR_FILTER", "" });
		if (settings.ResolutionMode == 1) defines.push_back({ "HALF_RES", "" });
		if (settings.ResolutionMode == 2) defines.push_back({ "QUARTER_RES", "" });
		if (settings.EnableTemporalDenoiser) defines.push_back({ "TEMPORAL_DENOISER", "" });
		if (info.gi) defines.push_back({ "GI", "" });
		if (info.gi && settings.EnableExperimentalSpecularGI) defines.push_back({ "GI_SPECULAR", "" });
		if (info.adaptive && settings.EnableAdaptiveSampling) defines.push_back({ "ADAPTIVE_SAMPLING", "" });
		auto path = std::filesystem::path("Data\\Shaders\\ScreenSpaceGI") / info.filename;
		replacements[i].attach(reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), defines, "cs_5_0")));
		complete = complete && static_cast<bool>(replacements[i]);
	}
	if (complete) {
		for (size_t i = 0; i < infos.size(); ++i)
			*infos[i].program = std::move(replacements[i]);
	} else {
		for (const auto& info : infos)
			*info.program = nullptr;
		logger::warn("Screen Space GI shader set incomplete; the effect remains disabled until shaders are rebuilt.");
	}
	recompileFlag = false;
}

bool ScreenSpaceGI::ShadersOK() const
{
	return prefilterDepthsCompute && prefilterNormalCompute && radianceDisoccAOOnlyCompute && giAOOnlyCompute &&
	       (settings.ResolutionMode == 0 || upsampleAOOnlyCompute) &&
	       (!AllowsGI() || (prefilterRadianceCompute && radianceDisoccCompute && giCompute &&
	       (!settings.EnableBlur || blurCompute) && (settings.ResolutionMode == 0 || upsampleCompute)));
}

bool ScreenSpaceGI::RuntimeResourcesOK() const
{
	if (settings.ResolutionMode < 0 || settings.ResolutionMode > 2)
		return false;

	if (!loaded ||
		!globals::d3d::device ||
		!globals::d3d::context ||
		!globals::game::renderer ||
		!globals::game::graphicsState ||
		!globals::game::shadowState ||
		!globals::deferred ||
		!globals::state ||
		!globals::state->sharedDataCB ||
		!globals::profiler)
		return false;

	if (globals::game::graphicsState->screenWidth == 0 ||
		globals::game::graphicsState->screenHeight == 0 ||
		!ssgiCB ||
		!ssgiCB->CB() ||
		!globals::state->sharedDataCB->CB() ||
		!pointClampSampler ||
		!linearClampSampler)
		return false;

	const auto hasSRV = [](const auto& a_texture) {
		return a_texture && a_texture->resource && a_texture->srv;
	};
	const auto hasSRVAndUAV = [&](const auto& a_texture) {
		return hasSRV(a_texture) && a_texture->uav;
	};

	if (!hasSRV(texNoise) ||
		(HasGIResources() && (!hasSRV(texRadiance) || !hasSRVAndUAV(texRadianceTemp))) ||
		!hasSRV(texWorkingDepth) ||
		!hasSRV(texNormal) ||
		!hasSRVAndUAV(texPrevGeo))
		return false;

	for (const auto& uav : uavWorkingDepth)
		if (!uav)
			return false;
	if (HasGIResources())
	for (const auto& uav : uavRadiance)
		if (!uav)
			return false;
	for (const auto& uav : uavNormal)
		if (!uav)
			return false;

	for (std::size_t i = 0; i < 2; ++i) {
		if (!hasSRVAndUAV(texAccumFrames[i]) ||
			!hasSRVAndUAV(texAo[i]) ||
			(HasGIResources() && (!hasSRVAndUAV(texIlY[i]) || !hasSRVAndUAV(texIlCoCg[i]) || !hasSRVAndUAV(texGiSpecular[i]))))
			return false;
	}

	const auto* graphicsState = globals::game::graphicsState;
	const auto& runtimeData = graphicsState->GetRuntimeData();
	if (!runtimeData.dynamicResolutionLock &&
		(!std::isfinite(runtimeData.dynamicResolutionWidthRatio) ||
			runtimeData.dynamicResolutionWidthRatio <= 0.0f ||
			!std::isfinite(runtimeData.dynamicResolutionHeightRatio) ||
			runtimeData.dynamicResolutionHeightRatio <= 0.0f)) {
		return false;
	}

	const uint32_t resolutionDivisor = 1u << settings.ResolutionMode;
	const auto dynamicScreenSize = Util::ConvertToDynamic(float2{
		static_cast<float>(graphicsState->screenWidth),
		static_cast<float>(graphicsState->screenHeight) });
	const auto dynamicTextureSize = Util::ConvertToDynamic(float2{
		static_cast<float>(texWorkingDepth->desc.Width),
		static_cast<float>(texWorkingDepth->desc.Height) });
	const auto validDynamicSize = [resolutionDivisor](
									  const float2& a_size,
									  uint32_t a_allocatedWidth,
									  uint32_t a_allocatedHeight) {
		return std::isfinite(a_size.x) &&
		       std::isfinite(a_size.y) &&
		       a_size.x >= static_cast<float>(resolutionDivisor) &&
		       a_size.y >= static_cast<float>(resolutionDivisor) &&
		       std::floor(a_size.x) <= static_cast<float>(a_allocatedWidth) &&
		       std::floor(a_size.y) <= static_cast<float>(a_allocatedHeight);
	};
	if (!validDynamicSize(
			dynamicScreenSize,
			texWorkingDepth->desc.Width,
			texWorkingDepth->desc.Height) ||
		!validDynamicSize(
			dynamicTextureSize,
			texWorkingDepth->desc.Width,
			texWorkingDepth->desc.Height)) {
		return false;
	}

	if (!Util::GetCurrentSceneDepthSRV())
		return false;

	const auto& renderTargets = globals::game::renderer->GetRuntimeData().renderTargets;
	return renderTargets[globals::deferred->forwardRenderTargets[0]].SRV &&
	       renderTargets[NORMALROUGHNESS].SRV &&
	       renderTargets[RE::RENDER_TARGET::kMOTION_VECTOR].SRV;
}

bool ScreenSpaceGI::IsRuntimeReady() const
{
	return RuntimeResourcesOK() && ShadersOK();
}

bool ScreenSpaceGI::IsPerformanceTuningApplicable() const
{
	return IsRuntimeReady();
}

const char* ScreenSpaceGI::GetPerformanceTuningApplicabilityReason() const
{
	if (IsPerformanceTuningApplicable())
		return nullptr;

	if (!RuntimeResourcesOK()) {
		return T(
			"menu.performance_tuning.feature.screen_space_gi.runtime_resources_unavailable",
			"Screen Space GI cannot be measured because its runtime resources are unavailable.");
	}

	return T(
		"menu.performance_tuning.feature.screen_space_gi.shaders_unavailable",
		"Screen Space GI cannot be measured because one or more required compute shaders failed to compile.");
}

void ScreenSpaceGI::UpdateSB()
{
	float2 res = { (float)texWorkingDepth->desc.Width, (float)texWorkingDepth->desc.Height };
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { floor(dynres.x), floor(dynres.y) };

	static float4x4 prevInvView = {};

	SSGICB data{};
	{
		{
			auto eye = globals::game::shadowState->GetRuntimeData().cameraData.getEye();

			data.PrevInvViewMat = prevInvView;
			data.NDCToViewMul = { 2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1), 0.0f, 0.0f };
			data.NDCToViewAdd = { -1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1), 0.0f, 0.0f };

			prevInvView = eye.viewMat.Invert();
		}

		data.TexDim = res;
		data.RcpTexDim = float2(1.0f) / res;
		data.FrameDim = dynres;
		data.RcpFrameDim = float2(1.0f) / dynres;
		data.FrameIndex = globals::state->frameCount;

		data.NumSlices = settings.NumSlices;
		data.NumSteps = settings.NumSteps;
		data.MinScreenRadius = settings.MinScreenRadius * dynres.x;

		data.EffectRadius = std::max(1e-3f, std::max(settings.AORadius, AllowsGI() ? settings.GIRadius : 0.0f));
		data.AORadius = settings.AORadius / data.EffectRadius;
		data.GIRadius = settings.GIRadius / data.EffectRadius;
		data.Thickness = settings.Thickness;
		data.DepthFadeRange = { std::min(settings.DepthFadeRange.x, settings.DepthFadeRange.y), std::max(settings.DepthFadeRange.x, settings.DepthFadeRange.y) };
		data.DepthFadeScaleConst = 1.0f / std::max(data.DepthFadeRange.y - data.DepthFadeRange.x, 1.0f);

		data.GISaturation = settings.GISaturation;
		data.GIDistanceCompensation = settings.GIDistanceCompensation;
		data.GICompensationMaxDist = settings.AORadius;

		data.AOPower = AllowsAO() ? settings.AOPower : 0.0f;
		data.GIStrength = AllowsGI() ? settings.GIStrength : 0.0f;

		data.DepthDisocclusion = settings.DepthDisocclusion;
		data.NormalDisocclusion = settings.NormalDisocclusion;
		data.MaxAccumFrames = settings.MaxAccumFrames;
		data.BlurRadius = settings.BlurRadius;
		data.DistanceNormalisation = settings.DistanceNormalisation;
	}

	ssgiCB->Update(data);
}

void ScreenSpaceGI::DrawSSGI()
{
	if (runtimeToggle.Apply(settings.Enabled, globals::state)) {
		QueueHistoryReset();
		ClearScreenSpaceGIProfilerTimersIfDisabled(true, settings);
	}
	auto context = globals::d3d::context;
	const bool allowAO = AllowsAO();
	const bool runILPath = AllowsGI();
	const bool runRadiance = runILPath || settings.EnableTemporalDenoiser;

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	if (imageSpaceManager) {
		auto& ssaoBlur = imageSpaceManager->GetRuntimeData().BSImagespaceShaderISSAOBlurH;
		if (ssaoBlur)
			ssaoBlur->enableSAO = settings.EnableVanillaSSAO;
	}

	if (settings.Enabled && recompileFlag && RuntimeResourcesOK())
		ClearShaderCache();

	if (!(settings.Enabled && (allowAO || runILPath) && IsRuntimeReady())) {
		QueueHistoryReset();
		if (settings.Enabled)
			ClearScreenSpaceGIProfilerTimers();
		if (!context)
			return;

		FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		const auto clearOutput = [&](const auto& a_texture) {
			if (a_texture && a_texture->uav)
				context->ClearUnorderedAccessViewFloat(a_texture->uav.get(), clr);
		};
		const auto aoIndex = std::min(outputAoIdx, 1u);
		const auto ilIndex = std::min(outputIlIdx, 1u);
		clearOutput(texAo[aoIndex]);
		clearOutput(texIlY[ilIndex]);
		clearOutput(texIlCoCg[ilIndex]);
		clearOutput(texGiSpecular[aoIndex]);
		return;
	}

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "SSGI");

	static uint lastFrameAoTexIdx = 0;
	static uint lastFrameGITexIdx = 0;
	static uint lastFrameAccumTexIdx = 0;
	uint inputAoTexIdx = lastFrameAoTexIdx;
	uint inputGITexIdx = lastFrameGITexIdx;

	static uint64_t previousMode = UINT64_MAX;
	const auto viewport = globals::game::graphicsState;
	const auto activeSize = Util::ConvertToDynamic(float2{ static_cast<float>(viewport->screenWidth), static_cast<float>(viewport->screenHeight) });
	const uint64_t mode = (static_cast<uint64_t>(static_cast<uint32_t>(activeSize.x)) << 32) |
		(static_cast<uint64_t>(static_cast<uint32_t>(activeSize.y)) << 8) |
		static_cast<uint64_t>(settings.ResolutionMode | (runILPath << 2) | (allowAO << 3) |
		(settings.EnableTemporalDenoiser << 4) | (settings.EnableBlur << 5));
	if (mode != previousMode) {
		previousMode = mode;
		QueueHistoryReset();
	}

	// A zero accumulation count makes the temporal denoiser fully accept the current frame,
	// dropping stale history immediately instead of fading it over MaxAccumFrames.
	if (queuedResetHistory.exchange(false, std::memory_order_acq_rel)) {
		FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		for (const auto& texture : texAccumFrames)
			context->ClearUnorderedAccessViewFloat(texture->uav.get(), clr);
	}

	//////////////////////////////////////////////////////

	UpdateSB();

	//////////////////////////////////////////////////////

	auto renderer = globals::game::renderer;
	auto rts = renderer->GetRuntimeData().renderTargets;
	auto deferred = globals::deferred;

	float2 size = Util::ConvertToDynamic(float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });
	auto resolution = std::array{ (uint)size.x, (uint)size.y };
	auto resChoices = std::array{
		resolution, std::array{ resolution[0] >> 1, resolution[1] >> 1 }, std::array{ resolution[0] >> 2, resolution[1] >> 2 }
	};
	auto internalRes = resChoices[settings.ResolutionMode];

	std::array<ID3D11ShaderResourceView*, 11> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 6> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 2> samplers = { pointClampSampler.get(), linearClampSampler.get() };
	auto cb = ssgiCB->CB();

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	//////////////////////////////////////////////////////

	context->CSSetConstantBuffers(1, 1, &cb);
	Util::BindFrameBufferConstantBuffersForCS(context);
	auto* sharedDataBuf = globals::state->sharedDataCB->CB();
	context->CSSetConstantBuffers(5, 1, &sharedDataBuf);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	// prefilter depths
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Prefilter Depths");

		srvs.at(0) = Util::GetCurrentSceneDepthSRV();
		for (int i = 0; i < 5; ++i)
			uavs.at(i) = uavWorkingDepth[i].get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(prefilterDepthsCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("ScreenSpaceGI::PrefilterDepths");
		context->Dispatch((resolution[0] + 15) >> 4, (resolution[1] + 15) >> 4, 1);
		globals::profiler->EndPass();
	}

	// Reprojection is unnecessary for AO without temporal accumulation.
	if (runRadiance) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Radiance Disocc");

		resetViews();
		srvs.at(0) = rts[deferred->forwardRenderTargets[0]].SRV;
		srvs.at(1) = texWorkingDepth->srv.get();
		srvs.at(2) = rts[NORMALROUGHNESS].SRV;
		srvs.at(3) = texPrevGeo->srv.get();
		srvs.at(4) = rts[RE::RENDER_TARGET::kMOTION_VECTOR].SRV;
		srvs.at(5) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
		srvs.at(6) = texAo[inputAoTexIdx]->srv.get();
		srvs.at(7) = (runILPath && texIlY[inputGITexIdx] ? texIlY[inputGITexIdx]->srv.get() : nullptr);
		srvs.at(8) = (runILPath && texIlCoCg[inputGITexIdx] ? texIlCoCg[inputGITexIdx]->srv.get() : nullptr);
		srvs.at(9) = (runILPath && texGiSpecular[inputAoTexIdx] ? texGiSpecular[inputAoTexIdx]->srv.get() : nullptr);
		srvs.at(10) = nullptr;

		uavs.at(0) = (runILPath && texRadianceTemp ? texRadianceTemp->uav.get() : nullptr);
		uavs.at(1) = texAccumFrames[!lastFrameAccumTexIdx]->uav.get();
		uavs.at(2) = texAo[!inputAoTexIdx]->uav.get();
		uavs.at(3) = (runILPath && texIlY[!inputGITexIdx] ? texIlY[!inputGITexIdx]->uav.get() : nullptr);
		uavs.at(4) = (runILPath && texIlCoCg[!inputGITexIdx] ? texIlCoCg[!inputGITexIdx]->uav.get() : nullptr);
		uavs.at(5) = (runILPath && texGiSpecular[!inputAoTexIdx] ? texGiSpecular[!inputAoTexIdx]->uav.get() : nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(runILPath ? radianceDisoccCompute.get() : radianceDisoccAOOnlyCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("ScreenSpaceGI::RadianceDisocc");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		// Prefilter radiance texture instead of using GenerateMips for proper dynamic resolution handling.
		// radianceDisocc wrote mip 0 directly to texRadianceTemp above.
		if (runILPath) {
			TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Prefilter Radiance");

			resetViews();
			srvs.at(0) = (runILPath && texRadianceTemp ? texRadianceTemp->srv.get() : nullptr);
			uavs.at(0) = uavRadiance[0].get();  // Mip 0
			uavs.at(1) = uavRadiance[1].get();  // Mip 1
			uavs.at(2) = uavRadiance[2].get();  // Mip 2
			uavs.at(3) = uavRadiance[3].get();  // Mip 3
			uavs.at(4) = uavRadiance[4].get();  // Mip 4

			context->CSSetShaderResources(0, 1, srvs.data());
			context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
			context->CSSetShader(prefilterRadianceCompute.get(), nullptr, 0);
			globals::profiler->BeginPass("ScreenSpaceGI::PrefilterRadiance");
			context->Dispatch((internalRes[0] + 15u) >> 4, (internalRes[1] + 15u) >> 4, 1);
			globals::profiler->EndPass();
		}

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		lastFrameAccumTexIdx = !lastFrameAccumTexIdx;
	}

	// Prefilter normals
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Prefilter Normals");

		resetViews();
		srvs.at(0) = rts[NORMALROUGHNESS].SRV;
		uavs.at(0) = uavNormal[0].get();
		uavs.at(1) = uavNormal[1].get();
		uavs.at(2) = uavNormal[2].get();
		uavs.at(3) = uavNormal[3].get();
		uavs.at(4) = uavNormal[4].get();

		context->CSSetShaderResources(0, 1, srvs.data());
		context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
		context->CSSetShader(prefilterNormalCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("ScreenSpaceGI::PrefilterNormals");
		context->Dispatch((internalRes[0] + 15u) >> 4, (internalRes[1] + 15u) >> 4, 1);
		globals::profiler->EndPass();
	}

	// GI
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - GI");

		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = rts[NORMALROUGHNESS].SRV;
		srvs.at(2) = (runILPath && texRadiance ? texRadiance->srv.get() : nullptr);
		srvs.at(3) = texNoise->srv.get();
		srvs.at(4) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
		srvs.at(5) = (runILPath && texIlY[inputGITexIdx] ? texIlY[inputGITexIdx]->srv.get() : nullptr);
		srvs.at(6) = (runILPath && texIlCoCg[inputGITexIdx] ? texIlCoCg[inputGITexIdx]->srv.get() : nullptr);
		srvs.at(7) = (runILPath && texGiSpecular[inputAoTexIdx] ? texGiSpecular[inputAoTexIdx]->srv.get() : nullptr);
		srvs.at(8) = texNormal->srv.get();

		uavs.at(0) = texAo[!inputAoTexIdx]->uav.get();
		uavs.at(1) = (runILPath && texIlY[!inputGITexIdx] ? texIlY[!inputGITexIdx]->uav.get() : nullptr);
		uavs.at(2) = (runILPath && texIlCoCg[!inputGITexIdx] ? texIlCoCg[!inputGITexIdx]->uav.get() : nullptr);
		uavs.at(3) = (runILPath && texGiSpecular[!inputAoTexIdx] ? texGiSpecular[!inputAoTexIdx]->uav.get() : nullptr);
		uavs.at(4) = texPrevGeo->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(runILPath ? giCompute.get() : giAOOnlyCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("ScreenSpaceGI::GI");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		lastFrameGITexIdx = inputGITexIdx;
		lastFrameAoTexIdx = inputAoTexIdx;
	}

	// blur
	if (settings.EnableBlur && runILPath) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Diffuse Blur");

		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = rts[NORMALROUGHNESS].SRV;
		srvs.at(2) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
		srvs.at(3) = (runILPath && texIlY[inputGITexIdx] ? texIlY[inputGITexIdx]->srv.get() : nullptr);
		srvs.at(4) = (runILPath && texIlCoCg[inputGITexIdx] ? texIlCoCg[inputGITexIdx]->srv.get() : nullptr);

		uavs.at(0) = texAccumFrames[!lastFrameAccumTexIdx]->uav.get();
		uavs.at(1) = (runILPath && texIlY[!inputGITexIdx] ? texIlY[!inputGITexIdx]->uav.get() : nullptr);
		uavs.at(2) = (runILPath && texIlCoCg[!inputGITexIdx] ? texIlCoCg[!inputGITexIdx]->uav.get() : nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(blurCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("ScreenSpaceGI::Blur");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		inputGITexIdx = !inputGITexIdx;
		lastFrameGITexIdx = inputGITexIdx;
		lastFrameAccumTexIdx = !lastFrameAccumTexIdx;
	}

	// upsample
	if (settings.ResolutionMode != 0) {
		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = texAo[inputAoTexIdx]->srv.get();
		srvs.at(2) = (runILPath && texIlY[inputGITexIdx] ? texIlY[inputGITexIdx]->srv.get() : nullptr);
		srvs.at(3) = (runILPath && texIlCoCg[inputGITexIdx] ? texIlCoCg[inputGITexIdx]->srv.get() : nullptr);
		srvs.at(4) = (runILPath && texGiSpecular[inputAoTexIdx] ? texGiSpecular[inputAoTexIdx]->srv.get() : nullptr);

		uavs.at(0) = texAo[!inputAoTexIdx]->uav.get();
		uavs.at(1) = (runILPath && texIlY[!inputGITexIdx] ? texIlY[!inputGITexIdx]->uav.get() : nullptr);
		uavs.at(2) = (runILPath && texIlCoCg[!inputGITexIdx] ? texIlCoCg[!inputGITexIdx]->uav.get() : nullptr);
		uavs.at(3) = (runILPath && texGiSpecular[!inputAoTexIdx] ? texGiSpecular[!inputAoTexIdx]->uav.get() : nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(runILPath ? upsampleCompute.get() : upsampleAOOnlyCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("ScreenSpaceGI::Upsample");
		context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
	}

	outputAoIdx = inputAoTexIdx;
	outputIlIdx = inputGITexIdx;

	// cleanup
	resetViews();

	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);
}

#undef I18N_KEY_PREFIX
