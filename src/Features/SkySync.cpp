#include "SkySync.h"

#include <algorithm>
#include <cmath>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SkySync::Settings,
	Enabled,
	UseAlternateSunPath,
	MoonLightSource,
	SunPath,
	CustomAngle,
	SunriseBeginOffset,
	SunriseEndOffset,
	SunsetBeginOffset,
	SunsetEndOffset,
	MinShadowElevation)

void SkySync::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Enable or disable Sky Sync features.");
	}

	ImGui::Checkbox("Use alternate sun path", &settings.UseAlternateSunPath);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Calculate sun position based on time of day and season instead of vanilla movement.");
	}

	if (settings.UseAlternateSunPath) {
		if (ImGui::SliderInt("Sun path", &settings.SunPath, 0, static_cast<uint8_t>(SunPath::Count) - 1, SunPathNames[settings.SunPath], ImGuiSliderFlags_AlwaysClamp))
			SetSunAngle();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Choose the trajectory the sun takes across the sky.");
		}

		if (settings.SunPath == static_cast<int32_t>(SunPath::Custom)) {
			if (ImGui::SliderFloat("Custom angle", &settings.CustomAngle, -90.0f, 90.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
				SetSunAngle();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Set a custom angle for the sun's trajectory.");
			}
		}
	}

	ImGui::SliderInt("Moon light source", &settings.MoonLightSource, 0, static_cast<uint8_t>(MoonLightSource::Count) - 1, MoonLightSourceNames[settings.MoonLightSource], ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Select which moon casts shadows during the night.");
	}

	ImGui::SliderFloat("Min Shadow Elevation", &settings.MinShadowElevation, 0.0f, 45.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("The minimum angle sunlight will set to. Caps shadow length. Higher = shorter shadows at sunset/sunrise.");
	}
	ImGui::Spacing();
	ImGui::Spacing();
	if (ImGui::TreeNodeEx("Sun Position Offsets", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("Moves sun height during sunrise/sunset. Reset weather to see changes.");
		ImGui::SliderFloat("Sunrise Begin (Hours)", &settings.SunriseBeginOffset, -5.0f, 5.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Offset for when the sun starts rising.");
		}
		ImGui::SliderFloat("Sunrise End (Hours)", &settings.SunriseEndOffset, -5.0f, 5.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Offset for when the sun finishes rising.");
		}
		ImGui::SliderFloat("Sunset Begin (Hours)", &settings.SunsetBeginOffset, -5.0f, 5.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Offset for when the sun starts setting.");
		}
		ImGui::SliderFloat("Sunset End (Hours)", &settings.SunsetEndOffset, -5.0f, 5.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Offset for when the sun finishes setting.");
		}
		ImGui::TreePop();
	}
}

void SkySync::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.MoonLightSource = std::clamp(settings.MoonLightSource, static_cast<int32_t>(MoonLightSource::Brightest), static_cast<int32_t>(MoonLightSource::Secunda));
	settings.SunPath = std::clamp(settings.SunPath, static_cast<int32_t>(SunPath::Southern), static_cast<int32_t>(SunPath::Custom));
	settings.CustomAngle = std::clamp(settings.CustomAngle, -90.0f, 90.0f);
	settings.SunriseBeginOffset = std::clamp(settings.SunriseBeginOffset, -5.0f, 5.0f);
	settings.SunriseEndOffset = std::clamp(settings.SunriseEndOffset, -5.0f, 5.0f);
	settings.SunsetBeginOffset = std::clamp(settings.SunsetBeginOffset, -5.0f, 5.0f);
	settings.SunsetEndOffset = std::clamp(settings.SunsetEndOffset, -5.0f, 5.0f);
	settings.MinShadowElevation = std::clamp(settings.MinShadowElevation, 0.0f, 45.0f);
	if (!settings.Enabled) {
		ResetRuntimeState();
	}
	SetSunAngle();
}

void SkySync::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SkySync::RestoreDefaultSettings()
{
	settings = {};
	ResetRuntimeState();
	SetSunAngle();
}

float SkySync::GetVolumetricLightingIntensityFactor() const
{
	if (!loaded || !settings.Enabled)
		return DefaultVolumetricLightingIntensityFactor;

	return SkySync::NormalizeVolumetricLightingIntensity(volumetricLightingIntensityFactor);
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

	stl::detour_thunk<Moon_Update>(REL::RelocationID(25626, 26169));
	stl::detour_thunk<Sky_Update>(REL::RelocationID(25682, 26229));
	stl::detour_thunk<Sky_OnNewClimate>(REL::RelocationID(25695, 26242));

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
}

