#include "I18n/I18n.h"
#include "SkySync.h"
#include "Menu/SettingsPage.h"
#include "RE/S/Setting.h"
#include "Utils/UI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "Utils/Finite.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SkySync::Settings,
	Enabled,
	UseAlternateSunPath,
	EnableSunLensFlare,
	MoonLightSource,
	SunPath,
	CustomAngle,
	SunriseBeginOffset,
	SunriseEndOffset,
	SunsetBeginOffset,
	SunsetEndOffset,
	MinShadowElevation,
	DimSunlightUnderHorizon,
	HorizonFadeHours,
	ShadowTransitionDuration,
	DimVolumetricLighting,
	NewMoonIntensity,
	CrescentMoonIntensity,
	FullMoonIntensity)

void SkySync::DrawSettings()
{
	const char* SunPathNames[] = { T("feature.sky_sync.southern_sky", "Southern Sky"), T("feature.sky_sync.northern_sky", "Northern Sky"), T("feature.sky_sync.vanilla", "Vanilla"), T("feature.sky_sync.custom", "Custom") };
	const char* MoonLightSourceNames[] = { T("feature.sky_sync.brightest", "Brightest"), T("feature.sky_sync.masser", "Masser"), T("feature.sky_sync.secunda", "Secunda") };
	ImGui::SliderFloat(T("feature.sky_sync.shadow_transition_duration", "Shadow transition duration"), &settings.ShadowTransitionDuration, 0.0f, 600.0f);
	ImGui::Checkbox(T("feature.sky_sync.dim_volumetric_lighting", "Dim volumetric lighting"), &settings.DimVolumetricLighting);
	ImGui::SliderFloat(T("feature.sky_sync.new_moon_intensity", "New moon intensity"), &settings.NewMoonIntensity, 0.0f, 1.0f);
	ImGui::SliderFloat(T("feature.sky_sync.crescent_moon_intensity", "Crescent moon intensity"), &settings.CrescentMoonIntensity, 0.0f, 1.0f);
	ImGui::SliderFloat(T("feature.sky_sync.full_moon_intensity", "Full moon intensity"), &settings.FullMoonIntensity, 0.0f, 1.0f);
	MenuUI::SettingsPage page("SkySync", {
											 { "path", T("feature.sky_sync.sky_path", "Sky path"), T("feature.sky_sync.choose_how_the_sun_and_moon_move_and_light_the_scene", "Choose how the sun and moon move and light the scene."), T("feature.sky_sync.sun_and_moon_trajectories", "Sun and moon trajectories"), true, true, T("feature.sky_sync.choose_movement_and_light", "Choose movement and light") },
											 { "lighting", T("feature.sky_sync.lighting", "Lighting"), T("feature.sky_sync.refine_horizon_lighting_and_shadow_length", "Refine horizon lighting and shadow length."), T("feature.sky_sync.horizon_fading_and_shadows", "Horizon fading and shadows"), true, true, nullptr },
											 { "timing", T("feature.sky_sync.timing", "Timing"), T("feature.sky_sync.adjust_the_visible_sunrise_and_sunset", "Adjust the visible sunrise and sunset."), T("feature.sky_sync.sunrise_and_sunset_offsets", "Sunrise and sunset offsets"), true, true, T("feature.sky_sync.refine_time_transitions", "Refine time transitions") },
											 { "worldspaces", T("feature.sky_sync.worldspaces", "Worldspaces"), T("feature.sky_sync.leave_selected_realms_to_the_game_s_own_lighting", "Leave selected realms to the game's own lighting."), T("feature.sky_sync.worldspace_exclusions", "Worldspace exclusions"), true, true, nullptr },
										 });

	if (page.Is("path")) {
		Util::Widgets::Checkbox(T("feature.sky_sync.use_alternate_sun_path", "Use alternate sun path"), &settings.UseAlternateSunPath);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.sky_sync.use_alternate_sun_path_tooltip", "Calculate sun position based on time of day and season instead of vanilla movement."));
		}

		Util::Widgets::Checkbox(T("feature.sky_sync.enable_sun_lens_flare", "Enable weather lens flare"), &settings.EnableSunLensFlare);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.sky_sync.show_the_active_weather_s_lens_flare_sprites_d11d54e5", "Show the active weather's lens-flare sprites. Has no visible effect if that weather provides none."));
		}

		if (settings.UseAlternateSunPath) {
			if (Util::Widgets::Combo(T("feature.sky_sync.sun_path", "Sun path"), &settings.SunPath, SunPathNames, static_cast<int>(SunPath::Count)))
				SetSunAngle();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T("feature.sky_sync.sun_path_tooltip", "Choose the trajectory the sun takes across the sky."));
			}

			if (settings.SunPath == static_cast<int32_t>(SunPath::Custom)) {
				if (Util::Widgets::SliderFloat(T("feature.sky_sync.custom_angle", "Custom angle"), &settings.CustomAngle, -90.0f, 90.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
					SetSunAngle();
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted(T("feature.sky_sync.custom_angle_tooltip", "Set a custom angle for the sun's trajectory."));
				}
			}
		}

		Util::Widgets::Combo(T("feature.sky_sync.moon_light_source", "Moon light source"), &settings.MoonLightSource, MoonLightSourceNames, static_cast<int>(MoonLightSource::Count));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.sky_sync.moon_light_source_tooltip", "Select which moon casts shadows during the night."));
		}
	}
	if (page.Is("lighting")) {
		Util::Widgets::SliderFloat(T("feature.sky_sync.min_shadow_elevation", "Min Shadow Elevation"), &settings.MinShadowElevation, 0.0f, 45.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(T("feature.sky_sync.min_shadow_elevation_tooltip", "The minimum angle sunlight will set to. Caps shadow length. Higher = shorter shadows at sunset/sunrise."));
		}
		bool dimSunlight = settings.DimSunlightUnderHorizon;
		float fadeHours = settings.HorizonFadeHours;
		bool dimmingChanged = Util::Widgets::Checkbox(T("feature.sky_sync.dim_sunlight_under_horizon", "Dim Sunlight Under Horizon"), &dimSunlight);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(T("feature.sky_sync.fade_direct_lighting_around_sunset_and_sunris_79c3804f", "Fade direct lighting around sunset and sunrise while preserving the weather's light colour."));
		if (dimSunlight)
			dimmingChanged |= Util::Widgets::SliderFloat(T("feature.sky_sync.horizon_fade_duration", "Horizon Fade Duration"), &fadeHours, 0.0f, MaxHorizonFadeHours, "%.1f h", ImGuiSliderFlags_AlwaysClamp);
		if (dimmingChanged)
			SetSunlightDimming(dimSunlight, fadeHours);
		ImGui::Spacing();
		ImGui::Spacing();
	}
	if (page.Is("worldspaces"))
		DrawWorldspaceSettings();
	if (page.Is("timing")) {
		ImGui::TextWrapped(T("feature.sky_sync.moves_the_visual_sun_path_during_sunrise_suns_a82ad59a", "Moves the visual sun path during sunrise/sunset. Moon lighting follows the climate's night timings."));
		Util::Widgets::SliderFloat(T("feature.sky_sync.sunrise_begin_hours", "Sunrise Begin (Hours)"), &settings.SunriseBeginOffset, -5.0f, 5.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.sky_sync.offset_for_when_the_sun_starts_rising", "Offset for when the sun starts rising."));
		}
		Util::Widgets::SliderFloat(T("feature.sky_sync.sunrise_end_hours", "Sunrise End (Hours)"), &settings.SunriseEndOffset, -5.0f, 5.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.sky_sync.offset_for_when_the_sun_finishes_rising", "Offset for when the sun finishes rising."));
		}
		Util::Widgets::SliderFloat(T("feature.sky_sync.sunset_begin_hours", "Sunset Begin (Hours)"), &settings.SunsetBeginOffset, -5.0f, 5.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.sky_sync.offset_for_when_the_sun_starts_setting", "Offset for when the sun starts setting."));
		}
		Util::Widgets::SliderFloat(T("feature.sky_sync.sunset_end_hours", "Sunset End (Hours)"), &settings.SunsetEndOffset, -5.0f, 5.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.sky_sync.offset_for_when_the_sun_finishes_setting", "Offset for when the sun finishes setting."));
		}
	}
}

