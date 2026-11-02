#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NVPERF_ENABLED)
#	include "Diagnostics/NvPerf.h"
#endif
#include "State.h"
#include "Diagnostics/EngineStutterMonitor.h"
#include "EngineFixes/LODBlendRingFix.h"
#include "Features/InteriorSun.h"
#include "Features/LightLimitFix/ShadowCulling.h"
#include "Features/TerrainSeams.h"
#include "Features/TextureStreaming.h"
#include "I18n/I18n.h"
#include "PresetCompatibility.h"
#include "SettingsMigrations.h"
#include "SettingsSerialization.h"
#include "Utils/SettingsOverlay.h"
#include "Utils/WorldLoadTransitionPolicy.h"
#include <RE/B/BGSSaveLoadGame.h>

#include <codecvt>

#include <pystring/pystring.h>

#include "Deferred.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "Features/AdaptiveBrightness.h"
#include "Features/CSEditor.h"
#include "Features/CloudShadows.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/HDRDisplay.h"
#include "Features/InteriorSun.h"
#include "Features/LightLimitFix.h"
#include "Features/PerformanceOverlay.h"
#include "Features/Skin.h"
#include "Features/SkySync.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/Upscaling.h"
#include "Features/VolumetricLighting.h"
#include "Features/VolumetricShadows.h"
#include "Features/Wetterness.h"
#include "Menu.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "ShaderCache.h"
#include "TruePBR.h"
#include "Utils/FileSystem.h"
#include "Utils/Moon.h"
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

