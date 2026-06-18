#include "AdaptiveBrightness.h"

#include "LocationContext.h"
#include "State.h"
#include "Utils/Form.h"
#include "Utils/UI.h"

#include "RE/B/BGSLocation.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/Sky.h"
#include "RE/T/TESObjectCELL.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <utility>

namespace
{
	constexpr float kBrightnessMin = 0.25f;
	constexpr float kBrightnessMax = 2.0f;
	constexpr float kGammaOffsetMin = -1.0f;
	constexpr float kGammaOffsetMax = 1.0f;

	using Profile = AdaptiveBrightness::Profile;

	constexpr std::array<Profile, AdaptiveBrightness::kProfileCount> kProfileOrder{
		Profile::ExteriorDay,
		Profile::ExteriorNight,
		Profile::Interior,
		Profile::Dungeon,
		Profile::Dwelling
	};

	constexpr std::array<const char*, AdaptiveBrightness::kProfileCount> kProfileNames{
		"Exterior Day",
		"Exterior Night",
		"Interior",
		"Dungeon",
		"Dwelling"
	};

	constexpr const char* kOverrideTypeLocation = "Location";
	constexpr const char* kOverrideTypeCell = "Cell";

	struct CurrentLocationForms
	{
		const RE::BGSLocation* location = nullptr;
		const RE::TESObjectCELL* cell = nullptr;
	};

	std::size_t ProfileIndex(Profile a_profile)
	{
		return std::clamp(
			static_cast<std::size_t>(a_profile),
			static_cast<std::size_t>(0),
			AdaptiveBrightness::kProfileCount - 1);
	}

	float SafeFinite(float a_value, float a_fallback)
	{
		return std::isfinite(a_value) ? a_value : a_fallback;
	}

	float ClampMultiplier(float a_value)
	{
		return std::clamp(SafeFinite(a_value, 1.0f), 0.0f, 10.0f);
	}

	float ClampGamma(float a_value)
	{
		return std::clamp(SafeFinite(a_value, 1.0f), 0.1f, 3.0f);
	}

	float ClampGammaOffset(float a_value)
	{
		return std::clamp(SafeFinite(a_value, 0.0f), kGammaOffsetMin, kGammaOffsetMax);
	}

	float ClampBrightness(float a_value)
	{
		return std::clamp(SafeFinite(a_value, 1.0f), kBrightnessMin, kBrightnessMax);
	}

	float WrapHour(float a_hour)
	{
		if (!std::isfinite(a_hour))
			return 12.0f;

		auto wrapped = std::fmod(a_hour, 24.0f);
		if (wrapped < 0.0f)
			wrapped += 24.0f;

		return wrapped;
	}

	float HoursSince(float a_startHour, float a_currentHour)
	{
		auto delta = WrapHour(a_currentHour) - WrapHour(a_startHour);
		if (delta < 0.0f)
			delta += 24.0f;

		return delta;
	}

