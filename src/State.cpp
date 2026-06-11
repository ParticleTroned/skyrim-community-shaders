#include "State.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <codecvt>
#include <cstring>
#include <format>
#include <limits>
#include <thread>

#include <pystring/pystring.h>
#include <RE/B/BGSSaveLoadGame.h>

#include "Deferred.h"
#include "FeatureIssues.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/FoveatedCommon.h"
#include "Features/InteriorSun.h"
#include "Features/LightLimitFix.h"
#include "Features/PerformanceOverlay.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Features/WaterEffects.h"
#include "Features/WetnessEffects.h"
#include "Features/Wetterness.h"
#include "Features/WeatherEditor.h"
#include "Menu.h"
#include "Profiler.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "ShaderCache.h"
#include "TruePBR.h"
#include "Utils/FileSystem.h"
#include "Utils/OpenCompositeInterop.h"
#include "Utils/SphericalHarmonics.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#ifdef TRACY_ENABLE
static thread_local std::vector<TracyCZoneCtx> s_tracyPerfZones;
#endif

namespace
{
	static constexpr std::string_view kForcedDisableAtBootFeatures[] = {
		"UnifiedWater"
	};
	static constexpr const char* kSharedDataLayoutCacheSection = "SharedData";
	static constexpr const char* kSharedDataLayoutCacheKey = "Layout";

	std::string GetSharedDataLayoutCacheValue()
	{
		return std::format(
			"size:{};refraction:{};ambient:{};fov0:{};fovmodes:{};fovoffsets:{}",
			sizeof(State::SharedDataCB),
			offsetof(State::SharedDataCB, RefractionScale),
			offsetof(State::SharedDataCB, AmbientSHR),
			offsetof(State::SharedDataCB, VRFoveationData0),
			offsetof(State::SharedDataCB, VRFoveationModes),
			offsetof(State::SharedDataCB, VRFoveationCenterOffsets));
	}

	void StoreMax(std::atomic_uint32_t& a_target, uint32_t a_value)
	{
		uint32_t current = a_target.load(std::memory_order_acquire);
		while (current < a_value) {
			if (a_target.compare_exchange_weak(current, a_value, std::memory_order_acq_rel, std::memory_order_acquire)) {
				return;
			}
		}
	}

	void ForceDisableAtBootFeature(json& a_disabledFeaturesJson, std::string_view a_featureName)
	{
		const std::string featureKey(a_featureName);
		if (!a_disabledFeaturesJson.value(featureKey, false)) {
			logger::info("Feature '{}' is force-disabled at boot by this build", a_featureName);
		}
		a_disabledFeaturesJson[featureKey] = true;
	}

	void TraceOCUExternalMipBiasState(const Util::OCUExternalUpscalerState& a_state)
	{
		static bool logged = false;
		static float previousMipBias = std::numeric_limits<float>::quiet_NaN();
		static float previousRenderScale = std::numeric_limits<float>::quiet_NaN();
		static uint32_t previousMethod = std::numeric_limits<uint32_t>::max();
		static uint32_t previousFlags = std::numeric_limits<uint32_t>::max();

		const bool changed =
			!logged ||
			std::abs(previousMipBias - a_state.mipBias) > 0.0005f ||
			std::abs(previousRenderScale - a_state.renderScale) > 0.0005f ||
			previousMethod != a_state.method ||
			previousFlags != a_state.flags;

		if (!changed)
			return;

		logger::info(
			"[MipBiasTrace] source=OpenCompositeUnleashedSharedState renderScale={:.3f} mipBias={:.3f} method={} flags=0x{:X}",
			a_state.renderScale,
			a_state.mipBias,
			a_state.method,
			a_state.flags);

		logged = true;
		previousMipBias = a_state.mipBias;
		previousRenderScale = a_state.renderScale;
		previousMethod = a_state.method;
		previousFlags = a_state.flags;
	}

	void ApplyDefaultDisableAtBootSettings(json& a_disabledFeaturesJson)
	{
		static constexpr std::pair<std::string_view, bool> defaultDisableAtBootSettings[] = {
			{ WetnessEffects::kShortName, false }
		};

		for (const auto& [featureName, isDisabled] : defaultDisableAtBootSettings) {
			if constexpr (WetnessEffects::kForceDisableInAIO) {
				if (featureName == WetnessEffects::kShortName) {
					continue;
				}
			}

			const std::string featureKey(featureName);
			if (!a_disabledFeaturesJson.contains(featureKey)) {
				a_disabledFeaturesJson[featureKey] = isDisabled;
				logger::info("Default boot state for '{}' set to {}", featureName, isDisabled ? "Disabled" : "Enabled");
			}
		}
	}

