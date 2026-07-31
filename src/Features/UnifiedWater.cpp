#include "UnifiedWater.h"

#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "Util.h"

#define I18N_KEY_PREFIX "feature.unified_water."

#include "RE/L/LoadingMenu.h"
#include "RE/M/MapMenu.h"
#include "RE/P/PlayerCharacter.h"

#include <algorithm>
#include <cmath>
#include <imgui_internal.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	UnifiedWater::Settings,
	UseOptimisedMeshes,
	UseOpenShadersDepthBehaviour,
	WaterTintColor,
	WaterTintStrength,
	DistantDepthFadeNearStrength,
	DistantDepthFadeFarStrength,
	DistantDepthFadeStart,
	DistantDepthFadeEnd,
	ShoreFeatherWidth,
	ShoreConfirmationCullDistance,
	DeepShoreSofteningStrength,
	DeepShoreSofteningStart)

namespace
{
	constexpr float kWaterTintColorMin = 0.0f;
	constexpr float kWaterTintColorMax = 1.0f;
	constexpr float kWaterTintStrengthMin = 0.0f;
	constexpr float kWaterTintStrengthMax = 1.0f;
	constexpr float kDistantDepthFadeStrengthMin = 0.0f;
	constexpr float kDistantDepthFadeStrengthMax = 1.0f;
	constexpr float kWorldCellSize = 4096.0f;
	constexpr float kDistantDepthFadeDistanceMin = 0.0f;
	constexpr float kDistantDepthFadeDistanceMax = kWorldCellSize * 16.0f;
	constexpr float kDistantDepthFadeMinimumRange = 1.0f;
	constexpr float kShoreFeatherWidthMin = 0.0f;
	constexpr float kShoreFeatherWidthMax = 64.0f;
	constexpr float kDeepShoreSofteningStrengthMin = 0.0f;
	constexpr float kDeepShoreSofteningStrengthMax = 1.0f;
	constexpr float kDeepShoreSofteningStartMin = 0.0f;
	constexpr float kDeepShoreSofteningStartMax = 1.0f;

	// Increment when Unified Water's generated flowmap or cache contract changes.
	constexpr char kUnifiedWaterDataRevision[] = "UnifiedWaterDataRevision=1";

	bool PersistLoadOrderHash(uint64_t a_hash);

	float ClampFiniteOrDefault(float a_value, float a_min, float a_max, float a_default)
	{
		if (!std::isfinite(a_value))
			return a_default;

		return std::clamp(a_value, a_min, a_max);
	}

	void SanitizeSettings(UnifiedWater::Settings& a_settings)
	{
		const UnifiedWater::Settings defaults{};
		a_settings.WaterTintColor.x = ClampFiniteOrDefault(
			a_settings.WaterTintColor.x,
			kWaterTintColorMin,
			kWaterTintColorMax,
			defaults.WaterTintColor.x);
		a_settings.WaterTintColor.y = ClampFiniteOrDefault(
			a_settings.WaterTintColor.y,
			kWaterTintColorMin,
			kWaterTintColorMax,
			defaults.WaterTintColor.y);
		a_settings.WaterTintColor.z = ClampFiniteOrDefault(
			a_settings.WaterTintColor.z,
			kWaterTintColorMin,
			kWaterTintColorMax,
			defaults.WaterTintColor.z);
		a_settings.WaterTintStrength = ClampFiniteOrDefault(
			a_settings.WaterTintStrength,
			kWaterTintStrengthMin,
			kWaterTintStrengthMax,
			defaults.WaterTintStrength);
		a_settings.DistantDepthFadeNearStrength = ClampFiniteOrDefault(
			a_settings.DistantDepthFadeNearStrength,
			kDistantDepthFadeStrengthMin,
			kDistantDepthFadeStrengthMax,
			defaults.DistantDepthFadeNearStrength);
		a_settings.DistantDepthFadeFarStrength = ClampFiniteOrDefault(
			a_settings.DistantDepthFadeFarStrength,
			kDistantDepthFadeStrengthMin,
			kDistantDepthFadeStrengthMax,
			defaults.DistantDepthFadeFarStrength);
		a_settings.DistantDepthFadeStart = ClampFiniteOrDefault(
			a_settings.DistantDepthFadeStart,
			kDistantDepthFadeDistanceMin,
			kDistantDepthFadeDistanceMax - kDistantDepthFadeMinimumRange,
			defaults.DistantDepthFadeStart);
		a_settings.DistantDepthFadeEnd = ClampFiniteOrDefault(
			a_settings.DistantDepthFadeEnd,
			kDistantDepthFadeDistanceMin + kDistantDepthFadeMinimumRange,
			kDistantDepthFadeDistanceMax,
			defaults.DistantDepthFadeEnd);

		if (a_settings.DistantDepthFadeEnd < a_settings.DistantDepthFadeStart + kDistantDepthFadeMinimumRange)
			a_settings.DistantDepthFadeEnd = a_settings.DistantDepthFadeStart + kDistantDepthFadeMinimumRange;

		a_settings.ShoreFeatherWidth = ClampFiniteOrDefault(
			a_settings.ShoreFeatherWidth,
			kShoreFeatherWidthMin,
			kShoreFeatherWidthMax,
			defaults.ShoreFeatherWidth);
		a_settings.ShoreConfirmationCullDistance = ClampFiniteOrDefault(
			a_settings.ShoreConfirmationCullDistance,
			kDistantDepthFadeDistanceMin,
			kDistantDepthFadeDistanceMax,
			defaults.ShoreConfirmationCullDistance);
		a_settings.DeepShoreSofteningStrength = ClampFiniteOrDefault(
			a_settings.DeepShoreSofteningStrength,
			kDeepShoreSofteningStrengthMin,
			kDeepShoreSofteningStrengthMax,
			defaults.DeepShoreSofteningStrength);
		a_settings.DeepShoreSofteningStart = ClampFiniteOrDefault(
			a_settings.DeepShoreSofteningStart,
			kDeepShoreSofteningStartMin,
			kDeepShoreSofteningStartMax,
			defaults.DeepShoreSofteningStart);
	}