void SkySync::DisableOnConflict(std::string_view conflictName)
{
	failedLoadedMessage = fmt::format("Disabled as {} has been detected, both cannot be used together", conflictName);
	loaded = false;
	settings.Enabled = false;
	ResetRuntimeState();
	logger::warn("[Sky Sync] {}", failedLoadedMessage);
}

void SkySync::ResetRuntimeState()
{
	shadowFader.Reset();
	volumetricLightingIntensityFactor = DefaultVolumetricLightingIntensityFactor;
}

float SkySync::NormalizeVolumetricLightingIntensity(float intensity)
{
	if (!std::isfinite(intensity))
		intensity = DefaultVolumetricLightingIntensityFactor;
	return std::clamp(intensity, 0.0f, 1.0f);
}

void SkySync::Sky_Update::thunk(RE::Sky* sky)
{
	func(sky);
	globals::features::skySync.Update(sky);
}

void SkySync::Update(const RE::Sky* sky)
{
	if (!settings.Enabled) {
		ResetRuntimeState();
		return;
	}

	if (!sky) {
		ResetRuntimeState();
		return;
	}

	const auto sun = sky->sun;
	const auto climate = sky->currentClimate;
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!sun || !sun->light || !sun->root || !sun->sunBase || !sun->sunGlareNode || !sky->root || !climate || !player || !gSunPosition || !gSunGlareSize || !gMasserSize || !gSecundaSize) {
		ResetRuntimeState();
		return;
	}

	const auto cell = player->GetParentCell();

	if (cell != currentCell) {
		const auto prevCell = currentCell;
		if (cell)
			SetSkyRotation(sky, cell);
		if (cell && prevCell && (cell->IsInteriorCell() != prevCell->IsInteriorCell() || cell->GetRuntimeData().worldSpace != prevCell->GetRuntimeData().worldSpace))
			shadowFader.Reset();
	}

	// Exterior worldspaces always run; interior cells require the sunlight-shadows flag.
	if (cell && cell->IsInteriorCell() && !cell->cellFlags.all(static_cast<RE::TESObjectCELL::Flag>(CellFlagExt::kSunlightShadows))) {
		ResetRuntimeState();
		return;
	}

	const float time = sky->currentGameHour;
	const bool isDayTime = time > timings.sunriseFadeOutMoonEnd && time < timings.sunsetFadeInMoonStart;

	const auto worldSpace = player->GetWorldspace();
	const float altitude = worldSpace ? player->GetPositionZ() - worldSpace->GetDefaultWaterHeight() : 0.0f;

	ProcessSun(sun, time, altitude, isDayTime);
	ProcessMoon(sky->masser, time, Caster::Masser, altitude, isDayTime);
	ProcessMoon(sky->secunda, time, Caster::Secunda, altitude, isDayTime);

	volumetricLightingIntensityFactor = shadowFader.Update(sun, directions, intensities, isDayTime, time);
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

void SkySync::ProcessMoon(const RE::Moon* moon, const float time, const Caster type, const float altitude, const bool isDayTime)
{
	intensities[static_cast<int>(type)] = 0.0f;
	directions[static_cast<int>(type)] = { 0.0f, 0.0f, 1.0f };
	rawDirections[static_cast<int>(type)] = { 0.0f, 0.0f, -1.0f };

	if (!moon)
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
		intensity *= masserPhaseIntensityFactor;
	else if (type == Caster::Secunda)
		intensity *= secundaPhaseIntensityFactor * SecundaIntensityFactor;

	if (time >= timings.sunriseFadeOutMoonStart && time <= timings.sunriseFadeOutMoonEnd)
		intensity *= SmoothStep(timings.sunriseFadeOutMoonEnd, timings.sunriseFadeOutMoonStart, time);
	else if (time >= timings.sunsetFadeInMoonStart && time <= timings.sunsetFadeInMoonEnd)
		intensity *= SmoothStep(timings.sunsetFadeInMoonStart, timings.sunsetFadeInMoonEnd, time);

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
}