	float SmoothStep(float a_edge0, float a_edge1, float a_x)
	{
		if (a_edge1 <= a_edge0)
			return a_x >= a_edge1 ? 1.0f : 0.0f;

		const float t = std::clamp((a_x - a_edge0) / (a_edge1 - a_edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	bool IsValidFormKey(const std::string& a_key)
	{
		return !a_key.empty() && a_key != "Invalid";
	}

	std::string GetFormDisplayName(const RE::TESForm* a_form)
	{
		if (!a_form)
			return "";

		const auto* name = a_form->GetName();
		if (name && name[0] != '\0')
			return name;

		auto editorID = Util::GetFormEditorID(a_form);
		if (!editorID.empty())
			return editorID;

		return Util::GetFormFileKey(a_form);
	}

	std::string GetCellCocCode(const RE::TESObjectCELL* a_cell)
	{
		if (!a_cell)
			return "";

		return Util::GetFormEditorID(a_cell);
	}

	const char* GetCocLabel(const std::string& a_cocCode)
	{
		return a_cocCode.empty() ? "-" : a_cocCode.c_str();
	}

	void DrawTableWrappedText(const char* a_text)
	{
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextUnformatted(a_text);
		ImGui::PopTextWrapPos();
	}

	int CompareString(const std::string& a_lhs, const std::string& a_rhs)
	{
		if (a_lhs == a_rhs)
			return 0;

		return a_lhs < a_rhs ? -1 : 1;
	}

	CurrentLocationForms GetCurrentLocationForms()
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->parentCell : nullptr;
		auto* location = player ? player->GetCurrentLocation() : nullptr;
		if (!location)
			location = cell ? cell->GetLocation() : nullptr;

		return {
			.location = location,
			.cell = cell,
		};
	}

	bool LocationHasAnyKeyword(const RE::BGSLocation* a_location, std::initializer_list<std::string_view> a_keywords)
	{
		for (auto* current = a_location; current; current = current->parentLoc) {
			for (auto keyword : a_keywords) {
				if (current->HasKeywordString(keyword))
					return true;
			}
		}

		return false;
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdaptiveBrightness::ProfileSettings,
	brightness,
	advanced,
	directionalLightMult,
	pointLightMult,
	ambientMult,
	emitColorMult,
	glowmapMult,
	effectLightingMult,
	skyGammaOffset,
	fogGammaOffset,
	fogAlphaGammaOffset,
	waterGammaOffset,
	vlGammaOffset)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdaptiveBrightness::LocationOverride,
	key,
	name,
	type,
	cocCode,
	profile)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdaptiveBrightness::Settings,
	enabled,
	dayStartHour,
	nightStartHour,
	transitionHours,
	profiles,
	locationOverrides)

void AdaptiveBrightness::DrawSettings()
{
	ImGui::Checkbox("Enable Adaptive Brightness", &settings.enabled);

	if (settings.enabled) {
		const auto contextLabel = GetContextLabel();
		ImGui::TextWrapped("%s", contextLabel.c_str());
	}

	ImGui::BeginDisabled(!settings.enabled);

	ImGui::SeparatorText("Exterior Time");
	ImGui::SliderFloat("Day Blend Start", &settings.dayStartHour, 0.0f, 24.0f, "%.1f h");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Game hour when exterior brightness starts blending from night to day.");
	}

	ImGui::SliderFloat("Night Blend Start", &settings.nightStartHour, 0.0f, 24.0f, "%.1f h");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Game hour when exterior brightness starts blending from day to night.");
	}

	ImGui::SliderFloat("Blend Duration", &settings.transitionHours, 0.0f, 4.0f, "%.1f h");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Hours needed to fully reach the target exterior profile.");
	}

	settings.dayStartHour = WrapHour(settings.dayStartHour);
	settings.nightStartHour = WrapHour(settings.nightStartHour);
	settings.transitionHours = std::clamp(SafeFinite(settings.transitionHours, 1.0f), 0.0f, 4.0f);

	ImGui::SeparatorText("Profiles");
	if (ImGui::BeginTabBar("##AdaptiveBrightnessProfiles", ImGuiTabBarFlags_None)) {
		for (auto profile : kProfileOrder) {
			if (ImGui::BeginTabItem(GetProfileName(profile))) {
				DrawProfile(profile);
				ImGui::EndTabItem();
			}
		}

		ImGui::EndTabBar();
	}

	DrawLocationOverrides();

	ImGui::EndDisabled();
}

void AdaptiveBrightness::LoadSettings(json& o_json)
{
	settings = o_json;
	NormalizeLocationOverrides();
	MarkLocationOverrideLookupDirty();
}

void AdaptiveBrightness::SaveSettings(json& o_json)
{
	NormalizeLocationOverrides();
	o_json = settings;
}

void AdaptiveBrightness::RestoreDefaultSettings()
{
	settings = {};
	selectedLocationOverrideKey.clear();
	MarkLocationOverrideLookupDirty();
}

const char* AdaptiveBrightness::GetProfileName(Profile a_profile)
{
	return kProfileNames[ProfileIndex(a_profile)];
}

void AdaptiveBrightness::DrawProfile(Profile a_profile)
{
	auto& profile = settings.profiles[ProfileIndex(a_profile)];

	ImGui::PushID(static_cast<int>(ProfileIndex(a_profile)));
	DrawProfileSettings(profile);
	ImGui::PopID();
}

