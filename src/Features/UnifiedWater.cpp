#include "UnifiedWater.h"

#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "Util.h"

#define I18N_KEY_PREFIX "feature.unified_water."

#include "RE/L/LoadingMenu.h"
#include "RE/M/MapMenu.h"
#include "RE/N/NiIntegersExtraData.h"
#include "RE/P/PlayerCharacter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <imgui_internal.h>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	UnifiedWater::Settings,
	UseOptimisedMeshes)

static const RE::BSFixedString kGeneratedWaterTileExtraDataName = "CS_UWGeneratedWaterTile";

static bool IsChildWorldSpace(const RE::TESWorldSpace* ws)
{
	return ws && ws->parentWorld &&
	       ws->parentUseFlags.all(RE::TESWorldSpace::ParentUseFlag::kUseLODData);
}

static int64_t SteadyClockMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch())
	    .count();
}

static bool IsInteriorCellActive()
{
	const auto tes = RE::TES::GetSingleton();
	if (tes && tes->interiorCell)
		return true;

	// TES::interiorCell can lag behind during load transitions.
	const auto player = RE::PlayerCharacter::GetSingleton();
	const auto cell = player ? player->GetParentCell() : nullptr;
	return cell && cell->IsInteriorCell();
}

static bool IsShortBranch(const std::uint8_t opcode)
{
	return opcode == 0xEB || (opcode >= 0x70 && opcode <= 0x7F);
}

static bool IsNearConditionalBranch(const std::uint8_t first, const std::uint8_t second)
{
	return first == 0x0F && second >= 0x80 && second <= 0x8F;
}

static void PatchBranchToUnconditional(const std::uintptr_t address, const char* label)
{
	const auto bytes = reinterpret_cast<const std::uint8_t*>(address);

	if (IsShortBranch(bytes[0])) {
		REL::safe_write(address, &REL::JMP8, 1);
		logger::debug("[Unified Water] Patched short branch for {} at {:X}", label, address);
		return;
	}

	if (IsNearConditionalBranch(bytes[0], bytes[1])) {
		constexpr std::uint8_t patch[2] = { REL::NOP, REL::JMP32 };
		REL::safe_write(address, patch, sizeof(patch));
		logger::debug("[Unified Water] Patched near branch for {} at {:X}", label, address);
		return;
	}

	logger::error("[Unified Water] Skipping {} patch at {:X}: unexpected branch bytes {:02X} {:02X}", label, address, bytes[0], bytes[1]);
}

// Engine behavior: CellState value 6 is the transition/attached state.
static constexpr auto kTransitionAttachedCellState = static_cast<RE::TESObjectCELL::CellState>(6);

static void ClearWaterNodeChildren(RE::NiNode* node, RE::TESWaterSystem* waterSystem)
{
	if (!node)
		return;

	auto count = node->GetChildren().size();
	while (count > 0) {
		const auto child = node->GetChildren()[count - 1];
		if (const auto childNode = child ? child->AsNode() : nullptr)
			ClearWaterNodeChildren(childNode, waterSystem);

		if (child && waterSystem)
			waterSystem->RemoveWater(child.get());

		node->DetachChildAt(--count);
	}
}

static void DetachAllChildOccurrences(RE::NiNode* node, const RE::NiAVObject* childToDetach)
{
	if (!node || !childToDetach)
		return;

	auto count = node->GetChildren().size();
	while (count > 0) {
		const auto child = node->GetChildren()[count - 1];
		if (child.get() == childToDetach) {
			node->DetachChildAt(--count);
		} else {
			count--;
		}
	}
}

struct WaterPositionKey
{
	int32_t x = 0;
	int32_t y = 0;
	int32_t z = 0;
	int32_t scale = 0;
	RE::FormID waterForm = 0;
	std::uint8_t waterFlags = 0;
};

struct GeneratedTileKey
{
	int32_t x = 0;
	int32_t y = 0;
	uint32_t size = 0;
	RE::FormID waterForm = 0;
	std::uint8_t waterFlags = 0;
};

static bool operator==(const GeneratedTileKey& lhs, const GeneratedTileKey& rhs)
{
	return lhs.x == rhs.x &&
	       lhs.y == rhs.y &&
	       lhs.size == rhs.size &&
	       lhs.waterForm == rhs.waterForm &&
	       lhs.waterFlags == rhs.waterFlags;
}

