#include "LightEditor.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LightLimitFix.h"
#include "Menu.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/PointLightFlags.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <tuple>
#include <unordered_set>

namespace
{
	bool IsSafeLightPlacerConfigPath(std::string_view configPath)
	{
		if (configPath.empty() || configPath.find(':') != std::string_view::npos)
			return false;

		const std::filesystem::path relative(configPath);
		if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
			return false;

		for (const auto& component : relative) {
			if (component == ".." || component == ".")
				return false;
		}
		return true;
	}

	bool IsPathWithin(const std::filesystem::path& root, const std::filesystem::path& candidate)
	{
		auto rootIt = root.begin();
		auto candidateIt = candidate.begin();
		for (; rootIt != root.end(); ++rootIt, ++candidateIt) {
			if (candidateIt == candidate.end() ||
				_wcsicmp(rootIt->c_str(), candidateIt->c_str()) != 0) {
				return false;
			}
		}
		return true;
	}

	std::optional<std::filesystem::path> ResolveLightPlacerConfigPath(std::string_view configPath)
	{
		if (!IsSafeLightPlacerConfigPath(configPath))
			return std::nullopt;

		std::filesystem::path relative(configPath);
		if (_wcsicmp(relative.extension().c_str(), L".json") != 0)
			relative += L".json";

		std::error_code ec;
		const auto root = std::filesystem::weakly_canonical(
			std::filesystem::path("Data") / "LightPlacer", ec);
		if (ec)
			return std::nullopt;

		const auto candidate = std::filesystem::weakly_canonical(root / relative, ec);
		if (ec || !IsPathWithin(root, candidate))
			return std::nullopt;

		return candidate;
	}

	bool HasHexPrefix(std::string_view value)
	{
		return value.starts_with("0x") || value.starts_with("0X");
	}

	RE::FormID ResolveLightPlacerFormEntry(std::string_view entry)
	{
		const auto tildePosition = entry.find('~');
		const bool hasHexPrefix = HasHexPrefix(entry);
		if (tildePosition == std::string_view::npos && !hasHexPrefix)
			return 0;

		const std::size_t hexStart = hasHexPrefix ? 2 : 0;
		const std::size_t hexEnd =
			tildePosition == std::string_view::npos ? entry.size() : tildePosition;
		if (hexEnd <= hexStart ||
			(tildePosition != std::string_view::npos && tildePosition + 1 >= entry.size())) {
			return 0;
		}

		const auto formIDText = entry.substr(hexStart, hexEnd - hexStart);
		RE::FormID formID = 0;
		const auto [end, error] = std::from_chars(
			formIDText.data(),
			formIDText.data() + formIDText.size(),
			formID,
			16);
		if (error != std::errc{} || end != formIDText.data() + formIDText.size())
			return 0;

		if (tildePosition == std::string_view::npos)
			return formID;

		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler)
			return 0;

		const auto pluginName = entry.substr(tildePosition + 1);
		auto* form = dataHandler->LookupForm(formID, std::string(pluginName));
		return form ? form->GetFormID() : 0;
	}

	const nlohmann::json* GetJsonArray(const nlohmann::json& object, const char* key)
	{
		const auto it = object.find(key);
		return it != object.end() && it->is_array() ? &*it : nullptr;
	}

	std::string NormalizeModelPath(std::string path)
	{
		std::ranges::transform(path, path.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		std::ranges::replace(path, '\\', '/');
		return path;
	}

	bool HasLightPlacerFlag(std::string_view flags, std::string_view wanted)
	{
		while (!flags.empty()) {
			const auto delimiter = flags.find('|');
			const auto flag = flags.substr(0, delimiter);
			if (flag == wanted)
				return true;
			if (delimiter == std::string_view::npos)
				break;
			flags.remove_prefix(delimiter + 1);
		}
		return false;
	}

	bool TryReadLightPlacerFloat(
		const nlohmann::json& data,
		const char* key,
		float fallback,
		float minimum,
		float maximum,
		float& value)
	{
		const auto it = data.find(key);
		if (it != data.end() && !it->is_number())
			return false;

		double number = fallback;
		try {
			if (it != data.end())
				number = it->get<double>();
		} catch (const nlohmann::json::exception&) {
			return false;
		}
		if (!std::isfinite(number) ||
			number < static_cast<double>(minimum) ||
			number > static_cast<double>(maximum)) {
			return false;
		}

		value = static_cast<float>(number);
		return true;
	}

	bool LightColorsEqual(const RE::NiColor& lhs, const RE::NiColor& rhs)
	{
		return lhs.red == rhs.red &&
		       lhs.green == rhs.green &&
		       lhs.blue == rhs.blue;
	}
}