void State::UpdateLightingShaderPermutation(RE::BSRenderPass* a_pass)
{
	constexpr auto additiveLighting = static_cast<uint32_t>(ExtraShaderDescriptors::AdditiveLighting);
	permutationData.ExtraShaderDescriptor &= ~additiveLighting;

	if (!a_pass || !a_pass->geometry)
		return;

	auto* alphaProperty = a_pass->geometry->GetGeometryRuntimeData().alphaProperty.get();
	if (alphaProperty && alphaProperty->GetAlphaBlending() &&
		alphaProperty->GetDestBlendMode() == RE::NiAlphaProperty::AlphaFunction::kOne) {
		permutationData.ExtraShaderDescriptor |= additiveLighting;
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
	CSX_CODE_EVALUATION_SCOPE(drawScope, currentShader && currentShader->shaderType.get() == RE::BSShader::Type::Sky &&
												 (currentPixelDescriptor & 255) == static_cast<uint32_t>(SIE::ShaderCache::SkyShaderTechniques::SunGlare) ?
											 "State::Draw[Sky::SunGlare]" :
											 "State::Draw");

	auto shaderCache = globals::shaderCache;
	auto weatherManager = globals::weatherManager;
	auto sceneSettingsManager = globals::sceneSettingsManager;
	auto& terrainBlending = globals::features::terrainBlending;
	auto& terrainHelper = globals::features::terrainHelper;
	auto& cloudShadows = globals::features::cloudShadows;
	auto& skin = globals::features::skin;
	auto& truePBR = globals::features::truePBR;
	auto context = globals::d3d::context;
	auto& volumetricShadows = globals::features::volumetricShadows;

	if (pendingPostLoadRuntimeReset.exchange(false, std::memory_order_acq_rel)) {
		CSX_CODE_EVALUATION_SCOPE(resetScope, "State::PostLoadRuntimeReset");
		globals::OnDataLoaded();
		weatherManager->ClearCache();
		globals::features::lightLimitFix.Reset();
		globals::features::interiorSun.isInteriorWithSun = false;
		globals::features::wetterness.ResetRuntimeStateAfterGameLoad();
		logger::info("Applied deferred post-load runtime reset");
	}

	if (shaderCache->IsEnabled()) {
		const bool sceneManagersReady =
			PerformanceTuningRenderer::PrepareForSceneUpdate();

		// Process deferred cell transitions (interior detection)
		if (sceneManagersReady) {
			CSX_CODE_EVALUATION_SCOPE(sceneScope, "State::SceneSettingsUpdate");
			sceneSettingsManager->Update();
		}

		if (sceneManagersReady) {
			CSX_CODE_EVALUATION_SCOPE(weatherScope, "WeatherManager::UpdateFeatures");
			ZoneScopedN("WeatherManager::UpdateFeatures");
			weatherManager->UpdateFeatures();
		}

		if (terrainBlending.loaded && terrainBlending.settings.Enabled) {
			CSX_CODE_EVALUATION_SCOPE(terrainScope, "TerrainBlending::TerrainShaderHacks");
			ZoneScopedN("TerrainBlending::TerrainShaderHacks");
			terrainBlending.TerrainShaderHacks();
		}

		if (cloudShadows.loaded) {
			CSX_CODE_EVALUATION_SCOPE(cloudScope, "CloudShadows::SkyShaderHacks");
			ZoneScopedN("CloudShadows::SkyShaderHacks");
			cloudShadows.SkyShaderHacks();
		}

		if (terrainHelper.loaded) {
			CSX_CODE_EVALUATION_SCOPE(terrainResourcesScope, "TerrainHelper::SetShaderResources");
			ZoneScopedN("TerrainHelper::SetShaderResources");
			terrainHelper.SetShaderResources(context);
		}

		if (skin.loaded) {
			CSX_CODE_EVALUATION_SCOPE(skinScope, "Skin::SetShaderResources");
			ZoneScopedN("Skin::SetShaderResources");
			skin.SetShaderResources(context);
		}

		if (truePBR.loaded) {
			CSX_CODE_EVALUATION_SCOPE(pbrScope, "TruePBR::SetShaderResources");
			ZoneScopedN("TruePBR::SetShaderResources");
			truePBR.SetShaderResources(context);
		}

		if (volumetricShadows.loaded) {
			CSX_CODE_EVALUATION_SCOPE(shadowsScope, "VolumetricShadows::SetShaderResources");
			ZoneScopedN("VolumetricShadows::SetShaderResources");
			volumetricShadows.SetShaderResources(context, selectedCustomPixelShaderType);
		}

		if (permutationData != permutationDataPrevious) {
			CSX_CODE_EVALUATION_SCOPE(permutationScope, "State::PermutationCBUpdate");
			permutationCB->Update(permutationData);
			permutationDataPrevious = permutationData;
		}

		if (currentShader && updateShader) {
			if (currentShader->shaderType.get() == RE::BSShader::Type::Utility) {
				if (currentPixelDescriptor & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmask)) {
					CSX_CODE_EVALUATION_SCOPE(shadowCopyScope, "State::ShadowDataCopy");
					if (volumetricShadows.loaded)
						volumetricShadows.CopyShadowLightData();
					else
						globals::deferred->CopyShadowData();
					if (globals::features::exponentialHeightFog.loaded)
						globals::features::exponentialHeightFog.CaptureDirectionalShadowMap();
				}
			}
		}

		if (globals::menu->overlayVisible && globals::features::performanceOverlay.loaded && globals::features::performanceOverlay.IsOverlayVisible()) {
			CSX_CODE_EVALUATION_SCOPE(overlayScope, "State::DebugOverlay");
			Debug();
		}

		if (currentShader && globals::profiler && globals::profiler->IsRenderTargetCaptureActive()) {
			if (auto* shadow = globals::game::shadowState) {
				const auto& runtime = shadow->GetRuntimeData();
				globals::profiler->CountRenderTargetDraw(runtime.depthStencil, static_cast<uint32_t>(runtime.renderTargets[0]));
			}
		}
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

namespace
{
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
	const wchar_t* WidenAnnotation(std::wstring& a_buffer, std::string_view a_title)
	{
		a_buffer.resize(a_title.size());
		std::copy(a_title.begin(), a_title.end(), a_buffer.begin());
		return a_buffer.c_str();
	}
	void ExtendFrameDeadline(
		std::atomic_uint32_t& a_target,
		uint32_t a_currentFrame,
		uint32_t a_frameCount)
	{
		uint32_t current = a_target.load(std::memory_order_acquire);
		for (;;) {
			const uint32_t extended = Util::WorldLoadTransition::ExtendDeadline(
				current, a_currentFrame, a_frameCount);
			if (extended == current)
				return;
			if (a_target.compare_exchange_weak(
					current,
					extended,
					std::memory_order_acq_rel,
					std::memory_order_acquire)) {
				return;
			}
		}
	}
}

bool State::IsSaveLoadSafeModeActive() const
{
	return saveLoadSafeModeActive.load(std::memory_order_acquire);
}

bool State::IsWorldLoadTransitionActive() const
{
	return worldLoadTransitionActive.load(std::memory_order_acquire);
}

bool State::IsEngineSaveLoadActivityActive() const
{
	return engineSaveLoadActivityActive.load(std::memory_order_acquire);
}

uint64_t State::GetOrdinarySaveRenderRecoveryToken() const
{
	std::lock_guard lock(saveLoadSafeModeMutex);
	const uint64_t recoveryState = saveLoadRenderRecoveryState;
	if ((recoveryState & kSaveLoadRenderRecoverySourceMask) !=
			static_cast<uint64_t>(SaveLoadRenderRecoverySource::OrdinarySave) ||
		!engineSaveLoadActivityKnown ||
		IsEngineSaveLoadActivityActive()) {
		return 0;
	}
	return recoveryState;
}

bool State::IsPersistentMutationBlocked() const
{
	return persistentMutationBlocked.load(std::memory_order_acquire);
}

void State::RecordSaveLoadRenderRecoverySource(SaveLoadRenderRecoverySource a_source)
{
	const auto previousSource = static_cast<SaveLoadRenderRecoverySource>(
		saveLoadRenderRecoveryState & kSaveLoadRenderRecoverySourceMask);
	const auto source = previousSource == SaveLoadRenderRecoverySource::Other ?
	                        SaveLoadRenderRecoverySource::Other :
	                        a_source;
	const uint64_t nextGeneration =
		(saveLoadRenderRecoveryState & ~kSaveLoadRenderRecoverySourceMask) + kSaveLoadRenderRecoverySourceMask + 1;
	saveLoadRenderRecoveryState = nextGeneration | static_cast<uint64_t>(source);
}

void State::NotifyOrdinarySave(uint32_t a_currentFrame)
{
	ExtendSaveGamePersistenceSafeMode(a_currentFrame, kSaveLoadSafeModeGraceFrames);
}

void State::BeginSaveLoadSafeMode(uint32_t a_currentFrame)
{
	std::lock_guard lock(saveLoadSafeModeMutex);
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	worldLoadTransitionStartFrame.store(currentFrame, std::memory_order_release);
	worldLoadTransitionEndFrame.store(0, std::memory_order_release);
	worldLoadTransitionActive.store(true, std::memory_order_release);
	saveLoadSafeModeStartFrame.store(currentFrame, std::memory_order_release);
	saveLoadSafeModeEndFrame.store(0, std::memory_order_release);
	if (globals::shaderCache)
		globals::shaderCache->SetSaveLoadDiskPersistenceBlocked(true);
	saveLoadSafeModeActive.store(true, std::memory_order_release);
	persistentMutationBlocked.store(true, std::memory_order_release);
	RecordSaveLoadRenderRecoverySource(SaveLoadRenderRecoverySource::Other);
}

void State::ExtendSaveLoadSafeMode(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	std::lock_guard lock(saveLoadSafeModeMutex);
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	if (worldLoadTransitionStartFrame.load(std::memory_order_acquire) == 0)
		worldLoadTransitionStartFrame.store(currentFrame, std::memory_order_release);
	ExtendFrameDeadline(worldLoadTransitionEndFrame, currentFrame, a_frameCount);
	worldLoadTransitionActive.store(true, std::memory_order_release);
	ExtendSaveLoadSafeModeImpl(currentFrame, a_frameCount);
	RecordSaveLoadRenderRecoverySource(SaveLoadRenderRecoverySource::Other);
}

void State::ExtendSaveGamePersistenceSafeMode(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	std::lock_guard lock(saveLoadSafeModeMutex);
	ExtendSaveLoadSafeModeImpl(a_currentFrame, a_frameCount);
	RecordSaveLoadRenderRecoverySource(SaveLoadRenderRecoverySource::OrdinarySave);
}

void State::ExtendSaveLoadSafeModeImpl(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	if (saveLoadSafeModeStartFrame.load(std::memory_order_acquire) == 0) {
		saveLoadSafeModeStartFrame.store(currentFrame, std::memory_order_release);
	}
	ExtendFrameDeadline(saveLoadSafeModeEndFrame, currentFrame, a_frameCount);
	if (globals::shaderCache)
		globals::shaderCache->SetSaveLoadDiskPersistenceBlocked(true);
	saveLoadSafeModeActive.store(true, std::memory_order_release);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::BeginPersistentMutationBlock(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	std::lock_guard lock(saveLoadSafeModeMutex);
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	ExtendFrameDeadline(persistentMutationBlockEndFrame, currentFrame, a_frameCount);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::ExtendPersistentMutationBlock(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	std::lock_guard lock(saveLoadSafeModeMutex);
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	ExtendFrameDeadline(persistentMutationBlockEndFrame, currentFrame, a_frameCount);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::UpdateSaveLoadSafeMode()
{
	std::lock_guard lock(saveLoadSafeModeMutex);
	const uint32_t currentFrame = std::max(frameCount, 1u);
	bool safeModeActive = saveLoadSafeModeActive.load(std::memory_order_acquire);
	const bool wasSafeModeActive = safeModeActive;

	bool engineStateKnown = false;
	Util::WorldLoadTransition::EngineSignals engineSignals;
	if (auto* saveLoad = RE::BGSSaveLoadGame::GetSingleton()) {
		engineStateKnown = true;
		engineSignals = {
			.loading = saveLoad->GetSaveGameLoading(),
			.saving = saveLoad->GetSaveGameSaving(),
			.initializingForms = saveLoad->GetInitingForms(),
			.deferredFormInitialization = saveLoad->GetDeferInitForms(),
			.positioningPlayer = saveLoad->GetPositioningPlayerCharacter(),
		};
	}
	const bool engineSaving = engineSignals.saving;
	const bool engineLoadingOrInitializing = engineSignals.ReplacesWorldState();
	const bool engineSaveLoadActive = engineSignals.RequiresPersistenceGuard();
	engineSaveLoadActivityActive.store(
		engineSaveLoadActive,
		std::memory_order_release);
	engineSaveLoadActivityKnown = engineStateKnown;
	const bool ordinarySaveRecoveryUnsafe = !engineStateKnown ||
	                                        engineLoadingOrInitializing || IsMainOrLoadingMenuOpen() || pendingPostLoadRuntimeReset;
	if (ordinarySaveRecoveryUnsafe) {
		RecordSaveLoadRenderRecoverySource(SaveLoadRenderRecoverySource::Other);
	} else if (engineSaving && !engineSavingWasActive) {
		RecordSaveLoadRenderRecoverySource(SaveLoadRenderRecoverySource::OrdinarySave);
	}
	engineSavingWasActive = engineSaving;

	{
		const auto worldTransition = Util::WorldLoadTransition::Advance(
			{
				.active = worldLoadTransitionActive.load(std::memory_order_acquire),
				.startFrame = worldLoadTransitionStartFrame.load(std::memory_order_acquire),
				.endFrame = worldLoadTransitionEndFrame.load(std::memory_order_acquire),
			},
			engineSignals,
			currentFrame,
			kSaveLoadSafeModeGraceFrames,
			kSaveLoadSafeModeFallbackFrames);
		worldLoadTransitionStartFrame.store(worldTransition.startFrame, std::memory_order_release);
		worldLoadTransitionEndFrame.store(worldTransition.endFrame, std::memory_order_release);
		worldLoadTransitionActive.store(worldTransition.active, std::memory_order_release);
	}

	if (engineSaveLoadActive) {
		if (!safeModeActive) {
			if (globals::shaderCache)
				globals::shaderCache->SetSaveLoadDiskPersistenceBlocked(true);
			saveLoadSafeModeStartFrame.store(currentFrame, std::memory_order_release);
		}
		ExtendFrameDeadline(
			saveLoadSafeModeEndFrame,
			currentFrame,
			kSaveLoadSafeModeGraceFrames);
		safeModeActive = true;
		saveLoadSafeModeActive.store(true, std::memory_order_release);
	} else if (safeModeActive) {
		const uint32_t endFrame = saveLoadSafeModeEndFrame.load(std::memory_order_acquire);
		if (endFrame != 0) {
			if (Util::WorldLoadTransition::ReachedDeadline(currentFrame, endFrame)) {
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
	if (mutationBlockEndFrame != 0 &&
		Util::WorldLoadTransition::ReachedDeadline(currentFrame, mutationBlockEndFrame)) {
		persistentMutationBlockEndFrame.store(0, std::memory_order_release);
		mutationBlockEndFrame = 0;
	}

	const bool mutationGraceActive =
		mutationBlockEndFrame != 0 &&
		!Util::WorldLoadTransition::ReachedDeadline(currentFrame, mutationBlockEndFrame);
	persistentMutationBlocked.store(safeModeActive || mutationGraceActive, std::memory_order_release);

	if (wasSafeModeActive && !safeModeActive && globals::shaderCache)
		globals::shaderCache->SetSaveLoadDiskPersistenceBlocked(false);

	if (!safeModeActive && !ordinarySaveRecoveryUnsafe) {
		saveLoadRenderRecoveryState &= ~kSaveLoadRenderRecoverySourceMask;
	}
}

void State::Reset()
{
	selectedCustomPixelShaderType = RE::BSShader::Type::None;
	auto& terrain = globals::features::terrainBlending;
	TerrainSeams::GetInstance().Update(terrain.loaded && terrain.settings.Enabled && terrain.settings.LandscapeSeams &&
									   globals::features::truePBR.loaded && globals::features::truePBR.settings.Enabled);
	if (globals::game::ui && !globals::game::ui->GameIsPaused())
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

	UpdateSaveLoadSafeMode();
	Feature::ForEachLoadedFeature("Reset", [](Feature* feature) { feature->Reset(); });

	lastModifiedPixelDescriptor = 0;
	lastModifiedVertexDescriptor = 0;
	lastPixelDescriptor = 0;
	lastVertexDescriptor = 0;
	std::memset(&permutationDataPrevious, 0xFF, sizeof(PermutationCB));
	globals::features::performanceOverlay.UpdateRenderTargetCapture();
	globals::profiler->SetEngineFrameCount(frameCount);
	frameCount++;
	frameCountAtomic.store(frameCount, std::memory_order_release);
	UpdateSaveLoadSafeMode();
	RefreshDrawAnnotations();
	LODBlendRingFix::GetInstance().Update();
	globals::shaderCache->TickActiveShaderCapture(globals::menu && globals::menu->IsEnabled);
	globals::shaderCache->ProcessPendingClear();
	globals::shaderCache->WritePendingDiskCacheInfoIfNeeded();

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

	const auto publicationGeneration = BeginRenderTargetResourcePublication();
	SetupResources();

	// Probe typed UAV load support before features set up their resources, so any
	// gating logic that wants to read the log can run during feature SetupResources.
	CheckTypedUAVLoadSupport();

	uint32_t loadedFeatureSetupCount = 0;
	Feature::ForEachLoadedFeature("SetupResources", [&](Feature* feature) { feature->SetupResources(); ++loadedFeatureSetupCount; });
	globals::deferred->SetupResources();
	CompleteRenderTargetResourcePublication(publicationGeneration, loadedFeatureSetupCount);

	// Load per-weather settings after features are setup
	globals::weatherManager->LoadPerWeatherSettingsFromDisk();

	// Load scene-specific settings (Interior Only, etc.)
	globals::sceneSettingsManager->LoadAll();
}

static std::filesystem::path GetConfigPath(State::ConfigMode a_configMode)
{
	switch (a_configMode) {
	case State::ConfigMode::USER:
		return Util::PathHelpers::GetSettingsUserPath();
	case State::ConfigMode::TEST:
		return Util::PathHelpers::GetSettingsTestPath();
	case State::ConfigMode::THEME:
		return Util::PathHelpers::GetSettingsThemePath();
	case State::ConfigMode::DEFAULT:
	default:
		return Util::PathHelpers::GetSettingsDefaultPath();
	}
}

bool State::RestoreDefaultSettings(std::string& a_error)
{
	a_error.clear();
	if (!globals::menu || !globals::shaderCache) {
		a_error = T("menu.defaults.not_ready", "Settings are not ready to restore defaults.");
		return false;
	}
	if (IsPersistentMutationBlocked() || PerformanceTuningRenderer::HasActiveMeasurements()) {
		a_error = T("menu.defaults.busy", "Defaults were not restored. Finish the current save, load or performance comparison first.");
		return false;
	}
	json defaults;
	const auto read = Util::FileHelpers::ReadJsonFile(GetConfigPath(ConfigMode::DEFAULT), defaults, a_error);
	if (read != Util::FileHelpers::JsonFileReadResult::Success) {
		a_error = read == Util::FileHelpers::JsonFileReadResult::NotFound ? T("menu.defaults.missing", "Defaults were not restored because SettingsDefault.json is missing.") :
		                                                                    std::format("{} {}", T("menu.defaults.read_failed", "Defaults could not be read:"), a_error);
		return false;
	}
	if (!defaults.is_object()) {
		a_error = T("menu.defaults.invalid_object", "Defaults were not restored because SettingsDefault.json must contain an object.");
		return false;
	}
	for (const char* section : { "Menu", "General", "Advanced" }) {
		if (!defaults.contains(section) || !defaults[section].is_object()) {
			a_error = std::vformat(T("menu.defaults.invalid_section", "Defaults were not restored because the {} section is missing or invalid."), std::make_format_args(section));
			return false;
		}
	}
	// Restoring tuning must not reopen the first-time setup dialog.
	if (globals::menu)
		defaults["Menu"]["FirstTimeSetupCompleted"] = globals::menu->GetSettings().FirstTimeSetupCompleted;
	if (!globals::menu->PrepareSettingsMutation()) {
		a_error = T("menu.defaults.baseline_failed", "Could not preserve the saved-settings baseline before restoring defaults.");
		return false;
	}
	json previous;
	try {
		SaveToJson(previous, false);
	} catch (const std::exception& e) {
		a_error = std::format("{} {}", T("menu.defaults.snapshot_failed", "Could not preserve current settings before restoring defaults:"), e.what());
		return false;
	}
	bool restored = false;
	try {
		LoadFromJson(defaults);
		restored = true;
	} catch (const std::exception& e) {
		a_error = std::format("{} {}", T("menu.defaults.apply_failed", "Defaults could not be applied:"), e.what());
		try {
			LoadFromJson(previous);
		} catch (const std::exception& rollback) {
			a_error += std::format(" {} {}", T("menu.defaults.rollback_failed", "Current settings could not be recovered:"), rollback.what());
		}
		logger::warn("{}", a_error);
	}
	WeatherManager::GetSingleton()->RefreshFeatureOverrides();
	if (globals::menu) {
		globals::menu->RequestSettingsDirtyCheck();
		if (restored)
			globals::menu->ClearSettingsSaveResult();
	}
	return restored;
}

void State::Load(
	ConfigMode a_configMode,
	bool a_allowReload,
	SettingsApplyMode a_applyMode)
{
	json settings = json::object();
	bool errorDetected = false;

	const auto configPath = GetConfigPath(a_configMode);
	const auto configFolderPath = configPath.parent_path();
	const auto defaultConfigFilePath = GetConfigPath(ConfigMode::DEFAULT);

	try {
		std::filesystem::create_directories(configFolderPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating directory during Load ({}): {}", configFolderPath.string(), e.what());
	}

	auto tryLoadConfig = [](const std::filesystem::path& a_path, json& o_settings) {
		logger::info("Attempting to open config file: {}", a_path.string());

		std::string errorMessage;
		const auto result = Util::FileHelpers::ReadJsonFile(a_path, o_settings, errorMessage);
		if (result == Util::FileHelpers::JsonFileReadResult::Success && !o_settings.is_object()) {
			logger::warn("Config file must contain a JSON object: {}", a_path.string());
			return Util::FileHelpers::JsonFileReadResult::Error;
		}

		if (result == Util::FileHelpers::JsonFileReadResult::Error)
			logger::warn("Unable to read config file {}: {}", a_path.string(), errorMessage);
		return result;
	};

	auto canonicalizeConfig = [](const std::filesystem::path& a_path, const json& a_settings) {
		std::string errorMessage;
		const auto result = SettingsSerialization::CanonicalizeFile(a_path, a_settings, errorMessage);
		if (result == SettingsSerialization::CanonicalizationResult::Rewritten) {
			logger::info("Reordered settings to match the UI: {}", a_path.string());
		} else if (result == SettingsSerialization::CanonicalizationResult::Error) {
			logger::warn("Could not reorder settings file {}: {}", a_path.string(), errorMessage);
		}
		return result;
	};

	auto generateFallbackSettingsInMemory = [&]() {
		settings = json::object();
		std::fill(enabledClasses, enabledClasses + magic_enum::enum_integer(RE::BSShader::Type::Total) - 1, true);
		try {
			SaveToJson(settings, true);
			return true;
		} catch (const std::exception& e) {
			logger::error("Could not generate fallback settings in memory: {}", e.what());
		} catch (...) {
			logger::error("Could not generate fallback settings in memory due to an unknown error");
		}
		return false;
	};

	// LOADING ORDER: Default -> User -> Overrides -> User Overrides (.user files)

	// Step 1: Always start with default settings
	logger::info("Loading default settings from: {}", defaultConfigFilePath.string());
	const auto defaultResult = tryLoadConfig(defaultConfigFilePath, settings);
	bool loadedDefaultFromDisk = defaultResult == Util::FileHelpers::JsonFileReadResult::Success;
	if (defaultResult == Util::FileHelpers::JsonFileReadResult::NotFound) {
		logger::info("No usable default config ({}), generating a new one", defaultConfigFilePath.string());
		std::fill(enabledClasses, enabledClasses + magic_enum::enum_integer(RE::BSShader::Type::Total) - 1, true);
		Save(ConfigMode::DEFAULT);
		loadedDefaultFromDisk =
			tryLoadConfig(defaultConfigFilePath, settings) == Util::FileHelpers::JsonFileReadResult::Success;
		if (!loadedDefaultFromDisk) {
			logger::warn("Could not persist default settings; continuing with an in-memory baseline");
			if (!generateFallbackSettingsInMemory())
				return;
		}
	} else if (defaultResult == Util::FileHelpers::JsonFileReadResult::Error) {
		logger::error("Default config is invalid or unreadable; preserving it and using an in-memory baseline: {}", defaultConfigFilePath.string());
		if (!generateFallbackSettingsInMemory())
			return;
	}
	if (loadedDefaultFromDisk) {
		canonicalizeConfig(defaultConfigFilePath, settings);
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(settings);
		if (auto adaptiveIt = settings.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
			adaptiveIt != settings.end())
			SettingsMigrations::MarkExplicitAdaptiveBalanceWaterProfiles(*adaptiveIt);
	}

	// Step 2: Apply user settings on top of defaults.
	if (a_configMode == ConfigMode::USER) {
		json userSettings;
		const auto userResult = tryLoadConfig(configPath, userSettings);
		if (userResult == Util::FileHelpers::JsonFileReadResult::Success) {
			auto presetCompatibility = PresetCompatibility::Evaluate(userSettings, Plugin::VERSION_LABEL);
			PresetCompatibility::Publish(presetCompatibility);
			if (!presetCompatibility.ShouldApply()) {
				logger::error(
					"Rejected user settings without modifying {}: {}",
					configPath.string(),
					presetCompatibility.message);
			} else {
				if (presetCompatibility.disposition == PresetCompatibility::Disposition::kCompatible)
					logger::info("Accepted marked preset: {}", presetCompatibility.message);
				canonicalizeConfig(configPath, userSettings);
				const auto adaptiveBalanceIt = userSettings.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
				const auto legacyAdaptiveBrightnessIt =
					userSettings.find(SettingsMigrations::kLegacyAdaptiveBrightnessSettingsName.data());
				const bool userDefinedAdaptiveBalance =
					(adaptiveBalanceIt != userSettings.end() && adaptiveBalanceIt->is_object()) ||
					(legacyAdaptiveBrightnessIt != userSettings.end() && legacyAdaptiveBrightnessIt->is_object());
				SettingsMigrations::MigrateAdaptiveBalanceRootLayer(userSettings, true);
				if (const auto sourceAdaptiveIt = userSettings.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
					sourceAdaptiveIt != userSettings.end() && SettingsMigrations::HasForcedLegacyWaterAppearance(*sourceAdaptiveIt)) {
					if (auto targetAdaptiveIt = settings.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
						targetAdaptiveIt != settings.end())
						SettingsMigrations::ClearExplicitAdaptiveBalanceWaterProfiles(*targetAdaptiveIt);
				}
				if (auto adaptiveIt = userSettings.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
					adaptiveIt != userSettings.end())
					SettingsMigrations::MarkExplicitAdaptiveBalanceWaterProfiles(*adaptiveIt);
				for (const auto& [key, value] : userSettings.items()) {
					if (Util::Settings::OverlayCoreSettings(settings, key, value))
						continue;
					auto existingIt = settings.find(key);
					if (!userDefinedAdaptiveBalance &&
						key == SettingsMigrations::kAdaptiveBalanceSettingsName &&
						existingIt != settings.end() && existingIt->is_object() && value.is_object()) {
						// Migration can synthesize a partial destination section in an
						// otherwise-CSUtility-only user file. Overlay that patch so it does
						// not replace richer Adaptive Balance defaults from Settings.json.
						existingIt->merge_patch(value);
					} else {
						settings[key] = value;
					}
				}
				logger::info("Applied user settings from: {}", configPath.string());
			}
		} else if (userResult == Util::FileHelpers::JsonFileReadResult::NotFound) {
			PresetCompatibility::Publish(PresetCompatibility::Evaluate(json::object(), Plugin::VERSION_LABEL));
			logger::info("No user config file found at: {}", configPath.string());
		} else {
			PresetCompatibility::Evaluation evaluation;
			evaluation.disposition = PresetCompatibility::Disposition::kRejected;
			evaluation.currentVersion = std::string{ Plugin::VERSION_LABEL };
			evaluation.message = "SettingsUser.json is unreadable; defaults remain active and the source file is protected from overwrite.";
			PresetCompatibility::Publish(std::move(evaluation));
		}
	}

	// Retire legacy distances before a separate-field override can merge with them.
	try {
		if (auto lighting = settings.find("Light Limit Fix"); lighting != settings.end())
			LightLimitFixDetail::ShadowCulling::MigrateLegacyOverride(*lighting);
	} catch (const std::exception& error) {
		logger::warn("Shadow-culling migration deferred to feature validation: {}", error.what());
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
	SettingsMigrations::MigrateAdaptiveBalanceRootLayer(settings);

	try {
		// Load core settings (Menu, Advanced, General, Replace Original Shaders)
		logger::info("Loading core settings");
		LoadFromJson(settings, false, a_applyMode);
		// Ensure 'Disable at Boot' section exists in the JSON
		if (!settings.contains("Disable at Boot") || !settings["Disable at Boot"].is_object()) {
			// Initialize to an empty object if it doesn't exist
			settings["Disable at Boot"] = json::object();
		}

		json& disabledFeaturesJson = settings["Disable at Boot"];
		logger::info("Loading 'Disable at Boot' settings");

		disabledFeatures.clear();
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
					if (overrideManager->HasFeatureOverrides(featureName) ||
						(featureName == SettingsMigrations::kAdaptiveBalanceFeatureName && overrideManager->HasUserOverride(featureName))) {
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

		WeatherManager::GetSingleton()->NotifyUserSettingsChanged();

		const auto currentVersion = std::string{ Plugin::VERSION_LABEL };
		const auto versionIt = settings.find("Version");
		if (versionIt == settings.end() || !versionIt->is_string() || versionIt->get<std::string>() != currentVersion) {
			const auto loadedVersion = versionIt != settings.end() && versionIt->is_string() ?
			                               versionIt->get<std::string>() :
			                               std::string{ "<missing or invalid>" };
			logger::info("Loaded config for version {}; version {} will be written on save", loadedVersion, currentVersion);
		}

		FeatureIssues::ScanForOrphanedFeatureINIs();

		logger::info("Loading Settings Complete");
	} catch (const json::exception& e) {
		logger::warn("General JSON error accessing settings: {}; preserving the source config", e.what());
		errorDetected = true;
	} catch (const std::exception& e) {
		logger::warn("General error accessing settings: {}; preserving the source config", e.what());
		errorDetected = true;
	}
	if (errorDetected && a_allowReload && a_configMode != ConfigMode::DEFAULT) {
		logger::warn("Loading default settings after the selected config failed");
		Load(ConfigMode::DEFAULT, false, a_applyMode);
	}
	if (!errorDetected && globals::menu)
		globals::menu->ResetSettingsDirtyState();
}

void State::SaveToJson(
	nlohmann::json& settings,
	bool a_includeMissingUnloadedFeatures)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	SettingsMigrations::MigrateAdaptiveBalanceRootLayer(settings);
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
	general["Enable Shaders"] = shaderCache->IsEnableRequested();
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

	// Save feature settings.
	auto* weatherRegistry =
		WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	for (auto* feature : Feature::GetFeatureList()) {
		// Preserve the last valid settings for features which were disabled, missing,
		// or incompatible and therefore never loaded into memory.
		const std::string settingsName = feature->GetName();
		if (feature->loaded || (a_includeMissingUnloadedFeatures && !settings.contains(settingsName))) {
			if (feature->loaded) {
				const auto activeVariables =
					WeatherManager::GetSingleton()->GetActiveVariablesForFeature(
						feature->GetShortName());
				weatherRegistry->SerializeFeatureUserSettings(
					feature->GetShortName(), activeVariables, [&]() {
						feature->Save(settings);
					});
			} else {
				feature->Save(settings);
			}
		}
	}
}

void State::LoadFromJson(nlohmann::json& settings, bool a_loadFeatureSettings, SettingsApplyMode a_applyMode)
{
	if (!PerformanceTuningRenderer::NotifyConfigurationChanging())
		throw std::runtime_error("Current performance comparison settings could not be restored; configuration was not changed.");
	if (a_loadFeatureSettings)
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(settings);
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
		if (general.contains("Enable Shaders") && general["Enable Shaders"].is_boolean()) {
			if (a_applyMode == SettingsApplyMode::StartupHydration)
				shaderCache->RestoreEnabledSetting(general["Enable Shaders"]);
			else
				shaderCache->SetEnabled(general["Enable Shaders"]);
		}
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
		if (a_loadFeatureSettings && feature->loaded) {
			feature->ReloadSettings(settings);
		}
	}
}

bool State::Save(ConfigMode a_configMode)
{
	if (IsPersistentMutationBlocked()) {
		if (globals::menu)
			globals::menu->ReportSettingsSaveResult(false, "Settings cannot be saved during game save/load. Try again after loading completes.");
		return false;
	}
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
			globals::menu->ResetSettingsDirtyState();
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

	permutationCB = new ConstantBuffer(ConstantBufferDesc<PermutationCB>(), "State::Permutation");
	sharedDataCB = new ConstantBuffer(ConstantBufferDesc<SharedDataCB>(), "State::SharedData");

	const auto featureDataSize = GetFeatureBufferData(false).second;
	featureDataCB = new ConstantBuffer(ConstantBufferDesc((uint32_t)featureDataSize), "State::FeatureData");

	// Grab main texture to get resolution
	// Use the physical main texture so dynamic resize and target recreation agree.
	screenSize = GetMainRenderTargetSize();
	if (globals::d3d::context) {
		globals::d3d::context->QueryInterface(
			__uuidof(REX::W32::ID3DUserDefinedAnnotation),
			reinterpret_cast<void**>(pPerf.ReleaseAndGetAddressOf()));
	}

	if (globals::profiler && globals::d3d::device && globals::d3d::context) {
		globals::profiler->Initialize(globals::d3d::device, globals::d3d::context);
		if (frameAnnotations) {
			globals::profiler->SetPerfEventCallbacks(
				[this](std::string_view a_title) { BeginPerfEvent(a_title); },
				[this](std::string_view) { EndPerfEvent(); });
		} else {
			globals::profiler->SetPerfEventCallbacks({}, {});
		}
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
	const auto categoryFlag = SIE::ShaderCache::GetCharacterCategoryFlag(a_shader.shaderType.get());
	a_vertexDescriptor &= ~categoryFlag;
	a_pixelDescriptor &= ~categoryFlag;
	if ((deferred->deferredPass || a_forceDeferred) && deferred->IsCharacterCategoryEnabled())
		a_pixelDescriptor |= categoryFlag;

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
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::TruePbr |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::LandscapeSeams);

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
	} else if (a_shader.shaderType.get() == RE::BSShader::Type::Utility) {
		a_vertexDescriptor &= ~static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::TruePbr);
	}
}

void State::BeginPerfEvent(std::string_view title)
{
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NVPERF_ENABLED)
	NvPerf::PushRange(title);
#endif
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
	if (pPerf.Get()) {
		thread_local std::wstring wideTitle;
		pPerf->BeginEvent(WidenAnnotation(wideTitle, title));
	}
}

void State::EndPerfEvent()
{
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NVPERF_ENABLED)
	NvPerf::PopRange();
#endif
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
	if (pPerf.Get()) {
		thread_local std::wstring wideMarker;
		pPerf->SetMarker(WidenAnnotation(wideMarker, title));
	}
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

		if (a_inWorld && !isMainMenuOpen && !isLoadingMenuOpen)
			TextureStreaming::Instance().CaptureWorldDemand(data.MipBias);

		data.RefractionScale = refractionScale;
		const auto& volumetricShadows = globals::features::volumetricShadows;
		data.VolumetricShadowsEnabled = volumetricShadows.loaded && volumetricShadows.settings.Enabled &&
		                                volumetricShadows.HasSharedShadowMapResources();
		const auto godrayProfile = a_inWorld ?
		                               globals::features::volumetricLighting.GetRuntimeGodrayProfile() :
		                               VolumetricLighting::GodrayProfile{};
		data.VolumetricLightingOpacity = godrayProfile.Opacity;
		data.VolumetricLightingSaturation = godrayProfile.Saturation;
		data.VolumetricLightingClarity = { a_inWorld ? globals::features::adaptiveBrightness.GetEffectiveVolumetricClarity() : AdaptiveBalanceGodray::kClarityDefault, godrayProfile.ShaftIntensity, 0.0f, 0.0f };
		data.VolumetricLightingCustomColor = {
			godrayProfile.CustomColorRed,
			godrayProfile.CustomColorGreen,
			godrayProfile.CustomColorBlue,
			godrayProfile.CustomColorContribution
		};

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
	if (!Util::IsInterior())
		return true;

	return globals::features::interiorSun.IsActiveInteriorSun();
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
	const uint64_t publicationGeneration =
		BeginRenderTargetResourcePublication();

	const auto mainRenderTargetSize = GetMainRenderTargetSize();
	if (mainRenderTargetSize.x > 0.0f && mainRenderTargetSize.y > 0.0f) {
		screenSize = mainRenderTargetSize;
	}
	featureLevel = globals::d3d::device->GetFeatureLevel();

	// Resize only resources tied to recreated render targets.
	// Keep disk/world discovery and full feature setup on State::Setup().
	uint32_t loadedFeatureSetupCount = 0;
	Feature::ForEachLoadedFeature("SetupRenderTargetResources", [&](Feature* feature) {
		feature->SetupRenderTargetResources();
		++loadedFeatureSetupCount;
	});
	globals::deferred->SetupResources();
	CompleteRenderTargetResourcePublication(
		publicationGeneration,
		loadedFeatureSetupCount);
}

uint64_t State::BeginRenderTargetResourcePublication() noexcept
{
	const uint64_t previousGeneration =
		renderTargetResourcePublicationGeneration.fetch_add(
			1,
			std::memory_order_acq_rel);
	return previousGeneration + 1;
}

void State::InvalidateRenderTargetResourcePublication() noexcept
{
	(void)BeginRenderTargetResourcePublication();
}

void State::CompleteRenderTargetResourcePublication(
	uint64_t a_generation,
	uint32_t a_loadedFeatureSetupCount)
{
	// The publication proves only the coordinated setup boundary: every loaded
	// feature callback returned and Deferred completed for this main-target size.
	// Exact required engine targets remain validated by the render-scale probe;
	// optional feature degradation remains owned by each feature.
	if (a_generation == 0 ||
		a_generation != renderTargetResourcePublicationGeneration.load(
							std::memory_order_acquire)) {
		return;
	}

	const auto mainRenderTargetSize = GetMainRenderTargetSize();
	const uint32_t width = mainRenderTargetSize.x > 0.0f ?
	                           static_cast<uint32_t>(mainRenderTargetSize.x) :
	                           0u;
	const uint32_t height = mainRenderTargetSize.y > 0.0f ?
	                            static_cast<uint32_t>(mainRenderTargetSize.y) :
	                            0u;
	auto publication = std::make_shared<RenderTargetResourcePublication>();
	publication->generation = a_generation;
	publication->width = width;
	publication->height = height;
	publication->loadedFeatureSetupCount = a_loadedFeatureSetupCount;
	publication->device = globals::d3d::device;
	publication->context = globals::d3d::context;
	publication->deferredSetupAcknowledged = true;
	publication->complete =
		width != 0 &&
		height != 0 &&
		publication->device != nullptr &&
		publication->context != nullptr &&
		publication->device == setupResourcesDevice &&
		publication->context == setupResourcesContext &&
		publication->deferredSetupAcknowledged;
	std::shared_ptr<const RenderTargetResourcePublication> completedPublication =
		std::move(publication);
	auto observedPublication = GetRenderTargetResourcePublication();
	while (true) {
		if (a_generation != renderTargetResourcePublicationGeneration.load(
								std::memory_order_acquire)) {
			return;
		}
		if (observedPublication &&
			observedPublication->generation >= a_generation) {
			return;
		}
		if (renderTargetResourcePublication.compare_exchange_weak(
				observedPublication,
				completedPublication,
				std::memory_order_release,
				std::memory_order_acquire)) {
			break;
		}
	}
	if (a_generation == renderTargetResourcePublicationGeneration.load(
							std::memory_order_acquire)) {
		completedRenderTargetResourcePublicationGeneration.store(
			a_generation,
			std::memory_order_release);
	}
}

uint64_t State::GetCompletedRenderTargetResourcePublicationGeneration() const
{
	const auto publication = GetRenderTargetResourcePublication();
	if (!publication || !publication->complete)
		return 0u;

	const uint64_t generation = publication->generation;
	const bool isCurrent =
		generation != 0 &&
		generation == renderTargetResourcePublicationGeneration.load(
						  std::memory_order_acquire) &&
		generation == completedRenderTargetResourcePublicationGeneration.load(
						  std::memory_order_acquire);
	return isCurrent ? generation : 0u;
}

bool State::HasCompleteRenderTargetResourcePublication(
	uint32_t a_width,
	uint32_t a_height) const
{
	return GetRenderTargetResourcePublicationDiagnostics(a_width, a_height).current;
}

State::RenderTargetResourcePublicationDiagnostics State::GetRenderTargetResourcePublicationDiagnostics(
	uint32_t a_expectedWidth,
	uint32_t a_expectedHeight) const noexcept
{
	RenderTargetResourcePublicationDiagnostics diagnostics{
		.expectedWidth = a_expectedWidth,
		.expectedHeight = a_expectedHeight,
		.evaluated = true,
	};
	const auto publication = GetRenderTargetResourcePublication();
	diagnostics.currentGeneration = renderTargetResourcePublicationGeneration.load(
		std::memory_order_acquire);
	diagnostics.completedGeneration = completedRenderTargetResourcePublicationGeneration.load(
		std::memory_order_acquire);
	if (!publication)
		return diagnostics;

	diagnostics.publishedGeneration = publication->generation;
	diagnostics.publishedWidth = publication->width;
	diagnostics.publishedHeight = publication->height;
	diagnostics.loadedFeatureSetupCount = publication->loadedFeatureSetupCount;
	diagnostics.present = publication->generation != 0;
	diagnostics.complete = publication->complete;
	diagnostics.deferredSetupAcknowledged = publication->deferredSetupAcknowledged;
	diagnostics.generationMatchesCurrent =
		diagnostics.present &&
		publication->generation == diagnostics.currentGeneration;
	diagnostics.generationMatchesCompleted =
		diagnostics.present &&
		publication->generation == diagnostics.completedGeneration;
	diagnostics.dimensionsMatch = diagnostics.present &&
	                              publication->width == a_expectedWidth &&
	                              publication->height == a_expectedHeight;
	diagnostics.deviceMatches = diagnostics.present &&
	                            publication->device != nullptr &&
	                            publication->device == globals::d3d::device;
	diagnostics.contextMatches = diagnostics.present &&
	                             publication->context != nullptr &&
	                             publication->context == globals::d3d::context;
	diagnostics.current = diagnostics.complete &&
	                      diagnostics.generationMatchesCurrent &&
	                      diagnostics.generationMatchesCompleted &&
	                      diagnostics.dimensionsMatch &&
	                      diagnostics.deviceMatches &&
	                      diagnostics.contextMatches;
	return diagnostics;
}

State::RenderTargetResourcePublicationDiagnostics
State::GetCurrentMainRenderTargetResourcePublicationDiagnostics() const noexcept
{
	const auto mainRenderTargetSize = GetMainRenderTargetSize();
	const uint32_t width = mainRenderTargetSize.x > 0.0f ?
	                           static_cast<uint32_t>(mainRenderTargetSize.x) :
	                           0u;
	const uint32_t height = mainRenderTargetSize.y > 0.0f ?
	                            static_cast<uint32_t>(mainRenderTargetSize.y) :
	                            0u;
	return GetRenderTargetResourcePublicationDiagnostics(width, height);
}

void State::RefreshDrawAnnotations()
{
	drawAnnotationsActive = frameAnnotations &&
	                        ((pPerf.Get() && pPerf->GetStatus()) ||
								GetModuleHandleW(L"renderdoc.dll") ||
								GetModuleHandleW(L"WinPixGpuCapturer.dll"));
}

void State::BeginDrawEvent(std::string_view title)
{
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NVPERF_ENABLED)
	NvPerf::PushRange(title);
#endif
	// Keep high-frequency draw annotations available to RenderDoc/PIX without
	// creating a dynamic Tracy source location for every geometry draw.
	if (pPerf.Get()) {
		thread_local std::wstring wideTitle;
		pPerf->BeginEvent(WidenAnnotation(wideTitle, title));
	}
}

void State::EndDrawEvent()
{
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NVPERF_ENABLED)
	NvPerf::PopRange();
#endif
	if (pPerf.Get())
		pPerf->EndEvent();
}

void State::UpdateEffectShaderPermutation(RE::BSRenderPass* a_pass)
{
	constexpr auto isAurora = static_cast<uint32_t>(ExtraShaderDescriptors::IsAurora);
	permutationData.ExtraShaderDescriptor &= ~isAurora;
	if (!a_pass || !a_pass->geometry ||
		!(currentVertexDescriptor & static_cast<uint32_t>(SIE::ShaderCache::EffectShaderFlags::SkyObject)))
		return;
	const auto sky = globals::game::sky;
	if (!sky || !sky->auroraRoot)
		return;
	for (const RE::NiAVObject* node = a_pass->geometry; node; node = node->parent) {
		if (node == sky->auroraRoot.get()) {
			permutationData.ExtraShaderDescriptor |= isAurora;
			return;
		}
	}
}
