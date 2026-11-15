#include "Globals.h"
#include "Features/ReverseZ.h"

#include "Deferred.h"
#include "Features/AdaptiveBrightness.h"
#include "Features/CSEditor.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/FoliageLighting.h"
#include "Features/FurShells.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
#include "Features/GrassOptimizations.h"
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
#include "Features/SeasonalWeather.h"
#include "Features/SkySync.h"
#include "Features/Skylighting.h"
#include "Features/SnowCover.h"
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
#include "Utils/DeviceContextState.h"
#include "Utils/Game.h"
#include "Utils/VRFrameBufferUpload.h"
#include <array>
#include <cstring>
#include <mutex>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/GpuCrashDiagnostics.h"
#	include <algorithm>
#	include <atomic>
#	include <intrin.h>
#	include <vector>
#endif

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
		ReverseZ reverseZ{};
		CloudShadows cloudShadows{};
		Wetterness wetterness{};
		DynamicCubemaps dynamicCubemaps{};
		ExtendedMaterials extendedMaterials{};
		FoliageLighting foliageLighting{};
		GrassCollision grassCollision{};
		GrassLighting grassLighting{};
		GrassOptimizations grassOptimizations{};
		IBL ibl{};
		LightLimitFix lightLimitFix{};
		LinearLighting linearLighting{};
		LODBlending lodBlending{};
		HairSpecular hairSpecular{};
		HorizonFix horizonFix{};
		InteriorSun interiorSun{};
		FurShells furShells{};
		SnowCover snowCover{};
		SeasonalWeather seasonalWeather{};
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (game::isVR && state && features::upscaling.IsNeuralRenderingEnabled())
			features::upscaling.RecordNeuralCaptureCamera(state->frameCount);
