#include "AdaptiveBrightness.h"
#include "GrassPlacement.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include <imgui_stdlib.h>

void AdaptiveBrightness::DrawGrassPlacementSettings(GrassPlacement::Settings& placement)
{
	MenuUI::SectionHeading("Grass placement",
		"Lower floating leaves, twigs and grass with a negative offset in game units. "
		"Placement follows each grass group's source cell and stays independent of weather and time. "
		"Location layers follow the saved inheritance mode. Large offsets can bury grass or expose gaps.");
	if (!GrassPlacement::IsAvailable())
		MenuUI::AvailabilityNotice("Grass placement is unavailable. Check native hook support and the plugin log.");
	bool changed = Util::Widgets::SliderFloat("Grass Vertical Offset", &placement.verticalOffset,
		-GrassPlacement::kMaximumOffset, GrassPlacement::kMaximumOffset, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	placement.verticalOffset = GrassPlacement::Clamp(placement.verticalOffset);
	for (auto it = placement.modelOffsets.begin(); it != placement.modelOffsets.end();) {
		ImGui::PushID(it->first.c_str());
		const SKSE::stl::scope_exit id([] { ImGui::PopID(); });
		ImGui::TextUnformatted(it->first.c_str());
		changed |= Util::Widgets::SliderFloat("Mesh Offset", &it->second,
			-GrassPlacement::kMaximumOffset, GrassPlacement::kMaximumOffset, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::Button("Remove correction")) {
			it = placement.modelOffsets.erase(it);
			changed = true;
		} else
			++it;
	}
	static std::string path;
	ImGui::InputText("Mesh path", &path);
	Util::AddTooltip("Mesh corrections add to this layer's vertical offset. Use the full path below meshes.");
	const auto key = GrassPlacement::ModelKey(path);
	const auto disabled = Util::DisableGuard(key.empty() || placement.modelOffsets.size() >= GrassPlacement::kMaximumModels);
	if (ImGui::Button("Add mesh correction")) {
		changed |= placement.modelOffsets.try_emplace(key, 0.0f).second;
		path.clear();
	}
	if (changed && &placement == &settings.globalProfile.grassPlacement)
		NotifyGrassPlacementChanged();
}

#ifdef DEVBENCH_BRIDGE_ENABLED
json AdaptiveBrightness::GrassPlacementControls(const json& request)
{
	const auto key = request.value("locationKey", std::string{});
	auto* location = key.empty() ? nullptr : FindLocationOverride(key);
	if (!key.empty() && !location)
		return { { "error", "Unknown saved location key" } };
	auto& profile = location ? location->profile.grassPlacement : settings.globalProfile.grassPlacement;
	if (const auto patch = request.find("grassPlacement"); patch != request.end()) {
		std::string error;
		if (!GrassPlacement::Validate(*patch, error))
			return { { "error", error } };
		json merged = profile;
		for (const auto& [field, value] : patch->items()) merged[field] = value;
		profile = merged.get<GrassPlacement::Settings>();
		if (location && locationOverrideEditKey == key)
			ResetLocationOverrideEdit();
		NotifyGrassPlacementChanged();
	}
	return { { "available", GrassPlacement::IsAvailable() }, { "enabled", IsRuntimeEnabled() },
		{ "locationKey", key }, { "grassPlacement", profile }, { "persisted", false },
		{ "sourceScope", "grass_group_cell" }, { "maximumEffectiveOffset", GrassPlacement::kMaximumOffset } };
}

#endif
