#include "SnowCover.h"

#include "GpuPass.h"
#include "Profiler.h"
#include "SnowCover/Config.h"
#include "SnowCover/Environment.h"
#include "SnowCoverMath.h"
#include "Utils/D3D.h"
#include "Utils/DdsResource.h"
#include "Utils/FileSystem.h"
#include "Utils/Finite.h"
#include "Utils/Serialize.h"
#include "Utils/StringUtils.h"
#include "Wetterness/SnowMelt.h"
#include <DirectXTex.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowCover::UserSettings,
	Enable,
	EnableExpensiveFoliage,
	AffectHavok,
	AffectFloraTint,
	SnowHeightOffset,
	RooftopSnow,
	RooftopHeight,
	RooftopExposure,
	EnableGlints,
	EnableRooftops,
	EnableFoliageTint,
	EnableBoundsCulling)

namespace
{
	constexpr uint32_t PassMask = static_cast<uint32_t>(State::ExtraShaderDescriptors::NoSnow) | static_cast<uint32_t>(State::ExtraShaderDescriptors::NoFoliageTint) |
	                              static_cast<uint32_t>(State::ExtraShaderDescriptors::SnowRooftop) | static_cast<uint32_t>(State::ExtraShaderDescriptors::SnowLODTree) |
	                              static_cast<uint32_t>(State::ExtraShaderDescriptors::SnowInactive);
	constexpr float MaxFrameSeconds = 0.1f;
	constexpr float MinAngleRange = 0.001f;
	constexpr float SnowBoundsPadding = 512.0f;
	constexpr float MapFilterPadding = 2.0f / 255.0f;
	constexpr float DisplacementInfluence = 5.0f;
	constexpr float FoliageTransitionWidth = 5000.0f;
	constexpr float SurvivalSnowOn = 0.5f;
	constexpr float SurvivalSnowOff = 0.4f;
	constexpr RE::FormID SurvivalRegionSpell = 0x87F;

	std::filesystem::path GetConfigPath(std::string_view name)
	{
		if (!Util::IsSinglePathComponent(name))
			throw std::invalid_argument("Invalid worldspace editor ID");
		return (Util::PathHelpers::GetShadersPath() / "SnowCover") / (std::string(name) + ".json");
	}
}

SnowCover::PerFrame SnowCover::GetCommonBufferData(bool inWorld)
{
	if (!loaded || !active.load(std::memory_order_relaxed) || (!inWorld && !globals::state->activeReflections) || globals::state->IsMainOrLoadingMenuOpen())
		return {};
	// Resolve resource changes before uploading the matching enabled state.
	if (reloadRequested.exchange(false)) {
		lastWorldspace.clear();
		LoadGeometryLists();
		frame = {};
	}
	if (refreshTextures.exchange(false)) {
		if (!lastWorldspace.empty() && SnowEnvironment::GetCurrentWorldspaceOrCellEditorID() == lastWorldspace) {
			ReleaseTextures();
			TryLoadTextures();
		}
		frame = {};
	}
	if (frame.IsNewFrame())
		UpdateFrame();
	return perFrame;
}

void SnowCover::UpdateFrame()
{
	if (!active.load(std::memory_order_relaxed) || globals::state->IsMainOrLoadingMenuOpen())
		return;
#ifdef TRACY_SUPPORT
	ZoneScopedN("SnowCover::UpdateFrame");
#endif
	CS_PROFILE_CPU_SCOPE("SnowCover::UpdateFrameCPU");
	Reload();
	bool snowing = false;
	bool raining = false;
	snowingDensity = 0.0f;
	if (wsettings.EnableSnowCover && texturesLoaded && !Util::IsInterior()) {
		if (auto* sky = globals::game::sky) {
			if (auto* weather = sky->currentWeather) {
				snowing = weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow);
				raining = !snowing && weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
				snowingDensity = SnowEnvironment::GetPrecipitationIntensity(weather);
			}
		}
	}
	const float snowRate = snowing ? snowingSpeed : (raining ? -2.0f * meltingSpeed : (timeSnowing > 0.0f ? -meltingSpeed : meltingSpeed));
	if (auto* calendar = globals::game::calendar) {
		const double hours = static_cast<double>(calendar->GetCurrentGameTime()) * 24.0;
		const float elapsed = std::isfinite(hours) && lastGameHours >= 0.0 ? static_cast<float>(std::max(0.0, hours - lastGameHours)) : 0.0f;
		if (std::isfinite(hours) && hours < lastGameHours)
			timeSnowing = 0.0f;
		lastGameHours = std::isfinite(hours) ? hours : -1.0;
		timeSnowing = SnowCoverMath::Accumulate(timeSnowing, elapsed, snowing, raining, snowingSpeed, meltingSpeed);
		perFrame.Month = SnowEnvironment::GetFractionalGameMonth(*calendar);
	}
	perFrame.SeasonalAltitude = SnowCoverMath::SeasonalAltitude(perFrame.Month, MaxSummerMonth, MaxWinterMonth, SummerHeightOffset, WinterHeightOffset);
	perFrame.TimeSnowing = timeSnowing;
	perFrame.SnowingDensity = snowingDensity;
	perFrame.settings = settings;
	if (!settings.EnableRooftops)
		perFrame.settings.RooftopSnow = 0.0f;
	perFrame.wsettings = wsettings;
	if (!settings.EnableFoliageTint) {
		perFrame.wsettings.AffectGrassTint = 0;
		perFrame.wsettings.AffectTreeTint = 0;
	}
	perFrame.wsettings.EnableSnowCover = wsettings.EnableSnowCover && texturesLoaded;
	UpdateFireMelt();
	WetternessSnowMelt::Update(*this, snowRate);
	UpdateSurvival();
}

void SnowCover::SetupResources()
{
	resourcesReady = true;
	if (!active.load(std::memory_order_relaxed))
		return;
	lastWorldspace.clear();
	LoadGeometryLists();
	UpdateFrame();
}

void SnowCover::Reset()
{
	const bool requested = settings.Enable != 0;
	const bool loading = globals::state->IsMainOrLoadingMenuOpen();
	if (loading && !wasLoading) {
		// Travel unloads references, but its elapsed game time still belongs to this timeline.
		trackedFires.clear();
		fireBaseCache.clear();
		buildingBaseCache.clear();
		fireFrame = {};
		fireScanTimer = FIRE_MELT_SCAN_INTERVAL;
		fireMeltSnap = true;
	}
	wasLoading = loading;
	if (active.load(std::memory_order_relaxed) == requested)
		return;
	active.store(requested, std::memory_order_relaxed);
	ResetRuntimeState();
	if (requested) {
		LoadGeometryLists();
		if (!lastWorldspace.empty() && SnowEnvironment::GetCurrentWorldspaceOrCellEditorID() == lastWorldspace)
			TryLoadTextures();
	}
}