#endif
	}

	static bool vrFrameBufferUploadHookReady = false;
	bool IsVRFrameBufferUploadHookReady() { return vrFrameBufferUploadHookReady; }

	void ObserveVRFrameBufferUpload(ID3D11DeviceContext* context, ID3D11Resource* resource,
		UINT subresource, const void* source, void* destination)
	{
		if (context == d3d::context && resource == *game::perFrame && subresource == 0) {
			game::mappedFrameBuffer = nullptr;
			if (source && destination) {
				auto corrected = *static_cast<const FrameBufferVR*>(source);
				if (features::reverseZ.FixupMappedFrameBufferVR(corrected))
					std::memcpy(destination, &corrected, sizeof(corrected));
				CacheFramebuffer(&corrected);
			}
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
		// Validate the copy and argument setup so the CPU source stays intact until Unmap.
		constexpr std::array<std::uint8_t, 120> sourceCopy{
			0x48, 0x8D, 0x4C, 0x24, 0x50, 0xBA, 0x0B, 0x00, 0x00, 0x00, 0x66, 0x66, 0x0F, 0x1F, 0x84, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x80, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x10, 0x01, 0x0F, 0x10,
			0x49, 0x10, 0x48, 0x8D, 0x89, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x11, 0x40, 0x80, 0x0F, 0x10, 0x41,
			0xA0, 0x0F, 0x11, 0x48, 0x90, 0x0F, 0x10, 0x49, 0xB0, 0x0F, 0x11, 0x40, 0xA0, 0x0F, 0x10, 0x41,
			0xC0, 0x0F, 0x11, 0x48, 0xB0, 0x0F, 0x10, 0x49, 0xD0, 0x0F, 0x11, 0x40, 0xC0, 0x0F, 0x10, 0x41,
			0xE0, 0x0F, 0x11, 0x48, 0xD0, 0x0F, 0x10, 0x49, 0xF0, 0x0F, 0x11, 0x40, 0xE0, 0x0F, 0x11, 0x48,
			0xF0, 0x48, 0x83, 0xEA, 0x01, 0x75, 0xAD, 0x48, 0x8B, 0x0D, 0x2E, 0x12, 0x3C, 0x02, 0x45, 0x33,
			0xC0, 0x48, 0x8B, 0x15, 0xF4, 0x35, 0x3C, 0x02
		};
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
		installed = vrFrameBufferUploadHookReady = true;
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		static void ReportDeviceLoss(ID3D11DeviceContext* a_context, ID3D11Resource* a_resource,
			UINT a_subresource, D3D11_MAP a_mapType, UINT a_mapFlags, HRESULT a_result, const void* a_caller) noexcept
		{
			if (!CSX::Diagnostics::GpuCrash::IsEnabled())
				return;
			winrt::com_ptr<ID3D11Device> device;
			a_context->GetDevice(device.put());
			if (device.get() != d3d::device)
				return;
			const HRESULT reason = device ? device->GetDeviceRemovedReason() : S_OK;
			if (SUCCEEDED(reason) && a_result != DXGI_ERROR_DEVICE_REMOVED && a_result != DXGI_ERROR_DEVICE_RESET &&
				a_result != DXGI_ERROR_DEVICE_HUNG && a_result != DXGI_ERROR_DRIVER_INTERNAL_ERROR)
				return;
			static std::atomic_bool reported{ false };
			if (!reported.exchange(true, std::memory_order_relaxed)) {
				try {
					HMODULE callerModule = nullptr;
					char modulePath[MAX_PATH]{};
					std::uintptr_t callerRva = 0;
					if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							reinterpret_cast<LPCSTR>(a_caller), &callerModule)) {
						GetModuleFileNameA(callerModule, modulePath, MAX_PATH);
						callerRva = reinterpret_cast<std::uintptr_t>(a_caller) - reinterpret_cast<std::uintptr_t>(callerModule);
					}
					D3D11_RESOURCE_DIMENSION type = D3D11_RESOURCE_DIMENSION_UNKNOWN;
					if (a_resource)
						a_resource->GetType(&type);
					logger::error("[GPUCrash] Map failed result=0x{:08X} deviceReason=0x{:08X} context=0x{:X} resource=0x{:X} type={} subresource={} mapType={} flags=0x{:X} caller=0x{:X} module={} callerRva=0x{:X}",
						static_cast<std::uint32_t>(a_result), static_cast<std::uint32_t>(reason),
						reinterpret_cast<std::uintptr_t>(a_context), reinterpret_cast<std::uintptr_t>(a_resource),
						static_cast<UINT>(type), a_subresource, static_cast<UINT>(a_mapType), a_mapFlags,
						reinterpret_cast<std::uintptr_t>(a_caller), modulePath, callerRva);
					switch (type) {
					case D3D11_RESOURCE_DIMENSION_BUFFER:
						{
							D3D11_BUFFER_DESC desc{};
							static_cast<ID3D11Buffer*>(a_resource)->GetDesc(&desc);
							logger::error("[GPUCrash] Map buffer bytes={} usage={} bind=0x{:X} cpuAccess=0x{:X} misc=0x{:X} stride={}",
								desc.ByteWidth, static_cast<UINT>(desc.Usage), desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags, desc.StructureByteStride);
							break;
						}
					case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
						{
							D3D11_TEXTURE1D_DESC desc{};
							static_cast<ID3D11Texture1D*>(a_resource)->GetDesc(&desc);
							logger::error("[GPUCrash] Map texture1D width={} mips={} array={} format={} usage={} bind=0x{:X} cpuAccess=0x{:X} misc=0x{:X}",
								desc.Width, desc.MipLevels, desc.ArraySize, static_cast<UINT>(desc.Format), static_cast<UINT>(desc.Usage), desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags);
							break;
						}
					case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
						{
							D3D11_TEXTURE2D_DESC desc{};
							static_cast<ID3D11Texture2D*>(a_resource)->GetDesc(&desc);
							logger::error("[GPUCrash] Map texture2D width={} height={} mips={} array={} format={} samples={} quality={} usage={} bind=0x{:X} cpuAccess=0x{:X} misc=0x{:X}",
								desc.Width, desc.Height, desc.MipLevels, desc.ArraySize, static_cast<UINT>(desc.Format), desc.SampleDesc.Count, desc.SampleDesc.Quality,
								static_cast<UINT>(desc.Usage), desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags);
							break;
						}
					case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
						{
							D3D11_TEXTURE3D_DESC desc{};
							static_cast<ID3D11Texture3D*>(a_resource)->GetDesc(&desc);
							logger::error("[GPUCrash] Map texture3D width={} height={} depth={} mips={} format={} usage={} bind=0x{:X} cpuAccess=0x{:X} misc=0x{:X}",
								desc.Width, desc.Height, desc.Depth, desc.MipLevels, static_cast<UINT>(desc.Format), static_cast<UINT>(desc.Usage), desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags);
							break;
						}
					default:
						break;
					}
					if (const auto log = spdlog::default_logger())
						log->flush();
				} catch (...) {
					OutputDebugStringA("[GPUCrash] Failed to write Map failure details.\n");
				}
			}
			CSX::Diagnostics::GpuCrash::ReportFailure(a_result, "ID3D11DeviceContext::Map");
		}