	bool IsInteriorCellActive()
	{
		const auto tes = RE::TES::GetSingleton();
		if (tes && tes->interiorCell)
			return true;

		// TES::interiorCell can lag behind during load transitions
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto cell = player ? player->GetParentCell() : nullptr;
		return cell && cell->IsInteriorCell();
	}

	bool IsShortBranch(const std::uint8_t opcode)
	{
		return opcode == 0xEB || (opcode >= 0x70 && opcode <= 0x7F);
	}

	bool IsNearConditionalBranch(const std::uint8_t first, const std::uint8_t second)
	{
		return first == 0x0F && second >= 0x80 && second <= 0x8F;
	}

	void PatchBranchToUnconditional(const std::uintptr_t address, const char* label)
	{
		const auto bytes = reinterpret_cast<const std::uint8_t*>(address);

		// Match the branch width in the loaded executable before patching
		if (IsShortBranch(bytes[0])) {
			REL::safe_write(address, &REL::JMP8, 1);
			logger::debug("[Unified Water] Patched short branch for {} at {:X}", label, address);
			return;
		}

		if (IsNearConditionalBranch(bytes[0], bytes[1])) {
			// Preserve the existing rel32 target when replacing a near conditional jump
			constexpr std::uint8_t patch[2] = { REL::NOP, REL::JMP32 };
			REL::safe_write(address, patch, sizeof(patch));
			logger::debug("[Unified Water] Patched near branch for {} at {:X}", label, address);
			return;
		}

		logger::error("[Unified Water] Skipping {} patch at {:X}: unexpected branch bytes {:02X} {:02X}", label, address, bytes[0], bytes[1]);
	}

}

void UnifiedWater::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings(settings);
}

void UnifiedWater::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
}

void UnifiedWater::RestoreDefaultSettings()
{
	settings = {};
}

