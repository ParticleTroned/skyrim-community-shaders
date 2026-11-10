#include "GrassOptimizations.h"
#include "Menu/SettingsPage.h"

#include "Globals.h"
#include "GrassOptimizations/GrassBucketRenderer.h"
#include "Util.h"
#include "Utils/UI.h"
#include <cmath>
namespace GrassPolicy
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, Enabled, CrossCellBatching, FrustumCulling,
		DensityReduction, MinPixelSize, FullDetailPixelSize, MinDensity, EnableMeshLOD, EnableMidLOD, EnableFarLOD,
		MidLODPixelSize, FarLODPixelSize, MeshLODBandPixels, EnableOcclusionCulling, OcclusionBias,
		MeshCostBias, CostBiasStartDistance, InvisibleFadeCull, RenderDistanceOverride, EdgeFadeStart, SimpleShadingPixelSize, CollisionDistance,
		FarLODOptimization, FarLODMinPixelSize, FarLODMinDensity, FarLODMaxDensity, FarLODCoverageBias, ProportionalDensityFade)
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

#ifdef DEVBENCH_BRIDGE_ENABLED
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

#endif

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
	return { "Reduce the work needed to display grass. Optional density, distance and detail settings trade visual quality for performance.",
		{ "Independent distant density, fading and mesh cost controls", "Optional middle/far meshes and simpler distant shading", "Separate grass Hi-Z switch for performance comparisons" } };
}

Feature::SettingsHeaderStatus GrassOptimizations::GetSettingsHeaderStatus() const
{
	const bool available = renderer->IsRenderingAvailable();
	return { available ? "Ready" : "Unavailable", !available };
}

