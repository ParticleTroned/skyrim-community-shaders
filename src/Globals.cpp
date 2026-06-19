#include "Globals.h"

#include "Deferred.h"
#include "Features/AdaptiveBrightness.h"
#include "Features/Wetterness.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
#include "Features/HairSpecular.h"
#include "Features/IBL.h"
#include "Features/InteriorSun.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/LinearLighting.h"
#include "Features/PerformanceOverlay.h"
#include "Features/RenderDoc.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/SkySync.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/TerrainShadows.h"
#include "Features/TerrainVariation.h"
#include "Features/UnifiedWater.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Features/VolumetricLighting.h"
#include "Features/WaterEffects.h"
#include "Features/WeatherEditor.h"
#include "Features/WetnessEffects.h"
#include "EngineFixes/ShadowmapCascadeRasterizerFix.h"
#include "Menu.h"
#include "Profiler.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Utils/Game.h"
#include "Features/LightLimitFix/ParticleLights.h"

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
		VR vr{};
		WaterEffects waterEffects{};
		PerformanceOverlay performanceOverlay{};
		WetnessEffects wetnessEffects{};
		ExtendedTranslucency extendedTranslucency{};
		Upscaling upscaling{};
		RenderDoc renderDoc{};
		WeatherEditor weatherEditor{};
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

	/**
 * @brief Caches the current frame buffer data and clears the mapped pointer.
 *
 * Copies the contents of the mapped frame buffer into an internal cache and resets the mapped frame buffer pointer.
 */
	void CacheFramebuffer()
	{
		using namespace game;
		if (REL::Module::IsVR()) {
			auto frameBufferVR = (FrameBufferVR*)mappedFrameBuffer->pData;
			frameBufferCached.vr = *frameBufferVR;
		} else {
			auto frameBuffer = (FrameBuffer*)mappedFrameBuffer->pData;
			frameBufferCached.nonVR = *frameBuffer;
		}
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
 * If the resource being unmapped matches the current per-frame buffer and a mapped frame buffer is present, caches the frame buffer data before calling the original Unmap function.
 */
	struct ID3D11DeviceContext_Unmap
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource)
		{
			if (*globals::game::perFrame.get() == pResource && globals::game::mappedFrameBuffer)
				CacheFramebuffer();
			func(This, pResource, Subresource);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexed
	{
		static void thunk(ID3D11DeviceContext* This, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
		{
			if (Upscaling::TraceVRTrackedDrawOperation(This, "DrawIndexed", 0, IndexCount, 0, 0, StartIndexLocation, BaseVertexLocation, 0))
				return;
			func(This, IndexCount, StartIndexLocation, BaseVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Draw
	{
		static void thunk(ID3D11DeviceContext* This, UINT VertexCount, UINT StartVertexLocation)
		{
			if (Upscaling::TraceVRTrackedDrawOperation(This, "Draw", VertexCount, 0, 0, StartVertexLocation, 0, 0, 0))
				return;
			func(This, VertexCount, StartVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstanced
	{
		static void thunk(
			ID3D11DeviceContext* This,
			UINT IndexCountPerInstance,
			UINT InstanceCount,
			UINT StartIndexLocation,
			INT BaseVertexLocation,
			UINT StartInstanceLocation)
		{
			if (Upscaling::TraceVRTrackedDrawOperation(
				This,
				"DrawIndexedInstanced",
				0,
				IndexCountPerInstance,
				InstanceCount,
				0,
				StartIndexLocation,
				BaseVertexLocation,
				StartInstanceLocation)) {
				return;
			}
			func(This, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawInstanced
	{
		static void thunk(ID3D11DeviceContext* This, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
		{
			if (Upscaling::TraceVRTrackedDrawOperation(
				This,
				"DrawInstanced",
				VertexCountPerInstance,
				0,
				InstanceCount,
				StartVertexLocation,
				0,
				0,
				StartInstanceLocation)) {
				return;
			}
			func(This, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_CopySubresourceRegion
	{
		static void thunk(
			ID3D11DeviceContext* This,
			ID3D11Resource* pDstResource,
			UINT DstSubresource,
			UINT DstX,
			UINT DstY,
			UINT DstZ,
			ID3D11Resource* pSrcResource,
			UINT SrcSubresource,
			const D3D11_BOX* pSrcBox)
		{
			Upscaling::TraceVRTrackedResourceCopyOperation(
				This,
				"CopySubresourceRegion",
				pDstResource,
				DstSubresource,
				DstX,
				DstY,
				DstZ,
				pSrcResource,
				SrcSubresource,
				pSrcBox);
			func(This, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_CopyResource
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource)
		{
			Upscaling::TraceVRTrackedResourceCopyOperation(
				This,
				"CopyResource",
				pDstResource,
				0,
				0,
				0,
				0,
				pSrcResource,
				0,
				nullptr);
			func(This, pDstResource, pSrcResource);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_UpdateSubresource
	{
		static void thunk(
			ID3D11DeviceContext* This,
			ID3D11Resource* pDstResource,
			UINT DstSubresource,
			const D3D11_BOX* pDstBox,
			const void* pSrcData,
			UINT SrcRowPitch,
			UINT SrcDepthPitch)
		{
			Upscaling::TraceVRTrackedResourceUpdateOperation(
				This,
				pDstResource,
				DstSubresource,
				pDstBox);
			func(This, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_ClearRenderTargetView
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4])
		{
			FLOAT convertedColor[4] = {};
			if (Upscaling::TryConvertVRMenuBoundaryClearColor(This, pRenderTargetView, ColorRGBA, convertedColor)) {
				Upscaling::TraceVRTrackedRenderTargetClearOperation(This, pRenderTargetView, convertedColor);
				func(This, pRenderTargetView, convertedColor);
				return;
			}

			Upscaling::TraceVRTrackedRenderTargetClearOperation(This, pRenderTargetView, ColorRGBA);
			func(This, pRenderTargetView, ColorRGBA);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_ClearUnorderedAccessViewUint
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11UnorderedAccessView* pUnorderedAccessView, const UINT Values[4])
		{
			ID3D11Resource* resource = nullptr;
			if (pUnorderedAccessView)
				pUnorderedAccessView->GetResource(&resource);

			Upscaling::TraceVRTrackedResourceCopyOperation(
				This,
				"ClearUnorderedAccessViewUint",
				resource,
				0,
				0,
				0,
				0,
				nullptr,
				0,
				nullptr);

			if (resource)
				resource->Release();

			func(This, pUnorderedAccessView, Values);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_ClearUnorderedAccessViewFloat
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11UnorderedAccessView* pUnorderedAccessView, const FLOAT Values[4])
		{
			ID3D11Resource* resource = nullptr;
			if (pUnorderedAccessView)
				pUnorderedAccessView->GetResource(&resource);

			Upscaling::TraceVRTrackedResourceCopyOperation(
				This,
				"ClearUnorderedAccessViewFloat",
				resource,
				0,
				0,
				0,
				0,
				nullptr,
				0,
				nullptr);

			if (resource)
				resource->Release();

			func(This, pUnorderedAccessView, Values);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_ResolveSubresource
	{
		static void thunk(
			ID3D11DeviceContext* This,
			ID3D11Resource* pDstResource,
			UINT DstSubresource,
			ID3D11Resource* pSrcResource,
			UINT SrcSubresource,
			DXGI_FORMAT Format)
		{
			Upscaling::TraceVRTrackedResourceResolveOperation(
				This,
				pDstResource,
				DstSubresource,
				pSrcResource,
				SrcSubresource,
				Format);
			func(This, pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/**
 * @brief Installs D3D11 device-context hooks used by diagnostic and rendering features.
 *
 * This enables interception of resource mapping, draw calls, copies, resolves, updates, and clear operations.
 */
	void InstallD3DHooks(ID3D11DeviceContext* a_context)
	{
		stl::detour_vfunc<12, ID3D11DeviceContext_DrawIndexed>(a_context);
		stl::detour_vfunc<13, ID3D11DeviceContext_Draw>(a_context);
		stl::detour_vfunc<14, ID3D11DeviceContext_Map>(a_context);
		stl::detour_vfunc<15, ID3D11DeviceContext_Unmap>(a_context);
		stl::detour_vfunc<20, ID3D11DeviceContext_DrawIndexedInstanced>(a_context);
		stl::detour_vfunc<21, ID3D11DeviceContext_DrawInstanced>(a_context);
		stl::detour_vfunc<46, ID3D11DeviceContext_CopySubresourceRegion>(a_context);
		stl::detour_vfunc<47, ID3D11DeviceContext_CopyResource>(a_context);
		stl::detour_vfunc<48, ID3D11DeviceContext_UpdateSubresource>(a_context);
		stl::detour_vfunc<50, ID3D11DeviceContext_ClearRenderTargetView>(a_context);
		stl::detour_vfunc<51, ID3D11DeviceContext_ClearUnorderedAccessViewUint>(a_context);
		stl::detour_vfunc<52, ID3D11DeviceContext_ClearUnorderedAccessViewFloat>(a_context);
		stl::detour_vfunc<57, ID3D11DeviceContext_ResolveSubresource>(a_context);
		ShadowmapRasterizerFix::InstallD3DHooks(a_context);
	}
}
