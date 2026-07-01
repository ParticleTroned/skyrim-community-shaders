#include "Globals.h"

#include "Deferred.h"
#include "Features/CSEditor.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
#include "Features/HDRDisplay.h"
#include "Features/HairSpecular.h"
#include "Features/IBL.h"
#include "Features/InteriorSun.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/LinearLighting.h"
#include "Features/PerformanceOverlay.h"
#include "Features/RemoteControl.h"
#include "Features/RenderDoc.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/ScreenshotFeature.h"
#include "Features/Skin.h"
#include "Features/SkySync.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/TerrainShadows.h"
#include "Features/TerrainVariation.h"
#include "Features/UnifiedWater.h"
#include "Features/Upscaling.h"
#include "Features/VolumetricLighting.h"
#include "Features/VolumetricShadows.h"
#include "Features/WaterEffects.h"
#include "Features/WetnessEffects.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Utils/Game.h"

namespace globals
{
	namespace d3d
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		IDXGISwapChain* swapChain = nullptr;
	}

	namespace features
	{
		CloudShadows cloudShadows{};
		DynamicCubemaps dynamicCubemaps{};
		VolumetricShadows volumetricShadows{};
		ExtendedMaterials extendedMaterials{};
		GrassCollision grassCollision{};
		GrassLighting grassLighting{};
		IBL ibl{};
		LightLimitFix lightLimitFix{};
		LinearLighting linearLighting{};
		LODBlending lodBlending{};
		HairSpecular hairSpecular{};
		InteriorSun interiorSun{};
		InverseSquareLighting inverseSquareLighting{};
		ScreenSpaceGI screenSpaceGI{};
		ScreenSpaceShadows screenSpaceShadows{};
		Skylighting skylighting{};
		TerrainVariation terrainVariation{};
		SkySync skySync{};
		SubsurfaceScattering subsurfaceScattering{};
		TerrainBlending terrainBlending{};
		TerrainHelper terrainHelper{};
		TerrainShadows terrainShadows{};
		UnifiedWater unifiedWater{};
		VolumetricLighting volumetricLighting{};
		WaterEffects waterEffects{};
		PerformanceOverlay performanceOverlay{};
		WetnessEffects wetnessEffects{};
		ExtendedTranslucency extendedTranslucency{};
		Upscaling upscaling{};
		HDRDisplay hdrDisplay{};
		RenderDoc renderDoc{};
		RemoteControl remoteControl{};
		ScreenshotFeature screenshotFeature{};
		CSEditor csEditor{};
		ExponentialHeightFog exponentialHeightFog{};
		TruePBR truePBR{};
		Skin skin{};

		namespace llf
		{
		}
	}

	namespace game
	{
		RE::BSGraphics::RendererShadowState* shadowState = nullptr;
		RE::BSGraphics::State* graphicsState = nullptr;
		RE::BSGraphics::Renderer* renderer = nullptr;
		RE::BSShaderManager::State* smState = nullptr;
		RE::TES* tes = nullptr;
		RE::MemoryManager* memoryManager = nullptr;
		RE::INISettingCollection* iniSettingCollection = nullptr;
		RE::INIPrefSettingCollection* iniPrefSettingCollection = nullptr;
		RE::GameSettingCollection* gameSettingCollection = nullptr;
		float* cameraNear = nullptr;
		float* cameraFar = nullptr;
		float* deltaTime = nullptr;
		RE::BSUtilityShader* utilityShader = nullptr;
		RE::PlayerCharacter* player = nullptr;
		RE::Sky* sky = nullptr;
		RE::UI* ui = nullptr;
		RE::Calendar* calendar = nullptr;
		RE::ImageSpaceManager* imageSpaceManager = nullptr;
		bool* bEnableVolumetricLighting = nullptr;
		std::atomic<bool> quitGame{ false };

		RE::BSGraphics::PixelShader** currentPixelShader = nullptr;
		RE::BSGraphics::VertexShader** currentVertexShader = nullptr;
		REX::EnumSet<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags = nullptr;

		RE::Setting* bEnableLandFade = nullptr;
		RE::Setting* bShadowsOnGrass = nullptr;
		RE::Setting* shadowMaskQuarter = nullptr;

		REL::Relocation<ID3D11Buffer**> perFrame;
		REL::Relocation<RE::BSGraphics::BSShaderAccumulator**> currentAccumulator;

		FrameBufferCache frameBufferCached{};
	}

	static void RefreshTES()
	{
		if (auto tes = RE::TES::GetSingleton())
			game::tes = tes;
	}

	namespace rtti
	{
		REL::Relocation<const RE::NiRTTI*> NiIntegerExtraDataRTTI;
		REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI;
		REL::Relocation<const RE::NiRTTI*> BSEffectShaderPropertyRTTI;
		REL::Relocation<const RE::NiRTTI*> BSWaterShaderPropertyRTTI;
		REL::Relocation<const RE::NiRTTI*> NiParticleSystemRTTI;
		REL::Relocation<const RE::NiRTTI*> NiBillboardNodeRTTI;
		REL::Relocation<const RE::NiRTTI*> NiAlphaPropertyRTTI;
		REL::Relocation<const RE::NiRTTI*> NiSourceTextureRTTI;
	}

	State* state = nullptr;
	Deferred* deferred = nullptr;
	Menu* menu = nullptr;
	ShaderCache* shaderCache = nullptr;

	static Profiler profilerInstance;
	Profiler* profiler = &profilerInstance;

	void OnInit()
	{
		shaderCache = &ShaderCache::Instance();
		state = State::GetSingleton();
		menu = Menu::GetSingleton();
		deferred = Deferred::GetSingleton();
	}

	void ReInit()
	{
		{
			using namespace game;

			shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			graphicsState = RE::BSGraphics::State::GetSingleton();
			renderer = RE::BSGraphics::Renderer::GetSingleton();
			smState = &RE::BSShaderManager::State::GetSingleton();
			iniSettingCollection = RE::INISettingCollection::GetSingleton();
			iniPrefSettingCollection = RE::INIPrefSettingCollection::GetSingleton();
			gameSettingCollection = RE::GameSettingCollection::GetSingleton();
			RefreshTES();
			cameraNear = (float*)(REL::RelocationID(517032, 403540).address() + 0x40);
			cameraFar = (float*)(REL::RelocationID(517032, 403540).address() + 0x44);
			deltaTime = (float*)REL::RelocationID(523660, 410199).address();

			currentPixelShader = &(shadowState->GetRuntimeData().currentPixelShader);
			currentVertexShader = &(shadowState->GetRuntimeData().currentVertexShader);
			stateUpdateFlags = &(shadowState->GetRuntimeData().stateUpdateFlags);

			ui = RE::UI::GetSingleton();
			calendar = RE::Calendar::GetSingleton();
			perFrame = { REL::RelocationID(524768, 411384) };

			currentAccumulator = { REL::RelocationID(527650, 414600) };
		}

		{
			using namespace rtti;
			NiIntegerExtraDataRTTI = { RE::NiIntegerExtraData::Ni_RTTI };
			BSLightingShaderPropertyRTTI = { RE::BSLightingShaderProperty::Ni_RTTI };
			BSEffectShaderPropertyRTTI = { RE::BSEffectShaderProperty::Ni_RTTI };
			BSWaterShaderPropertyRTTI = { RE::BSWaterShaderProperty::Ni_RTTI };
			NiParticleSystemRTTI = { RE::NiParticleSystem::Ni_RTTI };
			NiBillboardNodeRTTI = { RE::NiBillboardNode::Ni_RTTI };
			NiAlphaPropertyRTTI = { RE::NiAlphaProperty::Ni_RTTI };
			NiSourceTextureRTTI = { RE::NiSourceTexture::Ni_RTTI };
		}

		d3d::device = reinterpret_cast<ID3D11Device*>(game::renderer->GetRuntimeData().forwarder);
		d3d::context = reinterpret_cast<ID3D11DeviceContext*>(game::renderer->GetRuntimeData().context);
		d3d::swapChain = reinterpret_cast<IDXGISwapChain*>(game::renderer->GetRuntimeData().renderWindows->swapChain);
	}

	void OnDataLoaded()
	{
		using namespace game;
		RefreshTES();
		player = RE::PlayerCharacter::GetSingleton();
		sky = RE::Sky::GetSingleton();
		utilityShader = RE::BSUtilityShader::GetSingleton();
		imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		bEnableVolumetricLighting = reinterpret_cast<bool*>(REL::RelocationID(527940, 414913).address());

		bEnableLandFade = iniSettingCollection->GetSetting("bEnableLandFade:Display");

		bShadowsOnGrass = RE::GetINISetting("bShadowsOnGrass:Display");
		shadowMaskQuarter = RE::GetINISetting("iShadowMaskQuarter:Display");
	}

	void OnGameWindowClose()
	{
		game::quitGame = true;
		if (shaderCache)
			shaderCache->StopCompilation();
	}

	// --- FrameBufferCache: serve per-frame camera data live from the game struct (RendererShadowState).
	// The constant buffer stored these matrices transposed vs the game's row-major ViewData, and posAdjust
	// maps 1:1 with w=0 (verified at runtime and confirmed identical in-game), so we transpose / repack. ---