void UnifiedWater::DrawSettings()
{
	SanitizeSettings(settings);

	ImGui::Checkbox(T(TKEY("use_optimised_meshes"), "Use Optimised Meshes"), &settings.UseOptimisedMeshes);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("use_optimised_meshes_tooltip"),
							  "Uses meshes with significantly lower tri-count for improved performance with no visual quality loss.\n"
							  "Will only affect newly created water - requires a change of location or game restart to take effect."));
	}

	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("water_appearance"), "Water Appearance"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3(
			T(TKEY("water_tint_color"), "Water Tint Color"),
			reinterpret_cast<float*>(&settings.WaterTintColor));
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("water_tint_color_tooltip"), "Selects the colour of the water tint."));

		ImGui::SliderFloat(
			T(TKEY("water_tint"), "Water Tint"),
			&settings.WaterTintStrength,
			kWaterTintStrengthMin,
			kWaterTintStrengthMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("water_tint_tooltip"), "Adjusts how strongly the selected colour appears in the water."));

		ImGui::TreePop();
	}

	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("shallow_water_depth_stabilization"), "Shallow Water Depth Stabilization"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::BeginDisabled(settings.UseOpenShadersDepthBehaviour);

		ImGui::SliderFloat(
			T(TKEY("near_strength"), "Near Strength"),
			&settings.DistantDepthFadeNearStrength,
			kDistantDepthFadeStrengthMin,
			kDistantDepthFadeStrengthMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("near_strength_tooltip"),
								  "Stabilization applied at and before Fade Start.\n"
								  "A small nonzero value keeps nearby shallow water readable without fully suppressing transparency."));
		}

		ImGui::SliderFloat(
			T(TKEY("far_strength"), "Far Strength"),
			&settings.DistantDepthFadeFarStrength,
			kDistantDepthFadeStrengthMin,
			kDistantDepthFadeStrengthMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("far_strength_tooltip"),
								  "Stabilization applied at and after Fade End.\n"
								  "Values between Fade Start and Fade End interpolate smoothly from Near Strength."));
		}

		if (ImGui::SliderFloat(
				T(TKEY("fade_start"), "Fade Start"),
				&settings.DistantDepthFadeStart,
				kDistantDepthFadeDistanceMin,
				kDistantDepthFadeDistanceMax - kDistantDepthFadeMinimumRange,
				"%.0f units",
				ImGuiSliderFlags_AlwaysClamp)) {
			settings.DistantDepthFadeEnd = std::max(
				settings.DistantDepthFadeEnd,
				settings.DistantDepthFadeStart + kDistantDepthFadeMinimumRange);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				T(TKEY("fade_start_tooltip"), "View distance where stabilization begins.\nOne exterior cell is %.0f units."),
				kWorldCellSize);
		}

		if (ImGui::SliderFloat(
				T(TKEY("fade_end"), "Fade End"),
				&settings.DistantDepthFadeEnd,
				kDistantDepthFadeDistanceMin + kDistantDepthFadeMinimumRange,
				kDistantDepthFadeDistanceMax,
				"%.0f units",
				ImGuiSliderFlags_AlwaysClamp)) {
			settings.DistantDepthFadeStart = std::min(
				settings.DistantDepthFadeStart,
				settings.DistantDepthFadeEnd - kDistantDepthFadeMinimumRange);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("fade_end_tooltip"),
								  "View distance where the selected stabilization strength is fully applied.\n"
								  "Start and end remain at least one unit apart."));
		}

		ImGui::Spacing();
		ImGui::SeparatorText(T(TKEY("shoreline"), "Shoreline"));

		ImGui::SliderFloat(
			T(TKEY("shore_feather_width"), "Shore Feather Width"),
			&settings.ShoreFeatherWidth,
			kShoreFeatherWidthMin,
			kShoreFeatherWidthMax,
			"%.1f px",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("shore_feather_width_tooltip"),
								  "Fades stabilization back to natural transparency only at detected shorelines.\n"
								  "Set to 0 to disable; water away from the shoreline is unchanged."));
		}

		ImGui::SliderFloat(
			T(TKEY("deep_shore_softening"), "Deep Shore Softening"),
			&settings.DeepShoreSofteningStrength,
			kDeepShoreSofteningStrengthMin,
			kDeepShoreSofteningStrengthMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("deep_shore_softening_tooltip"),
								  "Softens deeper-water shoreline transitions while preserving the detected edge.\n"
								  "Set to 0 to use the standard shoreline fade."));
		}

		ImGui::SliderFloat(
			T(TKEY("deep_shore_start"), "Deep Shore Start"),
			&settings.DeepShoreSofteningStart,
			kDeepShoreSofteningStartMin,
			kDeepShoreSofteningStartMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("deep_shore_start_tooltip"),
								  "Controls how deep water must be before extra shoreline softening begins.\n"
								  "Higher values leave more medium-depth water unchanged."));
		}

		ImGui::SliderFloat(
			T(TKEY("shore_confirmation_cull_distance"), "Shore Confirmation Cull Distance"),
			&settings.ShoreConfirmationCullDistance,
			kDistantDepthFadeDistanceMin,
			kDistantDepthFadeDistanceMax,
			"%.0f units",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("shore_confirmation_cull_distance_tooltip"),
								  "Stops extra shoreline confirmation samples beyond this view distance.\n"
								  "A lightweight fallback preserves distant edge blending; set to 0 for no distance culling."));
		}

		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("debug"), "Debug"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(
			T(TKEY("use_open_shaders_depth_behaviour"), "Use Open Shaders Depth Behaviour"),
			&settings.UseOpenShadersDepthBehaviour);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("use_open_shaders_depth_behaviour_tooltip"),
								  "Bypasses Community Shaders' distance-based depth stabilization and uses Open Shaders' unmodified\n"
								  "depth/refraction behaviour. Custom stabilization values are preserved and resume when disabled."));
		}

		ImGui::Spacing();

		if (ImGui::Button(T(TKEY("regenerate_flowmap"), "Regenerate Flowmap")) && flowmap) {
			if (flowmap->RegenerateAndLoadFlowmap())
				SetFlowmapTex();
		}

		if (ImGui::Button(T(TKEY("regenerate_caches"), "Regenerate Caches")) && waterCache)
			waterCache->RegenerateCaches();

		ImGui::TreePop();
	}
}

UnifiedWater::CommonBufferData UnifiedWater::GetCommonBufferData() const
{
	auto sanitizedSettings = settings;
	SanitizeSettings(sanitizedSettings);

	CommonBufferData data{};
	const float depthBehaviourScale = sanitizedSettings.UseOpenShadersDepthBehaviour ? 0.0f : 1.0f;
	data.DistantDepthFadeNearStrength = sanitizedSettings.DistantDepthFadeNearStrength * depthBehaviourScale;
	data.DistantDepthFadeFarStrength = sanitizedSettings.DistantDepthFadeFarStrength * depthBehaviourScale;
	data.DistantDepthFadeStart = sanitizedSettings.DistantDepthFadeStart;
	data.DistantDepthFadeEnd = sanitizedSettings.DistantDepthFadeEnd;
	data.WaterTintColor = sanitizedSettings.WaterTintColor;
	data.WaterTintStrength = sanitizedSettings.WaterTintStrength;
	data.ShoreFeatherWidth = sanitizedSettings.ShoreFeatherWidth;
	data.ShoreConfirmationCullDistance = sanitizedSettings.ShoreConfirmationCullDistance;
	data.DeepShoreSofteningStrength = sanitizedSettings.DeepShoreSofteningStrength;
	data.DeepShoreSofteningStart = sanitizedSettings.DeepShoreSofteningStart;
	return data;
}

