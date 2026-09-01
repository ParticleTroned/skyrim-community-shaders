#include "InteriorSun.h"
#include "State.h"

#include <numbers>

#include "RE/B/BSMultiBoundRoom.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	InteriorSun::Settings,
	Enabled,
	ForceDoubleSidedRendering,
	ForceSingleShadowCascade,
	InteriorShadowDistance)

namespace
{
	struct ScopedFirstSliceDistanceOverride
	{
		ScopedFirstSliceDistanceOverride(RE::Setting* a_setting, float a_value) :
			setting(a_setting),
			original(a_setting ? a_setting->data.f : 0.0f),
			active(a_setting != nullptr)
		{
			if (active)
				setting->data.f = a_value;
		}

		~ScopedFirstSliceDistanceOverride()
		{
			if (active)
				setting->data.f = original;
		}

		RE::Setting* setting;
		float original;
		bool active;
	};
}

bool InteriorSun::DrawEnabledCheckbox()
{
	bool enabled = settings.Enabled;
	if (ImGui::Checkbox("Enable", &enabled))
		SetRuntimeEnabled(enabled);
	return enabled;
}

void InteriorSun::DrawSettings()
{
	const bool enabled = DrawEnabledCheckbox();
	ImGui::BeginDisabled(!enabled);

	ImGui::Checkbox("Force Double-Sided Rendering", &settings.ForceDoubleSidedRendering);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Disables backface culling during sun shadowmap rendering in interiors. "
			"Will prevent most light leaking through unmasked/unprepared interiors at a small performance cost. ");
	}
	ImGui::Checkbox("Force Single Shadow Cascade", &settings.ForceSingleShadowCascade);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Uses the high-detail directional shadow split for the full Interior Sun distance. "
			"Prevents prepared wall masks from falling into the lower-resolution later split.");
	}
	if (ImGui::SliderFloat("Interior Shadow Distance", &settings.InteriorShadowDistance, 1000.0f, 8000.0f)) {
		if (gInteriorShadowDistance) {
			*gInteriorShadowDistance = settings.InteriorShadowDistance;
			const auto* tes = RE::TES::GetSingleton();
			SetShadowDistance(tes && tes->interiorCell);
		}
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Sets the distance shadows are rendered at in interiors. "
			"Lower values provide higher quality shadows and improved performance but may cause distant interior spaces to light up incorrectly. ");
	}
	ImGui::EndDisabled();
}

void InteriorSun::DrawEssentialSettings()
{
	DrawEnabledCheckbox();
}

void InteriorSun::LoadSettings(json& o_json)
{
	settings = o_json;
	SetRuntimeEnabled(settings.Enabled);
}

void InteriorSun::SaveSettings(json& o_json)
{
	o_json = settings;
}

void InteriorSun::RestoreDefaultSettings()
{
	settings = {};
	SetRuntimeEnabled(settings.Enabled);
}

void InteriorSun::PostPostLoad()
{
	// Hooks and patch to enable directional lighting for interiors
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x399, 0x37D, 0x639));
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x3AE, 0x392, 0x64E));
	REL::safe_fill(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x397, 0x37B, 0x637), REL::NOP, 2);

	// Hook for overriding the rooms and portals passed to the directional light culling step to fix light leaking through unrendered geometry
	stl::detour_thunk<DirShadowLightCulling>(REL::RelocationID(101498, 108492));

	// Hooks and patches in AIProcess::CalculateLightValue to force interior cells with directional lights to perform raycast checks
	REL::safe_fill(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x1E7, 0x1F1), REL::NOP, REL::Module::IsAE() ? 2 : 6);
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x1ED, 0x1F3));
	REL::safe_fill(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x2CA, 0x22B), REL::NOP, REL::Module::IsAE() ? 6 : 2);

	gShadowDistance = reinterpret_cast<float*>(REL::RelocationID(528314, 415263).address());
	gInteriorShadowDistance = reinterpret_cast<float*>(REL::RelocationID(513755, 391724).address());
	vanillaInteriorShadowDistance = *gInteriorShadowDistance;
	fFirstSliceDistance = RE::GetINISetting("fFirstSliceDistance:Display");
	if (!fFirstSliceDistance)
		logger::warn("[Interior Sun] fFirstSliceDistance:Display not found; single shadow cascade mode is unavailable");

	// Patches BSShadowDirectionalLight::SetFrameCamera to read the correct shadow distance value in interior cells
	const std::uintptr_t address = REL::RelocationID(101499, 108496).address() + REL::Relocate(0xD62, 0xE6C, 0xE72);
	const std::int32_t displacement = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(gShadowDistance) - (address + 8));
	REL::safe_write(address + 4, &displacement, sizeof(displacement));

	stl::write_vfunc<0x10, BSShadowDirectionalLight_UpdateCamera>(RE::VTABLE_BSShadowDirectionalLight[0]);

	rasterStateCullMode = globals::game::isVR ? &globals::game::shadowState->GetVRRuntimeData().rasterStateCullMode : &globals::game::shadowState->GetRuntimeData().rasterStateCullMode;
	SetRuntimeEnabled(settings.Enabled);

	logger::info("[Interior Sun] Installed hooks");
}

