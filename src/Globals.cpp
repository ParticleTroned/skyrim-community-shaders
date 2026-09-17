#include "Globals.h"

#include "Deferred.h"
#include "Features/AdaptiveBrightness.h"
#include "Features/CSEditor.h"
#include "Features/CSUtility.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/FoliageLighting.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
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
#include "Features/Upscaling/NeuralRendering/ExposureCapture.h"
#include "Features/VR.h"
#include "Features/VolumetricLighting.h"
#include "Features/VolumetricShadows.h"
#include "Features/WaterEffects.h"
#include "Features/WeatherPicker.h"
#include "Features/WetnessEffects.h"
#include "Features/Wetterness.h"
#include "Menu.h"
#include "Profiler.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Utils/Game.h"
#include "Utils/VRFrameBufferUpload.h"
#include <array>
#include <cstring>

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
		AdaptiveBrightness adaptiveBrightness{};
		CloudShadows cloudShadows{};
		Wetterness wetterness{};
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
		VolumetricLighting volumetricLighting{};
		VolumetricShadows volumetricShadows{};
		VR vr{};
		WaterEffects waterEffects{};
		PerformanceOverlay performanceOverlay{};
		WetnessEffects wetnessEffects{};
		ExtendedTranslucency extendedTranslucency{};
		Upscaling upscaling{};
		RenderDoc renderDoc{};
		ScreenshotFeature screenshotFeature{};
		CSEditor csEditor{};
		WeatherPicker weatherPicker{};
		CSUtility csUtility{};
		TruePBR truePBR{};

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
		bool isVR = false;
		RE::MemoryManager* memoryManager = nullptr;
		RE::INISettingCollection* iniSettingCollection = nullptr;
		RE::INIPrefSettingCollection* iniPrefSettingCollection = nullptr;
		RE::GameSettingCollection* gameSettingCollection = nullptr;
		float* cameraNear = nullptr;
		float* cameraFar = nullptr;
		float* deltaTime = nullptr;
		RE::BSUtilityShader* utilityShader = nullptr;
		RE::Sky* sky = nullptr;
		RE::UI* ui = nullptr;
		RE::Calendar* calendar = nullptr;
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
	static Profiler profilerInstance;
	Profiler* profiler = &profilerInstance;

	void OnInit()
	{
		game::quitGame = false;
		shaderCache = &SIE::ShaderCache::Instance();
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
			isVR = REL::Module::IsVR();
			iniSettingCollection = RE::INISettingCollection::GetSingleton();
			iniPrefSettingCollection = RE::INIPrefSettingCollection::GetSingleton();
			gameSettingCollection = RE::GameSettingCollection::GetSingleton();
			RefreshTES();
			waterSystem = RE::TESWaterSystem::GetSingleton();
			cameraNear = (float*)(REL::RelocationID(517032, 403540).address() + 0x40);
			cameraFar = (float*)(REL::RelocationID(517032, 403540).address() + 0x44);
			deltaTime = (float*)REL::RelocationID(523660, 410199).address();

			currentPixelShader = GET_INSTANCE_MEMBER_PTR(currentPixelShader, shadowState);
			currentVertexShader = GET_INSTANCE_MEMBER_PTR(currentVertexShader, shadowState);
			stateUpdateFlags = GET_INSTANCE_MEMBER_PTR(stateUpdateFlags, shadowState);

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
		sky = RE::Sky::GetSingleton();
		utilityShader = RE::BSUtilityShader::GetSingleton();
		waterSystem = RE::TESWaterSystem::GetSingleton();
		bEnableVolumetricLighting = reinterpret_cast<bool*>(REL::RelocationID(527940, 414913).address());

		bEnableLandFade = iniSettingCollection->GetSetting("bEnableLandFade:Display");

		bShadowsOnGrass = RE::GetINISetting("bShadowsOnGrass:Display");
		shadowMaskQuarter = RE::GetINISetting("iShadowMaskQuarter:Display");
	}

	void OnGameWindowClose()
	{
		if (!game::quitGame.exchange(true, std::memory_order_acq_rel) && shaderCache) {
			shaderCache->StopCompilation();
		}
	}

	/// Copy a proven upload source before Unmap invalidates its CPU access.
	void CacheFramebuffer(const void* data)
	{
		using namespace game;
		if (REL::Module::IsVR()) {
			auto frameBufferVR = static_cast<const FrameBufferVR*>(data);
			frameBufferCached.vr = *frameBufferVR;
		} else {
			auto frameBuffer = static_cast<const FrameBuffer*>(data);
			frameBufferCached.nonVR = *frameBuffer;
		}
		mappedFrameBuffer = nullptr;
		if (game::isVR && state)
			features::upscaling.RecordNeuralCaptureCamera(state->frameCount);
	}

	void ObserveVRFrameBufferUpload(ID3D11DeviceContext* context, ID3D11Resource* resource,
		UINT subresource, const void* source)
	{
		if (context == d3d::context && resource == *game::perFrame && subresource == 0) {
			game::mappedFrameBuffer = nullptr;
			if (source)
				CacheFramebuffer(source);
		}
		context->Unmap(resource, subresource);
	}

	void InstallVRFrameBufferUploadHook()
	{
		static bool installed = false;
		if (!game::isVR || installed)
			return;
		if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
			logger::error("VR frame-buffer upload observer unavailable for this runtime");
			return;
		}
		const auto upload = REL::RelocationID(75472, 0).address();
		constexpr std::array<std::uint8_t, 8> mapResult{ 0xFF, 0x50, 0x70, 0x48, 0x8B, 0x44, 0x24, 0x40 };
		constexpr std::array<std::uint8_t, 10> sourceCopy{ 0x48, 0x8D, 0x4C, 0x24, 0x50, 0xBA, 0x0B, 0x00, 0x00, 0x00 };
		constexpr std::array<std::uint8_t, 6> unmap{ 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x78 };
		if (std::memcmp(reinterpret_cast<const void*>(upload + 0x6FA), mapResult.data(), mapResult.size()) != 0 ||
			std::memcmp(reinterpret_cast<const void*>(upload + 0x72C), sourceCopy.data(), sourceCopy.size()) != 0 ||
			std::memcmp(reinterpret_cast<const void*>(upload + 0x7A4), unmap.data(), unmap.size()) != 0) {
			logger::error("VR frame-buffer upload observer signature mismatch; retaining D3D observations");
			return;
		}
		static_assert(sizeof(FrameBufferVR) == 0x570);
		Util::VRFrameBufferUploadThunk code(reinterpret_cast<std::uintptr_t>(ObserveVRFrameBufferUpload));
		code.ready();
		auto& trampoline = SKSE::GetTrampoline();
		const auto observer = reinterpret_cast<std::uintptr_t>(trampoline.allocate(code));
		trampoline.write_call<6>(upload + 0x7A4, observer);
		installed = true;
		logger::info("Installed VR frame-buffer upload observer");
	}

	/**
 * @brief Hooks the ID3D11DeviceContext::Map method to track mapping of the per-frame resource.
 *
 * Calls the original Map function and tracks the current per-frame buffer.
 *
 * @return HRESULT Result of the original Map call.
 */
	struct ID3D11DeviceContext_Map
	{
		static HRESULT thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
		{
			HRESULT hr = func(This, pResource, Subresource, MapType, MapFlags, pMappedResource);
			if (hr == S_OK) {
				if (*globals::game::perFrame.get() == pResource)
					globals::game::mappedFrameBuffer = pMappedResource;
			}
			return hr;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/**
 * @brief Hooked implementation of ID3D11DeviceContext::Unmap that caches the frame buffer if applicable.
 *
 * Caches the per-frame buffer before unmapping.
 */
	struct ID3D11DeviceContext_Unmap
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource)
		{
			if (*globals::game::perFrame.get() == pResource && globals::game::mappedFrameBuffer)
				CacheFramebuffer(globals::game::mappedFrameBuffer->pData);
			func(This, pResource, Subresource);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexed
	{
		static void thunk(ID3D11DeviceContext* This, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
		{
			UnderwaterDepthOfField::BeforeDraw();
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
			NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Direct);
			func(This, VertexCount, StartVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstanced
	{
		static void thunk(ID3D11DeviceContext* This, UINT indexCount, UINT instanceCount, UINT startIndex, INT baseVertex, UINT startInstance)
		{
			NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::IndexedInstanced);
			func(This, indexCount, instanceCount, startIndex, baseVertex, startInstance);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawInstanced
	{
		static void thunk(ID3D11DeviceContext* This, UINT vertexCount, UINT instanceCount, UINT startVertex, UINT startInstance)
		{
			NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Instanced);
			func(This, vertexCount, instanceCount, startVertex, startInstance);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawAuto
	{
		static void thunk(ID3D11DeviceContext* This)
		{
			NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Auto);
			func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstancedIndirect
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Buffer* arguments, UINT offset)
		{
			NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::IndexedIndirect);
			func(This, arguments, offset);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawInstancedIndirect
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Buffer* arguments, UINT offset)
		{
			NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Indirect);
			func(This, arguments, offset);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/// Share draw hooks for underwater composition and live HDR observation.
	void InstallD3DHooks(ID3D11DeviceContext* a_context)
	{
		InstallVRFrameBufferUploadHook();
		stl::detour_vfunc<14, ID3D11DeviceContext_Map>(a_context);
		stl::detour_vfunc<15, ID3D11DeviceContext_Unmap>(a_context);
#ifdef DEVBENCH_BRIDGE_ENABLED
		Upscaling::InstallVRMenuPresentationTraceD3DHooks(a_context);
#endif
		stl::detour_vfunc<12, ID3D11DeviceContext_DrawIndexed>(a_context);
		stl::detour_vfunc<13, ID3D11DeviceContext_Draw>(a_context);
		stl::detour_vfunc<20, ID3D11DeviceContext_DrawIndexedInstanced>(a_context);
		stl::detour_vfunc<21, ID3D11DeviceContext_DrawInstanced>(a_context);
		stl::detour_vfunc<38, ID3D11DeviceContext_DrawAuto>(a_context);
		stl::detour_vfunc<39, ID3D11DeviceContext_DrawIndexedInstancedIndirect>(a_context);
		stl::detour_vfunc<40, ID3D11DeviceContext_DrawInstancedIndirect>(a_context);
	}
}