void AdaptiveBrightness::DrawProfileSettings(ProfileSettings& a_profile)
{
	a_profile.brightness = ClampBrightness(a_profile.brightness);
	ImGui::SliderFloat("Brightness", &a_profile.brightness, kBrightnessMin, kBrightnessMax, "%.2f");
	a_profile.brightness = ClampBrightness(a_profile.brightness);

	ImGui::Checkbox("Advanced Controls", &a_profile.advanced);

	if (a_profile.advanced) {
		ImGui::SeparatorText("Light Balance");
		ImGui::SliderFloat("Directional Light", &a_profile.directionalLightMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Point Lights", &a_profile.pointLightMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Ambient", &a_profile.ambientMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Emissive", &a_profile.emitColorMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Glowmaps", &a_profile.glowmapMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Effects", &a_profile.effectLightingMult, 0.0f, 3.0f, "%.2f");

		a_profile.directionalLightMult = ClampMultiplier(a_profile.directionalLightMult);
		a_profile.pointLightMult = ClampMultiplier(a_profile.pointLightMult);
		a_profile.ambientMult = ClampMultiplier(a_profile.ambientMult);
		a_profile.emitColorMult = ClampMultiplier(a_profile.emitColorMult);
		a_profile.glowmapMult = ClampMultiplier(a_profile.glowmapMult);
		a_profile.effectLightingMult = ClampMultiplier(a_profile.effectLightingMult);

		ImGui::SeparatorText("Atmosphere Gamma Offsets");
		ImGui::SliderFloat("Sky", &a_profile.skyGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
		ImGui::SliderFloat("Fog", &a_profile.fogGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
		ImGui::SliderFloat("Fog Transparency", &a_profile.fogAlphaGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
		ImGui::SliderFloat("Water", &a_profile.waterGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
		ImGui::SliderFloat("Volumetric Lighting", &a_profile.vlGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");

		a_profile.skyGammaOffset = ClampGammaOffset(a_profile.skyGammaOffset);
		a_profile.fogGammaOffset = ClampGammaOffset(a_profile.fogGammaOffset);
		a_profile.fogAlphaGammaOffset = ClampGammaOffset(a_profile.fogAlphaGammaOffset);
		a_profile.waterGammaOffset = ClampGammaOffset(a_profile.waterGammaOffset);
		a_profile.vlGammaOffset = ClampGammaOffset(a_profile.vlGammaOffset);
	}
}

void AdaptiveBrightness::DrawLocationOverrides()
{
	ImGui::SeparatorText("Location Overrides");

	const auto target = GetCurrentLocationOverrideTarget();
	ImGui::BeginDisabled(!target.has_value());
	if (ImGui::Button("Save Current Location")) {
		SaveCurrentLocationOverride();
	}
	ImGui::EndDisabled();

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Create or select an override for the current BGSLocation, falling back to the current cell.");
	}

	ImGui::SameLine();
	ImGui::TextDisabled("(%zu saved)", settings.locationOverrides.size());

	if (target) {
		ImGui::TextWrapped("Current target: %s (%s, %s, COC %s)", target->name.c_str(), target->type.c_str(), target->key.c_str(), GetCocLabel(target->cocCode));
	} else {
		ImGui::TextDisabled("No current location or cell form is available.");
	}

	if (settings.locationOverrides.empty()) {
		ImGui::TextDisabled("No location overrides saved.");
		return;
	}

	enum LocationOverrideColumn : ImGuiID
	{
		ColLocation,
		ColType,
		ColKey,
		ColCoc,
		ColActions
	};

	std::vector<std::size_t> sortedIndices(settings.locationOverrides.size());
	std::iota(sortedIndices.begin(), sortedIndices.end(), 0);

	const auto compareOverrides = [&](std::size_t a_lhsIndex, std::size_t a_rhsIndex, const ImGuiTableColumnSortSpecs& a_spec) {
		const auto& lhs = settings.locationOverrides[a_lhsIndex];
		const auto& rhs = settings.locationOverrides[a_rhsIndex];

		int cmp = 0;
		switch (a_spec.ColumnUserID) {
		case ColLocation:
			cmp = CompareString(lhs.name, rhs.name);
			break;
		case ColType:
			cmp = CompareString(lhs.type, rhs.type);
			break;
		case ColKey:
			cmp = CompareString(lhs.key, rhs.key);
			break;
		case ColCoc:
			cmp = CompareString(lhs.cocCode, rhs.cocCode);
			break;
		default:
			cmp = CompareString(lhs.key, rhs.key);
			break;
		}

		if (cmp == 0)
			cmp = CompareString(lhs.key, rhs.key);

		const bool ascending = a_spec.SortDirection != ImGuiSortDirection_Descending;
		return ascending ? cmp < 0 : cmp > 0;
	};

	if (ImGui::BeginTable("##AdaptiveBrightnessLocationOverrides", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp)) {
		const float actionColumnWidth = std::max(
			64.0f * Util::GetUIScale(),
			ImGui::CalcTextSize("Delete").x + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f);

		ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 2.4f, ColLocation);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 64.0f * Util::GetUIScale(), ColType);
		ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 2.2f, ColKey);
		ImGui::TableSetupColumn("coc", ImGuiTableColumnFlags_WidthStretch, 1.2f, ColCoc);
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, actionColumnWidth, ColActions);
		ImGui::TableHeadersRow();

		if (auto* sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs && sortSpecs->SpecsCount > 0) {
			std::sort(sortedIndices.begin(), sortedIndices.end(), [&](std::size_t a_lhsIndex, std::size_t a_rhsIndex) {
				for (int i = 0; i < sortSpecs->SpecsCount; ++i) {
					const auto& spec = sortSpecs->Specs[i];
					if (compareOverrides(a_lhsIndex, a_rhsIndex, spec))
						return true;
					if (compareOverrides(a_rhsIndex, a_lhsIndex, spec))
						return false;
				}

				return a_lhsIndex < a_rhsIndex;
			});
			sortSpecs->SpecsDirty = false;
		}

		std::size_t deleteIndex = kInvalidLocationOverrideIndex;

		for (const auto overrideIndex : sortedIndices) {
			auto& locationOverride = settings.locationOverrides[overrideIndex];
			const bool selected = selectedLocationOverrideKey == locationOverride.key;
			const auto selectOverride = [&]() {
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					selectedLocationOverrideKey = locationOverride.key;
			};

			ImGui::PushID(static_cast<int>(overrideIndex));
			ImGui::TableNextRow();
			if (selected) {
				const auto rowColor = ImGui::GetColorU32(ImGuiCol_Header);
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowColor);
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowColor);
			}

			ImGui::TableSetColumnIndex(0);
			DrawTableWrappedText(locationOverride.name.c_str());
			selectOverride();

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(locationOverride.type.c_str());
			selectOverride();

			ImGui::TableSetColumnIndex(2);
			DrawTableWrappedText(locationOverride.key.c_str());
			selectOverride();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", locationOverride.key.c_str());
			}

			ImGui::TableSetColumnIndex(3);
			DrawTableWrappedText(GetCocLabel(locationOverride.cocCode));
			selectOverride();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				if (locationOverride.cocCode.empty()) {
					ImGui::Text("No cell EditorID was saved for this override.");
				} else {
					ImGui::Text("Console: coc %s", locationOverride.cocCode.c_str());
				}
			}

			ImGui::TableSetColumnIndex(4);
			ImGui::BeginDisabled(locationOverride.cocCode.empty());
			if (ImGui::SmallButton("Copy")) {
				const auto command = std::format("coc {}", locationOverride.cocCode);
				ImGui::SetClipboardText(command.c_str());
			}
			ImGui::EndDisabled();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Copy the coc command for this saved cell.");
			}
			if (ImGui::SmallButton("Edit")) {
				selectedLocationOverrideKey = locationOverride.key;
			}
			if (ImGui::SmallButton("Delete")) {
				deleteIndex = overrideIndex;
			}

			ImGui::PopID();
		}

		ImGui::EndTable();

		if (deleteIndex != kInvalidLocationOverrideIndex && deleteIndex < settings.locationOverrides.size()) {
			if (selectedLocationOverrideKey == settings.locationOverrides[deleteIndex].key)
				selectedLocationOverrideKey.clear();

			settings.locationOverrides.erase(settings.locationOverrides.begin() + static_cast<std::ptrdiff_t>(deleteIndex));
			MarkLocationOverrideLookupDirty();
		}
	}

	if (auto* selectedOverride = FindLocationOverride(selectedLocationOverrideKey)) {
		ImGui::SeparatorText("Edit Override");
		ImGui::TextWrapped("%s (%s, %s)", selectedOverride->name.c_str(), selectedOverride->type.c_str(), selectedOverride->key.c_str());
		ImGui::PushID(selectedOverride->key.c_str());
		DrawProfileSettings(selectedOverride->profile);
		ImGui::PopID();
	} else if (!selectedLocationOverrideKey.empty()) {
		selectedLocationOverrideKey.clear();
	}
}

