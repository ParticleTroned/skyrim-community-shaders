#include "State.h"

#include <codecvt>

#include <pystring/pystring.h>

#include "Deferred.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "Features/CSEditor.h"
#include "Features/CloudShadows.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/HDRDisplay.h"
#include "Features/InteriorSun.h"
#include "Features/PerformanceOverlay.h"
#include "Features/Skin.h"
#include "Features/SkySync.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/Upscaling.h"
#include "Features/VolumetricShadows.h"
#include "Menu.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "ShaderCache.h"
#include "TruePBR.h"
#include "Utils/FileSystem.h"
#include "Utils/SphericalHarmonics.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#ifdef TRACY_ENABLE
static thread_local std::vector<TracyCZoneCtx> s_tracyPerfZones;
#endif

namespace
{
	std::vector<std::string> SaveUserOverrides(const json& a_settings)
	{
		std::vector<std::string> failedLayers;
		auto* overrideManager = SettingsOverrideManager::GetSingleton();
		if (!overrideManager->IsEnabled())
			return failedLayers;

		for (auto* feature : Feature::GetFeatureList()) {
			std::string featureName = "<unknown>";
			try {
				featureName = feature->GetShortName();
				if (!feature->loaded || !overrideManager->HasFeatureOverrides(featureName))
					continue;

				json currentSettings;
				feature->SaveSettings(currentSettings);
				const auto overrideSettings =
					overrideManager->GetMergedOverrideSettings(featureName, json::object());
				if (!overrideManager->SaveUserOverride(featureName, currentSettings, overrideSettings))
					failedLayers.push_back(featureName);
			} catch (const std::exception& e) {
				logger::warn("Failed to save user override for {}: {}", featureName, e.what());
				failedLayers.push_back(featureName);
			} catch (...) {
				logger::warn("Failed to save user override for {} due to an unknown error", featureName);
				failedLayers.push_back(featureName);
			}
		}

		try {
			const auto globalOverrideSettings =
				overrideManager->GetMergedOverrideSettings("Global", json::object());
			if (!overrideManager->SaveUserOverride("Global", a_settings, globalOverrideSettings))
				failedLayers.emplace_back("Global");
		} catch (const std::exception& e) {
			logger::warn("Failed to save global user override: {}", e.what());
			failedLayers.emplace_back("Global");
		} catch (...) {
			logger::warn("Failed to save global user override due to an unknown error");
			failedLayers.emplace_back("Global");
		}

		return failedLayers;
	}

	std::string JoinSettingLayerNames(const std::vector<std::string>& a_names)
	{
		std::string result;
		for (const auto& name : a_names) {
			if (!result.empty())
				result += ", ";
			result += name;
		}
		return result;
	}

	bool IsForcedDisabledFeature(const std::string& a_featureName)
	{
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature->GetShortName() == a_featureName)
				return feature->IsForcedDisabledAtBoot();
		}

		return false;
	}
}

void State::UpdateSkyShaderPermutation(RE::BSRenderPass* a_pass)
{
	permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::IsSun);

	if (!a_pass || !a_pass->shaderProperty)
		return;

	auto* skyProperty = static_cast<const RE::BSSkyShaderProperty*>(a_pass->shaderProperty);
	if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_SUN ||
		skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_SUN_GLARE) {
		permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::IsSun);
	}
}

void State::Draw()
{
	ZoneScoped;

	auto shaderCache = globals::shaderCache;
	auto weatherManager = globals::weatherManager;
	auto sceneSettingsManager = globals::sceneSettingsManager;
	auto& terrainBlending = globals::features::terrainBlending;
	auto& terrainHelper = globals::features::terrainHelper;
	auto& cloudShadows = globals::features::cloudShadows;
	auto& csEditor = globals::features::csEditor;
	auto& skin = globals::features::skin;
	auto& truePBR = globals::features::truePBR;
	auto context = globals::d3d::context;
	auto& volumetricShadows = globals::features::volumetricShadows;

	if (shaderCache->IsEnabled()) {
		const bool sceneManagersReady =
			PerformanceTuningRenderer::PrepareForSceneUpdate();

		// Process deferred cell transitions (interior detection)
		if (sceneManagersReady)
			sceneSettingsManager->Update();

		if (sceneManagersReady && csEditor.loaded) {
			ZoneScopedN("WeatherManager::UpdateFeatures");
			weatherManager->UpdateFeatures();
		}

		if (terrainBlending.loaded && terrainBlending.settings.Enabled) {
			ZoneScopedN("TerrainBlending::TerrainShaderHacks");
			terrainBlending.TerrainShaderHacks();
		}

		if (cloudShadows.loaded) {
			ZoneScopedN("CloudShadows::SkyShaderHacks");
			cloudShadows.SkyShaderHacks();
		}

		if (terrainHelper.loaded) {
			ZoneScopedN("TerrainHelper::SetShaderResources");
			terrainHelper.SetShaderResources(context);
		}

		if (skin.loaded) {
			ZoneScopedN("Skin::SetShaderResources");
			skin.SetShaderResources(context);
		}

		if (truePBR.loaded) {
			ZoneScopedN("TruePBR::SetShaderResources");
			truePBR.SetShaderResources(context);
		}

		if (permutationData != permutationDataPrevious) {
			permutationCB->Update(permutationData);
			permutationDataPrevious = permutationData;
		}

		if (currentShader && updateShader) {
			if (currentShader->shaderType.get() == RE::BSShader::Type::Utility) {
				if (currentPixelDescriptor & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmask)) {
					if (volumetricShadows.loaded)
						volumetricShadows.CopyShadowLightData();
					if (globals::features::exponentialHeightFog.loaded)
						globals::features::exponentialHeightFog.CaptureDirectionalShadowMap();
				}
			}
		}

		if (globals::menu->overlayVisible && globals::features::performanceOverlay.loaded && globals::features::performanceOverlay.IsOverlayVisible())
			Debug();

		updateShader = false;
	}
}