float SkySync::ShadowFader::Update(const RE::Sun* sun, RE::NiPoint3 dirs[3], float intensities[3], const bool isDayTime, const float time)
{
	const float masserIntensity = intensities[static_cast<int>(Caster::Masser)];
	const float secundaIntensity = intensities[static_cast<int>(Caster::Secunda)];

	auto desired = Caster::None;
	if (isDayTime)
		desired = Caster::Sun;
	else if (masserIntensity > 0.0f && masserIntensity >= secundaIntensity)
		desired = Caster::Masser;
	else if (secundaIntensity > 0.0f)
		desired = Caster::Secunda;

	LockSunElevation(dirs, time);

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
	if (const auto calendar = globals::game::calendar) {
		const float currentHoursPassed = calendar->GetHoursPassed();
		const float timeScale = calendar->GetTimescale();
		const bool validCurrentHours = std::isfinite(currentHoursPassed);
		const bool validPreviousHours = std::isfinite(previousHoursPassed);
		const float hoursPassedDiff = validCurrentHours && validPreviousHours ? std::abs(currentHoursPassed - previousHoursPassed) : FadeTime / SecondsPerGameHour;
		if (validCurrentHours)
			previousHoursPassed = currentHoursPassed;
		if (timeScale <= 0.0f || !validCurrentHours || !validPreviousHours || hoursPassedDiff >= 0.01f) {
			fadePhase = Phase::None;
			current = target;
		} else {
			fadeAdvance = hoursPassedDiff * SecondsPerGameHour;
		}
	} else if (globals::game::deltaTime) {
		fadeAdvance = *globals::game::deltaTime * 20.0f;
	}

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

	fadeTimer = std::min(fadeTimer + fadeAdvance, FadeTime);

	const float t = fadeTimer / FadeTime;
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
	const auto& timings = skySync.timings;
	const int sunIdx = static_cast<int>(Caster::Sun);
	const float minElev = DirectX::XMConvertToRadians(skySync.settings.MinShadowElevation);
	const float sunriseMiddle = (timings.sunriseBegin + timings.sunriseEnd) * 0.5f;
	const float sunsetMiddle = (timings.sunsetBegin + timings.sunsetEnd) * 0.5f;
	const bool sunRising = time >= timings.sunriseBegin && time < sunriseMiddle;
	const bool sunSetting = time >= sunsetMiddle && time < timings.sunsetEnd;

	if (sunSetting) {
		const float range = sunsetMiddle < timings.sunsetEnd ? timings.sunsetEnd - sunsetMiddle : 0.0f;
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

void SkySync::ClimateTimings::Update(const RE::TESClimate* climate)
{
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
	sunriseFadeOutMoonStart = sunriseBegin - 0.5f;
	sunriseFadeOutMoonEnd = sunriseBegin + 1.0f;
	sunsetFadeInMoonStart = sunsetEnd - 1.0f;
	sunsetFadeInMoonEnd = sunsetEnd + 0.5f;
}

void SkySync::Sky_OnNewClimate::thunk(RE::Sky* sky)
{
	if (auto& singleton = globals::features::skySync; singleton.settings.Enabled && sky && sky->currentClimate)
		singleton.timings.Update(sky->currentClimate);
	func(sky);
}

void SkySync::Moon_Update::thunk(RE::Moon* moon, RE::Sky* sky)
{
	const auto updateMoonTexture = moon->updateMoonTexture;

	func(moon, sky);

	if (auto& singleton = globals::features::skySync; singleton.settings.Enabled && updateMoonTexture != moon->updateMoonTexture) {
		// Gets the texture name of the current moon phase when it changes rather than reading direct global variables
		// Allows for compatability with other mods that don't directly update the in-game phase values
		const auto moonShaderProperty = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().shaderProperty.get());

		const auto name = moonShaderProperty->GetBaseTexture()->name.c_str();
		const size_t len = std::strlen(name);
		std::string lower;
		lower.reserve(len);
		for (size_t i = 0; i < len; ++i) {
			lower.push_back(static_cast<char>(std::tolower(name[i])));
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

		float* intensityFactor = moon == sky->masser ? &singleton.masserPhaseIntensityFactor : &singleton.secundaPhaseIntensityFactor;
		if (phase == RE::Moon::Phases::Phase::kNewMoon) {
			*intensityFactor = NewMoonIntensityFactor;
		} else {
			const float t = (abs(static_cast<float>(phase) - static_cast<float>(RE::Moon::Phases::Phase::kNewMoon)) - 1.0f) / 3.0f;
			*intensityFactor = std::lerp(CrescentMoonIntensityFactor, FullMoonIntensityFactor, t);
		}
	}
}

inline float SkySync::SmoothStep(const float start, const float end, const float x)
{
	const float t = std::clamp((x - start) / (end - start), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
