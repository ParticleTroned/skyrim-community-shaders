#pragma once

#include <atomic>

struct CloudShadows;
struct DynamicCubemaps;
struct VolumetricShadows;
struct ExtendedMaterials;
struct GrassCollision;
struct GrassLighting;
struct HairSpecular;
struct IBL;
struct LightLimitFix;
struct LinearLighting;
struct LODBlending;
struct InteriorSun;
struct InverseSquareLighting;
struct ScreenSpaceGI;
struct ScreenSpaceShadows;
struct Skylighting;
struct TerrainVariation;
struct SkySync;
struct SubsurfaceScattering;
struct TerrainBlending;
struct TerrainHelper;
struct TerrainShadows;
struct UnifiedWater;
struct VolumetricLighting;
struct WaterEffects;
struct PerformanceOverlay;
struct WetnessEffects;
struct ExtendedTranslucency;
struct Upscaling;
class Profiler;
struct CSEditor;
struct ExponentialHeightFog;
struct HDRDisplay;
struct ScreenshotFeature;
struct Skin;

class State;
class Deferred;
struct TruePBR;
class RenderDoc;
class RemoteControl;
class Menu;

class ShaderCache;
class ShaderFileDependencyTracker;

/**
 * @brief Initializes core singletons (ShaderCache, State, Menu, Deferred).
 */
void OnInit();

/**
 * @brief Resolves runtime game pointers, RTTI relocations, and D3D device references.
 */
void ReInit();

/**
 * @brief Caches late-binding game singletons (player, sky, INI settings).
 */
void OnDataLoaded();

/**
 * @brief Signals shader compilation to stop.
 */
void OnGameWindowClose();

namespace globals
{
	namespace d3d
	{
		extern ID3D11Device* device;
		extern ID3D11DeviceContext* context;
		extern IDXGISwapChain* swapChain;
	}

	namespace features
	{
		extern CloudShadows cloudShadows;
		extern DynamicCubemaps dynamicCubemaps;
		extern VolumetricShadows volumetricShadows;
		extern ExtendedMaterials extendedMaterials;
		extern GrassCollision grassCollision;
		extern GrassLighting grassLighting;
		extern HairSpecular hairSpecular;
		extern IBL ibl;
		extern LightLimitFix lightLimitFix;
		extern LinearLighting linearLighting;
		extern LODBlending lodBlending;
		extern InteriorSun interiorSun;
		extern InverseSquareLighting inverseSquareLighting;
		extern ScreenSpaceGI screenSpaceGI;
		extern ScreenSpaceShadows screenSpaceShadows;
		extern Skylighting skylighting;
		extern TerrainVariation terrainVariation;
		extern SkySync skySync;
		extern SubsurfaceScattering subsurfaceScattering;
		extern TerrainBlending terrainBlending;
		extern TerrainHelper terrainHelper;
		extern TerrainShadows terrainShadows;
		extern UnifiedWater unifiedWater;
		extern VolumetricLighting volumetricLighting;
		extern WaterEffects waterEffects;
		extern PerformanceOverlay performanceOverlay;
		extern WetnessEffects wetnessEffects;
		extern ExtendedTranslucency extendedTranslucency;
		extern Upscaling upscaling;
		extern HDRDisplay hdrDisplay;
		extern RenderDoc renderDoc;
		extern RemoteControl remoteControl;
		extern ScreenshotFeature screenshotFeature;
		extern CSEditor csEditor;
		extern ExponentialHeightFog exponentialHeightFog;
		extern TruePBR truePBR;
		extern Skin skin;

	}

	/**
	 * @brief Per-frame camera data read directly from the game (RendererShadowState::cameraData +
	 *        posAdjust), replacing the old approach of copying Skyrim's per-frame constant buffer via
	 *        D3D11 Map/Unmap hooks.
	 *
	 * The game's ViewData matrices are row-major; the constant buffer stored them transposed (verified at
	 * runtime, and confirmed identical in-game), and posAdjust maps 1:1 with w=0 — so the accessors
	 * transpose / repack here. Stateless: every accessor reads the live game struct.
	 */
	struct FrameBufferCache
	{
		Matrix GetCameraView() const;
		Matrix GetCameraProj() const;
		Matrix GetCameraViewProj() const;
		Matrix GetCameraViewProjUnjittered() const;
		Matrix GetCameraPreviousViewProjUnjittered() const;
		Matrix GetCameraProjUnjittered() const;
		Matrix GetCameraProjUnjitteredInverse() const;
		Matrix GetCameraViewInverse() const;
		Matrix GetCameraViewProjInverse() const;
		Matrix GetCameraProjInverse() const;
		float4 GetCameraPosAdjust() const;
		float4 GetCameraPreviousPosAdjust() const;
	};

	namespace game
	{
		extern RE::BSGraphics::RendererShadowState* shadowState;
		extern RE::BSGraphics::State* graphicsState;
		extern RE::BSGraphics::Renderer* renderer;
		extern RE::BSShaderManager::State* smState;
		extern RE::TES* tes;
		extern RE::MemoryManager* memoryManager;
		extern RE::INISettingCollection* iniSettingCollection;
		extern RE::INIPrefSettingCollection* iniPrefSettingCollection;
		extern RE::GameSettingCollection* gameSettingCollection;
		extern float* cameraNear;
		extern float* cameraFar;
		extern float* deltaTime;
		extern RE::BSUtilityShader* utilityShader;
		extern RE::PlayerCharacter* player;
		extern RE::Sky* sky;
		extern RE::UI* ui;
		extern RE::Calendar* calendar;
		extern RE::ImageSpaceManager* imageSpaceManager;
		extern bool* bEnableVolumetricLighting;
		extern std::atomic<bool> quitGame;

		extern RE::BSGraphics::PixelShader** currentPixelShader;
		extern RE::BSGraphics::VertexShader** currentVertexShader;
		extern REX::EnumSet<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags;

		extern RE::Setting* bEnableLandFade;
		extern RE::Setting* bShadowsOnGrass;
		extern RE::Setting* shadowMaskQuarter;
		extern REL::Relocation<ID3D11Buffer**> perFrame;
		extern REL::Relocation<RE::BSGraphics::BSShaderAccumulator**> currentAccumulator;

		extern FrameBufferCache frameBufferCached;
	}

	namespace rtti
	{
		extern REL::Relocation<const RE::NiRTTI*> NiIntegerExtraDataRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSEffectShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSWaterShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiParticleSystemRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiBillboardNodeRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiAlphaPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiSourceTextureRTTI;
	}

	extern State* state;
	extern Deferred* deferred;
	extern Menu* menu;
	extern ShaderCache* shaderCache;
	extern Profiler* profiler;

	/** @brief Initializes core singletons (ShaderCache, State, Menu, Deferred). Called once at plugin load. */
	void OnInit();
	/** @brief Resolves runtime game pointers, RTTI relocations, and D3D device references. Called when the renderer is ready. */
	void ReInit();
	/** @brief Caches late-binding game singletons (player, sky, INI settings) after Skyrim's data files are loaded. */
	void OnDataLoaded();
	/** @brief Signals shader compilation to stop when the game window is closing. */
	void OnGameWindowClose();
}