void State::Debug()
{
	auto lock = Lock();

	if (frameChecker.IsNewFrame()) {
		// Smooth draw calls and frame times for all shader types
		for (int i = 0; i < magic_enum::enum_integer(RE::BSShader::Type::Total) + 1; ++i) {
			smoothDrawCalls[i] = smoothDrawCalls[i] * static_cast<float>(0.95) + drawCalls[i] * static_cast<float>(0.05);
			smoothFrameTimePerType[i] = smoothFrameTimePerType[i] * static_cast<float>(0.95) + frameTimePerType[i] * static_cast<float>(0.05);
		}
		// Reset counters for next frame
		for (auto& c : drawCalls)
			c = 0;
		for (auto& ft : frameTimePerType)
			ft = 0.0f;

		// Reset active shader tracking for developer mode
		globals::shaderCache->ResetFrameShaderTracking();

		// Start timing for this frame
		if (frameTimingFrequency.QuadPart == 0) {
			QueryPerformanceFrequency(&frameTimingFrequency);
		}
		QueryPerformanceCounter(&frameStartTime);
		frameTimingActive = true;
	}

	// Track time for current shader type if timing is active
	if (frameTimingActive && currentShader) {
		LARGE_INTEGER currentTime;
		QueryPerformanceCounter(&currentTime);

		// Calculate elapsed time in milliseconds
		float elapsed = (currentTime.QuadPart - frameStartTime.QuadPart) * 1000.0f / frameTimingFrequency.QuadPart;

		// Add elapsed time to the current shader type
		frameTimePerType[magic_enum::enum_integer(currentShader->shaderType.get())] += elapsed;
		frameTimePerType[magic_enum::enum_integer(RE::BSShader::Type::Total)] += elapsed;

		// Update start time for next measurement
		frameStartTime = currentTime;
	}

	if (currentShader) {
		drawCalls[magic_enum::enum_integer(currentShader->shaderType.get())]++;
		drawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]++;
	}

	if (currentShader && updateShader && frameAnnotations) {
		BeginPerfEvent(std::format("Draw: CS {}::{:x}::{}", magic_enum::enum_name(currentShader->shaderType.get()), permutationData.PixelShaderDescriptor, currentShader->fxpFilename));
		SetPerfMarker(std::format("Defines: {}", SIE::ShaderCache::GetDefinesString(*currentShader, permutationData.PixelShaderDescriptor)));
		EndPerfEvent();
	}
}

void State::Reset()
{
	Feature::ForEachLoadedFeature("Reset", [](Feature* feature) { feature->Reset(); });
	if (!globals::game::ui->GameIsPaused())
		timer += RE::GetSecondsSinceLastFrame();

	// Cache menu open states once per frame to avoid repeated IsMenuOpen calls
	// (each call constructs a BSFixedString, which is expensive at scale).
	if (auto ui = globals::game::ui) {
		isMainMenuOpen = ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
		isLoadingMenuOpen = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		isMapMenuOpen = ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
	} else {
		isMainMenuOpen = false;
		isLoadingMenuOpen = false;
		isMapMenuOpen = false;
	}

	lastModifiedPixelDescriptor = 0;
	lastModifiedVertexDescriptor = 0;
	lastPixelDescriptor = 0;
	lastVertexDescriptor = 0;
	std::memset(&permutationDataPrevious, 0xFF, sizeof(PermutationCB));
	frameCount++;
	globals::shaderCache->TickActiveShaderCapture(globals::menu && globals::menu->IsEnabled);
	globals::shaderCache->ProcessPendingClear();

	if (auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton()) {
		auto& BSImagespaceShaderApplyReflections = imageSpaceManager->GetRuntimeData().BSImagespaceShaderApplyReflections;

		// Disable reflections being applied to things other than water
		if (BSImagespaceShaderApplyReflections.get()) {
			BSImagespaceShaderApplyReflections->active = false;
		}
	}

	// Disable "improved" snow shader, unsupported
	RE::GetINISetting("bEnableImprovedSnow:Display")->data.b = false;

	activeReflections = false;
}

void State::Setup()
{
	// Detect Moon and Stars mod for compatibility adjustments
	moonAndStarsLoaded = GetModuleHandle(L"po3_MoonMod.dll") != nullptr;
	if (moonAndStarsLoaded)
		logger::info("Moon and Stars detected, compatibility enabled");

	SetupResources();

	// Probe typed UAV load support before features set up their resources, so any
	// gating logic that wants to read the log can run during feature SetupResources.
	CheckTypedUAVLoadSupport();

	Feature::ForEachLoadedFeature("SetupResources", [](Feature* feature) { feature->SetupResources(); });
	globals::deferred->SetupResources();

	// Load per-weather settings after features are setup
	globals::weatherManager->LoadPerWeatherSettingsFromDisk();

	// Load scene-specific settings (Interior Only, etc.)
	globals::sceneSettingsManager->LoadAll();
}

static std::string GetConfigPath(State::ConfigMode a_configMode)
{
	switch (a_configMode) {
	case State::ConfigMode::USER:
		return Util::PathHelpers::GetSettingsUserPath().string();
	case State::ConfigMode::TEST:
		return Util::PathHelpers::GetSettingsTestPath().string();
	case State::ConfigMode::THEME:
		return Util::PathHelpers::GetSettingsThemePath().string();
	case State::ConfigMode::DEFAULT:
	default:
		return Util::PathHelpers::GetSettingsDefaultPath().string();
	}
}

