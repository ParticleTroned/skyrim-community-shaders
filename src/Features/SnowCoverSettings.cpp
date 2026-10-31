#include "SnowCover.h"

#include "Utils/UI.h"

void SnowCover::DrawSettings()
{
	Util::Widgets::CheckboxFlags("Enable Snow Cover", &settings.Enable, 1u);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Enables seasonal snow, foliage tint, fire melting and Survival Mode cold. All other settings are ignored when disabled.");
	ImGui::Indent();
	auto enabledGuard = Util::DisableGuard(!settings.Enable);
	DrawPerformanceSettings(true);
	Util::Widgets::CheckboxFlags("Snow on movable objects", &settings.AffectHavok, 1u);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Includes movable clutter. World-space snow slides across tumbling objects. Actors are excluded.");
	Util::Widgets::CheckboxFlags("Tint harvestable plants", &settings.AffectFloraTint, 1u);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Applies seasonal foliage color to flowers and other harvestable plants. Snow still applies when this is off.");
	ImGui::SliderFloat("Snow altitude offset", &settings.SnowHeightOffset, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
	ImGui::SliderFloat("Rooftop snow", &settings.RooftopSnow, 0.0f, DEFAULT_SUMMER_HEIGHT_OFFSET, "%.0f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Lowers the snow line on exposed roofs of buildings. Set to zero to disable.");
	{
		Util::DisableGuard disable(!settings.EnableRooftops || settings.RooftopSnow <= 0.0f);
		ImGui::SliderFloat("Rooftop height", &settings.RooftopHeight, 32.0f, 1024.0f, "%.0f");
		ImGui::SliderFloat("Rooftop sky exposure", &settings.RooftopExposure, 0.0f, 0.95f);
	}
	ImGui::Checkbox("Survival Mode cold", &survivalEnabled);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Snow-covered ground counts as freezing in Survival Mode and Survival Mode Improved. Requires an R8/BC4 altitude map; unknown map regions retain the game's cold rules.");

	if (fireMeltSettings.Enabled) {
		ImGui::SliderFloat("Fire melt radius scale", &fireMeltSettings.RadiusScale, 1.0f, 16.0f, "%.1fx");
		ImGui::SliderFloat("Fire melt strength", &fireMeltSettings.Strength, 0.0f, 1.0f);
	}
	ImGui::Separator();
	ImGui::TextUnformatted(lastWorldspace.c_str());
	ImGui::TextWrapped("%s", status.c_str());
	if (ImGui::Button("Reload config")) {
		reloadRequested.store(true);
	}
	ImGui::SameLine();
	if (ImGui::Button("Save worldspace config"))
		SaveConfig();
	ImGui::SameLine();
	if (ImGui::Button("Worldspace defaults")) {
		const auto enabled = wsettings.EnableSnowCover;
		ResetWorldConfig();
		wsettings.EnableSnowCover = enabled;
		refreshTextures.store(true);
		frame = {};
	}
	if (ImGui::TreeNodeEx("Worldspace config", ImGuiTreeNodeFlags_DefaultOpen)) {
		DrawWorldSettings();
		ImGui::TreePop();
	}
	ImGui::Unindent();
	ValidateSettings();
}

void SnowCover::DrawWorldSettings()
{
	if (Util::Widgets::CheckboxFlags("Enable snow cover in this worldspace", &wsettings.EnableSnowCover, 1u) && wsettings.EnableSnowCover)
		refreshTextures.store(true);
	Util::Widgets::CheckboxFlags("Seasonal grass tint", &wsettings.AffectGrassTint, 1u);
	Util::Widgets::CheckboxFlags("Seasonal tree tint", &wsettings.AffectTreeTint, 1u);
	ImGui::DragFloat("Foliage color altitude offset", &wsettings.FoliageHeightOffset, 10.0f, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
	ImGui::DragFloat("Snow transition width", &wsettings.BlendSmoothness, 10.0f, 1.0f, 10000.0f);
	if (ImGui::TreeNodeEx("Weather and seasons")) {
		int summer = static_cast<int>(MaxSummerMonth);
		int winter = static_cast<int>(MaxWinterMonth);
		if (ImGui::SliderInt("Peak summer month (0-11)", &summer, 0, 11))
			MaxSummerMonth = static_cast<uint>(summer);
		if (ImGui::SliderInt("Peak winter month (0-11)", &winter, 0, 11))
			MaxWinterMonth = static_cast<uint>(winter);
		ImGui::DragFloat("Summer snow altitude", &SummerHeightOffset, 10.0f, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
		ImGui::DragFloat("Winter snow altitude", &WinterHeightOffset, 10.0f, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
		ImGui::SliderFloat("Snow accumulation speed", &snowingSpeed, 0.01f, 10.0f);
		ImGui::SliderFloat("Snow melting speed", &meltingSpeed, 0.01f, 10.0f);
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Distance falloff")) {
		ImGui::DragFloat("Weather snow fade start", &wsettings.WeatherFadeStart, 100.0f, 0.0f, 200000.0f);
		ImGui::DragFloat("Weather snow fade end", &wsettings.WeatherFadeEnd, 100.0f, 1.0f, 300000.0f);
		ImGui::DragFloat("Object snow fade start", &wsettings.ObjectFadeStart, 10.0f, 0.0f, 40000.0f);
		ImGui::DragFloat("Object snow fade end", &wsettings.ObjectFadeEnd, 10.0f, 1.0f, 40001.0f);
		ImGui::SliderFloat("Object snow fade amount", &wsettings.ObjectFadeAmount, 0.0f, 1.0f);
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Snow altitude map")) {
		ImGui::InputText("Map texture", mapTextureInput, sizeof(mapTextureInput));
		const bool firstChanged = ImGui::InputFloat2("Map first corner", &mapMin.x);
		const bool oppositeChanged = ImGui::InputFloat2("Map opposite corner", &mapMax.x);
		if (firstChanged || oppositeChanged)
			UpdateMapTransform();
		ImGui::DragFloat("Map altitude scale", &wsettings.mapZscale, 100.0f, 0.1f, 1000000.0f);
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Snow material")) {
		DrawMaterialSettings();
		ImGui::TreePop();
	}
}

void SnowCover::DrawMaterialSettings()
{
	ImGui::TextWrapped("%s", "Texture paths are relative to Data, without .dds. Materials use diffuse, _n and _rmaos textures. Save to apply new paths.");
	ImGui::InputText("Main texture", mainTextureInput, sizeof(mainTextureInput));
	ImGui::InputText("Alternative texture", alternateTextureInput, sizeof(alternateTextureInput));
	ImGui::ColorEdit4("Main tint", &wsettings.MainTint.x);
	ImGui::ColorEdit4("Alternative tint", &wsettings.AltTint.x);
	ImGui::SliderFloat("Main specular", &wsettings.MainSpec, 0.0f, 1.0f);
	ImGui::SliderFloat("Alternative specular", &wsettings.AltSpec, 0.0f, 1.0f);
	ImGui::TextWrapped("%s", "Slope controls use the upward normal: 0 is a vertical surface, 1 is horizontal. Tint alpha controls color coverage.");
	ImGui::SliderFloat("Minimum slope", &wsettings.MinAngle, 0.0f, 1.0f);
	ImGui::SliderFloat("Full coverage slope", &wsettings.MaxAngle, 0.0f, 1.0f);
	ImGui::SliderFloat("Main material peak slope", &wsettings.PeakMainAngle, 0.0f, 1.0f);
	ImGui::SliderFloat("Alternative material peak slope", &wsettings.PeakAltAngle, 0.0f, 1.0f);
	ImGui::SliderFloat("Texture scale", &wsettings.UVScale, 0.01f, 10.0f);
	ImGui::SliderFloat("Glint screen-space scale", &wsettings.ScreenSpaceScale, 1.0f, 3.0f);
	ImGui::SliderFloat("Glint log microfacet density", &wsettings.LogMicrofacetDensity, 0.0f, 40.0f);
	ImGui::SliderFloat("Glint microfacet roughness", &wsettings.MicrofacetRoughness, 0.001f, 1.0f);
	ImGui::SliderFloat("Glint density variation", &wsettings.DensityRandomization, 0.001f, 5.0f);
}

void SnowCover::DrawPerformanceSettings(bool advanced)
{
	Util::Widgets::CheckboxFlags("Detailed foliage snow", &settings.EnableExpensiveFoliage, 1u);
	Util::Widgets::CheckboxFlags("Snow glints", &settings.EnableGlints, 1u);
	Util::Widgets::CheckboxFlags("Rooftop coverage", &settings.EnableRooftops, 1u);
	Util::Widgets::CheckboxFlags("Seasonal foliage tint", &settings.EnableFoliageTint, 1u);
	ImGui::Checkbox("Melt snow near fires", &fireMeltSettings.Enabled);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Clears snow around up to eight nearby placed fires. Spells, projectiles and impacts are excluded.");
	if (advanced) {
		Util::Widgets::CheckboxFlags("Skip snow-free object bounds", &settings.EnableBoundsCulling, 1u);
		ImGui::TextWrapped("Detailed foliage adds edge samples; glints add PBR shading; fire melting scans nearby placed fires. Bounds skipping preserves the overlay and can be disabled for comparison.");
	}
}