void UnifiedWater::DrawOverlay()
{
	if (!waterCache || !waterCache->IsBuildRunning() && !waterCache->HasBuildFailed())
		return;

	const float scale = Util::GetUIScale();
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	const auto& style = ImGui::GetStyle();

	// Stack below shader compilation window if it's visible this frame
	float vOffset = 0.0f;
	if (auto* shaderWin = ImGui::FindWindowByName("ShaderCompilationInfo")) {
		if (shaderWin->Active) {
			vOffset = (shaderWin->Pos.y + shaderWin->Size.y) - pos + style.ItemSpacing.y;
		}
	}
	// Also stack below shader blocking overlay if visible
	if (auto* blockingWin = ImGui::FindWindowByName("ShaderBlockingInfo")) {
		if (blockingWin->Active) {
			float blockingBottom = (blockingWin->Pos.y + blockingWin->Size.y) - pos + style.ItemSpacing.y;
			if (blockingBottom > vOffset)
				vOffset = blockingBottom;
		}
	}

	const auto snapshot = waterCache->GetBuildProgressSnapshot();

	auto& themeSettings = Menu::GetSingleton()->GetTheme();

	if (waterCache->IsBuildRunning()) {
		auto progressTitle = T(TKEY("generating_water_cache"), "Generating Water Cache:");
		auto percent = static_cast<float>(snapshot.completed) / static_cast<float>(snapshot.total);
		auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", snapshot.completed, snapshot.total, 100 * percent);

		ImGui::SetNextWindowPos(ImVec2(pos, pos + vOffset));
		if (!ImGui::Begin("UWCacheCreationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextUnformatted(progressTitle);
		ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());

		ImGui::End();
	} else if (waterCache->HasBuildFailed()) {
		ImGui::SetNextWindowPos(ImVec2(pos, pos + vOffset));
		if (!ImGui::Begin("UWCacheCreationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}

		if (snapshot.failed > 0) {
			ImGui::TextColored(themeSettings.StatusPalette.Error, T("feature.unified_water.error_water_cache_generation_failed_for_worldspaces_check", "ERROR: Water cache generation failed for %d WorldSpaces. Check installation and CommunityShaders.log"), snapshot.failed);
		} else {
			ImGui::TextColored(
				themeSettings.StatusPalette.Error,
				"%s",
				T(TKEY("error_water_cache_publication_failed"), "ERROR: Generated water caches could not be loaded completely. Check CommunityShaders.log"));
		}

		ImGui::End();
	}
}

bool UnifiedWater::IsOverlayVisible() const
{
	return waterCache && (waterCache->IsBuildRunning() || waterCache->HasBuildFailed());
}

void UnifiedWater::DataLoaded()
{
	auto args = RE::BSModelDB::DBTraits::ArgsType();
	args.unk8 = false;
	args.unkA = false;
	args.postProcess = false;
	RE::NiPointer<RE::NiNode> nif;

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\watermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		logger::error("[Unified Water] Failed to load water mesh");
		return;
	}
	if (!nif || nif->GetChildren().empty() || !nif->GetChildren().front()->AsNode() || nif->GetChildren().front()->AsNode()->GetChildren().empty()) {
		logger::error("[Unified Water] Invalid water mesh hierarchy");
		return;
	}
	const auto waterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	if (!waterShape) {
		logger::error("[Unified Water] Water mesh does not contain valid TriShape");
		return;
	}
	waterMesh = RE::NiPointer(waterShape);
	logger::debug("[Unified Water] Water mesh loaded");

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\optimisedwatermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		logger::error("[Unified Water] Failed to load optimised water mesh");
		return;
	}
	if (!nif || nif->GetChildren().empty() || !nif->GetChildren().front()->AsNode() || nif->GetChildren().front()->AsNode()->GetChildren().empty()) {
		logger::error("[Unified Water] Invalid optimised water mesh hierarchy");
		return;
	}
	const auto optimisedWaterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	if (!optimisedWaterShape) {
		logger::error("[Unified Water] Optimised water mesh does not contain valid TriShape");
		return;
	}
	optimisedWaterMesh = RE::NiPointer(optimisedWaterShape);
	logger::debug("[Unified Water] Optimised water mesh loaded");

	flowmap = new Flowmap();
	waterCache = new WaterCache();

	uint64_t pendingLoadOrderHash = 0;
	bool persistLoadOrderHash = false;

	if (LoadOrderChanged(pendingLoadOrderHash)) {
		logger::info("[Unified Water] Load order or data revision changed, regenerating flowmap and caches");

		const bool flowmapRegenerated = flowmap->RegenerateAndLoadFlowmap();
		if (flowmapRegenerated)
			SetFlowmapTex();

		const bool cacheRegenerationStarted = waterCache->RegenerateCaches();
		persistLoadOrderHash = flowmapRegenerated && cacheRegenerationStarted;
	} else {
		if (flowmap->LoadOrGenerateFlowmap())
			SetFlowmapTex();

		waterCache->LoadOrGenerateCaches();
	}

	while (waterCache->IsBuildRunning()) {
		std::this_thread::sleep_for(100ms);
	}

	if (persistLoadOrderHash) {
		if (waterCache->HasBuildFailed()) {
			logger::warn("[Unified Water] Generated data is incomplete; retaining the previous load-order hash so regeneration retries next launch");
		} else if (!PersistLoadOrderHash(pendingLoadOrderHash)) {
			logger::warn("[Unified Water] Failed to persist the regenerated data hash; regeneration will retry next launch");
		}
	}

	if (!MenuOpenCloseEventHandler::Register()) {
		logger::warn("[Unified Water] MenuOpenCloseEventHandler registration failed");
	}
}

RE::BSEventNotifyControl UnifiedWater::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (!event)
		return RE::BSEventNotifyControl::kContinue;

	auto& singleton = globals::features::unifiedWater;

	if (event->menuName == RE::LoadingMenu::MENU_NAME && !event->opening) {
		// Some interiors keep exterior state alive until after the load screen closes
		singleton.UpdateWaterLODCull();
	} else if (event->menuName == RE::MapMenu::MENU_NAME) {
		// The world map renders exterior LOD even while the player is in an interior
		singleton.mapMenuOpen.store(event->opening, std::memory_order_release);
		singleton.UpdateWaterLODCull();
	}

	return RE::BSEventNotifyControl::kContinue;
}