struct GeneratedTileKeyHash
{
	size_t operator()(const GeneratedTileKey& key) const noexcept
	{
		size_t hash = std::hash<int32_t>{}(key.x);
		hash ^= std::hash<int32_t>{}(key.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<uint32_t>{}(key.size) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<RE::FormID>{}(key.waterForm) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<std::uint8_t>{}(key.waterFlags) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		return hash;
	}
};

static GeneratedTileKey GetGeneratedTileKey(const UnifiedWater::WaterTilePlacement& placement)
{
	return {
		placement.x,
		placement.y,
		placement.size,
		placement.waterForm,
		placement.waterFlags,
	};
}

static bool ShouldUseOptimisedGeneratedWaterMesh(const WaterCache::Instruction& instruction, const bool useOptimisedMeshes)
{
	if (!useOptimisedMeshes)
		return false;

	// LOD4 tiles still need per-cell culling. For coarser LOD, partial tiles are
	// usually shoreline/boundary tiles, so keep them on the full mesh.
	return instruction.lodLevel <= 4 || instruction.size == instruction.lodLevel;
}

static int32_t QuantizeWaterPosition(float value)
{
	return static_cast<int32_t>(std::lround(value));
}

static RE::NiIntegersExtraData* GetGeneratedWaterTileMarker(const RE::NiAVObject* object)
{
	const auto extraData = object ? object->GetExtraData(kGeneratedWaterTileExtraDataName) : nullptr;
	if (!extraData)
		return nullptr;

	static const REL::Relocation<const RE::NiRTTI*> niIntegersExtraDataRTTI{ RE::NiIntegersExtraData::Ni_RTTI };
	if (extraData->GetRTTI() != niIntegersExtraDataRTTI.get())
		return nullptr;

	return static_cast<RE::NiIntegersExtraData*>(extraData);
}

static bool TryGetGeneratedWaterTileMarker(const RE::NiAVObject* object, UnifiedWater::WaterTilePlacement& placement)
{
	const auto* marker = GetGeneratedWaterTileMarker(object);
	if (!marker || !marker->value || marker->size < 3)
		return false;

	const auto size = marker->value[2];
	if (size <= 0)
		return false;

	placement.x = marker->value[0];
	placement.y = marker->value[1];
	placement.size = static_cast<uint32_t>(size);
	placement.waterForm = 0;
	placement.waterFlags = 0;
	return true;
}

static void SetGeneratedWaterTileMarker(RE::NiAVObject* object, const UnifiedWater::WaterTilePlacement& placement)
{
	if (!object)
		return;

	if (auto* marker = GetGeneratedWaterTileMarker(object);
		marker && marker->value && marker->size >= 3) {
		marker->value[0] = placement.x;
		marker->value[1] = placement.y;
		marker->value[2] = static_cast<std::int32_t>(placement.size);
		return;
	}

	if (auto* staleMarker = object->GetExtraData(kGeneratedWaterTileExtraDataName)) {
		object->RemoveExtraData(staleMarker);
	}

	if (auto* marker = RE::NiIntegersExtraData::Create(
			kGeneratedWaterTileExtraDataName,
			{ placement.x, placement.y, static_cast<std::int32_t>(placement.size) })) {
		object->AddExtraData(marker);
	}
}

static void RemoveGeneratedWaterTileMarker(RE::NiAVObject* object)
{
	if (!object)
		return;

	if (auto* marker = object->GetExtraData(kGeneratedWaterTileExtraDataName)) {
		object->RemoveExtraData(marker);
	}
}

static WaterPositionKey GetWaterPositionKey(const RE::TESWaterObject* waterObject)
{
	if (!waterObject || !waterObject->shape)
		return {};

	const auto object = waterObject->shape.get();
	return {
		QuantizeWaterPosition(object->world.translate.x),
		QuantizeWaterPosition(object->world.translate.y),
		QuantizeWaterPosition(object->world.translate.z),
		QuantizeWaterPosition(object->world.scale * 1000.0f),
		waterObject->waterType ? waterObject->waterType->formID : 0,
		waterObject->flags,
	};
}

static bool IsChildOfNode(const RE::NiAVObject* object, const RE::NiNode* root)
{
	if (!object || !root)
		return false;

	for (auto parent = object->parent; parent; parent = parent->parent) {
		if (parent == root)
			return true;
	}

	return object == root;
}

static bool RemoveWaterObjectForShape(RE::TESWaterSystem* waterSystem, const RE::BSTriShape* shape, WaterPositionKey* removedKey = nullptr)
{
	if (!waterSystem || !shape)
		return false;

	RE::BSSpinLockGuard guard(waterSystem->lock);
	for (auto it = waterSystem->waterObjects.begin(); it != waterSystem->waterObjects.end(); ++it) {
		const auto waterObject = it->get();
		if (waterObject && waterObject->shape.get() == shape) {
			if (removedKey)
				*removedKey = GetWaterPositionKey(waterObject);
			waterSystem->waterObjects.erase(it);
			return true;
		}
	}

	return false;
}

static bool ShouldCullAtCell(
	const RE::TES* tes,
	int32_t cellX,
	int32_t cellY,
	bool* isInGrid = nullptr,
	bool* isAttached = nullptr)
{
	if (isInGrid)
		*isInGrid = false;
	if (isAttached)
		*isAttached = false;
	if (!tes || !tes->gridCells)
		return false;

	const auto& gridCells = tes->gridCells;
	const int32_t offsetX = tes->currentGridX - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t offsetY = tes->currentGridY - static_cast<int32_t>(gridCells->length >> 1);
	const int32_t length = static_cast<int32_t>(gridCells->length);

	const int32_t x = cellX - offsetX;
	const int32_t y = cellY - offsetY;
	if (x < 0 || y < 0 || x >= length || y >= length)
		return false;

	if (isInGrid)
		*isInGrid = true;

	if (const auto cell = gridCells->GetCell(x, y)) {
		const bool attached = cell->cellState.any(RE::TESObjectCELL::CellState::kAttached, kTransitionAttachedCellState);
		if (isAttached)
			*isAttached = attached;

		// Keep LOD visible when a loaded dry cell has no active water to replace it.
		return attached && cell->cellFlags.any(RE::TESObjectCELL::Flag::kHasWater);
	}

	return false;
}

struct CullCompletionState
{
	bool foundAttachedCell = false;
	bool hasPotentiallyAttachableChild = false;
	bool shouldCull = false;