bool AdaptiveBrightness::IsRuntimeEnabled() const
{
	if (!loaded || !settings.enabled)
		return false;

	auto state = globals::state;
	if (state && state->IsMainOrLoadingMenuOpen())
		return false;

	return true;
}

AdaptiveBrightness::Profile AdaptiveBrightness::GetInteriorProfile() const
{
	const auto forms = GetCurrentLocationForms();
	const auto* location = forms.location;

	if (LocationHasAnyKeyword(location, { "LocTypeDungeon", "LocTypeMine" }))
		return Profile::Dungeon;

	if (LocationHasAnyKeyword(location, { "LocTypeDwelling" }))
		return Profile::Dwelling;

	return Profile::Interior;
}

AdaptiveBrightness::Profile AdaptiveBrightness::GetCurrentProfileForUI() const
{
	const auto location = LocationContext::Get();
	if (location.inInterior)
		return GetInteriorProfile();

	return GetExteriorNightFactor() >= 0.5f ? Profile::ExteriorNight : Profile::ExteriorDay;
}

const AdaptiveBrightness::LocationOverride* AdaptiveBrightness::GetActiveLocationOverride() const
{
	const auto overrideIndex = ResolveLocationOverrideIndex();
	if (overrideIndex == kInvalidLocationOverrideIndex || overrideIndex >= settings.locationOverrides.size())
		return nullptr;

	return &settings.locationOverrides[overrideIndex];
}