void LightEditor::DrawSettings()
{
	ImGui::Checkbox("Disable Regular Falloff Lights", &disableRegularLights);
	ImGui::Checkbox("Disable Inverse Square Falloff Lights", &disableInvSqLights);

	ImGui::Spacing();
	ImGui::Text("Total Lights: %u", totalLightCount);
	ImGui::Text("Active Shadow Lights: %u", activeShadowLightCount);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Checkbox("Shadows Only", &shadowsOnly);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Only show lights currently rendered as shadow lights.");
	}

	int selectedFilter = static_cast<int>(filterOption);
	if (ImGui::Combo("Filter By", &selectedFilter, FilterOptionLabels, static_cast<int>(FilterOption::Count))) {
		filterOption = static_cast<FilterOption>(selectedFilter);
	}

	int selectedSort = static_cast<int>(sortOption);
	if (ImGui::Combo("Sort By", &selectedSort, SortOptionLabels, static_cast<int>(SortOption::Count))) {
		sortOption = static_cast<SortOption>(selectedSort);
	}

	if (ImGui::BeginCombo("Lights", selected.isSelected ? GetLightName(selected).c_str() : "Select a light")) {
		for (auto& light : lights) {
			const auto displayName = GetLightName(light);
			const bool isSelected = light == selected;

			if (ImGui::Selectable(displayName.c_str(), isSelected))
				selected = light;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (!selected.isSelected)
		return;

	if (selected.isRef || selected.isAttached) {
		ImGui::Text("Owner: 0x%08X | %s", selected.id, displayInfo.ownerEditorId.c_str());
		ImGui::Text("Owner last edited by: %s", displayInfo.ownerLastEditedBy.c_str());
		ImGui::Text("Base Object: 0x%08X | %s", displayInfo.baseObjectFormId, selected.name.c_str());
		ImGui::Text("LIGH: 0x%08X | %s", displayInfo.lighFormId, displayInfo.lighEditorId.c_str());
		ImGui::Text("Cell: %s", displayInfo.cellEditorId.c_str());
	} else {
		ImGui::Text("Memory Address: %p", selected.ptr);
		ImGui::Text("NiLight Name: %s", selected.name.c_str());
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Button("Revert Changes")) {
		current = original;
		current.pos = { 0, 0, 0 };
		waitFrames = 1;
	}

	if (lpInfo.isLPLight) {
		ImGui::SameLine();
		ImGui::BeginDisabled(!lpInfo.hasAuthoredState);
		if (ImGui::Button("Save to Light Placer")) {
			SaveToLightPlacer();
		}
		ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				lpInfo.hasAuthoredState ?
					"Save current settings to the Light Placer JSON." :
					"Saving is unavailable because no unique Light Placer entry was found.");
		}
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (selected.isSpotlight)
		ImGui::TextDisabled("Spotlight: ISL light type flags not applicable");
	ImGui::BeginDisabled(selected.isSpotlight);
	const bool wasInverseSquare =
		current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);
	const bool inverseSquareChanged =
		ImGui::CheckboxFlags(
			"Inverse Square Light",
			reinterpret_cast<uint32_t*>(&current.data.flags),
			static_cast<uint32_t>(LightLimitFix::LightFlags::InverseSquare));
	ImGui::EndDisabled();
	if (inverseSquareChanged &&
		wasInverseSquare &&
		current.data.flags.none(LightLimitFix::LightFlags::InverseSquare) &&
		!(lpInfo.isLPLight && lpInfo.hasAuthoredState) &&
		current.data.radius == original.data.radius) {
		// For an existing IS light, radius is the derived runtime radius while
		// originalRadius is the authored regular-falloff baseline.
		current.data.radius = original.data.originalRadius;
	}
	ImGui::CheckboxFlags("Linear Light", reinterpret_cast<uint32_t*>(&current.data.flags), static_cast<uint32_t>(LightLimitFix::LightFlags::Linear));

	ImGui::Spacing();
	ImGui::Spacing();

	ImGui::ColorEdit3("Color", &current.data.diffuse.red);
	ImGui::SliderFloat("Intensity", &current.data.fade, 0.01f, 16.f, "%.3f");

	const auto isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);

	if (isInvSq)
		ImGui::BeginDisabled();
	ImGui::SliderFloat(
		"Radius",
		isInvSq ? &inverseSquareRadius : &current.data.radius,
		2.f, 8096.f, "%.0f");
	if (isInvSq)
		ImGui::EndDisabled();

	if (isInvSq) {
		ImGui::SliderFloat("Size", &current.data.size, 0.01f, 10.0f, "%.3f");
		ImGui::SliderFloat("Cutoff", &current.data.cutoffOverride, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (!selected.isOther && current.data.lighFormId != 0 && selected.hasPosition) {
		ImGui::Text("X: %.2f, Y: %.2f, Z: %.2f", displayInfo.pos.x, displayInfo.pos.y, displayInfo.pos.z);
		if (selected.isRef) {
			ImGui::Spacing();
			ImGui::SliderFloat3("Position Offset", &current.pos.x, -500.f, 500.f, "%.0f");
		}

		// LP bulbs author their flags in JSON. Mutating their shared base LIGH form can
		// rebuild unrelated references and make the selected attached bulb disappear.
		if (!lpInfo.isLPLight) {
			ImGui::Spacing();
			ImGui::Spacing();

			auto* flags = reinterpret_cast<uint32_t*>(&current.tesFlags);
			ImGui::Spacing();
			ImGui::Text("Light Flags");
			ImGui::CheckboxFlags("Dynamic", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kDynamic));
			ImGui::CheckboxFlags("Negative", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kNegative));
			ImGui::CheckboxFlags("Flicker", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlicker));
			ImGui::CheckboxFlags("Flicker Slow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlickerSlow));
			ImGui::CheckboxFlags("Pulse", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulse));
			ImGui::CheckboxFlags("Pulse Slow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulseSlow));
			ImGui::CheckboxFlags("Hemi Shadow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow));
			ImGui::CheckboxFlags("Omni Shadow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow));
			ImGui::CheckboxFlags("Portal Strict", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict));
		}
	}
}