	bool IsComplete() const
	{
		return foundAttachedCell && !hasPotentiallyAttachableChild;
	}
};

static CullCompletionState ShouldCullTileFootprint(
	const RE::TES* tes,
	const int32_t originX,
	const int32_t originY,
	const uint32_t tileSize)
{
	CullCompletionState state;
	const auto size = static_cast<int32_t>(std::max(tileSize, 1u));

	for (int32_t y = originY; y < originY + size; ++y) {
		for (int32_t x = originX; x < originX + size; ++x) {
			bool isInGrid = false;
			bool isAttached = false;
			const bool cull = ShouldCullAtCell(tes, x, y, &isInGrid, &isAttached);

			state.shouldCull = state.shouldCull || cull;
			state.foundAttachedCell = state.foundAttachedCell || isAttached;
			state.hasPotentiallyAttachableChild = state.hasPotentiallyAttachableChild || (isInGrid && !isAttached);
		}
	}

	return state;
}

// Cull waterParent children using tes->gridCells attachment state.
// Pass tes explicitly when globals::game::tes is not ready (e.g., TES_SetWorldSpace).
static CullCompletionState CullWaterParentByGridCells(RE::NiNode* waterParent, RE::TES* tes = nullptr)
{
	if (!tes)
		tes = globals::game::tes;
	if (!tes || !waterParent)
		return {};

	CullCompletionState state;

	for (const auto& child : waterParent->GetChildren()) {
		if (!child)
			continue;

		CullCompletionState childState;
		UnifiedWater::WaterTilePlacement placement;
		if (TryGetGeneratedWaterTileMarker(child.get(), placement) ||
			globals::features::unifiedWater.TryGetGeneratedWaterTile(child.get(), placement)) {
			childState = ShouldCullTileFootprint(tes, placement.x, placement.y, placement.size);
		} else {
			int32_t x, y;
			Util::WorldToCell(child->world.translate, x, y);
			bool isInGrid = false;
			bool isAttached = false;
			childState.shouldCull = ShouldCullAtCell(tes, x, y, &isInGrid, &isAttached);
			childState.foundAttachedCell = isAttached;
			childState.hasPotentiallyAttachableChild = isInGrid && !isAttached;
		}

		state.shouldCull = state.shouldCull || childState.shouldCull;
		state.foundAttachedCell = state.foundAttachedCell || childState.foundAttachedCell;
		state.hasPotentiallyAttachableChild = state.hasPotentiallyAttachableChild || childState.hasPotentiallyAttachableChild;
		child->SetAppCulled(childState.shouldCull);
	}

	return state;
}

// Cull all tiles under every water LOD parent.
static bool CullAllWaterLODParents(RE::NiNode* waterLOD, RE::TES* tes = nullptr)
{
	if (!waterLOD)
		return false;

	CullCompletionState aggregate;

	for (const auto& waterParentPtr : waterLOD->GetChildren()) {
		if (!waterParentPtr)
			continue;
		const auto waterParent = waterParentPtr->AsNode();
		if (!waterParent)
			continue;
		const auto state = CullWaterParentByGridCells(waterParent, tes);
		aggregate.foundAttachedCell = aggregate.foundAttachedCell || state.foundAttachedCell;
		aggregate.hasPotentiallyAttachableChild = aggregate.hasPotentiallyAttachableChild || state.hasPotentiallyAttachableChild;
	}

	return aggregate.IsComplete();
}

void UnifiedWater::TryCompleteDeferredChildWorldspaceCull(RE::TES* tes)
{
	if (!pendingChildWsCull.load(std::memory_order_acquire) ||
		!IsChildWorldSpace(currentPlayerWorldSpace.load(std::memory_order_acquire)) ||
		!gWaterLOD || !*gWaterLOD)
		return;

	if (!tes)
		tes = cachedTes.load(std::memory_order_acquire);
	if (!tes || !tes->gridCells)
		return;

	constexpr int64_t kRetryIntervalMs = 100;
	const auto now = SteadyClockMs();
	const auto nextRetry = nextChildWsCullRetryMs.load(std::memory_order_acquire);
	if (nextRetry > now)
		return;
	nextChildWsCullRetryMs.store(now + kRetryIntervalMs, std::memory_order_release);

	if (CullAllWaterLODParents(*gWaterLOD, tes)) {
		pendingChildWsCull.store(false, std::memory_order_release);
		nextChildWsCullRetryMs.store(0, std::memory_order_release);
	}
}

void UnifiedWater::LoadSettings(json& o_json)
{
	settings = o_json;
}

void UnifiedWater::SaveSettings(json& o_json)
{
	o_json = settings;
}

void UnifiedWater::RestoreDefaultSettings()
{
	settings = {};
}

void UnifiedWater::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("use_optimised_meshes"), "Use Optimised Meshes"), &settings.UseOptimisedMeshes);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("use_optimised_meshes_tooltip"),
							  "Uses meshes with significantly lower tri-count for improved performance with no visual quality loss.\n"
							  "Will only affect newly created water - requires a change of location or game restart to take effect."));
	}

	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("debug"), "Debug"), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Button(T(TKEY("regenerate_flowmap"), "Regenerate Flowmap")) && flowmap) {
			if (flowmap->RegenerateAndLoadFlowmap())
				SetFlowmapTex();
		}

		if (ImGui::Button(T(TKEY("regenerate_caches"), "Regenerate Caches")) && waterCache)
			waterCache->RegenerateCaches();

		ImGui::TreePop();
	}
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
		auto percent = snapshot.total ? std::min(1.0f, static_cast<float>(snapshot.done) / static_cast<float>(snapshot.total)) : 0.0f;
		auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", snapshot.done, snapshot.total, 100 * percent);

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

		ImGui::TextColored(themeSettings.StatusPalette.Error, T("feature.unified_water.error_water_cache_generation_failed_for_worldspaces_check", "ERROR: Water cache generation failed for %d WorldSpaces. Check installation and CommunityShaders.log"), snapshot.failed);

		ImGui::End();
	}
}

