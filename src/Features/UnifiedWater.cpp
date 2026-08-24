#include "UnifiedWater.h"

#include "Menu.h"
#include "Menu/OverlayRenderer.h"
#include "Menu/ThemeManager.h"
#include "State.h"
#include "Util.h"

#include "RE/L/LoadingMenu.h"
#include "RE/M/MapMenu.h"
#include "RE/P/PlayerCharacter.h"

#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <memory>
#include <unordered_map>
#include <vector>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	UnifiedWater::Settings,
	SurfaceVisibilityModelVersion,
	UseOptimisedMeshes,
	UseOpenShadersDepthBehaviour,
	WaterTintColor,
	WaterTintStrength,
	ShallowFallbackStrength,
	DeepConnectionProbeReachUnits,
	DeepContextDepthUnits,
	DeepContextTransitionUnits,
	ShoreContactMinFadePixels,
	ShoreDepthBlendRangeUnits,
	ShallowSurfaceDepthRangeUnits,
	ShallowFallbackMaxDistance)

namespace
{
	constexpr std::uint32_t kSurfaceVisibilityModelVersion = 11;
	constexpr float kWaterTintColorMin = 0.0f;
	constexpr float kWaterTintColorMax = 1.0f;
	constexpr float kWaterTintStrengthMin = 0.0f;
	constexpr float kWaterTintStrengthMax = 1.0f;
	constexpr float kShallowFallbackStrengthMin = 0.0f;
	constexpr float kShallowFallbackStrengthMax = 1.0f;
	constexpr float kDeepConnectionProbeReachUnitsMin = 0.0f;
	constexpr float kDeepConnectionProbeReachUnitsMax = 768.0f;
	constexpr float kDeepContextDepthUnitsMin = 32.0f;
	constexpr float kDeepContextDepthUnitsMax = 384.0f;
	constexpr float kDeepContextTransitionUnitsMin = 4.0f;
	constexpr float kDeepContextTransitionUnitsMax = 128.0f;
	constexpr float kShoreContactMinFadePixelsMin = 0.0f;
	constexpr float kShoreContactMinFadePixelsMax = 4.0f;
	constexpr float kWorldCellSize = 4096.0f;
	constexpr float kShoreDepthBlendRangeUnitsMin = 0.0f;
	constexpr float kShoreDepthBlendRangeUnitsMax = 20.0f;
	constexpr float kShallowSurfaceDepthRangeUnitsMin = 16.0f;
	constexpr float kShallowSurfaceDepthRangeUnitsMax = 256.0f;
	constexpr float kShallowFallbackMaxDistanceMin = 0.0f;
	constexpr float kShallowFallbackMaxDistanceMax = kWorldCellSize * 16.0f;
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
		a_settings.ShallowFallbackStrength = ClampFiniteOrDefault(
			a_settings.ShallowFallbackStrength,
			kShallowFallbackStrengthMin,
			kShallowFallbackStrengthMax,
			defaults.ShallowFallbackStrength);
		a_settings.DeepConnectionProbeReachUnits = ClampFiniteOrDefault(
			a_settings.DeepConnectionProbeReachUnits,
			kDeepConnectionProbeReachUnitsMin,
			kDeepConnectionProbeReachUnitsMax,
			defaults.DeepConnectionProbeReachUnits);
		a_settings.DeepContextDepthUnits = ClampFiniteOrDefault(
			a_settings.DeepContextDepthUnits,
			kDeepContextDepthUnitsMin,
			kDeepContextDepthUnitsMax,
			defaults.DeepContextDepthUnits);
		a_settings.DeepContextTransitionUnits = ClampFiniteOrDefault(
			a_settings.DeepContextTransitionUnits,
			kDeepContextTransitionUnitsMin,
			kDeepContextTransitionUnitsMax,
			defaults.DeepContextTransitionUnits);
		a_settings.ShoreContactMinFadePixels = ClampFiniteOrDefault(
			a_settings.ShoreContactMinFadePixels,
			kShoreContactMinFadePixelsMin,
			kShoreContactMinFadePixelsMax,
			defaults.ShoreContactMinFadePixels);
		a_settings.ShoreDepthBlendRangeUnits = ClampFiniteOrDefault(
			a_settings.ShoreDepthBlendRangeUnits,
			kShoreDepthBlendRangeUnitsMin,
			kShoreDepthBlendRangeUnitsMax,
			defaults.ShoreDepthBlendRangeUnits);
		a_settings.ShallowSurfaceDepthRangeUnits = ClampFiniteOrDefault(
			a_settings.ShallowSurfaceDepthRangeUnits,
			kShallowSurfaceDepthRangeUnitsMin,
			kShallowSurfaceDepthRangeUnitsMax,
			defaults.ShallowSurfaceDepthRangeUnits);
		a_settings.ShallowFallbackMaxDistance = ClampFiniteOrDefault(
			a_settings.ShallowFallbackMaxDistance,
			kShallowFallbackMaxDistanceMin,
			kShallowFallbackMaxDistanceMax,
			defaults.ShallowFallbackMaxDistance);
	}

	void DrawWaterTintSettings(UnifiedWater::Settings& a_settings)
	{
		auto* tintColor = reinterpret_cast<float*>(&a_settings.WaterTintColor);
		const ImVec4 tintPreview(tintColor[0], tintColor[1], tintColor[2], 1.0f);

		ImGui::PushID("WaterTintColor");
		if (ImGui::ColorButton("##Preview", tintPreview, ImGuiColorEditFlags_NoAlpha))
			ImGui::OpenPopup("Picker");
		ImGui::SameLine();
		ImGui::ColorEdit3(
			"Water Tint Color",
			tintColor,
			ImGuiColorEditFlags_NoAlpha |
				ImGuiColorEditFlags_NoPicker |
				ImGuiColorEditFlags_NoSmallPreview);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Selects the colour of the water tint.");

		if (ImGui::BeginPopup("Picker")) {
			ImGui::ColorPicker3("##ColorPicker", tintColor, ImGuiColorEditFlags_NoAlpha);
			ImGui::Spacing();
			if (ImGui::Button("Exit Tint Menu", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopID();

		ImGui::SliderFloat(
			"Water Tint",
			&a_settings.WaterTintStrength,
			kWaterTintStrengthMin,
			kWaterTintStrengthMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Adjusts how strongly the selected colour appears in the water.");
	}
}

static const RE::TESWorldSpace* ResolveLODDataWorldSpace(const RE::TESWorldSpace* ws)
{
	while (ws && ws->parentWorld && ws->parentUseFlags.any(RE::TESWorldSpace::ParentUseFlag::kUseLODData)) {
		ws = ws->parentWorld;
	}
	return ws;
}
static bool IsChildWorldSpace(const RE::TESWorldSpace* ws)
{
	return ws && ResolveLODDataWorldSpace(ws) != ws;
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

static void LogMeshLoadFailure(RE::BSResource::ErrorCode error, const char* dataRelativePath)
{
	std::error_code ec;
	logger::error(
		"[Unified Water] {} load failed: {}, loose file present: {}",
		dataRelativePath,
		magic_enum::enum_name(error),
		std::filesystem::exists(std::filesystem::path("Data") / dataRelativePath, ec));
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

static bool CanPatchBranch(const std::uintptr_t address)
{
	const auto bytes = reinterpret_cast<const std::uint8_t*>(address);
	return IsShortBranch(bytes[0]) || IsNearConditionalBranch(bytes[0], bytes[1]);
}

static bool CanPatchRelativeCall(const std::uintptr_t address)
{
	return *reinterpret_cast<const std::uint8_t*>(address) == 0xE8;
}

static bool DisableVanillaWaterLOD()
{
	// DataLoaded can be delivered more than once on reload paths. Re-patching a
	// near branch would no longer match its original conditional encoding.
	static bool patched = false;
	if (patched)
		return true;

	// Unified Water owns these paths only after both replacement meshes have
	// validated. Preflight both branches before making either write so a runtime
	// conflict leaves vanilla LOD and flow-map handling intact on SE, AE, and VR.
	const auto attachedMeshAddLoop = REL::RelocationID(30934, 31737).address() + REL::Relocate(0x109, 0x109);
	const auto lodWaterAddLoop = REL::RelocationID(30978, 31751).address() + REL::Relocate(0x54, 0xEA);
	const auto flowMapCall1 = REL::RelocationID(35561, 36560).address() + REL::Relocate(0x1B7, 0x1F7);
	const auto flowMapCall2 = REL::RelocationID(35561, 36560).address() + REL::Relocate(0x1EA, 0x22A);
	const auto flowMapCall3 = REL::RelocationID(35561, 36560).address() + REL::Relocate(0x202, 0x242);
	if (!CanPatchBranch(attachedMeshAddLoop) ||
		!CanPatchBranch(lodWaterAddLoop) ||
		!CanPatchRelativeCall(flowMapCall1) ||
		!CanPatchRelativeCall(flowMapCall2) ||
		!CanPatchRelativeCall(flowMapCall3)) {
		logger::error(
			"[Unified Water] Cannot disable vanilla water paths; unexpected instructions at branches {:X}/{:X} or calls {:X}/{:X}/{:X}. Another mod may patch the same code",
			attachedMeshAddLoop,
			lodWaterAddLoop,
			flowMapCall1,
			flowMapCall2,
			flowMapCall3);
		return false;
	}

	PatchBranchToUnconditional(attachedMeshAddLoop, "attached mesh add loop");
	PatchBranchToUnconditional(lodWaterAddLoop, "LOD water add loop");

	// Patch out the compute shader calls that write to the vanilla flow map.
	REL::safe_fill(flowMapCall1, REL::NOP, 5);
	REL::safe_fill(flowMapCall2, REL::NOP, 5);
	REL::safe_fill(flowMapCall3, REL::NOP, 5);

	patched = true;
	return true;
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
};

static bool operator==(const WaterPositionKey& lhs, const WaterPositionKey& rhs)
{
	return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.scale == rhs.scale;
}

struct WaterPositionKeyHash
{
	size_t operator()(const WaterPositionKey& key) const noexcept
	{
		size_t hash = std::hash<int32_t>{}(key.x);
		hash ^= std::hash<int32_t>{}(key.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<int32_t>{}(key.z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<int32_t>{}(key.scale) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		return hash;
	}
};

static int32_t QuantizeWaterPosition(float value)
{
	return static_cast<int32_t>(std::lround(value));
}

static WaterPositionKey GetWaterPositionKey(const RE::NiAVObject* object)
{
	if (!object)
		return {};

	return {
		QuantizeWaterPosition(object->world.translate.x),
		QuantizeWaterPosition(object->world.translate.y),
		QuantizeWaterPosition(object->world.translate.z),
		QuantizeWaterPosition(object->world.scale * 1000.0f),
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

static RE::BSTriShape* SelectDuplicateWaterSystemShapeToRemove(RE::BSTriShape* existing, RE::BSTriShape* candidate, RE::NiNode* lodRoot)
{
	if (!existing)
		return candidate;
	if (!candidate)
		return existing;

	const bool existingIsLOD = IsChildOfNode(existing, lodRoot);
	const bool candidateIsLOD = IsChildOfNode(candidate, lodRoot);
	if (existingIsLOD != candidateIsLOD)
		return existingIsLOD ? existing : candidate;

	return candidate;
}

static void RemoveDuplicateWaterSystemObjects(RE::TESWaterSystem* waterSystem, RE::NiNode* lodRoot)
{
	if (!waterSystem)
		return;

	static thread_local std::unordered_map<WaterPositionKey, RE::BSTriShape*, WaterPositionKeyHash> shapeByPosition;
	static thread_local std::vector<RE::BSTriShape*> duplicateShapes;

	shapeByPosition.clear();
	duplicateShapes.clear();

	const auto objectCount = waterSystem->waterObjects.size();
	if (shapeByPosition.bucket_count() < objectCount)
		shapeByPosition.reserve(objectCount);
	if (duplicateShapes.capacity() < objectCount)
		duplicateShapes.reserve(objectCount);

	for (const auto& waterObject : waterSystem->waterObjects) {
		const auto shape = waterObject ? waterObject->shape.get() : nullptr;
		if (!shape)
			continue;

		const auto key = GetWaterPositionKey(shape);
		const auto [it, inserted] = shapeByPosition.try_emplace(key, shape);
		if (inserted)
			continue;

		const auto existing = it->second;
		const auto duplicate = SelectDuplicateWaterSystemShapeToRemove(existing, shape, lodRoot);
		if (duplicate == existing)
			it->second = shape;

		duplicateShapes.push_back(duplicate);
	}

	for (const auto shape : duplicateShapes) {
		if (!shape)
			continue;

		shape->SetAppCulled(true);
		waterSystem->RemoveWater(shape);
	}
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

	bool IsComplete() const
	{
		return foundAttachedCell && !hasPotentiallyAttachableChild;
	}
};

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
		int32_t x, y;
		Util::WorldToCell(child->world.translate, x, y);
		bool isInGrid = false;
		bool isAttached = false;
		const bool cull = ShouldCullAtCell(tes, x, y, &isInGrid, &isAttached);
		if (isAttached)
			state.foundAttachedCell = true;
		else if (isInGrid)
			state.hasPotentiallyAttachableChild = true;
		child->SetAppCulled(cull);
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

	if (CullAllWaterLODParents(*gWaterLOD, tes))
		pendingChildWsCull.store(false, std::memory_order_release);
}

void UnifiedWater::LoadSettings(json& o_json)
{
	settings = o_json;
	const auto loadedModelVersion = o_json.value("SurfaceVisibilityModelVersion", 0u);
	if (loadedModelVersion != kSurfaceVisibilityModelVersion) {
		const Settings defaults{};

		if (loadedModelVersion < 11) {
			// The former model selected between separate shallow and deep edge
			// curves. Start the replacement single curve at its balanced default
			// instead of reinterpreting either legacy value.
			settings.ShoreDepthBlendRangeUnits = defaults.ShoreDepthBlendRangeUnits;
		}

		if (loadedModelVersion < 9) {
			// Earlier appearance thresholds are not geometric water-depth units.
			settings.DeepConnectionProbeReachUnits = defaults.DeepConnectionProbeReachUnits;
			settings.DeepContextDepthUnits = defaults.DeepContextDepthUnits;
			settings.DeepContextTransitionUnits = defaults.DeepContextTransitionUnits;
		}

		if (loadedModelVersion < 7) {
			// The replacement is a screen-space minimum contact fade, so never
			// reinterpret the old view-dependent coverage value.
			settings.ShoreContactMinFadePixels = defaults.ShoreContactMinFadePixels;
		}

		if (loadedModelVersion < 6) {
			// Preserve useful legacy strength/distance intent while switching to
			// the native-first shallow fallback model.
			settings.ShallowFallbackStrength = o_json.value(
				"DistantDepthFadeFarStrength",
				defaults.ShallowFallbackStrength);
			settings.ShallowFallbackMaxDistance = o_json.value(
				"ShoreConfirmationMaxDistance",
				o_json.value(
					"ShoreConfirmationCullDistance",
					defaults.ShallowFallbackMaxDistance));
		}
	}
	settings.SurfaceVisibilityModelVersion = kSurfaceVisibilityModelVersion;
	SanitizeSettings(settings);
}

void UnifiedWater::SaveSettings(json& o_json)
{
	settings.SurfaceVisibilityModelVersion = kSurfaceVisibilityModelVersion;
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

	ImGui::Checkbox("Use Optimised Meshes", &settings.UseOptimisedMeshes);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Uses meshes with significantly lower tri-count for improved performance with no visual quality loss.\n"
			"Will only affect newly created water - requires a change of location or game restart to take effect.");
	}

	ImGui::Spacing();
	ImGui::SeparatorText("Water Appearance");
	DrawWaterTintSettings(settings);

	ImGui::Spacing();

	if (ImGui::TreeNodeEx("Shallow Water Surface Visibility")) {
		ImGui::BeginDisabled(settings.UseOpenShadersDepthBehaviour);
		ImGui::SeparatorText("Shore Contact");

		ImGui::SliderFloat(
			"Edge Fade Depth",
			&settings.ShoreDepthBlendRangeUnits,
			kShoreDepthBlendRangeUnitsMin,
			kShoreDepthBlendRangeUnitsMax,
			"%.1f units",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Plane-normal depth over which the shallow surface cue fades in from the shore.\n"
				"Set to 0 to disable this world-space fade; Minimum Edge Fade Width remains active.");
		}

		ImGui::SliderFloat(
			"Shallow Fallback Max Distance",
			&settings.ShallowFallbackMaxDistance,
			kShallowFallbackMaxDistanceMin,
			kShallowFallbackMaxDistanceMax,
			"%.0f units",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Maximum view distance for the shallow fallback and its connected-depth reads.\n"
				"Set to 0 to use native/Open depth blending everywhere.");
		}

		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	ImGui::Spacing();

	if (ImGui::TreeNodeEx("Debug")) {
		ImGui::Checkbox("Use Open Shaders Depth Behaviour", &settings.UseOpenShadersDepthBehaviour);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Disables the shallow-only surface cue and uses the native Open Shaders-like water blend.\n"
				"Custom visibility values are preserved and resume when disabled.");
		}

		ImGui::BeginDisabled(settings.UseOpenShadersDepthBehaviour);
		ImGui::SliderFloat(
			"Shallow Fallback Strength",
			&settings.ShallowFallbackStrength,
			kShallowFallbackStrengthMin,
			kShallowFallbackStrengthMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Strength of the bounded surface cue used only when native/Open water is visually indistinguishable from the riverbed.");
		}
		ImGui::EndDisabled();

		if (globals::state && globals::state->IsDeveloperMode()) {
			ImGui::Spacing();
			ImGui::BeginDisabled(settings.UseOpenShadersDepthBehaviour);

			ImGui::SeparatorText("Deep Water Protection");

			ImGui::SliderFloat(
				"Connection Search Reach",
				&settings.DeepConnectionProbeReachUnits,
				kDeepConnectionProbeReachUnitsMin,
				kDeepConnectionProbeReachUnitsMax,
				"%.0f units",
				ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Physical distance searched toward increasing terrain depth at half and full reach.\n"
					"Increase this for broad shallow banks. Set to 0 to disable connected-depth protection.");
			}

			ImGui::SliderFloat(
				"Deep Context Depth",
				&settings.DeepContextDepthUnits,
				kDeepContextDepthUnitsMin,
				kDeepContextDepthUnitsMax,
				"%.0f units",
				ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Plane-normal depth that gives full native/Open protection to a connected medium/deep channel.\n"
					"Keep this above Shallow Surface Depth so a uniformly shallow stream retains its fallback surface.");
			}

			ImGui::SliderFloat(
				"Deep Context Transition",
				&settings.DeepContextTransitionUnits,
				kDeepContextTransitionUnitsMin,
				kDeepContextTransitionUnitsMax,
				"%.0f units",
				ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Depth width below Deep Context Depth over which connected deep-water protection fades in.\n"
					"Raise this for a softer handoff; use the minimum for the sharpest transition.");
			}

			ImGui::Spacing();
			ImGui::SeparatorText("Depth Separation");

			ImGui::SliderFloat(
				"Shallow Surface Depth",
				&settings.ShallowSurfaceDepthRangeUnits,
				kShallowSurfaceDepthRangeUnitsMin,
				kShallowSurfaceDepthRangeUnitsMax,
				"%.0f units",
				ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Plane-normal water depth where the shallow surface cue has faded completely to native water.\n"
					"Lower this if the cue reaches medium water; raise it only when a shallow stream still loses its surface.");
			}

			ImGui::Spacing();
			ImGui::SeparatorText("Shore Contact");

			ImGui::SliderFloat(
				"Minimum Edge Fade Width",
				&settings.ShoreContactMinFadePixels,
				kShoreContactMinFadePixelsMin,
				kShoreContactMinFadePixelsMax,
				"%.1f px",
				ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Minimum screen-space width of the shallow cue's terrain-contact fade.\n"
					"This prevents subpixel terminal seams without extra texture samples. Set to 0 to use only Edge Fade Depth.");
			}

			ImGui::TextDisabled(
				"Up to two bounded connection reads run only for unresolved shallow pixels inside the fallback distance.");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Native/Open is always the base. Pixels outside the shallow range, disabled, or distance-culled skip the connectivity reads.");
			}

			ImGui::EndDisabled();
			ImGui::Spacing();

			if (ImGui::Button("Regenerate Flowmap") && flowmap) {
				if (flowmap->RegenerateAndLoadFlowmap())
					SetFlowmapTex();
			}

			if (ImGui::Button("Regenerate Caches") && waterCache)
				waterCache->RegenerateCaches();
		}

		ImGui::TreePop();
	}
}

