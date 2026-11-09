#include "SnowCover.h"

#include "Utils/UI.h"
#include "Wetterness/SnowMelt.h"

void SnowCover::DrawSettings()
{
	auto enabledGuard = Util::DisableGuard(!settings.Enable);
	DrawPerformanceSettings(true);
	Util::Widgets::CheckboxFlags("Snow on movable objects", &settings.AffectHavok, 1u);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("On: loose objects can collect snow. Off: they keep their original appearance. Snow may slide over objects as they tumble; characters are unaffected.");
	Util::Widgets::CheckboxFlags("Tint harvestable plants", &settings.AffectFloraTint, 1u);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("On: harvestable plants change color with the season. Off: their original colors remain. Snow coverage is controlled separately.");
	ImGui::SliderFloat("Snow altitude offset", &settings.SnowHeightOffset, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
	Util::AddTooltip("Higher values keep snow on higher ground; lower values spread it farther downhill. Adjusts the seasonal snow line.");
	ImGui::SliderFloat("Rooftop snow", &settings.RooftopSnow, 0.0f, DEFAULT_SUMMER_HEIGHT_OFFSET, "%.0f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Adds more snow to exposed rooftops. Higher values keep roofs snowy in milder conditions; 0 removes this extra coverage.");
	{
		Util::DisableGuard disable(!settings.EnableRooftops || settings.RooftopSnow <= 0.0f);
		ImGui::SliderFloat("Rooftop height", &settings.RooftopHeight, 32.0f, 1024.0f, "%.0f");
		Util::AddTooltip("Height above a building's base where full rooftop snow begins. Lower values allow extra snow farther down the building.");
		ImGui::SliderFloat("Rooftop sky exposure", &settings.RooftopExposure, 0.0f, 0.95f);
		Util::AddTooltip("How open to the sky a roof must be to receive extra snow. Higher values restrict it to more exposed roofs.");
	}
	ImGui::Checkbox("Survival Mode cold", &survivalEnabled);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("On: seasonal snow can make the area freezing in Survival Mode. Off: the game's usual cold rules apply. Needs a supported snow map for this region.");

	if (fireMeltSettings.Enabled) {
		ImGui::SliderFloat("Fire melt radius scale", &fireMeltSettings.RadiusScale, 1.0f, 16.0f, "%.1fx");
		Util::AddTooltip("How far melting spreads around each fire. Higher values clear a wider patch.");
		ImGui::SliderFloat("Fire melt strength", &fireMeltSettings.Strength, 0.0f, 1.0f);
		Util::AddTooltip("How much snow fires remove. 1 fully clears the hottest area; 0 leaves the snow unchanged.");
	}
	ImGui::Separator();
	ImGui::TextUnformatted(lastWorldspace.c_str());
	ImGui::TextWrapped("%s", status.c_str());
	if (ImGui::Button("Reload config")) {
		reloadRequested.store(true);
	}
	Util::AddTooltip("Reloads the current region's saved snow settings and discards its unsaved edits.");
	ImGui::SameLine();
	if (ImGui::Button("Save region settings"))
		SaveConfig();
	Util::AddTooltip("Saves the current region's snow settings and applies any edited texture paths.");
	ImGui::SameLine();
	if (ImGui::Button("Region defaults")) {
		WetternessSnowMelt::Reset();
		const auto enabled = wsettings.EnableSnowCover;
		ResetWorldConfig();
		wsettings.EnableSnowCover = enabled;
		refreshTextures.store(true);
		frame = {};
	}
	Util::AddTooltip("Restores default snow settings for this region, while keeping its snow switch as it is. Save to retain the changes.");
	if (ImGui::TreeNodeEx("Region settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		DrawWorldSettings();
		ImGui::TreePop();
	}
	ValidateSettings();
}

void SnowCover::DrawWorldSettings()
{
	if (Util::Widgets::CheckboxFlags("Snow in this region", &wsettings.EnableSnowCover, 1u) && wsettings.EnableSnowCover)
		refreshTextures.store(true);
	Util::AddTooltip("On: allows seasonal snow in the current region. Off: leaves this region unchanged while other regions keep their own settings.");
	Util::Widgets::CheckboxFlags("Seasonal grass tint", &wsettings.AffectGrassTint, 1u);
	Util::AddTooltip("On: grass changes color with the season. Off: it keeps its original color. Requires Seasonal foliage tint; snow coverage is separate.");
	Util::Widgets::CheckboxFlags("Seasonal tree tint", &wsettings.AffectTreeTint, 1u);
	Util::AddTooltip("On: tree foliage changes color with the season. Off: it keeps its original color. Requires Seasonal foliage tint; snow coverage is separate.");
	ImGui::DragFloat("Foliage color altitude offset", &wsettings.FoliageHeightOffset, 10.0f, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
	Util::AddTooltip("Moves seasonal color changes uphill or downhill. Lower values change plants farther downhill; higher values keep the change nearer snowy heights.");
	ImGui::DragFloat("Snow transition width", &wsettings.BlendSmoothness, 10.0f, 1.0f, 10000.0f);
	Util::AddTooltip("How gradually bare ground becomes snowy across different elevations. Higher values give a wider, softer transition.");
	if (ImGui::TreeNodeEx("Weather and seasons")) {
		int summer = static_cast<int>(MaxSummerMonth);
		int winter = static_cast<int>(MaxWinterMonth);
		if (ImGui::SliderInt("Peak summer month (0-11)", &summer, 0, 11))
			MaxSummerMonth = static_cast<uint>(summer);
		Util::AddTooltip("Month used for peak summer conditions. 0 is Morning Star; 6 is Sun's Height.");
		if (ImGui::SliderInt("Peak winter month (0-11)", &winter, 0, 11))
			MaxWinterMonth = static_cast<uint>(winter);
		Util::AddTooltip("Month used for peak winter conditions. 0 is Morning Star; 11 is Evening Star.");
		ImGui::DragFloat("Summer snow altitude", &SummerHeightOffset, 10.0f, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
		Util::AddTooltip("Snow line at peak summer. Higher values leave snow only on higher ground; lower values spread it downhill.");
		ImGui::DragFloat("Winter snow altitude", &WinterHeightOffset, 10.0f, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
		Util::AddTooltip("Snow line at peak winter. Lower values bring snow farther downhill; higher values keep lower ground clearer.");
		ImGui::SliderFloat("Snow accumulation speed", &snowingSpeed, 0.01f, 10.0f);
		Util::AddTooltip("How quickly snowfall builds visible cover as game time passes. Higher values cover the ground sooner.");
		ImGui::SliderFloat("Snow melting speed", &meltingSpeed, 0.01f, 10.0f);
		Util::AddTooltip("How quickly weather-built snow clears after snowfall ends. Higher values clear it sooner; rain speeds melting up.");
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Distance falloff")) {
		ImGui::DragFloat("Weather snow fade start", &wsettings.WeatherFadeStart, 100.0f, 0.0f, 200000.0f);
		Util::AddTooltip("Distance where snow added by current weather starts fading. Increase it to keep recent snowfall visible farther away.");
		ImGui::DragFloat("Weather snow fade end", &wsettings.WeatherFadeEnd, 100.0f, 1.0f, 300000.0f);
		Util::AddTooltip("Distance where weather-added snow finishes fading. Seasonal mountain snow is controlled separately.");
		ImGui::DragFloat("Object snow fade start", &wsettings.ObjectFadeStart, 10.0f, 0.0f, 40000.0f);
		Util::AddTooltip("Distance where snow on objects starts fading. Lower values reduce distant coverage sooner.");
		ImGui::DragFloat("Object snow fade end", &wsettings.ObjectFadeEnd, 10.0f, 1.0f, 40001.0f);
		Util::AddTooltip("Distance where object snow reaches the selected fade amount. Increase it to keep full coverage farther away.");
		ImGui::SliderFloat("Object snow fade amount", &wsettings.ObjectFadeAmount, 0.0f, 1.0f);
		Util::AddTooltip("How much distant object snow disappears. 0 keeps all coverage; 1 removes it fully beyond the fade distance.");
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Snow altitude map")) {
		ImGui::InputText("Map texture", mapTextureInput, sizeof(mapTextureInput));
		Util::AddTooltip("Snow map supplied for this region. Change it only when configuring a custom map; save region settings to apply it.");
		const bool firstChanged = ImGui::InputFloat2("Map first corner", &mapMin.x);
		Util::AddTooltip("World coordinates of one corner covered by the snow map. Use the coordinates supplied with that map.");
		const bool oppositeChanged = ImGui::InputFloat2("Map opposite corner", &mapMax.x);
		Util::AddTooltip("World coordinates of the opposite corner covered by the snow map. Wrong coordinates place snow in the wrong areas.");
		if (firstChanged || oppositeChanged)
			UpdateMapTransform();
		ImGui::DragFloat("Map altitude scale", &wsettings.mapZscale, 100.0f, 0.1f, 1000000.0f);
		Util::AddTooltip("Strength of the snow map's height differences. Higher values make snowy and snow-free areas differ more strongly.");
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Snow material")) {
		DrawMaterialSettings();
		ImGui::TreePop();
	}
}

void SnowCover::DrawMaterialSettings()
{
	if (ImGui::Checkbox("Use landscape snow color", &useLandscapeSnowColor))
		refreshTextures.store(true);
	Util::AddTooltip("Follows the installed main and alternate snow texture sets, including custom texture paths, seasonal swaps and PBR snow. Uses the standard landscape snow textures when those records are unavailable. Keeps the configured snow detail, shine and tints. Missing or unsupported textures keep the configured color. Save region settings to retain this option.");
	ImGui::TextWrapped("%s", "Choose texture files supplied by your snow mod. Enter their paths below Data without the .dds ending, then save the region settings to apply them.");
	ImGui::InputText("Main texture", mainTextureInput, sizeof(mainTextureInput));
	Util::AddTooltip("Main snow surface supplied by your texture mod. Landscape snow color overrides its color when available, while keeping its normal and material maps. Save region settings after changing the path.");
	ImGui::InputText("Alternative texture", alternateTextureInput, sizeof(alternateTextureInput));
	Util::AddTooltip("Second snow or ice surface used on different slopes. Landscape snow color overrides its color when available, while keeping its normal and material maps. Save region settings after changing the path.");
	ImGui::ColorEdit4("Main tint", &wsettings.MainTint.x);
	Util::AddTooltip("Color of the main snow surface. Lower opacity reveals more of the original color and reduces seasonal snow on foliage.");
	ImGui::ColorEdit4("Alternative tint", &wsettings.AltTint.x);
	Util::AddTooltip("Color of the second snow surface. Lower opacity lets more of the original surface color show through.");
	ImGui::SliderFloat("Main shine", &wsettings.MainSpec, 0.0f, 1.0f);
	Util::AddTooltip("Brightness of reflections on the main snow surface. Higher values make it shinier.");
	ImGui::SliderFloat("Alternative shine", &wsettings.AltSpec, 0.0f, 1.0f);
	Util::AddTooltip("Brightness of reflections on the second snow surface. Higher values make it shinier.");
	ImGui::TextWrapped("%s", "For slope settings, 0 means a vertical wall and 1 means flat ground. Tint opacity controls surface color coverage; main tint opacity also affects foliage snow.");
	ImGui::SliderFloat("Minimum slope", &wsettings.MinAngle, 0.0f, 1.0f);
	Util::AddTooltip("How flat a surface must be before snow starts sticking. Higher values keep snow off steeper slopes.");
	ImGui::SliderFloat("Full coverage slope", &wsettings.MaxAngle, 0.0f, 1.0f);
	Util::AddTooltip("How flat a surface must be for full snow coverage. Higher values reserve full coverage for flatter ground.");
	ImGui::SliderFloat("Main material peak slope", &wsettings.PeakMainAngle, 0.0f, 1.0f);
	Util::AddTooltip("Slope where the main snow texture is preferred. Lower values favor steep surfaces; higher values favor flat ground.");
	ImGui::SliderFloat("Alternative material peak slope", &wsettings.PeakAltAngle, 0.0f, 1.0f);
	Util::AddTooltip("Slope where the second snow texture is preferred. Lower values favor steep surfaces; higher values favor flat ground.");
	ImGui::SliderFloat("Texture scale", &wsettings.UVScale, 0.01f, 10.0f);
	Util::AddTooltip("Size of repeating snow detail. Higher values repeat it more often, making the visible pattern smaller.");
	ImGui::SliderFloat("Sparkle smoothing", &wsettings.ScreenSpaceScale, 1.0f, 3.0f);
	Util::AddTooltip("Smooths very fine sparkle detail. Higher values blend small flashes together more strongly.");
	ImGui::SliderFloat("Sparkle density", &wsettings.LogMicrofacetDensity, 0.0f, 40.0f);
	Util::AddTooltip("How densely tiny reflections cover the snow. Higher values produce finer, denser sparkle; lower values leave more isolated flashes.");
	ImGui::SliderFloat("Sparkle softness", &wsettings.MicrofacetRoughness, 0.001f, 1.0f);
	Util::AddTooltip("How widely snow reflects sparkle light. Higher values soften and broaden the highlights.");
	ImGui::SliderFloat("Sparkle variation", &wsettings.DensityRandomization, 0.001f, 5.0f);
	Util::AddTooltip("How unevenly sparkles are distributed. Higher values give more variation between patches of snow.");
}

void SnowCover::DrawPerformanceSettings(bool advanced)
{
	Util::Widgets::CheckboxFlags("Detailed foliage snow", &settings.EnableExpensiveFoliage, 1u);
	Util::AddTooltip("On: snow follows leaf and grass edges more closely. Off: uses simpler coverage that costs less to draw.");
	Util::Widgets::CheckboxFlags("Snow glints", &settings.EnableGlints, 1u);
	Util::AddTooltip("On: compatible snow surfaces sparkle in the light. Off: keeps ordinary snow reflections without the extra sparkle work.");
	Util::Widgets::CheckboxFlags("Rooftop coverage", &settings.EnableRooftops, 1u);
	Util::AddTooltip("On: exposed roofs can collect extra snow. Off: roofs follow the same snow line as other surfaces.");
	Util::Widgets::CheckboxFlags("Seasonal foliage tint", &settings.EnableFoliageTint, 1u);
	Util::AddTooltip("On: grass and plants can change color with the season. Off: keeps their original colors. Snow coverage is unaffected.");
	ImGui::Checkbox("Melt snow near fires", &fireMeltSettings.Enabled);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("On: nearby fires clear patches of snow. Off: fires leave snow untouched. Applies to placed fires, not spells or projectiles.");
	if (advanced) {
		Util::Widgets::CheckboxFlags("Skip objects outside snowy areas", &settings.EnableBoundsCulling, 1u);
		Util::AddTooltip("On: skips snow processing for objects that cannot receive snow. Off: checks them anyway. Normally the picture should stay the same.");
		ImGui::TextWrapped("Detailed foliage, sparkles and fire melting can reduce performance. Enable the details you want.");
	}
}
