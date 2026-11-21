#include "Menu/SettingsPage.h"
#include "SnowCover.h"

#include "Utils/UI.h"
#include "Wetterness/SnowMelt.h"

void SnowCover::DrawSettings()
{
	MenuUI::SettingsPage page("SnowCover", {
											   { "coverage", "Coverage", "Choose snow effects, rooftop coverage and melting around fires.", "Snow coverage and effects", true, true, "Choose coverage" },
											   { "seasons", "Seasons", "Set the seasonal snow line and how quickly weather builds or melts snow in this region.", "Seasonal snow and weather", true, true, "Tune the current region" },
											   { "region", "Region", "Adjust regional foliage, distance falloff and the snow altitude map.", "Regional coverage and distance" },
											   { "material", "Material", "Refine snow textures, colour, slopes and sparkle for this region.", "Snow surface appearance" },
										   });
	auto enabledGuard = Util::DisableGuard(!settings.Enable);
	if (page.Is("coverage"))
		DrawCoverageSettings();
	if (page.Is("seasons") || page.Is("region") || page.Is("material"))
		DrawWorldActions();
	if (page.Is("seasons"))
		DrawSeasonSettings();
	if (page.Is("region"))
		DrawWorldSettings();
	if (page.Is("material"))
		DrawMaterialSettings();
	ValidateSettings();
}