void SnowCover::ResetRuntimeStateAfterGameLoad()
{
	lastWorldspace.clear();
	ResetRuntimeState();
}

void SnowCover::ResetRuntimeState()
{
	WetternessSnowMelt::Reset();
	lastGameHours = -1.0;
	timeSnowing = 0.0f;
	perFrame = {};
	frame = {};
	fireFrame = {};
	trackedFires.clear();
	fireBaseCache.clear();
	buildingBaseCache.clear();
	survivalSnowy.store(false, std::memory_order_relaxed);
	survivalSnowAmount = 0.0f;
	PublishSurvival(false);
	ReleaseTextures();
	fireScanTimer = FIRE_MELT_SCAN_INTERVAL;
	fireMeltSnap = true;
}

void SnowCover::LoadGeometryLists()
{
	try {
		std::vector<std::string> names;
		auto load = [&](std::string_view name) {
			std::ifstream file((Util::PathHelpers::GetShadersPath() / "SnowCover") / name);
			if (!file)
				throw std::runtime_error(std::format("Could not open {}", name));
			Util::ReadConfigLines(file, names);
		};
		load("whitelist.txt");
		const size_t whiteCount = names.size();
		load("blacklist.txt");
		std::unordered_set<std::string_view> newWhitelist, newBlacklist;
		for (size_t i = 0; i < names.size(); ++i)
			(i < whiteCount ? newWhitelist : newBlacklist).insert(names[i]);
		geometryNames.swap(names);
		whitelist.swap(newWhitelist);
		blacklist.swap(newBlacklist);
	} catch (const std::exception& error) {
		logger::warn("Snow Cover: retaining geometry filters after read failure: {}", error.what());
	}
}

void SnowCover::ResetWorldConfig()
{
	wsettings = WorldSettings{};
	MaxSummerMonth = DEFAULT_PEAK_SUMMER_MONTH;
	MaxWinterMonth = DEFAULT_PEAK_WINTER_MONTH;
	SummerHeightOffset = DEFAULT_SUMMER_HEIGHT_OFFSET;
	WinterHeightOffset = DEFAULT_WINTER_HEIGHT_OFFSET;
	snowingSpeed = 1.0f;
	meltingSpeed = 1.0f;
	mapMin = DEFAULT_MAP_MIN;
	mapMax = DEFAULT_MAP_MAX;
	UpdateMapTransform();
	mapTexture.clear();
	mainTexture.clear();
	alternateTexture.clear();
	mapTextureInput[0] = '\0';
	mainTextureInput[0] = '\0';
	alternateTextureInput[0] = '\0';
}

void SnowCover::UpdateMapTransform()
{
	if (!std::isfinite(mapMin.x) || !std::isfinite(mapMin.y) || std::abs(mapMin.x) > 1e7f || std::abs(mapMin.y) > 1e7f)
		mapMin = DEFAULT_MAP_MIN;
	if (!std::isfinite(mapMax.x) || !std::isfinite(mapMax.y) || std::abs(mapMax.x) > 1e7f || std::abs(mapMax.y) > 1e7f)
		mapMax = DEFAULT_MAP_MAX;
	if (std::abs(mapMax.x - mapMin.x) < 1.0f) {
		mapMin.x = DEFAULT_MAP_MIN.x;
		mapMax.x = DEFAULT_MAP_MAX.x;
	}
	if (std::abs(mapMax.y - mapMin.y) < 1.0f) {
		mapMin.y = DEFAULT_MAP_MIN.y;
		mapMax.y = DEFAULT_MAP_MAX.y;
	}
	const float2 extent = mapMax - mapMin;
	wsettings.mapScale = float2(1.0) / extent;
	wsettings.mapOffset = -mapMin * wsettings.mapScale;
}

json SnowCover::GetWorldConfig() const
{
	return {
		{ "EnableSnowCover", wsettings.EnableSnowCover != 0 },
		{ "AffectGrassTint", wsettings.AffectGrassTint },
		{ "AffectTreeTint", wsettings.AffectTreeTint },
		{ "FoliageHeightOffset", wsettings.FoliageHeightOffset },
		{ "UVScale", wsettings.UVScale },
		{ "MaxSummerMonth", MaxSummerMonth },
		{ "MaxWinterMonth", MaxWinterMonth },
		{ "SummerHeightOffset", SummerHeightOffset },
		{ "WinterHeightOffset", WinterHeightOffset },
		{ "MapTexture", mapTexture.generic_string() },
		{ "MapZscale", wsettings.mapZscale },
		{ "BlendSmoothness", wsettings.BlendSmoothness },
		{ "WeatherFadeStart", wsettings.WeatherFadeStart },
		{ "WeatherFadeEnd", wsettings.WeatherFadeEnd },
		{ "ObjectFadeStart", wsettings.ObjectFadeStart },
		{ "ObjectFadeEnd", wsettings.ObjectFadeEnd },
		{ "ObjectFadeAmount", wsettings.ObjectFadeAmount },
		{ "ScreenSpaceScale", wsettings.ScreenSpaceScale },
		{ "LogMicrofacetDensity", wsettings.LogMicrofacetDensity },
		{ "MicrofacetRoughness", wsettings.MicrofacetRoughness },
		{ "DensityRandomization", wsettings.DensityRandomization },
		{ "MapMin", mapMin },
		{ "MapMax", mapMax },
		{ "MainTexture", mainTexture.generic_string() },
		{ "AltTexture", alternateTexture.generic_string() },
		{ "MainTint", wsettings.MainTint },
		{ "AltTint", wsettings.AltTint },
		{ "SnowingSpeed", snowingSpeed },
		{ "MeltingSpeed", meltingSpeed },
		{ "PeakMainAngle", wsettings.PeakMainAngle },
		{ "PeakAltAngle", wsettings.PeakAltAngle },
		{ "MinAngle", wsettings.MinAngle },
		{ "MaxAngle", wsettings.MaxAngle },
		{ "MainSpec", wsettings.MainSpec },
		{ "AltSpec", wsettings.AltSpec },
	};
}