void State::Load(ConfigMode a_configMode, bool a_allowReload)
{
	json settings;
	bool errorDetected = false;

	auto configFolderPath = std::filesystem::path(GetConfigPath(a_configMode)).parent_path().string();
	auto defaultConfigFilePath = GetConfigPath(ConfigMode::DEFAULT);
	auto userConfigFilePath = GetConfigPath(ConfigMode::USER);

	try {
		std::filesystem::create_directories(configFolderPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating directory during Load ({}) : {}\n", configFolderPath, e.what());
		errorDetected = true;
	}

	// Attempt to load the config file
	auto tryLoadConfig = [&](const std::string& path) -> bool {
		std::ifstream i(path);
		logger::info("Attempting to open config file: {}", path);
		if (!i.is_open()) {
			logger::warn("Unable to open config file: {}", path);
			return false;
		}
		try {
			i >> settings;
			i.close();
			return true;
		} catch (const nlohmann::json::parse_error& e) {
			logger::warn("Error parsing json config file ({}) : {}\n", path, e.what());
			i.close();
			return false;
		}
	};

	// LOADING ORDER: Default → User → Overrides → User Overrides (.user files)

	// Step 1: Always start with default settings
	logger::info("Loading default settings from: {}", defaultConfigFilePath);
	if (!tryLoadConfig(defaultConfigFilePath)) {
		logger::info("No default config ({}), generating new one", defaultConfigFilePath);
		std::fill(enabledClasses, enabledClasses + magic_enum::enum_integer(RE::BSShader::Type::Total) - 1, true);
		Save(ConfigMode::DEFAULT);
		// Attempt to load the newly created config
		if (!tryLoadConfig(defaultConfigFilePath)) {
			logger::error("Error opening newly created default config file ({})\n", defaultConfigFilePath);
			return;
		}
	}

	// Step 2: Apply user settings on top of defaults (user preferences)
	if (a_configMode == ConfigMode::USER) {
		json userSettings;
		std::ifstream userFile(userConfigFilePath);
		if (userFile.is_open()) {
			try {
				userFile >> userSettings;
				userFile.close();

				// Merge user settings on top of defaults
				for (auto& [key, value] : userSettings.items()) {
					settings[key] = value;
				}
				logger::info("Applied user settings from: {}", userConfigFilePath);
			} catch (const nlohmann::json::parse_error& e) {
				logger::warn("Error parsing user config file: {}", e.what());
				userFile.close();
			}
		} else {
			logger::info("No user config file found at: {}", userConfigFilePath);
		}
	}

	// Step 3: Discover and prepare overrides (applied after user settings, so overrides take priority)
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	size_t overridesDiscovered = overrideManager->DiscoverOverrides();

	// Cleanup stale user override files (where override hash has changed)
	if (overridesDiscovered > 0) {
		logger::info("Discovered {} override files", overridesDiscovered);
		overrideManager->CleanupStaleUserOverrides();

		// Apply global overrides to main settings
		size_t globalOverrides = overrideManager->ApplyGlobalOverrides(settings);
		if (globalOverrides > 0) {
			logger::info("Applied {} global override(s)", globalOverrides);
		}

		// Apply global user overrides on top (if any)
		if (overrideManager->LoadUserOverride("Global", settings)) {
			logger::info("Applied global user override customizations");
		}
	}

	try {
		// Load core settings (Menu, Advanced, General, Replace Original Shaders)
		logger::info("Loading core settings");
		LoadFromJson(settings);
		// Ensure 'Disable at Boot' section exists in the JSON
		if (!settings.contains("Disable at Boot") || !settings["Disable at Boot"].is_object()) {
			// Initialize to an empty object if it doesn't exist
			settings["Disable at Boot"] = json::object();
		}

		json& disabledFeaturesJson = settings["Disable at Boot"];
		logger::info("Loading 'Disable at Boot' settings");

		for (auto& [featureName, featureStatus] : disabledFeaturesJson.items()) {
			if (featureStatus.is_boolean()) {
				disabledFeatures[featureName] = featureStatus.get<bool>();
			} else {
				logger::warn("Invalid entry for feature '{}' in 'Disable at Boot', expected boolean.", featureName);
			}
		}

		// Once DataLoaded has run, a full settings reload must not repeat feature
		// validation or lifecycle registration; only live settings may change.
		const bool runtimeReload = globals::shaderCache->menuLoaded.load(std::memory_order_relaxed);
		for (auto* feature : Feature::GetFeatureList()) {
			const bool wasLoaded = feature->loaded;
			try {
				const std::string featureName = feature->GetShortName();
				if (feature->IsForcedDisabledAtBoot()) {
					disabledFeatures[featureName] = true;
					logger::info("Feature '{}' is forced disabled at boot", featureName);
				} else if (!disabledFeatures.contains(featureName) && feature->IsDisabledByDefault()) {
					disabledFeatures[featureName] = true;
					logger::info("Feature '{}' is disabled by default", featureName);
				}
				bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];
				if (!isDisabled) {
					logger::info("{} Feature: '{}'", runtimeReload ? "Reloading" : "Loading", featureName);

					if (runtimeReload && !wasLoaded) {
						logger::info("Feature '{}' remains unavailable until restart", featureName);
						continue;
					}

					// Load base feature settings from merged config (default + user)
					if (runtimeReload) {
						feature->ReloadSettings(settings);
					} else {
						feature->Load(settings);
					}

					// Register weather variables (features opt-in by implementing this)
					if (!runtimeReload) {
						feature->RegisterWeatherVariables();
					}

					// Apply feature-specific overrides on top (overrides take priority over user settings)
					if (overridesDiscovered > 0 && overrideManager->HasFeatureOverrides(featureName)) {
						json featureJson;
						feature->SaveSettings(featureJson);  // Get current settings as JSON

						// Apply overrides
						size_t appliedOverrides = overrideManager->ApplyOverrides(featureName, featureJson);
						if (appliedOverrides > 0) {
							logger::info("Applied {} override(s) to {}", appliedOverrides, feature->GetName());
						}

						// Apply user override customizations on top (if any)
						if (overrideManager->LoadUserOverride(featureName, featureJson)) {
							logger::info("Applied user override customizations to {}", feature->GetName());
						}

						// Reload settings with overrides applied
						try {
							feature->LoadSettings(featureJson);
						} catch (...) {
							logger::warn("Invalid override settings for {}, keeping original settings.", feature->GetName());
						}
					}

					// Capture current values as user settings baseline for weather overrides
					WeatherVariables::GlobalWeatherRegistry::GetSingleton()->CaptureFeatureUserSettings(featureName);
				} else {
					// Initial boot gating is deliberate, not a load failure. Runtime
					// reloads preserve lifecycle failures until a restart can retry them.
					if (!runtimeReload) {
						feature->loadFailed = false;
					}
					logger::info("Feature '{}' is disabled at boot.", featureName);
				}
			} catch (const std::exception& e) {
				const auto displayName = feature->GetDisplayName();
				// Initial validation failures are fatal. A later runtime reload can
				// fail in override/registration code after hooks already exist, so do
				// not tear down a previously working feature without its own cleanup.
				if (!runtimeReload || !wasLoaded) {
					feature->loaded = false;
					feature->loadFailed = true;
					feature->failedLoadedMessage = feature->failedLoadedMessage.empty() ?
					                                   (displayName + " failed to load. Check CommunityShaders.log") :
					                                   (feature->failedLoadedMessage + "\n" + displayName + " failed to load. Check CommunityShaders.log");
				}
				logger::warn("Error loading setting for feature '{}': {}", feature->GetShortName(), e.what());
			}
		}

		const auto currentVersion = std::string{ Plugin::VERSION_LABEL };
		if (settings["Version"].is_string() && settings["Version"].get<std::string>() != currentVersion) {
			logger::info("Found older config for version {}; upgrading to {}", (std::string)settings["Version"], currentVersion);
			Save(a_configMode);  // Use original config mode
		}

		FeatureIssues::ScanForOrphanedFeatureINIs();

		logger::info("Loading Settings Complete");
	} catch (const json::exception& e) {
		logger::info("General JSON error accessing settings: {}; recreating config", e.what());
		Save(a_configMode);
		errorDetected = true;
	} catch (const std::exception& e) {
		logger::info("General error accessing settings: {}; recreating config", e.what());
		Save(a_configMode);
		errorDetected = true;
	}
	if (errorDetected && a_allowReload)
		Load(a_configMode, false);
}