std::optional<AdaptiveBrightness::LocationOverrideTarget> AdaptiveBrightness::GetCurrentLocationOverrideTarget() const
{
	const auto forms = GetCurrentLocationForms();
	const auto currentProfile = GetCurrentProfileForUI();
	const auto cocCode = GetCellCocCode(forms.cell);

	const auto makeTarget = [&](const RE::TESForm* a_form, std::string_view a_type) -> std::optional<LocationOverrideTarget> {
		if (!a_form)
			return std::nullopt;

		auto key = Util::GetFormFileKey(a_form);
		if (!IsValidFormKey(key))
			return std::nullopt;

		return LocationOverrideTarget{
			.key = std::move(key),
			.name = GetFormDisplayName(a_form),
			.type = std::string(a_type),
			.cocCode = cocCode,
			.defaultProfile = currentProfile,
		};
	};

	if (auto target = makeTarget(forms.location, kOverrideTypeLocation))
		return target;

	return makeTarget(forms.cell, kOverrideTypeCell);
}

void AdaptiveBrightness::SaveCurrentLocationOverride()
{
	const auto target = GetCurrentLocationOverrideTarget();
	if (!target)
		return;

	if (auto* existingOverride = FindLocationOverride(target->key)) {
		existingOverride->name = target->name;
		existingOverride->type = target->type;
		existingOverride->cocCode = target->cocCode;
		selectedLocationOverrideKey = existingOverride->key;
		return;
	}

	LocationOverride locationOverride;
	locationOverride.key = target->key;
	locationOverride.name = target->name;
	locationOverride.type = target->type;
	locationOverride.cocCode = target->cocCode;
	if (const auto* activeOverride = GetActiveLocationOverride()) {
		locationOverride.profile = activeOverride->profile;
	} else {
		locationOverride.profile = settings.profiles[ProfileIndex(target->defaultProfile)];
	}

	selectedLocationOverrideKey = locationOverride.key;
	settings.locationOverrides.push_back(std::move(locationOverride));
	MarkLocationOverrideLookupDirty();
}

AdaptiveBrightness::LocationOverride* AdaptiveBrightness::FindLocationOverride(const std::string& a_key)
{
	const auto overrideIndex = FindLocationOverrideIndexByKey(a_key);
	if (overrideIndex == kInvalidLocationOverrideIndex)
		return nullptr;

	return &settings.locationOverrides[overrideIndex];
}

const AdaptiveBrightness::LocationOverride* AdaptiveBrightness::FindLocationOverride(const std::string& a_key) const
{
	const auto overrideIndex = FindLocationOverrideIndexByKey(a_key);
	if (overrideIndex == kInvalidLocationOverrideIndex)
		return nullptr;

	return &settings.locationOverrides[overrideIndex];
}

std::size_t AdaptiveBrightness::FindLocationOverrideIndexByKey(const std::string& a_key) const
{
	if (!IsValidFormKey(a_key))
		return kInvalidLocationOverrideIndex;

	EnsureLocationOverrideLookup();

	const auto it = locationOverrideLookup.find(a_key);
	if (it == locationOverrideLookup.end() || it->second >= settings.locationOverrides.size())
		return kInvalidLocationOverrideIndex;

	return it->second;
}