bool UnifiedWater::IsOverlayVisible() const
{
	return waterCache && (waterCache->IsBuildRunning() || waterCache->HasBuildFailed());
}

void UnifiedWater::DataLoaded()
{
	ClearGeneratedWaterTiles();

	if (waterCache && waterCache->IsBuildRunning()) {
		logger::info("[Unified Water] Waiting for prior cache build to finish before reloading resources");
		while (waterCache->IsBuildRunning()) {
			std::this_thread::sleep_for(100ms);
		}
	}

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

	flowmap.reset();
	waterCache.reset();
	flowmap = std::make_unique<Flowmap>();
	waterCache = std::make_unique<WaterCache>();

	if (LoadOrderChanged()) {
		logger::info("[Unified Water] Load order changed, regenerating flowmap and caches");

		if (flowmap->RegenerateAndLoadFlowmap())
			SetFlowmapTex();

		waterCache->RegenerateCaches();
	} else {
		if (flowmap->LoadOrGenerateFlowmap())
			SetFlowmapTex();

		waterCache->LoadOrGenerateCaches();
	}

	while (waterCache->IsBuildRunning()) {
		std::this_thread::sleep_for(100ms);
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

bool UnifiedWater::LoadOrderChanged()
{
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler)
		return false;

	uint64_t hash = 14695981039346656037ull;

	auto addValueToHash = [&](const auto value) {
		using Value = std::decay_t<decltype(value)>;
		const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
		for (size_t i = 0; i < sizeof(Value); ++i) {
			hash ^= bytes[i];
			hash *= 1099511628211ull;
		}
	};

	auto addToHash = [&](const RE::TESFile* file) {
		if (!file || !file->fileName)
			return;
		for (auto p = reinterpret_cast<const unsigned char*>(file->fileName); *p; ++p) {
			hash ^= *p;
			hash *= 1099511628211ull;
		}

		namespace fs = std::filesystem;
		const auto pluginPath = Util::PathHelpers::GetDataPath() / file->fileName;
		std::error_code ec;
		const fs::directory_entry entry(pluginPath, ec);
		const bool hasMetadata = !ec && entry.exists(ec) && !ec;

		uintmax_t fileSize = 0;
		if (hasMetadata) {
			fileSize = entry.file_size(ec);
			if (ec) {
				fileSize = 0;
			}
		}
		addValueToHash(fileSize);

		ec.clear();
		int64_t writeTime = 0;
		if (hasMetadata) {
			const auto lastWriteTime = entry.last_write_time(ec);
			if (!ec) {
				writeTime = lastWriteTime.time_since_epoch().count();
			}
		}
		addValueToHash(writeTime);
	};

	static constexpr uint64_t kUnifiedWaterCacheVersion = 2;
	addValueToHash(kUnifiedWaterCacheVersion);

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

	namespace fs = std::filesystem;
	const fs::path path = Util::PathHelpers::GetDataPath() / "UWLoadOrder.hash";

	uint64_t existingHash = 0;
	std::error_code hashEc;
	if (fs::exists(path, hashEc)) {
		std::ifstream file(path, std::ios::binary);
		if (file.is_open()) {
			file.read(reinterpret_cast<char*>(&existingHash), sizeof(existingHash));
			file.close();
		}
	}

	if (hash != existingHash) {
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (file.is_open()) {
			file.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
		}
	}

	return hash != existingHash;
}

void UnifiedWater::RegisterGeneratedWaterTile(const RE::NiAVObject* object, const WaterTilePlacement& placement)
{
	if (!object)
		return;

	SetGeneratedWaterTileMarker(const_cast<RE::NiAVObject*>(object), placement);

	std::unique_lock lock(generatedWaterTilesLock);
	generatedWaterTiles[object] = placement;
}

void UnifiedWater::UnregisterGeneratedWaterTilesInTree(const RE::NiAVObject* object)
{
	if (!object)
		return;

	std::unique_lock lock(generatedWaterTilesLock);
	auto eraseTree = [&](auto&& self, const RE::NiAVObject* current) -> void {
		if (!current)
			return;

		RemoveGeneratedWaterTileMarker(const_cast<RE::NiAVObject*>(current));
		generatedWaterTiles.erase(current);
		if (const auto node = const_cast<RE::NiAVObject*>(current)->AsNode()) {
			for (const auto& child : node->GetChildren()) {
				self(self, child.get());
			}
		}
	};

	eraseTree(eraseTree, object);
}

bool UnifiedWater::TryGetGeneratedWaterTile(const RE::NiAVObject* object, WaterTilePlacement& placement) const
{
	if (!object)
		return false;

	std::shared_lock lock(generatedWaterTilesLock);
	const auto it = generatedWaterTiles.find(object);
	if (it == generatedWaterTiles.end())
		return false;

	placement = it->second;
	return true;
}

void UnifiedWater::RemoveDuplicateGeneratedWaterTiles(RE::TESWaterSystem* waterSystem, RE::NiNode* lodRoot, const std::vector<WaterTilePlacement>& touchedTiles)
{
	if (!waterSystem || !lodRoot || touchedTiles.empty())
		return;

	static thread_local std::unordered_set<GeneratedTileKey, GeneratedTileKeyHash> touchedTileKeys;
	static thread_local std::vector<RE::NiPointer<RE::BSTriShape>> duplicateShapes;
	touchedTileKeys.clear();
	duplicateShapes.clear();
	touchedTileKeys.reserve(touchedTiles.size());
	duplicateShapes.reserve(touchedTiles.size());

	for (const auto& touched : touchedTiles) {
		touchedTileKeys.insert(GetGeneratedTileKey(touched));
	}

	{
		std::shared_lock lock(generatedWaterTilesLock);
		for (const auto& [object, placement] : generatedWaterTiles) {
			if (!object || !touchedTileKeys.contains(GetGeneratedTileKey(placement)) || !IsChildOfNode(object, lodRoot))
				continue;

			if (const auto shape = const_cast<RE::NiAVObject*>(object)->AsTriShape()) {
				duplicateShapes.emplace_back(shape);
			}
		}
	}

	if (duplicateShapes.empty())
		return;

	{
		std::unique_lock lock(generatedWaterTilesLock);
		for (const auto& shape : duplicateShapes) {
			generatedWaterTiles.erase(shape.get());
		}
	}

	for (const auto& shape : duplicateShapes) {
		if (!shape)
			continue;

		RemoveGeneratedWaterTileMarker(shape.get());
		shape->SetAppCulled(true);
		RemoveWaterObjectForShape(waterSystem, shape.get());
		if (shape->parent) {
			shape->parent->DetachChild2(shape.get());
		}
	}

	duplicateShapes.clear();
}

void UnifiedWater::ClearGeneratedWaterTiles()
{
	std::unique_lock lock(generatedWaterTilesLock);
	generatedWaterTiles.clear();
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
	const bool enteringChild = IsChildWorldSpace(worldSpace);

	// Set before func so attachment hooks fired inside func see the new worldspace.
	auto& uw = globals::features::unifiedWater;
	uw.currentPlayerWorldSpace.store(worldSpace, std::memory_order_release);
	uw.cachedTes.store(tes, std::memory_order_release);
	if (!enteringChild) {
		uw.pendingChildWsCull.store(false, std::memory_order_release);  // leaving child WS: discard any stale pending cull
		uw.nextChildWsCullRetryMs.store(0, std::memory_order_release);
	}

	func(tes, worldSpace, isExterior);

	uw.exteriorWorldspaceActive.store(worldSpace && isExterior, std::memory_order_release);

	if (!uw.waterCache) {
		uw.pendingChildWsCull.store(false, std::memory_order_release);
		uw.nextChildWsCullRetryMs.store(0, std::memory_order_release);
		uw.UpdateWaterLODCull();
		return;
	}

	uw.waterCache->SetCurrentWorldSpace(worldSpace);
	uw.UpdateWaterLODCull();

	if (enteringChild) {
		// BGSTerrainBlock_Attach calls Enable() on block attach.
		// Child-worldspace transitions can keep old LOD blocks attached, so re-enable here.
		if (const auto waterSystem = globals::game::waterSystem)
			waterSystem->Enable();

		// Try an immediate cull with tes (globals::game::tes may still be null).
		// Newly transitioned cells are often not attached yet, so deferred retries are still needed.
		if (uw.gWaterLOD && *uw.gWaterLOD && tes && tes->gridCells)
			CullAllWaterLODParents(*uw.gWaterLOD, tes);

		// Keep deferred retries enabled until attached cells are observed and culled.
		uw.pendingChildWsCull.store(true, std::memory_order_release);
		uw.nextChildWsCullRetryMs.store(0, std::memory_order_release);
	}
}

void UnifiedWater::TES_DestroySkyCell::thunk(RE::TES* tes)
{
	func(tes);

	auto& uw = globals::features::unifiedWater;
	uw.currentPlayerWorldSpace.store(nullptr, std::memory_order_release);
	uw.pendingChildWsCull.store(false, std::memory_order_release);
	uw.nextChildWsCullRetryMs.store(0, std::memory_order_release);
	uw.cachedTes.store(nullptr, std::memory_order_release);
	uw.exteriorWorldspaceActive.store(false, std::memory_order_release);
	uw.ClearGeneratedWaterTiles();

	if (uw.waterCache)
		uw.waterCache->SetCurrentWorldSpace(nullptr);
	uw.UpdateWaterLODCull();
}

void UnifiedWater::BGSTerrainNode_UpdateWaterMeshSubVisibility::thunk(const RE::BGSTerrainNode* node, RE::BSMultiBoundNode* waterParent)
{
	if (!node || !waterParent)
		return;

	if (node->GetLODLevel() != 4)
		return;

	CullWaterParentByGridCells(waterParent);
}

void UnifiedWater::BGSTerrainBlock_Attach::thunk(RE::BGSTerrainBlock* block)
{
	const auto waterSystem = globals::game::waterSystem;
	auto& uw = globals::features::unifiedWater;

	if (!waterSystem || !uw.waterCache) {
		func(block);
		return;
	}

	// Additional game-thread retry path for deferred child-WS cull completion.
	uw.TryCompleteDeferredChildWorldspaceCull(uw.cachedTes.load(std::memory_order_acquire));

	std::vector<std::pair<RE::BSTriShape*, const WaterCache::Instruction*>> built;
	bool attaching = false;
	RE::NiPointer<RE::BSMultiBoundNode> water;

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

		const auto instructions = uw.waterCache->GetInstructions(worldSpace, lodLevel, node->baseCellX, node->baseCellY);
		if (!instructions) {
			logger::warn("[Unified Water] No instructions found for {} chunk at {}, {}", worldSpace->GetFormEditorID(), node->baseCellX, node->baseCellY);
			// Reattach the saved node before falling back to vanilla
			block->chunk->AttachChild(water.get(), true);
			func(block);
			uw.UpdateWaterLODCull();
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
			uw.UpdateWaterLODCull();
			return;
		}

		built.reserve(instructions->size());

		uw.UnregisterGeneratedWaterTilesInTree(water.get());
		ClearWaterNodeChildren(water.get(), waterSystem);

		for (auto& instruction : *instructions) {
			if (!instruction.form.ptr)
				continue;

			RE::NiCloningProcess cloningProcess;

			const auto targetShape = ShouldUseOptimisedGeneratedWaterMesh(instruction, uw.settings.UseOptimisedMeshes) ? uw.optimisedWaterMesh : uw.waterMesh;
			RE::BSTriShape* shape = targetShape->CreateClone(cloningProcess)->AsTriShape();

			const auto posX = (instruction.x - node->baseCellX) * 4096.0f + instruction.size * 2048.0f;
			const auto posY = (instruction.y - node->baseCellY) * 4096.0f + instruction.size * 2048.0f;
			shape->local.scale = static_cast<float>(instruction.size);
			shape->local.translate = { posX, posY, instruction.waterHeight };

			uw.RegisterGeneratedWaterTile(shape, { instruction.x, instruction.y, instruction.size });
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
		uw.UpdateWaterLODCull();
		return;
	}

	// Reserve up front so AddWater can't reallocate waterObjects mid-loop and free a buffer other threads may be iterating.
	{
		RE::BSSpinLockGuard guard(waterSystem->lock);
		waterSystem->waterObjects.reserve(waterSystem->waterObjects.size() + static_cast<std::uint32_t>(built.size()));
	}

	std::vector<WaterTilePlacement> touchedTiles;
	touchedTiles.reserve(built.size());

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

		// Remove the exact object added above. Generated UW tiles are rendered from the LOD root
		// and should not remain in TESWaterSystem's active close-water object list.
		WaterPositionKey removedKey;
		if (RemoveWaterObjectForShape(waterSystem, shape, &removedKey)) {
			const WaterTilePlacement touchedTile{ instruction->x, instruction->y, instruction->size, removedKey.waterForm, removedKey.waterFlags };
			uw.RegisterGeneratedWaterTile(shape, touchedTile);
			touchedTiles.push_back(touchedTile);
		} else {
			logger::warn("[Unified Water] Failed to remove generated tile from TESWaterSystem at {},{}", instruction->x, instruction->y);
		}
	}

	if (auto waterLOD = uw.gWaterLOD; waterLOD && *waterLOD) {
		uw.RemoveDuplicateGeneratedWaterTiles(waterSystem, *waterLOD, touchedTiles);
		DetachAllChildOccurrences(*waterLOD, water.get());
		(*waterLOD)->AttachChild(water.get(), true);
		uw.UpdateWaterLODCull();
	} else if (block->chunk) {
		// If the LOD root is unavailable, keep ownership on the chunk
		block->chunk->AttachChild(water.get(), true);
		uw.UpdateWaterLODCull();
	} else {
		uw.UnregisterGeneratedWaterTilesInTree(water.get());
		block->water = nullptr;
		block->waterAttached = false;
	}
	waterSystem->Enable();

	// BGSTerrainNode_UpdateWaterMeshSubVisibility never fires in child worldspaces.
	// Cull newly built tiles here; full deferred retries are handled by
	// TryCompleteDeferredChildWorldspaceCull().
	if (IsChildWorldSpace(uw.currentPlayerWorldSpace.load(std::memory_order_acquire))) {
		const auto tes = uw.cachedTes.load(std::memory_order_acquire);
		if (tes && tes->gridCells) {
			for (const auto& [shape, instruction] : built) {
				const auto cullState = ShouldCullTileFootprint(tes, instruction->x, instruction->y, instruction->size);
				shape->SetAppCulled(cullState.shouldCull);
			}
		}
	}
}