void SnowCover::SaveConfig(bool applyTextureInputs)
{
	ValidateSettings();
	if (lastWorldspace.empty() || SnowEnvironment::GetCurrentWorldspaceOrCellEditorID() != lastWorldspace || globals::state->IsMainOrLoadingMenuOpen()) {
		status = "Wait for the current worldspace config to load before saving.";
		return;
	}
	std::filesystem::path path;
	try {
		auto config = GetWorldConfig();
		if (applyTextureInputs) {
			config["MapTexture"] = mapTextureInput;
			config["MainTexture"] = mainTextureInput;
			config["AltTexture"] = alternateTextureInput;
		}
		SnowConfig::ValidateWorld(config);
		path = GetConfigPath(lastWorldspace);
		std::filesystem::create_directories(path.parent_path());
		std::string writeError;
		if (!Util::FileHelpers::WriteTextFileAtomic(path, config.dump(4), writeError, false)) {
			status = std::string("Could not save the worldspace config. See the log for details.");
			logger::error("[Snow Cover] Failed to write {}: {}", path.generic_string(), writeError);
			return;
		}
		if (LoadWorldConfig(config) || (wsettings.EnableSnowCover && !texturesLoaded))
			refreshTextures.store(true);
		frame = {};
	} catch (const std::exception& e) {
		status = std::string("Could not save the worldspace config. See the log for details.");
		logger::error("[Snow Cover] Error saving {}: {}", path.generic_string(), e.what());
		return;
	}
	status = std::string("Worldspace config saved.");
}

bool SnowCover::LoadWorldConfig(const json& input, bool worldTransition)
{
	SnowConfig::Validate(input, SnowConfig::Schemas().at("world"), "snowWorld");
	auto config = GetWorldConfig();
	config.update(input);
	config["EnableSnowCover"] = input.value("EnableSnowCover", true);
	SnowConfig::ValidateWorld(config);
	if (!worldTransition)
		WetternessSnowMelt::Reset();
	wsettings.EnableSnowCover = config.value("EnableSnowCover", true);
	wsettings.AffectGrassTint = config.at("AffectGrassTint");
	wsettings.AffectTreeTint = config.at("AffectTreeTint");
	wsettings.FoliageHeightOffset = config.at("FoliageHeightOffset");
	wsettings.UVScale = config.at("UVScale");
	MaxSummerMonth = config.at("MaxSummerMonth");
	MaxWinterMonth = config.at("MaxWinterMonth");
	SummerHeightOffset = config.at("SummerHeightOffset");
	WinterHeightOffset = config.at("WinterHeightOffset");
	config.at("MapMin").get_to(mapMin);
	config.at("MapMax").get_to(mapMax);
	UpdateMapTransform();
	wsettings.mapZscale = config.at("MapZscale");
	wsettings.BlendSmoothness = config.at("BlendSmoothness");
	wsettings.WeatherFadeStart = config.value("WeatherFadeStart", DEFAULT_WEATHER_FADE_START);
	wsettings.WeatherFadeEnd = config.value("WeatherFadeEnd", DEFAULT_WEATHER_FADE_END);
	wsettings.ObjectFadeStart = config.value("ObjectFadeStart", DEFAULT_OBJECT_FADE_START);
	wsettings.ObjectFadeEnd = config.value("ObjectFadeEnd", DEFAULT_OBJECT_FADE_END);
	wsettings.ObjectFadeAmount = config.value("ObjectFadeAmount", DEFAULT_OBJECT_FADE_AMOUNT);
	wsettings.ScreenSpaceScale = config.at("ScreenSpaceScale");
	wsettings.LogMicrofacetDensity = config.at("LogMicrofacetDensity");
	wsettings.MicrofacetRoughness = config.at("MicrofacetRoughness");
	wsettings.DensityRandomization = config.at("DensityRandomization");
	config.at("MainTint").get_to(wsettings.MainTint);
	config.at("AltTint").get_to(wsettings.AltTint);
	snowingSpeed = config.at("SnowingSpeed");
	meltingSpeed = config.at("MeltingSpeed");
	wsettings.PeakMainAngle = config.at("PeakMainAngle");
	wsettings.PeakAltAngle = config.at("PeakAltAngle");
	wsettings.MinAngle = config.at("MinAngle");
	wsettings.MaxAngle = config.at("MaxAngle");
	wsettings.MainSpec = config.at("MainSpec");
	wsettings.AltSpec = config.at("AltSpec");
	bool texturesChanged = false;
	auto updatePath = [&](const char* key, std::filesystem::path& path, auto& text) {
		const auto next = config.at(key).get<std::string>();
		if (next != path.generic_string()) {
			path = next;
			strncpy_s(text, next.c_str(), _TRUNCATE);
			texturesChanged = true;
		}
	};
	updatePath("MainTexture", mainTexture, mainTextureInput);
	updatePath("AltTexture", alternateTexture, alternateTextureInput);
	updatePath("MapTexture", mapTexture, mapTextureInput);
	ValidateSettings();
	return texturesChanged;
}

bool SnowCover::LoadTextures()
{
	const auto basePath = Util::PathHelpers::GetShadersPath() / "SnowCover";
	const auto dataPath = Util::PathHelpers::GetDataPath();
	auto load = [&](const std::filesystem::path& path, std::string_view name, Util::HeightMap* heightMap = nullptr) {
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		try {
			const auto resourcePath = path.lexically_relative(dataPath).make_preferred().string();
			DirectX::ScratchImage image;
			DX::ThrowIfFailed(Util::LoadDDSImageFromResource(resourcePath.c_str(), image));
			view = Util::CreateDDSTextureFromImage(globals::d3d::device, image, std::format("SnowCover::{}", name).c_str());
			if (heightMap && !Util::LoadR8HeightMapFromDDS(image, *heightMap))
				logger::warn("Snow Cover: CPU altitude queries disabled; {} requires R8_UNORM or BC4_UNORM", path.generic_string());
		} catch (const std::exception& error) {
			logger::warn("Snow Cover: could not load {}: {}", path.generic_string(), error.what());
		}
		return view;
	};
	constexpr std::array suffixes{ ".dds", "_n.dds", "_rmaos.dds" };
	constexpr std::array names{ "SnowAlbedo", "SnowNormal", "SnowRmaos", "IceAlbedo", "IceNormal", "IceRmaos" };
	for (size_t i = 0; i < suffixes.size(); ++i) {
		if (!mainTexture.empty())
			views[i] = load(Util::JoinRelativePath(dataPath, mainTexture).concat(suffixes[i]), names[i]);
		if (!views[i])
			views[i] = load(basePath / "default" / (std::string("main") + suffixes[i]), names[i]);
		if (!alternateTexture.empty())
			views[i + suffixes.size()] = load(Util::JoinRelativePath(dataPath, alternateTexture).concat(suffixes[i]), names[i + suffixes.size()]);
		if (!views[i + suffixes.size()])
			views[i + suffixes.size()] = views[i];
	}
	if (!mapTexture.empty()) {
		const auto path = Util::JoinRelativePath(dataPath, mapTexture).concat(".dds");
		views.back() = load(path, "SnowMap", &snowMap);
	}
	if (!views.back()) {
		constexpr float neutralHeight = 0.5f;
		views.back() = Util::CreateConstantFloatTexture(globals::d3d::device, neutralHeight, "SnowCover::SnowMap");
		snowMap.SetConstant(neutralHeight);
	}
	return std::ranges::all_of(views, [](const auto& view) { return view != nullptr; });
}