std::size_t AdaptiveBrightness::FindLocationOverrideIndexByForm(const RE::TESForm* a_form) const
{
	if (!a_form)
		return kInvalidLocationOverrideIndex;

	const auto key = Util::GetFormFileKey(a_form);
	return FindLocationOverrideIndexByKey(key);
}

std::size_t AdaptiveBrightness::ResolveLocationOverrideIndex() const
{
	EnsureLocationOverrideLookup();

	if (settings.locationOverrides.empty())
		return kInvalidLocationOverrideIndex;

	const auto forms = GetCurrentLocationForms();
	const uint32_t locationFormID = forms.location ? forms.location->GetFormID() : 0;
	const uint32_t cellFormID = forms.cell ? forms.cell->GetFormID() : 0;

	if (locationOverrideCache.valid &&
	    locationOverrideCache.lookupVersion == locationOverrideLookupVersion &&
	    locationOverrideCache.locationFormID == locationFormID &&
	    locationOverrideCache.cellFormID == cellFormID) {
		return locationOverrideCache.overrideIndex;
	}

	auto resolvedIndex = kInvalidLocationOverrideIndex;

	for (auto* current = forms.location; current; current = current->parentLoc) {
		resolvedIndex = FindLocationOverrideIndexByForm(current);
		if (resolvedIndex != kInvalidLocationOverrideIndex)
			break;
	}

	if (resolvedIndex == kInvalidLocationOverrideIndex)
		resolvedIndex = FindLocationOverrideIndexByForm(forms.cell);

	locationOverrideCache = {
		.locationFormID = locationFormID,
		.cellFormID = cellFormID,
		.lookupVersion = locationOverrideLookupVersion,
		.overrideIndex = resolvedIndex,
		.valid = true,
	};

	return resolvedIndex;
}

void AdaptiveBrightness::NormalizeLocationOverrides()
{
	bool changedLookup = false;

	for (auto it = settings.locationOverrides.begin(); it != settings.locationOverrides.end();) {
		if (!IsValidFormKey(it->key)) {
			it = settings.locationOverrides.erase(it);
			changedLookup = true;
			continue;
		}

		if (it->name.empty())
			it->name = it->key;

		if (it->type != kOverrideTypeCell)
			it->type = kOverrideTypeLocation;

		it->profile.brightness = ClampBrightness(it->profile.brightness);
		++it;
	}

	if (settings.locationOverrides.size() > 1) {
		std::unordered_set<std::string> seenKeys;
		std::vector<LocationOverride> dedupedOverrides;
		dedupedOverrides.reserve(settings.locationOverrides.size());

		for (auto it = settings.locationOverrides.rbegin(); it != settings.locationOverrides.rend(); ++it) {
			if (seenKeys.insert(it->key).second)
				dedupedOverrides.push_back(*it);
			else
				changedLookup = true;
		}

		std::reverse(dedupedOverrides.begin(), dedupedOverrides.end());
		if (dedupedOverrides.size() != settings.locationOverrides.size())
			settings.locationOverrides = std::move(dedupedOverrides);
	}

	if (changedLookup)
		MarkLocationOverrideLookupDirty();
}

void AdaptiveBrightness::MarkLocationOverrideLookupDirty()
{
	locationOverrideLookupDirty = true;
	locationOverrideCache = {};
}

void AdaptiveBrightness::EnsureLocationOverrideLookup() const
{
	if (!locationOverrideLookupDirty)
		return;

	locationOverrideLookup.clear();
	for (std::size_t i = 0; i < settings.locationOverrides.size(); ++i) {
		const auto& locationOverride = settings.locationOverrides[i];
		if (IsValidFormKey(locationOverride.key))
			locationOverrideLookup[locationOverride.key] = i;
	}

	locationOverrideCache = {};
	locationOverrideLookupDirty = false;
	++locationOverrideLookupVersion;
}