void SkySync::DrawWorldspaceSettings()
{
	ImGui::TextWrapped(T("feature.sky_sync.sky_sync_leaves_lighting_and_weather_lens_fla_cda2a59a", "Sky Sync leaves lighting and weather lens flares to the game in these worldspaces. Changes take effect on the next sky update."));
	static std::array<char, MaxWorldspaceEditorIDLength + 1> editorID{};
	static bool invalidList = false;
	auto addWorldspace = [&](const char* a_editorID) {
		auto worldspaces = settings.DisabledWorldspaces;
		if (std::ranges::none_of(worldspaces, [&](const auto& entry) { return _stricmp(entry.c_str(), a_editorID) == 0; }))
			worldspaces.emplace_back(a_editorID);
		invalidList = !SetDisabledWorldspaces(worldspaces);
		if (!invalidList)
			editorID.fill('\0');
	};
	Util::Widgets::InputText(T("feature.sky_sync.worldspace_editor_id", "Worldspace Editor ID"), editorID.data(), editorID.size());
	if (ImGui::Button(T("feature.sky_sync.add_worldspace", "Add worldspace")))
		addWorldspace(editorID.data());
	if (const auto worldspace = GetCurrentWorldspace()) {
		if (const char* currentID = worldspace->GetFormEditorID(); currentID && *currentID) {
			ImGui::Text(T("feature.sky_sync.current_worldspace_s", "Current worldspace: %s"), currentID);
			if (ImGui::Button(T("feature.sky_sync.add_current_worldspace", "Add current worldspace")))
				addWorldspace(currentID);
		}
	}
	if (invalidList)
		ImGui::TextWrapped(T("feature.sky_sync.list_unchanged_use_at_most_zu_entries_each_wi_46da1a8c", "List unchanged. Use at most %zu entries, each with 1-%zu bytes and no control characters."), MaxDisabledWorldspaces, MaxWorldspaceEditorIDLength);

	std::optional<size_t> removeIndex;
	if (ImGui::BeginTable("DisabledWorldspaces", 2, ImGuiTableFlags_SizingStretchProp)) {
		const SKSE::stl::scope_exit endTable([] { ImGui::EndTable(); });
		for (size_t i = 0; i < settings.DisabledWorldspaces.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			const SKSE::stl::scope_exit popID([] { ImGui::PopID(); });
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(settings.DisabledWorldspaces[i].c_str());
			ImGui::TableNextColumn();
			if (ImGui::Button(T("feature.sky_sync.remove", "Remove")))
				removeIndex = i;
		}
	}
	if (removeIndex) {
		auto worldspaces = settings.DisabledWorldspaces;
		worldspaces.erase(worldspaces.begin() + *removeIndex);
		invalidList = !SetDisabledWorldspaces(worldspaces);
	}
}