void UnifiedWater::BGSTerrainBlock_Detach::thunk(RE::BGSTerrainBlock* block)
{
	auto& uw = globals::features::unifiedWater;

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
		uw.UnregisterGeneratedWaterTilesInTree(water.get());
		ClearWaterNodeChildren(water.get(), globals::game::waterSystem);

		if (auto waterLOD = uw.gWaterLOD; waterLOD && *waterLOD)
			DetachAllChildOccurrences(*waterLOD, water.get());

		// Park water under the detached chunk so block->water stays valid
		if (block->chunk) {
			block->chunk->AttachChild(water.get(), true);
			block->water = water.get();
		} else {
			block->water = nullptr;
		}

		block->waterAttached = false;
		uw.UpdateWaterLODCull();
	}
}

void UnifiedWater::BSWaterShader_SetupGeometry::thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass, uint32_t renderFlags)
{
	auto& uw = globals::features::unifiedWater;

	if (pass && pass->geometry) {
		// Re-stabilize BSWaterShaderProperty.plane every draw. After interior/exterior
		// transitions the cached plane can be stale for exactly one of two overlapping
		// water surfaces, which presents as heavy flicker rather than missing water.
		if (const auto prop = pass->geometry->GetGeometryRuntimeData().shaderProperty.get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
			const float waterHeight = pass->geometry->world.translate.z;

			waterShaderProp->plane.normal = { 0.0f, 0.0f, 1.0f };
			waterShaderProp->plane.constant = waterHeight;
		}
	}

	if (uw.IsExteriorWorldspaceActive() && uw.flowmap && pass && pass->geometry) {
		// ObjectUV.xyz below, xy contains width and height, z contains mesh scale
		// Previously flowmap size was in x, yz contained flowmap offset for water displacement mesh
		*uw.gFlowMapSize = uw.flowmap->GetWidth();                                            // ObjectUV.x
		uw.gDisplacementMeshFlowCellOffset->x = static_cast<float>(uw.flowmap->GetHeight());  // ObjectUV.y
		uw.gDisplacementMeshFlowCellOffset->y = 1.0f - pass->geometry->local.scale;           // ObjectUV.z (counters 1 - x in SetupGeometry)

		if (const auto prop = pass->geometry->GetGeometryRuntimeData().shaderProperty.get(); prop && prop->GetRTTI() == globals::rtti::BSWaterShaderPropertyRTTI.get()) {
			const auto waterShaderProp = static_cast<RE::BSWaterShaderProperty*>(prop);
			int32_t x, y;
			WaterTilePlacement placement;
			if (TryGetGeneratedWaterTileMarker(pass->geometry, placement) || uw.TryGetGeneratedWaterTile(pass->geometry, placement)) {
				x = placement.x;
				y = placement.y;
			} else {
				Util::WorldToCell(pass->geometry->world.translate, x, y);
			}
			// CellTexCoordOffset.xyzw below - applies to non-displacement water only
			// xy is world cell flowmap based (0,0 is corner of flow map), zw is world cell
			// Previously these values were relative to the 5x5 flow grid centered on the player
			waterShaderProp->flowX = x + uw.flowmap->GetOffsetX();                                                     // CellTexCoordOffset.x
			waterShaderProp->flowY = y + uw.flowmap->GetOffsetY() + uw.flowmap->GetWidth() - uw.flowmap->GetHeight();  // CellTexCoordOffset.y
			waterShaderProp->cellX = x;                                                                                // CellTexCoordOffset.z
			waterShaderProp->cellY = y;                                                                                // CellTexCoordOffset.w
		}
	}

	func(waterShader, pass, renderFlags);
}

