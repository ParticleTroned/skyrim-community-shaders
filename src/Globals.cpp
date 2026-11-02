#include "Globals.h"
#include "Features/GrassOptimizations.h"
#include "Features/NeuralRenderingFeature.h"
#include "Features/ReverseZ.h"
#include "Features/TerrainSeams.h"
#include "Features/Upscaling/NeuralRendering/ExposureCapture.h"
#include "Features/WeatherPicker.h"
#include "Features/Wetterness.h"

#include "Deferred.h"
#include "EngineFixes/ShadowmapCascadeRasterizerFix.h"
#include "Features/AdaptiveBrightness.h"
#include "Features/CSEditor.h"
#include "Features/CSUtility.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/FoliageLighting.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
#include "Features/HDRDisplay.h"
#include "Features/HairSpecular.h"
#include "Features/HorizonFix.h"
#include "Features/IBL.h"
#include "Features/InteriorSun.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/LightLimitFix/ParticleLights.h"
#include "Features/LinearLighting.h"
#include "Features/PerformanceOverlay.h"
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
#include "Features/UnderwaterDepthOfField.h"
#include "Features/UnifiedWater.h"
#include "Features/Upscaling.h"
#include "Features/VanillaFresnel.h"
#include "Features/VolumetricLighting.h"
#include "Features/VolumetricShadows.h"
#include "Features/WaterEffects.h"
#include "Features/WetnessEffects.h"
#include "Menu.h"
#include "Profiler.h"
#include "SceneSettingsManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Utils/Game.h"
#include "WeatherManager.h"
#include <mutex>

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
		GrassOptimizations grassOptimizations{};
		Wetterness wetterness{};
		WeatherPicker weatherPicker{};
		NeuralRenderingFeature neuralRendering{};
		AdaptiveBrightness adaptiveBrightness{};
		CSUtility csUtility{};
		ReverseZ reverseZ{};
		CloudShadows cloudShadows{};
		DynamicCubemaps dynamicCubemaps{};
		ExtendedMaterials extendedMaterials{};
		FoliageLighting foliageLighting{};
		GrassCollision grassCollision{};
		GrassLighting grassLighting{};
		IBL ibl{};
		LightLimitFix lightLimitFix{};
		LinearLighting linearLighting{};
		LODBlending lodBlending{};
		HairSpecular hairSpecular{};
		HorizonFix horizonFix{};
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
		VanillaFresnel vanillaFresnel{};
		VolumetricLighting volumetricLighting{};
		WaterEffects waterEffects{};
		PerformanceOverlay performanceOverlay{};
		WetnessEffects wetnessEffects{};
		ExtendedTranslucency extendedTranslucency{};
		Upscaling upscaling{};
		RenderDoc renderDoc{};
		VolumetricShadows volumetricShadows{};
		CSEditor csEditor{};
		TruePBR truePBR{};
		HDRDisplay hdrDisplay{};
		ScreenshotFeature screenshotFeature{};
		ExponentialHeightFog exponentialHeightFog{};
		Skin skin{};

		namespace llf
		{
			ParticleLights particleLights{};
		}
	}

	namespace game
	{
		RE::BSGraphics::RendererShadowState* shadowState = nullptr;
		RE::BSGraphics::State* graphicsState = nullptr;
		RE::BSGraphics::Renderer* renderer = nullptr;
		RE::BSShaderManager::State* smState = nullptr;
		RE::TES* tes = nullptr;
		RE::TESWaterSystem* waterSystem = nullptr;
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

		D3D11_MAPPED_SUBRESOURCE* mappedFrameBuffer = nullptr;
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
	SIE::ShaderCache* shaderCache = nullptr;
	WeatherManager* weatherManager = nullptr;
	SceneSettingsManager* sceneSettingsManager = nullptr;
	static Profiler profilerInstance;
	Profiler* profiler = &profilerInstance;

	void OnInit()
	{
		game::quitGame = false;
		shaderCache = &SIE::ShaderCache::Instance();
		state = State::GetSingleton();
		menu = Menu::GetSingleton();
		deferred = Deferred::GetSingleton();
		weatherManager = WeatherManager::GetSingleton();
		sceneSettingsManager = SceneSettingsManager::GetSingleton();
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
			waterSystem = RE::TESWaterSystem::GetSingleton();
			cameraNear = (float*)(REL::RelocationID(517032, 403540).address() + 0x40);
			cameraFar = (float*)(REL::RelocationID(517032, 403540).address() + 0x44);
			deltaTime = (float*)REL::RelocationID(523660, 410199).address();

			if (shadowState) {
				currentPixelShader = &shadowState->GetRuntimeData().currentPixelShader;
				currentVertexShader = &shadowState->GetRuntimeData().currentVertexShader;
				stateUpdateFlags = &shadowState->GetRuntimeData().stateUpdateFlags;
			}

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
		waterSystem = RE::TESWaterSystem::GetSingleton();
		imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		bEnableVolumetricLighting = reinterpret_cast<bool*>(REL::RelocationID(527940, 414913).address());

		bEnableLandFade = iniSettingCollection->GetSetting("bEnableLandFade:Display");

		bShadowsOnGrass = RE::GetINISetting("bShadowsOnGrass:Display");
		shadowMaskQuarter = RE::GetINISetting("iShadowMaskQuarter:Display");
	}

	void OnGameWindowClose()
	{
		if (!game::quitGame.exchange(true, std::memory_order_acq_rel)) {
			if (shaderCache)
				shaderCache->StopCompilation();
			features::upscaling.RequestBackendShutdown();
		}
	}

	/**
 * @brief Caches the current frame buffer data and clears the mapped pointer.
 *
 * Copies the contents of the mapped frame buffer into an internal cache and resets the mapped frame buffer pointer.
 */
	void CacheFramebuffer()
	{
		using namespace game;
		auto frameBuffer = (FrameBuffer*)mappedFrameBuffer->pData;
		frameBufferCached.data = *frameBuffer;
		mappedFrameBuffer = nullptr;
	}

	/**
 * @brief Hooks the ID3D11DeviceContext::Map method to track mapping of the per-frame resource.
 *
 * Calls the original Map function and, if the mapped resource matches the current per-frame buffer, stores the mapped subresource pointer for later use.
 *
 * @return HRESULT Result of the original Map call.
 */
	struct ID3D11DeviceContext_Map
	{
		static HRESULT thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
		{
			HRESULT hr = func(This, pResource, Subresource, MapType, MapFlags, pMappedResource);
			if (hr == S_OK) {
				if (This == d3d::context && Subresource == 0 && *globals::game::perFrame.get() == pResource)
					globals::game::mappedFrameBuffer = pMappedResource;
			}
			return hr;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/**
 * @brief Hooked implementation of ID3D11DeviceContext::Unmap that caches the frame buffer if applicable.
 *
 * If the resource being unmapped matches the current per-frame buffer and a mapped frame buffer is present, caches the frame buffer data before calling the original Unmap function.
 */
	struct ID3D11DeviceContext_Unmap
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource)
		{
			if (This == d3d::context && Subresource == 0 && *globals::game::perFrame.get() == pResource && globals::game::mappedFrameBuffer) {
				auto* bytes = globals::game::mappedFrameBuffer->pData;
				if (bytes) {
					features::reverseZ.FixupMappedFrameBuffer(*static_cast<FrameBuffer*>(bytes));
					CacheFramebuffer();
				}
			}
			func(This, pResource, Subresource);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/**
 * @brief Installs hooks on the Map and Unmap methods of the provided D3D11 device context.
 *
 * This enables interception of resource mapping and unmapping operations for frame buffer caching.
 */
	struct ID3D11DeviceContext_DrawIndexed
	{
		static void thunk(ID3D11DeviceContext* This, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
		{
			UnderwaterDepthOfField::BeforeDraw();
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Indexed);
			func(This, IndexCount, StartIndexLocation, BaseVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Draw
	{
		static void thunk(ID3D11DeviceContext* This, UINT VertexCount, UINT StartVertexLocation)
		{
			UnderwaterDepthOfField::BeforeDraw();
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Direct);
			func(This, VertexCount, StartVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstanced
	{
		static void thunk(ID3D11DeviceContext* This, UINT indexCount, UINT instanceCount, UINT startIndex, INT baseVertex, UINT startInstance)
		{
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::IndexedInstanced);
			func(This, indexCount, instanceCount, startIndex, baseVertex, startInstance);
#ifdef DEVBENCH_BRIDGE_ENABLED
			const GrassCollisionVertexProbe::Draw draw{ indexCount, instanceCount, startIndex, startInstance, baseVertex };
			globals::features::grassCollision.ObserveVertexDraw(This, draw, func.get(), nullptr);
#endif
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawInstanced
	{
		static void thunk(ID3D11DeviceContext* This, UINT vertexCount, UINT instanceCount, UINT startVertex, UINT startInstance)
		{
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Instanced);
			func(This, vertexCount, instanceCount, startVertex, startInstance);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawAuto
	{
		static void thunk(ID3D11DeviceContext* This)
		{
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Auto);
			func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstancedIndirect
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Buffer* arguments, UINT offset)
		{
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::IndexedIndirect);
			func(This, arguments, offset);
#ifdef DEVBENCH_BRIDGE_ENABLED
			const GrassCollisionVertexProbe::Draw draw{ .arguments = arguments, .argumentOffset = offset };
			globals::features::grassCollision.ObserveVertexDraw(This, draw, nullptr, func.get());
#endif
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawInstancedIndirect
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Buffer* arguments, UINT offset)
		{
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Indirect);
			func(This, arguments, offset);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallD3DHooks(ID3D11DeviceContext* a_context)
	{
		if (!a_context)
			return;
		static std::mutex installationMutex;
		static winrt::com_ptr<ID3D11DeviceContext> installedContext;
		std::scoped_lock lock(installationMutex);
		// Native context dispatch can change after first use without replacing the context.
		if (installedContext.get() == a_context)
			return;
		stl::detour_vfunc<14, ID3D11DeviceContext_Map>(a_context);
		stl::detour_vfunc<15, ID3D11DeviceContext_Unmap>(a_context);
		stl::detour_vfunc<12, ID3D11DeviceContext_DrawIndexed>(a_context);
		stl::detour_vfunc<13, ID3D11DeviceContext_Draw>(a_context);
		stl::detour_vfunc<20, ID3D11DeviceContext_DrawIndexedInstanced>(a_context);
		stl::detour_vfunc<21, ID3D11DeviceContext_DrawInstanced>(a_context);
		stl::detour_vfunc<38, ID3D11DeviceContext_DrawAuto>(a_context);
		stl::detour_vfunc<39, ID3D11DeviceContext_DrawIndexedInstancedIndirect>(a_context);
		stl::detour_vfunc<40, ID3D11DeviceContext_DrawInstancedIndirect>(a_context);
		installedContext.copy_from(a_context);
	}
}