void SnowCover::ReleaseTextures()
{
	texturesLoaded = false;
	ID3D11ShaderResourceView* empty[std::tuple_size_v<decltype(views)>]{};
	if (globals::d3d::context)
		globals::d3d::context->PSSetShaderResources(FIRST_SRV_SLOT, static_cast<uint32_t>(views.size()), empty);
	for (auto& view : views)
		view = nullptr;
	snowMap = {};
}

bool SnowCover::TryLoadTextures()
{
	texturesLoaded = false;
	try {
		texturesLoaded = LoadTextures();
	} catch (const std::exception& e) {
		logger::error("Snow Cover: {}: {}", lastWorldspace, e.what());
	}
	if (!texturesLoaded) {
		ReleaseTextures();
		status = "Required snow textures could not be loaded. See the log for details.";
	}
	return texturesLoaded;
}

void SnowCover::Reload()
{
	const auto current = SnowEnvironment::GetCurrentWorldspaceOrCellEditorID();
	if (current == lastWorldspace)
		return;
	WetternessSnowMelt::SuspendWorld();
	lastWorldspace = current;
	ResetWorldConfig();
	ReleaseTextures();
	trackedFires.clear();
	fireBaseCache.clear();
	buildingBaseCache.clear();
	fireScanTimer = FIRE_MELT_SCAN_INTERVAL;
	fireMeltSnap = true;
	if (current.empty()) {
		status = "No named worldspace or cell is active.";
		return;
	}
	try {
		const auto path = GetConfigPath(current);
		std::ifstream file(path);
		if (!file) {
			status = "No snow config exists for this worldspace or cell.";
			return;
		}
		LoadWorldConfig(json::parse(file), true);
		if (!TryLoadTextures())
			return;
		WetternessSnowMelt::ResumeWorld(*this);
		status = "Worldspace config loaded.";
	} catch (const std::exception& e) {
		wsettings.EnableSnowCover = false;
		status = "Could not load the snow config. See the log for details.";
		logger::error("Snow Cover: {}: {}", current, e.what());
	}
}

void SnowCover::ValidateSettings()
{
	const auto& userRules = SnowConfig::Schemas().at("settings").at("properties");
	const auto& worldRules = SnowConfig::Schemas().at("world").at("properties");
	auto clamp = [](float value, const json& rule, float fallback) {
		return Util::ClampFinite(value, rule.at("minimum").get<float>(), rule.at("maximum").get<float>(), fallback);
	};
	settings.EnableGlints = settings.EnableGlints != 0;
	settings.EnableRooftops = settings.EnableRooftops != 0;
	settings.EnableFoliageTint = settings.EnableFoliageTint != 0;
	settings.EnableBoundsCulling = settings.EnableBoundsCulling != 0;
	settings.Enable = settings.Enable != 0;
	settings.EnableExpensiveFoliage = settings.EnableExpensiveFoliage != 0;
	settings.AffectHavok = settings.AffectHavok != 0;
	settings.AffectFloraTint = settings.AffectFloraTint != 0;
	settings.SnowHeightOffset = clamp(settings.SnowHeightOffset, userRules.at("SnowHeightOffset"), 0.0f);
	settings.RooftopSnow = clamp(settings.RooftopSnow, userRules.at("RooftopSnow"), DEFAULT_ROOFTOP_SNOW);
	settings.RooftopHeight = clamp(settings.RooftopHeight, userRules.at("RooftopHeight"), DEFAULT_ROOFTOP_HEIGHT);
	settings.RooftopExposure = clamp(settings.RooftopExposure, userRules.at("RooftopExposure"), DEFAULT_ROOFTOP_EXPOSURE);
	fireMeltSettings.RadiusScale = clamp(fireMeltSettings.RadiusScale, userRules.at("FireMeltRadiusScale"), 4.0f);
	fireMeltSettings.Strength = clamp(fireMeltSettings.Strength, userRules.at("FireMeltStrength"), 1.0f);
	MaxSummerMonth = std::min(MaxSummerMonth, 11u);
	MaxWinterMonth = std::min(MaxWinterMonth, 11u);
	SummerHeightOffset = clamp(SummerHeightOffset, worldRules.at("SummerHeightOffset"), DEFAULT_SUMMER_HEIGHT_OFFSET);
	WinterHeightOffset = clamp(WinterHeightOffset, worldRules.at("WinterHeightOffset"), DEFAULT_WINTER_HEIGHT_OFFSET);
	snowingSpeed = clamp(snowingSpeed, worldRules.at("SnowingSpeed"), 1.0f);
	meltingSpeed = clamp(meltingSpeed, worldRules.at("MeltingSpeed"), 1.0f);
	wsettings.BlendSmoothness = clamp(wsettings.BlendSmoothness, worldRules.at("BlendSmoothness"), DEFAULT_BLEND_SMOOTHNESS);
	wsettings.MinAngle = clamp(wsettings.MinAngle, worldRules.at("MinAngle"), DEFAULT_MIN_ANGLE);
	wsettings.MaxAngle = std::max(wsettings.MinAngle + MinAngleRange, clamp(wsettings.MaxAngle, worldRules.at("MaxAngle"), DEFAULT_MAX_ANGLE));
	wsettings.PeakMainAngle = clamp(wsettings.PeakMainAngle, worldRules.at("PeakMainAngle"), DEFAULT_PEAK_MAIN_ANGLE);
	wsettings.PeakAltAngle = clamp(wsettings.PeakAltAngle, worldRules.at("PeakAltAngle"), DEFAULT_PEAK_ALT_ANGLE);
	wsettings.UVScale = clamp(wsettings.UVScale, worldRules.at("UVScale"), DEFAULT_UV_SCALE);
	wsettings.WeatherFadeStart = clamp(wsettings.WeatherFadeStart, worldRules.at("WeatherFadeStart"), DEFAULT_WEATHER_FADE_START);
	wsettings.WeatherFadeEnd = std::max(wsettings.WeatherFadeStart + 1.0f, clamp(wsettings.WeatherFadeEnd, worldRules.at("WeatherFadeEnd"), DEFAULT_WEATHER_FADE_END));
	wsettings.ObjectFadeStart = clamp(wsettings.ObjectFadeStart, worldRules.at("ObjectFadeStart"), DEFAULT_OBJECT_FADE_START);
	wsettings.ObjectFadeEnd = std::max(wsettings.ObjectFadeStart + 1.0f, clamp(wsettings.ObjectFadeEnd, worldRules.at("ObjectFadeEnd"), DEFAULT_OBJECT_FADE_END));
	wsettings.ObjectFadeAmount = clamp(wsettings.ObjectFadeAmount, worldRules.at("ObjectFadeAmount"), DEFAULT_OBJECT_FADE_AMOUNT);
	wsettings.MainSpec = clamp(wsettings.MainSpec, worldRules.at("MainSpec"), DEFAULT_MAIN_SPEC);
	wsettings.AltSpec = clamp(wsettings.AltSpec, worldRules.at("AltSpec"), DEFAULT_ALT_SPEC);
	wsettings.FoliageHeightOffset = clamp(wsettings.FoliageHeightOffset, worldRules.at("FoliageHeightOffset"), DEFAULT_FOLIAGE_EFFECT_OFFSET);
	wsettings.mapZscale = clamp(wsettings.mapZscale, worldRules.at("MapZscale"), DEFAULT_MAP_ZSCALE);
	wsettings.ScreenSpaceScale = clamp(wsettings.ScreenSpaceScale, worldRules.at("ScreenSpaceScale"), DEFAULT_GLINT_1);
	wsettings.LogMicrofacetDensity = clamp(wsettings.LogMicrofacetDensity, worldRules.at("LogMicrofacetDensity"), DEFAULT_GLINT_2);
	wsettings.MicrofacetRoughness = clamp(wsettings.MicrofacetRoughness, worldRules.at("MicrofacetRoughness"), DEFAULT_GLINT_3);
	wsettings.DensityRandomization = clamp(wsettings.DensityRandomization, worldRules.at("DensityRandomization"), DEFAULT_GLINT_4);
	for (const auto& [tint, name] : { std::pair{ &wsettings.MainTint, "MainTint" }, std::pair{ &wsettings.AltTint, "AltTint" } }) {
		const auto& tintRules = worldRules.at(name).at("items");
		tint->x = clamp(tint->x, tintRules, 1.0f);
		tint->y = clamp(tint->y, tintRules, 1.0f);
		tint->z = clamp(tint->z, tintRules, 1.0f);
		tint->w = clamp(tint->w, tintRules, 1.0f);
	}
}