	bool IsForcedDisableAtBootFeature(std::string_view a_featureName)
	{
		if constexpr (WetnessEffects::kForceDisableInAIO) {
			if (a_featureName == WetnessEffects::kShortName) {
				return true;
			}
		}
		return std::ranges::find(kForcedDisableAtBootFeatures, a_featureName) != std::end(kForcedDisableAtBootFeatures);
	}

	void ApplyForcedDisableAtBootSettings(json& a_disabledFeaturesJson)
	{
		// Build-level kill switches: keep features registered for cache/config handling
		// while preventing load, hooks, resources, prepass, and shader defines.
		for (const auto featureName : kForcedDisableAtBootFeatures) {
			ForceDisableAtBootFeature(a_disabledFeaturesJson, featureName);
		}
		if constexpr (WetnessEffects::kForceDisableInAIO) {
			ForceDisableAtBootFeature(a_disabledFeaturesJson, WetnessEffects::kShortName);
		}
	}

	float2 GetMainRenderTargetSize()
	{
		auto* renderer = globals::game::renderer;
		if (!renderer) {
			return {};
		}

		const auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		if (!main.texture) {
			return {};
		}

		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);
		return { static_cast<float>(texDesc.Width), static_cast<float>(texDesc.Height) };
	}
}

void State::Draw()
{
	ZoneScoped;

	auto shaderCache = globals::shaderCache;
	auto deferred = globals::deferred;
	auto& terrainBlending = globals::features::terrainBlending;
	auto& terrainHelper = globals::features::terrainHelper;
	auto& cloudShadows = globals::features::cloudShadows;
	auto& weatherEditor = globals::features::weatherEditor;
	auto& truePBR = globals::features::truePBR;
	auto context = globals::d3d::context;

	if (shaderCache->IsEnabled()) {
		// Process deferred cell transitions (interior detection)
		SceneSettingsManager::GetSingleton()->Update();
		globals::features::upscaling.ApplyPendingPerfModeRenderTargetRecreate("State::Draw");
		if (globals::features::upscaling.ShouldSkipVRRenderScaleRelatchFrame()) {
			updateShader = false;
			return;
		}

		if (pendingPostLoadRuntimeReset) {
			globals::OnDataLoaded();
			WeatherManager::GetSingleton()->ClearCache();
			globals::features::lightLimitFix.Reset();
			globals::features::interiorSun.isInteriorWithSun = false;
			globals::features::wetterness.ResetRuntimeStateAfterGameLoad();
			pendingPostLoadRuntimeReset = false;
			logger::info("Applied deferred post-load runtime reset");
		}

		if (weatherEditor.loaded) {
			ZoneScopedN("WeatherManager::UpdateFeatures");
			WeatherManager::GetSingleton()->UpdateFeatures();
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
			ZoneScopedN("TerrainHelper::SetShaderResouces");
			terrainHelper.SetShaderResouces(context);
		}

		if (truePBR.loaded) {
			ZoneScopedN("TruePBR::SetShaderResouces");
			truePBR.SetShaderResouces(context);
		}

		if (permutationData != permutationDataPrevious) {
			permutationCB->Update(permutationData);
			permutationDataPrevious = permutationData;
		}

		if (currentShader && updateShader) {
			if (currentShader->shaderType.get() == RE::BSShader::Type::Utility) {
				if (currentPixelDescriptor & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmask)) {
					deferred->CopyShadowData();
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

bool State::IsSaveLoadSafeModeActive() const
{
	return saveLoadSafeModeActive.load(std::memory_order_acquire);
}

bool State::IsPersistentMutationBlocked() const
{
	return persistentMutationBlocked.load(std::memory_order_acquire);
}

void State::BeginSaveLoadSafeMode(uint32_t a_currentFrame)
{
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	saveLoadSafeModeStartFrame.store(currentFrame, std::memory_order_release);
	saveLoadSafeModeEndFrame.store(0, std::memory_order_release);
	saveLoadSafeModeActive.store(true, std::memory_order_release);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::ExtendSaveLoadSafeMode(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	const uint32_t endFrame = currentFrame + std::max(a_frameCount, 1u);
	if (saveLoadSafeModeStartFrame.load(std::memory_order_acquire) == 0) {
		saveLoadSafeModeStartFrame.store(currentFrame, std::memory_order_release);
	}
	StoreMax(saveLoadSafeModeEndFrame, endFrame);
	saveLoadSafeModeActive.store(true, std::memory_order_release);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::BeginPersistentMutationBlock(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	const uint32_t endFrame = currentFrame + std::max(a_frameCount, 1u);
	StoreMax(persistentMutationBlockEndFrame, endFrame);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::ExtendPersistentMutationBlock(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	const uint32_t endFrame = currentFrame + std::max(a_frameCount, 1u);
	StoreMax(persistentMutationBlockEndFrame, endFrame);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::UpdateSaveLoadSafeMode()
{
	const uint32_t currentFrame = std::max(frameCount, 1u);
	bool safeModeActive = saveLoadSafeModeActive.load(std::memory_order_acquire);

	bool engineSaveLoadActive = false;
	if (auto* saveLoad = RE::BGSSaveLoadGame::GetSingleton()) {
		engineSaveLoadActive =
			saveLoad->GetSaveGameLoading() ||
			saveLoad->GetSaveGameSaving() ||
			saveLoad->GetInitingForms() ||
			saveLoad->GetDeferInitForms() ||
			saveLoad->GetPositioningPlayerCharacter();
	}

	if (engineSaveLoadActive) {
		if (!safeModeActive) {
			saveLoadSafeModeStartFrame.store(currentFrame, std::memory_order_release);
		}
		StoreMax(saveLoadSafeModeEndFrame, currentFrame + kSaveLoadSafeModeGraceFrames);
		safeModeActive = true;
		saveLoadSafeModeActive.store(true, std::memory_order_release);
	} else if (safeModeActive) {
		const uint32_t endFrame = saveLoadSafeModeEndFrame.load(std::memory_order_acquire);
		if (endFrame != 0) {
			if (currentFrame >= endFrame) {
				safeModeActive = false;
				saveLoadSafeModeActive.store(false, std::memory_order_release);
				saveLoadSafeModeStartFrame.store(0, std::memory_order_release);
				saveLoadSafeModeEndFrame.store(0, std::memory_order_release);
			}
		} else {
			const uint32_t startFrame = saveLoadSafeModeStartFrame.load(std::memory_order_acquire);
			if (startFrame != 0 && currentFrame - startFrame >= kSaveLoadSafeModeFallbackFrames) {
				logger::warn("Save/load safe mode timed out after {} frames without a completion event", currentFrame - startFrame);
				safeModeActive = false;
				saveLoadSafeModeActive.store(false, std::memory_order_release);
				saveLoadSafeModeStartFrame.store(0, std::memory_order_release);
				saveLoadSafeModeEndFrame.store(0, std::memory_order_release);
			}
		}
	}

	uint32_t mutationBlockEndFrame = persistentMutationBlockEndFrame.load(std::memory_order_acquire);
	if (mutationBlockEndFrame != 0 && currentFrame >= mutationBlockEndFrame) {
		persistentMutationBlockEndFrame.store(0, std::memory_order_release);
		mutationBlockEndFrame = 0;
	}

	const bool mutationGraceActive = mutationBlockEndFrame != 0 && currentFrame < mutationBlockEndFrame;
	persistentMutationBlocked.store(safeModeActive || mutationGraceActive, std::memory_order_release);
}

void State::Reset()
{
	globals::profiler->EndFrame();
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
	UpdateSaveLoadSafeMode();

	if (auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton()) {
		GET_INSTANCE_MEMBER(BSImagespaceShaderApplyReflections, imageSpaceManager);

		// Disable reflections being applied to things other than water
		if (BSImagespaceShaderApplyReflections.get()) {
			BSImagespaceShaderApplyReflections->active = false;
		}
	}

	// Disable "improved" snow shader, unsupported
	if (!globals::game::isVR) {
		RE::GetINISetting("bEnableImprovedSnow:Display")->data.b = false;
	}

	activeReflections = false;
}

void State::Setup()
{
	SetupResources();

	// Probe typed UAV load support before features set up their resources, so any
	// gating logic that wants to read the log can run during feature SetupResources.
	CheckTypedUAVLoadSupport();

	Feature::ForEachLoadedFeature("SetupResources", [](Feature* feature) { feature->SetupResources(); });
	globals::deferred->SetupResources();

	// Load per-weather settings after features are setup
	WeatherManager::GetSingleton()->LoadPerWeatherSettingsFromDisk();

	// Load scene-specific settings (Interior Only, etc.)
	SceneSettingsManager::GetSingleton()->LoadAll();
}

void State::SetupRenderTargetResources()
{
	const bool stateResourcesMissing =
		!permutationCB ||
		!sharedDataCB ||
		!featureDataCB;
	const bool d3dDeviceChanged =
		setupResourcesDevice != globals::d3d::device ||
		setupResourcesContext != globals::d3d::context;

	if (stateResourcesMissing || d3dDeviceChanged) {
		Setup();
		return;
	}

	const auto mainRenderTargetSize = GetMainRenderTargetSize();
	if (mainRenderTargetSize.x > 0.0f && mainRenderTargetSize.y > 0.0f) {
		screenSize = mainRenderTargetSize;
	}
	featureLevel = globals::d3d::device->GetFeatureLevel();

	// VR render-scale relatch only needs resources tied to recreated render targets.
	// Keep disk/world discovery and full feature setup on State::Setup().
	Feature::ForEachLoadedFeature("SetupRenderTargetResources", [](Feature* feature) { feature->SetupRenderTargetResources(); });
	globals::deferred->SetupResources();
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
		ApplyDefaultDisableAtBootSettings(disabledFeaturesJson);
		ApplyForcedDisableAtBootSettings(disabledFeaturesJson);
		logger::info("Loading 'Disable at Boot' settings");

		disabledFeatures.clear();
		for (auto& [featureName, featureStatus] : disabledFeaturesJson.items()) {
			if (featureStatus.is_boolean()) {
				disabledFeatures[featureName] = featureStatus.get<bool>();
			} else {
				logger::warn("Invalid entry for feature '{}' in 'Disable at Boot', expected boolean.", featureName);
			}
		}
		for (auto* feature : Feature::GetFeatureList()) {
			try {
				const std::string featureName = feature->GetShortName();
				bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];
				if (!isDisabled) {
					logger::info("Loading Feature: '{}'", featureName);

					// Load base feature settings from merged config (default + user)
					feature->Load(settings);
					if (!feature->loaded) {
						logger::info("Feature '{}' did not finish loading; skipping post-load initialization.", featureName);
						continue;
					}

					// Register weather variables (features opt-in by implementing this)
					feature->RegisterWeatherVariables();

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
					logger::info("Feature '{}' is disabled at boot.", featureName);
				}
			} catch (const std::exception& e) {
				feature->failedLoadedMessage = feature->failedLoadedMessage.empty() ?
				                                   (feature->GetName() + " failed to load. Check CommunityShaders.log") :
				                                   (feature->failedLoadedMessage + "\n" + feature->GetName() + " failed to load. Check CommunityShaders.log");
				logger::warn("Error loading setting for feature '{}': {}", feature->GetShortName(), e.what());
			}
		}

		if (settings["Version"].is_string() && settings["Version"].get<std::string>() != Plugin::VERSION.string()) {
			logger::info("Found older config for version {}; upgrading to {}", (std::string)settings["Version"], Plugin::VERSION.string());
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

void State::SaveToJson(nlohmann::json& settings)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto shaderCache = globals::shaderCache;

	globals::menu->Save(settings["Menu"]);

	json advanced;
	advanced["Dump Shaders"] = shaderCache->IsDump();
	advanced["Log Level"] = logLevel;
	advanced["Shader Defines"] = shaderDefinesString;
	advanced["Compiler Threads"] = shaderCache->compilationThreadCount;
	advanced["Background Compiler Threads"] = shaderCache->backgroundCompilationThreadCount;
	advanced["Use FileWatcher"] = shaderCache->UseFileWatcher();
	advanced["Frame Annotations"] = frameAnnotations;
	advanced["Refraction Scale"] = refractionScale;
	advanced["PBR Metal Reflection Scale"] = pbrMetalReflectionScale;
	advanced["PBR Metal Highlight Scale"] = pbrMetalHighlightScale;
	advanced["Partial Precision"] = enablePartialPrecision.load(std::memory_order_relaxed);
	settings["Advanced"] = advanced;

	json general;
	general["Enable Shaders"] = shaderCache->IsEnabled();
	general["Enable Disk Cache"] = shaderCache->IsDiskCache();
	general["Skip Unchanged Shaders"] = shaderCache->IsSkipUnchangedShaders();
	general["Enable Async"] = shaderCache->IsAsync();

	settings["General"] = general;

	auto& upscaling = globals::features::upscaling;
	auto& upscalingJson = settings[upscaling.GetShortName()];
	upscaling.SaveSettings(upscalingJson);

	json originalShaders;
	ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
		originalShaders[magic_enum::enum_name(type)] = enabledClasses[classIndex];
	});
	settings["Replace Original Shaders"] = originalShaders;

	json disabledFeaturesJson;
	for (const auto& [featureName, isDisabled] : disabledFeatures) {
		if (IsForcedDisableAtBootFeature(featureName))
			continue;

		disabledFeaturesJson[featureName] = isDisabled;
	}
	ApplyDefaultDisableAtBootSettings(disabledFeaturesJson);
	settings["Disable at Boot"] = disabledFeaturesJson;

	settings["Version"] = Plugin::VERSION.string();

	// Save feature settings and user overrides
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	for (auto* feature : Feature::GetFeatureList()) {
		feature->Save(settings);

		// If feature has overrides, save user modifications to .user file
		const std::string featureName = feature->GetShortName();
		if (overrideManager->HasFeatureOverrides(featureName) && feature->loaded) {
			json currentSettings;
			feature->SaveSettings(currentSettings);

			// Get the merged override settings (all overrides applied to empty base)
			json overrideSettings = overrideManager->GetMergedOverrideSettings(featureName, json::object());

			// Save user override only if settings differ from override
			overrideManager->SaveUserOverride(featureName, currentSettings, overrideSettings);
		}
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
		const auto maxCompilerThreads = std::max(1, static_cast<int32_t>(std::thread::hardware_concurrency()));
		if (advanced.contains("Dump Shaders") && advanced["Dump Shaders"].is_boolean())
			shaderCache->SetDump(advanced["Dump Shaders"]);
		if (advanced.contains("Log Level") && advanced["Log Level"].is_number_integer())
			logLevel = magic_enum::enum_cast<spdlog::level::level_enum>(advanced["Log Level"].get<int>()).value_or(spdlog::level::info);
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
		if (advanced.contains("PBR Metal Reflection Scale") && advanced["PBR Metal Reflection Scale"].is_number())
			pbrMetalReflectionScale = std::clamp(advanced["PBR Metal Reflection Scale"].get<float>(), 0.0f, 2.0f);
		if (advanced.contains("PBR Metal Highlight Scale") && advanced["PBR Metal Highlight Scale"].is_number())
			pbrMetalHighlightScale = std::clamp(advanced["PBR Metal Highlight Scale"].get<float>(), 0.0f, 2.0f);
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
			feature->Load(settings);
		}
	}
}

void State::Save(ConfigMode a_configMode)
{
	std::string configPath = GetConfigPath(a_configMode);
	std::ofstream o{ configPath };

	try {
		std::filesystem::create_directories(Util::PathHelpers::GetCommunityShaderPath());
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating directory during Save ({}) : {}\n", Util::PathHelpers::GetCommunityShaderPath().string(), e.what());
		return;
	}

	// Check if the file opened successfully
	if (!o.is_open()) {
		logger::warn("Failed to open config file for saving: {}", configPath);
		return;  // Exit early if file cannot be opened
	}

	json settings;
	SaveToJson(settings);

	try {
		o << settings.dump(1);
		logger::info("Saving settings to {}", configPath);
	} catch (const std::exception& e) {
		logger::warn("Failed to write settings to file: {}. Error: {}", configPath, e.what());
	}
}

bool State::ValidateCache(CSimpleIniA& a_ini)
{
	bool valid = true;
	const auto currentSharedDataLayout = GetSharedDataLayoutCacheValue();
	if (const auto cachedSharedDataLayout = a_ini.GetValue(kSharedDataLayoutCacheSection, kSharedDataLayoutCacheKey)) {
		if (currentSharedDataLayout != cachedSharedDataLayout) {
			logger::info("Disk cache outdated: SharedData layout changed (current: {}, cached: {})",
				currentSharedDataLayout, cachedSharedDataLayout);
			valid = false;
		}
	} else {
		logger::info("Disk cache outdated: no SharedData layout key found");
		valid = false;
	}

	for (auto* feature : Feature::GetFeatureList())
		valid = feature->ValidateCache(a_ini) && valid;
	return valid;
}

void State::WriteDiskCacheInfo(CSimpleIniA& a_ini)
{
	const auto sharedDataLayout = GetSharedDataLayoutCacheValue();
	a_ini.SetValue(kSharedDataLayoutCacheSection, kSharedDataLayoutCacheKey, sharedDataLayout.c_str());
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
	shaderDefines.clear();
	shaderDefinesString = "";
	std::string name = "";
	std::string definition = "";
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
		name = pystring::strip(token[0]);
		if (token.size() == 2) {
			definition = pystring::strip(token[1]);
		}
		shaderDefinesString += pystring::strip(define) + ";";
		shaderDefines.push_back(std::pair(name, definition));
	}
	shaderDefinesString = shaderDefinesString.substr(0, shaderDefinesString.size() - 1);
	logger::debug("Shader Defines set to {}", shaderDefinesString);
}

std::vector<std::pair<std::string, std::string>>* State::GetDefines()
{
	return &shaderDefines;
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

void State::ModifyRenderTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
{
	if (globals::features::upscaling.AdjustVRRenderScaleRenderTargetProperties(a_target, a_properties)) {
		logger::debug(
			"[Upscaling] Adjusted {} render target properties to {}x{} for VR render scale.",
			magic_enum::enum_name(a_target),
			a_properties->width,
			a_properties->height);
	}

	a_properties->supportUnorderedAccess = true;
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
		{ DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", "Dynamic Cubemaps (HDR), Skylighting outProbeArray, Grass Collision (collisionTexture)" },
		{ DXGI_FORMAT_R16G16_UNORM, "R16G16_UNORM", "Terrain Shadows (RWTexShadowHeights)" },
		{ DXGI_FORMAT_R16G16_FLOAT, "R16G16_FLOAT", "VR Stereo Blend (kMOTION_VECTOR reprojection)" },
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
			"Consider disabling: Dynamic Cubemaps, Grass Collision, Terrain Shadows, Skylighting, HDR Display, VR Stereo Optimisations.");
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

	delete permutationCB;
	permutationCB = nullptr;
	delete sharedDataCB;
	sharedDataCB = nullptr;
	delete featureDataCB;
	featureDataCB = nullptr;
	pPerf = nullptr;
#ifdef TRACY_ENABLE
	if (tracyCtx) {
		TracyD3D11Destroy(tracyCtx);
		tracyCtx = nullptr;
	}
#endif

	permutationCB = new ConstantBuffer(ConstantBufferDesc<PermutationCB>());
	sharedDataCB = new ConstantBuffer(ConstantBufferDesc<SharedDataCB>());

	const auto featureDataSize = GetFeatureBufferData(false).second;
	featureDataCB = new ConstantBuffer(ConstantBufferDesc((uint32_t)featureDataSize));

	// Grab main texture to get resolution
	// VR cannot use viewport->screenWidth/Height as it's the desktop preview window's resolution and not HMD
	screenSize = GetMainRenderTargetSize();
	if (globals::d3d::context) {
		globals::d3d::context->QueryInterface(
			__uuidof(REX::W32::ID3DUserDefinedAnnotation),
			reinterpret_cast<void**>(pPerf.ReleaseAndGetAddressOf()));
	}
	globals::profiler->Initialize(globals::d3d::device, globals::d3d::context);
	if (frameAnnotations) {
		globals::profiler->SetPerfEventCallbacks(
			[this](std::string_view a_title) { BeginPerfEvent(a_title); },
			[this](std::string_view) { EndPerfEvent(); });
	} else {
		globals::profiler->SetPerfEventCallbacks({}, {});
	}

	featureLevel = globals::d3d::device->GetFeatureLevel();
	setupResourcesDevice = globals::d3d::device;
	setupResourcesContext = globals::d3d::context;

	tracyCtx = TracyD3D11Context(globals::d3d::device, globals::d3d::context);
#ifdef TRACY_ENABLE
	Feature::SetTracyCtx(tracyCtx);
#endif
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
	if (pPerf.Get())
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
	if (pPerf.Get())
		pPerf->EndEvent();
}

void State::SetPerfMarker(std::string_view title)
{
	if (pPerf.Get())
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
		Util::StoreTransform3x4NoScale(data.DirectionalAmbient, dalcTransform);

		auto shadowSceneNode = shaderManager->shadowSceneNode[0];
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto& lightRuntimeData = dirLight->GetLightRuntimeData();
		data.DirLightColor = { lightRuntimeData.diffuse.red, lightRuntimeData.diffuse.green, lightRuntimeData.diffuse.blue, 1.0f };
		data.DirLightColor *= lightRuntimeData.fade;

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		data.DirLightColor *= !globals::game::isVR ? imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale : imageSpaceManager->GetVRRuntimeData().data.baseData.hdr.sunlightScale;

		const auto& direction = dirLight->GetWorldDirection();
		data.DirLightDirection = { -direction.x, -direction.y, -direction.z, 0.0f };
		data.DirLightDirection.Normalize();

		data.CameraData = Util::GetCameraData();
		data.BufferDim = { screenSize.x, screenSize.y, 1.0f / screenSize.x, 1.0f / screenSize.y };
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

		// Fallback water height for the VR analytical mask when tile 12 returns the sentinel.
		// Uses player->GetWaterHeight() (reads relevantWaterHeight from LOADED_REF_DATA,
		// then falls back to the cell water height) when it is valid.
		// Covers both interior water (where TES::GetWaterHeight returns -NI_INFINITY) and exterior
		// partial submersion. Stored as eye-0 camera-relative Z to match WaterData[].w.
		data.WaterSystemHeight = -RE::NI_INFINITY;
		if (globals::game::isVR) {
			if (auto player = RE::PlayerCharacter::GetSingleton()) {
				auto waterSystem = globals::game::waterSystem;
				const bool waterSystemInWater = waterSystem &&
				                                (waterSystem->playerUnderwater || waterSystem->partiallyUnderwater);
				float worldHeight = player->GetWaterHeight();
				if (worldHeight <= -RE::NI_INFINITY && waterSystemInWater) {
					worldHeight = waterSystem->underwaterHeight;
				}
				if (worldHeight > -RE::NI_INFINITY) {
					auto eye0Pos = Util::GetEyePosition(0);
					data.WaterSystemHeight = worldHeight - eye0Pos.z;
				}
			}
		}

		data.InInterior = Util::IsInterior();

		if (globals::game::sky)
			data.HideSky = globals::game::sky->flags.any(RE::Sky::Flags::kHideSky);
		else
			data.HideSky = false;

		data.InMapMenu = isMapMenuOpen;

		auto& upscaling = globals::features::upscaling;
		const bool upscalingLoaded = upscaling.loaded;
		const auto upscaleMethod = upscalingLoaded ? upscaling.GetUpscaleMethod() : Upscaling::UpscaleMethod::kNONE;
		const auto renderSize = Util::ConvertToDynamic(screenSize, true);

		float computedMipBias = 0.0f;
		if (upscalingLoaded &&
		    temporal &&
		    upscaleMethod != Upscaling::UpscaleMethod::kNONE &&
		    upscaleMethod != Upscaling::UpscaleMethod::kTAA &&
		    screenSize.x > 0.0f &&
		    renderSize.x > 0.0f) {
			computedMipBias = std::log2f(renderSize.x / screenSize.x);
			if (upscaleMethod == Upscaling::UpscaleMethod::kDLSS)
				computedMipBias -= 1.0f;
		}

		Util::OCUExternalUpscalerState externalMipBiasState{};
		const bool externalOpenCompositeMipBias =
			globals::game::isVR &&
			upscalingLoaded &&
			Util::TryReadOCUExternalUpscalerState(externalMipBiasState);

		data.MipBias = externalOpenCompositeMipBias ? externalMipBiasState.mipBias : computedMipBias;
		if (externalOpenCompositeMipBias)
			TraceOCUExternalMipBiasState(externalMipBiasState);
		data.RefractionScale = refractionScale;
		data.PBRMetalReflectionScale = pbrMetalReflectionScale;
		data.PBRMetalHighlightScale = pbrMetalHighlightScale;

		data.SSSHumanMaleIntensity = sssHumanMaleIntensity;
		data.SSSHumanMaleSaturation = sssHumanMaleSaturation;
		data.SSSHumanMaleBrightness = sssHumanMaleBrightness;
		data.SSSHumanMaleBaseSaturation = sssHumanMaleBaseSaturation;
		data.SSSHumanFemaleIntensity = sssHumanFemaleIntensity;
		data.SSSHumanFemaleSaturation = sssHumanFemaleSaturation;
		data.SSSHumanFemaleBrightness = sssHumanFemaleBrightness;
		data.SSSHumanFemaleBaseSaturation = sssHumanFemaleBaseSaturation;

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

		data.VRFoveationData0 = { FoveatedCommon::kCenterScaleMax, FoveatedCommon::kCenterFeather, 1.0f, FoveatedCommon::GetShaderMode(FoveatedCommon::DetailMode::Off) };
		data.VRFoveationModes = { 0.0f, 0.0f, 0.0f, 0.0f };
		data.VRFoveationCenterOffsets = { 0.0f, 0.0f, 0.0f, 0.0f };
		const auto& vr = globals::features::vr;
		const auto& dynamicCubemaps = globals::features::dynamicCubemaps;
		const auto& waterEffects = globals::features::waterEffects;
		const auto& wetnessEffects = globals::features::wetnessEffects;
		const auto& wetterness = globals::features::wetterness;
		const bool dynamicSSRActive = dynamicCubemaps.IsSSRRuntimeActive();
		const bool waterParallaxActive = vr.settings.EnableWaterParallaxFoveation && waterEffects.loaded;
		const bool wetnessEffectsActive = wetnessEffects.IsRuntimeActive();
		const bool wetternessFoveationActive =
			vr.settings.EnableWetternessFoveation &&
			wetterness.IsRuntimeProcessingActive() &&
			!wetnessEffectsActive;
		const bool anyFoveatedShaderDetailEnabled =
			vr.settings.EnableLightingFoveation ||
			(vr.settings.EnableSSRFoveation && dynamicSSRActive) ||
			waterParallaxActive ||
			wetternessFoveationActive;
		if (globals::game::isVR &&
			vr.loaded &&
			anyFoveatedShaderDetailEnabled &&
			upscaling.loaded) {
			const auto profile = upscaling.GetActiveUpscalingFoveatedProfile();
			if (profile.available) {
				const float centerScale = FoveatedCommon::ClampCenterScale(profile.sharedVisibleScale);
				const bool foveationActive = FoveatedCommon::IsActiveCoverage(centerScale);
				const float disabledFoveationMode = FoveatedCommon::GetShaderMode(FoveatedCommon::DetailMode::Off);
				const float lightingFoveationMode = FoveatedCommon::GetShaderMode(
					FoveatedCommon::GetDetailMode(vr.settings.EnableLightingFoveation, vr.settings.EnableLightingFoveationHardCutoff));
				const float ssrFoveationMode = FoveatedCommon::GetShaderMode(FoveatedCommon::GetDetailMode(
					vr.settings.EnableSSRFoveation && dynamicSSRActive,
					vr.settings.EnableSSRFoveationHardCutoff));
				const float waterParallaxFoveationMode = FoveatedCommon::GetShaderMode(FoveatedCommon::GetDetailMode(
					waterParallaxActive,
					vr.settings.EnableWaterParallaxFoveationHardCutoff));
				const float wetternessFoveationMode = FoveatedCommon::GetShaderMode(FoveatedCommon::GetDetailMode(
					wetternessFoveationActive,
					vr.settings.EnableWetternessFoveationHardCutoff));
				const float centerHorizontalScale = FoveatedCommon::ClampCenterHorizontalScale(profile.centerHorizontalScale);
				const float activeLightingMode = foveationActive ? lightingFoveationMode : disabledFoveationMode;
				data.VRFoveationData0 = { centerScale, FoveatedCommon::kCenterFeather, centerHorizontalScale, activeLightingMode };
				data.VRFoveationModes = {
					foveationActive ? ssrFoveationMode : disabledFoveationMode,
					foveationActive ? waterParallaxFoveationMode : disabledFoveationMode,
					foveationActive ? wetternessFoveationMode : disabledFoveationMode,
					disabledFoveationMode
				};
				data.VRFoveationCenterOffsets = {
					profile.centerOffsets[0].x,
					profile.centerOffsets[0].y,
					profile.centerOffsets[1].x,
					profile.centerOffsets[1].y
				};
			}
		}

		sharedDataCB->Update(data);
	}

	{
		const auto [data, size] = GetFeatureBufferData(a_inWorld);

		featureDataCB->Update(data, size);
	}

	auto* srv = Util::GetCurrentSceneDepthSRV(true);
	globals::d3d::context->PSSetShaderResources(17, 1, &srv);
}

void State::ClearDisabledFeatures()
{
	disabledFeatures.clear();
}

bool State::SetFeatureDisabled(const std::string& featureName, bool isDisabled)
{
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

void State::SaveTheme()
{
	// SelectedThemePreset is now persisted via SettingsUser.json (State::Save)
	// Keep this function as a no-op for backward compatibility and to avoid writing separate theme files.
	logger::info("SaveTheme() no longer writes SettingsTheme.json; SelectedThemePreset is saved with SettingsUser.json");
}