bool SkySync::SetDisabledWorldspaces(const json& a_worldspaces)
{
	if (!a_worldspaces.is_array() || a_worldspaces.size() > MaxDisabledWorldspaces)
		return false;
	std::vector<std::string> worldspaces;
	worldspaces.reserve(a_worldspaces.size());
	for (const auto& value : a_worldspaces) {
		if (!value.is_string())
			return false;
		const auto& editorID = value.get_ref<const std::string&>();
		if (editorID.empty() || editorID.size() > MaxWorldspaceEditorIDLength ||
			std::ranges::any_of(editorID, [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
			return false;
		if (std::ranges::none_of(worldspaces, [&](const auto& entry) { return _stricmp(entry.c_str(), editorID.c_str()) == 0; }))
			worldspaces.push_back(editorID);
	}
	if (worldspaces != settings.DisabledWorldspaces) {
		settings.DisabledWorldspaces = std::move(worldspaces);
		worldspaceCacheValid = false;
	}
	return true;
}

#ifdef DEVBENCH_BRIDGE_ENABLED
json SkySync::GetWorldspaceStatus() const
{
	return { { "disabledWorldspaces", settings.DisabledWorldspaces }, { "lastWorldspaceEditorID", cachedWorldspaceEditorID },
		{ "lastWorldspaceDisabled", worldspaceDisabled }, { "updatePending", !worldspaceCacheValid } };
}
#endif

RE::TESWorldSpace* SkySync::GetCurrentWorldspace()
{
	const auto player = RE::PlayerCharacter::GetSingleton();
	const auto cell = player ? player->GetParentCell() : nullptr;
	return cell ? cell->GetRuntimeData().worldSpace : nullptr;
}

void SkySync::UpdateWorldspace()
{
	if (!loaded || !settings.Enabled) {
		worldspaceCacheValid = false;
		return;
	}
	const auto worldspace = GetCurrentWorldspace();
	if (worldspaceCacheValid && worldspace == cachedWorldspace)
		return;
	const char* editorID = worldspace ? worldspace->GetFormEditorID() : nullptr;
	const bool disabled = editorID && std::ranges::any_of(settings.DisabledWorldspaces, [&](const auto& entry) { return _stricmp(entry.c_str(), editorID) == 0; });
	if (worldspace != cachedWorldspace || disabled != worldspaceDisabled) {
		ResetRuntimeState();
		currentCell = nullptr;
		currentSkyRotation = D3D11_FLOAT32_MAX;
	}
	cachedWorldspace = worldspace;
#ifdef DEVBENCH_BRIDGE_ENABLED
	cachedWorldspaceEditorID = editorID ? editorID : "";
#endif
	worldspaceDisabled = disabled;
	worldspaceCacheValid = true;
}

void SkySync::LoadSettings(json& o_json)
{
	const auto previousWorldspaces = settings.DisabledWorldspaces;
	settings = o_json;
	// Validate the list before converting it so settings cannot allocate an unbounded vector.
	if (const auto worldspaces = o_json.find("DisabledWorldspaces"); worldspaces != o_json.end() && !SetDisabledWorldspaces(*worldspaces)) {
		settings.DisabledWorldspaces = previousWorldspaces;
		logger::warn("[Sky Sync] Invalid DisabledWorldspaces; retaining the previous list");
	}
	worldspaceCacheValid = false;
	const Settings defaults{};
	settings.MoonLightSource = std::clamp(settings.MoonLightSource, static_cast<int32_t>(MoonLightSource::Brightest), static_cast<int32_t>(MoonLightSource::Secunda));
	settings.SunPath = std::clamp(settings.SunPath, static_cast<int32_t>(SunPath::Southern), static_cast<int32_t>(SunPath::Custom));
	settings.CustomAngle = Util::ClampFinite(settings.CustomAngle, -90.0f, 90.0f, defaults.CustomAngle);
	settings.SunriseBeginOffset = Util::ClampFinite(settings.SunriseBeginOffset, -5.0f, 5.0f, defaults.SunriseBeginOffset);
	settings.SunriseEndOffset = Util::ClampFinite(settings.SunriseEndOffset, -5.0f, 5.0f, defaults.SunriseEndOffset);
	settings.SunsetBeginOffset = Util::ClampFinite(settings.SunsetBeginOffset, -5.0f, 5.0f, defaults.SunsetBeginOffset);
	settings.SunsetEndOffset = Util::ClampFinite(settings.SunsetEndOffset, -5.0f, 5.0f, defaults.SunsetEndOffset);
	settings.MinShadowElevation = Util::ClampFinite(settings.MinShadowElevation, 0.0f, 45.0f, defaults.MinShadowElevation);
	settings.ShadowTransitionDuration = Util::ClampFinite(settings.ShadowTransitionDuration, 0.0f, 600.0f, defaults.ShadowTransitionDuration);
	settings.NewMoonIntensity = Util::ClampFinite(settings.NewMoonIntensity, 0.0f, 1.0f, defaults.NewMoonIntensity);
	settings.CrescentMoonIntensity = Util::ClampFinite(settings.CrescentMoonIntensity, 0.0f, 1.0f, defaults.CrescentMoonIntensity);
	settings.FullMoonIntensity = Util::ClampFinite(settings.FullMoonIntensity, 0.0f, 1.0f, defaults.FullMoonIntensity);
	settings.HorizonFadeHours = Util::ClampFinite(settings.HorizonFadeHours, 0.0f, MaxHorizonFadeHours, DefaultHorizonFadeHours);
	if (!settings.Enabled) {
		ResetRuntimeState();
	}
	SetSunAngle();
}

void SkySync::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["DisabledWorldspaces"] = settings.DisabledWorldspaces;
}

void SkySync::RestoreDefaultSettings()
{
	settings = {};
	worldspaceCacheValid = false;
	ResetRuntimeState();
	SetSunAngle();
}

bool SkySync::SetSunlightDimming(bool enabled, float fadeHours)
{
	if (!std::isfinite(fadeHours) || fadeHours < 0.0f || fadeHours > MaxHorizonFadeHours)
		return false;
	settings.DimSunlightUnderHorizon = enabled;
	settings.HorizonFadeHours = fadeHours;
	return true;
}

float SkySync::GetVolumetricLightingIntensityFactor() const
{
	if (!loaded || !settings.Enabled || !settings.DimVolumetricLighting)
		return DefaultVolumetricLightingIntensityFactor;

	return SkySync::NormalizeVolumetricLightingIntensity(volumetricLightingIntensityFactor);
}

std::optional<RE::NiPoint3> SkySync::GetCelestialLightDirection() const
{
	const auto sky = globals::game::sky;
	if (!loaded || !settings.Enabled || !shadowFader.celestialDirection || !sky || !sky->root)
		return std::nullopt;

	auto direction = sky->root->world.rotate * *shadowFader.celestialDirection;
	const float length = direction.Unitize();
	if (!std::isfinite(length) || length <= FLT_EPSILON)
		return std::nullopt;

	return direction;
}

void SkySync::PostPostLoad()
{
	moonAndStarsLoaded = GetModuleHandle(L"po3_MoonMod.dll");
	if (moonAndStarsLoaded)
		logger::info("[Sky Sync] Moon and Stars detected, compatibility enabled");

	if (GetModuleHandle(L"EVLaS.dll")) {
		DisableOnConflict("EVLaS");
		return;
	}

	const auto moonHookResult = stl::detour_thunk<Moon_Update>(REL::RelocationID(25626, 26169));
	const auto hookResult = moonHookResult == NO_ERROR ? stl::detour_thunk<Sky_Update>(REL::RelocationID(25682, 26229)) : moonHookResult;
	if (hookResult != NO_ERROR) {
		failedLoadedMessage = fmt::format("Failed to install Sky Sync hook (error {})", hookResult);
		loadFailed = true;
		loaded = false;
		settings.Enabled = false;
		Util::SetCelestialTransitionHandlerAvailable(false);
		ResetRuntimeState();
		logger::error("[Sky Sync] {}", failedLoadedMessage);
		return;
	}
	Util::SetCelestialTransitionHandlerAvailable(true);

	gSunPosition = reinterpret_cast<RE::NiPoint3*>(REL::RelocationID(527924, 414871).address());

	gSunGlareSize = reinterpret_cast<float*>(REL::RelocationID(502611, 370235).address());
	gMasserSize = reinterpret_cast<uint32_t*>(REL::RelocationID(502558, 370155).address());
	gSecundaSize = reinterpret_cast<uint32_t*>(REL::RelocationID(502570, 370173).address());
	logger::info("[Sky Sync] Installed hooks");
}

void SkySync::DataLoaded()
{
	const auto data = RE::TESDataHandler::GetSingleton();
	if (data && (data->LookupLoadedModByName("DVLaSS.esp"sv) || data->LookupLoadedLightModByName("DVLaSS.esp"sv)))
		DisableOnConflict("DVLaSS");
	if (const auto collection = globals::game::gameSettingCollection)
		gSunAlphaTransTime = collection->GetSetting("fSunAlphaTransTime");
}

void SkySync::DisableOnConflict(std::string_view conflictName)
{
	// A detected conflict is expected environment gating, not a broken install.
	loadFailed = false;
	loaded = false;
	settings.Enabled = false;
	Util::SetCelestialTransitionHandlerAvailable(false);
	ResetRuntimeState();
	logger::warn("[Sky Sync] Disabled as {} has been detected, both cannot be used together", conflictName);
}

void SkySync::ResetRuntimeState()
{
	RestoreSunlight();
	RestoreWeatherLensFlares();
	shadowFader.Reset();
	volumetricLightingIntensityFactor = DefaultVolumetricLightingIntensityFactor;
}

void SkySync::RestoreSunlight()
{
	if (adjustedSunlight) {
		sunlightAdjustment.Restore(adjustedSunlight.get(), adjustedSunlight->GetLightRuntimeData().diffuse);
		adjustedSunlight.reset();
	}
	sunlightDimmingFactor = 1.0f;
}

bool SkySync::ClimateTimings::IsDayTime(float time) const
{
	return time >= weatherSunriseBegin && time < weatherSunsetEnd;
}

float SkySync::ClimateTimings::NightHorizonDistance(float time) const
{
	if (IsDayTime(time))
		return 0.0f;
	const float sinceSunset = time >= weatherSunsetEnd ? time - weatherSunsetEnd : time + HoursPerDay - weatherSunsetEnd;
	const float untilSunrise = time < weatherSunriseBegin ? weatherSunriseBegin - time : weatherSunriseBegin + HoursPerDay - time;
	return std::min(sinceSunset, untilSunrise);
}

float SkySync::ClimateTimings::SunlightDimming(float time, float fadeHours) const
{
	fadeHours = Util::ClampFinite(fadeHours, 0.0f, MaxHorizonFadeHours, DefaultHorizonFadeHours);

	if (time >= weatherSunsetMiddle && time < weatherSunsetEnd)
		return std::sqrt(std::clamp((weatherSunsetEnd - time) / (weatherSunsetEnd - weatherSunsetMiddle), 0.0f, 1.0f));
	if (time >= weatherSunriseBegin && time < weatherSunriseMiddle)
		return std::sqrt(std::clamp((time - weatherSunriseBegin) / (weatherSunriseMiddle - weatherSunriseBegin), 0.0f, 1.0f));
	if (!IsDayTime(time) && fadeHours > 0.0f)
		return std::clamp(NightHorizonDistance(time) / fadeHours, 0.0f, 1.0f);
	return 1.0f;
}

void SkySync::ApplyWeatherLensFlareSetting(const RE::Sky* sky)
{
	if (!sky || !loaded || !settings.Enabled || worldspaceDisabled || settings.EnableSunLensFlare) {
		RestoreWeatherLensFlares();
		return;
	}

	auto suppress = [this](RE::TESWeather* weather) {
		if (!weather || !weather->sunGlareLensFlare)
			return;

		auto [entry, inserted] = suppressedWeatherLensFlares.try_emplace(weather, weather->sunGlareLensFlare);
		// Preserve a replacement made by another system while this toggle was active.
		if (!inserted)
			entry->second = weather->sunGlareLensFlare;
		weather->sunGlareLensFlare = nullptr;
	};

	suppress(sky->currentWeather);
	suppress(sky->lastWeather);
}

void SkySync::RestoreWeatherLensFlares()
{
	for (const auto& [weather, lensFlare] : suppressedWeatherLensFlares) {
		// Never overwrite a flare another system already restored or replaced.
		if (weather && !weather->sunGlareLensFlare)
			weather->sunGlareLensFlare = lensFlare;
	}
	suppressedWeatherLensFlares.clear();
}

float SkySync::NormalizeVolumetricLightingIntensity(float intensity)
{
	if (!std::isfinite(intensity))
		intensity = DefaultVolumetricLightingIntensityFactor;
	return std::clamp(intensity, 0.0f, 1.0f);
}

void SkySync::Sky_Update::thunk(RE::Sky* sky)
{
	auto& skySync = globals::features::skySync;
	// Remove our previous scale before the engine refreshes or reuses its light colour.
	skySync.RestoreSunlight();
	skySync.UpdateWorldspace();
	skySync.ApplyWeatherLensFlareSetting(sky);
	func(sky);
	skySync.PreparePendingTransitions();
	if (skySync.Update(sky))
		Util::CompleteCelestialTransition();
}

bool SkySync::Update(const RE::Sky* sky)
{
	if (!loaded || !settings.Enabled || worldspaceDisabled) {
		ResetRuntimeState();
		const bool completed = immediateTransitionReady;
		immediateTransitionReady = false;
		return completed;
	}

	if (!sky) {
		ResetRuntimeState();
		return false;
	}

	const auto sun = sky->sun;
	const auto climate = sky->currentClimate;
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!sun || !sun->light || !sun->root || !sun->sunBase || !sun->sunGlareNode || !sky->root || !climate || !player || !gSunPosition || !gSunGlareSize || !gMasserSize || !gSecundaSize) {
		ResetRuntimeState();
		return false;
	}

	const auto cell = player->GetParentCell();

	if (cell != currentCell) {
		const auto prevCell = currentCell;
		if (cell)
			SetSkyRotation(sky, cell);
		if (cell && prevCell && (cell->IsInteriorCell() != currentCellInterior || cell->GetRuntimeData().worldSpace != currentCellWorldspace))
			shadowFader.Reset();
	}

	// Interior cells also require the sunlight-shadows flag.
	if (cell && cell->IsInteriorCell() && !cell->cellFlags.all(static_cast<RE::TESObjectCELL::Flag>(CellFlagExt::kSunlightShadows))) {
		ResetRuntimeState();
		return false;
	}

	if (cell) {
		currentCellInterior = cell->IsInteriorCell();
		currentCellWorldspace = cell->GetRuntimeData().worldSpace;
	}
	const float hour = sky->currentGameHour;
	if (!std::isfinite(hour) || hour < 0.0f || hour > HoursPerDay || !timings.Update(climate)) {
		ResetRuntimeState();
		return false;
	}
	const float time = hour == HoursPerDay ? 0.0f : hour;
	const bool isDayTime = timings.IsDayTime(time);

	const auto worldSpace = player->GetWorldspace();
	const float altitude = worldSpace ? player->GetPositionZ() - worldSpace->GetDefaultWaterHeight() : 0.0f;

	ProcessSun(sun, time, altitude, isDayTime);
	HideSunOutsideFadeWindow(sky);
	ProcessMoon(sky->masser, time, Caster::Masser, altitude, isDayTime);
	ProcessMoon(sky->secunda, time, Caster::Secunda, altitude, isDayTime);

	if (settings.DimSunlightUnderHorizon) {
		auto& diffuse = sun->light->GetLightRuntimeData().diffuse;
		if (std::isfinite(diffuse.red) && std::isfinite(diffuse.green) && std::isfinite(diffuse.blue)) {
			sunlightDimmingFactor = timings.SunlightDimming(time, settings.HorizonFadeHours);
			if (sunlightDimmingFactor != 1.0f) {
				adjustedSunlight = sun->light;
				sunlightAdjustment.Apply(adjustedSunlight.get(), diffuse, sunlightDimmingFactor);
			}
		}
	}
	volumetricLightingIntensityFactor = shadowFader.Update(sun, directions, intensities, isDayTime, time, immediateTransitionReady);
	const bool completed = immediateTransitionReady;
	immediateTransitionReady = false;
	return completed;
}
void SkySync::SetSunAngle()
{
	switch (static_cast<SunPath>(settings.SunPath)) {
	case SunPath::Southern:
		sunAngle = SouthernSunAngle;
		break;
	case SunPath::Northern:
		sunAngle = NorthernSunAngle;
		break;
	case SunPath::Vanilla:
		sunAngle = VanillaSunAngle;
		break;
	case SunPath::Custom:
		sunAngle = 90.0f + settings.CustomAngle;
		break;
	default:;
	}
}

void SkySync::SetSkyRotation(const RE::Sky* sky, RE::TESObjectCELL* cell)
{
	// If the interior cell isn't initialised it won't have the north rotation extra data ready, skip for a frame
	if (cell->IsInteriorCell() && cell->cellState == static_cast<RE::TESObjectCELL::CellState>(0))
		return;

	currentCell = cell;
	const float rotation = cell->GetNorthRotation();
	if (rotation == currentSkyRotation)
		return;

	currentSkyRotation = rotation;
	sky->root->local.rotate = RE::NiMatrix3{ RE::NiPoint3{ 0.0f, 0.0f, -rotation } };
	RE::NiUpdateData updateData;
	sky->root->Update(updateData);
}

void SkySync::ProcessSun(const RE::Sun* sun, const float time, const float altitude, const bool isDayTime)
{
	RE::NiPoint3 dir;
	float dist;

	if (settings.UseAlternateSunPath) {
		CalculateAlternateSunDirectionAndDistance(dir, dist, time, timings.sunrise, timings.sunset, sunAngle);
	} else
		CalculateSunDirectionAndDistance(sun, dir, dist);

	rawDirections[static_cast<int>(Caster::Sun)] = dir;

	const RE::NiPoint3 apparentDir = GetApparentDirection(dir, altitude);
	SetSunPosition(sun, apparentDir, dist);

	directions[static_cast<int>(Caster::Sun)] = apparentDir;

	SetSunBaseVisibility(sun, isDayTime ? 1.0f : 0.0f);

	intensities[static_cast<int>(Caster::Sun)] = isDayTime ? CalculateVisibility(dir, dist, *gSunGlareSize * SunScaleFactor) : 0.0f;
}

void SkySync::HideSunOutsideFadeWindow(const RE::Sky* sky)
{
	if (!sky || !sky->sun || !sky->currentClimate || !gSunAlphaTransTime || gSunAlphaTransTime->GetType() != RE::Setting::Type::kFloat)
		return;

	const float transitionHours = gSunAlphaTransTime->GetFloat();
	if (!std::isfinite(transitionHours) || transitionHours < 0.0f || !std::isfinite(sky->currentGameHour))
		return;

	// Match the engine's float operations so exact fade boundaries agree.
	constexpr float HoursPerTimingUnit = 1.0f / 6.0f;
	auto middleHour = [](const RE::TESClimate::Timing::Interval& interval) {
		return (interval.end * HoursPerTimingUnit + interval.begin * HoursPerTimingUnit) * 0.5f;
	};
	const auto& timing = sky->currentClimate->timing;
	const float halfTransition = transitionHours * 0.5f;
	const float fadeInStart = middleHour(timing.sunrise) - halfTransition;
	const float fadeOutEnd = middleHour(timing.sunset) + halfTransition;
	const float hour = sky->currentGameHour;
	if (hour > fadeInStart && hour < fadeOutEnd)
		return;

	for (const auto& geometry : { sky->sun->sunBase, sky->sun->sunGlare }) {
		if (const auto property = geometry ? skyrim_cast<RE::BSSkyShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get()) : nullptr)
			property->kBlendColor.alpha = 0.0f;
	}
}