bool UnifiedWater::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	static bool registered = false;

	// DataLoaded can run more than once on some reload paths
	if (registered)
		return true;

	const auto ui = globals::game::ui;
	if (!ui) {
		logger::error("[Unified Water] UI event source not found");
		return false;
	}

	const auto source = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!source) {
		logger::error("[Unified Water] MenuOpenCloseEvent source not found");
		return false;
	}

	source->AddEventSink(&singleton);
	registered = true;
	logger::info("[Unified Water] Registered MenuOpenCloseEventHandler");
	return true;
}

namespace
{
	// Allow reads and writes to coexist with antivirus or indexing handles. Short
	// retries handle transient contention without delaying startup indefinitely.
	constexpr int kShareRetries = 3;
	constexpr DWORD kShareRetryDelayMs = 50;

	std::filesystem::path GetLoadOrderHashPath()
	{
		return Util::PathHelpers::GetCommunityShaderPath() / "UWLoadOrder.hash";
	}

	bool ReadHashFile(const std::filesystem::path& path, uint64_t& outHash)
	{
		for (int attempt = 0; attempt < kShareRetries; ++attempt) {
			winrt::file_handle handle{ CreateFileW(path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
			if (handle) {
				DWORD bytesRead = 0;
				const bool ok = ReadFile(handle.get(), &outHash, sizeof(outHash), &bytesRead, nullptr) &&
				                bytesRead == sizeof(outHash);
				if (!ok)
					logger::warn("[Unified Water] '{}' exists but could not be fully read; treating as no persisted hash", path.string());
				return ok;
			}
			const DWORD err = GetLastError();
			if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
				return false;
			if ((err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION) && attempt + 1 < kShareRetries) {
				Sleep(kShareRetryDelayMs);
				continue;
			}
			logger::warn("[Unified Water] Failed to open '{}' for reading (error {})", path.string(), err);
			return false;
		}
		return false;
	}

	bool WriteHashFile(const std::filesystem::path& path, uint64_t hash)
	{
		for (int attempt = 0; attempt < kShareRetries; ++attempt) {
			winrt::file_handle handle{ CreateFileW(path.c_str(), GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
			if (handle) {
				DWORD bytesWritten = 0;
				const bool ok = WriteFile(handle.get(), &hash, sizeof(hash), &bytesWritten, nullptr) &&
				                bytesWritten == sizeof(hash);
				if (!ok)
					logger::error("[Unified Water] Failed to persist load-order hash to '{}'; cache will regenerate again next launch", path.string());
				return ok;
			}
			const DWORD err = GetLastError();
			if ((err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION) && attempt + 1 < kShareRetries) {
				Sleep(kShareRetryDelayMs);
				continue;
			}
			logger::error("[Unified Water] Failed to open '{}' for writing (error {}); cache will regenerate again next launch", path.string(), err);
			return false;
		}
		return false;
	}

	bool PersistLoadOrderHash(const uint64_t hash)
	{
		const auto path = GetLoadOrderHashPath();

		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		if (error) {
			logger::error("[Unified Water] Failed to create load-order hash directory '{}': {}; regeneration will retry next launch",
				path.parent_path().string(), error.message());
			return false;
		}

		return WriteHashFile(path, hash);
	}
}

bool UnifiedWater::LoadOrderChanged(uint64_t& a_hash)
{
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler)
		return false;

	a_hash = 14695981039346656037ull;

	auto addBytes = [&](const unsigned char* bytes) {
		for (auto p = bytes; *p; ++p) {
			a_hash ^= *p;
			a_hash *= 1099511628211ull;
		}
	};

	auto addToHash = [&](const RE::TESFile* file) {
		if (!file || !file->fileName)
			return;
		addBytes(reinterpret_cast<const unsigned char*>(file->fileName));
	};

	if (const auto mods = dataHandler->GetLoadedMods()) {
		const uint32_t count = dataHandler->GetLoadedModCount();
		for (uint32_t i = 0, n = count; i < n; ++i)
			addToHash(mods[i]);
	}

	if (const auto lightMods = dataHandler->GetLoadedLightMods()) {
		const uint32_t count = dataHandler->GetLoadedLightModCount();
		for (uint32_t i = 0, n = count; i < n; ++i)
			addToHash(lightMods[i]);
	}

	addBytes(reinterpret_cast<const unsigned char*>(kUnifiedWaterDataRevision));

	const auto path = GetLoadOrderHashPath();

	uint64_t existingHash = 0;
	ReadHashFile(path, existingHash);

	const bool changed = a_hash != existingHash;
	logger::debug("[Unified Water] Load order hash: computed={:#x} persisted={:#x} changed={}", a_hash, existingHash, changed);

	return changed;
}

void UnifiedWater::SetFlowmapTex() const
{
	RE::NiPointer<RE::NiSourceTexture> tex;
	if (!flowmap->TryGetFlowmap(tex))
		return;

	if (!gFlowMapSourceTex || !gFlowMapSize) {
		logger::error("[Unified Water] Global pointers not initialized");
		return;
	}

	*gFlowMapSourceTex = tex;
	*gFlowMapSize = flowmap->GetWidth();

	logger::debug("[Unified Water] [Flowmap] Texture set");
}

void UnifiedWater::PostPostLoad()
{
	stl::detour_thunk<TES_SetWorldSpace>(REL::RelocationID(13170, 13315));
	stl::detour_thunk<TES_DestroySkyCell>(REL::RelocationID(20029, 20463));

	stl::write_thunk_call<TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams>(REL::RelocationID(31388, 32179).address() + REL::Relocate(0x360, 0x3BC));
	stl::write_vfunc<0x4, BSWaterShaderMaterial_ComputeCRC32>(RE::VTABLE_BSWaterShaderMaterial[0]);

	stl::detour_thunk<BGSTerrainBlock_Attach>(REL::RelocationID(30934, 31737));

	// Skip iterating attached meshes and calling TESWaterSystem::AddLODWater, this is handled in Attach now
	const auto addLoopOffset = REL::RelocationID(30934, 31737).address() + REL::Relocate(0x109, 0x109);
	const auto addLoopOffset2 = REL::RelocationID(30978, 31751).address() + REL::Relocate(0x54, 0xEA);
	PatchBranchToUnconditional(addLoopOffset, "attached mesh add loop");
	PatchBranchToUnconditional(addLoopOffset2, "LOD water add loop");

	stl::detour_thunk<BGSTerrainBlock_Detach>(REL::RelocationID(30936, 31739));

	stl::detour_thunk<BGSTerrainNode_UpdateWaterMeshSubVisibility>(REL::RelocationID(31059, 31846));

	stl::detour_thunk<TESWaterSystem_UpdateDisplacementMeshPosition>(REL::RelocationID(31384, 32175));

	stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);

	// Patch out the code compute shader calls that write to the flow map in Main::RenderWaterEffects
	REL::safe_fill(REL::RelocationID(35561, 36560).address() + REL::Relocate(0x1B7, 0x1F7), REL::NOP, 5);
	REL::safe_fill(REL::RelocationID(35561, 36560).address() + REL::Relocate(0x1EA, 0x22A), REL::NOP, 5);
	REL::safe_fill(REL::RelocationID(35561, 36560).address() + REL::Relocate(0x202, 0x242), REL::NOP, 5);

	gWaterLOD = reinterpret_cast<RE::NiNode**>(REL::RelocationID(516171, 402322).address());
	gFlowMapSize = reinterpret_cast<int32_t*>(REL::RelocationID(527644, 414596).address());
	gFlowMapSourceTex = reinterpret_cast<RE::NiPointer<RE::NiSourceTexture>*>(REL::RelocationID(527694, 414616).address());
	gDisplacementCellTexCoordOffset = reinterpret_cast<float4*>(REL::RelocationID(528184, 415129).address());
	gDisplacementMeshPos = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(516235, 402400).address());
	gDisplacementMeshFlowCellOffset = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(528164, 415109).address());