void SnowCover::DrawCoverageSettings()
{
	MenuUI::SectionHeading("Snow effects");
	DrawPerformanceSettings(true);
	MenuUI::SectionHeading("Snow coverage");
	Util::Widgets::CheckboxFlags("Snow on movable objects", &settings.AffectHavok, 1u);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("On: loose objects can collect snow. Off: they keep their original appearance. Snow may slide over objects as they tumble; characters are unaffected.");
	Util::Widgets::CheckboxFlags("Tint harvestable plants", &settings.AffectFloraTint, 1u);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("On: harvestable plants change color with the season. Off: their original colors remain. Snow coverage is controlled separately.");
	Util::Widgets::SliderFloat("Snow altitude offset", &settings.SnowHeightOffset, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
	Util::AddTooltip("Higher values keep snow on higher ground; lower values spread it farther downhill. Adjusts the seasonal snow line.");
	Util::Widgets::Checkbox("Survival Mode cold", &survivalEnabled);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("On: seasonal snow can make the area freezing in Survival Mode. Off: the game's usual cold rules apply. Needs a supported snow map for this region.");

	MenuUI::SectionHeading("Rooftops");
	Util::Widgets::SliderFloat("Rooftop snow", &settings.RooftopSnow, 0.0f, DEFAULT_SUMMER_HEIGHT_OFFSET, "%.0f");
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Adds more snow to exposed rooftops. Higher values keep roofs snowy in milder conditions; 0 removes this extra coverage.");
	{
		Util::DisableGuard disable(!settings.EnableRooftops || settings.RooftopSnow <= 0.0f);
		Util::Widgets::SliderFloat("Rooftop height", &settings.RooftopHeight, 32.0f, 1024.0f, "%.0f");
		Util::AddTooltip("Height above a building's base where full rooftop snow begins. Lower values allow extra snow farther down the building.");
		Util::Widgets::SliderFloat("Rooftop sky exposure", &settings.RooftopExposure, 0.0f, 0.95f);
		Util::AddTooltip("How open to the sky a roof must be to receive extra snow. Higher values restrict it to more exposed roofs.");
	}
	if (fireMeltSettings.Enabled) {
		MenuUI::SectionHeading("Fire melting");
		Util::Widgets::SliderFloat("Fire melt radius scale", &fireMeltSettings.RadiusScale, 1.0f, 16.0f, "%.1fx");
		Util::AddTooltip("How far melting spreads around each fire. Higher values clear a wider patch.");
		Util::Widgets::SliderFloat("Fire melt strength", &fireMeltSettings.Strength, 0.0f, 1.0f);
		Util::AddTooltip("How much snow fires remove. 1 fully clears the hottest area; 0 leaves the snow unchanged.");
	}
}

void SnowCover::DrawWorldActions()
{
	MenuUI::SectionHeading(lastWorldspace.empty() ? "Current region" : lastWorldspace.c_str(),
		"Season, region and material settings are saved separately for the current region. Use Save region settings to retain these changes.");
	const float actionWidth = ImGui::CalcTextSize("Save region settings").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFontSize() * 1.2f;
	MenuUI::DetailGrid actions("SnowCoverRegionActions", 3, actionWidth);
	actions.Next();
	if (ImGui::Button("Reload config", { -FLT_MIN, 0 })) {
		reloadRequested.store(true);
	}
	Util::AddTooltip("Reloads the current region's saved snow settings and discards its unsaved edits.");
	actions.Next();
	if (ImGui::Button("Save region settings", { -FLT_MIN, 0 }))
		SaveConfig();
	Util::AddTooltip("Saves the current region's snow settings and applies any edited texture paths.");
	actions.Next();
	if (ImGui::Button("Region defaults", { -FLT_MIN, 0 })) {
		WetternessSnowMelt::Reset();
		const auto enabled = wsettings.EnableSnowCover;
		ResetWorldConfig();
		wsettings.EnableSnowCover = enabled;
		refreshTextures.store(true);
		frame = {};
	}
	Util::AddTooltip("Restores default snow settings for this region, while keeping its snow switch as it is. Save to retain the changes.");
	if (!status.empty())
		MenuUI::PageNotice(status);
}

void SnowCover::DrawWorldSettings()
{
	MenuUI::SectionHeading("Regional coverage");
	if (Util::Widgets::CheckboxFlags("Snow in this region", &wsettings.EnableSnowCover, 1u) && wsettings.EnableSnowCover)
		refreshTextures.store(true);
	Util::AddTooltip("On: allows seasonal snow in the current region. Off: leaves this region unchanged while other regions keep their own settings.");
	Util::Widgets::CheckboxFlags("Seasonal grass tint", &wsettings.AffectGrassTint, 1u);
	Util::AddTooltip("On: grass changes color with the season. Off: it keeps its original color. Requires Seasonal foliage tint; snow coverage is separate.");
	Util::Widgets::CheckboxFlags("Seasonal tree tint", &wsettings.AffectTreeTint, 1u);
	Util::AddTooltip("On: tree foliage changes color with the season. Off: it keeps its original color. Requires Seasonal foliage tint; snow coverage is separate.");
	Util::Widgets::SliderFloat("Foliage tint altitude bias", &wsettings.FoliageHeightOffset, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET, "%.0f");
	Util::AddTooltip("Moves the autumn-colour transition relative to the seasonal snow line. Lower (more negative) values spread the tint farther downhill; higher values restrict it to higher ground. Zero starts the tint at the seasonal snow line. Requires Seasonal foliage tint and the relevant grass, tree or harvestable-plant tint switch. Changes foliage colour, not snow coverage.");
	Util::Widgets::SliderFloat("Snowline transition width", &wsettings.BlendSmoothness, 1.0f, 10000.0f, "%.0f units");
	Util::AddTooltip("Height range, in game units, over which seasonal snow builds from no cover to full cover. Lower values make a sharper snow boundary; higher values spread the transition over more elevation. This also changes the altitude range of seasonal foliage tint. It does not change how quickly snow accumulates or melts; use Seasons for those speeds.");
	MenuUI::SectionHeading("Distance falloff");
	Util::Widgets::SliderFloat("Weather snow fade start", &wsettings.WeatherFadeStart, 0.0f, 200000.0f);
	Util::AddTooltip("Distance where snow added by current weather starts fading. Increase it to keep recent snowfall visible farther away.");
	Util::Widgets::SliderFloat("Weather snow fade end", &wsettings.WeatherFadeEnd, 1.0f, 300000.0f);
	Util::AddTooltip("Distance where weather-added snow finishes fading. Seasonal mountain snow is controlled separately.");
	Util::Widgets::SliderFloat("Object snow fade start", &wsettings.ObjectFadeStart, 0.0f, 40000.0f);
	Util::AddTooltip("Distance where snow on objects starts fading. Lower values reduce distant coverage sooner.");
	Util::Widgets::SliderFloat("Object snow fade end", &wsettings.ObjectFadeEnd, 1.0f, 40001.0f);
	Util::AddTooltip("Distance where object snow reaches the selected fade amount. Increase it to keep full coverage farther away.");
	Util::Widgets::SliderFloat("Object snow fade amount", &wsettings.ObjectFadeAmount, 0.0f, 1.0f);
	Util::AddTooltip("How much distant object snow disappears. 0 keeps all coverage; 1 removes it fully beyond the fade distance.");
	MenuUI::SectionHeading("Snow altitude map", "Use the texture and world coordinates supplied with a custom snow map. Save region settings to apply texture changes.");
	Util::Widgets::InputText("Map texture", mapTextureInput, sizeof(mapTextureInput));
	Util::AddTooltip("Snow map supplied for this region. Change it only when configuring a custom map; save region settings to apply it.");
	const auto coordinate = [](const char* label, float& value, const char* help) {
		double edited = value;
		const bool changed = Util::Widgets::InputDouble(label, &edited, 0, 0, "%.3f");
		Util::AddTooltip(help);
		if (changed)
			value = static_cast<float>(edited);
		return changed;
	};
	const char* firstHelp = "World coordinates of one corner covered by the snow map. Use the coordinates supplied with that map.";
	const char* oppositeHelp = "World coordinates of the opposite corner covered by the snow map. Wrong coordinates place snow in the wrong areas.";
	bool firstChanged = coordinate("Map first corner X", mapMin.x, firstHelp);
	firstChanged |= coordinate("Map first corner Y", mapMin.y, firstHelp);
	bool oppositeChanged = coordinate("Map opposite corner X", mapMax.x, oppositeHelp);
	oppositeChanged |= coordinate("Map opposite corner Y", mapMax.y, oppositeHelp);
	if (firstChanged || oppositeChanged)
		UpdateMapTransform();
	Util::Widgets::SliderFloat("Map altitude scale", &wsettings.mapZscale, 0.1f, 1000000.0f);
	Util::AddTooltip("Strength of the snow map's height differences. Higher values make snowy and snow-free areas differ more strongly.");
}

void SnowCover::DrawSeasonSettings()
{
	MenuUI::SectionHeading("Seasonal snow line");
	int summer = static_cast<int>(MaxSummerMonth);
	int winter = static_cast<int>(MaxWinterMonth);
	if (Util::Widgets::SliderInt("Peak summer month (0-11)", &summer, 0, 11))
		MaxSummerMonth = static_cast<uint>(summer);
	Util::AddTooltip("Month used for peak summer conditions. 0 is Morning Star; 6 is Sun's Height.");
	if (Util::Widgets::SliderInt("Peak winter month (0-11)", &winter, 0, 11))
		MaxWinterMonth = static_cast<uint>(winter);
	Util::AddTooltip("Month used for peak winter conditions. 0 is Morning Star; 11 is Evening Star.");
	Util::Widgets::SliderFloat("Summer snow altitude", &SummerHeightOffset, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
	Util::AddTooltip("Snow line at peak summer. Higher values leave snow only on higher ground; lower values spread it downhill.");
	Util::Widgets::SliderFloat("Winter snow altitude", &WinterHeightOffset, DEFAULT_WINTER_HEIGHT_OFFSET, DEFAULT_SUMMER_HEIGHT_OFFSET);
	Util::AddTooltip("Snow line at peak winter. Lower values bring snow farther downhill; higher values keep lower ground clearer.");
	MenuUI::SectionHeading("Weather snow");
	Util::Widgets::SliderFloat("Snow accumulation speed", &snowingSpeed, 0.01f, 10.0f);
	Util::AddTooltip("How quickly snowfall builds visible cover as game time passes. Higher values cover the ground sooner.");
	Util::Widgets::SliderFloat("Snow melting speed", &meltingSpeed, 0.01f, 10.0f);
	Util::AddTooltip("How quickly weather-built snow clears after snowfall ends. Higher values clear it sooner; rain speeds melting up.");
}

void SnowCover::DrawMaterialSettings()
{
	MenuUI::SectionHeading("Snow textures");
	if (Util::Widgets::Checkbox("Use landscape snow color", &useLandscapeSnowColor))
		refreshTextures.store(true);
	Util::AddTooltip("Follows the installed main and alternate snow texture sets, including custom texture paths, seasonal swaps and PBR snow. Uses the standard landscape snow textures when those records are unavailable. Keeps the configured snow detail, shine and tints. Missing or unsupported textures keep the configured color. Save region settings to retain this option.");
	Util::Widgets::InputText("Main texture", mainTextureInput, sizeof(mainTextureInput));
	Util::AddTooltip("Main snow surface supplied by your texture mod. Enter its path below Data without the .dds ending. Landscape snow color overrides its color when available, while keeping its normal and material maps. Save region settings after changing the path.");
	Util::Widgets::InputText("Alternative texture", alternateTextureInput, sizeof(alternateTextureInput));
	Util::AddTooltip("Second snow or ice surface used on different slopes. Enter its path below Data without the .dds ending. Landscape snow color overrides its color when available, while keeping its normal and material maps. Save region settings after changing the path.");
	MenuUI::SectionHeading("Colour and reflections");
	Util::Widgets::ColorEdit4("Main tint", &wsettings.MainTint.x);
	Util::AddTooltip("Color of the main snow surface. Lower opacity reveals more of the original color and reduces seasonal snow on foliage.");
	Util::Widgets::ColorEdit4("Alternative tint", &wsettings.AltTint.x);
	Util::AddTooltip("Color of the second snow surface. Lower opacity lets more of the original surface color show through.");
	Util::Widgets::SliderFloat("Main shine", &wsettings.MainSpec, 0.0f, 1.0f);
	Util::AddTooltip("Brightness of reflections on the main snow surface. Higher values make it shinier.");
	Util::Widgets::SliderFloat("Alternative shine", &wsettings.AltSpec, 0.0f, 1.0f);
	Util::AddTooltip("Brightness of reflections on the second snow surface. Higher values make it shinier.");
	MenuUI::SectionHeading("Slopes");
	Util::Widgets::SliderFloat("Minimum slope", &wsettings.MinAngle, 0.0f, 1.0f);
	Util::AddTooltip("How flat a surface must be before snow starts sticking. 0 means a vertical wall; 1 means flat ground. Higher values keep snow off steeper slopes.");
	Util::Widgets::SliderFloat("Full coverage slope", &wsettings.MaxAngle, 0.0f, 1.0f);
	Util::AddTooltip("How flat a surface must be for full snow coverage. 0 means a vertical wall; 1 means flat ground. Higher values reserve full coverage for flatter ground.");
	Util::Widgets::SliderFloat("Main material peak slope", &wsettings.PeakMainAngle, 0.0f, 1.0f);
	Util::AddTooltip("Slope where the main snow texture is preferred. Lower values favor steep surfaces; higher values favor flat ground.");
	Util::Widgets::SliderFloat("Alternative material peak slope", &wsettings.PeakAltAngle, 0.0f, 1.0f);
	Util::AddTooltip("Slope where the second snow texture is preferred. Lower values favor steep surfaces; higher values favor flat ground.");
	MenuUI::SectionHeading("Surface detail");
	Util::Widgets::SliderFloat("Texture scale", &wsettings.UVScale, 0.01f, 10.0f);
	Util::AddTooltip("Size of repeating snow detail. Higher values repeat it more often, making the visible pattern smaller.");
	Util::Widgets::SliderFloat("Sparkle smoothing", &wsettings.ScreenSpaceScale, 1.0f, 3.0f);
	Util::AddTooltip("Smooths very fine sparkle detail. Higher values blend small flashes together more strongly.");
	Util::Widgets::SliderFloat("Sparkle density", &wsettings.LogMicrofacetDensity, 0.0f, 40.0f);
	Util::AddTooltip("How densely tiny reflections cover the snow. Higher values produce finer, denser sparkle; lower values leave more isolated flashes.");
	Util::Widgets::SliderFloat("Sparkle softness", &wsettings.MicrofacetRoughness, 0.001f, 1.0f);
	Util::AddTooltip("How widely snow reflects sparkle light. Higher values soften and broaden the highlights.");
	Util::Widgets::SliderFloat("Sparkle variation", &wsettings.DensityRandomization, 0.001f, 5.0f);
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
	if (Util::Widgets::Checkbox("Snowmelt wetness", &meltWetnessEnabled))
		WetternessSnowMelt::Reset();
	Util::AddTooltip("Leaves local wet surfaces and puddles as snow melts, including around fires. Uses Wetterness drying controls and requires Wetterness to be enabled.");
	Util::Widgets::Checkbox("Melt snow near fires", &fireMeltSettings.Enabled);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("On: nearby fires clear patches of snow. Off: fires leave snow untouched. Applies to placed fires, not spells or projectiles.");
	if (advanced) {
		Util::Widgets::CheckboxFlags("Skip objects outside snowy areas", &settings.EnableBoundsCulling, 1u);
		Util::AddTooltip("On: skips snow processing for objects that cannot receive snow. Off: checks them anyway. Normally the picture should stay the same.");
	}
}