std::string LightEditor::GetLightName(LightInfo& lightInfo)
{
	if (lightInfo.isRef)
		return fmt::format("0x{:08X} - {}", lightInfo.id, lightInfo.name.c_str());
	if (lightInfo.isAttached)
		return fmt::format("0x{:08X}|{} - {}", lightInfo.id, lightInfo.index, lightInfo.name.c_str());
	return fmt::format("{:p} - {}", lightInfo.ptr, lightInfo.name.c_str());
}

void LightEditor::GatherLights()
{
	if (State::GetSingleton()->IsPersistentMutationBlocked()) {
		RestoreOriginal();
		selected = {};
		previous = {};
		lights.clear();
		lightsAttached.clear();
		totalLightCount = 0;
		activeShadowLightCount = 0;
		return;
	}

	if (!Menu::GetSingleton()->ShouldSwallowInput()) {
		ResetOverrides();
		return;
	}

	if (waitFrames > 0) {
		waitFrames--;
		return;
	}

	bool foundSelected = false;

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& light) {
		const auto bsLight = light.get();
		if (!bsLight)
			return;

		const auto niLight = bsLight->light.get();
		if (!niLight)
			return;

		LightInfo current{};
		current.ptr = reinterpret_cast<void*>(niLight);
		RE::TESObjectLIGH* ligh = nullptr;

		const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
		if (!runtimeData)
			return;
		const auto refr = niLight->GetUserData();
		if (refr) {
			if (refr->IsDisabled())
				return;
			current.id = refr->GetFormID();
			current.index = lightsAttached[refr]++;
			if (auto* objRef = refr->GetObjectReference()) {
				if (objRef->GetFormType() == RE::FormType::Light)
					ligh = objRef->As<RE::TESObjectLIGH>();
				current.name = clib_util::editorID::get_editorID(objRef);
			}
		}

		current.isRef = ligh != nullptr;

		if (!current.isRef && runtimeData->lighFormId != 0) {
			if (auto* lightForm = RE::TESForm::LookupByID(runtimeData->lighFormId))
				ligh = lightForm->As<RE::TESObjectLIGH>();
		}

		current.isSpotlight =
			(PointLightFlags::GetPointLightTypeFlags(bsLight) &
				PointLightFlags::ToMask(PointLightFlags::Flags::Spot)) != 0;
		current.isShadow = bsLight->IsShadowLight();

		totalLightCount++;
		if (current.isShadow)
			activeShadowLightCount++;

		if (shadowsOnly && !current.isShadow) {
			return;
		}

		current.isAttached = !current.isRef && refr != nullptr;
		current.isOther = (!current.isRef && !current.isAttached) || (current.isSpotlight);

		const bool isRefMatch = (current.isRef && !current.isSpotlight) && filterOption == FilterOption::RefLights;
		const bool isAttachedMatch = current.isAttached && filterOption == FilterOption::AttachedLights;
		const bool isOtherMatch = current.isOther && filterOption == FilterOption::OtherLights;

		if (!(isRefMatch || isAttachedMatch || isOtherMatch))
			return;

		if (current.isRef) {
			current.position = refr->GetPosition();
			current.hasPosition = true;
		} else {
			current.position = niLight->world.translate;
			current.hasPosition = true;
		}
		if (current.isOther) {
			current.ptr = reinterpret_cast<void*>(niLight);
			if (current.name.empty())
				current.name = niLight->name.c_str();
			if (!current.isAttached)
				current.index = 0;
		}

		current.isSelected = selected == current;

		lights.push_back(current);

		if (!current.isSelected)
			return;
		selected = current;
		foundSelected = true;
		UpdateSelectedLight(refr, ligh, niLight);
	};

	lights.clear();
	lightsAttached.clear();
	totalLightCount = 0;
	activeShadowLightCount = 0;
	const auto smState = globals::game::smState;
	if (!smState || !smState->shadowSceneNode[0]) {
		ResetOverrides();
		lights.clear();
		lightsAttached.clear();
		return;
	}
	const auto shadowSceneNode = smState->shadowSceneNode[0];

	const auto& activeLights = shadowSceneNode->GetRuntimeData().activeLights;

	for (auto& light : activeLights) {
		addLight(light);
	}

	const auto& activeShadowLights = shadowSceneNode->GetRuntimeData().activeShadowLights;

	for (auto& light : activeShadowLights) {
		addLight(light);
	}

	if (!foundSelected) {
		RestoreOriginal();
		previous = {};
		selected = {};
	}

	SortLights();
}

void LightEditor::ResetOverrides()
{
	RestoreOriginal();
	selected = {};
	previous = {};
}

void LightEditor::RestoreDefaultSettings()
{
	if (State::GetSingleton()->IsPersistentMutationBlocked())
		return;

	RestoreOriginal();
	*this = {};
}