	logger::info("[Unified Water] Installed hooks");
}

void UnifiedWater::TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams::thunk(RE::TESWaterForm* form, RE::BSWaterShaderMaterial* material)
{
	// The game prefills the material and hashes its contents, it uses this hash to check if there is an existing identical material and swaps
	// to using that material if so.
	// Problem is it does not include all data from the form, especially normal textures which can cause problems with existing materials
	// having their textures swapped out.
	// This func hash the texture names and temporarily stashes them in a ptr slot, this is added to the hash in ComputeCRC and zeroed back out again
	func(form, material);

	uint32_t hash = 2166136261u;
	auto addStrToHash = [&](const char* str) {
		for (auto p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
			hash ^= *p;
			hash *= 16777619u;
		}
	};

	addStrToHash(form->noiseTextures[0].textureName.c_str());
	addStrToHash(form->noiseTextures[1].textureName.c_str());
	addStrToHash(form->noiseTextures[2].textureName.c_str());
	addStrToHash(form->noiseTextures[3].textureName.c_str());
	uintptr_t bits = hash;
	std::memcpy(&material->normalTexture1, &bits, sizeof(uintptr_t));
}

int32_t UnifiedWater::BSWaterShaderMaterial_ComputeCRC32::thunk(RE::BSWaterShaderMaterial* material, uint32_t srcHash)
{
	srcHash ^= static_cast<uint32_t>(reinterpret_cast<uint64_t>(material->normalTexture1.get())) + (srcHash << 6) + (srcHash >> 2);
	constexpr auto zero = static_cast<uintptr_t>(0);
	std::memcpy(&material->normalTexture1, &zero, sizeof(uintptr_t));
	return func(material, srcHash);
}

bool UnifiedWater::IsExteriorWorldspaceActive() const
{
	// Interior cells may still inherit stale exterior worldspace state during transitions
	return exteriorWorldspaceActive.load(std::memory_order_acquire) && !IsInteriorCellActive();
}

void UnifiedWater::UpdateWaterLODCull() const
{
	// Only hide UW's generated LOD root, preserving child tile cull flags
	if (gWaterLOD && *gWaterLOD) {
		const bool cull = !IsExteriorWorldspaceActive() && !mapMenuOpen.load(std::memory_order_acquire);
		if ((*gWaterLOD)->GetAppCulled() != cull) {
			(*gWaterLOD)->SetAppCulled(cull);
		}
	}
}

void UnifiedWater::TES_SetWorldSpace::thunk(RE::TES* tes, RE::TESWorldSpace* worldSpace, bool isExterior)
{
	func(tes, worldSpace, isExterior);

	auto& singleton = globals::features::unifiedWater;
	singleton.exteriorWorldspaceActive.store(worldSpace && isExterior, std::memory_order_release);
	singleton.waterCache->SetCurrentWorldSpace(worldSpace);
	singleton.UpdateWaterLODCull();
}

void UnifiedWater::TES_DestroySkyCell::thunk(RE::TES* tes)
{
	func(tes);

	auto& singleton = globals::features::unifiedWater;
	singleton.exteriorWorldspaceActive.store(false, std::memory_order_release);
	singleton.waterCache->SetCurrentWorldSpace(nullptr);
	singleton.UpdateWaterLODCull();
}