UnifiedWater::CommonBufferData UnifiedWater::GetCommonBufferData() const
{
	auto sanitizedSettings = settings;
	SanitizeSettings(sanitizedSettings);

	CommonBufferData data{};
	const float depthBehaviourScale = sanitizedSettings.UseOpenShadersDepthBehaviour ? 0.0f : 1.0f;
	data.ShallowFallbackStrength = sanitizedSettings.ShallowFallbackStrength * depthBehaviourScale;
	data.DeepConnectionProbeReachUnits = sanitizedSettings.DeepConnectionProbeReachUnits;
	data.DeepContextDepthUnits = sanitizedSettings.DeepContextDepthUnits;
	data.ShoreContactMinFadePixels = sanitizedSettings.ShoreContactMinFadePixels;
	data.WaterTintColor = sanitizedSettings.WaterTintColor;
	data.WaterTintStrength = sanitizedSettings.WaterTintStrength;
	data.ShoreDepthBlendRangeUnits = sanitizedSettings.ShoreDepthBlendRangeUnits;
	data.ShallowSurfaceDepthRangeUnits = sanitizedSettings.ShallowSurfaceDepthRangeUnits;
	data.ShallowFallbackMaxDistance = sanitizedSettings.ShallowFallbackMaxDistance;
	data.DeepContextTransitionUnits = sanitizedSettings.DeepContextTransitionUnits;
	return data;
}