void LightEditor::UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight)
{
	if (State::GetSingleton()->IsPersistentMutationBlocked() || !niLight)
		return;

	auto tesFlags = ligh ? &ligh->data.flags : nullptr;
	const bool selectionChanged = previous != selected;

	if (selectionChanged) {
		const auto previousRefr = activeRefr.get();
		const bool recreatesPreviousLight =
			activeLigh && previousRefr && !lpInfo.isLPLight &&
			current.tesFlags.underlying() != original.tesFlags.underlying();
		RestoreOriginal();
		if (recreatesPreviousLight) {
			previous = {};
			selected.isSelected = false;
			waitFrames = 1;
			return;
		}
	}

	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	if (!runtimeData)
		return;

	if (selectionChanged) {
		original.tesFlags = tesFlags ? static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(tesFlags->underlying()) : static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(0);
		original.data = *runtimeData;
		original.pos = selected.isRef && refr ? refr->GetPosition() : RE::NiPoint3{};

		current = original;
		current.pos = { 0, 0, 0 };

		lpInfo = selected.isAttached ? ParseLPLightName(niLight->name.c_str()) : LPLightInfo{};
		if (lpInfo.isLPLight && refr) {
			if (auto* baseObj = refr->GetBaseObject()) {
				lpInfo.ownerEditorId = clib_util::editorID::get_editorID(baseObj);
				if (auto* model = baseObj->As<RE::TESModel>()) {
					if (const char* path = model->GetModel())
						lpInfo.ownerModelPath = path;
				}
			}
		}

		activeIsRef = selected.isRef;
		activeRefr = refr ? RE::ObjectRefHandle(refr) : RE::ObjectRefHandle{};
		activeLigh = ligh;

		if (lpInfo.isLPLight)
			LoadLightPlacerAuthoredState(refr);

		previous = selected;
	}

	activeNiLight.reset(niLight);

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)) {
		inverseSquareRadius = InverseSquareLighting::CalculateRadius(
			current.data.fade * GetLightPlacerFadeFactor() * 4.f,
			selected.isShadow,
			std::clamp(current.data.cutoffOverride, 0.01f, 1.0f),
			current.data.size * GetLightPlacerSizeFactor());
	} else {
		inverseSquareRadius =
			current.data.radius * GetLightPlacerRadiusFactor();
	}

	if (selected.isRef) {
		const auto currentPos = refr->GetPosition();
		const auto newPos = original.pos + current.pos;
		if (currentPos != newPos) {
			refr->SetPosition(newPos);
			waitFrames = 1;
		}
		displayInfo.pos = newPos;
	} else if (selected.isAttached) {
		// Attached lights can share their parent with the source mesh. Moving that
		// node moves the mesh, not just the bulb.
		displayInfo.pos = niLight->world.translate;
	}

	if (!selected.isOther && !lpInfo.isLPLight && refr && tesFlags &&
		current.tesFlags.underlying() != tesFlags->underlying()) {
		*tesFlags = static_cast<RE::TES_LIGHT_FLAGS>(current.tesFlags.underlying());
		refr->Disable();
		refr->Enable(false);
		waitFrames = 1;
	}

	displayInfo.ownerFormId = refr ? refr->GetFormID() : 0;
	displayInfo.ownerEditorId = refr ? clib_util::editorID::get_editorID(refr) : "Unknown";
	displayInfo.baseObjectFormId = refr && refr->GetBaseObject() ? refr->GetBaseObject()->formID : 0;
	displayInfo.ownerLastEditedBy = refr && refr->GetDescriptionOwnerFile() ? refr->GetDescriptionOwnerFile()->fileName : "Unknown";
	displayInfo.cellEditorId =
		refr && refr->GetParentCell() ?
			clib_util::editorID::get_editorID(refr->GetParentCell()) :
			"Unknown";
	displayInfo.lighFormId = ligh ? ligh->GetFormID() : 0;
	displayInfo.lighEditorId = ligh ? clib_util::editorID::get_editorID(ligh) : "Unknown";
}

float LightEditor::GetLightPlacerFadeFactor() const
{
	return lpInfo.isLPLight && lpInfo.hasAuthoredState ?
	           lpInfo.fadeFactor :
	           1.0f;
}

float LightEditor::GetLightPlacerRadiusFactor() const
{
	return lpInfo.isLPLight && lpInfo.hasAuthoredState ?
	           lpInfo.radiusFactor :
	           1.0f;
}

float LightEditor::GetLightPlacerSizeFactor() const
{
	return lpInfo.isLPLight && lpInfo.hasAuthoredState ?
	           lpInfo.sizeFactor :
	           1.0f;
}

void LightEditor::ApplyCurrentRuntimeData(
	ISLCommon::RuntimeLightDataExt& runtimeData) const
{
	runtimeData.diffuse = current.data.diffuse;
	runtimeData.fade = current.data.fade * GetLightPlacerFadeFactor();
	runtimeData.cutoffOverride = current.data.cutoffOverride;
	runtimeData.size = current.data.size * GetLightPlacerSizeFactor();

	const bool isInverseSquare =
		current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);
	const bool radiusWasEdited = current.data.radius != original.data.radius;
	if ((lpInfo.isLPLight && lpInfo.hasAuthoredState) ||
		!isInverseSquare ||
		original.data.flags.none(LightLimitFix::LightFlags::InverseSquare) ||
		radiusWasEdited) {
		runtimeData.originalRadius =
			current.data.radius * GetLightPlacerRadiusFactor();
	}

	if (isInverseSquare) {
		runtimeData.flags.set(LightLimitFix::LightFlags::InverseSquare);
	} else {
		runtimeData.flags.reset(LightLimitFix::LightFlags::InverseSquare);
		runtimeData.radius = runtimeData.originalRadius;
	}

	if (current.data.flags.any(LightLimitFix::LightFlags::Linear))
		runtimeData.flags.set(LightLimitFix::LightFlags::Linear);
	else
		runtimeData.flags.reset(LightLimitFix::LightFlags::Linear);
}