float AdaptiveBrightness::GetExteriorNightFactor() const
{
	const auto* sky = globals::game::sky;
	const float hour = sky ? WrapHour(sky->currentGameHour) : 12.0f;
	const float dayStart = WrapHour(settings.dayStartHour);
	const float nightStart = WrapHour(settings.nightStartHour);
	float dayLength = HoursSince(dayStart, nightStart);

	if (dayLength < 0.25f || dayLength > 23.75f)
		dayLength = 12.0f;

	const float hoursIntoDay = HoursSince(dayStart, hour);
	float transition = std::clamp(SafeFinite(settings.transitionHours, 1.0f), 0.0f, 4.0f);
	transition = std::min(transition, dayLength * 0.5f);

	if (transition <= 0.0f)
		return hoursIntoDay < dayLength ? 0.0f : 1.0f;

	float dayFactor = 0.0f;
	if (hoursIntoDay < dayLength) {
		dayFactor = 1.0f;

		if (hoursIntoDay < transition) {
			dayFactor = SmoothStep(0.0f, transition, hoursIntoDay);
		} else if (hoursIntoDay > dayLength - transition) {
			dayFactor = 1.0f - SmoothStep(dayLength - transition, dayLength, hoursIntoDay);
		}
	}

	return 1.0f - std::clamp(dayFactor, 0.0f, 1.0f);
}

LinearLighting::Settings AdaptiveBrightness::GetNeutralLinearLightingSettings() const
{
	auto neutral = LinearLighting::Settings{};

	neutral.lightGamma = 1.0f;
	neutral.colorGamma = 1.0f;
	neutral.emitColorGamma = 1.0f;
	neutral.glowmapGamma = 1.0f;
	neutral.ambientGamma = 1.0f;
	neutral.fogGamma = 1.0f;
	neutral.fogAlphaGamma = 1.0f;
	neutral.effectGamma = 1.0f;
	neutral.effectAlphaGamma = 1.0f;
	neutral.skyGamma = 1.0f;
	neutral.waterGamma = 1.0f;
	neutral.vlGamma = 1.0f;
	neutral.glowmapMult = 1.0f;
	neutral.effectLightingMult = 1.0f;

	return neutral;
}

LinearLighting::Settings AdaptiveBrightness::ApplyProfile(const LinearLighting::Settings& a_base, const ProfileSettings& a_profile) const
{
	auto out = a_base;
	const float brightness = ClampBrightness(a_profile.brightness);
	const float brightnessDelta = brightness - 1.0f;
	const float masterGammaOffset = std::clamp((1.0f - brightness) * 0.35f, -0.35f, 0.35f);

	const auto masterScale = [&](float a_weight) {
		return std::max(0.0f, 1.0f + brightnessDelta * a_weight);
	};
	const auto advancedMult = [&](float a_multiplier) {
		return a_profile.advanced ? ClampMultiplier(a_multiplier) : 1.0f;
	};
	const auto advancedOffset = [&](float a_offset) {
		return a_profile.advanced ? ClampGammaOffset(a_offset) : 0.0f;
	};

	out.directionalLightMult = ClampMultiplier(out.directionalLightMult * masterScale(0.70f) * advancedMult(a_profile.directionalLightMult));
	out.pointLightMult = ClampMultiplier(out.pointLightMult * masterScale(0.75f) * advancedMult(a_profile.pointLightMult));
	out.ambientMult = ClampMultiplier(out.ambientMult * masterScale(0.95f) * advancedMult(a_profile.ambientMult));
	out.emitColorMult = ClampMultiplier(out.emitColorMult * masterScale(0.35f) * advancedMult(a_profile.emitColorMult));
	out.glowmapMult = ClampMultiplier(out.glowmapMult * masterScale(0.35f) * advancedMult(a_profile.glowmapMult));
	out.effectLightingMult = ClampMultiplier(out.effectLightingMult * masterScale(0.55f) * advancedMult(a_profile.effectLightingMult));

	out.skyGamma = ClampGamma(out.skyGamma + masterGammaOffset * 0.90f + advancedOffset(a_profile.skyGammaOffset));
	out.fogGamma = ClampGamma(out.fogGamma + masterGammaOffset * 0.75f + advancedOffset(a_profile.fogGammaOffset));
	out.fogAlphaGamma = ClampGamma(out.fogAlphaGamma + masterGammaOffset * 0.50f + advancedOffset(a_profile.fogAlphaGammaOffset));
	out.waterGamma = ClampGamma(out.waterGamma + masterGammaOffset * 0.75f + advancedOffset(a_profile.waterGammaOffset));
	out.vlGamma = ClampGamma(out.vlGamma + masterGammaOffset * 0.85f + advancedOffset(a_profile.vlGammaOffset));

	return out;
}

