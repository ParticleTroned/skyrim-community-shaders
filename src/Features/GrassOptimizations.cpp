#include "GrassOptimizations.h"

#include "Globals.h"
#include "GrassOptimizations/GrassBucketRenderer.h"
#include "Util.h"
#include "Utils/UI.h"
#include "VR.h"
#include "VRDepthCullingTemporal.h"

static_assert(static_cast<int>(VRDepthCullingTemporal::Mode::Hybrid) == GrassPolicy::kSceneHiZMode);

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

bool GrassOptimizations::IsEnabled() const { return GetSettings().Enabled; }
bool GrassOptimizations::IsHookInstalled() const { return renderer->IsHookInstalled(); }
bool GrassOptimizations::IsGrassHiZAvailable() const
{
	return GrassPolicy::OcclusionAllowed(globals::game::isVR,
		static_cast<int>(globals::features::vr.GetDepthCullingMode()));
}

bool GrassOptimizations::SetSettings(const GrassPolicy::Settings& requested, std::string& error)
{
	if (!requested.Valid()) {
		error = "Invalid grass settings: finite ordered pixel thresholds and bounded density, bias and distances are required";
		return false;
	}
	if (requested.EnableOcclusionCulling && !IsGrassHiZAvailable()) {
		error = "Grass Hi-Z is unavailable while scene Hi-Z culling is selected";
		return false;
	}
	{
		std::scoped_lock lock(settingsMutex);
		settings = requested;
	}
	return true;
}

void GrassOptimizations::SetEnabled(bool enabled)
{
	auto next = GetSettings();
	next.Enabled = enabled;
	if (!IsGrassHiZAvailable())
		next.EnableOcclusionCulling = false;
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
	if (!IsGrassHiZAvailable())
		next.EnableOcclusionCulling = false;
	std::string error;
	if (!SetSettings(next, error))
		logger::warn("Saved grass settings rejected: {}", error);
}
void GrassOptimizations::SaveSettings(json& saved) { saved = GetSettings(); }
void GrassOptimizations::RestoreDefaultSettings()
{
	GrassPolicy::Settings next;
	if (!IsGrassHiZAvailable())
		next.EnableOcclusionCulling = false;
	std::string error;
	if (!SetSettings(next, error))
		logger::warn("Grass defaults rejected: {}", error);
}
void GrassOptimizations::SetupResources() { renderer->SetupResources(); }
void GrassOptimizations::ClearShaderCache() { renderer->ClearShaderCache(); }
void GrassOptimizations::PostPostLoad() { renderer->InstallHooks(); }
void GrassOptimizations::PrepareGeometry(RE::BSRenderPass* pass) { renderer->PrepareGeometry(pass); }

std::pair<std::string, std::vector<std::string>> GrassOptimizations::GetFeatureSummary()
{
	return { "Combines compatible grass draws and removes grass outside the view.",
		{ "Independent distant density, fading and mesh cost controls", "Optional middle/far meshes and simpler distant shading", "Separate grass Hi-Z switch for performance comparisons" } };
}

void GrassOptimizations::DrawSettings() { DrawControls(true); }
void GrassOptimizations::DrawEssentialSettings() { DrawControls(false); }
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