void SkySync::ProcessMoon(const RE::Moon* moon, const float time, const Caster type, const float altitude, const bool isDayTime)
{
	intensities[static_cast<int>(type)] = 0.0f;
	directions[static_cast<int>(type)] = { 0.0f, 0.0f, 1.0f };
	rawDirections[static_cast<int>(type)] = { 0.0f, 0.0f, -1.0f };

	if (!moon || !moon->root || !moon->moonMesh)
		return;

	const auto dir = moon->root->local.rotate.GetVectorY();

	rawDirections[static_cast<int>(type)] = dir;

	auto apparentDir = GetApparentDirection(dir, altitude);
	SetMoonDirection(moon, apparentDir);

	// Moon and Stars adjusts some intermediary rotation matrices for the moon
	// Directly changing the directions here avoids 3 matrix multiplications and a vector rotation
	if (moonAndStarsLoaded)
		apparentDir = { apparentDir.y, -apparentDir.x, apparentDir.z };

	directions[static_cast<int>(type)] = apparentDir;

	if (isDayTime)
		return;

	const auto src = static_cast<MoonLightSource>(settings.MoonLightSource);
	const bool isValidSource = src == MoonLightSource::Brightest || (src == MoonLightSource::Masser && type == Caster::Masser) || (src == MoonLightSource::Secunda && type == Caster::Secunda);
	if (!isValidSource)
		return;

	const float moonRadius = type == Caster::Masser ? static_cast<float>(*gMasserSize) : static_cast<float>(*gSecundaSize);
	float intensity = CalculateVisibility(dir, moon->moonMesh->local.translate.y, moonRadius);

	if (type == Caster::Masser)
		intensity *= GetMoonIntensity(masserPhase);
	else if (type == Caster::Secunda)
		intensity *= GetMoonIntensity(secundaPhase) * SecundaIntensityFactor;

	intensity *= SmoothStep(0.0f, MoonFadeHours, timings.NightHorizonDistance(time));

	intensities[static_cast<int>(type)] = intensity;
}