bool LightEditor::ApplyOverrides(RE::NiLight* niLight, ISLCommon::RuntimeLightDataExt* runtimeData) const
{
	if (State::GetSingleton()->IsPersistentMutationBlocked() || !niLight || !runtimeData)
		return false;

	if (niLight != activeNiLight.get())
		return false;

	ApplyCurrentRuntimeData(*runtimeData);
	return true;
}

void LightEditor::RestoreOriginal()
{
	if (!activeNiLight)
		return;

	auto* runtimeData = ISLCommon::RuntimeLightDataExt::Get(activeNiLight.get());
	if (runtimeData) {
		if (lpInfo.isLPLight && lpInfo.hasAuthoredState)
			*runtimeData = lpInfo.runtimeSnapshot;
		else
			*runtimeData = original.data;
	}

	const auto refr = activeRefr.get();
	if (activeIsRef && refr)
		refr->SetPosition(original.pos);

	if (activeLigh && refr && !lpInfo.isLPLight &&
		current.tesFlags.underlying() != original.tesFlags.underlying()) {
		activeLigh->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(original.tesFlags.underlying());
		refr->Disable();
		refr->Enable(false);
	}

	activeNiLight.reset();
	activeRefr = {};
	activeLigh = nullptr;
	activeIsRef = false;
}

LightEditor::LPLightInfo LightEditor::ParseLPLightName(const std::string& name)
{
	LPLightInfo info;

	constexpr std::string_view prefix = "LP_Light[";
	if (!name.starts_with(prefix))
		return info;

	auto bracketEnd = name.find(']');
	if (bracketEnd == std::string::npos)
		return info;

	auto inner = name.substr(prefix.size(), bracketEnd - prefix.size());
	auto pipePos = inner.find('|');
	if (pipePos == std::string::npos)
		return info;

	info.configPath = inner.substr(0, pipePos);
	info.lightEDID = inner.substr(pipePos + 1);

	if (!IsSafeLightPlacerConfigPath(info.configPath) || info.lightEDID.empty()) {
		logger::warn("[LightEditor] Rejected malformed LP light name: {}", name);
		return info;
	}

	info.isLPLight = true;
	return info;
}

std::string LightEditor::UpdateLPFlags(const std::string& existingFlags, bool inverseSquare, bool linear)
{
	std::vector<std::string> flags;
	if (!existingFlags.empty()) {
		std::istringstream ss(existingFlags);
		std::string flag;
		while (std::getline(ss, flag, '|')) {
			if (!flag.empty() &&
				flag != "InverseSquare" &&
				flag != "Linear") {
				flags.push_back(flag);
			}
		}
	}
	if (inverseSquare)
		flags.push_back("InverseSquare");
	if (linear)
		flags.push_back("Linear");

	std::string result;
	for (size_t i = 0; i < flags.size(); ++i) {
		if (i > 0)
			result += "|";
		result += flags[i];
	}
	return result;
}

bool LightEditor::MatchesLPFilters(const json& lightEntry, RE::TESObjectREFR* refr)
{
	if (!refr)
		return true;

	std::unordered_set<RE::FormID> formIDs;
	std::unordered_set<std::string> editorIDs;
	auto addForm = [&](RE::TESForm* form) {
		if (!form)
			return;

		formIDs.insert(form->GetFormID());
		auto editorID = clib_util::editorID::get_editorID(form);
		if (!editorID.empty())
			editorIDs.insert(std::move(editorID));
	};

	addForm(refr);
	addForm(refr->GetBaseObject());
	auto* cell = refr->GetParentCell();
	addForm(cell);
	addForm(refr->GetWorldspace());

	std::unordered_set<RE::BGSLocation*> visitedLocations;
	auto* location = refr->GetCurrentLocation();
	if (!location && cell)
		location = cell->GetLocation();
	for (; location && visitedLocations.insert(location).second; location = location->parentLoc) {
		addForm(location);
	}

	auto matchesEntry = [&](const std::string& entry) -> bool {
		if (entry.find('~') != std::string::npos || HasHexPrefix(entry)) {
			const RE::FormID resolvedId = ResolveLightPlacerFormEntry(entry);
			return resolvedId != 0 && formIDs.contains(resolvedId);
		}
		return editorIDs.contains(entry);
	};

	auto anyMatches = [&](const json& list) {
		for (const auto& item : list)
			if (item.is_string() && matchesEntry(item.get<std::string>()))
				return true;
		return false;
	};

	for (const auto& [key, isWhitelist] :
		{ std::pair{ "whiteList", true }, std::pair{ "blackList", false } }) {
		const auto it = lightEntry.find(key);
		if (it == lightEntry.end())
			continue;
		if (!it->is_array())
			return false;

		const bool matches = anyMatches(*it);
		if ((isWhitelist && !matches) || (!isWhitelist && matches))
			return false;
	}

	return true;
}