void GrassOptimizations::DrawSettings()
{
	MenuUI::SettingsPage page("GrassOptimizations", {
														{ "visibility", "Visibility", "Choose batching and visibility checks first.", "Batching and visibility checks", true, true, "Balance coverage and cost" },
														{ "density", "Density", "Balance grass coverage and distant density.", "Near and distant coverage", true, true, nullptr },
														{ "distance", "Distance", "Refine how far grass is drawn and shaded.", "Draw and shading distances", true, true, "Refine distant grass" },
														{ "meshes", "Meshes", "Choose simpler distant meshes and their transitions.", "Simplified meshes and transitions", true, true, nullptr },
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
		changed = Util::Widgets::Checkbox("Grass optimizations", &next.Enabled);
		Util::AddTooltip("Enable these controls to reduce the work needed to display grass. Your settings determine any loss of density, detail or visible range.", tooltipFlags);
	}
	auto guard = Util::DisableGuard(!next.Enabled);

	if (panel("visibility")) {
		if (advanced) {
			changed |= Util::Widgets::Checkbox("Combine grass across cells", &next.CrossCellBatching);
			Util::AddTooltip("Handle grass from several areas together. Can improve performance without changing its density or appearance.", tooltipFlags);
		}
		changed |= Util::Widgets::Checkbox("Skip grass outside the view", &next.FrustumCulling);
		Util::AddTooltip("Skip grass outside your view to save rendering work without changing visible grass.", tooltipFlags);
	}

	if (panel("density")) {
		changed |= Util::Widgets::Checkbox("Reduce distant grass density", &next.DensityReduction);
		Util::AddTooltip("Thin distant grass to save rendering work. Can improve performance but makes grass coverage sparser.", tooltipFlags);
		if (advanced && next.DensityReduction) {
			changed |= Util::Widgets::Checkbox("Proportional density fade", &next.ProportionalDensityFade);
			Util::AddTooltip("Use a shorter fade when thinning grass. Removing barely visible clumps sooner can improve performance, but changes in density may be more noticeable.", tooltipFlags);
			changed |= Util::Widgets::SliderFloat("Smallest grass size", &next.MinPixelSize, 0.0f, 64.0f, "%.1f px");
			Util::AddTooltip("Remove grass smaller than this on screen. Higher values save rendering work but can leave sparse patches or shorten its visible reach.", tooltipFlags);
			next.FullDetailPixelSize = std::max(next.FullDetailPixelSize, next.MinPixelSize + 0.01f);
			changed |= Util::Widgets::SliderFloat("Full-density grass size", &next.FullDetailPixelSize, next.MinPixelSize + 0.01f, 256.0f, "%.2f px", thresholdFlags);
			Util::AddTooltip("Grass above this on-screen size keeps full density. Higher values thin more grass, saving work at the cost of coverage.", tooltipFlags);
			changed |= Util::Widgets::SliderFloat("Minimum density", &next.MinDensity, 0.0f, 1.0f, "%.2f");
			Util::AddTooltip("Set how much distant grass to keep. Higher values fill gaps but cost more; grass below the smallest-size cutoff is still removed.", tooltipFlags);
		}
	}

	if (panel("distance")) {
		if (advanced) {
			MenuUI::DetailGrid distanceGrid("GrassDistance", 2, ImGui::GetFontSize() * 32);
			const Util::Widgets::ControlLayout controls(true);
			distanceGrid.Next();
			MenuUI::SectionHeading("Draw distance");
			changed |= Util::Widgets::SliderFloat("Distant mesh cost bias", &next.MeshCostBias, 0.0f, 1.0f, "%.2f");
			Util::AddTooltip("Give complex grass meshes less distant reach and, with density reduction on, thinner coverage to save work. Zero disables this adjustment.", tooltipFlags);
			changed |= Util::Widgets::SliderFloat("Cost bias start distance", &next.CostBiasStartDistance, 0.0f, 20000.0f, "%.0f");
			Util::AddTooltip("Choose where mesh cost bias starts reducing grass. Lower distances trade coverage for performance sooner; zero starts immediately.", tooltipFlags);
			changed |= Util::Widgets::SliderFloat("Grass render distance", &next.RenderDistanceOverride, 0.0f, 100000.0f, "%.0f");
			Util::AddTooltip("Set how far grass is visible. Shorter distances save rendering work; zero uses the game's setting. Limited to loaded grass.", tooltipFlags);
			changed |= Util::Widgets::SliderFloat("Distance fade start", &next.EdgeFadeStart, 0.0f, 1.0f, "%.2f");
			Util::AddTooltip("Choose where grass starts fading toward its distance limit. Lower values fade it sooner and more gradually; skipping faded grass can save work.", tooltipFlags);
			distanceGrid.Next();
			MenuUI::SectionHeading("Distant detail and collision");
			changed |= Util::Widgets::SliderFloat("Skip nearly invisible grass", &next.InvisibleFadeCull, 0.0f, 1.0f, "%.3f");
			Util::AddTooltip("Stop drawing grass below this fade value. Higher values save rendering work but can make grass disappear more abruptly.", tooltipFlags);
			changed |= Util::Widgets::SliderFloat("Simpler shading below", &next.SimpleShadingPixelSize, 0.0f, 32.0f, "%.1f px");
			Util::AddTooltip("Use simpler lighting on grass smaller than this on screen. Higher values save work but reduce shading detail; zero keeps full shading.", tooltipFlags);
			changed |= Util::Widgets::SliderFloat("Grass collision distance", &next.CollisionDistance, 0.0f, GrassPolicy::kMaxCollisionDistance, "%.0f units");
			Util::AddTooltip("Limit how far optimized grass responds to collisions. Shorter distances save work; zero disables this bending. Requires Grass Collision and its local coverage.", tooltipFlags);
		}
	}

	if (panel("meshes")) {
		changed |= Util::Widgets::Checkbox("Use distant grass meshes", &next.EnableMeshLOD);
		Util::AddTooltip("Use simpler shapes for distant grass to improve performance, at the cost of fine detail. Requires compatible distant grass models; otherwise the original grass is used.", tooltipFlags);
		if (advanced && next.EnableMeshLOD) {
			changed |= Util::Widgets::Checkbox("Middle grass LOD", &next.EnableMidLOD);
			Util::AddTooltip("Use simpler grass shapes at medium distances. Can improve performance, with less fine detail. Requires compatible distant grass models.", tooltipFlags);
			changed |= Util::Widgets::Checkbox("Far grass LOD", &next.EnableFarLOD);
			Util::AddTooltip("Use the simplest available grass models farther away. Can improve performance, but distant grass may look flatter or less detailed. Requires compatible distant grass models.", tooltipFlags);
			changed |= Util::Widgets::SliderFloat("Middle LOD size", &next.MidLODPixelSize, 0.01f, 128.0f, "%.2f px", thresholdFlags);
			Util::AddTooltip("Use the middle grass mesh below this on-screen size. Higher values switch sooner, saving work but reducing detail.", tooltipFlags);
			next.FarLODPixelSize = std::min(next.FarLODPixelSize, next.MidLODPixelSize);
			changed |= Util::Widgets::SliderFloat("Far LOD size", &next.FarLODPixelSize, 0.01f, next.MidLODPixelSize, "%.2f px", thresholdFlags);
			Util::AddTooltip("Use the far grass mesh below this on-screen size. Higher values trade more distant detail for performance.", tooltipFlags);
			changed |= Util::Widgets::SliderFloat("LOD transition width", &next.MeshLODBandPixels, 0.01f, 32.0f, "%.2f px", thresholdFlags);
			Util::AddTooltip("Spread changes between grass meshes to soften transitions. Wider values may keep detailed meshes longer and add rendering work.", tooltipFlags);
			changed |= Util::Widgets::Checkbox("Optimize far billboard density", &next.FarLODOptimization);
			Util::AddTooltip("Give the farthest grass its own density and minimum size on screen. Lower density can improve performance; keeping smaller clumps visible farther away can cost more. Requires compatible far grass models and distant density reduction.", tooltipFlags);
			if (next.FarLODOptimization) {
				const float maximumMinimum = std::min(64.0f, std::nextafter(next.FarLODPixelSize, 0.0f));
				next.FarLODMinPixelSize = std::min(next.FarLODMinPixelSize, maximumMinimum);
				changed |= Util::Widgets::SliderFloat("Smallest far billboard size", &next.FarLODMinPixelSize, 0.0f, maximumMinimum, "%.2f px", thresholdFlags);
				Util::AddTooltip("Lower values keep smaller distant grass visible, extending coverage but potentially costing performance or causing shimmering. Draw distance still limits its reach.", tooltipFlags);
				changed |= Util::Widgets::SliderFloat("Far minimum density", &next.FarLODMinDensity, 0.0f, 1.0f, "%.3f", thresholdFlags);
				Util::AddTooltip("Set how much of the smallest visible distant grass to keep. Higher values fill gaps but cost more performance. Billboard coverage bias can reduce this amount.", tooltipFlags);
				next.FarLODMaxDensity = std::max(next.FarLODMaxDensity, next.FarLODMinDensity);
				changed |= Util::Widgets::SliderFloat("Far maximum density", &next.FarLODMaxDensity, next.FarLODMinDensity, 1.0f, "%.3f", thresholdFlags);
				Util::AddTooltip("Set how much grass to keep where the farthest grass models first appear. Lower values can improve performance but leave thinner coverage. Billboard coverage bias can reduce this amount.", tooltipFlags);
				changed |= Util::Widgets::SliderFloat("Billboard coverage bias", &next.FarLODCoverageBias, 0.0f, 1.0f, "%.2f", thresholdFlags);
				Util::AddTooltip("Thin grass more when its distant model is larger than the original. Higher values can improve performance but leave larger gaps. Zero disables this adjustment.", tooltipFlags);
			}
		}
	}

	if (panel("visibility")) {
		{
			changed |= Util::Widgets::Checkbox("Grass Hi-Z culling", &next.EnableOcclusionCulling);
			Util::AddTooltip("Skip grass hidden behind solid objects. Depending on the scene, this can save work or have a small performance cost.", tooltipFlags);
			if (advanced && next.EnableOcclusionCulling) {
				changed |= Util::Widgets::SliderFloat("Grass occlusion tolerance", &next.OcclusionBias, 0.0f, 0.05f, "%.4f");
				Util::AddTooltip("Make hidden-grass skipping more cautious. Increase this if grass disappears incorrectly; higher values keep more grass and can cost performance.", tooltipFlags);
			}
		}
	}

	if (changed) {
		std::string error;
		if (!SetSettings(next, error))
			logger::warn("Grass settings rejected: {}", error);
	}
	if (!renderer->IsRenderingAvailable())
		MenuUI::DetailNote("Native grass rendering is active; optimizations are unavailable.");
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