void SnowCover::Prepass()
{
	if (loaded && active.load(std::memory_order_relaxed) && wsettings.EnableSnowCover && texturesLoaded) {
		CS_GPU_PASS("SnowCover::BindTextures");
		ID3D11ShaderResourceView* srvs[std::tuple_size_v<decltype(views)>]{};
		for (size_t i = 0; i < views.size(); ++i)
			srvs[i] = views[i].get();
		globals::d3d::context->PSSetShaderResources(FIRST_SRV_SLOT, (uint)views.size(), srvs);
	}
}

void SnowCover::LoadSettings(json& o_json)
{
	SnowConfig::Validate(o_json, SnowConfig::Schemas().at("settings"), "snowCover");
	WetternessSnowMelt::Reset();
	settings = o_json;
	const FireMeltSettings defaults{};
	fireMeltSettings.Enabled = o_json.value("FireMeltEnabled", defaults.Enabled);
	fireMeltSettings.RadiusScale = o_json.value("FireMeltRadiusScale", defaults.RadiusScale);
	fireMeltSettings.Strength = o_json.value("FireMeltStrength", defaults.Strength);
	survivalEnabled = o_json.value("SurvivalEnabled", false);
	ValidateSettings();
	if (!resourcesReady)
		active.store(settings.Enable != 0, std::memory_order_relaxed);
}

void SnowCover::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["FireMeltEnabled"] = fireMeltSettings.Enabled;
	o_json["FireMeltRadiusScale"] = fireMeltSettings.RadiusScale;
	o_json["FireMeltStrength"] = fireMeltSettings.Strength;
	o_json["SurvivalEnabled"] = survivalEnabled;
}

void SnowCover::RestoreDefaultSettings()
{
	WetternessSnowMelt::Reset();
	settings = {};
	fireMeltSettings = {};
	survivalEnabled = false;
	if (!resourcesReady)
		active.store(false, std::memory_order_relaxed);
}