void State::SaveToJson(
	nlohmann::json& settings,
	bool a_includeMissingUnloadedFeatures)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto shaderCache = globals::shaderCache;

	globals::menu->Save(settings["Menu"]);

	json advanced;
	advanced["Dump Shaders"] = shaderCache->IsDump();
	advanced["Log Level"] = logLevel;
	advanced["Shader Defines"] = GetShaderDefinesSnapshot()->canonicalText;
	advanced["Compiler Threads"] = shaderCache->compilationThreadCount;
	advanced["Background Compiler Threads"] = shaderCache->backgroundCompilationThreadCount;
	advanced["Use FileWatcher"] = shaderCache->UseFileWatcher();
	advanced["Frame Annotations"] = frameAnnotations;
	advanced["Refraction Scale"] = refractionScale;
	advanced["Partial Precision"] = enablePartialPrecision.load(std::memory_order_relaxed);
	settings["Advanced"] = advanced;

	json general;
	general["Enable Shaders"] = shaderCache->IsEnabled();
	general["Enable Disk Cache"] = shaderCache->IsDiskCache();
	general["Skip Unchanged Shaders"] = shaderCache->IsSkipUnchangedShaders();
	general["Enable Async"] = shaderCache->IsAsync();
	general["Language"] = I18n::GetSingleton()->GetCurrentLocale();

	settings["General"] = general;

	json originalShaders;
	ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
		originalShaders[magic_enum::enum_name(type)] = enabledClasses[classIndex];
	});
	settings["Replace Original Shaders"] = originalShaders;

	json disabledFeaturesJson;
	for (const auto& [featureName, isDisabled] : disabledFeatures) {
		disabledFeaturesJson[featureName] = isDisabled;
	}
	settings["Disable at Boot"] = disabledFeaturesJson;

	settings["Version"] = std::string{ Plugin::VERSION_LABEL };

	// Save feature settings without performing disk I/O. Preserve an existing
	// section for any feature which is unavailable in this session.
	for (auto* feature : Feature::GetFeatureList()) {
		const std::string settingsName = feature->GetName();
		if (feature->loaded || (a_includeMissingUnloadedFeatures && !settings.contains(settingsName)))
			feature->Save(settings);
	}
}

void State::LoadFromJson(nlohmann::json& settings)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto shaderCache = globals::shaderCache;

	// Load Menu settings
	if (settings.contains("Menu") && settings["Menu"].is_object()) {
		globals::menu->Load(settings["Menu"]);
	}

	if (settings.contains("Advanced") && settings["Advanced"].is_object()) {
		json& advanced = settings["Advanced"];
		// The compilation pool is constructed at the responsive hardware-derived
		// ceiling. Clamp persisted legacy/preset values to the number of workers that
		// actually exists instead of accepting ineffective or CPU-hostile values.
		const auto maxCompilerThreads = std::max(
			1,
			static_cast<int32_t>(shaderCache->compilationPool.get_thread_count()));
		if (advanced.contains("Dump Shaders") && advanced["Dump Shaders"].is_boolean())
			shaderCache->SetDump(advanced["Dump Shaders"]);
		if (advanced.contains("Log Level") && advanced["Log Level"].is_number_integer()) {
			const auto rawLogLevel = advanced["Log Level"].get<int64_t>();
			const auto newLogLevel = (rawLogLevel >= 0 && rawLogLevel <= static_cast<int64_t>(spdlog::level::off)) ?
			                             magic_enum::enum_cast<spdlog::level::level_enum>(static_cast<int>(rawLogLevel)).value_or(spdlog::level::info) :
			                             spdlog::level::info;
			if (newLogLevel != logLevel)
				SetLogLevel(newLogLevel);
		}
		if (advanced.contains("Shader Defines") && advanced["Shader Defines"].is_string())
			SetDefines(advanced["Shader Defines"]);
		if (advanced.contains("Compiler Threads") && advanced["Compiler Threads"].is_number_integer())
			shaderCache->compilationThreadCount = std::clamp(advanced["Compiler Threads"].get<int32_t>(), 1, maxCompilerThreads);
		if (advanced.contains("Background Compiler Threads") && advanced["Background Compiler Threads"].is_number_integer())
			shaderCache->backgroundCompilationThreadCount = std::clamp(advanced["Background Compiler Threads"].get<int32_t>(), 1, maxCompilerThreads);
		if (advanced.contains("Use FileWatcher") && advanced["Use FileWatcher"].is_boolean())
			shaderCache->SetFileWatcher(advanced["Use FileWatcher"]);
		if (advanced.contains("Frame Annotations") && advanced["Frame Annotations"].is_boolean())
			frameAnnotations = advanced["Frame Annotations"];
		if (advanced.contains("Refraction Scale") && advanced["Refraction Scale"].is_number())
			refractionScale = std::clamp(advanced["Refraction Scale"].get<float>(), 0.0f, 2.0f);
		if (advanced.contains("Partial Precision") && advanced["Partial Precision"].is_boolean())
			enablePartialPrecision.store(advanced["Partial Precision"].get<bool>(), std::memory_order_relaxed);
	}

	if (settings.contains("General") && settings["General"].is_object()) {
		json& general = settings["General"];
		if (general.contains("Enable Shaders") && general["Enable Shaders"].is_boolean())
			shaderCache->SetEnabled(general["Enable Shaders"]);
		if (general.contains("Enable Disk Cache") && general["Enable Disk Cache"].is_boolean())
			shaderCache->SetDiskCache(general["Enable Disk Cache"]);
		if (general.contains("Skip Unchanged Shaders") && general["Skip Unchanged Shaders"].is_boolean())
			shaderCache->SetSkipUnchangedShaders(general["Skip Unchanged Shaders"]);
		if (general.contains("Enable Async") && general["Enable Async"].is_boolean())
			shaderCache->SetAsync(general["Enable Async"]);

		// Load i18n locale preference
		if (general.contains("Language") && general["Language"].is_string()) {
			auto locale = general["Language"].get<std::string>();
			auto* i18n = I18n::GetSingleton();
			if (locale != i18n->GetCurrentLocale()) {
				i18n->SetLocale(locale);
			}
		} else {
			// No saved language preference — auto-detect from system locale on first launch
			auto* i18n = I18n::GetSingleton();
			auto detected = i18n->DetectSystemLocale();
			if (detected != "en" && detected != i18n->GetCurrentLocale()) {
				i18n->SetLocale(detected);
				logger::info("[I18n] Auto-detected system locale: '{}'", detected);
			}
		}
	}

	if (settings.contains("Replace Original Shaders") && settings["Replace Original Shaders"].is_object()) {
		json& originalShaders = settings["Replace Original Shaders"];
		ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
			auto name = magic_enum::enum_name(type);
			if (originalShaders.contains(name) && originalShaders[name].is_boolean()) {
				enabledClasses[classIndex] = originalShaders[name];
			} else {
				logger::warn("Invalid entry for shader class '{}', using current value", name);
			}
		});
	}

	// Load feature settings (only for already-loaded features)
	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->loaded) {
			feature->ReloadSettings(settings);
		}
	}
}