#define CS_CAMERA_MAT(LIVE_EXPR)                                                                   \
	auto* ss = game::shadowState;                                                                 \
	return ss ? (ss->GetRuntimeData().cameraData.getEye().LIVE_EXPR) : Matrix::Identity;

	Matrix FrameBufferCache::GetCameraView() const { CS_CAMERA_MAT(viewMat.Transpose()) }
	Matrix FrameBufferCache::GetCameraProj() const { CS_CAMERA_MAT(projMat.Transpose()) }
	Matrix FrameBufferCache::GetCameraViewProj() const { CS_CAMERA_MAT(viewProjMat.Transpose()) }
	Matrix FrameBufferCache::GetCameraViewProjUnjittered() const { CS_CAMERA_MAT(viewProjMatrixUnjittered.Transpose()) }
	Matrix FrameBufferCache::GetCameraPreviousViewProjUnjittered() const { CS_CAMERA_MAT(previousViewProjMatrixUnjittered.Transpose()) }
	Matrix FrameBufferCache::GetCameraProjUnjittered() const { CS_CAMERA_MAT(projMatrixUnjittered.Transpose()) }
	Matrix FrameBufferCache::GetCameraProjUnjitteredInverse() const { CS_CAMERA_MAT(projMatrixUnjittered.Invert().Transpose()) }
	Matrix FrameBufferCache::GetCameraViewInverse() const { CS_CAMERA_MAT(viewMat.Invert().Transpose()) }
	Matrix FrameBufferCache::GetCameraViewProjInverse() const { CS_CAMERA_MAT(viewProjMat.Invert().Transpose()) }
	Matrix FrameBufferCache::GetCameraProjInverse() const { CS_CAMERA_MAT(projMat.Invert().Transpose()) }

#undef CS_CAMERA_MAT

	float4 FrameBufferCache::GetCameraPosAdjust() const
	{
		auto* ss = game::shadowState;
		if (!ss)
			return {};
		auto p = ss->GetRuntimeData().posAdjust.getEye();
		return { p.x, p.y, p.z, 0.0f };
	}

	float4 FrameBufferCache::GetCameraPreviousPosAdjust() const
	{
		auto* ss = game::shadowState;
		if (!ss)
			return {};
		auto p = ss->GetRuntimeData().previousPosAdjust.getEye();
		return { p.x, p.y, p.z, 0.0f };
	}
}