bool SnowCover::ScanFireSources()
{
#ifdef TRACY_SUPPORT
	ZoneScopedN("SnowCover::ScanFireSources");
#endif
	CS_PROFILE_CPU_SCOPE("SnowCover::ScanFireSourcesCPU");
	for (auto& fire : trackedFires)
		++fire.missedScans;

	auto* tes = globals::game::tes;
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!tes || !player)
		return false;

	const auto eye = player->GetPosition();
	uint32_t classified = 0;
	fireClusters.clear();

	tes->ForEachReferenceInRange(player, FIRE_MELT_MAX_DISTANCE, [&](RE::TESObjectREFR* a_ref) {
		if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted())
			return RE::BSContainer::ForEachResult::kContinue;
		auto* base = a_ref->GetBaseObject();
		if (!SnowEnvironment::IsPlacedFireCandidate(base))
			return RE::BSContainer::ForEachResult::kContinue;
		const auto cached = fireBaseCache.find(base->GetFormID());
		const bool known = cached != fireBaseCache.end();
		if ((known && !cached->second) || (!known && classified >= MAX_FIRE_BASE_CLASSIFICATIONS_PER_SCAN))
			return RE::BSContainer::ForEachResult::kContinue;
		auto* root = a_ref->Get3D();
		if (!root)
			return RE::BSContainer::ForEachResult::kContinue;

		bool hasFire = false;
		fireSamples.clear();
		SnowEnvironment::CollectFireGeometry(root, false, hasFire, fireSamples);
		if (!known) {
			fireBaseCache.emplace(base->GetFormID(), hasFire);
			++classified;
		}

		const auto refPosition = a_ref->GetPosition();
		const size_t first = fireClusters.size();
		for (const auto& sample : fireSamples) {
			if (sample.center.GetSquaredDistance(refPosition) > FIRE_MELT_MAX_REF_OFFSET * FIRE_MELT_MAX_REF_OFFSET)
				continue;
			auto cluster = std::find_if(fireClusters.begin() + first, fireClusters.end(), [&](const FireCluster& a_cluster) {
				return a_cluster.bound.center.GetSquaredDistance(sample.center) <= FIRE_MELT_CLUSTER_DISTANCE * FIRE_MELT_CLUSTER_DISTANCE;
			});
			if (cluster != fireClusters.end())
				cluster->bound = SnowEnvironment::MergeBounds(cluster->bound, sample);
			else
				fireClusters.push_back({ a_ref->GetFormID(), static_cast<uint32_t>(fireClusters.size() - first), sample, 0.0f });
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});

	for (auto& cluster : fireClusters)
		cluster.distanceSq = eye.GetSquaredDistance(cluster.bound.center);
	std::erase_if(fireClusters, [](const FireCluster& cluster) { return !std::isfinite(cluster.distanceSq); });
	const size_t keep = std::min<size_t>(fireClusters.size(), MAX_TRACKED_FIRES);
	std::partial_sort(fireClusters.begin(), fireClusters.begin() + keep, fireClusters.end(), [](const FireCluster& a, const FireCluster& b) { return a.distanceSq < b.distanceSq; });

	for (size_t i = 0; i < keep; ++i) {
		const auto& cluster = fireClusters[i];
		const float radius = std::clamp(cluster.bound.radius, FIRE_MELT_MIN_RADIUS, FIRE_MELT_MAX_RADIUS);
		auto fire = std::find_if(trackedFires.begin(), trackedFires.end(), [&](const TrackedFire& a_fire) {
			return a_fire.refID == cluster.refID && a_fire.cluster == cluster.cluster;
		});
		if (fire == trackedFires.end()) {
			trackedFires.push_back({ cluster.refID, cluster.cluster, cluster.bound.center, radius, cluster.bound.center, radius, fireMeltSnap ? 1.0f : 0.0f, 0, 1 });
			continue;
		}
		fire->samples = std::min(fire->samples + 1, FIRE_MELT_FOOTPRINT_SAMPLES);
		const float sampleBlend = 1.0f / static_cast<float>(fire->samples);
		fire->sampleCenter += (cluster.bound.center - fire->sampleCenter) * sampleBlend;
		fire->sampleRadius += (radius - fire->sampleRadius) * sampleBlend;
		fire->missedScans = 0;
	}
	return classified < MAX_FIRE_BASE_CLASSIFICATIONS_PER_SCAN;
}

void SnowCover::UpdateFireMelt()
{
	if (!perFrame.wsettings.EnableSnowCover || !fireMeltSettings.Enabled || fireMeltSettings.Strength <= 0.0f || globals::state->IsMainOrLoadingMenuOpen()) {
		trackedFires.clear();
		fireScanTimer = FIRE_MELT_SCAN_INTERVAL;
		fireMeltSnap = true;
		UploadFireMelt();
		return;
	}

	// Settings refreshes may rebuild the buffer more than once in the same frame.
	if (!fireFrame.IsNewFrame()) {
		UploadFireMelt();
		return;
	}
	const float dt = globals::game::ui && globals::game::ui->GameIsPaused() ? 0.0f : Util::ClampFinite(RE::GetSecondsSinceLastFrame(), 0.0f, MaxFrameSeconds, 0.0f);
	fireScanTimer += dt;
	if (dt > 0.0f && fireScanTimer >= FIRE_MELT_SCAN_INTERVAL) {
		fireScanTimer = 0.0f;
		if (ScanFireSources())
			fireMeltSnap = false;
	}

	const float footprintBlend = 1.0f - std::exp(-dt / FIRE_MELT_FOOTPRINT_SMOOTHING);
	const float fadeStep = dt / FIRE_MELT_FADE_TIME;
	for (auto& fire : trackedFires) {
		fire.center += (fire.sampleCenter - fire.center) * footprintBlend;
		fire.radius += (fire.sampleRadius - fire.radius) * footprintBlend;
		fire.strength = std::clamp(fire.strength + (fire.missedScans < FIRE_MELT_GRACE_SCANS ? fadeStep : -fadeStep), 0.0f, 1.0f);
	}
	std::erase_if(trackedFires, [](const TrackedFire& a_fire) { return a_fire.missedScans >= FIRE_MELT_GRACE_SCANS && a_fire.strength <= 0.0f; });

	UploadFireMelt();
}

void SnowCover::UploadFireMelt()
{
	auto& fireMelt = perFrame.fireMelt;
	fireMelt.Count = 0;
	fireMelt.Strength = fireMeltSettings.Strength;
	fireMelt.RadiusScale = fireMeltSettings.RadiusScale;
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (trackedFires.empty() || !player)
		return;

	const auto eye = player->GetPosition();
	fireOrder.clear();
	for (uint32_t i = 0; i < trackedFires.size(); ++i) {
		const float distance = eye.GetDistance(trackedFires[i].center);
		if (trackedFires[i].strength > 0.0f && std::isfinite(distance))
			fireOrder.emplace_back(distance, i);
	}
	const size_t ranked = std::min<size_t>(fireOrder.size(), MAX_FIRE_MELT_SOURCES + 1);
	std::partial_sort(fireOrder.begin(), fireOrder.begin() + ranked, fireOrder.end());

	const float slotCutoff = fireOrder.size() > MAX_FIRE_MELT_SOURCES ? fireOrder[MAX_FIRE_MELT_SOURCES].first : std::numeric_limits<float>::max();
	for (size_t i = 0; i < std::min<size_t>(ranked, MAX_FIRE_MELT_SOURCES); ++i) {
		const auto [distance, index] = fireOrder[i];
		const auto& fire = trackedFires[index];
		const float weight = fire.strength *
		                     std::clamp((FIRE_MELT_MAX_DISTANCE - distance) / FIRE_MELT_DISTANCE_FADE, 0.0f, 1.0f) *
		                     std::clamp((slotCutoff - distance) / FIRE_MELT_SLOT_FADE, 0.0f, 1.0f);
		const float radius = fire.radius * weight;
		if (radius >= 1.0f)
			fireMelt.Spheres[fireMelt.Count++] = float4{ fire.center.x, fire.center.y, fire.center.z, radius };
	}
}

