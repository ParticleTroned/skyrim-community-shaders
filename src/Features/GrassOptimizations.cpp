#include "GrassOptimizations.h"
#include "Menu/SettingsPage.h"

#include "Globals.h"
#include "GrassOptimizations/GrassBucketRenderer.h"
#include "Util.h"
#include "Utils/UI.h"
namespace GrassPolicy
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, Enabled, CrossCellBatching, FrustumCulling,
		DensityReduction, MinPixelSize, FullDetailPixelSize, MinDensity, EnableMeshLOD, EnableMidLOD, EnableFarLOD,
		MidLODPixelSize, FarLODPixelSize, MeshLODBandPixels, EnableOcclusionCulling, OcclusionBias,
		MeshCostBias, CostBiasStartDistance, InvisibleFadeCull, RenderDistanceOverride, EdgeFadeStart, SimpleShadingPixelSize, CollisionDistance)
}

GrassOptimizations::GrassOptimizations() : renderer(std::make_unique<GrassBucketRenderer>()) {}
GrassOptimizations::~GrassOptimizations() = default;

GrassPolicy::Settings GrassOptimizations::GetSettings() const
{
	std::scoped_lock lock(settingsMutex);
	return settings;
}

bool GrassOptimizations::IsEnabled() const { return queueSettings.load(std::memory_order_acquire) & 1u; }
bool GrassOptimizations::IsCrossCellBatchingEnabled() const { return queueSettings.load(std::memory_order_acquire) & 2u; }
bool GrassOptimizations::IsHookInstalled() const { return renderer->IsHookInstalled(); }
bool GrassOptimizations::SetSettings(const GrassPolicy::Settings& requested, std::string& error)
{
	if (!requested.Valid()) {
		error = "Invalid grass settings: finite ordered pixel thresholds and bounded density, bias and distances are required";
		return false;
	}
	{
		std::scoped_lock lock(settingsMutex);
		settings = requested;
		queueSettings.store((requested.Enabled ? 1u : 0u) | (requested.CrossCellBatching ? 2u : 0u), std::memory_order_release);
	}
	return true;
}

void GrassOptimizations::SetEnabled(bool enabled)
{
	auto next = GetSettings();
	next.Enabled = enabled;
	std::string error;
	if (!SetSettings(next, error))
		logger::warn("Grass optimization toggle rejected: {}", error);
}

bool GrassOptimizations::UpdateSettings(const json& update, std::string& error)
{
	if (!update.is_object() || update.empty()) {
		error = "Grass settings must be a nonempty object";
		return false;
	}
	json merged = GetSettings();
	for (const auto& [key, value] : update.items()) {
		if (!merged.contains(key) || (merged[key].is_boolean() ? !value.is_boolean() : !value.is_number())) {
			error = "Unknown grass setting or incorrect type: " + key;
			return false;
		}
		merged[key] = value;
	}
	try {
		return SetSettings(merged.get<GrassPolicy::Settings>(), error);
	} catch (const json::exception& exception) {
		error = exception.what();
		return false;
	}
}

void GrassOptimizations::LoadSettings(json& saved)
{
	GrassPolicy::Settings next;
	try {
		next = saved.get<GrassPolicy::Settings>();
	} catch (const json::exception& error) {
		logger::warn("Invalid saved grass settings: {}", error.what());
	}
	if (!next.Valid()) {
		logger::warn("Invalid saved grass settings; restoring grass optimization defaults");
		next = {};
	}
	std::string error;
	if (!SetSettings(next, error))
		logger::warn("Saved grass settings rejected: {}", error);
}
void GrassOptimizations::SaveSettings(json& saved) { saved = GetSettings(); }
void GrassOptimizations::RestoreDefaultSettings()
{
	GrassPolicy::Settings next;
	std::string error;
	if (!SetSettings(next, error))
		logger::warn("Grass defaults rejected: {}", error);
}
void GrassOptimizations::SetupResources() { renderer->SetupResources(); }
void GrassOptimizations::ClearShaderCache() { renderer->ClearShaderCache(); }
void GrassOptimizations::Reset() { renderer->EndFrame(); }
void GrassOptimizations::PostPostLoad() { renderer->InstallHooks(); }
void GrassOptimizations::PrepareGeometry(RE::BSRenderPass* pass, RE::BSShader* shader, uint32_t flags, GrassRuntime::SetupGeometry setup, std::optional<uint32_t> nativeWindTimer) { renderer->PrepareGeometry(pass, shader, flags, setup, nativeWindTimer); }