#endif
		static HRESULT thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
		{
			HRESULT hr = func(This, pResource, Subresource, MapType, MapFlags, pMappedResource);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (FAILED(hr))
				ReportDeviceLoss(This, pResource, Subresource, MapType, MapFlags, hr, _ReturnAddress());
#endif
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
 * Caches the per-frame buffer before unmapping.
 */
	struct ID3D11DeviceContext_Unmap
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource)
		{
			if (This == d3d::context && Subresource == 0 && *globals::game::perFrame.get() == pResource && globals::game::mappedFrameBuffer) {
				auto* bytes = globals::game::mappedFrameBuffer->pData;
				if (game::isVR)
					features::reverseZ.FixupMappedFrameBufferVR(*static_cast<FrameBufferVR*>(bytes));
				else
					features::reverseZ.FixupMappedFrameBuffer(*static_cast<FrameBuffer*>(bytes));
				CacheFramebuffer(bytes);
			}
			func(This, pResource, Subresource);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexed
	{
		static void thunk(ID3D11DeviceContext* This, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
		{
			UnderwaterDepthOfField::BeforeDraw();
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Indexed);
#endif
			func(This, IndexCount, StartIndexLocation, BaseVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Draw
	{
		static void thunk(ID3D11DeviceContext* This, UINT VertexCount, UINT StartVertexLocation)
		{
			UnderwaterDepthOfField::BeforeDraw();
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Direct);
#endif
			func(This, VertexCount, StartVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstanced
	{
		static void thunk(ID3D11DeviceContext* This, UINT indexCount, UINT instanceCount, UINT startIndex, INT baseVertex, UINT startInstance)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::IndexedInstanced);
#endif
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
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Instanced);
#endif
			func(This, vertexCount, instanceCount, startVertex, startInstance);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawAuto
	{
		static void thunk(ID3D11DeviceContext* This)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Auto);
#endif
			func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstancedIndirect
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Buffer* arguments, UINT offset)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::IndexedIndirect);
#endif
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
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (globals::features::upscaling.IsNeuralRenderingEnabled())
				NeuralRendering::Color::ExposureCapture::Instance().ObserveDraw(This, NeuralRendering::Color::ExposureDrawKind::Indirect);
#endif
			func(This, arguments, offset);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

#ifdef DEVBENCH_BRIDGE_ENABLED
	struct ID3D11DeviceContext_Dispatch
	{
		static void thunk(ID3D11DeviceContext* This, UINT x, UINT y, UINT z)
		{
			if (This != d3d::context || !CSX::Diagnostics::GpuCrash::IsEnabled()) {
				func(This, x, y, z);
				return;
			}
			const auto token = CSX::Diagnostics::GpuCrash::BeginPass("D3D11::Dispatch");
			const SKSE::stl::scope_exit finish([&] { CSX::Diagnostics::GpuCrash::EndPass(token); });
			CSX::Diagnostics::GpuCrash::RecordDispatch("D3D11::Dispatch", 0, x, y, z, false);
			func(This, x, y, z);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DispatchIndirect
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Buffer* arguments, UINT offset)
		{
			if (This != d3d::context || !CSX::Diagnostics::GpuCrash::IsEnabled()) {
				func(This, arguments, offset);
				return;
			}
			const auto token = CSX::Diagnostics::GpuCrash::BeginPass("D3D11::DispatchIndirect");
			const SKSE::stl::scope_exit finish([&] { CSX::Diagnostics::GpuCrash::EndPass(token); });
			CSX::Diagnostics::GpuCrash::RecordDispatch("D3D11::DispatchIndirect", 0, 0, 0, 0, false, arguments, offset);
			func(This, arguments, offset);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
#endif

	/// Share draw hooks for underwater composition and live HDR observation.
	void InstallD3DHooks(ID3D11DeviceContext* a_context)
	{
		static std::mutex installationMutex;
		static winrt::com_ptr<ID3D11DeviceContext> installedContext;
		std::scoped_lock lock(installationMutex);
#ifdef DEVBENCH_BRIDGE_ENABLED
		const bool rendererHooksInstalled = installedContext.get() == a_context;
		if (!rendererHooksInstalled)
			Util::RegisterDeviceContextStateForHooks(a_context);
		struct DispatchHookCoverage
		{
			winrt::com_ptr<ID3D11DeviceContext> context;
			Util::VirtualHookResult direct;
			Util::VirtualHookResult indirect;
		};
		static std::vector<DispatchHookCoverage> dispatchCoverage;
		// Diagnostics can arm after renderer hooks exist; each context gets one independent attempt.
		if (a_context && CSX::Diagnostics::GpuCrash::IsEnabled() &&
			std::none_of(dispatchCoverage.begin(), dispatchCoverage.end(),
				[a_context](const auto& coverage) { return coverage.context.get() == a_context; })) {
			auto& coverage = dispatchCoverage.emplace_back();
			coverage.context.copy_from(a_context);
			coverage.direct = Util::InstallVirtualFunctionHook(a_context, 41,
				reinterpret_cast<void**>(&ID3D11DeviceContext_Dispatch::func), reinterpret_cast<void*>(ID3D11DeviceContext_Dispatch::thunk));
			coverage.indirect = Util::InstallVirtualFunctionHook(a_context, 42,
				reinterpret_cast<void**>(&ID3D11DeviceContext_DispatchIndirect::func), reinterpret_cast<void*>(ID3D11DeviceContext_DispatchIndirect::thunk));
			logger::info("[GPUCrash] Compute dispatch hooks context=0x{:X} direct(error={}, detourError={}, method={}) indirect(error={}, detourError={}, method={}); coverage is limited to the renderer immediate context",
				reinterpret_cast<std::uintptr_t>(a_context),
				coverage.direct.error, coverage.direct.detourError, static_cast<int>(coverage.direct.method),
				coverage.indirect.error, coverage.indirect.detourError, static_cast<int>(coverage.indirect.method));
			if (coverage.direct.error != NO_ERROR || coverage.indirect.error != NO_ERROR)
				logger::error("[GPUCrash] Compute dispatch hook coverage is incomplete; installation is not retried on this context");
		}
		// Native context dispatch can change after first use without replacing the context.
		if (rendererHooksInstalled)
			return;
#else
		// Native context dispatch can change after first use without replacing the context.
		if (installedContext.get() == a_context)
			return;
		Util::RegisterDeviceContextStateForHooks(a_context);
#endif
		InstallVRFrameBufferUploadHook();
		stl::detour_vfunc<14, ID3D11DeviceContext_Map>(a_context);
		stl::detour_vfunc<15, ID3D11DeviceContext_Unmap>(a_context);
#ifdef DEVBENCH_BRIDGE_ENABLED
		Upscaling::InstallVRMenuPresentationTraceD3DHooks(a_context);
#endif
		stl::detour_vfunc<12, ID3D11DeviceContext_DrawIndexed>(a_context);
		stl::detour_vfunc<13, ID3D11DeviceContext_Draw>(a_context);
#ifdef DEVBENCH_BRIDGE_ENABLED
		stl::detour_vfunc<20, ID3D11DeviceContext_DrawIndexedInstanced>(a_context);
		stl::detour_vfunc<21, ID3D11DeviceContext_DrawInstanced>(a_context);
		stl::detour_vfunc<38, ID3D11DeviceContext_DrawAuto>(a_context);
		stl::detour_vfunc<39, ID3D11DeviceContext_DrawIndexedInstancedIndirect>(a_context);
		stl::detour_vfunc<40, ID3D11DeviceContext_DrawInstancedIndirect>(a_context);
#endif
		installedContext.copy_from(a_context);
	}
}