uint32_t SnowCover::BeginPass(const RE::BSRenderPass* pass)
{
	auto* state = globals::state;
	const uint32_t previous = state->permutationData.ExtraShaderDescriptor & PassMask;
	if (!active.load(std::memory_order_relaxed) || !wsettings.EnableSnowCover || !texturesLoaded || !pass || !pass->shader || pass->shader->shaderType.get() != RE::BSShader::Type::Lighting || !pass->geometry)
		return previous;
	uint32_t flags = 0;
	auto* reference = pass->geometry->GetUserData();
	const char* name = pass->geometry->name.c_str();
	const std::string_view geometryName = name ? name : "";
	const bool actor = reference && reference->GetFormType() == RE::FormType::ActorCharacter;
	const bool movable = reference && reference->CanBeMoved();
	const bool animated = pass->geometry->HasAnimation() || (reference && reference->GetBaseObject() && reference->GetBaseObject()->IsBoundAnimObject());
	if (actor || blacklist.contains(geometryName) || ((animated || movable) && !whitelist.contains(geometryName) && !(settings.AffectHavok && movable)))
		flags |= static_cast<uint32_t>(State::ExtraShaderDescriptors::NoSnow);
	if (!settings.AffectFloraTint && SnowEnvironment::IsHarvestableFlora(reference))
		flags |= static_cast<uint32_t>(State::ExtraShaderDescriptors::NoFoliageTint);
	if (settings.EnableRooftops && settings.RooftopSnow > 0.0f && IsBuilding(reference))
		flags |= static_cast<uint32_t>(State::ExtraShaderDescriptors::SnowRooftop);
	if (!reference && SnowEnvironment::IsObjectLODTree(name, pass->shaderProperty))
		flags |= static_cast<uint32_t>(State::ExtraShaderDescriptors::SnowLODTree);
	if (settings.EnableBoundsCulling && IsSnowFree(pass, flags))
		flags |= static_cast<uint32_t>(State::ExtraShaderDescriptors::SnowInactive);
	if (flags == previous)
		return previous;
	state->permutationData.ExtraShaderDescriptor = (state->permutationData.ExtraShaderDescriptor & ~PassMask) | flags;
	return previous;
}

void SnowCover::EndPass(uint32_t previous)
{
	auto* state = globals::state;
	state->permutationData.ExtraShaderDescriptor = (state->permutationData.ExtraShaderDescriptor & ~PassMask) | previous;
}

bool SnowCover::IsBuilding(RE::TESObjectREFR* reference)
{
	auto* base = reference ? reference->GetBaseObject() : nullptr;
	if (!base || (base->GetFormType() != RE::FormType::Static && base->GetFormType() != RE::FormType::MovableStatic))
		return false;
	if (const auto found = buildingBaseCache.find(base->GetFormID()); found != buildingBaseCache.end())
		return found->second;
	const bool building = SnowEnvironment::IsArchitecturalModel(base->As<RE::TESModel>());
	buildingBaseCache.emplace(base->GetFormID(), building);
	return building;
}

bool SnowCover::IsSnowFree(const RE::BSRenderPass* pass, uint32_t flags) const
{
	using Material = RE::BSShaderMaterial::Feature;
	const auto* property = pass->shaderProperty;
	const auto* material = property ? property->material : nullptr;
	if (!material || perFrame.TimeSnowing > 0.0f || WetternessSnowMelt::HasHistory())
		return false;
	const auto feature = material->GetFeature();
	if (feature == Material::kMultiTexLand || feature == Material::kMultiTexLandLODBlend || feature == Material::kLODLand || feature == Material::kLODLandNoise)
		return false;
	const bool tree = feature == Material::kTreeAnim || (flags & static_cast<uint32_t>(State::ExtraShaderDescriptors::SnowLODTree)) != 0;
	if (!tree && (flags & static_cast<uint32_t>(State::ExtraShaderDescriptors::NoSnow)) != 0)
		return true;
	float displacement = 0.5f;
	if (feature == Material::kDefault && property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
		// PBR stores displacement scale in the shared lighting material's rim power.
		const float scale = static_cast<const RE::BSLightingShaderMaterialBase*>(material)->rimLightPower;
		if (!std::isfinite(scale))
			return false;
		displacement = 0.5f * std::abs(scale);
	}
	const auto& bound = pass->geometry->worldBound;
	if (!std::isfinite(bound.radius) || bound.radius < 0.0f || !std::isfinite(bound.center.x) ||
		!std::isfinite(bound.center.y) || !std::isfinite(bound.center.z))
		return false;
	const float radius = std::max(bound.radius, 0.0f);
	const auto mapMaximum = snowMap.Maximum((bound.center.x - radius) * wsettings.mapScale.x + wsettings.mapOffset.x,
		(bound.center.y - radius) * wsettings.mapScale.y + wsettings.mapOffset.y,
		(bound.center.x + radius) * wsettings.mapScale.x + wsettings.mapOffset.x,
		(bound.center.y + radius) * wsettings.mapScale.y + wsettings.mapOffset.y);
	if (!mapMaximum)
		return false;
	const float rooftop = (flags & static_cast<uint32_t>(State::ExtraShaderDescriptors::SnowRooftop)) != 0 ? settings.RooftopSnow : 0.0f;
	const float top = bound.center.z + radius + SnowBoundsPadding + rooftop;
	const float environment = (top - settings.SnowHeightOffset + (*mapMaximum + MapFilterPadding - 0.5f) * wsettings.mapZscale + perFrame.SeasonalAltitude) / wsettings.BlendSmoothness;
	if (!(environment + DisplacementInfluence * displacement < 0.0f))
		return false;
	return !tree || !settings.EnableFoliageTint || !wsettings.AffectTreeTint || environment < wsettings.FoliageHeightOffset / FoliageTransitionWidth;
}

void SnowCover::DataLoaded()
{
	if (survivalGlobal)
		return;
	auto* handler = RE::TESDataHandler::GetSingleton();
	auto* spell = handler ? handler->LookupForm<RE::SpellItem>(SurvivalRegionSpell, "ccqdrsse001-survivalmode.esl") : nullptr;
	if (!spell || spell->effects.size() != 3 || std::ranges::any_of(spell->effects, [](const auto* effect) { return !effect || !effect->conditions.head; }))
		return;
	survivalGlobal = RE::IFormFactory::Create<RE::TESGlobal>();
	if (!survivalGlobal)
		return;
	survivalGlobal->type = RE::TESGlobal::Type::kFloat;
	survivalGlobal->value = 0.0f;
	for (uint32_t i = 0; i < 3; ++i) {
		auto* condition = new RE::TESConditionItem();
		condition->data.comparisonValue.f = 1.0f;
		condition->data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetGlobalValue;
		condition->data.functionData.params[0] = survivalGlobal;
		condition->data.flags.isOR = i == 2;
		condition->data.flags.opCode = i == 2 ? RE::CONDITION_ITEM_DATA::OpCode::kGreaterThanOrEqualTo : RE::CONDITION_ITEM_DATA::OpCode::kLessThan;
		condition->next = spell->effects[i]->conditions.head;
		spell->effects[i]->conditions.head = condition;
	}
}