std::pair<std::string, std::vector<std::string>> GrassOptimizations::GetFeatureSummary()
{
	return { T("feature.grass_optimizations.combines_compatible_grass_draws_and_removes_grass_outside_the", "Combines compatible grass draws and removes grass outside the view."),
		{ T("feature.grass_optimizations.independent_distant_density_fading_and_mesh_cost_controls", "Independent distant density, fading and mesh cost controls"), T("feature.grass_optimizations.optional_middle_far_meshes_and_simpler_distant_shading", "Optional middle/far meshes and simpler distant shading"), T("feature.grass_optimizations.separate_grass_hi_z_switch_for_performance_comparisons", "Separate grass Hi-Z switch for performance comparisons") } };
}

Feature::SettingsHeaderStatus GrassOptimizations::GetSettingsHeaderStatus() const
{
	const bool available = renderer->IsRenderingAvailable();
	return { available ? T("feature.grass_optimizations.ready", "Ready") : T("feature.grass_optimizations.unavailable", "Unavailable"), !available };
}

void GrassOptimizations::DrawSettings()
{
	MenuUI::SettingsPage page("GrassOptimizations", {
														{ "visibility", T("feature.grass_optimizations.visibility", "Visibility"), T("feature.grass_optimizations.choose_batching_and_visibility_checks_first", "Choose batching and visibility checks first."), T("feature.grass_optimizations.batching_and_visibility_checks", "Batching and visibility checks"), true, true, T("feature.grass_optimizations.balance_coverage_and_cost", "Balance coverage and cost") },
														{ "density", T("feature.grass_optimizations.density", "Density"), T("feature.grass_optimizations.balance_grass_coverage_and_distant_density", "Balance grass coverage and distant density."), T("feature.grass_optimizations.near_and_distant_coverage", "Near and distant coverage"), true, true, nullptr },
														{ "distance", T("feature.grass_optimizations.distance", "Distance"), T("feature.grass_optimizations.refine_how_far_grass_is_drawn_and_shaded", "Refine how far grass is drawn and shaded."), T("feature.grass_optimizations.draw_and_shading_distances", "Draw and shading distances"), true, true, T("feature.grass_optimizations.refine_distant_grass", "Refine distant grass") },
														{ "meshes", T("feature.grass_optimizations.meshes", "Meshes"), T("feature.grass_optimizations.choose_simpler_distant_meshes_and_their_transitions", "Choose simpler distant meshes and their transitions."), T("feature.grass_optimizations.simplified_meshes_and_transitions", "Simplified meshes and transitions"), true, true, nullptr },
													});
	for (const auto section : { "visibility", "density", "distance", "meshes" })
		if (page.Is(section))
			DrawControls(true, section);
}
void GrassOptimizations::DrawPerformanceSettings(bool advanced) { DrawControls(advanced); }
json GrassOptimizations::CapturePerformanceSettingsState() const { return GetSettings(); }
bool GrassOptimizations::IsPerformanceCostMeasurementEnabled() const { return IsEnabled() && renderer->IsRenderingAvailable(); }
bool GrassOptimizations::IsPerformanceCostMeasurementReady() const { return !IsEnabled() || renderer->IsRenderingAvailable(); }
void GrassOptimizations::RestorePerformanceCostMeasurementState(const json& state)
{
	if (!state.is_object())
		return;
	auto saved = state;
	LoadSettings(saved);
}