inline void SkySync::CalculateSunDirectionAndDistance(const RE::Sun* sun, RE::NiPoint3& outDir, float& outDistance)
{
	outDir = sun->root->local.translate;
	if (outDistance = outDir.Unitize(); outDistance < FLT_EPSILON) {
		outDir = { 0.0f, 0.0f, 1.0f };
		outDistance = SunPeakDistance;
	}
}

inline void SkySync::CalculateAlternateSunDirectionAndDistance(RE::NiPoint3& outDir, float& outDist, const float time, const float sunrise, const float sunset, const float sunAngle)
{
	const float phi = DirectX::XM_PI * ((time - sunrise) / (sunset - sunrise));
	float sinPhi, cosPhi;
	DirectX::XMScalarSinCosEst(&sinPhi, &cosPhi, phi);

	float tiltRadians = DirectX::XMConvertToRadians(sunAngle);
	float cosTilt, sinTilt;
	DirectX::XMScalarSinCosEst(&sinTilt, &cosTilt, tiltRadians);

	outDir = { cosPhi, -sinPhi * cosTilt, sinPhi * sinTilt };

	if (const float length = outDir.Unitize(); length < FLT_EPSILON)
		outDir = { 0.0f, 0.0f, 1.0f };

	const float elevationRatio = std::max(sinPhi, 0.0f);
	outDist = std::lerp(SunHorizonDistance, SunPeakDistance, elevationRatio);
}