LinearLighting::Settings AdaptiveBrightness::LerpSettings(const LinearLighting::Settings& a_a, const LinearLighting::Settings& a_b, float a_t) const
{
	auto out = a_a;
	const float t = std::clamp(SafeFinite(a_t, 0.0f), 0.0f, 1.0f);
	const auto lerp = [&](float a_start, float a_end) {
		return std::lerp(a_start, a_end, t);
	};

	out.lightGamma = lerp(a_a.lightGamma, a_b.lightGamma);
	out.colorGamma = lerp(a_a.colorGamma, a_b.colorGamma);
	out.emitColorGamma = lerp(a_a.emitColorGamma, a_b.emitColorGamma);
	out.glowmapGamma = lerp(a_a.glowmapGamma, a_b.glowmapGamma);
	out.ambientGamma = lerp(a_a.ambientGamma, a_b.ambientGamma);
	out.fogGamma = lerp(a_a.fogGamma, a_b.fogGamma);
	out.fogAlphaGamma = lerp(a_a.fogAlphaGamma, a_b.fogAlphaGamma);
	out.effectGamma = lerp(a_a.effectGamma, a_b.effectGamma);
	out.effectAlphaGamma = lerp(a_a.effectAlphaGamma, a_b.effectAlphaGamma);
	out.skyGamma = lerp(a_a.skyGamma, a_b.skyGamma);
	out.waterGamma = lerp(a_a.waterGamma, a_b.waterGamma);
	out.vlGamma = lerp(a_a.vlGamma, a_b.vlGamma);
	out.vanillaDiffuseColorMult = lerp(a_a.vanillaDiffuseColorMult, a_b.vanillaDiffuseColorMult);
	out.directionalLightMult = lerp(a_a.directionalLightMult, a_b.directionalLightMult);
	out.pointLightMult = lerp(a_a.pointLightMult, a_b.pointLightMult);
	out.ambientMult = lerp(a_a.ambientMult, a_b.ambientMult);
	out.emitColorMult = lerp(a_a.emitColorMult, a_b.emitColorMult);
	out.glowmapMult = lerp(a_a.glowmapMult, a_b.glowmapMult);
	out.effectLightingMult = lerp(a_a.effectLightingMult, a_b.effectLightingMult);
	out.membraneEffectMult = lerp(a_a.membraneEffectMult, a_b.membraneEffectMult);
	out.bloodEffectMult = lerp(a_a.bloodEffectMult, a_b.bloodEffectMult);
	out.projectedEffectMult = lerp(a_a.projectedEffectMult, a_b.projectedEffectMult);
	out.deferredEffectMult = lerp(a_a.deferredEffectMult, a_b.deferredEffectMult);
	out.otherEffectMult = lerp(a_a.otherEffectMult, a_b.otherEffectMult);

	return out;
}

LinearLighting::Settings AdaptiveBrightness::GetEffectiveLinearLightingSettings(const LinearLighting::Settings& a_linearLightingSettings, bool a_linearLightingEnabled) const
{
	auto baseSettings = a_linearLightingEnabled ? a_linearLightingSettings : GetNeutralLinearLightingSettings();

	if (!IsRuntimeEnabled())
		return baseSettings;

	if (const auto* locationOverride = GetActiveLocationOverride())
		return ApplyProfile(baseSettings, locationOverride->profile);

	const auto location = LocationContext::Get();

	if (location.inInterior) {
		const auto profile = GetInteriorProfile();
		return ApplyProfile(baseSettings, settings.profiles[ProfileIndex(profile)]);
	}

	const auto daySettings = ApplyProfile(baseSettings, settings.profiles[ProfileIndex(Profile::ExteriorDay)]);
	const auto nightSettings = ApplyProfile(baseSettings, settings.profiles[ProfileIndex(Profile::ExteriorNight)]);
	return LerpSettings(daySettings, nightSettings, GetExteriorNightFactor());
}

std::string AdaptiveBrightness::GetContextLabel() const
{
	if (!settings.enabled)
		return "Adaptive Brightness is disabled.";

	if (!IsRuntimeEnabled())
		return "Adaptive Brightness is inactive in the current menu or while the feature is unloaded.";

	if (const auto* locationOverride = GetActiveLocationOverride()) {
		return std::format("Current override: {} ({})", locationOverride->name, locationOverride->type);
	}

	const auto location = LocationContext::Get();
	if (location.inInterior) {
		return std::format("Current profile: {}", GetProfileName(GetCurrentProfileForUI()));
	}

	const float nightFactor = GetExteriorNightFactor();
	const auto dominantProfile = GetCurrentProfileForUI();
	return std::format("Current profile: {} ({:.0f}% night blend)", GetProfileName(dominantProfile), nightFactor * 100.0f);
}