bool LightEditor::LoadLightPlacerConfig(
	json& configArray,
	std::filesystem::path& filePath) const
{
	const auto resolvedPath = ResolveLightPlacerConfigPath(lpInfo.configPath);
	if (!resolvedPath) {
		logger::warn("[LightEditor] Rejected Light Placer config path: {}", lpInfo.configPath);
		return false;
	}
	filePath = *resolvedPath;

	std::error_code existsError;
	if (!std::filesystem::exists(filePath, existsError)) {
		if (existsError) {
			logger::warn(
				"[LightEditor] Could not inspect Light Placer config {}: {}",
				filePath.string(), existsError.message());
			return false;
		}
		logger::warn("[LightEditor] Light Placer config not found: {}", filePath.string());
		return false;
	}

	std::string readError;
	if (Util::FileHelpers::ReadJsonFile(filePath, configArray, readError) !=
		Util::FileHelpers::JsonFileReadResult::Success) {
		logger::warn(
			"[LightEditor] Failed to read Light Placer config {}: {}",
			filePath.string(), readError);
		return false;
	}

	if (!configArray.is_array()) {
		logger::warn("[LightEditor] Light Placer config root is not an array: {}", filePath.string());
		return false;
	}

	return true;
}

json* LightEditor::FindUniqueLightPlacerData(
	json& configArray,
	RE::TESObjectREFR* refr) const
{
	const std::string normalizedOwner = NormalizeModelPath(lpInfo.ownerModelPath);
	const RE::FormID ownerBaseFormID =
		refr && refr->GetBaseObject() ? refr->GetBaseObject()->GetFormID() : 0;
	std::vector<json*> ownerCandidates;
	std::vector<json*> filteredCandidates;
	std::vector<json*> unfilteredCandidates;

	for (auto& entry : configArray) {
		auto lightsIt = entry.find("lights");
		if (lightsIt == entry.end() || !lightsIt->is_array())
			continue;

		bool entryMatches = false;
		if (auto* models = GetJsonArray(entry, "models"); !normalizedOwner.empty() && models) {
			entryMatches = std::ranges::any_of(*models, [&](const json& value) {
				return value.is_string() &&
				       NormalizeModelPath(value.get<std::string>()) == normalizedOwner;
			});
		}

		auto matchesOwnerFormArray = [&](const char* key) {
			const auto* forms = GetJsonArray(entry, key);
			return forms && std::ranges::any_of(*forms, [&](const json& value) {
				if (!value.is_string())
					return false;

				const auto formEntry = value.get<std::string>();
				if (formEntry.find('~') == std::string::npos && !HasHexPrefix(formEntry))
					return !lpInfo.ownerEditorId.empty() && formEntry == lpInfo.ownerEditorId;
				return ownerBaseFormID != 0 &&
				       ResolveLightPlacerFormEntry(formEntry) == ownerBaseFormID;
			});
		};

		// visualEffects was the original name for form-targeted entries. LP accepts
		// both spellings, so the editor must resolve both as the same owner selector.
		entryMatches = entryMatches ||
		               matchesOwnerFormArray("formIDs") ||
		               matchesOwnerFormArray("visualEffects");

		for (auto& lightEntry : *lightsIt) {
			const auto dataIt = lightEntry.find("data");
			if (dataIt == lightEntry.end() || !dataIt->is_object())
				continue;
			auto& data = *dataIt;
			const auto lightIt = data.find("light");
			if (lightIt == data.end() || !lightIt->is_string() ||
				lightIt->get<std::string>() != lpInfo.lightEDID) {
				continue;
			}

			unfilteredCandidates.push_back(&data);
			if (!MatchesLPFilters(lightEntry, refr))
				continue;

			filteredCandidates.push_back(&data);
			if (entryMatches)
				ownerCandidates.push_back(&data);
		}
	}

	if (ownerCandidates.size() == 1)
		return ownerCandidates.front();

	if (ownerCandidates.size() > 1) {
		logger::warn(
			"[LightEditor] Refusing ambiguous Light Placer edit: {} entries match model '{}' and light '{}' in {}",
			ownerCandidates.size(), lpInfo.ownerModelPath, lpInfo.lightEDID, lpInfo.configPath);
		return nullptr;
	}

	// Some valid LP owners (worn items, casting art, and reference effects) do
	// not expose the top-level model/form selector through GetBaseObject().
	// Falling back is safe only when the light and reference filters identify
	// exactly one entry in this already-resolved config file.
	if (filteredCandidates.size() == 1)
		return filteredCandidates.front();

	if (filteredCandidates.size() > 1) {
		logger::warn(
			"[LightEditor] Refusing ambiguous Light Placer fallback: {} entries match light '{}' in {}",
			filteredCandidates.size(), lpInfo.lightEDID, lpInfo.configPath);
		return nullptr;
	}

	// LP may have attached the bulb through a worn item, art/effect source, or
	// Merge Mapper identity that is not recoverable from the NiLight user data.
	// The live bulb proves its source-side filters passed; bypass them only when
	// config path + light EDID still identify one entry unambiguously.
	if (unfilteredCandidates.size() == 1)
		return unfilteredCandidates.front();

	if (unfilteredCandidates.size() > 1) {
		logger::warn(
			"[LightEditor] Refusing ambiguous unfiltered Light Placer fallback: {} entries match light '{}' in {}",
			unfilteredCandidates.size(), lpInfo.lightEDID, lpInfo.configPath);
	}
	return nullptr;
}