void UnifiedWater::BGSTerrainNode_UpdateWaterMeshSubVisibility::thunk(const RE::BGSTerrainNode* node, RE::BSMultiBoundNode* waterParent)
{
	if (!node || !waterParent)
		return;

	if (node->GetLODLevel() != 4)
		return;

	const auto tes = globals::game::tes;
	if (!tes || !tes->gridCells)
		return;

	const auto& gridCells = tes->gridCells;

	const int32_t offsetX = tes->currentGridX - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t offsetY = tes->currentGridY - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t length = static_cast<int32_t>(gridCells->length);

	for (const auto& child : waterParent->GetChildren()) {
		if (!child)
			continue;

		int32_t x, y;
		Util::WorldToCell(child->world.translate, x, y);

		x -= offsetX;
		y -= offsetY;

		bool cull = false;
		if (x >= 0 && y >= 0 && x < length && y < length) {
			if (const auto cell = gridCells->GetCell(x, y); cell && cell->cellState.any(RE::TESObjectCELL::CellState::kAttached, static_cast<RE::TESObjectCELL::CellState>(6))) {
				// Keep LOD visible when a loaded dry cell has no active water to replace it
				cull = cell->cellFlags.any(RE::TESObjectCELL::Flag::kHasWater);
			}
		}

		child->SetAppCulled(cull);
	}
}

void UnifiedWater::BGSTerrainBlock_Attach::thunk(RE::BGSTerrainBlock* block)
{
	const auto waterSystem = RE::TESWaterSystem::GetSingleton();
	const auto& singleton = globals::features::unifiedWater;

	std::vector<std::pair<RE::BSTriShape*, const WaterCache::Instruction*>> built;
	bool attaching = false;
	RE::NiPointer<RE::BSMultiBoundNode> water;
	// Keep the runtime cache alive while built retains pointers into its instruction storage.
	WaterCache::InstructionResult instructionResult;

	if (block && block->loaded && !block->attached && block->chunk && block->water) {
		// Keep terrain water alive while moving it out of its owning node
		water = RE::NiPointer<RE::BSMultiBoundNode>(block->water);
		block->chunk->DetachChild2(water.get());
		water->local.translate = block->chunk->local.translate;

		RE::NiUpdateData updateData;
		water->UpdateUpwardPass(updateData);

		const auto node = block->node;
		const auto lodLevel = node->GetLODLevel();
		const auto worldSpace = block->node->manager->worldSpace;

		instructionResult = singleton.waterCache->GetInstructions(worldSpace, lodLevel, node->baseCellX, node->baseCellY);
		const auto instructions = instructionResult.instructions;
		if (!instructions) {
			logger::warn("[Unified Water] No instructions found for {} chunk at {}, {}", worldSpace->GetFormEditorID(), node->baseCellX, node->baseCellY);
			// Reattach the saved node before falling back to vanilla
			block->chunk->AttachChild(water.get(), true);
			func(block);
			singleton.UpdateWaterLODCull();
			return;
		}

		bool hasInstruction = false;
		for (const auto& instruction : *instructions) {
			if (instruction.form.ptr) {
				hasInstruction = true;
				break;
			}
		}

		if (!hasInstruction) {
			// Empty instruction sets mean this block should stay vanilla
			block->chunk->AttachChild(water.get(), true);
			func(block);
			singleton.UpdateWaterLODCull();
			return;
		}

		built.reserve(instructions->size());

		// Detach by index because DetachChild mutates the child list
		auto count = water->GetChildren().size();
		while (count > 0) {
			const auto child = water->GetChildren()[count - 1];
			if (child) {
				waterSystem->RemoveWater(child.get());
			}
			water->DetachChildAt(--count);
		}

		for (auto& instruction : *instructions) {
			if (!instruction.form.ptr)
				continue;

			RE::NiCloningProcess cloningProcess;

			const auto targetShape = lodLevel > 4 || singleton.settings.UseOptimisedMeshes ? singleton.optimisedWaterMesh : singleton.waterMesh;
			RE::BSTriShape* shape = targetShape->CreateClone(cloningProcess)->AsTriShape();

			const auto posX = (instruction.x - node->baseCellX) * 4096.0f + instruction.size * 2048.0f;
			const auto posY = (instruction.y - node->baseCellY) * 4096.0f + instruction.size * 2048.0f;
			shape->local.scale = static_cast<float>(instruction.size);
			shape->local.translate = { posX, posY, instruction.waterHeight };

			water->AttachChild(shape, true);
			built.emplace_back(shape, &instruction);

			block->waterAttached = true;
		}

		if (built.empty()) {
			// If every UW tile failed to build, keep the original water visible
			block->chunk->AttachChild(water.get(), true);
		} else {
			attaching = true;
		}
	}

	func(block);

	if (!attaching || !block->waterAttached) {
		singleton.UpdateWaterLODCull();
		return;
	}

	// Reserve up front so AddWater can't reallocate waterObjects mid-loop and free a buffer other threads may be iterating.
	{
		RE::BSSpinLockGuard guard(waterSystem->lock);
		waterSystem->waterObjects.reserve(waterSystem->waterObjects.size() + static_cast<std::uint32_t>(built.size()));
	}

	for (auto& [shape, instruction] : built) {
		waterSystem->AddWater(shape, instruction->form.ptr, instruction->waterHeight, nullptr, true, false);

		if (const auto prop = shape->GetGeometryRuntimeData().shaderProperty.get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
			REX::EnumSet waterFlags = static_cast<RE::BSWaterShaderProperty::WaterFlag>(0b10000100);
			waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kUseCubemapReflections;
			waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kUseReflections;
			if (instruction->form.ptr->flags.any(RE::TESWaterForm::Flag::kEnableFlowmap))
				waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kEnableFlowmap;
			if (instruction->form.ptr->flags.any(RE::TESWaterForm::Flag::kBlendNormals))
				waterFlags |= RE::BSWaterShaderProperty::WaterFlag::kBlendNormals;
			waterShaderProp->waterFlags = waterFlags;
		}

		// Remove from WaterSystem, will manage it ourselves. Lock: our only direct edit to the shared list.
		{
			RE::BSSpinLockGuard guard(waterSystem->lock);
			if (!waterSystem->waterObjects.empty()) {
				waterSystem->waterObjects.pop_back();
			}
		}
	}

	if (auto waterLOD = singleton.gWaterLOD; waterLOD && *waterLOD) {
		(*waterLOD)->AttachChild(water.get(), true);
		singleton.UpdateWaterLODCull();
	} else if (block->chunk) {
		// If the LOD root is unavailable, keep ownership on the chunk
		block->chunk->AttachChild(water.get(), true);
		block->waterAttached = false;
	} else {
		block->water = nullptr;
		block->waterAttached = false;
	}
	waterSystem->Enable();
}

