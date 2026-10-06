#include "GrassOptimizations.h"

#include "Globals.h"
#include "GrassOptimizations/GrassBucketRenderer.h"
#include "Util.h"
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

void GrassOptimizations::DrawSettings()
{
	auto next = GetSettings();
	bool changed = ImGui::Checkbox("Grass optimizations", &next.Enabled);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Combine compatible grass across cells. Optional quality controls can reduce distant grass detail.");
	auto guard = Util::DisableGuard(!next.Enabled);
	changed |= ImGui::Checkbox("Combine grass across cells", &next.CrossCellBatching);
	changed |= ImGui::Checkbox("Skip grass outside the view", &next.FrustumCulling);
	changed |= ImGui::Checkbox("Reduce distant grass density", &next.DensityReduction);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Thin small distant grass to save rendering work. Off preserves density; separate visibility and distance controls still apply.");
	if (next.DensityReduction) {
		changed |= ImGui::SliderFloat("Smallest grass size", &next.MinPixelSize, 0.0f, 64.0f, "%.1f px");
		next.FullDetailPixelSize = std::max(next.FullDetailPixelSize, next.MinPixelSize + 0.01f);
		changed |= ImGui::SliderFloat("Full-density grass size", &next.FullDetailPixelSize, next.MinPixelSize + 0.01f, 256.0f, "%.1f px");
		changed |= ImGui::SliderFloat("Minimum density", &next.MinDensity, 0.0f, 1.0f, "%.2f");
	}
	changed |= ImGui::SliderFloat("Distant mesh cost bias", &next.MeshCostBias, 0.0f, 1.0f, "%.2f");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Reduce the reach and density of expensive grass meshes in the distance. Zero treats all meshes equally.");
	changed |= ImGui::SliderFloat("Cost bias start distance", &next.CostBiasStartDistance, 0.0f, 20000.0f, "%.0f");
	changed |= ImGui::SliderFloat("Grass render distance", &next.RenderDistanceOverride, 0.0f, 100000.0f, "%.0f");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Zero uses the game's grass distance. Larger distances still depend on grass in loaded cells.");
	changed |= ImGui::SliderFloat("Distance fade start", &next.EdgeFadeStart, 0.0f, 1.0f, "%.2f");
	changed |= ImGui::SliderFloat("Skip nearly invisible grass", &next.InvisibleFadeCull, 0.0f, 1.0f, "%.3f");
	changed |= ImGui::SliderFloat("Simpler shading below", &next.SimpleShadingPixelSize, 0.0f, 32.0f, "%.1f px");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Use simpler lighting for very small grass. Zero keeps full shading everywhere.");
	changed |= ImGui::SliderFloat("Grass collision distance", &next.CollisionDistance, 0.0f, GrassPolicy::kMaxCollisionDistance, "%.0f units");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Fade grass collision with distance. Zero disables collision for optimized grass. Requires Grass Collision; its local collision area still applies.");
	changed |= ImGui::Checkbox("Use distant grass meshes", &next.EnableMeshLOD);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Use authored middle/far grass LOD meshes when available. Missing or incompatible meshes retain the full mesh.");
	if (next.EnableMeshLOD) {
		changed |= ImGui::Checkbox("Middle grass LOD", &next.EnableMidLOD);
		changed |= ImGui::Checkbox("Far grass LOD", &next.EnableFarLOD);
		changed |= ImGui::SliderFloat("Middle LOD size", &next.MidLODPixelSize, 0.01f, 128.0f, "%.1f px");
		next.FarLODPixelSize = std::min(next.FarLODPixelSize, next.MidLODPixelSize);
		changed |= ImGui::SliderFloat("Far LOD size", &next.FarLODPixelSize, 0.01f, next.MidLODPixelSize, "%.1f px");
		changed |= ImGui::SliderFloat("LOD transition width", &next.MeshLODBandPixels, 0.01f, 32.0f, "%.1f px");
	}
	{
		const bool available = IsGrassHiZAvailable();
		auto hiZGuard = Util::DisableGuard(!available);
		bool hiZ = available && next.EnableOcclusionCulling;
		if (ImGui::Checkbox("Grass Hi-Z culling", &hiZ)) {
			next.EnableOcclusionCulling = hiZ;
			changed = true;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip(available ? "Skip grass hidden behind solid objects. Compare on/off performance in the same build; it can cost more than it saves." :
										  "Unavailable while scene Hi-Z is selected. Use Advanced or Legacy scene culling to compare grass Hi-Z.");
		if (hiZ)
			changed |= ImGui::SliderFloat("Grass occlusion tolerance", &next.OcclusionBias, 0.0f, 0.05f, "%.4f");
	}
	if (changed) {
		if (!IsGrassHiZAvailable())
			next.EnableOcclusionCulling = false;
		std::string error;
		if (!SetSettings(next, error))
			logger::warn("Grass settings rejected: {}", error);
	}
	if (!IsHookInstalled())
		ImGui::TextUnformatted("Native grass rendering: draw hook unavailable.");
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