void UnifiedWater::TESWaterSystem_UpdateDisplacementMeshPosition::thunk(RE::TESWaterSystem* waterSystem)
{
	func(waterSystem);

	auto& uw = globals::features::unifiedWater;

	// Game-thread fallback for deferred child-worldspace cull completion.
	// Needed when entering child worldspaces with already-attached LOD blocks,
	// where BGSTerrainBlock_Attach/UpdateWaterMeshSubVisibility may not run.
	uw.TryCompleteDeferredChildWorldspaceCull(uw.cachedTes.load(std::memory_order_acquire));
	uw.UpdateWaterLODCull();

	if (!uw.flowmap || !uw.IsExteriorWorldspaceActive())
		return;

	const float posX = uw.gDisplacementMeshPos->x / 4096.0f;
	const float posY = uw.gDisplacementMeshPos->y / 4096.0f;
	const float offsetX = static_cast<float>(uw.flowmap->GetOffsetX());
	const float offsetY = static_cast<float>(uw.flowmap->GetOffsetY());
	const float height = static_cast<float>(uw.flowmap->GetHeight());

	// CellTexCoordOffset.xyzw below - applies to displacement water only
	// Previously the values were calculated relative to the 5x5 flow grid
	*uw.gDisplacementCellTexCoordOffset = float4(posX + offsetX, height - (posY + offsetY), posX, 1 - posY);
}
#undef I18N_KEY_PREFIX