void InteriorSun::EarlyPrepass()
{
	const auto* tes = RE::TES::GetSingleton();
	isInteriorWithSun.store(IsEnabled() && IsInteriorWithSun(tes ? tes->interiorCell : nullptr), std::memory_order_release);
}

inline bool InteriorSun::IsInteriorWithSun(const RE::TESObjectCELL* cell)
{
	return cell && cell->cellFlags.all(RE::TESObjectCELL::Flag::kIsInteriorCell, RE::TESObjectCELL::Flag::kShowSky, RE::TESObjectCELL::Flag::kUseSkyLighting, static_cast<RE::TESObjectCELL::Flag>(CellFlagExt::kSunlightShadows));
}

RE::TESWorldSpace* InteriorSun::GetWorldSpace::thunk(RE::TES* tes)
{
	if (tes) {
		if (const auto cell = tes->interiorCell) {
			// Patched interior callers dereference this result, so disabled mode must still return a non-null sentinel.
			if (!globals::features::interiorSun.IsEnabled())
				return disableInteriorSun;

			return IsInteriorWithSun(cell) ? enableInteriorSun : disableInteriorSun;
		}
	}
	return func(tes);
}

RE::TESWorldSpace* InteriorSun::enableInteriorSun = [] {
	alignas(RE::TESWorldSpace) static char buffer[sizeof(RE::TESWorldSpace)]{};
	return reinterpret_cast<RE::TESWorldSpace*>(buffer);
}();

RE::TESWorldSpace* InteriorSun::disableInteriorSun = [] {
	alignas(RE::TESWorldSpace) static char buffer[sizeof(RE::TESWorldSpace)] = {};
	const auto noShadows = reinterpret_cast<RE::TESWorldSpace*>(buffer);
	noShadows->flags.set(RE::TESWorldSpace::Flag::kNoSky, RE::TESWorldSpace::Flag::kFixedDimensions);
	return noShadows;
}();

void InteriorSun::DirShadowLightCulling::thunk(RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays, RE::BSTArray<RE::NiPointer<RE::NiAVObject>>& nodes)
{
	auto& singleton = globals::features::interiorSun;
	const auto* tes = RE::TES::GetSingleton();
	const auto* cell = tes ? tes->interiorCell : nullptr;
	auto* passedJobArrays = &jobArrays;

	if (cell && singleton.IsActiveInteriorSun()) {
		const auto* loadedData = cell->GetRuntimeData().loadedData;
		const auto portalGraph = loadedData ? loadedData->portalGraph : nullptr;
		if (portalGraph) {
			singleton.PopulateReplacementJobArrays(cell, portalGraph, dirLight, jobArrays);
			passedJobArrays = &singleton.replacementJobArrays;
		} else
			singleton.currentCell = nullptr;
	} else {
		if (!singleton.arraysCleared)
			singleton.ClearArrays();
		singleton.currentCell = nullptr;
	}

	func(dirLight, *passedJobArrays, nodes);
}

RE::BSEventNotifyControl InteriorSun::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event->menuName == RE::MainMenu::MENU_NAME) {
		if (a_event->opening)
			globals::features::interiorSun.isInteriorWithSun.store(false, std::memory_order_release);
	}

	return RE::BSEventNotifyControl::kContinue;
}

void InteriorSun::ClearArrays()
{
	currentCellRoomsAndPortals.clear();

	for (auto& jobArray : replacementJobArrays)
		jobArray.clear();

	arraysCleared = true;
}

void InteriorSun::SetRuntimeEnabled(bool a_enabled)
{
	settings.Enabled = a_enabled;
	runtimeEnabled.store(a_enabled, std::memory_order_release);

	const auto* tes = RE::TES::GetSingleton();
	const auto* interiorCell = tes ? tes->interiorCell : nullptr;
	isInteriorWithSun.store(a_enabled && loaded && IsInteriorWithSun(interiorCell), std::memory_order_release);

	if (gInteriorShadowDistance) {
		*gInteriorShadowDistance = a_enabled ? settings.InteriorShadowDistance : vanillaInteriorShadowDistance;
		SetShadowDistance(interiorCell != nullptr);
	}
}