void GrassOptimizations::DrawControls(bool advanced)
{
	constexpr auto tooltipFlags = ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled;
	auto next = GetSettings();
	bool changed = ImGui::Checkbox("Grass optimizations", &next.Enabled);
	Util::AddTooltip("Optimize grass drawing and visibility. Quality controls can trade grass density, range or detail for performance.", tooltipFlags);
	auto guard = Util::DisableGuard(!next.Enabled);
	if (advanced) {
		changed |= ImGui::Checkbox("Combine grass across cells", &next.CrossCellBatching);
		Util::AddTooltip("Group grass across loaded cells into fewer draws. Keeps its appearance and may improve performance.", tooltipFlags);
	}
	changed |= ImGui::Checkbox("Skip grass outside the view", &next.FrustumCulling);
	Util::AddTooltip("Skip grass outside your view to save rendering work without changing visible grass.", tooltipFlags);
	changed |= ImGui::Checkbox("Reduce distant grass density", &next.DensityReduction);
	Util::AddTooltip("Thin distant grass to save rendering work. Can improve performance but makes grass coverage sparser.", tooltipFlags);
	if (advanced && next.DensityReduction) {
		changed |= ImGui::SliderFloat("Smallest grass size", &next.MinPixelSize, 0.0f, 64.0f, "%.1f px");
		Util::AddTooltip("Remove grass smaller than this on screen. Higher values save rendering work but can leave sparse patches or shorten its visible reach.", tooltipFlags);
		next.FullDetailPixelSize = std::max(next.FullDetailPixelSize, next.MinPixelSize + 0.01f);
		changed |= ImGui::SliderFloat("Full-density grass size", &next.FullDetailPixelSize, next.MinPixelSize + 0.01f, 256.0f, "%.1f px");
		Util::AddTooltip("Grass above this on-screen size keeps full density. Higher values thin more grass, saving work at the cost of coverage.", tooltipFlags);
		changed |= ImGui::SliderFloat("Minimum density", &next.MinDensity, 0.0f, 1.0f, "%.2f");
		Util::AddTooltip("Set how much distant grass to keep. Higher values fill gaps but cost more; grass below the smallest-size cutoff is still removed.", tooltipFlags);
	}
	if (advanced) {
		changed |= ImGui::SliderFloat("Distant mesh cost bias", &next.MeshCostBias, 0.0f, 1.0f, "%.2f");
		Util::AddTooltip("Give complex grass meshes less distant reach and, with density reduction on, thinner coverage to save work. Zero disables this adjustment.", tooltipFlags);
		changed |= ImGui::SliderFloat("Cost bias start distance", &next.CostBiasStartDistance, 0.0f, 20000.0f, "%.0f");
		Util::AddTooltip("Choose where mesh cost bias starts reducing grass. Lower distances trade coverage for performance sooner; zero starts immediately.", tooltipFlags);
		changed |= ImGui::SliderFloat("Grass render distance", &next.RenderDistanceOverride, 0.0f, 100000.0f, "%.0f");
		Util::AddTooltip("Set how far grass is visible. Shorter distances save rendering work; zero uses the game's setting. Limited to loaded grass.", tooltipFlags);
		changed |= ImGui::SliderFloat("Distance fade start", &next.EdgeFadeStart, 0.0f, 1.0f, "%.2f");
		Util::AddTooltip("Choose where grass starts fading toward its distance limit. Lower values fade it sooner and more gradually; skipping faded grass can save work.", tooltipFlags);
		changed |= ImGui::SliderFloat("Skip nearly invisible grass", &next.InvisibleFadeCull, 0.0f, 1.0f, "%.3f");
		Util::AddTooltip("Stop drawing grass below this fade value. Higher values save rendering work but can make grass disappear more abruptly.", tooltipFlags);
		changed |= ImGui::SliderFloat("Simpler shading below", &next.SimpleShadingPixelSize, 0.0f, 32.0f, "%.1f px");
		Util::AddTooltip("Use simpler lighting on grass smaller than this on screen. Higher values save work but reduce shading detail; zero keeps full shading.", tooltipFlags);
		changed |= ImGui::SliderFloat("Grass collision distance", &next.CollisionDistance, 0.0f, GrassPolicy::kMaxCollisionDistance, "%.0f units");
		Util::AddTooltip("Limit how far optimized grass responds to collisions. Shorter distances save work; zero disables this bending. Requires Grass Collision and its local coverage.", tooltipFlags);
	}
	changed |= ImGui::Checkbox("Use distant grass meshes", &next.EnableMeshLOD);
	Util::AddTooltip("Use simpler distant meshes to save rendering work, with less detail. Requires compatible grass LOD meshes; missing ones keep full detail.", tooltipFlags);
	if (advanced && next.EnableMeshLOD) {
		changed |= ImGui::Checkbox("Middle grass LOD", &next.EnableMidLOD);
		Util::AddTooltip("Use simpler meshes for middle-distance grass, reducing detail and rendering cost. Requires compatible grass LOD meshes.", tooltipFlags);
		changed |= ImGui::Checkbox("Far grass LOD", &next.EnableFarLOD);
		Util::AddTooltip("Use simpler meshes for faraway grass, reducing distant detail and rendering cost. Requires compatible grass LOD meshes.", tooltipFlags);
		changed |= ImGui::SliderFloat("Middle LOD size", &next.MidLODPixelSize, 0.01f, 128.0f, "%.1f px");
		Util::AddTooltip("Use the middle grass mesh below this on-screen size. Higher values switch sooner, saving work but reducing detail.", tooltipFlags);
		next.FarLODPixelSize = std::min(next.FarLODPixelSize, next.MidLODPixelSize);
		changed |= ImGui::SliderFloat("Far LOD size", &next.FarLODPixelSize, 0.01f, next.MidLODPixelSize, "%.1f px");
		Util::AddTooltip("Use the far grass mesh below this on-screen size. Higher values trade more distant detail for performance.", tooltipFlags);
		changed |= ImGui::SliderFloat("LOD transition width", &next.MeshLODBandPixels, 0.01f, 32.0f, "%.1f px");
		Util::AddTooltip("Spread changes between grass meshes to soften transitions. Wider values may keep detailed meshes longer and add rendering work.", tooltipFlags);
	}
	{
		const bool available = IsGrassHiZAvailable();
		auto hiZGuard = Util::DisableGuard(!available);
		bool hiZ = available && next.EnableOcclusionCulling;
		if (ImGui::Checkbox("Grass Hi-Z culling", &hiZ)) {
			next.EnableOcclusionCulling = hiZ;
			changed = true;
		}
		Util::AddTooltip(available ? "Skip grass hidden behind solid objects. Depending on the scene, this can save work or have a small performance cost." :
									 "Skip hidden grass; some scenes have a small performance cost. Unavailable with scene Hi-Z; select Advanced or Legacy to enable it.",
			tooltipFlags);
		if (advanced && hiZ) {
			changed |= ImGui::SliderFloat("Grass occlusion tolerance", &next.OcclusionBias, 0.0f, 0.05f, "%.4f");
			Util::AddTooltip("Make hidden-grass skipping more cautious. Increase this if grass disappears incorrectly; higher values keep more grass and can cost performance.", tooltipFlags);
		}
	}
	if (changed) {
		if (!IsGrassHiZAvailable())
			next.EnableOcclusionCulling = false;
		std::string error;
		if (!SetSettings(next, error))
			logger::warn("Grass settings rejected: {}", error);
	}
	if (!renderer->IsRenderingAvailable())
		ImGui::TextUnformatted("Native grass rendering: optimizations are unavailable.");
}
#ifdef DEVBENCH_BRIDGE_ENABLED
void GrassOptimizations::SetDiagnosticsEnabled(bool enabled) { renderer->SetDiagnosticsEnabled(enabled); }
json GrassOptimizations::GetDiagnostics() const
{
	auto result = renderer->GetDiagnostics();
	const auto configured = GetSettings();
	result["loaded"] = loaded;
	result["enabled"] = configured.Enabled;
	result["settings"] = configured;
	result["grassHiZAvailable"] = IsGrassHiZAvailable();
	result["grassHiZRequested"] = configured.EnableOcclusionCulling;
	result["grassHiZEnabled"] = configured.Enabled && configured.EnableOcclusionCulling && IsGrassHiZAvailable();
	return result;
}
#endif