bool LightEditor::LoadLightPlacerAuthoredState(RE::TESObjectREFR* refr)
{
	if (!lpInfo.isLPLight || !refr)
		return false;

	lpInfo.ignoreScale = false;
	lpInfo.hasAuthoredState = false;
	lpInfo.fadeFactor = 1.0f;
	lpInfo.radiusFactor = 1.0f;
	lpInfo.sizeFactor = 1.0f;

	json configArray;
	std::filesystem::path filePath;
	if (!LoadLightPlacerConfig(configArray, filePath))
		return false;

	auto* data = FindUniqueLightPlacerData(configArray, refr);
	if (!data) {
		logger::warn(
			"[LightEditor] No unique Light Placer entry for model '{}' and light '{}' in {}",
			lpInfo.ownerModelPath, lpInfo.lightEDID, filePath.string());
		return false;
	}

	std::string flags;
	if (const auto flagsIt = data->find("flags"); flagsIt != data->end()) {
		if (!flagsIt->is_string()) {
			logger::warn(
				"[LightEditor] Light Placer flags are not a string for '{}' in {}",
				lpInfo.lightEDID, filePath.string());
			return false;
		}
		flags = flagsIt->get<std::string>();
	}
	lpInfo.ignoreScale = HasLightPlacerFlag(flags, "IgnoreScale");

	if (!activeLigh) {
		logger::warn(
			"[LightEditor] Cannot resolve base LIGH '{}' for Light Placer entry in {}",
			lpInfo.lightEDID, filePath.string());
		return false;
	}

	const auto runtimeSnapshot = original.data;
	float fallbackFactor = lpInfo.ignoreScale ? 1.0f : refr->GetScale();
	if (!std::isfinite(fallbackFactor) || fallbackFactor <= 0.0f)
		fallbackFactor = 1.0f;

	const float baseFade = activeLigh->fade;
	const float baseRadius = static_cast<float>(activeLigh->data.radius);
	const float baseSize = activeLigh->data.fov;
	const float baseCutoff = activeLigh->data.fallofExponent;

	float fadeValue = baseFade;
	float radiusValue = baseRadius;
	float sizeValue = baseSize;
	float cutoffValue = baseCutoff;

	auto readFloat = [&](const char* key, float fallback, float minimum, float maximum, float& value) {
		if (TryReadLightPlacerFloat(*data, key, fallback, minimum, maximum, value))
			return true;
		logger::warn(
			"[LightEditor] Invalid Light Placer '{}' value for light '{}' in {}",
			key, lpInfo.lightEDID, filePath.string());
		return false;
	};

	constexpr float maxFloat = std::numeric_limits<float>::max();
	if (!readFloat("fade", baseFade, -maxFloat, maxFloat, fadeValue) ||
		!readFloat("radius", baseRadius, -maxFloat, maxFloat, radiusValue) ||
		!readFloat("size", baseSize, -maxFloat, maxFloat, sizeValue) ||
		!readFloat("cutoff", baseCutoff, -maxFloat, maxFloat, cutoffValue)) {
		return false;
	}

	// Mirror LP's effective-value rules. Non-positive JSON values select the
	// base LIGH value; legacy FOV-sized bulbs use LP's literal 1.414 value.
	const float fade = fadeValue > 0.0f ? fadeValue : baseFade;
	const float radius = radiusValue > 0.0f ? radiusValue : baseRadius;
	float size = sizeValue > 0.0f ? sizeValue : baseSize;
	float cutoff = cutoffValue > 0.0f ? cutoffValue : baseCutoff;
	if (size >= 50.0f)
		size = 1.414f;
	size = std::clamp(size, 0.01f, 50.0f);
	cutoff = std::clamp(cutoff, 0.01f, 1.0f);

	const auto inferFactor = [](float runtimeValue, float rawValue, float fallback) {
		if (std::isfinite(runtimeValue) &&
			std::isfinite(rawValue) &&
			rawValue != 0.0f) {
			const float factor = runtimeValue / rawValue;
			if (std::isfinite(factor) && factor >= 0.0f)
				return factor;
		}
		return fallback;
	};
	const float runtimeRadius =
		runtimeSnapshot.flags.any(LightLimitFix::LightFlags::Initialised) ?
			runtimeSnapshot.originalRadius :
			runtimeSnapshot.radius;
	lpInfo.fadeFactor =
		inferFactor(runtimeSnapshot.fade, fade, fallbackFactor);
	lpInfo.radiusFactor =
		inferFactor(runtimeRadius, radius, fallbackFactor);
	lpInfo.sizeFactor =
		inferFactor(runtimeSnapshot.size, size, fallbackFactor);
	lpInfo.runtimeSnapshot = runtimeSnapshot;

	original.data.fade = current.data.fade = fade;
	original.data.radius = current.data.radius = radius;
	original.data.originalRadius = current.data.originalRadius = radius;
	original.data.size = current.data.size = size;
	original.data.cutoffOverride = current.data.cutoffOverride = cutoff;
	lpInfo.hasAuthoredState = true;
	return true;
}