RE::NiPoint3 SkySync::GetApparentDirection(const RE::NiPoint3& dir, const float altitude)
{
	const float dipAngle = -std::atan(altitude / RenderDistance);
	float sinPhi, cosPhi;
	DirectX::XMScalarSinCosEst(&sinPhi, &cosPhi, dipAngle);

	const auto rotationAxis = dir.UnitCross({ 0.0f, 0.0f, 1.0f });
	const float axisDotDir = rotationAxis.Dot(dir);
	const auto axisCrossDir = rotationAxis.Cross(dir);
	const float oneMinusCosPhi = 1.0f - cosPhi;

	const float x = dir.x * cosPhi + axisCrossDir.x * sinPhi + rotationAxis.x * (axisDotDir * oneMinusCosPhi);
	const float y = dir.y * cosPhi + axisCrossDir.y * sinPhi + rotationAxis.y * (axisDotDir * oneMinusCosPhi);
	const float z = dir.z * cosPhi + axisCrossDir.z * sinPhi + rotationAxis.z * (axisDotDir * oneMinusCosPhi);

	RE::NiPoint3 rotated = { x, y, z };
	rotated.Unitize();
	return rotated;
}

inline void SkySync::SetSunPosition(const RE::Sun* sun, const RE::NiPoint3& dir, const float distance)
{
	const auto position = dir * distance;
	sun->root->local.translate = position;
	sun->sunGlareNode->local.translate = position;
	*gSunPosition = position;
}

inline void SkySync::SetMoonDirection(const RE::Moon* moon, const RE::NiPoint3& dir)
{
	auto& m = moon->root->local.rotate;
	m.entry[0][1] = dir.x;
	m.entry[1][1] = dir.y;
	m.entry[2][1] = dir.z;
}

inline float SkySync::CalculateVisibility(const RE::NiPoint3& dir, const float dist, const float radius)
{
	const float height = dir.Dot({ 0.0f, 0.0f, 1.0f }) * dist;
	return SmoothStep(-radius, radius, height);
}

inline void SkySync::SetSunBaseVisibility(const RE::Sun* sun, const float visibility)
{
	if (const auto property = skyrim_cast<RE::BSSkyShaderProperty*>(sun->sunBase->GetGeometryRuntimeData().shaderProperty.get()))
		property->kBlendColor.alpha = visibility;
}

void SkySync::ShadowFader::Reset()
{
	fadePhase = Phase::None;
	current = Caster::None;
	target = Caster::None;
	fadeTimer = 0.0f;
	previousHoursPassed = globals::game::calendar ? globals::game::calendar->GetHoursPassed() : 0.0f;
	sunriseReleased = false;
	frozenHeading = 0.0f;
	sunsetHeadingLocked = false;
	celestialDirection.reset();
}

