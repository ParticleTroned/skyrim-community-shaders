#include "Upscaling.h"

#include "Deferred.h"
#include "Features/RenderDoc.h"
#include "FoveatedCommon.h"
#include "Hooks.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "RE/B/BSOpenVR.h"
#include "RE/R/RaceSexMenu.h"
#include "State.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"
#include "Utils/OpenCompositeInterop.h"
#include "Utils/UI.h"
#include "VR.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cwctype>
#include <directx/d3dx12.h>
#include <dxgi.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	qualityMode,
	dlssPreset,
	renderScaleMode,
	vrFpsStabilizerSync,
	perfMode,
	frameLimitMode,
	frameGenerationMode,
	frameGenerationForceEnable,
	frameGenerationAllowInMenus,
	streamlineLogLevel,
	sharpnessFSR,
	sharpnessDLSS,
	dlssSharpener,
	fsr4RuntimeEnable,
	foveatedVendorDispatch,
	foveatedCenterArea,
	foveatedCenterHorizontalScale,
	foveatedLeftEyeMaskOffsetX,
	foveatedLeftEyeMaskOffsetY,
	foveatedRightEyeMaskOffsetX,
	foveatedRightEyeMaskOffsetY,
	periphery_taa_center_area,
	foveatedPeripheryMaskVisualization,
	periphery_taa_enable,
	periphery_taa_outer_scale,
	periphery_taa_center_blend_feather,
	reflexLowLatencyMode,
	reflexLowLatencyBoost,
	reflexUseMarkersToOptimize,
	reflexUseFPSLimit,
	reflexFPSLimit);

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChainUpscaling;

namespace
{
	constexpr float kDLSSRCASSharpnessOverdrive = 1.15457f;  // Previous 1.75x curve at slider 0.7.
	constexpr float kDLSSLumaSharpnessOverdrive = 2.5f;

	template <class Fn>
	struct ScopeExit
	{
		Fn fn;
		bool active = true;

		explicit ScopeExit(Fn a_fn) :
			fn(a_fn)
		{}

		ScopeExit(const ScopeExit&) = delete;
		ScopeExit& operator=(const ScopeExit&) = delete;

		~ScopeExit()
		{
			if (active)
				fn();
		}

		void Release()
		{
			active = false;
		}
	};

	template <class Fn>
	ScopeExit(Fn) -> ScopeExit<Fn>;

	constexpr float kPeripheryTAAOuterScaleMin = 0.30f;
	constexpr float kPeripheryTAAOuterScaleMax = 1.0f;
	constexpr float kPeripheryTAACenterBlendFeatherMin = 0.0f;
	constexpr float kPeripheryTAACenterBlendFeatherMax = 0.10f;
	constexpr float kDynamicResolutionUpscalingScaleThreshold = 0.99f;
	constexpr uint32_t kDefaultRenderScaleQualityMode = 3u;  // Quality
	constexpr uint32_t kVRUpscalingTransitionApplyDelayFrames = 6u;
	constexpr uint32_t kVRRenderScaleRelatchBusyRetryFrames = 60u;
	constexpr uint32_t kVRRenderScaleRelatchD3DFailureRetryFrames = 300u;
	constexpr uint32_t kVRRenderScalePostLoadSettleRetryFrames = kVRUpscalingTransitionApplyDelayFrames;
	constexpr uint32_t kVRSubmitStageVendorRelatchCooldownFrames = 30u;
	constexpr uint32_t kVRSubmitStageVendorRelatchMinCooldownFrames = 6u;
	constexpr uint32_t kVRSubmitStageVendorRelatchStableFrames = 3u;
	constexpr uint32_t kVRSubmitStageFoveatedFailureRetryFrames = 30u;
	constexpr uint32_t kVRSubmitStageUnderwaterMaskTailFrames = 4u;
	constexpr float kFoveatedMaskOffsetAdjustMin = -0.30f;
	constexpr float kFoveatedMaskOffsetAdjustMax = 0.30f;
	constexpr float kFoveatedMaskOffsetResolvedMin = -0.30f;
	constexpr float kFoveatedMaskOffsetResolvedMax = 0.30f;
	std::atomic_bool g_vrLoadingMenuOpenFromEvent{ false };
	std::atomic_uint32_t g_vrLoadingTransitionCloseFrame{ 0 };
	std::atomic_uint32_t g_vrLoadingTransitionTailEndFrame{ 0 };
	std::atomic_uint32_t g_vrMenuPresentationTailEndFrame{ 0 };
	std::atomic_uint32_t g_vrObservedProjectedMenuTailEndFrame{ 0 };
	// Keep the DrawIndexedInstanced hook hot only around menu bridge windows.
	std::atomic_uint32_t g_vrMenuBridgeTraceTailEndFrame{ 0 };
	std::atomic_uint32_t g_vrSubmitStageUnderwaterMaskTailEndFrame{ 0 };
	std::atomic_uint64_t g_vrMenuBridgeTraceCachedState{ 0 };
	std::atomic_bool g_renderDocDllDetected{ false };
	std::atomic_bool g_renderDocUpscalingD3DHookBypassLogged{ false };
	constexpr uint32_t kVRCellTransitionTailFrames = 4;
	constexpr uint32_t kVRCellTransitionPresentationTailFrames = kVRCellTransitionTailFrames;

	bool IsExplicitVRUpscalingTransitionOrigin(Upscaling::VRUpscalingTransitionOrigin a_origin)
	{
		return a_origin != Upscaling::VRUpscalingTransitionOrigin::PostLoadSync;
	}

	bool UsesVRRenderScalePostLoadSettle(Upscaling::VRUpscalingTransitionOrigin a_origin)
	{
		return !IsExplicitVRUpscalingTransitionOrigin(a_origin);
	}

	Upscaling::VRUpscalingTransitionOrigin LoadVRUpscalingTransitionOrigin(const std::atomic<uint32_t>& a_origin)
	{
		const auto origin = static_cast<Upscaling::VRUpscalingTransitionOrigin>(a_origin.load(std::memory_order_acquire));
		switch (origin) {
		case Upscaling::VRUpscalingTransitionOrigin::CSMenu:
		case Upscaling::VRUpscalingTransitionOrigin::VRAPI:
		case Upscaling::VRUpscalingTransitionOrigin::PostLoadSync:
			return origin;
		default:
			return Upscaling::VRUpscalingTransitionOrigin::CSMenu;
		}
	}

	bool ShouldStoreVRUpscalingTransitionOrigin(
		Upscaling::VRUpscalingTransitionOrigin a_current,
		Upscaling::VRUpscalingTransitionOrigin a_next,
		bool a_transitionAlreadyQueued)
	{
		if (!a_transitionAlreadyQueued)
			return true;

		if (a_next == Upscaling::VRUpscalingTransitionOrigin::PostLoadSync)
			return true;

		return IsExplicitVRUpscalingTransitionOrigin(a_next) ||
		       !IsExplicitVRUpscalingTransitionOrigin(a_current);
	}

	bool ShouldStoreVRRenderScalePostLoadSettle(
		bool a_currentRequiresPostLoadSettle,
		bool a_nextRequiresPostLoadSettle,
		bool a_recreateAlreadyQueued,
		Upscaling::VRUpscalingTransitionOrigin a_nextOrigin)
	{
		if (!a_recreateAlreadyQueued)
			return true;

		if (a_nextOrigin == Upscaling::VRUpscalingTransitionOrigin::PostLoadSync)
			return true;

		return !a_nextRequiresPostLoadSettle || a_currentRequiresPostLoadSettle;
	}

	bool HasPendingExplicitVRUpscalingWork(const Upscaling& a_upscaling)
	{
		if (a_upscaling.HasPendingVRUpscalingTransition() &&
			IsExplicitVRUpscalingTransitionOrigin(LoadVRUpscalingTransitionOrigin(a_upscaling.pendingVRUpscalingTransitionOrigin))) {
			return true;
		}

		return a_upscaling.pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) &&
		       !a_upscaling.pendingPerfModeRenderTargetRecreatePostLoadSettle.load(std::memory_order_acquire);
	}

	bool IsMainMenuContextActive();
	bool IsMainOrLoadingMenuContextActive();
	bool IsKnownGameMenuContextActive();
	bool IsVRMenuScenePresentationBlockActive();

	bool IsRenderDocDllLoaded(bool a_probeProcess)
	{
		if (g_renderDocDllDetected.load(std::memory_order_acquire))
			return true;
		if (!a_probeProcess)
			return false;
		if (GetModuleHandleW(L"renderdoc.dll") == nullptr)
			return false;

		g_renderDocDllDetected.store(true, std::memory_order_release);
		return true;
	}

	bool IsRenderDocUpscalingBlocked(bool a_probeProcessForDll = false)
	{
		const auto& renderDoc = globals::features::renderDoc;
		return renderDoc.ShouldBlockUpscaling() || IsRenderDocDllLoaded(a_probeProcessForDll);
	}

	const char* GetRenderDocUpscalingBlockReason()
	{
		const auto& renderDoc = globals::features::renderDoc;
		if (renderDoc.IsAvailable())
			return "RenderDoc capture is active";
		if (renderDoc.enableRenderDocCapture)
			return "RenderDoc capture is enabled";
		return "renderdoc.dll is loaded";
	}
	constexpr uint32_t kVRSaveLoadTransitionTailFrames = 30;
	constexpr uint32_t kVRMenuPresentationTailFrames = 30;
	constexpr uint32_t kVRObservedMenuPresentationTailFrames = 3;
	constexpr std::string_view kSkyrimPresentationMenuNames[] = {
		"Journal Menu",
		"StatsMenu",
		"InventoryMenu",
		"MagicMenu",
		"TweenMenu",
		"Book Menu",
		"ContainerMenu",
		"BarterMenu",
		"Sleep/Wait Menu",
		"Crafting Menu",
		"Lockpicking Menu",
		"Training Menu",
		"LevelUp Menu",
		"Dialogue Menu",
		"MessageBoxMenu",
		RE::RaceSexMenu::MENU_NAME,
		"Tutorial Menu",
		"Console",
	};

	constexpr const char* kFoveatedUpscalingMethodAvailabilityText = "VR FOV mask setup is available only with DLSS or FSR.";
	constexpr const char* kFoveatedUpscalingSetupIntro = R"(- Upscaling FOV renders the green visible area with DLSS/DLAA or FSR and uses a cheaper outer mask. Smaller visible scale means more performance, but more risk of peripheral shimmer.

- Upscaling FOV + TAA adds a yellow TAA ring around a smaller green center to reduce shimmer. It costs more than Upscaling FOV alone, but can let you keep the vendor center scale smaller while the visible scale still covers the HMD view.

- Shader foveation features reuse this shared mask; they do not have separate scale sliders.)";
	constexpr const char* kFoveatedUpscalingSetupInstructions = R"(1) Activate FOV Mask Visualization
2) Use the FOV Only Visible Scale slider to decrease FOV scale to 0.25 and place the green center mask in the center of each eye. Per-eye positions do not have to be vertically or horizontally aligned.
3) Expand FOV Only Visible Scale until the green mask touches the top and bottom view of your HMD. If needed, reposition right and left eye to get the best top and bottom fit.
4) Use the Expand FOV Scale R/L slider to horizontally expand the mask until the green part just touches the field of view.
5) Ideally, you do not see the blue outer mask anymore, except in the corners, or only a tiny bit.
6) The larger the visible scale, the less performance savings you have.
7) Test in game that you do not have strong peripheral shimmer. If yes, increase the green mask scale. If not, reduce it to just before shimmer appears for best performance.)";
	constexpr const char* kFoveatedUpscalingPeripheralTaaSetupInstructions = R"(1) Activate FOV Mask Visualization
2) Lower the FOV + TAA Center Scale slider to 0.30. You can later try 0.25 if these settings work for you for even more performance wins.
3) Use the FOV + TAA Visible Outer Scale slider until the yellow ring touches the top and bottom view of your HMD. If needed, reposition right and left eye to get the best top and bottom fit.
4) Ideally, you do not see the blue outer ring anymore, except in the corners, or only a tiny bit.
5) The larger the FOV + TAA center or visible outer scale, the less performance savings you have.
6) Test in game that you do not have strong peripheral shimmer. If yes, increase the FOV + TAA visible outer scale or, if needed, the center scale. If not, reduce them to just before shimmer appears for best performance.)";

	uint ClampToggleUInt(uint value);

	struct VRStereoEyeRegion
	{
		uint32_t minX = 0;
		uint32_t minY = 0;
		uint32_t width = 0;
		uint32_t height = 0;

		[[nodiscard]] bool IsValid() const
		{
			return width > 0 && height > 0;
		}

		[[nodiscard]] uint32_t MaxX() const
		{
			return minX + width;
		}

		[[nodiscard]] uint32_t MaxY() const
		{
			return minY + height;
		}

		[[nodiscard]] D3D11_BOX ToClampedD3DBox(uint32_t textureWidth, uint32_t textureHeight) const
		{
			const uint32_t left = std::min(minX, textureWidth);
			const uint32_t top = std::min(minY, textureHeight);
			return {
				left,
				top,
				0,
				std::min(MaxX(), textureWidth),
				std::min(MaxY(), textureHeight),
				1
			};
		}
	};

	struct VRStereoLayout
	{
		std::array<VRStereoEyeRegion, 2> eyes{};
		uint32_t width = 0;
		uint32_t height = 0;

		[[nodiscard]] bool IsValid() const
		{
			return width > 0 && height > 0 && eyes[0].IsValid() && eyes[1].IsValid();
		}
	};

	VRStereoLayout ResolveVRSideBySideStereoLayout(uint32_t eyeWidth, uint32_t eyeHeight)
	{
		VRStereoLayout layout{};
		if (!eyeWidth || !eyeHeight)
			return layout;

		const uint64_t stereoWidth = static_cast<uint64_t>(eyeWidth) * 2u;
		if (stereoWidth > std::numeric_limits<uint32_t>::max())
			return layout;

		layout.width = static_cast<uint32_t>(stereoWidth);
		layout.height = eyeHeight;
		layout.eyes[0] = { 0u, 0u, eyeWidth, eyeHeight };
		layout.eyes[1] = { eyeWidth, 0u, eyeWidth, eyeHeight };
		return layout;
	}

	bool TextureContainsVREyeRegion(uint32_t textureWidth, uint32_t textureHeight, const VRStereoEyeRegion& region)
	{
		return region.IsValid() && textureWidth >= region.MaxX() && textureHeight >= region.MaxY();
	}

	struct VRSubmitSourceRegion
	{
		D3D11_BOX box{};
		UINT subresource = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depthOffsetX = 0;
		uint32_t depthOffsetY = 0;
		uint32_t depthWidth = 0;
		uint32_t depthHeight = 0;
		bool valid = false;
		bool matchesExpectedSize = false;
		bool fromOpenVRBounds = false;
	};

	uint32_t GetD3DBoxWidth(const D3D11_BOX& box)
	{
		return box.right > box.left ? box.right - box.left : 0u;
	}

	uint32_t GetD3DBoxHeight(const D3D11_BOX& box)
	{
		return box.bottom > box.top ? box.bottom - box.top : 0u;
	}

	uint32_t ResolveVRBoundsPixel(float value, uint32_t maxValue)
	{
		const double normalized = std::clamp(static_cast<double>(value), 0.0, 1.0);
		return static_cast<uint32_t>(std::llround(normalized * static_cast<double>(maxValue)));
	}

	bool TryGetNormalizedVRBounds(const vr::VRTextureBounds_t* inputBounds, float& minU, float& minV, float& maxU, float& maxV)
	{
		if (!inputBounds ||
			!std::isfinite(inputBounds->uMin) ||
			!std::isfinite(inputBounds->uMax) ||
			!std::isfinite(inputBounds->vMin) ||
			!std::isfinite(inputBounds->vMax)) {
			return false;
		}

		minU = std::min(inputBounds->uMin, inputBounds->uMax);
		maxU = std::max(inputBounds->uMin, inputBounds->uMax);
		minV = std::min(inputBounds->vMin, inputBounds->vMax);
		maxV = std::max(inputBounds->vMin, inputBounds->vMax);
		return true;
	}

	bool InputBoundsUseCombinedStereoSpace(const vr::VRTextureBounds_t* inputBounds, uint32_t eyeIndex)
	{
		float minU = 0.0f;
		float minV = 0.0f;
		float maxU = 0.0f;
		float maxV = 0.0f;
		if (!TryGetNormalizedVRBounds(inputBounds, minU, minV, maxU, maxV)) {
			return false;
		}
		(void)minV;
		(void)maxV;
		constexpr float halfEyeEpsilon = 0.001f;
		if (eyeIndex == 0u)
			return maxU <= 0.5f + halfEyeEpsilon;

		return minU >= 0.5f - halfEyeEpsilon;
	}

	bool InputBoundsCoverFullTexture(const vr::VRTextureBounds_t* inputBounds)
	{
		float minU = 0.0f;
		float minV = 0.0f;
		float maxU = 0.0f;
		float maxV = 0.0f;
		if (!TryGetNormalizedVRBounds(inputBounds, minU, minV, maxU, maxV)) {
			return false;
		}

		constexpr float fullBoundsEpsilon = 0.001f;
		return minU <= fullBoundsEpsilon &&
		       minV <= fullBoundsEpsilon &&
		       maxU >= 1.0f - fullBoundsEpsilon &&
		       maxV >= 1.0f - fullBoundsEpsilon;
	}

	VRSubmitSourceRegion ResolveVRSubmitSourceRegion(
		const D3D11_TEXTURE2D_DESC& sourceDesc,
		uint32_t eyeIndex,
		uint32_t expectedEyeWidth,
		uint32_t expectedEyeHeight,
		const VRStereoLayout& sourceStereoLayout,
		bool sourceUsesCombinedStereoLayout,
		bool inputBoundsUseCombinedStereoSpace,
		const vr::VRTextureBounds_t* inputBounds)
	{
		VRSubmitSourceRegion region{};
		if (!sourceDesc.Width || !sourceDesc.Height || !expectedEyeWidth || !expectedEyeHeight)
			return region;

		const uint32_t baseDepthOffsetX = sourceStereoLayout.eyes[eyeIndex].minX;
		region.depthWidth = expectedEyeWidth;
		region.depthHeight = expectedEyeHeight;
		region.depthOffsetX = baseDepthOffsetX;

		if (sourceDesc.ArraySize > 1) {
			const UINT arraySlice = std::min<UINT>(eyeIndex, sourceDesc.ArraySize - 1);
			region.subresource = D3D11CalcSubresource(0, arraySlice, sourceDesc.MipLevels);
		}

		const bool ignoreFullCombinedStereoBounds =
			sourceUsesCombinedStereoLayout &&
			!inputBoundsUseCombinedStereoSpace &&
			InputBoundsCoverFullTexture(inputBounds);

		if (inputBounds && !ignoreFullCombinedStereoBounds) {
			float minU = 0.0f;
			float minV = 0.0f;
			float maxU = 0.0f;
			float maxV = 0.0f;
			if (!TryGetNormalizedVRBounds(inputBounds, minU, minV, maxU, maxV)) {
				return region;
			}
			const uint32_t left = ResolveVRBoundsPixel(minU, sourceDesc.Width);
			const uint32_t top = ResolveVRBoundsPixel(minV, sourceDesc.Height);
			const uint32_t right = ResolveVRBoundsPixel(maxU, sourceDesc.Width);
			const uint32_t bottom = ResolveVRBoundsPixel(maxV, sourceDesc.Height);
			region.box = {
				left,
				top,
				0,
				right,
				bottom,
				1
			};
			region.fromOpenVRBounds = true;
			if (inputBoundsUseCombinedStereoSpace) {
				const uint32_t depthLeft = ResolveVRBoundsPixel(minU, sourceStereoLayout.width);
				const uint32_t depthTop = ResolveVRBoundsPixel(minV, sourceStereoLayout.height);
				const uint32_t depthRight = ResolveVRBoundsPixel(maxU, sourceStereoLayout.width);
				const uint32_t depthBottom = ResolveVRBoundsPixel(maxV, sourceStereoLayout.height);
				region.depthOffsetX = depthLeft;
				region.depthOffsetY = depthTop;
				region.depthWidth = depthRight > depthLeft ? depthRight - depthLeft : 0u;
				region.depthHeight = depthBottom > depthTop ? depthBottom - depthTop : 0u;
			} else {
				const uint32_t depthLeft = ResolveVRBoundsPixel(minU, expectedEyeWidth);
				const uint32_t depthTop = ResolveVRBoundsPixel(minV, expectedEyeHeight);
				const uint32_t depthRight = ResolveVRBoundsPixel(maxU, expectedEyeWidth);
				const uint32_t depthBottom = ResolveVRBoundsPixel(maxV, expectedEyeHeight);
				region.depthOffsetX = baseDepthOffsetX + depthLeft;
				region.depthOffsetY = depthTop;
				region.depthWidth = depthRight > depthLeft ? depthRight - depthLeft : 0u;
				region.depthHeight = depthBottom > depthTop ? depthBottom - depthTop : 0u;
			}
		} else if (sourceUsesCombinedStereoLayout) {
			region.box = sourceStereoLayout.eyes[eyeIndex].ToClampedD3DBox(sourceDesc.Width, sourceDesc.Height);
			region.depthOffsetX = sourceStereoLayout.eyes[eyeIndex].minX;
		} else {
			region.box = { 0, 0, 0, std::min(expectedEyeWidth, sourceDesc.Width), std::min(expectedEyeHeight, sourceDesc.Height), 1 };
		}

		region.width = GetD3DBoxWidth(region.box);
		region.height = GetD3DBoxHeight(region.box);
		region.valid = region.width != 0 && region.height != 0 && region.depthWidth != 0 && region.depthHeight != 0;
		region.matchesExpectedSize = region.width == expectedEyeWidth && region.height == expectedEyeHeight;
		return region;
	}

	bool IsVendorUpscalingMethod(Upscaling::UpscaleMethod a_upscaleMethod)
	{
		return a_upscaleMethod == Upscaling::UpscaleMethod::kFSR || a_upscaleMethod == Upscaling::UpscaleMethod::kDLSS;
	}

	bool IsRenderScaleQualityMode(uint32_t a_qualityMode)
	{
		return Upscaling::GetQualityModeResolutionScale(
				   std::min<uint32_t>(a_qualityMode, Upscaling::kQualityModeMaxIndex)) <
		       kDynamicResolutionUpscalingScaleThreshold;
	}

	bool IsRenderScaleMethodEligible(Upscaling::UpscaleMethod a_upscaleMethod)
	{
		return REL::Module::IsVR() && IsVendorUpscalingMethod(a_upscaleMethod);
	}

	bool IsVRRenderScaleSubmitPathEligible(Upscaling::UpscaleMethod a_upscaleMethod)
	{
		if (!IsRenderScaleMethodEligible(a_upscaleMethod))
			return false;

		return IsRenderScaleQualityMode(globals::features::upscaling.GetEffectiveUpscalingQualityMode());
	}

	bool ShouldDelayVRRenderScaleForPendingDLSS(const Upscaling& a_upscaling);

	bool IsVRRenderScaleSubmitPathEnabled()
	{
		auto& upscaling = globals::features::upscaling;
		if (upscaling.IsSubmitStageDeviceLost())
			return false;

		if (ShouldDelayVRRenderScaleForPendingDLSS(upscaling))
			return false;

		const auto upscaleMethod = upscaling.GetUpscaleMethod();
		if (!upscaling.IsRenderScaleModeRequested())
			return false;

		if (!IsVRRenderScaleSubmitPathEligible(upscaleMethod))
			return false;

		return true;
	}

	bool ShouldDelayVRRenderScaleForPendingDLSS(const Upscaling& a_upscaling)
	{
		if (!globals::game::isVR)
			return false;

		if (a_upscaling.IsOpenCompositeUpscalingBlocked())
			return false;

		if (Upscaling::streamline.featureDLSS || Upscaling::streamline.featureCheckComplete)
			return false;

		if (static_cast<Upscaling::UpscaleMethod>(a_upscaling.settings.upscaleMethod) != Upscaling::UpscaleMethod::kDLSS)
			return false;

		const uint32_t pendingPerfMode = a_upscaling.pendingVRPerfMode.load(std::memory_order_acquire);
		const bool perfModeRequested = pendingPerfMode != Upscaling::kPendingVRUpscalingSettingUnset ?
		                                   pendingPerfMode != 0 :
		                                   a_upscaling.perfMode.IsRequested(a_upscaling.settings);
		if (!perfModeRequested)
			return false;

		const uint32_t pendingRenderScaleMode = a_upscaling.pendingVRRenderScaleMode.load(std::memory_order_acquire);
		const bool renderScaleModeRequested = pendingRenderScaleMode != Upscaling::kPendingVRUpscalingSettingUnset ?
		                                          pendingRenderScaleMode != 0 :
		                                          ClampToggleUInt(a_upscaling.settings.renderScaleMode) != 0;
		if (!renderScaleModeRequested)
			return false;

		const uint32_t pendingQualityMode = a_upscaling.pendingVRUpscalingQualityMode.load(std::memory_order_acquire);
		const uint32_t qualityMode = pendingQualityMode != Upscaling::kPendingVRUpscalingSettingUnset ?
		                                 pendingQualityMode :
		                                 a_upscaling.settings.qualityMode;
		return IsRenderScaleQualityMode(qualityMode);
	}

	bool DeferVRPerfModeBootLatchForPendingDLSS(Upscaling& a_upscaling)
	{
		if (!ShouldDelayVRRenderScaleForPendingDLSS(a_upscaling))
			return false;

		a_upscaling.perfModeAllowBootLatchCreate.store(true, std::memory_order_release);
		if (!a_upscaling.delayedVRPerfModeBootLatchForDLSS.exchange(true, std::memory_order_acq_rel)) {
			logger::debug("[VRRenderScale] Delaying VR render-scale boot latch until Streamline DLSS availability is known.");
		}
		return true;
	}

	void CompleteDelayedVRPerfModeBootLatchForDLSSAvailability(Upscaling& a_upscaling, const char* a_reason)
	{
		if (!a_upscaling.delayedVRPerfModeBootLatchForDLSS.exchange(false, std::memory_order_acq_rel))
			return;

		logger::debug(
			"[VRRenderScale] Streamline DLSS availability resolved after deferred VR render-scale boot latch (DLSS {}).",
			Upscaling::streamline.featureDLSS ? "available" : "unavailable");
		auto origin = LoadVRUpscalingTransitionOrigin(a_upscaling.pendingVRUpscalingTransitionOrigin);
		if (!a_upscaling.HasPendingVRUpscalingTransition()) {
			origin = a_upscaling.pendingPerfModeRenderTargetRecreatePostLoadSettle.load(std::memory_order_acquire) ?
			             Upscaling::VRUpscalingTransitionOrigin::CSMenu :
			             Upscaling::VRUpscalingTransitionOrigin::VRAPI;
		}
		a_upscaling.RequestPerfModeRenderTargetRecreate(a_reason, origin);
	}

	// These targets feed native-layout menu/HUD/fade work. They are protected
	// from submit-stage presentation handling because they are not final eye
	// images, but they must not be force-resized to final HMD dimensions.
	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 6> kVRNativeLayoutSubmitProtectedTargets{
		RE::RENDER_TARGETS::kMENUBG,
		RE::RENDER_TARGETS::kPROJECTEDMENU,
		RE::RENDER_TARGETS::kHUDMENU,
		RE::RENDER_TARGETS::kFADERUI,
		RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_1,
		RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_2,
	};

	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 1> kVRObservedMenuPresentationSeedTargets{
		RE::RENDER_TARGETS::kPROJECTEDMENU,
	};

	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 2> kVRObservedMenuPresentationFollowTargets{
		RE::RENDER_TARGETS::kPROJECTEDMENU,
		RE::RENDER_TARGETS::kHUDMENU,
	};

	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 2> kVRKnownGameMenuFinalCompositeTargets{
		RE::RENDER_TARGETS::kPROJECTEDMENU,
		RE::RENDER_TARGETS::kHUDMENU,
	};

	// Submit-stage should only operate on runtime-submitted eye textures.
	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 0> kSubmittedVRPresentationTargets{};

	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 2> kVRRenderScaleDisplaySizedTargets{
		RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY,
		RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY2,
	};

	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 12> kVRRenderScaleEngineSizedTargets{
		RE::RENDER_TARGETS::kMAIN,
		RE::RENDER_TARGETS::kMAIN_COPY,
		RE::RENDER_TARGETS::kMAIN_ONLY_ALPHA,
		RE::RENDER_TARGETS::kNORMAL_TAAMASK_SSRMASK,
		RE::RENDER_TARGETS::kNORMAL_TAAMASK_SSRMASK_SWAP,
		RE::RENDER_TARGETS::kMOTION_VECTOR,
		RE::RENDER_TARGETS::kREFRACTION_NORMALS,
		RE::RENDER_TARGETS::kTEMPORAL_AA_ACCUMULATION_1,
		RE::RENDER_TARGETS::kTEMPORAL_AA_ACCUMULATION_2,
		RE::RENDER_TARGETS::kTEMPORAL_AA_MASK,
		RE::RENDER_TARGETS::kTEMPORAL_AA_WATER_1,
		RE::RENDER_TARGETS::kTEMPORAL_AA_WATER_2,
	};

	bool IsSubmittedVRPresentationTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		return std::find(
				   kSubmittedVRPresentationTargets.begin(),
				   kSubmittedVRPresentationTargets.end(),
				   a_target) != kSubmittedVRPresentationTargets.end();
	}

	bool IsVRNativeLayoutSubmitProtectedTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		return std::find(
				   kVRNativeLayoutSubmitProtectedTargets.begin(),
				   kVRNativeLayoutSubmitProtectedTargets.end(),
				   a_target) != kVRNativeLayoutSubmitProtectedTargets.end();
	}

	bool IsVRRenderScaleDisplaySizedTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		return std::find(
				   kVRRenderScaleDisplaySizedTargets.begin(),
				   kVRRenderScaleDisplaySizedTargets.end(),
				   a_target) != kVRRenderScaleDisplaySizedTargets.end();
	}

	bool IsVRRenderScaleEngineSizedTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		return std::find(
				   kVRRenderScaleEngineSizedTargets.begin(),
				   kVRRenderScaleEngineSizedTargets.end(),
				   a_target) != kVRRenderScaleEngineSizedTargets.end();
	}

	bool IsVRPresentationRenderTargetTexture(ID3D11Texture2D* a_texture)
	{
		auto renderer = globals::game::renderer;
		if (!a_texture || !renderer)
			return false;

		const auto& renderTargets = renderer->GetRuntimeData().renderTargets;
		for (const auto target : kSubmittedVRPresentationTargets) {
			if (renderTargets[target].texture == a_texture)
				return true;
		}

		return false;
	}

	template <size_t N>
	bool IsRenderTargetTextureInTargets(
		ID3D11Texture2D* a_texture,
		const std::array<RE::RENDER_TARGETS::RENDER_TARGET, N>& a_targets)
	{
		auto renderer = globals::game::renderer;
		if (!a_texture || !renderer)
			return false;

		const auto& renderTargets = renderer->GetRuntimeData().renderTargets;
		const int targetCount = Util::GetRenderTargetCount();
		for (const auto target : a_targets) {
			const auto targetIndex = static_cast<int>(target);
			if (targetIndex < 0 || targetIndex >= targetCount)
				continue;

			if (renderTargets[targetIndex].texture == a_texture || renderTargets[targetIndex].textureCopy == a_texture)
				return true;
		}

		return false;
	}

	bool IsVRNativeLayoutSubmitProtectedRenderTargetTexture(ID3D11Texture2D* a_texture)
	{
		return IsRenderTargetTextureInTargets(a_texture, kVRNativeLayoutSubmitProtectedTargets);
	}

	bool IsVRObservedMenuPresentationSeedRenderTargetTexture(ID3D11Texture2D* a_texture)
	{
		return IsRenderTargetTextureInTargets(a_texture, kVRObservedMenuPresentationSeedTargets);
	}

	bool IsVRObservedMenuPresentationFollowRenderTargetTexture(ID3D11Texture2D* a_texture)
	{
		return IsRenderTargetTextureInTargets(a_texture, kVRObservedMenuPresentationFollowTargets);
	}

	template <class Predicate>
	bool IsCurrentRenderTargetTextureMatch(Predicate&& a_predicate)
	{
		auto context = globals::d3d::context;
		if (!context)
			return false;

		ID3D11RenderTargetView* rtv = nullptr;
		context->OMGetRenderTargets(1, &rtv, nullptr);
		if (!rtv)
			return false;
		auto rtvRelease = ScopeExit([&]() {
			rtv->Release();
		});

		ID3D11Resource* resource = nullptr;
		rtv->GetResource(&resource);
		if (!resource)
			return false;
		auto resourceRelease = ScopeExit([&]() {
			resource->Release();
		});

		ID3D11Texture2D* texture = nullptr;
		if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) || !texture)
			return false;
		auto textureRelease = ScopeExit([&]() {
			texture->Release();
		});

		return a_predicate(texture);
	}

	bool IsCurrentRenderTargetVRNativeLayoutSubmitProtectedTexture()
	{
		return IsCurrentRenderTargetTextureMatch(IsVRNativeLayoutSubmitProtectedRenderTargetTexture);
	}

	bool IsCurrentRenderTargetVRObservedMenuPresentationSeedTexture()
	{
		return IsCurrentRenderTargetTextureMatch(IsVRObservedMenuPresentationSeedRenderTargetTexture);
	}

	bool IsCurrentRenderTargetVRObservedMenuPresentationFollowTexture()
	{
		return IsCurrentRenderTargetTextureMatch(IsVRObservedMenuPresentationFollowRenderTargetTexture);
	}

	const char* GetVRMenuCompositionTargetName(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		switch (a_target) {
		case RE::RENDER_TARGETS::kMENUBG:
			return "kMENUBG";
		case RE::RENDER_TARGETS::kPROJECTEDMENU:
			return "kPROJECTEDMENU";
		case RE::RENDER_TARGETS::kHUDMENU:
			return "kHUDMENU";
		default:
			return "unknown";
		}
	}

	struct VRMenuCompositionTargetMatch
	{
		bool matched = false;
		RE::RENDER_TARGETS::RENDER_TARGET target = RE::RENDER_TARGETS::kMENUBG;
		const char* name = "unknown";
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t samples = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	};

	bool TryResolveVRMenuCompositionView(ID3D11View* a_view, VRMenuCompositionTargetMatch& a_outMatch)
	{
		auto renderer = globals::game::renderer;
		if (!renderer || !a_view)
			return false;

		ID3D11Resource* resource = nullptr;
		a_view->GetResource(&resource);
		if (!resource)
			return false;
		auto resourceRelease = ScopeExit([&]() {
			resource->Release();
		});

		ID3D11Texture2D* texture = nullptr;
		if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) || !texture)
			return false;
		auto textureRelease = ScopeExit([&]() {
			texture->Release();
		});

		static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 3> targets{
			RE::RENDER_TARGETS::kMENUBG,
			RE::RENDER_TARGETS::kPROJECTEDMENU,
			RE::RENDER_TARGETS::kHUDMENU,
		};

		const auto& renderTargets = renderer->GetRuntimeData().renderTargets;
		const int targetCount = Util::GetRenderTargetCount();
		for (const auto target : targets) {
			const auto targetIndex = static_cast<int>(target);
			if (targetIndex < 0 || targetIndex >= targetCount)
				continue;

			const auto& renderTarget = renderTargets[targetIndex];
			if (renderTarget.texture != texture && renderTarget.textureCopy != texture)
				continue;

			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			a_outMatch.matched = true;
			a_outMatch.target = target;
			a_outMatch.name = GetVRMenuCompositionTargetName(target);
			a_outMatch.width = desc.Width;
			a_outMatch.height = desc.Height;
			a_outMatch.samples = desc.SampleDesc.Count;
			a_outMatch.format = desc.Format;
			return true;
		}

		return false;
	}

	uint32_t ClampPositiveDimension(float a_dimension)
	{
		if (!std::isfinite(a_dimension))
			return 1u;

		return std::max<uint32_t>(1u, static_cast<uint32_t>(std::floor(std::max(a_dimension, 1.0f))));
	}

	bool RenderTargetTextureSizeMatches(ID3D11Texture2D* a_texture, uint32_t a_width, uint32_t a_height)
	{
		if (!a_texture)
			return false;

		D3D11_TEXTURE2D_DESC desc{};
		a_texture->GetDesc(&desc);
		return desc.Width == a_width && desc.Height == a_height;
	}

	struct RenderTargetSizeCheckOptions
	{
		bool allowMissing = false;
		bool requireCopy = false;
		bool requireUAV = false;
	};

	bool RenderTargetDataSizeMatches(
		const RE::BSGraphics::RenderTargetData& a_renderTarget,
		uint32_t a_width,
		uint32_t a_height,
		RenderTargetSizeCheckOptions a_options)
	{
		if (!a_renderTarget.texture)
			return a_options.allowMissing;

		if (!RenderTargetTextureSizeMatches(a_renderTarget.texture, a_width, a_height) ||
			!a_renderTarget.RTV ||
			!a_renderTarget.SRV) {
			return false;
		}

		if (a_options.requireUAV && !a_renderTarget.UAV)
			return false;

		if (a_options.requireCopy && !a_renderTarget.textureCopy)
			return false;

		if (a_renderTarget.textureCopy) {
			if (!RenderTargetTextureSizeMatches(a_renderTarget.textureCopy, a_width, a_height) ||
				!a_renderTarget.SRVCopy) {
				return false;
			}
		}

		return true;
	}

	bool RequiredRenderTargetTextureSizeMatches(RE::RENDER_TARGETS::RENDER_TARGET a_target, uint32_t a_width, uint32_t a_height)
	{
		auto renderer = globals::game::renderer;
		if (!renderer)
			return false;

		const auto& renderTarget = renderer->GetRuntimeData().renderTargets[a_target];
		const bool requiresCopy =
			a_target == RE::RENDER_TARGETS::kREFRACTION_NORMALS ||
			a_target == RE::RENDER_TARGETS::kUNDERWATER_MASK;
		const bool requiresUAV = a_target == RE::RENDER_TARGETS::kMAIN;
		return RenderTargetDataSizeMatches(
			renderTarget,
			a_width,
			a_height,
			{ .requireCopy = requiresCopy, .requireUAV = requiresUAV });
	}

	bool ExistingRenderTargetTextureSizeMatches(RE::RENDER_TARGETS::RENDER_TARGET a_target, uint32_t a_width, uint32_t a_height)
	{
		auto renderer = globals::game::renderer;
		if (!renderer)
			return false;

		const auto& renderTarget = renderer->GetRuntimeData().renderTargets[a_target];
		return RenderTargetDataSizeMatches(renderTarget, a_width, a_height, { .allowMissing = true });
	}

	bool DepthStencilDataSizeMatches(
		const RE::BSGraphics::DepthStencilData& a_depthStencil,
		uint32_t a_width,
		uint32_t a_height,
		bool a_requireStencilSRV)
	{
		if (!RenderTargetTextureSizeMatches(a_depthStencil.texture, a_width, a_height) ||
			!a_depthStencil.views[0] ||
			!a_depthStencil.depthSRV) {
			return false;
		}

		if (a_requireStencilSRV && !a_depthStencil.stencilSRV)
			return false;

		return true;
	}

	bool RequiredDepthStencilTextureSizeMatches(
		RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL a_target,
		uint32_t a_width,
		uint32_t a_height,
		bool a_requireStencilSRV)
	{
		auto renderer = globals::game::renderer;
		if (!renderer)
			return false;

		const auto& depthStencil = renderer->GetDepthStencilData().depthStencils[a_target];
		return DepthStencilDataSizeMatches(depthStencil, a_width, a_height, a_requireStencilSRV);
	}

	bool AreVRRenderScaleRenderTargetsSizedForDimensions(float2 a_engineSize, float2 a_displaySize)
	{
		if (!globals::game::isVR || !globals::game::renderer)
			return false;

		if (a_displaySize.x <= 0.0f || a_displaySize.y <= 0.0f)
			return false;

		if (a_engineSize.x <= 0.0f || a_engineSize.y <= 0.0f)
			return false;

		const uint32_t engineWidth = ClampPositiveDimension(a_engineSize.x);
		const uint32_t engineHeight = ClampPositiveDimension(a_engineSize.y);
		for (const auto target : kVRRenderScaleEngineSizedTargets) {
			if (!RequiredRenderTargetTextureSizeMatches(target, engineWidth, engineHeight))
				return false;
		}

		if (!RequiredDepthStencilTextureSizeMatches(RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN, engineWidth, engineHeight, false) ||
			!RequiredDepthStencilTextureSizeMatches(RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY, engineWidth, engineHeight, true)) {
			return false;
		}

		if (!RequiredRenderTargetTextureSizeMatches(
				RE::RENDER_TARGETS::kUNDERWATER_MASK,
				ClampPositiveDimension(a_engineSize.x * 0.5f),
				engineHeight)) {
			return false;
		}

		const uint32_t displayWidth = ClampPositiveDimension(a_displaySize.x);
		const uint32_t displayHeight = ClampPositiveDimension(a_displaySize.y);
		for (const auto target : kVRRenderScaleDisplaySizedTargets) {
			if (!ExistingRenderTargetTextureSizeMatches(target, displayWidth, displayHeight))
				return false;
		}

		return true;
	}

	void CopyResourceIfNonAliased(ID3D11DeviceContext* a_context, ID3D11Resource* a_dst, ID3D11Resource* a_src)
	{
		if (a_context && a_dst && a_src && a_dst != a_src) {
			a_context->CopyResource(a_dst, a_src);
		}
	}

	float ClampFoveatedCenterScale(float value)
	{
		return FoveatedCommon::ClampCenterScale(value);
	}

	float ClampFoveatedCenterHorizontalScale(float value)
	{
		return FoveatedCommon::ClampCenterHorizontalScale(value);
	}

	float ClampFoveatedMaskOffsetAdjustment(float value)
	{
		if (!std::isfinite(value))
			return 0.0f;
		return std::clamp(value, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax);
	}

	uint ClampToggleUInt(uint value)
	{
		return std::min<uint>(value, 1u);
	}

	uint ClampQualityModeUInt(uint value)
	{
		return std::min<uint>(value, Upscaling::kQualityModeMaxIndex);
	}

	uint ClampDLSSSharpenerModeUInt(uint value)
	{
		return std::min<uint>(value, Upscaling::kDLSSSharpenerModeMaxIndex);
	}

	Upscaling::UpscaleMethod ClampUpscaleMethod(uint value, Upscaling::UpscaleMethod maxMethod)
	{
		return static_cast<Upscaling::UpscaleMethod>(std::clamp(
			static_cast<int>(value),
			static_cast<int>(Upscaling::UpscaleMethod::kNONE),
			static_cast<int>(maxMethod)));
	}

	std::string_view TrimAsciiWhitespace(std::string_view value);
	bool TryParseAsciiBoolSetting(std::string value, bool& outValue);
	void AddUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path);

	bool StartsWithAsciiInsensitive(std::string_view value, std::string_view prefix)
	{
		if (value.size() < prefix.size())
			return false;

		for (size_t i = 0; i < prefix.size(); ++i) {
			const auto lhs = static_cast<unsigned char>(value[i]);
			const auto rhs = static_cast<unsigned char>(prefix[i]);
			if (std::tolower(lhs) != std::tolower(rhs))
				return false;
		}
		return true;
	}

	bool EqualsAsciiInsensitive(std::string_view lhs, std::string_view rhs)
	{
		lhs = TrimAsciiWhitespace(lhs);
		rhs = TrimAsciiWhitespace(rhs);
		if (lhs.size() != rhs.size())
			return false;

		for (size_t i = 0; i < lhs.size(); ++i) {
			const auto lhsChar = static_cast<unsigned char>(lhs[i]);
			const auto rhsChar = static_cast<unsigned char>(rhs[i]);
			if (std::tolower(lhsChar) != std::tolower(rhsChar))
				return false;
		}
		return true;
	}

	bool TryParseUnsigned(std::string_view value, uint32_t& outValue)
	{
		value = TrimAsciiWhitespace(value);
		if (value.empty())
			return false;
		if (value.front() == '-')
			return false;

		try {
			const std::string text(value);
			size_t parsedChars = 0;
			const auto parsedValue = std::stoul(text, &parsedChars, 10);
			if (parsedChars != text.size())
				return false;

			outValue = static_cast<uint32_t>(std::min<unsigned long>(parsedValue, std::numeric_limits<uint32_t>::max()));
			return true;
		} catch (...) {
			return false;
		}
	}

	bool TryParseToggle(std::string_view value, bool& outValue)
	{
		uint32_t parsedValue = 0;
		if (TryParseUnsigned(value, parsedValue)) {
			outValue = ClampToggleUInt(parsedValue) != 0;
			return true;
		}

		return TryParseAsciiBoolSetting(std::string(value), outValue);
	}

	struct VRFpsStabilizerUpscalingProfile
	{
		bool hasUpscaleMethod = false;
		Upscaling::UpscaleMethod upscaleMethod = Upscaling::UpscaleMethod::kNONE;
		bool hasLegacyMethodSelection = false;
		bool hasQualityMode = false;
		uint32_t qualityMode = 0;
		bool hasDLSSPreset = false;
		uint32_t dlssPreset = 1;
		bool hasRenderScaleMode = false;
		bool renderScaleMode = false;

		[[nodiscard]] bool HasAnySetting() const
		{
			return hasUpscaleMethod || hasLegacyMethodSelection || hasQualityMode || hasDLSSPreset || hasRenderScaleMode;
		}
	};

	struct VRFpsStabilizerUpscalingProfiles
	{
		std::filesystem::path path;
		VRFpsStabilizerUpscalingProfile interior;
		VRFpsStabilizerUpscalingProfile exterior;
	};

	struct VRFpsStabilizerTransitionTarget
	{
		Upscaling::UpscaleMethod method = Upscaling::UpscaleMethod::kNONE;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 1;
		bool renderScaleMode = false;
	};

	std::filesystem::path GetVRFpsStabilizerIniDefaultPath()
	{
		return Util::PathHelpers::GetDataPath() / "SKSE" / "Plugins" / "VRFpsStabilizer.ini";
	}

	std::filesystem::path FindVRFpsStabilizerIniPath()
	{
		std::vector<std::filesystem::path> candidatePaths;
		AddUniquePath(candidatePaths, GetVRFpsStabilizerIniDefaultPath());
		try {
			const auto currentDirectory = std::filesystem::current_path();
			AddUniquePath(candidatePaths, currentDirectory / "Data" / "SKSE" / "Plugins" / "VRFpsStabilizer.ini");
			AddUniquePath(candidatePaths, currentDirectory / "SKSE" / "Plugins" / "VRFpsStabilizer.ini");
		} catch (const std::exception& e) {
			logger::warn("[Upscaling] VR FPS Stabilizer Sync could not inspect current directory: {}", e.what());
		}

		for (const auto& path : candidatePaths) {
			std::error_code ec;
			if (std::filesystem::exists(path, ec) && !ec)
				return path;
		}

		return GetVRFpsStabilizerIniDefaultPath();
	}

	uint32_t VRFpsStabilizerUpscalePresetToQualityMode(uint32_t a_preset)
	{
		switch (a_preset) {
		case 5:
			return 1u;  // API kHoshipa
		case 6:
			return 2u;  // API kUltraQuality
		case 1:
			return 3u;  // API kQuality
		case 2:
			return 4u;  // API kBalanced
		case 3:
			return 5u;  // API kPerformance
		case 4:
			return 6u;  // API kUltraPerformance
		default:
			return 0u;  // API kNativeAA/kDLAA or invalid
		}
	}

	void ApplyVRFpsStabilizerUpscalingSetting(VRFpsStabilizerUpscalingProfile& profile, std::string_view settingName, std::string_view value)
	{
		const bool isUpscaleMethod = EqualsAsciiInsensitive(settingName, "UpscaleMethod");
		const bool isLegacyDLSSMode = EqualsAsciiInsensitive(settingName, "DLSSMode");
		const bool isUpscalePreset = EqualsAsciiInsensitive(settingName, "UpscalePreset") || isLegacyDLSSMode;
		const bool isQualityMode = EqualsAsciiInsensitive(settingName, "QualityMode");
		const bool isLegacyDLSSProfile = EqualsAsciiInsensitive(settingName, "DLSSProfile");
		const bool isDLSSPreset = isLegacyDLSSProfile || EqualsAsciiInsensitive(settingName, "DLSSPreset");
		const bool isLegacyRenderScaleMode =
			EqualsAsciiInsensitive(settingName, "RenderAtUpscaleRes") ||
			EqualsAsciiInsensitive(settingName, "RenderAtUpscaleResEnabled");
		const bool isRenderScaleMode = EqualsAsciiInsensitive(settingName, "RenderScaleMode") || isLegacyRenderScaleMode;

		if (isRenderScaleMode) {
			bool renderScaleMode = false;
			if (!TryParseToggle(value, renderScaleMode))
				return;

			profile.renderScaleMode = renderScaleMode;
			profile.hasRenderScaleMode = true;
			if (isLegacyRenderScaleMode)
				profile.hasLegacyMethodSelection = true;
			return;
		}

		uint32_t parsedValue = 0;
		if (!TryParseUnsigned(value, parsedValue))
			return;

		if (isUpscaleMethod) {
			profile.upscaleMethod = ClampUpscaleMethod(parsedValue, Upscaling::UpscaleMethod::kDLSS);
			profile.hasUpscaleMethod = true;
		} else if (isUpscalePreset) {
			profile.qualityMode = VRFpsStabilizerUpscalePresetToQualityMode(parsedValue);
			profile.hasQualityMode = true;
			if (isLegacyDLSSMode)
				profile.hasLegacyMethodSelection = true;
		} else if (isQualityMode) {
			profile.qualityMode = ClampQualityModeUInt(parsedValue);
			profile.hasQualityMode = true;
		} else if (isDLSSPreset) {
			profile.dlssPreset = std::min<uint32_t>(parsedValue, Upscaling::kDLSSPresetMaxIndex);
			profile.hasDLSSPreset = true;
			if (isLegacyDLSSProfile)
				profile.hasLegacyMethodSelection = true;
		}
	}

	bool TryLoadVRFpsStabilizerUpscalingProfiles(VRFpsStabilizerUpscalingProfiles& outProfiles)
	{
		outProfiles = {};
		outProfiles.path = FindVRFpsStabilizerIniPath();

		std::error_code ec;
		if (!std::filesystem::exists(outProfiles.path, ec) || ec)
			return false;

		std::ifstream iniFile(outProfiles.path);
		if (!iniFile.is_open()) {
			logger::warn("[Upscaling] VR FPS Stabilizer Sync could not open {}.", outProfiles.path.string());
			return false;
		}

		bool inConditionalSection = false;
		std::string line;
		while (std::getline(iniFile, line)) {
			const auto hashCommentOffset = line.find('#');
			const auto semicolonCommentOffset = line.find(';');
			const auto commentOffset = std::min(
				hashCommentOffset != std::string::npos ? hashCommentOffset : line.size(),
				semicolonCommentOffset != std::string::npos ? semicolonCommentOffset : line.size());
			if (commentOffset != line.size())
				line.erase(commentOffset);

			std::string_view lineView = TrimAsciiWhitespace(line);
			if (lineView.empty())
				continue;

			if (lineView.front() == '[' && lineView.back() == ']') {
				lineView.remove_prefix(1);
				lineView.remove_suffix(1);
				inConditionalSection = EqualsAsciiInsensitive(lineView, "Conditional");
				continue;
			}

			if (!inConditionalSection)
				continue;

			const auto pipeOffset = lineView.find('|');
			if (pipeOffset == std::string_view::npos)
				continue;

			const auto condition = TrimAsciiWhitespace(lineView.substr(0, pipeOffset));
			if (condition.find(',') != std::string_view::npos)
				continue;

			const bool interiorProfile = EqualsAsciiInsensitive(condition, "Interior");
			const bool exteriorProfile = EqualsAsciiInsensitive(condition, "Exterior");
			if (!interiorProfile && !exteriorProfile)
				continue;

			auto command = TrimAsciiWhitespace(lineView.substr(pipeOffset + 1));
			if (!StartsWithAsciiInsensitive(command, "CS>"))
				continue;

			command.remove_prefix(3);
			command = TrimAsciiWhitespace(command);
			const auto equalsOffset = command.find('=');
			if (equalsOffset == std::string_view::npos)
				continue;

			const auto settingName = TrimAsciiWhitespace(command.substr(0, equalsOffset));
			const auto settingValue = TrimAsciiWhitespace(command.substr(equalsOffset + 1));
			auto& profile = interiorProfile ? outProfiles.interior : outProfiles.exterior;
			ApplyVRFpsStabilizerUpscalingSetting(profile, settingName, settingValue);
		}

		return outProfiles.interior.HasAnySetting() || outProfiles.exterior.HasAnySetting();
	}

	VRFpsStabilizerTransitionTarget ResolveVRFpsStabilizerTransitionTarget(
		const Upscaling& a_upscaling,
		const VRFpsStabilizerUpscalingProfile& a_profile)
	{
		VRFpsStabilizerTransitionTarget target;
		const auto currentMethod = a_upscaling.GetConfiguredUpscaleMethodForTransition();
		target.method = a_profile.hasUpscaleMethod ?
		                    a_profile.upscaleMethod :
		                    (a_profile.hasLegacyMethodSelection ? a_upscaling.GetLegacyDLSSPreferredUpscaleMethodForAPI() : currentMethod);
		target.qualityMode = a_profile.hasQualityMode ? a_profile.qualityMode : a_upscaling.GetEffectiveUpscalingQualityMode();
		target.dlssPreset = a_profile.hasDLSSPreset ? a_profile.dlssPreset : a_upscaling.GetEffectiveDLSSPreset();

		const bool requestedRenderScaleMode = a_profile.hasRenderScaleMode ? a_profile.renderScaleMode : a_upscaling.IsRenderScaleModeRequested();
		if (requestedRenderScaleMode &&
			IsRenderScaleMethodEligible(target.method) &&
			!a_profile.hasQualityMode &&
			!IsRenderScaleQualityMode(target.qualityMode)) {
			target.qualityMode = kDefaultRenderScaleQualityMode;
		}

		target.renderScaleMode =
			requestedRenderScaleMode &&
			IsRenderScaleMethodEligible(target.method) &&
			IsRenderScaleQualityMode(target.qualityMode);
		return target;
	}

	uint MigrateLegacyQualityModeUInt(uint value)
	{
		switch (value) {
		case 0:
			return 0u;
		case 1:
			return 3u;
		case 2:
			return 4u;
		case 3:
			return 5u;
		case 4:
			return 6u;
		// Preserve values written by transitional builds that introduced 0-6 modes
		// before `qualityModeSchemaVersion` existed.
		case 5:
		case 6:
			return value;
		default:
			return 6u;
		}
	}

	const char* GetQualityModeName(uint value, bool isDLSS)
	{
		switch (ClampQualityModeUInt(value)) {
		case 1:
			return "Hoshipa";
		case 2:
			return "Ultra Quality";
		case 3:
			return "Quality";
		case 4:
			return "Balanced";
		case 5:
			return "Performance";
		case 6:
			return "Ultra Performance";
		default:
			return isDLSS ? "DLAA" : "Native AA";
		}
	}

	uint ClampStreamlineLogLevelUInt(uint value)
	{
		return std::min<uint>(value, 2u);
	}

	void DestroyTexture(Texture2D*& texture)
	{
		if (!texture)
			return;

		texture->srv = nullptr;
		texture->uav = nullptr;
		texture->rtv = nullptr;
		texture->dsv = nullptr;
		texture->resource = nullptr;
		delete texture;
		texture = nullptr;
	}

	float GetDLSSRCASSharpness(float a_sharpness)
	{
		const float clampedSharpness = std::clamp(a_sharpness, 0.0f, 1.0f);
		return exp2((2.0f * clampedSharpness) - 2.0f) * kDLSSRCASSharpnessOverdrive;
	}

	float GetDLSSLumaSharpness(float a_sharpness)
	{
		const float clampedSharpness = std::clamp(a_sharpness, 0.0f, 1.0f);
		return exp2((2.0f * clampedSharpness) - 2.0f) * kDLSSLumaSharpnessOverdrive;
	}

	constexpr std::array<const char*, Upscaling::kDLSSSharpenerModeMaxIndex + 1> kDLSSSharpenerModeNames = {
		"Off",
		"RCAS",
		"Luma Unsharp"
	};

	const char* GetDLSSSharpenerModeName(Upscaling::DLSSSharpenerMode a_mode)
	{
		const uint index = static_cast<uint>(a_mode);
		if (index >= kDLSSSharpenerModeNames.size())
			return "RCAS";
		return kDLSSSharpenerModeNames[index];
	}

	bool DispatchDLSSSharpener(Upscaling& a_upscaling, ID3D11ShaderResourceView* a_inputSRV, ID3D11UnorderedAccessView* a_outputUAV, uint32_t a_width = 0, uint32_t a_height = 0)
	{
		switch (a_upscaling.GetDLSSSharpenerMode()) {
		case Upscaling::DLSSSharpenerMode::RCAS:
			return Upscaling::rcas.ApplySharpen(a_inputSRV, a_outputUAV, GetDLSSRCASSharpness(a_upscaling.settings.sharpnessDLSS), a_width, a_height);
		case Upscaling::DLSSSharpenerMode::LumaUnsharp:
			return Upscaling::lumaSharpen.ApplySharpen(a_inputSRV, a_outputUAV, GetDLSSLumaSharpness(a_upscaling.settings.sharpnessDLSS), a_width, a_height);
		case Upscaling::DLSSSharpenerMode::Off:
		default:
			return true;
		}
	}

	struct OpenCompositeSettingValue
	{
		bool value = false;
		std::string configPath;
	};

	struct OpenCompositeUpscalingSettings
	{
		OpenCompositeSettingValue dlssEnabled;
		OpenCompositeSettingValue fsrEnabled;
		OpenCompositeSettingValue dlaaEnabled;
		OpenCompositeSettingValue fsrNativeAA;
		OpenCompositeSettingValue fsr3PostAAEnabled;
	};

	struct DetectedOpenCompositeUpscalingBlocker
	{
		bool active = false;
		std::string settingName;
		std::string configPath;
	};

	std::string_view TrimAsciiWhitespace(std::string_view value)
	{
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
			value.remove_prefix(1);
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
			value.remove_suffix(1);
		return value;
	}

	std::string ToLowerAscii(std::string_view value)
	{
		std::string result(value);
		std::ranges::transform(result, result.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return result;
	}

	bool TryParseAsciiBoolSetting(std::string value, bool& outValue)
	{
		value = ToLowerAscii(TrimAsciiWhitespace(value));
		if (value == "true" || value == "yes" || value == "on" || value == "enabled" || value == "1") {
			outValue = true;
			return true;
		}
		if (value == "false" || value == "no" || value == "off" || value == "disabled" || value == "0") {
			outValue = false;
			return true;
		}
		return false;
	}

	std::string PathToDisplayString(const std::filesystem::path& path)
	{
		return path.string();
	}

	void AddUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path)
	{
		if (path.empty())
			return;

		auto normalized = path.lexically_normal().wstring();
		std::ranges::transform(normalized, normalized.begin(), [](wchar_t c) {
			return static_cast<wchar_t>(std::towlower(c));
		});

		const bool alreadyAdded = std::ranges::any_of(paths, [&](const std::filesystem::path& existing) {
			auto existingNormalized = existing.lexically_normal().wstring();
			std::ranges::transform(existingNormalized, existingNormalized.begin(), [](wchar_t c) {
				return static_cast<wchar_t>(std::towlower(c));
			});
			return existingNormalized == normalized;
		});
		if (!alreadyAdded)
			paths.push_back(path);
	}

	std::filesystem::path GetCurrentDirectoryPath()
	{
		std::wstring buffer(MAX_PATH, L'\0');
		const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
		if (length == 0)
			return {};

		if (length >= buffer.size()) {
			buffer.resize(length + 1);
			const DWORD retryLength = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
			if (retryLength == 0 || retryLength >= buffer.size())
				return {};
			buffer.resize(retryLength);
		} else {
			buffer.resize(length);
		}

		return std::filesystem::path(buffer);
	}

	std::filesystem::path GetLoadedOpenVRDirectory()
	{
		HMODULE openVRModule = GetModuleHandleW(L"openvr_api.dll");
		if (!openVRModule)
			return {};

		std::wstring buffer(MAX_PATH, L'\0');
		const DWORD length = GetModuleFileNameW(openVRModule, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size())
			return {};

		buffer.resize(length);
		return std::filesystem::path(buffer).parent_path();
	}

	bool ShouldProbeOpenCompositeConfig()
	{
		if (!globals::game::isVR)
			return false;

		const auto& cachedInfo = globals::features::vr.openVRInfo;
		if (cachedInfo.isAvailable)
			return cachedInfo.runtimeType == VRDetection::RuntimeType::OpenComposite;

		const auto detectedInfo = VRDetection::Detect();
		return detectedInfo.isAvailable &&
		       detectedInfo.runtimeType == VRDetection::RuntimeType::OpenComposite;
	}

	std::vector<std::filesystem::path> GetOpenCompositeConfigCandidates()
	{
		std::vector<std::filesystem::path> candidates;

		const auto loadedOpenVRDirectory = GetLoadedOpenVRDirectory();
		if (!loadedOpenVRDirectory.empty())
			AddUniquePath(candidates, loadedOpenVRDirectory / L"opencomposite.ini");

		const auto currentDirectory = GetCurrentDirectoryPath();
		if (!currentDirectory.empty()) {
			AddUniquePath(candidates, currentDirectory / L"opencomposite.ini");
			AddUniquePath(candidates, currentDirectory / L"opencomposite_ext.ini");
		}

		return candidates;
	}

	bool TryReadIniBoolSetting(const CSimpleIniA& ini, const char* key, bool& outValue)
	{
		auto tryReadSection = [&](const char* section) {
			const char* rawValue = ini.GetValue(section, key, nullptr);
			return rawValue && TryParseAsciiBoolSetting(rawValue, outValue);
		};

		if (tryReadSection(""))
			return true;

		CSimpleIniA::TNamesDepend sections;
		ini.GetAllSections(sections);
		for (const auto& section : sections) {
			if (section.pItem && tryReadSection(section.pItem))
				return true;
		}

		return false;
	}

	void UpdateOpenCompositeSettingValue(OpenCompositeSettingValue& setting, const CSimpleIniA& ini, const char* key, const std::filesystem::path& path)
	{
		bool parsedValue = false;
		if (!TryReadIniBoolSetting(ini, key, parsedValue))
			return;

		setting.value = parsedValue;
		setting.configPath = PathToDisplayString(path);
	}

	OpenCompositeUpscalingSettings ReadOpenCompositeUpscalingSettings()
	{
		OpenCompositeUpscalingSettings settings;

		std::error_code ec;
		for (const auto& path : GetOpenCompositeConfigCandidates()) {
			if (!std::filesystem::exists(path, ec))
				continue;
			ec.clear();

			CSimpleIniA ini;
			ini.SetUnicode();
			const SI_Error rc = ini.LoadFile(path.c_str());
			if (rc < 0) {
				logger::warn("[Upscaling] Failed to read Open Composite config '{}': {}", PathToDisplayString(path), rc);
				continue;
			}

			UpdateOpenCompositeSettingValue(settings.dlssEnabled, ini, "dlssEnabled", path);
			UpdateOpenCompositeSettingValue(settings.fsrEnabled, ini, "fsrEnabled", path);
			UpdateOpenCompositeSettingValue(settings.dlaaEnabled, ini, "dlaaEnabled", path);
			UpdateOpenCompositeSettingValue(settings.fsrNativeAA, ini, "fsrNativeAA", path);
			UpdateOpenCompositeSettingValue(settings.fsr3PostAAEnabled, ini, "fsr3PostAAEnabled", path);
		}

		return settings;
	}

	DetectedOpenCompositeUpscalingBlocker FindOpenCompositeUpscalingBlocker()
	{
		DetectedOpenCompositeUpscalingBlocker blocker;
		if (!globals::game::isVR)
			return blocker;

		Util::OCUExternalUpscalerState externalState{};
		if (Util::TryReadOCUExternalUpscalerState(externalState)) {
			blocker.active = true;
			blocker.settingName = "OpenCompositeUnleashedSharedState";
			blocker.configPath = "Local\\OpenCompositeUnleashedUpscalingState";
			return blocker;
		}

		if (!ShouldProbeOpenCompositeConfig())
			return blocker;

		const auto settings = ReadOpenCompositeUpscalingSettings();
		auto setBlocker = [&](const char* settingName, const OpenCompositeSettingValue& setting) {
			blocker.active = true;
			blocker.settingName = settingName;
			blocker.configPath = setting.configPath;
		};

		if (settings.dlaaEnabled.value)
			setBlocker("dlaaEnabled", settings.dlaaEnabled);
		else if (settings.fsrNativeAA.value)
			setBlocker("fsrNativeAA", settings.fsrNativeAA);
		else if (settings.fsr3PostAAEnabled.value)
			setBlocker("fsr3PostAAEnabled", settings.fsr3PostAAEnabled);
		else if (settings.dlssEnabled.value)
			setBlocker("dlssEnabled", settings.dlssEnabled);
		else if (settings.fsrEnabled.value)
			setBlocker("fsrEnabled", settings.fsrEnabled);

		return blocker;
	}

	float ClampPeripheryTAACenterBlendFeather(float value)
	{
		if (!std::isfinite(value))
			return FoveatedCommon::kCenterFeather;
		return std::clamp(value, kPeripheryTAACenterBlendFeatherMin, kPeripheryTAACenterBlendFeatherMax);
	}

	float ClampPeripheryTAAOuterScale(float value)
	{
		if (!std::isfinite(value))
			return 1.0f;
		return std::clamp(value, kPeripheryTAAOuterScaleMin, kPeripheryTAAOuterScaleMax);
	}

	float GetPeripheryTAAOuterScaleFloor(float centerScale)
	{
		centerScale = ClampFoveatedCenterScale(centerScale);

		// Ring circumference is owned by outer scale; center feather only affects
		// the blend between center and periphery, not the ring radius.
		const float minOuterScale = centerScale;
		return std::clamp(minOuterScale, kPeripheryTAAOuterScaleMin, kPeripheryTAAOuterScaleMax);
	}

	float ClampPeripheryTAAOuterScaleForCenter(float value, float centerScale)
	{
		const float minOuterScale = GetPeripheryTAAOuterScaleFloor(centerScale);
		return std::clamp(ClampPeripheryTAAOuterScale(value), minOuterScale, kPeripheryTAAOuterScaleMax);
	}

	float ClampFiniteUnitRange(float value, float fallback)
	{
		if (!std::isfinite(value))
			return fallback;
		return std::clamp(value, 0.0f, 1.0f);
	}

	int32_t QuantizePeripheryTAATileParam(float value)
	{
		if (!std::isfinite(value))
			value = 0.0f;
		return static_cast<int32_t>(std::lround(value * 10000.0f));
	}

	struct FoveatedMaskProfileParams
	{
		float centerScale = 0.6f;
		float centerHorizontalScale = 1.0f;
		float leftOffsetX = 0.0f;
		float leftOffsetY = 0.0f;
		float rightOffsetX = 0.0f;
		float rightOffsetY = 0.0f;
	};

	FoveatedMaskProfileParams GetFoveatedMaskProfileParams(const Upscaling::Settings& settings, bool usePeripheryTAAProfile)
	{
		FoveatedMaskProfileParams params{};
		params.centerScale = ClampFoveatedCenterScale(usePeripheryTAAProfile ? settings.periphery_taa_center_area : settings.foveatedCenterArea);
		params.centerHorizontalScale = ClampFoveatedCenterHorizontalScale(settings.foveatedCenterHorizontalScale);
		params.leftOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetX);
		params.leftOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetY);
		params.rightOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetX);
		params.rightOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetY);
		return params;
	}

	float FoveatedMaskDistanceUV(float uvX, float uvY, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY)
	{
		centerScale = ClampFoveatedCenterScale(centerScale);
		centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);

		const float radiusX = std::max(centerScale * centerHorizontalScale * 0.5f, 1e-4f);
		const float radiusY = std::max(centerScale * 0.5f, 1e-4f);
		const float centerX = std::clamp(0.5f + centerOffsetX, 0.0f, 1.0f);
		const float centerY = std::clamp(0.5f + centerOffsetY, 0.0f, 1.0f);
		const float normalizedX = std::abs((uvX - centerX) / radiusX);
		const float normalizedY = std::abs((uvY - centerY) / radiusY);
		const float pNorm = std::pow(normalizedX, FoveatedCommon::kMaskShapePower) + std::pow(normalizedY, FoveatedCommon::kMaskShapePower);
		return std::pow(std::max(pNorm, 0.0f), 1.0f / FoveatedCommon::kMaskShapePower);
	}

	float FoveatedMaskDistancePixelCenter(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY)
	{
		const float invWidth = width > 0 ? 1.0f / static_cast<float>(width) : 0.0f;
		const float invHeight = height > 0 ? 1.0f / static_cast<float>(height) : 0.0f;
		return FoveatedMaskDistanceUV((static_cast<float>(x) + 0.5f) * invWidth, (static_cast<float>(y) + 0.5f) * invHeight, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY);
	}

	float FoveatedMaskTileMinDistance(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, uint32_t width, uint32_t height, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY)
	{
		const float invWidth = width > 0 ? 1.0f / static_cast<float>(width) : 0.0f;
		const float invHeight = height > 0 ? 1.0f / static_cast<float>(height) : 0.0f;
		const float minUvX = (static_cast<float>(minX) + 0.5f) * invWidth;
		const float minUvY = (static_cast<float>(minY) + 0.5f) * invHeight;
		const float maxUvX = (static_cast<float>(maxX - 1u) + 0.5f) * invWidth;
		const float maxUvY = (static_cast<float>(maxY - 1u) + 0.5f) * invHeight;
		const float centerUvX = std::clamp(0.5f + centerOffsetX, 0.0f, 1.0f);
		const float centerUvY = std::clamp(0.5f + centerOffsetY, 0.0f, 1.0f);
		return FoveatedMaskDistanceUV(
			std::clamp(centerUvX, minUvX, maxUvX),
			std::clamp(centerUvY, minUvY, maxUvY),
			centerScale,
			centerHorizontalScale,
			centerOffsetX,
			centerOffsetY);
	}

	float FoveatedMaskTileMaxDistance(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, uint32_t width, uint32_t height, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY)
	{
		const uint32_t maxPixelX = maxX - 1u;
		const uint32_t maxPixelY = maxY - 1u;
		return std::max({ FoveatedMaskDistancePixelCenter(minX, minY, width, height, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY),
			FoveatedMaskDistancePixelCenter(maxPixelX, minY, width, height, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY),
			FoveatedMaskDistancePixelCenter(minX, maxPixelY, width, height, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY),
			FoveatedMaskDistancePixelCenter(maxPixelX, maxPixelY, width, height, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY) });
	}

	void SanitizeFoveatedSettings(Upscaling::Settings& settings)
	{
		settings.foveatedCenterArea = ClampFoveatedCenterScale(settings.foveatedCenterArea);
		settings.foveatedCenterHorizontalScale = ClampFoveatedCenterHorizontalScale(settings.foveatedCenterHorizontalScale);
		settings.foveatedLeftEyeMaskOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetX);
		settings.foveatedLeftEyeMaskOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetY);
		settings.foveatedRightEyeMaskOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetX);
		settings.foveatedRightEyeMaskOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetY);
		settings.periphery_taa_center_area = ClampFoveatedCenterScale(settings.periphery_taa_center_area);
	}

	bool IsDefaultFoveatedMaskGeometry(const Upscaling::Settings& settings)
	{
		const Upscaling::Settings defaults{};
		auto nearlyEqual = [](float lhs, float rhs) {
			return std::abs(lhs - rhs) <= 0.0001f;
		};

		return nearlyEqual(settings.foveatedCenterArea, defaults.foveatedCenterArea) &&
		       nearlyEqual(settings.foveatedCenterHorizontalScale, defaults.foveatedCenterHorizontalScale) &&
		       nearlyEqual(settings.foveatedLeftEyeMaskOffsetX, defaults.foveatedLeftEyeMaskOffsetX) &&
		       nearlyEqual(settings.foveatedLeftEyeMaskOffsetY, defaults.foveatedLeftEyeMaskOffsetY) &&
		       nearlyEqual(settings.foveatedRightEyeMaskOffsetX, defaults.foveatedRightEyeMaskOffsetX) &&
		       nearlyEqual(settings.foveatedRightEyeMaskOffsetY, defaults.foveatedRightEyeMaskOffsetY) &&
		       nearlyEqual(settings.periphery_taa_center_area, defaults.periphery_taa_center_area) &&
		       nearlyEqual(settings.periphery_taa_outer_scale, defaults.periphery_taa_outer_scale) &&
		       nearlyEqual(settings.periphery_taa_center_blend_feather, defaults.periphery_taa_center_blend_feather);
	}

	void SanitizeUpscalingSettings(Upscaling::Settings& settings)
	{
		const Upscaling::Settings defaults{};
		settings.upscaleMethod = std::min<uint>(settings.upscaleMethod, static_cast<uint>(Upscaling::UpscaleMethod::kDLSS));
		settings.upscaleMethodNoDLSS = std::min<uint>(settings.upscaleMethodNoDLSS, static_cast<uint>(Upscaling::UpscaleMethod::kFSR));
		settings.qualityMode = ClampQualityModeUInt(settings.qualityMode);
		settings.dlssPreset = std::min<uint>(settings.dlssPreset, Upscaling::kDLSSPresetMaxIndex);
		settings.renderScaleMode = ClampToggleUInt(settings.renderScaleMode);
		settings.perfMode = ClampToggleUInt(settings.perfMode);
		if (REL::Module::IsVR() && !IsRenderScaleQualityMode(settings.qualityMode)) {
			settings.renderScaleMode = 0;
			settings.perfMode = 0;
		} else if (REL::Module::IsVR() && settings.renderScaleMode) {
			settings.perfMode = 1;
		} else if (REL::Module::IsVR()) {
			settings.perfMode = 0;
		}
		settings.vrFpsStabilizerSync = settings.vrFpsStabilizerSync && REL::Module::IsVR();
		settings.frameLimitMode = ClampToggleUInt(settings.frameLimitMode);
		settings.frameGenerationMode = ClampToggleUInt(settings.frameGenerationMode);
		settings.frameGenerationForceEnable = ClampToggleUInt(settings.frameGenerationForceEnable);
		settings.streamlineLogLevel = ClampStreamlineLogLevelUInt(settings.streamlineLogLevel);
		settings.sharpnessFSR = ClampFiniteUnitRange(settings.sharpnessFSR, defaults.sharpnessFSR);
		settings.sharpnessDLSS = ClampFiniteUnitRange(settings.sharpnessDLSS, defaults.sharpnessDLSS);
		settings.dlssSharpener = ClampDLSSSharpenerModeUInt(settings.dlssSharpener);
		settings.periphery_taa_center_blend_feather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);
		SanitizeFoveatedSettings(settings);
		settings.periphery_taa_outer_scale = ClampPeripheryTAAOuterScaleForCenter(
			settings.periphery_taa_outer_scale,
			settings.periphery_taa_center_area);
	}

	void ResetVRSpecificUpscalingSettings(Upscaling::Settings& settings)
	{
		settings.renderScaleMode = 0;
		settings.vrFpsStabilizerSync = false;
		settings.perfMode = 0;
		settings.foveatedVendorDispatch = false;
		settings.foveatedCenterArea = 0.6f;
		settings.foveatedCenterHorizontalScale = 1.0f;
		settings.foveatedLeftEyeMaskOffsetX = 0.0f;
		settings.foveatedLeftEyeMaskOffsetY = 0.0f;
		settings.foveatedRightEyeMaskOffsetX = 0.0f;
		settings.foveatedRightEyeMaskOffsetY = 0.0f;
		settings.periphery_taa_center_area = 0.6f;
		settings.foveatedPeripheryMaskVisualization = false;
		settings.periphery_taa_enable = false;
		settings.periphery_taa_outer_scale = 0.70f;
		settings.periphery_taa_center_blend_feather = FoveatedCommon::kCenterFeather;
	}

	void StripVRSpecificUpscalingSettings(json& o_json)
	{
		o_json.erase("renderScaleMode");
		o_json.erase("vrFpsStabilizerSync");
		o_json.erase("perfMode");
		o_json.erase("foveatedVendorDispatch");
		o_json.erase("foveatedCenterArea");
		o_json.erase("foveatedCenterHorizontalScale");
		o_json.erase("foveatedLeftEyeMaskOffsetX");
		o_json.erase("foveatedLeftEyeMaskOffsetY");
		o_json.erase("foveatedRightEyeMaskOffsetX");
		o_json.erase("foveatedRightEyeMaskOffsetY");
		o_json.erase("periphery_taa_center_area");
		o_json.erase("foveatedPeripheryMaskVisualization");
		o_json.erase("periphery_taa_enable");
		o_json.erase("periphery_taa_outer_scale");
		o_json.erase("periphery_taa_center_blend_feather");
	}

	bool SupportsFoveatedVendorDispatch(Upscaling::UpscaleMethod a_upscaleMethod)
	{
		if (!globals::game::isVR)
			return false;

		switch (a_upscaleMethod) {
		case Upscaling::UpscaleMethod::kDLSS:
			return true;
		case Upscaling::UpscaleMethod::kFSR:
			return true;
		default:
			return false;
		}
	}

	bool IsFoveatedVendorDispatchRequested(const Upscaling::Settings& settings, Upscaling::UpscaleMethod a_upscaleMethod)
	{
		return SupportsFoveatedVendorDispatch(a_upscaleMethod) && settings.foveatedVendorDispatch;
	}

	bool ShouldUnlockDynamicResolutionForUpscaling(Upscaling::UpscaleMethod a_upscaleMethod, const float2& a_resolutionScale)
	{
		return IsVendorUpscalingMethod(a_upscaleMethod) &&
		       (a_resolutionScale.x < kDynamicResolutionUpscalingScaleThreshold ||
				   a_resolutionScale.y < kDynamicResolutionUpscalingScaleThreshold);
	}

	bool SetDynamicResolutionEnabledForUpscaling(bool a_enabled, bool a_forceDisabled = false)
	{
		if (!globals::game::isVR)
			return false;

		static bool initialized = false;
		static bool originalEnabled = false;
		static bool changedByUpscaling = false;

		const static auto address = REL::RelocationID{ 508794, 380760 }.address();
		auto* enabled = reinterpret_cast<bool*>(address);
		if (!initialized) {
			originalEnabled = *enabled;
			initialized = true;
		}

		const bool targetEnabled = a_enabled ? true : (a_forceDisabled ? false : (changedByUpscaling ? originalEnabled : *enabled));
		bool changed = false;
		if (*enabled != targetEnabled) {
			*enabled = targetEnabled;
			changed = true;
		}

		const bool targetChangedByUpscaling = a_enabled || a_forceDisabled;
		if (changedByUpscaling != targetChangedByUpscaling)
			changed = true;
		changedByUpscaling = targetChangedByUpscaling;
		return changed;
	}

	void DisableAutoDynamicResolutionSetting()
	{
		if (!globals::game::isVR)
			return;

		constexpr const char* settingNames[] = {
			"bEnableAutoDynamicResolution:Display",
			"bEnableAutoDynamicResolution"
		};

		bool found = false;
		bool changed = false;
		auto disableInCollection = [&](auto* a_collection, const char* a_collectionName) {
			if (!a_collection)
				return;

			for (const auto* settingName : settingNames) {
				auto* setting = a_collection->GetSetting(settingName);
				if (!setting)
					continue;

				found = true;
				if (setting->data.b) {
					setting->data.b = false;
					changed = true;
					if (a_collection->WriteSetting(setting)) {
						logger::info("[Upscaling] Disabled {} in {}.", settingName, a_collectionName);
					} else {
						logger::warn("[Upscaling] Disabled {} in memory, but failed to write {}.", settingName, a_collectionName);
					}
				}
				return;
			}
		};

		disableInCollection(globals::game::iniSettingCollection, "Skyrim.ini");
		disableInCollection(globals::game::iniPrefSettingCollection, "SkyrimPrefs.ini");

		for (const auto* settingName : settingNames) {
			auto* setting = RE::GetINISetting(settingName);
			if (!setting)
				continue;

			found = true;
			if (setting->data.b) {
				setting->data.b = false;
				changed = true;
				logger::info("[Upscaling] Disabled {} in runtime settings.", settingName);
			}
			break;
		}

		if (!found) {
			logger::debug("[Upscaling] bEnableAutoDynamicResolution setting was not found.");
		} else if (!changed) {
			logger::debug("[Upscaling] bEnableAutoDynamicResolution was already disabled.");
		}
	}

	bool IsVRRuntimeActive()
	{
		return globals::game::isVR;
	}

	bool IsLoadingMenuContextActive()
	{
		auto state = globals::state;
		auto ui = globals::game::ui;
		return g_vrLoadingMenuOpenFromEvent.load(std::memory_order_relaxed) ||
		       (state && state->isLoadingMenuOpen) ||
		       (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
	}

	bool IsLoadingTransitionTailActive(const State* a_state)
	{
		if (!a_state)
			return false;

		const uint32_t endFrame = g_vrLoadingTransitionTailEndFrame.load(std::memory_order_acquire);
		return endFrame != 0 && a_state->frameCount < endFrame;
	}

	bool IsCellTransitionTailActive(const State* a_state, uint32_t a_tailFrames = kVRCellTransitionTailFrames)
	{
		if (!a_state || a_tailFrames == 0)
			return false;

		const uint32_t closeFrame = g_vrLoadingTransitionCloseFrame.load(std::memory_order_acquire);
		return closeFrame != 0 &&
		       a_state->frameCount >= closeFrame &&
		       a_state->frameCount - closeFrame < a_tailFrames;
	}

	bool IsCellTransitionContextActive(const State* a_state)
	{
		return IsLoadingMenuContextActive() ||
		       IsCellTransitionTailActive(a_state);
	}

	bool IsVRRenderScaleCurrentOrTargetRelevant(const Upscaling& a_upscaling)
	{
		const uint32_t pendingQualityMode = a_upscaling.pendingVRUpscalingQualityMode.load(std::memory_order_acquire);
		const uint32_t targetQualityMode = pendingQualityMode != Upscaling::kPendingVRUpscalingSettingUnset ?
		                                       pendingQualityMode :
		                                       a_upscaling.settings.qualityMode;
		const uint32_t pendingRenderScaleMode = a_upscaling.pendingVRRenderScaleMode.load(std::memory_order_acquire);
		const bool targetRenderScaleMode = pendingRenderScaleMode != Upscaling::kPendingVRUpscalingSettingUnset ?
		                                       pendingRenderScaleMode != 0 :
		                                       ClampToggleUInt(a_upscaling.settings.renderScaleMode) != 0;
		const uint32_t pendingPerfMode = a_upscaling.pendingVRPerfMode.load(std::memory_order_acquire);
		const bool targetPerfMode = pendingPerfMode != Upscaling::kPendingVRUpscalingSettingUnset ?
		                                pendingPerfMode != 0 :
		                                ClampToggleUInt(a_upscaling.settings.perfMode) != 0;
		const bool currentRenderScaleRelevant =
			a_upscaling.IsVRRenderScaleModeLatched() ||
			a_upscaling.perfMode.HasRestartRequiredChange() ||
			(IsRenderScaleQualityMode(a_upscaling.settings.qualityMode) &&
				(ClampToggleUInt(a_upscaling.settings.renderScaleMode) != 0 ||
					ClampToggleUInt(a_upscaling.settings.perfMode) != 0));
		const bool targetRenderScaleRelevant =
			IsRenderScaleQualityMode(targetQualityMode) &&
			(targetRenderScaleMode || targetPerfMode);
		return currentRenderScaleRelevant || targetRenderScaleRelevant;
	}

	bool IsVRRenderScaleTransitionSafetyRelevant(const Upscaling& a_upscaling, Upscaling::UpscaleMethod a_upscaleMethod)
	{
		if (!globals::game::isVR)
			return false;

		const bool renderScaleLatched = a_upscaling.IsVRRenderScaleModeLatched();
		const bool requestedMethodEligible = IsRenderScaleMethodEligible(a_upscaleMethod);
		const bool runtimeMethodEligible =
			renderScaleLatched &&
			IsRenderScaleMethodEligible(a_upscaling.GetRuntimeUpscaleMethod());
		if (!requestedMethodEligible && !runtimeMethodEligible)
			return false;

		return IsVRRenderScaleCurrentOrTargetRelevant(a_upscaling);
	}

	bool IsVRRenderScaleTransitionSafetyRelevant(const Upscaling& a_upscaling)
	{
		return IsVRRenderScaleTransitionSafetyRelevant(a_upscaling, a_upscaling.GetConfiguredUpscaleMethodForTransition());
	}

	bool ShouldIncludeInactiveVRVendorReset(const Upscaling& a_upscaling, Upscaling::UpscaleMethod a_upscaleMethod)
	{
		if (!IsVRRenderScaleTransitionSafetyRelevant(a_upscaling, a_upscaleMethod))
			return false;

		return a_upscaling.HasPendingVRRenderScaleTransition() ||
		       a_upscaling.pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
		       a_upscaling.perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) ||
		       a_upscaling.postLoadRuntimeResetPending.load(std::memory_order_acquire) ||
		       a_upscaling.IsVRRenderScaleModeLatched() ||
		       a_upscaling.perfMode.HasRestartRequiredChange();
	}

	uint32_t GetVRLoadingTransitionCloseElapsedFrames(const State* a_state)
	{
		if (!globals::game::isVR || !a_state)
			return std::numeric_limits<uint32_t>::max();

		const uint32_t closeFrame = g_vrLoadingTransitionCloseFrame.load(std::memory_order_acquire);
		if (closeFrame == 0 || a_state->frameCount < closeFrame)
			return std::numeric_limits<uint32_t>::max();

		return a_state->frameCount - closeFrame;
	}

	bool IsSaveLoadTransitionContextActive(const State* a_state)
	{
		if (!a_state)
			return false;

		return a_state->pendingPostLoadRuntimeReset || a_state->IsSaveLoadSafeModeActive();
	}

	bool IsSaveLoadTransitionContextActive()
	{
		return IsSaveLoadTransitionContextActive(globals::state);
	}

	bool IsVRRenderScalePostLoadResetRelevant(const Upscaling& a_upscaling, Upscaling::UpscaleMethod a_upscaleMethod)
	{
		return a_upscaling.postLoadRuntimeResetPending.load(std::memory_order_acquire) &&
		       IsVRRenderScaleTransitionSafetyRelevant(a_upscaling, a_upscaleMethod);
	}

	bool IsVRRenderScalePostLoadResetRelevant(const Upscaling& a_upscaling)
	{
		return IsVRRenderScalePostLoadResetRelevant(a_upscaling, a_upscaling.GetConfiguredUpscaleMethodForTransition());
	}

	bool IsUpscalingLoadTransitionContextActive(const Upscaling& a_upscaling, const State* a_state)
	{
		return IsVRRenderScalePostLoadResetRelevant(a_upscaling) ||
		       IsSaveLoadTransitionContextActive(a_state);
	}

	bool IsUpscalingLoadTransitionContextActive(const Upscaling& a_upscaling)
	{
		return IsUpscalingLoadTransitionContextActive(a_upscaling, globals::state);
	}

	bool IsVRTransitionPresentationProtectionActive(const Upscaling& a_upscaling, const State* a_state)
	{
		return IsVRRenderScaleTransitionSafetyRelevant(a_upscaling) &&
		       (IsVRRenderScalePostLoadResetRelevant(a_upscaling) ||
				   IsSaveLoadTransitionContextActive(a_state));
	}

	bool IsVRLoadingPresentationTailActive(const State* a_state)
	{
		return IsSaveLoadTransitionContextActive(a_state) ?
		           IsLoadingTransitionTailActive(a_state) :
		           IsCellTransitionTailActive(a_state, kVRCellTransitionPresentationTailFrames);
	}

	bool IsVRLoadingPresentationContextActive(const State* a_state)
	{
		return IsLoadingMenuContextActive() ||
		       IsVRLoadingPresentationTailActive(a_state);
	}

	bool IsVRRenderScaleRelatchSubmitProtectionTailActive(const Upscaling& a_upscaling, const State* a_state)
	{
		if (!globals::game::isVR || !a_state)
			return false;

		if (!IsLoadingTransitionTailActive(a_state))
			return false;

		if (!IsVRRenderScaleTransitionSafetyRelevant(a_upscaling))
			return false;

		return a_upscaling.pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
		       a_upscaling.perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire);
	}

	bool IsVRLoadingSubmitProtectionContextActive(const Upscaling& a_upscaling, const State* a_state)
	{
		if (!globals::game::isVR || !a_state)
			return false;

		return (IsVRTransitionPresentationProtectionActive(a_upscaling, a_state) &&
				   IsVRLoadingPresentationContextActive(a_state)) ||
		       IsVRRenderScaleRelatchSubmitProtectionTailActive(a_upscaling, a_state);
	}

	bool HasCompletedVRWorldFrameAfterLatestLoad(const State* a_state)
	{
		if (!a_state)
			return false;

		const uint32_t lastCompletedWorldFrame = a_state->lastCompletedWorldRenderFrame;
		if (lastCompletedWorldFrame == std::numeric_limits<uint32_t>::max())
			return false;

		const uint32_t closeFrame = g_vrLoadingTransitionCloseFrame.load(std::memory_order_acquire);
		if (closeFrame != 0 && lastCompletedWorldFrame <= closeFrame)
			return false;

		return true;
	}

	bool IsVRFpsStabilizerLoadSyncReady(const State* a_state)
	{
		return a_state &&
		       !IsMainOrLoadingMenuContextActive() &&
		       HasCompletedVRWorldFrameAfterLatestLoad(a_state);
	}

	uint32_t GetVRFpsStabilizerCurrentSyncFrame(const State* a_state)
	{
		if (!IsVRFpsStabilizerLoadSyncReady(a_state))
			return 0;

		const uint32_t closeFrame = g_vrLoadingTransitionCloseFrame.load(std::memory_order_acquire);
		if (closeFrame != 0)
			return closeFrame;

		return 1u;
	}

	void MarkVRFpsStabilizerSyncResolved(Upscaling& a_upscaling, uint32_t a_syncFrame)
	{
		if (a_syncFrame == 0)
			return;

		a_upscaling.vrFpsStabilizerSyncResolvedFrame.store(a_syncFrame, std::memory_order_release);
	}

	void QueueVRFpsStabilizerSyncForCurrentLoadIfNeeded(Upscaling& a_upscaling)
	{
		if (!globals::game::isVR || !a_upscaling.settings.vrFpsStabilizerSync)
			return;

		if (a_upscaling.pendingVRFpsStabilizerSyncFrame.load(std::memory_order_acquire) != 0)
			return;

		const auto* state = globals::state;
		if (!IsVRFpsStabilizerLoadSyncReady(state))
			return;

		const uint32_t syncFrame = GetVRFpsStabilizerCurrentSyncFrame(state);
		if (syncFrame == 0 ||
			a_upscaling.vrFpsStabilizerSyncResolvedFrame.load(std::memory_order_acquire) == syncFrame) {
			return;
		}

		a_upscaling.QueueVRFpsStabilizerLoadSync(syncFrame);
	}

	bool HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(const Upscaling& a_upscaling)
	{
		if (!globals::game::isVR || !a_upscaling.settings.vrFpsStabilizerSync)
			return false;

		if (a_upscaling.pendingVRFpsStabilizerSyncFrame.load(std::memory_order_acquire) != 0)
			return true;

		const auto* state = globals::state;
		if (!IsVRFpsStabilizerLoadSyncReady(state))
			return false;

		const uint32_t syncFrame = GetVRFpsStabilizerCurrentSyncFrame(state);
		return syncFrame != 0 &&
		       a_upscaling.vrFpsStabilizerSyncResolvedFrame.load(std::memory_order_acquire) != syncFrame;
	}

	bool CanActivateVRRenderScaleRuntime(const Upscaling& a_upscaling)
	{
		if (!a_upscaling.loaded)
			return false;

		if (!globals::game::isVR)
			return true;

		if (IsVRMenuScenePresentationBlockActive())
			return false;

		if (HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(a_upscaling))
			return false;

		const auto* state = globals::state;
		if (!HasCompletedVRWorldFrameAfterLatestLoad(state))
			return false;

		if (state->pendingPostLoadRuntimeReset ||
			a_upscaling.postLoadRuntimeResetPending.load(std::memory_order_acquire)) {
			return false;
		}

		return !IsVRLoadingPresentationContextActive(state);
	}

	bool ShouldBlockForPendingExplicitVRUpscalingWork(const Upscaling& a_upscaling)
	{
		return HasPendingExplicitVRUpscalingWork(a_upscaling) &&
		       !HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(a_upscaling);
	}

	bool ShouldQueueDeferredVRRenderScaleActivation(const Upscaling& a_upscaling)
	{
		if (!globals::game::isVR)
			return false;

		if (!CanActivateVRRenderScaleRuntime(a_upscaling))
			return false;

		if (a_upscaling.IsOpenCompositeUpscalingBlocked())
			return false;

		if (a_upscaling.pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
			a_upscaling.perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) ||
			a_upscaling.HasPendingVRUpscalingTransition()) {
			return false;
		}

		const auto& boot = a_upscaling.perfMode.GetBootSnapshot();
		if (boot.valid && boot.active)
			return false;

		if (!a_upscaling.GetPerfModeRequested() || !a_upscaling.perfMode.HasKnownHMDSize())
			return false;

		return a_upscaling.perfMode.IsEligible(
			a_upscaling.settings,
			a_upscaling.GetConfiguredUpscaleMethodForTransition());
	}

	bool QueueDeferredVRRenderScaleActivationIfReady(Upscaling& a_upscaling)
	{
		if (a_upscaling.pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire))
			return false;

		if (!ShouldQueueDeferredVRRenderScaleActivation(a_upscaling))
			return false;

		a_upscaling.RequestPerfModeRenderTargetRecreate("deferred in-game render-scale activation", Upscaling::VRUpscalingTransitionOrigin::PostLoadSync);
		return true;
	}

	bool ShouldDeferVRTransitionMaskRepair(const Upscaling& a_upscaling, const State* a_state)
	{
		(void)a_upscaling;
		return IsSaveLoadTransitionContextActive(a_state);
	}

	bool ShouldDeferVRProjectedMaskRepair(const Upscaling& a_upscaling, const State* a_state)
	{
		(void)a_upscaling;
		return IsSaveLoadTransitionContextActive(a_state);
	}

	bool ShouldBypassVRFoveatedVendorDispatchForTransition(const Upscaling& a_upscaling, const State* a_state)
	{
		return IsSaveLoadTransitionContextActive(a_state) ||
		       IsVRLoadingSubmitProtectionContextActive(a_upscaling, a_state);
	}

	bool HasPendingVRVendorRuntimeReset(const Upscaling& a_upscaling, Upscaling::UpscaleMethod a_upscaleMethod)
	{
		const bool includeInactiveVendorReset = ShouldIncludeInactiveVRVendorReset(a_upscaling, a_upscaleMethod);
		switch (a_upscaleMethod) {
		case Upscaling::UpscaleMethod::kDLSS:
			return a_upscaling.pendingDLSSReset.load(std::memory_order_acquire) ||
			       (includeInactiveVendorReset && a_upscaling.pendingFSRReset.load(std::memory_order_acquire));
		case Upscaling::UpscaleMethod::kFSR:
			return a_upscaling.pendingFSRReset.load(std::memory_order_acquire) ||
			       (includeInactiveVendorReset && a_upscaling.pendingDLSSReset.load(std::memory_order_acquire));
		default:
			return false;
		}
	}

	bool ShouldDeferHMDClearMask()
	{
		return ShouldDeferVRTransitionMaskRepair(globals::features::upscaling, globals::state);
	}

	bool IsCommunityShadersMenuOpen();

	uint32_t ElapsedFrames(uint32_t a_startFrame, uint32_t a_frame)
	{
		return a_startFrame != 0 && a_frame >= a_startFrame ? a_frame - a_startFrame : 0u;
	}

	uint32_t GetFrameScopedUpscalingWorkFrame()
	{
		const auto* state = globals::state;
		return state ? std::max(state->frameCount, 1u) : std::numeric_limits<uint32_t>::max();
	}

	uint64_t HashResourceCheckValue(uint64_t a_hash, uint64_t a_value)
	{
		return a_hash ^ (a_value + 0x9E3779B97F4A7C15ull + (a_hash << 6) + (a_hash >> 2));
	}

	uint64_t QuantizeResourceCheckFloat(float a_value)
	{
		return std::isfinite(a_value) ? static_cast<uint64_t>(static_cast<int64_t>(std::llround(a_value * 100000.0f))) : 0ull;
	}

	uint64_t BuildUpscalingResourceMutationSettingsKey(const Upscaling::Settings& a_settings)
	{
		uint64_t hash = 0xCBF29CE484222325ull;
		auto add = [&](uint64_t a_value) {
			hash = HashResourceCheckValue(hash, a_value);
		};
		auto addFloat = [&](float a_value) {
			add(QuantizeResourceCheckFloat(a_value));
		};

		add(std::min<uint>(a_settings.upscaleMethod, static_cast<uint>(Upscaling::UpscaleMethod::kDLSS)));
		add(std::min<uint>(a_settings.upscaleMethodNoDLSS, static_cast<uint>(Upscaling::UpscaleMethod::kFSR)));
		add(ClampQualityModeUInt(a_settings.qualityMode));
		add(std::min<uint>(a_settings.dlssPreset, Upscaling::kDLSSPresetMaxIndex));
		add(ClampToggleUInt(a_settings.renderScaleMode));
		add(ClampToggleUInt(a_settings.perfMode));
		add(ClampToggleUInt(a_settings.frameGenerationMode));
		add(a_settings.fsr4RuntimeEnable);
		add(a_settings.foveatedVendorDispatch);
		add(a_settings.foveatedPeripheryMaskVisualization);
		add(a_settings.periphery_taa_enable);
		const float clampedPeripheryTAACenterArea = ClampFoveatedCenterScale(a_settings.periphery_taa_center_area);
		addFloat(ClampFoveatedCenterScale(a_settings.foveatedCenterArea));
		addFloat(ClampFoveatedCenterHorizontalScale(a_settings.foveatedCenterHorizontalScale));
		addFloat(ClampFoveatedMaskOffsetAdjustment(a_settings.foveatedLeftEyeMaskOffsetX));
		addFloat(ClampFoveatedMaskOffsetAdjustment(a_settings.foveatedLeftEyeMaskOffsetY));
		addFloat(ClampFoveatedMaskOffsetAdjustment(a_settings.foveatedRightEyeMaskOffsetX));
		addFloat(ClampFoveatedMaskOffsetAdjustment(a_settings.foveatedRightEyeMaskOffsetY));
		addFloat(clampedPeripheryTAACenterArea);
		addFloat(ClampPeripheryTAAOuterScaleForCenter(a_settings.periphery_taa_outer_scale, clampedPeripheryTAACenterArea));
		addFloat(ClampPeripheryTAACenterBlendFeather(a_settings.periphery_taa_center_blend_feather));
		return hash;
	}

	uint64_t BuildResourceCheckStableKey(const Upscaling& a_upscaling, Upscaling::UpscaleMethod a_upscaleMethod)
	{
		const auto& settings = a_upscaling.settings;
		uint64_t hash = 0xCBF29CE484222325ull;
		auto add = [&](uint64_t a_value) {
			hash = HashResourceCheckValue(hash, a_value);
		};
		auto addFloat = [&](float a_value) {
			add(QuantizeResourceCheckFloat(a_value));
		};

		add(static_cast<uint64_t>(a_upscaleMethod));
		add(a_upscaling.GetRuntimeQualityMode());
		if (a_upscaleMethod == Upscaling::UpscaleMethod::kDLSS)
			add(std::min<uint>(settings.dlssPreset, Upscaling::kDLSSPresetMaxIndex));

		const uint32_t trackedRenderScaleMode = !globals::game::isVR ?
		                                            (a_upscaling.IsRenderScaleModeRequested() ? 1u : 0u) :
		                                            (IsRenderScaleMethodEligible(a_upscaleMethod) ? ClampToggleUInt(settings.renderScaleMode) : 0u);
		add(trackedRenderScaleMode);
		add(ClampToggleUInt(settings.perfMode));
		add(settings.frameGenerationMode && a_upscaling.d3d12SwapChainActive);
		if (a_upscaleMethod == Upscaling::UpscaleMethod::kFSR) {
			const bool runtimePathActive = a_upscaling.IsFSRRuntimePathActive(a_upscaleMethod);
			const bool runtimeFsr4Configured =
				settings.fsr4RuntimeEnable &&
				Upscaling::fidelityFX.IsRuntimeFsr4Available();
			const bool runtimeFsr4PathActive = a_upscaling.IsFSRRuntimeFsr4PathActive(a_upscaleMethod);

			add(runtimePathActive);
			add(runtimeFsr4Configured);
			add(runtimeFsr4PathActive);
		}

		const bool foveatedDispatchActive = a_upscaling.IsFoveatedVendorDispatchEnabled(a_upscaleMethod);
		add(foveatedDispatchActive);
		if (foveatedDispatchActive) {
			const bool peripheryTAAEnabled = a_upscaling.IsPeripheryTAAEnabled(a_upscaleMethod);
			add(peripheryTAAEnabled);
			add(a_upscaling.IsPeripheryTAAPathActive(a_upscaleMethod));
			addFloat(ClampFoveatedCenterHorizontalScale(settings.foveatedCenterHorizontalScale));
			addFloat(ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetX));
			addFloat(ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetY));
			addFloat(ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetX));
			addFloat(ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetY));
			if (peripheryTAAEnabled) {
				const float clampedPeripheryTAACenterArea = ClampFoveatedCenterScale(settings.periphery_taa_center_area);
				addFloat(clampedPeripheryTAACenterArea);
				addFloat(ClampPeripheryTAAOuterScaleForCenter(settings.periphery_taa_outer_scale, clampedPeripheryTAACenterArea));
				addFloat(ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather));
			} else {
				addFloat(ClampFoveatedCenterScale(settings.foveatedCenterArea));
			}
		}
		return hash;
	}

	bool ShouldDeferVRRenderScaleRelatchForPostLoadSettle(const Upscaling& a_upscaling, const State* a_state)
	{
		if (!a_upscaling.pendingPerfModeRenderTargetRecreatePostLoadSettle.load(std::memory_order_acquire))
			return false;
		if (!globals::game::isVR || !a_state)
			return false;

		if (IsVRMenuScenePresentationBlockActive() || IsSaveLoadTransitionContextActive(a_state) || IsCommunityShadersMenuOpen())
			return true;

		return !HasCompletedVRWorldFrameAfterLatestLoad(a_state);
	}

	const char* BoolText(bool a_value)
	{
		return a_value ? "yes" : "no";
	}

	struct VRBoundsInfo
	{
		float uMin = -1.0f;
		float vMin = -1.0f;
		float uMax = -1.0f;
		float vMax = -1.0f;
	};

	VRBoundsInfo BuildVRBoundsInfo(const vr::VRTextureBounds_t* a_bounds)
	{
		VRBoundsInfo info{};
		if (!a_bounds)
			return info;

		info.uMin = a_bounds->uMin;
		info.vMin = a_bounds->vMin;
		info.uMax = a_bounds->uMax;
		info.vMax = a_bounds->vMax;
		return info;
	}

	bool IsOutOfMemoryResult(HRESULT a_result)
	{
		return a_result == E_OUTOFMEMORY ||
		       a_result == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
	}

	bool IsOutOfMemoryException(const std::exception& a_exception)
	{
		const auto* comException = dynamic_cast<const DX::com_exception*>(&a_exception);
		return comException && IsOutOfMemoryResult(comException->Error());
	}

	void QueueVendorRuntimeResetAfterLoadingMenu(Upscaling& a_upscaling)
	{
		if (!IsVRRenderScalePostLoadResetRelevant(a_upscaling))
			return;

		const auto upscaleMethod = a_upscaling.GetRuntimeUpscaleMethod();
		const bool preserveInactiveVendorReset = ShouldIncludeInactiveVRVendorReset(a_upscaling, upscaleMethod);
		if (upscaleMethod == Upscaling::UpscaleMethod::kDLSS) {
			a_upscaling.pendingDLSSHistoryReset.store(true, std::memory_order_relaxed);
			a_upscaling.pendingDLSSReset.store(true, std::memory_order_relaxed);
			if (!preserveInactiveVendorReset)
				a_upscaling.pendingFSRReset.store(false, std::memory_order_relaxed);
		} else if (upscaleMethod == Upscaling::UpscaleMethod::kFSR) {
			a_upscaling.pendingFSRReset.store(true, std::memory_order_relaxed);
			a_upscaling.pendingDLSSHistoryReset.store(false, std::memory_order_relaxed);
			if (!preserveInactiveVendorReset)
				a_upscaling.pendingDLSSReset.store(false, std::memory_order_relaxed);
		} else {
			a_upscaling.pendingDLSSReset.store(false, std::memory_order_relaxed);
			a_upscaling.pendingDLSSHistoryReset.store(false, std::memory_order_relaxed);
			a_upscaling.pendingFSRReset.store(false, std::memory_order_relaxed);
		}
	}

	bool IsMainMenuContextActive()
	{
		auto state = globals::state;
		auto ui = globals::game::ui;
		return (state && state->isMainMenuOpen) ||
		       (ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME));
	}

	bool IsMainOrLoadingMenuContextActive()
	{
		return IsMainMenuContextActive() || IsLoadingMenuContextActive();
	}

	bool IsRaceSexMenuContextActive(RE::UI* a_ui)
	{
		return a_ui && a_ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
	}

	bool IsSkyrimMenuPresentationMenuName(std::string_view a_menuName)
	{
		for (const auto menuName : kSkyrimPresentationMenuNames) {
			if (a_menuName == menuName)
				return true;
		}

		return false;
	}

	bool IsVRMenuPresentationTailMenuName(std::string_view a_menuName)
	{
		return IsSkyrimMenuPresentationMenuName(a_menuName) || a_menuName == RE::MapMenu::MENU_NAME;
	}

	bool IsFrameTailActive(const State* a_state, const std::atomic_uint32_t& a_endFrame)
	{
		if (!a_state)
			return false;

		const uint32_t endFrame = a_endFrame.load(std::memory_order_acquire);
		return endFrame != 0 && a_state->frameCount < endFrame;
	}

	void ExtendFrameTail(std::atomic_uint32_t& a_endFrame, uint32_t a_tailFrames)
	{
		if (!globals::state)
			return;

		const uint32_t currentFrame = std::max(globals::state->frameCount, 1u);
		const uint32_t tailEndFrame = currentFrame + std::max(a_tailFrames, 1u);
		uint32_t previousEndFrame = a_endFrame.load(std::memory_order_acquire);
		while (previousEndFrame < tailEndFrame &&
			   !a_endFrame.compare_exchange_weak(
				   previousEndFrame,
				   tailEndFrame,
				   std::memory_order_acq_rel,
				   std::memory_order_acquire)) {
		}
	}

	bool IsVRMenuPresentationTailActive(const State* a_state)
	{
		return globals::game::isVR && IsFrameTailActive(a_state, g_vrMenuPresentationTailEndFrame);
	}

	bool IsVRObservedProjectedMenuTailActive(const State* a_state)
	{
		return globals::game::isVR && IsFrameTailActive(a_state, g_vrObservedProjectedMenuTailEndFrame);
	}

	bool IsVRMenuBridgeTraceTailActive(const State* a_state)
	{
		return globals::game::isVR && IsFrameTailActive(a_state, g_vrMenuBridgeTraceTailEndFrame);
	}

	bool IsVRSubmitStageUnderwaterMaskRefreshRelevant(const State* a_state)
	{
		if (!globals::game::isVR || !a_state)
			return false;

		const uint32_t currentFrame = std::max(a_state->frameCount, 1u);
		uint32_t tailEndFrame = g_vrSubmitStageUnderwaterMaskTailEndFrame.load(std::memory_order_acquire);
		if (tailEndFrame != 0 && currentFrame + kVRSubmitStageUnderwaterMaskTailFrames + 1u < tailEndFrame) {
			g_vrSubmitStageUnderwaterMaskTailEndFrame.store(0, std::memory_order_release);
			tailEndFrame = 0;
		}

		auto* waterSystem = globals::game::waterSystem;
		const bool waterContextActive =
			waterSystem &&
			(waterSystem->playerUnderwater || waterSystem->partiallyUnderwater);
		if (waterContextActive) {
			// ExtendFrameTail includes the current frame; keep the configured tail after water clears.
			ExtendFrameTail(g_vrSubmitStageUnderwaterMaskTailEndFrame, kVRSubmitStageUnderwaterMaskTailFrames + 1u);
			return true;
		}

		return tailEndFrame != 0 && currentFrame < tailEndFrame;
	}

	bool IsVRMenuPresentationContextActive()
	{
		return globals::game::isVR &&
		       (IsKnownGameMenuContextActive() || IsVRMenuPresentationTailActive(globals::state));
	}

	bool IsVRMenuScenePresentationBlockActive()
	{
		auto ui = globals::game::ui;
		return globals::game::isVR &&
		       (IsMainOrLoadingMenuContextActive() || IsRaceSexMenuContextActive(ui));
	}

	bool IsVRRenderScaleMenuPreparationContextActive(const State* a_state)
	{
		if (!globals::game::isVR)
			return false;

		if (IsVRMenuScenePresentationBlockActive())
			return true;

		return IsVRObservedProjectedMenuTailActive(a_state);
	}

	void ExtendVRMenuPresentationTail(uint32_t a_tailFrames = kVRMenuPresentationTailFrames)
	{
		if (!globals::game::isVR || !globals::state)
			return;

		ExtendFrameTail(g_vrMenuPresentationTailEndFrame, a_tailFrames);
	}

	void ExtendVRObservedProjectedMenuTail(uint32_t a_tailFrames = kVRObservedMenuPresentationTailFrames)
	{
		if (!globals::game::isVR || !globals::state)
			return;

		ExtendFrameTail(g_vrObservedProjectedMenuTailEndFrame, a_tailFrames);
	}

	void ResetVRMenuBridgeTraceState()
	{
		g_vrMenuBridgeTraceTailEndFrame.store(0, std::memory_order_release);
		g_vrMenuBridgeTraceCachedState.store(0, std::memory_order_relaxed);
	}

	void ResetVRMenuPresentationTrackingState()
	{
		g_vrMenuPresentationTailEndFrame.store(0, std::memory_order_release);
		g_vrObservedProjectedMenuTailEndFrame.store(0, std::memory_order_release);
		ResetVRMenuBridgeTraceState();
	}

	void ExtendVRMenuBridgeTraceTail(uint32_t a_tailFrames = kVRObservedMenuPresentationTailFrames)
	{
		if (!globals::game::isVR || !globals::state)
			return;

		ExtendFrameTail(g_vrMenuBridgeTraceTailEndFrame, a_tailFrames);
	}

	bool IsSkyrimMenuPresentationContextActive(RE::UI* a_ui)
	{
		if (!a_ui)
			return false;

		for (const auto menuName : kSkyrimPresentationMenuNames) {
			if (a_ui->IsMenuOpen(menuName))
				return true;
		}

		return false;
	}

	bool IsKnownGameMenuContextActive()
	{
		auto state = globals::state;
		auto ui = globals::game::ui;
		return (state && state->isMapMenuOpen) ||
		       IsMainMenuContextActive() ||
		       IsLoadingMenuContextActive() ||
		       IsSkyrimMenuPresentationContextActive(ui);
	}

	bool IsCommunityShadersMenuOpen()
	{
		auto* menu = globals::menu;
		return menu && menu->initialized && menu->ShouldSwallowInput();
	}

	bool IsVRSceneFeatureMenuPauseContextActive()
	{
		return globals::game::isVR &&
		       (IsVRMenuPresentationContextActive() || IsCommunityShadersMenuOpen());
	}

	bool IsExplicitVRMenuPresentationContextActive()
	{
		return globals::game::isVR && IsKnownGameMenuContextActive();
	}

	uint64_t EncodeVRMenuBridgeTraceState(uint32_t a_frame, bool a_active)
	{
		return (static_cast<uint64_t>(a_frame) << 1) | (a_active ? 1ull : 0ull);
	}

	bool IsVRMenuBridgeTraceContextActive(const State* a_state)
	{
		if (!globals::game::isVR || !a_state)
			return false;

		auto& upscaling = globals::features::upscaling;
		if (!upscaling.IsVRRenderScaleModeActive() || !upscaling.IsPresentationUpscalingActive())
			return false;

		return IsExplicitVRMenuPresentationContextActive() ||
		       IsVRMenuBridgeTraceTailActive(a_state) ||
		       IsVRObservedProjectedMenuTailActive(a_state);
	}

	void RefreshVRMenuBridgeTraceState(const State* a_state)
	{
		if (!globals::game::isVR || !a_state) {
			g_vrMenuBridgeTraceCachedState.store(0, std::memory_order_relaxed);
			return;
		}

		const uint32_t currentFrame = std::max(a_state->frameCount, 1u);
		g_vrMenuBridgeTraceCachedState.store(
			EncodeVRMenuBridgeTraceState(currentFrame, IsVRMenuBridgeTraceContextActive(a_state)),
			std::memory_order_relaxed);
	}

	bool IsGameMenuContextActive()
	{
		return IsKnownGameMenuContextActive();
	}

	bool TryGetTexture2DDesc(ID3D11Resource* resource, D3D11_TEXTURE2D_DESC& outDesc)
	{
		if (!resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
			return false;

		texture->GetDesc(&outDesc);
		return true;
	}

	DXGI_FORMAT GetRenderTargetViewFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default:
			return format;
		}
	}

	bool SupportsRenderTargetView(ID3D11Device* device, DXGI_FORMAT format)
	{
		if (!device || format == DXGI_FORMAT_UNKNOWN)
			return false;

		UINT support = 0;
		if (FAILED(device->CheckFormatSupport(format, &support)))
			return false;

		return (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
	}

	struct RuntimeResolutionPlanLogKey
	{
		Upscaling::UpscaleMethod method = Upscaling::UpscaleMethod::kNONE;
		Upscaling::ResolutionOwner owner = Upscaling::ResolutionOwner::Native;
		uint32_t qualityMode = 0;
		uint32_t displayWidth = 0;
		uint32_t displayHeight = 0;
		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		uint32_t finalWidth = 0;
		uint32_t finalHeight = 0;
		bool operator==(const RuntimeResolutionPlanLogKey&) const = default;
	};

	RuntimeResolutionPlanLogKey MakeRuntimeResolutionPlanLogKey(const Upscaling::RuntimeResolutionPlan& a_plan)
	{
		auto clampLogDimension = [](float a_dimension) {
			if (!std::isfinite(a_dimension) || a_dimension <= 0.0f)
				return 0u;
			return static_cast<uint32_t>(std::floor(a_dimension));
		};

		RuntimeResolutionPlanLogKey key{};
		key.method = a_plan.upscaleMethod;
		key.owner = a_plan.owner;
		key.qualityMode = a_plan.qualityMode;
		key.displayWidth = clampLogDimension(a_plan.trueHMDDisplaySize.x);
		key.displayHeight = clampLogDimension(a_plan.trueHMDDisplaySize.y);
		key.renderWidth = clampLogDimension(a_plan.engineRenderSize.x);
		key.renderHeight = clampLogDimension(a_plan.engineRenderSize.y);
		key.finalWidth = clampLogDimension(a_plan.finalOutputSize.x);
		key.finalHeight = clampLogDimension(a_plan.finalOutputSize.y);
		return key;
	}

	void LogRuntimeResolutionPlanIfChanged(const Upscaling::RuntimeResolutionPlan& a_plan)
	{
		static RuntimeResolutionPlanLogKey previousKey{};
		static bool previousKeyValid = false;
		if (!globals::state || !globals::state->IsDeveloperMode()) {
			previousKeyValid = false;
			return;
		}

		const RuntimeResolutionPlanLogKey key = MakeRuntimeResolutionPlanLogKey(a_plan);
		if (previousKeyValid && key == previousKey)
			return;

		logger::debug(
			"[VRRenderScale] Runtime plan: owner={} method={} quality={} display={}x{} render={}x{} final={}x{}",
			magic_enum::enum_name(key.owner),
			magic_enum::enum_name(key.method),
			key.qualityMode,
			key.displayWidth,
			key.displayHeight,
			key.renderWidth,
			key.renderHeight,
			key.finalWidth,
			key.finalHeight);

		previousKey = key;
		previousKeyValid = true;
	}

	struct VRRenderScalePresentationLogKey
	{
		Upscaling::UpscaleMethod method = Upscaling::UpscaleMethod::kNONE;
		std::string context{};
		uint32_t displayWidth = 0;
		uint32_t displayHeight = 0;
		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		bool operator==(const VRRenderScalePresentationLogKey&) const = default;
	};

	void LogVRRenderScalePresentationPlanIfChanged(
		Upscaling::UpscaleMethod a_method,
		const char* a_context,
		const float2& a_displaySize,
		const float2& a_renderSize)
	{
		static VRRenderScalePresentationLogKey previousKey{};
		static bool previousKeyValid = false;
		if (!globals::state || !globals::state->IsDeveloperMode()) {
			previousKeyValid = false;
			return;
		}

		auto clampLogDimension = [](float a_dimension) {
			if (!std::isfinite(a_dimension) || a_dimension <= 0.0f)
				return 0u;
			return static_cast<uint32_t>(std::floor(a_dimension));
		};

		VRRenderScalePresentationLogKey key{};
		key.method = a_method;
		key.context = a_context ? a_context : "";
		key.displayWidth = clampLogDimension(a_displaySize.x);
		key.displayHeight = clampLogDimension(a_displaySize.y);
		key.renderWidth = clampLogDimension(a_renderSize.x);
		key.renderHeight = clampLogDimension(a_renderSize.y);
		if (previousKeyValid && key == previousKey)
			return;

		logger::debug(
			"[VRRenderScale] Presentation plan: context={} method={} display={}x{} render={}x{}",
			a_context && *a_context ? a_context : "unknown",
			magic_enum::enum_name(key.method),
			key.displayWidth,
			key.displayHeight,
			key.renderWidth,
			key.renderHeight);

		previousKey = key;
		previousKeyValid = true;
	}

	bool IsD3DDeviceRemovedResult(HRESULT a_result)
	{
		return a_result == DXGI_ERROR_DEVICE_REMOVED ||
		       a_result == DXGI_ERROR_DEVICE_RESET ||
		       a_result == DXGI_ERROR_DEVICE_HUNG;
	}

	HRESULT GetD3DDeviceRemovedReason()
	{
		auto* device = globals::d3d::device;
		return device ? device->GetDeviceRemovedReason() : S_OK;
	}

	void LogWarnOnce(bool& logged, std::string_view message)
	{
		if (logged)
			return;

		logger::warn("{}", message);
		logged = true;
	}

	void LogWarnOnce(bool& logged, std::string_view message, const std::exception& e)
	{
		if (logged)
			return;

		logger::warn("{}: {}", message, e.what());
		logged = true;
	}

	template <class... Args>
	void LogWarnOnceFmt(bool& logged, const char* format, Args&&... args)
	{
		if (logged)
			return;

		try {
			logger::warn(fmt::runtime(format), std::forward<Args>(args)...);
		} catch (...) {
			try {
				logger::warn("{}", format);
			} catch (...) {
				// Do not rethrow from logging paths.
			}
		}
		logged = true;
	}

	eastl::unique_ptr<Texture2D> CreateNamedTexture2D(uint32_t width, uint32_t height, DXGI_FORMAT format, bool createSRV, bool createUAV, bool createRTV, const char* name)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = (createSRV ? D3D11_BIND_SHADER_RESOURCE : 0u) | (createUAV ? D3D11_BIND_UNORDERED_ACCESS : 0u) | (createRTV ? D3D11_BIND_RENDER_TARGET : 0u);

		auto texture = eastl::make_unique<Texture2D>(desc);
		if (name) {
			Util::SetResourceName(texture->resource.get(), name);
		}

		if (createSRV) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			texture->CreateSRV(srvDesc);
		}

		if (createUAV) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			texture->CreateUAV(uavDesc);
		}

		if (createRTV) {
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Texture2D.MipSlice = 0;
			texture->CreateRTV(rtvDesc);
		}

		return texture;
	}
}

void Upscaling::BeginVRMenuFinalCompositeFrame(uint32_t a_frame)
{
	if (vrMenuFinalCompositeFrame == a_frame)
		return;

	vrMenuFinalCompositeFrame = a_frame;
	vrMenuFinalCompositeSuppressedTargets = {};
	vrMenuFinalCompositeLayerDrawCount = 0;
}

void Upscaling::ResetVRMenuFinalCompositeLayer()
{
	vrMenuFinalCompositeLayer.reset();
	vrMenuFinalCompositeLayerWidth = 0;
	vrMenuFinalCompositeLayerHeight = 0;
	vrMenuFinalCompositeLayerFormat = DXGI_FORMAT_UNKNOWN;
	vrMenuFinalCompositeLayerClearedFrame = std::numeric_limits<uint32_t>::max();
	vrMenuFinalCompositeLayerDrawCount = 0;
	vrMenuParallelBridgeDrawInProgress = false;
}

bool Upscaling::EnsureVRMenuFinalCompositeLayer(uint32_t a_width, uint32_t a_height, DXGI_FORMAT a_format)
{
	if (!a_width || !a_height || a_format == DXGI_FORMAT_UNKNOWN)
		return false;

	if (vrMenuFinalCompositeLayer &&
		vrMenuFinalCompositeLayer->resource &&
		vrMenuFinalCompositeLayer->srv &&
		vrMenuFinalCompositeLayer->rtv &&
		vrMenuFinalCompositeLayerWidth == a_width &&
		vrMenuFinalCompositeLayerHeight == a_height &&
		vrMenuFinalCompositeLayerFormat == a_format) {
		return true;
	}

	try {
		auto layer = CreateNamedTexture2D(a_width, a_height, a_format, true, false, true, "VRMenuFinalCompositeLayer");
		if (!layer || !layer->resource || !layer->srv || !layer->rtv) {
			ResetVRMenuFinalCompositeLayer();
			return false;
		}

		vrMenuFinalCompositeLayer = std::move(layer);
		vrMenuFinalCompositeLayerWidth = a_width;
		vrMenuFinalCompositeLayerHeight = a_height;
		vrMenuFinalCompositeLayerFormat = a_format;
		vrMenuFinalCompositeLayerClearedFrame = std::numeric_limits<uint32_t>::max();
		return true;
	} catch (const std::exception& e) {
		static bool loggedCreateFailure = false;
		LogWarnOnce(loggedCreateFailure, "[VRMenuComposite] Final menu layer allocation failed", e);
	} catch (...) {
		static bool loggedCreateFailure = false;
		LogWarnOnce(loggedCreateFailure, "[VRMenuComposite] Final menu layer allocation failed");
	}

	ResetVRMenuFinalCompositeLayer();
	return false;
}

bool Upscaling::DrawVRMenuBridgeIntoFinalCompositeLayer(
	ID3D11DeviceContext* a_context,
	DXGI_FORMAT a_format,
	UINT a_indexCount,
	UINT a_instanceCount,
	UINT a_startIndexLocation,
	INT a_baseVertexLocation,
	UINT a_startInstanceLocation,
	uint32_t a_renderWidth,
	uint32_t a_renderHeight,
	uint32_t a_finalWidth,
	uint32_t a_finalHeight)
{
	if (!a_context ||
		!a_indexCount ||
		a_instanceCount != 2 ||
		!a_renderWidth ||
		!a_renderHeight ||
		!a_finalWidth ||
		!a_finalHeight ||
		!vrMenuLayerCaptureBlendState ||
		!EnsureVRMenuFinalCompositeLayer(a_finalWidth, a_finalHeight, a_format)) {
		return false;
	}

	const auto* state = globals::state;
	const uint32_t frame = state ? state->frameCount : 0;
	static constexpr float kTransparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	if (vrMenuFinalCompositeLayerClearedFrame != frame) {
		a_context->ClearRenderTargetView(vrMenuFinalCompositeLayer->rtv.get(), kTransparent);
		vrMenuFinalCompositeLayerClearedFrame = frame;
		vrMenuFinalCompositeLayerDrawCount = 0;
	}

	std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> previousRTVs{};
	ID3D11DepthStencilView* previousDSV = nullptr;
	ID3D11BlendState* previousBlendState = nullptr;
	FLOAT previousBlendFactor[4] = {};
	UINT previousSampleMask = 0;
	std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> previousViewports{};
	UINT previousViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> previousScissors{};
	UINT previousScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

	a_context->OMGetRenderTargets(static_cast<UINT>(previousRTVs.size()), previousRTVs.data(), &previousDSV);
	a_context->OMGetBlendState(&previousBlendState, previousBlendFactor, &previousSampleMask);
	a_context->RSGetViewports(&previousViewportCount, previousViewports.data());
	a_context->RSGetScissorRects(&previousScissorCount, previousScissors.data());

	auto restoreState = ScopeExit([&]() {
		a_context->OMSetRenderTargets(static_cast<UINT>(previousRTVs.size()), previousRTVs.data(), previousDSV);
		a_context->OMSetBlendState(previousBlendState, previousBlendFactor, previousSampleMask);
		a_context->RSSetViewports(previousViewportCount, previousViewportCount ? previousViewports.data() : nullptr);
		a_context->RSSetScissorRects(previousScissorCount, previousScissorCount ? previousScissors.data() : nullptr);

		for (auto* rtv : previousRTVs) {
			if (rtv)
				rtv->Release();
		}
		if (previousDSV)
			previousDSV->Release();
		if (previousBlendState)
			previousBlendState->Release();
		vrMenuParallelBridgeDrawInProgress = false;
	});
	if (!previousViewportCount)
		return false;

	const float scaleX = static_cast<float>(a_finalWidth) / static_cast<float>(a_renderWidth);
	const float scaleY = static_cast<float>(a_finalHeight) / static_cast<float>(a_renderHeight);
	auto finalViewports = previousViewports;
	for (UINT index = 0; index < previousViewportCount; ++index) {
		finalViewports[index].TopLeftX *= scaleX;
		finalViewports[index].TopLeftY *= scaleY;
		finalViewports[index].Width *= scaleX;
		finalViewports[index].Height *= scaleY;
	}

	auto finalScissors = previousScissors;
	for (UINT index = 0; index < previousScissorCount; ++index) {
		finalScissors[index].left = static_cast<LONG>(std::lround(static_cast<double>(previousScissors[index].left) * scaleX));
		finalScissors[index].right = static_cast<LONG>(std::lround(static_cast<double>(previousScissors[index].right) * scaleX));
		finalScissors[index].top = static_cast<LONG>(std::lround(static_cast<double>(previousScissors[index].top) * scaleY));
		finalScissors[index].bottom = static_cast<LONG>(std::lround(static_cast<double>(previousScissors[index].bottom) * scaleY));
	}

	ID3D11RenderTargetView* layerRTV = vrMenuFinalCompositeLayer->rtv.get();
	a_context->OMSetRenderTargets(1, &layerRTV, nullptr);
	a_context->OMSetBlendState(vrMenuLayerCaptureBlendState.get(), previousBlendFactor, previousSampleMask);
	a_context->RSSetViewports(previousViewportCount, finalViewports.data());
	a_context->RSSetScissorRects(previousScissorCount, previousScissorCount ? finalScissors.data() : nullptr);

	vrMenuParallelBridgeDrawInProgress = true;
	a_context->DrawIndexedInstanced(a_indexCount, a_instanceCount, a_startIndexLocation, a_baseVertexLocation, a_startInstanceLocation);
	vrMenuParallelBridgeDrawInProgress = false;
	++vrMenuFinalCompositeLayerDrawCount;

	return true;
}

bool Upscaling::TryCaptureAndSuppressVRMenuBridgeDraw(
	ID3D11DeviceContext* a_context,
	UINT a_indexCount,
	UINT a_instanceCount,
	UINT a_startIndexLocation,
	INT a_baseVertexLocation,
	UINT a_startInstanceLocation)
{
	if (!globals::game::isVR || !a_context || vrMenuParallelBridgeDrawInProgress)
		return false;

	auto* state = globals::state;
	if (!state)
		return false;

	if (!IsVRRenderScaleModeActive() || !IsPresentationUpscalingActive())
		return false;
	if (IsCommunityShadersMenuOpen())
		return false;
	if (!a_indexCount ||
		a_instanceCount != 2 ||
		a_startIndexLocation != 0 ||
		a_baseVertexLocation != 0 ||
		a_startInstanceLocation != 0) {
		return false;
	}

	if (!IsExplicitVRMenuPresentationContextActive())
		return false;

	const auto& plan = GetRuntimeResolutionPlan();
	const uint32_t renderWidth = ClampPositiveDimension(plan.engineRenderSize.x);
	const uint32_t renderHeight = ClampPositiveDimension(plan.engineRenderSize.y);
	const uint32_t finalWidth = ClampPositiveDimension(plan.finalOutputSize.x);
	const uint32_t finalHeight = ClampPositiveDimension(plan.finalOutputSize.y);
	if (!renderWidth || !renderHeight || !finalWidth || !finalHeight ||
		finalWidth <= renderWidth || finalHeight <= renderHeight) {
		return false;
	}

	BeginVRMenuFinalCompositeFrame(state->frameCount);

	ID3D11RenderTargetView* rtv = nullptr;
	a_context->OMGetRenderTargets(1, &rtv, nullptr);
	auto rtvRelease = ScopeExit([&]() {
		if (rtv)
			rtv->Release();
	});
	if (!rtv)
		return false;

	VRMenuCompositionTargetMatch destination{};
	if (!TryResolveVRMenuCompositionView(rtv, destination) ||
		destination.target != RE::RENDER_TARGETS::kMENUBG ||
		destination.width != renderWidth ||
		destination.height != renderHeight ||
		destination.samples != 1) {
		return false;
	}

	std::array<ID3D11ShaderResourceView*, kVRMenuBridgeSRVSlots> psSRVs{};
	a_context->PSGetShaderResources(0, static_cast<UINT>(psSRVs.size()), psSRVs.data());
	auto srvRelease = ScopeExit([&]() {
		for (auto* srv : psSRVs) {
			if (srv)
				srv->Release();
		}
	});

	size_t menuSourceTargetIndex = kVRKnownGameMenuFinalCompositeTargets.size();
	for (uint32_t slot = 0; slot < psSRVs.size(); ++slot) {
		VRMenuCompositionTargetMatch source{};
		if (!TryResolveVRMenuCompositionView(psSRVs[slot], source) || source.samples != 1)
			continue;

		for (size_t targetIndex = 0; targetIndex < kVRKnownGameMenuFinalCompositeTargets.size(); ++targetIndex) {
			if (source.target == kVRKnownGameMenuFinalCompositeTargets[targetIndex]) {
				menuSourceTargetIndex = targetIndex;
				break;
			}
		}
		if (menuSourceTargetIndex != kVRKnownGameMenuFinalCompositeTargets.size())
			break;
	}
	if (menuSourceTargetIndex == kVRKnownGameMenuFinalCompositeTargets.size())
		return false;

	// The exact reduced menu bridge is a direct menu-text signal. Arm the short
	// observed tail only after an explicit menu context confirms this is not
	// normal in-game projected UI/HUD work.
	ExtendVRObservedProjectedMenuTail(kVRObservedMenuPresentationTailFrames);
	ExtendVRMenuPresentationTail(kVRObservedMenuPresentationTailFrames);
	ExtendVRMenuBridgeTraceTail(kVRObservedMenuPresentationTailFrames);

	const bool liveLayerDrawn = DrawVRMenuBridgeIntoFinalCompositeLayer(
		a_context,
		destination.format,
		a_indexCount,
		a_instanceCount,
		a_startIndexLocation,
		a_baseVertexLocation,
		a_startInstanceLocation,
		renderWidth,
		renderHeight,
		finalWidth,
		finalHeight);
	if (!liveLayerDrawn)
		return false;

	vrMenuFinalCompositeSuppressedTargets[menuSourceTargetIndex] = true;
	return true;
}

bool Upscaling::ShouldTraceVRMenuBridgeDrawOperation()
{
	auto* state = globals::state;
	if (!globals::game::isVR || !state)
		return false;

	const uint32_t currentFrame = std::max(state->frameCount, 1u);
	const uint64_t cachedState = g_vrMenuBridgeTraceCachedState.load(std::memory_order_relaxed);
	if ((cachedState >> 1) != currentFrame ||
		((cachedState & 1ull) == 0 &&
			(IsVRMenuBridgeTraceTailActive(state) || IsVRObservedProjectedMenuTailActive(state)))) {
		RefreshVRMenuBridgeTraceState(state);
	}

	return g_vrMenuBridgeTraceCachedState.load(std::memory_order_relaxed) ==
	       EncodeVRMenuBridgeTraceState(currentFrame, true);
}

bool Upscaling::TraceVRMenuBridgeDrawOperation(
	ID3D11DeviceContext* a_context,
	UINT a_indexCount,
	UINT a_instanceCount,
	UINT a_startIndexLocation,
	INT a_baseVertexLocation,
	UINT a_startInstanceLocation)
{
	auto& upscaling = globals::features::upscaling;
	return upscaling.TryCaptureAndSuppressVRMenuBridgeDraw(
		a_context,
		a_indexCount,
		a_instanceCount,
		a_startIndexLocation,
		a_baseVertexLocation,
		a_startInstanceLocation);
}

bool Upscaling::ApplyKnownGameMenuFinalComposite(uint32_t a_eyeIndex, Texture2D& a_outputTexture, uint32_t a_eyeWidth, uint32_t a_eyeHeight, uint32_t a_frame)
{
	if (!globals::game::isVR ||
		a_eyeIndex >= 2 ||
		!a_eyeWidth ||
		!a_eyeHeight ||
		!a_outputTexture.resource ||
		!a_outputTexture.rtv ||
		vrMenuFinalCompositeFrame != a_frame ||
		!vrMenuFinalCompositeLayer ||
		!vrMenuFinalCompositeLayer->resource ||
		!vrMenuFinalCompositeLayer->srv ||
		vrMenuFinalCompositeLayerDrawCount == 0 ||
		std::none_of(vrMenuFinalCompositeSuppressedTargets.begin(), vrMenuFinalCompositeSuppressedTargets.end(), [](bool suppressed) { return suppressed; })) {
		return false;
	}
	if (!IsKnownGameMenuContextActive() ||
		IsCommunityShadersMenuOpen() ||
		IsVRMenuScenePresentationBlockActive() ||
		IsSaveLoadTransitionContextActive()) {
		return false;
	}

	auto* context = globals::d3d::context;
	auto* deferred = globals::deferred;
	if (!context || !deferred || !deferred->linearSampler || !upscaleRasterizerState || !vrMenuCompositeBlendState || !vrMenuLayerCompositeCB)
		return false;

	ID3D11PixelShader* pixelShader = nullptr;
	try {
		pixelShader = GetVRMenuLayerCompositePS();
	} catch (const std::exception& e) {
		static bool loggedShaderFailure = false;
		LogWarnOnce(loggedShaderFailure, "[VRMenuComposite] Final menu composite shader unavailable", e);
		if (MarkSubmitStageDeviceLostIfNeeded(e, "VR menu final composite shader creation"))
			return false;
	} catch (...) {
		static bool loggedShaderFailure = false;
		LogWarnOnce(loggedShaderFailure, "[VRMenuComposite] Final menu composite shader unavailable");
		if (MarkSubmitStageDeviceLostIfDeviceRemoved("VR menu final composite shader creation"))
			return false;
	}
	if (!pixelShader)
		return false;

	ID3D11VertexShader* previousVS = nullptr;
	ID3D11PixelShader* previousPS = nullptr;
	ID3D11HullShader* previousHS = nullptr;
	ID3D11DomainShader* previousDS = nullptr;
	ID3D11GeometryShader* previousGS = nullptr;
	ID3D11ShaderResourceView* previousSRV = nullptr;
	ID3D11SamplerState* previousSampler = nullptr;
	ID3D11Buffer* previousPSCB0 = nullptr;
	std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> previousRTVs{};
	ID3D11DepthStencilView* previousDSV = nullptr;
	ID3D11BlendState* previousBlendState = nullptr;
	FLOAT previousBlendFactor[4] = {};
	UINT previousSampleMask = 0;
	ID3D11DepthStencilState* previousDepthStencilState = nullptr;
	UINT previousStencilRef = 0;
	ID3D11RasterizerState* previousRasterizerState = nullptr;
	std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> previousViewports{};
	UINT previousViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	D3D11_PRIMITIVE_TOPOLOGY previousTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	ID3D11InputLayout* previousInputLayout = nullptr;

	context->VSGetShader(&previousVS, nullptr, nullptr);
	context->PSGetShader(&previousPS, nullptr, nullptr);
	context->HSGetShader(&previousHS, nullptr, nullptr);
	context->DSGetShader(&previousDS, nullptr, nullptr);
	context->GSGetShader(&previousGS, nullptr, nullptr);
	context->PSGetShaderResources(0, 1, &previousSRV);
	context->PSGetSamplers(0, 1, &previousSampler);
	context->PSGetConstantBuffers(0, 1, &previousPSCB0);
	context->OMGetRenderTargets(static_cast<UINT>(previousRTVs.size()), previousRTVs.data(), &previousDSV);
	context->OMGetBlendState(&previousBlendState, previousBlendFactor, &previousSampleMask);
	context->OMGetDepthStencilState(&previousDepthStencilState, &previousStencilRef);
	context->RSGetState(&previousRasterizerState);
	context->RSGetViewports(&previousViewportCount, previousViewports.data());
	context->IAGetPrimitiveTopology(&previousTopology);
	context->IAGetInputLayout(&previousInputLayout);

	auto restoreState = ScopeExit([&]() {
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11SamplerState* nullSampler = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
		context->PSSetSamplers(0, 1, &nullSampler);

		context->VSSetShader(previousVS, nullptr, 0);
		context->PSSetShader(previousPS, nullptr, 0);
		context->HSSetShader(previousHS, nullptr, 0);
		context->DSSetShader(previousDS, nullptr, 0);
		context->GSSetShader(previousGS, nullptr, 0);
		context->PSSetShaderResources(0, 1, &previousSRV);
		context->PSSetSamplers(0, 1, &previousSampler);
		context->PSSetConstantBuffers(0, 1, &previousPSCB0);
		context->OMSetRenderTargets(static_cast<UINT>(previousRTVs.size()), previousRTVs.data(), previousDSV);
		context->OMSetBlendState(previousBlendState, previousBlendFactor, previousSampleMask);
		context->OMSetDepthStencilState(previousDepthStencilState, previousStencilRef);
		context->RSSetState(previousRasterizerState);
		context->RSSetViewports(previousViewportCount, previousViewportCount ? previousViewports.data() : nullptr);
		context->IASetPrimitiveTopology(previousTopology);
		context->IASetInputLayout(previousInputLayout);

		if (previousVS)
			previousVS->Release();
		if (previousPS)
			previousPS->Release();
		if (previousHS)
			previousHS->Release();
		if (previousDS)
			previousDS->Release();
		if (previousGS)
			previousGS->Release();
		if (previousSRV)
			previousSRV->Release();
		if (previousSampler)
			previousSampler->Release();
		if (previousPSCB0)
			previousPSCB0->Release();
		for (auto* rtv : previousRTVs) {
			if (rtv)
				rtv->Release();
		}
		if (previousDSV)
			previousDSV->Release();
		if (previousBlendState)
			previousBlendState->Release();
		if (previousDepthStencilState)
			previousDepthStencilState->Release();
		if (previousRasterizerState)
			previousRasterizerState->Release();
		if (previousInputLayout)
			previousInputLayout->Release();
	});

	try {
		D3D11_TEXTURE2D_DESC layerDesc{};
		if (!TryGetTexture2DDesc(vrMenuFinalCompositeLayer->resource.get(), layerDesc) ||
			layerDesc.Width != vrMenuFinalCompositeLayerWidth ||
			layerDesc.Height != vrMenuFinalCompositeLayerHeight ||
			layerDesc.Width != a_eyeWidth * 2u ||
			layerDesc.Height != a_eyeHeight) {
			return false;
		}

		VRMenuLayerCompositeCB compositeData{};
		compositeData.sourceScale = { 0.5f, 1.0f };
		compositeData.sourceOffset = { a_eyeIndex == 0 ? 0.0f : 0.5f, 0.0f };
		vrMenuLayerCompositeCB->Update(compositeData);
		auto* compositeBuffer = vrMenuLayerCompositeCB->CB();
		auto* vertexShader = GetUpscaleVS();
		if (!compositeBuffer || !vertexShader)
			return false;

		ID3D11RenderTargetView* targetRTV = a_outputTexture.rtv.get();
		ID3D11SamplerState* sampler = deferred->linearSampler;
		ID3D11Buffer* constantBuffer = compositeBuffer;
		D3D11_VIEWPORT viewport{};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(a_eyeWidth);
		viewport.Height = static_cast<float>(a_eyeHeight);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		const float blendFactor[4] = {};

		context->OMSetRenderTargets(1, &targetRTV, nullptr);
		context->OMSetBlendState(vrMenuCompositeBlendState.get(), blendFactor, 0xFFFFFFFF);
		context->OMSetDepthStencilState(nullptr, 0);
		context->RSSetState(upscaleRasterizerState.get());
		context->RSSetViewports(1, &viewport);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);
		context->PSSetShader(pixelShader, nullptr, 0);
		context->PSSetSamplers(0, 1, &sampler);
		context->PSSetConstantBuffers(0, 1, &constantBuffer);

		ID3D11ShaderResourceView* sourceSRV = vrMenuFinalCompositeLayer->srv.get();
		context->PSSetShaderResources(0, 1, &sourceSRV);
		context->Draw(3, 0);
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
	} catch (const std::exception& e) {
		static bool loggedCompositeFailure = false;
		LogWarnOnce(loggedCompositeFailure, "[VRMenuComposite] Final menu composite failed", e);
		if (MarkSubmitStageDeviceLostIfNeeded(e, "VR menu final composite"))
			return false;
		return false;
	} catch (...) {
		static bool loggedCompositeFailure = false;
		LogWarnOnce(loggedCompositeFailure, "[VRMenuComposite] Final menu composite failed");
		if (MarkSubmitStageDeviceLostIfDeviceRemoved("VR menu final composite"))
			return false;
		return false;
	}

	return true;
}

/**
 * @brief Creates a Direct3D 11 device and swap chain, with support for advanced upscaling and frame generation features.
 *
 * This function intercepts the standard D3D11 device and swap chain creation process to enable integration with Streamline and FidelityFX technologies, as well as optional D3D12 proxying for frame generation. It adjusts swap chain flags for tearing support, manages feature checks, and conditionally routes device creation through Streamline or FidelityFX proxies based on runtime settings and hardware capabilities. If frame generation is enabled and supported, a D3D12 proxy is used; otherwise, the standard D3D11 creation path is followed.
 *
 * @return HRESULT indicating the success or failure of device and swap chain creation.
 */
HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChainUpscaling(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);
	globals::state->SetAdapterDescription(adapterDesc.Description);

	auto& upscaling = globals::features::upscaling;
	if (IsRenderDocUpscalingBlocked(true)) {
		if (!g_renderDocUpscalingD3DHookBypassLogged.exchange(true, std::memory_order_acq_rel)) {
			logger::warn(
				"[Upscaling] Bypassing D3D11 upscaling device hook because {}.",
				GetRenderDocUpscalingBlockReason());
		}
		return ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter,
			DriverType,
			Software,
			Flags,
			pFeatureLevels,
			FeatureLevels,
			SDKVersion,
			pSwapChainDesc,
			ppSwapChain,
			ppDevice,
			pFeatureLevel,
			ppImmediateContext);
	}

	upscaling.LoadUpscalingSDKs();

	if (upscaling.IsBackendInitialized())
		upscaling.CheckBackendFeatures(pAdapter);

	// Use better swap effect to prevent tearing and improve performance
	pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	// FLIP_DISCARD requires at least two buffers.
	if (pSwapChainDesc->BufferCount < 2)
		pSwapChainDesc->BufferCount = 2;
	// This branch currently runs without HDRDisplay integration; normalize sRGB
	// swapchain formats to UNORM for the D3D12 proxy/inter-op path.
	if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	const bool isVR = REL::Module::IsVR();
	bool shouldProxy = !isVR;
	if (shouldProxy)
		if (!pSwapChainDesc->Windowed)
			shouldProxy = false;

	auto refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);
	upscaling.refreshRate = refreshRate;

	if (shouldProxy) {
		if (upscaling.settings.frameGenerationMode)
			if (refreshRate >= 120)
				shouldProxy = true;
			else if (upscaling.settings.frameGenerationForceEnable)
				shouldProxy = true;
			else
				shouldProxy = false;
		else
			shouldProxy = false;
	}

	upscaling.lowRefreshRate = refreshRate < 120;
	upscaling.isWindowed = pSwapChainDesc->Windowed;

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	if (shouldProxy) {
		logger::info("[Frame Generation] Frame Generation enabled, using D3D12 proxy");

		const bool hasFrameGenModule = upscaling.HasFrameGenModule();
		if (hasFrameGenModule) {
			DX::ThrowIfFailed(D3D11CreateDevice(
				pAdapter,
				DriverType,
				Software,
				Flags,
				&featureLevel,
				1,
				SDKVersion,
				ppDevice,
				pFeatureLevel,
				ppImmediateContext));

			upscaling.SetProxyD3D11Device(*ppDevice);
			upscaling.SetProxyD3D11DeviceContext(*ppImmediateContext);
			upscaling.CreateProxySwapChain(pAdapter, *pSwapChainDesc);
			upscaling.CreateProxyInterop();

			*ppSwapChain = upscaling.GetProxySwapChain();

			upscaling.d3d12SwapChainActive = true;

			if (upscaling.IsBackendInitialized()) {
				upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
				// Keep the D3D12 proxy swap chain as the outermost layer so
				// GetDevice(IID_ID3D11Device) stays compatible with other SKSE plugins.
				upscaling.SetBackendD3DDevice(*ppDevice);
				// Some Streamline features (notably Reflex/PCL) may not report
				// load/support status reliably until the D3D device is bound.
				upscaling.CheckBackendFeatures(pAdapter);
				upscaling.PostBackendDevice();
			}

			return S_OK;
		} else {
			logger::warn("[Frame Generation] FidelityFX DLLs are not loaded, skipping proxy");
		}
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	if (upscaling.IsBackendInitialized()) {
		upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
		upscaling.UpgradeBackendInterface((void**)&(*ppSwapChain));
		upscaling.SetBackendD3DDevice(*ppDevice);
		// Re-check after device bind to ensure feature availability is accurate.
		upscaling.CheckBackendFeatures(pAdapter);
		upscaling.PostBackendDevice();
	}

	return ret;
}

void Upscaling::DrawSettings()
{
	const uint64_t resourceSettingsKeyBefore = BuildUpscalingResourceMutationSettingsKey(settings);

	struct UpscaleUiChoice
	{
		UpscaleMethod method;
		bool useRuntimeFsr4;
		const char* label;
	};

	const bool isNvidiaAdapter = fidelityFX.IsNvidiaAdapterDetected();
	const bool runtimeUpscalerPresent = fidelityFX.IsRuntimeUpscalerPresent();
	const bool runtimeFsr4AutoEligible = fidelityFX.IsRuntimeFsr4AutoEligible();
	const bool featureDLSS = streamline.featureDLSS;
	ApplyOpenCompositeUpscalingBlocker();
	const auto& openCompositeBlocker = GetOpenCompositeUpscalingBlocker();
	const bool openCompositeBlocksUpscaling = openCompositeBlocker.active;
	const bool renderDocBlocksUpscaling = IsRenderDocUpscalingBlocked();

	uint32_t* currentUpscaleMode = &settings.upscaleMethod;
	if (!featureDLSS)
		currentUpscaleMode = &settings.upscaleMethodNoDLSS;
	if (*currentUpscaleMode == static_cast<uint32_t>(UpscaleMethod::kFSR) && !runtimeFsr4AutoEligible)
		settings.fsr4RuntimeEnable = false;

	std::vector<UpscaleUiChoice> upscaleChoices = {
		{ UpscaleMethod::kNONE, false, "None" }
	};

	if (!openCompositeBlocksUpscaling) {
		upscaleChoices.push_back({ UpscaleMethod::kTAA, false, "TAA" });
		upscaleChoices.push_back({ UpscaleMethod::kFSR, false, "AMD FSR 3.1.5" });

		if (runtimeFsr4AutoEligible)
			upscaleChoices.push_back({ UpscaleMethod::kFSR, true, "AMD FSR 4.1" });

		if (featureDLSS)
			upscaleChoices.push_back({ UpscaleMethod::kDLSS, false, "NVIDIA DLSS" });
	}

	auto matchesCurrentChoice = [&](const UpscaleUiChoice& choice) {
		if (static_cast<uint32_t>(choice.method) != *currentUpscaleMode)
			return false;
		if (choice.method == UpscaleMethod::kFSR)
			return settings.fsr4RuntimeEnable == choice.useRuntimeFsr4;
		return true;
	};

	int methodUiIndex = 0;
	for (int i = 0; i < static_cast<int>(upscaleChoices.size()); ++i) {
		if (matchesCurrentChoice(upscaleChoices[i])) {
			methodUiIndex = i;
			break;
		}
	}
	if (methodUiIndex == 0 && !matchesCurrentChoice(upscaleChoices[0])) {
		for (int i = 0; i < static_cast<int>(upscaleChoices.size()); ++i) {
			if (static_cast<uint32_t>(upscaleChoices[i].method) == *currentUpscaleMode) {
				methodUiIndex = i;
				break;
			}
		}
	}

	const char* currentMethodLabel = upscaleChoices[methodUiIndex].label;
	if (openCompositeBlocksUpscaling)
		ImGui::BeginDisabled();
	const bool methodChanged = ImGui::SliderInt("Method", &methodUiIndex, 0, static_cast<int>(upscaleChoices.size() - 1), currentMethodLabel);
	if (openCompositeBlocksUpscaling)
		ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (openCompositeBlocksUpscaling) {
			ImGui::Text("Locked to None while Open Composite has %s=true.", openCompositeBlocker.settingName.c_str());
		} else {
			ImGui::TextUnformatted("Selects the upscaling backend.");
			if (runtimeFsr4AutoEligible)
				ImGui::TextUnformatted("Range: choose between TAA, DLSS, FSR 3.1.5, Runtime FSR 4.1, or None.");
			else
				ImGui::TextUnformatted("Range: choose between TAA, DLSS, FSR 3.1.5, or None.");
			if (renderDocBlocksUpscaling)
				ImGui::Text("Runtime is forced to None while %s.", GetRenderDocUpscalingBlockReason());
		}
	}
	methodUiIndex = std::clamp(methodUiIndex, 0, static_cast<int>(upscaleChoices.size() - 1));
	const auto& selectedUpscaleChoice = upscaleChoices[methodUiIndex];
	const bool shouldApplyMethodSelection = methodChanged || !matchesCurrentChoice(selectedUpscaleChoice);
	if (shouldApplyMethodSelection) {
		const bool targetRenderScaleMode = IsRenderScaleModeRequested();
		const uint32_t targetQualityMode = GetEffectiveUpscalingQualityMode();
		const uint32_t targetDLSSPreset = GetEffectiveDLSSPreset();
		if (selectedUpscaleChoice.method == UpscaleMethod::kFSR)
			settings.fsr4RuntimeEnable = selectedUpscaleChoice.useRuntimeFsr4;
		ApplyCSMenuUpscalingTransition(
			selectedUpscaleChoice.method,
			targetRenderScaleMode,
			targetQualityMode,
			targetDLSSPreset,
			"upscaling menu method change");
	}
	if (openCompositeBlocksUpscaling) {
		ApplyOpenCompositeUpscalingBlocker();
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
		if (openCompositeBlocker.configPath.empty()) {
			ImGui::TextWrapped(
				"Community Shaders Upscaling is locked to None because Open Composite has %s=true.",
				openCompositeBlocker.settingName.c_str());
		} else {
			ImGui::TextWrapped(
				"Community Shaders Upscaling is locked to None because Open Composite has %s=true in %s.",
				openCompositeBlocker.settingName.c_str(),
				openCompositeBlocker.configPath.c_str());
		}
		ImGui::PopStyleColor();
	}
	if (renderDocBlocksUpscaling) {
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
		ImGui::TextWrapped(
			"Community Shaders Upscaling runs as None while %s to avoid DLSS/FSR backend startup crashes.",
			GetRenderDocUpscalingBlockReason());
		ImGui::PopStyleColor();
	}

	// Check the current upscale method
	auto upscaleMethod = GetUpscaleMethod();
	const bool runtimeFsr4Requested =
		upscaleMethod == UpscaleMethod::kFSR &&
		settings.fsr4RuntimeEnable;

	const bool runtimeFsrPathRequested =
		upscaleMethod == UpscaleMethod::kFSR &&
		fidelityFX.ShouldUseRuntimeUpscalerForFSR();
	const bool showRuntimeFsrFramePath = runtimeFsr4Requested || runtimeFsrPathRequested;

	if (showRuntimeFsrFramePath) {
		ImGui::TextDisabled("Current frame path: %s", fidelityFX.GetRuntimeUpscalerLastFramePathLabel());
		if (fidelityFX.IsRuntimeUpscalerFailureLatched()) {
			ImGui::TextDisabled("Runtime FSR path is latched off after a runtime failure; using host FSR 3.1.5 fallback.");
		} else if (fidelityFX.IsRuntimeFsr4FailureLatched()) {
			ImGui::TextDisabled("Runtime FSR 4.1 is latched off after a runtime failure; using runtime FSR 3.1.5 fallback.");
		} else if (fidelityFX.HasRuntimeUpscalerSupportCheckResult() &&
				   !fidelityFX.IsRuntimeUpscalerSupportConfirmed()) {
			ImGui::TextDisabled("Runtime FSR context creation failed; using host FSR 3.1.5 fallback.");
		}
		if (!runtimeUpscalerPresent && runtimeFsr4Requested)
			ImGui::TextDisabled("Runtime FSR 4.1 unavailable: missing FidelityFX upscaler runtime.");
	}

	// Display warning for DLSS resolution limits (non-VR only; VR handles this automatically)
	if (!globals::game::isVR && upscaleMethod == UpscaleMethod::kDLSS) {
		auto screenSize = globals::state->screenSize;
		if (screenSize.x > streamline.MAX_RESOLUTION || screenSize.y > streamline.MAX_RESOLUTION) {
			Util::Text::Warning("Warning: Requested resolution %.0f x %.0f exceeds maximum supported resolution %d x %d for DLSS.",
				screenSize.x, screenSize.y, streamline.MAX_RESOLUTION, streamline.MAX_RESOLUTION);
			Util::Text::Warning("DLSS will not function. Lower your resolution or select a different upscaling method.");
		}
	}

	auto drawRenderPipelineBlock = [&]() {
		if (!globals::game::isVR)
			return;

		const auto vrRenderScaleStatus = GetVRRenderScaleModeStatus();
		const bool renderScaleMethodEligible = IsRenderScaleMethodEligible(upscaleMethod);
		const uint32_t renderScaleQualityMode = renderScaleMethodEligible ? GetEffectiveUpscalingQualityMode() : settings.qualityMode;
		const bool renderScaleQualitySelected = IsRenderScaleQualityMode(renderScaleQualityMode);
		const bool vrRenderScaleRequested = GetPerfModeRequested();
		const bool perfModeRelatchPending = pendingPerfModeRenderTargetRecreate.load(std::memory_order_relaxed);
		const bool publicRenderScaleRequested = vrRenderScaleRequested;
		const bool publicRenderScaleCanEdit =
			(renderScaleMethodEligible && renderScaleQualitySelected) ||
			publicRenderScaleRequested;

		ImGui::Separator();
		if (!ImGui::TreeNodeEx("Render Pipeline", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		const char* renderScaleModes[] = { "Disabled", "Enabled" };
		int renderScaleMode = publicRenderScaleRequested ? 1 : 0;
		{
			auto disabledGuard = Util::DisableGuard(!publicRenderScaleCanEdit);
			if (ImGui::SliderInt("VR Render Scale Mode", &renderScaleMode, 0, 1, renderScaleModes[std::clamp(renderScaleMode, 0, 1)])) {
				const bool enableRenderScaleMode = std::clamp(renderScaleMode, 0, 1) != 0;
				ApplyCSMenuUpscalingTransition(
					upscaleMethod,
					enableRenderScaleMode,
					renderScaleQualityMode,
					GetEffectiveDLSSPreset(),
					"upscaling menu render-scale mode change");
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("DLSS/FSR VR only.");
			ImGui::TextUnformatted("CS applies menu changes after closing the menu while render targets rebuild.");
			ImGui::TextUnformatted("Restart Skyrim VR if the change stays pending.");
		}
		ImGui::Text("VR Render Scale Status: %s", GetVRRenderScaleModeStatusName(vrRenderScaleStatus));

		ImGui::Checkbox("VR FPS Stabilizer Sync", &settings.vrFpsStabilizerSync);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("On save-load, reads VRFpsStabilizer.ini [Conditional] Interior/Exterior CS upscaling rows.");
			ImGui::TextUnformatted("Syncs Method, Upscale Preset, DLSS Profile, and VR Render Scale Mode to the cell you loaded into.");
			ImGui::TextUnformatted("Legacy DLSSMode / RenderAtUpscaleRes rows are supported; explicit UpscaleMethod rows can select FSR.");
			ImGui::TextUnformatted("Use this when VR FPS Stabilizer drives different interior/exterior upscaling profiles.");
		}

		if (perfMode.HasRestartRequiredChange())
			Util::Text::Warning(perfModeRelatchPending ? "Warning: VR Render Scale Mode relatch pending" : "Warning: VR Render Scale Mode change requires relatch or restart");
		if (!renderScaleMethodEligible)
			ImGui::TextDisabled("VR Render Scale Mode is available only with DLSS/FSR in VR.");

		ImGui::TreePop();
	};

	// Display upscaling settings if applicable
	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		settings.qualityMode = ClampQualityModeUInt(settings.qualityMode);
		const bool usePendingVRUpscalingQuality = globals::game::isVR && IsRenderScaleMethodEligible(upscaleMethod);
		const uint32_t effectiveQualityMode = usePendingVRUpscalingQuality ? GetEffectiveUpscalingQualityMode() : settings.qualityMode;
		const char* baseLabel = GetQualityModeName(effectiveQualityMode, upscaleMethod == UpscaleMethod::kDLSS);
		std::string labelWithScale = std::format(
			"{} ( {:.2f}x )",
			baseLabel,
			Upscaling::GetQualityModeResolutionScale(effectiveQualityMode));

		int qualityMode = static_cast<int>(effectiveQualityMode);
		if (ImGui::SliderInt(
				"Upscale Preset",
				&qualityMode,
				0,
				static_cast<int>(kQualityModeMaxIndex),
				labelWithScale.c_str())) {
			const uint32_t requestedQualityMode = static_cast<uint32_t>(std::clamp(qualityMode, 0, static_cast<int>(kQualityModeMaxIndex)));
			const bool targetRenderScaleMode = IsRenderScaleModeRequested() && IsRenderScaleQualityMode(requestedQualityMode);
			ApplyCSMenuUpscalingTransition(
				upscaleMethod,
				targetRenderScaleMode,
				requestedQualityMode,
				GetEffectiveDLSSPreset(),
				"upscaling menu preset change");
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls the shared DLSS/FSR/FSR4.1 internal render scale / quality level.");
			ImGui::TextUnformatted(
				"Range: low 0 (highest quality, lowest performance gain) to high 6 (highest performance gain, lowest quality).");
		}

		if (upscaleMethod == UpscaleMethod::kFSR) {
			ImGui::SliderFloat("Sharpness", &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Adjusts post-upscale sharpness for FSR.");
				ImGui::TextUnformatted("Range: low 0.0 (softest) to high 1.0 (sharpest).");
			}
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			// Keep persisted preset values stable (0=J,1=K,2=L,3=M,4=F) while
			// presenting an alphabetical selection list in the UI.
			const uint32_t dlssProfileOrder[] = { 4u, 0u, 1u, 2u, 3u };  // F, J, K, L, M
			const char* dlssProfiles[] = { "F", "J", "K", "L", "M" };
			settings.dlssPreset = std::min(settings.dlssPreset, kDLSSPresetMaxIndex);
			const uint32_t effectiveDLSSPreset = GetEffectiveDLSSPreset();

			int dlssProfileUiIndex = 0;
			for (int i = 0; i < IM_ARRAYSIZE(dlssProfileOrder); ++i) {
				if (dlssProfileOrder[i] == effectiveDLSSPreset) {
					dlssProfileUiIndex = i;
					break;
				}
			}

			if (ImGui::SliderInt("DLSS Profile", &dlssProfileUiIndex, 0, static_cast<int>(kDLSSPresetMaxIndex), dlssProfiles[dlssProfileUiIndex])) {
				dlssProfileUiIndex = std::clamp(dlssProfileUiIndex, 0, static_cast<int>(kDLSSPresetMaxIndex));
				ApplyCSMenuUpscalingTransition(
					upscaleMethod,
					IsRenderScaleModeRequested(),
					GetEffectiveUpscalingQualityMode(),
					dlssProfileOrder[dlssProfileUiIndex],
					"upscaling menu DLSS profile change");
			}

			if (auto _tt = Util::HoverTooltipWrapper()) {
				switch (effectiveDLSSPreset) {
				case 0:
					ImGui::Text("DLAA/Quality/Balanced preset. Slightly less ghosting than K, but more flicker. Speed: ~K. Use only if K ghosts.");
					break;
				case 1:
					ImGui::Text("Default for DLAA/Quality/Balanced. Best all-round stability and image quality. Speed: fast. Recommended for most users.");
					break;
				case 2:
					ImGui::Text("Default for Ultra Performance on newer RTX cards. Sharper and more stable, but higher cost than J/K/F.");
					ImGui::Text("For RTX 3000-series cards, F is usually the better Performance/Ultra Performance choice.");
					break;
				case 3:
					ImGui::Text("Default for Performance on newer RTX cards. Similar image-quality improvements to L, closer in speed to J/K.");
					ImGui::Text("For RTX 3000-series cards, F is usually the better Performance/Ultra Performance choice.");
					break;
				case 4:
					ImGui::Text("Intended for Ultra Performance/DLAA. Default preset for Ultra Performance.");
					ImGui::Text("Best Performance/Ultra Performance choice for RTX 3000-series cards.");
					break;
				default:
					ImGui::Text("Default for DLAA/Quality/Balanced. Best all-round stability and image quality. Speed: fast. Recommended for most users.");
					break;
				}
			}

			int dlssSharpenerMode = static_cast<int>(ClampDLSSSharpenerModeUInt(settings.dlssSharpener));
			if (ImGui::Combo("Sharpener", &dlssSharpenerMode, kDLSSSharpenerModeNames.data(), static_cast<int>(kDLSSSharpenerModeNames.size()))) {
				settings.dlssSharpener = ClampDLSSSharpenerModeUInt(static_cast<uint>(std::max(dlssSharpenerMode, 0)));
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Selects the post-DLSS sharpening pass.");
				ImGui::TextUnformatted("RCAS is punchier and more obvious, but can add shimmer or harsher edge contrast.");
				ImGui::TextUnformatted("Luma Unsharp is cleaner and more natural, preserving color while sharpening luminance.");
			}

			if (GetDLSSSharpenerMode() != DLSSSharpenerMode::Off) {
				ImGui::SliderFloat("Sharpness", &settings.sharpnessDLSS, 0.0f, 1.0f, "%.1f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted("Adjusts post-upscale sharpness for DLSS.");
					ImGui::TextUnformatted("Range: 0.0 off/softest to 1.0 strongest.");
				}
			}

			if (isNvidiaAdapter) {
				ImGui::TextWrapped("Note: Use K for DLAA/Quality/Balanced. For Performance and Ultra Performance, use L/M on newer RTX cards and F on RTX 3000-series cards.");
			}
		}

		if (globals::game::isVR) {
			SanitizeFoveatedSettings(settings);
			const bool foveatedDispatchSupportedForMethod = SupportsFoveatedVendorDispatch(upscaleMethod);
			if (foveatedDispatchSupportedForMethod) {
				const auto foveatedProfile = GetActiveUpscalingFoveatedProfile();
				const bool fovActive = foveatedProfile.available && FoveatedCommon::IsActiveCoverage(foveatedProfile.sharedVisibleScale);
				ImGui::TextDisabled("Configure foveated upscaling in VR > FOV.");
				ImGui::TextColored(
					fovActive ? ImVec4(0.40f, 0.85f, 0.50f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
					"FOV: %s",
					fovActive ? "active" : "inactive");
			} else {
				ImGui::TextDisabled(kFoveatedUpscalingMethodAvailabilityText);
			}
		}
	}

	const bool frameGenerationDx12PathActive = IsFrameGenerationDx12PathActive();

	if (!globals::game::isVR) {
		if (ImGui::TreeNodeEx("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Frame Generation interpolates real frames with generated ones for a smoother experience");
			ImGui::Text("Uses AMD FSR Frame Generation technology");
			if (HasFrameGenModule())
				ImGui::Text("AMD FSR Frame Generation is available.");
			ImGui::Text("Requires a D3D11 to D3D12 proxy which can create compatibility issues");
			ImGui::Text("Toggling this setting requires a restart to work correctly");

			bool onlyRequiresRestart = true;

			if (!isWindowed) {
				Util::Text::Warning("Warning: Requires windowed mode");

				onlyRequiresRestart = false;
			}

			if (lowRefreshRate && !settings.frameGenerationForceEnable) {
				Util::Text::Warning("Warning: Requires a high refresh rate monitor or Force Enable Frame Generation");

				onlyRequiresRestart = false;
			}

			if (settings.frameGenerationMode && !HasFrameGenModule()) {
				Util::Text::Warning("Warning: FidelityFX DLLs are not loaded");

				onlyRequiresRestart = false;
			}

			if (onlyRequiresRestart && settings.frameGenerationMode && !frameGenerationDx12PathActive)
				Util::Text::Warning("Warning: Requires restart");

			if (!settings.frameGenerationMode && frameGenerationDx12PathActive)
				Util::Text::Warning("Warning: Requires restart");

			std::string enabledLabel = "Enabled";
			const char* toggleModes[] = { "Disabled", "Enabled" };
			const char* toggleModesFG[] = { "Disabled", enabledLabel.c_str() };

			ImGui::SliderInt("Frame Generation", (int*)&settings.frameGenerationMode, 0, 1, toggleModesFG[settings.frameGenerationMode]);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Enables generated intermediate frames for higher apparent framerate.");
				ImGui::TextUnformatted("Range: 0 Disabled, 1 Enabled.");
			}

			if (!frameGenerationDx12PathActive)
				ImGui::BeginDisabled();

			ImGui::SliderInt("Frame Limit (Variable Refresh Rate)", (int*)&settings.frameLimitMode, 0, 1, std::format("{}", toggleModes[settings.frameLimitMode]).c_str());
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Applies VRR-aware frame limiting for smoother pacing with Frame Generation.");
				ImGui::TextUnformatted("Range: 0 Disabled, 1 Enabled.");
			}

			if (!frameGenerationDx12PathActive)
				ImGui::EndDisabled();

			ImGui::TextWrapped("Allows frame generation to function on low refresh rate monitors. Detected: %.2f Hz", refreshRate);
			ImGui::SliderInt("Force Enable Frame Generation", (int*)&settings.frameGenerationForceEnable, 0, 1, std::format("{}", toggleModes[settings.frameGenerationForceEnable]).c_str());
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Forces Frame Generation on unsupported/low-refresh setups.");
				ImGui::TextUnformatted("Range: 0 Disabled, 1 Enabled.");
			}

			ImGui::Checkbox("Frame Generation in Menus", &settings.frameGenerationAllowInMenus);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Keeps frame generation active while game menus are open.");
				ImGui::TextUnformatted("May feel smoother, but increases menu input latency.");
			}

			ImGui::TreePop();
		}
	}

	if (streamline.reflexSupportedOnCurrentAdapter && ImGui::TreeNodeEx("NVIDIA Reflex", ImGuiTreeNodeFlags_DefaultOpen)) {
		const bool reflexAvailable = streamline.initialized && streamline.featureReflex;
		const bool markerOptimizationAvailable = reflexAvailable && streamline.featurePCL;
		const bool reflexBlockedByFrameGeneration = IsFrameGenerationDx12PathActive();
		const char* toggleModes[] = { "Disabled", "Enabled" };

		if (!reflexAvailable) {
			ImGui::TextDisabled("Reflex is not available. Ensure sl.reflex.dll is present and restart.");
		}

		if (reflexBlockedByFrameGeneration) {
			ImGui::TextDisabled("Reflex is disabled while Frame Generation is active on the DX12 swap chain.");
		}

		if (!reflexAvailable || reflexBlockedByFrameGeneration)
			ImGui::BeginDisabled();

		int lowLatencyMode = settings.reflexLowLatencyMode ? 1 : 0;
		ImGui::SliderInt("Low Latency Mode", &lowLatencyMode, 0, 1, toggleModes[lowLatencyMode]);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Cuts input delay by syncing CPU work closer to the GPU.");
			ImGui::TextUnformatted("May reduce max FPS a little, but usually feels much more responsive.");
		}
		settings.reflexLowLatencyMode = lowLatencyMode > 0;

		if (!settings.reflexLowLatencyMode)
			ImGui::BeginDisabled();

		int lowLatencyBoost = settings.reflexLowLatencyBoost ? 1 : 0;
		ImGui::SliderInt("Low Latency Boost", &lowLatencyBoost, 0, 1, toggleModes[lowLatencyBoost]);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Keeps GPU clocks higher to avoid latency spikes at low GPU load.");
			ImGui::TextUnformatted("Useful if frametime jumps and responsiveness feels inconsistent.");
			ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "Increases power draw and heat, so leave Off unless needed.");
		}
		settings.reflexLowLatencyBoost = lowLatencyBoost > 0;

		if (!settings.reflexLowLatencyMode)
			ImGui::EndDisabled();

		if (!markerOptimizationAvailable)
			ImGui::BeginDisabled();

		int markersToOptimize = settings.reflexUseMarkersToOptimize ? 1 : 0;
		ImGui::SliderInt("Use Markers To Optimize", &markersToOptimize, 0, 1, toggleModes[markersToOptimize]);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses frame markers for tighter Reflex timing.");
			ImGui::TextUnformatted("Try On first; turn Off if it causes stutter on your setup.");
		}
		settings.reflexUseMarkersToOptimize = markersToOptimize > 0;

		if (!markerOptimizationAvailable)
			ImGui::EndDisabled();

		if (!markerOptimizationAvailable) {
			ImGui::TextDisabled("Marker optimization unavailable (PCL not loaded).");
		}

		int useFPSLimit = settings.reflexUseFPSLimit ? 1 : 0;
		ImGui::SliderInt("Use FPS Limit", &useFPSLimit, 0, 1, toggleModes[useFPSLimit]);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses Reflex's internal FPS cap for steadier frametimes.");
			ImGui::TextUnformatted("Can lower latency versus uncapped rendering.");
		}
		settings.reflexUseFPSLimit = useFPSLimit > 0;

		if (!settings.reflexUseFPSLimit)
			ImGui::BeginDisabled();

		if (!std::isfinite(settings.reflexFPSLimit))
			settings.reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
		ImGui::SliderFloat("FPS Limit", &settings.reflexFPSLimit, 20.0f, 240.0f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Set your frame cap target.");
			ImGui::TextUnformatted("Start about 2-3 FPS below refresh rate (e.g. 117 for 120 Hz).");
		}

		if (!settings.reflexUseFPSLimit)
			ImGui::EndDisabled();

		if (!reflexAvailable || reflexBlockedByFrameGeneration)
			ImGui::EndDisabled();

		ImGui::TreePop();
	}

	drawRenderPipelineBlock();

	if (ImGui::TreeNodeEx("Backend Diagnostics")) {
		// Streamline log level selection
		const char* logLevels[] = { "Off", "Default", "Verbose" };
		const auto logLevelMax = static_cast<uint>(IM_ARRAYSIZE(logLevels) - 1);
		int logLevelIdx = static_cast<int>(std::clamp(settings.streamlineLogLevel, 0u, logLevelMax));
		if (ImGui::Combo("Streamline Logging", &logLevelIdx, logLevels, IM_ARRAYSIZE(logLevels))) {
			settings.streamlineLogLevel = static_cast<uint>(logLevelIdx);
		}
		ImGui::TextUnformatted("Changing this requires a restart to take effect.");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Streamline logging controls the verbosity of NVIDIA Streamline backend logs. Useful for debugging issues with DLSS/DLSS-G.");
		}

		if (upscaleMethod == UpscaleMethod::kFSR) {
			ImGui::Separator();
			const bool showRuntimeFsrDiagnostics =
				settings.fsr4RuntimeEnable ||
				runtimeFsrPathRequested ||
				fidelityFX.HasRuntimeUpscalerSupportCheckResult();
			const bool runtimeFsr4EffectiveRequested =
				upscaleMethod == UpscaleMethod::kFSR &&
				fidelityFX.ShouldRequestRuntimeFsr4();
			const char* fsrModeLabel = settings.fsr4RuntimeEnable ?
			                               (runtimeFsr4EffectiveRequested ? "Runtime FSR 4.1 requested" :
																			(runtimeFsrPathRequested ? "Runtime FSR 3.1.5 fallback requested" : "Host FSR 3.1.5 fallback requested")) :
			                               (runtimeFsrPathRequested ? "Runtime FSR 3.1.5 requested" : "Host FSR 3.1.5 requested");
			ImGui::Text("AMD FSR Mode: %s", fsrModeLabel);
			ImGui::Text("Current Frame Path: %s", fidelityFX.GetRuntimeUpscalerLastFramePathLabel());
			if (showRuntimeFsrDiagnostics) {
				const bool supportKnown = fidelityFX.HasRuntimeUpscalerSupportCheckResult();
				const bool supportConfirmed = fidelityFX.IsRuntimeUpscalerSupportConfirmed();
				const bool runtimeFailureLatched = fidelityFX.IsRuntimeUpscalerFailureLatched();
				const bool runtimeFsr4FailureLatched = fidelityFX.IsRuntimeFsr4FailureLatched();
				const std::string requestedVersion = fidelityFX.GetRuntimeUpscalerRequestedVersionString();
				const std::string providerName = fidelityFX.GetRuntimeUpscalerProviderName();
				const bool providerMismatch =
					supportKnown &&
					supportConfirmed &&
					!providerName.empty() &&
					!fidelityFX.IsRuntimeUpscalerProviderMatchingRequestedVersion();
				const auto getRuntimePathSupportLabel = [&]() -> const char* {
					if (!runtimeUpscalerPresent)
						return "Unavailable (missing runtime)";
					if (!runtimeFsrPathRequested && settings.fsr4RuntimeEnable)
						return "Unavailable for adapter";
					if (runtimeFailureLatched)
						return "Unavailable (latched fallback)";
					if (runtimeFsr4FailureLatched)
						return "Available (FSR 4.1 fallback latched)";
					if (!supportKnown)
						return "Pending";
					if (supportConfirmed && providerMismatch)
						return "Available (provider fallback)";
					return supportConfirmed ? "Available" : "Unavailable";
				};
				std::string providerDisplay = providerName.empty() ? "(not reported by SDK)" : providerName;
				if (providerMismatch)
					providerDisplay += " (requested version unavailable)";
				ImGui::Text("Runtime Path Support: %s", getRuntimePathSupportLabel());
				ImGui::Text("Failure Latch: %s", runtimeFailureLatched ? "Active" : "Clear");
				ImGui::Text("Runtime Requested FSR Version: %s", requestedVersion.c_str());
				ImGui::Text("Runtime Provider: %s", providerDisplay.c_str());
			}
		}

		// VR Debug visualization -- per-eye buffers and native inputs
		if (globals::game::isVR) {
			ImGui::Separator();
			static float debugRescale = 0.15f;
			ImGui::SliderFloat("View Resize", &debugRescale, 0.05f, 1.f);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Scales debug buffer previews in the diagnostics panel.");
				ImGui::TextUnformatted("Range: low 0.05 (small previews) to high 1.00 (full-size previews).");
			}

			if (ImGui::TreeNode("Upscaling Intermediates")) {
				if (vrIntermediateMotionVectors[0]) {
					bool isDLSS = GetRuntimeUpscaleMethod() == UpscaleMethod::kDLSS;
					if (vrIntermediateColorIn[0] && vrIntermediateColorOut[0]) {
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorIn[0], "Left Eye In", debugRescale)
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorIn[1], "Right Eye In", debugRescale)
						if (!isDLSS)
							BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorOut[0], "Left Eye Out", debugRescale)
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorOut[1], "Right Eye Out", debugRescale)
					}
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateMotionVectors[0], "Left Eye MVec", debugRescale)
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateMotionVectors[1], "Right Eye MVec", debugRescale)
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateReactiveMask[0], "Left Eye Reactive", debugRescale)
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateReactiveMask[1], "Right Eye Reactive", debugRescale)
					if (vrIntermediateTransparencyMask[0]) {
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateTransparencyMask[0], "Left Eye Transparency", debugRescale)
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateTransparencyMask[1], "Right Eye Transparency", debugRescale)
					}
				} else {
					ImGui::TextDisabled("VR intermediates not yet created (enter game world)");
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Native Inputs")) {
				auto renderer = globals::game::renderer;
				auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
				auto& mvec = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
				auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

				auto DisplayRT = [&](const char* label, ID3D11Texture2D* tex, ID3D11ShaderResourceView* srv) {
					if (srv && tex) {
						D3D11_TEXTURE2D_DESC desc;
						tex->GetDesc(&desc);
						char buf[128];
						snprintf(buf, sizeof(buf), "%s (%ux%u)", label, desc.Width, desc.Height);
						if (ImGui::TreeNode(buf)) {
							ImGui::Image(srv, { desc.Width * debugRescale, desc.Height * debugRescale });
							ImGui::TreePop();
						}
					}
				};

				DisplayRT("kMAIN (Color Input)", (ID3D11Texture2D*)main.texture, (ID3D11ShaderResourceView*)main.SRV);
				DisplayRT("Motion Vectors", (ID3D11Texture2D*)mvec.texture, (ID3D11ShaderResourceView*)mvec.SRV);
				DisplayRT("Depth", depth.texture, depth.depthSRV);

				if (reactiveMaskTexture)
					BUFFER_VIEWER_NODE_TITLE(reactiveMaskTexture, "Reactive Mask", debugRescale)
				if (transparencyCompositionMaskTexture)
					BUFFER_VIEWER_NODE_TITLE(transparencyCompositionMaskTexture, "Transparency Mask", debugRescale)

				ImGui::TreePop();
			}
		}

		ImGui::Separator();
		Util::DrawDllVersionTable("AMD FidelityFX DLLs (click to open folder)", FidelityFX::PluginDir, FidelityFX::dllVersions, "ffx_dll_versions");
		Util::DrawDllVersionTable("NVIDIA Streamline DLLs (click to open folder)", Streamline::PluginDir, Streamline::dllVersions, "sl_dll_versions");
		ImGui::TreePop();
	}

	if (resourceSettingsKeyBefore != BuildUpscalingResourceMutationSettingsKey(settings))
		InvalidateFrameScopedUpscalingState();
}

void Upscaling::DrawFoveatedSetupInstructions()
{
	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	const bool showFovSetupInstructions = ImGui::CollapsingHeader("Upscaling FOV Setup Instructions");
	if (showFovSetupInstructions) {
		const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
		const float availableHeight = ImGui::GetContentRegionAvail().y;
		const float instructionHeight = std::clamp(availableHeight - (lineHeight * 2.0f), lineHeight * 5.0f, lineHeight * 14.0f);
		ImGui::BeginChild("##UpscalingFOVSetupInstructions", ImVec2(0.0f, instructionHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::PushTextWrapPos(0.0f);
		auto drawInstructionHeadline = [](const char* a_label) {
			MenuFonts::FontRoleGuard headingFont(Menu::FontRole::Subheading);
			ImGui::SeparatorText(a_label);
		};
		ImGui::TextUnformatted(kFoveatedUpscalingSetupIntro);
		ImGui::Spacing();
		drawInstructionHeadline("Upscaling FOV setup");
		ImGui::TextUnformatted(kFoveatedUpscalingSetupInstructions);
		ImGui::Spacing();
		drawInstructionHeadline("Upscaling FOV + TAA setup");
		ImGui::TextUnformatted(kFoveatedUpscalingPeripheralTaaSetupInstructions);
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
	}
}

void Upscaling::DrawFoveatedSettings()
{
	if (!globals::game::isVR) {
		ImGui::TextDisabled("VR FOV mask setup is available only in VR.");
		return;
	}
	if (!loaded) {
		ImGui::TextDisabled("VR FOV mask setup requires Upscaling.");
		return;
	}

	const uint64_t resourceSettingsKeyBefore = BuildUpscalingResourceMutationSettingsKey(settings);
	SanitizeFoveatedSettings(settings);
	const UpscaleMethod upscaleMethod = GetUpscaleMethod();
	const bool foveatedDispatchSupportedForMethod = SupportsFoveatedVendorDispatch(upscaleMethod);

	if (foveatedDispatchSupportedForMethod) {
		{
			Util::BlueFrameStyleWrapper foveatedStyle(true);
			ImGui::Checkbox("Foveated Upscaling (FOV)", &settings.foveatedVendorDispatch);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Master switch for VR FOV-mask upscaling.");
			ImGui::TextUnformatted("On: enables foveated upscaling controls and the shared FOV mask used by VR foveated effects.");
		}
	} else {
		ImGui::TextDisabled(kFoveatedUpscalingMethodAvailabilityText);
	}

	const bool foveatedDispatchRequestedForMethod = IsFoveatedVendorDispatchRequested(settings, upscaleMethod);
	auto drawInactiveSavedFoveatedProfile = [&](const char* a_reason) {
		const float savedFovOnlyVisibleScale = ClampFoveatedCenterScale(settings.foveatedCenterArea);
		const float savedTaaCenterScale = ClampFoveatedCenterScale(settings.periphery_taa_center_area);
		const float savedTaaVisibleScale = ClampPeripheryTAAOuterScaleForCenter(settings.periphery_taa_outer_scale, savedTaaCenterScale);
		if (settings.periphery_taa_enable) {
			ImGui::TextDisabled(
				"Saved FOV profile inactive (%s): FOV + TAA center %.2f, visible %.2f.",
				a_reason,
				savedTaaCenterScale,
				savedTaaVisibleScale);
		} else {
			ImGui::TextDisabled(
				"Saved FOV profile inactive (%s): FOV only visible %.2f.",
				a_reason,
				savedFovOnlyVisibleScale);
		}
	};
	if (!foveatedDispatchRequestedForMethod) {
		drawInactiveSavedFoveatedProfile(foveatedDispatchSupportedForMethod ? "enable Foveated Upscaling (FOV)" : "requires DLSS or FSR");
		if (resourceSettingsKeyBefore != BuildUpscalingResourceMutationSettingsKey(settings))
			InvalidateFrameScopedUpscalingState();
		return;
	}

	auto drawActiveFoveatedProfileStatus = [&]() {
		const auto activeFoveatedProfile = GetActiveUpscalingFoveatedProfile();
		if (activeFoveatedProfile.available && activeFoveatedProfile.mode == FoveatedUpscalingMode::PeripheralTAA) {
			ImGui::Text(
				"Active FOV mode: %s, center %.2f, visible %.2f",
				GetFoveatedUpscalingModeName(activeFoveatedProfile.mode),
				activeFoveatedProfile.vendorCenterScale,
				activeFoveatedProfile.sharedVisibleScale);
		} else if (activeFoveatedProfile.available) {
			ImGui::Text(
				"Active FOV mode: %s, visible %.2f",
				GetFoveatedUpscalingModeName(activeFoveatedProfile.mode),
				activeFoveatedProfile.sharedVisibleScale);
		} else {
			ImGui::TextDisabled("Active FOV mode: Off (full visible coverage).");
		}
	};

	if (IsDefaultFoveatedMaskGeometry(settings)) {
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.05f, 1.0f), "Default FOV mask active. Tune it for your HMD for best image and performance.");
	}

	{
		Util::BlueFrameStyleWrapper maskStyle(true);
		ImGui::Checkbox("FOV Mask Visualization", &settings.foveatedPeripheryMaskVisualization);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Use this while tuning FOV masks.");
		ImGui::TextUnformatted("Green = upscaling center mask.");
		if (settings.periphery_taa_enable)
			ImGui::TextUnformatted("Gold = TAA ring, blue = outer lightweight ring.");
		else
			ImGui::TextUnformatted("Dark = outside the upscaling FOV mask.");
	}

	ImGui::Dummy(ImVec2(0.0f, 6.0f));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	ImGui::TextUnformatted("Upscaling FOV Controls");

	{
		Util::BlueFrameStyleWrapper areaStyle;
		auto areaGuard = Util::DisableGuard(settings.periphery_taa_enable);
		ImGui::SliderFloat("FOV Only Visible Scale", &settings.foveatedCenterArea, FoveatedCommon::kCenterScaleMin, FoveatedCommon::kCenterScaleMax, "%.2f");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (settings.periphery_taa_enable) {
			ImGui::TextUnformatted("Inactive while FOV + TAA is enabled.");
			ImGui::TextUnformatted("This saved value is kept for the FOV-only profile.");
		} else {
			ImGui::TextUnformatted("Defines the visible/protected HMD area for FOV-only mode.");
			ImGui::TextUnformatted("Lower values = smaller visible area and more performance.");
			ImGui::TextUnformatted("Range: low 0.25 (smallest visible area) to high 1.00 (full visible area).");
		}
	}
	settings.foveatedCenterArea = ClampFoveatedCenterScale(settings.foveatedCenterArea);

	{
		Util::BlueFrameStyleWrapper baseExpandStyle;
		ImGui::SliderFloat("Expand FOV Scale R/L", &settings.foveatedCenterHorizontalScale, FoveatedCommon::kCenterHorizontalScaleMin, FoveatedCommon::kCenterHorizontalScaleMax, "%.2f");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Widens the upscaling center mask horizontally.");
		if (settings.periphery_taa_enable)
			ImGui::TextUnformatted("FOV + TAA uses this shared horizontal expansion.");
		ImGui::TextUnformatted("Range: low 1.00 (no extra width) to high 2.00 (maximum extra width).");
	}

	auto drawEyeOffsetTooltip = [&](const char* eye, const char* axis, const char* direction) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (settings.periphery_taa_enable)
				ImGui::Text("%s-eye %s offset shared by upscaling and FOV + TAA.", eye, axis);
			else
				ImGui::Text("%s-eye %s offset for the upscaling center mask.", eye, axis);
			ImGui::TextUnformatted(direction);
		}
	};
	{
		Util::BlueFrameStyleWrapper baseOffsetStyle;
		ImGui::SliderFloat("FOV Left Eye Offset X", &settings.foveatedLeftEyeMaskOffsetX, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax, "%.3f");
		drawEyeOffsetTooltip("Left", "horizontal", "+X moves right, -X moves left.");
		ImGui::SliderFloat("FOV Left Eye Offset Y", &settings.foveatedLeftEyeMaskOffsetY, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax, "%.3f");
		drawEyeOffsetTooltip("Left", "vertical", "+Y moves down, -Y moves up.");
		ImGui::SliderFloat("FOV Right Eye Offset X", &settings.foveatedRightEyeMaskOffsetX, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax, "%.3f");
		drawEyeOffsetTooltip("Right", "horizontal", "+X moves right, -X moves left.");
		ImGui::SliderFloat("FOV Right Eye Offset Y", &settings.foveatedRightEyeMaskOffsetY, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax, "%.3f");
		drawEyeOffsetTooltip("Right", "vertical", "+Y moves down, -Y moves up.");
	}

	settings.foveatedCenterHorizontalScale = ClampFoveatedCenterHorizontalScale(settings.foveatedCenterHorizontalScale);
	settings.foveatedLeftEyeMaskOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetX);
	settings.foveatedLeftEyeMaskOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetY);
	settings.foveatedRightEyeMaskOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetX);
	settings.foveatedRightEyeMaskOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetY);

	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	ImGui::Separator();
	ImGui::TextUnformatted("Upscaling FOV + TAA Settings");
	{
		Util::YellowFrameStyleWrapper taaStyle(true);
		ImGui::Checkbox("FOV + TAA", &settings.periphery_taa_enable);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Enables periphery-only TAA outside the smaller vendor center region.");
		ImGui::TextUnformatted("When ON, the FOV + TAA center scale becomes the active DLSS/FSR dispatch center.");
		ImGui::TextUnformatted("The visible outer scale defines the shared HMD-visible mask boundary.");
		ImGui::TextUnformatted("Expand and eye offsets are shared with the upscaling controls above.");
	}
	ImGui::BeginDisabled(!settings.periphery_taa_enable);
	if (!settings.periphery_taa_enable)
		ImGui::TextDisabled("Enable FOV + TAA to edit the center scale, transition, and visible outer scale.");
	{
		Util::YellowFrameStyleWrapper taaAreaStyle;
		ImGui::SliderFloat("FOV + TAA Center Scale", &settings.periphery_taa_center_area, FoveatedCommon::kCenterScaleMin, FoveatedCommon::kCenterScaleMax, "%.2f");
	}
	if (settings.periphery_taa_enable) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Actual DLSS/FSR center dispatch size for FOV + TAA.");
			ImGui::TextUnformatted("Lower values = smaller vendor center and more TAA ring coverage.");
			ImGui::TextUnformatted("Range: low 0.25 (smallest center) to high 1.00 (largest center).");
		}
	}
	settings.periphery_taa_center_area = ClampFoveatedCenterScale(settings.periphery_taa_center_area);
	{
		Util::YellowFrameStyleWrapper transitionStyle;
		ImGui::SliderFloat(
			"Center Blend/TAA Transition",
			&settings.periphery_taa_center_blend_feather,
			kPeripheryTAACenterBlendFeatherMin,
			kPeripheryTAACenterBlendFeatherMax,
			"%.3f");
	}
	if (settings.periphery_taa_enable) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls softness of the center-to-TAA transition edge.");
			ImGui::TextUnformatted("Lower = harder edge, higher = softer edge.");
			ImGui::Text("Range: low %.2f (harder transition) to high %.2f (softer transition).", kPeripheryTAACenterBlendFeatherMin, kPeripheryTAACenterBlendFeatherMax);
		}
	}
	settings.periphery_taa_center_blend_feather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);
	const float taaOuterRangeMin = GetPeripheryTAAOuterScaleFloor(settings.periphery_taa_center_area);
	{
		Util::YellowFrameStyleWrapper taaRangeStyle;
		ImGui::SliderFloat(
			"FOV + TAA Visible Outer Scale",
			&settings.periphery_taa_outer_scale,
			taaOuterRangeMin,
			kPeripheryTAAOuterScaleMax,
			"%.2f");
	}
	if (settings.periphery_taa_enable) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls the shared HMD-visible boundary for FOV + TAA.");
			ImGui::Text("Range: low %.2f (minimum allowed by current center scale) to high %.2f (full range).", taaOuterRangeMin, kPeripheryTAAOuterScaleMax);
			ImGui::TextUnformatted("Lower values are faster.");
			ImGui::TextUnformatted("Increase until the gold ring reaches the edge of your visible field of view.");
		}
	}
	ImGui::EndDisabled();

	settings.periphery_taa_outer_scale = ClampPeripheryTAAOuterScaleForCenter(
		settings.periphery_taa_outer_scale,
		settings.periphery_taa_center_area);

	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	drawActiveFoveatedProfileStatus();

	if (resourceSettingsKeyBefore != BuildUpscalingResourceMutationSettingsKey(settings))
		InvalidateFrameScopedUpscalingState();
}

const Upscaling::OpenCompositeUpscalingBlocker& Upscaling::GetOpenCompositeUpscalingBlocker(bool a_forceRefresh) const
{
	const ULONGLONG now = GetTickCount64();

	if (!a_forceRefresh && openCompositeUpscalingBlockerCacheValid) {
		return openCompositeUpscalingBlocker;
	}

	const auto detectedBlocker = FindOpenCompositeUpscalingBlocker();
	openCompositeUpscalingBlocker.active = detectedBlocker.active;
	openCompositeUpscalingBlocker.settingName = detectedBlocker.settingName;
	openCompositeUpscalingBlocker.configPath = detectedBlocker.configPath;
	openCompositeUpscalingBlockerCacheValid = true;
	openCompositeUpscalingBlockerLastRefresh = now;

	return openCompositeUpscalingBlocker;
}

void Upscaling::ApplyOpenCompositeUpscalingBlocker(bool a_forceRefresh)
{
	const auto& blocker = GetOpenCompositeUpscalingBlocker(a_forceRefresh);
	if (!blocker.active)
		return;

	if (settings.upscaleMethod != static_cast<uint>(UpscaleMethod::kNONE) ||
		settings.upscaleMethodNoDLSS != static_cast<uint>(UpscaleMethod::kNONE)) {
		if (blocker.configPath.empty()) {
			logger::warn(
				"[Upscaling] Forcing Community Shaders Upscaling to None because Open Composite has {}=true.",
				blocker.settingName);
		} else {
			logger::warn(
				"[Upscaling] Forcing Community Shaders Upscaling to None because Open Composite has {}=true in {}.",
				blocker.settingName,
				blocker.configPath);
		}
	}

	const bool changedUpscaleMode =
		settings.upscaleMethod != static_cast<uint>(UpscaleMethod::kNONE) ||
		settings.upscaleMethodNoDLSS != static_cast<uint>(UpscaleMethod::kNONE);
	settings.upscaleMethod = static_cast<uint>(UpscaleMethod::kNONE);
	settings.upscaleMethodNoDLSS = static_cast<uint>(UpscaleMethod::kNONE);
	if (changedUpscaleMode)
		InvalidateFrameScopedUpscalingState();
}

void Upscaling::SaveSettings(json& o_json)
{
	ApplyOpenCompositeUpscalingBlocker(true);
	SanitizeUpscalingSettings(settings);
	o_json = settings;
	o_json["qualityModeSchemaVersion"] = 2;
	if (IsVRRuntimeActive()) {
		o_json.erase("perfMode");
	}
	if (!IsVRRuntimeActive()) {
		StripVRSpecificUpscalingSettings(o_json);
	}
	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		if (auto setting = iniSettingCollection->GetSetting("bUseTAA:Display"))
			iniSettingCollection->WriteSetting(setting);
	}
}

void Upscaling::LoadSettings(json& o_json)
{
	const Settings previousSettings = settings;
	const bool hasQualityModeSchemaVersion = o_json.contains("qualityModeSchemaVersion");
	const bool hasRenderScaleModeSetting = o_json.contains("renderScaleMode");
	const bool hasLegacyPerfModeSetting = o_json.contains("perfMode");
	const bool hasLegacySettings = o_json.is_object() && !o_json.empty();
	settings = o_json;
	if (!hasRenderScaleModeSetting && hasLegacyPerfModeSetting) {
		try {
			settings.renderScaleMode = o_json.at("perfMode").get<uint>();
		} catch (...) {
			logger::warn("[Upscaling] Loaded legacy perfMode setting could not be migrated; using VR Render Scale Mode default.");
		}
	} else if (!hasRenderScaleModeSetting && hasLegacySettings) {
		// Pre-render-scale configs may still carry non-native quality presets for
		// regular vendor upscaling. Do not infer VR Render Scale Mode from quality alone.
		settings.renderScaleMode = 0u;
	}
	if (!hasQualityModeSchemaVersion) {
		settings.qualityMode = MigrateLegacyQualityModeUInt(settings.qualityMode);
	}
	if (!IsVRRuntimeActive()) {
		ResetVRSpecificUpscalingSettings(settings);
	}
	// Force mask visualization OFF on load for all existing profiles.
	settings.foveatedPeripheryMaskVisualization = false;

	if (settings.upscaleMethod > static_cast<uint>(UpscaleMethod::kDLSS)) {
		logger::warn("[Upscaling] Loaded upscaleMethod {} out of range, clamping to {}", settings.upscaleMethod, static_cast<uint>(UpscaleMethod::kDLSS));
	}
	if (settings.upscaleMethodNoDLSS > static_cast<uint>(UpscaleMethod::kFSR)) {
		logger::warn("[Upscaling] Loaded upscaleMethodNoDLSS {} out of range, clamping to {}", settings.upscaleMethodNoDLSS, static_cast<uint>(UpscaleMethod::kFSR));
	}
	SanitizeUpscalingSettings(settings);
	ApplyOpenCompositeUpscalingBlocker(true);
	const float originalReflexFPSLimit = settings.reflexFPSLimit;
	if (!std::isfinite(settings.reflexFPSLimit)) {
		settings.reflexFPSLimit = 60.0f;
		logger::warn(
			"[Upscaling] Loaded reflexFPSLimit {} is not finite, resetting to {}",
			originalReflexFPSLimit,
			settings.reflexFPSLimit);
	}
	const float clampedReflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
	if (clampedReflexFPSLimit != settings.reflexFPSLimit) {
		logger::warn(
			"[Upscaling] Loaded reflexFPSLimit {} out of range, clamping to {}",
			settings.reflexFPSLimit,
			clampedReflexFPSLimit);
	}
	settings.reflexFPSLimit = clampedReflexFPSLimit;

	const bool runtimeReady =
		globals::game::isVR &&
		globals::d3d::device &&
		globals::game::renderer &&
		globals::state;
	const uint previousUpscaleMethod = streamline.featureDLSS ? previousSettings.upscaleMethod : previousSettings.upscaleMethodNoDLSS;
	const uint currentUpscaleMethod = streamline.featureDLSS ? settings.upscaleMethod : settings.upscaleMethodNoDLSS;
	const bool perfModeRelevantSettingChanged =
		ClampToggleUInt(previousSettings.perfMode) != ClampToggleUInt(settings.perfMode) ||
		ClampQualityModeUInt(previousSettings.qualityMode) != ClampQualityModeUInt(settings.qualityMode) ||
		previousUpscaleMethod != currentUpscaleMethod;
	if (runtimeReady && perfModeRelevantSettingChanged)
		RequestPerfModeRenderTargetRecreate("upscaling settings reload");
	InvalidateFrameScopedUpscalingState();

	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		if (auto setting = iniSettingCollection->GetSetting("bUseTAA:Display"))
			iniSettingCollection->ReadSetting(setting);
	}
}

void Upscaling::RestoreDefaultSettings()
{
	settings = {};
	settings.foveatedVendorDispatch = false;
	settings.foveatedPeripheryMaskVisualization = false;
	settings.reflexLowLatencyMode = true;
	settings.reflexUseMarkersToOptimize = true;
	settings.reflexLowLatencyBoost = false;
	settings.reflexUseFPSLimit = false;
	SanitizeUpscalingSettings(settings);
	ApplyOpenCompositeUpscalingBlocker(true);
	InvalidateFrameScopedUpscalingState();
}

struct BSOpenVR_GetRenderTargetSize
{
	static void thunk(RE::BSOpenVR* a_this, std::uint32_t* a_width, std::uint32_t* a_height)
	{
		func(a_this, a_width, a_height);

		if (!a_width || !a_height || !*a_width || !*a_height)
			return;

		const uint32_t trueEyeWidth = *a_width;
		const uint32_t trueEyeHeight = *a_height;

		auto& upscaling = globals::features::upscaling;
		upscaling.RecordTrueHMDRenderTargetSize(trueEyeWidth, trueEyeHeight);

		uint32_t perfModeWidth = trueEyeWidth;
		uint32_t perfModeHeight = trueEyeHeight;
		const bool allowPerfModeBootLatchCreate =
			CanActivateVRRenderScaleRuntime(upscaling) &&
			!ShouldBlockForPendingExplicitVRUpscalingWork(upscaling) &&
			upscaling.ConsumePerfModeBootLatchCreate();
		if (upscaling.TryGetPerfModeOpenVRRenderTargetSize(perfModeWidth, perfModeHeight, allowPerfModeBootLatchCreate)) {
			*a_width = perfModeWidth;
			*a_height = perfModeHeight;
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::DataLoaded()
{
	DisableAutoDynamicResolutionSetting();
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		logger::warn("[Upscaling] Skipping data-loaded upscaling adjustments because Open Composite has {}=true.", blocker.settingName);
		return;
	}
	if (IsRenderDocUpscalingBlocked(true)) {
		logger::warn(
			"[Upscaling] Skipping data-loaded upscaling adjustments because {}.",
			GetRenderDocUpscalingBlockReason());
		return;
	}

	// Fix screenshots fix from Engine Fixes
	RE::GetINISetting("bUseTAA:Display")->data.b = false;

	// The game defaults this to a non-zero value
	static auto fDRClampOffset = RE::GetINISetting("fDRClampOffset:Display");
	fDRClampOffset->data.f = 0.0f;

	// VR + DLSS workaround: loading transitions need a temporal reset, but full
	// DLSS resource rebuilds on every door load can flicker and stress the driver.
	if (globals::game::isVR)
		MenuOpenCloseEventHandler::Register();
}

RE::BSEventNotifyControl Upscaling::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && IsVRMenuPresentationTailMenuName(a_event->menuName)) {
		ExtendVRMenuPresentationTail();
		ExtendVRMenuBridgeTraceTail();
	}

	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		g_vrLoadingMenuOpenFromEvent.store(a_event->opening, std::memory_order_relaxed);
		if (a_event->opening) {
			g_vrLoadingTransitionCloseFrame.store(0, std::memory_order_release);
			g_vrLoadingTransitionTailEndFrame.store(0, std::memory_order_release);
			ResetVRMenuPresentationTrackingState();
			globals::features::upscaling.pendingVRFpsStabilizerSyncFrame.store(0, std::memory_order_release);
		} else if (globals::state) {
			const uint32_t currentFrame = std::max(globals::state->frameCount, 1u);
			g_vrLoadingTransitionCloseFrame.store(currentFrame, std::memory_order_release);
			g_vrLoadingTransitionTailEndFrame.store(
				currentFrame + kVRSaveLoadTransitionTailFrames,
				std::memory_order_release);
		}
		if (!a_event->opening) {
			QueueVendorRuntimeResetAfterLoadingMenu(globals::features::upscaling);
			if (globals::state && IsSaveLoadTransitionContextActive(globals::state))
				globals::features::upscaling.QueueVRFpsStabilizerLoadSync(globals::state->frameCount);
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

bool Upscaling::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	static std::atomic<bool> registered{ false };

	if (registered.load(std::memory_order_acquire))
		return true;

	auto ui = globals::game::ui;
	if (!ui) {
		logger::error("[Upscaling] UI event source not found; DLSS history reset-on-load disabled");
		return false;
	}

	auto eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!eventSource) {
		logger::error("[Upscaling] MenuOpenCloseEvent source not found; DLSS history reset-on-load disabled");
		return false;
	}

	g_vrLoadingMenuOpenFromEvent.store(ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME), std::memory_order_relaxed);
	eventSource->AddEventSink(&singleton);
	registered.store(true, std::memory_order_release);
	logger::info("[Upscaling] Registered MenuOpenCloseEventHandler for DLSS history reset-on-load");
	return true;
}

void Upscaling::Load()
{
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		logger::warn("[Upscaling] Skipping D3D11 device hook because Open Composite has {}=true.", blocker.settingName);
		return;
	}
	if (IsRenderDocUpscalingBlocked(true)) {
		logger::warn(
			"[Upscaling] Skipping D3D11 device hook because {}.",
			GetRenderDocUpscalingBlockReason());
		return;
	}

	if (REL::Module::IsVR()) {
		stl::write_vfunc<0x12, BSOpenVR_GetRenderTargetSize>(RE::VTABLE_BSOpenVR[0]);
	}

	*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChainUpscaling = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChainUpscaling, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
}

struct BSImageSpace_Init_FXAA
{
	static void thunk()
	{
		func();

		// Force FXAA off safely
		auto fxaaEnabled = reinterpret_cast<bool*>(REL::RelocationID(513281, 391028).address());
		*fxaaEnabled = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

#ifdef TRACY_ENABLE
namespace
{
	constexpr tracy::SourceLocationData kSSRReflectionsRayTracingZone{
		"SSR ReflectionsRayTracing",
		"ISReflectionsRayTracing::Render",
		__FILE__,
		static_cast<uint32_t>(__LINE__),
		0
	};

	alignas(tracy::D3D11ZoneScope) unsigned char g_ssrReflectionsRayTracingZoneStorage[sizeof(tracy::D3D11ZoneScope)];
	bool g_ssrReflectionsRayTracingZoneOpen = false;

	tracy::D3D11ZoneScope* GetSSRReflectionsRayTracingZone()
	{
		return reinterpret_cast<tracy::D3D11ZoneScope*>(&g_ssrReflectionsRayTracingZoneStorage);
	}

	void CloseSSRReflectionsRayTracingZone()
	{
		if (!g_ssrReflectionsRayTracingZoneOpen)
			return;

		GetSSRReflectionsRayTracingZone()->~D3D11ZoneScope();
		g_ssrReflectionsRayTracingZoneOpen = false;
	}

	void OpenSSRReflectionsRayTracingZone()
	{
		auto state = globals::state;
		if (!state || !state->tracyCtx)
			return;

		CloseSSRReflectionsRayTracingZone();
		new (GetSSRReflectionsRayTracingZone()) tracy::D3D11ZoneScope(state->tracyCtx, &kSSRReflectionsRayTracingZone, true);
		g_ssrReflectionsRayTracingZoneOpen = true;
	}
}

struct SSRReflectionsRayTracingPreRenderHook
{
	static void thunk(void* a_this)
	{
		func(a_this);
		OpenSSRReflectionsRayTracingZone();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct SSRReflectionsRayTracingPostRenderHook
{
	static void thunk(void* a_this)
	{
		CloseSSRReflectionsRayTracingZone();
		func(a_this);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};
#endif

void Upscaling::PostPostLoad()
{
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		logger::warn("[Upscaling] Skipping upscaling render hooks because Open Composite has {}=true.", blocker.settingName);
		return;
	}
	if (IsRenderDocUpscalingBlocked(true)) {
		logger::warn(
			"[Upscaling] Skipping upscaling render hooks because {}.",
			GetRenderDocUpscalingBlockReason());
		return;
	}

	bool isGOG = !GetModuleHandle(L"steam_api64.dll");
	stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

	// Calculates resolution and jitter
	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));

	// Performs upscaling in between volumetric lighting and post processing
	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7, 0x206));

	// Patches RSSetScissorRect calls to use dynamic resolution
	// This is a PC-specific function hence it was missing
	if (!globals::game::isVR)
		stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	// Patches facegen texture generation to not use dynamic resolution
	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	// Patches precipitation camera to not use dynamic resolution
	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1, 0x2FA));

	// Forces FXAA off
	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

#ifdef TRACY_ENABLE
	// ReflectionsRayTracing renders through BSImagespaceShader::Render, so PreRender/PostRender
	// bracket the SSR draw on SE/AE/VR without per-version callsite addresses.
	stl::write_vfunc<0x0A, SSRReflectionsRayTracingPreRenderHook>(RE::VTABLE_BSImagespaceShaderReflectionsRayTracing[0]);
	stl::write_vfunc<0x0B, SSRReflectionsRayTracingPostRenderHook>(RE::VTABLE_BSImagespaceShaderReflectionsRayTracing[0]);
	logger::info("[Upscaling] Installed SSR ReflectionsRayTracing Tracy zone");
#endif

	logger::info("[Upscaling] Installed hooks");
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
{
	if (GetOpenCompositeUpscalingBlocker().active)
		return UpscaleMethod::kNONE;
	if (IsRenderDocUpscalingBlocked())
		return UpscaleMethod::kNONE;

	if (streamline.featureDLSS)
		return (UpscaleMethod)settings.upscaleMethod;
	return (UpscaleMethod)settings.upscaleMethodNoDLSS;
}

Upscaling::UpscaleMethod Upscaling::GetConfiguredUpscaleMethodForTransition() const
{
	if (GetOpenCompositeUpscalingBlocker().active)
		return UpscaleMethod::kNONE;
	if (IsRenderDocUpscalingBlocked())
		return UpscaleMethod::kNONE;

	const auto primaryMethod = ClampUpscaleMethod(settings.upscaleMethod, UpscaleMethod::kDLSS);
	if (primaryMethod != UpscaleMethod::kDLSS)
		return primaryMethod;

	if (streamline.featureDLSS || !streamline.featureCheckComplete)
		return UpscaleMethod::kDLSS;

	return ClampUpscaleMethod(settings.upscaleMethodNoDLSS, UpscaleMethod::kFSR);
}

Upscaling::UpscaleMethod Upscaling::GetLegacyDLSSPreferredUpscaleMethodForAPI() const
{
	if (GetOpenCompositeUpscalingBlocker().active)
		return UpscaleMethod::kNONE;
	if (IsRenderDocUpscalingBlocked())
		return UpscaleMethod::kNONE;

	if (streamline.featureDLSS || !streamline.featureCheckComplete)
		return UpscaleMethod::kDLSS;

	return ClampUpscaleMethod(settings.upscaleMethodNoDLSS, UpscaleMethod::kFSR);
}

Upscaling::UpscaleMethod Upscaling::GetRuntimeUpscaleMethod() const
{
	const auto requestedMethod = GetUpscaleMethod();
	if (GetOpenCompositeUpscalingBlocker().active)
		return requestedMethod;
	if (IsRenderDocUpscalingBlocked())
		return requestedMethod;

	const auto& boot = perfMode.GetBootSnapshot();
	if (IsVRRenderScaleModeLatched() && IsVendorUpscalingMethod(boot.method))
		return boot.method;

	return requestedMethod;
}

uint32_t Upscaling::GetRuntimeQualityMode() const
{
	if (GetOpenCompositeUpscalingBlocker().active)
		return ClampQualityModeUInt(settings.qualityMode);
	if (IsRenderDocUpscalingBlocked())
		return ClampQualityModeUInt(settings.qualityMode);

	const auto& boot = perfMode.GetBootSnapshot();
	if (IsVRRenderScaleModeLatched())
		return ClampQualityModeUInt(boot.qualityMode);

	return ClampQualityModeUInt(settings.qualityMode);
}

Upscaling::DLSSSharpenerMode Upscaling::GetDLSSSharpenerMode() const
{
	return static_cast<DLSSSharpenerMode>(ClampDLSSSharpenerModeUInt(settings.dlssSharpener));
}

bool Upscaling::ShouldApplyDLSSSharpening() const
{
	return settings.sharpnessDLSS > 0.0f && GetDLSSSharpenerMode() != DLSSSharpenerMode::Off;
}

const Upscaling::RuntimeResolutionPlan& Upscaling::GetRuntimeResolutionPlan() const
{
	return runtimeResolutionPlan;
}

void Upscaling::RefreshRuntimeResolutionState()
{
	const auto requestedUpscaleMethod = GetConfiguredUpscaleMethodForTransition();
	perfMode.UpdateRestartRequiredState(settings, requestedUpscaleMethod);
	RefreshRuntimeResolutionPlan();
	runtimeResolutionStateLastRefreshFrame = GetFrameScopedUpscalingWorkFrame();
}

void Upscaling::EnsureRuntimeResolutionStateCurrent()
{
	const uint32_t currentFrame = GetFrameScopedUpscalingWorkFrame();
	if (currentFrame != std::numeric_limits<uint32_t>::max() &&
		runtimeResolutionStateLastRefreshFrame == currentFrame) {
		return;
	}

	RefreshRuntimeResolutionState();
}

void Upscaling::InvalidateFrameScopedUpscalingState()
{
	runtimeResolutionStateLastRefreshFrame = std::numeric_limits<uint32_t>::max();
	resourceCheckLastCompletedFrame = std::numeric_limits<uint32_t>::max();
	resourceCheckLastCompletedMethod = UpscaleMethod::kNONE;
	resourceCheckStable = false;
	resourceCheckStableMethod = UpscaleMethod::kNONE;
	resourceCheckStableKey = 0;
}

void Upscaling::RefreshRuntimeResolutionPlan()
{
	RuntimeResolutionPlan plan{};
	plan.upscaleMethod = GetRuntimeUpscaleMethod();
	plan.qualityMode = GetRuntimeQualityMode();
	plan.vendorMethod = IsVendorUpscalingMethod(plan.upscaleMethod);
	plan.knownMenuContextActive = IsKnownGameMenuContextActive();
	plan.menuContextActive = globals::game::isVR ? IsVRMenuScenePresentationBlockActive() : IsGameMenuContextActive();
	plan.loadingMenuActive = IsLoadingMenuContextActive();
	plan.perfModeRestartRequired = perfMode.HasRestartRequiredChange();

	auto* state = globals::state;
	const float2 screenSize = state ? state->screenSize : float2{ 0.0f, 0.0f };
	plan.trueHMDDisplaySize = screenSize;
	plan.engineRenderSize = state ? Util::ConvertToDynamic(screenSize) : screenSize;
	plan.finalOutputSize = screenSize;

	// In VR vendor paths, the engine dynamic-resolution ratios are temporarily forced to 1:1
	// during full-resolution presentation phases. The runtime plan should keep using the
	// intended upscaling ratio so history/resource sizing does not oscillate every frame.
	auto resolveVendorDynamicRenderSize = [&](const float2& a_displaySize) {
		if (a_displaySize.x <= 0.0f || a_displaySize.y <= 0.0f)
			return a_displaySize;

		auto resolveScale = [](float a_scale) {
			return std::isfinite(a_scale) && a_scale > 0.0f ? std::clamp(a_scale, 0.0f, 1.0f) : 1.0f;
		};
		auto scaleDimension = [](float a_dimension, float a_scale) {
			const float scaled = a_dimension * a_scale;
			return std::isfinite(scaled) && scaled > 0.0f ? std::max(1.0f, std::floor(scaled + 0.5f)) : 1.0f;
		};

		const float scaleX = resolveScale(resolutionScale.x);
		const float scaleY = resolveScale(resolutionScale.y);
		return float2{
			scaleDimension(a_displaySize.x, scaleX),
			scaleDimension(a_displaySize.y, scaleY)
		};
	};

	const bool vrRenderScaleLatched = IsVRRenderScaleModeLatched();
	if (vrRenderScaleLatched) {
		const float2 displaySize = perfMode.GetDisplayScreenSize();
		const float2 renderSize = perfMode.GetRenderScreenSize();
		if (displaySize.x > 0.0f && displaySize.y > 0.0f)
			plan.trueHMDDisplaySize = displaySize;
		if (renderSize.x > 0.0f && renderSize.y > 0.0f)
			plan.engineRenderSize = renderSize;
		plan.finalOutputSize = plan.trueHMDDisplaySize;
		plan.owner = ResolutionOwner::VRRenderScaleMode;
		plan.outputTarget = UpscalingOutputTarget::SubmitStageIntermediate;
	} else if (plan.vendorMethod && IsUpscalingActive()) {
		if (globals::game::isVR)
			plan.engineRenderSize = resolveVendorDynamicRenderSize(plan.trueHMDDisplaySize);
		plan.owner = ResolutionOwner::VendorDynamicResolution;
		plan.outputTarget = plan.upscaleMethod == UpscaleMethod::kDLSS && ShouldApplyDLSSSharpening() ?
		                        UpscalingOutputTarget::Sharpener :
		                        UpscalingOutputTarget::Main;
	}

	plan.foveatedActive = IsFoveatedVendorDispatchEnabled(plan.upscaleMethod);
	plan.peripheryTAAActive = IsPeripheryTAAEnabled(plan.upscaleMethod);
	if (plan.foveatedActive) {
		const bool usePeripheryTAAProfile = plan.peripheryTAAActive;
		const bool usePeripheryTAAPath = IsPeripheryTAAPathActive(plan.upscaleMethod);
		const auto profile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
		const float centerFeather = usePeripheryTAAPath ?
		                                ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) :
		                                FoveatedCommon::kCenterFeather;
		auto centerOffsets = GetResolvedFoveatedMaskCenterOffsets(usePeripheryTAAProfile);
		if (!globals::game::isVR)
			centerOffsets[1] = { 0.0f, 0.0f };

		const uint32_t eyeDivisor = globals::game::isVR ? 2u : 1u;
		const uint32_t inputWidthPerEye = std::max<uint32_t>(1u, ClampPositiveDimension(plan.engineRenderSize.x) / eyeDivisor);
		const uint32_t inputHeight = ClampPositiveDimension(plan.engineRenderSize.y);
		const uint32_t outputWidthPerEye = std::max<uint32_t>(1u, ClampPositiveDimension(plan.finalOutputSize.x) / eyeDivisor);
		const uint32_t outputHeight = ClampPositiveDimension(plan.finalOutputSize.y);
		const float peripheryTAAOuterScale = plan.peripheryTAAActive ?
		                                         ClampPeripheryTAAOuterScaleForCenter(
													 settings.periphery_taa_outer_scale,
													 profile.centerScale) :
		                                         0.0f;
		plan.foveatedRegion = FoveatedRegionPlan::Build(
			inputWidthPerEye,
			inputHeight,
			outputWidthPerEye,
			outputHeight,
			globals::game::isVR,
			profile.centerScale,
			centerFeather,
			profile.centerHorizontalScale,
			centerOffsets,
			0u,
			peripheryTAAOuterScale);
	}

	runtimeResolutionPlan = plan;
	LogRuntimeResolutionPlanIfChanged(runtimeResolutionPlan);
}

bool Upscaling::IsRenderScaleModeRequested() const
{
	if (!REL::Module::IsVR())
		return true;

	if (!IsRenderScaleMethodEligible(GetConfiguredUpscaleMethodForTransition()))
		return false;

	return GetVRRenderScaleModeRequested();
}

bool Upscaling::GetVRRenderScaleModeRequested() const
{
	if (!REL::Module::IsVR())
		return false;

	const uint32_t pendingRenderScaleMode = pendingVRRenderScaleMode.load(std::memory_order_acquire);
	if (pendingRenderScaleMode != kPendingVRUpscalingSettingUnset)
		return pendingRenderScaleMode != 0;

	return ClampToggleUInt(settings.renderScaleMode) != 0;
}

bool Upscaling::CanUseVRRenderScaleMode() const
{
	if (!REL::Module::IsVR())
		return false;

	if (GetOpenCompositeUpscalingBlocker().active)
		return false;
	if (IsRenderDocUpscalingBlocked())
		return false;

	if (!IsRenderScaleMethodEligible(GetConfiguredUpscaleMethodForTransition()))
		return false;

	return IsRenderScaleQualityMode(GetEffectiveUpscalingQualityMode());
}

bool Upscaling::IsVRRenderScaleModeLatched() const
{
	if (!REL::Module::IsVR())
		return false;

	if (GetOpenCompositeUpscalingBlocker().active)
		return false;
	if (IsRenderDocUpscalingBlocked())
		return false;
	if (IsSubmitStageDeviceLost())
		return false;

	const auto& boot = perfMode.GetBootSnapshot();
	return boot.valid &&
	       boot.active &&
	       boot.renderEyeWidth != 0 &&
	       boot.renderEyeHeight != 0 &&
	       IsRenderScaleMethodEligible(boot.method) &&
	       IsRenderScaleQualityMode(boot.qualityMode);
}

bool Upscaling::IsVRRenderScaleModeActive() const
{
	return IsPerfModeActive();
}

Upscaling::VRRenderScaleStatus Upscaling::GetVRRenderScaleModeStatus() const
{
	if (!REL::Module::IsVR())
		return VRRenderScaleStatus::Disabled;

	const bool renderScaleToggleRequested = GetVRRenderScaleModeRequested();
	const bool active = IsVRRenderScaleModeLatched();
	const bool runtimeBlocked =
		GetOpenCompositeUpscalingBlocker().active ||
		IsRenderDocUpscalingBlocked() ||
		IsSubmitStageDeviceLost();
	const bool relatchPending =
		pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
		perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) ||
		postLoadRuntimeResetPending.load(std::memory_order_acquire) ||
		HasPendingVRRenderScaleTransition();
	if (!renderScaleToggleRequested && !active && !relatchPending && !perfMode.HasRestartRequiredChange())
		return VRRenderScaleStatus::Disabled;

	if (runtimeBlocked)
		return VRRenderScaleStatus::RuntimeBlocked;

	if (relatchPending)
		return VRRenderScaleStatus::PendingRelatch;

	if (perfMode.HasRestartRequiredChange())
		return VRRenderScaleStatus::RestartRequired;

	if (active)
		return VRRenderScaleStatus::Active;

	if (!IsRenderScaleMethodEligible(GetConfiguredUpscaleMethodForTransition()))
		return VRRenderScaleStatus::IneligibleMethod;

	if (!IsRenderScaleQualityMode(GetEffectiveUpscalingQualityMode()))
		return VRRenderScaleStatus::NativeQuality;

	return VRRenderScaleStatus::PendingRelatch;
}

const char* Upscaling::GetVRRenderScaleModeStatusName(VRRenderScaleStatus a_status)
{
	switch (a_status) {
	case VRRenderScaleStatus::Disabled:
		return "Disabled";
	case VRRenderScaleStatus::IneligibleMethod:
		return "Ineligible method";
	case VRRenderScaleStatus::NativeQuality:
		return "Native quality";
	case VRRenderScaleStatus::RuntimeBlocked:
		return "Runtime blocked";
	case VRRenderScaleStatus::PendingRelatch:
		return "Pending relatch";
	case VRRenderScaleStatus::Active:
		return "Active";
	case VRRenderScaleStatus::RestartRequired:
		return "Restart required";
	default:
		return "Unknown";
	}
}

bool Upscaling::IsPerfModeActive() const
{
	if (GetOpenCompositeUpscalingBlocker().active)
		return false;
	if (IsRenderDocUpscalingBlocked())
		return false;

	if (!CanActivateVRRenderScaleRuntime(*this))
		return false;

	return perfMode.IsActive(settings, GetUpscaleMethod());
}

bool Upscaling::GetPerfModeRequested() const
{
	if (REL::Module::IsVR()) {
		const uint32_t pendingPerfMode = pendingVRPerfMode.load(std::memory_order_acquire);
		if (pendingPerfMode != kPendingVRUpscalingSettingUnset)
			return pendingPerfMode != 0 && GetVRRenderScaleModeRequested();
	}

	return GetVRRenderScaleModeRequested() && ClampToggleUInt(settings.perfMode) != 0;
}

void Upscaling::SetVRRenderScaleModeRequested(bool a_enabled, const char* a_reason, bool a_allowDefer, VRUpscalingTransitionOrigin a_origin)
{
	SetPerfModeRequested(a_enabled, a_reason, a_allowDefer, a_origin);
}

void Upscaling::SetPerfModeRequested(bool a_enabled, const char* a_reason, bool a_allowDefer, VRUpscalingTransitionOrigin a_origin)
{
	const auto configuredMethod = GetConfiguredUpscaleMethodForTransition();
	if (a_allowDefer &&
		globals::game::isVR &&
		IsRenderScaleMethodEligible(configuredMethod)) {
		const uint32_t effectiveQualityMode = GetEffectiveUpscalingQualityMode();
		const uint32_t qualityMode = a_enabled && !IsRenderScaleQualityMode(effectiveQualityMode) ? kDefaultRenderScaleQualityMode : effectiveQualityMode;
		SetVRUpscalingTransitionProfile(a_enabled, qualityMode, GetEffectiveDLSSPreset(), a_reason, a_origin);
		return;
	}

	bool renderScaleSettingsChanged = false;
	if (a_enabled && REL::Module::IsVR()) {
		if (!IsRenderScaleMethodEligible(configuredMethod)) {
			a_enabled = false;
			if (settings.renderScaleMode != 0) {
				settings.renderScaleMode = 0;
				renderScaleSettingsChanged = true;
			}
		}
	}

	if (a_enabled && REL::Module::IsVR()) {
		if (settings.renderScaleMode != 1) {
			settings.renderScaleMode = 1;
			renderScaleSettingsChanged = true;
		}
		const UpscaleMethod requestedMethod = configuredMethod;
		const uint32_t requestedQualityMode = requestedMethod == UpscaleMethod::kDLSS ?
		                                          GetEffectiveUpscalingQualityMode() :
		                                          settings.qualityMode;
		if (!IsRenderScaleQualityMode(requestedQualityMode)) {
			settings.qualityMode = kDefaultRenderScaleQualityMode;
			renderScaleSettingsChanged = true;
		}
	}

	const uint32_t requested = a_enabled ? 1u : 0u;
	const bool activeMatchesRequest = IsVRRenderScaleModeLatched() == a_enabled;
	if (ClampToggleUInt(settings.perfMode) == requested && activeMatchesRequest && !perfMode.HasRestartRequiredChange()) {
		if (renderScaleSettingsChanged) {
			InvalidateFrameScopedUpscalingState();
			RequestHistoryReset();
			RequestPerfModeRenderTargetRecreate(a_reason, a_origin);
		}
		return;
	}

	settings.perfMode = requested;
	InvalidateFrameScopedUpscalingState();
	RequestHistoryReset();
	RequestPerfModeRenderTargetRecreate(a_reason, a_origin);
}

void Upscaling::ApplyCSMenuUpscalingTransition(UpscaleMethod a_targetMethod, bool a_renderScaleModeEnabled, uint32_t a_qualityMode, uint32_t a_dlssPreset, const char* a_reason, VRUpscalingTransitionOrigin a_origin)
{
	const bool isVR = globals::game::isVR;
	const bool allowPendingDLSSSelection =
		a_targetMethod == UpscaleMethod::kDLSS &&
		!streamline.featureCheckComplete;
	const bool allowDLSSSelection = streamline.featureDLSS || allowPendingDLSSSelection;
	const int maxMethodValue = allowDLSSSelection ?
	                               static_cast<int>(UpscaleMethod::kDLSS) :
	                               static_cast<int>(UpscaleMethod::kFSR);
	const int targetMethodValue = std::clamp(static_cast<int>(a_targetMethod), static_cast<int>(UpscaleMethod::kNONE), maxMethodValue);
	UpscaleMethod targetMethod = static_cast<UpscaleMethod>(targetMethodValue);
	const auto previousMethod = GetUpscaleMethod();
	if (GetOpenCompositeUpscalingBlocker().active) {
		targetMethod = UpscaleMethod::kNONE;
		settings.upscaleMethod = static_cast<uint32_t>(UpscaleMethod::kNONE);
		settings.upscaleMethodNoDLSS = static_cast<uint32_t>(UpscaleMethod::kNONE);
	}
	const uint32_t qualityMode = std::min(a_qualityMode, kQualityModeMaxIndex);
	const uint32_t dlssPreset = std::min(a_dlssPreset, kDLSSPresetMaxIndex);
	const bool renderScaleQuality = IsRenderScaleQualityMode(qualityMode);
	const bool previousRenderScaleRelevant =
		isVR &&
		IsVRRenderScaleTransitionSafetyRelevant(*this, previousMethod);
	uint32_t* currentUpscaleMode = (streamline.featureDLSS || targetMethod == UpscaleMethod::kDLSS) ? &settings.upscaleMethod : &settings.upscaleMethodNoDLSS;
	*currentUpscaleMode = static_cast<uint32_t>(targetMethod);
	if (targetMethod != UpscaleMethod::kDLSS)
		pendingVRDLSSPreset.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);

	const bool targetMethodRenderScaleEligible = IsRenderScaleMethodEligible(targetMethod);
	const bool targetRenderScaleMode = targetMethodRenderScaleEligible && a_renderScaleModeEnabled && renderScaleQuality;
	const bool methodChanged = previousMethod != targetMethod;
	if (methodChanged)
		InvalidateFrameScopedUpscalingState();
	const bool methodRelatchRequired =
		isVR &&
		methodChanged &&
		(previousRenderScaleRelevant || targetRenderScaleMode);
	const bool qualityTargetChanged = GetEffectiveUpscalingQualityMode() != qualityMode;
	const bool renderScaleTargetChanged = isVR && IsRenderScaleModeRequested() != targetRenderScaleMode;
	const bool qualityPending = pendingVRUpscalingQualityMode.load(std::memory_order_acquire) != kPendingVRUpscalingSettingUnset;
	const bool renderScaleModePending = pendingVRRenderScaleMode.load(std::memory_order_acquire) != kPendingVRUpscalingSettingUnset;
	const bool perfModeRequestPending = pendingVRPerfMode.load(std::memory_order_acquire) != kPendingVRUpscalingSettingUnset;
	const bool dlssPresetPending = pendingVRDLSSPreset.load(std::memory_order_acquire) != kPendingVRUpscalingSettingUnset;
	const bool dlssPresetChanged = targetMethod == UpscaleMethod::kDLSS && (settings.dlssPreset != dlssPreset || dlssPresetPending);
	auto clearPendingTransitionTimingIfIdle = [&]() {
		if (HasPendingVRUpscalingTransition())
			return;

		pendingVRUpscalingTransitionFrame.store(0, std::memory_order_release);
	};

	if (!isVR) {
		bool settingsChanged = false;
		if (settings.qualityMode != qualityMode || qualityPending) {
			settingsChanged = settingsChanged || settings.qualityMode != qualityMode;
			settings.qualityMode = qualityMode;
		}
		if (dlssPresetChanged) {
			settings.dlssPreset = dlssPreset;
			settingsChanged = true;
		}
		ClearPendingVRUpscalingTransition();
		if (settingsChanged) {
			InvalidateFrameScopedUpscalingState();
			RequestHistoryReset();
		}
		return;
	}

	if (!methodChanged && !qualityTargetChanged && !renderScaleTargetChanged && dlssPresetChanged) {
		settings.dlssPreset = dlssPreset;
		pendingVRDLSSPreset.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
		clearPendingTransitionTimingIfIdle();
		InvalidateFrameScopedUpscalingState();
		RequestHistoryReset();
		return;
	}

	const bool stageVRUpscalingChange =
		((targetMethodRenderScaleEligible && ShouldStageVRRenderScaleTransition(targetRenderScaleMode, qualityMode)) ||
			methodRelatchRequired);
	bool qualityChanged = false;
	bool presetChanged = false;

	if (stageVRUpscalingChange) {
		bool queuedRenderScaleTransition = false;
		if (qualityTargetChanged) {
			QueueVRUpscalingQualityMode(qualityMode, a_origin);
			queuedRenderScaleTransition = true;
		}
		if (renderScaleTargetChanged) {
			QueueVRRenderScaleModeRequest(targetRenderScaleMode, a_origin);
			queuedRenderScaleTransition = true;
		}
		if (targetMethod == UpscaleMethod::kDLSS && GetEffectiveDLSSPreset() != dlssPreset) {
			QueueVRDLSSPreset(dlssPreset, a_origin);
		}

		const bool targetPerfMode = targetRenderScaleMode;
		const uint32_t requestedPerfMode = targetPerfMode ? 1u : 0u;
		const uint32_t pendingPerfMode = pendingVRPerfMode.load(std::memory_order_acquire);
		const bool perfModeAlreadyPending =
			pendingPerfMode != kPendingVRUpscalingSettingUnset &&
			(pendingPerfMode != 0) == targetPerfMode;
		const bool perfModeNeedsApply =
			!perfModeAlreadyPending &&
			(ClampToggleUInt(settings.perfMode) != requestedPerfMode ||
				IsVRRenderScaleModeLatched() != targetPerfMode ||
				perfMode.HasRestartRequiredChange());
		if (perfModeNeedsApply || methodRelatchRequired) {
			QueueVRPerfModeRequest(targetPerfMode, a_origin);
			queuedRenderScaleTransition = true;
		}

		if (queuedRenderScaleTransition)
			RequestHistoryReset();
		return;
	}

	const uint32_t requestedRenderScaleMode = targetRenderScaleMode ? 1u : 0u;
	const bool renderScaleModeChanged = settings.renderScaleMode != requestedRenderScaleMode;
	settings.renderScaleMode = requestedRenderScaleMode;
	if (renderScaleModeChanged || renderScaleModePending)
		pendingVRRenderScaleMode.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);

	const bool qualitySettingChanged = settings.qualityMode != qualityMode;
	if (qualitySettingChanged || qualityPending) {
		settings.qualityMode = qualityMode;
		pendingVRUpscalingQualityMode.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
		if (qualitySettingChanged) {
			qualityChanged = true;
			presetChanged = true;
		}
	}
	if (dlssPresetChanged) {
		settings.dlssPreset = dlssPreset;
		pendingVRDLSSPreset.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
		presetChanged = true;
	}

	if (presetChanged || renderScaleModeChanged) {
		InvalidateFrameScopedUpscalingState();
		RequestHistoryReset();
	}

	const uint32_t requestedPerfMode = targetRenderScaleMode ? 1u : 0u;
	if (perfModeRequestPending)
		pendingVRPerfMode.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
	clearPendingTransitionTimingIfIdle();

	if (renderScaleModeChanged ||
		qualityChanged ||
		ClampToggleUInt(settings.perfMode) != requestedPerfMode ||
		IsVRRenderScaleModeLatched() != targetRenderScaleMode ||
		perfModeRequestPending) {
		SetPerfModeRequested(targetRenderScaleMode, a_reason, false, a_origin);
	}
	if (qualityChanged || renderScaleModeChanged)
		RequestPerfModeRenderTargetRecreate(a_reason, a_origin);
}

void Upscaling::SetVRUpscalingTransitionProfile(bool a_renderScaleModeEnabled, uint32_t a_qualityMode, uint32_t a_dlssPreset, const char* a_reason, VRUpscalingTransitionOrigin a_origin)
{
	ApplyCSMenuUpscalingTransition(GetConfiguredUpscaleMethodForTransition(), a_renderScaleModeEnabled, a_qualityMode, a_dlssPreset, a_reason, a_origin);
}

void Upscaling::QueueVRFpsStabilizerLoadSync(uint32_t a_frame)
{
	if (!globals::game::isVR || !settings.vrFpsStabilizerSync) {
		pendingVRFpsStabilizerSyncFrame.store(0, std::memory_order_release);
		return;
	}

	const uint32_t frame = std::max(a_frame, 1u);
	pendingVRFpsStabilizerSyncFrame.store(frame, std::memory_order_release);
	logger::debug("[Upscaling] VR FPS Stabilizer Sync queued after save-load menu close at frame {}.", frame);
}

void Upscaling::ApplyPendingVRFpsStabilizerLoadSync()
{
	const uint32_t queuedFrame = pendingVRFpsStabilizerSyncFrame.load(std::memory_order_acquire);
	if (queuedFrame == 0)
		return;

	if (!globals::game::isVR || !settings.vrFpsStabilizerSync) {
		pendingVRFpsStabilizerSyncFrame.store(0, std::memory_order_release);
		return;
	}

	const auto* state = globals::state;
	if (!IsVRFpsStabilizerLoadSyncReady(state))
		return;

	VRFpsStabilizerUpscalingProfiles profiles;
	if (!TryLoadVRFpsStabilizerUpscalingProfiles(profiles)) {
		pendingVRFpsStabilizerSyncFrame.store(0, std::memory_order_release);
		MarkVRFpsStabilizerSyncResolved(*this, queuedFrame);
		logger::warn("[Upscaling] VR FPS Stabilizer Sync enabled, but no unconditional Interior/Exterior upscaling profile was found in {}.", profiles.path.string());
		return;
	}

	const bool loadedInterior = Util::IsInterior();
	const auto& profile = loadedInterior ? profiles.interior : profiles.exterior;
	const char* profileName = loadedInterior ? "Interior" : "Exterior";
	if (!profile.HasAnySetting()) {
		pendingVRFpsStabilizerSyncFrame.store(0, std::memory_order_release);
		MarkVRFpsStabilizerSyncResolved(*this, queuedFrame);
		logger::warn("[Upscaling] VR FPS Stabilizer Sync found no {} upscaling profile in {}.", profileName, profiles.path.string());
		return;
	}

	const auto currentMethod = GetConfiguredUpscaleMethodForTransition();
	const auto target = ResolveVRFpsStabilizerTransitionTarget(*this, profile);

	const bool methodMatches = currentMethod == target.method;
	const bool qualityMatches = GetEffectiveUpscalingQualityMode() == target.qualityMode;
	const bool renderScaleMatches = IsRenderScaleModeRequested() == target.renderScaleMode;
	const bool dlssPresetMatches = target.method != UpscaleMethod::kDLSS || GetEffectiveDLSSPreset() == target.dlssPreset;
	pendingVRFpsStabilizerSyncFrame.store(0, std::memory_order_release);
	MarkVRFpsStabilizerSyncResolved(*this, queuedFrame);

	if (methodMatches && qualityMatches && renderScaleMatches && dlssPresetMatches) {
		logger::debug(
			"[Upscaling] VR FPS Stabilizer Sync: {} profile already matched after save-load (method={}, quality={}, dlssProfile={}, renderScale={}).",
			profileName,
			magic_enum::enum_name(target.method),
			target.qualityMode,
			target.dlssPreset,
			BoolText(target.renderScaleMode));
		return;
	}

	logger::debug(
		"[Upscaling] VR FPS Stabilizer Sync applying {} profile from {}: method {} -> {}, quality {} -> {}, dlssProfile {} -> {}, renderScale {} -> {}.",
		profileName,
		profiles.path.string(),
		magic_enum::enum_name(currentMethod),
		magic_enum::enum_name(target.method),
		GetEffectiveUpscalingQualityMode(),
		target.qualityMode,
		GetEffectiveDLSSPreset(),
		target.dlssPreset,
		BoolText(IsRenderScaleModeRequested()),
		BoolText(target.renderScaleMode));
	ApplyCSMenuUpscalingTransition(
		target.method,
		target.renderScaleMode,
		target.qualityMode,
		target.dlssPreset,
		"VR FPS Stabilizer save-load sync",
		VRUpscalingTransitionOrigin::PostLoadSync);
}

bool Upscaling::IsPerfModePresentationActive() const
{
	return IsPerfModeActive() && perfMode.HasKnownHMDSize();
}

bool Upscaling::IsPresentationUpscalingActive() const
{
	return IsSubmitStageUpscalingActive();
}

void Upscaling::RecordTrueHMDRenderTargetSize(uint32_t a_eyeWidth, uint32_t a_eyeHeight)
{
	perfMode.RecordTrueHMDSize(a_eyeWidth, a_eyeHeight);
}

bool Upscaling::ConsumePerfModeBootLatchCreate()
{
	return perfModeAllowBootLatchCreate.exchange(false, std::memory_order_acq_rel);
}

bool Upscaling::TryGetPerfModeOpenVRRenderTargetSize(uint32_t& a_width, uint32_t& a_height, bool a_allowCreate)
{
	if (GetOpenCompositeUpscalingBlocker().active)
		return false;
	if (IsRenderDocUpscalingBlocked())
		return false;

	if (!CanActivateVRRenderScaleRuntime(*this)) {
		if (a_allowCreate)
			perfModeAllowBootLatchCreate.store(true, std::memory_order_release);
		return false;
	}

	if (ShouldBlockForPendingExplicitVRUpscalingWork(*this)) {
		if (a_allowCreate)
			perfModeAllowBootLatchCreate.store(true, std::memory_order_release);
		return false;
	}

	if (a_allowCreate && DeferVRPerfModeBootLatchForPendingDLSS(*this))
		return false;

	return perfMode.TryGetOpenVRRenderTargetSize(settings, GetUpscaleMethod(), a_width, a_height, a_allowCreate);
}

bool Upscaling::AdjustVRRenderScaleRenderTargetProperties(RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties) const
{
	if (!a_properties || !globals::game::isVR)
		return false;

	auto setSize = [a_properties](float2 a_size) {
		const uint32_t width = ClampPositiveDimension(a_size.x);
		const uint32_t height = ClampPositiveDimension(a_size.y);
		if (a_properties->width == width && a_properties->height == height)
			return false;

		a_properties->width = width;
		a_properties->height = height;
		return true;
	};

	if (IsVRRenderScaleModeLatched()) {
		const auto displaySize = perfMode.GetDisplayScreenSize();
		const auto renderSize = perfMode.GetRenderScreenSize();
		if (displaySize.x <= 0.0f || displaySize.y <= 0.0f || renderSize.x <= 0.0f || renderSize.y <= 0.0f)
			return false;

		if (IsVRRenderScaleDisplaySizedTarget(a_target))
			return setSize(displaySize);

		switch (a_target) {
		case RE::RENDER_TARGETS::kUNDERWATER_MASK:
			// The submit-stage underwater repair refreshes the lock-independent
			// stereo mask region, which is per-eye width by full internal height.
			return setSize({ renderSize.x * 0.5f, renderSize.y });
		default:
			if (IsVRRenderScaleEngineSizedTarget(a_target))
				return setSize(renderSize);
			return false;
		}
	}

	return false;
}

void Upscaling::CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	InvalidateFrameScopedUpscalingState();
	logger::debug("[Upscaling] Creating texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	if (a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR) {
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		if (!reactiveMaskTexture) {
			reactiveMaskTexture = new Texture2D(texDesc);
			reactiveMaskTexture->CreateSRV(srvDesc);
			reactiveMaskTexture->CreateUAV(uavDesc);
		}

		if (!transparencyCompositionMaskTexture) {
			transparencyCompositionMaskTexture = new Texture2D(texDesc);
			transparencyCompositionMaskTexture->CreateSRV(srvDesc);
			transparencyCompositionMaskTexture->CreateUAV(uavDesc);
		}
	}

	// Motion vector copy texture is used by DLSS and FSR encode pass.
	if (a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR) {
		if (!motionVectorCopyTexture) {
			auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

			D3D11_TEXTURE2D_DESC motionTexDesc{};
			motionVector.texture->GetDesc(&motionTexDesc);

			texDesc.Format = motionTexDesc.Format;
			srvDesc.Format = texDesc.Format;
			uavDesc.Format = texDesc.Format;

			motionVectorCopyTexture = new Texture2D(motionTexDesc);
			motionVectorCopyTexture->CreateSRV(srvDesc);
			motionVectorCopyTexture->CreateUAV(uavDesc);
		}
	}

	// Shared DLSS sharpener texture - matches kMAIN format for HDR sharpening
	if (a_upscalemethod == UpscaleMethod::kDLSS) {
		if (!sharpenerTexture) {
			main.texture->GetDesc(&texDesc);
			main.SRV->GetDesc(&srvDesc);

			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			srvDesc.Format = texDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;

			uavDesc.Format = texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			sharpenerTexture = new Texture2D(texDesc);
			sharpenerTexture->CreateSRV(srvDesc);
			sharpenerTexture->CreateUAV(uavDesc);
		}
	}
}

void Upscaling::DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	InvalidateFrameScopedUpscalingState();
	logger::debug("[Upscaling] Destroying texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	// Clean up D3D11 textures that are no longer needed
	// Only destroy textures when switching away from methods that use them
	if (a_upscalemethod != UpscaleMethod::kDLSS && a_upscalemethod != UpscaleMethod::kFSR) {
		DestroyTexture(reactiveMaskTexture);
		DestroyTexture(transparencyCompositionMaskTexture);
	}

	// Motion vector copy texture is used by DLSS/FSR - destroy when switching away from both.
	if (a_upscalemethod != UpscaleMethod::kDLSS && a_upscalemethod != UpscaleMethod::kFSR) {
		DestroyTexture(motionVectorCopyTexture);
	}

	// Shared DLSS sharpener texture is only needed for DLSS.
	if (a_upscalemethod != UpscaleMethod::kDLSS) {
		DestroyTexture(sharpenerTexture);
		DestroySubmitStageDLSSSharpenerTextures();
	}
}

void Upscaling::DestroySubmitStageDLSSSharpenerTextures()
{
	InvalidateFrameScopedUpscalingState();
	for (auto& texture : submitStageDLSSSharpenerTexture)
		texture.reset();
}

void Upscaling::DestroyCommonUpscalingTextures()
{
	InvalidateFrameScopedUpscalingState();
	DestroyTexture(reactiveMaskTexture);
	DestroyTexture(transparencyCompositionMaskTexture);
	DestroyTexture(motionVectorCopyTexture);
	DestroyTexture(sharpenerTexture);
	DestroySubmitStageDLSSSharpenerTextures();
}

bool Upscaling::AreCommonVendorTexturesReady(UpscaleMethod a_upscaleMethod) const
{
	if (!IsVendorUpscalingMethod(a_upscaleMethod))
		return true;

	const auto textureReady = [](const Texture2D* a_texture) {
		return a_texture && a_texture->resource && a_texture->srv && a_texture->uav;
	};

	return textureReady(reactiveMaskTexture) &&
	       textureReady(transparencyCompositionMaskTexture) &&
	       (globals::game::isVR || textureReady(motionVectorCopyTexture)) &&
	       (a_upscaleMethod != UpscaleMethod::kDLSS || textureReady(sharpenerTexture));
}

namespace
{
	bool HasVRIntermediateTextureCache(const Upscaling::VRIntermediateTextureCache& a_cache)
	{
		return a_cache.colorIn[0] && a_cache.colorIn[1] &&
		       a_cache.colorOut[0] && a_cache.colorOut[1] &&
		       a_cache.depth[0] && a_cache.depth[1] &&
		       a_cache.linearDepth[0] && a_cache.linearDepth[1] &&
		       a_cache.motionVectors[0] && a_cache.motionVectors[1] &&
		       a_cache.reactiveMask[0] && a_cache.reactiveMask[1] &&
		       a_cache.transparencyMask[0] && a_cache.transparencyMask[1] &&
		       a_cache.colorOut[0]->uav && a_cache.colorOut[1]->uav &&
		       a_cache.linearDepth[0]->uav && a_cache.linearDepth[1]->uav &&
		       a_cache.motionVectors[0]->uav && a_cache.motionVectors[1]->uav &&
		       a_cache.reactiveMask[0]->uav && a_cache.reactiveMask[1]->uav &&
		       a_cache.transparencyMask[0]->uav && a_cache.transparencyMask[1]->uav;
	}

	bool MatchesVRIntermediateTextureCache(const Upscaling::VRIntermediateTextureCache& a_cache,
		uint32_t a_inWidth, uint32_t a_inHeight, uint32_t a_outWidth, uint32_t a_outHeight)
	{
		return HasVRIntermediateTextureCache(a_cache) &&
		       a_cache.inWidth == a_inWidth &&
		       a_cache.inHeight == a_inHeight &&
		       a_cache.outWidth == a_outWidth &&
		       a_cache.outHeight == a_outHeight;
	}

	void ClearVRIntermediateTextureCache(Upscaling::VRIntermediateTextureCache& a_cache)
	{
		for (uint32_t i = 0; i < 2; ++i) {
			a_cache.colorIn[i].reset();
			a_cache.colorOut[i].reset();
			a_cache.depth[i].reset();
			a_cache.linearDepth[i].reset();
			a_cache.motionVectors[i].reset();
			a_cache.reactiveMask[i].reset();
			a_cache.transparencyMask[i].reset();
		}
		a_cache.inWidth = 0;
		a_cache.inHeight = 0;
		a_cache.outWidth = 0;
		a_cache.outHeight = 0;
	}
}

void Upscaling::DestroyVRIntermediateTextures()
{
	InvalidateFrameScopedUpscalingState();
	RetiredVRIntermediateTextures retired{};
	retired.retireFrame = globals::state ? globals::state->frameCount : 0u;
	bool hasRetiredTextures = false;
	const auto retireArray = [&hasRetiredTextures](auto& a_source, auto& a_destination) {
		for (uint32_t i = 0; i < 2; ++i) {
			if (a_source[i])
				hasRetiredTextures = true;
			a_destination[i] = std::move(a_source[i]);
		}
	};

	retireArray(vrIntermediateColorIn, retired.colorIn);
	retireArray(vrIntermediateColorOut, retired.colorOut);
	retireArray(vrIntermediateDepth, retired.depth);
	retireArray(vrIntermediateLinearDepth, retired.linearDepth);
	retireArray(vrIntermediateMotionVectors, retired.motionVectors);
	retireArray(vrIntermediateReactiveMask, retired.reactiveMask);
	retireArray(vrIntermediateTransparencyMask, retired.transparencyMask);
	retireArray(submitStageDLSSSharpenerTexture, retired.submitStageDLSSSharpener);

	if (hasRetiredTextures) {
		retiredVRIntermediateTextures.push_back(std::move(retired));
		const uint32_t currentFrame = globals::state ? globals::state->frameCount : 0u;
		std::erase_if(retiredVRIntermediateTextures, [currentFrame](const RetiredVRIntermediateTextures& entry) {
			return currentFrame >= entry.retireFrame && currentFrame - entry.retireFrame > 4u;
		});
		while (retiredVRIntermediateTextures.size() > 4) {
			retiredVRIntermediateTextures.erase(retiredVRIntermediateTextures.begin());
		}
	}

	for (uint32_t i = 0; i < 2; ++i) {
		vrIntermediateColorIn[i].reset();
		vrIntermediateColorOut[i].reset();
		vrIntermediateDepth[i].reset();
		vrIntermediateLinearDepth[i].reset();
		vrIntermediateMotionVectors[i].reset();
		vrIntermediateReactiveMask[i].reset();
		vrIntermediateTransparencyMask[i].reset();
	}
	ClearVRIntermediateTextureCache(cachedVRIntermediateTextures);
	peripheryTAAHistoryReadIndex = 0;
	peripheryTAAHistoryValid = false;

	submitStagePreparedFrame = std::numeric_limits<uint32_t>::max();
	submitStagePreparedFramePresentationOnly = false;
	submitStagePreparedFrameFoveatedRegionEncode = false;
	submitStageVendorOutputFrame = std::numeric_limits<uint32_t>::max();
	submitStageVendorOutputSourceTexture = nullptr;
	submitStageVendorEyeState = {};
	submitStageForceFullEyeVendorFallback = false;
	ClearSubmitStageVendorResumeCooldown();
	ClearSubmitStageFoveatedVendorRetryBackoff();
	submitStageMirrorFrame = std::numeric_limits<uint32_t>::max();
	submitStageMirrorEyeReady = {};
	submitStageMirrorSourceTexture = nullptr;
	submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	submitStageFoveatedPeripheryTAAEyeReady = {};
}

void Upscaling::UnbindUpscalingResources()
{
	auto context = globals::d3d::context;
	if (!context)
		return;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	ID3D11ShaderResourceView* nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
	context->VSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->PSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->GSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->HSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->DSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);

	ID3D11UnorderedAccessView* nullUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

	ID3D11Buffer* nullCBs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
	context->VSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->PSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->GSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->HSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->DSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->CSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);

	context->CSSetShader(nullptr, nullptr, 0);
}

void Upscaling::RequestPostLoadRuntimeReset()
{
	if (!globals::game::isVR)
		return;

	postLoadRuntimeResetPending.store(true, std::memory_order_release);
	InvalidateFrameScopedUpscalingState();
	logger::debug("[Upscaling] Armed VR post-load runtime reset");
}

void Upscaling::RequestPerfModeRenderTargetRecreate(const char* a_reason, VRUpscalingTransitionOrigin a_origin)
{
	if (!globals::game::isVR)
		return;

	if (GetOpenCompositeUpscalingBlocker().active)
		return;

	const auto configuredMethod = GetConfiguredUpscaleMethodForTransition();
	const bool perfModeActive = IsVRRenderScaleModeLatched();
	const bool perfModeEligible = perfMode.IsEligible(settings, configuredMethod);
	if (!perfModeActive && !perfModeEligible && !perfMode.HasRestartRequiredChange())
		return;

	const uint32_t relatchDelayFrames = kVRUpscalingTransitionApplyDelayFrames;
	const bool requirePostLoadSettle = UsesVRRenderScalePostLoadSettle(a_origin);
	const bool currentRequirePostLoadSettle =
		pendingPerfModeRenderTargetRecreatePostLoadSettle.load(std::memory_order_acquire);
	const bool wasPending = pendingPerfModeRenderTargetRecreate.exchange(true, std::memory_order_acq_rel);
	if (ShouldStoreVRRenderScalePostLoadSettle(currentRequirePostLoadSettle, requirePostLoadSettle, wasPending, a_origin)) {
		pendingPerfModeRenderTargetRecreatePostLoadSettle.store(requirePostLoadSettle, std::memory_order_release);
	}
	const uint32_t previousRelatchDelay = pendingPerfModeRenderTargetRecreateDelayFrames.load(std::memory_order_acquire);
	if (!wasPending || relatchDelayFrames > previousRelatchDelay)
		MarkPerfModeRenderTargetRecreateQueued(relatchDelayFrames);
	RequestHistoryReset();
	InvalidateFrameScopedUpscalingState();
	BeginVRRenderScaleInfoTransition(a_reason);
	if (wasPending)
		return;

	if (a_reason && *a_reason) {
		logger::debug("[VRRenderScale] Queued render-target relatch: {}", a_reason);
	} else {
		logger::debug("[VRRenderScale] Queued render-target relatch");
	}
}

bool Upscaling::ApplyPendingPerfModeRenderTargetRecreate(const char* a_caller)
{
	if (!globals::game::isVR)
		return true;

	if (!pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire))
		return true;

	if (GetOpenCompositeUpscalingBlocker().active) {
		pendingPerfModeRenderTargetRecreate.store(false, std::memory_order_release);
		pendingPerfModeRenderTargetRecreateFrame.store(0, std::memory_order_release);
		pendingPerfModeRenderTargetRecreateDelayFrames.store(0, std::memory_order_release);
		pendingPerfModeRenderTargetRecreatePostLoadSettle.store(false, std::memory_order_release);
		ClearVRRenderScaleInfoTransition();
		return true;
	}

	auto* state = globals::state;
	if (!state || !globals::game::renderer || !globals::d3d::context)
		return false;

	if (DeferVRPerfModeBootLatchForPendingDLSS(*this)) {
		MarkPerfModeRenderTargetRecreateQueued();
		return false;
	}

	if (ShouldDeferVRUpscalingTransitionSettings()) {
		MarkPerfModeRenderTargetRecreateQueued();
		return false;
	}

	if (ShouldWaitForPerfModeRenderTargetRecreateDelay())
		return false;

	if (IsVRRenderScaleTransitionSafetyRelevant(*this) && HasPendingVRRenderScaleTransition()) {
		return false;
	}

	static bool loggedRelatchPostLoadSettle = false;
	if (ShouldDeferVRRenderScaleRelatchForPostLoadSettle(*this, state)) {
		MarkPerfModeRenderTargetRecreateQueued(kVRRenderScalePostLoadSettleRetryFrames);
		if (!loggedRelatchPostLoadSettle) {
			logger::debug("[VRRenderScale] Render-target relatch waiting for post-load world-render settle.");
			loggedRelatchPostLoadSettle = true;
		}
		return false;
	}
	loggedRelatchPostLoadSettle = false;

	static bool loggedRelatchLoadingPresentation = false;
	if (IsVRLoadingPresentationContextActive(state)) {
		MarkPerfModeRenderTargetRecreateQueued(kVRRenderScalePostLoadSettleRetryFrames);
		if (!loggedRelatchLoadingPresentation) {
			logger::debug("[VRRenderScale] Render-target relatch waiting for loading presentation context to clear.");
			loggedRelatchLoadingPresentation = true;
		}
		return false;
	}
	loggedRelatchLoadingPresentation = false;

	if (perfModeRenderTargetRecreateInProgress.exchange(true, std::memory_order_acq_rel))
		return false;

	ScopeExit guard([&]() {
		perfModeRenderTargetRecreateInProgress.store(false, std::memory_order_release);
	});

	if (!pendingPerfModeRenderTargetRecreate.exchange(false, std::memory_order_acq_rel))
		return true;
	const uint32_t retryDelayFrames = std::max(
		pendingPerfModeRenderTargetRecreateDelayFrames.load(std::memory_order_acquire),
		kVRUpscalingTransitionApplyDelayFrames);
	auto clearRelatchDelay = [&]() {
		pendingPerfModeRenderTargetRecreateFrame.store(0, std::memory_order_release);
		pendingPerfModeRenderTargetRecreateDelayFrames.store(0, std::memory_order_release);
		pendingPerfModeRenderTargetRecreatePostLoadSettle.store(false, std::memory_order_release);
	};
	auto requeueRelatch = [&](uint32_t a_minDelayFrames, bool a_includeExistingRetryDelay = true) {
		pendingPerfModeRenderTargetRecreate.store(true, std::memory_order_release);
		const uint32_t delayFrames = a_includeExistingRetryDelay ?
		                                 std::max(retryDelayFrames, a_minDelayFrames) :
		                                 a_minDelayFrames;
		if (!a_includeExistingRetryDelay) {
			pendingPerfModeRenderTargetRecreateDelayFrames.store(0, std::memory_order_release);
		}
		MarkPerfModeRenderTargetRecreateQueued(delayFrames);
	};
	pendingPerfModeRenderTargetRecreateFrame.store(0, std::memory_order_release);
	const auto relatchUpscaleMethod = GetUpscaleMethod();
	auto hasPendingVendorReset = [&]() {
		return pendingDLSSReset.load(std::memory_order_acquire) ||
		       pendingFSRReset.load(std::memory_order_acquire) ||
		       (relatchUpscaleMethod != UpscaleMethod::kFSR && fidelityFX.HasFSRResourcesPendingTeardown());
	};
	auto shouldSkipNoOpRelatch = [&]() {
		if (!IsRenderScaleMethodEligible(relatchUpscaleMethod))
			return false;

		if (HasPendingVRUpscalingTransition() ||
			postLoadRuntimeResetPending.load(std::memory_order_acquire) ||
			hasPendingVendorReset() ||
			perfMode.HasRestartRequiredChange() ||
			!AreCommonVendorTexturesReady(relatchUpscaleMethod)) {
			return false;
		}

		const auto& boot = perfMode.GetBootSnapshot();
		if (!boot.valid || !boot.active || boot.method != relatchUpscaleMethod)
			return false;

		if (ClampToggleUInt(settings.renderScaleMode) == 0 ||
			ClampQualityModeUInt(settings.qualityMode) != ClampQualityModeUInt(boot.qualityMode)) {
			return false;
		}

		const float2 targetDisplaySize = perfMode.GetDisplayScreenSize();
		const float2 targetRenderSize = perfMode.GetRenderScreenSize();
		if (targetDisplaySize.x <= 0.0f || targetDisplaySize.y <= 0.0f ||
			targetRenderSize.x <= 0.0f || targetRenderSize.y <= 0.0f) {
			return false;
		}

		return AreVRRenderScaleRenderTargetsSizedForDimensions(targetRenderSize, targetDisplaySize);
	};

	static bool loggedRelatchVendorDefer = false;
	static bool loggedRelatchD3DDefer = false;
	static UpscaleMethod lastRelatchLogMethod = UpscaleMethod::kNONE;
	const auto clearRelatchRetryLogs = [&]() {
		loggedRelatchVendorDefer = false;
		loggedRelatchD3DDefer = false;
	};
	if (lastRelatchLogMethod != relatchUpscaleMethod) {
		clearRelatchRetryLogs();
		lastRelatchLogMethod = relatchUpscaleMethod;
	}

	logger::debug(
		"[VRRenderScale] Applying render-target relatch{}{}",
		a_caller && *a_caller ? " from " : "",
		a_caller && *a_caller ? a_caller : "");

	if (shouldSkipNoOpRelatch()) {
		if (ClampToggleUInt(settings.perfMode) == 0)
			settings.perfMode = 1;
		clearRelatchDelay();
		clearRelatchRetryLogs();
		logger::debug("[VRRenderScale] Skipped render-target relatch; current render-scale target is already active.");
		CompleteVRRenderScaleInfoTransition(
			"already stable",
			true,
			relatchUpscaleMethod,
			perfMode.GetDisplayScreenSize(),
			perfMode.GetRenderScreenSize());
		return false;
	}

	bool relatchRenderScaleActive = false;
	float2 relatchTargetDisplaySize{ 0.0f, 0.0f };
	float2 relatchTargetEngineSize{ 0.0f, 0.0f };
	try {
		const auto canPreserveFSRResourcesForRelatch = [&]() {
			if (relatchUpscaleMethod != UpscaleMethod::kFSR ||
				pendingFSRReset.load(std::memory_order_acquire) ||
				!fidelityFX.HasFSRResources() ||
				!perfMode.trueHMDEyeWidth ||
				!perfMode.trueHMDEyeHeight) {
				return false;
			}

			const uint32_t qualityMode = ClampQualityModeUInt(settings.qualityMode);
			const bool renderScaleTargetActive =
				IsRenderScaleMethodEligible(relatchUpscaleMethod) &&
				IsRenderScaleQualityMode(qualityMode) &&
				ClampToggleUInt(settings.renderScaleMode) != 0 &&
				ClampToggleUInt(settings.perfMode) != 0;
			const float renderScale = renderScaleTargetActive ? GetQualityModeResolutionScale(qualityMode) : 1.0f;
			auto scaleDimension = [](uint32_t a_dimension, float a_scale) {
				if (!std::isfinite(a_scale))
					return a_dimension;

				const float scaled = static_cast<float>(a_dimension) * std::clamp(a_scale, 0.1f, 1.0f);
				return std::clamp<uint32_t>(
					static_cast<uint32_t>(std::floor(scaled)),
					1u,
					std::max<uint32_t>(a_dimension, 1u));
			};
			const uint32_t renderEyeWidth = renderScaleTargetActive ? scaleDimension(perfMode.trueHMDEyeWidth, renderScale) : perfMode.trueHMDEyeWidth;
			const uint32_t renderEyeHeight = renderScaleTargetActive ? scaleDimension(perfMode.trueHMDEyeHeight, renderScale) : perfMode.trueHMDEyeHeight;
			return fidelityFX.AreFSRResourcesCompatible(
				renderEyeWidth,
				renderEyeHeight,
				perfMode.trueHMDEyeWidth,
				perfMode.trueHMDEyeHeight,
				2u);
		};
		const bool preserveFSRResourcesForRelatch = canPreserveFSRResourcesForRelatch();
		if (!ResetVRVendorRuntimeResources(true, true, !preserveFSRResourcesForRelatch)) {
			if (IsSubmitStageDeviceLost() || MarkSubmitStageDeviceLostIfDeviceRemoved("render-target relatch vendor resource teardown")) {
				clearRelatchDelay();
				clearRelatchRetryLogs();
				return false;
			}

			requeueRelatch(kVRUpscalingTransitionApplyDelayFrames, false);
			if (relatchUpscaleMethod == UpscaleMethod::kDLSS)
				pendingDLSSReset.store(true, std::memory_order_release);
			if (!loggedRelatchVendorDefer) {
				logger::warn("[VRRenderScale] Render-target relatch deferred because vendor resources are still in use.");
				loggedRelatchVendorDefer = true;
			}
			return false;
		}

		perfMode.ResetBootLatch();
		perfModeAllowBootLatchCreate.store(true, std::memory_order_release);
		perfMode.EnsureBootLatch(settings, relatchUpscaleMethod, true);

		relatchRenderScaleActive = perfMode.IsActive(settings, relatchUpscaleMethod);
		relatchTargetDisplaySize = perfMode.GetDisplayScreenSize();
		relatchTargetEngineSize = relatchRenderScaleActive ?
		                              perfMode.GetRenderScreenSize() :
		                              relatchTargetDisplaySize;
		const bool renderTargetsAlreadySized = AreVRRenderScaleRenderTargetsSizedForDimensions(
			relatchTargetEngineSize,
			relatchTargetDisplaySize);
		if (renderTargetsAlreadySized) {
			state->screenSize = relatchTargetEngineSize;
			logger::debug(
				"[VRRenderScale] Skipped D3D render-target recreate; existing render targets already match {}x{} -> {}x{}.",
				ClampPositiveDimension(relatchTargetEngineSize.x),
				ClampPositiveDimension(relatchTargetEngineSize.y),
				ClampPositiveDimension(relatchTargetDisplaySize.x),
				ClampPositiveDimension(relatchTargetDisplaySize.y));
		} else {
			if (!Hooks::RecreateRenderTargetsForVRRenderScale()) {
				requeueRelatch(kVRRenderScaleRelatchBusyRetryFrames);
				if (!loggedRelatchD3DDefer) {
					logger::warn("[VRRenderScale] Render-target relatch could not run; will retry.");
					loggedRelatchD3DDefer = true;
				}
				return false;
			}
		}

		RecreateVendorRuntimeResources(relatchUpscaleMethod, relatchUpscaleMethod != UpscaleMethod::kFSR);

		if (relatchUpscaleMethod == UpscaleMethod::kDLSS) {
			pendingDLSSHistoryReset.store(true, std::memory_order_release);
			pendingDLSSReset.store(false, std::memory_order_release);
			pendingFSRReset.store(false, std::memory_order_release);
			vrDLSSSettingsRelatched.store(true, std::memory_order_release);
		} else if (relatchUpscaleMethod == UpscaleMethod::kFSR) {
			pendingFSRReset.store(!preserveFSRResourcesForRelatch, std::memory_order_release);
			pendingDLSSReset.store(false, std::memory_order_release);
			pendingDLSSHistoryReset.store(false, std::memory_order_release);
			vrDLSSSettingsRelatched.store(false, std::memory_order_release);
			if (preserveFSRResourcesForRelatch)
				RequestHistoryReset();
		} else {
			pendingDLSSReset.store(false, std::memory_order_release);
			pendingDLSSHistoryReset.store(false, std::memory_order_release);
			pendingFSRReset.store(false, std::memory_order_release);
			vrDLSSSettingsRelatched.store(false, std::memory_order_release);
		}
		RefreshRuntimeResolutionState();
	} catch (const std::exception& e) {
		if (!MarkSubmitStageDeviceLostIfNeeded(e, "render-target relatch")) {
			const uint32_t retryFrames = IsOutOfMemoryException(e) ?
			                                 kVRRenderScaleRelatchD3DFailureRetryFrames :
			                                 kVRRenderScaleRelatchBusyRetryFrames;
			requeueRelatch(retryFrames);
		} else {
			clearRelatchDelay();
			clearRelatchRetryLogs();
		}
		logger::error("[VRRenderScale] Render-target relatch failed: {}", e.what());
		return false;
	} catch (...) {
		if (!MarkSubmitStageDeviceLostIfDeviceRemoved("render-target relatch")) {
			requeueRelatch(kVRRenderScaleRelatchBusyRetryFrames);
		} else {
			clearRelatchDelay();
			clearRelatchRetryLogs();
		}
		logger::error("[VRRenderScale] Render-target relatch failed with an unknown exception");
		return false;
	}

	vrRenderScaleResourceTrackingSyncPending.store(true, std::memory_order_release);
	ClearSubmitStageFoveatedVendorRetryBackoff();
	if (IsVendorUpscalingMethod(relatchUpscaleMethod) && relatchRenderScaleActive) {
		ArmSubmitStageVendorResumeCooldown(std::max(state->frameCount, 1u));
	} else {
		ClearSubmitStageVendorResumeCooldown();
		CompleteVRRenderScaleInfoTransition(
			"render-target relatch",
			relatchRenderScaleActive,
			relatchUpscaleMethod,
			relatchTargetDisplaySize,
			relatchTargetEngineSize);
	}
	clearRelatchDelay();
	clearRelatchRetryLogs();
	logger::debug("[VRRenderScale] Applied render-target relatch");
	return true;
}

void Upscaling::ArmSubmitStageVendorResumeCooldown(uint32_t a_currentFrame)
{
	const uint32_t currentFrame = std::max(a_currentFrame, 1u);
	submitStageVendorResumeStartFrame.store(currentFrame, std::memory_order_release);
	submitStageVendorResumeStableFrames.store(0, std::memory_order_release);
	submitStageVendorResumeLastStableFrame.store(0, std::memory_order_release);
	submitStageVendorResumeFrame.store(currentFrame + kVRSubmitStageVendorRelatchCooldownFrames, std::memory_order_release);
}

void Upscaling::ClearSubmitStageVendorResumeCooldown()
{
	submitStageVendorResumeFrame.store(0, std::memory_order_release);
	submitStageVendorResumeStartFrame.store(0, std::memory_order_release);
	submitStageVendorResumeStableFrames.store(0, std::memory_order_release);
	submitStageVendorResumeLastStableFrame.store(0, std::memory_order_release);
}

void Upscaling::ArmSubmitStageFoveatedVendorRetryBackoff(uint32_t a_currentFrame)
{
	const uint32_t currentFrame = std::max(a_currentFrame, 1u);
	submitStageFoveatedVendorRetryFrame.store(currentFrame + kVRSubmitStageFoveatedFailureRetryFrames, std::memory_order_release);
}

void Upscaling::ClearSubmitStageFoveatedVendorRetryBackoff()
{
	submitStageFoveatedVendorRetryFrame.store(0, std::memory_order_release);
}

bool Upscaling::ResetVRSubmitStageState(bool a_destroyDLSSResources)
{
	if (!globals::game::isVR)
		return true;

	InvalidateFrameScopedUpscalingState();
	UnbindUpscalingResources();
	DestroyVRIntermediateTextures();
	DestroyFoveatedResources();
	ResetVRMenuFinalCompositeLayer();

	bool dlssResourcesDestroyed = true;
	if (a_destroyDLSSResources && streamline.initialized && streamline.featureDLSS && streamline.slDLSSSetOptions && streamline.slFreeResources) {
		dlssResourcesDestroyed = streamline.DestroyDLSSResources();
		if (!dlssResourcesDestroyed)
			MarkSubmitStageDeviceLostIfDeviceRemoved("VR submit-stage DLSS resource teardown");
	} else {
		streamline.InvalidateDLSSOptionsCache();
	}

	submitStagePreparedFrame = std::numeric_limits<uint32_t>::max();
	submitStagePreparedFramePresentationOnly = false;
	submitStagePreparedFrameFoveatedRegionEncode = false;
	submitStageVendorOutputFrame = std::numeric_limits<uint32_t>::max();
	submitStageVendorOutputSourceTexture = nullptr;
	submitStageVendorEyeState = {};
	submitStageForceFullEyeVendorFallback = false;
	ClearSubmitStageVendorResumeCooldown();
	ClearSubmitStageFoveatedVendorRetryBackoff();
	submitStageMirrorFrame = std::numeric_limits<uint32_t>::max();
	submitStageMirrorEyeReady = {};
	submitStageMirrorSourceTexture = nullptr;
	submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	submitStageFoveatedPeripheryTAAEyeReady = {};
	vrRenderScaleResourceTrackingSyncPending.store(false, std::memory_order_release);
	historyResetTrackingInitialized = false;
	historyResetLatchedFrame = std::numeric_limits<uint32_t>::max();
	historyResetThisFrame = false;
	RequestHistoryReset();
	return dlssResourcesDestroyed;
}

void Upscaling::BeginVRRenderScaleInfoTransition(const char* a_reason)
{
	if (!globals::game::isVR)
		return;

	auto* state = globals::state;
	if (!state)
		return;

	if (vrRenderScaleInfoTransitionPending)
		return;

	vrRenderScaleInfoTransitionPending = true;
	vrRenderScaleInfoTransitionStartFrame = std::max(state->frameCount, 1u);

	if (state->IsDeveloperMode()) {
		logger::debug(
			"[VRRenderScale] Transition timing started at frame {}{}{}",
			vrRenderScaleInfoTransitionStartFrame,
			a_reason && *a_reason ? ": " : "",
			a_reason && *a_reason ? a_reason : "");
	}
}

void Upscaling::CompleteVRRenderScaleInfoTransition(const char* a_phase, bool a_active, UpscaleMethod a_method, const float2& a_displaySize, const float2& a_renderSize)
{
	if (!globals::game::isVR || !vrRenderScaleInfoTransitionPending)
		return;

	auto* state = globals::state;
	if (!state)
		return;

	const uint32_t currentFrame = std::max(state->frameCount, 1u);
	const uint32_t elapsedFrames = ElapsedFrames(vrRenderScaleInfoTransitionStartFrame, currentFrame);
	const uint32_t displayWidth = ClampPositiveDimension(a_displaySize.x);
	const uint32_t displayHeight = ClampPositiveDimension(a_displaySize.y);
	const uint32_t renderWidth = ClampPositiveDimension(a_renderSize.x);
	const uint32_t renderHeight = ClampPositiveDimension(a_renderSize.y);

	logger::info(
		"[VRRenderScale] Stable after {} frame(s): state={} method={} render={}x{} display={}x{} quality={} phase={}",
		elapsedFrames,
		a_active ? "on" : "off",
		magic_enum::enum_name(a_method),
		renderWidth,
		renderHeight,
		displayWidth,
		displayHeight,
		GetRuntimeQualityMode(),
		a_phase && *a_phase ? a_phase : "unknown");

	ClearVRRenderScaleInfoTransition();
}

void Upscaling::ClearVRRenderScaleInfoTransition()
{
	vrRenderScaleInfoTransitionPending = false;
	vrRenderScaleInfoTransitionStartFrame = 0;
}

bool Upscaling::ResetVRVendorRuntimeResources(bool a_destroyDLSSResources, bool a_destroyPeripheryTAAResources, bool a_destroyFSRResources)
{
	if (!globals::game::isVR)
		return true;

	const bool destroyFSRResources = a_destroyFSRResources || pendingFSRReset.load(std::memory_order_acquire);
	if (destroyFSRResources && !fidelityFX.PollFSRResourceTeardownReady("VR vendor runtime FSR resource teardown")) {
		pendingFSRReset.store(true, std::memory_order_release);
		return false;
	}

	const bool submitStageReset = ResetVRSubmitStageState(a_destroyDLSSResources);
	if (!submitStageReset) {
		if (a_destroyDLSSResources)
			pendingDLSSReset.store(true, std::memory_order_release);
		return false;
	}

	if (destroyFSRResources)
		fidelityFX.DestroyFSRResources(false);
	DestroyCommonUpscalingTextures();
	if (a_destroyPeripheryTAAResources)
		DestroyPeripheryTAAResources();
	return true;
}

void Upscaling::RecreateVendorRuntimeResources(UpscaleMethod a_upscaleMethod, bool a_recreateTemporalResources)
{
	if (!IsVendorUpscalingMethod(a_upscaleMethod))
		return;

	CreateUpscalingTextureResources(a_upscaleMethod);
	if (a_recreateTemporalResources && a_upscaleMethod == UpscaleMethod::kFSR)
		fidelityFX.CreateFSRResources();
}

bool Upscaling::ApplyPendingVendorRuntimeReset(UpscaleMethod a_upscaleMethod, const char* a_context)
{
	if (!globals::game::isVR)
		return true;

	const bool dlssResetPending = pendingDLSSReset.load(std::memory_order_acquire);
	const bool fsrResetPending = pendingFSRReset.load(std::memory_order_acquire);
	const bool currentMethodDLSS = a_upscaleMethod == UpscaleMethod::kDLSS;
	const bool currentMethodFSR = a_upscaleMethod == UpscaleMethod::kFSR;
	const bool includeInactiveVendorReset = ShouldIncludeInactiveVRVendorReset(*this, a_upscaleMethod);
	const bool activeResetPending =
		(currentMethodDLSS && dlssResetPending) ||
		(currentMethodFSR && fsrResetPending);
	const bool inactiveResetPending =
		includeInactiveVendorReset &&
		((currentMethodDLSS && fsrResetPending) ||
			(currentMethodFSR && dlssResetPending));
	if (!activeResetPending && !inactiveResetPending)
		return true;

	if (IsUpscalingLoadTransitionContextActive(*this)) {
		return true;
	}
	if (pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
		perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire)) {
		return true;
	}

	const char* context = a_context ? a_context : "";
	bool retireDLSS = false;
	bool retireFSR = false;
	bool rebuildDLSS = false;
	bool rebuildFSR = false;

	static bool loggedVendorResetFailure = false;
	static bool loggedVendorResetDeferral = false;
	try {
		retireFSR = includeInactiveVendorReset && !currentMethodFSR && pendingFSRReset.exchange(false, std::memory_order_relaxed);
		if (retireFSR) {
			logger::debug("[Upscaling] Retiring {}inactive FSR resources before {} runtime reset", context, magic_enum::enum_name(a_upscaleMethod));
			if (!fidelityFX.PollFSRResourceTeardownReady("inactive FSR resource teardown before vendor runtime reset")) {
				pendingFSRReset.store(true, std::memory_order_release);
				retireFSR = false;
				LogWarnOnceFmt(
					loggedVendorResetDeferral,
					"[Upscaling] Deferred {}runtime reset because inactive FidelityFX resources are still in use",
					context);
				return false;
			}
			UnbindUpscalingResources();
			fidelityFX.DestroyFSRResources(false);
			RequestHistoryReset();
			retireFSR = false;
		}

		retireDLSS = includeInactiveVendorReset && !currentMethodDLSS && pendingDLSSReset.exchange(false, std::memory_order_relaxed);
		if (retireDLSS) {
			logger::debug("[Upscaling] Retiring {}inactive DLSS resources before {} runtime reset", context, magic_enum::enum_name(a_upscaleMethod));
			UnbindUpscalingResources();
			if (!streamline.DestroyDLSSResources()) {
				if (!MarkSubmitStageDeviceLostIfDeviceRemoved("inactive DLSS resource teardown before vendor runtime reset")) {
					pendingDLSSReset.store(true, std::memory_order_release);
					LogWarnOnceFmt(
						loggedVendorResetDeferral,
						"[Upscaling] Deferred {}runtime reset because inactive Streamline resources are still in use",
						context);
				}
				retireDLSS = false;
				return false;
			}
			pendingDLSSHistoryReset.store(false, std::memory_order_release);
			RequestHistoryReset();
			retireDLSS = false;
		}

		rebuildDLSS = currentMethodDLSS && pendingDLSSReset.exchange(false, std::memory_order_relaxed);
		rebuildFSR = currentMethodFSR && pendingFSRReset.exchange(false, std::memory_order_relaxed);
		if (!rebuildDLSS && !rebuildFSR)
			return true;

		if (rebuildDLSS) {
			logger::debug("[Upscaling] Rebuilding {}DLSS feature after VR reset", context);
			UnbindUpscalingResources();
			if (!streamline.DestroyDLSSResources()) {
				if (!MarkSubmitStageDeviceLostIfDeviceRemoved("vendor runtime DLSS resource teardown")) {
					pendingDLSSReset.store(true, std::memory_order_release);
					LogWarnOnceFmt(
						loggedVendorResetDeferral,
						"[Upscaling] Deferred rebuild of {}DLSS resources after VR reset because Streamline resources are still in use",
						context);
				}
				rebuildDLSS = false;
				return false;
			}
			RequestHistoryReset();
			rebuildDLSS = false;
		} else if (rebuildFSR) {
			logger::debug("[Upscaling] Rebuilding {}FSR resources after VR reset", context);
			if (!fidelityFX.PollFSRResourceTeardownReady("vendor runtime FSR resource teardown")) {
				pendingFSRReset.store(true, std::memory_order_release);
				LogWarnOnceFmt(
					loggedVendorResetDeferral,
					"[Upscaling] Deferred rebuild of {}FSR resources after VR reset because FidelityFX resources are still in use",
					context);
				rebuildFSR = false;
				return false;
			}
			UnbindUpscalingResources();
			fidelityFX.DestroyFSRResources(false);
			fidelityFX.CreateFSRResources();
			RequestHistoryReset();
			rebuildFSR = false;
		}
	} catch (const std::exception& e) {
		if (rebuildDLSS || retireDLSS)
			pendingDLSSReset.store(true, std::memory_order_release);
		if (rebuildFSR || retireFSR)
			pendingFSRReset.store(true, std::memory_order_release);
		MarkSubmitStageDeviceLostIfNeeded(e, "vendor runtime reset");
		LogWarnOnceFmt(
			loggedVendorResetFailure,
			"[Upscaling] Failed to rebuild {}{} resources after VR reset: {}",
			context,
			magic_enum::enum_name(a_upscaleMethod),
			e.what());
		return false;
	} catch (...) {
		if (rebuildDLSS || retireDLSS)
			pendingDLSSReset.store(true, std::memory_order_release);
		if (rebuildFSR || retireFSR)
			pendingFSRReset.store(true, std::memory_order_release);
		MarkSubmitStageDeviceLostIfDeviceRemoved("vendor runtime reset");
		LogWarnOnceFmt(
			loggedVendorResetFailure,
			"[Upscaling] Failed to rebuild {}{} resources after VR reset",
			context,
			magic_enum::enum_name(a_upscaleMethod));
		return false;
	}

	return true;
}

void Upscaling::RequestVRSubmitStageHistoryReset()
{
	if (!globals::game::isVR)
		return;

	RequestHistoryReset();
}

bool Upscaling::ApplyPendingPostLoadRuntimeReset(UpscaleMethod a_upscaleMethod)
{
	if (!postLoadRuntimeResetPending.load(std::memory_order_acquire))
		return true;

	auto* state = globals::state;
	if (IsSaveLoadTransitionContextActive(state)) {
		return true;
	}

	const bool renderScalePostLoadResetRelevant = IsVRRenderScalePostLoadResetRelevant(*this, a_upscaleMethod);
	if (!postLoadRuntimeResetPending.exchange(false, std::memory_order_acq_rel))
		return true;

	if (!globals::game::isVR)
		return true;

	if (!renderScalePostLoadResetRelevant)
		return true;

	logger::debug("[Upscaling] Applying VR post-load runtime reset for method {}",
		magic_enum::enum_name(a_upscaleMethod));

	try {
		if (!ResetVRVendorRuntimeResources(true, false)) {
			logger::warn("[Upscaling] VR post-load runtime reset deferred because vendor resources are still in use");
			postLoadRuntimeResetPending.store(true, std::memory_order_release);
			return false;
		}
		RecreateVendorRuntimeResources(a_upscaleMethod, true);
	} catch (const std::exception& e) {
		logger::error("[Upscaling] VR post-load runtime reset failed: {}", e.what());
		if (!MarkSubmitStageDeviceLostIfNeeded(e, "VR post-load runtime reset"))
			postLoadRuntimeResetPending.store(true, std::memory_order_release);
		return false;
	} catch (...) {
		logger::error("[Upscaling] VR post-load runtime reset failed with an unknown exception");
		if (!MarkSubmitStageDeviceLostIfDeviceRemoved("VR post-load runtime reset"))
			postLoadRuntimeResetPending.store(true, std::memory_order_release);
		return false;
	}

	logger::debug("[Upscaling] Applied VR post-load runtime reset");
	return true;
}

bool Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
{
	resourceCheckLastCompletedFrame = std::numeric_limits<uint32_t>::max();
	resourceCheckLastCompletedMethod = UpscaleMethod::kNONE;
	resourceCheckStable = false;
	resourceCheckStableMethod = UpscaleMethod::kNONE;
	resourceCheckStableKey = 0;

	struct FoveatedLayoutKey
	{
		int32_t centerScaleQ = 0;
		int32_t centerHorizontalScaleQ = 0;
		int32_t centerFeatherQ = 0;
		std::array<int32_t, 4> centerOffsetQ{};
	};

	const auto makeFoveatedLayoutKey = [&](bool usePeripheryTAAProfile, bool usePeripheryTAAPath) {
		const auto profile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
		const float centerFeather = usePeripheryTAAPath ?
		                                ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) :
		                                FoveatedCommon::kCenterFeather;
		const auto centerOffsets = GetResolvedFoveatedMaskCenterOffsets(usePeripheryTAAProfile);

		FoveatedLayoutKey key{};
		key.centerScaleQ = QuantizePeripheryTAATileParam(profile.centerScale);
		key.centerHorizontalScaleQ = QuantizePeripheryTAATileParam(profile.centerHorizontalScale);
		key.centerFeatherQ = QuantizePeripheryTAATileParam(centerFeather);
		key.centerOffsetQ = {
			QuantizePeripheryTAATileParam(centerOffsets[0].x),
			QuantizePeripheryTAATileParam(centerOffsets[0].y),
			QuantizePeripheryTAATileParam(centerOffsets[1].x),
			QuantizePeripheryTAATileParam(centerOffsets[1].y)
		};
		return key;
	};
	const auto getTrackedRenderScaleMode = [&]() {
		if (!globals::game::isVR)
			return IsRenderScaleModeRequested() ? 1u : 0u;

		if (!IsRenderScaleMethodEligible(a_upscalemethod))
			return 0u;

		return ClampToggleUInt(settings.renderScaleMode);
	};

	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	static bool previousFrameGenMode = false;
	static bool previousFoveatedDispatch = false;
	static bool previousPeripheryTAA = false;
	static bool previousFSRRuntimePathActive = false;
	static bool previousFSRRuntimeFsr4Configured = false;
	static bool previousFSRRuntimeFsr4Active = false;
	static uint32_t previousQualityMode = GetRuntimeQualityMode();
	static uint32_t previousDLSSPreset = std::min<uint>(settings.dlssPreset, kDLSSPresetMaxIndex);
	static uint32_t previousRenderScaleMode = getTrackedRenderScaleMode();
	static uint32_t previousPerfMode = ClampToggleUInt(settings.perfMode);
	static FoveatedLayoutKey previousFoveatedLayout = makeFoveatedLayoutKey(settings.periphery_taa_enable, settings.periphery_taa_enable && !settings.foveatedPeripheryMaskVisualization);

	bool frameGenModeCurrent = (settings.frameGenerationMode && d3d12SwapChainActive);
	bool frameGenModeChanged = frameGenModeCurrent != previousFrameGenMode;
	bool upscaleModeChanged = (previousUpscaleMode != a_upscalemethod);
	const uint32_t qualityModeCurrent = GetRuntimeQualityMode();
	const uint32_t dlssPresetCurrent = std::min<uint>(settings.dlssPreset, kDLSSPresetMaxIndex);
	const uint32_t renderScaleModeCurrent = getTrackedRenderScaleMode();
	const uint32_t perfModeCurrent = ClampToggleUInt(settings.perfMode);
	const bool qualityModeChanged = previousQualityMode != qualityModeCurrent;
	const bool dlssPresetChanged = previousDLSSPreset != dlssPresetCurrent;
	const bool renderScaleModeChanged = previousRenderScaleMode != renderScaleModeCurrent;
	const bool perfModeChanged = previousPerfMode != perfModeCurrent;
	const bool dlssQualityModeChanged = qualityModeChanged && (previousUpscaleMode == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kDLSS);
	const bool dlssPresetResourceChanged = dlssPresetChanged && (previousUpscaleMode == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kDLSS);
	const bool dlssResourceSettingsChanged = dlssQualityModeChanged || dlssPresetResourceChanged;
	const bool dlssOptionSettingsChanged =
		dlssResourceSettingsChanged &&
		previousUpscaleMode == UpscaleMethod::kDLSS &&
		a_upscalemethod == UpscaleMethod::kDLSS;
	const bool fsrQualityModeChanged = qualityModeChanged && (previousUpscaleMode == UpscaleMethod::kFSR || a_upscalemethod == UpscaleMethod::kFSR);
	const bool foveatedDispatchCurrent = IsFoveatedVendorDispatchEnabled(a_upscalemethod);
	const bool peripheryTAACurrent = IsPeripheryTAAEnabled(a_upscalemethod);
	const bool peripheryTAAPathCurrent = IsPeripheryTAAPathActive(a_upscalemethod);
	const bool fsrRuntimePathCurrent = IsFSRRuntimePathActive(a_upscalemethod);
	const bool fsrRuntimeFsr4Configured =
		a_upscalemethod == UpscaleMethod::kFSR &&
		settings.fsr4RuntimeEnable &&
		fidelityFX.IsRuntimeFsr4Available();
	const bool fsrRuntimeFsr4Current = IsFSRRuntimeFsr4PathActive(a_upscalemethod);
	const FoveatedLayoutKey foveatedLayoutCurrent = makeFoveatedLayoutKey(peripheryTAACurrent, peripheryTAAPathCurrent);
	const bool compareFoveatedScale = foveatedDispatchCurrent || previousFoveatedDispatch;
	const bool foveatedDispatchToggleChanged = previousFoveatedDispatch != foveatedDispatchCurrent;
	const bool foveatedGeometryChanged =
		compareFoveatedScale &&
		(previousFoveatedLayout.centerScaleQ != foveatedLayoutCurrent.centerScaleQ ||
			previousFoveatedLayout.centerHorizontalScaleQ != foveatedLayoutCurrent.centerHorizontalScaleQ ||
			previousFoveatedLayout.centerFeatherQ != foveatedLayoutCurrent.centerFeatherQ ||
			previousFoveatedLayout.centerOffsetQ != foveatedLayoutCurrent.centerOffsetQ);
	const bool foveatedDispatchChanged = foveatedDispatchToggleChanged || foveatedGeometryChanged;
	const bool peripheryTAAChanged = previousPeripheryTAA != peripheryTAACurrent;
	const bool compareFSRRuntimePath = a_upscalemethod == UpscaleMethod::kFSR || previousUpscaleMode == UpscaleMethod::kFSR;
	const bool fsrRuntimePathChanged = compareFSRRuntimePath && previousFSRRuntimePathActive != fsrRuntimePathCurrent;
	const bool fsrRuntimeFsr4ConfiguredChanged =
		compareFSRRuntimePath &&
		(fsrRuntimePathCurrent || previousFSRRuntimePathActive) &&
		previousFSRRuntimeFsr4Configured != fsrRuntimeFsr4Configured;
	const bool fsrRuntimeVersionChanged =
		compareFSRRuntimePath &&
		(fsrRuntimePathCurrent || previousFSRRuntimePathActive) &&
		previousFSRRuntimeFsr4Active != fsrRuntimeFsr4Current;
	const bool fsrRuntimeFoveatedLayoutChanged =
		a_upscalemethod == UpscaleMethod::kFSR &&
		fsrRuntimePathCurrent &&
		foveatedDispatchChanged;
	const auto canPreserveFSRResourcesForCurrentVRPlan = [&]() {
		if (!globals::game::isVR ||
			!fidelityFX.HasFSRResources() ||
			runtimeResolutionPlan.finalOutputSize.x <= 0.0f ||
			runtimeResolutionPlan.finalOutputSize.y <= 0.0f ||
			runtimeResolutionPlan.engineRenderSize.x <= 0.0f ||
			runtimeResolutionPlan.engineRenderSize.y <= 0.0f) {
			return false;
		}

		const uint32_t displayWidthPerEye = std::max<uint32_t>(1u, ClampPositiveDimension(runtimeResolutionPlan.finalOutputSize.x) / 2u);
		const uint32_t displayHeight = ClampPositiveDimension(runtimeResolutionPlan.finalOutputSize.y);
		const uint32_t renderWidthPerEye = std::max<uint32_t>(1u, ClampPositiveDimension(runtimeResolutionPlan.engineRenderSize.x) / 2u);
		const uint32_t renderHeight = ClampPositiveDimension(runtimeResolutionPlan.engineRenderSize.y);
		return fidelityFX.AreFSRResourcesCompatible(renderWidthPerEye, renderHeight, displayWidthPerEye, displayHeight, 2u);
	};
	const bool vrFSRQualityChangeCanPreserveResources =
		fsrQualityModeChanged &&
		previousUpscaleMode == UpscaleMethod::kFSR &&
		a_upscalemethod == UpscaleMethod::kFSR &&
		!fsrRuntimePathChanged &&
		!fsrRuntimeFsr4ConfiguredChanged &&
		!fsrRuntimeVersionChanged &&
		!pendingFSRReset.load(std::memory_order_acquire) &&
		canPreserveFSRResourcesForCurrentVRPlan();
	const bool renderScaleTransitionRelevant = ShouldIncludeInactiveVRVendorReset(*this, a_upscalemethod);
	const bool resourceChangeDetected =
		upscaleModeChanged ||
		frameGenModeChanged ||
		foveatedDispatchChanged ||
		peripheryTAAChanged ||
		fsrRuntimePathChanged ||
		fsrRuntimeFsr4ConfiguredChanged ||
		fsrRuntimeVersionChanged ||
		qualityModeChanged ||
		dlssPresetResourceChanged ||
		renderScaleModeChanged ||
		perfModeChanged;
	const bool vrRenderScaleRelatchOwnsResourceChange =
		globals::game::isVR &&
		(pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
			perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire));
	const bool resourceChangeOwnedByVRRenderScaleRelatch =
		!frameGenModeChanged &&
		!foveatedDispatchChanged &&
		!peripheryTAAChanged &&
		!fsrRuntimePathChanged &&
		!fsrRuntimeFsr4ConfiguredChanged &&
		!fsrRuntimeVersionChanged;
	const bool vrRenderScaleRelatchCanSyncResourceChange =
		globals::game::isVR &&
		vrRenderScaleResourceTrackingSyncPending.load(std::memory_order_acquire) &&
		!HasPendingVRUpscalingTransition() &&
		!vrRenderScaleRelatchOwnsResourceChange &&
		resourceChangeOwnedByVRRenderScaleRelatch;
	auto syncResourceChangeTracking = [&]() {
		previousUpscaleMode = a_upscalemethod;
		previousFrameGenMode = (settings.frameGenerationMode && d3d12SwapChainActive);
		previousFoveatedDispatch = foveatedDispatchCurrent;
		previousPeripheryTAA = peripheryTAACurrent;
		previousFSRRuntimePathActive = fsrRuntimePathCurrent;
		previousFSRRuntimeFsr4Configured = fsrRuntimeFsr4Configured;
		previousFSRRuntimeFsr4Active = fsrRuntimeFsr4Current;
		previousQualityMode = qualityModeCurrent;
		previousDLSSPreset = dlssPresetCurrent;
		previousRenderScaleMode = renderScaleModeCurrent;
		previousPerfMode = perfModeCurrent;
		previousFoveatedLayout = foveatedLayoutCurrent;
		previousVendorUpscalerSelected = a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR;
	};

	if (resourceChangeDetected && vrRenderScaleRelatchCanSyncResourceChange) {
		syncResourceChangeTracking();
		vrDLSSSettingsRelatched.store(false, std::memory_order_release);
		vrRenderScaleResourceTrackingSyncPending.store(false, std::memory_order_release);
	} else if (resourceChangeDetected && vrRenderScaleRelatchOwnsResourceChange) {
		return false;
	} else if (resourceChangeDetected) {
		logger::debug("[Upscaling] Resource change detected - Upscale: {} ({}) -> {} ({}), Quality: {} -> {}, DLSSPreset: {} -> {}, SubmitStage: {} -> {}, VRRenderScaleLatch: {} -> {}, FrameGen: {} -> {} (d3d12Active={}), FSRRuntimePath: {} -> {}",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode), static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod),
			previousQualityMode, qualityModeCurrent, previousDLSSPreset, dlssPresetCurrent, previousRenderScaleMode, renderScaleModeCurrent, previousPerfMode, perfModeCurrent, previousFrameGenMode, frameGenModeCurrent, d3d12SwapChainActive, previousFSRRuntimePathActive, fsrRuntimePathCurrent);

		const bool requiresFullPipelineUnbind =
			upscaleModeChanged ||
			renderScaleModeChanged ||
			perfModeChanged ||
			frameGenModeChanged ||
			fsrRuntimePathChanged ||
			fsrRuntimeFsr4ConfiguredChanged ||
			(fsrRuntimeVersionChanged && !fidelityFX.IsRuntimeFsr4FailureLatched()) ||
			dlssResourceSettingsChanged ||
			fsrQualityModeChanged;
		if (requiresFullPipelineUnbind)
			UnbindUpscalingResources();

		if (renderScaleModeChanged) {
			if (globals::game::isVR) {
				ResetVRSubmitStageState(false);
				DestroyPeripheryTAAResources();
			} else {
				RequestHistoryReset();
			}
		}

		if (perfModeChanged) {
			if (globals::game::isVR) {
				ResetVRSubmitStageState(false);
				DestroyPeripheryTAAResources();
			} else {
				RequestHistoryReset();
			}
		}

		const auto createFSRResourcesWhenSafe = [&]() {
			fidelityFX.CreateFSRResources();
			return true;
		};

		bool fsrResourcesDestroyedForQuality = false;
		bool fsrResourcesRecreatedForQuality = false;
		if (qualityModeChanged || dlssPresetResourceChanged) {
			const auto destroyVRQualityResources = [&]() {
				if (!globals::game::isVR)
					return;
				DestroyVRIntermediateTextures();
				DestroyFoveatedResources();
			};

			RequestHistoryReset();
			if (dlssResourceSettingsChanged) {
				if (globals::game::isVR && a_upscalemethod == UpscaleMethod::kDLSS) {
					const bool relatchAlreadyRebuiltDLSS =
						previousUpscaleMode == UpscaleMethod::kDLSS &&
						vrDLSSSettingsRelatched.exchange(false, std::memory_order_acq_rel);
					pendingDLSSHistoryReset.store(true, std::memory_order_relaxed);
					if (!relatchAlreadyRebuiltDLSS) {
						pendingDLSSReset.store(true, std::memory_order_release);
						destroyVRQualityResources();
						DestroyPeripheryTAAResources();
					}
				}
				if (!(globals::game::isVR && previousUpscaleMode == UpscaleMethod::kDLSS && a_upscalemethod == UpscaleMethod::kDLSS))
					streamline.InvalidateDLSSOptionsCache();
				if (!dlssOptionSettingsChanged && qualityModeChanged && a_upscalemethod != UpscaleMethod::kDLSS)
					destroyVRQualityResources();
			} else if (fsrQualityModeChanged) {
				vrDLSSSettingsRelatched.store(false, std::memory_order_release);
				destroyVRQualityResources();
				if (vrFSRQualityChangeCanPreserveResources) {
					RequestHistoryReset();
				} else {
					fidelityFX.DestroyFSRResources();
					fsrResourcesDestroyedForQuality = true;
				}
				if (a_upscalemethod == UpscaleMethod::kFSR && !vrFSRQualityChangeCanPreserveResources) {
					fsrResourcesRecreatedForQuality = createFSRResourcesWhenSafe();
				}
			} else {
				vrDLSSSettingsRelatched.store(false, std::memory_order_release);
			}
		} else {
			vrDLSSSettingsRelatched.store(false, std::memory_order_release);
		}

		// Destroy previous vendor resources even for Native AA/DLAA, where the method is selected
		// but IsUpscalingActive() is false because the render scale is 1:1.
		if (upscaleModeChanged) {
			if (previousVendorUpscalerSelected) {
				if (previousUpscaleMode == UpscaleMethod::kDLSS) {
					if (!streamline.DestroyDLSSResources() &&
						!MarkSubmitStageDeviceLostIfDeviceRemoved("upscale method DLSS resource teardown")) {
						pendingDLSSReset.store(renderScaleTransitionRelevant, std::memory_order_release);
					}
				} else if (previousUpscaleMode == UpscaleMethod::kFSR && !fsrResourcesDestroyedForQuality) {
					if (renderScaleTransitionRelevant &&
						!fidelityFX.PollFSRResourceTeardownReady("upscale method FSR resource teardown")) {
						pendingFSRReset.store(true, std::memory_order_release);
					} else {
						fidelityFX.DestroyFSRResources(!renderScaleTransitionRelevant);
					}
				}

				if (globals::game::isVR && !fsrResourcesDestroyedForQuality) {
					DestroyVRIntermediateTextures();
				}
			}
			DestroyUpscalingTextureResources(a_upscalemethod);

			if (a_upscalemethod == UpscaleMethod::kFSR && !fsrResourcesRecreatedForQuality)
				fsrResourcesRecreatedForQuality = createFSRResourcesWhenSafe();
		}

		// Create new upscaling method resources
		if (upscaleModeChanged) {
			CreateUpscalingTextureResources(a_upscalemethod);
		}

		// Host FSR 3.1.5 and runtime upscaler providers keep separate temporal state; rebuild on path changes.
		if (!upscaleModeChanged && fsrRuntimePathChanged && a_upscalemethod == UpscaleMethod::kFSR && !fsrResourcesRecreatedForQuality) {
			fidelityFX.DestroyFSRResources();
			fsrResourcesRecreatedForQuality = createFSRResourcesWhenSafe();
			RequestHistoryReset();
		} else if (!upscaleModeChanged && (fsrRuntimeFsr4ConfiguredChanged || fsrRuntimeVersionChanged) && a_upscalemethod == UpscaleMethod::kFSR && !fsrResourcesRecreatedForQuality) {
			if (fsrRuntimeFsr4ConfiguredChanged || !fidelityFX.IsRuntimeFsr4FailureLatched())
				fidelityFX.ResetRuntimeUpscalerResources(true);
			RequestHistoryReset();
		} else if (!upscaleModeChanged && fsrRuntimeFoveatedLayoutChanged && !fsrResourcesRecreatedForQuality) {
			// Keep runtime contexts alive; the dispatch reset flag is enough for layout-only temporal changes.
			RequestHistoryReset();
		}

		if (upscaleModeChanged || foveatedDispatchChanged) {
			if (!foveatedDispatchCurrent)
				DestroyFoveatedResources();
		}

		if ((upscaleModeChanged || foveatedDispatchChanged || peripheryTAAChanged) && !peripheryTAACurrent) {
			DestroyPeripheryTAAResources();
		}

		syncResourceChangeTracking();
		vrRenderScaleResourceTrackingSyncPending.store(false, std::memory_order_release);
	} else if (vrRenderScaleResourceTrackingSyncPending.load(std::memory_order_acquire)) {
		vrDLSSSettingsRelatched.store(false, std::memory_order_release);
		vrRenderScaleResourceTrackingSyncPending.store(false, std::memory_order_release);
	}

	if (!AreCommonVendorTexturesReady(a_upscalemethod)) {
		const bool deferForVRRenderScaleRelatch =
			globals::game::isVR &&
			IsVRRenderScaleTransitionSafetyRelevant(*this, a_upscalemethod) &&
			(pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
				perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) ||
				HasPendingVRVendorRuntimeReset(*this, a_upscalemethod));

		if (deferForVRRenderScaleRelatch) {
			return false;
		}

		const auto requeueMissingCommonRecreate = [&]() {
			pendingPerfModeRenderTargetRecreate.store(true, std::memory_order_release);
			MarkPerfModeRenderTargetRecreateQueued(kVRRenderScaleRelatchBusyRetryFrames);
			if (a_upscalemethod == UpscaleMethod::kDLSS)
				pendingDLSSReset.store(true, std::memory_order_release);
			else if (a_upscalemethod == UpscaleMethod::kFSR)
				pendingFSRReset.store(true, std::memory_order_release);
		};

		try {
			logger::debug("[Upscaling] Recreating missing common texture resources for method {}", magic_enum::enum_name(a_upscalemethod));
			DestroyCommonUpscalingTextures();
			CreateUpscalingTextureResources(a_upscalemethod);
		} catch (const std::exception& e) {
			if (globals::game::isVR && IsVRRenderScaleTransitionSafetyRelevant(*this, a_upscalemethod)) {
				if (MarkSubmitStageDeviceLostIfNeeded(e, "missing common texture recreation"))
					return false;

				requeueMissingCommonRecreate();
				logger::warn(
					"[VRRenderScale] Deferred missing common texture recreation for {} after failure: {}",
					magic_enum::enum_name(a_upscalemethod),
					e.what());
				return false;
			}
			throw;
		} catch (...) {
			if (globals::game::isVR && IsVRRenderScaleTransitionSafetyRelevant(*this, a_upscalemethod)) {
				if (MarkSubmitStageDeviceLostIfDeviceRemoved("missing common texture recreation"))
					return false;

				requeueMissingCommonRecreate();
				logger::warn(
					"[VRRenderScale] Deferred missing common texture recreation for {} after an unknown failure",
					magic_enum::enum_name(a_upscalemethod));
				return false;
			}
			throw;
		}
	}

	resourceCheckLastCompletedFrame = GetFrameScopedUpscalingWorkFrame();
	resourceCheckLastCompletedMethod = a_upscalemethod;
	resourceCheckStable = true;
	resourceCheckStableMethod = a_upscalemethod;
	resourceCheckStableKey = BuildResourceCheckStableKey(*this, a_upscalemethod);
	return true;
}

bool Upscaling::EnsureResourcesCurrent(UpscaleMethod a_upscalemethod)
{
	const uint32_t currentFrame = GetFrameScopedUpscalingWorkFrame();
	if (currentFrame != std::numeric_limits<uint32_t>::max() &&
		resourceCheckLastCompletedFrame == currentFrame &&
		resourceCheckLastCompletedMethod == a_upscalemethod) {
		return true;
	}

	const bool fsrResourcesNeedRetirement =
		a_upscalemethod != UpscaleMethod::kFSR &&
		fidelityFX.HasFSRResourcesPendingTeardown();
	if (resourceCheckStable &&
		resourceCheckStableMethod == a_upscalemethod &&
		!pendingDLSSReset.load(std::memory_order_acquire) &&
		!pendingFSRReset.load(std::memory_order_acquire) &&
		!fsrResourcesNeedRetirement &&
		!pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) &&
		!perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) &&
		!vrRenderScaleResourceTrackingSyncPending.load(std::memory_order_acquire) &&
		!HasPendingVRUpscalingTransition() &&
		AreCommonVendorTexturesReady(a_upscalemethod)) {
		const uint64_t stableKey = BuildResourceCheckStableKey(*this, a_upscalemethod);
		if (resourceCheckStableKey == stableKey) {
			resourceCheckLastCompletedFrame = currentFrame;
			resourceCheckLastCompletedMethod = a_upscalemethod;
			return true;
		}
	}

	return CheckResources(a_upscalemethod);
}

ID3D11ComputeShader* Upscaling::GetEncodeTexturesCS()
{
	auto upscaleMethod = GetRuntimeUpscaleMethod();
	uint methodIndex = (uint)upscaleMethod;

	// VR FSR requires a depth-output variant so we can feed FidelityFX a typed
	// R32_FLOAT depth texture instead of relying on typeless depth resources.
	if (globals::game::isVR && upscaleMethod == UpscaleMethod::kFSR) {
		if (!encodeTexturesCSDepthOutput) {
			logger::debug("Compiling EncodeTexturesCS.hlsl for VR FSR (FSR + DEPTH_OUTPUT)");
			std::vector<std::pair<const char*, const char*>> defines = {
				{ "FSR", "" },
				{ "DEPTH_OUTPUT", "" }
			};
			encodeTexturesCSDepthOutput.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", defines, "cs_5_0"));
		}
		return encodeTexturesCSDepthOutput.get();
	}

	if (!encodeTexturesCS[methodIndex]) {
		logger::debug("Compiling EncodeTexturesCS.hlsl for upscale method {}", methodIndex);

		std::vector<std::pair<const char*, const char*>> defines;

		// Add upscale method define
		switch (upscaleMethod) {
		case UpscaleMethod::kDLSS:
			defines.push_back({ "DLSS", "" });
			break;
		case UpscaleMethod::kFSR:
			defines.push_back({ "FSR", "" });
			break;
		default:
			// No define for NONE or TAA
			break;
		}

		encodeTexturesCS[methodIndex].attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", defines, "cs_5_0"));
	}
	return encodeTexturesCS[methodIndex].get();
}

ID3D11PixelShader* Upscaling::GetDepthRefractionUpscalePS()
{
	if (!depthRefractionUpscalePS) {
		logger::debug("Compiling DepthRefractionUpscalePS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		depthRefractionUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/DepthRefractionUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return depthRefractionUpscalePS.get();
}

ID3D11PixelShader* Upscaling::GetUnderwaterMaskUpscalePS(bool a_useRawSceneDepth)
{
	auto& shader = a_useRawSceneDepth ? underwaterMaskUpscaleRawDepthNoStencilPS : underwaterMaskUpscalePS;
	if (!shader) {
		logger::debug("Compiling UnderwaterMaskPS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		if (globals::game::isVR) {
			defines.push_back({ "VR", "" });
			if (a_useRawSceneDepth) {
				defines.push_back({ "NO_HMD_STENCIL_MASK", "" });
				defines.push_back({ "RAW_SCENE_DEPTH", "" });
			}
		}
		shader.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UnderwaterMaskUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return shader.get();
}

ID3D11VertexShader* Upscaling::GetUpscaleVS()
{
	if (!upscaleVS) {
		logger::debug("Compiling UpscaleVS.hlsl");
		upscaleVS.attach((ID3D11VertexShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UpscaleVS.hlsl", { { "VSHADER", "" } }, "vs_5_0"));
	}

	return upscaleVS.get();
}

ID3D11ComputeShader* Upscaling::GetFoveatedPeripheryCS()
{
	if (!foveatedPeripheryCS) {
		logger::debug("Compiling FoveatedPeripheryCS.hlsl");
		foveatedPeripheryCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/FoveatedPeripheryCS.hlsl", {}, "cs_5_0"));
	}

	return foveatedPeripheryCS.get();
}

ID3D11ComputeShader* Upscaling::GetFoveatedCenterBlendCS()
{
	if (!foveatedCenterBlendCS) {
		logger::debug("Compiling FoveatedCenterBlendCS.hlsl");
		foveatedCenterBlendCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl", {}, "cs_5_0"));
	}

	return foveatedCenterBlendCS.get();
}

ID3D11ComputeShader* Upscaling::GetPeripheryTAACS()
{
	if (!peripheryTAACS) {
		logger::debug("Compiling PeripheryTAACS.hlsl");
		peripheryTAACS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/PeripheryTAACS.hlsl", {}, "cs_5_0"));
	}

	return peripheryTAACS.get();
}

ID3D11ComputeShader* Upscaling::GetSubmitStageStretchCS()
{
	if (!submitStageStretchCS) {
		logger::debug("Compiling SubmitStageStretchCS.hlsl");
		submitStageStretchCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/SubmitStageStretchCS.hlsl", {}, "cs_5_0"));
	}

	return submitStageStretchCS.get();
}

ID3D11PixelShader* Upscaling::GetVRDesktopMirrorBlitPS()
{
	if (!vrDesktopMirrorBlitPS) {
		logger::debug("Compiling VRDesktopMirrorBlitPS.hlsl");
		vrDesktopMirrorBlitPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/VRDesktopMirrorBlitPS.hlsl", {}, "ps_5_0"));
	}

	return vrDesktopMirrorBlitPS.get();
}

ID3D11PixelShader* Upscaling::GetVRMenuLayerCompositePS()
{
	if (!vrMenuLayerCompositePS) {
		logger::debug("Compiling VRMenuLayerCompositePS.hlsl");
		vrMenuLayerCompositePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/VRMenuLayerCompositePS.hlsl", {}, "ps_5_0"));
	}

	return vrMenuLayerCompositePS.get();
}

eastl::unique_ptr<Texture2D> Upscaling::CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
	bool copyBindFlags, bool createSRV, bool createUAV, const char* name, bool createRTV)
{
	D3D11_TEXTURE2D_DESC srcDesc;
	static_cast<ID3D11Texture2D*>(src)->GetDesc(&srcDesc);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = srcDesc.Format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = copyBindFlags ? srcDesc.BindFlags : (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
	if (createRTV)
		desc.BindFlags |= D3D11_BIND_RENDER_TARGET;

	auto tex = eastl::make_unique<Texture2D>(desc);

	if (name) {
		Util::SetResourceName(tex->resource.get(), name);
	}

	if (createSRV) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = srcDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		tex->CreateSRV(srvDesc);
	}
	if (createUAV) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = srcDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		tex->CreateUAV(uavDesc);
	}
	if (createRTV) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = srcDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		tex->CreateRTV(rtvDesc);
	}
	return tex;
}

bool Upscaling::IsFoveatedVendorDispatchEnabled(UpscaleMethod a_upscaleMethod) const
{
	if (!IsFoveatedVendorDispatchRequested(settings, a_upscaleMethod))
		return false;

	const bool usePeripheryTAAProfile = settings.periphery_taa_enable;
	const float centerScale = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile).centerScale;
	// 1.0 is effectively full-frame vendor dispatch, so keep the default path.
	return FoveatedCommon::IsActiveCoverage(centerScale);
}

bool Upscaling::IsFSRRuntimePathActive(UpscaleMethod a_upscaleMethod) const
{
	return a_upscaleMethod == UpscaleMethod::kFSR &&
	       !fidelityFX.IsRuntimeUpscalerFailureLatched() &&
	       fidelityFX.ShouldUseRuntimeUpscalerForFSR();
}

bool Upscaling::IsFSRRuntimeFsr4PathActive(UpscaleMethod a_upscaleMethod) const
{
	return a_upscaleMethod == UpscaleMethod::kFSR &&
	       !fidelityFX.IsRuntimeUpscalerFailureLatched() &&
	       fidelityFX.ShouldRequestRuntimeFsr4();
}

bool Upscaling::IsPeripheryTAAEnabled(UpscaleMethod a_upscaleMethod) const
{
	return IsFoveatedVendorDispatchEnabled(a_upscaleMethod) && settings.periphery_taa_enable;
}

bool Upscaling::IsPeripheryTAAPathActive(UpscaleMethod a_upscaleMethod) const
{
	return IsPeripheryTAAEnabled(a_upscaleMethod) && !settings.foveatedPeripheryMaskVisualization;
}

bool Upscaling::UseActiveFoveatedPeripheryTAAProfile() const
{
	const auto upscaleMethod = GetRuntimeUpscaleMethod();
	return IsPeripheryTAAEnabled(upscaleMethod);
}

bool Upscaling::IsActiveUpscalingFoveatedProfileAvailable() const
{
	return IsFoveatedVendorDispatchEnabled(GetRuntimeUpscaleMethod());
}

const char* Upscaling::GetFoveatedUpscalingModeName(FoveatedUpscalingMode a_mode)
{
	switch (a_mode) {
	case FoveatedUpscalingMode::CenterOnly:
		return "FOV only";
	case FoveatedUpscalingMode::PeripheralTAA:
		return "FOV + TAA";
	case FoveatedUpscalingMode::Disabled:
	default:
		return "Off";
	}
}

Upscaling::ActiveUpscalingFoveatedProfile Upscaling::GetActiveUpscalingFoveatedProfile() const
{
	ActiveUpscalingFoveatedProfile profile{};
	const auto upscaleMethod = GetRuntimeUpscaleMethod();
	profile.available = IsFoveatedVendorDispatchEnabled(upscaleMethod);
	if (!profile.available)
		return profile;

	profile.usesPeripheryTAAOuterMask = IsPeripheryTAAEnabled(upscaleMethod);
	profile.mode = profile.usesPeripheryTAAOuterMask ? FoveatedUpscalingMode::PeripheralTAA : FoveatedUpscalingMode::CenterOnly;

	const auto maskParams = GetFoveatedMaskProfileParams(settings, profile.usesPeripheryTAAOuterMask);
	profile.vendorCenterScale = maskParams.centerScale;

	if (profile.usesPeripheryTAAOuterMask) {
		profile.sharedVisibleScale = ClampPeripheryTAAOuterScaleForCenter(
			settings.periphery_taa_outer_scale,
			settings.periphery_taa_center_area);
	} else {
		profile.sharedVisibleScale = maskParams.centerScale;
	}

	profile.centerHorizontalScale = maskParams.centerHorizontalScale;
	profile.centerOffsets = GetResolvedFoveatedMaskCenterOffsets(profile.usesPeripheryTAAOuterMask);
	if (!globals::game::isVR)
		profile.centerOffsets[1] = { 0.0f, 0.0f };
	return profile;
}

float Upscaling::GetActiveFoveatedSharedVisibleScale() const
{
	return GetActiveUpscalingFoveatedProfile().sharedVisibleScale;
}

float Upscaling::GetActiveFoveatedCenterHorizontalScale() const
{
	if (!globals::game::isVR)
		return 1.0f;

	return GetActiveUpscalingFoveatedProfile().centerHorizontalScale;
}

float2 Upscaling::GetDefaultFoveatedMaskCenterOffset(uint32_t eyeIndex) const
{
	(void)eyeIndex;
	return { 0.0f, 0.0f };
}

float2 Upscaling::GetResolvedFoveatedMaskCenterOffset(uint32_t eyeIndex, bool usePeripheryTAAProfile) const
{
	float2 resolved = GetDefaultFoveatedMaskCenterOffset(eyeIndex);
	const bool isLeftEye = eyeIndex == 0;
	const auto params = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
	const float userAdjustX = isLeftEye ? params.leftOffsetX : params.rightOffsetX;
	const float userAdjustY = isLeftEye ? params.leftOffsetY : params.rightOffsetY;
	resolved.x += ClampFoveatedMaskOffsetAdjustment(userAdjustX);
	resolved.y += ClampFoveatedMaskOffsetAdjustment(userAdjustY);

	if (globals::game::isVR) {
		const float centerScale = params.centerScale;
		const float centerHorizontalScale = params.centerHorizontalScale;
		const float outwardExpansion = centerScale * 0.5f * std::max(0.0f, centerHorizontalScale - 1.0f);
		resolved.x += isLeftEye ? -outwardExpansion : outwardExpansion;
	}

	resolved.x = std::clamp(resolved.x, kFoveatedMaskOffsetResolvedMin, kFoveatedMaskOffsetResolvedMax);
	resolved.y = std::clamp(resolved.y, kFoveatedMaskOffsetResolvedMin, kFoveatedMaskOffsetResolvedMax);
	return resolved;
}

std::array<float2, 2> Upscaling::GetResolvedFoveatedMaskCenterOffsets(bool usePeripheryTAAProfile) const
{
	return { GetResolvedFoveatedMaskCenterOffset(0, usePeripheryTAAProfile), GetResolvedFoveatedMaskCenterOffset(1, usePeripheryTAAProfile) };
}

std::array<float2, 2> Upscaling::GetActiveResolvedFoveatedMaskCenterOffsets() const
{
	return GetActiveUpscalingFoveatedProfile().centerOffsets;
}

bool Upscaling::GetRuntimeFoveatedRegionDimensions(uint32_t& a_inputWidthPerEye, uint32_t& a_inputHeight, uint32_t& a_outputWidthPerEye, uint32_t& a_outputHeight) const
{
	a_inputWidthPerEye = 0;
	a_inputHeight = 0;
	a_outputWidthPerEye = 0;
	a_outputHeight = 0;

	if (runtimeResolutionPlan.foveatedRegion.IsValid()) {
		a_inputWidthPerEye = runtimeResolutionPlan.foveatedRegion.inputWidthPerEye;
		a_inputHeight = runtimeResolutionPlan.foveatedRegion.inputHeight;
		a_outputWidthPerEye = runtimeResolutionPlan.foveatedRegion.outputWidthPerEye;
		a_outputHeight = runtimeResolutionPlan.foveatedRegion.outputHeight;
		return a_inputWidthPerEye > 0 && a_inputHeight > 0 && a_outputWidthPerEye > 0 && a_outputHeight > 0;
	}

	auto* state = globals::state;
	if (!state)
		return false;

	float2 outputSize = runtimeResolutionPlan.finalOutputSize;
	float2 renderSize = runtimeResolutionPlan.engineRenderSize;
	if (outputSize.x <= 0.0f || outputSize.y <= 0.0f)
		outputSize = state->screenSize;
	if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
		renderSize = Util::ConvertToDynamic(outputSize);

	const uint32_t eyeDivisor = globals::game::isVR ? 2u : 1u;
	a_inputWidthPerEye = std::max<uint32_t>(1u, ClampPositiveDimension(renderSize.x) / eyeDivisor);
	a_inputHeight = ClampPositiveDimension(renderSize.y);
	a_outputWidthPerEye = std::max<uint32_t>(1u, ClampPositiveDimension(outputSize.x) / eyeDivisor);
	a_outputHeight = ClampPositiveDimension(outputSize.y);
	return true;
}

bool Upscaling::BuildFoveatedDispatchRects(uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, bool isVR, float centerScale, float centerFeather, float centerHorizontalScale, bool usePeripheryTAAProfile)
{
	centerScale = ClampFoveatedCenterScale(centerScale);
	centerFeather = std::isfinite(centerFeather) ? std::max(0.0f, centerFeather) : FoveatedCommon::kCenterFeather;
	centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);
	const float taaOuterScale = usePeripheryTAAProfile ?
	                                ClampPeripheryTAAOuterScaleForCenter(
										settings.periphery_taa_outer_scale,
										centerScale) :
	                                0.0f;

	auto& cache = foveatedRectCache;
	auto centerOffsets = GetResolvedFoveatedMaskCenterOffsets(usePeripheryTAAProfile);
	if (!isVR)
		centerOffsets[1] = { 0.0f, 0.0f };
	const bool cacheDirty =
		cache.inputWidthPerEye != inputWidthPerEye ||
		cache.inputHeight != inputHeight ||
		cache.outputWidthPerEye != outputWidthPerEye ||
		cache.outputHeight != outputHeight ||
		cache.isVR != isVR ||
		std::abs(cache.centerScale - centerScale) > 1e-6f ||
		std::abs(cache.centerFeather - centerFeather) > 1e-6f ||
		std::abs(cache.centerHorizontalScale - centerHorizontalScale) > 1e-6f ||
		std::abs(cache.peripheryTAAOuterScale - taaOuterScale) > 1e-6f ||
		std::abs(cache.centerOffsets[0].x - centerOffsets[0].x) > 1e-6f ||
		std::abs(cache.centerOffsets[0].y - centerOffsets[0].y) > 1e-6f ||
		(isVR && (std::abs(cache.centerOffsets[1].x - centerOffsets[1].x) > 1e-6f ||
					 std::abs(cache.centerOffsets[1].y - centerOffsets[1].y) > 1e-6f));

	if (!cacheDirty)
		return true;

	cache.inputWidthPerEye = inputWidthPerEye;
	cache.inputHeight = inputHeight;
	cache.outputWidthPerEye = outputWidthPerEye;
	cache.outputHeight = outputHeight;
	cache.isVR = isVR;
	cache.centerScale = centerScale;
	cache.centerFeather = centerFeather;
	cache.centerHorizontalScale = centerHorizontalScale;
	cache.peripheryTAAOuterScale = taaOuterScale;
	cache.centerOffsets = centerOffsets;
	cache.rects = {};

	const auto plan = FoveatedRegionPlan::Build(
		inputWidthPerEye,
		inputHeight,
		outputWidthPerEye,
		outputHeight,
		isVR,
		centerScale,
		centerFeather,
		centerHorizontalScale,
		centerOffsets,
		0u,
		taaOuterScale);
	cache.plan = plan;

	auto copyEyeRect = [&](uint32_t eyeIndex) {
		const auto& eye = plan.eyes[eyeIndex];
		if (!eye.IsValid())
			return;

		auto& rect = cache.rects[eyeIndex];
		rect.outputOffsetX = eye.output.minX;
		rect.outputOffsetY = eye.output.minY;
		rect.outputWidth = eye.output.Width();
		rect.outputHeight = eye.output.Height();
		rect.inputOffsetX = eye.input.minX;
		rect.inputOffsetY = eye.input.minY;
		rect.inputWidth = eye.input.Width();
		rect.inputHeight = eye.input.Height();
	};

	copyEyeRect(0);
	if (isVR)
		copyEyeRect(1);

	return true;
}

bool Upscaling::GetFoveatedEncodeRegions(uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, bool usePeripheryTAAProfile, bool usePeripheryTAAPath, std::array<FoveatedEncodeRegion, 2>& outRegions)
{
	outRegions = {};

	const auto foveatedProfile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
	const float centerFeather = usePeripheryTAAPath ?
	                                ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) :
	                                FoveatedCommon::kCenterFeather;
	if (!BuildFoveatedDispatchRects(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight, true, foveatedProfile.centerScale, centerFeather, foveatedProfile.centerHorizontalScale, usePeripheryTAAProfile))
		return false;

	auto includeInputRect = [&](FoveatedEncodeRegion& region, uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) {
		minX = std::min(minX, inputWidthPerEye);
		minY = std::min(minY, inputHeight);
		maxX = std::min(maxX, inputWidthPerEye);
		maxY = std::min(maxY, inputHeight);
		if (maxX <= minX || maxY <= minY)
			return;

		if (!region.valid) {
			region.minX = minX;
			region.minY = minY;
			region.maxX = maxX;
			region.maxY = maxY;
			region.valid = true;
		} else {
			region.minX = std::min(region.minX, minX);
			region.minY = std::min(region.minY, minY);
			region.maxX = std::max(region.maxX, maxX);
			region.maxY = std::max(region.maxY, maxY);
		}
	};

	for (uint32_t eye = 0; eye < 2; ++eye) {
		const auto& eyePlan = foveatedRectCache.plan.eyes[eye];
		if (!eyePlan.encodeInput.IsValid())
			return false;

		includeInputRect(outRegions[eye], eyePlan.encodeInput.minX, eyePlan.encodeInput.minY, eyePlan.encodeInput.maxX, eyePlan.encodeInput.maxY);
		if (!outRegions[eye].valid)
			return false;
	}

	return true;
}

bool Upscaling::EnsureFoveatedTexture(eastl::unique_ptr<Texture2D>& texture, ID3D11Resource* source, uint32_t width, uint32_t height, bool copyBindFlags, bool createSRV, bool createUAV, bool createRTV, const char* name)
{
	if (!source || !width || !height)
		return false;

	D3D11_TEXTURE2D_DESC sourceDesc{};
	if (!TryGetTexture2DDesc(source, sourceDesc))
		return false;

	bool recreate = !texture;
	if (!recreate) {
		recreate = texture->desc.Width != width ||
		           texture->desc.Height != height ||
		           texture->desc.Format != sourceDesc.Format;
		if (createSRV && !texture->srv)
			recreate = true;
		if (createUAV && !texture->uav)
			recreate = true;
		if (createRTV && !texture->rtv)
			recreate = true;
	}

	if (recreate) {
		static bool loggedTextureCreateFailure = false;
		const auto createFailureMessage = [&]() {
			return std::format(
				"[Upscaling] Failed to create foveated texture '{}' ({}x{}); foveated vendor dispatch will fall back this frame",
				name ? name : "<unnamed>",
				width,
				height);
		};
		try {
			texture = CreateTextureFromSource(source, width, height, copyBindFlags, createSRV, createUAV, name, createRTV);
		} catch (const std::exception& e) {
			LogWarnOnce(loggedTextureCreateFailure, createFailureMessage(), e);
			MarkSubmitStageDeviceLostIfNeeded(e, name ? name : "foveated texture creation");
			texture = nullptr;
			return false;
		} catch (...) {
			LogWarnOnce(loggedTextureCreateFailure, createFailureMessage());
			MarkSubmitStageDeviceLostIfDeviceRemoved(name ? name : "foveated texture creation");
			texture = nullptr;
			return false;
		}
		if (!texture)
			return false;
	}

	static bool loggedTextureViewFailure = false;
	const auto viewFailureMessage = [&]() {
		return std::format(
			"[Upscaling] Failed to create foveated texture views for '{}'; foveated vendor dispatch will fall back this frame",
			name ? name : "<unnamed>");
	};
	try {
		if (createRTV && !texture->rtv) {
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = texture->desc.Format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Texture2D.MipSlice = 0;
			texture->CreateRTV(rtvDesc);
		}

		if (createSRV && !texture->srv) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = texture->desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			texture->CreateSRV(srvDesc);
		}

		if (createUAV && !texture->uav) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = texture->desc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			texture->CreateUAV(uavDesc);
		}
	} catch (const std::exception& e) {
		LogWarnOnce(loggedTextureViewFailure, viewFailureMessage(), e);
		MarkSubmitStageDeviceLostIfNeeded(e, name ? name : "foveated texture view creation");
		texture = nullptr;
		return false;
	} catch (...) {
		LogWarnOnce(loggedTextureViewFailure, viewFailureMessage());
		MarkSubmitStageDeviceLostIfDeviceRemoved(name ? name : "foveated texture view creation");
		texture = nullptr;
		return false;
	}

	return true;
}

bool Upscaling::EnsureFoveatedDispatchShaders(bool usePeripheryTAA, bool visualizeMask, const char* context, const char* fallbackAction)
{
	const char* contextText = context ? context : "";
	const char* fallbackText = fallbackAction ? fallbackAction : "skipping foveated vendor dispatch";
	static bool loggedFoveatedShaderFailure = false;

	try {
		auto* peripheryCS = GetFoveatedPeripheryCS();
		auto* peripheryTAA = usePeripheryTAA ? GetPeripheryTAACS() : nullptr;
		auto* blendCS = visualizeMask ? nullptr : GetFoveatedCenterBlendCS();
		return peripheryCS && foveatedPeripheryCB &&
		       (!usePeripheryTAA || (peripheryTAA && peripheryTAACB)) &&
		       (visualizeMask || (blendCS && foveatedCenterBlendCB));
	} catch (const std::exception& e) {
		LogWarnOnceFmt(
			loggedFoveatedShaderFailure,
			"[Upscaling] {}foveated dispatch shader unavailable; {}: {}",
			contextText,
			fallbackText,
			e.what());
		MarkSubmitStageDeviceLostIfNeeded(e, "foveated dispatch shader creation");
		return false;
	} catch (...) {
		LogWarnOnceFmt(
			loggedFoveatedShaderFailure,
			"[Upscaling] {}foveated dispatch shader unavailable; {}",
			contextText,
			fallbackText);
		MarkSubmitStageDeviceLostIfDeviceRemoved("foveated dispatch shader creation");
		return false;
	}
}

bool Upscaling::EnsurePeripheryTAAResources(uint32_t outputWidthPerEye, uint32_t outputHeight, ID3D11Resource* colorSource)
{
	if (!outputWidthPerEye || !outputHeight || !colorSource)
		return false;

	D3D11_TEXTURE2D_DESC colorDesc{};
	if (!TryGetTexture2DDesc(colorSource, colorDesc))
		return false;

	bool recreatedResources = false;
	static bool loggedPeripheryTAAResourceFailure = false;

	try {
		for (uint32_t eye = 0; eye < 2; ++eye) {
			const std::string suffix = eye == 0 ? "Left" : "Right";

			for (uint32_t historySlot = 0; historySlot < 2; ++historySlot) {
				auto& historyColorTexture = peripheryTAAHistoryColor[eye][historySlot];
				const bool recreateHistoryColor =
					!historyColorTexture ||
					historyColorTexture->desc.Width != outputWidthPerEye ||
					historyColorTexture->desc.Height != outputHeight ||
					historyColorTexture->desc.Format != colorDesc.Format ||
					!historyColorTexture->srv || !historyColorTexture->uav;
				recreatedResources = recreatedResources || recreateHistoryColor;

				if (!EnsureFoveatedTexture(
						historyColorTexture,
						colorSource,
						outputWidthPerEye,
						outputHeight,
						false,
						true,
						true,
						false,
						(std::format("Upscale_PeripheryTAA_HistoryColor_{}_{}", suffix, historySlot)).c_str())) {
					DestroyPeripheryTAAResources();
					return false;
				}

				auto& velocityTexture = peripheryTAAVelocityHistory[eye][historySlot];
				const bool recreateVelocity =
					!velocityTexture ||
					velocityTexture->desc.Width != outputWidthPerEye ||
					velocityTexture->desc.Height != outputHeight ||
					velocityTexture->desc.Format != DXGI_FORMAT_R16G16_FLOAT ||
					!velocityTexture->srv || !velocityTexture->uav;
				if (recreateVelocity) {
					velocityTexture = CreateNamedTexture2D(
						outputWidthPerEye,
						outputHeight,
						DXGI_FORMAT_R16G16_FLOAT,
						true,
						true,
						false,
						(std::format("Upscale_PeripheryTAA_Velocity_{}_{}", suffix, historySlot)).c_str());
					recreatedResources = true;
				}

				auto& lockTexture = peripheryTAALockHistory[eye][historySlot];
				const bool recreateLock =
					!lockTexture ||
					lockTexture->desc.Width != outputWidthPerEye ||
					lockTexture->desc.Height != outputHeight ||
					lockTexture->desc.Format != DXGI_FORMAT_R16_FLOAT ||
					!lockTexture->srv || !lockTexture->uav;
				if (recreateLock) {
					lockTexture = CreateNamedTexture2D(
						outputWidthPerEye,
						outputHeight,
						DXGI_FORMAT_R16_FLOAT,
						true,
						true,
						false,
						(std::format("Upscale_PeripheryTAA_Lock_{}_{}", suffix, historySlot)).c_str());
					recreatedResources = true;
				}
			}
		}
	} catch (const std::exception& e) {
		LogWarnOnce(
			loggedPeripheryTAAResourceFailure,
			"[Upscaling] Failed to create Periphery TAA resources; foveated vendor dispatch will fall back this frame",
			e);
		MarkSubmitStageDeviceLostIfNeeded(e, "Periphery TAA resource creation");
		DestroyPeripheryTAAResources();
		return false;
	} catch (...) {
		LogWarnOnce(
			loggedPeripheryTAAResourceFailure,
			"[Upscaling] Failed to create Periphery TAA resources; foveated vendor dispatch will fall back this frame");
		MarkSubmitStageDeviceLostIfDeviceRemoved("Periphery TAA resource creation");
		DestroyPeripheryTAAResources();
		return false;
	}

	if (recreatedResources) {
		// Any recreated history surface invalidates temporal continuity.
		peripheryTAAHistoryReadIndex = 0;
		peripheryTAAHistoryValid = false;
		submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
		submitStageFoveatedPeripheryTAAEyeReady = {};
	}

	return true;
}

bool Upscaling::EnsurePeripheryTAATileBuffer(uint32_t eyeIndex, uint32_t tileCapacity)
{
	if (eyeIndex >= 2 || tileCapacity == 0)
		return false;
	if (tileCapacity > std::numeric_limits<uint32_t>::max() / sizeof(PeripheryTAATile))
		return false;

	auto& tileBuffer = peripheryTAATileBuffer[eyeIndex];
	auto& tileCapacityCurrent = peripheryTAATileCapacity[eyeIndex];
	if (tileBuffer && tileCapacityCurrent >= tileCapacity && tileBuffer->srv)
		return true;

	D3D11_BUFFER_DESC sbDesc{};
	sbDesc.Usage = D3D11_USAGE_DYNAMIC;
	sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	sbDesc.StructureByteStride = sizeof(PeripheryTAATile);
	sbDesc.ByteWidth = static_cast<uint32_t>(sizeof(PeripheryTAATile) * tileCapacity);

	static bool loggedTileBufferFailure = false;
	try {
		tileBuffer = eastl::make_unique<Buffer>(sbDesc);
		tileCapacityCurrent = tileCapacity;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = tileCapacity;
		tileBuffer->CreateSRV(srvDesc);
	} catch (const std::exception& e) {
		LogWarnOnce(
			loggedTileBufferFailure,
			"[Upscaling] Failed to create Periphery TAA tile buffer; foveated vendor dispatch will fall back this frame",
			e);
		MarkSubmitStageDeviceLostIfNeeded(e, "Periphery TAA tile buffer creation");
		tileBuffer = nullptr;
		tileCapacityCurrent = 0;
		return false;
	} catch (...) {
		LogWarnOnce(
			loggedTileBufferFailure,
			"[Upscaling] Failed to create Periphery TAA tile buffer; foveated vendor dispatch will fall back this frame");
		MarkSubmitStageDeviceLostIfDeviceRemoved("Periphery TAA tile buffer creation");
		tileBuffer = nullptr;
		tileCapacityCurrent = 0;
		return false;
	}

	peripheryTAATileCache[eyeIndex].uploaded = false;
	return tileBuffer->srv != nullptr;
}

bool Upscaling::BuildPeripheryTAATileList(uint32_t eyeIndex, uint32_t outputWidth, uint32_t outputHeight, float centerScale, float taaOuterScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY, uint32_t coveragePadding, uint32_t& outTileCount)
{
	outTileCount = 0;
	if (eyeIndex >= 2 || outputWidth == 0 || outputHeight == 0)
		return false;

	const uint32_t tileSize = static_cast<uint32_t>(FoveatedCommon::kThreadGroupSize);
	const uint32_t tileColumns = (outputWidth + tileSize - 1u) / tileSize;
	const uint32_t tileRows = (outputHeight + tileSize - 1u) / tileSize;
	if (tileColumns != 0 && tileRows > (std::numeric_limits<uint32_t>::max() / tileColumns))
		return false;
	const uint32_t maxTileCount = tileColumns * tileRows;
	if (!EnsurePeripheryTAATileBuffer(eyeIndex, maxTileCount))
		return false;

	centerScale = ClampFoveatedCenterScale(centerScale);
	taaOuterScale = ClampPeripheryTAAOuterScaleForCenter(taaOuterScale, centerScale);
	centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);

	PeripheryTAATileCacheKey cacheKey{};
	cacheKey.outputWidth = outputWidth;
	cacheKey.outputHeight = outputHeight;
	cacheKey.coveragePadding = coveragePadding;
	cacheKey.centerScaleQ = QuantizePeripheryTAATileParam(centerScale);
	cacheKey.taaOuterScaleQ = QuantizePeripheryTAATileParam(taaOuterScale);
	cacheKey.centerHorizontalScaleQ = QuantizePeripheryTAATileParam(centerHorizontalScale);
	cacheKey.centerOffsetXQ = QuantizePeripheryTAATileParam(centerOffsetX);
	cacheKey.centerOffsetYQ = QuantizePeripheryTAATileParam(centerOffsetY);

	auto& cacheState = peripheryTAATileCache[eyeIndex];
	const bool keyMatches =
		cacheState.valid &&
		cacheState.key.outputWidth == cacheKey.outputWidth &&
		cacheState.key.outputHeight == cacheKey.outputHeight &&
		cacheState.key.coveragePadding == cacheKey.coveragePadding &&
		cacheState.key.centerScaleQ == cacheKey.centerScaleQ &&
		cacheState.key.taaOuterScaleQ == cacheKey.taaOuterScaleQ &&
		cacheState.key.centerHorizontalScaleQ == cacheKey.centerHorizontalScaleQ &&
		cacheState.key.centerOffsetXQ == cacheKey.centerOffsetXQ &&
		cacheState.key.centerOffsetYQ == cacheKey.centerOffsetYQ;

	if (!keyMatches) {
		cacheState.tiles.clear();
		cacheState.tiles.reserve(maxTileCount);

		const float2 centerOffset{ centerOffsetX, centerOffsetY };
		const auto coverageBounds = FoveatedRegionPlan::ExpandRect(
			FoveatedRegionPlan::BuildCenteredOutputRect(outputWidth, outputHeight, taaOuterScale, 0.0f, centerHorizontalScale, centerOffset),
			coveragePadding,
			outputWidth,
			outputHeight);
		const uint32_t coverageMinX = coverageBounds.minX;
		const uint32_t coverageMinY = coverageBounds.minY;
		const uint32_t coverageMaxX = coverageBounds.maxX;
		const uint32_t coverageMaxY = coverageBounds.maxY;
		const bool useRectangularCoverage = coveragePadding > 0 && coverageMaxX > coverageMinX && coverageMaxY > coverageMinY;

		for (uint32_t tileY = 0; tileY < outputHeight; tileY += tileSize) {
			const uint32_t maxY = std::min(tileY + tileSize, outputHeight);
			for (uint32_t tileX = 0; tileX < outputWidth; tileX += tileSize) {
				const uint32_t maxX = std::min(tileX + tileSize, outputWidth);
				if (useRectangularCoverage) {
					if (maxX <= coverageMinX || tileX >= coverageMaxX || maxY <= coverageMinY || tileY >= coverageMaxY)
						continue;
				} else {
					const float outerMinDistance = FoveatedMaskTileMinDistance(tileX, tileY, maxX, maxY, outputWidth, outputHeight, taaOuterScale, centerHorizontalScale, centerOffsetX, centerOffsetY);
					if (outerMinDistance > 1.0f + 1e-4f)
						continue;
				}

				const uint32_t centerTestMinX = tileX > coveragePadding ? tileX - coveragePadding : 0u;
				const uint32_t centerTestMinY = tileY > coveragePadding ? tileY - coveragePadding : 0u;
				const uint32_t centerTestMaxX = std::min(outputWidth, maxX + coveragePadding);
				const uint32_t centerTestMaxY = std::min(outputHeight, maxY + coveragePadding);
				const float centerMaxDistance = FoveatedMaskTileMaxDistance(centerTestMinX, centerTestMinY, centerTestMaxX, centerTestMaxY, outputWidth, outputHeight, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY);
				if (centerMaxDistance <= 1.0f - 1e-4f)
					continue;

				cacheState.tiles.push_back({ tileX, tileY });
			}
		}

		cacheState.key = cacheKey;
		cacheState.tileCount = static_cast<uint32_t>(cacheState.tiles.size());
		cacheState.valid = true;
		cacheState.uploaded = false;
	}

	outTileCount = cacheState.tileCount;
	if (outTileCount == 0)
		return true;
	if (cacheState.uploaded)
		return true;

	auto context = globals::d3d::context;
	auto& tileBuffer = peripheryTAATileBuffer[eyeIndex];
	const uint32_t tileCapacity = peripheryTAATileCapacity[eyeIndex];
	if (!context || !tileBuffer || !tileBuffer->resource || tileCapacity < outTileCount)
		return false;

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(context->Map(tileBuffer->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;
	const size_t bytes = sizeof(PeripheryTAATile) * outTileCount;
	memcpy_s(mapped.pData, sizeof(PeripheryTAATile) * tileCapacity, cacheState.tiles.data(), bytes);
	context->Unmap(tileBuffer->resource.get(), 0);
	cacheState.uploaded = true;
	return true;
}

void Upscaling::DestroyFoveatedResources()
{
	InvalidateFrameScopedUpscalingState();
	for (uint32_t i = 0; i < 2; ++i) {
		foveatedCenterColorIn[i].reset();
		foveatedCenterColorOut[i].reset();
		foveatedCenterDepth[i].reset();
		foveatedCenterMotionVectors[i].reset();
		foveatedCenterReactiveMask[i].reset();
		foveatedCenterTransparencyMask[i].reset();
	}
	foveatedRectCache = {};
	DestroyPeripheryTAAResources();
}

void Upscaling::DestroyPeripheryTAAResources()
{
	InvalidateFrameScopedUpscalingState();
	for (uint32_t eye = 0; eye < 2; ++eye) {
		for (uint32_t historySlot = 0; historySlot < 2; ++historySlot) {
			peripheryTAAHistoryColor[eye][historySlot].reset();
			peripheryTAAVelocityHistory[eye][historySlot].reset();
			peripheryTAALockHistory[eye][historySlot].reset();
		}
		peripheryTAATileBuffer[eye].reset();
		peripheryTAATileCapacity[eye] = 0;
		peripheryTAATileCache[eye] = {};
	}
	peripheryTAAHistoryReadIndex = 0;
	peripheryTAAHistoryValid = false;
	submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	submitStageFoveatedPeripheryTAAEyeReady = {};
}

void Upscaling::DispatchFoveatedPeripheryPass(ID3D11ShaderResourceView* sourceSRV, ID3D11UnorderedAccessView* outputUAV, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t outputWidth, uint32_t outputHeight, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, float centerScale, float centerHorizontalScale, bool keepBindingsBound, float sourceScaleX, float sourceScaleY, float sourceOffsetX, float sourceOffsetY, float centerOffsetX, float centerOffsetY)
{
	auto* peripheryCS = GetFoveatedPeripheryCS();
	if (!peripheryCS || !sourceSRV || !outputUAV || !foveatedPeripheryCB)
		return;
	if (!dispatchWidth || !dispatchHeight)
		return;

	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!context || !deferred || !deferred->linearSampler)
		return;
	if (outputOffsetX >= outputWidth || outputOffsetY >= outputHeight)
		return;
	dispatchWidth = std::min(dispatchWidth, outputWidth - outputOffsetX);
	dispatchHeight = std::min(dispatchHeight, outputHeight - outputOffsetY);
	if (!dispatchWidth || !dispatchHeight)
		return;

	FoveatedPeripheryCB cbData{};
	cbData.outputDim = { static_cast<float>(outputWidth), static_cast<float>(outputHeight) };
	cbData.invOutputDim = {
		outputWidth > 0 ? 1.0f / static_cast<float>(outputWidth) : 0.0f,
		outputHeight > 0 ? 1.0f / static_cast<float>(outputHeight) : 0.0f
	};
	cbData.invSourceDim = {
		sourceWidth > 0 ? 1.0f / static_cast<float>(sourceWidth) : 0.0f,
		sourceHeight > 0 ? 1.0f / static_cast<float>(sourceHeight) : 0.0f
	};
	const auto sourceRegion = FoveatedRegionPlan::ClampNormalizedTextureRegion(sourceScaleX, sourceScaleY, sourceOffsetX, sourceOffsetY);
	cbData.sourceScale = sourceRegion.scale;
	cbData.sourceOffset = sourceRegion.offset;
	cbData.dispatchDim = { static_cast<float>(dispatchWidth), static_cast<float>(dispatchHeight) };
	cbData.outputOffset = { static_cast<float>(outputOffsetX), static_cast<float>(outputOffsetY) };
	cbData.jitter = jitter;
	centerScale = ClampFoveatedCenterScale(centerScale);
	centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);
	const bool visualizeMask = settings.foveatedPeripheryMaskVisualization;
	const bool showThreeZoneMask = visualizeMask && settings.periphery_taa_enable;
	const float centerFeather = showThreeZoneMask ? ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) : FoveatedCommon::kCenterFeather;
	const float taaOuterScale = ClampPeripheryTAAOuterScaleForCenter(settings.periphery_taa_outer_scale, centerScale);
	cbData.centerAndMask = {
		centerOffsetX,
		centerOffsetY,
		visualizeMask ? 1.0f : 0.0f,
		showThreeZoneMask ? 1.0f : 0.0f
	};
	cbData.tuning0 = {
		centerScale,
		centerFeather,
		centerHorizontalScale,
		taaOuterScale
	};
	foveatedPeripheryCB->Update(cbData);

	if (keepBindingsBound) {
		auto state = globals::state;
		if (state && state->frameAnnotations)
			state->BeginPerfEvent("Foveated Periphery");
		context->Dispatch((dispatchWidth + 7u) >> 3, (dispatchHeight + 7u) >> 3, 1);
		if (state && state->frameAnnotations)
			state->EndPerfEvent();
	} else {
		ID3D11Buffer* cb = foveatedPeripheryCB->CB();
		ID3D11SamplerState* samplers[1] = { deferred->linearSampler };
		ID3D11ShaderResourceView* srvs[1] = { sourceSRV };
		ID3D11UnorderedAccessView* uavs[1] = { outputUAV };

		context->CSSetShader(peripheryCS, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetSamplers(0, 1, samplers);
		context->CSSetShaderResources(0, 1, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		auto state = globals::state;
		if (state && state->frameAnnotations)
			state->BeginPerfEvent("Foveated Periphery");
		context->Dispatch((dispatchWidth + 7u) >> 3, (dispatchHeight + 7u) >> 3, 1);
		if (state && state->frameAnnotations)
			state->EndPerfEvent();

		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11SamplerState* nullSampler[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetSamplers(0, 1, nullSampler);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	}
}

void Upscaling::DispatchPeripheryTAAPass(ID3D11ShaderResourceView* currentColorSRV, ID3D11ShaderResourceView* currentDepthSRV, ID3D11ShaderResourceView* currentMotionVectorSRV,
	ID3D11ShaderResourceView* currentReactiveSRV, ID3D11ShaderResourceView* currentTransparencySRV, ID3D11ShaderResourceView* historyColorSRV,
	ID3D11ShaderResourceView* historyVelocitySRV, ID3D11ShaderResourceView* historyLockSRV, ID3D11UnorderedAccessView* outputColorUAV, ID3D11UnorderedAccessView* outputHistoryColorUAV,
	ID3D11UnorderedAccessView* outputVelocityUAV, ID3D11UnorderedAccessView* outputLockUAV, ID3D11ShaderResourceView* tileListSRV, uint32_t tileCount,
	uint32_t inputWidth, uint32_t inputHeight, uint32_t outputWidth,
	uint32_t outputHeight, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, const float4x4& currentViewProjInverse,
	const float4x4& previousViewProj, const float4& currentCameraPosAdjust, const float4& previousCameraPosAdjust, bool resetHistory, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY,
	float inputTextureScaleX, float inputTextureScaleY, float inputTextureOffsetX, float inputTextureOffsetY)
{
	// This custom periphery-only TAA path adapts MIT-licensed ideas from:
	// - Godot's TAA resolve / Spartan Engine lineage (taa_resolve.glsl, copyright Panos Karabelas)
	// - AMD FidelityFX FSR2/FSR3 lock/reactivity/luminance-instability heuristics.
	// - Temporal AA survey background: Yang, Liu, Salvi, "A Survey of Temporal Antialiasing Techniques" (2020).
	// The implementation below is purpose-built for Community Shaders VR periphery resolve and is not a verbatim copy.
	auto* peripheryTAA = GetPeripheryTAACS();
	if (!peripheryTAA || !peripheryTAACB)
		return;
	if (!currentColorSRV || !currentDepthSRV || !currentMotionVectorSRV || !currentReactiveSRV || !currentTransparencySRV)
		return;
	if (!historyColorSRV || !historyVelocitySRV || !historyLockSRV)
		return;
	if (!outputColorUAV || !outputHistoryColorUAV || !outputVelocityUAV || !outputLockUAV)
		return;
	if (!inputWidth || !inputHeight || !outputWidth || !outputHeight)
		return;
	const bool useTileList = tileListSRV && tileCount > 0;
	if (!useTileList && (!dispatchWidth || !dispatchHeight))
		return;

	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!context || !deferred || !deferred->linearSampler)
		return;

	uint32_t dispatchGroupsX = 0;
	uint32_t dispatchGroupsY = 0;
	if (useTileList) {
		dispatchGroupsX = std::min(tileCount, 65535u);
		dispatchGroupsY = (tileCount + dispatchGroupsX - 1u) / dispatchGroupsX;
		outputOffsetX = 0;
		outputOffsetY = 0;
		dispatchWidth = tileCount;
		dispatchHeight = 1;
	} else {
		if (outputOffsetX >= outputWidth || outputOffsetY >= outputHeight)
			return;

		dispatchWidth = std::min(dispatchWidth, outputWidth - outputOffsetX);
		dispatchHeight = std::min(dispatchHeight, outputHeight - outputOffsetY);
		if (!dispatchWidth || !dispatchHeight)
			return;

		dispatchGroupsX = (dispatchWidth + 7u) >> 3;
		dispatchGroupsY = (dispatchHeight + 7u) >> 3;
	}

	PeripheryTAACB cbData{};
	cbData.outputDim = { static_cast<float>(outputWidth), static_cast<float>(outputHeight) };
	cbData.invOutputDim = {
		outputWidth > 0 ? 1.0f / static_cast<float>(outputWidth) : 0.0f,
		outputHeight > 0 ? 1.0f / static_cast<float>(outputHeight) : 0.0f
	};
	cbData.inputDim = { static_cast<float>(inputWidth), static_cast<float>(inputHeight) };
	cbData.invInputDim = {
		inputWidth > 0 ? 1.0f / static_cast<float>(inputWidth) : 0.0f,
		inputHeight > 0 ? 1.0f / static_cast<float>(inputHeight) : 0.0f
	};
	const auto inputTextureRegion = FoveatedRegionPlan::ClampNormalizedTextureRegion(inputTextureScaleX, inputTextureScaleY, inputTextureOffsetX, inputTextureOffsetY);
	cbData.inputTextureScale = inputTextureRegion.scale;
	cbData.inputTextureOffset = inputTextureRegion.offset;
	cbData.dispatchDim = { static_cast<float>(dispatchWidth), static_cast<float>(dispatchHeight) };
	cbData.outputOffset = { static_cast<float>(outputOffsetX), static_cast<float>(outputOffsetY) };
	cbData.jitter = jitter;
	cbData.centerOffset = { centerOffsetX, centerOffsetY };
	centerScale = ClampFoveatedCenterScale(centerScale);
	centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);
	const float centerFeather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);
	const float taaOuterScale = ClampPeripheryTAAOuterScaleForCenter(
		settings.periphery_taa_outer_scale,
		centerScale);
	const float2 centerOffset{ centerOffsetX, centerOffsetY };
	const auto taaColorWriteBounds = FoveatedRegionPlan::BuildCenteredOutputRect(
		outputWidth,
		outputHeight,
		taaOuterScale,
		0.0f,
		centerHorizontalScale,
		centerOffset);
	cbData.tuning0 = {
		centerScale,
		centerFeather,
		resetHistory ? 1.0f : 0.0f,
		taaOuterScale
	};
	cbData.tuning1 = {
		peripheryTAAHistoryValid && !resetHistory ? 1.0f : 0.0f,
		centerHorizontalScale,
		useTileList ? 1.0f : 0.0f,
		static_cast<float>(dispatchGroupsX)
	};
	cbData.tuning2 = {
		1.0f,
		1.25f,
		0.10f,
		0.92f
	};
	cbData.tuning3 = {
		static_cast<float>(taaColorWriteBounds.minX),
		static_cast<float>(taaColorWriteBounds.minY),
		static_cast<float>(taaColorWriteBounds.maxX),
		static_cast<float>(taaColorWriteBounds.maxY)
	};
	cbData.currentViewProjInverse = currentViewProjInverse;
	cbData.previousViewProj = previousViewProj;
	cbData.currentCameraPosAdjust = currentCameraPosAdjust;
	cbData.previousCameraPosAdjust = previousCameraPosAdjust;
	peripheryTAACB->Update(cbData);

	ID3D11Buffer* cb = peripheryTAACB->CB();
	ID3D11SamplerState* samplers[1] = { deferred->linearSampler };
	ID3D11ShaderResourceView* srvs[9] = {
		currentColorSRV,
		currentDepthSRV,
		currentMotionVectorSRV,
		currentReactiveSRV,
		currentTransparencySRV,
		historyColorSRV,
		historyVelocitySRV,
		historyLockSRV,
		tileListSRV
	};
	ID3D11UnorderedAccessView* uavs[4] = { outputColorUAV, outputVelocityUAV, outputLockUAV, outputHistoryColorUAV };

	context->CSSetShader(peripheryTAA, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetSamplers(0, 1, samplers);
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	auto state = globals::state;
	if (state && state->frameAnnotations) {
		char buf[64];
		if (useTileList)
			snprintf(buf, sizeof(buf), "Periphery TAA Tiles %u", tileCount);
		else
			snprintf(buf, sizeof(buf), "Periphery TAA Rect %ux%u", dispatchWidth, dispatchHeight);
		state->BeginPerfEvent(buf);
	}
	context->Dispatch(dispatchGroupsX, dispatchGroupsY, 1);
	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	ID3D11ShaderResourceView* nullSRV[9] = {};
	ID3D11UnorderedAccessView* nullUAV[4] = {};
	ID3D11SamplerState* nullSampler[1] = { nullptr };
	ID3D11Buffer* nullCB[1] = { nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullSRV), nullSRV);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAV), nullUAV, nullptr);
	context->CSSetSamplers(0, 1, nullSampler);
	context->CSSetConstantBuffers(0, 1, nullCB);
	context->CSSetShader(nullptr, nullptr, 0);
}

void Upscaling::DispatchFoveatedBlendPass(ID3D11ShaderResourceView* centerSRV, ID3D11UnorderedAccessView* outputUAV, uint32_t outputWidthPerEye, uint32_t outputHeight, const FoveatedDispatchRect& rect, uint32_t dispatchOffsetX, uint32_t dispatchOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, float centerScale, float centerHorizontalScale, const float2& centerOffset, float centerFeather)
{
	if (!centerSRV || !outputUAV || rect.outputWidth == 0 || rect.outputHeight == 0 || !foveatedCenterBlendCB)
		return;
	if (!dispatchWidth || !dispatchHeight)
		return;

	auto* blendCS = GetFoveatedCenterBlendCS();
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!blendCS || !context || !deferred || !deferred->linearSampler)
		return;
	if (dispatchOffsetX >= outputWidthPerEye || dispatchOffsetY >= outputHeight)
		return;

	dispatchWidth = std::min(dispatchWidth, outputWidthPerEye - dispatchOffsetX);
	dispatchHeight = std::min(dispatchHeight, outputHeight - dispatchOffsetY);
	if (!dispatchWidth || !dispatchHeight)
		return;

	const uint32_t rectMinX = rect.outputOffsetX;
	const uint32_t rectMinY = rect.outputOffsetY;
	const uint32_t rectMaxX = rect.outputOffsetX + rect.outputWidth;
	const uint32_t rectMaxY = rect.outputOffsetY + rect.outputHeight;

	const uint32_t dispatchMinX = std::max(dispatchOffsetX, rectMinX);
	const uint32_t dispatchMinY = std::max(dispatchOffsetY, rectMinY);
	const uint32_t dispatchMaxX = std::min(dispatchOffsetX + dispatchWidth, rectMaxX);
	const uint32_t dispatchMaxY = std::min(dispatchOffsetY + dispatchHeight, rectMaxY);
	if (dispatchMaxX <= dispatchMinX || dispatchMaxY <= dispatchMinY)
		return;

	const uint32_t actualDispatchWidth = dispatchMaxX - dispatchMinX;
	const uint32_t actualDispatchHeight = dispatchMaxY - dispatchMinY;
	const uint32_t sourceOffsetX = dispatchMinX - rectMinX;
	const uint32_t sourceOffsetY = dispatchMinY - rectMinY;

	FoveatedCenterBlendCB cbData{};
	cbData.invOutputDim = {
		outputWidthPerEye > 0 ? 1.0f / static_cast<float>(outputWidthPerEye) : 0.0f,
		outputHeight > 0 ? 1.0f / static_cast<float>(outputHeight) : 0.0f
	};
	cbData.centerScale = ClampFoveatedCenterScale(centerScale);
	cbData.centerFeather = std::isfinite(centerFeather) ? std::max(0.0f, centerFeather) : FoveatedCommon::kCenterFeather;
	cbData.centerOffset = centerOffset;
	cbData.outputOffset = { static_cast<float>(dispatchMinX), static_cast<float>(dispatchMinY) };
	cbData.dispatchDim = { static_cast<float>(actualDispatchWidth), static_cast<float>(actualDispatchHeight) };
	cbData.sourceOffset = { static_cast<float>(sourceOffsetX), static_cast<float>(sourceOffsetY) };
	cbData.invSourceDim = {
		rect.outputWidth > 0 ? 1.0f / static_cast<float>(rect.outputWidth) : 0.0f,
		rect.outputHeight > 0 ? 1.0f / static_cast<float>(rect.outputHeight) : 0.0f
	};
	cbData.centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);
	cbData.centerHorizontalScalePadding = 0.0f;
	foveatedCenterBlendCB->Update(cbData);

	ID3D11Buffer* cb = foveatedCenterBlendCB->CB();
	ID3D11SamplerState* samplers[1] = { deferred->linearSampler };
	ID3D11ShaderResourceView* srvs[1] = { centerSRV };
	ID3D11UnorderedAccessView* uavs[1] = { outputUAV };

	context->CSSetShader(blendCS, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetSamplers(0, 1, samplers);
	context->CSSetShaderResources(0, 1, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	auto state = globals::state;
	if (state && state->frameAnnotations)
		state->BeginPerfEvent("Foveated Center Blend");
	context->Dispatch((actualDispatchWidth + 7u) >> 3, (actualDispatchHeight + 7u) >> 3, 1);
	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	ID3D11SamplerState* nullSampler[1] = { nullptr };
	ID3D11Buffer* nullCB[1] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	context->CSSetSamplers(0, 1, nullSampler);
	context->CSSetConstantBuffers(0, 1, nullCB);
	context->CSSetShader(nullptr, nullptr, 0);
}

bool Upscaling::DispatchVendorEyeRegion(UpscaleMethod a_upscaleMethod, const Upscaling::VendorEyeDispatchParams& params)
{
	if (!IsVendorUpscalingMethod(a_upscaleMethod) || params.eyeIndex >= 2)
		return false;
	if (!params.inputWidth || !params.inputHeight || !params.outputWidth || !params.outputHeight)
		return false;
	if (!params.colorIn || !params.depth || !params.motionVectors || !params.reactiveMask || !params.transparencyMask || !params.colorOut)
		return false;

	const auto validateTexture = [&](ID3D11Resource* resource, const char* resourceName, uint32_t requiredWidth, uint32_t requiredHeight) {
		D3D11_TEXTURE2D_DESC desc{};
		if (!TryGetTexture2DDesc(resource, desc)) {
			logger::debug(
				"[Upscaling] {} {} eye {} failed because {} is not a Texture2D.",
				params.label ? params.label : "vendor eye dispatch",
				magic_enum::enum_name(a_upscaleMethod),
				params.eyeIndex,
				resourceName);
			return false;
		}

		if (desc.Width < requiredWidth || desc.Height < requiredHeight) {
			logger::debug(
				"[Upscaling] {} {} eye {} failed because {} is too small. required={}x{} actual={}x{}",
				params.label ? params.label : "vendor eye dispatch",
				magic_enum::enum_name(a_upscaleMethod),
				params.eyeIndex,
				resourceName,
				requiredWidth,
				requiredHeight,
				desc.Width,
				desc.Height);
			return false;
		}

		return true;
	};

	const bool validateResourceDescs = globals::state && globals::state->IsDeveloperMode();
	if (validateResourceDescs) {
		if (!validateTexture(params.colorIn, "color input", params.inputWidth, params.inputHeight) ||
			!validateTexture(params.depth, "depth input", params.inputWidth, params.inputHeight) ||
			!validateTexture(params.motionVectors, "motion-vector input", params.inputWidth, params.inputHeight) ||
			!validateTexture(params.reactiveMask, "reactive mask input", params.inputWidth, params.inputHeight) ||
			!validateTexture(params.transparencyMask, "transparency mask input", params.inputWidth, params.inputHeight) ||
			!validateTexture(params.colorOut, "color output", params.outputWidth, params.outputHeight)) {
			return false;
		}
	}

	if (a_upscaleMethod == UpscaleMethod::kDLSS) {
		const sl::Extent extentIn{ 0u, 0u, params.inputWidth, params.inputHeight };
		const sl::Extent extentOut{ 0u, 0u, params.outputWidth, params.outputHeight };
		const sl::ViewportHandle viewport = params.eyeIndex == 1 ? streamline.viewportRight : streamline.viewport;
		return streamline.EvaluateDLSS(
			viewport,
			params.eyeIndex,
			params.colorIn,
			params.colorOut,
			params.depth,
			params.motionVectors,
			params.reactiveMask,
			params.transparencyMask,
			extentIn,
			extentOut,
			params.outputWidth,
			params.pinholeOffsetX,
			params.pinholeOffsetY);
	}

	if (a_upscaleMethod == UpscaleMethod::kFSR) {
		const float motionVectorScaleX = std::isfinite(params.motionVectorScaleX) && params.motionVectorScaleX > 0.0f ?
		                                     params.motionVectorScaleX :
		                                     static_cast<float>(params.inputWidth);
		const float motionVectorScaleY = std::isfinite(params.motionVectorScaleY) && params.motionVectorScaleY > 0.0f ?
		                                     params.motionVectorScaleY :
		                                     static_cast<float>(params.inputHeight);
		return fidelityFX.UpscaleRegion(
			params.eyeIndex,
			params.colorIn,
			params.depth,
			params.motionVectors,
			params.reactiveMask,
			params.transparencyMask,
			params.colorOut,
			params.inputWidth,
			params.inputHeight,
			params.outputWidth,
			params.outputHeight,
			motionVectorScaleX,
			motionVectorScaleY,
			settings.sharpnessFSR);
	}

	return false;
}

bool Upscaling::DispatchSingleFoveatedVendorEye(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, ID3D11Resource* colorIn, ID3D11Resource* depthIn, ID3D11Resource* motionVectorsIn, ID3D11Resource* reactiveMaskIn, ID3D11Resource* transparencyMaskIn, uint32_t outputWidthPerEye, uint32_t outputHeight, uint32_t inputWidthPerEye, uint32_t inputHeight, float centerScale, float centerHorizontalScale, const float2& centerOffset, float centerFeather, uint32_t colorInputBaseOffsetX, uint32_t depthInputBaseOffsetX, uint32_t auxInputBaseOffsetX, ID3D11UnorderedAccessView* outputUAV)
{
	if (!SupportsFoveatedVendorDispatch(a_upscaleMethod))
		return false;

	const bool useFSR = a_upscaleMethod == UpscaleMethod::kFSR;

	if (eyeIndex > 1)
		return false;

	const auto& rect = foveatedRectCache.rects[eyeIndex];
	if (!rect.outputWidth || !rect.outputHeight || !rect.inputWidth || !rect.inputHeight)
		return false;

	const std::string suffix = eyeIndex == 0 ? "Left" : "Right";
	const bool createFsrViews = useFSR;

	if (!EnsureFoveatedTexture(foveatedCenterColorIn[eyeIndex], colorIn, rect.inputWidth, rect.inputHeight, false, createFsrViews, false, false, ("Upscale_FoveatedCenter_ColorIn_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterColorOut[eyeIndex], colorIn, rect.outputWidth, rect.outputHeight, false, true, createFsrViews, false, ("Upscale_FoveatedCenter_ColorOut_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterDepth[eyeIndex], depthIn, rect.inputWidth, rect.inputHeight, true, createFsrViews, false, false, ("Upscale_FoveatedCenter_Depth_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterMotionVectors[eyeIndex], motionVectorsIn, rect.inputWidth, rect.inputHeight, false, createFsrViews, false, false, ("Upscale_FoveatedCenter_MVec_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterReactiveMask[eyeIndex], reactiveMaskIn, rect.inputWidth, rect.inputHeight, false, createFsrViews, false, false, ("Upscale_FoveatedCenter_Reactive_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterTransparencyMask[eyeIndex], transparencyMaskIn, rect.inputWidth, rect.inputHeight, false, createFsrViews, false, false, ("Upscale_FoveatedCenter_Transparency_" + suffix).c_str()))
		return false;

	auto context = globals::d3d::context;
	if (!context)
		return false;

	D3D11_BOX colorSrcBox{
		colorInputBaseOffsetX + rect.inputOffsetX,
		rect.inputOffsetY,
		0u,
		colorInputBaseOffsetX + rect.inputOffsetX + rect.inputWidth,
		rect.inputOffsetY + rect.inputHeight,
		1u
	};
	D3D11_BOX depthSrcBox{
		depthInputBaseOffsetX + rect.inputOffsetX,
		rect.inputOffsetY,
		0u,
		depthInputBaseOffsetX + rect.inputOffsetX + rect.inputWidth,
		rect.inputOffsetY + rect.inputHeight,
		1u
	};
	D3D11_BOX auxSrcBox{
		auxInputBaseOffsetX + rect.inputOffsetX,
		rect.inputOffsetY,
		0u,
		auxInputBaseOffsetX + rect.inputOffsetX + rect.inputWidth,
		rect.inputOffsetY + rect.inputHeight,
		1u
	};

	context->CopySubresourceRegion(foveatedCenterColorIn[eyeIndex]->resource.get(), 0, 0, 0, 0, colorIn, 0, &colorSrcBox);
	context->CopySubresourceRegion(foveatedCenterDepth[eyeIndex]->resource.get(), 0, 0, 0, 0, depthIn, 0, &depthSrcBox);
	context->CopySubresourceRegion(foveatedCenterMotionVectors[eyeIndex]->resource.get(), 0, 0, 0, 0, motionVectorsIn, 0, &auxSrcBox);
	context->CopySubresourceRegion(foveatedCenterReactiveMask[eyeIndex]->resource.get(), 0, 0, 0, 0, reactiveMaskIn, 0, &auxSrcBox);
	context->CopySubresourceRegion(foveatedCenterTransparencyMask[eyeIndex]->resource.get(), 0, 0, 0, 0, transparencyMaskIn, 0, &auxSrcBox);

	const auto& plan = foveatedRectCache.plan;
	if (!plan.IsValid())
		return false;
	const auto& eyePlan = plan.eyes[eyeIndex];
	if (!eyePlan.IsValid())
		return false;
	const float2 pinholeOffset = eyePlan.pinholeOffset;

	VendorEyeDispatchParams vendorParams{};
	vendorParams.eyeIndex = eyeIndex;
	vendorParams.inputWidth = rect.inputWidth;
	vendorParams.inputHeight = rect.inputHeight;
	vendorParams.outputWidth = rect.outputWidth;
	vendorParams.outputHeight = rect.outputHeight;
	vendorParams.motionVectorScaleX = static_cast<float>(std::max(inputWidthPerEye, 1u));
	vendorParams.motionVectorScaleY = static_cast<float>(std::max(inputHeight, 1u));
	vendorParams.pinholeOffsetX = pinholeOffset.x;
	vendorParams.pinholeOffsetY = pinholeOffset.y;
	vendorParams.colorIn = foveatedCenterColorIn[eyeIndex]->resource.get();
	vendorParams.depth = foveatedCenterDepth[eyeIndex]->resource.get();
	vendorParams.motionVectors = foveatedCenterMotionVectors[eyeIndex]->resource.get();
	vendorParams.reactiveMask = foveatedCenterReactiveMask[eyeIndex]->resource.get();
	vendorParams.transparencyMask = foveatedCenterTransparencyMask[eyeIndex]->resource.get();
	vendorParams.colorOut = foveatedCenterColorOut[eyeIndex]->resource.get();
	vendorParams.label = "foveated center";
	if (!DispatchVendorEyeRegion(a_upscaleMethod, vendorParams))
		return false;

	if (!foveatedCenterColorOut[eyeIndex] || !foveatedCenterColorOut[eyeIndex]->resource || !foveatedCenterColorOut[eyeIndex]->srv)
		return false;
	if (!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->uav || !vrIntermediateColorOut[eyeIndex]->resource)
		return false;

	const uint32_t rectMinX = rect.outputOffsetX;
	const uint32_t rectMinY = rect.outputOffsetY;
	const uint32_t rectMaxX = rect.outputOffsetX + rect.outputWidth;
	const uint32_t rectMaxY = rect.outputOffsetY + rect.outputHeight;

	if (!outputUAV)
		outputUAV = vrIntermediateColorOut[eyeIndex]->uav.get();
	ID3D11ShaderResourceView* centerSRV = foveatedCenterColorOut[eyeIndex]->srv.get();
	const float centerBlendFeather = std::isfinite(centerFeather) ?
	                                     ClampPeripheryTAACenterBlendFeather(centerFeather) :
	                                     ClampPeripheryTAACenterBlendFeather(FoveatedCommon::kCenterFeather);

	DispatchFoveatedBlendPass(
		centerSRV,
		outputUAV,
		outputWidthPerEye,
		outputHeight,
		rect,
		rectMinX,
		rectMinY,
		rectMaxX - rectMinX,
		rectMaxY - rectMinY,
		centerScale,
		centerHorizontalScale,
		centerOffset,
		centerBlendFeather);
	return true;
}

void Upscaling::ConfigureFoveatedPeripherySourceRegion(FoveatedEyeDispatchParams& params, const eastl::unique_ptr<Texture2D>& sourceTexture, uint32_t validWidth, uint32_t validHeight) const
{
	params.peripherySourceSRV = sourceTexture && sourceTexture->srv ? sourceTexture->srv.get() : nullptr;

	const uint32_t textureWidth = sourceTexture ? sourceTexture->desc.Width : 0u;
	const uint32_t textureHeight = sourceTexture ? sourceTexture->desc.Height : 0u;
	params.peripherySourceWidth = textureWidth ? textureWidth : validWidth;
	params.peripherySourceHeight = textureHeight ? textureHeight : validHeight;

	const auto sourceRegion = FoveatedRegionPlan::BuildTopLeftValidTextureRegion(validWidth, validHeight, params.peripherySourceWidth, params.peripherySourceHeight);
	params.peripherySourceScaleX = sourceRegion.scale.x;
	params.peripherySourceScaleY = sourceRegion.scale.y;
	params.peripherySourceOffsetX = sourceRegion.offset.x;
	params.peripherySourceOffsetY = sourceRegion.offset.y;
}

bool Upscaling::DispatchFoveatedVendorEyeComposite(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, const FoveatedEyeDispatchParams& params)
{
	if (!globals::game::isVR || eyeIndex >= 2)
		return false;
	if (!SupportsFoveatedVendorDispatch(a_upscaleMethod))
		return false;
	if (!params.inputWidthPerEye || !params.inputHeight || !params.outputWidthPerEye || !params.outputHeight)
		return false;
	if (!params.peripherySourceSRV || !params.peripherySourceWidth || !params.peripherySourceHeight)
		return false;

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !context || !deferred || !deferred->linearSampler)
		return false;

	if (!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->uav || !vrIntermediateColorOut[eyeIndex]->resource)
		return false;

	if (!params.visualizeMask &&
		(!params.centerColorInput || !params.centerDepthInput || !params.centerMotionVectorsInput ||
			!params.centerReactiveMaskInput || !params.centerTransparencyMaskInput)) {
		return false;
	}

	if (params.usePeripheryTAA) {
		if (!vrIntermediateColorIn[eyeIndex] || !vrIntermediateColorIn[eyeIndex]->srv ||
			!vrIntermediateDepth[eyeIndex] || !vrIntermediateDepth[eyeIndex]->srv ||
			!vrIntermediateMotionVectors[eyeIndex] || !vrIntermediateMotionVectors[eyeIndex]->srv ||
			!vrIntermediateReactiveMask[eyeIndex] || !vrIntermediateReactiveMask[eyeIndex]->srv ||
			!vrIntermediateTransparencyMask[eyeIndex] || !vrIntermediateTransparencyMask[eyeIndex]->srv ||
			!peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryReadIndex] || !peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv ||
			!peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryWriteIndex] || !peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav ||
			!peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryReadIndex] || !peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv ||
			!peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex] || !peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav ||
			!peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryReadIndex] || !peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv ||
			!peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex] || !peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav) {
			return false;
		}
	}

	const auto& regionPlan = foveatedRectCache.plan;
	if (!regionPlan.IsValid() || eyeIndex >= regionPlan.eyes.size())
		return false;
	const auto& eyePlan = regionPlan.eyes[eyeIndex];
	if (!eyePlan.IsValid())
		return false;

	const float2 centerOffset = eyePlan.centerOffset;
	const float taaOuterScale = params.usePeripheryTAA ? regionPlan.peripheryTAAOuterScale : 0.0f;
	const auto& underlayHole = eyePlan.centerUnderlayHoleOutput;
	const bool hasCenterUnderlayHole = underlayHole.IsValid();
	const auto& taaOuter = eyePlan.peripheryTAAOuterOutput;
	const bool hasTaaOuterRegion = params.usePeripheryTAA && taaOuter.IsValid();
	const auto& taaDispatch = eyePlan.peripheryTAAHistoryOutput.IsValid() ?
	                              eyePlan.peripheryTAAHistoryOutput :
	                              eyePlan.peripheryTAAOuterOutput;

	float4x4 currentViewProjInverse{};
	float4x4 previousViewProj{};
	float4 currentCameraPosAdjust{};
	float4 previousCameraPosAdjust{};
	if (params.usePeripheryTAA) {
		currentViewProjInverse = globals::game::frameBufferCached.GetCameraViewProjUnjittered(eyeIndex).Invert();
		previousViewProj = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered(eyeIndex);
		currentCameraPosAdjust = globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
		previousCameraPosAdjust = globals::game::frameBufferCached.GetCameraPreviousPosAdjust(eyeIndex);
	}

	ID3D11UnorderedAccessView* outputColorUAV = params.outputUAV ? params.outputUAV : vrIntermediateColorOut[eyeIndex]->uav.get();

	bool peripheryBindingsBound = false;
	auto bindPeripheryBindings = [&]() -> bool {
		if (peripheryBindingsBound)
			return true;

		auto* peripheryShader = GetFoveatedPeripheryCS();
		if (!peripheryShader || !foveatedPeripheryCB || !params.peripherySourceSRV || !outputColorUAV)
			return false;

		ID3D11Buffer* cb = foveatedPeripheryCB->CB();
		ID3D11SamplerState* samplers[1] = { deferred->linearSampler };
		ID3D11ShaderResourceView* srvs[1] = { params.peripherySourceSRV };
		ID3D11UnorderedAccessView* uavs[1] = { outputColorUAV };
		context->CSSetShader(peripheryShader, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetSamplers(0, 1, samplers);
		context->CSSetShaderResources(0, 1, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		peripheryBindingsBound = true;
		return true;
	};

	auto unbindPeripheryBindings = [&]() {
		if (!peripheryBindingsBound)
			return;

		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11SamplerState* nullSampler[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetSamplers(0, 1, nullSampler);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
		peripheryBindingsBound = false;
	};

	auto dispatchPeripheryBand = [&](uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight) -> bool {
		if (!dispatchWidth || !dispatchHeight)
			return true;
		if (!bindPeripheryBindings())
			return false;

		DispatchFoveatedPeripheryPass(
			params.peripherySourceSRV,
			outputColorUAV,
			params.peripherySourceWidth,
			params.peripherySourceHeight,
			params.outputWidthPerEye,
			params.outputHeight,
			outputOffsetX,
			outputOffsetY,
			dispatchWidth,
			dispatchHeight,
			params.centerScale,
			params.centerHorizontalScale,
			true,
			params.peripherySourceScaleX,
			params.peripherySourceScaleY,
			params.peripherySourceOffsetX,
			params.peripherySourceOffsetY,
			centerOffset.x,
			centerOffset.y);
		return true;
	};

	auto dispatchPeripheryTAA = [&](ID3D11ShaderResourceView* tileListSRV, uint32_t tileCount, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight) -> bool {
		DispatchPeripheryTAAPass(
			vrIntermediateColorIn[eyeIndex]->srv.get(),
			vrIntermediateDepth[eyeIndex]->srv.get(),
			vrIntermediateMotionVectors[eyeIndex]->srv.get(),
			vrIntermediateReactiveMask[eyeIndex]->srv.get(),
			vrIntermediateTransparencyMask[eyeIndex]->srv.get(),
			peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv.get(),
			peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv.get(),
			peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv.get(),
			outputColorUAV,
			peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav.get(),
			peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav.get(),
			peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav.get(),
			tileListSRV,
			tileCount,
			params.inputWidthPerEye,
			params.inputHeight,
			params.outputWidthPerEye,
			params.outputHeight,
			outputOffsetX,
			outputOffsetY,
			dispatchWidth,
			dispatchHeight,
			currentViewProjInverse,
			previousViewProj,
			currentCameraPosAdjust,
			previousCameraPosAdjust,
			params.resetPeripheryTAA,
			params.centerScale,
			params.centerHorizontalScale,
			centerOffset.x,
			centerOffset.y,
			params.peripherySourceScaleX,
			params.peripherySourceScaleY,
			params.peripherySourceOffsetX,
			params.peripherySourceOffsetY);
		return true;
	};

	auto dispatchPeripheryTAABand = [&](uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight) -> bool {
		if (!dispatchWidth || !dispatchHeight)
			return true;
		return dispatchPeripheryTAA(nullptr, 0, outputOffsetX, outputOffsetY, dispatchWidth, dispatchHeight);
	};

	auto dispatchRectMinusHole = [&](uint32_t outerMinX, uint32_t outerMinY, uint32_t outerMaxX, uint32_t outerMaxY, uint32_t holeMinX, uint32_t holeMinY, uint32_t holeMaxX, uint32_t holeMaxY, auto&& dispatchBand) -> bool {
		if (outerMaxX <= outerMinX || outerMaxY <= outerMinY)
			return true;

		const uint32_t clampedHoleMinX = std::clamp(holeMinX, outerMinX, outerMaxX);
		const uint32_t clampedHoleMaxX = std::clamp(holeMaxX, outerMinX, outerMaxX);
		const uint32_t clampedHoleMinY = std::clamp(holeMinY, outerMinY, outerMaxY);
		const uint32_t clampedHoleMaxY = std::clamp(holeMaxY, outerMinY, outerMaxY);
		const bool hasHole = clampedHoleMaxX > clampedHoleMinX && clampedHoleMaxY > clampedHoleMinY;
		if (!hasHole)
			return dispatchBand(outerMinX, outerMinY, outerMaxX - outerMinX, outerMaxY - outerMinY);

		const uint32_t outerWidth = outerMaxX - outerMinX;
		const uint32_t middleHeight = clampedHoleMaxY - clampedHoleMinY;
		return dispatchBand(outerMinX, outerMinY, outerWidth, clampedHoleMinY - outerMinY) &&
		       dispatchBand(outerMinX, clampedHoleMaxY, outerWidth, outerMaxY - clampedHoleMaxY) &&
		       dispatchBand(outerMinX, clampedHoleMinY, clampedHoleMinX - outerMinX, middleHeight) &&
		       dispatchBand(clampedHoleMaxX, clampedHoleMinY, outerMaxX - clampedHoleMaxX, middleHeight);
	};

	auto failAfterUnbind = [&]() {
		unbindPeripheryBindings();
		return false;
	};

	if (params.usePeripheryTAA) {
		if (hasTaaOuterRegion) {
			uint32_t tileCount = 0;
			const bool tileListBuilt = BuildPeripheryTAATileList(eyeIndex, params.outputWidthPerEye, params.outputHeight, params.centerScale, taaOuterScale, params.centerHorizontalScale, centerOffset.x, centerOffset.y, FoveatedRegionPlan::kDefaultPeripheryHistoryPadding, tileCount);
			const bool hasTileListSRV = peripheryTAATileBuffer[eyeIndex] && peripheryTAATileBuffer[eyeIndex]->srv;
			if (tileListBuilt && tileCount > 0 && hasTileListSRV) {
				if (!dispatchPeripheryTAA(peripheryTAATileBuffer[eyeIndex]->srv.get(), tileCount, 0, 0, params.outputWidthPerEye, params.outputHeight))
					return false;
			} else if (!tileListBuilt || tileCount == 0 || (tileCount > 0 && !hasTileListSRV)) {
				if (state->frameAnnotations)
					state->BeginPerfEvent("Periphery TAA Fallback Rect");
				const bool fallbackDispatched = hasCenterUnderlayHole ?
				                                    dispatchRectMinusHole(
														taaDispatch.minX,
														taaDispatch.minY,
														taaDispatch.maxX,
														taaDispatch.maxY,
														underlayHole.minX,
														underlayHole.minY,
														underlayHole.maxX,
														underlayHole.maxY,
														dispatchPeripheryTAABand) :
				                                    dispatchPeripheryTAABand(taaDispatch.minX, taaDispatch.minY, taaDispatch.Width(), taaDispatch.Height());
				if (state->frameAnnotations)
					state->EndPerfEvent();
				if (!fallbackDispatched)
					return failAfterUnbind();
			}

			if (!dispatchRectMinusHole(
					0,
					0,
					params.outputWidthPerEye,
					params.outputHeight,
					taaOuter.minX,
					taaOuter.minY,
					taaOuter.maxX,
					taaOuter.maxY,
					dispatchPeripheryBand)) {
				return failAfterUnbind();
			}
		} else if (!dispatchPeripheryBand(0, 0, params.outputWidthPerEye, params.outputHeight)) {
			return failAfterUnbind();
		}
	} else if (params.visualizeMask) {
		if (!dispatchPeripheryBand(0, 0, params.outputWidthPerEye, params.outputHeight))
			return failAfterUnbind();
	} else if (hasCenterUnderlayHole) {
		if (!dispatchRectMinusHole(
				0,
				0,
				params.outputWidthPerEye,
				params.outputHeight,
				underlayHole.minX,
				underlayHole.minY,
				underlayHole.maxX,
				underlayHole.maxY,
				dispatchPeripheryBand)) {
			return failAfterUnbind();
		}
	} else if (!dispatchPeripheryBand(0, 0, params.outputWidthPerEye, params.outputHeight)) {
		return failAfterUnbind();
	}

	unbindPeripheryBindings();

	if (params.visualizeMask)
		return true;

	return DispatchSingleFoveatedVendorEye(
		a_upscaleMethod,
		eyeIndex,
		params.centerColorInput,
		params.centerDepthInput,
		params.centerMotionVectorsInput,
		params.centerReactiveMaskInput,
		params.centerTransparencyMaskInput,
		params.outputWidthPerEye,
		params.outputHeight,
		params.inputWidthPerEye,
		params.inputHeight,
		params.centerScale,
		params.centerHorizontalScale,
		centerOffset,
		params.centerBlendFeather,
		params.centerColorInputBaseOffsetX,
		params.centerDepthInputBaseOffsetX,
		params.centerAuxInputBaseOffsetX,
		outputColorUAV);
}

bool Upscaling::DispatchFoveatedVendorUpscaling(UpscaleMethod a_upscaleMethod, ID3D11Resource* colorTexture, ID3D11Resource* depthTexture, ID3D11Resource* motionVectors, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask, ID3D11Resource* colorOutput)
{
	if (!globals::game::isVR)
		return false;
	if (!SupportsFoveatedVendorDispatch(a_upscaleMethod))
		return false;

	if (!colorTexture || !depthTexture || !motionVectors || !reactiveMask || !transparencyMask)
		return false;

	auto state = globals::state;
	if (!state)
		return false;

	uint32_t inputWidthPerEye = 0;
	uint32_t inputHeight = 0;
	uint32_t outputWidthPerEye = 0;
	uint32_t outputHeight = 0;
	if (!GetRuntimeFoveatedRegionDimensions(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight))
		return false;

	const bool visualizeMask = settings.foveatedPeripheryMaskVisualization;
	const bool usePeripheryTAA = IsPeripheryTAAPathActive(a_upscaleMethod);
	const bool usePeripheryTAAProfile = IsPeripheryTAAEnabled(a_upscaleMethod);
	const auto foveatedProfile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
	const float centerScale = foveatedProfile.centerScale;
	const float centerHorizontalScale = foveatedProfile.centerHorizontalScale;
	const float effectiveCenterBlendFeather = usePeripheryTAA ? ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) : FoveatedCommon::kCenterFeather;
	if (!BuildFoveatedDispatchRects(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight, true, centerScale, effectiveCenterBlendFeather, centerHorizontalScale, usePeripheryTAAProfile))
		return false;

	if (!EnsureFoveatedDispatchShaders(usePeripheryTAA, visualizeMask, "", "skipping foveated vendor dispatch")) {
		return false;
	}

	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	auto renderer = globals::game::renderer;
	if (!context || !deferred || !deferred->linearSampler || !renderer)
		return false;

	// Keep all foveated VR paths on per-eye inputs. The old DLAA/direct-source
	// shortcut, and the Peripheral TAA center pass, sampled kMAIN directly and
	// bypassed the HMD hidden-area cleanup from PreparePerEyeInputs.
	// Keep copyAuxiliaryInputs=false here: Encode Upscaling Textures has already
	// written the per-eye motion/reactive/transparency resources used by
	// Periphery TAA, and this pass must not overwrite them.
	const bool copyDepthInput = !visualizeMask && (a_upscaleMethod == UpscaleMethod::kDLSS || usePeripheryTAA);
	if (!PreparePerEyeInputs(colorTexture, depthTexture, motionVectors, reactiveMask, transparencyMask, false, copyDepthInput))
		return false;
	if (usePeripheryTAA && !EnsurePeripheryTAAResources(outputWidthPerEye, outputHeight, colorTexture))
		return false;

	const bool resetPeripheryTAA = usePeripheryTAA && (ShouldResetHistoryThisFrame() || !peripheryTAAHistoryValid);
	const uint32_t peripheryTAAReadIndex = peripheryTAAHistoryReadIndex;
	const uint32_t peripheryTAAWriteIndex = 1u - peripheryTAAReadIndex;
	bool anyEyeDispatched = false;

	for (uint32_t eye = 0; eye < 2; ++eye) {
		if (!vrIntermediateColorIn[eye] || !vrIntermediateColorIn[eye]->srv || !vrIntermediateColorIn[eye]->resource ||
			!vrIntermediateColorOut[eye] || !vrIntermediateColorOut[eye]->uav || !vrIntermediateColorOut[eye]->resource) {
			return false;
		}

		ID3D11Resource* centerDepthInput = nullptr;
		if (!visualizeMask) {
			centerDepthInput = a_upscaleMethod == UpscaleMethod::kFSR ?
			                       (vrIntermediateLinearDepth[eye] ? vrIntermediateLinearDepth[eye]->resource.get() : nullptr) :
			                       (vrIntermediateDepth[eye] ? vrIntermediateDepth[eye]->resource.get() : nullptr);
			if (!centerDepthInput)
				return false;
		}

		FoveatedEyeDispatchParams params{};
		params.inputWidthPerEye = inputWidthPerEye;
		params.inputHeight = inputHeight;
		params.outputWidthPerEye = outputWidthPerEye;
		params.outputHeight = outputHeight;
		params.centerScale = centerScale;
		params.centerHorizontalScale = centerHorizontalScale;
		params.centerBlendFeather = effectiveCenterBlendFeather;
		params.usePeripheryTAA = usePeripheryTAA;
		params.usePeripheryTAAProfile = usePeripheryTAAProfile;
		params.visualizeMask = visualizeMask;
		params.resetPeripheryTAA = resetPeripheryTAA;
		params.peripheryTAAHistoryReadIndex = peripheryTAAReadIndex;
		params.peripheryTAAHistoryWriteIndex = peripheryTAAWriteIndex;
		ConfigureFoveatedPeripherySourceRegion(params, vrIntermediateColorIn[eye], inputWidthPerEye, inputHeight);
		params.centerColorInput = vrIntermediateColorIn[eye]->resource.get();
		params.centerDepthInput = centerDepthInput;
		params.centerMotionVectorsInput = vrIntermediateMotionVectors[eye] ? vrIntermediateMotionVectors[eye]->resource.get() : nullptr;
		params.centerReactiveMaskInput = vrIntermediateReactiveMask[eye] ? vrIntermediateReactiveMask[eye]->resource.get() : nullptr;
		params.centerTransparencyMaskInput = vrIntermediateTransparencyMask[eye] ? vrIntermediateTransparencyMask[eye]->resource.get() : nullptr;

		static bool loggedFoveatedDispatchFailure = false;
		try {
			if (!DispatchFoveatedVendorEyeComposite(a_upscaleMethod, eye, params)) {
				UnbindUpscalingResources();
				if (anyEyeDispatched)
					RequestHistoryReset();
				return false;
			}
			anyEyeDispatched = true;
		} catch (const std::exception& e) {
			UnbindUpscalingResources();
			if (anyEyeDispatched)
				RequestHistoryReset();
			LogWarnOnce(
				loggedFoveatedDispatchFailure,
				"[Upscaling] Foveated dispatch threw; skipping foveated vendor dispatch",
				e);
			return false;
		} catch (...) {
			UnbindUpscalingResources();
			if (anyEyeDispatched)
				RequestHistoryReset();
			LogWarnOnce(
				loggedFoveatedDispatchFailure,
				"[Upscaling] Foveated dispatch threw; skipping foveated vendor dispatch");
			return false;
		}
	}

	if (usePeripheryTAA) {
		peripheryTAAHistoryReadIndex = peripheryTAAWriteIndex;
		peripheryTAAHistoryValid = true;
	}

	FinalizePerEyeOutputs(colorOutput ? colorOutput : colorTexture);
	return true;
}

bool Upscaling::DispatchSubmitStageFoveatedVendorEye(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, ID3D11Resource* outputResource, ID3D11UnorderedAccessView* outputUAV)
{
	if (!globals::game::isVR || eyeIndex >= 2)
		return false;
	if (!SupportsFoveatedVendorDispatch(a_upscaleMethod))
		return false;
	if (!inputWidthPerEye || !inputHeight || !outputWidthPerEye || !outputHeight)
		return false;
	const auto inputStereoLayout = ResolveVRSideBySideStereoLayout(inputWidthPerEye, inputHeight);
	if (!inputStereoLayout.IsValid())
		return false;

	auto state = globals::state;
	auto deferred = globals::deferred;
	auto renderer = globals::game::renderer;
	if (!state || !deferred || !deferred->linearSampler || !renderer)
		return false;

	if (!vrIntermediateColorIn[eyeIndex] || !vrIntermediateColorIn[eyeIndex]->resource || !vrIntermediateColorIn[eyeIndex]->srv ||
		!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->resource || !vrIntermediateColorOut[eyeIndex]->uav) {
		return false;
	}
	if (!outputResource)
		outputResource = vrIntermediateColorOut[eyeIndex]->resource.get();
	if (!outputUAV)
		outputUAV = vrIntermediateColorOut[eyeIndex]->uav.get();
	if (!outputResource || !outputUAV)
		return false;

	const bool visualizeMask = settings.foveatedPeripheryMaskVisualization;
	const bool usePeripheryTAA = IsPeripheryTAAPathActive(a_upscaleMethod);
	const bool usePeripheryTAAProfile = IsPeripheryTAAEnabled(a_upscaleMethod);
	const auto foveatedProfile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
	const float centerScale = foveatedProfile.centerScale;
	const float centerHorizontalScale = foveatedProfile.centerHorizontalScale;
	const float effectiveCenterBlendFeather = usePeripheryTAA ? ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) : FoveatedCommon::kCenterFeather;
	if (!BuildFoveatedDispatchRects(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight, true, centerScale, effectiveCenterBlendFeather, centerHorizontalScale, usePeripheryTAAProfile))
		return false;

	if (!EnsureFoveatedDispatchShaders(usePeripheryTAA, visualizeMask, "Submit-stage ", "falling back to full-eye dispatch")) {
		return false;
	}

	if (!visualizeMask) {
		const bool centerUsesFSRDepth = a_upscaleMethod == UpscaleMethod::kFSR;
		if ((!centerUsesFSRDepth && (!vrIntermediateDepth[eyeIndex] || !vrIntermediateDepth[eyeIndex]->resource || !vrIntermediateDepth[eyeIndex]->srv)) ||
			!vrIntermediateMotionVectors[eyeIndex] || !vrIntermediateMotionVectors[eyeIndex]->resource ||
			!vrIntermediateReactiveMask[eyeIndex] || !vrIntermediateReactiveMask[eyeIndex]->resource ||
			!vrIntermediateTransparencyMask[eyeIndex] || !vrIntermediateTransparencyMask[eyeIndex]->resource) {
			return false;
		}
		if (centerUsesFSRDepth &&
			(!vrIntermediateLinearDepth[eyeIndex] || !vrIntermediateLinearDepth[eyeIndex]->resource)) {
			return false;
		}
	}

	if (usePeripheryTAA) {
		if (!vrIntermediateDepth[eyeIndex] || !vrIntermediateDepth[eyeIndex]->srv ||
			!vrIntermediateMotionVectors[eyeIndex] || !vrIntermediateMotionVectors[eyeIndex]->srv ||
			!vrIntermediateReactiveMask[eyeIndex] || !vrIntermediateReactiveMask[eyeIndex]->srv ||
			!vrIntermediateTransparencyMask[eyeIndex] || !vrIntermediateTransparencyMask[eyeIndex]->srv) {
			return false;
		}
		if (!EnsurePeripheryTAAResources(outputWidthPerEye, outputHeight, outputResource))
			return false;
	}

	const bool resetPeripheryTAA = usePeripheryTAA && (ShouldResetHistoryThisFrame() || !peripheryTAAHistoryValid);
	const uint32_t peripheryTAAReadIndex = peripheryTAAHistoryReadIndex;
	const uint32_t peripheryTAAWriteIndex = 1u - peripheryTAAReadIndex;
	const uint32_t currentFrame = state->frameCount;
	const bool previousPeripheryFrameMatches = submitStageFoveatedPeripheryTAAFrame == currentFrame;
	const bool previousPeripheryEyeReady = previousPeripheryFrameMatches && submitStageFoveatedPeripheryTAAEyeReady[eyeIndex];
	auto peripheryEyeReadyGuard = ScopeExit([&]() {
		if (!usePeripheryTAA)
			return;

		if (previousPeripheryFrameMatches) {
			submitStageFoveatedPeripheryTAAEyeReady[eyeIndex] = previousPeripheryEyeReady;
		} else if (submitStageFoveatedPeripheryTAAFrame == currentFrame) {
			submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
			submitStageFoveatedPeripheryTAAEyeReady = {};
		}
	});

	ID3D11Resource* centerDepthInput = nullptr;
	if (!visualizeMask) {
		centerDepthInput = a_upscaleMethod == UpscaleMethod::kFSR ?
		                       (vrIntermediateLinearDepth[eyeIndex] ? vrIntermediateLinearDepth[eyeIndex]->resource.get() : nullptr) :
		                       (vrIntermediateDepth[eyeIndex] ? vrIntermediateDepth[eyeIndex]->resource.get() : nullptr);
		if (!centerDepthInput)
			return false;
	}

	FoveatedEyeDispatchParams params{};
	params.inputWidthPerEye = inputWidthPerEye;
	params.inputHeight = inputHeight;
	params.outputWidthPerEye = outputWidthPerEye;
	params.outputHeight = outputHeight;
	params.centerScale = centerScale;
	params.centerHorizontalScale = centerHorizontalScale;
	params.centerBlendFeather = effectiveCenterBlendFeather;
	params.usePeripheryTAA = usePeripheryTAA;
	params.usePeripheryTAAProfile = usePeripheryTAAProfile;
	params.visualizeMask = visualizeMask;
	params.resetPeripheryTAA = resetPeripheryTAA;
	params.peripheryTAAHistoryReadIndex = peripheryTAAReadIndex;
	params.peripheryTAAHistoryWriteIndex = peripheryTAAWriteIndex;
	ConfigureFoveatedPeripherySourceRegion(params, vrIntermediateColorIn[eyeIndex], inputWidthPerEye, inputHeight);
	params.centerColorInput = vrIntermediateColorIn[eyeIndex]->resource.get();
	params.centerDepthInput = centerDepthInput;
	params.centerMotionVectorsInput = vrIntermediateMotionVectors[eyeIndex] ? vrIntermediateMotionVectors[eyeIndex]->resource.get() : nullptr;
	params.centerReactiveMaskInput = vrIntermediateReactiveMask[eyeIndex] ? vrIntermediateReactiveMask[eyeIndex]->resource.get() : nullptr;
	params.centerTransparencyMaskInput = vrIntermediateTransparencyMask[eyeIndex] ? vrIntermediateTransparencyMask[eyeIndex]->resource.get() : nullptr;
	params.outputUAV = outputUAV;

	static bool loggedFoveatedDispatchFailure = false;
	try {
		if (!DispatchFoveatedVendorEyeComposite(a_upscaleMethod, eyeIndex, params)) {
			UnbindUpscalingResources();
			return false;
		}
	} catch (const std::exception& e) {
		UnbindUpscalingResources();
		LogWarnOnce(
			loggedFoveatedDispatchFailure,
			"[Upscaling] Submit-stage foveated dispatch threw; falling back to full-eye dispatch",
			e);
		MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage foveated dispatch");
		return false;
	} catch (...) {
		UnbindUpscalingResources();
		LogWarnOnce(
			loggedFoveatedDispatchFailure,
			"[Upscaling] Submit-stage foveated dispatch threw; falling back to full-eye dispatch");
		MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage foveated dispatch");
		return false;
	}

	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	if (depthTexture.depthSRV) {
		ClearHMDMaskForEye(
			HMDMaskClearPhase::SubmitStageFoveatedOutput,
			outputUAV,
			depthTexture.depthSRV,
			inputWidthPerEye,
			inputHeight,
			outputWidthPerEye,
			outputHeight,
			inputStereoLayout.eyes[eyeIndex].minX,
			0u);
	}

	if (usePeripheryTAA) {
		if (submitStageFoveatedPeripheryTAAFrame != currentFrame) {
			submitStageFoveatedPeripheryTAAFrame = currentFrame;
			submitStageFoveatedPeripheryTAAEyeReady = {};
		}

		submitStageFoveatedPeripheryTAAEyeReady[eyeIndex] = true;
		if (submitStageFoveatedPeripheryTAAEyeReady[0] && submitStageFoveatedPeripheryTAAEyeReady[1]) {
			peripheryTAAHistoryReadIndex = peripheryTAAWriteIndex;
			peripheryTAAHistoryValid = true;
			submitStageFoveatedPeripheryTAAEyeReady = {};
		}
	}

	peripheryEyeReadyGuard.Release();
	return true;
}

void Upscaling::CreateVRIntermediateTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
	ID3D11Resource* colorSrc, ID3D11Resource* mvecSrc, ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc)
{
	// All buffers are per-eye: Streamline validates all extents against the input color texture
	// dimensions, so every tagged resource must be isolated per-eye at {0,0}.
	D3D11_TEXTURE2D_DESC colorSrcDesc{};
	static_cast<ID3D11Texture2D*>(colorSrc)->GetDesc(&colorSrcDesc);
	const bool presentationOutputActive = IsPresentationUpscalingActive();
	// Keep submit-stage output copy-compatible with the stereo source. The desktop
	// mirror writeback uses CopySubresourceRegion into that source texture.
	const DXGI_FORMAT colorOutFormat = colorSrcDesc.Format;
	const bool requiresColorOutRTV = presentationOutputActive;
	const uint32_t allocationInWidth = inWidth;
	const uint32_t allocationInHeight = inHeight;

	for (int i = 0; i < 2; i++) {
		std::string suffix = (i == 0) ? "Left" : "Right";

		vrIntermediateColorIn[i] = CreateTextureFromSource(colorSrc, allocationInWidth, allocationInHeight, false, true, true, ("Upscale_ColorIn_" + suffix).c_str());
		vrIntermediateColorOut[i] =
			colorOutFormat == colorSrcDesc.Format ?
				CreateTextureFromSource(colorSrc, outWidth, outHeight, false, true, true, ("Upscale_ColorOut_" + suffix).c_str(), requiresColorOutRTV) :
				CreateNamedTexture2D(outWidth, outHeight, colorOutFormat, true, true, requiresColorOutRTV, ("Upscale_ColorOut_" + suffix).c_str());

		// Depth copy: R24G8_TYPELESS matches the game's D24S8 typeless cast-group.
		// This avoids format-group copy failures on some drivers.
		{
			D3D11_TEXTURE2D_DESC depthDesc = {};
			depthDesc.Width = allocationInWidth;
			depthDesc.Height = allocationInHeight;
			depthDesc.MipLevels = 1;
			depthDesc.ArraySize = 1;
			depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
			depthDesc.SampleDesc.Count = 1;
			depthDesc.Usage = D3D11_USAGE_DEFAULT;
			depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			vrIntermediateDepth[i] = eastl::make_unique<Texture2D>(depthDesc);

			Util::SetResourceName(vrIntermediateDepth[i]->resource.get(), ("Upscale_Depth_" + suffix).c_str());

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			vrIntermediateDepth[i]->CreateSRV(srvDesc);
		}

		// FSR input depth: typed R32_FLOAT so FidelityFX receives a known surface format.
		{
			D3D11_TEXTURE2D_DESC linearDepthDesc = {};
			linearDepthDesc.Width = allocationInWidth;
			linearDepthDesc.Height = allocationInHeight;
			linearDepthDesc.MipLevels = 1;
			linearDepthDesc.ArraySize = 1;
			linearDepthDesc.Format = DXGI_FORMAT_R32_FLOAT;
			linearDepthDesc.SampleDesc.Count = 1;
			linearDepthDesc.Usage = D3D11_USAGE_DEFAULT;
			linearDepthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			vrIntermediateLinearDepth[i] = eastl::make_unique<Texture2D>(linearDepthDesc);

			Util::SetResourceName(vrIntermediateLinearDepth[i]->resource.get(), ("Upscale_LinearDepth_" + suffix).c_str());

			D3D11_SHADER_RESOURCE_VIEW_DESC linearSRVDesc = {};
			linearSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
			linearSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			linearSRVDesc.Texture2D.MipLevels = 1;
			vrIntermediateLinearDepth[i]->CreateSRV(linearSRVDesc);

			D3D11_UNORDERED_ACCESS_VIEW_DESC linearUAVDesc = {};
			linearUAVDesc.Format = DXGI_FORMAT_R32_FLOAT;
			linearUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			linearUAVDesc.Texture2D.MipSlice = 0;
			vrIntermediateLinearDepth[i]->CreateUAV(linearUAVDesc);
		}

		vrIntermediateMotionVectors[i] = CreateTextureFromSource(mvecSrc, allocationInWidth, allocationInHeight, false, true, true, ("Upscale_MVec_" + suffix).c_str());
		vrIntermediateReactiveMask[i] = CreateTextureFromSource(reactiveSrc, allocationInWidth, allocationInHeight, false, true, true, ("Upscale_Reactive_" + suffix).c_str());
		vrIntermediateTransparencyMask[i] = CreateTextureFromSource(transparencySrc, allocationInWidth, allocationInHeight, false, true, true, ("Upscale_Transparency_" + suffix).c_str());
	}

	logger::debug("[Upscaling] Created VR intermediate textures: per-eye in {}x{}, out {}x{}",
		inWidth, inHeight, outWidth, outHeight);
}

void Upscaling::EnsureVRIntermediateTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
	ID3D11Resource* colorSrc, ID3D11Resource* mvecSrc, ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc)
{
	D3D11_TEXTURE2D_DESC colorSrcDesc{};
	static_cast<ID3D11Texture2D*>(colorSrc)->GetDesc(&colorSrcDesc);
	D3D11_TEXTURE2D_DESC mvecSrcDesc{};
	static_cast<ID3D11Texture2D*>(mvecSrc)->GetDesc(&mvecSrcDesc);
	D3D11_TEXTURE2D_DESC reactiveSrcDesc{};
	static_cast<ID3D11Texture2D*>(reactiveSrc)->GetDesc(&reactiveSrcDesc);
	D3D11_TEXTURE2D_DESC transparencySrcDesc{};
	static_cast<ID3D11Texture2D*>(transparencySrc)->GetDesc(&transparencySrcDesc);
	const bool presentationOutputActive = IsPresentationUpscalingActive();
	// Must match CreateVRIntermediateTextures(), otherwise the cached output can
	// make the desktop mirror writeback fail its format compatibility check.
	const DXGI_FORMAT expectedColorOutFormat = colorSrcDesc.Format;
	const bool requiresColorOutRTV = presentationOutputActive;
	const uint32_t allocationInWidth = inWidth;
	const uint32_t allocationInHeight = inHeight;
	const auto coversInput = [allocationInWidth, allocationInHeight](const eastl::unique_ptr<Texture2D>& texture, DXGI_FORMAT format, bool requireUAV) {
		return texture &&
		       texture->resource &&
		       texture->srv &&
		       (!requireUAV || texture->uav) &&
		       texture->desc.Width >= allocationInWidth &&
		       texture->desc.Height >= allocationInHeight &&
		       texture->desc.Format == format;
	};
	const auto matchesOutput = [outWidth, outHeight, expectedColorOutFormat, requiresColorOutRTV](const eastl::unique_ptr<Texture2D>& texture) {
		return texture &&
		       texture->resource &&
		       texture->srv &&
		       texture->uav &&
		       (!requiresColorOutRTV || texture->rtv) &&
		       texture->desc.Width == outWidth &&
		       texture->desc.Height == outHeight &&
		       texture->desc.Format == expectedColorOutFormat;
	};

	bool hasAllIntermediates =
		vrIntermediateColorIn[0] && vrIntermediateColorIn[1] &&
		vrIntermediateColorOut[0] && vrIntermediateColorOut[1] &&
		vrIntermediateDepth[0] && vrIntermediateDepth[1] &&
		vrIntermediateLinearDepth[0] && vrIntermediateLinearDepth[1] &&
		vrIntermediateMotionVectors[0] && vrIntermediateMotionVectors[1] &&
		vrIntermediateReactiveMask[0] && vrIntermediateReactiveMask[1] &&
		vrIntermediateTransparencyMask[0] && vrIntermediateTransparencyMask[1];

	bool needsRecreate = !hasAllIntermediates;
	uint32_t currentInWidth = 0;
	uint32_t currentInHeight = 0;
	uint32_t currentOutWidth = 0;
	uint32_t currentOutHeight = 0;
	bool currentHasRequiredViews = false;
	if (!needsRecreate) {
		currentInWidth = vrIntermediateColorIn[0]->desc.Width;
		currentInHeight = vrIntermediateColorIn[0]->desc.Height;
		currentOutWidth = vrIntermediateColorOut[0]->desc.Width;
		currentOutHeight = vrIntermediateColorOut[0]->desc.Height;
		currentHasRequiredViews = true;
		for (uint32_t eye = 0; eye < 2 && !needsRecreate; ++eye) {
			const bool eyeHasRequiredViews =
				coversInput(vrIntermediateColorIn[eye], colorSrcDesc.Format, true) &&
				matchesOutput(vrIntermediateColorOut[eye]) &&
				coversInput(vrIntermediateDepth[eye], DXGI_FORMAT_R24G8_TYPELESS, false) &&
				coversInput(vrIntermediateLinearDepth[eye], DXGI_FORMAT_R32_FLOAT, true) &&
				coversInput(vrIntermediateMotionVectors[eye], mvecSrcDesc.Format, true) &&
				coversInput(vrIntermediateReactiveMask[eye], reactiveSrcDesc.Format, true) &&
				coversInput(vrIntermediateTransparencyMask[eye], transparencySrcDesc.Format, true);
			currentHasRequiredViews = currentHasRequiredViews && eyeHasRequiredViews;
			needsRecreate = !eyeHasRequiredViews;
		}
	}

	if (needsRecreate) {
		const bool cacheMatchesOutputFormat =
			cachedVRIntermediateTextures.colorOut[0] &&
			cachedVRIntermediateTextures.colorOut[1] &&
			cachedVRIntermediateTextures.colorOut[0]->desc.Format == expectedColorOutFormat &&
			cachedVRIntermediateTextures.colorOut[1]->desc.Format == expectedColorOutFormat &&
			(!requiresColorOutRTV || (cachedVRIntermediateTextures.colorOut[0]->rtv && cachedVRIntermediateTextures.colorOut[1]->rtv));
		if (MatchesVRIntermediateTextureCache(cachedVRIntermediateTextures, inWidth, inHeight, outWidth, outHeight) && cacheMatchesOutputFormat) {
			logger::debug("[Upscaling] Reusing cached VR intermediates: per-eye in {}x{}, out {}x{}",
				inWidth, inHeight, outWidth, outHeight);

			for (uint32_t i = 0; i < 2; ++i) {
				if (hasAllIntermediates && currentHasRequiredViews) {
					std::swap(vrIntermediateColorIn[i], cachedVRIntermediateTextures.colorIn[i]);
					std::swap(vrIntermediateColorOut[i], cachedVRIntermediateTextures.colorOut[i]);
					std::swap(vrIntermediateDepth[i], cachedVRIntermediateTextures.depth[i]);
					std::swap(vrIntermediateLinearDepth[i], cachedVRIntermediateTextures.linearDepth[i]);
					std::swap(vrIntermediateMotionVectors[i], cachedVRIntermediateTextures.motionVectors[i]);
					std::swap(vrIntermediateReactiveMask[i], cachedVRIntermediateTextures.reactiveMask[i]);
					std::swap(vrIntermediateTransparencyMask[i], cachedVRIntermediateTextures.transparencyMask[i]);
				} else {
					vrIntermediateColorIn[i] = std::move(cachedVRIntermediateTextures.colorIn[i]);
					vrIntermediateColorOut[i] = std::move(cachedVRIntermediateTextures.colorOut[i]);
					vrIntermediateDepth[i] = std::move(cachedVRIntermediateTextures.depth[i]);
					vrIntermediateLinearDepth[i] = std::move(cachedVRIntermediateTextures.linearDepth[i]);
					vrIntermediateMotionVectors[i] = std::move(cachedVRIntermediateTextures.motionVectors[i]);
					vrIntermediateReactiveMask[i] = std::move(cachedVRIntermediateTextures.reactiveMask[i]);
					vrIntermediateTransparencyMask[i] = std::move(cachedVRIntermediateTextures.transparencyMask[i]);
				}
			}

			if (hasAllIntermediates && currentHasRequiredViews) {
				cachedVRIntermediateTextures.inWidth = currentInWidth;
				cachedVRIntermediateTextures.inHeight = currentInHeight;
				cachedVRIntermediateTextures.outWidth = currentOutWidth;
				cachedVRIntermediateTextures.outHeight = currentOutHeight;
			} else {
				ClearVRIntermediateTextureCache(cachedVRIntermediateTextures);
			}
			return;
		}

		if (hasAllIntermediates && currentHasRequiredViews) {
			for (uint32_t i = 0; i < 2; ++i) {
				cachedVRIntermediateTextures.colorIn[i] = std::move(vrIntermediateColorIn[i]);
				cachedVRIntermediateTextures.colorOut[i] = std::move(vrIntermediateColorOut[i]);
				cachedVRIntermediateTextures.depth[i] = std::move(vrIntermediateDepth[i]);
				cachedVRIntermediateTextures.linearDepth[i] = std::move(vrIntermediateLinearDepth[i]);
				cachedVRIntermediateTextures.motionVectors[i] = std::move(vrIntermediateMotionVectors[i]);
				cachedVRIntermediateTextures.reactiveMask[i] = std::move(vrIntermediateReactiveMask[i]);
				cachedVRIntermediateTextures.transparencyMask[i] = std::move(vrIntermediateTransparencyMask[i]);
			}
			cachedVRIntermediateTextures.inWidth = currentInWidth;
			cachedVRIntermediateTextures.inHeight = currentInHeight;
			cachedVRIntermediateTextures.outWidth = currentOutWidth;
			cachedVRIntermediateTextures.outHeight = currentOutHeight;
		}

		logger::debug("[Upscaling] (Re)creating VR intermediates: per-eye in {}x{}, out {}x{}",
			inWidth, inHeight, outWidth, outHeight);
		CreateVRIntermediateTextures(inWidth, inHeight, outWidth, outHeight, colorSrc, mvecSrc, reactiveSrc, transparencySrc);
	}
}

bool Upscaling::EnsureVRPresentationTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
	ID3D11Resource* colorSrc)
{
	if (!colorSrc || !inWidth || !inHeight || !outWidth || !outHeight)
		return false;

	D3D11_TEXTURE2D_DESC colorSrcDesc{};
	if (!TryGetTexture2DDesc(colorSrc, colorSrcDesc))
		return false;

	constexpr DXGI_FORMAT outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	const uint32_t allocationInWidth = std::max<uint32_t>(1u, inWidth);
	const uint32_t allocationInHeight = std::max<uint32_t>(1u, inHeight);
	const auto matchesInput = [allocationInWidth, allocationInHeight, sourceFormat = colorSrcDesc.Format](const eastl::unique_ptr<Texture2D>& texture) {
		return texture &&
		       texture->resource &&
		       texture->srv &&
		       texture->desc.Width >= allocationInWidth &&
		       texture->desc.Height >= allocationInHeight &&
		       texture->desc.Format == sourceFormat;
	};
	const auto matchesOutput = [outWidth, outHeight, outputFormat](const eastl::unique_ptr<Texture2D>& texture) {
		return texture &&
		       texture->resource &&
		       texture->srv &&
		       texture->uav &&
		       texture->desc.Width == outWidth &&
		       texture->desc.Height == outHeight &&
		       texture->desc.Format == outputFormat;
	};

	bool needsRecreate = false;
	for (uint32_t eye = 0; eye < 2; ++eye) {
		needsRecreate = needsRecreate ||
		                !matchesInput(vrIntermediateColorIn[eye]) ||
		                !matchesOutput(vrIntermediateColorOut[eye]);
	}

	if (!needsRecreate)
		return true;

	logger::debug("[VRRenderScale] (Re)creating presentation textures: per-eye in {}x{}, out {}x{}",
		inWidth, inHeight, outWidth, outHeight);

	for (uint32_t eye = 0; eye < 2; ++eye) {
		const std::string suffix = eye == 0 ? "Left" : "Right";
		vrIntermediateColorIn[eye] = CreateTextureFromSource(
			colorSrc,
			allocationInWidth,
			allocationInHeight,
			false,
			true,
			false,
			("PerfMode_ColorIn_" + suffix).c_str());
		vrIntermediateColorOut[eye] = CreateNamedTexture2D(
			outWidth,
			outHeight,
			outputFormat,
			true,
			true,
			true,
			("PerfMode_ColorOut_" + suffix).c_str());
	}

	return true;
}

bool Upscaling::EnsureSubmitStageDLSSSharpenerTexture(uint32_t eyeIndex, const Texture2D& colorOutput)
{
	if (eyeIndex >= 2 || !colorOutput.resource || !colorOutput.srv || !colorOutput.uav ||
		!colorOutput.desc.Width || !colorOutput.desc.Height) {
		return false;
	}

	auto& texture = submitStageDLSSSharpenerTexture[eyeIndex];
	const auto matchesOutput = [&]() {
		return texture &&
		       texture->resource &&
		       texture->srv &&
		       texture->uav &&
		       texture->desc.Width == colorOutput.desc.Width &&
		       texture->desc.Height == colorOutput.desc.Height &&
		       texture->desc.Format == colorOutput.desc.Format;
	};
	if (matchesOutput())
		return true;

	const std::string suffix = eyeIndex == 0 ? "Left" : "Right";
	texture = CreateNamedTexture2D(
		colorOutput.desc.Width,
		colorOutput.desc.Height,
		colorOutput.desc.Format,
		true,
		true,
		false,
		("SubmitStageDLSSSharpener_" + suffix).c_str());

	return matchesOutput();
}

bool Upscaling::ApplySubmitStageDLSSSharpening(uint32_t eyeIndex, const Texture2D& sharpenInput)
{
	if (!ShouldApplyDLSSSharpening())
		return true;
	if (eyeIndex >= 2)
		return false;

	auto context = globals::d3d::context;
	if (!context)
		return false;

	auto& colorOutput = vrIntermediateColorOut[eyeIndex];
	if (!colorOutput || !colorOutput->resource || !colorOutput->uav ||
		!colorOutput->desc.Width || !colorOutput->desc.Height ||
		!sharpenInput.resource || !sharpenInput.srv ||
		sharpenInput.desc.Width != colorOutput->desc.Width ||
		sharpenInput.desc.Height != colorOutput->desc.Height ||
		sharpenInput.desc.Format != colorOutput->desc.Format) {
		return false;
	}

	static bool loggedSharpenerFailure[2] = {};
	try {
		const uint32_t dispatchWidth = colorOutput->desc.Width;
		const uint32_t dispatchHeight = colorOutput->desc.Height;

		UnbindUpscalingResources();
		if (!DispatchDLSSSharpener(*this, sharpenInput.srv.get(), colorOutput->uav.get(), dispatchWidth, dispatchHeight)) {
			LogWarnOnceFmt(
				loggedSharpenerFailure[eyeIndex],
				"[Upscaling] Submit-stage DLSS sharpening skipped for eye {} because {} dispatch failed.",
				eyeIndex,
				GetDLSSSharpenerModeName(GetDLSSSharpenerMode()));
			return false;
		}
		if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage DLSS sharpening"))
			return false;
		return true;
	} catch (const std::exception& e) {
		if (!MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage DLSS sharpening")) {
			LogWarnOnceFmt(
				loggedSharpenerFailure[eyeIndex],
				"[Upscaling] Submit-stage DLSS sharpening threw for eye {}; submitting unsharpened output: {}",
				eyeIndex,
				e.what());
		}
		return false;
	} catch (...) {
		if (!MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage DLSS sharpening")) {
			LogWarnOnceFmt(
				loggedSharpenerFailure[eyeIndex],
				"[Upscaling] Submit-stage DLSS sharpening threw for eye {}; submitting unsharpened output",
				eyeIndex);
		}
		return false;
	}
}

bool Upscaling::PreparePerEyeInputs(ID3D11Resource* colorSrc, ID3D11Resource* depthSrc, ID3D11Resource* mvecSrc,
	ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc, bool copyAuxiliaryInputs, bool copyDepthInput)
{
	if (!globals::game::isVR)
		return false;

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto device = globals::d3d::device;
	auto renderer = globals::game::renderer;
	if (!state || !context || !device || !renderer || !colorSrc || !mvecSrc || !reactiveSrc || !transparencySrc ||
		(copyDepthInput && !depthSrc)) {
		return false;
	}

	const bool frameAnnotations = state->frameAnnotations;
	if (frameAnnotations)
		state->BeginPerfEvent("VR Upscaling Prepare");
	auto perfEventGuard = ScopeExit([&]() {
		if (frameAnnotations)
			state->EndPerfEvent();
	});

	auto screenSize = runtimeResolutionPlan.finalOutputSize;
	auto renderSize = runtimeResolutionPlan.engineRenderSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f)
		screenSize = state->screenSize;
	if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
		renderSize = Util::ConvertToDynamic(screenSize);

	uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
	uint32_t eyeHeightOut = (uint32_t)screenSize.y;
	uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
	uint32_t eyeHeightIn = (uint32_t)renderSize.y;

	try {
		EnsureVRIntermediateTextures(eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut, colorSrc, mvecSrc, reactiveSrc, transparencySrc);
	} catch (const std::exception& e) {
		logger::warn("[Upscaling] Failed to prepare VR per-eye inputs: {}", e.what());
		return false;
	} catch (...) {
		logger::warn("[Upscaling] Failed to prepare VR per-eye inputs.");
		return false;
	}

	if (!vrIntermediateColorIn[0] || !vrIntermediateColorIn[0]->resource || !vrIntermediateColorIn[0]->uav ||
		!vrIntermediateColorIn[1] || !vrIntermediateColorIn[1]->resource || !vrIntermediateColorIn[1]->uav ||
		(copyDepthInput && (!vrIntermediateDepth[0] || !vrIntermediateDepth[0]->resource ||
							   !vrIntermediateDepth[1] || !vrIntermediateDepth[1]->resource)) ||
		(copyAuxiliaryInputs &&
			(!vrIntermediateMotionVectors[0] || !vrIntermediateMotionVectors[0]->resource ||
				!vrIntermediateMotionVectors[1] || !vrIntermediateMotionVectors[1]->resource ||
				!vrIntermediateReactiveMask[0] || !vrIntermediateReactiveMask[0]->resource ||
				!vrIntermediateReactiveMask[1] || !vrIntermediateReactiveMask[1]->resource ||
				!vrIntermediateTransparencyMask[0] || !vrIntermediateTransparencyMask[0]->resource ||
				!vrIntermediateTransparencyMask[1] || !vrIntermediateTransparencyMask[1]->resource))) {
		return false;
	}

	// Extract both eyes' required inputs from combined stereo buffers.
	// Reactive / transparency / encoded motion vectors can be pre-generated directly per-eye by the encode pass.
	for (uint32_t i = 0; i < 2; ++i) {
		uint32_t offsetXIn = (i == 1) ? eyeWidthIn : 0;
		D3D11_BOX srcBox = { offsetXIn, 0, 0, offsetXIn + eyeWidthIn, eyeHeightIn, 1 };

		context->CopySubresourceRegion(vrIntermediateColorIn[i]->resource.get(), 0, 0, 0, 0, colorSrc, 0, &srcBox);
		if (copyDepthInput)
			context->CopySubresourceRegion(vrIntermediateDepth[i]->resource.get(), 0, 0, 0, 0, depthSrc, 0, &srcBox);
		if (copyAuxiliaryInputs) {
			context->CopySubresourceRegion(vrIntermediateMotionVectors[i]->resource.get(), 0, 0, 0, 0, mvecSrc, 0, &srcBox);
			context->CopySubresourceRegion(vrIntermediateTransparencyMask[i]->resource.get(), 0, 0, 0, 0, transparencySrc, 0, &srcBox);
			context->CopySubresourceRegion(vrIntermediateReactiveMask[i]->resource.get(), 0, 0, 0, 0, reactiveSrc, 0, &srcBox);
		}
	}

	// Zero color in the HMD hidden area, including a tiny mask-edge expansion,
	// in each per-eye buffer before temporal reuse.
	// Bind CS/SRV/CB once for both eyes to reduce per-frame CPU overhead.
	const bool clearPerEyeInputHMDMask = ShouldClearHMDMaskInPhase(HMDMaskClearPhase::PerEyeInput);
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	if (clearPerEyeInputHMDMask)
		(void)EnsureHMDMaskClearResources();

	if (clearPerEyeInputHMDMask && depthTexture.depthSRV && vrClearHMDMaskCS && vrClearHMDMaskCB) {
		auto dispatchX = (eyeWidthIn + 7) / 8;
		auto dispatchY = (eyeHeightIn + 7) / 8;

		context->CSSetShader(vrClearHMDMaskCS.get(), nullptr, 0);

		ID3D11ShaderResourceView* srvs[1] = { depthTexture.depthSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11Buffer* cbs[1] = { vrClearHMDMaskCB.get() };
		context->CSSetConstantBuffers(0, 1, cbs);

		for (uint32_t i = 0; i < 2; ++i) {
			uint32_t depthOffset = (i == 1) ? eyeWidthIn : 0;
			uint32_t clearMaskParams[8] = {
				depthOffset,
				0,
				0,
				0,
				eyeWidthIn,
				eyeHeightIn,
				eyeWidthIn,
				eyeHeightIn
			};
			context->UpdateSubresource(vrClearHMDMaskCB.get(), 0, nullptr, clearMaskParams, 0, 0);

			ID3D11UnorderedAccessView* uavs[1] = { vrIntermediateColorIn[i]->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	return true;
}

bool Upscaling::AreVRPerEyeUpscalingResourcesReady(bool requireDepth, bool requireLinearDepth) const
{
	for (uint32_t eye = 0; eye < 2; ++eye) {
		if (!vrIntermediateColorIn[eye] || !vrIntermediateColorIn[eye]->resource ||
			!vrIntermediateColorIn[eye]->uav ||
			!vrIntermediateColorOut[eye] || !vrIntermediateColorOut[eye]->resource ||
			!vrIntermediateColorOut[eye]->uav ||
			!vrIntermediateMotionVectors[eye] || !vrIntermediateMotionVectors[eye]->resource ||
			!vrIntermediateReactiveMask[eye] || !vrIntermediateReactiveMask[eye]->resource ||
			!vrIntermediateTransparencyMask[eye] || !vrIntermediateTransparencyMask[eye]->resource) {
			return false;
		}
		if (requireDepth && (!vrIntermediateDepth[eye] || !vrIntermediateDepth[eye]->resource)) {
			return false;
		}
		if (requireLinearDepth && (!vrIntermediateLinearDepth[eye] || !vrIntermediateLinearDepth[eye]->resource)) {
			return false;
		}
	}

	return true;
}

void Upscaling::FinalizePerEyeOutputs(ID3D11Resource* colorDst)
{
	ZoneScoped;

	if (!globals::game::isVR)
		return;

	auto state = globals::state;
	auto context = globals::d3d::context;
	if (!state || !context || !colorDst)
		return;

	TracyD3D11Zone(state->tracyCtx, "VR Upscaling - Finalize Per Eye");

	const bool frameAnnotations = state->frameAnnotations;
	if (frameAnnotations)
		state->BeginPerfEvent("VR Upscaling Finalize");
	auto perfEventGuard = ScopeExit([&]() {
		if (frameAnnotations)
			state->EndPerfEvent();
	});

	auto screenSize = runtimeResolutionPlan.finalOutputSize;
	auto renderSize = runtimeResolutionPlan.engineRenderSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f)
		screenSize = state->screenSize;
	if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
		renderSize = Util::ConvertToDynamic(screenSize);

	uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
	uint32_t eyeHeightOut = (uint32_t)screenSize.y;
	uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
	uint32_t eyeHeightIn = (uint32_t)renderSize.y;
	const auto inputStereoLayout = ResolveVRSideBySideStereoLayout(eyeWidthIn, eyeHeightIn);
	const auto outputStereoLayout = ResolveVRSideBySideStereoLayout(eyeWidthOut, eyeHeightOut);
	if (!inputStereoLayout.IsValid() || !outputStereoLayout.IsValid())
		return;

	D3D11_TEXTURE2D_DESC dstDesc{};
	if (TryGetTexture2DDesc(colorDst, dstDesc) &&
		(dstDesc.Width < outputStereoLayout.width || dstDesc.Height < outputStereoLayout.height)) {
		static bool loggedPerfModeDstTooSmall = false;
		if (!loggedPerfModeDstTooSmall) {
			logger::warn(
				"[Upscaling] Skipping VR per-eye finalize because destination {}x{} is smaller than runtime output {}x{}.",
				dstDesc.Width,
				dstDesc.Height,
				outputStereoLayout.width,
				outputStereoLayout.height);
			loggedPerfModeDstTooSmall = true;
		}
		return;
	}

	// Final display-color scrub only. Periphery TAA history, velocity, and lock
	// resources are left untouched so the temporal path remains active.
	auto renderer = globals::game::renderer;
	if (renderer) {
		auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		if (depthTexture.depthSRV) {
			for (uint32_t i = 0; i < 2; ++i) {
				if (!vrIntermediateColorOut[i] || !vrIntermediateColorOut[i]->uav)
					continue;

				ClearHMDMaskForEye(
					HMDMaskClearPhase::PerEyeOutput,
					vrIntermediateColorOut[i]->uav.get(),
					depthTexture.depthSRV,
					eyeWidthIn,
					eyeHeightIn,
					eyeWidthOut,
					eyeHeightOut,
					inputStereoLayout.eyes[i].minX,
					0u);
			}
		}
	}

	// Write upscaled outputs back
	if (!vrIntermediateColorOut[0] || !vrIntermediateColorOut[0]->resource ||
		!vrIntermediateColorOut[1] || !vrIntermediateColorOut[1]->resource) {
		return;
	}
	for (uint32_t i = 0; i < 2; ++i) {
		const auto& outputEyeRegion = outputStereoLayout.eyes[i];
		D3D11_BOX outBox = { 0, 0, 0, outputEyeRegion.width, outputEyeRegion.height, 1 };
		context->CopySubresourceRegion(colorDst, 0, outputEyeRegion.minX, 0, 0, vrIntermediateColorOut[i]->resource.get(), 0, &outBox);
	}
}

bool Upscaling::BlitVRRenderScaleDesktopMirror(ID3D11Texture2D* a_targetTexture, const D3D11_TEXTURE2D_DESC& a_targetDesc, uint32_t a_eyeWidth, uint32_t a_eyeHeight)
{
	if (!a_targetTexture || a_targetDesc.Width < 2 || a_targetDesc.Height == 0 || !a_eyeWidth || !a_eyeHeight ||
		a_targetDesc.ArraySize != 1 || a_targetDesc.SampleDesc.Count != 1) {
		return false;
	}
	if (!vrIntermediateColorOut[0] || !vrIntermediateColorOut[1] ||
		!vrIntermediateColorOut[0]->srv || !vrIntermediateColorOut[1]->srv) {
		return false;
	}
	if (vrIntermediateColorOut[0]->desc.Width < a_eyeWidth ||
		vrIntermediateColorOut[0]->desc.Height < a_eyeHeight ||
		vrIntermediateColorOut[1]->desc.Width < a_eyeWidth ||
		vrIntermediateColorOut[1]->desc.Height < a_eyeHeight) {
		return false;
	}
	if ((a_targetDesc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
		return false;
	}

	auto* context = globals::d3d::context;
	auto* device = globals::d3d::device;
	auto* deferred = globals::deferred;
	if (!context || !device || !deferred || !deferred->linearSampler || !upscaleRasterizerState || !upscaleBlendState) {
		return false;
	}
	const DXGI_FORMAT targetRTVFormat = GetRenderTargetViewFormat(a_targetDesc.Format);
	if (!SupportsRenderTargetView(device, targetRTVFormat)) {
		return false;
	}
	if (GetRenderTargetViewFormat(vrIntermediateColorOut[0]->desc.Format) != targetRTVFormat ||
		GetRenderTargetViewFormat(vrIntermediateColorOut[1]->desc.Format) != targetRTVFormat) {
		return false;
	}

	ID3D11PixelShader* pixelShader = nullptr;
	try {
		pixelShader = GetVRDesktopMirrorBlitPS();
	} catch (const std::exception& e) {
		static bool loggedShaderFailure = false;
		LogWarnOnce(loggedShaderFailure, "[Upscaling] Desktop mirror fallback shader unavailable; leaving mirror unchanged", e);
		if (MarkSubmitStageDeviceLostIfNeeded(e, "desktop mirror fallback shader creation")) {
			return false;
		}
	} catch (...) {
		static bool loggedShaderFailure = false;
		LogWarnOnce(loggedShaderFailure, "[Upscaling] Desktop mirror fallback shader unavailable; leaving mirror unchanged");
		if (MarkSubmitStageDeviceLostIfDeviceRemoved("desktop mirror fallback shader creation")) {
			return false;
		}
	}
	if (!pixelShader) {
		return false;
	}

	if (vrDesktopMirrorBlitTarget != a_targetTexture || !vrDesktopMirrorBlitRTV) {
		vrDesktopMirrorBlitRTV = nullptr;
		vrDesktopMirrorBlitTarget = nullptr;

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = targetRTVFormat;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		try {
			DX::ThrowIfFailed(device->CreateRenderTargetView(a_targetTexture, &rtvDesc, vrDesktopMirrorBlitRTV.put()));
			vrDesktopMirrorBlitTarget = a_targetTexture;
		} catch (const std::exception& e) {
			static bool loggedRTVFailure = false;
			LogWarnOnce(loggedRTVFailure, "[Upscaling] Desktop mirror fallback RTV creation failed; leaving mirror unchanged", e);
			if (MarkSubmitStageDeviceLostIfNeeded(e, "desktop mirror fallback RTV creation")) {
				return false;
			}
		} catch (...) {
			static bool loggedRTVFailure = false;
			LogWarnOnce(loggedRTVFailure, "[Upscaling] Desktop mirror fallback RTV creation failed; leaving mirror unchanged");
			if (MarkSubmitStageDeviceLostIfDeviceRemoved("desktop mirror fallback RTV creation")) {
				return false;
			}
		}
	}
	if (!vrDesktopMirrorBlitRTV) {
		return false;
	}

	ID3D11VertexShader* previousVS = nullptr;
	ID3D11PixelShader* previousPS = nullptr;
	ID3D11HullShader* previousHS = nullptr;
	ID3D11DomainShader* previousDS = nullptr;
	ID3D11GeometryShader* previousGS = nullptr;
	ID3D11ShaderResourceView* previousSRV = nullptr;
	ID3D11SamplerState* previousSampler = nullptr;
	std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> previousRTVs{};
	ID3D11DepthStencilView* previousDSV = nullptr;
	ID3D11BlendState* previousBlendState = nullptr;
	FLOAT previousBlendFactor[4] = {};
	UINT previousSampleMask = 0;
	ID3D11DepthStencilState* previousDepthStencilState = nullptr;
	UINT previousStencilRef = 0;
	ID3D11RasterizerState* previousRasterizerState = nullptr;
	std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> previousViewports{};
	UINT previousViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> previousScissors{};
	UINT previousScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	D3D11_PRIMITIVE_TOPOLOGY previousTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	ID3D11InputLayout* previousInputLayout = nullptr;

	context->VSGetShader(&previousVS, nullptr, nullptr);
	context->PSGetShader(&previousPS, nullptr, nullptr);
	context->HSGetShader(&previousHS, nullptr, nullptr);
	context->DSGetShader(&previousDS, nullptr, nullptr);
	context->GSGetShader(&previousGS, nullptr, nullptr);
	context->PSGetShaderResources(0, 1, &previousSRV);
	context->PSGetSamplers(0, 1, &previousSampler);
	context->OMGetRenderTargets(static_cast<UINT>(previousRTVs.size()), previousRTVs.data(), &previousDSV);
	context->OMGetBlendState(&previousBlendState, previousBlendFactor, &previousSampleMask);
	context->OMGetDepthStencilState(&previousDepthStencilState, &previousStencilRef);
	context->RSGetState(&previousRasterizerState);
	context->RSGetViewports(&previousViewportCount, previousViewports.data());
	context->RSGetScissorRects(&previousScissorCount, previousScissors.data());
	context->IAGetPrimitiveTopology(&previousTopology);
	context->IAGetInputLayout(&previousInputLayout);

	auto restoreState = ScopeExit([&]() {
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11SamplerState* nullSampler = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
		context->PSSetSamplers(0, 1, &nullSampler);

		context->VSSetShader(previousVS, nullptr, 0);
		context->PSSetShader(previousPS, nullptr, 0);
		context->HSSetShader(previousHS, nullptr, 0);
		context->DSSetShader(previousDS, nullptr, 0);
		context->GSSetShader(previousGS, nullptr, 0);
		context->OMSetRenderTargets(static_cast<UINT>(previousRTVs.size()), previousRTVs.data(), previousDSV);
		context->PSSetShaderResources(0, 1, &previousSRV);
		context->PSSetSamplers(0, 1, &previousSampler);
		context->OMSetBlendState(previousBlendState, previousBlendFactor, previousSampleMask);
		context->OMSetDepthStencilState(previousDepthStencilState, previousStencilRef);
		context->RSSetState(previousRasterizerState);
		context->RSSetViewports(previousViewportCount, previousViewportCount ? previousViewports.data() : nullptr);
		context->RSSetScissorRects(previousScissorCount, previousScissorCount ? previousScissors.data() : nullptr);
		context->IASetPrimitiveTopology(previousTopology);
		context->IASetInputLayout(previousInputLayout);

		if (previousVS)
			previousVS->Release();
		if (previousPS)
			previousPS->Release();
		if (previousHS)
			previousHS->Release();
		if (previousDS)
			previousDS->Release();
		if (previousGS)
			previousGS->Release();
		if (previousSRV)
			previousSRV->Release();
		if (previousSampler)
			previousSampler->Release();
		for (auto* rtv : previousRTVs) {
			if (rtv)
				rtv->Release();
		}
		if (previousDSV)
			previousDSV->Release();
		if (previousBlendState)
			previousBlendState->Release();
		if (previousDepthStencilState)
			previousDepthStencilState->Release();
		if (previousRasterizerState)
			previousRasterizerState->Release();
		if (previousInputLayout)
			previousInputLayout->Release();
	});

	try {
		auto* vertexShader = GetUpscaleVS();
		if (!vertexShader) {
			return false;
		}

		ID3D11RenderTargetView* targetRTV = vrDesktopMirrorBlitRTV.get();
		ID3D11SamplerState* sampler = deferred->linearSampler;
		const float blendFactor[4] = {};
		context->OMSetRenderTargets(1, &targetRTV, nullptr);
		context->OMSetBlendState(upscaleBlendState.get(), blendFactor, 0xFFFFFFFF);
		context->OMSetDepthStencilState(nullptr, 0);
		context->RSSetState(upscaleRasterizerState.get());
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);
		context->PSSetShader(pixelShader, nullptr, 0);
		context->PSSetSamplers(0, 1, &sampler);

		const uint32_t leftWidth = a_targetDesc.Width / 2u;
		const uint32_t rightWidth = a_targetDesc.Width - leftWidth;
		for (uint32_t eye = 0; eye < 2; ++eye) {
			const uint32_t eyeOffsetX = eye == 0 ? 0u : leftWidth;
			const uint32_t eyeWidth = eye == 0 ? leftWidth : rightWidth;
			if (!eyeWidth) {
				continue;
			}

			D3D11_VIEWPORT viewport{};
			viewport.TopLeftX = static_cast<float>(eyeOffsetX);
			viewport.TopLeftY = 0.0f;
			viewport.Width = static_cast<float>(eyeWidth);
			viewport.Height = static_cast<float>(a_targetDesc.Height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;

			ID3D11ShaderResourceView* sourceSRV = vrIntermediateColorOut[eye]->srv.get();
			context->RSSetViewports(1, &viewport);
			context->PSSetShaderResources(0, 1, &sourceSRV);
			context->Draw(3, 0);
		}
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
	} catch (const std::exception& e) {
		static bool loggedBlitFailure = false;
		LogWarnOnce(loggedBlitFailure, "[Upscaling] Desktop mirror fallback blit failed; leaving mirror unchanged", e);
		if (MarkSubmitStageDeviceLostIfNeeded(e, "desktop mirror fallback blit")) {
			return false;
		}
		return false;
	} catch (...) {
		static bool loggedBlitFailure = false;
		LogWarnOnce(loggedBlitFailure, "[Upscaling] Desktop mirror fallback blit failed; leaving mirror unchanged");
		if (MarkSubmitStageDeviceLostIfDeviceRemoved("desktop mirror fallback blit")) {
			return false;
		}
		return false;
	}

	if (MarkSubmitStageDeviceLostIfDeviceRemoved("desktop mirror fallback blit")) {
		return false;
	}

	return true;
}

void Upscaling::ClearVRDirectUpscaledEyeOutput(uint32_t eyeIndex, ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
	uint32_t depthWidthPerEye, uint32_t depthHeight, uint32_t colorWidthPerEye, uint32_t colorHeight, uint32_t colorOffsetX)
{
	if (!globals::game::isVR || eyeIndex >= 2 || !colorUAV || !depthSRV)
		return;
	if (!depthWidthPerEye || !depthHeight || !colorWidthPerEye || !colorHeight)
		return;

	ClearHMDMaskForEye(
		HMDMaskClearPhase::PerEyeOutput,
		colorUAV,
		depthSRV,
		depthWidthPerEye,
		depthHeight,
		colorWidthPerEye,
		colorHeight,
		eyeIndex * depthWidthPerEye,
		colorOffsetX);
}

bool Upscaling::EncodeSubmitStageVRInputs(ID3D11Resource* colorSource, ID3D11Resource* motionVectors, ID3D11Resource* depthSource,
	uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, bool copyDepthInput, bool allowFoveatedRegionEncode, bool* encodedFoveatedRegions)
{
	if (encodedFoveatedRegions)
		*encodedFoveatedRegions = false;
	if (!globals::game::isVR || !colorSource || !motionVectors || !depthSource || !inputWidthPerEye || !inputHeight || !outputWidthPerEye || !outputHeight)
		return false;
	const auto inputStereoLayout = ResolveVRSideBySideStereoLayout(inputWidthPerEye, inputHeight);
	if (!inputStereoLayout.IsValid())
		return false;

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	if (!state || !renderer || !context || !globals::deferred || !upscalingDataCB || !reactiveMaskTexture || !transparencyCompositionMaskTexture)
		return false;

	if (!reactiveMaskTexture->resource || !reactiveMaskTexture->uav ||
		!transparencyCompositionMaskTexture->resource || !transparencyCompositionMaskTexture->uav)
		return false;

	auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
	auto& normals = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[2]];
	auto& sourceMotionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	const auto upscaleMethod = GetRuntimeUpscaleMethod();
	ID3D11ComputeShader* encodeShader = nullptr;
	static bool loggedEncodeShaderFailure = false;
	try {
		encodeShader = GetEncodeTexturesCS();
	} catch (const std::exception& e) {
		LogWarnOnce(
			loggedEncodeShaderFailure,
			"[Upscaling] Submit-stage vendor upscaling failed to create encode shader",
			e);
		MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage encode shader creation");
		return false;
	} catch (...) {
		LogWarnOnce(
			loggedEncodeShaderFailure,
			"[Upscaling] Submit-stage vendor upscaling failed to create encode shader");
		MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage encode shader creation");
		return false;
	}
	if (!temporalAAMask.SRV || !normals.SRV || !sourceMotionVector.SRV || !depth.depthSRV || !encodeShader)
		return false;

	try {
		EnsureVRIntermediateTextures(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight,
			colorSource, motionVectors, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get());
	} catch (const std::exception& e) {
		logger::warn("[Upscaling] Submit-stage vendor upscaling failed to create intermediates: {}", e.what());
		MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage intermediate creation");
		return false;
	} catch (...) {
		logger::warn("[Upscaling] Submit-stage vendor upscaling failed to create intermediates.");
		MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage intermediate creation");
		return false;
	}

	for (uint32_t eye = 0; eye < 2; ++eye) {
		if ((copyDepthInput && (!vrIntermediateDepth[eye] || !vrIntermediateDepth[eye]->resource)) ||
			(upscaleMethod == UpscaleMethod::kFSR && (!vrIntermediateLinearDepth[eye] || !vrIntermediateLinearDepth[eye]->resource || !vrIntermediateLinearDepth[eye]->uav)) ||
			!vrIntermediateMotionVectors[eye] || !vrIntermediateMotionVectors[eye]->uav ||
			!vrIntermediateReactiveMask[eye] || !vrIntermediateReactiveMask[eye]->uav ||
			!vrIntermediateTransparencyMask[eye] || !vrIntermediateTransparencyMask[eye]->uav) {
			return false;
		}
	}

	const bool annotateEncode = state->frameAnnotations;
	if (annotateEncode)
		state->BeginPerfEvent("VR Render Scale Mode Encode Inputs");
	auto encodePerfEvent = ScopeExit([&]() {
		if (annotateEncode)
			state->EndPerfEvent();
	});

	static bool loggedEncodeDispatchFailure = false;
	try {
		ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, sourceMotionVector.SRV, depth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		auto upscalingBuffer = upscalingDataCB->CB();
		context->CSSetConstantBuffers(0, 1, &upscalingBuffer);
		context->CSSetShader(encodeShader, nullptr, 0);

		const float2 renderSize = { static_cast<float>(inputStereoLayout.width), static_cast<float>(inputStereoLayout.height) };

		auto dispatchEyeEncode = [&](uint32_t eye, uint32_t inputMinX, uint32_t inputMinY, uint32_t inputMaxX, uint32_t inputMaxY) -> bool {
			if (eye >= 2 || inputMaxX <= inputMinX || inputMaxY <= inputMinY)
				return false;
			const auto& sourceEyeRegion = inputStereoLayout.eyes[eye];

			inputMinX = std::min(inputMinX, inputWidthPerEye);
			inputMinY = std::min(inputMinY, inputHeight);
			inputMaxX = std::min(inputMaxX, inputWidthPerEye);
			inputMaxY = std::min(inputMaxY, inputHeight);
			if (inputMaxX <= inputMinX || inputMaxY <= inputMinY)
				return false;

			const uint32_t dispatchWidth = inputMaxX - inputMinX;
			const uint32_t dispatchHeight = inputMaxY - inputMinY;
			UpscalingDataCB upscalingData{};
			upscalingData.dispatchDim = { static_cast<float>(dispatchWidth), static_cast<float>(dispatchHeight) };
			upscalingData.trueSamplingDim = renderSize;
			upscalingData.invTrueSamplingDim = { renderSize.x > 0.0f ? 1.0f / renderSize.x : 0.0f, renderSize.y > 0.0f ? 1.0f / renderSize.y : 0.0f };
			upscalingData.seamCenterX = renderSize.x * 0.5f;
			upscalingData.seamHalfWidthPx = 2.0f;
			upscalingData.maskDepthThreshold = 1e-6f;
			upscalingData.vrSeamHardening = 1.0f;
			upscalingData.sourceOffset = { static_cast<float>(sourceEyeRegion.minX + inputMinX), static_cast<float>(inputMinY) };
			upscalingData.outputOffset = { static_cast<float>(inputMinX), static_cast<float>(inputMinY) };
			upscalingDataCB->Update(upscalingData);

			ID3D11UnorderedAccessView* uavs[4] = {
				vrIntermediateReactiveMask[eye]->uav.get(),
				vrIntermediateTransparencyMask[eye]->uav.get(),
				vrIntermediateMotionVectors[eye]->uav.get(),
				upscaleMethod == UpscaleMethod::kFSR ? vrIntermediateLinearDepth[eye]->uav.get() : nullptr
			};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
			context->Dispatch((dispatchWidth + 7u) >> 3, (dispatchHeight + 7u) >> 3, 1);

			if (copyDepthInput) {
				D3D11_BOX srcBox{
					sourceEyeRegion.minX + inputMinX,
					sourceEyeRegion.minY + inputMinY,
					0,
					sourceEyeRegion.minX + inputMaxX,
					sourceEyeRegion.minY + inputMaxY,
					1
				};
				context->CopySubresourceRegion(vrIntermediateDepth[eye]->resource.get(), 0, inputMinX, inputMinY, 0, depthSource, 0, &srcBox);
			}
			return true;
		};

		auto dispatchFullEyes = [&]() -> bool {
			bool allDispatched = true;
			for (uint32_t eye = 0; eye < 2; ++eye)
				allDispatched = dispatchEyeEncode(eye, 0, 0, inputWidthPerEye, inputHeight) && allDispatched;
			return allDispatched;
		};

		bool dispatchedRegionEncode = false;
		if (allowFoveatedRegionEncode) {
			const bool usePeripheryTAAProfile = IsPeripheryTAAEnabled(upscaleMethod);
			const bool usePeripheryTAAPath = IsPeripheryTAAPathActive(upscaleMethod);
			std::array<FoveatedEncodeRegion, 2> regions{};
			if (GetFoveatedEncodeRegions(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight, usePeripheryTAAProfile, usePeripheryTAAPath, regions)) {
				bool allDispatched = true;
				for (uint32_t eye = 0; eye < 2; ++eye) {
					allDispatched = dispatchEyeEncode(eye, regions[eye].minX, regions[eye].minY, regions[eye].maxX, regions[eye].maxY) && allDispatched;
				}
				dispatchedRegionEncode = allDispatched;
				if (encodedFoveatedRegions)
					*encodedFoveatedRegions = dispatchedRegionEncode;
			}
		}

		const bool encodeSucceeded = dispatchedRegionEncode || dispatchFullEyes();

		ID3D11ShaderResourceView* nullSRV[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullSRV), nullSRV);

		ID3D11UnorderedAccessView* nullUAV[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAV), nullUAV, nullptr);

		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);
		context->CSSetShader(nullptr, nullptr, 0);

		if (!encodeSucceeded)
			return false;
	} catch (const std::exception& e) {
		UnbindUpscalingResources();
		LogWarnOnce(
			loggedEncodeDispatchFailure,
			"[Upscaling] Submit-stage vendor input encode threw; using vanilla presentation for this frame",
			e);
		MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage input encode");
		return false;
	} catch (...) {
		UnbindUpscalingResources();
		LogWarnOnce(
			loggedEncodeDispatchFailure,
			"[Upscaling] Submit-stage vendor input encode threw; using vanilla presentation for this frame");
		MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage input encode");
		return false;
	}

	if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage input encode"))
		return false;

	return true;
}

bool Upscaling::StretchSubmitStageEyeOutput(uint32_t eyeIndex, uint32_t inputWidth, uint32_t inputHeight, uint32_t outputWidth, uint32_t outputHeight)
{
	if (eyeIndex >= 2 || !inputWidth || !inputHeight || !outputWidth || !outputHeight)
		return false;
	if (IsSubmitStageDeviceLost())
		return false;

	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!context || !deferred || !deferred->linearSampler || !dynamicResolutionStretchCB)
		return false;

	if (!vrIntermediateColorIn[eyeIndex] || !vrIntermediateColorIn[eyeIndex]->resource || !vrIntermediateColorIn[eyeIndex]->srv ||
		!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->resource || !vrIntermediateColorOut[eyeIndex]->uav)
		return false;

	ID3D11ComputeShader* stretchCS = nullptr;
	static bool loggedStretchShaderFailure = false;
	try {
		stretchCS = GetSubmitStageStretchCS();
	} catch (const std::exception& e) {
		LogWarnOnce(
			loggedStretchShaderFailure,
			"[Upscaling] Submit-stage fallback shader unavailable; using emergency copy fallback",
			e);
		if (MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage stretch shader creation"))
			return false;
	} catch (...) {
		LogWarnOnce(
			loggedStretchShaderFailure,
			"[Upscaling] Submit-stage fallback shader unavailable; using emergency copy fallback");
		if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage stretch shader creation"))
			return false;
	}
	if (!stretchCS) {
		float clearColor[4] = {};
		context->ClearUnorderedAccessViewFloat(vrIntermediateColorOut[eyeIndex]->uav.get(), clearColor);

		D3D11_TEXTURE2D_DESC inputDesc{};
		D3D11_TEXTURE2D_DESC outputDesc{};
		if (TryGetTexture2DDesc(vrIntermediateColorIn[eyeIndex]->resource.get(), inputDesc) &&
			TryGetTexture2DDesc(vrIntermediateColorOut[eyeIndex]->resource.get(), outputDesc) &&
			inputDesc.Format == outputDesc.Format) {
			D3D11_BOX copyBox{
				0,
				0,
				0,
				std::min(inputWidth, outputWidth),
				std::min(inputHeight, outputHeight),
				1
			};
			context->CopySubresourceRegion(vrIntermediateColorOut[eyeIndex]->resource.get(), 0, 0, 0, 0,
				vrIntermediateColorIn[eyeIndex]->resource.get(), 0, &copyBox);
		}

		static bool loggedEmergencyFallback[2] = {};
		if (!loggedEmergencyFallback[eyeIndex]) {
			logger::warn(
				"[Upscaling] Submit-stage fallback shader unavailable for eye {}; returning a full-size emergency fallback texture.",
				eyeIndex);
			loggedEmergencyFallback[eyeIndex] = true;
		}
		return true;
	}

	ID3D11ComputeShader* previousCS = nullptr;
	ID3D11ShaderResourceView* previousSRV = nullptr;
	ID3D11UnorderedAccessView* previousUAV = nullptr;
	ID3D11Buffer* previousCB = nullptr;
	ID3D11SamplerState* previousSampler = nullptr;

	context->CSGetShader(&previousCS, nullptr, nullptr);
	context->CSGetShaderResources(0, 1, &previousSRV);
	context->CSGetUnorderedAccessViews(0, 1, &previousUAV);
	context->CSGetConstantBuffers(0, 1, &previousCB);
	context->CSGetSamplers(0, 1, &previousSampler);

	auto restoreStretchState = ScopeExit([&]() {
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		ID3D11SamplerState* nullSampler[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetSamplers(0, 1, nullSampler);

		context->CSSetShader(previousCS, nullptr, 0);
		context->CSSetShaderResources(0, 1, &previousSRV);
		context->CSSetUnorderedAccessViews(0, 1, &previousUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, &previousCB);
		context->CSSetSamplers(0, 1, &previousSampler);

		if (previousCS)
			previousCS->Release();
		if (previousSRV)
			previousSRV->Release();
		if (previousUAV)
			previousUAV->Release();
		if (previousCB)
			previousCB->Release();
		if (previousSampler)
			previousSampler->Release();
	});

	static bool loggedStretchDispatchFailure = false;
	try {
		DynamicResolutionStretchCB stretchData{};
		stretchData.inputSize = { static_cast<float>(inputWidth), static_cast<float>(inputHeight) };
		stretchData.outputSize = { static_cast<float>(outputWidth), static_cast<float>(outputHeight) };
		stretchData.sourceTextureSize = {
			static_cast<float>(vrIntermediateColorIn[eyeIndex]->desc.Width),
			static_cast<float>(vrIntermediateColorIn[eyeIndex]->desc.Height)
		};
		dynamicResolutionStretchCB->Update(stretchData);

		ID3D11ShaderResourceView* sourceSRV = vrIntermediateColorIn[eyeIndex]->srv.get();
		ID3D11UnorderedAccessView* outputUAV = vrIntermediateColorOut[eyeIndex]->uav.get();
		ID3D11Buffer* stretchBuffer = dynamicResolutionStretchCB->CB();
		ID3D11SamplerState* sampler = deferred->linearSampler;

		context->CSSetShader(stretchCS, nullptr, 0);
		context->CSSetShaderResources(0, 1, &sourceSRV);
		context->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, &stretchBuffer);
		context->CSSetSamplers(0, 1, &sampler);

		auto state = globals::state;
		bool perfEventActive = false;
		if (state && state->frameAnnotations) {
			state->BeginPerfEvent("VR Render Scale Mode Stretch Fallback");
			perfEventActive = true;
		}
		auto perfEventGuard = ScopeExit([&]() {
			if (perfEventActive && state && state->frameAnnotations)
				state->EndPerfEvent();
		});
		context->Dispatch((outputWidth + 7u) >> 3, (outputHeight + 7u) >> 3, 1);
		perfEventActive = false;
		if (state && state->frameAnnotations)
			state->EndPerfEvent();
	} catch (const std::exception& e) {
		LogWarnOnce(
			loggedStretchDispatchFailure,
			"[Upscaling] Submit-stage stretch fallback threw; using vanilla presentation for this frame",
			e);
		MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage stretch fallback");
		return false;
	} catch (...) {
		LogWarnOnce(
			loggedStretchDispatchFailure,
			"[Upscaling] Submit-stage stretch fallback threw; using vanilla presentation for this frame");
		MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage stretch fallback");
		return false;
	}

	return true;
}

bool Upscaling::EnsureHMDMaskClearResources()
{
	if (!globals::game::isVR)
		return false;
	if (vrClearHMDMaskCS && vrClearHMDMaskCB)
		return true;

	auto device = globals::d3d::device;
	if (!device)
		return false;

	static bool loggedHMDMaskClearFailure = false;
	try {
		if (!vrClearHMDMaskCS) {
			vrClearHMDMaskCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/ClearHMDMaskCS.hlsl", {}, "cs_5_0"));
		}

		if (!vrClearHMDMaskCB) {
			D3D11_BUFFER_DESC cbDesc = {};
			cbDesc.ByteWidth = 32;  // 8 uints
			cbDesc.Usage = D3D11_USAGE_DEFAULT;
			cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cbDesc.CPUAccessFlags = 0;
			DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, nullptr, vrClearHMDMaskCB.put()));
		}
	} catch (const std::exception& e) {
		LogWarnOnce(
			loggedHMDMaskClearFailure,
			"[Upscaling] HMD mask clear resources unavailable; hidden-area clear will be skipped",
			e);
		MarkSubmitStageDeviceLostIfNeeded(e, "HMD mask clear resource creation");
		vrClearHMDMaskCS = nullptr;
		vrClearHMDMaskCB = nullptr;
		return false;
	} catch (...) {
		LogWarnOnce(
			loggedHMDMaskClearFailure,
			"[Upscaling] HMD mask clear resources unavailable; hidden-area clear will be skipped");
		MarkSubmitStageDeviceLostIfDeviceRemoved("HMD mask clear resource creation");
		vrClearHMDMaskCS = nullptr;
		vrClearHMDMaskCB = nullptr;
		return false;
	}

	return vrClearHMDMaskCS && vrClearHMDMaskCB;
}

bool Upscaling::ShouldClearHMDMaskInPhase(Upscaling::HMDMaskClearPhase a_phase) const
{
	if (!globals::game::isVR)
		return false;
	if (ShouldDeferHMDClearMask())
		return false;

	// The normal VR vendor path and the render-scale submit path are mutually exclusive.
	// Keep HMD clears on the path that currently owns the eye outputs.
	bool submitStagePhase = false;
	switch (a_phase) {
	case HMDMaskClearPhase::PerEyeInput:
	case HMDMaskClearPhase::PerEyeOutput:
		break;
	case HMDMaskClearPhase::SubmitStageOutput:
	case HMDMaskClearPhase::SubmitStageFoveatedOutput:
		submitStagePhase = true;
		break;
	default:
		return false;
	}

	if (!IsVendorUpscalingMethod(GetRuntimeUpscaleMethod()))
		return false;

	const bool presentationUpscalingActive = IsPresentationUpscalingActive();
	if (!submitStagePhase)
		return !presentationUpscalingActive;

	return presentationUpscalingActive &&
	       runtimeResolutionPlan.owner == ResolutionOwner::VRRenderScaleMode;
}

void Upscaling::ClearHMDMask(ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
	uint32_t depthWidth, uint32_t depthHeight, uint32_t colorWidth, uint32_t colorHeight, uint32_t depthOffsetX, uint32_t colorOffsetX, uint32_t depthOffsetY, uint32_t colorOffsetY)
{
	if (!globals::game::isVR)
		return;
	if (!colorUAV || !depthSRV || !depthWidth || !depthHeight || !colorWidth || !colorHeight)
		return;
	// During save/load, the depth feed can be transiently mismatched with the current eye target.
	// Running HAM clear in this window can briefly project a rectangular mask.
	if (ShouldDeferHMDClearMask())
		return;

	auto context = globals::d3d::context;
	if (!context)
		return;

	if (!EnsureHMDMaskClearResources())
		return;

	if (vrClearHMDMaskCS && vrClearHMDMaskCB) {
		auto dispatchX = (colorWidth + 7) / 8;
		auto dispatchY = (colorHeight + 7) / 8;

		context->CSSetShader(vrClearHMDMaskCS.get(), nullptr, 0);

		ID3D11ShaderResourceView* srvs[1] = { depthSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[1] = { colorUAV };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		uint32_t clearMaskParams[8] = {
			depthOffsetX,
			colorOffsetX,
			depthOffsetY,
			colorOffsetY,
			depthWidth,
			depthHeight,
			colorWidth,
			colorHeight
		};
		context->UpdateSubresource(vrClearHMDMaskCB.get(), 0, nullptr, clearMaskParams, 0, 0);

		ID3D11Buffer* cbs[1] = { vrClearHMDMaskCB.get() };
		context->CSSetConstantBuffers(0, 1, cbs);

		{
			CS_PROFILE_SCOPE("Upscaling::ClearHMDMask");
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		// Unbind
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	}
}

void Upscaling::ClearHMDMaskForEye(Upscaling::HMDMaskClearPhase a_phase, ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
	uint32_t depthWidth, uint32_t depthHeight, uint32_t colorWidth, uint32_t colorHeight, uint32_t depthOffsetX, uint32_t colorOffsetX, uint32_t depthOffsetY, uint32_t colorOffsetY)
{
	const bool shouldClear = ShouldClearHMDMaskInPhase(a_phase);
	if (!shouldClear)
		return;

	ClearHMDMask(
		colorUAV,
		depthSRV,
		depthWidth,
		depthHeight,
		colorWidth,
		colorHeight,
		depthOffsetX,
		colorOffsetX,
		depthOffsetY,
		colorOffsetY);
}

int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

// Calculate halton number for index and base.
static float Halton(int32_t index, int32_t base)
{
	float f = 1.0f, result = 0.0f;

	for (int32_t currentIndex = index; currentIndex > 0;) {
		f /= (float)base;
		result = result + f * (float)(currentIndex % base);
		currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
	}

	return result;
}

void GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
	const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
	const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

	*outX = x;
	*outY = y;
}

void UpdateCameraData();

void Upscaling::ConfigureTAA()
{
	auto upscaleMethod = GetRuntimeUpscaleMethod();

	// When no upscaling method is active, preserve vanilla TAA state.
	// UpdateJitter (called immediately after this hook) owns the non-upscaling path.
	if (upscaleMethod == UpscaleMethod::kNONE)
		return;

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

	// CS TAA replaces vanilla TAA, so disable water TAA there.
	// FSR/DLSS keep water TAA enabled.
	bool* enableWaterTAA = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(BSImagespaceShaderISTemporalAA) + 0x38LL);
	*enableWaterTAA = upscaleMethod != UpscaleMethod::kTAA;

	BSImagespaceShaderISTemporalAA->taaEnabled = true;
}

void Upscaling::ConfigureUpscaling(RE::BSGraphics::State* a_viewport)
{
	QueueVRFpsStabilizerSyncForCurrentLoadIfNeeded(*this);
	ApplyPendingVRFpsStabilizerLoadSync();
	const auto requestedUpscaleMethod = GetConfiguredUpscaleMethodForTransition();
	ApplyPendingVRUpscalingTransition(requestedUpscaleMethod);
	QueueDeferredVRRenderScaleActivationIfReady(*this);
	if (pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire)) {
		if (ApplyPendingPerfModeRenderTargetRecreate("ConfigureUpscaling"))
			return;
	}
	auto upscaleMethod = GetRuntimeUpscaleMethod();

	// Cache original TAA values for UI
	projectionPosScaleX = a_viewport->projectionPosScaleX;
	projectionPosScaleY = a_viewport->projectionPosScaleY;

	// Get full screen size
	auto state = globals::state;
	auto screenSize = state->screenSize;

	auto screenWidth = static_cast<int>(screenSize.x);
	auto screenHeight = static_cast<int>(screenSize.y);

	const bool vendorUpscalingMethod = IsVendorUpscalingMethod(upscaleMethod);
	const bool vrRenderScaleMenuPresentationContext =
		globals::game::isVR &&
		vendorUpscalingMethod &&
		IsVRRenderScaleMenuPreparationContextActive(state);
	EnsureRuntimeResolutionStateCurrent();
	auto applyFullResolutionPresentation = [&](UpscaleMethod a_upscaleMethod, const char* a_context) {
		float2 presentationDisplaySize = runtimeResolutionPlan.trueHMDDisplaySize;
		if (presentationDisplaySize.x <= 0.0f || presentationDisplaySize.y <= 0.0f)
			presentationDisplaySize = screenSize;
		PrepareFullResolutionPostProcessing(a_viewport, true);
		LogVRRenderScalePresentationPlanIfChanged(a_upscaleMethod, a_context, presentationDisplaySize, screenSize);
		EnsureResourcesCurrent(a_upscaleMethod);
	};

	if (runtimeResolutionPlan.owner == ResolutionOwner::VRRenderScaleMode) {
		if (vrRenderScaleMenuPresentationContext) {
			applyFullResolutionPresentation(runtimeResolutionPlan.upscaleMethod, "menu/full-resolution");
			return;
		}

		const int renderWidth = std::max(1, static_cast<int>(runtimeResolutionPlan.engineRenderSize.x));
		const int renderHeight = std::max(1, static_cast<int>(runtimeResolutionPlan.engineRenderSize.y));
		const int outputWidth = std::max(renderWidth, static_cast<int>(runtimeResolutionPlan.finalOutputSize.x));

		resolutionScale = { 1.0f, 1.0f };
		auto phaseCount = GetJitterPhaseCount(renderWidth, outputWidth);
		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		const float targetProjectionPosScaleX = -jitter.x / renderWidth;
		const float targetProjectionPosScaleY = 2.0f * jitter.y / renderHeight;
		const bool projectionDirty =
			a_viewport->projectionPosScaleX != targetProjectionPosScaleX ||
			a_viewport->projectionPosScaleY != targetProjectionPosScaleY;
		a_viewport->projectionPosScaleX = targetProjectionPosScaleX;
		a_viewport->projectionPosScaleY = targetProjectionPosScaleY;

		const bool dynamicResolutionDirty = ApplyLockedFullResolutionDynamicResolutionState(a_viewport);
		if (projectionDirty && !dynamicResolutionDirty)
			UpdateCameraData();

		EnsureResourcesCurrent(runtimeResolutionPlan.upscaleMethod);
		return;
	}
	if (globals::game::isVR &&
		vendorUpscalingMethod &&
		(IsVRMenuScenePresentationBlockActive() || IsVRLoadingPresentationTailActive(state))) {
		applyFullResolutionPresentation(upscaleMethod, "vr-transition/full-resolution");
		return;
	}
	if (globals::game::isVR &&
		vendorUpscalingMethod &&
		IsVRTransitionPresentationProtectionActive(*this, state) &&
		IsVRLoadingPresentationContextActive(state)) {
		applyFullResolutionPresentation(upscaleMethod, "loading/full-resolution");
		return;
	}

	bool vendorProjectionDirty = false;
	if (vendorUpscalingMethod) {
		float resolutionScaleBase = GetQualityModeResolutionScale(GetRuntimeQualityMode());

		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase);

		resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
		resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		const float targetProjectionPosScaleX =
			globals::game::isVR ?
				-jitter.x / renderWidth :
				-2.0f * jitter.x / renderWidth;
		const float targetProjectionPosScaleY = 2.0f * jitter.y / renderHeight;
		vendorProjectionDirty =
			a_viewport->projectionPosScaleX != targetProjectionPosScaleX ||
			a_viewport->projectionPosScaleY != targetProjectionPosScaleY;
		a_viewport->projectionPosScaleX = targetProjectionPosScaleX;
		a_viewport->projectionPosScaleY = targetProjectionPosScaleY;
	} else {
		resolutionScale = { 1.0f, 1.0f };

		if (globals::game::isVR)
			jitter.x = -a_viewport->projectionPosScaleX * screenWidth;
		else
			jitter.x = -a_viewport->projectionPosScaleX * screenWidth / 2.0f;

		jitter.y = a_viewport->projectionPosScaleY * screenHeight / 2.0f;
	}

	auto& runtimeData = a_viewport->GetRuntimeData();

	if (!vendorUpscalingMethod) {
		if (dynamicResolutionWidthRatio != 1.0f || dynamicResolutionHeightRatio != 1.0f) {
			if (globals::game::isVR) {
				SetDynamicResolutionEnabledForUpscaling(false);
				runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
				runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
				runtimeData.dynamicResolutionWidthRatio = 1.0f;
				runtimeData.dynamicResolutionHeightRatio = 1.0f;
				runtimeData.dynamicResolutionLock = 1;
			} else {
				runtimeData.dynamicResolutionPreviousWidthRatio = runtimeData.dynamicResolutionWidthRatio;
				runtimeData.dynamicResolutionPreviousHeightRatio = runtimeData.dynamicResolutionHeightRatio;
				runtimeData.dynamicResolutionWidthRatio = 1.0f;
				runtimeData.dynamicResolutionHeightRatio = 1.0f;
				runtimeData.dynamicResolutionLock = 1;
			}
			dynamicResolutionWidthRatio = runtimeData.dynamicResolutionWidthRatio;
			dynamicResolutionHeightRatio = runtimeData.dynamicResolutionHeightRatio;
			UpdateCameraData();
		}
		EnsureResourcesCurrent(upscaleMethod);
		return;
	}

	const bool dynamicResolutionDirty = ApplyDynamicResolutionState(a_viewport);
	if (globals::game::isVR && vendorProjectionDirty && !dynamicResolutionDirty)
		UpdateCameraData();
	EnsureResourcesCurrent(upscaleMethod);

	// Disable dynamic resolution unless the game explicitly enables it.
	if (!globals::game::isVR)
		runtimeData.dynamicResolutionLock = 1;
}

bool Upscaling::ApplyLockedFullResolutionDynamicResolutionState(RE::BSGraphics::State* a_viewport)
{
	if (!a_viewport)
		return false;

	auto& runtimeData = a_viewport->GetRuntimeData();
	bool cameraDataDirty = globals::game::isVR && SetDynamicResolutionEnabledForUpscaling(false);
	if (runtimeData.dynamicResolutionPreviousWidthRatio != 1.0f ||
		runtimeData.dynamicResolutionPreviousHeightRatio != 1.0f ||
		runtimeData.dynamicResolutionWidthRatio != 1.0f ||
		runtimeData.dynamicResolutionHeightRatio != 1.0f ||
		runtimeData.dynamicResolutionLock != 1 ||
		dynamicResolutionWidthRatio != 1.0f ||
		dynamicResolutionHeightRatio != 1.0f) {
		runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
		runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
		runtimeData.dynamicResolutionWidthRatio = 1.0f;
		runtimeData.dynamicResolutionHeightRatio = 1.0f;
		runtimeData.dynamicResolutionLock = 1;
		dynamicResolutionWidthRatio = 1.0f;
		dynamicResolutionHeightRatio = 1.0f;
		cameraDataDirty = true;
	}

	if (cameraDataDirty)
		UpdateCameraData();

	return cameraDataDirty;
}

bool Upscaling::ApplyDynamicResolutionState(RE::BSGraphics::State* a_viewport)
{
	if (!a_viewport)
		return false;

	if (IsVRRenderScaleModeLatched()) {
		return ApplyLockedFullResolutionDynamicResolutionState(a_viewport);
	}

	auto& runtimeData = a_viewport->GetRuntimeData();
	auto upscaleMethod = GetRuntimeUpscaleMethod();
	if (!IsVendorUpscalingMethod(upscaleMethod))
		return false;

	const bool shouldUnlockDynamicResolution = globals::game::isVR && ShouldUnlockDynamicResolutionForUpscaling(upscaleMethod, resolutionScale);

	if (globals::game::isVR) {
		bool cameraDataDirty = SetDynamicResolutionEnabledForUpscaling(shouldUnlockDynamicResolution);
		if (shouldUnlockDynamicResolution) {
			if (runtimeData.dynamicResolutionPreviousWidthRatio != runtimeData.dynamicResolutionWidthRatio ||
				runtimeData.dynamicResolutionPreviousHeightRatio != runtimeData.dynamicResolutionHeightRatio ||
				runtimeData.dynamicResolutionWidthRatio != resolutionScale.x ||
				runtimeData.dynamicResolutionHeightRatio != resolutionScale.y ||
				runtimeData.dynamicResolutionLock != 0 ||
				dynamicResolutionWidthRatio != resolutionScale.x ||
				dynamicResolutionHeightRatio != resolutionScale.y) {
				runtimeData.dynamicResolutionPreviousWidthRatio = runtimeData.dynamicResolutionWidthRatio;
				runtimeData.dynamicResolutionPreviousHeightRatio = runtimeData.dynamicResolutionHeightRatio;
				runtimeData.dynamicResolutionWidthRatio = resolutionScale.x;
				runtimeData.dynamicResolutionHeightRatio = resolutionScale.y;
				runtimeData.dynamicResolutionLock = 0;
				dynamicResolutionWidthRatio = resolutionScale.x;
				dynamicResolutionHeightRatio = resolutionScale.y;
				cameraDataDirty = true;
			}
		} else {
			if (runtimeData.dynamicResolutionPreviousWidthRatio != 1.0f ||
				runtimeData.dynamicResolutionPreviousHeightRatio != 1.0f ||
				runtimeData.dynamicResolutionWidthRatio != 1.0f ||
				runtimeData.dynamicResolutionHeightRatio != 1.0f ||
				runtimeData.dynamicResolutionLock != 1 ||
				dynamicResolutionWidthRatio != 1.0f ||
				dynamicResolutionHeightRatio != 1.0f) {
				runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
				runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
				runtimeData.dynamicResolutionWidthRatio = 1.0f;
				runtimeData.dynamicResolutionHeightRatio = 1.0f;
				runtimeData.dynamicResolutionLock = 1;
				dynamicResolutionWidthRatio = 1.0f;
				dynamicResolutionHeightRatio = 1.0f;
				cameraDataDirty = true;
			}
		}
		if (cameraDataDirty)
			UpdateCameraData();
		return cameraDataDirty;
	}

	runtimeData.dynamicResolutionPreviousWidthRatio = dynamicResolutionWidthRatio;
	runtimeData.dynamicResolutionPreviousHeightRatio = dynamicResolutionHeightRatio;
	runtimeData.dynamicResolutionWidthRatio = resolutionScale.x;
	runtimeData.dynamicResolutionHeightRatio = resolutionScale.y;
	runtimeData.dynamicResolutionLock = 1;

	dynamicResolutionWidthRatio = resolutionScale.x;
	dynamicResolutionHeightRatio = resolutionScale.y;
	return false;
}

void Upscaling::PrepareFullResolutionPostProcessing(RE::BSGraphics::State* a_viewport, bool a_resetProjection)
{
	auto* viewport = a_viewport ? a_viewport : globals::game::graphicsState;
	if (!viewport)
		return;

	bool cameraDataDirty = false;
	if (a_resetProjection) {
		cameraDataDirty =
			resolutionScale.x != 1.0f ||
			resolutionScale.y != 1.0f ||
			jitter.x != 0.0f ||
			jitter.y != 0.0f ||
			viewport->projectionPosScaleX != 0.0f ||
			viewport->projectionPosScaleY != 0.0f;
		if (cameraDataDirty) {
			resolutionScale = { 1.0f, 1.0f };
			jitter = { 0.0f, 0.0f };
			viewport->projectionPosScaleX = 0.0f;
			viewport->projectionPosScaleY = 0.0f;
		}
	}

	const bool dynamicResolutionDirty = ApplyLockedFullResolutionDynamicResolutionState(viewport);
	if (cameraDataDirty && !dynamicResolutionDirty)
		UpdateCameraData();
}

void Upscaling::SetupResources()
{
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		logger::warn("[Upscaling] Skipping upscaling resource setup because Open Composite has {}=true.", blocker.settingName);
		return;
	}
	if (IsRenderDocUpscalingBlocked(true)) {
		logger::warn(
			"[Upscaling] Skipping upscaling resource setup because {}.",
			GetRenderDocUpscalingBlockReason());
		return;
	}
	auto device = globals::d3d::device;
	static ID3D11Device* shaderDevice = nullptr;
	if (shaderDevice != device) {
		ClearShaderCache();
		ResetVRMenuFinalCompositeLayer();
		shaderDevice = device;
	}

	QueryPerformanceFrequency(&qpf);

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = true;                           // Enable depth testing
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // Write to all depth bits
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;          // Always pass depth test (write all depths)

	if (globals::game::isVR) {
		depthStencilDesc.StencilEnable = true;     // Enable stencil testing
		depthStencilDesc.StencilReadMask = 0xFF;   // Read all stencil bits
		depthStencilDesc.StencilWriteMask = 0xFF;  // Write to all stencil bits

		// Configure front-facing stencil operations
		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;       // Replace on stencil fail
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;  // Replace on depth fail
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;    // Replace on pass
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;       // Always pass stencil test

		// Configure back-facing stencil operations (same as front)
		depthStencilDesc.BackFace.StencilFailOp = depthStencilDesc.FrontFace.StencilFailOp;
		depthStencilDesc.BackFace.StencilDepthFailOp = depthStencilDesc.FrontFace.StencilDepthFailOp;
		depthStencilDesc.BackFace.StencilPassOp = depthStencilDesc.FrontFace.StencilPassOp;
		depthStencilDesc.BackFace.StencilFunc = depthStencilDesc.FrontFace.StencilFunc;
	} else {
		depthStencilDesc.StencilEnable = false;  // Disable stencil testing
	}

	DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, upscaleDepthStencilState.put()));

	// Create jitter offset constant buffer for depth upscaling
	delete jitterCB;
	jitterCB = new ConstantBuffer(ConstantBufferDesc<JitterCB>());

	// Create upscaling data constant buffer for encode textures compute shader
	delete upscalingDataCB;
	upscalingDataCB = new ConstantBuffer(ConstantBufferDesc<UpscalingDataCB>());
	delete dynamicResolutionStretchCB;
	dynamicResolutionStretchCB = new ConstantBuffer(ConstantBufferDesc<DynamicResolutionStretchCB>(), "Upscaling::DynamicResolutionStretchCB");
	delete vrMenuLayerCompositeCB;
	vrMenuLayerCompositeCB = new ConstantBuffer(ConstantBufferDesc<VRMenuLayerCompositeCB>(), "Upscaling::VRMenuLayerCompositeCB");
	delete foveatedPeripheryCB;
	foveatedPeripheryCB = new ConstantBuffer(ConstantBufferDesc<FoveatedPeripheryCB>());
	delete foveatedCenterBlendCB;
	foveatedCenterBlendCB = new ConstantBuffer(ConstantBufferDesc<FoveatedCenterBlendCB>());
	delete peripheryTAACB;
	peripheryTAACB = new ConstantBuffer(ConstantBufferDesc<PeripheryTAACB>());

	// Create blend state for depth upscaling
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, upscaleBlendState.put()));

	D3D11_BLEND_DESC menuBlendDesc = {};
	menuBlendDesc.AlphaToCoverageEnable = false;
	menuBlendDesc.IndependentBlendEnable = false;
	menuBlendDesc.RenderTarget[0].BlendEnable = true;
	menuBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	menuBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	menuBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	menuBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	menuBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	menuBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	menuBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(device->CreateBlendState(&menuBlendDesc, vrMenuCompositeBlendState.put()));

	D3D11_BLEND_DESC menuLayerCaptureBlendDesc = menuBlendDesc;
	menuLayerCaptureBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	DX::ThrowIfFailed(device->CreateBlendState(&menuLayerCaptureBlendDesc, vrMenuLayerCaptureBlendState.put()));

	// Create rasterizer state for fullscreen rendering
	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.DepthBiasClamp = 0.0f;
	rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	rasterizerDesc.DepthClipEnable = false;
	rasterizerDesc.ScissorEnable = false;
	rasterizerDesc.MultisampleEnable = false;
	rasterizerDesc.AntialiasedLineEnable = false;
	DX::ThrowIfFailed(device->CreateRasterizerState(&rasterizerDesc, upscaleRasterizerState.put()));

	CheckResources(GetRuntimeUpscaleMethod());
	RefreshRuntimeResolutionState();

	rcas.Initialize();
	if (GetDLSSSharpenerMode() == DLSSSharpenerMode::LumaUnsharp)
		lumaSharpen.Initialize();

	if (d3d12SwapChainActive)
		dx12SwapChain.CreateSharedResources();

	if (!copyDepthToSharedBufferPS)
		copyDepthToSharedBufferPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\CopyDepthToSharedBufferPS.hlsl", { { "PSHADER", "" } }, "ps_5_0"));
}

void Upscaling::SetupRenderTargetResources()
{
	SetupResources();
}

void Upscaling::ClearShaderCache()
{
	for (int i = 0; i < 5; ++i) {
		encodeTexturesCS[i] = nullptr;  // com_ptr automatically releases
	}
	encodeTexturesCSDepthOutput = nullptr;

	depthRefractionUpscalePS = nullptr;  // com_ptr automatically releases
	underwaterMaskUpscalePS = nullptr;   // com_ptr automatically releases
	underwaterMaskUpscaleRawDepthNoStencilPS = nullptr;
	upscaleVS = nullptr;               // com_ptr automatically releases
	foveatedPeripheryCS = nullptr;     // com_ptr automatically releases
	foveatedCenterBlendCS = nullptr;   // com_ptr automatically releases
	peripheryTAACS = nullptr;          // com_ptr automatically releases
	submitStageStretchCS = nullptr;    // com_ptr automatically releases
	vrDesktopMirrorBlitPS = nullptr;   // com_ptr automatically releases
	vrDesktopMirrorBlitRTV = nullptr;  // com_ptr automatically releases
	vrDesktopMirrorBlitTarget = nullptr;
	vrMenuLayerCompositePS = nullptr;     // com_ptr automatically releases
	vrClearHMDMaskCS = nullptr;           // com_ptr automatically releases
	vrClearHMDMaskCB = nullptr;           // com_ptr automatically releases
	copyDepthToSharedBufferPS = nullptr;  // com_ptr automatically releases
	rcas.ClearShaderCache();
	lumaSharpen.ClearShaderCache();
}

void Upscaling::CopySharedD3D12Resources()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Copy Shared D3D12 Resources");
	globals::state->BeginPerfEvent("Copy Shared D3D12 Resources");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	context->CopyResource(dx12SwapChain.motionVectorBufferShared12->resource11, motionVector.texture);

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	{
		// Set up viewport for fullscreen rendering
		auto screenSize = globals::state->screenSize;

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = screenSize.x;
		viewport.Height = screenSize.y;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		// Set up Input Assembler for fullscreen triangle
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Set up vertex shader
		context->VSSetShader(GetUpscaleVS(), nullptr, 0);

		// Set up rasterizer and blend states
		context->RSSetState(upscaleRasterizerState.get());
		context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

		// Set up pixel shader resources
		ID3D11ShaderResourceView* views[1] = { depth.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(views), views);

		// Set render target view for pixel shader output
		ID3D11RenderTargetView* rtvs[1] = { dx12SwapChain.depthBufferShared12->rtv };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(copyDepthToSharedBufferPS.get(), nullptr, 0);

		{
			CS_PROFILE_SCOPE("Upscaling::CopyDepthD3D12");
			context->Draw(3, 0);
		}
	}

	// Clean up
	ID3D11ShaderResourceView* views[1] = { nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(views), views);

	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->PSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);

	globals::state->EndPerfEvent();
}

void UpdateCameraData()
{
	using func_t = decltype(&UpdateCameraData);
	static REL::Relocation<func_t> func{ RELOCATION_ID(75472, 77258) };
	func();
}

void Upscaling::PostDisplay()
{
	auto viewport = globals::game::graphicsState;

	viewport->projectionPosScaleX = projectionPosScaleX;
	viewport->projectionPosScaleY = projectionPosScaleY;

	const bool vrVendorMenu =
		globals::game::isVR &&
		IsVendorUpscalingMethod(GetRuntimeUpscaleMethod()) &&
		IsVRMenuPresentationContextActive() &&
		IsVRRenderScaleTransitionSafetyRelevant(*this);
	if (vrVendorMenu) {
		PrepareFullResolutionPostProcessing(viewport, true);
	}

	RefreshVRMenuBridgeTraceState(globals::state);

	if (d3d12SwapChainActive)
		SetUIBuffer();

	globals::state->UpdateSharedData(false, false);
}

void Upscaling::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void Upscaling::FrameLimiter()
{
	if (d3d12SwapChainActive) {
		// Use frame latency waitable object if available for better frame pacing
		HANDLE waitableObject = GetFrameLatencyWaitableObject();

		// Wait for the next frame presentation slot
		WaitForSingleObject(waitableObject, INFINITE);

		if (settings.frameLimitMode) {
			// Fall back to the original timing method
			// Use integer arithmetic for more precise timing
			static constexpr int64_t kNanosecondsPerSecond = 1000000000LL;
			static constexpr double kFrameGenerationRateScale = 0.5;
			const double frameRateScale = ShouldUseFrameGenerationThisFrame() ? kFrameGenerationRateScale : 1.0;
			int64_t targetFrameTimeNS = int64_t(static_cast<double>(kNanosecondsPerSecond) / (refreshRate * frameRateScale));
			int64_t targetFrameTicks = (targetFrameTimeNS * qpf.QuadPart) / kNanosecondsPerSecond;

			static LARGE_INTEGER lastFrame = {};
			LARGE_INTEGER timeNow;
			QueryPerformanceCounter(&timeNow);

			int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
			if (delta < targetFrameTicks) {
				TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
			}
			QueryPerformanceCounter(&lastFrame);
		}
	}
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double Upscaling::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS && wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						// get the refresh rate
						UINT numerator = p.targetInfo.refreshRate.Numerator;
						UINT denominator = p.targetInfo.refreshRate.Denominator;
						return (double)numerator / (double)denominator;
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

bool Upscaling::IsFrameGenerationActive() const
{
	return IsFrameGenerationDx12PathActive() && settings.frameGenerationMode && fidelityFX.isFrameGenActive;
}

bool Upscaling::IsFrameGenerationDx12PathActive() const
{
	// Frame generation in this implementation runs via the DX12 swap-chain proxy path.
	return d3d12SwapChainActive && !globals::game::isVR;
}

bool Upscaling::ShouldUseFrameGenerationThisFrame() const
{
	auto* ui = globals::game::ui;
	auto* state = globals::state;
	const bool pausedMenuOpen = ui && ui->GameIsPaused();
	const bool mainOrLoadingMenuOpen = state && state->IsMainOrLoadingMenuOpen(ui);
	const bool menuOpen = pausedMenuOpen || mainOrLoadingMenuOpen;

	return IsFrameGenerationDx12PathActive() && settings.frameGenerationMode && (settings.frameGenerationAllowInMenus || !menuOpen);
}

bool Upscaling::IsUpscalingActive() const
{
	auto method = GetRuntimeUpscaleMethod();

	// Only consider vendor upscalers (FSR/DLSS) as "active" when the
	// selected method actually produces a downscale. If the renderer is
	// currently running at 1:1 (no downscale), treat upscaling as inactive.
	if (!IsVendorUpscalingMethod(method)) {
		return false;
	}

	return resolutionScale.x < kDynamicResolutionUpscalingScaleThreshold ||
	       resolutionScale.y < kDynamicResolutionUpscalingScaleThreshold;
}

bool Upscaling::IsSubmitStageUpscalingActive() const
{
	if (IsSubmitStageDeviceLost()) {
		return false;
	}

	const bool submitStageSceneActive = IsPerfModePresentationActive();

	const bool menuBlocksSubmitStage =
		globals::game::isVR ? IsVRMenuScenePresentationBlockActive() : IsGameMenuContextActive();
	return submitStageSceneActive && !menuBlocksSubmitStage;
}

bool Upscaling::ShouldSuppressVRInSceneOverlaySubmit() const
{
	if (!globals::game::isVR)
		return false;

	if (IsVRRenderScaleTransitionSafetyRelevant(*this) && HasPendingVRRenderScaleTransition())
		return true;

	if (pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
		perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire)) {
		return true;
	}

	const auto requestedMethod = GetConfiguredUpscaleMethodForTransition();
	const auto runtimeMethod = GetRuntimeUpscaleMethod();
	const bool transitionRelevant =
		IsVRRenderScaleTransitionSafetyRelevant(*this, requestedMethod) ||
		IsVRRenderScaleTransitionSafetyRelevant(*this, runtimeMethod);
	if (transitionRelevant &&
		(postLoadRuntimeResetPending.load(std::memory_order_acquire) ||
			HasPendingVRVendorRuntimeReset(*this, requestedMethod) ||
			HasPendingVRVendorRuntimeReset(*this, runtimeMethod))) {
		return true;
	}

	return false;
}

bool Upscaling::IsVRProtectedFullSizeSubmitTexture(const vr::Texture_t* a_texture) const
{
	if (!globals::game::isVR || !a_texture || !a_texture->handle || a_texture->eType != vr::TextureType_DirectX)
		return false;

	return IsVRNativeLayoutSubmitProtectedRenderTargetTexture(static_cast<ID3D11Texture2D*>(a_texture->handle));
}

bool Upscaling::ShouldSuppressVRRenderScaleOriginalSubmitFallback(const vr::Texture_t* a_texture) const
{
	if (!globals::game::isVR || !a_texture || !a_texture->handle || a_texture->eType != vr::TextureType_DirectX)
		return false;
	if (!IsVRRenderScaleModeLatched())
		return false;
	if (IsVRProtectedFullSizeSubmitTexture(a_texture))
		return false;

	const auto* state = globals::state;
	if (!state)
		return false;

	if (!IsVRLoadingSubmitProtectionContextActive(*this, state))
		return false;

	float2 finalOutputSize = perfMode.GetDisplayScreenSize();
	if (finalOutputSize.x <= 0.0f || finalOutputSize.y <= 0.0f)
		finalOutputSize = runtimeResolutionPlan.finalOutputSize;

	const uint32_t finalWidth = ClampPositiveDimension(finalOutputSize.x);
	const uint32_t finalHeight = ClampPositiveDimension(finalOutputSize.y);
	if (!finalWidth || !finalHeight)
		return false;

	auto* sourceTexture = static_cast<ID3D11Texture2D*>(a_texture->handle);
	D3D11_TEXTURE2D_DESC sourceDesc{};
	sourceTexture->GetDesc(&sourceDesc);
	if (sourceDesc.SampleDesc.Count != 1)
		return false;

	const uint32_t requiredWidth = sourceDesc.ArraySize > 1 ? std::max<uint32_t>(1u, finalWidth / 2u) : finalWidth;
	const uint32_t requiredHeight = finalHeight;
	return sourceDesc.Width < requiredWidth || sourceDesc.Height < requiredHeight;
}

bool Upscaling::IsSubmitStageDeviceLost() const
{
	return submitStageDeviceLost.load(std::memory_order_acquire);
}

void Upscaling::MarkSubmitStageDeviceLost(HRESULT a_result, const char* a_context)
{
	const HRESULT deviceReason = GetD3DDeviceRemovedReason();
	const HRESULT loggedResult = IsD3DDeviceRemovedResult(deviceReason) ? deviceReason : a_result;
	if (!IsD3DDeviceRemovedResult(loggedResult))
		return;

	const bool alreadyMarked = submitStageDeviceLost.exchange(true, std::memory_order_acq_rel);
	InvalidateFrameScopedUpscalingState();
	submitStagePreparedFrame = std::numeric_limits<uint32_t>::max();
	submitStagePreparedFramePresentationOnly = false;
	submitStagePreparedFrameFoveatedRegionEncode = false;
	submitStageVendorOutputFrame = std::numeric_limits<uint32_t>::max();
	submitStageVendorOutputSourceTexture = nullptr;
	submitStageVendorEyeState = {};
	submitStageForceFullEyeVendorFallback = false;
	ClearSubmitStageVendorResumeCooldown();
	ClearSubmitStageFoveatedVendorRetryBackoff();
	submitStageMirrorFrame = std::numeric_limits<uint32_t>::max();
	submitStageMirrorEyeReady = {};
	submitStageMirrorSourceTexture = nullptr;
	submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	submitStageFoveatedPeripheryTAAEyeReady = {};
	ResetVRMenuFinalCompositeLayer();
	pendingDLSSReset.store(false, std::memory_order_release);
	pendingFSRReset.store(false, std::memory_order_release);
	pendingPerfModeRenderTargetRecreate.store(false, std::memory_order_release);
	pendingPerfModeRenderTargetRecreateFrame.store(0, std::memory_order_release);
	pendingPerfModeRenderTargetRecreateDelayFrames.store(0, std::memory_order_release);
	pendingPerfModeRenderTargetRecreatePostLoadSettle.store(false, std::memory_order_release);
	postLoadRuntimeResetPending.store(false, std::memory_order_release);
	vrRenderScaleResourceTrackingSyncPending.store(false, std::memory_order_release);
	ClearVRRenderScaleInfoTransition();
	ClearPendingVRUpscalingTransition();
	streamline.ResetDLSSIdleFences();
	streamline.InvalidateDLSSOptionsCache();
	streamline.ResetFrameTracking();
	RequestHistoryReset();

	if (!alreadyMarked) {
		logger::error(
			"[Upscaling] Submit-stage D3D device removal detected during {}; disabling submit-stage upscaling for this device. result=0x{:08X} deviceReason=0x{:08X}",
			a_context && *a_context ? a_context : "submit-stage work",
			static_cast<uint32_t>(a_result),
			static_cast<uint32_t>(deviceReason));
	}
}

bool Upscaling::MarkSubmitStageDeviceLostIfNeeded(const std::exception& a_exception, const char* a_context)
{
	if (const auto* comException = dynamic_cast<const DX::com_exception*>(&a_exception)) {
		const HRESULT result = comException->Error();
		if (IsD3DDeviceRemovedResult(result)) {
			MarkSubmitStageDeviceLost(result, a_context);
			return true;
		}
	}

	return MarkSubmitStageDeviceLostIfDeviceRemoved(a_context);
}

bool Upscaling::MarkSubmitStageDeviceLostIfDeviceRemoved(const char* a_context)
{
	const HRESULT deviceReason = GetD3DDeviceRemovedReason();
	if (!IsD3DDeviceRemovedResult(deviceReason))
		return false;

	MarkSubmitStageDeviceLost(deviceReason, a_context);
	return true;
}

bool Upscaling::SubmitVRUpscaledFrame(vr::EVREye a_eye, const vr::Texture_t* a_inputTexture, const vr::VRTextureBounds_t* a_inputBounds,
	vr::Texture_t& a_outputTexture, vr::VRTextureBounds_t& a_outputBounds)
{
	if (!a_inputTexture || !a_inputTexture->handle || a_inputTexture->eType != vr::TextureType_DirectX) {
		return false;
	}
	if (a_eye != vr::Eye_Left && a_eye != vr::Eye_Right)
		return false;
	if (IsSubmitStageDeviceLost())
		return false;

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	if (!state || !renderer || !context)
		return false;

	EnsureRuntimeResolutionStateCurrent();
	const auto& resolutionPlan = GetRuntimeResolutionPlan();
	const auto upscaleMethod = resolutionPlan.upscaleMethod;
	if (!IsVendorUpscalingMethod(upscaleMethod))
		return false;
	const auto upscaleMethodName = magic_enum::enum_name(upscaleMethod);
	const uint32_t currentFrame = state->frameCount;
	const bool presentationUpscalingActive = IsPresentationUpscalingActive();
	const bool vrRenderScaleMode = resolutionPlan.owner == ResolutionOwner::VRRenderScaleMode;
	const bool loadingSubmitProtectionContext = IsVRLoadingSubmitProtectionContextActive(*this, state);
	const bool loadingPresentationFallbackActive =
		vrRenderScaleMode &&
		loadingSubmitProtectionContext;
	if (!presentationUpscalingActive && !loadingPresentationFallbackActive)
		return false;

	BeginVRMenuFinalCompositeFrame(currentFrame);
	auto* sourceTexture = static_cast<ID3D11Texture2D*>(a_inputTexture->handle);
	if (IsVRNativeLayoutSubmitProtectedRenderTargetTexture(sourceTexture))
		return false;

	const bool presentationRenderTarget = IsVRPresentationRenderTargetTexture(sourceTexture);
	const bool currentMenuPresentationContext = IsVRMenuPresentationContextActive();
	const bool loadingPresentationContext = loadingSubmitProtectionContext;
	const bool submitPresentationContext =
		loadingPresentationContext ||
		presentationRenderTarget;
	const bool sceneFeatureMenuPauseContext = IsVRSceneFeatureMenuPauseContextActive();
	const bool menuTextProtectionContext =
		resolutionPlan.knownMenuContextActive ||
		resolutionPlan.menuContextActive ||
		currentMenuPresentationContext ||
		sceneFeatureMenuPauseContext;

	D3D11_TEXTURE2D_DESC sourceDesc{};
	sourceTexture->GetDesc(&sourceDesc);
	if (sourceDesc.SampleDesc.Count != 1) {
		static bool loggedMSAA = false;
		if (!loggedMSAA) {
			logger::warn("[Upscaling] Submit-stage {} skipped because the submitted texture is MSAA.", upscaleMethodName);
			loggedMSAA = true;
		}
		return false;
	}

	const auto screenSize = state->screenSize;
	uint32_t eyeWidthOut = 0;
	uint32_t eyeHeightOut = 0;
	uint32_t eyeWidthIn = 0;
	uint32_t eyeHeightIn = 0;
	if (vrRenderScaleMode) {
		eyeWidthOut = std::max<uint32_t>(1u, ClampPositiveDimension(resolutionPlan.finalOutputSize.x) / 2u);
		eyeHeightOut = ClampPositiveDimension(resolutionPlan.finalOutputSize.y);
		eyeWidthIn = std::max<uint32_t>(1u, ClampPositiveDimension(resolutionPlan.engineRenderSize.x) / 2u);
		eyeHeightIn = ClampPositiveDimension(resolutionPlan.engineRenderSize.y);
	} else {
		const auto dynamicRenderSize = Util::ConvertToDynamic(screenSize, true);
		eyeWidthOut = static_cast<uint32_t>(screenSize.x / 2.0f);
		eyeHeightOut = static_cast<uint32_t>(screenSize.y);
		eyeWidthIn = static_cast<uint32_t>(dynamicRenderSize.x / 2.0f);
		eyeHeightIn = static_cast<uint32_t>(dynamicRenderSize.y);
	}
	if (!eyeWidthIn || !eyeHeightIn || !eyeWidthOut || !eyeHeightOut)
		return false;
	const auto outputStereoLayout = ResolveVRSideBySideStereoLayout(eyeWidthOut, eyeHeightOut);
	if (!outputStereoLayout.IsValid())
		return false;
	const bool presentationSourceHasFullArrayEye =
		presentationRenderTarget &&
		sourceDesc.ArraySize > 1 &&
		sourceDesc.Width >= eyeWidthOut &&
		sourceDesc.Height >= eyeHeightOut;
	const bool presentationSourceHasFullCombinedStereo =
		presentationRenderTarget &&
		sourceDesc.ArraySize == 1 &&
		TextureContainsVREyeRegion(sourceDesc.Width, sourceDesc.Height, outputStereoLayout.eyes[1]);
	const bool presentationSourceHasFullSingleEye =
		presentationRenderTarget &&
		sourceDesc.ArraySize == 1 &&
		sourceDesc.Width >= eyeWidthOut &&
		sourceDesc.Height >= eyeHeightOut &&
		!presentationSourceHasFullCombinedStereo;
	const bool presentationSourceHasFullOutputSize =
		presentationSourceHasFullArrayEye ||
		presentationSourceHasFullCombinedStereo ||
		presentationSourceHasFullSingleEye;
	const uint32_t sourceEyeWidthIn = presentationSourceHasFullOutputSize ? eyeWidthOut : eyeWidthIn;
	const uint32_t sourceEyeHeightIn = presentationSourceHasFullOutputSize ? eyeHeightOut : eyeHeightIn;
	if (!sourceEyeWidthIn || !sourceEyeHeightIn)
		return false;
	const auto sourceStereoLayout = ResolveVRSideBySideStereoLayout(sourceEyeWidthIn, sourceEyeHeightIn);
	if (!sourceStereoLayout.IsValid())
		return false;
	const uint32_t eyeIndex = a_eye == vr::Eye_Right ? 1u : 0u;
	const bool sourceUsesCombinedStereoLayout =
		sourceDesc.ArraySize == 1 &&
		TextureContainsVREyeRegion(sourceDesc.Width, sourceDesc.Height, sourceStereoLayout.eyes[1]);
	const bool inputBoundsUseCombinedStereoSpace =
		sourceUsesCombinedStereoLayout &&
		InputBoundsUseCombinedStereoSpace(a_inputBounds, eyeIndex);
	const auto sourceRegion = ResolveVRSubmitSourceRegion(
		sourceDesc,
		eyeIndex,
		sourceEyeWidthIn,
		sourceEyeHeightIn,
		sourceStereoLayout,
		sourceUsesCombinedStereoLayout,
		inputBoundsUseCombinedStereoSpace,
		a_inputBounds);
	if (!sourceRegion.valid) {
		static bool loggedInvalidSourceRegion = false;
		if (!loggedInvalidSourceRegion) {
			const auto inputBounds = BuildVRBoundsInfo(a_inputBounds);
			logger::warn(
				"[Upscaling] Submit-stage {} skipped because the submit source region is empty or invalid. eye={} source={}x{} bounds=({:.4f},{:.4f})->({:.4f},{:.4f}) expected={}x{}",
				upscaleMethodName,
				eyeIndex,
				sourceDesc.Width,
				sourceDesc.Height,
				inputBounds.uMin,
				inputBounds.vMin,
				inputBounds.uMax,
				inputBounds.vMax,
				sourceEyeWidthIn,
				sourceEyeHeightIn);
			loggedInvalidSourceRegion = true;
		}
		return false;
	}
	const bool submitBoundsPresentationFallback = vrRenderScaleMode && !sourceRegion.matchesExpectedSize;
	const bool transitionProtectionActive = IsVRTransitionPresentationProtectionActive(*this, state);
	bool transitionPresentationCooldown = false;
	const uint32_t vendorResumeFrame = submitStageVendorResumeFrame.load(std::memory_order_acquire);
	if (vrRenderScaleMode && vendorResumeFrame != 0) {
		if (currentFrame < vendorResumeFrame) {
			transitionPresentationCooldown = true;
		} else {
			ClearSubmitStageVendorResumeCooldown();
		}
	}
	auto computePresentationOnly = [&]() {
		return vrRenderScaleMode &&
		       (transitionPresentationCooldown ||
				   submitPresentationContext ||
				   submitBoundsPresentationFallback);
	};
	bool presentationOnly = computePresentationOnly();
	if (IsSubmitStageDeviceLost())
		return false;

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	if (submitStageVendorOutputFrame != currentFrame || submitStageVendorOutputSourceTexture != sourceTexture) {
		submitStageVendorOutputFrame = currentFrame;
		submitStageVendorOutputSourceTexture = sourceTexture;
		submitStageVendorEyeState = {};
		submitStageForceFullEyeVendorFallback = false;
	}

	const bool foveatedTransitionBypass = ShouldBypassVRFoveatedVendorDispatchForTransition(*this, state);
	if (transitionPresentationCooldown) {
		const bool hmdClearDeferred = ShouldDeferHMDClearMask();
		const bool projectedMaskDeferred = ShouldDeferVRProjectedMaskRepair(*this, state);
		const bool cooldownStableCandidate =
			vrRenderScaleMode &&
			!currentMenuPresentationContext &&
			!presentationRenderTarget &&
			!submitBoundsPresentationFallback &&
			sourceRegion.matchesExpectedSize &&
			!pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) &&
			!perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) &&
			!HasPendingVRVendorRuntimeReset(*this, upscaleMethod) &&
			!transitionProtectionActive &&
			!hmdClearDeferred &&
			!projectedMaskDeferred &&
			!foveatedTransitionBypass &&
			motionVector.texture &&
			depth.texture;
		uint32_t stableFrames = submitStageVendorResumeStableFrames.load(std::memory_order_acquire);
		if (cooldownStableCandidate) {
			const uint32_t lastStableFrame = submitStageVendorResumeLastStableFrame.load(std::memory_order_acquire);
			if (lastStableFrame != currentFrame) {
				stableFrames =
					lastStableFrame != 0 && currentFrame == lastStableFrame + 1 ?
						stableFrames + 1u :
						1u;
				submitStageVendorResumeStableFrames.store(stableFrames, std::memory_order_release);
				submitStageVendorResumeLastStableFrame.store(currentFrame, std::memory_order_release);
			}
		} else {
			stableFrames = 0;
			submitStageVendorResumeStableFrames.store(0, std::memory_order_release);
			submitStageVendorResumeLastStableFrame.store(0, std::memory_order_release);
		}

		const uint32_t cooldownStartFrame = submitStageVendorResumeStartFrame.load(std::memory_order_acquire);
		if (ElapsedFrames(cooldownStartFrame, currentFrame) >= kVRSubmitStageVendorRelatchMinCooldownFrames &&
			stableFrames >= kVRSubmitStageVendorRelatchStableFrames) {
			ClearSubmitStageVendorResumeCooldown();
			transitionPresentationCooldown = false;
			presentationOnly = computePresentationOnly();
			logger::debug("[VRRenderScale] Cleared submit-stage vendor resume cooldown after {} stable frames.", stableFrames);
			CompleteVRRenderScaleInfoTransition(
				"submit-stage vendor resume",
				true,
				upscaleMethod,
				perfMode.GetDisplayScreenSize(),
				perfMode.GetRenderScreenSize());
		}
	}

	if (!presentationOnly)
		EnsureResourcesCurrent(upscaleMethod);
	if (IsSubmitStageDeviceLost())
		return false;

	bool foveatedFailureBackoffActive = false;
	const uint32_t foveatedRetryFrame = submitStageFoveatedVendorRetryFrame.load(std::memory_order_acquire);
	if (upscaleMethod == UpscaleMethod::kDLSS && foveatedRetryFrame != 0) {
		if (currentFrame < foveatedRetryFrame) {
			foveatedFailureBackoffActive = true;
		} else {
			ClearSubmitStageFoveatedVendorRetryBackoff();
		}
	}

	const bool foveatedRequested =
		!presentationOnly &&
		!sceneFeatureMenuPauseContext &&
		IsFoveatedVendorDispatchEnabled(upscaleMethod) &&
		!foveatedTransitionBypass &&
		!foveatedFailureBackoffActive;
	const bool submitStageFoveatedPeripheryTAAPathActive =
		foveatedRequested &&
		IsPeripheryTAAPathActive(upscaleMethod);
	const bool submitStageNeedsRawDepthInput =
		upscaleMethod == UpscaleMethod::kDLSS ||
		submitStageFoveatedPeripheryTAAPathActive;

	const bool submitStagePreparedThisFrame = submitStagePreparedFrame == currentFrame;
	if (!submitStagePreparedThisFrame && !presentationOnly) {
		if (!ApplyPendingVendorRuntimeReset(upscaleMethod, "submit-stage ")) {
			if (IsSubmitStageDeviceLost())
				return false;

			return false;
		}
		if (IsSubmitStageDeviceLost())
			return false;

		if (HasPendingVRVendorRuntimeReset(*this, upscaleMethod))
			return false;

		if (historyResetLatchedFrame != currentFrame)
			UpdateHistoryResetState(upscaleMethod);
		LatchHistoryResetForCurrentFrame();
	} else {
		const bool vendorResetPending = HasPendingVRVendorRuntimeReset(*this, upscaleMethod);
		if (vendorResetPending && !presentationOnly)
			return false;
	}

	if (!presentationOnly && (!motionVector.texture || !depth.texture))
		return false;

	const bool shouldUseFoveatedVendorThisEye =
		foveatedRequested &&
		!submitStageForceFullEyeVendorFallback;

	const uint32_t presentationInputWidth = submitBoundsPresentationFallback ? sourceRegion.width : sourceEyeWidthIn;
	const uint32_t presentationInputHeight = submitBoundsPresentationFallback ? sourceRegion.height : sourceEyeHeightIn;

	if (presentationOnly) {
		const char* presentationContext = submitBoundsPresentationFallback ? "Submit bounds presentation fallback" : "Menu/loading presentation";
		try {
			if (!EnsureVRPresentationTextures(presentationInputWidth, presentationInputHeight, eyeWidthOut, eyeHeightOut, sourceTexture)) {
				logger::warn("[VRRenderScale] {} failed to create presentation textures.", presentationContext);
				return false;
			}
		} catch (const std::exception& e) {
			logger::warn("[VRRenderScale] {} failed to create presentation textures: {}", presentationContext, e.what());
			MarkSubmitStageDeviceLostIfNeeded(e, presentationContext);
			return false;
		} catch (...) {
			logger::warn("[VRRenderScale] {} failed to create presentation textures.", presentationContext);
			MarkSubmitStageDeviceLostIfDeviceRemoved(presentationContext);
			return false;
		}

		submitStagePreparedFrame = currentFrame;
		submitStagePreparedFramePresentationOnly = true;
		submitStagePreparedFrameFoveatedRegionEncode = false;
	} else if (!submitStagePreparedThisFrame || submitStagePreparedFramePresentationOnly) {
		bool encodedFoveatedRegions = false;
		if (!EncodeSubmitStageVRInputs(sourceTexture, motionVector.texture, depth.texture, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut, submitStageNeedsRawDepthInput, foveatedRequested, &encodedFoveatedRegions)) {
			if (IsSubmitStageDeviceLost())
				return false;
			return false;
		}

		submitStagePreparedFrame = currentFrame;
		submitStagePreparedFramePresentationOnly = false;
		submitStagePreparedFrameFoveatedRegionEncode = encodedFoveatedRegions;
	}

	if (!vrIntermediateColorIn[eyeIndex] || !vrIntermediateColorIn[eyeIndex]->resource ||
		!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->resource) {
		return false;
	}
	if (!presentationOnly &&
		(!vrIntermediateMotionVectors[eyeIndex] || !vrIntermediateMotionVectors[eyeIndex]->resource ||
			!vrIntermediateDepth[eyeIndex] || !vrIntermediateDepth[eyeIndex]->resource ||
			(upscaleMethod == UpscaleMethod::kFSR && (!vrIntermediateLinearDepth[eyeIndex] || !vrIntermediateLinearDepth[eyeIndex]->resource)) ||
			!vrIntermediateReactiveMask[eyeIndex] || !vrIntermediateReactiveMask[eyeIndex]->resource ||
			!vrIntermediateTransparencyMask[eyeIndex] || !vrIntermediateTransparencyMask[eyeIndex]->resource)) {
		return false;
	}

	const auto clearSubmittedEyeHMDMask = [&]() {
		if (presentationRenderTarget)
			return;
		if (!depth.depthSRV || !vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->uav)
			return;

		ClearHMDMaskForEye(
			HMDMaskClearPhase::SubmitStageOutput,
			vrIntermediateColorOut[eyeIndex]->uav.get(),
			depth.depthSRV,
			sourceRegion.depthWidth,
			sourceRegion.depthHeight,
			eyeWidthOut,
			eyeHeightOut,
			sourceRegion.depthOffsetX,
			0u,
			sourceRegion.depthOffsetY);
	};

	const UINT sourceSubresource = sourceRegion.subresource;
	const D3D11_BOX colorBox = sourceRegion.box;

	if (!sourceRegion.matchesExpectedSize && !submitBoundsPresentationFallback) {
		static bool loggedBadBounds = false;
		if (!loggedBadBounds) {
			logger::warn(
				"[Upscaling] Submit-stage {} skipped because submit bounds do not contain the expected render area. eye={} source={}x{} box=({},{})->({},{}) actual={}x{} expected={}x{}",
				upscaleMethodName,
				eyeIndex,
				sourceDesc.Width,
				sourceDesc.Height,
				colorBox.left,
				colorBox.top,
				colorBox.right,
				colorBox.bottom,
				sourceRegion.width,
				sourceRegion.height,
				sourceEyeWidthIn,
				sourceEyeHeightIn);
			loggedBadBounds = true;
		}
		return false;
	}
	if (submitBoundsPresentationFallback) {
		static bool loggedBoundsStretchFallback = false;
		if (!loggedBoundsStretchFallback) {
			logger::warn(
				"[VRRenderScale] Submit-stage {} using stretch fallback for non-standard submit bounds. eye={} source={}x{} box=({},{})->({},{}) actual={}x{} expected={}x{} openVRBounds={}",
				upscaleMethodName,
				eyeIndex,
				sourceDesc.Width,
				sourceDesc.Height,
				colorBox.left,
				colorBox.top,
				colorBox.right,
				colorBox.bottom,
				sourceRegion.width,
				sourceRegion.height,
				sourceEyeWidthIn,
				sourceEyeHeightIn,
				BoolText(sourceRegion.fromOpenVRBounds));
			loggedBoundsStretchFallback = true;
		}
	}

	context->CopySubresourceRegion(vrIntermediateColorIn[eyeIndex]->resource.get(), 0, 0, 0, 0, sourceTexture, sourceSubresource, &colorBox);
	if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage source copy"))
		return false;

	const auto presentStretchOutput = [&](uint32_t inputWidth, uint32_t inputHeight, const char* path) {
		(void)path;
		if (!StretchSubmitStageEyeOutput(eyeIndex, inputWidth, inputHeight, eyeWidthOut, eyeHeightOut) ||
			!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->resource) {
			return false;
		}
		if (IsSubmitStageDeviceLost())
			return false;

		clearSubmittedEyeHMDMask();
		if (IsSubmitStageDeviceLost())
			return false;

		a_outputTexture = *a_inputTexture;
		a_outputTexture.handle = vrIntermediateColorOut[eyeIndex]->resource.get();
		a_outputTexture.eType = vr::TextureType_DirectX;
		a_outputBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
		return true;
	};

	auto finalizeSubmitStageEyeOutput = [&](uint32_t targetEyeIndex, Texture2D& targetVendorColorOutput, bool targetSubmitDLSSSharpening,
											uint32_t clearDepthWidth, uint32_t clearDepthHeight, uint32_t clearDepthOffsetX, uint32_t clearDepthOffsetY) -> bool {
		if (targetSubmitDLSSSharpening) {
			if (!ApplySubmitStageDLSSSharpening(targetEyeIndex, targetVendorColorOutput)) {
				if (IsSubmitStageDeviceLost())
					return false;
				context->CopyResource(vrIntermediateColorOut[targetEyeIndex]->resource.get(), targetVendorColorOutput.resource.get());
				if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage DLSS sharpening fallback copy"))
					return false;
			}
			if (IsSubmitStageDeviceLost())
				return false;
		}

		if (vrRenderScaleMode &&
			!presentationRenderTarget &&
			menuTextProtectionContext) {
			ApplyKnownGameMenuFinalComposite(targetEyeIndex, *vrIntermediateColorOut[targetEyeIndex], eyeWidthOut, eyeHeightOut, currentFrame);
		}

		if (!presentationRenderTarget &&
			depth.depthSRV &&
			clearDepthWidth &&
			clearDepthHeight &&
			vrIntermediateColorOut[targetEyeIndex] &&
			vrIntermediateColorOut[targetEyeIndex]->uav) {
			ClearHMDMaskForEye(
				HMDMaskClearPhase::SubmitStageOutput,
				vrIntermediateColorOut[targetEyeIndex]->uav.get(),
				depth.depthSRV,
				clearDepthWidth,
				clearDepthHeight,
				eyeWidthOut,
				eyeHeightOut,
				clearDepthOffsetX,
				0u,
				clearDepthOffsetY);
			if (IsSubmitStageDeviceLost())
				return false;
		}

		return true;
	};

	auto replayStoredFullEyeVendorOutput = [&](uint32_t targetEyeIndex, bool preferDLSSSharpening) -> bool {
		const auto& targetEyeState = submitStageVendorEyeState[targetEyeIndex];
		if (!targetEyeState.ready || !targetEyeState.usedFoveatedVendorPath)
			return true;
		if (!vrIntermediateColorIn[targetEyeIndex] || !vrIntermediateColorIn[targetEyeIndex]->resource ||
			!vrIntermediateColorOut[targetEyeIndex] || !vrIntermediateColorOut[targetEyeIndex]->resource ||
			!vrIntermediateColorOut[targetEyeIndex]->uav ||
			!vrIntermediateDepth[targetEyeIndex] || !vrIntermediateDepth[targetEyeIndex]->resource ||
			!vrIntermediateMotionVectors[targetEyeIndex] || !vrIntermediateMotionVectors[targetEyeIndex]->resource ||
			!vrIntermediateReactiveMask[targetEyeIndex] || !vrIntermediateReactiveMask[targetEyeIndex]->resource ||
			!vrIntermediateTransparencyMask[targetEyeIndex] || !vrIntermediateTransparencyMask[targetEyeIndex]->resource ||
			(upscaleMethod == UpscaleMethod::kFSR &&
				(!vrIntermediateLinearDepth[targetEyeIndex] || !vrIntermediateLinearDepth[targetEyeIndex]->resource))) {
			return false;
		}

		Texture2D* replayVendorColorOutput = vrIntermediateColorOut[targetEyeIndex].get();
		bool replaySubmitDLSSSharpening = preferDLSSSharpening;
		static bool loggedReplaySharpenerOutputFailure[2] = {};
		if (replaySubmitDLSSSharpening) {
			if (!EnsureSubmitStageDLSSSharpenerTexture(targetEyeIndex, *vrIntermediateColorOut[targetEyeIndex])) {
				LogWarnOnceFmt(
					loggedReplaySharpenerOutputFailure[targetEyeIndex],
					"[Upscaling] Submit-stage DLSS replay sharpening skipped for eye {} because the intermediate output is unavailable.",
					targetEyeIndex);
				replaySubmitDLSSSharpening = false;
			} else {
				replayVendorColorOutput = submitStageDLSSSharpenerTexture[targetEyeIndex].get();
			}
		}
		if (!replayVendorColorOutput || !replayVendorColorOutput->resource || !replayVendorColorOutput->uav)
			return false;

		static bool loggedReplayFullEyeSubmitException[2] = {};
		try {
			VendorEyeDispatchParams vendorParams{};
			vendorParams.eyeIndex = targetEyeIndex;
			vendorParams.inputWidth = eyeWidthIn;
			vendorParams.inputHeight = eyeHeightIn;
			vendorParams.outputWidth = eyeWidthOut;
			vendorParams.outputHeight = eyeHeightOut;
			vendorParams.motionVectorScaleX = static_cast<float>(eyeWidthIn);
			vendorParams.motionVectorScaleY = static_cast<float>(eyeHeightIn);
			vendorParams.colorIn = vrIntermediateColorIn[targetEyeIndex]->resource.get();
			vendorParams.depth = upscaleMethod == UpscaleMethod::kFSR ?
			                         vrIntermediateLinearDepth[targetEyeIndex]->resource.get() :
			                         vrIntermediateDepth[targetEyeIndex]->resource.get();
			vendorParams.motionVectors = vrIntermediateMotionVectors[targetEyeIndex]->resource.get();
			vendorParams.reactiveMask = vrIntermediateReactiveMask[targetEyeIndex]->resource.get();
			vendorParams.transparencyMask = vrIntermediateTransparencyMask[targetEyeIndex]->resource.get();
			vendorParams.colorOut = replayVendorColorOutput->resource.get();
			vendorParams.label = "submit-stage full-eye replay";
			if (!DispatchVendorEyeRegion(upscaleMethod, vendorParams))
				return false;
		} catch (const std::exception& e) {
			UnbindUpscalingResources();
			if (MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage full-eye replay"))
				return false;
			LogWarnOnceFmt(
				loggedReplayFullEyeSubmitException[targetEyeIndex],
				"[Upscaling] Submit-stage full-eye {} replay threw for eye {}; keeping the earlier eye output for this frame: {}",
				upscaleMethodName,
				targetEyeIndex,
				e.what());
			return false;
		} catch (...) {
			UnbindUpscalingResources();
			if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage full-eye replay"))
				return false;
			LogWarnOnceFmt(
				loggedReplayFullEyeSubmitException[targetEyeIndex],
				"[Upscaling] Submit-stage full-eye {} replay threw for eye {}; keeping the earlier eye output for this frame",
				upscaleMethodName,
				targetEyeIndex);
			return false;
		}

		if (!finalizeSubmitStageEyeOutput(
				targetEyeIndex,
				*replayVendorColorOutput,
				replaySubmitDLSSSharpening,
				targetEyeState.depthWidth,
				targetEyeState.depthHeight,
				targetEyeState.depthOffsetX,
				targetEyeState.depthOffsetY)) {
			return false;
		}

		submitStageVendorEyeState[targetEyeIndex].usedFoveatedVendorPath = false;
		return true;
	};

	if (presentationOnly) {
		return presentStretchOutput(
			presentationInputWidth,
			presentationInputHeight,
			submitBoundsPresentationFallback ? "submit-bounds-stretch-output" : "menu-loading-presentation-output");
	}

	bool submitDLSSSharpening = upscaleMethod == UpscaleMethod::kDLSS && ShouldApplyDLSSSharpening();
	Texture2D* vendorColorOutput = vrIntermediateColorOut[eyeIndex].get();
	if (submitDLSSSharpening) {
		static bool loggedSharpenerOutputFailure[2] = {};
		if (!EnsureSubmitStageDLSSSharpenerTexture(eyeIndex, *vrIntermediateColorOut[eyeIndex])) {
			LogWarnOnceFmt(
				loggedSharpenerOutputFailure[eyeIndex],
				"[Upscaling] Submit-stage DLSS sharpening skipped for eye {} because the intermediate output is unavailable.",
				eyeIndex);
			submitDLSSSharpening = false;
		} else {
			vendorColorOutput = submitStageDLSSSharpenerTexture[eyeIndex].get();
		}
	}
	if (!vendorColorOutput || !vendorColorOutput->resource || !vendorColorOutput->uav)
		return false;

	bool vendorSucceeded = false;
	if (shouldUseFoveatedVendorThisEye) {
		static bool loggedFoveatedSubmitException[2] = {};
		try {
			vendorSucceeded = DispatchSubmitStageFoveatedVendorEye(
				upscaleMethod,
				eyeIndex,
				eyeWidthIn,
				eyeHeightIn,
				eyeWidthOut,
				eyeHeightOut,
				vendorColorOutput->resource.get(),
				vendorColorOutput->uav.get());
		} catch (const std::exception& e) {
			UnbindUpscalingResources();
			if (MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage foveated vendor dispatch"))
				return false;
			LogWarnOnceFmt(
				loggedFoveatedSubmitException[eyeIndex],
				"[Upscaling] Submit-stage foveated {} threw for eye {}; using fallback path: {}",
				upscaleMethodName,
				eyeIndex,
				e.what());
			vendorSucceeded = false;
		} catch (...) {
			UnbindUpscalingResources();
			if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage foveated vendor dispatch"))
				return false;
			LogWarnOnceFmt(
				loggedFoveatedSubmitException[eyeIndex],
				"[Upscaling] Submit-stage foveated {} threw for eye {}; using fallback path",
				upscaleMethodName,
				eyeIndex);
			vendorSucceeded = false;
		}
		if (!vendorSucceeded && MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage foveated vendor dispatch"))
			return false;
		if (!vendorSucceeded) {
			submitStageForceFullEyeVendorFallback = true;
			RequestHistoryReset();
			if (upscaleMethod == UpscaleMethod::kDLSS)
				ArmSubmitStageFoveatedVendorRetryBackoff(currentFrame);
			static bool loggedFoveatedSubmitFallback[2] = {};
			if (!loggedFoveatedSubmitFallback[eyeIndex]) {
				logger::warn(
					"[Upscaling] Submit-stage foveated {} failed for eye {}; using stretch fallback for this frame and retrying foveated dispatch after a short backoff.",
					upscaleMethodName,
					eyeIndex);
				loggedFoveatedSubmitFallback[eyeIndex] = true;
			}
		}
	}

	bool fullEyeVendorFallbackAvailable =
		!(upscaleMethod == UpscaleMethod::kDLSS &&
			foveatedRequested &&
			submitStageForceFullEyeVendorFallback);
	if (!vendorSucceeded && fullEyeVendorFallbackAvailable && submitStagePreparedFrameFoveatedRegionEncode) {
		bool encodedFoveatedRegions = false;
		fullEyeVendorFallbackAvailable = EncodeSubmitStageVRInputs(
			sourceTexture,
			motionVector.texture,
			depth.texture,
			eyeWidthIn,
			eyeHeightIn,
			eyeWidthOut,
			eyeHeightOut,
			submitStageNeedsRawDepthInput,
			false,
			&encodedFoveatedRegions);
		if (IsSubmitStageDeviceLost())
			return false;

		if (fullEyeVendorFallbackAvailable) {
			submitStagePreparedFrame = currentFrame;
			submitStagePreparedFramePresentationOnly = false;
			submitStagePreparedFrameFoveatedRegionEncode = false;
		} else {
			static bool loggedFullEyeEncodeFailure[2] = {};
			if (!loggedFullEyeEncodeFailure[eyeIndex]) {
				logger::warn(
					"[Upscaling] Submit-stage full-eye {} fallback skipped for eye {} because full input encode failed after foveated dispatch fallback.",
					upscaleMethodName,
					eyeIndex);
				loggedFullEyeEncodeFailure[eyeIndex] = true;
			}
		}
	}

	if (!vendorSucceeded && fullEyeVendorFallbackAvailable) {
		const uint32_t otherEyeIndex = eyeIndex ^ 1u;
		const bool replayOtherEyeFromFoveated =
			submitStageVendorEyeState[otherEyeIndex].ready &&
			submitStageVendorEyeState[otherEyeIndex].usedFoveatedVendorPath;
		if (replayOtherEyeFromFoveated && !replayStoredFullEyeVendorOutput(otherEyeIndex, submitDLSSSharpening) && IsSubmitStageDeviceLost())
			return false;

		static bool loggedFullEyeSubmitException[2] = {};
		try {
			VendorEyeDispatchParams vendorParams{};
			vendorParams.eyeIndex = eyeIndex;
			vendorParams.inputWidth = eyeWidthIn;
			vendorParams.inputHeight = eyeHeightIn;
			vendorParams.outputWidth = eyeWidthOut;
			vendorParams.outputHeight = eyeHeightOut;
			vendorParams.motionVectorScaleX = static_cast<float>(eyeWidthIn);
			vendorParams.motionVectorScaleY = static_cast<float>(eyeHeightIn);
			vendorParams.colorIn = vrIntermediateColorIn[eyeIndex]->resource.get();
			vendorParams.depth = upscaleMethod == UpscaleMethod::kFSR ?
			                         vrIntermediateLinearDepth[eyeIndex]->resource.get() :
			                         vrIntermediateDepth[eyeIndex]->resource.get();
			vendorParams.motionVectors = vrIntermediateMotionVectors[eyeIndex]->resource.get();
			vendorParams.reactiveMask = vrIntermediateReactiveMask[eyeIndex]->resource.get();
			vendorParams.transparencyMask = vrIntermediateTransparencyMask[eyeIndex]->resource.get();
			vendorParams.colorOut = vendorColorOutput->resource.get();
			vendorParams.label = "submit-stage full-eye";
			vendorSucceeded = DispatchVendorEyeRegion(upscaleMethod, vendorParams);
		} catch (const std::exception& e) {
			UnbindUpscalingResources();
			if (MarkSubmitStageDeviceLostIfNeeded(e, "submit-stage full-eye vendor dispatch"))
				return false;
			LogWarnOnceFmt(
				loggedFullEyeSubmitException[eyeIndex],
				"[Upscaling] Submit-stage full-eye {} threw for eye {}; using stretch fallback: {}",
				upscaleMethodName,
				eyeIndex,
				e.what());
			vendorSucceeded = false;
		} catch (...) {
			UnbindUpscalingResources();
			if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage full-eye vendor dispatch"))
				return false;
			LogWarnOnceFmt(
				loggedFullEyeSubmitException[eyeIndex],
				"[Upscaling] Submit-stage full-eye {} threw for eye {}; using stretch fallback",
				upscaleMethodName,
				eyeIndex);
			vendorSucceeded = false;
		}
		if (!vendorSucceeded && MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage full-eye vendor dispatch"))
			return false;
		if (!vendorSucceeded)
			UnbindUpscalingResources();
	}

	if (!vendorSucceeded) {
		static bool loggedSubmitFailure[2] = {};
		if (!loggedSubmitFailure[eyeIndex]) {
			logger::warn(
				"[Upscaling] Submit-stage {} failed for eye {}; using a full-size stretch fallback for this frame.",
				upscaleMethodName,
				eyeIndex);
			loggedSubmitFailure[eyeIndex] = true;
		}

		if (upscaleMethod == UpscaleMethod::kDLSS) {
			streamline.InvalidateDLSSOptionsCache();
			streamline.ResetFrameTracking();
		}
		RequestHistoryReset();

		if (IsSubmitStageDeviceLost())
			return false;

		if (vrRenderScaleMode &&
			presentStretchOutput(eyeWidthIn, eyeHeightIn, "vendor-failed-stretch-output")) {
			return true;
		}

		return false;
	}

	if (!finalizeSubmitStageEyeOutput(
			eyeIndex,
			*vendorColorOutput,
			submitDLSSSharpening,
			sourceRegion.depthWidth,
			sourceRegion.depthHeight,
			sourceRegion.depthOffsetX,
			sourceRegion.depthOffsetY)) {
		return false;
	}

	submitStageVendorEyeState[eyeIndex].ready = true;
	submitStageVendorEyeState[eyeIndex].usedFoveatedVendorPath = shouldUseFoveatedVendorThisEye && !submitStageForceFullEyeVendorFallback;
	submitStageVendorEyeState[eyeIndex].depthWidth = sourceRegion.depthWidth;
	submitStageVendorEyeState[eyeIndex].depthHeight = sourceRegion.depthHeight;
	submitStageVendorEyeState[eyeIndex].depthOffsetX = sourceRegion.depthOffsetX;
	submitStageVendorEyeState[eyeIndex].depthOffsetY = sourceRegion.depthOffsetY;
	if (vrRenderScaleMode) {
		const bool canMirrorToSource =
			sourceDesc.ArraySize == 1 &&
			sourceDesc.Width >= eyeWidthOut * 2 &&
			sourceDesc.Height >= eyeHeightOut &&
			vrIntermediateColorOut[0] && vrIntermediateColorOut[1] &&
			vrIntermediateColorOut[0]->resource && vrIntermediateColorOut[1]->resource &&
			vrIntermediateColorOut[0]->desc.Width >= eyeWidthOut &&
			vrIntermediateColorOut[0]->desc.Height >= eyeHeightOut &&
			vrIntermediateColorOut[1]->desc.Width >= eyeWidthOut &&
			vrIntermediateColorOut[1]->desc.Height >= eyeHeightOut &&
			vrIntermediateColorOut[0]->desc.Format == sourceDesc.Format &&
			vrIntermediateColorOut[1]->desc.Format == sourceDesc.Format;

		if (canMirrorToSource) {
			vrDesktopMirrorBlitRTV = nullptr;
			vrDesktopMirrorBlitTarget = nullptr;

			if (submitStageMirrorFrame != currentFrame || submitStageMirrorSourceTexture != sourceTexture) {
				submitStageMirrorFrame = currentFrame;
				submitStageMirrorSourceTexture = sourceTexture;
				submitStageMirrorEyeReady = {};
			}

			submitStageMirrorEyeReady[eyeIndex] = true;
			if (submitStageMirrorEyeReady[0] && submitStageMirrorEyeReady[1]) {
				D3D11_BOX mirrorBox{ 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
				context->CopySubresourceRegion(sourceTexture, 0, 0, 0, 0, vrIntermediateColorOut[0]->resource.get(), 0, &mirrorBox);
				context->CopySubresourceRegion(sourceTexture, 0, eyeWidthOut, 0, 0, vrIntermediateColorOut[1]->resource.get(), 0, &mirrorBox);
				if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage mirror writeback"))
					return false;
				submitStageMirrorEyeReady = {};
			}
		} else {
			if (globals::features::vr.settings.StabilizeRenderScaleDesktopMirror) {
				if (submitStageMirrorFrame != currentFrame || submitStageMirrorSourceTexture != sourceTexture) {
					submitStageMirrorFrame = currentFrame;
					submitStageMirrorSourceTexture = sourceTexture;
					submitStageMirrorEyeReady = {};
				}

				submitStageMirrorEyeReady[eyeIndex] = true;
				if (submitStageMirrorEyeReady[0] && submitStageMirrorEyeReady[1]) {
					static bool loggedSubmitStageMirrorFallbackFailure = false;
					const bool mirrorUpdated = BlitVRRenderScaleDesktopMirror(sourceTexture, sourceDesc, eyeWidthOut, eyeHeightOut);
					if (!mirrorUpdated && IsSubmitStageDeviceLost())
						return false;
					if (!mirrorUpdated && !loggedSubmitStageMirrorFallbackFailure) {
						logger::warn(
							"[Upscaling] Desktop mirror fallback could not update incompatible render-scale submit texture. source={}x{} array={} format={} outputL={}x{} format={} outputR={}x{} format={}",
							sourceDesc.Width,
							sourceDesc.Height,
							sourceDesc.ArraySize,
							static_cast<uint32_t>(sourceDesc.Format),
							vrIntermediateColorOut[0] ? vrIntermediateColorOut[0]->desc.Width : 0,
							vrIntermediateColorOut[0] ? vrIntermediateColorOut[0]->desc.Height : 0,
							vrIntermediateColorOut[0] ? static_cast<uint32_t>(vrIntermediateColorOut[0]->desc.Format) : 0,
							vrIntermediateColorOut[1] ? vrIntermediateColorOut[1]->desc.Width : 0,
							vrIntermediateColorOut[1] ? vrIntermediateColorOut[1]->desc.Height : 0,
							vrIntermediateColorOut[1] ? static_cast<uint32_t>(vrIntermediateColorOut[1]->desc.Format) : 0);
						loggedSubmitStageMirrorFallbackFailure = true;
					}
					submitStageMirrorEyeReady = {};
				}
			} else {
				vrDesktopMirrorBlitRTV = nullptr;
				vrDesktopMirrorBlitTarget = nullptr;
				submitStageMirrorEyeReady = {};
			}
		}

		a_outputTexture = *a_inputTexture;
		a_outputTexture.handle = vrIntermediateColorOut[eyeIndex]->resource.get();
		a_outputTexture.eType = vr::TextureType_DirectX;
		a_outputBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
		return true;
	}

	UINT outputSubresource = 0;
	uint32_t outputOffsetX = 0;
	if (sourceDesc.ArraySize > 1) {
		const UINT arraySlice = std::min<UINT>(eyeIndex, sourceDesc.ArraySize - 1);
		outputSubresource = D3D11CalcSubresource(0, arraySlice, sourceDesc.MipLevels);
	} else if (sourceDesc.Width >= eyeWidthOut * 2) {
		outputOffsetX = eyeIndex * eyeWidthOut;
	}
	if (outputOffsetX + eyeWidthOut > sourceDesc.Width || eyeHeightOut > sourceDesc.Height) {
		static bool loggedBadOutputBounds = false;
		if (!loggedBadOutputBounds) {
			logger::warn(
				"[Upscaling] Submit-stage {} skipped because output copy would exceed submit texture bounds. eye={} source={}x{} dst=({},0) size={}x{}",
				upscaleMethodName,
				eyeIndex,
				sourceDesc.Width,
				sourceDesc.Height,
				outputOffsetX,
				eyeWidthOut,
				eyeHeightOut);
			loggedBadOutputBounds = true;
		}
		return false;
	}

	D3D11_BOX outputBox{ 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
	context->CopySubresourceRegion(sourceTexture, outputSubresource, outputOffsetX, 0, 0, vrIntermediateColorOut[eyeIndex]->resource.get(), 0, &outputBox);
	if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage output copy"))
		return false;

	a_outputTexture = *a_inputTexture;
	a_outputTexture.eType = vr::TextureType_DirectX;
	a_outputBounds = a_inputBounds ? *a_inputBounds : vr::VRTextureBounds_t{ 0.0f, 0.0f, 1.0f, 1.0f };
	return true;
}

bool Upscaling::TryReplaceVanillaDynamicResolutionUpsample(const char* a_passName, DynamicResolutionUpsampleStage a_stage)
{
	auto state = globals::state;
	const auto upscaleMethod = GetRuntimeUpscaleMethod();
	if (!globals::game::isVR ||
		!IsVendorUpscalingMethod(upscaleMethod) ||
		!IsVRRenderScaleSubmitPathEnabled() ||
		!IsSubmitStageUpscalingActive() ||
		ShouldSuppressVRInSceneOverlaySubmit() ||
		IsCommunityShadersMenuOpen()) {
		return false;
	}
	if (!state)
		return false;
	if (IsVRMenuPresentationContextActive() ||
		IsVRRenderScaleMenuPreparationContextActive(state)) {
		return false;
	}

	if (IsVRTransitionPresentationProtectionActive(*this, state) &&
		IsVRLoadingPresentationContextActive(state)) {
		return false;
	}

	EnsureRuntimeResolutionStateCurrent();
	const auto& resolutionPlan = GetRuntimeResolutionPlan();
	if (resolutionPlan.owner != ResolutionOwner::VRRenderScaleMode ||
		resolutionPlan.outputTarget != UpscalingOutputTarget::SubmitStageIntermediate) {
		return false;
	}

	auto context = globals::d3d::context;
	if (!context)
		return false;

	uint32_t inputWidth = ClampPositiveDimension(resolutionPlan.engineRenderSize.x);
	uint32_t inputHeight = ClampPositiveDimension(resolutionPlan.engineRenderSize.y);
	uint32_t outputWidth = ClampPositiveDimension(resolutionPlan.finalOutputSize.x);
	uint32_t outputHeight = ClampPositiveDimension(resolutionPlan.finalOutputSize.y);
	if (!inputWidth || !inputHeight) {
		const auto dynamicRenderSize = Util::ConvertToDynamic(state->screenSize);
		inputWidth = ClampPositiveDimension(dynamicRenderSize.x);
		inputHeight = ClampPositiveDimension(dynamicRenderSize.y);
	}
	if (!outputWidth || !outputHeight) {
		outputWidth = ClampPositiveDimension(state->screenSize.x);
		outputHeight = ClampPositiveDimension(state->screenSize.y);
	}
	if (!inputWidth || !inputHeight)
		return false;
	if (inputWidth >= outputWidth && inputHeight >= outputHeight)
		return false;

	ID3D11ShaderResourceView* psSourceSRV = nullptr;
	ID3D11ShaderResourceView* csSourceSRV = nullptr;
	context->PSGetShaderResources(0, 1, &psSourceSRV);
	context->CSGetShaderResources(0, 1, &csSourceSRV);

	const auto releaseSourceSRVs = [&]() {
		if (psSourceSRV)
			psSourceSRV->Release();
		if (csSourceSRV)
			csSourceSRV->Release();
	};

	ID3D11ShaderResourceView* sourceSRV = nullptr;
	ID3D11Resource* sourceResource = nullptr;
	ID3D11Texture2D* sourceTexture = nullptr;
	ID3D11ShaderResourceView* sourceCandidates[2] = {};
	if (a_stage == DynamicResolutionUpsampleStage::Dispatch) {
		sourceCandidates[0] = csSourceSRV;
		sourceCandidates[1] = psSourceSRV;
	} else {
		sourceCandidates[0] = psSourceSRV;
		sourceCandidates[1] = csSourceSRV;
	}

	const auto tryAcquireSource = [&](ID3D11ShaderResourceView* a_candidateSRV) {
		if (!a_candidateSRV)
			return false;

		ID3D11Resource* candidateResource = nullptr;
		a_candidateSRV->GetResource(&candidateResource);
		if (!candidateResource)
			return false;

		ID3D11Texture2D* candidateTexture = nullptr;
		if (FAILED(candidateResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&candidateTexture))) || !candidateTexture) {
			candidateResource->Release();
			return false;
		}

		D3D11_TEXTURE2D_DESC candidateDesc{};
		candidateTexture->GetDesc(&candidateDesc);
		if (candidateDesc.Width < inputWidth || candidateDesc.Height < inputHeight) {
			candidateTexture->Release();
			candidateResource->Release();
			return false;
		}

		sourceSRV = a_candidateSRV;
		sourceResource = candidateResource;
		sourceTexture = candidateTexture;
		return true;
	};

	for (uint32_t i = 0; i < 2 && !sourceTexture; ++i) {
		if (!sourceCandidates[i])
			continue;
		if (i == 1 && sourceCandidates[i] == sourceCandidates[0])
			continue;
		(void)tryAcquireSource(sourceCandidates[i]);
	}

	const char* passName = a_passName ? a_passName : "dynamic-resolution pass";
	if (!sourceSRV || !sourceResource || !sourceTexture) {
		static bool loggedMissingSource = false;
		if (!loggedMissingSource) {
			logger::warn(
				"[Upscaling] {} replacement could not find a suitable source SRV t0 for {}x{}; falling back to vanilla pass.",
				passName,
				inputWidth,
				inputHeight);
			loggedMissingSource = true;
		}
		releaseSourceSRVs();
		return false;
	}

	ID3D11RenderTargetView* outputRTV = nullptr;
	ID3D11DepthStencilView* outputDSV = nullptr;
	context->OMGetRenderTargets(1, &outputRTV, &outputDSV);
	if (!outputRTV) {
		sourceTexture->Release();
		sourceResource->Release();
		releaseSourceSRVs();
		return false;
	}

	ID3D11Resource* outputResource = nullptr;
	outputRTV->GetResource(&outputResource);
	ID3D11Texture2D* outputTexture = nullptr;
	if (!outputResource || FAILED(outputResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&outputTexture))) || !outputTexture) {
		if (outputResource)
			outputResource->Release();
		outputRTV->Release();
		if (outputDSV)
			outputDSV->Release();
		sourceTexture->Release();
		sourceResource->Release();
		releaseSourceSRVs();
		return false;
	}

	const auto releaseRefs = [&]() {
		outputTexture->Release();
		outputResource->Release();
		outputRTV->Release();
		if (outputDSV)
			outputDSV->Release();
		sourceTexture->Release();
		sourceResource->Release();
		releaseSourceSRVs();
	};
	const auto unbindSourceSRV = [&]() {
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
		context->CSSetShaderResources(0, 1, &nullSRV);
	};
	const auto restoreSourceSRVs = [&]() {
		context->PSSetShaderResources(0, 1, &psSourceSRV);
		context->CSSetShaderResources(0, 1, &csSourceSRV);
	};

	// In-place/UI-target passes can carry late full-resolution HUD/interactions.
	// Let vanilla execute these to avoid submitting cropped low-res prompt frames.
	const bool inPlacePass = outputTexture == sourceTexture;
	const bool uiRenderTargetPass = IsVRPresentationRenderTargetTexture(sourceTexture) || IsVRPresentationRenderTargetTexture(outputTexture);
	const bool interactionUiContext = !IsKnownGameMenuContextActive();
	if ((inPlacePass || uiRenderTargetPass) && interactionUiContext) {
		releaseRefs();
		return false;
	}

	unbindSourceSRV();
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto copyDynamicRegionToTarget = [&](ID3D11Texture2D* a_targetTexture) {
		if (!a_targetTexture)
			return false;
		if (a_targetTexture == sourceTexture)
			return true;

		D3D11_TEXTURE2D_DESC sourceDesc{};
		D3D11_TEXTURE2D_DESC targetDesc{};
		sourceTexture->GetDesc(&sourceDesc);
		a_targetTexture->GetDesc(&targetDesc);
		if (sourceDesc.SampleDesc.Count != targetDesc.SampleDesc.Count ||
			sourceDesc.Format != targetDesc.Format ||
			inputWidth > sourceDesc.Width ||
			inputHeight > sourceDesc.Height ||
			inputWidth > targetDesc.Width ||
			inputHeight > targetDesc.Height) {
			static bool loggedCopyMismatch = false;
			if (!loggedCopyMismatch) {
				logger::warn(
					"[Upscaling] Submit-stage replacement could not copy source: input={}x{} source={}x{} fmt={} samples={} target={}x{} fmt={} samples={}",
					inputWidth,
					inputHeight,
					sourceDesc.Width,
					sourceDesc.Height,
					static_cast<uint32_t>(sourceDesc.Format),
					sourceDesc.SampleDesc.Count,
					targetDesc.Width,
					targetDesc.Height,
					static_cast<uint32_t>(targetDesc.Format),
					targetDesc.SampleDesc.Count);
				loggedCopyMismatch = true;
			}
			return false;
		}

		D3D11_BOX sourceBox{ 0, 0, 0, inputWidth, inputHeight, 1 };
		context->CopySubresourceRegion(a_targetTexture, 0, 0, 0, 0, sourceTexture, 0, &sourceBox);
		return true;
	};

	const bool copiedToOutput = copyDynamicRegionToTarget(outputTexture);
	if (copiedToOutput) {
		context->OMSetRenderTargets(1, &outputRTV, outputDSV);
		releaseRefs();
		if (globals::game::stateUpdateFlags)
			globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
		return true;
	}

	context->OMSetRenderTargets(1, &outputRTV, outputDSV);
	restoreSourceSRVs();
	releaseRefs();
	return false;
}

void Upscaling::RequestHistoryReset()
{
	historyResetRequested = true;
	if (auto* state = globals::state; state && historyResetLatchedFrame == state->frameCount)
		historyResetThisFrame = true;
}

uint32_t Upscaling::GetEffectiveUpscalingQualityMode() const
{
	const uint32_t pendingQualityMode = pendingVRUpscalingQualityMode.load(std::memory_order_acquire);
	return pendingQualityMode != kPendingVRUpscalingSettingUnset ? pendingQualityMode : settings.qualityMode;
}

uint32_t Upscaling::GetEffectiveDLSSQualityMode() const
{
	return GetEffectiveUpscalingQualityMode();
}

uint32_t Upscaling::GetEffectiveDLSSPreset() const
{
	const uint32_t pendingPreset = pendingVRDLSSPreset.load(std::memory_order_acquire);
	return pendingPreset != kPendingVRUpscalingSettingUnset ? pendingPreset : settings.dlssPreset;
}

void Upscaling::QueueVRUpscalingQualityMode(uint32_t a_qualityMode, VRUpscalingTransitionOrigin a_origin)
{
	pendingVRUpscalingQualityMode.store(std::min(a_qualityMode, kQualityModeMaxIndex), std::memory_order_release);
	MarkVRUpscalingTransitionQueued(a_origin);
}

void Upscaling::QueueVRRenderScaleModeRequest(bool a_enabled, VRUpscalingTransitionOrigin a_origin)
{
	pendingVRRenderScaleMode.store(a_enabled ? 1u : 0u, std::memory_order_release);
	MarkVRUpscalingTransitionQueued(a_origin);
}

void Upscaling::QueueVRDLSSPreset(uint32_t a_dlssPreset, VRUpscalingTransitionOrigin a_origin)
{
	pendingVRDLSSPreset.store(std::min(a_dlssPreset, kDLSSPresetMaxIndex), std::memory_order_release);
	MarkVRUpscalingTransitionQueued(a_origin);
}

void Upscaling::QueueVRPerfModeRequest(bool a_enabled, VRUpscalingTransitionOrigin a_origin)
{
	pendingVRPerfMode.store(a_enabled ? 1u : 0u, std::memory_order_release);
	MarkVRUpscalingTransitionQueued(a_origin);
}

void Upscaling::MarkVRUpscalingTransitionQueued(VRUpscalingTransitionOrigin a_origin)
{
	InvalidateFrameScopedUpscalingState();
	const uint32_t frame = globals::state ? std::max(globals::state->frameCount, 1u) : 1u;
	const bool transitionAlreadyQueued = pendingVRUpscalingTransitionFrame.load(std::memory_order_acquire) != 0;
	const auto currentOrigin = LoadVRUpscalingTransitionOrigin(pendingVRUpscalingTransitionOrigin);
	if (ShouldStoreVRUpscalingTransitionOrigin(currentOrigin, a_origin, transitionAlreadyQueued)) {
		pendingVRUpscalingTransitionOrigin.store(static_cast<uint32_t>(a_origin), std::memory_order_release);
	}
	pendingVRUpscalingTransitionFrame.store(frame, std::memory_order_release);
}

void Upscaling::ClearPendingVRUpscalingTransition()
{
	pendingVRUpscalingQualityMode.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
	pendingVRRenderScaleMode.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
	pendingVRDLSSPreset.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
	pendingVRPerfMode.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
	pendingVRUpscalingTransitionFrame.store(0, std::memory_order_release);
	pendingVRUpscalingTransitionOrigin.store(static_cast<uint32_t>(VRUpscalingTransitionOrigin::CSMenu), std::memory_order_release);
	InvalidateFrameScopedUpscalingState();
}

bool Upscaling::HasPendingVRUpscalingTransition() const
{
	return pendingVRUpscalingQualityMode.load(std::memory_order_acquire) != kPendingVRUpscalingSettingUnset ||
	       pendingVRRenderScaleMode.load(std::memory_order_acquire) != kPendingVRUpscalingSettingUnset ||
	       pendingVRDLSSPreset.load(std::memory_order_acquire) != kPendingVRUpscalingSettingUnset ||
	       pendingVRPerfMode.load(std::memory_order_acquire) != kPendingVRUpscalingSettingUnset;
}

bool Upscaling::HasPendingVRRenderScaleTransition() const
{
	const uint32_t pendingRenderScaleMode = pendingVRRenderScaleMode.load(std::memory_order_acquire);
	const uint32_t pendingPerfMode = pendingVRPerfMode.load(std::memory_order_acquire);
	const uint32_t pendingQualityMode = pendingVRUpscalingQualityMode.load(std::memory_order_acquire);
	const bool hasPendingRenderScaleSetting =
		pendingRenderScaleMode != kPendingVRUpscalingSettingUnset ||
		pendingPerfMode != kPendingVRUpscalingSettingUnset ||
		pendingQualityMode != kPendingVRUpscalingSettingUnset;
	if (!hasPendingRenderScaleSetting)
		return false;

	return IsVRRenderScaleCurrentOrTargetRelevant(*this);
}

bool Upscaling::ShouldStageVRRenderScaleTransition(bool a_renderScaleModeEnabled, uint32_t a_qualityMode) const
{
	if (!globals::game::isVR || !IsRenderScaleMethodEligible(GetConfiguredUpscaleMethodForTransition()))
		return false;

	const uint32_t qualityMode = std::min(a_qualityMode, kQualityModeMaxIndex);
	const uint32_t effectiveQualityMode = GetEffectiveUpscalingQualityMode();
	const bool currentRenderScaleMode =
		IsRenderScaleModeRequested() &&
		IsRenderScaleQualityMode(effectiveQualityMode);
	const bool targetRenderScaleQuality = IsRenderScaleQualityMode(qualityMode);
	const bool targetRenderScaleMode = a_renderScaleModeEnabled && targetRenderScaleQuality;
	const bool targetPerfMode = targetRenderScaleMode;
	const bool currentPerfMode =
		GetPerfModeRequested() ||
		IsVRRenderScaleModeLatched() ||
		perfMode.HasRestartRequiredChange();

	if (currentRenderScaleMode != targetRenderScaleMode)
		return true;

	if (effectiveQualityMode != qualityMode && (currentRenderScaleMode || targetRenderScaleMode || IsVRRenderScaleModeLatched() || perfMode.HasRestartRequiredChange()))
		return true;

	return currentPerfMode != targetPerfMode;
}

bool Upscaling::ShouldDeferVRUpscalingTransitionSettings() const
{
	if (!globals::game::isVR)
		return false;

	if (postLoadRuntimeResetPending.load(std::memory_order_acquire))
		return true;

	const auto* state = globals::state;
	if (!state)
		return false;

	return state->pendingPostLoadRuntimeReset ||
	       IsCommunityShadersMenuOpen() ||
	       IsKnownGameMenuContextActive();
}

bool Upscaling::ShouldWaitForVRUpscalingTransitionDelay() const
{
	if (!HasPendingVRRenderScaleTransition())
		return false;

	const uint32_t queuedFrame = pendingVRUpscalingTransitionFrame.load(std::memory_order_acquire);
	if (queuedFrame == 0)
		return false;

	const uint32_t currentFrame = globals::state ? std::max(globals::state->frameCount, 1u) : queuedFrame;
	return currentFrame - queuedFrame < kVRUpscalingTransitionApplyDelayFrames;
}

void Upscaling::MarkPerfModeRenderTargetRecreateQueued(uint32_t a_delayFrames)
{
	const uint32_t frame = globals::state ? std::max(globals::state->frameCount, 1u) : 1u;
	const uint32_t delayFrames = a_delayFrames != 0 ? a_delayFrames : kVRUpscalingTransitionApplyDelayFrames;
	pendingPerfModeRenderTargetRecreateFrame.store(frame, std::memory_order_release);
	const uint32_t previousDelay = pendingPerfModeRenderTargetRecreateDelayFrames.load(std::memory_order_acquire);
	if (previousDelay == 0 || delayFrames > previousDelay)
		pendingPerfModeRenderTargetRecreateDelayFrames.store(delayFrames, std::memory_order_release);
}

bool Upscaling::ShouldWaitForPerfModeRenderTargetRecreateDelay() const
{
	const uint32_t queuedFrame = pendingPerfModeRenderTargetRecreateFrame.load(std::memory_order_acquire);
	if (queuedFrame == 0)
		return false;

	uint32_t delayFrames = pendingPerfModeRenderTargetRecreateDelayFrames.load(std::memory_order_acquire);
	if (delayFrames == 0)
		delayFrames = kVRUpscalingTransitionApplyDelayFrames;

	const uint32_t currentFrame = globals::state ? std::max(globals::state->frameCount, 1u) : queuedFrame;
	return currentFrame - queuedFrame < delayFrames;
}

void Upscaling::ApplyPendingVRUpscalingTransition(UpscaleMethod a_upscaleMethod)
{
	if (!globals::game::isVR)
		return;

	if (!HasPendingVRUpscalingTransition())
		return;

	if (ShouldDeferVRUpscalingTransitionSettings()) {
		MarkVRUpscalingTransitionQueued(LoadVRUpscalingTransitionOrigin(pendingVRUpscalingTransitionOrigin));
		return;
	}

	if (ShouldWaitForVRUpscalingTransitionDelay())
		return;

	const uint32_t pendingQualityMode = pendingVRUpscalingQualityMode.exchange(kPendingVRUpscalingSettingUnset, std::memory_order_acq_rel);
	const uint32_t pendingRenderScaleMode = pendingVRRenderScaleMode.exchange(kPendingVRUpscalingSettingUnset, std::memory_order_acq_rel);
	const uint32_t pendingPreset = pendingVRDLSSPreset.exchange(kPendingVRUpscalingSettingUnset, std::memory_order_acq_rel);
	const uint32_t pendingPerfMode = pendingVRPerfMode.exchange(kPendingVRUpscalingSettingUnset, std::memory_order_acq_rel);
	const auto transitionOrigin = LoadVRUpscalingTransitionOrigin(pendingVRUpscalingTransitionOrigin);
	pendingVRUpscalingTransitionFrame.store(0, std::memory_order_release);
	pendingVRUpscalingTransitionOrigin.store(static_cast<uint32_t>(VRUpscalingTransitionOrigin::CSMenu), std::memory_order_release);
	bool changed = false;
	bool qualityChanged = false;
	bool renderScaleModeChanged = false;

	if (!IsRenderScaleMethodEligible(a_upscaleMethod)) {
		if (settings.renderScaleMode != 0) {
			settings.renderScaleMode = 0;
			renderScaleModeChanged = true;
		}

		const bool perfModePending = pendingPerfMode != kPendingVRUpscalingSettingUnset;
		if (perfModePending || ClampToggleUInt(settings.perfMode) != 0 || IsVRRenderScaleModeLatched() || perfMode.HasRestartRequiredChange())
			SetPerfModeRequested(false, "VR upscaling deferred transition", false, transitionOrigin);

		if (renderScaleModeChanged) {
			InvalidateFrameScopedUpscalingState();
			RequestHistoryReset();
		}
		return;
	}

	const bool qualityPending = pendingQualityMode != kPendingVRUpscalingSettingUnset;
	const bool renderScaleModePending = pendingRenderScaleMode != kPendingVRUpscalingSettingUnset;
	const uint32_t targetQualityMode = qualityPending ? pendingQualityMode : settings.qualityMode;
	bool targetRenderScaleMode =
		renderScaleModePending ?
			pendingRenderScaleMode != 0 :
			ClampToggleUInt(settings.renderScaleMode) != 0;
	targetRenderScaleMode = targetRenderScaleMode && IsRenderScaleQualityMode(targetQualityMode);

	if (qualityPending || renderScaleModePending) {
		const uint32_t requestedRenderScaleMode = targetRenderScaleMode ? 1u : 0u;
		if (settings.renderScaleMode != requestedRenderScaleMode) {
			settings.renderScaleMode = requestedRenderScaleMode;
			renderScaleModeChanged = true;
		}
	}

	if (pendingQualityMode != kPendingVRUpscalingSettingUnset) {
		if (settings.qualityMode != pendingQualityMode) {
			settings.qualityMode = pendingQualityMode;
			qualityChanged = true;
			changed = true;
		}
	}

	if (a_upscaleMethod == UpscaleMethod::kDLSS &&
		pendingPreset != kPendingVRUpscalingSettingUnset &&
		settings.dlssPreset != pendingPreset) {
		settings.dlssPreset = pendingPreset;
		changed = true;
	}

	const bool perfModePending = pendingPerfMode != kPendingVRUpscalingSettingUnset;
	if (perfModePending) {
		const bool targetPerfMode = pendingPerfMode != 0 && targetRenderScaleMode;
		SetPerfModeRequested(targetPerfMode, "VR upscaling deferred transition", false, transitionOrigin);
		const auto& perfModeBoot = perfMode.GetBootSnapshot();
		if (targetPerfMode &&
			perfModeBoot.valid &&
			perfModeBoot.active &&
			perfModeBoot.method != a_upscaleMethod) {
			RequestPerfModeRenderTargetRecreate("VR upscaling method change", transitionOrigin);
		}
	} else if (!targetRenderScaleMode && ClampToggleUInt(settings.perfMode) != 0) {
		SetPerfModeRequested(false, "VR upscaling deferred transition", false, transitionOrigin);
	}

	if (changed || renderScaleModeChanged) {
		InvalidateFrameScopedUpscalingState();
		RequestHistoryReset();
		if (a_upscaleMethod == UpscaleMethod::kDLSS)
			pendingDLSSHistoryReset.store(true, std::memory_order_release);
		if ((qualityChanged || renderScaleModeChanged) && !perfModePending && (IsVRRenderScaleModeLatched() || GetPerfModeRequested()))
			RequestPerfModeRenderTargetRecreate("VR upscaling preset change", transitionOrigin);
	}
}

bool Upscaling::ShouldResetHistoryThisFrame() const
{
	return historyResetThisFrame;
}

void Upscaling::LatchHistoryResetForCurrentFrame()
{
	const uint32_t frame = globals::state ? globals::state->frameCount : 0;
	if (historyResetLatchedFrame == frame)
		return;

	historyResetLatchedFrame = frame;
	historyResetThisFrame = historyResetRequested;
	historyResetRequested = false;
}

void Upscaling::UpdateHistoryResetState(UpscaleMethod a_upscaleMethod)
{
	auto state = globals::state;
	if (!state)
		return;

	const bool inWorld = state->inWorld;
	const bool inMapMenu = globals::game::ui ? globals::game::ui->IsMenuOpen(RE::MapMenu::MENU_NAME) : false;
	const float2 screenSize = state->screenSize;
	EnsureRuntimeResolutionStateCurrent();
	float2 engineRenderSize = runtimeResolutionPlan.engineRenderSize;
	float2 finalOutputSize = runtimeResolutionPlan.finalOutputSize;
	if (engineRenderSize.x <= 0.0f || engineRenderSize.y <= 0.0f)
		engineRenderSize = Util::ConvertToDynamic(screenSize);
	if (finalOutputSize.x <= 0.0f || finalOutputSize.y <= 0.0f)
		finalOutputSize = screenSize;
	const auto resolutionOwner = runtimeResolutionPlan.owner;
	const bool foveatedDispatchEnabled = IsFoveatedVendorDispatchEnabled(a_upscaleMethod);
	const bool peripheryTAAEnabled = IsPeripheryTAAEnabled(a_upscaleMethod);
	const bool peripheryTAAPathActive = IsPeripheryTAAPathActive(a_upscaleMethod);
	const bool fsrRuntimePathActive = IsFSRRuntimePathActive(a_upscaleMethod);
	const bool fsrRuntimeFsr4Active = IsFSRRuntimeFsr4PathActive(a_upscaleMethod);
	const uint32_t qualityMode = GetRuntimeQualityMode();
	const auto foveatedProfile = GetFoveatedMaskProfileParams(settings, peripheryTAAEnabled);
	const float foveatedCenterScale = foveatedProfile.centerScale;
	const float foveatedCenterHorizontalScale = foveatedProfile.centerHorizontalScale;
	const float peripheryTAACenterBlendFeather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);
	const float peripheryTAAOuterScale = ClampPeripheryTAAOuterScaleForCenter(
		settings.periphery_taa_outer_scale,
		foveatedCenterScale);
	const auto foveatedCenterOffsets = GetResolvedFoveatedMaskCenterOffsets(peripheryTAAEnabled);

	auto cameraCutDetected = []() {
		constexpr float kCameraCutDistanceThreshold = 2500.0f;  // ~35m teleport/cut in Skyrim units
		const float cutDistanceSq = kCameraCutDistanceThreshold * kCameraCutDistanceThreshold;

		auto exceededThreshold = [&](uint32_t eyeIndex) {
			const auto& currentPos = globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
			const auto& previousPos = globals::game::frameBufferCached.GetCameraPreviousPosAdjust(eyeIndex);
			const float dx = currentPos.x - previousPos.x;
			const float dy = currentPos.y - previousPos.y;
			const float dz = currentPos.z - previousPos.z;
			return (dx * dx + dy * dy + dz * dz) > cutDistanceSq;
		};

		if (globals::game::isVR)
			return exceededThreshold(0) || exceededThreshold(1);
		return exceededThreshold(0);
	};

	bool shouldReset = false;
	if (!historyResetTrackingInitialized) {
		shouldReset = true;
		historyResetTrackingInitialized = true;
	} else {
		const bool screenSizeChanged =
			std::abs(screenSize.x - previousHistoryScreenSize.x) > 0.5f ||
			std::abs(screenSize.y - previousHistoryScreenSize.y) > 0.5f;
		const bool scaleChanged =
			std::abs(resolutionScale.x - previousHistoryResolutionScale.x) > 1e-4f ||
			std::abs(resolutionScale.y - previousHistoryResolutionScale.y) > 1e-4f;
		const bool engineRenderSizeChanged =
			std::abs(engineRenderSize.x - previousHistoryEngineRenderSize.x) > 0.5f ||
			std::abs(engineRenderSize.y - previousHistoryEngineRenderSize.y) > 0.5f;
		const bool finalOutputSizeChanged =
			std::abs(finalOutputSize.x - previousHistoryFinalOutputSize.x) > 0.5f ||
			std::abs(finalOutputSize.y - previousHistoryFinalOutputSize.y) > 0.5f;
		const bool resolutionOwnerChanged = resolutionOwner != previousHistoryResolutionOwner;
		const bool qualityModeChanged = qualityMode != previousHistoryQualityMode;
		const bool worldStateChanged =
			inWorld != previousHistoryInWorld ||
			inMapMenu != previousHistoryInMapMenu;
		const bool methodChanged = a_upscaleMethod != previousHistoryUpscaleMethod;
		const bool fsrRuntimePathChanged = fsrRuntimePathActive != previousHistoryFSRRuntimePathActive;
		const bool fsrRuntimeVersionChanged =
			(fsrRuntimePathActive || previousHistoryFSRRuntimePathActive) &&
			fsrRuntimeFsr4Active != previousHistoryFSRRuntimeFsr4Active;
		const bool compareFoveatedScale = foveatedDispatchEnabled || previousHistoryFoveatedDispatch;
		const bool foveatedOffsetsChanged =
			compareFoveatedScale &&
			(std::abs(foveatedCenterOffsets[0].x - previousHistoryFoveatedCenterOffsets[0].x) > 1e-4f ||
				std::abs(foveatedCenterOffsets[0].y - previousHistoryFoveatedCenterOffsets[0].y) > 1e-4f ||
				std::abs(foveatedCenterOffsets[1].x - previousHistoryFoveatedCenterOffsets[1].x) > 1e-4f ||
				std::abs(foveatedCenterOffsets[1].y - previousHistoryFoveatedCenterOffsets[1].y) > 1e-4f);
		const bool foveatedChanged =
			foveatedDispatchEnabled != previousHistoryFoveatedDispatch ||
			(compareFoveatedScale && std::abs(foveatedCenterScale - previousHistoryFoveatedCenterScale) > 1e-4f) ||
			(compareFoveatedScale && std::abs(foveatedCenterHorizontalScale - previousHistoryFoveatedCenterHorizontalScale) > 1e-4f) ||
			foveatedOffsetsChanged;
		const bool longFrameGap = globals::game::deltaTime &&
		                          std::isfinite(*globals::game::deltaTime) &&
		                          *globals::game::deltaTime > 0.20f;
		const bool cameraCut = inWorld && cameraCutDetected();

		const bool effectivePeripheryTAAChanged =
			peripheryTAAEnabled != previousHistoryPeripheryTAA ||
			peripheryTAAPathActive != previousHistoryPeripheryTAAPathActive ||
			(peripheryTAAPathActive && (std::abs(peripheryTAAOuterScale - previousHistoryPeripheryTAAOuterScale) > 1e-4f ||
										   std::abs(peripheryTAACenterBlendFeather - previousHistoryPeripheryTAACenterBlendFeather) > 1e-4f));

		shouldReset =
			screenSizeChanged ||
			scaleChanged ||
			engineRenderSizeChanged ||
			finalOutputSizeChanged ||
			resolutionOwnerChanged ||
			qualityModeChanged ||
			worldStateChanged ||
			methodChanged ||
			fsrRuntimePathChanged ||
			fsrRuntimeVersionChanged ||
			foveatedChanged ||
			effectivePeripheryTAAChanged ||
			longFrameGap ||
			cameraCut;
	}

	if (state->pendingPostLoadRuntimeReset)
		shouldReset = true;

	if (shouldReset)
		RequestHistoryReset();

	previousHistoryScreenSize = screenSize;
	previousHistoryResolutionScale = resolutionScale;
	previousHistoryEngineRenderSize = engineRenderSize;
	previousHistoryFinalOutputSize = finalOutputSize;
	previousHistoryResolutionOwner = resolutionOwner;
	previousHistoryQualityMode = qualityMode;
	previousHistoryInWorld = inWorld;
	previousHistoryInMapMenu = inMapMenu;
	previousHistoryUpscaleMethod = a_upscaleMethod;
	previousHistoryFoveatedDispatch = foveatedDispatchEnabled;
	previousHistoryFoveatedCenterScale = foveatedCenterScale;
	previousHistoryFoveatedCenterHorizontalScale = foveatedCenterHorizontalScale;
	previousHistoryFoveatedCenterOffsets = foveatedCenterOffsets;
	previousHistoryPeripheryTAA = peripheryTAAEnabled;
	previousHistoryPeripheryTAAPathActive = peripheryTAAPathActive;
	previousHistoryPeripheryTAAOuterScale = peripheryTAAOuterScale;
	previousHistoryPeripheryTAACenterBlendFeather = peripheryTAACenterBlendFeather;
	previousHistoryFSRRuntimePathActive = fsrRuntimePathActive;
	previousHistoryFSRRuntimeFsr4Active = fsrRuntimeFsr4Active;
}

/**
 * @brief Retrieves the current frame time for frame generation.
 *
 * Returns the frame time from the D3D12 swap chain if frame generation is active; otherwise, returns 0.
 *
 * @return float The current frame time in seconds, or 0 if frame generation is inactive.
 */
float Upscaling::GetFrameGenerationFrameTime() const
{
	if (!IsFrameGenerationActive())
		return 0.0f;

	// Get the current frame time from D3D12 swapchain
	if (dx12SwapChain.swapChain) {
		// Get frame time from the D3D12 SwapChain
		return GetFrameTime();
	}

	return 0.0f;
}

// Unified interface methods
void Upscaling::LoadUpscalingSDKs()
{
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		if (!openCompositeUpscalingBackendSkipLogged) {
			if (blocker.configPath.empty()) {
				logger::warn(
					"[Upscaling] Skipping Community Shaders Streamline/FidelityFX backend initialization because Open Composite has {}=true.",
					blocker.settingName);
			} else {
				logger::warn(
					"[Upscaling] Skipping Community Shaders Streamline/FidelityFX backend initialization because Open Composite has {}=true in {}.",
					blocker.settingName,
					blocker.configPath);
			}
			openCompositeUpscalingBackendSkipLogged = true;
		}
		return;
	}
	if (IsRenderDocUpscalingBlocked(true)) {
		if (!renderDocUpscalingBackendSkipLogged) {
			logger::warn(
				"[Upscaling] Skipping Community Shaders Streamline/FidelityFX backend initialization because {}.",
				GetRenderDocUpscalingBlockReason());
			renderDocUpscalingBackendSkipLogged = true;
		}
		return;
	}

	// Initialize upscaling SDK components during plugin startup
	// This ensures all SDKs are available before any D3D device creation
	streamline.LoadInterposer();
	fidelityFX.LoadFFX();
	if (streamline.featureCheckComplete)
		CompleteDelayedVRPerfModeBootLatchForDLSSAvailability(*this, "Streamline DLSS availability resolved after deferred VR boot latch");
}

void Upscaling::SetUIBuffer()
{
	dx12SwapChain.SetUIBuffer();
}

HANDLE Upscaling::GetFrameLatencyWaitableObject() const
{
	return dx12SwapChain.GetFrameLatencyWaitableObject();
}

float Upscaling::GetFrameTime() const
{
	return dx12SwapChain.GetFrameTime();
}

// Backend interface methods
bool Upscaling::IsBackendInitialized() const
{
	if (IsRenderDocUpscalingBlocked())
		return false;

	return streamline.initialized;
}

void Upscaling::CheckBackendFeatures(IDXGIAdapter* adapter)
{
	if (IsRenderDocUpscalingBlocked())
		return;

	streamline.CheckFeatures(adapter);
	CompleteDelayedVRPerfModeBootLatchForDLSSAvailability(*this, "Streamline DLSS availability resolved after deferred VR boot latch");
}

void Upscaling::UpgradeBackendInterface(void** ppInterface)
{
	if (IsRenderDocUpscalingBlocked())
		return;

	streamline.slUpgradeInterface(ppInterface);
}

void Upscaling::SetBackendD3DDevice(ID3D11Device* device)
{
	if (IsRenderDocUpscalingBlocked())
		return;

	submitStageDeviceLost.store(false, std::memory_order_release);
	ClearSubmitStageVendorResumeCooldown();
	ClearSubmitStageFoveatedVendorRetryBackoff();
	streamline.ResetDLSSIdleFences();
	streamline.slSetD3DDevice(device);
}

void Upscaling::PostBackendDevice()
{
	if (IsRenderDocUpscalingBlocked())
		return;

	streamline.PostDevice();
}

// Module availability methods
bool Upscaling::HasFrameGenModule() const
{
	if (IsRenderDocUpscalingBlocked())
		return false;

	return fidelityFX.featureFSR3FG;
}

// Proxy interface methods
void Upscaling::SetProxyD3D11Device(ID3D11Device* device)
{
	dx12SwapChain.SetD3D11Device(device);
}

void Upscaling::SetProxyD3D11DeviceContext(ID3D11DeviceContext* context)
{
	dx12SwapChain.SetD3D11DeviceContext(context);
}

void Upscaling::CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc)
{
	dx12SwapChain.CreateSwapChain(adapter, swapChainDesc);
}

void Upscaling::CreateProxyInterop()
{
	dx12SwapChain.CreateInterop();
}

IDXGISwapChain* Upscaling::GetProxySwapChain()
{
	return dx12SwapChain.GetSwapChainProxy();
}

bool Upscaling::IsOpenCompositeUpscalingBlocked(bool a_forceRefresh) const
{
	return GetOpenCompositeUpscalingBlocker(a_forceRefresh).active;
}

void Upscaling::Upscale()
{
	ZoneScoped;
	auto upscaleMethod = GetRuntimeUpscaleMethod();
	dlssUpscaleOutputInSharpenerTexture = false;

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto deferred = globals::deferred;
	if (!state || !context || !renderer || !deferred)
		return;

	if (globals::game::isVR && upscaleMethod == UpscaleMethod::kDLSS && pendingDLSSHistoryReset.exchange(false, std::memory_order_relaxed)) {
		logger::debug("[Upscaling] Resetting DLSS history after VR option/load transition");
		RequestHistoryReset();
	}

	UpdateHistoryResetState(upscaleMethod);
	LatchHistoryResetForCurrentFrame();

	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	const bool requiresEncodedMotionVectors = upscaleMethod == UpscaleMethod::kDLSS || upscaleMethod == UpscaleMethod::kFSR;
	const bool requiresCombinedEncodedMotionVectors = requiresEncodedMotionVectors && !globals::game::isVR;
	if (requiresCombinedEncodedMotionVectors && (!motionVectorCopyTexture || !motionVectorCopyTexture->uav || !motionVectorCopyTexture->resource)) {
		logger::error("[Upscaling] Missing encoded motion-vector resources for method {}", magic_enum::enum_name(upscaleMethod));
		return;
	}

	auto dispatchCount = Util::GetScreenDispatchCount(true);
	const bool foveatedTransitionBypass = ShouldBypassVRFoveatedVendorDispatchForTransition(*this, state);
	const bool foveatedDispatchRequested =
		IsFoveatedVendorDispatchEnabled(upscaleMethod) && !foveatedTransitionBypass;
	bool encodedVRFoveatedRegions = false;

	auto encodeUpscalingTextures = [&](bool forceFullVREncode, const char* eventName) -> bool {
		encodedVRFoveatedRegions = false;
		state->BeginPerfEvent(eventName);
		auto perfEventGuard = ScopeExit([&]() {
			state->EndPerfEvent();
		});
		TracyD3D11Zone(state->tracyCtx, "Encode Upscaling Textures");
		Profiler::ScopedPass profile(globals::profiler, forceFullVREncode ? "Upscaling::EncodeTexturesFallbackFull" : "Upscaling::EncodeTextures");

		auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
		auto& normals = renderer->GetRuntimeData().renderTargets[deferred->forwardRenderTargets[2]];
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto* encodeShader = GetEncodeTexturesCS();
		if (!temporalAAMask.SRV || !normals.SRV || !motionVector.SRV || !motionVector.texture || !depth.depthSRV || !encodeShader ||
			!upscalingDataCB || !main.texture || !reactiveMaskTexture || !reactiveMaskTexture->resource || !reactiveMaskTexture->uav ||
			!transparencyCompositionMaskTexture || !transparencyCompositionMaskTexture->resource || !transparencyCompositionMaskTexture->uav)
			return false;

		auto outputSize = runtimeResolutionPlan.finalOutputSize;
		auto renderSize = runtimeResolutionPlan.engineRenderSize;
		if (outputSize.x <= 0.0f || outputSize.y <= 0.0f)
			outputSize = state->screenSize;
		if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
			renderSize = Util::ConvertToDynamic(state->screenSize);
		if (outputSize.x <= 0.0f || outputSize.y <= 0.0f || renderSize.x <= 0.0f || renderSize.y <= 0.0f)
			return false;

		ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, motionVector.SRV, depth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		auto upscalingBuffer = upscalingDataCB->CB();
		context->CSSetConstantBuffers(0, 1, &upscalingBuffer);
		context->CSSetShader(encodeShader, nullptr, 0);
		auto cleanupEncodeState = ScopeExit([&]() {
			ID3D11ShaderResourceView* nullSRV[4] = { nullptr, nullptr, nullptr, nullptr };
			context->CSSetShaderResources(0, ARRAYSIZE(nullSRV), nullSRV);

			ID3D11UnorderedAccessView* nullUAV[4] = { nullptr, nullptr, nullptr, nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAV), nullUAV, nullptr);

			ID3D11Buffer* nullBuffer = nullptr;
			context->CSSetConstantBuffers(0, 1, &nullBuffer);
			context->CSSetShader(nullptr, nullptr, 0);
		});

		if (globals::game::isVR) {
			const uint32_t eyeWidthOut = static_cast<uint32_t>(outputSize.x / 2);
			const uint32_t eyeHeightOut = static_cast<uint32_t>(outputSize.y);
			const uint32_t eyeWidthIn = static_cast<uint32_t>(renderSize.x / 2);
			const uint32_t eyeHeightIn = static_cast<uint32_t>(renderSize.y);
			if (!eyeWidthIn || !eyeHeightIn || !eyeWidthOut || !eyeHeightOut)
				return false;
			const auto inputStereoLayout = ResolveVRSideBySideStereoLayout(eyeWidthIn, eyeHeightIn);
			if (!inputStereoLayout.IsValid())
				return false;

			try {
				EnsureVRIntermediateTextures(eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut,
					main.texture, motionVector.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get());
			} catch (const std::exception& e) {
				logger::warn("[Upscaling] Failed to create VR encode intermediates: {}", e.what());
				return false;
			} catch (...) {
				logger::warn("[Upscaling] Failed to create VR encode intermediates.");
				return false;
			}

			for (uint32_t eye = 0; eye < 2; ++eye) {
				if (!vrIntermediateMotionVectors[eye] || !vrIntermediateMotionVectors[eye]->uav ||
					!vrIntermediateReactiveMask[eye] || !vrIntermediateReactiveMask[eye]->uav ||
					!vrIntermediateTransparencyMask[eye] || !vrIntermediateTransparencyMask[eye]->uav ||
					(upscaleMethod == UpscaleMethod::kFSR && (!vrIntermediateLinearDepth[eye] || !vrIntermediateLinearDepth[eye]->uav))) {
					return false;
				}
			}

			auto dispatchEyeEncode = [&](uint32_t eye, uint32_t inputMinX, uint32_t inputMinY, uint32_t inputMaxX, uint32_t inputMaxY) {
				if (eye >= 2 || inputMaxX <= inputMinX || inputMaxY <= inputMinY)
					return;
				const auto& sourceEyeRegion = inputStereoLayout.eyes[eye];

				inputMinX = std::min(inputMinX, eyeWidthIn);
				inputMinY = std::min(inputMinY, eyeHeightIn);
				inputMaxX = std::min(inputMaxX, eyeWidthIn);
				inputMaxY = std::min(inputMaxY, eyeHeightIn);
				if (inputMaxX <= inputMinX || inputMaxY <= inputMinY)
					return;

				const uint32_t dispatchWidth = inputMaxX - inputMinX;
				const uint32_t dispatchHeight = inputMaxY - inputMinY;
				UpscalingDataCB upscalingData{};
				upscalingData.dispatchDim = { static_cast<float>(dispatchWidth), static_cast<float>(dispatchHeight) };
				upscalingData.trueSamplingDim = renderSize;
				upscalingData.invTrueSamplingDim = { renderSize.x > 0.0f ? 1.0f / renderSize.x : 0.0f, renderSize.y > 0.0f ? 1.0f / renderSize.y : 0.0f };
				upscalingData.seamCenterX = renderSize.x * 0.5f;
				upscalingData.seamHalfWidthPx = 2.0f;
				upscalingData.maskDepthThreshold = 1e-6f;
				upscalingData.vrSeamHardening = 1.0f;
				upscalingData.sourceOffset = { static_cast<float>(sourceEyeRegion.minX + inputMinX), static_cast<float>(inputMinY) };
				upscalingData.outputOffset = { static_cast<float>(inputMinX), static_cast<float>(inputMinY) };
				upscalingDataCB->Update(upscalingData);

				ID3D11UnorderedAccessView* uavs[4] = {
					vrIntermediateReactiveMask[eye]->uav.get(),
					vrIntermediateTransparencyMask[eye]->uav.get(),
					vrIntermediateMotionVectors[eye]->uav.get(),
					(upscaleMethod == UpscaleMethod::kFSR) ? vrIntermediateLinearDepth[eye]->uav.get() : nullptr
				};
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
				context->Dispatch((dispatchWidth + 7u) >> 3, (dispatchHeight + 7u) >> 3, 1);
			};

			auto dispatchFullEyes = [&]() {
				for (uint32_t eye = 0; eye < 2; ++eye) {
					dispatchEyeEncode(eye, 0, 0, eyeWidthIn, eyeHeightIn);
				}
			};

			const bool useRegionEncode = !forceFullVREncode && foveatedDispatchRequested;
			bool dispatchedRegionEncode = false;
			if (useRegionEncode) {
				const bool usePeripheryTAAProfile = IsPeripheryTAAEnabled(upscaleMethod);
				const bool usePeripheryTAAPath = IsPeripheryTAAPathActive(upscaleMethod);
				std::array<FoveatedEncodeRegion, 2> regions{};
				if (GetFoveatedEncodeRegions(eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut, usePeripheryTAAProfile, usePeripheryTAAPath, regions)) {
					for (uint32_t eye = 0; eye < 2; ++eye) {
						dispatchEyeEncode(eye, regions[eye].minX, regions[eye].minY, regions[eye].maxX, regions[eye].maxY);
					}
					dispatchedRegionEncode = true;
					encodedVRFoveatedRegions = true;
				}
			}

			if (!dispatchedRegionEncode) {
				dispatchFullEyes();
			}
		} else {
			UpscalingDataCB upscalingData{};
			upscalingData.dispatchDim = renderSize;
			upscalingData.trueSamplingDim = renderSize;
			upscalingData.invTrueSamplingDim = { renderSize.x > 0.0f ? 1.0f / renderSize.x : 0.0f, renderSize.y > 0.0f ? 1.0f / renderSize.y : 0.0f };
			upscalingData.seamCenterX = renderSize.x * 0.5f;
			upscalingData.seamHalfWidthPx = 2.0f;
			upscalingData.maskDepthThreshold = 1e-6f;
			upscalingData.vrSeamHardening = 0.0f;
			upscalingData.sourceOffset = { 0.0f, 0.0f };
			upscalingData.outputOffset = { 0.0f, 0.0f };
			upscalingDataCB->Update(upscalingData);

			ID3D11UnorderedAccessView* uavs[4] = {
				reactiveMaskTexture->uav.get(),
				transparencyCompositionMaskTexture->uav.get(),
				requiresCombinedEncodedMotionVectors ? motionVectorCopyTexture->uav.get() : nullptr,
				nullptr
			};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		return true;
	};

	if (!encodeUpscalingTextures(false, "Encode Upscaling Textures"))
		return;

	{
		state->BeginPerfEvent("Upscaling");
		ID3D11Resource* motionVectorResource = globals::game::isVR ? motionVector.texture : motionVectorCopyTexture->resource.get();
		bool dispatched = false;
		static bool loggedFoveatedFallback = false;
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling Dispatch");

		// VR-only resets can leave vendor upscalers with stale viewport state.
		if (!ApplyPendingVendorRuntimeReset(upscaleMethod, ""))
			return;

		if (foveatedDispatchRequested) {
			auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			const bool foveatedOutputToSharpener =
				upscaleMethod == UpscaleMethod::kDLSS &&
				ShouldApplyDLSSSharpening() &&
				sharpenerTexture &&
				sharpenerTexture->resource &&
				sharpenerTexture->srv &&
				main.UAV;
			ID3D11Resource* foveatedOutput = foveatedOutputToSharpener ? sharpenerTexture->resource.get() : main.texture;
			{
				CS_PROFILE_SCOPE("Upscaling::Upscale");
				dispatched = DispatchFoveatedVendorUpscaling(
					upscaleMethod,
					main.texture,
					depth.texture,
					motionVectorResource,
					reactiveMaskTexture->resource.get(),
					transparencyCompositionMaskTexture->resource.get(),
					foveatedOutput);
			}
			if (dispatched && upscaleMethod == UpscaleMethod::kDLSS)
				dlssUpscaleOutputInSharpenerTexture = foveatedOutputToSharpener;
			if (!dispatched) {
				RequestHistoryReset();
				if (!loggedFoveatedFallback) {
					logger::warn("[Upscaling] Foveated vendor dispatch failed; falling back to full-frame {} dispatch.",
						magic_enum::enum_name(upscaleMethod));
					loggedFoveatedFallback = true;
				}
			} else {
				loggedFoveatedFallback = false;
			}
		} else {
			loggedFoveatedFallback = false;
		}

		if (!dispatched) {
			bool fallbackEncodeOk = true;
			if (encodedVRFoveatedRegions) {
				fallbackEncodeOk = encodeUpscalingTextures(true, "Encode Upscaling Textures (Fallback Full)");
			}
			if (!fallbackEncodeOk) {
				logger::warn("[Upscaling] Full-frame {} fallback skipped because input encoding failed.", magic_enum::enum_name(upscaleMethod));
			} else if (upscaleMethod == UpscaleMethod::kDLSS) {
				CS_PROFILE_SCOPE("Upscaling::Upscale");
				streamline.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorResource);
			} else if (upscaleMethod == UpscaleMethod::kFSR) {
				CS_PROFILE_SCOPE("Upscaling::Upscale");
				fidelityFX.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorResource, settings.sharpnessFSR);
			}
		}

		state->EndPerfEvent();
	}
}

void Upscaling::PerformUpscaling()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling");
	Upscale();
	UpscaleDepth();

	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();

	// Disable dynamic resolution past this point
	runtimeData.dynamicResolutionLock = 1;

	// Updates the PerFrame constant buffer so that dynamic resolution settings are disabled
	UpdateCameraData();
}

void Upscaling::UpdateDepthUpscaleKernelState(JitterCB& a_jitterData, bool a_enableWideKernelLogic)
{
	if (!a_enableWideKernelLogic)
		return;

	constexpr float kEnterWideKernelRatio = 1.55f;
	constexpr float kExitWideKernelRatio = 1.45f;
	const float minScale = std::max(std::min(resolutionScale.x, resolutionScale.y), FLT_EPSILON);
	const float upscaleRatio = 1.0f / minScale;

	if (depthUpscaleUseWideKernel) {
		if (upscaleRatio < kExitWideKernelRatio) {
			depthUpscaleUseWideKernel = false;
		}
	} else {
		if (upscaleRatio > kEnterWideKernelRatio) {
			depthUpscaleUseWideKernel = true;
		}
	}

	a_jitterData.useWideKernel = depthUpscaleUseWideKernel ? 1.0f : 0.0f;
}

void Upscaling::UpscaleDepth()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth");
	// Optimization overview:
	// 1) Early validation exits before issuing GPU work.
	// 2) Wide-kernel depth mode uses hysteresis to avoid frequent toggles.
	// 3) Resource copies are skipped for aliased src/dst to reduce copy churn.

	// (1) Early validation exits
	const bool depthUpscaleActive = IsUpscalingActive();
	const auto upscaleMethod = GetRuntimeUpscaleMethod();
	const uint32_t runtimeQualityMode = GetRuntimeQualityMode();
	const bool isVR = globals::game::isVR;
	const bool vendorUpscaler = upscaleMethod == UpscaleMethod::kDLSS || upscaleMethod == UpscaleMethod::kFSR;
	const bool fullResolutionMaskPath =
		upscaleMethod == UpscaleMethod::kNONE ||
		upscaleMethod == UpscaleMethod::kTAA ||
		(vendorUpscaler && runtimeQualityMode == 0);
	const bool repairVRFullResolutionMask =
		isVR &&
		fullResolutionMaskPath &&
		!depthUpscaleActive;

	if (!depthUpscaleActive && !repairVRFullResolutionMask) {
		return;
	}

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !renderer || !context || !deferred || !deferred->linearSampler || !jitterCB || !upscaleRasterizerState || !upscaleBlendState ||
		(depthUpscaleActive && !upscaleDepthStencilState)) {
		return;
	}

	auto screenSize = state->screenSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		return;
	}

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
	auto& refractionNormals = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kREFRACTION_NORMALS];
	auto& saoCameraZ = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSAO_CAMERAZ];
	auto& underwaterMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kUNDERWATER_MASK];

	if (!depth.texture || !depthCopy.texture || !depthCopy.depthSRV ||
		!underwaterMask.texture || !underwaterMask.textureCopy || !underwaterMask.SRVCopy || !underwaterMask.RTV) {
		return;
	}
	if (depthUpscaleActive &&
		(!depth.views[0] || !refractionNormals.texture || !refractionNormals.textureCopy || !refractionNormals.SRVCopy || !refractionNormals.RTV || !saoCameraZ.RTV)) {
		return;
	}
	if (depthUpscaleActive && isVR && (!depthCopy.stencilSRV || !depthCopy.views[0])) {
		return;
	}

	auto* fullscreenVS = GetUpscaleVS();
	auto* depthUpscalePS = depthUpscaleActive ? GetDepthRefractionUpscalePS() : nullptr;
	auto* underwaterMaskPS = GetUnderwaterMaskUpscalePS();
	if (!fullscreenVS || !underwaterMaskPS || (depthUpscaleActive && !depthUpscalePS)) {
		return;
	}

	state->BeginPerfEvent("Render Target Upscaling");
	auto perfEvent = ScopeExit([&]() {
		state->EndPerfEvent();
	});

	// UpscaleDepth can run without the main upscale pass on VR full-resolution
	// mask paths. Unbind current outputs before copying depth/depthCopy.
	context->OMSetRenderTargets(0, nullptr, nullptr);

	// Set up Input Assembler for fullscreen triangle (no vertex/index buffers needed)
	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set up vertex shader that generates fullscreen triangle using SV_VertexID
	context->VSSetShader(fullscreenVS, nullptr, 0);

	// Set up viewport for fullscreen rendering
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = screenSize.x;
	viewport.Height = screenSize.y;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	// Set rasterizer and blend state
	context->RSSetState(upscaleRasterizerState.get());
	context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	// Set up jitter/depth-kernel constant buffer for upscaling
	JitterCB jitterData{};
	jitterData.jitter = jitter;
	UpdateDepthUpscaleKernelState(jitterData, depthUpscaleActive);

	jitterCB->Update(jitterData);
	auto bufferArray = jitterCB->CB();
	context->PSSetConstantBuffers(0, 1, &bufferArray);

	if (depthUpscaleActive) {
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth Upscale");

		// Engine copies kMAIN->kMAIN_COPY during 3D scene rendering.
		// In menu/non-3D contexts the engine path may skip this copy.
		if (IsKnownGameMenuContextActive()) {
			CopyResourceIfNonAliased(context, depthCopy.texture, depth.texture);
		}

		// Clear stencil to be 0xFF
		if (isVR) {
			context->ClearDepthStencilView(depthCopy.views[0], D3D11_CLEAR_STENCIL, 1.0f, 0xFF);
		}

		// Set depth stencil state to write 0x00
		context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

		CopyResourceIfNonAliased(context, refractionNormals.textureCopy, refractionNormals.texture);

		ID3D11ShaderResourceView* srvs[] = { refractionNormals.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		// kSAO_CAMERAZ is at quarter-stereo resolution in VR; the full-stereo viewport would
		// corrupt only the top-left quarter. The engine's ISSAOCameraZ pass populates it correctly.
		ID3D11RenderTargetView* rtvs[] = { refractionNormals.RTV,
			isVR ? nullptr : saoCameraZ.RTV };
		context->OMSetRenderTargets(2, rtvs, depth.views[0]);

		context->PSSetShader(depthUpscalePS, nullptr, 0);
		{
			CS_PROFILE_SCOPE("Upscaling::DepthUpscale");
			context->Draw(3, 0);
		}

		// Depth copy is also used on VR.
		if (isVR) {
			CopyResourceIfNonAliased(context, depthCopy.texture, depth.texture);
		}
	} else {
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Full Resolution Underwater Mask Depth Copy");

		// Full-resolution paths only need to refresh the underwater mask depth source.
		CopyResourceIfNonAliased(context, depthCopy.texture, depth.texture);
	}

	if (!(isVR && ShouldDeferVRProjectedMaskRepair(*this, state))) {
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Underwater Mask");

		viewport.Width = screenSize.x * 0.5f;
		viewport.Height = screenSize.y * 0.5f;
		context->RSSetViewports(1, &viewport);

		CopyResourceIfNonAliased(context, underwaterMask.textureCopy, underwaterMask.texture);

		context->OMSetDepthStencilState(nullptr, 0x00);

		// t0: vanilla mask copy, t1: current upscaled depth, t2: current stencil/HAM mask (VR).
		ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(underwaterMaskPS, nullptr, 0);
		{
			CS_PROFILE_SCOPE("Upscaling::UnderwaterMaskUpscale");
			context->Draw(3, 0);
		}
	}

	ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);
}

void Upscaling::RefreshSubmitStageUnderwaterMask()
{
	ZoneScoped;

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !renderer || !context || !deferred || !deferred->linearSampler || !jitterCB || !upscaleRasterizerState || !upscaleBlendState) {
		return;
	}
	if (!IsVRSubmitStageUnderwaterMaskRefreshRelevant(state)) {
		return;
	}
	if (ShouldDeferVRProjectedMaskRepair(*this, state)) {
		return;
	}

	auto screenSize = state->screenSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		return;
	}
	EnsureRuntimeResolutionStateCurrent();
	const bool vrRenderScaleActive = runtimeResolutionPlan.owner == ResolutionOwner::VRRenderScaleMode;
	if (!vrRenderScaleActive) {
		return;
	}
	const uint32_t inputEyeWidth = std::max<uint32_t>(1u, ClampPositiveDimension(runtimeResolutionPlan.engineRenderSize.x) / 2u);
	const uint32_t inputHeight = ClampPositiveDimension(runtimeResolutionPlan.engineRenderSize.y);
	if (!inputEyeWidth || !inputHeight) {
		return;
	}

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
	auto& underwaterMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kUNDERWATER_MASK];
	if (!depth.texture || !depthCopy.texture || !depthCopy.depthSRV ||
		!underwaterMask.texture || !underwaterMask.textureCopy || !underwaterMask.SRVCopy || !underwaterMask.RTV) {
		return;
	}
	D3D11_TEXTURE2D_DESC underwaterMaskDesc{};
	if (!TryGetTexture2DDesc(underwaterMask.texture, underwaterMaskDesc) || !underwaterMaskDesc.Width || !underwaterMaskDesc.Height) {
		return;
	}

	ID3D11VertexShader* fullscreenVS = nullptr;
	ID3D11PixelShader* underwaterMaskPS = nullptr;
	static bool loggedSubmitStageUnderwaterShaderFailure = false;
	try {
		fullscreenVS = GetUpscaleVS();
		underwaterMaskPS = GetUnderwaterMaskUpscalePS(true);
	} catch (const std::exception& e) {
		LogWarnOnce(
			loggedSubmitStageUnderwaterShaderFailure,
			"[Upscaling] Submit-stage underwater mask refresh shader unavailable; skipping mask refresh",
			e);
		return;
	} catch (...) {
		LogWarnOnce(
			loggedSubmitStageUnderwaterShaderFailure,
			"[Upscaling] Submit-stage underwater mask refresh shader unavailable; skipping mask refresh");
		return;
	}
	if (!fullscreenVS || !underwaterMaskPS) {
		return;
	}

	TracyD3D11Zone(state->tracyCtx, "Upscaling - VR Render Scale Mode Underwater Mask");
	state->BeginPerfEvent("VR Render Scale Mode Underwater Mask Refresh");
	auto perfEvent = ScopeExit([&]() {
		state->EndPerfEvent();
	});

	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(fullscreenVS, nullptr, 0);

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	const float repairWidth = static_cast<float>(std::min<uint32_t>(underwaterMaskDesc.Width, inputEyeWidth));
	const float repairHeight = static_cast<float>(std::min<uint32_t>(underwaterMaskDesc.Height, inputHeight));
	viewport.Width = repairWidth;
	viewport.Height = repairHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	context->RSSetState(upscaleRasterizerState.get());
	context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	JitterCB jitterData{};
	jitterData.jitter = jitter;
	UpdateDepthUpscaleKernelState(jitterData, IsUpscalingActive());
	jitterCB->Update(jitterData);
	auto bufferArray = jitterCB->CB();
	context->PSSetConstantBuffers(0, 1, &bufferArray);

	// This repair can run immediately after Skyrim's ISUnderwaterMask pass.
	// Unbind the vanilla RTV before copying kUNDERWATER_MASK into its SRV copy.
	context->OMSetRenderTargets(0, nullptr, nullptr);

	CopyResourceIfNonAliased(context, depthCopy.texture, depth.texture);
	CopyResourceIfNonAliased(context, underwaterMask.textureCopy, underwaterMask.texture);

	ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
	context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);
	context->OMSetDepthStencilState(nullptr, 0x00);

	ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy, depthCopy.depthSRV };
	context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

	context->PSSetShader(underwaterMaskPS, nullptr, 0);
	{
		CS_PROFILE_SCOPE("Upscaling::SubmitStageUnderwaterMask");
		context->Draw(3, 0);
	}

	ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);
}

void Upscaling::ApplySharpening()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Sharpening");

	if (!ShouldApplyDLSSSharpening())
		return;

	if (!sharpenerTexture)
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	context->OMSetRenderTargets(0, nullptr, nullptr);

	if (dlssUpscaleOutputInSharpenerTexture) {
		if (!main.texture || !sharpenerTexture->resource)
			return;

		if (!main.UAV || !sharpenerTexture->srv || !DispatchDLSSSharpener(*this, sharpenerTexture->srv.get(), main.UAV))
			context->CopyResource(main.texture, sharpenerTexture->resource.get());
	} else {
		if (!main.SRV || !main.texture || !sharpenerTexture->resource || !sharpenerTexture->uav)
			return;

		if (!DispatchDLSSSharpener(*this, main.SRV, sharpenerTexture->uav.get()))
			return;
		context->CopyResource(main.texture, sharpenerTexture->resource.get());
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	globals::features::upscaling.ConfigureTAA();
	func(a_state);
	globals::features::upscaling.ConfigureUpscaling(a_state);
}

void Upscaling::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	auto& upscaling = globals::features::upscaling;
	upscaling.PostDisplay();
	func(a1);

	if (globals::game::isVR && upscaling.IsVRRenderScaleModeLatched() && IsExplicitVRMenuPresentationContextActive()) {
		const bool observedProjectedMenu = IsCurrentRenderTargetVRObservedMenuPresentationSeedTexture();
		if (observedProjectedMenu) {
			ExtendVRObservedProjectedMenuTail();
			ExtendVRMenuPresentationTail(kVRObservedMenuPresentationTailFrames);
			ExtendVRMenuBridgeTraceTail(kVRObservedMenuPresentationTailFrames);
		} else if (IsVRObservedProjectedMenuTailActive(globals::state) &&
				   IsCurrentRenderTargetVRObservedMenuPresentationFollowTexture()) {
			ExtendVRMenuPresentationTail(kVRObservedMenuPresentationTailFrames);
			ExtendVRMenuBridgeTraceTail(kVRObservedMenuPresentationTailFrames);
		}
	}
}

void Upscaling::Main_PostProcessing::thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
{
	auto& upscaling = globals::features::upscaling;
	auto upscaleMethod = upscaling.GetRuntimeUpscaleMethod();

	if (!upscaling.ApplyPendingPostLoadRuntimeReset(upscaleMethod)) {
		func(a_this, a3, a_target, a_4, a_5);
		return;
	}

	const bool vendorMethodSelected = IsVendorUpscalingMethod(upscaleMethod);
	const bool loadingTransitionTailActive =
		globals::game::isVR &&
		IsVRLoadingPresentationTailActive(globals::state);
	const bool vrScenePresentationBlockActive = IsVRMenuScenePresentationBlockActive();
	const bool renderScalePresentationProtection =
		globals::game::isVR &&
		IsVRTransitionPresentationProtectionActive(upscaling, globals::state);
	const bool menuPresentationContext =
		vendorMethodSelected &&
		globals::game::isVR &&
		(upscaling.IsVRRenderScaleModeLatched() || renderScalePresentationProtection) &&
		(vrScenePresentationBlockActive || loadingTransitionTailActive);
	const bool vendorDynamicResolutionActive =
		vendorMethodSelected &&
		upscaling.IsUpscalingActive() &&
		!upscaling.IsVRRenderScaleModeLatched();
	const bool presentationUpscalingActive = upscaling.IsPresentationUpscalingActive();
	const bool submitPathDisabledForVendor =
		vendorMethodSelected &&
		globals::game::isVR &&
		!menuPresentationContext &&
		!upscaling.IsPerfModeActive() &&
		!IsVRRenderScaleSubmitPathEnabled();
	if (submitPathDisabledForVendor) {
		if (upscaling.ShouldUseFrameGenerationThisFrame())
			upscaling.CopySharedD3D12Resources();

		upscaling.PerformUpscaling();

		if (upscaleMethod == UpscaleMethod::kDLSS)
			upscaling.ApplySharpening();

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		func(a_this, a3, a_target, a_4, a_5);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		return;
	}

	if (menuPresentationContext && !presentationUpscalingActive) {
		if (upscaling.IsPerfModeActive())
			globals::features::vr.InstallSubmitHook();

		if (upscaling.ShouldUseFrameGenerationThisFrame())
			upscaling.CopySharedD3D12Resources();

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

		upscaling.PrepareFullResolutionPostProcessing();
		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		func(a_this, a3, a_target, a_4, a_5);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		upscaling.PrepareFullResolutionPostProcessing();
		return;
	}

	if (vendorDynamicResolutionActive && !presentationUpscalingActive) {
		if (upscaling.ShouldUseFrameGenerationThisFrame())
			upscaling.CopySharedD3D12Resources();

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

		upscaling.UpscaleDepth();

		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		func(a_this, a3, a_target, a_4, a_5);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;

		upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
		return;
	}

	if (presentationUpscalingActive) {
		globals::features::vr.InstallSubmitHook();

		if (upscaling.ShouldUseFrameGenerationThisFrame())
			upscaling.CopySharedD3D12Resources();

		upscaling.UpdateHistoryResetState(upscaleMethod);
		upscaling.LatchHistoryResetForCurrentFrame();

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

		static bool loggedSubmitStageUnderwaterRefreshException = false;
		try {
			upscaling.RefreshSubmitStageUnderwaterMask();
		} catch (const std::exception& e) {
			upscaling.UnbindUpscalingResources();
			LogWarnOnce(
				loggedSubmitStageUnderwaterRefreshException,
				"[Upscaling] Submit-stage underwater mask refresh threw; skipping mask refresh",
				e);
		} catch (...) {
			upscaling.UnbindUpscalingResources();
			LogWarnOnce(
				loggedSubmitStageUnderwaterRefreshException,
				"[Upscaling] Submit-stage underwater mask refresh threw; skipping mask refresh");
		}

		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		func(a_this, a3, a_target, a_4, a_5);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;

		upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
		return;
	}

	if (upscaling.ShouldUseFrameGenerationThisFrame())
		upscaling.CopySharedD3D12Resources();

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		upscaling.PerformUpscaling();
	} else if (globals::game::isVR) {
		upscaling.UpscaleDepth();
	}

	if (upscaleMethod == UpscaleMethod::kDLSS)
		upscaling.ApplySharpening();

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

	if (upscaleMethod == UpscaleMethod::kNONE) {
		// Keep vanilla TAA/water stabilization state untouched when no upscaler is active.
		func(a_this, a3, a_target, a_4, a_5);
		return;
	}

	const bool restoreDynamicResolution = vendorDynamicResolutionActive;
	if (restoreDynamicResolution)
		upscaling.PrepareFullResolutionPostProcessing();

	BSImagespaceShaderISTemporalAA->taaEnabled = upscaleMethod == UpscaleMethod::kTAA;
	func(a_this, a3, a_target, a_4, a_5);

	BSImagespaceShaderISTemporalAA->taaEnabled = false;

	if (restoreDynamicResolution)
		upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
}

void Upscaling::SetScissorRect::thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom)
{
	auto viewport = globals::game::graphicsState;
	auto& runtimeData = viewport->GetRuntimeData();

	const bool vrNativeLayoutSubmitProtectedTarget = globals::game::isVR && IsCurrentRenderTargetVRNativeLayoutSubmitProtectedTexture();
	if (!runtimeData.dynamicResolutionLock && !vrNativeLayoutSubmitProtectedTarget) {
		a_left = static_cast<int>(a_left * runtimeData.dynamicResolutionWidthRatio);
		a_right = static_cast<int>(a_right * runtimeData.dynamicResolutionWidthRatio);

		a_top = static_cast<int>(a_top * runtimeData.dynamicResolutionHeightRatio);
		a_bottom = static_cast<int>(a_bottom * runtimeData.dynamicResolutionHeightRatio);
	}

	func(This, a_left, a_top, a_right, a_bottom);
}

void Upscaling::Main_RenderPrecipitation::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}

void Upscaling::BSFaceGenManager_UpdatePendingCustomizationTextures::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}