void InteriorSun::PopulateReplacementJobArrays(const RE::TESObjectCELL* cell, const RE::NiPointer<RE::BSPortalGraph>& portalGraph, const RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays)
{
	if (cell != currentCell) {
		InitialiseOnNewCell(portalGraph);
		currentCell = cell;
	}

	const auto jobArraySize = jobArrays.size();
	if (jobArraySize == 0) {
		ClearArrays();
		currentCell = nullptr;
		return;
	}

	if (replacementJobArrays.size() != jobArraySize)
		replacementJobArrays.resize(jobArraySize);

	for (auto& jobArray : replacementJobArrays)
		jobArray.clear();

	addedSet.clear();

	// Copy the original job arrays contents into the replacement job arrays
	uint32_t count = 0;
	for (uint32_t i = 0; i < jobArraySize; ++i) {
		for (const auto& object : jobArrays[i]) {
			replacementJobArrays[i].push_back(object);
			addedSet.insert(object.get());
			count++;
		}
	}

	const auto playerPos = RE::PlayerCharacter::GetSingleton()->GetPosition();
	auto lightDir = -dirLight->GetShadowDirectionalLightRuntimeData().sunVector;
	lightDir.Unitize();

	// Add extra rooms and portals that are in the direction of the sun
	for (const auto& object : currentCellRoomsAndPortals) {
		if (addedSet.find(object.get()) != addedSet.end() || !IsInSunDirectionAndWithinShadowDistance(object, lightDir, playerPos))
			continue;

		addedSet.insert(object.get());
		replacementJobArrays[count++ % jobArraySize].push_back(object);
	}

	arraysCleared = false;
}

void InteriorSun::InitialiseOnNewCell(const RE::NiPointer<RE::BSPortalGraph>& portalGraph)
{
	currentCellRoomsAndPortals.clear();

	if (const auto portalSharedNode = portalGraph->portalSharedNode) {
		for (const auto room : portalGraph->rooms) {
			currentCellRoomsAndPortals.push_back(RE::NiPointer<RE::NiAVObject>(static_cast<RE::NiAVObject*>(room.get())));
		}

		for (auto child : portalGraph->portalSharedNode->GetChildren())
			currentCellRoomsAndPortals.push_back(child);
	}
}

bool InteriorSun::IsInSunDirectionAndWithinShadowDistance(const RE::NiPointer<RE::NiAVObject>& object, const RE::NiPoint3& lightDir, const RE::NiPoint3& playerPos) const
{
	const float radius = object->worldBound.radius;
	const auto diff = object->worldBound.center - playerPos;
	const float distance = diff.Length();
	const float projection = lightDir.Dot(diff);
	return projection >= -radius && (distance - radius) <= GetInteriorShadowDistance();
}

bool InteriorSun::ShouldForceSingleShadowCascade() const
{
	if (!(IsEnabled() && fFirstSliceDistance && settings.ForceSingleShadowCascade))
		return false;

	const auto* tes = RE::TES::GetSingleton();
	return tes && IsInteriorWithSun(tes->interiorCell);
}

float InteriorSun::GetInteriorShadowDistance() const
{
	if (gInteriorShadowDistance)
		return *gInteriorShadowDistance;
	if (gShadowDistance)
		return *gShadowDistance;
	return settings.InteriorShadowDistance;
}

void InteriorSun::ApplySingleShadowCascade(RE::BSShadowDirectionalLight* dirLight) const
{
	if (!dirLight)
		return;

	auto& runtimeData = dirLight->GetShadowDirectionalLightRuntimeData();
	const float shadowDistance = GetInteriorShadowDistance();

	runtimeData.startSplitDistances[0] = 0.0f;
	runtimeData.endSplitDistances[0] = shadowDistance;

	for (std::uint32_t i = 1; i < 3; ++i) {
		runtimeData.startSplitDistances[i] = shadowDistance;
		runtimeData.endSplitDistances[i] = shadowDistance;
	}
}

bool InteriorSun::BSShadowDirectionalLight_UpdateCamera::thunk(RE::BSShadowDirectionalLight* a_light, const RE::NiCamera* a_camera)
{
	auto& singleton = globals::features::interiorSun;
	const bool forceSingleCascade = singleton.ShouldForceSingleShadowCascade();
	const float shadowDistance = singleton.GetInteriorShadowDistance();

	ScopedFirstSliceDistanceOverride firstSliceOverride(forceSingleCascade ? singleton.fFirstSliceDistance : nullptr, shadowDistance);
	const bool result = func(a_light, a_camera);

	if (result && forceSingleCascade)
		singleton.ApplySingleShadowCascade(a_light);

	return result;
}

void InteriorSun::SetShadowDistance(bool inInterior)
{
	using func_t = decltype(SetShadowDistance);
	static REL::Relocation<func_t> func{ REL::RelocationID(98978, 105631).address() };
	func(inInterior);
}