void GrassOptimizations::DrawControls(bool advanced, std::string_view section)
{
	constexpr auto tooltipFlags = ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled;
	// Decimal rounding can cross a strict lower bound or a saved fractional upper bound.
	constexpr auto thresholdFlags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat;
	auto next = GetSettings();
	const auto panel = [section](std::string_view id) { return section.empty() || section == id; };
	bool changed = false;
	if (section.empty()) {
		changed = Util::Widgets::Checkbox(T("feature.grass_optimizations.grass_optimizations", "Grass optimizations"), &next.Enabled);
		Util::AddTooltip(T("feature.grass_optimizations.optimize_grass_drawing_and_visibility_quality_controls_can_trade", "Optimize grass drawing and visibility. Quality controls can trade grass density, range or detail for performance."), tooltipFlags);
	}
	auto guard = Util::DisableGuard(!next.Enabled);

	if (panel("visibility")) {
		if (advanced) {
			changed |= Util::Widgets::Checkbox(T("feature.grass_optimizations.combine_grass_across_cells", "Combine grass across cells"), &next.CrossCellBatching);
			Util::AddTooltip(T("feature.grass_optimizations.group_grass_across_loaded_cells_into_fewer_draws_keeps", "Group grass across loaded cells into fewer draws. Keeps its appearance and may improve performance."), tooltipFlags);
		}
		changed |= Util::Widgets::Checkbox(T("feature.grass_optimizations.skip_grass_outside_the_view", "Skip grass outside the view"), &next.FrustumCulling);
		Util::AddTooltip(T("feature.grass_optimizations.skip_grass_outside_your_view_to_save_rendering_work", "Skip grass outside your view to save rendering work without changing visible grass."), tooltipFlags);
	}

	if (panel("density")) {
		changed |= Util::Widgets::Checkbox(T("feature.grass_optimizations.reduce_distant_grass_density", "Reduce distant grass density"), &next.DensityReduction);
		Util::AddTooltip(T("feature.grass_optimizations.thin_distant_grass_to_save_rendering_work_can_improve", "Thin distant grass to save rendering work. Can improve performance but makes grass coverage sparser."), tooltipFlags);
		if (advanced && next.DensityReduction) {
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.smallest_grass_size", "Smallest grass size"), &next.MinPixelSize, 0.0f, 64.0f, "%.1f px");
			Util::AddTooltip(T("feature.grass_optimizations.remove_grass_smaller_than_this_on_screen_higher_values", "Remove grass smaller than this on screen. Higher values save rendering work but can leave sparse patches or shorten its visible reach."), tooltipFlags);
			next.FullDetailPixelSize = std::max(next.FullDetailPixelSize, next.MinPixelSize + 0.01f);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.full_density_grass_size", "Full-density grass size"), &next.FullDetailPixelSize, next.MinPixelSize + 0.01f, 256.0f, "%.2f px", thresholdFlags);
			Util::AddTooltip(T("feature.grass_optimizations.grass_above_this_on_screen_size_keeps_full_density", "Grass above this on-screen size keeps full density. Higher values thin more grass, saving work at the cost of coverage."), tooltipFlags);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.minimum_density", "Minimum density"), &next.MinDensity, 0.0f, 1.0f, "%.2f");
			Util::AddTooltip(T("feature.grass_optimizations.set_how_much_distant_grass_to_keep_higher_values", "Set how much distant grass to keep. Higher values fill gaps but cost more; grass below the smallest-size cutoff is still removed."), tooltipFlags);
		}
	}

	if (panel("distance")) {
		if (advanced) {
			MenuUI::DetailGrid distanceGrid("GrassDistance", 2, ImGui::GetFontSize() * 32);
			const Util::Widgets::ControlLayout controls(true);
			distanceGrid.Next();
			MenuUI::SectionHeading(T("feature.grass_optimizations.draw_distance", "Draw distance"));
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.distant_mesh_cost_bias", "Distant mesh cost bias"), &next.MeshCostBias, 0.0f, 1.0f, "%.2f");
			Util::AddTooltip(T("feature.grass_optimizations.give_complex_grass_meshes_less_distant_reach_and_with", "Give complex grass meshes less distant reach and, with density reduction on, thinner coverage to save work. Zero disables this adjustment."), tooltipFlags);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.cost_bias_start_distance", "Cost bias start distance"), &next.CostBiasStartDistance, 0.0f, 20000.0f, "%.0f");
			Util::AddTooltip(T("feature.grass_optimizations.choose_where_mesh_cost_bias_starts_reducing_grass_lower", "Choose where mesh cost bias starts reducing grass. Lower distances trade coverage for performance sooner; zero starts immediately."), tooltipFlags);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.grass_render_distance", "Grass render distance"), &next.RenderDistanceOverride, 0.0f, 100000.0f, "%.0f");
			Util::AddTooltip(T("feature.grass_optimizations.set_how_far_grass_is_visible_shorter_distances_save", "Set how far grass is visible. Shorter distances save rendering work; zero uses the game's setting. Limited to loaded grass."), tooltipFlags);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.distance_fade_start", "Distance fade start"), &next.EdgeFadeStart, 0.0f, 1.0f, "%.2f");
			Util::AddTooltip(T("feature.grass_optimizations.choose_where_grass_starts_fading_toward_its_distance_limit", "Choose where grass starts fading toward its distance limit. Lower values fade it sooner and more gradually; skipping faded grass can save work."), tooltipFlags);
			distanceGrid.Next();
			MenuUI::SectionHeading(T("feature.grass_optimizations.distant_detail_and_collision", "Distant detail and collision"));
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.skip_nearly_invisible_grass", "Skip nearly invisible grass"), &next.InvisibleFadeCull, 0.0f, 1.0f, "%.3f");
			Util::AddTooltip(T("feature.grass_optimizations.stop_drawing_grass_below_this_fade_value_higher_values", "Stop drawing grass below this fade value. Higher values save rendering work but can make grass disappear more abruptly."), tooltipFlags);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.simpler_shading_below", "Simpler shading below"), &next.SimpleShadingPixelSize, 0.0f, 32.0f, "%.1f px");
			Util::AddTooltip(T("feature.grass_optimizations.use_simpler_lighting_on_grass_smaller_than_this_on", "Use simpler lighting on grass smaller than this on screen. Higher values save work but reduce shading detail; zero keeps full shading."), tooltipFlags);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.grass_collision_distance", "Grass collision distance"), &next.CollisionDistance, 0.0f, GrassPolicy::kMaxCollisionDistance, "%.0f units");
			Util::AddTooltip(T("feature.grass_optimizations.limit_how_far_optimized_grass_responds_to_collisions_shorter", "Limit how far optimized grass responds to collisions. Shorter distances save work; zero disables this bending. Requires Grass Collision and its local coverage."), tooltipFlags);
		}
	}

	if (panel("meshes")) {
		changed |= Util::Widgets::Checkbox(T("feature.grass_optimizations.use_distant_grass_meshes", "Use distant grass meshes"), &next.EnableMeshLOD);
		Util::AddTooltip(T("feature.grass_optimizations.use_simpler_distant_meshes_to_save_rendering_work_with", "Use simpler distant meshes to save rendering work, with less detail. Requires compatible grass LOD meshes; missing ones keep full detail."), tooltipFlags);
		if (advanced && next.EnableMeshLOD) {
			changed |= Util::Widgets::Checkbox(T("feature.grass_optimizations.middle_grass_lod", "Middle grass LOD"), &next.EnableMidLOD);
			Util::AddTooltip(T("feature.grass_optimizations.use_simpler_meshes_for_middle_distance_grass_reducing_detail", "Use simpler meshes for middle-distance grass, reducing detail and rendering cost. Requires compatible grass LOD meshes."), tooltipFlags);
			changed |= Util::Widgets::Checkbox(T("feature.grass_optimizations.far_grass_lod", "Far grass LOD"), &next.EnableFarLOD);
			Util::AddTooltip(T("feature.grass_optimizations.use_simpler_meshes_for_faraway_grass_reducing_distant_detail", "Use simpler meshes for faraway grass, reducing distant detail and rendering cost. Requires compatible grass LOD meshes."), tooltipFlags);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.middle_lod_size", "Middle LOD size"), &next.MidLODPixelSize, 0.01f, 128.0f, "%.2f px", thresholdFlags);
			Util::AddTooltip(T("feature.grass_optimizations.use_the_middle_grass_mesh_below_this_on_screen", "Use the middle grass mesh below this on-screen size. Higher values switch sooner, saving work but reducing detail."), tooltipFlags);
			next.FarLODPixelSize = std::min(next.FarLODPixelSize, next.MidLODPixelSize);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.far_lod_size", "Far LOD size"), &next.FarLODPixelSize, 0.01f, next.MidLODPixelSize, "%.2f px", thresholdFlags);
			Util::AddTooltip(T("feature.grass_optimizations.use_the_far_grass_mesh_below_this_on_screen", "Use the far grass mesh below this on-screen size. Higher values trade more distant detail for performance."), tooltipFlags);
			changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.lod_transition_width", "LOD transition width"), &next.MeshLODBandPixels, 0.01f, 32.0f, "%.2f px", thresholdFlags);
			Util::AddTooltip(T("feature.grass_optimizations.spread_changes_between_grass_meshes_to_soften_transitions_wider", "Spread changes between grass meshes to soften transitions. Wider values may keep detailed meshes longer and add rendering work."), tooltipFlags);
		}
	}

	if (panel("visibility")) {
		{
			changed |= Util::Widgets::Checkbox(T("feature.grass_optimizations.grass_hi_z_culling", "Grass Hi-Z culling"), &next.EnableOcclusionCulling);
			Util::AddTooltip(T("feature.grass_optimizations.skip_grass_hidden_behind_solid_objects_depending_on_the", "Skip grass hidden behind solid objects. Depending on the scene, this can save work or have a small performance cost."), tooltipFlags);
			if (advanced && next.EnableOcclusionCulling) {
				changed |= Util::Widgets::SliderFloat(T("feature.grass_optimizations.grass_occlusion_tolerance", "Grass occlusion tolerance"), &next.OcclusionBias, 0.0f, 0.05f, "%.4f");
				Util::AddTooltip(T("feature.grass_optimizations.make_hidden_grass_skipping_more_cautious_increase_this_if", "Make hidden-grass skipping more cautious. Increase this if grass disappears incorrectly; higher values keep more grass and can cost performance."), tooltipFlags);
			}
		}
	}

	if (changed) {
		std::string error;
		if (!SetSettings(next, error))
			logger::warn("Grass settings rejected: {}", error);
	}
	if (!renderer->IsRenderingAvailable())
		MenuUI::DetailNote(T("feature.grass_optimizations.native_grass_rendering_is_active_optimizations_are_unavailable", "Native grass rendering is active; optimizations are unavailable."));
}
#ifdef DEVBENCH_BRIDGE_ENABLED
void GrassOptimizations::SetDiagnosticsEnabled(bool enabled) { renderer->SetDiagnosticsEnabled(enabled); }
void GrassOptimizations::RequestTelemetry() { renderer->RequestTelemetry(); }
json GrassOptimizations::GetDiagnostics() const
{
	auto result = renderer->GetDiagnostics();
	const auto configured = GetSettings();
	result["loaded"] = loaded;
	result["enabled"] = configured.Enabled;
	result["settings"] = configured;
	result["grassHiZAvailable"] = true;
	result["grassHiZRequested"] = configured.EnableOcclusionCulling;
	result["grassHiZEnabled"] = configured.Enabled && configured.EnableOcclusionCulling;
	return result;
}
#endif