bool LightEditor::SaveToLightPlacer()
{
	if (!lpInfo.isLPLight || !lpInfo.hasAuthoredState)
		return false;

	const auto refr = activeRefr.get();
	if (!refr)
		return false;

	json configArray;
	std::filesystem::path filePath;
	if (!LoadLightPlacerConfig(configArray, filePath))
		return false;

	auto* data = FindUniqueLightPlacerData(configArray, refr.get());
	if (!data) {
		logger::warn(
			"[LightEditor] No unique Light Placer entry for model '{}' and light '{}' in {}",
			lpInfo.ownerModelPath, lpInfo.lightEDID, filePath.string());
		return false;
	}

	const auto valuesAreFinite =
		std::isfinite(current.data.diffuse.red) &&
		std::isfinite(current.data.diffuse.green) &&
		std::isfinite(current.data.diffuse.blue) &&
		std::isfinite(current.data.fade) &&
		std::isfinite(current.data.radius) &&
		std::isfinite(current.data.size) &&
		std::isfinite(current.data.cutoffOverride);
	if (!valuesAreFinite ||
		current.data.fade < 0.0f ||
		current.data.radius < 0.0f) {
		logger::warn("[LightEditor] Refusing to save non-finite or negative Light Placer values");
		return false;
	}

	// Existing LP colors can be integer, normalized, or supplied by another
	// feature. Preserve the JSON verbatim until the color control is edited.
	if (!LightColorsEqual(current.data.diffuse, original.data.diffuse)) {
		auto authoredColor = current.data.diffuse;
		if (activeLigh &&
			activeLigh->data.flags.any(RE::TES_LIGHT_FLAGS::kNegative)) {
			authoredColor.red = -authoredColor.red;
			authoredColor.green = -authoredColor.green;
			authoredColor.blue = -authoredColor.blue;
		}
		(*data)["color"] = {
			std::clamp(authoredColor.red, -1.0f, 1.0f),
			std::clamp(authoredColor.green, -1.0f, 1.0f),
			std::clamp(authoredColor.blue, -1.0f, 1.0f)
		};
	}

	(*data)["fade"] = current.data.fade;
	const bool isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);
	if (isInvSq) {
		(*data)["size"] = std::clamp(current.data.size, 0.01f, 50.0f);
		(*data)["cutoff"] = std::clamp(current.data.cutoffOverride, 0.01f, 1.0f);
		if (current.data.radius != original.data.radius)
			(*data)["radius"] = current.data.radius;
	} else {
		(*data)["radius"] = current.data.radius;
		if (current.data.size != original.data.size)
			(*data)["size"] = std::clamp(current.data.size, 0.01f, 50.0f);
		if (current.data.cutoffOverride != original.data.cutoffOverride)
			(*data)["cutoff"] =
				std::clamp(current.data.cutoffOverride, 0.01f, 1.0f);
	}

	std::string existingFlags;
	if (const auto flags = data->find("flags"); flags != data->end()) {
		if (!flags->is_string()) {
			logger::warn(
				"[LightEditor] Refusing to overwrite malformed flags for '{}' in {}",
				lpInfo.lightEDID, filePath.string());
			return false;
		}
		existingFlags = flags->get<std::string>();
	}
	const bool isLinear = current.data.flags.any(LightLimitFix::LightFlags::Linear);
	const std::string newFlags = UpdateLPFlags(existingFlags, isInvSq, isLinear);
	if (!newFlags.empty())
		(*data)["flags"] = newFlags;
	else
		data->erase("flags");

	std::string serializedConfig;
	try {
		serializedConfig = configArray.dump(1, '\t');
	} catch (const std::exception& e) {
		logger::warn(
			"[LightEditor] Failed to serialize Light Placer config {}: {}",
			filePath.string(), e.what());
		return false;
	}

	std::string writeError;
	if (!Util::FileHelpers::WriteTextFileAtomic(filePath, serializedConfig, writeError)) {
		logger::warn("[LightEditor] Failed to save Light Placer config {}: {}", filePath.string(), writeError);
		return false;
	}

	auto savedRuntime = lpInfo.runtimeSnapshot;
	ApplyCurrentRuntimeData(savedRuntime);
	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)) {
		savedRuntime.radius = InverseSquareLighting::CalculateRadius(
			savedRuntime.fade * 4.0f,
			selected.isShadow,
			std::clamp(savedRuntime.cutoffOverride, 0.01f, 1.0f),
			savedRuntime.size);
	}
	lpInfo.runtimeSnapshot = savedRuntime;
	original.data = current.data;
	original.data.originalRadius = original.data.radius;
	logger::info("[LightEditor] Saved light settings to {}", filePath.string());
	return true;
}

void LightEditor::SortLights()
{
	if (filterOption == FilterOption::OtherLights && (sortOption == SortOption::FormID || sortOption == SortOption::EditorID))
		sortOption = SortOption::None;

	switch (sortOption) {
	case SortOption::Distance:
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player)
				break;
			const auto playerPos = player->GetPosition();
			std::ranges::sort(lights, [&](const LightInfo& a, const LightInfo& b) {
				if (a.hasPosition != b.hasPosition)
					return a.hasPosition;
				return a.position.GetSquaredDistance(playerPos) < b.position.GetSquaredDistance(playerPos);
			});
			break;
		}
	case SortOption::FormID:
		std::ranges::sort(lights, [](const LightInfo& a, const LightInfo& b) {
			return std::tie(a.id, a.index) < std::tie(b.id, b.index);
		});
		break;
	case SortOption::EditorID:
		std::ranges::sort(lights, [](const LightInfo& a, const LightInfo& b) {
			return a.name < b.name;
		});
		break;
	case SortOption::None:
	default:
		break;
	}
}