float SkySync::ShadowFader::Update(const RE::Sun* sun, RE::NiPoint3 dirs[3], float intensities[3], const bool isDayTime, const float time, const bool a_immediateTransition)
{
	const float fadeDuration = std::max(globals::features::skySync.settings.ShadowTransitionDuration, 1e-3f);
	const float masserIntensity = intensities[static_cast<int>(Caster::Masser)];
	const float secundaIntensity = intensities[static_cast<int>(Caster::Secunda)];

	auto desired = Caster::None;
	if (isDayTime)
		desired = Caster::Sun;
	else if (masserIntensity > 0.0f && masserIntensity >= secundaIntensity)
		desired = Caster::Masser;
	else if (secundaIntensity > 0.0f)
		desired = Caster::Secunda;

	if (desired != target) {
		target = desired;
		fadeTimer = 0.0f;

		if (current == Caster::None) {
			fadePhase = Phase::FadeIn;
			current = target;
		} else
			fadePhase = Phase::FadeOut;
	}

	float fadeAdvance = 0.0f;
	// A fading-out moon must not retain the light once the weather starts its dawn colours.
	if (isDayTime && current != Caster::Sun) {
		current = Caster::Sun;
		fadePhase = Phase::FadeIn;
		fadeTimer = 0.0f;
	}
	if (const auto calendar = globals::game::calendar) {
		const float currentHoursPassed = calendar->GetHoursPassed();
		const float timeScale = calendar->GetTimescale();
		const bool validCurrentHours = std::isfinite(currentHoursPassed);
		const bool validPreviousHours = std::isfinite(previousHoursPassed);
		const float hoursPassedDiff = validCurrentHours && validPreviousHours ? std::abs(currentHoursPassed - previousHoursPassed) : fadeDuration / SecondsPerGameHour;
		if (validCurrentHours)
			previousHoursPassed = currentHoursPassed;
		if (a_immediateTransition || globals::features::skySync.settings.ShadowTransitionDuration <= 0.0f || timeScale <= 0.0f || !validCurrentHours || !validPreviousHours || hoursPassedDiff >= 0.01f) {
			fadePhase = Phase::None;
			current = target;
		} else {
			fadeAdvance = hoursPassedDiff * SecondsPerGameHour;
			if (globals::game::deltaTime && std::isfinite(*globals::game::deltaTime))
				fadeAdvance = std::max(fadeAdvance, std::max(*globals::game::deltaTime * timeScale, 0.0f));
		}
	} else if (globals::game::deltaTime) {
		fadeAdvance = *globals::game::deltaTime * 20.0f;
	}

	if (a_immediateTransition) {
		current = target;
		fadePhase = Phase::None;
	}

	// Capture this frame's caster before elevation limits or the end-of-frame handoff.
	celestialDirection.reset();
	if (current != Caster::None)
		celestialDirection = dirs[static_cast<int>(current)];
	LockSunElevation(dirs, time);

	if (current == Caster::None) {
		fadePhase = Phase::None;
		return SetLighting(sun, { 0.0f, 0.0f, 1.0f }, 0.0f);
	}

	const auto& dir = dirs[static_cast<int>(current)];
	const auto intensity = intensities[static_cast<int>(current)];
	const auto targetDir = target == Caster::None ? RE::NiPoint3{ 0.0f, 0.0f, 1.0f } : dirs[static_cast<int>(target)];

	if (fadePhase == Phase::None) {
		return SetLighting(sun, dir, intensity);
	}

	fadeTimer = std::min(fadeTimer + fadeAdvance, fadeDuration);

	const float t = fadeTimer / fadeDuration;
	const float fade = fadePhase == Phase::FadeIn ? t : 1.0f - t;
	const float vlFactor = target == Caster::None ? 1.0f : ComputeVLFactor(dir, targetDir);
	const float lightingIntensity = SetLighting(sun, dir, intensity * fade * vlFactor);

	if (fadePhase == Phase::FadeOut) {
		if (t >= 1.0f || intensity <= 0.0f) {
			current = target;
			fadePhase = Phase::FadeIn;
			fadeTimer = 0.0f;
		}
	} else if (fadePhase == Phase::FadeIn) {
		if (t >= 1.0f)
			fadePhase = Phase::None;
	}

	return lightingIntensity;
}

void SkySync::ShadowFader::LockSunElevation(RE::NiPoint3 dirs[3], const float time)
{
	// Keep the visual sun on its apparent path; only latch the shadow/VL caster near the horizon.
	const auto& skySync = globals::features::skySync;
	const auto& climateTimings = skySync.timings;
	const int sunIdx = static_cast<int>(Caster::Sun);
	const float minElev = DirectX::XMConvertToRadians(skySync.settings.MinShadowElevation);
	const float sunriseMiddle = (climateTimings.sunriseBegin + climateTimings.sunriseEnd) * 0.5f;
	const float sunsetMiddle = (climateTimings.sunsetBegin + climateTimings.sunsetEnd) * 0.5f;
	const bool sunRising = time >= climateTimings.sunriseBegin && time < sunriseMiddle;
	const bool sunSetting = time >= sunsetMiddle && time < climateTimings.sunsetEnd;

	if (sunSetting) {
		const float range = sunsetMiddle < climateTimings.sunsetEnd ? climateTimings.sunsetEnd - sunsetMiddle : 0.0f;
		const float t = range > FLT_EPSILON ? std::clamp((time - sunsetMiddle) / range, 0.0f, 1.0f) : 1.0f;
		const float dim = std::sqrt(std::max(0.0f, 1.0f - t));
		if (dim <= SunsetHeadingLockThreshold) {
			if (!sunsetHeadingLocked) {
				frozenHeading = std::atan2(dirs[sunIdx].y, dirs[sunIdx].x);
				sunsetHeadingLocked = true;
			}
			SetDirection(dirs[sunIdx], frozenHeading, minElev);
		} else {
			SetElevation(dirs[sunIdx], minElev);
		}
	} else if (sunRising) {
		if (!sunriseReleased) {
			if (DirectX::XMScalarASinEst(dirs[sunIdx].z) >= minElev)
				sunriseReleased = true;
			else
				SetElevation(dirs[sunIdx], minElev);
		}
	} else {
		sunriseReleased = false;
		sunsetHeadingLocked = false;
	}
}

float SkySync::ShadowFader::SetLighting(const RE::Sun* sun, RE::NiPoint3 dir, float intensity)
{
	ClampDirection(dir);

	RE::NiMatrix3& m = sun->light->local.rotate;
	m.entry[0][0] = -dir.x;
	m.entry[1][0] = -dir.y;
	m.entry[2][0] = -dir.z;

	RE::NiUpdateData updateData;
	sun->light->Update(updateData);

	intensity = SkySync::NormalizeVolumetricLightingIntensity(intensity);
	return intensity;
}

inline void SkySync::ShadowFader::SetDirection(RE::NiPoint3& dir, const float headingRadians, const float elevRadians)
{
	float sinElev, cosElev, sinHeading, cosHeading;
	DirectX::XMScalarSinCosEst(&sinElev, &cosElev, elevRadians);
	DirectX::XMScalarSinCosEst(&sinHeading, &cosHeading, headingRadians);

	dir.x = cosElev * cosHeading;
	dir.y = cosElev * sinHeading;
	dir.z = sinElev;
}

inline void SkySync::ShadowFader::SetElevation(RE::NiPoint3& dir, const float elevRadians)
{
	SetDirection(dir, std::atan2(dir.y, dir.x), elevRadians);
}

float SkySync::ShadowFader::ComputeVLFactor(const RE::NiPoint3& current, const RE::NiPoint3& target)
{
	const float dot = std::clamp(current.Dot(target), -1.0f, 1.0f);
	const float angle = DirectX::XMConvertToDegrees(DirectX::XMScalarACosEst(dot));
	return std::clamp((VLFadeEndAngle - angle) / (VLFadeEndAngle - VLFadeStartAngle), 0.0f, 1.0f);
}