bool State::Save(ConfigMode a_configMode)
{
	const std::filesystem::path configPath = GetConfigPath(a_configMode);
	const std::string configName = configPath.filename().string();
	const auto reportFailure = [&](std::string a_logMessage, std::string a_userMessage) {
		logger::warn("{}", a_logMessage);
		if (a_configMode == ConfigMode::USER && globals::menu)
			globals::menu->ReportSettingsSaveResult(false, std::move(a_userMessage));
		return false;
	};

	json settings = json::object();
	std::error_code existsError;
	const bool configExists = std::filesystem::exists(configPath, existsError);
	if (existsError) {
		return reportFailure(
			std::format("Failed to inspect settings file {}: {}", configPath.string(), existsError.message()),
			std::format("Settings were not saved because {} could not be inspected: {}", configName, existsError.message()));
	}

	if (configExists) {
		try {
			std::ifstream input(configPath, std::ios::binary);
			if (!input.is_open()) {
				return reportFailure(
					std::format("Failed to open existing settings file for reading: {}", configPath.string()),
					std::format("Settings were not saved because {} could not be read.", configName));
			}

			json existingSettings;
			input >> existingSettings;
			if (input.bad()) {
				return reportFailure(
					std::format("I/O error while reading existing settings file: {}", configPath.string()),
					std::format("Settings were not saved because an I/O error occurred while reading {}.", configName));
			}
			if (!existingSettings.is_object()) {
				return reportFailure(
					std::format("Refusing to overwrite settings file which is not a JSON object: {}", configPath.string()),
					std::format("Settings were not saved because {} is not a JSON object. Fix or remove it, then try again.", configName));
			}
			settings = std::move(existingSettings);
		} catch (const std::exception& e) {
			return reportFailure(
				std::format("Refusing to overwrite unreadable settings file {}: {}", configPath.string(), e.what()),
				std::format("Settings were not saved because {} could not be read: {}", configName, e.what()));
		}
	}

	try {
		SaveToJson(settings, a_configMode == ConfigMode::DEFAULT);
	} catch (const std::exception& e) {
		return reportFailure(
			std::format("Failed to collect settings for {}: {}", configPath.string(), e.what()),
			std::format("Settings were not saved because the active values could not be collected: {}", e.what()));
	} catch (...) {
		return reportFailure(
			std::format("Failed to collect settings for {} due to an unknown error", configPath.string()),
			"Settings were not saved because the active values could not be collected. See CommunityShaders.log.");
	}

	const auto writeResult = Util::FileHelpers::WriteJsonFileAtomic(configPath, settings, 1);
	if (!writeResult) {
		return reportFailure(
			std::format("Failed to save settings to {}: {}", configPath.string(), writeResult.errorMessage),
			std::format("Settings were not saved to {}: {}", configName, writeResult.errorMessage));
	}

	if (a_configMode == ConfigMode::USER) {
		std::vector<std::string> postSaveFailures;
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded)
				continue;
			try {
				feature->OnSettingsSaved();
			} catch (const std::exception& e) {
				logger::warn("Post-save handling failed for {}: {}", feature->GetName(), e.what());
				postSaveFailures.push_back(feature->GetDisplayName());
			} catch (...) {
				logger::warn("Post-save handling failed for {} due to an unknown error", feature->GetName());
				postSaveFailures.push_back(feature->GetDisplayName());
			}
		}

		const auto overrideFailures = SaveUserOverrides(settings);
		if (!postSaveFailures.empty() || !overrideFailures.empty()) {
			std::string failureDetails;
			if (!overrideFailures.empty())
				failureDetails = std::format("override customizations for {}", JoinSettingLayerNames(overrideFailures));
			if (!postSaveFailures.empty()) {
				if (!failureDetails.empty())
					failureDetails += "; ";
				failureDetails += std::format("post-save handling for {}", JoinSettingLayerNames(postSaveFailures));
			}

			return reportFailure(
				std::format("Settings save incomplete after writing {}: failed {}", configPath.string(), failureDetails),
				std::format("{} was written, but {} could not be persisted. Try saving again; see CommunityShaders.log.", configName, failureDetails));
		}

		if (globals::menu)
			globals::menu->ReportSettingsSaveResult(true, std::format("Settings saved successfully to {}.", configName));
	}

	logger::info("Saved settings to {}", configPath.string());
	return true;
}

bool State::ValidateCache(CSimpleIniA& a_ini)
{
	bool valid = true;
	for (auto* feature : Feature::GetFeatureList())
		valid = valid && feature->ValidateCache(a_ini);
	return valid;
}

void State::WriteDiskCacheInfo(CSimpleIniA& a_ini)
{
	for (auto* feature : Feature::GetFeatureList())
		feature->WriteDiskCacheInfo(a_ini);
}

void State::SetLogLevel(spdlog::level::level_enum a_level)
{
	logLevel = a_level;
	spdlog::set_level(logLevel);
	spdlog::flush_on(logLevel);
	logger::info("Log Level set to {} ({})", magic_enum::enum_name(logLevel), magic_enum::enum_integer(logLevel));
}

spdlog::level::level_enum State::GetLogLevel()
{
	return logLevel;
}

void State::SetDefines(std::string a_defines)
{
	ShaderDefinesSnapshot snapshot;
	auto defines = pystring::split(a_defines, ";");
	for (const auto& define : defines) {
		auto cleanedDefine = pystring::strip(define);
		auto token = pystring::split(cleanedDefine, "=");
		if (token.empty() || token[0].empty())
			continue;
		if (token.size() > 2) {
			logger::warn("Define string has too many '='; ignoring {}", define);
			continue;
		}
		auto name = pystring::strip(token[0]);
		std::string definition;
		if (token.size() == 2) {
			definition = pystring::strip(token[1]);
		}
		if (!snapshot.canonicalText.empty())
			snapshot.canonicalText += ";";
		snapshot.canonicalText += cleanedDefine;
		snapshot.defines.emplace_back(std::move(name), std::move(definition));
	}

	auto immutableSnapshot =
		std::make_shared<const ShaderDefinesSnapshot>(std::move(snapshot));
	shaderDefinesSnapshot.store(immutableSnapshot, std::memory_order_release);
	shaderDefinesGeneration.fetch_add(1, std::memory_order_release);
	logger::debug("Shader Defines set to {}", immutableSnapshot->canonicalText);
}