void SnowCover::UpdateSurvival()
{
	float amount = 0.0f;
	const auto* player = RE::PlayerCharacter::GetSingleton();
	if (survivalEnabled && survivalGlobal && perFrame.wsettings.EnableSnowCover && player && !Util::IsInterior()) {
		const auto position = player->GetPosition();
		const float u = position.x * wsettings.mapScale.x + wsettings.mapOffset.x;
		const float v = position.y * wsettings.mapScale.y + wsettings.mapOffset.y;
		if (snowMap.ContainsSample(u, v)) {
			const float map = snowMap.Sample(u, v);
			const float environment = (position.z - settings.SnowHeightOffset + (map - 0.5f) * wsettings.mapZscale + perFrame.SeasonalAltitude) / wsettings.BlendSmoothness;
			const float weather = timeSnowing * timeSnowing * timeSnowing * std::max(500.0f, snowingDensity) / 500.0f;
			amount = std::clamp(std::max(environment, weather), 0.0f, 1.0f);
		}
	}
	survivalSnowAmount = amount;
	if (amount <= SurvivalSnowOff)
		survivalSnowy.store(false, std::memory_order_relaxed);
	else if (amount >= SurvivalSnowOn)
		survivalSnowy.store(true, std::memory_order_relaxed);
	PublishSurvival(active.load(std::memory_order_relaxed) && survivalSnowy.load(std::memory_order_relaxed));
}

void SnowCover::PublishSurvival(bool snowy)
{
	if (!survivalGlobal)
		return;
	if (auto* tasks = SKSE::GetTaskInterface(); tasks && survivalRequested.exchange(snowy) != snowy) {
		tasks->AddTask([this] {
			survivalGlobal->value = survivalRequested.load(std::memory_order_acquire) ? 1.0f : 0.0f;
		});
	}
}

#ifdef DEVBENCH_BRIDGE_ENABLED
json SnowCover::GetDiagnostics()
{
	json configured;
	SaveSettings(configured);
	const auto melt = WetternessSnowMelt::GetCommonBufferData();
	return {
		{ "settings", configured },
		{ "world", GetWorldConfig() },
		{ "status", status },
		{ "Worldspace", lastWorldspace },
		{ "MasterEnabled", settings.Enable != 0 },
		{ "Enabled", perFrame.wsettings.EnableSnowCover != 0 },
		{ "CpuSnowBoundsAvailable", snowMap.IsLoaded() },
		{ "Month", perFrame.Month },
		{ "SeasonalAltitude", perFrame.SeasonalAltitude },
		{ "TimeSnowing", perFrame.TimeSnowing },
		{ "SnowingDensity", perFrame.SnowingDensity },
		{ "MeltWetness", { { "Active", melt.enabled != 0 }, { "HistorySamples", melt.count },
							 { "LightDryingHours", melt.lightDryingHours }, { "HeavyDryingHours", melt.heavyDryingHours },
							 { "MaximumHistoryHours", WetternessSnowMelt::MaximumDryingHours } } },
		{ "TrackedFires", trackedFires.size() },
		{ "FireMeltCount", perFrame.fireMelt.Count },
		{ "SurvivalAvailable", survivalGlobal != nullptr },
		{ "SurvivalSnowAmount", survivalSnowAmount },
		{ "SurvivalFreezing", survivalSnowy.load(std::memory_order_relaxed) }
	};
}

#endif

#ifdef DEVBENCH_BRIDGE_ENABLED
json SnowCover::GetControlSchemas()
{
	return SnowConfig::Schemas();
}

json SnowCover::Control(const json& args)
{
	for (const auto& [key, value] : args.items()) {
		(void)value;
		if (key != "action" && key != "contractMajor" && key != "clientId" && key != "commandId" && key != "expectedBuildId" &&
			key != "snowCover" && key != "snowWorld" && key != "snowOperation")
			throw std::invalid_argument("Unknown snow_cover argument: " + key);
	}
	if (!loaded)
		throw std::invalid_argument("Snow Cover must be loaded");
	const auto operation = args.value("snowOperation", std::string("inspect"));
	if (operation != "inspect" && operation != "reload" && operation != "save" && operation != "world_defaults")
		throw std::invalid_argument("Unknown snowOperation");
	if (operation != "inspect" && (args.contains("snowCover") || args.contains("snowWorld")))
		throw std::invalid_argument("Apply settings and world commands in separate calls");
	const bool changing = operation != "inspect" || args.contains("snowCover") || args.contains("snowWorld");
	if (changing && globals::state->IsMainOrLoadingMenuOpen())
		throw std::invalid_argument("Snow mutations wait until a world is loaded");
	if ((args.contains("snowWorld") || operation == "save" || operation == "world_defaults") &&
		(lastWorldspace.empty() || SnowEnvironment::GetCurrentWorldspaceOrCellEditorID() != lastWorldspace))
		throw std::invalid_argument("Enable Snow Cover and wait for its current world config before editing or saving world settings");
	if (args.contains("snowCover") || args.contains("snowWorld")) {
		const auto schemas = GetControlSchemas();
		json currentSettings;
		SaveSettings(currentSettings);
		auto nextSettings = currentSettings;
		const auto currentWorld = GetWorldConfig();
		auto nextWorld = currentWorld;
		if (args.contains("snowCover")) {
			SnowConfig::Validate(args.at("snowCover"), schemas.at("settings"), "snowCover");
			nextSettings.update(args.at("snowCover"));
		}
		if (args.contains("snowWorld")) {
			SnowConfig::Validate(args.at("snowWorld"), schemas.at("world"), "snowWorld");
			nextWorld.update(args.at("snowWorld"));
		}
		SnowConfig::ValidateWorld(nextWorld);
		if (nextSettings != currentSettings)
			LoadSettings(nextSettings);
		if (nextWorld != currentWorld) {
			if (LoadWorldConfig(nextWorld) || (wsettings.EnableSnowCover && !texturesLoaded))
				refreshTextures.store(true);
		}
		if (nextSettings != currentSettings || nextWorld != currentWorld)
			frame = {};
	}
	if (operation == "reload")
		reloadRequested.store(true);
	else if (operation == "save") {
		SaveConfig(false);
		if (status != "Worldspace config saved.")
			throw std::runtime_error(status);
	} else if (operation == "world_defaults") {
		const auto enabled = wsettings.EnableSnowCover;
		ResetWorldConfig();
		wsettings.EnableSnowCover = enabled;
		refreshTextures.store(true);
		frame = {};
	}
	return GetDiagnostics();
}
#endif