void UnifiedWater::BGSTerrainBlock_Detach::thunk(RE::BGSTerrainBlock* block)
{
	if (!block) {
		return;
	}

	RE::NiPointer<RE::BSMultiBoundNode> water(block->water);
	const bool wasWaterAttached = water && block->waterAttached;

	// Hide UW-managed water from vanilla detach so it does not delete it
	if (wasWaterAttached)
		block->water = nullptr;

	func(block);

	if (wasWaterAttached) {
		// Drop generated child tiles before parking the reusable water node
		auto count = water->GetChildren().size();
		while (count > 0) {
			water->DetachChildAt(--count);
		}

		if (auto waterLOD = globals::features::unifiedWater.gWaterLOD; waterLOD && *waterLOD)
			(*waterLOD)->DetachChild(water.get());

		// Park water under the detached chunk so block->water stays valid
		if (block->chunk) {
			block->chunk->AttachChild(water.get(), true);
			block->water = water.get();
		} else {
			block->water = nullptr;
		}

		block->waterAttached = false;
		globals::features::unifiedWater.UpdateWaterLODCull();
	}
}

void UnifiedWater::BSWaterShader_SetupGeometry::thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass)
{
	const auto& singleton = globals::features::unifiedWater;

	if (singleton.IsExteriorWorldspaceActive() && singleton.flowmap && pass && pass->geometry) {
		// ObjectUV.xyz below, xy contains width and height, z contains mesh scale
		// Previously flowmap size was in x, yz contained flowmap offset for water displacement mesh
		*singleton.gFlowMapSize = singleton.flowmap->GetWidth();                                            // ObjectUV.x
		singleton.gDisplacementMeshFlowCellOffset->x = static_cast<float>(singleton.flowmap->GetHeight());  // ObjectUV.y
		singleton.gDisplacementMeshFlowCellOffset->y = 1.0f - pass->geometry->local.scale;                  // ObjectUV.z (counters 1 - x in SetupGeometry)

		if (const auto prop = pass->geometry->GetGeometryRuntimeData().shaderProperty.get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
			int32_t x, y;
			Util::WorldToCell(pass->geometry->world.translate, x, y);
			// CellTexCoordOffset.xyzw below - applies to non-displacement water only
			// xy is world cell flowmap based (0,0 is corner of flow map), zw is world cell
			// Funky maths here to counter what's being done in SetupGeometry
			// Previously these values were relative to the 5x5 flow grid centered on the player
			waterShaderProp->flowX = x + singleton.flowmap->GetOffsetX();                                                                   // CellTexCoordOffset.x
			waterShaderProp->flowY = y + singleton.flowmap->GetOffsetY() + singleton.flowmap->GetWidth() - singleton.flowmap->GetHeight();  // CellTexCoordOffset.y
			waterShaderProp->cellX = x;                                                                                                     // CellTexCoordOffset.z
			waterShaderProp->cellY = y;                                                                                                     // CellTexCoordOffset.w
		}
	}

	func(waterShader, pass);
}

void UnifiedWater::TESWaterSystem_UpdateDisplacementMeshPosition::thunk(RE::TESWaterSystem* waterSystem)
{
	func(waterSystem);

	const auto& singleton = globals::features::unifiedWater;
	singleton.UpdateWaterLODCull();

	if (!singleton.flowmap || !singleton.IsExteriorWorldspaceActive())
		return;

	const float posX = singleton.gDisplacementMeshPos->x / 4096.0f;
	const float posY = singleton.gDisplacementMeshPos->y / 4096.0f;
	const float offsetX = static_cast<float>(singleton.flowmap->GetOffsetX());
	const float offsetY = static_cast<float>(singleton.flowmap->GetOffsetY());
	const float height = static_cast<float>(singleton.flowmap->GetHeight());

	// CellTexCoordOffset.xyzw below - applies to displacement water only
	// Previously the values were calculated relative to the 5x5 flow grid
	*singleton.gDisplacementCellTexCoordOffset = float4(posX + offsetX, height - (posY + offsetY), posX, 1 - posY);
}
#undef I18N_KEY_PREFIX