std::shared_ptr<const State::ShaderDefinesSnapshot> State::GetShaderDefinesSnapshot() const
{
	return shaderDefinesSnapshot.load(std::memory_order_acquire);
}

bool State::ShaderEnabled(const RE::BSShader::Type a_type)
{
	auto index = magic_enum::enum_integer(a_type) + 1;
	if (index < sizeof(enabledClasses)) {
		return enabledClasses[index];
	}
	return false;
}

bool State::IsShaderEnabled(const RE::BSShader& a_shader)
{
	return ShaderEnabled(a_shader.shaderType.get());
}

bool State::IsDeveloperMode()
{
	return GetLogLevel() <= spdlog::level::debug;
}

void State::ModifyRenderTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties& a_properties)
{
	a_properties.supportUnorderedAccess = true;
	logger::debug("Adding UAV access to {}", magic_enum::enum_name(a_target));
}

void State::CheckTypedUAVLoadSupport()
{
	auto device = globals::d3d::device;
	if (!device) {
		logger::warn("[TypedUAVLoad] Device unavailable; skipping format support probe.");
		return;
	}

	// Formats this codebase does typed UAV loads on (RWTexture<T> read via subscript).
	// Identified by static analysis; keep in sync with new typed reads.
	// All require the optional D3D11 feature D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD —
	// guaranteed only for R32_FLOAT/R32_UINT/R32_SINT, otherwise gated by
	// D3D11_FEATURE_DATA_D3D11_OPTIONS2.TypedUAVLoadAdditionalFormats (FL12+).
	struct FormatEntry
	{
		DXGI_FORMAT format;
		const char* name;
		const char* usage;
	};
	static const FormatEntry kFormats[] = {
		{ DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT", "Dynamic Cubemaps (envCapture/Raw/Position) — non-HDR" },
		{ DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", "Dynamic Cubemaps (HDR), Skylighting outProbeArray" },
		{ DXGI_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM", "Grass Collision (collisionTexture)" },
		{ DXGI_FORMAT_R16G16_UNORM, "R16G16_UNORM", "Terrain Shadows (RWTexShadowHeights)" },
		{ DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM", "HDR Display UI brightness (uiTexture)" },
		{ DXGI_FORMAT_R8_UINT, "R8_UINT", "Skylighting accumulation frames (outAccumFramesArray)" },
		{ DXGI_FORMAT_R16_FLOAT, "R16_FLOAT", "Vanilla volumetric lighting density (DensityRW)" },
	};

	bool anyUnsupported = false;
	logger::info("[TypedUAVLoad] Probing per-format UAV typed-load support:");
	for (const auto& entry : kFormats) {
		D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support2{};
		support2.InFormat = entry.format;
		HRESULT hr = device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support2, sizeof(support2));
		if (FAILED(hr)) {
			logger::warn("[TypedUAVLoad] {} ({}): CheckFeatureSupport failed (hr=0x{:08x})", entry.name, entry.usage, static_cast<uint32_t>(hr));
			anyUnsupported = true;
			continue;
		}
		const bool supported = (support2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;
		if (supported) {
			logger::info("[TypedUAVLoad] {} — supported ({})", entry.name, entry.usage);
		} else {
			logger::warn("[TypedUAVLoad] {} — UNSUPPORTED ({})", entry.name, entry.usage);
			anyUnsupported = true;
		}
	}

	if (anyUnsupported) {
		logger::warn(
			"[TypedUAVLoad] One or more required formats lack typed-UAV-load support on this GPU. "
			"Affected features will read undefined data and may produce visual artifacts. "
			"Consider disabling: Dynamic Cubemaps, Grass Collision, Terrain Shadows, Skylighting, HDR Display.");
	}
}

void State::SetupResources()
{
	for (auto& c : drawCalls)
		c = 0;
	for (auto& c : smoothDrawCalls)
		c = 0;
	for (auto& ft : frameTimePerType)
		ft = 0.0f;
	for (auto& sft : smoothFrameTimePerType)
		sft = 0.0f;

	frameTimingActive = false;

	auto renderer = globals::game::renderer;

	permutationCB = new ConstantBuffer(ConstantBufferDesc<PermutationCB>());
	sharedDataCB = new ConstantBuffer(ConstantBufferDesc<SharedDataCB>());

	auto [data, size] = GetFeatureBufferData(false);
	(void)data;
	// Feature data is dynamically packed and only needs D3D11's 16-byte CB alignment.
	featureDataCB = new ConstantBuffer(ConstantBufferDesc((uint32_t)size, true, 16));

	// Grab main texture to get resolution
	D3D11_TEXTURE2D_DESC texDesc{};
	renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture->GetDesc(&texDesc);

	globals::d3d::context->QueryInterface(__uuidof(pPerf), reinterpret_cast<void**>(&pPerf));

	featureLevel = globals::d3d::device->GetFeatureLevel();

	tracyCtx = TracyD3D11Context(globals::d3d::device, globals::d3d::context);
#ifdef TRACY_ENABLE
	Feature::SetTracyCtx(tracyCtx);
#endif

	globals::profiler->Initialize(globals::d3d::device, globals::d3d::context);

	if (frameAnnotations) {
		globals::profiler->SetPerfEventCallbacks(
			[this](std::string_view name) { BeginPerfEvent(name); },
			[this](std::string_view) { EndPerfEvent(); });
	} else {
		globals::profiler->SetPerfEventCallbacks({}, {});
	}
}

void State::ModifyShaderLookup(const RE::BSShader& a_shader, uint& a_vertexDescriptor, uint& a_pixelDescriptor, bool a_forceDeferred)
{
	auto deferred = globals::deferred;

	if (a_shader.shaderType.get() != RE::BSShader::Type::Utility && a_shader.shaderType.get() != RE::BSShader::Type::ImageSpace) {
		switch (a_shader.shaderType.get()) {
		case RE::BSShader::Type::Lighting:
			{
				a_vertexDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::AmbientSpecular |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::DoAlphaTest |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::ShadowDir |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::DefShadow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::CharacterLight |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::RimLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::SoftLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::BackLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::Specular |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::AnisoLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::BaseObjectIsSnow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::Snow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::TruePbr);

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::AmbientSpecular |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::ShadowDir |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::DefShadow |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::CharacterLight |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::BaseObjectIsSnow);
				if (a_pixelDescriptor & (uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask) {
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::LightingShaderFlags::DoAlphaTest;
					a_pixelDescriptor &= ~(uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask;
				}

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::Snow);

				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::LightingShaderFlags::Deferred;

				{
					uint32_t technique = 0x3F & (a_vertexDescriptor >> 24);
					if (technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Glowmap ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Parallax ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Facegen ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::FacegenRGBTint ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjects ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjectHD ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::MultiIndexSparkle ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Hair)
						a_vertexDescriptor &= ~(0x3F << 24);
				}

				{
					uint32_t technique = 0x3F & (a_pixelDescriptor >> 24);
					if (technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Glowmap)
						a_pixelDescriptor &= ~(0x3F << 24);
				}
			}
			break;
		case RE::BSShader::Type::Water:
			{
				auto flags = ~((uint32_t)SIE::ShaderCache::WaterShaderFlags::Reflections |
							   (uint32_t)SIE::ShaderCache::WaterShaderFlags::Cubemap |
							   (uint32_t)SIE::ShaderCache::WaterShaderFlags::Interior);
				a_vertexDescriptor &= flags;
				a_pixelDescriptor &= flags;
			}
			break;
		case RE::BSShader::Type::Effect:
			{
				auto flags = ~((uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToColor |
							   (uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToAlpha |
							   (uint32_t)SIE::ShaderCache::EffectShaderFlags::IgnoreTexAlpha);
				a_vertexDescriptor &= flags;
				a_pixelDescriptor &= flags;

				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::EffectShaderFlags::Deferred;
			}
			break;
		case RE::BSShader::Type::DistantTree:
			{
				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::DistantTreeShaderFlags::Deferred;
			}
			break;
		case RE::BSShader::Type::Sky:
			{
				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= 256;
			}
			break;
		case RE::BSShader::Type::Grass:
			{
				auto technique = a_vertexDescriptor & 0xF;
				auto flags = a_vertexDescriptor & ~0xF;
				if (technique == static_cast<uint32_t>(SIE::ShaderCache::GrassShaderTechniques::TruePbr)) {
					technique = 0;
				}
				a_vertexDescriptor = flags | technique;
			}
			break;
		}
	}
}

void State::BeginPerfEvent(std::string_view title)
{
#ifdef TRACY_ENABLE
	// Use dynamic source location so Tracy displays the title as the zone name
	// rather than the static function name "BeginPerfEvent".
	const auto srcloc = ___tracy_alloc_srcloc_name(
		static_cast<uint32_t>(__LINE__),
		__FILE__, sizeof(__FILE__) - 1,
		__func__, sizeof(__func__) - 1,
		title.data(), title.size(),
		0);
	const TracyCZoneCtx ctx = ___tracy_emit_zone_begin_alloc(srcloc, true);
	s_tracyPerfZones.push_back(ctx);
#endif
	pPerf->BeginEvent(std::wstring(title.begin(), title.end()).c_str());
}

void State::EndPerfEvent()
{
#ifdef TRACY_ENABLE
	if (!s_tracyPerfZones.empty()) {
		TracyCZoneEnd(s_tracyPerfZones.back());
		s_tracyPerfZones.pop_back();
	} else {
		logger::warn("EndPerfEvent called without a matching BeginPerfEvent");
	}
#endif
	pPerf->EndEvent();
}

void State::SetPerfMarker(std::string_view title)
{
	pPerf->SetMarker(std::wstring(title.begin(), title.end()).c_str());
}

void State::SetAdapterDescription(const std::wstring& description)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	adapterDescription = converter.to_bytes(description);
}

void State::UpdateSharedData([[maybe_unused]] bool a_inWorld, [[maybe_unused]] bool a_prepass)
{
	{
		SharedDataCB data{};

		const auto shaderManager = globals::game::smState;
		const RE::NiTransform& dalcTransform = shaderManager->directionalAmbientTransform;

		auto shadowSceneNode = shaderManager->shadowSceneNode[0];
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto& lightRuntimeData = dirLight->GetLightRuntimeData();
		data.DirLightColor = { lightRuntimeData.diffuse.red, lightRuntimeData.diffuse.green, lightRuntimeData.diffuse.blue, 1.0f };
		data.DirLightColor *= lightRuntimeData.fade;

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		data.DirLightColor *= imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale;

		const auto& direction = dirLight->GetWorldDirection();
		data.DirLightDirection = { -direction.x, -direction.y, -direction.z, 0.0f };
		data.DirLightDirection.Normalize();

		data.CameraData = Util::GetCameraData();
		data.BufferDim = { (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight, 1.0f / (float)globals::game::graphicsState->screenWidth, 1.0f / (float)globals::game::graphicsState->screenHeight };
		data.Timer = timer;

		auto temporal = Util::GetTemporal();

		data.FrameCount = frameCount * temporal;
		data.FrameCountAlwaysActive = frameCount;

		if (a_inWorld) {
			for (int i = -2; i <= 2; i++) {
				for (int k = -2; k <= 2; k++) {
					int waterTile = (i + 2) + ((k + 2) * 5);
					data.WaterData[waterTile] = Util::TryGetWaterData((float)i * 4096.0f, (float)k * 4096.0f);
				}
			}
		}

		data.WaterSystemHeight = -RE::NI_INFINITY;

		data.InInterior = Util::IsInterior();
		data.HasDirectionalShadows = HasDirectionalShadows();

		if (globals::game::sky)
			data.HideSky = globals::game::sky->flags.any(RE::Sky::Flags::kHideSky);
		else
			data.HideSky = false;

		data.InMapMenu = isMapMenuOpen;

		auto& upscaling = globals::features::upscaling;

		if (upscaling.loaded) {
			auto upscaleMethod = upscaling.GetUpscaleMethod();
			if (temporal && upscaleMethod != Upscaling::UpscaleMethod::kTAA) {
				float2 screenSz{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
				auto renderSize = Util::ConvertToDynamic(screenSz, true);
				data.MipBias = std::log2f(renderSize.x / screenSz.x);
				if (upscaleMethod == Upscaling::UpscaleMethod::kDLSS)
					data.MipBias -= 1.0f;
			} else {
				data.MipBias = 0;
			}
		} else {
			data.MipBias = 0;
		}

		data.RefractionScale = refractionScale;
		const auto& volumetricShadows = globals::features::volumetricShadows;
		data.VolumetricShadowsEnabled = volumetricShadows.loaded && volumetricShadows.settings.Enabled;

		if (auto sky = globals::game::sky) {
			// Process sun
			if (auto sun = sky->sun; sun && sun->root && sky->root) {
				const auto& sunPos = sun->root->world.translate;
				const auto& skyPos = sky->root->world.translate;
				float3 sunDirection = { sunPos.x - skyPos.x, sunPos.y - skyPos.y, sunPos.z - skyPos.z };
				sunDirection.Normalize();
				data.SunDirection = { sunDirection.x, sunDirection.y, sunDirection.z, 0.0f };

				if (sun->sunBase) {
					if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(sun->sunBase->GetGeometryRuntimeData().shaderProperty.get()))
						data.SunColor = { prop->kBlendColor.red * prop->kBlendColor.alpha, prop->kBlendColor.green * prop->kBlendColor.alpha, prop->kBlendColor.blue * prop->kBlendColor.alpha, prop->kBlendColor.alpha };
				}
			}

			if (auto masser = sky->masser) {
				auto dir = Util::Moon::GetDirection(masser, moonAndStarsLoaded);
				data.MasserDirection = { dir.x, dir.y, dir.z, 0.0f };
				data.MasserColor = Util::Moon::GetBlendColor(masser, Util::Moon::MasserBaseColor, globals::features::skySync.settings.NewMoonIntensity, globals::features::skySync.settings.CrescentMoonIntensity, globals::features::skySync.settings.FullMoonIntensity);
			}

			if (auto secunda = sky->secunda) {
				auto dir = Util::Moon::GetDirection(secunda, moonAndStarsLoaded);
				data.SecundaDirection = { dir.x, dir.y, dir.z, 0.0f };
				data.SecundaColor = Util::Moon::GetBlendColor(secunda, Util::Moon::SecundaBaseColor, globals::features::skySync.settings.NewMoonIntensity, globals::features::skySync.settings.CrescentMoonIntensity, globals::features::skySync.settings.FullMoonIntensity);
			}
		}

		// DALC to SH
		const auto& m = dalcTransform.rotate;
		const auto& t = dalcTransform.translate;
		float3 dalcColors[6];
		dalcColors[0] = float3{ m.entry[0][0] + t.x, m.entry[1][0] + t.y, m.entry[2][0] + t.z };     // +X
		dalcColors[1] = float3{ -m.entry[0][0] + t.x, -m.entry[1][0] + t.y, -m.entry[2][0] + t.z };  // -X
		dalcColors[2] = float3{ m.entry[0][1] + t.x, m.entry[1][1] + t.y, m.entry[2][1] + t.z };     // +Y
		dalcColors[3] = float3{ -m.entry[0][1] + t.x, -m.entry[1][1] + t.y, -m.entry[2][1] + t.z };  // -Y
		dalcColors[4] = float3{ m.entry[0][2] + t.x, m.entry[1][2] + t.y, m.entry[2][2] + t.z };     // +Z
		dalcColors[5] = float3{ -m.entry[0][2] + t.x, -m.entry[1][2] + t.y, -m.entry[2][2] + t.z };  // -Z

		SphericalHarmonics::SH2Color dalcSH = SphericalHarmonics::DALCToSH(dalcColors);
		data.AmbientSHR = { dalcSH.r.c0, dalcSH.r.c1[0], dalcSH.r.c1[1], dalcSH.r.c1[2] };
		data.AmbientSHG = { dalcSH.g.c0, dalcSH.g.c1[0], dalcSH.g.c1[1], dalcSH.g.c1[2] };
		data.AmbientSHB = { dalcSH.b.c0, dalcSH.b.c1[0], dalcSH.b.c1[1], dalcSH.b.c1[2] };

		data.HDRData = globals::features::hdrDisplay.GetSharedDataHDR();

		sharedDataCB->Update(data);
	}

	UpdateFeatureData(a_inWorld);

	auto* srv = Util::GetCurrentSceneDepthSRV(true);
	globals::d3d::context->PSSetShaderResources(17, 1, &srv);
}

void State::UpdateFeatureData(bool a_inWorld)
{
	if (!featureDataCB)
		return;

	auto [data, size] = GetFeatureBufferData(a_inWorld);
	featureDataCB->Update(data, size);
}

void State::ClearDisabledFeatures()
{
	disabledFeatures.clear();
}

bool State::SetFeatureDisabled(const std::string& featureName, bool isDisabled)
{
	if (IsForcedDisabledFeature(featureName))
		isDisabled = true;

	bool wasPreviouslyDisabled = disabledFeatures.count(featureName) > 0 ? disabledFeatures[featureName] : false;  // Properly check if it exists
	disabledFeatures[featureName] = isDisabled;

	// Log the change
	if (wasPreviouslyDisabled != isDisabled) {
		logger::info("Set feature '{}' to: {}", featureName, isDisabled ? "Disabled" : "Enabled");
	} else {
		logger::info("Feature '{}' state remains: {}", featureName, isDisabled ? "Disabled" : "Enabled");
	}

	return disabledFeatures[featureName];  // Return the current state instead of the input parameter
}

bool State::IsFeatureDisabled(const std::string& featureName)
{
	return disabledFeatures.contains(featureName) && disabledFeatures[featureName];
}

std::unordered_map<std::string, bool>& State::GetDisabledFeatures()
{
	return disabledFeatures;
}

// --- Utility Method Implementations ---

float State::GetTotalSmoothedDrawCalls() const
{
	return static_cast<float>(smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);
}

void State::LoadTheme()
{
	// Load the active preset from SettingsUser.json (already read during State::Load)
	auto presetName = globals::menu->GetSettings().SelectedThemePreset;
	if (presetName.empty()) {
		logger::info("No active theme preset set; skipping preset load");
		return;
	}

	// Ensure default themes exist and theme manager has discovered themes
	globals::menu->CreateDefaultThemes();
	auto themeManager = ThemeManager::GetSingleton();
	if (themeManager && !themeManager->IsDiscovered()) {
		themeManager->DiscoverThemes();
	}

	logger::info("Loading active theme preset: '{}'", presetName);
	if (!globals::menu->LoadThemePreset(presetName)) {
		logger::warn("Failed to load preset '{}', attempting to fall back to 'Default'", presetName);
		if (globals::menu->LoadThemePreset("Default")) {
			globals::menu->GetSettings().SelectedThemePreset = "Default";
			logger::info("Fallback to 'Default' theme succeeded");
		} else {
			logger::warn("Fallback to 'Default' theme failed");
		}
	}
}

bool State::HasDirectionalShadows() const
{
	return !Util::IsInterior() || globals::features::interiorSun.IsActiveInteriorSun();
}