inline void SkySync::ShadowFader::ClampDirection(RE::NiPoint3& dir)
{
	const float minDegrees = globals::features::skySync.settings.MinShadowElevation;
	const float minElev = DirectX::XMConvertToRadians(minDegrees);
	const float elev = DirectX::XMScalarASinEst(dir.z);
	if (elev >= minElev)
		return;

	SetElevation(dir, minElev);
}

bool SkySync::ClimateTimings::Update(const RE::TESClimate* climate)
{
	const auto& timing = climate->timing;
	// Invalid weather intervals cannot safely define a colour or caster handoff.
	if (timing.sunrise.begin > timing.sunrise.end || timing.sunrise.end > timing.sunset.begin ||
		timing.sunset.begin > timing.sunset.end || timing.sunset.end > HoursPerDay * 6.0f)
		return false;
	weatherSunriseBegin = timing.sunrise.begin / 6.0f;
	weatherSunriseMiddle = (timing.sunrise.begin + timing.sunrise.end) / 12.0f;
	weatherSunsetMiddle = (timing.sunset.begin + timing.sunset.end) / 12.0f;
	weatherSunsetEnd = timing.sunset.end / 6.0f;

	const float SunriseBeginOffset = globals::features::skySync.settings.SunriseBeginOffset;
	const float SunriseEndOffset = globals::features::skySync.settings.SunriseEndOffset;
	const float SunsetBeginOffset = globals::features::skySync.settings.SunsetBeginOffset;
	const float SunsetEndOffset = globals::features::skySync.settings.SunsetEndOffset;

	sunriseBegin = (climate->timing.sunrise.begin / 6.0f) + SunriseBeginOffset;
	sunriseEnd = (climate->timing.sunrise.end / 6.0f) + SunriseEndOffset;
	sunsetBegin = (climate->timing.sunset.begin / 6.0f) + SunsetBeginOffset;
	sunsetEnd = (climate->timing.sunset.end / 6.0f) + SunsetEndOffset;
	// Basic ordering guarantees (prevents divide-by-zero / negative duration paths).
	constexpr float kMinGapHours = 0.1f;
	if (sunriseEnd <= sunriseBegin)
		sunriseEnd = sunriseBegin + kMinGapHours;
	if (sunsetEnd <= sunsetBegin)
		sunsetEnd = sunsetBegin + kMinGapHours;
	if (sunsetBegin <= sunriseEnd)
		sunsetBegin = sunriseEnd + kMinGapHours;
	if (sunsetEnd <= sunsetBegin)
		sunsetEnd = sunsetBegin + kMinGapHours;
	sunrise = (sunriseBegin + sunriseEnd) * 0.5f - 0.25f;
	sunset = (sunsetBegin + sunsetEnd) * 0.5f + 0.25f;
	return true;
}

void SkySync::Moon_Update::thunk(RE::Moon* moon, RE::Sky* sky)
{
	if (!moon || !sky) {
		func(moon, sky);
		return;
	}
	const auto updateMoonTexture = moon->updateMoonTexture;

	func(moon, sky);

	if (auto& singleton = globals::features::skySync; singleton.loaded && singleton.settings.Enabled && moon->moonMesh && updateMoonTexture != moon->updateMoonTexture) {
		// Gets the texture name of the current moon phase when it changes rather than reading direct global variables
		// Allows for compatability with other mods that don't directly update the in-game phase values
		const auto moonShaderProperty = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().shaderProperty.get());

		if (!moonShaderProperty || !moonShaderProperty->GetBaseTexture())
			return;
		const auto name = moonShaderProperty->GetBaseTexture()->name.c_str();
		if (!name)
			return;
		const size_t len = std::strlen(name);
		std::string lower;
		lower.reserve(len);
		for (size_t i = 0; i < len; ++i) {
			lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(name[i]))));
		}

		static constexpr std::array<std::pair<std::string_view, RE::Moon::Phases::Phase>, 8> Lookup{
			{ { "full", RE::Moon::Phases::Phase::kFull },
				{ "three_wan", RE::Moon::Phases::Phase::kWaningGibbous },
				{ "half_wan", RE::Moon::Phases::Phase::kWaningQuarter },
				{ "one_wan", RE::Moon::Phases::Phase::kWaningCrescent },
				{ "new", RE::Moon::Phases::Phase::kNewMoon },
				{ "one_wax", RE::Moon::Phases::Phase::kWaxingCrescent },
				{ "half_wax", RE::Moon::Phases::Phase::kWaxingQuarter },
				{ "three_wax", RE::Moon::Phases::Phase::kWaxingGibbous } }
		};

		RE::Moon::Phases::Phase phase = RE::Moon::Phases::Phase::kFull;
		for (auto& [suffix, id] : Lookup) {
			if (lower.find(suffix) != std::string::npos) {
				phase = id;
				break;
			}
		}

		if (moon == sky->masser)
			singleton.masserPhase = phase;
		else if (moon == sky->secunda)
			singleton.secundaPhase = phase;
	}
}

inline float SkySync::SmoothStep(const float start, const float end, const float x)
{
	const float t = std::clamp((x - start) / (end - start), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

void SkySync::DrawEssentialSettings()
{
	ImGui::Checkbox(T("feature.sky_sync.enable", "Enable"), &settings.Enabled);
}

void SkySync::PreparePendingTransitions()
{
	const auto request = Util::ConsumeCelestialTransitionRequest();
	if (!request.timeJump && !request.gameLoad)
		return;

	if (request.gameLoad) {
		shadowFader.Reset();
		currentCell = nullptr;
		currentCellInterior = false;
		currentCellWorldspace = nullptr;
		currentSkyRotation = D3D11_FLOAT32_MAX;
	}

	immediateTransitionReady = true;
}

float SkySync::GetMoonIntensity(RE::Moon::Phases::Phase a_phase) const
{
	if (a_phase == RE::Moon::Phases::Phase::kNewMoon)
		return settings.NewMoonIntensity;
	const float t = std::clamp((std::abs(static_cast<float>(a_phase) - static_cast<float>(RE::Moon::Phases::Phase::kNewMoon)) - 1.0f) / 3.0f, 0.0f, 1.0f);
	return std::lerp(settings.CrescentMoonIntensity, settings.FullMoonIntensity, t);
}