void UnifiedWater::DrawEssentialSettings()
{
	SanitizeSettings(settings);
	ImGui::SeparatorText("Water Appearance");
	DrawWaterTintSettings(settings);
}

void UnifiedWater::DrawPerformanceSettings(bool)
{
	ImGui::Checkbox("Use Optimised Meshes", &settings.UseOptimisedMeshes);
}

json UnifiedWater::CapturePerformanceSettingsState() const
{
	return {
		{ "UseOptimisedMeshes", settings.UseOptimisedMeshes }
	};
}

void UnifiedWater::DrawOverlay()
{
	if (!waterCache || !waterCache->IsBuildRunning() && !waterCache->HasBuildFailed())
		return;

	const float scale = Util::GetUIScale();
	float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	if (REL::Module::IsVR()) {
		pos = OverlayRenderer::GetDefaultVRLeftAnchorX(OverlayRenderer::GetDefaultVRSettingsWindowSize(false).x);
	}
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
		auto progressTitle = fmt::format("Generating Water Cache:");
		auto percent = static_cast<float>(snapshot.completed) / static_cast<float>(snapshot.total);
		auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", snapshot.completed, snapshot.total, 100 * percent);

		ImGui::SetNextWindowPos(ImVec2(pos, pos + vOffset));
		if (!ImGui::Begin("UWCacheCreationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextUnformatted(progressTitle.c_str());
		ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());

		ImGui::End();
	} else if (waterCache->HasBuildFailed()) {
		ImGui::SetNextWindowPos(ImVec2(pos, pos + vOffset));
		if (!ImGui::Begin("UWCacheCreationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}

		if (snapshot.failed > 0) {
			ImGui::TextColored(themeSettings.StatusPalette.Error, "ERROR: Water cache generation failed for %d WorldSpaces. Check installation and CommunityShaders.log", snapshot.failed);
		} else {
			ImGui::TextColored(themeSettings.StatusPalette.Error, "ERROR: Generated water caches could not be loaded completely. Check CommunityShaders.log");
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
	if (IsWaterDataReady()) {
		logger::debug("[Unified Water] Runtime data is already initialized");
		return;
	}

	auto args = RE::BSModelDB::DBTraits::ArgsType();
	args.unk8 = false;
	args.unkA = false;
	args.postProcess = false;
	RE::NiPointer<RE::NiNode> nif;
	RE::NiPointer<RE::BSTriShape> loadedWaterMesh;
	RE::NiPointer<RE::BSTriShape> loadedOptimisedWaterMesh;

	const auto fail = [this](std::string reason) {
		logger::error("[Unified Water] {}; distant water falls back to vanilla LOD", reason);
		failedLoadedMessage = std::move(reason);
	};

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\watermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		LogMeshLoadFailure(error, "meshes\\water\\WaterMesh.nif");
		fail("Failed to load water mesh");
		return;
	}
	if (!nif || nif->GetChildren().empty() || !nif->GetChildren().front()->AsNode() || nif->GetChildren().front()->AsNode()->GetChildren().empty()) {
		fail("Invalid water mesh hierarchy");
		return;
	}
	const auto waterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	if (!waterShape) {
		fail("Water mesh does not contain valid TriShape");
		return;
	}
	loadedWaterMesh = RE::NiPointer(waterShape);
	logger::debug("[Unified Water] Water mesh loaded");

	if (const auto error = RE::BSModelDB::Demand("meshes\\water\\optimisedwatermesh.nif", nif, args); error != RE::BSResource::ErrorCode::kNone) {
		LogMeshLoadFailure(error, "meshes\\water\\OptimisedWaterMesh.nif");
		fail("Failed to load optimised water mesh");
		return;
	}
	if (!nif || nif->GetChildren().empty() || !nif->GetChildren().front()->AsNode() || nif->GetChildren().front()->AsNode()->GetChildren().empty()) {
		fail("Invalid optimised water mesh hierarchy");
		return;
	}
	const auto optimisedWaterShape = nif->GetChildren().front()->AsNode()->GetChildren().front()->AsTriShape();
	if (!optimisedWaterShape) {
		fail("Optimised water mesh does not contain valid TriShape");
		return;
	}
	loadedOptimisedWaterMesh = RE::NiPointer(optimisedWaterShape);
	logger::debug("[Unified Water] Optimised water mesh loaded");

	// Construct every fallible resource before disabling vanilla. These remain
	// locally owned until the executable patches have also been validated.
	auto loadedFlowmap = std::make_unique<Flowmap>();
	auto loadedWaterCache = std::make_unique<WaterCache>();

	if (!DisableVanillaWaterLOD()) {
		fail("Could not disable vanilla water LOD");
		return;
	}

	// Publish the validated meshes together. Hooks continue using vanilla until
	// the cache pointer below completes the readiness invariant.
	waterMesh = std::move(loadedWaterMesh);
	optimisedWaterMesh = std::move(loadedOptimisedWaterMesh);
	flowmap = loadedFlowmap.release();
	waterCache = loadedWaterCache.release();
	failedLoadedMessage.clear();

	uint64_t pendingLoadOrderHash = 0;

	if (LoadOrderChanged(pendingLoadOrderHash)) {
		logger::info("[Unified Water] Load order or data revision changed, regenerating flowmap and caches");

		const bool flowmapRegenerated = flowmap->RegenerateAndLoadFlowmap();
		if (flowmapRegenerated)
			SetFlowmapTex();

		const bool cacheRegenerationStarted = waterCache->RegenerateCaches(
			[pendingLoadOrderHash, flowmapRegenerated](const bool succeeded) {
				if (!flowmapRegenerated)
					return;

				if (!succeeded) {
					logger::warn("[Unified Water] Generated data is incomplete; retaining the previous load-order hash so regeneration retries next launch");
				} else if (!PersistLoadOrderHash(pendingLoadOrderHash)) {
					logger::warn("[Unified Water] Failed to persist the regenerated data hash; regeneration will retry next launch");
				}
			});

		if (flowmapRegenerated && !cacheRegenerationStarted) {
			logger::warn("[Unified Water] Cache regeneration did not start; retaining the previous load-order hash so regeneration retries next launch");
		}
	} else {
		if (flowmap->LoadOrGenerateFlowmap())
			SetFlowmapTex();

		waterCache->LoadOrGenerateCaches();
	}

	// Runtime readers keep using the previously published cache snapshot (or the
	// vanilla path) until the complete replacement is published atomically.
	// Do not hold DataLoaded through an asynchronous disk build.
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

	bool ReadHashFile(const std::filesystem::path& a_path, uint64_t& a_hash)
	{
		for (int attempt = 0; attempt < kShareRetries; ++attempt) {
			winrt::file_handle handle{ CreateFileW(a_path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
			if (handle) {
				DWORD bytesRead = 0;
				const bool success = ReadFile(handle.get(), &a_hash, sizeof(a_hash), &bytesRead, nullptr) &&
				                     bytesRead == sizeof(a_hash);
				if (!success)
					logger::warn("[Unified Water] '{}' exists but could not be fully read; treating as no persisted hash", a_path.string());
				return success;
			}

			const DWORD error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
				return false;
			if ((error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) && attempt + 1 < kShareRetries) {
				Sleep(kShareRetryDelayMs);
				continue;
			}
			logger::warn("[Unified Water] Failed to open '{}' for reading (error {})", a_path.string(), error);
			return false;
		}
		return false;
	}

	bool WriteHashFile(const std::filesystem::path& a_path, uint64_t a_hash)
	{
		for (int attempt = 0; attempt < kShareRetries; ++attempt) {
			winrt::file_handle handle{ CreateFileW(a_path.c_str(), GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
			if (handle) {
				DWORD bytesWritten = 0;
				const bool success = WriteFile(handle.get(), &a_hash, sizeof(a_hash), &bytesWritten, nullptr) &&
				                     bytesWritten == sizeof(a_hash);
				if (!success)
					logger::error("[Unified Water] Failed to persist load-order hash to '{}'; cache will regenerate again next launch", a_path.string());
				return success;
			}

			const DWORD error = GetLastError();
			if ((error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) && attempt + 1 < kShareRetries) {
				Sleep(kShareRetryDelayMs);
				continue;
			}
			logger::error("[Unified Water] Failed to open '{}' for writing (error {}); cache will regenerate again next launch", a_path.string(), error);
			return false;
		}
		return false;
	}

	bool PersistLoadOrderHash(const uint64_t a_hash)
	{
		const auto path = GetLoadOrderHashPath();

		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		if (error) {
			logger::error("[Unified Water] Failed to create load-order hash directory '{}': {}; regeneration will retry next launch",
				path.parent_path().string(), error.message());
			return false;
		}

		return WriteHashFile(path, a_hash);
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

	stl::write_thunk_call<TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams>(REL::RelocationID(31388, 32179).address() + REL::Relocate(0x360, 0x3BC, 0x35B));
	stl::write_vfunc<0x4, BSWaterShaderMaterial_ComputeCRC32>(RE::VTABLE_BSWaterShaderMaterial[0]);

	stl::detour_thunk<BGSTerrainBlock_Attach>(REL::RelocationID(30934, 31737));

	stl::detour_thunk<BGSTerrainBlock_Detach>(REL::RelocationID(30936, 31739));

	stl::detour_thunk<BGSTerrainNode_UpdateWaterMeshSubVisibility>(REL::RelocationID(31059, 31846));

	stl::detour_thunk<TESWaterSystem_UpdateDisplacementMeshPosition>(REL::RelocationID(31384, 32175));

	stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);

	gWaterLOD = reinterpret_cast<RE::NiNode**>(REL::RelocationID(516171, 402322).address());
	gFlowMapSize = reinterpret_cast<int32_t*>(REL::RelocationID(527644, 414596).address());
	gFlowMapSourceTex = reinterpret_cast<RE::NiPointer<RE::NiSourceTexture>*>(REL::RelocationID(527694, 414616).address());
	gDisplacementCellTexCoordOffset = reinterpret_cast<float4*>(REL::RelocationID(528184, 415129).address());
	gDisplacementMeshPos = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(516235, 402400).address());
	gDisplacementMeshFlowCellOffset = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(528164, 415109).address());

	// Register before DataLoaded can be delayed by foreground shader compilation
	// or first-run cache generation.
	if (!MenuOpenCloseEventHandler::Register()) {
		logger::warn("[Unified Water] MenuOpenCloseEventHandler registration failed");
	}

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

bool UnifiedWater::IsWaterDataReady() const
{
	return waterCache && waterMesh && optimisedWaterMesh;
}

bool UnifiedWater::IsExteriorWorldspaceActive() const
{
	if (IsChildWorldSpace(currentPlayerWorldSpace.load(std::memory_order_acquire)))
		return true;

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
	if (!enteringChild)
		uw.pendingChildWsCull.store(false, std::memory_order_release);  // leaving child WS: discard any stale pending cull

	func(tes, worldSpace, isExterior);

	uw.exteriorWorldspaceActive.store(worldSpace && isExterior, std::memory_order_release);

	if (!uw.IsWaterDataReady()) {
		uw.pendingChildWsCull.store(false, std::memory_order_release);
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
	}
}

void UnifiedWater::TES_DestroySkyCell::thunk(RE::TES* tes)
{
	func(tes);

	auto& uw = globals::features::unifiedWater;
	uw.currentPlayerWorldSpace.store(nullptr, std::memory_order_release);
	uw.pendingChildWsCull.store(false, std::memory_order_release);
	uw.cachedTes.store(nullptr, std::memory_order_release);
	uw.exteriorWorldspaceActive.store(false, std::memory_order_release);

	if (uw.IsWaterDataReady())
		uw.waterCache->SetCurrentWorldSpace(nullptr);
	uw.UpdateWaterLODCull();
}

void UnifiedWater::BGSTerrainNode_UpdateWaterMeshSubVisibility::thunk(const RE::BGSTerrainNode* node, RE::BSMultiBoundNode* waterParent)
{
	if (!globals::features::unifiedWater.IsWaterDataReady()) {
		func(node, waterParent);
		return;
	}

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

	if (!waterSystem || !uw.IsWaterDataReady()) {
		func(block);
		return;
	}

	// Additional game-thread retry path for deferred child-WS cull completion.
	uw.TryCompleteDeferredChildWorldspaceCull(uw.cachedTes.load(std::memory_order_acquire));

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

		instructionResult = uw.waterCache->GetInstructions(worldSpace, lodLevel, node->baseCellX, node->baseCellY);
		const auto instructions = instructionResult.instructions;
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
		ClearWaterNodeChildren(water.get(), waterSystem);

		for (auto& instruction : *instructions) {
			if (!instruction.form.ptr)
				continue;

			RE::NiCloningProcess cloningProcess;

			const auto targetShape = lodLevel > 4 || uw.settings.UseOptimisedMeshes ? uw.optimisedWaterMesh : uw.waterMesh;
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
		uw.UpdateWaterLODCull();
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

	if (auto waterLOD = uw.gWaterLOD; waterLOD && *waterLOD) {
		RemoveDuplicateWaterSystemObjects(waterSystem, *waterLOD);
		DetachAllChildOccurrences(*waterLOD, water.get());
		(*waterLOD)->AttachChild(water.get(), true);
		uw.UpdateWaterLODCull();
	} else if (block->chunk) {
		// If the LOD root is unavailable, keep ownership on the chunk
		block->chunk->AttachChild(water.get(), true);
		block->waterAttached = false;
	} else {
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
				const bool cull = ShouldCullAtCell(tes, instruction->x, instruction->y);
				shape->SetAppCulled(cull);
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

	if (!uw.IsWaterDataReady()) {
		func(block);
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

void UnifiedWater::BSWaterShader_SetupGeometry::thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass)
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
			Util::WorldToCell(pass->geometry->world.translate, x, y);
			// CellTexCoordOffset.xyzw below - applies to non-displacement water only
			// xy is world cell flowmap based (0,0 is corner of flow map), zw is world cell
			// Funky maths here to counter what's being done in SetupGeometry
			// Previously these values were relative to the 5x5 flow grid centered on the player
			waterShaderProp->flowX = x + uw.flowmap->GetOffsetX();                                                     // CellTexCoordOffset.x
			waterShaderProp->flowY = y + uw.flowmap->GetOffsetY() + uw.flowmap->GetWidth() - uw.flowmap->GetHeight();  // CellTexCoordOffset.y
			waterShaderProp->cellX = x;                                                                                // CellTexCoordOffset.z
			waterShaderProp->cellY = y;                                                                                // CellTexCoordOffset.w
		}
	}

	func(waterShader, pass);
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
