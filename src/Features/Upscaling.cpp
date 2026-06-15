#include "Upscaling.h"

#include "Deferred.h"
#include "FoveatedCommon.h"
#include "Hooks.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "RE/B/BSLightingShader.h"
#include "RE/B/BSOpenVR.h"
#include "RE/B/BSRenderPass.h"
#include "RE/B/BSShaderProperty.h"
#include "Features/RenderDoc.h"
#include "ShaderCache.h"
#include "State.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Utils/FileSystem.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "Utils/OpenCompositeInterop.h"
#include "Utils/UI.h"
#include "VR.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cwctype>
#include <directx/d3dx12.h>
#include <dxgi.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
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
	aaVrs,
	aaVrsVisualization,
	aaVrsPerformanceMode,
	aaVrsPerformanceAnisotropy,
	aaVrsPassAware,
	aaVrsContentAware,
	aaVrsProtectWater,
	aaVrsSafeOpaqueOnly,
	aaVrsMaxRate,
	aaVrsPassTelemetry,
	experimentalDeferredCompositePS,
	aaVrsDeferredComposite,
	frameLimitMode,
	frameGenerationMode,
	frameGenerationForceEnable,
	frameGenerationAllowInMenus,
	streamlineLogLevel,
	sharpnessFSR,
	sharpnessDLSS,
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
	// Keep transition diagnostics behind one switch. They default to debug so
	// normal info logs stay free of render-scale/menu transition trace noise.
#ifndef VR_TRANSITION_DIAG_ENABLED
#define VR_TRANSITION_DIAG_ENABLED 1
#endif
#ifndef VR_TRANSITION_DIAG_LOG_LEVEL
#define VR_TRANSITION_DIAG_LOG_LEVEL debug
#endif
#if VR_TRANSITION_DIAG_ENABLED
#define VR_TRANSITION_DIAG_LOG(...)                                      \
	do {                                                                 \
		if (globals::game::isVR)                                         \
			logger::VR_TRANSITION_DIAG_LOG_LEVEL(__VA_ARGS__);           \
	} while (false)
#else
#define VR_TRANSITION_DIAG_LOG(...) \
	do {                            \
	} while (false)
#endif

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
	constexpr uint32_t kVRLoadingMenuRelatchDelayFrames = 1u;
	constexpr uint32_t kVRSubmitStageVendorRelatchCooldownFrames = 30u;
	constexpr uint32_t kVRSubmitStageVendorRelatchMinCooldownFrames = 6u;
	constexpr uint32_t kVRSubmitStageVendorRelatchStableFrames = 3u;
	constexpr uint32_t kVRRenderScaleRelatchBusyRetryFrames = 60u;
	constexpr uint32_t kVRRenderScaleRelatchD3DFailureRetryFrames = 300u;
	constexpr uint32_t kVRRenderScalePostLoadSettleRetryFrames = kVRUpscalingTransitionApplyDelayFrames;
	constexpr uint32_t kVRFpsStabilizerSyncWaitLogIntervalFrames = 120u;
	constexpr float kFoveatedMaskOffsetAdjustMin = -0.30f;
	constexpr float kFoveatedMaskOffsetAdjustMax = 0.30f;
	constexpr float kFoveatedMaskOffsetResolvedMin = -0.30f;
	constexpr float kFoveatedMaskOffsetResolvedMax = 0.30f;
	constexpr float kAAVRSRefinementLumaRangeThreshold = 0.20f;
	constexpr float kAAVRSRefinementBrightLumaThreshold = 2.50f;
	constexpr float kAAVRSRefinementMotionPixelsThreshold = 3.00f;
	constexpr float kAAVRSRefinementDepthRangeThreshold = 0.02f;
	std::atomic_bool g_vrLoadingMenuOpenFromEvent{ false };
	std::atomic_uint32_t g_vrLoadingTransitionCloseFrame{ 0 };
	std::atomic_uint32_t g_vrLoadingTransitionTailEndFrame{ 0 };
	std::atomic_uint32_t g_vrMenuPresentationTailEndFrame{ 0 };
	std::atomic_uint32_t g_vrObservedProjectedMenuTailEndFrame{ 0 };
	std::atomic_bool g_renderDocDllDetected{ false };
	std::atomic_bool g_renderDocUpscalingD3DHookBypassLogged{ false };
	constexpr uint32_t kVRCellTransitionTailFrames = 4;
	constexpr uint32_t kVRCellTransitionPresentationTailFrames = kVRCellTransitionTailFrames;

	bool UsesVRRenderScalePostLoadSettle(Upscaling::VRUpscalingTransitionOrigin a_origin)
	{
		return a_origin != Upscaling::VRUpscalingTransitionOrigin::VRAPI;
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

	bool IsMainMenuContextActive();
	bool IsKnownGameMenuContextActive();

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
	// Hold vendor dynamic-resolution rendering at full resolution briefly around
	// VR menu open/close so fullscreen fades and menu overlays do not inherit a
	// stale reduced scene footprint.
	constexpr uint32_t kVRMenuPresentationTailFrames = 30;
	// Observed projected-menu frames can briefly extend VR menu presentation
	// blocking when the menu state graph drops a trailing text pass too early.
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
		"RaceSex Menu",
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
	constexpr const char* kFoveatedVrsName = "Foveated Variable Rate Shading (VRS)";
	constexpr const char* kVrsMaskVisualizationName = "VRS Mask Visualization";
	constexpr const char* kVrsMaskRefinementInstructions = R"(These steps apply to the default mask-aligned VRS mode. VRS Performance Mode deliberately shows magenta outside its fixed 0.25 inner band.
1) Once Foveated Variable Rate Shading (VRS) is active, refine the FOV masks in position and size.
2) Turn off FOV Mask Visualization and turn on VRS Mask Visualization.
3) Reposition each per-eye FOV mask with FOV Left Eye Offset X/Y and FOV Right Eye Offset X/Y so the visible center becomes dark with no magenta. Light magenta at the far periphery is acceptable.
4) Turn off VRS Mask Visualization and check in game for high-frequency flicker anywhere in the periphery.
5) If flicker is present, reposition the masks and, if needed, enlarge the active mask with FOV Only Visible Scale or FOV + TAA Visible Outer Scale until flicker is no longer visible.
6) If no flicker is visible after positioning, lower the active mask scale to the smallest value that still shows no flicker for maximum performance and image quality.
7) Test whether Foveated Upscaling (FOV) or FOV + TAA gives better performance by toggling FOV + TAA while watching frame times.
8) Save your mask settings and enjoy the performance win.)";

	uint ClampToggleUInt(uint value);

	const char* GetAAVRSPassPolicyReasonName(Upscaling::AAVRSPassPolicyReason a_reason)
	{
		switch (a_reason) {
		case Upscaling::AAVRSPassPolicyReason::AlphaTest:
			return "Alpha test";
		case Upscaling::AAVRSPassPolicyReason::ShaderPropertyAlpha:
			return "Alpha property";
		case Upscaling::AAVRSPassPolicyReason::ShaderPropertyDecal:
			return "Decal property";
		case Upscaling::AAVRSPassPolicyReason::ShaderPropertyEmissive:
			return "Emissive property";
		case Upscaling::AAVRSPassPolicyReason::ShaderPropertyHighFrequency:
			return "High-frequency property";
		case Upscaling::AAVRSPassPolicyReason::EffectShader:
			return "Effect shader";
		case Upscaling::AAVRSPassPolicyReason::ParticleShader:
			return "Particle shader";
		case Upscaling::AAVRSPassPolicyReason::WaterShader:
			return "Water shader";
		case Upscaling::AAVRSPassPolicyReason::GrassShader:
			return "Grass shader";
		case Upscaling::AAVRSPassPolicyReason::DistantTreeShader:
			return "Distant-tree shader";
		case Upscaling::AAVRSPassPolicyReason::BloodSplatterShader:
			return "Blood-splatter shader";
		case Upscaling::AAVRSPassPolicyReason::SkyShader:
			return "Sky shader";
		case Upscaling::AAVRSPassPolicyReason::LightingTechnique:
			return "Lighting technique";
		case Upscaling::AAVRSPassPolicyReason::LightingDescriptor:
			return "Lighting descriptor";
		case Upscaling::AAVRSPassPolicyReason::UtilityDescriptor:
			return "Utility descriptor";
		case Upscaling::AAVRSPassPolicyReason::SafeOpaqueOnly:
			return "Safe-opaque filter";
		case Upscaling::AAVRSPassPolicyReason::DecalPhase:
			return "Blended decals phase";
		case Upscaling::AAVRSPassPolicyReason::None:
		case Upscaling::AAVRSPassPolicyReason::Count:
		default:
			return "None";
		}
	}

	const char* GetAAVRSPerformanceAnisotropyName(uint a_value)
	{
		switch (std::min<uint>(a_value, 2u)) {
		case 1:
			return "2x1";
		case 2:
			return "1x2";
		case 0:
		default:
			return "Auto";
		}
	}

	template <class Enum>
	bool HasShaderFlag(uint64_t a_flags, Enum a_flag)
	{
		return (a_flags & static_cast<uint64_t>(a_flag)) != 0;
	}

	bool TryDecodeLightingDescriptor(const RE::BSRenderPass* a_pass, uint32_t& a_outDescriptor)
	{
		if (a_pass &&
			a_pass->shader &&
			a_pass->shader->shaderType.get() == RE::BSShader::Type::Lighting &&
			a_pass->passEnum >= RE::BSLightingShader::kTechniqueIDBase) {
			a_outDescriptor = a_pass->passEnum - RE::BSLightingShader::kTechniqueIDBase;
			return true;
		}

		a_outDescriptor = 0;
		return false;
	}

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

	bool IsSubmitStagePathEligible(Upscaling::UpscaleMethod a_upscaleMethod)
	{
		if (!IsRenderScaleMethodEligible(a_upscaleMethod))
			return false;

		return IsRenderScaleQualityMode(globals::features::upscaling.settings.qualityMode);
	}

	bool ShouldDelayVRRenderScaleForPendingDLSS(const Upscaling& a_upscaling);

	bool IsSubmitStagePathEnabled()
	{
		auto& upscaling = globals::features::upscaling;
		if (upscaling.IsSubmitStageDeviceLost())
			return false;

		if (ShouldDelayVRRenderScaleForPendingDLSS(upscaling))
			return false;

		const auto upscaleMethod = upscaling.GetUpscaleMethod();
		if (!upscaling.IsRenderScaleModeRequested())
			return false;

		if (!IsSubmitStagePathEligible(upscaleMethod))
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

	// These targets feed late menu/HUD/fade work and must stay display-sized
	// under VR render-scale mode. They are not final eye images and must not be
	// treated as submit-stage presentation sources.
	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 6> kVRProtectedFullSizeTargets{
		RE::RENDER_TARGETS::kMENUBG,
		RE::RENDER_TARGETS::kPROJECTEDMENU,
		RE::RENDER_TARGETS::kHUDMENU,
		RE::RENDER_TARGETS::kFADERUI,
		RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_1,
		RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_2,
	};

	// Projected-menu is the only strong menu-specific seed we have here. HUDMENU
	// can also appear during ordinary gameplay, so it is only safe as a short
	// follow-up signal after a recent projected-menu observation.
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

	// No engine-managed render target is currently a submit-stage eye source.
	// Submit-stage should only operate on the runtime-submitted eye textures.
	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 0> kSubmittedVRPresentationTargets{};

	static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 2> kVRRenderScaleExtraFullSizeTargets{
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

	bool IsVRProtectedFullSizeTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		return std::find(
			       kVRProtectedFullSizeTargets.begin(),
			       kVRProtectedFullSizeTargets.end(),
			       a_target) != kVRProtectedFullSizeTargets.end();
	}

	bool IsVRRenderScaleExtraFullSizeTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		return std::find(
			       kVRRenderScaleExtraFullSizeTargets.begin(),
			       kVRRenderScaleExtraFullSizeTargets.end(),
			       a_target) != kVRRenderScaleExtraFullSizeTargets.end();
	}

	bool IsVRRenderScaleEngineSizedTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		return std::find(
			       kVRRenderScaleEngineSizedTargets.begin(),
			       kVRRenderScaleEngineSizedTargets.end(),
			       a_target) != kVRRenderScaleEngineSizedTargets.end();
	}

	bool UsesFullSizeVRProtectedTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		if (IsVRProtectedFullSizeTarget(a_target))
			return true;

		return IsVRRenderScaleExtraFullSizeTarget(a_target);
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

	bool IsVRProtectedFullSizeRenderTargetTexture(ID3D11Texture2D* a_texture)
	{
		return IsRenderTargetTextureInTargets(a_texture, kVRProtectedFullSizeTargets);
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

	bool IsCurrentRenderTargetVRProtectedFullSizeTexture()
	{
		return IsCurrentRenderTargetTextureMatch(IsVRProtectedFullSizeRenderTargetTexture);
	}

	bool IsCurrentRenderTargetVRObservedMenuPresentationSeedTexture()
	{
		return IsCurrentRenderTargetTextureMatch(IsVRObservedMenuPresentationSeedRenderTargetTexture);
	}

	bool IsCurrentRenderTargetVRObservedMenuPresentationFollowTexture()
	{
		return IsCurrentRenderTargetTextureMatch(IsVRObservedMenuPresentationFollowRenderTargetTexture);
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
		for (const auto target : kVRProtectedFullSizeTargets) {
			if (!ExistingRenderTargetTextureSizeMatches(target, displayWidth, displayHeight))
				return false;
		}
		for (const auto target : kVRRenderScaleExtraFullSizeTargets) {
			if (!ExistingRenderTargetTextureSizeMatches(target, displayWidth, displayHeight))
				return false;
		}

		return true;
	}

	float ComputeAAVRSSafetyScalePadding(uint32_t a_inputWidthPerEye, uint32_t a_inputHeight, float a_centerHorizontalScale)
	{
		const float inputWidth = static_cast<float>(std::max<uint32_t>(a_inputWidthPerEye, 1u));
		const float inputHeight = static_cast<float>(std::max<uint32_t>(a_inputHeight, 1u));
		const float horizontalScale = std::max(FoveatedCommon::ClampCenterHorizontalScale(a_centerHorizontalScale), 1e-4f);
		const float horizontalScalePadding = (2.0f * static_cast<float>(AAVRSController::kTileWidth)) / (inputWidth * horizontalScale);
		const float verticalScalePadding = (2.0f * static_cast<float>(AAVRSController::kTileHeight)) / inputHeight;
		return std::max(horizontalScalePadding, verticalScalePadding);
	}

	void CopyResourceIfNonAliased(ID3D11DeviceContext* a_context, ID3D11Resource* a_dst, ID3D11Resource* a_src)
	{
		if (a_context && a_dst && a_src && a_dst != a_src) {
			a_context->CopyResource(a_dst, a_src);
		}
	}

	template <class T>
	void ReleaseD3D11StatePointer(T*& a_pointer)
	{
		if (a_pointer) {
			a_pointer->Release();
			a_pointer = nullptr;
		}
	}

	template <class T, size_t N>
	void ReleaseD3D11StateArray(std::array<T*, N>& a_values)
	{
		for (auto*& value : a_values)
			ReleaseD3D11StatePointer(value);
	}

	struct ScopedVRMenuCompositeD3D11State
	{
		enum : UINT
		{
			kVBCount = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
			kRTVCount = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
			kViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE
		};

		explicit ScopedVRMenuCompositeD3D11State(ID3D11DeviceContext* a_context) :
			context(a_context)
		{
			if (!context)
				return;

			context->IAGetInputLayout(&inputLayout);
			context->IAGetVertexBuffers(0, kVBCount, vertexBuffers.data(), vertexStrides.data(), vertexOffsets.data());
			context->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
			context->IAGetPrimitiveTopology(&primitiveTopology);

			context->VSGetShader(&vertexShader, nullptr, nullptr);
			context->HSGetShader(&hullShader, nullptr, nullptr);
			context->DSGetShader(&domainShader, nullptr, nullptr);
			context->GSGetShader(&geometryShader, nullptr, nullptr);
			context->PSGetShader(&pixelShader, nullptr, nullptr);
			context->PSGetShaderResources(0, 1, &pixelSRV);
			context->PSGetConstantBuffers(0, 1, &pixelCB);
			context->PSGetSamplers(0, 1, &pixelSampler);
			context->CSGetUnorderedAccessViews(0, 1, &computeUAV);

			context->RSGetState(&rasterizerState);
			viewportCount = kViewportCount;
			context->RSGetViewports(&viewportCount, viewports.data());
			scissorCount = kViewportCount;
			context->RSGetScissorRects(&scissorCount, scissors.data());

			context->OMGetBlendState(&blendState, blendFactor, &sampleMask);
			context->OMGetDepthStencilState(&depthStencilState, &stencilRef);
			context->OMGetRenderTargets(kRTVCount, renderTargets.data(), &depthStencilView);
		}

		~ScopedVRMenuCompositeD3D11State()
		{
			if (context) {
				ID3D11ShaderResourceView* nullSRV = nullptr;
				ID3D11Buffer* nullCB = nullptr;
				ID3D11SamplerState* nullSampler = nullptr;
				ID3D11UnorderedAccessView* nullUAV = nullptr;
				context->PSSetShaderResources(0, 1, &nullSRV);
				context->PSSetConstantBuffers(0, 1, &nullCB);
				context->PSSetSamplers(0, 1, &nullSampler);
				context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

				context->IASetInputLayout(inputLayout);
				context->IASetVertexBuffers(0, kVBCount, vertexBuffers.data(), vertexStrides.data(), vertexOffsets.data());
				context->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
				context->IASetPrimitiveTopology(primitiveTopology);

				context->VSSetShader(vertexShader, nullptr, 0);
				context->HSSetShader(hullShader, nullptr, 0);
				context->DSSetShader(domainShader, nullptr, 0);
				context->GSSetShader(geometryShader, nullptr, 0);
				context->PSSetShader(pixelShader, nullptr, 0);
				context->PSSetShaderResources(0, 1, &pixelSRV);
				context->PSSetConstantBuffers(0, 1, &pixelCB);
				context->PSSetSamplers(0, 1, &pixelSampler);
				context->CSSetUnorderedAccessViews(0, 1, &computeUAV, nullptr);

				context->RSSetState(rasterizerState);
				context->RSSetViewports(viewportCount, viewports.data());
				context->RSSetScissorRects(scissorCount, scissors.data());
				context->OMSetBlendState(blendState, blendFactor, sampleMask);
				context->OMSetDepthStencilState(depthStencilState, stencilRef);
				context->OMSetRenderTargets(kRTVCount, renderTargets.data(), depthStencilView);
			}

			ReleaseD3D11StatePointer(inputLayout);
			ReleaseD3D11StateArray(vertexBuffers);
			ReleaseD3D11StatePointer(indexBuffer);
			ReleaseD3D11StatePointer(vertexShader);
			ReleaseD3D11StatePointer(hullShader);
			ReleaseD3D11StatePointer(domainShader);
			ReleaseD3D11StatePointer(geometryShader);
			ReleaseD3D11StatePointer(pixelShader);
			ReleaseD3D11StatePointer(pixelSRV);
			ReleaseD3D11StatePointer(pixelCB);
			ReleaseD3D11StatePointer(pixelSampler);
			ReleaseD3D11StatePointer(computeUAV);
			ReleaseD3D11StatePointer(rasterizerState);
			ReleaseD3D11StatePointer(blendState);
			ReleaseD3D11StatePointer(depthStencilState);
			ReleaseD3D11StateArray(renderTargets);
			ReleaseD3D11StatePointer(depthStencilView);
		}

		ID3D11DeviceContext* context = nullptr;
		ID3D11InputLayout* inputLayout = nullptr;
		std::array<ID3D11Buffer*, kVBCount> vertexBuffers{};
		std::array<UINT, kVBCount> vertexStrides{};
		std::array<UINT, kVBCount> vertexOffsets{};
		ID3D11Buffer* indexBuffer = nullptr;
		DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
		UINT indexOffset = 0;
		D3D11_PRIMITIVE_TOPOLOGY primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11VertexShader* vertexShader = nullptr;
		ID3D11HullShader* hullShader = nullptr;
		ID3D11DomainShader* domainShader = nullptr;
		ID3D11GeometryShader* geometryShader = nullptr;
		ID3D11PixelShader* pixelShader = nullptr;
		ID3D11ShaderResourceView* pixelSRV = nullptr;
		ID3D11Buffer* pixelCB = nullptr;
		ID3D11SamplerState* pixelSampler = nullptr;
		ID3D11UnorderedAccessView* computeUAV = nullptr;
		ID3D11RasterizerState* rasterizerState = nullptr;
		std::array<D3D11_VIEWPORT, kViewportCount> viewports{};
		UINT viewportCount = 0;
		std::array<D3D11_RECT, kViewportCount> scissors{};
		UINT scissorCount = 0;
		ID3D11BlendState* blendState = nullptr;
		FLOAT blendFactor[4]{};
		UINT sampleMask = 0xffffffff;
		ID3D11DepthStencilState* depthStencilState = nullptr;
		UINT stencilRef = 0;
		std::array<ID3D11RenderTargetView*, kRTVCount> renderTargets{};
		ID3D11DepthStencilView* depthStencilView = nullptr;
	};

	const char* GetVRMenuCompositionTargetName(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		switch (a_target) {
		case RE::RENDER_TARGETS::kTOTAL:
			return "kTOTAL";
		case RE::RENDER_TARGETS::kMAIN:
			return "kMAIN";
		case RE::RENDER_TARGETS::kMENUBG:
			return "kMENUBG";
		case RE::RENDER_TARGETS::kPROJECTEDMENU:
			return "kPROJECTEDMENU";
		case RE::RENDER_TARGETS::kHUDMENU:
			return "kHUDMENU";
		case RE::RENDER_TARGETS::kFADERUI:
			return "kFADERUI";
		case RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_1:
			return "kTEMPORAL_AA_UI_ACCUMULATION_1";
		case RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_2:
			return "kTEMPORAL_AA_UI_ACCUMULATION_2";
		default:
			return "unknown";
		}
	}

	bool IsVRMenuCompositionDestinationTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		return a_target == RE::RENDER_TARGETS::kTOTAL ||
		       a_target == RE::RENDER_TARGETS::kMAIN;
	}

	bool IsVRMenuCompositionSourceTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target)
	{
		switch (a_target) {
		case RE::RENDER_TARGETS::kMENUBG:
		case RE::RENDER_TARGETS::kPROJECTEDMENU:
		case RE::RENDER_TARGETS::kHUDMENU:
		case RE::RENDER_TARGETS::kFADERUI:
		case RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_1:
		case RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_2:
			return true;
		default:
			return false;
		}
	}

	std::string GetD3DDebugObjectName(ID3D11DeviceChild* a_object);
	bool TryHashD3DBufferContents(ID3D11Buffer* a_buffer, const D3D11_BUFFER_DESC& a_desc, uint64_t& a_hash);

	std::string FormatVRMenuDiagnosticHex(uint64_t a_value)
	{
		constexpr char kHexDigits[] = "0123456789ABCDEF";

		std::string result = "0x";
		bool started = false;
		for (int shift = 60; shift >= 0; shift -= 4) {
			const auto digit = static_cast<uint8_t>((a_value >> shift) & 0xFull);
			if (digit != 0 || started || shift == 0) {
				started = true;
				result += kHexDigits[digit];
			}
		}
		return result;
	}

	template <class T>
	std::string FormatVRMenuDiagnosticPointer(T* a_pointer)
	{
		return FormatVRMenuDiagnosticHex(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(a_pointer)));
	}

	struct VRMenuCompositionTargetMatch
	{
		bool matched = false;
		RE::RENDER_TARGETS::RENDER_TARGET target = RE::RENDER_TARGETS::kTOTAL;
		const char* name = "unknown";
		uint32_t width = 0;
		uint32_t height = 0;
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

		static constexpr std::array<RE::RENDER_TARGETS::RENDER_TARGET, 8> targets{
			RE::RENDER_TARGETS::kTOTAL,
			RE::RENDER_TARGETS::kMAIN,
			RE::RENDER_TARGETS::kMENUBG,
			RE::RENDER_TARGETS::kPROJECTEDMENU,
			RE::RENDER_TARGETS::kHUDMENU,
			RE::RENDER_TARGETS::kFADERUI,
			RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_1,
			RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_2,
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
			a_outMatch.format = desc.Format;
			return true;
		}

		return false;
	}

	void MixVRMenuOriginalCompositeTextSignature(uint64_t& a_signature, const std::string& a_text)
	{
		for (const auto ch : a_text) {
			a_signature ^= static_cast<uint8_t>(ch);
			a_signature *= 1099511628211ull;
		}
	}

	std::string BuildVRMenuOriginalCompositeViewportDiagnostics(ID3D11DeviceContext* a_context, uint64_t& a_signature)
	{
		if (!a_context)
			return "-";

		UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
		a_context->RSGetViewports(&viewportCount, viewports.data());

		std::string result = "count=" + std::to_string(viewportCount);
		const auto loggedCount = std::min<size_t>(viewportCount, 4);
		for (size_t i = 0; i < loggedCount; ++i) {
			const auto& viewport = viewports[i];
			result += " v";
			result += std::to_string(i);
			result += "=(";
			result += std::to_string(viewport.TopLeftX);
			result += ",";
			result += std::to_string(viewport.TopLeftY);
			result += " ";
			result += std::to_string(viewport.Width);
			result += "x";
			result += std::to_string(viewport.Height);
			result += " z=";
			result += std::to_string(viewport.MinDepth);
			result += "->";
			result += std::to_string(viewport.MaxDepth);
			result += ")";
		}

		MixVRMenuOriginalCompositeTextSignature(a_signature, result);
		return result;
	}

	std::string BuildVRMenuOriginalCompositeScissorDiagnostics(ID3D11DeviceContext* a_context, uint64_t& a_signature)
	{
		if (!a_context)
			return "-";

		UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
		a_context->RSGetScissorRects(&scissorCount, scissors.data());

		std::string result = "count=" + std::to_string(scissorCount);
		const auto loggedCount = std::min<size_t>(scissorCount, 4);
		for (size_t i = 0; i < loggedCount; ++i) {
			const auto& scissor = scissors[i];
			result += " s";
			result += std::to_string(i);
			result += "=(";
			result += std::to_string(scissor.left);
			result += ",";
			result += std::to_string(scissor.top);
			result += ")->(";
			result += std::to_string(scissor.right);
			result += ",";
			result += std::to_string(scissor.bottom);
			result += ")";
		}

		MixVRMenuOriginalCompositeTextSignature(a_signature, result);
		return result;
	}

	std::string BuildVRMenuOriginalCompositeShaderDiagnostics(ID3D11DeviceContext* a_context, uint64_t& a_signature)
	{
		if (!a_context)
			return "-";

		ID3D11VertexShader* vertexShader = nullptr;
		ID3D11GeometryShader* geometryShader = nullptr;
		ID3D11PixelShader* pixelShader = nullptr;
		a_context->VSGetShader(&vertexShader, nullptr, nullptr);
		a_context->GSGetShader(&geometryShader, nullptr, nullptr);
		a_context->PSGetShader(&pixelShader, nullptr, nullptr);

		const auto appendShader = [&](std::string& a_result, const char* a_label, ID3D11DeviceChild* a_shader) {
			if (!a_result.empty())
				a_result += " ";
			a_result += a_label;
			a_result += "=";
			a_result += FormatVRMenuDiagnosticPointer(a_shader);
			const auto debugName = GetD3DDebugObjectName(a_shader);
			if (!debugName.empty()) {
				a_result += "(";
				a_result += debugName;
				a_result += ")";
			}
		};

		std::string result;
		appendShader(result, "vs", vertexShader);
		appendShader(result, "gs", geometryShader);
		appendShader(result, "ps", pixelShader);

		MixVRMenuOriginalCompositeTextSignature(a_signature, result);

		if (vertexShader)
			vertexShader->Release();
		if (geometryShader)
			geometryShader->Release();
		if (pixelShader)
			pixelShader->Release();

		return result;
	}

	std::string BuildVRMenuOriginalCompositeCBDiagnostics(ID3D11DeviceContext* a_context, bool a_vertexStage, uint64_t& a_signature)
	{
		if (!a_context)
			return "-";

		std::array<ID3D11Buffer*, 4> buffers{};
		if (a_vertexStage) {
			a_context->VSGetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
		} else {
			a_context->PSGetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
		}

		std::string result;
		for (size_t i = 0; i < buffers.size(); ++i) {
			auto* buffer = buffers[i];
			if (!buffer)
				continue;

			D3D11_BUFFER_DESC desc{};
			buffer->GetDesc(&desc);

			uint64_t hash = 0;
			const bool hashValid = TryHashD3DBufferContents(buffer, desc, hash);

			if (!result.empty())
				result += " ";
			result += "b";
			result += std::to_string(i);
			result += "=";
			result += FormatVRMenuDiagnosticPointer(buffer);
			result += "/";
			result += std::to_string(desc.ByteWidth);
			result += "/h=";
			result += hashValid ? FormatVRMenuDiagnosticHex(hash) : "-";

			a_signature ^= static_cast<uint64_t>(i + 1);
			a_signature *= 1099511628211ull;
			a_signature ^= static_cast<uint64_t>(desc.ByteWidth);
			a_signature *= 1099511628211ull;
			a_signature ^= hashValid ? hash : 0;
			a_signature *= 1099511628211ull;
		}

		for (auto* buffer : buffers) {
			if (buffer)
				buffer->Release();
		}

		return result.empty() ? "-" : result;
	}

	std::string BuildVRMenuOriginalCompositePipelineStateDiagnostics(ID3D11DeviceContext* a_context, uint64_t& a_signature)
	{
		if (!a_context)
			return "-";

		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		a_context->IAGetPrimitiveTopology(&topology);

		ID3D11BlendState* blendState = nullptr;
		FLOAT blendFactor[4]{};
		UINT sampleMask = 0xffffffffu;
		a_context->OMGetBlendState(&blendState, blendFactor, &sampleMask);

		ID3D11DepthStencilState* depthStencilState = nullptr;
		UINT stencilRef = 0;
		a_context->OMGetDepthStencilState(&depthStencilState, &stencilRef);

		ID3D11RasterizerState* rasterizerState = nullptr;
		a_context->RSGetState(&rasterizerState);

		std::string result;
		result += "topology=";
		result += std::to_string(static_cast<uint32_t>(topology));
		result += " blend=";
		result += FormatVRMenuDiagnosticPointer(blendState);
		result += " blendFactor=(";
		result += std::to_string(blendFactor[0]);
		result += ",";
		result += std::to_string(blendFactor[1]);
		result += ",";
		result += std::to_string(blendFactor[2]);
		result += ",";
		result += std::to_string(blendFactor[3]);
		result += ") sampleMask=";
		result += FormatVRMenuDiagnosticHex(sampleMask);
		result += " depth=";
		result += FormatVRMenuDiagnosticPointer(depthStencilState);
		result += " stencilRef=";
		result += std::to_string(stencilRef);
		result += " raster=";
		result += FormatVRMenuDiagnosticPointer(rasterizerState);

		MixVRMenuOriginalCompositeTextSignature(a_signature, result);

		if (blendState)
			blendState->Release();
		if (depthStencilState)
			depthStencilState->Release();
		if (rasterizerState)
			rasterizerState->Release();

		return result;
	}

	void LogVRMenuOriginalCompositeCandidateDiagnostics(
		ID3D11DeviceContext* a_context,
		const char* a_passName,
		const char* a_phase,
		bool a_knownMenu,
		bool a_vrMenuPresentation,
		bool a_communityShadersMenu,
		bool a_renderScaleActive,
		bool a_presentationUpscaling)
	{
		if (!globals::game::isVR || !a_context)
			return;
		if (a_communityShadersMenu || (!a_knownMenu && !a_vrMenuPresentation))
			return;

		ID3D11RenderTargetView* rtv = nullptr;
		a_context->OMGetRenderTargets(1, &rtv, nullptr);
		if (!rtv)
			return;
		auto rtvRelease = ScopeExit([&]() {
			rtv->Release();
		});

		VRMenuCompositionTargetMatch destination{};
		if (!TryResolveVRMenuCompositionView(rtv, destination) ||
			!IsVRMenuCompositionDestinationTarget(destination.target)) {
			return;
		}

		std::array<ID3D11ShaderResourceView*, 8> srvs{};
		a_context->PSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
		auto srvRelease = ScopeExit([&]() {
			for (auto* srv : srvs) {
				if (srv)
					srv->Release();
			}
		});

		std::string sources;
		uint64_t signature = 1469598103934665603ull;
		const auto mixSignature = [&](uint64_t a_value) {
			signature ^= a_value;
			signature *= 1099511628211ull;
		};
		mixSignature(static_cast<uint64_t>(destination.target));
		mixSignature(destination.width);
		mixSignature(destination.height);
		mixSignature(static_cast<uint64_t>(destination.format));

		for (size_t slot = 0; slot < srvs.size(); ++slot) {
			VRMenuCompositionTargetMatch source{};
			if (!TryResolveVRMenuCompositionView(srvs[slot], source) ||
				!IsVRMenuCompositionSourceTarget(source.target)) {
				continue;
			}

			if (!sources.empty())
				sources += ", ";
			sources += "t";
			sources += std::to_string(slot);
			sources += "=";
			sources += source.name;
			sources += "(";
			sources += std::to_string(source.width);
			sources += "x";
			sources += std::to_string(source.height);
			sources += ")";

			mixSignature(static_cast<uint64_t>(slot + 1));
			mixSignature(static_cast<uint64_t>(source.target));
			mixSignature(source.width);
			mixSignature(source.height);
			mixSignature(static_cast<uint64_t>(source.format));
		}

		if (sources.empty())
			return;

		static std::atomic<uint64_t> lastLoggedSignature{ 0 };
		static std::atomic<uint32_t> detailedCaptureCount{ 0 };
		const bool captureDetail =
			a_renderScaleActive &&
			a_presentationUpscaling &&
			detailedCaptureCount.load(std::memory_order_acquire) < 96;
		std::string viewports = "-";
		std::string scissors = "-";
		std::string shaders = "-";
		std::string vsCBs = "-";
		std::string psCBs = "-";
		std::string pipelineState = "-";
		if (captureDetail) {
			detailedCaptureCount.fetch_add(1, std::memory_order_acq_rel);
			viewports = BuildVRMenuOriginalCompositeViewportDiagnostics(a_context, signature);
			scissors = BuildVRMenuOriginalCompositeScissorDiagnostics(a_context, signature);
			shaders = BuildVRMenuOriginalCompositeShaderDiagnostics(a_context, signature);
			vsCBs = BuildVRMenuOriginalCompositeCBDiagnostics(a_context, true, signature);
			psCBs = BuildVRMenuOriginalCompositeCBDiagnostics(a_context, false, signature);
			pipelineState = BuildVRMenuOriginalCompositePipelineStateDiagnostics(a_context, signature);
		}

		if (lastLoggedSignature.exchange(signature, std::memory_order_acq_rel) == signature)
			return;

		const auto frame = globals::state ? globals::state->frameCount : 0;
		logger::debug(
			"[VRMenuOriginalComposite] frame={} pass={} phase={} signature={} dst={}({}x{} fmt={}) sources={} knownMenu={} vrMenuPresentation={} renderScaleActive={} presentationUpscaling={} viewports={} scissors={} shaders={} vsCBs={} psCBs={} state={}",
			frame,
			a_passName ? a_passName : "-",
			a_phase ? a_phase : "-",
			FormatVRMenuDiagnosticHex(signature),
			destination.name,
			destination.width,
			destination.height,
			static_cast<uint32_t>(destination.format),
			sources,
			a_knownMenu ? "yes" : "no",
			a_vrMenuPresentation ? "yes" : "no",
			a_renderScaleActive ? "yes" : "no",
			a_presentationUpscaling ? "yes" : "no",
			viewports,
			scissors,
			shaders,
			vsCBs,
			psCBs,
			pipelineState);
	}

	float ClampFoveatedCenterScale(float value)
	{
		return FoveatedCommon::ClampCenterScale(value);
	}

	float ResolveActiveFoveatedMaskCoverageScale(
		const Upscaling::ActiveUpscalingFoveatedProfile& a_profile,
		const FoveatedRegionPlan& a_regionPlan)
	{
		if (!a_regionPlan.IsValid())
			return ClampFoveatedCenterScale(a_profile.sharedVisibleScale);

		if (a_profile.usesPeripheryTAAOuterMask) {
			const float sharedVisibleScale = a_regionPlan.peripheryTAAOuterScale > 0.0f ?
				a_regionPlan.peripheryTAAOuterScale :
				a_profile.sharedVisibleScale;
			return ClampFoveatedCenterScale(sharedVisibleScale);
		}

		return ClampFoveatedCenterScale(a_regionPlan.centerScale);
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
		return exp2((2.0f * clampedSharpness) - 2.0f);
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
		return std::max({
			FoveatedMaskDistancePixelCenter(minX, minY, width, height, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY),
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
		settings.aaVrs = settings.aaVrs && REL::Module::IsVR();
		settings.aaVrsVisualization = settings.aaVrsVisualization && settings.aaVrs && REL::Module::IsVR();
		settings.aaVrsPerformanceMode = settings.aaVrsPerformanceMode && REL::Module::IsVR();
		settings.aaVrsPerformanceAnisotropy = std::min<uint>(settings.aaVrsPerformanceAnisotropy, 2u);
		settings.aaVrsPassAware = settings.aaVrsPassAware && REL::Module::IsVR();
		settings.aaVrsContentAware = settings.aaVrsContentAware && REL::Module::IsVR();
		settings.aaVrsProtectWater = settings.aaVrsProtectWater && REL::Module::IsVR();
		settings.aaVrsSafeOpaqueOnly = settings.aaVrsSafeOpaqueOnly && REL::Module::IsVR();
		settings.aaVrsMaxRate = std::min<uint>(settings.aaVrsMaxRate, 1u);
		settings.aaVrsPassTelemetry = settings.aaVrsPassTelemetry && settings.aaVrs && REL::Module::IsVR();
		settings.experimentalDeferredCompositePS = settings.experimentalDeferredCompositePS && settings.aaVrs && REL::Module::IsVR();
		settings.aaVrsDeferredComposite = settings.aaVrsDeferredComposite && settings.experimentalDeferredCompositePS && settings.aaVrs && REL::Module::IsVR();
		settings.frameLimitMode = ClampToggleUInt(settings.frameLimitMode);
		settings.frameGenerationMode = ClampToggleUInt(settings.frameGenerationMode);
		settings.frameGenerationForceEnable = ClampToggleUInt(settings.frameGenerationForceEnable);
		settings.streamlineLogLevel = ClampStreamlineLogLevelUInt(settings.streamlineLogLevel);
		settings.sharpnessFSR = ClampFiniteUnitRange(settings.sharpnessFSR, 0.0f);
		settings.sharpnessDLSS = ClampFiniteUnitRange(settings.sharpnessDLSS, 0.1f);
		settings.periphery_taa_center_blend_feather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);
		SanitizeFoveatedSettings(settings);
		settings.periphery_taa_outer_scale = ClampPeripheryTAAOuterScaleForCenter(
			settings.periphery_taa_outer_scale,
			settings.periphery_taa_center_area);
	}

	void ResetVRSpecificUpscalingSettings(Upscaling::Settings& settings)
	{
		const Upscaling::Settings defaults{};
		settings.aaVrs = false;
		settings.aaVrsVisualization = false;
		settings.aaVrsPerformanceMode = defaults.aaVrsPerformanceMode;
		settings.aaVrsPerformanceAnisotropy = defaults.aaVrsPerformanceAnisotropy;
		settings.aaVrsPassAware = defaults.aaVrsPassAware;
		settings.aaVrsContentAware = defaults.aaVrsContentAware;
		settings.aaVrsProtectWater = defaults.aaVrsProtectWater;
		settings.aaVrsSafeOpaqueOnly = defaults.aaVrsSafeOpaqueOnly;
		settings.aaVrsMaxRate = defaults.aaVrsMaxRate;
		settings.aaVrsPassTelemetry = defaults.aaVrsPassTelemetry;
		settings.experimentalDeferredCompositePS = defaults.experimentalDeferredCompositePS;
		settings.aaVrsDeferredComposite = defaults.aaVrsDeferredComposite;
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
		o_json.erase("aaVrs");
		o_json.erase("aaVrsVisualization");
		o_json.erase("aaVrsPerformanceMode");
		o_json.erase("aaVrsPerformanceAnisotropy");
		o_json.erase("aaVrsPassAware");
		o_json.erase("aaVrsContentAware");
		o_json.erase("aaVrsProtectWater");
		o_json.erase("aaVrsSafeOpaqueOnly");
		o_json.erase("aaVrsMaxRate");
		o_json.erase("aaVrsPassTelemetry");
		o_json.erase("experimentalDeferredCompositePS");
		o_json.erase("aaVrsDeferredComposite");
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

	void SetDynamicResolutionEnabledForUpscaling(bool a_enabled, bool a_forceDisabled = false)
	{
		if (!globals::game::isVR)
			return;

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
		if (*enabled != targetEnabled) {
			*enabled = targetEnabled;
		}

		changedByUpscaling = a_enabled || a_forceDisabled;
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
			a_upscaling.IsPerfModeActive() ||
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

		const bool perfModeActive = a_upscaling.IsPerfModeActive();
		const bool requestedMethodEligible = IsRenderScaleMethodEligible(a_upscaleMethod);
		const bool runtimeMethodEligible =
			perfModeActive &&
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
		       a_upscaling.IsPerfModeActive() ||
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

		return a_state->pendingPostLoadRuntimeReset ||
		       a_state->IsSaveLoadSafeModeActive();
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
		return IsVRRenderScalePostLoadResetRelevant(a_upscaling) ||
		       IsSaveLoadTransitionContextActive(a_state);
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

	bool ShouldApplyVRRenderScaleTransitionDuringLoadingMenu(const Upscaling& a_upscaling, const State* a_state)
	{
		return globals::game::isVR &&
		       IsLoadingMenuContextActive() &&
		       !IsMainMenuContextActive() &&
		       !IsUpscalingLoadTransitionContextActive(a_upscaling, a_state);
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
		(void)a_upscaling;
		return IsSaveLoadTransitionContextActive(a_state);
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

	bool ShouldDeferVRRenderScaleRelatchForPostLoadSettle(const Upscaling& a_upscaling, const State* a_state)
	{
		if (!a_upscaling.pendingPerfModeRenderTargetRecreatePostLoadSettle.load(std::memory_order_acquire))
			return false;
		if (!globals::game::isVR || !a_state)
			return false;

		const uint32_t closeFrame = g_vrLoadingTransitionCloseFrame.load(std::memory_order_acquire);
		if (closeFrame == 0)
			return false;

		const uint32_t currentFrame = std::max(a_state->frameCount, 1u);
		if (currentFrame < closeFrame)
			return true;
		if (IsLoadingMenuContextActive() || IsSaveLoadTransitionContextActive(a_state) || IsCommunityShadersMenuOpen())
			return true;

		const uint32_t lastCompletedWorldFrame = a_state->lastCompletedWorldRenderFrame;
		if (lastCompletedWorldFrame == std::numeric_limits<uint32_t>::max())
			return true;
		if (lastCompletedWorldFrame <= closeFrame)
			return true;

		return currentFrame <= lastCompletedWorldFrame;
	}

	const char* BoolText(bool a_value)
	{
		return a_value ? "yes" : "no";
	}

	uint32_t GetCurrentFrameForLog()
	{
		const auto* state = globals::state;
		return state ? std::max(state->frameCount, 1u) : 0u;
	}

	uint32_t ClearPendingVRFpsStabilizerLoadSync(Upscaling& a_upscaling)
	{
		a_upscaling.pendingVRFpsStabilizerSyncLastWaitLogFrame.store(0, std::memory_order_release);
		return a_upscaling.pendingVRFpsStabilizerSyncFrame.exchange(0, std::memory_order_acq_rel);
	}

	void CancelPendingVRFpsStabilizerLoadSync(Upscaling& a_upscaling, const char* a_reason)
	{
		const uint32_t queuedFrame = ClearPendingVRFpsStabilizerLoadSync(a_upscaling);
		if (queuedFrame == 0)
			return;

		logger::debug(
			"[Upscaling] VR FPS Stabilizer Sync cancelled before apply ({}): queuedFrame={}, frame={}.",
			a_reason && a_reason[0] != '\0' ? a_reason : "unspecified reason",
			queuedFrame,
			GetCurrentFrameForLog());
	}

	void LogPendingVRFpsStabilizerLoadSyncRetained(const Upscaling& a_upscaling, const char* a_reason)
	{
		const uint32_t queuedFrame = a_upscaling.pendingVRFpsStabilizerSyncFrame.load(std::memory_order_acquire);
		if (queuedFrame == 0)
			return;

		logger::debug(
			"[Upscaling] VR FPS Stabilizer Sync still pending ({}): queuedFrame={}, frame={}.",
			a_reason && a_reason[0] != '\0' ? a_reason : "unspecified reason",
			queuedFrame,
			GetCurrentFrameForLog());
	}

	void LogPendingVRFpsStabilizerLoadSyncWait(Upscaling& a_upscaling, const char* a_reason)
	{
		const uint32_t queuedFrame = a_upscaling.pendingVRFpsStabilizerSyncFrame.load(std::memory_order_acquire);
		if (queuedFrame == 0)
			return;

		const auto* state = globals::state;
		const uint32_t currentFrame = GetCurrentFrameForLog();
		const uint32_t logFrame = currentFrame != 0 ? currentFrame : queuedFrame;
		const uint32_t lastLogFrame = a_upscaling.pendingVRFpsStabilizerSyncLastWaitLogFrame.load(std::memory_order_acquire);
		if (lastLogFrame != 0 && logFrame >= lastLogFrame && logFrame - lastLogFrame < kVRFpsStabilizerSyncWaitLogIntervalFrames)
			return;

		a_upscaling.pendingVRFpsStabilizerSyncLastWaitLogFrame.store(logFrame, std::memory_order_release);
		logger::debug(
			"[Upscaling] VR FPS Stabilizer Sync waiting for {}: queuedFrame={}, frame={}, loadingMenu={}, inWorld={}.",
			a_reason && a_reason[0] != '\0' ? a_reason : "world-ready state",
			queuedFrame,
			currentFrame,
			BoolText(IsLoadingMenuContextActive()),
			BoolText(state && state->inWorld));
	}

	template <class Fn>
	bool LogVRTransitionDiagnosticOnce(bool& a_logged, Fn&& a_log)
	{
		if (a_logged)
			return false;

		std::forward<Fn>(a_log)();
		a_logged = true;
		return true;
	}

	enum class VRTransitionDiagnosticFlag : uint64_t
	{
		LoadingMenu = 1ull << 0,
		LoadingTail = 1ull << 1,
		CellTail = 1ull << 2,
		SaveLoad = 1ull << 3,
		RenderScaleRelevant = 1ull << 4,
		PendingVRTransition = 1ull << 5,
		PendingVRRenderScaleTransition = 1ull << 6,
		PendingRelatch = 1ull << 7,
		RelatchInProgress = 1ull << 8,
		PendingDLSSReset = 1ull << 9,
		PendingFSRReset = 1ull << 10,
		PendingDLSSHistoryReset = 1ull << 11,
		PostLoadReset = 1ull << 12,
		HMDMaskDeferred = 1ull << 13,
		ProjectedMaskDeferred = 1ull << 14,
		FoveatedBypass = 1ull << 15,
		VendorResetPending = 1ull << 16,
		SubmitStageDeviceLost = 1ull << 17,
		VRRenderScaleModeActive = 1ull << 18,
		VRRenderScaleModeRequested = 1ull << 19,
		RenderScaleToggleRequested = 1ull << 20,
		CellContext = 1ull << 21,
	};

	constexpr uint64_t FlagBit(VRTransitionDiagnosticFlag a_flag)
	{
		return static_cast<uint64_t>(a_flag);
	}

	void SetDiagnosticFlag(uint64_t& a_flags, VRTransitionDiagnosticFlag a_flag, bool a_value)
	{
		if (a_value)
			a_flags |= FlagBit(a_flag);
	}

	bool HasDiagnosticFlag(uint64_t a_flags, VRTransitionDiagnosticFlag a_flag)
	{
		return (a_flags & FlagBit(a_flag)) != 0;
	}

	struct VRTransitionDiagnosticSnapshot
	{
		uint32_t frame = 0;
		uint64_t flags = 0;
		Upscaling::UpscaleMethod requestedMethod = Upscaling::UpscaleMethod::kNONE;
		Upscaling::UpscaleMethod runtimeMethod = Upscaling::UpscaleMethod::kNONE;
		uint32_t qualityMode = 0;
		uint32_t closeAge = 0;
		uint32_t relatchAge = 0;
		uint32_t relatchDelay = 0;
		uint32_t screenWidth = 0;
		uint32_t screenHeight = 0;
	};

	struct VRTransitionDiagnosticHistory
	{
		bool initialized = false;
		bool interesting = false;
		VRTransitionDiagnosticSnapshot last{};
		uint32_t hmdMaskStartFrame = 0;
		uint32_t projectedMaskStartFrame = 0;
		uint32_t foveatedBypassStartFrame = 0;
		uint32_t relatchPendingStartFrame = 0;
		uint32_t vendorResetStartFrame = 0;
		const char* repeatCategory = nullptr;
		uint64_t repeatSignature = 0;
		uint32_t repeatCount = 0;
		uint32_t repeatStartFrame = 0;
		uint32_t repeatLastFrame = 0;
		uint32_t repeatLastSummaryCount = 0;
		uint32_t repeatLastSummaryFrame = 0;
		VRTransitionDiagnosticSnapshot repeatLast{};
	};

	VRTransitionDiagnosticHistory g_vrTransitionDiagnostics;
	constexpr uint32_t kVRTransitionRepeatSummaryCount = 20u;
	constexpr uint32_t kVRTransitionRepeatSummaryFrames = 120u;

	VRTransitionDiagnosticSnapshot BuildVRTransitionDiagnosticSnapshot(const Upscaling& a_upscaling)
	{
		VRTransitionDiagnosticSnapshot snapshot{};
		const auto* state = globals::state;
		snapshot.frame = state ? state->frameCount : 0u;
		snapshot.requestedMethod = a_upscaling.GetConfiguredUpscaleMethodForTransition();
		snapshot.runtimeMethod = a_upscaling.GetRuntimeUpscaleMethod();
		snapshot.qualityMode = a_upscaling.GetRuntimeQualityMode();
		if (state) {
			snapshot.screenWidth = static_cast<uint32_t>(std::max(state->screenSize.x, 0.0f));
			snapshot.screenHeight = static_cast<uint32_t>(std::max(state->screenSize.y, 0.0f));
		}

		const bool loadingMenu = IsLoadingMenuContextActive();
		const bool loadingTail = IsLoadingTransitionTailActive(state);
		const bool cellTail = IsCellTransitionTailActive(state);
		const bool cellContext = IsCellTransitionContextActive(state);
		const bool saveLoad = IsSaveLoadTransitionContextActive(state);
		const bool renderScaleRelevant = IsVRRenderScaleTransitionSafetyRelevant(a_upscaling, snapshot.requestedMethod);
		const bool pendingVRTransition = a_upscaling.HasPendingVRUpscalingTransition();
		const bool pendingRenderScaleTransition = a_upscaling.HasPendingVRRenderScaleTransition();
		const bool pendingRelatch = a_upscaling.pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire);
		const bool relatchInProgress = a_upscaling.perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire);
		const bool pendingDLSSReset = a_upscaling.pendingDLSSReset.load(std::memory_order_acquire);
		const bool pendingFSRReset = a_upscaling.pendingFSRReset.load(std::memory_order_acquire);
		const bool pendingDLSSHistoryReset = a_upscaling.pendingDLSSHistoryReset.load(std::memory_order_acquire);
		const bool postLoadReset = IsVRRenderScalePostLoadResetRelevant(a_upscaling, snapshot.requestedMethod);
		const bool foveatedBypass = ShouldBypassVRFoveatedVendorDispatchForTransition(a_upscaling, state);
		const bool hmdMaskDeferred = ShouldDeferVRTransitionMaskRepair(a_upscaling, state);
		const bool projectedMaskDeferred = ShouldDeferVRProjectedMaskRepair(a_upscaling, state);
		const bool vendorResetPending = HasPendingVRVendorRuntimeReset(a_upscaling, snapshot.runtimeMethod);

		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::LoadingMenu, loadingMenu);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::LoadingTail, loadingTail);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::CellTail, cellTail);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::CellContext, cellContext);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::SaveLoad, saveLoad);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::RenderScaleRelevant, renderScaleRelevant);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingVRTransition, pendingVRTransition);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingVRRenderScaleTransition, pendingRenderScaleTransition);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingRelatch, pendingRelatch);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::RelatchInProgress, relatchInProgress);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingDLSSReset, pendingDLSSReset);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingFSRReset, pendingFSRReset);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingDLSSHistoryReset, pendingDLSSHistoryReset);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PostLoadReset, postLoadReset);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::HMDMaskDeferred, hmdMaskDeferred);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::ProjectedMaskDeferred, projectedMaskDeferred);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::FoveatedBypass, foveatedBypass);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::VendorResetPending, vendorResetPending);
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::SubmitStageDeviceLost, a_upscaling.IsSubmitStageDeviceLost());
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::VRRenderScaleModeActive, a_upscaling.IsVRRenderScaleModeActive());
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::VRRenderScaleModeRequested, a_upscaling.GetPerfModeRequested());
		SetDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::RenderScaleToggleRequested, a_upscaling.GetVRRenderScaleModeRequested());

		const uint32_t closeFrame = g_vrLoadingTransitionCloseFrame.load(std::memory_order_acquire);
		snapshot.closeAge = closeFrame != 0 ? ElapsedFrames(closeFrame, snapshot.frame) : 0u;
		const uint32_t relatchFrame = a_upscaling.pendingPerfModeRenderTargetRecreateFrame.load(std::memory_order_acquire);
		snapshot.relatchAge = relatchFrame != 0 ? ElapsedFrames(relatchFrame, snapshot.frame) : 0u;
		snapshot.relatchDelay = a_upscaling.pendingPerfModeRenderTargetRecreateDelayFrames.load(std::memory_order_acquire);
		return snapshot;
	}

	bool IsVRTransitionSnapshotInteresting(const VRTransitionDiagnosticSnapshot& a_snapshot)
	{
		constexpr uint64_t interestingFlags =
			FlagBit(VRTransitionDiagnosticFlag::LoadingMenu) |
			FlagBit(VRTransitionDiagnosticFlag::LoadingTail) |
			FlagBit(VRTransitionDiagnosticFlag::CellTail) |
			FlagBit(VRTransitionDiagnosticFlag::CellContext) |
			FlagBit(VRTransitionDiagnosticFlag::SaveLoad) |
			FlagBit(VRTransitionDiagnosticFlag::PendingVRTransition) |
			FlagBit(VRTransitionDiagnosticFlag::PendingVRRenderScaleTransition) |
			FlagBit(VRTransitionDiagnosticFlag::PendingRelatch) |
			FlagBit(VRTransitionDiagnosticFlag::RelatchInProgress) |
			FlagBit(VRTransitionDiagnosticFlag::PendingDLSSReset) |
			FlagBit(VRTransitionDiagnosticFlag::PendingFSRReset) |
			FlagBit(VRTransitionDiagnosticFlag::PendingDLSSHistoryReset) |
			FlagBit(VRTransitionDiagnosticFlag::PostLoadReset) |
			FlagBit(VRTransitionDiagnosticFlag::HMDMaskDeferred) |
			FlagBit(VRTransitionDiagnosticFlag::ProjectedMaskDeferred) |
			FlagBit(VRTransitionDiagnosticFlag::FoveatedBypass) |
			FlagBit(VRTransitionDiagnosticFlag::VendorResetPending) |
			FlagBit(VRTransitionDiagnosticFlag::SubmitStageDeviceLost);

		return (a_snapshot.flags & interestingFlags) != 0;
	}

	const char* GetVRTransitionRepeatCategory(const char* a_event)
	{
		if (!a_event || !*a_event)
			return nullptr;

		const std::string_view event(a_event);
		if (event.find("vendor runtime reset deferred") != std::string_view::npos)
			return "vendor runtime reset deferred";
		if (event.find("vendor teardown deferred") != std::string_view::npos)
			return "vendor teardown deferred";
		if (event.find("render-target relatch deferred") != std::string_view::npos)
			return "render-target relatch deferred";
		if (event.find("vendor runtime reset waiting") != std::string_view::npos)
			return "vendor runtime reset waiting";
		if (event.find("skipped FSR full-frame dispatch") != std::string_view::npos)
			return "skipped FSR full-frame dispatch";

		return nullptr;
	}

	uint64_t MixVRTransitionDiagnosticValue(uint64_t a_seed, uint64_t a_value)
	{
		return a_seed ^ (a_value + 0x9e3779b97f4a7c15ull + (a_seed << 6) + (a_seed >> 2));
	}

	uint64_t GetVRTransitionRepeatSignature(const VRTransitionDiagnosticSnapshot& a_snapshot)
	{
		constexpr uint64_t volatileFlags =
			FlagBit(VRTransitionDiagnosticFlag::PendingDLSSReset) |
			FlagBit(VRTransitionDiagnosticFlag::PendingFSRReset) |
			FlagBit(VRTransitionDiagnosticFlag::PendingDLSSHistoryReset) |
			FlagBit(VRTransitionDiagnosticFlag::VendorResetPending);

		uint64_t signature = a_snapshot.flags & ~volatileFlags;
		signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint64_t>(a_snapshot.requestedMethod));
		signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint64_t>(a_snapshot.runtimeMethod));
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.qualityMode);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.screenWidth);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.screenHeight);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.relatchDelay);
		return signature;
	}

	void ResetVRTransitionRepeatSummary(VRTransitionDiagnosticHistory& a_history)
	{
		a_history.repeatCategory = nullptr;
		a_history.repeatSignature = 0;
		a_history.repeatCount = 0;
		a_history.repeatStartFrame = 0;
		a_history.repeatLastFrame = 0;
		a_history.repeatLastSummaryCount = 0;
		a_history.repeatLastSummaryFrame = 0;
		a_history.repeatLast = {};
	}

	void LogVRTransitionRepeatSummary(VRTransitionDiagnosticHistory& a_history, bool a_final)
	{
		if (!a_history.repeatCategory || a_history.repeatCount <= a_history.repeatLastSummaryCount)
			return;

		const uint32_t repeatedSinceSummary = a_history.repeatCount - a_history.repeatLastSummaryCount;
		if (repeatedSinceSummary == 0)
			return;

		const auto& snapshot = a_history.repeatLast;
		VR_TRANSITION_DIAG_LOG(
			"[VRTransition] {} {}: {} additional occurrences over {} frames (total={}, lastFrame={}, req={}, runtime={}, quality={}, renderScaleRelevant={}, relatchPending={}, relatchInProgress={}, relatchAge={}, relatchDelay={}, vendorPending={}, pendingReset(DLSS={},FSR={},DLSSHistory={}), submitDeviceLost={})",
			a_history.repeatCategory,
			a_final ? "summary" : "still repeating",
			repeatedSinceSummary,
			ElapsedFrames(a_history.repeatStartFrame, a_history.repeatLastFrame),
			a_history.repeatCount,
			snapshot.frame,
			magic_enum::enum_name(snapshot.requestedMethod),
			magic_enum::enum_name(snapshot.runtimeMethod),
			snapshot.qualityMode,
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::RenderScaleRelevant)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingRelatch)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::RelatchInProgress)),
			snapshot.relatchAge,
			snapshot.relatchDelay,
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::VendorResetPending)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingDLSSReset)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingFSRReset)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingDLSSHistoryReset)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::SubmitStageDeviceLost)));
		a_history.repeatLastSummaryCount = a_history.repeatCount;
		a_history.repeatLastSummaryFrame = a_history.repeatLastFrame;
	}

	bool ShouldSuppressVRTransitionRepeatSnapshot(
		VRTransitionDiagnosticHistory& a_history,
		const char* a_repeatCategory,
		const VRTransitionDiagnosticSnapshot& a_snapshot)
	{
		const uint64_t signature = GetVRTransitionRepeatSignature(a_snapshot);
		if (a_history.repeatCategory != a_repeatCategory || a_history.repeatSignature != signature) {
			LogVRTransitionRepeatSummary(a_history, true);
			ResetVRTransitionRepeatSummary(a_history);
			a_history.repeatCategory = a_repeatCategory;
			a_history.repeatSignature = signature;
			a_history.repeatCount = 1;
			a_history.repeatStartFrame = std::max(a_snapshot.frame, 1u);
			a_history.repeatLastFrame = a_snapshot.frame;
			a_history.repeatLastSummaryCount = 1;
			a_history.repeatLastSummaryFrame = a_snapshot.frame;
			a_history.repeatLast = a_snapshot;
			return false;
		}

		++a_history.repeatCount;
		a_history.repeatLastFrame = a_snapshot.frame;
		a_history.repeatLast = a_snapshot;
		if (a_history.repeatCount - a_history.repeatLastSummaryCount >= kVRTransitionRepeatSummaryCount ||
			ElapsedFrames(a_history.repeatLastSummaryFrame, a_snapshot.frame) >= kVRTransitionRepeatSummaryFrames) {
			LogVRTransitionRepeatSummary(a_history, false);
		}
		return true;
	}

	void LogVRTransitionGuardChange(
		const char* a_name,
		VRTransitionDiagnosticFlag a_flag,
		const VRTransitionDiagnosticSnapshot& a_snapshot,
		bool a_previousActive,
		uint32_t& a_startFrame)
	{
		const bool active = HasDiagnosticFlag(a_snapshot.flags, a_flag);
		if (active == a_previousActive)
			return;

		if (active) {
			a_startFrame = std::max(a_snapshot.frame, 1u);
			VR_TRANSITION_DIAG_LOG(
				"[VRTransition] {} entered at frame {} (req={}, runtime={}, closeAge={}, relatchAge={}, relatchDelay={})",
				a_name,
				a_snapshot.frame,
				magic_enum::enum_name(a_snapshot.requestedMethod),
				magic_enum::enum_name(a_snapshot.runtimeMethod),
				a_snapshot.closeAge,
				a_snapshot.relatchAge,
				a_snapshot.relatchDelay);
			return;
		}

		const uint32_t duration = a_startFrame != 0 ? ElapsedFrames(a_startFrame, a_snapshot.frame) : 0u;
		VR_TRANSITION_DIAG_LOG(
			"[VRTransition] {} exited at frame {} after {} frames (req={}, runtime={})",
			a_name,
			a_snapshot.frame,
			duration,
			magic_enum::enum_name(a_snapshot.requestedMethod),
			magic_enum::enum_name(a_snapshot.runtimeMethod));
		a_startFrame = 0;
	}

	void LogVRTransitionDiagnostics(const Upscaling& a_upscaling, const char* a_event = nullptr, bool a_force = false)
	{
#if !VR_TRANSITION_DIAG_ENABLED
		(void)a_upscaling;
		(void)a_event;
		(void)a_force;
		return;
#else
		if (!globals::game::isVR)
			return;

		const auto snapshot = BuildVRTransitionDiagnosticSnapshot(a_upscaling);
		const bool interesting = IsVRTransitionSnapshotInteresting(snapshot);
		auto& history = g_vrTransitionDiagnostics;
		const bool stateChanged =
			!history.initialized ||
			history.last.flags != snapshot.flags ||
			history.last.requestedMethod != snapshot.requestedMethod ||
			history.last.runtimeMethod != snapshot.runtimeMethod ||
			history.last.qualityMode != snapshot.qualityMode ||
			history.last.screenWidth != snapshot.screenWidth ||
			history.last.screenHeight != snapshot.screenHeight;
		const bool shouldLog = a_force || stateChanged || (history.interesting && !interesting);
		const char* repeatCategory = GetVRTransitionRepeatCategory(a_event);
		if (!shouldLog && !interesting && !repeatCategory)
			return;

		const auto previousFlags = history.initialized ? history.last.flags : 0u;
		LogVRTransitionGuardChange(
			"HAM/HMD mask clear guard",
			VRTransitionDiagnosticFlag::HMDMaskDeferred,
			snapshot,
			HasDiagnosticFlag(previousFlags, VRTransitionDiagnosticFlag::HMDMaskDeferred),
			history.hmdMaskStartFrame);
		LogVRTransitionGuardChange(
			"Projected HAM/FOV mask repair guard",
			VRTransitionDiagnosticFlag::ProjectedMaskDeferred,
			snapshot,
			HasDiagnosticFlag(previousFlags, VRTransitionDiagnosticFlag::ProjectedMaskDeferred),
			history.projectedMaskStartFrame);
		LogVRTransitionGuardChange(
			"FOV foveated vendor bypass",
			VRTransitionDiagnosticFlag::FoveatedBypass,
			snapshot,
			HasDiagnosticFlag(previousFlags, VRTransitionDiagnosticFlag::FoveatedBypass),
			history.foveatedBypassStartFrame);
		LogVRTransitionGuardChange(
			"Render-target relatch pending",
			VRTransitionDiagnosticFlag::PendingRelatch,
			snapshot,
			HasDiagnosticFlag(previousFlags, VRTransitionDiagnosticFlag::PendingRelatch),
			history.relatchPendingStartFrame);
		LogVRTransitionGuardChange(
			"Vendor runtime reset pending",
			VRTransitionDiagnosticFlag::VendorResetPending,
			snapshot,
			HasDiagnosticFlag(previousFlags, VRTransitionDiagnosticFlag::VendorResetPending),
			history.vendorResetStartFrame);

		bool suppressSnapshot = false;
		bool logSnapshot = shouldLog;
		if (repeatCategory) {
			suppressSnapshot = ShouldSuppressVRTransitionRepeatSnapshot(history, repeatCategory, snapshot);
			logSnapshot = logSnapshot || !suppressSnapshot;
		} else if (logSnapshot) {
			LogVRTransitionRepeatSummary(history, true);
			ResetVRTransitionRepeatSummary(history);
		}

		if (logSnapshot && !suppressSnapshot) {
			const char* event = a_event && *a_event ? a_event : (stateChanged ? "state changed" : "state stable");
			VR_TRANSITION_DIAG_LOG(
				"[VRTransition] {} frame={} req={} runtime={} quality={} screen={}x{} renderScaleRelevant={} renderScaleToggleRequested={} vrRenderScaleRequested={} vrRenderScaleActive={} loadingMenu={} loadingTail={} cellTail={} cellContext={} saveLoad={} closeAge={} pendingVR={} pendingRenderScale={} relatchPending={} relatchInProgress={} relatchAge={} relatchDelay={} hmdDefer={} projectedDefer={} foveatedBypass={} vendorPending={} pendingReset(DLSS={},FSR={},DLSSHistory={}) postLoadReset={} submitDeviceLost={}",
				event,
				snapshot.frame,
				magic_enum::enum_name(snapshot.requestedMethod),
				magic_enum::enum_name(snapshot.runtimeMethod),
				snapshot.qualityMode,
				snapshot.screenWidth,
				snapshot.screenHeight,
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::RenderScaleRelevant)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::RenderScaleToggleRequested)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::VRRenderScaleModeRequested)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::VRRenderScaleModeActive)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::LoadingMenu)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::LoadingTail)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::CellTail)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::CellContext)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::SaveLoad)),
				snapshot.closeAge,
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingVRTransition)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingVRRenderScaleTransition)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingRelatch)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::RelatchInProgress)),
				snapshot.relatchAge,
				snapshot.relatchDelay,
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::HMDMaskDeferred)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::ProjectedMaskDeferred)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::FoveatedBypass)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::VendorResetPending)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingDLSSReset)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingFSRReset)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingDLSSHistoryReset)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PostLoadReset)),
				BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::SubmitStageDeviceLost)));
		}

		history.last = snapshot;
		history.interesting = interesting;
		history.initialized = true;
#endif
	}

	const char* VREyeName(vr::EVREye a_eye)
	{
		switch (a_eye) {
		case vr::Eye_Left:
			return "left";
		case vr::Eye_Right:
			return "right";
		default:
			return "unknown";
		}
	}

	const char* DiagnosticText(const char* a_text, const char* a_fallback)
	{
		return a_text && *a_text ? a_text : a_fallback;
	}

	const char* DiagnosticFlagText(const VRTransitionDiagnosticSnapshot& a_snapshot, VRTransitionDiagnosticFlag a_flag)
	{
		return BoolText(HasDiagnosticFlag(a_snapshot.flags, a_flag));
	}

	struct VRBoundsDiagnosticInfo
	{
		float uMin = -1.0f;
		float vMin = -1.0f;
		float uMax = -1.0f;
		float vMax = -1.0f;
	};

	VRBoundsDiagnosticInfo BuildVRBoundsDiagnosticInfo(const vr::VRTextureBounds_t* a_bounds)
	{
		VRBoundsDiagnosticInfo info{};
		if (!a_bounds)
			return info;

		info.uMin = a_bounds->uMin;
		info.vMin = a_bounds->vMin;
		info.uMax = a_bounds->uMax;
		info.vMax = a_bounds->vMax;
		return info;
	}

	bool ShouldLogVRSubmitPathDiagnosticSnapshot(const VRTransitionDiagnosticSnapshot& a_snapshot)
	{
		if (IsVRTransitionSnapshotInteresting(a_snapshot))
			return true;

		const uint32_t closeFrame = g_vrLoadingTransitionCloseFrame.load(std::memory_order_acquire);
		if (closeFrame != 0 && a_snapshot.frame >= closeFrame && a_snapshot.frame - closeFrame <= 300u)
			return true;

		return IsCommunityShadersMenuOpen() &&
		       (IsVendorUpscalingMethod(a_snapshot.requestedMethod) ||
		           IsVendorUpscalingMethod(a_snapshot.runtimeMethod) ||
		           HasDiagnosticFlag(a_snapshot.flags, VRTransitionDiagnosticFlag::VRRenderScaleModeActive) ||
		           HasDiagnosticFlag(a_snapshot.flags, VRTransitionDiagnosticFlag::VRRenderScaleModeRequested) ||
		           HasDiagnosticFlag(a_snapshot.flags, VRTransitionDiagnosticFlag::RenderScaleToggleRequested));
	}

	bool TryBuildVRSubmitPathDiagnosticSnapshot(const Upscaling& a_upscaling, VRTransitionDiagnosticSnapshot& a_snapshot)
	{
#if !VR_TRANSITION_DIAG_ENABLED
		(void)a_upscaling;
		(void)a_snapshot;
		return false;
#else
		if (!globals::game::isVR || !globals::state)
			return false;

		a_snapshot = BuildVRTransitionDiagnosticSnapshot(a_upscaling);
		return ShouldLogVRSubmitPathDiagnosticSnapshot(a_snapshot);
#endif
	}

	struct VRTextureDiagnosticInfo
	{
		bool valid = false;
		const void* handle = nullptr;
		uint32_t type = 0;
		uint32_t colorSpace = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t arraySize = 0;
		uint32_t samples = 0;
		uint32_t format = 0;
		bool presentationTarget = false;
	};

	VRTextureDiagnosticInfo BuildVRTextureDiagnosticInfo(const vr::Texture_t* a_texture)
	{
		VRTextureDiagnosticInfo info{};
		if (!a_texture)
			return info;

		info.handle = a_texture->handle;
		info.type = static_cast<uint32_t>(a_texture->eType);
		info.colorSpace = static_cast<uint32_t>(a_texture->eColorSpace);

		if (!a_texture->handle || a_texture->eType != vr::TextureType_DirectX)
			return info;

		auto* texture = static_cast<ID3D11Texture2D*>(a_texture->handle);
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);

		info.valid = true;
		info.width = desc.Width;
		info.height = desc.Height;
		info.arraySize = desc.ArraySize;
		info.samples = desc.SampleDesc.Count;
		info.format = static_cast<uint32_t>(desc.Format);
		info.presentationTarget = IsVRPresentationRenderTargetTexture(texture);
		return info;
	}

	void LogVRSubmitStagePathDiagnostics(
		const Upscaling& a_upscaling,
		const char* a_path,
		vr::EVREye a_eye,
		const vr::VRTextureBounds_t* a_inputBounds,
		const D3D11_TEXTURE2D_DESC& a_sourceDesc,
		const D3D11_BOX& a_sourceBox,
		uint32_t a_sourceSubresource,
		uint32_t a_inputEyeWidth,
		uint32_t a_inputEyeHeight,
		uint32_t a_outputEyeWidth,
		uint32_t a_outputEyeHeight,
		bool a_targetScaleMode,
		bool a_presentationOnly,
		bool a_foveatedRequested,
		bool a_presentationRenderTarget)
	{
		VRTransitionDiagnosticSnapshot snapshot{};
		if (!TryBuildVRSubmitPathDiagnosticSnapshot(a_upscaling, snapshot))
			return;

		const auto inputBounds = BuildVRBoundsDiagnosticInfo(a_inputBounds);
		VR_TRANSITION_DIAG_LOG(
			"[VRSubmitStage] {} frame={} eye={} closeAge={} req={} runtime={} quality={} targetScale={} presentationOnly={} foveatedRequested={} source={}x{} fmt={} array={} samples={} sourceSubresource={} sourceBox=({},{})->({},{}) inputEye={}x{} outputEye={}x{} inputBounds=({:.4f},{:.4f})->({:.4f},{:.4f}) presentationRT={} relatchPending={} vendorPending={} hmdDefer={} projectedDefer={}",
			DiagnosticText(a_path, "unknown"),
			snapshot.frame,
			VREyeName(a_eye),
			snapshot.closeAge,
			magic_enum::enum_name(snapshot.requestedMethod),
			magic_enum::enum_name(snapshot.runtimeMethod),
			snapshot.qualityMode,
			BoolText(a_targetScaleMode),
			BoolText(a_presentationOnly),
			BoolText(a_foveatedRequested),
			a_sourceDesc.Width,
			a_sourceDesc.Height,
			static_cast<uint32_t>(a_sourceDesc.Format),
			a_sourceDesc.ArraySize,
			a_sourceDesc.SampleDesc.Count,
			a_sourceSubresource,
			a_sourceBox.left,
			a_sourceBox.top,
			a_sourceBox.right,
			a_sourceBox.bottom,
			a_inputEyeWidth,
			a_inputEyeHeight,
			a_outputEyeWidth,
			a_outputEyeHeight,
			inputBounds.uMin,
			inputBounds.vMin,
			inputBounds.uMax,
			inputBounds.vMax,
			BoolText(a_presentationRenderTarget),
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::PendingRelatch),
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::VendorResetPending),
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::HMDMaskDeferred),
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::ProjectedMaskDeferred));
	}

	void LogVRHMDMaskClearDispatch(
		const Upscaling& a_upscaling,
		const char* a_phase,
		uint32_t a_depthWidth,
		uint32_t a_depthHeight,
		uint32_t a_colorWidth,
		uint32_t a_colorHeight,
		uint32_t a_depthOffsetX,
		uint32_t a_colorOffsetX,
		uint32_t a_depthOffsetY,
		uint32_t a_colorOffsetY)
	{
		VRTransitionDiagnosticSnapshot snapshot{};
		if (!TryBuildVRSubmitPathDiagnosticSnapshot(a_upscaling, snapshot))
			return;

		VR_TRANSITION_DIAG_LOG(
			"[VRMask] HMD clear dispatch phase={} frame={} closeAge={} req={} runtime={} quality={} depth={}x{} color={}x{} depthOffset=({}, {}) colorOffset=({}, {}) relatchPending={} hmdDefer={} projectedDefer={}",
			DiagnosticText(a_phase, "<unknown>"),
			snapshot.frame,
			snapshot.closeAge,
			magic_enum::enum_name(snapshot.requestedMethod),
			magic_enum::enum_name(snapshot.runtimeMethod),
			snapshot.qualityMode,
			a_depthWidth,
			a_depthHeight,
			a_colorWidth,
			a_colorHeight,
			a_depthOffsetX,
			a_depthOffsetY,
			a_colorOffsetX,
			a_colorOffsetY,
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::PendingRelatch),
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::HMDMaskDeferred),
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::ProjectedMaskDeferred));
	}

	void LogVRProjectedMaskRepairDispatch(const Upscaling& a_upscaling, const char* a_context, float a_width, float a_height)
	{
		VRTransitionDiagnosticSnapshot snapshot{};
		if (!TryBuildVRSubmitPathDiagnosticSnapshot(a_upscaling, snapshot))
			return;

		VR_TRANSITION_DIAG_LOG(
			"[VRMask] Projected/underwater mask repair dispatch context={} frame={} closeAge={} req={} runtime={} quality={} viewport={:.0f}x{:.0f} relatchPending={} hmdDefer={} projectedDefer={}",
			DiagnosticText(a_context, "<unknown>"),
			snapshot.frame,
			snapshot.closeAge,
			magic_enum::enum_name(snapshot.requestedMethod),
			magic_enum::enum_name(snapshot.runtimeMethod),
			snapshot.qualityMode,
			a_width,
			a_height,
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::PendingRelatch),
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::HMDMaskDeferred),
			DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::ProjectedMaskDeferred));
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
		LogVRTransitionDiagnostics(a_upscaling, "queued vendor reset after loading menu", true);
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

	bool IsVRMenuPresentationContextActive()
	{
		return globals::game::isVR &&
		       (IsKnownGameMenuContextActive() ||
		        IsVRMenuPresentationTailActive(globals::state));
	}

	bool IsVRMenuScenePresentationBlockActive()
	{
		return globals::game::isVR && IsMainOrLoadingMenuContextActive();
	}

	bool IsVRSceneFeatureMenuPauseContextActive()
	{
		return globals::game::isVR &&
		       (IsVRMenuPresentationContextActive() || IsCommunityShadersMenuOpen());
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

	bool IsGameMenuContextActive()
	{
		return IsKnownGameMenuContextActive();
	}

	bool IsVRKnownGameMenuLayerSeparationContextActive(const Upscaling& a_upscaling)
	{
		if (!globals::game::isVR ||
			!IsKnownGameMenuContextActive() ||
			IsCommunityShadersMenuOpen() ||
			IsMainOrLoadingMenuContextActive() ||
			IsSaveLoadTransitionContextActive()) {
			return false;
		}

		auto state = globals::state;
		if (state && state->isMapMenuOpen)
			return false;

		auto ui = globals::game::ui;
		if (ui &&
			(ui->IsMenuOpen(RE::MapMenu::MENU_NAME) ||
			 ui->IsMenuOpen("StatsMenu"))) {
			return false;
		}

		const auto& resolutionPlan = a_upscaling.GetRuntimeResolutionPlan();
		return resolutionPlan.owner == Upscaling::ResolutionOwner::VRRenderScaleMode &&
		       a_upscaling.IsPresentationUpscalingActive() &&
		       a_upscaling.IsSubmitStageUpscalingActive() &&
		       IsVendorUpscalingMethod(a_upscaling.GetRuntimeUpscaleMethod()) &&
		       a_upscaling.GetRuntimeQualityMode() > 0;
	}

	bool IsSubmitStageMenuPresentationContextActive()
	{
		if (!globals::game::isVR)
			return false;

		// Keep VR menus off the submit-stage path entirely. The full-size protected
		// menu targets already preserve coverage, and allowing final-eye submit-stage
		// upscaling here makes late glyph/text passes appear head-relative and fuzzy.
		return false;
	}

	enum class VRPresentationDiagnosticSlot : uint8_t
	{
		FadeRender,
		FadeDispatch,
		MenuDraw,
		TemporalAAUIRender,
		TemporalAAUIDispatch,
		LightingCompositeMenuRender,
		LightingCompositeMenuDispatch,
		DynamicUpsampleRender,
		DynamicUpsampleDispatch,
		MainPostProcessing,
		SubmitStage,
		Count
	};

	void LogVRPresentationPassDiagnostics(
		const Upscaling& a_upscaling,
		VRPresentationDiagnosticSlot a_slot,
		const char* a_passName,
		const char* a_phase,
		bool a_includeKnownTargets);

	struct D3DViewDiagnosticInfo
	{
		bool valid = false;
		uintptr_t view = 0;
		uintptr_t resource = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t arraySize = 0;
		uint32_t mipLevels = 0;
		uint32_t format = 0;
		uint32_t viewFormat = 0;
		uint32_t viewDimension = 0;
		uint32_t samples = 0;
		uint32_t bindFlags = 0;
		bool presentationTarget = false;
		std::string engineName = "NONE";
		std::string debugName;
	};

	struct D3DShaderDiagnosticInfo
	{
		uintptr_t shader = 0;
		std::string debugName;
	};

	struct D3DBufferDiagnosticInfo
	{
		bool valid = false;
		uintptr_t buffer = 0;
		uint32_t byteWidth = 0;
		uint32_t usage = 0;
		uint32_t bindFlags = 0;
		uint32_t cpuAccessFlags = 0;
		uint32_t miscFlags = 0;
		bool contentHashValid = false;
		uint64_t contentHash = 0;
		std::string debugName;
	};

	struct VRPresentationDiagnosticSnapshot
	{
		uint32_t frame = 0;
		Upscaling::UpscaleMethod requestedMethod = Upscaling::UpscaleMethod::kNONE;
		Upscaling::UpscaleMethod runtimeMethod = Upscaling::UpscaleMethod::kNONE;
		Upscaling::ResolutionOwner owner = Upscaling::ResolutionOwner::Native;
		Upscaling::UpscalingOutputTarget outputTarget = Upscaling::UpscalingOutputTarget::Main;
		uint32_t qualityMode = 0;
		uint32_t screenWidth = 0;
		uint32_t screenHeight = 0;
		uint32_t engineWidth = 0;
		uint32_t engineHeight = 0;
		uint32_t finalWidth = 0;
		uint32_t finalHeight = 0;
		float resolutionScaleX = 1.0f;
		float resolutionScaleY = 1.0f;
		float dynamicWidthRatio = 1.0f;
		float dynamicHeightRatio = 1.0f;
		float previousDynamicWidthRatio = 1.0f;
		float previousDynamicHeightRatio = 1.0f;
		bool dynamicResolutionLock = true;
		bool knownMenu = false;
		bool gameMenu = false;
		bool communityShadersMenu = false;
		bool mainOrLoadingMenu = false;
		bool loadingMenu = false;
		bool saveLoad = false;
		bool vrMenuPresentation = false;
		bool submitMenuPresentation = false;
		bool renderScaleActive = false;
		bool renderScaleRequested = false;
		bool perfModeActive = false;
		bool submitStageActive = false;
		bool submitStageDeviceLost = false;
		bool presentationUpscalingActive = false;
		bool fullResolutionMenuUIDraw = false;
		bool menuTextRasterDiagnosticContext = false;
		bool menuPlaneDiagnosticContext = false;
		bool currentRTPresentation = false;
		uint32_t viewportCount = 0;
		std::array<D3D11_VIEWPORT, 2> viewports{};
		uint32_t scissorCount = 0;
		std::array<D3D11_RECT, 2> scissors{};
		std::array<D3DViewDiagnosticInfo, 4> psSRVs{};
		std::array<D3DViewDiagnosticInfo, 4> csSRVs{};
		std::array<D3DViewDiagnosticInfo, 4> rtvs{};
		D3DViewDiagnosticInfo dsv{};
		D3DShaderDiagnosticInfo vertexShader{};
		D3DShaderDiagnosticInfo geometryShader{};
		D3DShaderDiagnosticInfo pixelShader{};
		std::array<D3DBufferDiagnosticInfo, 4> vertexCBs{};
		std::array<D3DBufferDiagnosticInfo, 4> pixelCBs{};
		uint64_t signature = 0;
	};

	struct VRPresentationDiagnosticHistory
	{
		bool initialized = false;
		uint64_t key = 0;
		uint64_t signature = 0;
		std::string passName;
		std::string phase;
		uint32_t firstFrame = 0;
		uint32_t lastFrame = 0;
		uint32_t repeatCount = 0;
		uint32_t lastSummaryRepeatCount = 0;
		uint32_t lastSummaryFrame = 0;
	};

	struct VRPresentationKnownTargetHistory
	{
		bool initialized = false;
		uint64_t signature = 0;
		uint32_t firstFrame = 0;
		uint32_t lastFrame = 0;
		uint32_t repeatCount = 0;
		uint32_t lastSummaryRepeatCount = 0;
		uint32_t lastSummaryFrame = 0;
	};

	struct VRPresentationKnownTargetEntry
	{
		RE::RENDER_TARGETS::RENDER_TARGET target;
		const char* reason;
	};

	constexpr uint32_t kVRPresentationRepeatSummaryCount = 900u;
	constexpr uint32_t kVRPresentationRepeatSummaryFrames = 1800u;
	constexpr uint32_t kVRPresentationSignatureReentryFrames = 120u;
	constexpr size_t kVRPresentationDiagnosticHistoryCapacity = 96u;
	constexpr uint32_t kVRMenuPlaneDiagnosticMaxCBBytes = 4096u;
	constexpr std::array<VRPresentationKnownTargetEntry, 10> kVRPresentationKnownTargetEntries{ {
		{ RE::RENDER_TARGETS::kMAIN, "scene" },
		{ RE::RENDER_TARGETS::kMAIN_COPY, "scene-copy" },
		{ RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY, "imagespace-copy" },
		{ RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY2, "imagespace-copy2" },
		{ RE::RENDER_TARGETS::kMENUBG, "menu-bg" },
		{ RE::RENDER_TARGETS::kPROJECTEDMENU, "projected-menu" },
		{ RE::RENDER_TARGETS::kHUDMENU, "hud-menu" },
		{ RE::RENDER_TARGETS::kFADERUI, "fader-ui" },
		{ RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_1, "taa-ui-history-1" },
		{ RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_2, "taa-ui-history-2" },
	} };
	std::array<VRPresentationDiagnosticHistory, kVRPresentationDiagnosticHistoryCapacity> g_vrPresentationDiagnostics{};
	VRPresentationKnownTargetHistory g_vrPresentationKnownTargets{};

	uint32_t ClampDiagnosticDimension(float a_dimension)
	{
		if (!std::isfinite(a_dimension) || a_dimension <= 0.0f)
			return 0u;
		return static_cast<uint32_t>(std::floor(a_dimension));
	}

	uint32_t QuantizeDiagnosticFloat(float a_value)
	{
		if (!std::isfinite(a_value))
			return std::numeric_limits<uint32_t>::max();
		return static_cast<uint32_t>(static_cast<int32_t>(std::round(a_value * 10000.0f)));
	}

	uint64_t HashDiagnosticText(std::string_view a_text)
	{
		uint64_t hash = 1469598103934665603ull;
		for (char ch : a_text) {
			hash ^= static_cast<uint8_t>(ch);
			hash *= 1099511628211ull;
		}
		return hash;
	}

	uint64_t HashDiagnosticBytes(const void* a_data, size_t a_size)
	{
		if (!a_data || a_size == 0)
			return 0;

		uint64_t hash = 1469598103934665603ull;
		const auto* bytes = static_cast<const uint8_t*>(a_data);
		for (size_t i = 0; i < a_size; ++i) {
			hash ^= bytes[i];
			hash *= 1099511628211ull;
		}
		return hash;
	}

	uint64_t BuildVRPresentationDiagnosticHistoryKey(
		VRPresentationDiagnosticSlot a_slot,
		const char* a_passName,
		const char* a_phase,
		uint64_t a_signature)
	{
		uint64_t key = static_cast<uint64_t>(a_slot);
		key = MixVRTransitionDiagnosticValue(key, HashDiagnosticText(DiagnosticText(a_passName, "unknown")));
		key = MixVRTransitionDiagnosticValue(key, HashDiagnosticText(DiagnosticText(a_phase, "unknown")));
		key = MixVRTransitionDiagnosticValue(key, a_signature);
		return key;
	}

	std::string GetD3DDebugObjectName(ID3D11DeviceChild* a_object)
	{
		if (!a_object)
			return {};

		static const GUID kD3DDebugObjectNameGuid{ 0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 } };
		char buffer[256] = {};
		UINT size = static_cast<UINT>(sizeof(buffer) - 1);
		if (FAILED(a_object->GetPrivateData(kD3DDebugObjectNameGuid, &size, buffer)) || size == 0)
			return {};

		const char* end = std::find(buffer, buffer + size, '\0');
		return std::string(buffer, static_cast<size_t>(end - buffer));
	}

	D3DShaderDiagnosticInfo BuildShaderDiagnosticInfo(ID3D11DeviceChild* a_shader)
	{
		D3DShaderDiagnosticInfo info{};
		if (!a_shader)
			return info;

		info.shader = reinterpret_cast<uintptr_t>(a_shader);
		info.debugName = GetD3DDebugObjectName(a_shader);
		return info;
	}

	bool TryHashD3DBufferContents(ID3D11Buffer* a_buffer, const D3D11_BUFFER_DESC& a_desc, uint64_t& a_hash)
	{
		auto* device = globals::d3d::device;
		auto* context = globals::d3d::context;
		if (!device || !context || !a_buffer || a_desc.ByteWidth == 0 || a_desc.ByteWidth > kVRMenuPlaneDiagnosticMaxCBBytes)
			return false;

		D3D11_BUFFER_DESC stagingDesc = a_desc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;
		stagingDesc.StructureByteStride = 0;

		winrt::com_ptr<ID3D11Buffer> stagingBuffer;
		if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, stagingBuffer.put())))
			return false;

		context->CopyResource(stagingBuffer.get(), a_buffer);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(stagingBuffer.get(), 0, D3D11_MAP_READ, 0, &mapped)))
			return false;

		a_hash = HashDiagnosticBytes(mapped.pData, a_desc.ByteWidth);
		context->Unmap(stagingBuffer.get(), 0);
		return true;
	}

	D3DBufferDiagnosticInfo BuildConstantBufferDiagnosticInfo(ID3D11Buffer* a_buffer)
	{
		D3DBufferDiagnosticInfo info{};
		if (!a_buffer)
			return info;

		D3D11_BUFFER_DESC desc{};
		a_buffer->GetDesc(&desc);

		info.valid = true;
		info.buffer = reinterpret_cast<uintptr_t>(a_buffer);
		info.byteWidth = desc.ByteWidth;
		info.usage = static_cast<uint32_t>(desc.Usage);
		info.bindFlags = desc.BindFlags;
		info.cpuAccessFlags = desc.CPUAccessFlags;
		info.miscFlags = desc.MiscFlags;
		info.debugName = GetD3DDebugObjectName(a_buffer);
		info.contentHashValid = TryHashD3DBufferContents(a_buffer, desc, info.contentHash);
		return info;
	}

	std::string GetRenderTargetTextureName(ID3D11Texture2D* a_texture)
	{
		if (!a_texture)
			return "NONE";

		auto renderer = globals::game::renderer;
		if (!renderer)
			return "UNKNOWN";

		auto& renderTargets = renderer->GetRuntimeData().renderTargets;
		const int targetCount = Util::GetRenderTargetCount();
		for (int i = 0; i < targetCount; ++i) {
			const auto target = static_cast<RE::RENDER_TARGETS::RENDER_TARGET>(i);
			const auto& renderTarget = renderTargets[i];
			if (renderTarget.texture == a_texture)
				return std::string(magic_enum::enum_name(target));
			if (renderTarget.textureCopy == a_texture)
				return std::string(magic_enum::enum_name(target)) + ".copy";
		}

		return "UNKNOWN";
	}

	void PopulateTextureDiagnosticInfo(D3DViewDiagnosticInfo& a_info, ID3D11Texture2D* a_texture)
	{
		if (!a_texture)
			return;

		D3D11_TEXTURE2D_DESC desc{};
		a_texture->GetDesc(&desc);
		a_info.valid = true;
		a_info.resource = reinterpret_cast<uintptr_t>(a_texture);
		a_info.width = desc.Width;
		a_info.height = desc.Height;
		a_info.arraySize = desc.ArraySize;
		a_info.mipLevels = desc.MipLevels;
		a_info.format = static_cast<uint32_t>(desc.Format);
		a_info.samples = desc.SampleDesc.Count;
		a_info.bindFlags = desc.BindFlags;
		a_info.presentationTarget = IsVRPresentationRenderTargetTexture(a_texture);
		if (a_info.engineName == "NONE" || a_info.engineName == "UNKNOWN" || a_info.engineName == "CS_SRV")
			a_info.engineName = GetRenderTargetTextureName(a_texture);
		if (a_info.debugName.empty())
			a_info.debugName = GetD3DDebugObjectName(a_texture);
	}

	D3DViewDiagnosticInfo BuildSRVDiagnosticInfo(ID3D11ShaderResourceView* a_srv, bool a_pixelShaderStage)
	{
		D3DViewDiagnosticInfo info{};
		if (!a_srv)
			return info;

		info.view = reinterpret_cast<uintptr_t>(a_srv);
		info.engineName = Util::GetNameFromSRV(a_srv);
		if (!a_pixelShaderStage && info.engineName == "NONE")
			info.engineName = "CS_SRV";
		info.debugName = GetD3DDebugObjectName(a_srv);

		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		a_srv->GetDesc(&viewDesc);
		info.viewFormat = static_cast<uint32_t>(viewDesc.Format);
		info.viewDimension = static_cast<uint32_t>(viewDesc.ViewDimension);

		ID3D11Resource* resource = nullptr;
		a_srv->GetResource(&resource);
		if (!resource)
			return info;

		ID3D11Texture2D* texture = nullptr;
		if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
			PopulateTextureDiagnosticInfo(info, texture);
			texture->Release();
		}
		resource->Release();
		return info;
	}

	D3DViewDiagnosticInfo BuildRTVDiagnosticInfo(ID3D11RenderTargetView* a_rtv)
	{
		D3DViewDiagnosticInfo info{};
		if (!a_rtv)
			return info;

		info.view = reinterpret_cast<uintptr_t>(a_rtv);
		info.engineName = Util::GetNameFromRTV(a_rtv);
		info.debugName = GetD3DDebugObjectName(a_rtv);

		D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
		a_rtv->GetDesc(&viewDesc);
		info.viewFormat = static_cast<uint32_t>(viewDesc.Format);
		info.viewDimension = static_cast<uint32_t>(viewDesc.ViewDimension);

		ID3D11Resource* resource = nullptr;
		a_rtv->GetResource(&resource);
		if (!resource)
			return info;

		ID3D11Texture2D* texture = nullptr;
		if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
			PopulateTextureDiagnosticInfo(info, texture);
			texture->Release();
		}
		resource->Release();
		return info;
	}

	D3DViewDiagnosticInfo BuildDSVDiagnosticInfo(ID3D11DepthStencilView* a_dsv)
	{
		D3DViewDiagnosticInfo info{};
		if (!a_dsv)
			return info;

		info.view = reinterpret_cast<uintptr_t>(a_dsv);
		info.engineName = "DSV";
		info.debugName = GetD3DDebugObjectName(a_dsv);

		D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
		a_dsv->GetDesc(&viewDesc);
		info.viewFormat = static_cast<uint32_t>(viewDesc.Format);
		info.viewDimension = static_cast<uint32_t>(viewDesc.ViewDimension);

		ID3D11Resource* resource = nullptr;
		a_dsv->GetResource(&resource);
		if (!resource)
			return info;

		ID3D11Texture2D* texture = nullptr;
		if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
			PopulateTextureDiagnosticInfo(info, texture);
			texture->Release();
		}
		resource->Release();
		return info;
	}

	std::string FormatD3DViewDiagnosticInfo(const D3DViewDiagnosticInfo& a_info)
	{
		if (!a_info.valid) {
			if (a_info.view != 0)
				return std::format("view=0x{:X} unresolved", a_info.view);
			return "null";
		}

		return std::format(
			"name={} debug={} view=0x{:X} tex=0x{:X} {}x{} fmt={} viewFmt={} dim={} array={} mips={} samples={} bind=0x{:X} presentation={}",
			a_info.engineName.empty() ? "UNKNOWN" : a_info.engineName,
			a_info.debugName.empty() ? "-" : a_info.debugName,
			a_info.view,
			a_info.resource,
			a_info.width,
			a_info.height,
			a_info.format,
			a_info.viewFormat,
			a_info.viewDimension,
			a_info.arraySize,
			a_info.mipLevels,
			a_info.samples,
			a_info.bindFlags,
			BoolText(a_info.presentationTarget));
	}

	std::string FormatD3DShaderDiagnosticInfo(const D3DShaderDiagnosticInfo& a_info)
	{
		if (a_info.shader == 0)
			return "null";

		return std::format(
			"shader=0x{:X} debug={}",
			a_info.shader,
			a_info.debugName.empty() ? "-" : a_info.debugName);
	}

	std::string FormatD3DBufferDiagnosticInfo(const D3DBufferDiagnosticInfo& a_info)
	{
		if (!a_info.valid)
			return "null";

		return std::format(
			"buffer=0x{:X} debug={} bytes={} usage={} bind=0x{:X} cpu=0x{:X} misc=0x{:X} hash={}",
			a_info.buffer,
			a_info.debugName.empty() ? "-" : a_info.debugName,
			a_info.byteWidth,
			a_info.usage,
			a_info.bindFlags,
			a_info.cpuAccessFlags,
			a_info.miscFlags,
			a_info.contentHashValid ? std::format("0x{:X}", a_info.contentHash) : "unread");
	}

	uint64_t MixShaderDiagnosticSignature(uint64_t a_signature, const D3DShaderDiagnosticInfo& a_info)
	{
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.shader != 0 ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.shader);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, HashDiagnosticText(a_info.debugName));
		return a_signature;
	}

	uint64_t MixBufferDiagnosticSignature(uint64_t a_signature, const D3DBufferDiagnosticInfo& a_info)
	{
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.valid ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.buffer);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.byteWidth);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.usage);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.bindFlags);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.cpuAccessFlags);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.miscFlags);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.contentHashValid ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.contentHash);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, HashDiagnosticText(a_info.debugName));
		return a_signature;
	}

	uint64_t MixViewDiagnosticSignature(uint64_t a_signature, const D3DViewDiagnosticInfo& a_info)
	{
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.valid ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.view != 0 ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.resource != 0 ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.width);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.height);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.format);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.viewFormat);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.viewDimension);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.arraySize);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.samples);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.bindFlags);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.presentationTarget ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, HashDiagnosticText(a_info.engineName));
		return a_signature;
	}

	uint64_t MixKnownTargetDiagnosticSignature(uint64_t a_signature, const D3DViewDiagnosticInfo& a_info)
	{
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.valid ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.width);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.height);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.format);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.arraySize);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.mipLevels);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.samples);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.bindFlags);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.presentationTarget ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, HashDiagnosticText(a_info.engineName));
		return a_signature;
	}

	bool IsFadePresentationDiagnosticSlot(VRPresentationDiagnosticSlot a_slot)
	{
		return a_slot == VRPresentationDiagnosticSlot::FadeRender ||
		       a_slot == VRPresentationDiagnosticSlot::FadeDispatch;
	}

	bool IsMenuUIPresentationDiagnosticSlot(VRPresentationDiagnosticSlot a_slot)
	{
		switch (a_slot) {
		case VRPresentationDiagnosticSlot::MenuDraw:
		case VRPresentationDiagnosticSlot::TemporalAAUIRender:
		case VRPresentationDiagnosticSlot::TemporalAAUIDispatch:
		case VRPresentationDiagnosticSlot::LightingCompositeMenuRender:
		case VRPresentationDiagnosticSlot::LightingCompositeMenuDispatch:
			return true;
		default:
			return false;
		}
	}

	const char* GetVRPresentationDiagnosticRole(VRPresentationDiagnosticSlot a_slot)
	{
		if (IsFadePresentationDiagnosticSlot(a_slot))
			return "fade";
		if (IsMenuUIPresentationDiagnosticSlot(a_slot))
			return "menu-ui";
		if (a_slot == VRPresentationDiagnosticSlot::SubmitStage)
			return "submit-stage";
		if (a_slot == VRPresentationDiagnosticSlot::DynamicUpsampleRender ||
			a_slot == VRPresentationDiagnosticSlot::DynamicUpsampleDispatch)
			return "dynamic-upsample";
		if (a_slot == VRPresentationDiagnosticSlot::MainPostProcessing)
			return "main-post";
		return "other";
	}

	bool DiagnosticDimensionBelow(uint32_t a_actual, uint32_t a_expected)
	{
		return a_expected > 2u && a_actual + 2u < a_expected;
	}

	bool DiagnosticViewBelow(const D3DViewDiagnosticInfo& a_info, uint32_t a_expectedWidth, uint32_t a_expectedHeight)
	{
		if (!a_info.valid)
			return false;
		return DiagnosticDimensionBelow(a_info.width, a_expectedWidth) ||
		       DiagnosticDimensionBelow(a_info.height, a_expectedHeight);
	}

	bool HasMeaningfulDiagnosticView(const D3DViewDiagnosticInfo& a_info)
	{
		return a_info.valid || a_info.view != 0 || a_info.resource != 0;
	}

	bool AnyDiagnosticViewBelow(
		const std::array<D3DViewDiagnosticInfo, 4>& a_infos,
		uint32_t a_expectedWidth,
		uint32_t a_expectedHeight)
	{
		for (const auto& info : a_infos) {
			if (DiagnosticViewBelow(info, a_expectedWidth, a_expectedHeight))
				return true;
		}
		return false;
	}

	bool DiagnosticViewportBelow(const D3D11_VIEWPORT& a_viewport, uint32_t a_expectedWidth, uint32_t a_expectedHeight)
	{
		return DiagnosticDimensionBelow(ClampDiagnosticDimension(a_viewport.Width), a_expectedWidth) ||
		       DiagnosticDimensionBelow(ClampDiagnosticDimension(a_viewport.Height), a_expectedHeight);
	}

	std::string BuildVRPresentationDiagnosticVerdict(
		VRPresentationDiagnosticSlot a_slot,
		const VRPresentationDiagnosticSnapshot& a_snapshot)
	{
		const bool fadePass = IsFadePresentationDiagnosticSlot(a_slot);
		const bool menuUIPass = IsMenuUIPresentationDiagnosticSlot(a_slot);
		const uint32_t expectedWidth = a_snapshot.finalWidth != 0 ? a_snapshot.finalWidth : a_snapshot.screenWidth;
		const uint32_t expectedHeight = a_snapshot.finalHeight != 0 ? a_snapshot.finalHeight : a_snapshot.screenHeight;
		const bool hasExpectedSize = expectedWidth != 0 && expectedHeight != 0;
		const bool rtvBelowOutput = hasExpectedSize && DiagnosticViewBelow(a_snapshot.rtvs[0], expectedWidth, expectedHeight);
		const bool viewportBelowOutput = hasExpectedSize && DiagnosticViewportBelow(a_snapshot.viewports[0], expectedWidth, expectedHeight);
		const bool pixelSourceBelowOutput = hasExpectedSize && AnyDiagnosticViewBelow(a_snapshot.psSRVs, expectedWidth, expectedHeight);
		const bool computeSourceBelowOutput = hasExpectedSize && AnyDiagnosticViewBelow(a_snapshot.csSRVs, expectedWidth, expectedHeight);
		const bool rtvPresentation = a_snapshot.rtvs[0].presentationTarget || a_snapshot.currentRTPresentation;

		if (fadePass) {
			if (rtvBelowOutput)
				return "risk:fade-target-smaller-than-final";
			if (viewportBelowOutput)
				return "risk:fade-viewport-smaller-than-final";
			if (pixelSourceBelowOutput || computeSourceBelowOutput)
				return "check:fade-source-smaller-than-final";
			if (rtvPresentation)
				return "ok:fade-full-presentation-target";
			if (a_snapshot.knownMenu && IsVendorUpscalingMethod(a_snapshot.runtimeMethod) && !a_snapshot.renderScaleActive)
				return "check:fade-vendor-menu-no-renderscale";
			return "check:fade-nonpresentation-target";
		}

		if (menuUIPass) {
			if (rtvBelowOutput && a_snapshot.renderScaleActive)
				return "risk:menu-ui-rendering-at-renderscale";
			if (viewportBelowOutput && a_snapshot.renderScaleActive)
				return "risk:menu-ui-viewport-at-renderscale";
			if ((pixelSourceBelowOutput || computeSourceBelowOutput) && a_snapshot.renderScaleActive)
				return "risk:menu-ui-source-at-renderscale";
			if (rtvPresentation)
				return "ok:menu-ui-full-presentation-target";
			if (a_snapshot.renderScaleActive)
				return "check:menu-ui-nonpresentation-target";
			return "ok:menu-ui-native-path";
		}

		if (a_slot == VRPresentationDiagnosticSlot::SubmitStage) {
			if (a_snapshot.submitStageDeviceLost)
				return "risk:submit-stage-device-lost";
			if (a_snapshot.submitMenuPresentation)
				return "ok:submit-menu-presentation";
			if (a_snapshot.submitStageActive)
				return "ok:submit-stage-active";
			return "check:submit-stage-inactive";
		}

		return "check:state-snapshot";
	}

	template <size_t N>
	void LogVRPresentationDiagnosticViews(
		const char* a_passName,
		const char* a_phase,
		uint32_t a_frame,
		const char* a_label,
		const std::array<D3DViewDiagnosticInfo, N>& a_infos)
	{
		bool loggedAny = false;
		for (size_t i = 0; i < a_infos.size(); ++i) {
			if (!HasMeaningfulDiagnosticView(a_infos[i]))
				continue;

			logger::debug(
				"[VRMenuDiag] {} {} frame={} {}{} {}",
				DiagnosticText(a_passName, "unknown"),
				DiagnosticText(a_phase, "unknown"),
				a_frame,
				DiagnosticText(a_label, "view"),
				i,
				FormatD3DViewDiagnosticInfo(a_infos[i]));
			loggedAny = true;
		}

		if (!loggedAny) {
			logger::debug(
				"[VRMenuDiag] {} {} frame={} {} none",
				DiagnosticText(a_passName, "unknown"),
				DiagnosticText(a_phase, "unknown"),
				a_frame,
				DiagnosticText(a_label, "view"));
		}
	}

	template <class Callback>
	void LogVRPresentationAroundCall(
		Upscaling& a_upscaling,
		VRPresentationDiagnosticSlot a_slot,
		const char* a_passName,
		const char* a_beforePhase,
		const char* a_afterPhase,
		bool a_includeKnownTargetsBefore,
		Callback&& a_callback)
	{
		LogVRPresentationPassDiagnostics(
			a_upscaling,
			a_slot,
			a_passName,
			a_beforePhase,
			a_includeKnownTargetsBefore);
		std::forward<Callback>(a_callback)();
		LogVRPresentationPassDiagnostics(
			a_upscaling,
			a_slot,
			a_passName,
			a_afterPhase,
			false);
	}

	uint64_t BuildVRPresentationDiagnosticSignature(
		const VRPresentationDiagnosticSnapshot& a_snapshot,
		const char* a_passName,
		const char* a_phase)
	{
		uint64_t signature = HashDiagnosticText(DiagnosticText(a_passName, "unknown"));
		signature = MixVRTransitionDiagnosticValue(signature, HashDiagnosticText(DiagnosticText(a_phase, "unknown")));
		signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint64_t>(a_snapshot.requestedMethod));
		signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint64_t>(a_snapshot.runtimeMethod));
		signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint64_t>(a_snapshot.owner));
		signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint64_t>(a_snapshot.outputTarget));
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.qualityMode);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.screenWidth);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.screenHeight);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.engineWidth);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.engineHeight);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.finalWidth);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.finalHeight);
		signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(a_snapshot.resolutionScaleX));
		signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(a_snapshot.resolutionScaleY));
		signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(a_snapshot.dynamicWidthRatio));
		signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(a_snapshot.dynamicHeightRatio));
		signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(a_snapshot.previousDynamicWidthRatio));
		signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(a_snapshot.previousDynamicHeightRatio));
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.dynamicResolutionLock ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.knownMenu ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.gameMenu ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.communityShadersMenu ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.mainOrLoadingMenu ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.loadingMenu ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.saveLoad ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.vrMenuPresentation ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.submitMenuPresentation ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.renderScaleActive ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.renderScaleRequested ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.perfModeActive ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.submitStageActive ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.submitStageDeviceLost ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.presentationUpscalingActive ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.fullResolutionMenuUIDraw ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.menuTextRasterDiagnosticContext ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.menuPlaneDiagnosticContext ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.currentRTPresentation ? 1u : 0u);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.viewportCount);
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.scissorCount);
		for (const auto& viewport : a_snapshot.viewports) {
			signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(viewport.TopLeftX));
			signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(viewport.TopLeftY));
			signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(viewport.Width));
			signature = MixVRTransitionDiagnosticValue(signature, QuantizeDiagnosticFloat(viewport.Height));
		}
		for (const auto& scissor : a_snapshot.scissors) {
			signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint32_t>(scissor.left));
			signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint32_t>(scissor.top));
			signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint32_t>(scissor.right));
			signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint32_t>(scissor.bottom));
		}
		for (const auto& info : a_snapshot.psSRVs)
			signature = MixViewDiagnosticSignature(signature, info);
		for (const auto& info : a_snapshot.csSRVs)
			signature = MixViewDiagnosticSignature(signature, info);
		for (const auto& info : a_snapshot.rtvs)
			signature = MixViewDiagnosticSignature(signature, info);
		signature = MixViewDiagnosticSignature(signature, a_snapshot.dsv);
		signature = MixShaderDiagnosticSignature(signature, a_snapshot.vertexShader);
		signature = MixShaderDiagnosticSignature(signature, a_snapshot.geometryShader);
		signature = MixShaderDiagnosticSignature(signature, a_snapshot.pixelShader);
		for (const auto& info : a_snapshot.vertexCBs)
			signature = MixBufferDiagnosticSignature(signature, info);
		for (const auto& info : a_snapshot.pixelCBs)
			signature = MixBufferDiagnosticSignature(signature, info);
		return signature;
	}

	bool ShouldCaptureVRMenuPlaneDiagnostics(
		const char* a_passName,
		const VRPresentationDiagnosticSnapshot& a_snapshot)
	{
		return std::string_view(DiagnosticText(a_passName, "")) == "MenuManagerDrawInterface" &&
		       a_snapshot.renderScaleActive &&
		       IsVendorUpscalingMethod(a_snapshot.runtimeMethod) &&
		       a_snapshot.qualityMode > 0 &&
		       (a_snapshot.knownMenu || a_snapshot.vrMenuPresentation || a_snapshot.communityShadersMenu) &&
		       !a_snapshot.mainOrLoadingMenu;
	}

	void CaptureVRMenuPlaneDiagnostics(
		ID3D11DeviceContext* a_context,
		const char* a_passName,
		VRPresentationDiagnosticSnapshot& a_snapshot)
	{
		if (!a_context || !ShouldCaptureVRMenuPlaneDiagnostics(a_passName, a_snapshot))
			return;

		a_snapshot.menuPlaneDiagnosticContext = true;

		ID3D11VertexShader* vertexShader = nullptr;
		ID3D11GeometryShader* geometryShader = nullptr;
		ID3D11PixelShader* pixelShader = nullptr;
		a_context->VSGetShader(&vertexShader, nullptr, nullptr);
		a_context->GSGetShader(&geometryShader, nullptr, nullptr);
		a_context->PSGetShader(&pixelShader, nullptr, nullptr);
		a_snapshot.vertexShader = BuildShaderDiagnosticInfo(vertexShader);
		a_snapshot.geometryShader = BuildShaderDiagnosticInfo(geometryShader);
		a_snapshot.pixelShader = BuildShaderDiagnosticInfo(pixelShader);
		if (vertexShader)
			vertexShader->Release();
		if (geometryShader)
			geometryShader->Release();
		if (pixelShader)
			pixelShader->Release();

		ID3D11Buffer* vertexCBs[4] = {};
		ID3D11Buffer* pixelCBs[4] = {};
		a_context->VSGetConstantBuffers(0, static_cast<UINT>(std::size(vertexCBs)), vertexCBs);
		a_context->PSGetConstantBuffers(0, static_cast<UINT>(std::size(pixelCBs)), pixelCBs);
		for (size_t i = 0; i < std::size(vertexCBs); ++i) {
			a_snapshot.vertexCBs[i] = BuildConstantBufferDiagnosticInfo(vertexCBs[i]);
			if (vertexCBs[i])
				vertexCBs[i]->Release();
		}
		for (size_t i = 0; i < std::size(pixelCBs); ++i) {
			a_snapshot.pixelCBs[i] = BuildConstantBufferDiagnosticInfo(pixelCBs[i]);
			if (pixelCBs[i])
				pixelCBs[i]->Release();
		}
	}

	bool BuildVRPresentationDiagnosticSnapshot(
		const Upscaling& a_upscaling,
		const char* a_passName,
		const char* a_phase,
		VRPresentationDiagnosticSnapshot& a_snapshot)
	{
		if (!globals::game::isVR)
			return false;

		auto state = globals::state;
		auto context = globals::d3d::context;
		if (!state || !context)
			return false;

		a_snapshot.frame = state->frameCount;
		a_snapshot.requestedMethod = a_upscaling.GetConfiguredUpscaleMethodForTransition();
		a_snapshot.runtimeMethod = a_upscaling.GetRuntimeUpscaleMethod();
		a_snapshot.qualityMode = a_upscaling.GetRuntimeQualityMode();
		a_snapshot.screenWidth = ClampDiagnosticDimension(state->screenSize.x);
		a_snapshot.screenHeight = ClampDiagnosticDimension(state->screenSize.y);
		a_snapshot.resolutionScaleX = a_upscaling.resolutionScale.x;
		a_snapshot.resolutionScaleY = a_upscaling.resolutionScale.y;
		a_snapshot.knownMenu = IsKnownGameMenuContextActive();
		a_snapshot.gameMenu = IsGameMenuContextActive();
		a_snapshot.communityShadersMenu = IsCommunityShadersMenuOpen();
		a_snapshot.mainOrLoadingMenu = IsMainOrLoadingMenuContextActive();
		a_snapshot.loadingMenu = IsLoadingMenuContextActive();
		a_snapshot.saveLoad = IsSaveLoadTransitionContextActive(state);
		a_snapshot.vrMenuPresentation = IsVRMenuPresentationContextActive();
		a_snapshot.submitMenuPresentation = IsSubmitStageMenuPresentationContextActive();
		a_snapshot.renderScaleActive = a_upscaling.IsVRRenderScaleModeActive();
		a_snapshot.renderScaleRequested = a_upscaling.IsRenderScaleModeRequested();
		a_snapshot.perfModeActive = a_upscaling.IsPerfModeActive();
		a_snapshot.submitStageActive = a_upscaling.IsSubmitStageUpscalingActive();
		a_snapshot.submitStageDeviceLost = a_upscaling.IsSubmitStageDeviceLost();
		a_snapshot.presentationUpscalingActive = a_upscaling.IsPresentationUpscalingActive();
		a_snapshot.fullResolutionMenuUIDraw = false;
		a_snapshot.menuTextRasterDiagnosticContext =
			a_snapshot.renderScaleActive &&
			IsVendorUpscalingMethod(a_snapshot.runtimeMethod) &&
			a_snapshot.qualityMode > 0 &&
			(a_snapshot.knownMenu || a_snapshot.vrMenuPresentation || a_snapshot.communityShadersMenu) &&
			!a_snapshot.mainOrLoadingMenu;

		const auto& plan = a_upscaling.GetRuntimeResolutionPlan();
		a_snapshot.owner = plan.owner;
		a_snapshot.outputTarget = plan.outputTarget;
		a_snapshot.engineWidth = ClampDiagnosticDimension(plan.engineRenderSize.x);
		a_snapshot.engineHeight = ClampDiagnosticDimension(plan.engineRenderSize.y);
		a_snapshot.finalWidth = ClampDiagnosticDimension(plan.finalOutputSize.x);
		a_snapshot.finalHeight = ClampDiagnosticDimension(plan.finalOutputSize.y);

		const bool relevant =
			IsVendorUpscalingMethod(a_snapshot.requestedMethod) ||
			IsVendorUpscalingMethod(a_snapshot.runtimeMethod) ||
			a_snapshot.renderScaleActive ||
			a_snapshot.renderScaleRequested ||
			a_snapshot.perfModeActive ||
			a_snapshot.submitStageActive ||
			a_snapshot.knownMenu ||
			a_snapshot.communityShadersMenu ||
			a_snapshot.loadingMenu ||
			a_snapshot.saveLoad ||
			a_snapshot.vrMenuPresentation ||
			a_snapshot.submitMenuPresentation;
		if (!relevant)
			return false;

		if (auto viewport = globals::game::graphicsState) {
			const auto& runtimeData = viewport->GetRuntimeData();
			a_snapshot.dynamicWidthRatio = runtimeData.dynamicResolutionWidthRatio;
			a_snapshot.dynamicHeightRatio = runtimeData.dynamicResolutionHeightRatio;
			a_snapshot.previousDynamicWidthRatio = runtimeData.dynamicResolutionPreviousWidthRatio;
			a_snapshot.previousDynamicHeightRatio = runtimeData.dynamicResolutionPreviousHeightRatio;
			a_snapshot.dynamicResolutionLock = runtimeData.dynamicResolutionLock != 0;
		}

		ID3D11ShaderResourceView* psSRVs[4] = {};
		ID3D11ShaderResourceView* csSRVs[4] = {};
		context->PSGetShaderResources(0, static_cast<UINT>(std::size(psSRVs)), psSRVs);
		context->CSGetShaderResources(0, static_cast<UINT>(std::size(csSRVs)), csSRVs);
		for (size_t i = 0; i < std::size(psSRVs); ++i) {
			a_snapshot.psSRVs[i] = BuildSRVDiagnosticInfo(psSRVs[i], true);
			if (psSRVs[i])
				psSRVs[i]->Release();
		}
		for (size_t i = 0; i < std::size(csSRVs); ++i) {
			a_snapshot.csSRVs[i] = BuildSRVDiagnosticInfo(csSRVs[i], false);
			if (csSRVs[i])
				csSRVs[i]->Release();
		}

		ID3D11RenderTargetView* rtvs[4] = {};
		ID3D11DepthStencilView* dsv = nullptr;
		context->OMGetRenderTargets(static_cast<UINT>(std::size(rtvs)), rtvs, &dsv);
		for (size_t i = 0; i < std::size(rtvs); ++i) {
			a_snapshot.rtvs[i] = BuildRTVDiagnosticInfo(rtvs[i]);
			if (rtvs[i])
				rtvs[i]->Release();
		}
		a_snapshot.currentRTPresentation = a_snapshot.rtvs[0].presentationTarget;
		a_snapshot.dsv = BuildDSVDiagnosticInfo(dsv);
		if (dsv)
			dsv->Release();

		UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
		context->RSGetViewports(&viewportCount, viewports.data());
		a_snapshot.viewportCount = viewportCount;
		for (size_t i = 0; i < a_snapshot.viewports.size() && i < viewportCount; ++i)
			a_snapshot.viewports[i] = viewports[i];

		UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
		context->RSGetScissorRects(&scissorCount, scissors.data());
		a_snapshot.scissorCount = scissorCount;
		for (size_t i = 0; i < a_snapshot.scissors.size() && i < scissorCount; ++i)
			a_snapshot.scissors[i] = scissors[i];

		CaptureVRMenuPlaneDiagnostics(context, a_passName, a_snapshot);
		LogVRMenuOriginalCompositeCandidateDiagnostics(
			context,
			a_passName,
			a_phase,
			a_snapshot.knownMenu,
			a_snapshot.vrMenuPresentation,
			a_snapshot.communityShadersMenu,
			a_snapshot.renderScaleActive,
			a_snapshot.presentationUpscalingActive);

		a_snapshot.signature = BuildVRPresentationDiagnosticSignature(a_snapshot, a_passName, a_phase);
		return true;
	}

	void LogVRPresentationRepeatSummary(
		VRPresentationDiagnosticHistory& a_history,
		bool a_final)
	{
		if (!a_history.initialized || a_history.repeatCount <= a_history.lastSummaryRepeatCount)
			return;

		const uint32_t repeated = a_history.repeatCount - a_history.lastSummaryRepeatCount;
		if (repeated == 0)
			return;

		logger::debug(
			"[VRMenuDiag] {} {} {}: repeated {} additional times over {} frames with unchanged D3D/menu state (signature=0x{:X}, lastFrame={})",
			a_history.passName.empty() ? "unknown" : a_history.passName,
			a_history.phase.empty() ? "unknown" : a_history.phase,
			a_final ? "summary" : "still repeating",
			repeated,
			ElapsedFrames(a_history.firstFrame, a_history.lastFrame),
			a_history.signature,
			a_history.lastFrame);
		a_history.lastSummaryRepeatCount = a_history.repeatCount;
		a_history.lastSummaryFrame = a_history.lastFrame;
	}

	VRPresentationDiagnosticHistory& GetVRPresentationDiagnosticHistory(
		VRPresentationDiagnosticSlot a_slot,
		const char* a_passName,
		const char* a_phase,
		uint64_t a_signature,
		uint32_t a_frame)
	{
		const uint64_t key = BuildVRPresentationDiagnosticHistoryKey(a_slot, a_passName, a_phase, a_signature);
		VRPresentationDiagnosticHistory* emptyHistory = nullptr;
		VRPresentationDiagnosticHistory* oldestHistory = nullptr;
		for (auto& history : g_vrPresentationDiagnostics) {
			if (history.initialized && history.key == key)
				return history;
			if (!history.initialized && !emptyHistory)
				emptyHistory = &history;
			if (history.initialized && (!oldestHistory || history.lastFrame < oldestHistory->lastFrame))
				oldestHistory = &history;
		}

		auto* selectedHistory = emptyHistory ? emptyHistory : oldestHistory;
		if (!selectedHistory)
			selectedHistory = &g_vrPresentationDiagnostics.front();

		if (selectedHistory->initialized)
			LogVRPresentationRepeatSummary(*selectedHistory, true);

		*selectedHistory = {};
		selectedHistory->key = key;
		selectedHistory->firstFrame = std::max(a_frame, 1u);
		selectedHistory->lastFrame = a_frame;
		return *selectedHistory;
	}

	bool ShouldLogVRPresentationSnapshot(
		VRPresentationDiagnosticSlot a_slot,
		const VRPresentationDiagnosticSnapshot& a_snapshot,
		const char* a_passName,
		const char* a_phase)
	{
		auto& history = GetVRPresentationDiagnosticHistory(a_slot, a_passName, a_phase, a_snapshot.signature, a_snapshot.frame);
		const bool reenteredAfterGap =
			history.initialized &&
			history.lastFrame != 0 &&
			ElapsedFrames(history.lastFrame, a_snapshot.frame) >= kVRPresentationSignatureReentryFrames;
		if (!history.initialized || reenteredAfterGap) {
			LogVRPresentationRepeatSummary(history, true);
			history.initialized = true;
			history.signature = a_snapshot.signature;
			history.passName = DiagnosticText(a_passName, "unknown");
			history.phase = DiagnosticText(a_phase, "unknown");
			history.firstFrame = std::max(a_snapshot.frame, 1u);
			history.lastFrame = a_snapshot.frame;
			history.repeatCount = 1;
			history.lastSummaryRepeatCount = 1;
			history.lastSummaryFrame = a_snapshot.frame;
			return true;
		}

		++history.repeatCount;
		history.lastFrame = a_snapshot.frame;
		const bool countThreshold =
			history.repeatCount - history.lastSummaryRepeatCount >= kVRPresentationRepeatSummaryCount;
		const bool frameThreshold =
			ElapsedFrames(history.lastSummaryFrame, a_snapshot.frame) >= kVRPresentationRepeatSummaryFrames;
		if (countThreshold || frameThreshold)
			LogVRPresentationRepeatSummary(history, false);

		return false;
	}

	void LogVRPresentationKnownTargets(const char* a_passName, const char* a_phase, uint32_t a_frame)
	{
		auto renderer = globals::game::renderer;
		if (!renderer)
			return;

		auto& renderTargets = renderer->GetRuntimeData().renderTargets;
		const int targetCount = Util::GetRenderTargetCount();
		std::array<D3DViewDiagnosticInfo, kVRPresentationKnownTargetEntries.size()> textureInfos{};
		std::array<D3DViewDiagnosticInfo, kVRPresentationKnownTargetEntries.size()> copyInfos{};
		uint64_t signature = HashDiagnosticText("known-targets");
		for (size_t i = 0; i < kVRPresentationKnownTargetEntries.size(); ++i) {
			const auto& entry = kVRPresentationKnownTargetEntries[i];
			const auto targetIndex = static_cast<int>(entry.target);
			if (targetIndex < 0 || targetIndex >= targetCount)
				continue;

			const auto& renderTarget = renderTargets[targetIndex];
			PopulateTextureDiagnosticInfo(textureInfos[i], renderTarget.texture);
			textureInfos[i].engineName = std::string(magic_enum::enum_name(entry.target));
			PopulateTextureDiagnosticInfo(copyInfos[i], renderTarget.textureCopy);
			copyInfos[i].engineName = std::string(magic_enum::enum_name(entry.target)) + ".copy";
			signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint32_t>(entry.target));
			signature = MixKnownTargetDiagnosticSignature(signature, textureInfos[i]);
			signature = MixKnownTargetDiagnosticSignature(signature, copyInfos[i]);
			signature = MixVRTransitionDiagnosticValue(signature, renderTarget.SRV != nullptr ? 1u : 0u);
			signature = MixVRTransitionDiagnosticValue(signature, renderTarget.SRVCopy != nullptr ? 1u : 0u);
			signature = MixVRTransitionDiagnosticValue(signature, renderTarget.RTV != nullptr ? 1u : 0u);
		}

		if (g_vrPresentationKnownTargets.initialized &&
			g_vrPresentationKnownTargets.signature == signature) {
			++g_vrPresentationKnownTargets.repeatCount;
			g_vrPresentationKnownTargets.lastFrame = a_frame;
			const bool countThreshold =
				g_vrPresentationKnownTargets.repeatCount - g_vrPresentationKnownTargets.lastSummaryRepeatCount >= kVRPresentationRepeatSummaryCount;
			const bool frameThreshold =
				ElapsedFrames(g_vrPresentationKnownTargets.lastSummaryFrame, a_frame) >= kVRPresentationRepeatSummaryFrames;
			if (countThreshold || frameThreshold) {
				logger::debug(
					"[VRMenuDiag] knownTargets unchanged: repeated {} additional requested dumps over {} frames (signature=0x{:X}, caller={} {}, lastFrame={})",
					g_vrPresentationKnownTargets.repeatCount - g_vrPresentationKnownTargets.lastSummaryRepeatCount,
					ElapsedFrames(g_vrPresentationKnownTargets.firstFrame, g_vrPresentationKnownTargets.lastFrame),
					g_vrPresentationKnownTargets.signature,
					DiagnosticText(a_passName, "unknown"),
					DiagnosticText(a_phase, "unknown"),
					g_vrPresentationKnownTargets.lastFrame);
				g_vrPresentationKnownTargets.lastSummaryRepeatCount = g_vrPresentationKnownTargets.repeatCount;
				g_vrPresentationKnownTargets.lastSummaryFrame = g_vrPresentationKnownTargets.lastFrame;
			}
			return;
		}

		if (g_vrPresentationKnownTargets.initialized &&
			g_vrPresentationKnownTargets.repeatCount > g_vrPresentationKnownTargets.lastSummaryRepeatCount) {
			logger::debug(
				"[VRMenuDiag] knownTargets changed after {} requested dumps over {} frames (previousSignature=0x{:X}, caller={} {}, frame={})",
				g_vrPresentationKnownTargets.repeatCount,
				ElapsedFrames(g_vrPresentationKnownTargets.firstFrame, g_vrPresentationKnownTargets.lastFrame),
				g_vrPresentationKnownTargets.signature,
				DiagnosticText(a_passName, "unknown"),
				DiagnosticText(a_phase, "unknown"),
				a_frame);
		}

		g_vrPresentationKnownTargets.initialized = true;
		g_vrPresentationKnownTargets.signature = signature;
		g_vrPresentationKnownTargets.firstFrame = std::max(a_frame, 1u);
		g_vrPresentationKnownTargets.lastFrame = a_frame;
		g_vrPresentationKnownTargets.repeatCount = 1;
		g_vrPresentationKnownTargets.lastSummaryRepeatCount = 1;
		g_vrPresentationKnownTargets.lastSummaryFrame = a_frame;

		logger::debug(
			"[VRMenuDiag] {} {} frame={} knownTargets snapshot signature=0x{:X}",
			DiagnosticText(a_passName, "unknown"),
			DiagnosticText(a_phase, "unknown"),
			a_frame,
			signature);

		for (size_t i = 0; i < kVRPresentationKnownTargetEntries.size(); ++i) {
			const auto& entry = kVRPresentationKnownTargetEntries[i];
			const auto targetIndex = static_cast<int>(entry.target);
			if (targetIndex < 0 || targetIndex >= targetCount)
				continue;

			const auto& renderTarget = renderTargets[targetIndex];

			logger::debug(
				"[VRMenuDiag] {} {} frame={} knownTarget={} reason={} usesFullSize={} submittedPresentation={} texture={} copy={} srv=0x{:X} srvCopy=0x{:X} rtv=0x{:X}",
				DiagnosticText(a_passName, "unknown"),
				DiagnosticText(a_phase, "unknown"),
				a_frame,
				magic_enum::enum_name(entry.target),
				entry.reason,
				BoolText(UsesFullSizeVRProtectedTarget(entry.target)),
				BoolText(IsSubmittedVRPresentationTarget(entry.target)),
				FormatD3DViewDiagnosticInfo(textureInfos[i]),
				FormatD3DViewDiagnosticInfo(copyInfos[i]),
				reinterpret_cast<uintptr_t>(renderTarget.SRV),
				reinterpret_cast<uintptr_t>(renderTarget.SRVCopy),
				reinterpret_cast<uintptr_t>(renderTarget.RTV));
		}
	}

	float GetDiagnosticExtentDelta(float a_lhs, float a_rhs)
	{
		return a_lhs > a_rhs ? a_lhs - a_rhs : a_rhs - a_lhs;
	}

	bool DiagnosticExtentMatches(float a_width, float a_height, float a_expectedWidth, float a_expectedHeight)
	{
		if (a_expectedWidth <= 0.0f || a_expectedHeight <= 0.0f)
			return false;

		return GetDiagnosticExtentDelta(a_width, a_expectedWidth) <= 1.0f &&
		       GetDiagnosticExtentDelta(a_height, a_expectedHeight) <= 1.0f;
	}

	const char* ClassifyVRMenuRasterExtent(float a_width, float a_height, const VRPresentationDiagnosticSnapshot& a_snapshot)
	{
		if (a_width <= 0.0f || a_height <= 0.0f)
			return "empty";

		if (DiagnosticExtentMatches(a_width, a_height, static_cast<float>(a_snapshot.finalWidth), static_cast<float>(a_snapshot.finalHeight)))
			return "final";
		if (DiagnosticExtentMatches(a_width, a_height, static_cast<float>(a_snapshot.screenWidth), static_cast<float>(a_snapshot.screenHeight)))
			return "screen";
		if (DiagnosticExtentMatches(a_width, a_height, static_cast<float>(a_snapshot.engineWidth), static_cast<float>(a_snapshot.engineHeight)))
			return "engine";

		const float dynamicWidth = static_cast<float>(a_snapshot.finalWidth) * a_snapshot.dynamicWidthRatio;
		const float dynamicHeight = static_cast<float>(a_snapshot.finalHeight) * a_snapshot.dynamicHeightRatio;
		if (DiagnosticExtentMatches(a_width, a_height, dynamicWidth, dynamicHeight))
			return "dynamic-final";

		return "other";
	}

	float GetDiagnosticScissorWidth(const D3D11_RECT& a_scissor)
	{
		return static_cast<float>(a_scissor.right - a_scissor.left);
	}

	float GetDiagnosticScissorHeight(const D3D11_RECT& a_scissor)
	{
		return static_cast<float>(a_scissor.bottom - a_scissor.top);
	}

	void LogVRPresentationPassDiagnostics(
		const Upscaling& a_upscaling,
		VRPresentationDiagnosticSlot a_slot,
		const char* a_passName,
		const char* a_phase,
		bool a_includeKnownTargets = false)
	{
		VRPresentationDiagnosticSnapshot snapshot{};
		if (!BuildVRPresentationDiagnosticSnapshot(a_upscaling, a_passName, a_phase, snapshot))
			return;
		if (!ShouldLogVRPresentationSnapshot(a_slot, snapshot, a_passName, a_phase))
			return;

		const char* role = GetVRPresentationDiagnosticRole(a_slot);
		const std::string verdict = BuildVRPresentationDiagnosticVerdict(a_slot, snapshot);
		logger::debug(
			"[VRMenuDiag] {} {} frame={} role={} verdict={} signature=0x{:X} req={} runtime={} quality={} owner={} target={} screen={}x{} engine={}x{} final={}x{} resScale={:.4f},{:.4f} drRuntime={:.4f},{:.4f} drPrev={:.4f},{:.4f} drLock={} knownMenu={} gameMenu={} csMenu={} mainOrLoading={} loading={} saveLoad={} vrMenuPresentation={} submitMenuPresentation={} renderScaleActive={} renderScaleRequested={} perfModeActive={} submitStageActive={} submitDeviceLost={} presentationUpscaling={} fullResMenuDraw={} textRasterCtx={} menuPlaneCtx={} currentRTPresentation={} viewports={} scissors={}",
			DiagnosticText(a_passName, "unknown"),
			DiagnosticText(a_phase, "unknown"),
			snapshot.frame,
			role,
			verdict,
			snapshot.signature,
			magic_enum::enum_name(snapshot.requestedMethod),
			magic_enum::enum_name(snapshot.runtimeMethod),
			snapshot.qualityMode,
			magic_enum::enum_name(snapshot.owner),
			magic_enum::enum_name(snapshot.outputTarget),
			snapshot.screenWidth,
			snapshot.screenHeight,
			snapshot.engineWidth,
			snapshot.engineHeight,
			snapshot.finalWidth,
			snapshot.finalHeight,
			snapshot.resolutionScaleX,
			snapshot.resolutionScaleY,
			snapshot.dynamicWidthRatio,
			snapshot.dynamicHeightRatio,
			snapshot.previousDynamicWidthRatio,
			snapshot.previousDynamicHeightRatio,
			BoolText(snapshot.dynamicResolutionLock),
			BoolText(snapshot.knownMenu),
			BoolText(snapshot.gameMenu),
			BoolText(snapshot.communityShadersMenu),
			BoolText(snapshot.mainOrLoadingMenu),
			BoolText(snapshot.loadingMenu),
			BoolText(snapshot.saveLoad),
			BoolText(snapshot.vrMenuPresentation),
			BoolText(snapshot.submitMenuPresentation),
			BoolText(snapshot.renderScaleActive),
			BoolText(snapshot.renderScaleRequested),
			BoolText(snapshot.perfModeActive),
			BoolText(snapshot.submitStageActive),
			BoolText(snapshot.submitStageDeviceLost),
			BoolText(snapshot.presentationUpscalingActive),
			BoolText(snapshot.fullResolutionMenuUIDraw),
			BoolText(snapshot.menuTextRasterDiagnosticContext),
			BoolText(snapshot.menuPlaneDiagnosticContext),
			BoolText(snapshot.currentRTPresentation),
			snapshot.viewportCount,
			snapshot.scissorCount);

		const float scissor0Width = GetDiagnosticScissorWidth(snapshot.scissors[0]);
		const float scissor0Height = GetDiagnosticScissorHeight(snapshot.scissors[0]);
		const bool viewport0EngineExtent =
			DiagnosticExtentMatches(
				snapshot.viewports[0].Width,
				snapshot.viewports[0].Height,
				static_cast<float>(snapshot.engineWidth),
				static_cast<float>(snapshot.engineHeight)) &&
			!DiagnosticExtentMatches(
				snapshot.viewports[0].Width,
				snapshot.viewports[0].Height,
				static_cast<float>(snapshot.finalWidth),
				static_cast<float>(snapshot.finalHeight));
		const bool scissor0EngineExtent =
			DiagnosticExtentMatches(
				scissor0Width,
				scissor0Height,
				static_cast<float>(snapshot.engineWidth),
				static_cast<float>(snapshot.engineHeight)) &&
			!DiagnosticExtentMatches(
				scissor0Width,
				scissor0Height,
				static_cast<float>(snapshot.finalWidth),
				static_cast<float>(snapshot.finalHeight));
		logger::debug(
			"[VRMenuDiag] {} {} frame={} textRaster context={} fullResMenuDraw={} menuPlaneCtx={} viewport0Class={} scissor0Class={} viewport0EngineExtent={} scissor0EngineExtent={} guardCandidate={}",
			DiagnosticText(a_passName, "unknown"),
			DiagnosticText(a_phase, "unknown"),
			snapshot.frame,
			BoolText(snapshot.menuTextRasterDiagnosticContext),
			BoolText(snapshot.fullResolutionMenuUIDraw),
			BoolText(snapshot.menuPlaneDiagnosticContext),
			ClassifyVRMenuRasterExtent(snapshot.viewports[0].Width, snapshot.viewports[0].Height, snapshot),
			ClassifyVRMenuRasterExtent(scissor0Width, scissor0Height, snapshot),
			BoolText(viewport0EngineExtent),
			BoolText(scissor0EngineExtent),
			BoolText(snapshot.menuTextRasterDiagnosticContext && (viewport0EngineExtent || scissor0EngineExtent)));

		if (snapshot.menuPlaneDiagnosticContext) {
			logger::debug(
				"[VRMenuDiag] {} {} frame={} menuPlane shaders VS={} GS={} PS={}",
				DiagnosticText(a_passName, "unknown"),
				DiagnosticText(a_phase, "unknown"),
				snapshot.frame,
				FormatD3DShaderDiagnosticInfo(snapshot.vertexShader),
				FormatD3DShaderDiagnosticInfo(snapshot.geometryShader),
				FormatD3DShaderDiagnosticInfo(snapshot.pixelShader));

			for (size_t i = 0; i < snapshot.vertexCBs.size(); ++i) {
				logger::debug(
					"[VRMenuDiag] {} {} frame={} menuPlane VS cb{} {}",
					DiagnosticText(a_passName, "unknown"),
					DiagnosticText(a_phase, "unknown"),
					snapshot.frame,
					i,
					FormatD3DBufferDiagnosticInfo(snapshot.vertexCBs[i]));
			}
			for (size_t i = 0; i < snapshot.pixelCBs.size(); ++i) {
				logger::debug(
					"[VRMenuDiag] {} {} frame={} menuPlane PS cb{} {}",
					DiagnosticText(a_passName, "unknown"),
					DiagnosticText(a_phase, "unknown"),
					snapshot.frame,
					i,
					FormatD3DBufferDiagnosticInfo(snapshot.pixelCBs[i]));
			}
		}

		logger::debug(
			"[VRMenuDiag] {} {} frame={} viewport0=({:.1f},{:.1f}) {:.1f}x{:.1f} viewport1=({:.1f},{:.1f}) {:.1f}x{:.1f} scissor0=({},{})->({},{}) scissor1=({},{})->({},{})",
			DiagnosticText(a_passName, "unknown"),
			DiagnosticText(a_phase, "unknown"),
			snapshot.frame,
			snapshot.viewports[0].TopLeftX,
			snapshot.viewports[0].TopLeftY,
			snapshot.viewports[0].Width,
			snapshot.viewports[0].Height,
			snapshot.viewports[1].TopLeftX,
			snapshot.viewports[1].TopLeftY,
			snapshot.viewports[1].Width,
			snapshot.viewports[1].Height,
			snapshot.scissors[0].left,
			snapshot.scissors[0].top,
			snapshot.scissors[0].right,
			snapshot.scissors[0].bottom,
			snapshot.scissors[1].left,
			snapshot.scissors[1].top,
			snapshot.scissors[1].right,
			snapshot.scissors[1].bottom);

		LogVRPresentationDiagnosticViews(a_passName, a_phase, snapshot.frame, "PS t", snapshot.psSRVs);
		LogVRPresentationDiagnosticViews(a_passName, a_phase, snapshot.frame, "CS t", snapshot.csSRVs);
		LogVRPresentationDiagnosticViews(a_passName, a_phase, snapshot.frame, "RTV", snapshot.rtvs);
		logger::debug(
			"[VRMenuDiag] {} {} frame={} DSV {}",
			DiagnosticText(a_passName, "unknown"),
			DiagnosticText(a_phase, "unknown"),
			snapshot.frame,
			FormatD3DViewDiagnosticInfo(snapshot.dsv));

		if (a_includeKnownTargets)
			LogVRPresentationKnownTargets(a_passName, a_phase, snapshot.frame);
	}

	struct ScenePausedUiState
	{
		bool canEnable = false;
		bool requested = false;
		bool active = false;
		bool pausedInMenu = false;
		bool highlight = false;
		const char* statusText = "inactive";
	};

	ScenePausedUiState BuildScenePausedUiState(
		bool a_canEnable,
		bool a_requested,
		bool a_liveActive,
		bool a_wasActiveInScene,
		bool a_menuPaused)
	{
		ScenePausedUiState state{};
		state.canEnable = a_canEnable;
		state.requested = state.canEnable && a_requested;
		state.active = state.requested && a_liveActive;
		state.pausedInMenu = state.requested && !state.active && a_menuPaused && a_wasActiveInScene;
		state.highlight = state.active || state.pausedInMenu;
		state.statusText =
			state.active ? "active" :
			(state.pausedInMenu ? "paused in menu" :
				(state.requested ? "pending scene" : "inactive"));
		return state;
	}

	ScenePausedUiState BuildAAVRSUiState(bool a_methodEligible, bool a_adapterEligible, bool a_toggleEnabled, bool a_runtimeActive)
	{
		const bool canEnable = a_methodEligible && a_adapterEligible;
		const bool requested = canEnable && a_toggleEnabled;
		const bool menuPaused = globals::game::isVR ? IsVRSceneFeatureMenuPauseContextActive() : IsKnownGameMenuContextActive();
		return BuildScenePausedUiState(canEnable, requested, a_runtimeActive && !menuPaused, a_runtimeActive, menuPaused);
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

	struct RuntimeResolutionPlanLogKey
	{
		Upscaling::UpscaleMethod method = Upscaling::UpscaleMethod::kNONE;
		Upscaling::ResolutionOwner owner = Upscaling::ResolutionOwner::Native;
		Upscaling::UpscalingOutputTarget target = Upscaling::UpscalingOutputTarget::Main;
		uint32_t qualityMode = 0;
		uint32_t displayWidth = 0;
		uint32_t displayHeight = 0;
		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		uint32_t finalWidth = 0;
		uint32_t finalHeight = 0;
		bool vendorMethod = false;
		bool foveatedActive = false;
		bool peripheryTAAActive = false;
		bool menuContextActive = false;
		bool knownMenuContextActive = false;
		bool loadingMenuActive = false;
		bool perfModeRestartRequired = false;
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
		key.target = a_plan.outputTarget;
		key.qualityMode = a_plan.qualityMode;
		key.displayWidth = clampLogDimension(a_plan.trueHMDDisplaySize.x);
		key.displayHeight = clampLogDimension(a_plan.trueHMDDisplaySize.y);
		key.renderWidth = clampLogDimension(a_plan.engineRenderSize.x);
		key.renderHeight = clampLogDimension(a_plan.engineRenderSize.y);
		key.finalWidth = clampLogDimension(a_plan.finalOutputSize.x);
		key.finalHeight = clampLogDimension(a_plan.finalOutputSize.y);
		key.vendorMethod = a_plan.vendorMethod;
		key.foveatedActive = a_plan.foveatedActive;
		key.peripheryTAAActive = a_plan.peripheryTAAActive;
		key.menuContextActive = a_plan.menuContextActive;
		key.knownMenuContextActive = a_plan.knownMenuContextActive;
		key.loadingMenuActive = a_plan.loadingMenuActive;
		key.perfModeRestartRequired = a_plan.perfModeRestartRequired;
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
			"[Upscaling] Runtime plan: owner={} target={} method={} quality={} display={}x{} render={}x{} final={}x{} vendor={} foveated={} peripheryTAA={} menu={} knownMenu={} loading={} perfRestart={}",
			magic_enum::enum_name(key.owner),
			magic_enum::enum_name(key.target),
			magic_enum::enum_name(key.method),
			key.qualityMode,
			key.displayWidth,
			key.displayHeight,
			key.renderWidth,
			key.renderHeight,
			key.finalWidth,
			key.finalHeight,
			key.vendorMethod,
			key.foveatedActive,
			key.peripheryTAAActive,
			key.menuContextActive,
			key.knownMenuContextActive,
			key.loadingMenuActive,
			key.perfModeRestartRequired);

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
				upscaling.UpgradeBackendInterface((void**)&(*ppSwapChain));
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
		const bool vrRenderScaleActive = IsVRRenderScaleModeActive();
		const bool renderScaleMethodEligible = IsRenderScaleMethodEligible(upscaleMethod);
		const uint32_t renderScaleQualityMode = renderScaleMethodEligible ? GetEffectiveUpscalingQualityMode() : settings.qualityMode;
		const bool renderScaleQualitySelected = IsRenderScaleQualityMode(renderScaleQualityMode);
		const bool renderScaleToggleRequested = GetVRRenderScaleModeRequested();
		const bool vrRenderScaleRequested = GetPerfModeRequested();
		if (!renderScaleToggleRequested && !vrRenderScaleActive)
			submitStageRuntimeActive.store(false, std::memory_order_relaxed);
		const auto renderScaleUiState = BuildScenePausedUiState(
			renderScaleMethodEligible,
			vrRenderScaleRequested,
			vrRenderScaleActive,
			submitStageRuntimeActive.load(std::memory_order_relaxed) || vrRenderScaleActive,
			IsGameMenuContextActive());
		const bool perfModeRelatchPending = pendingPerfModeRenderTargetRecreate.load(std::memory_order_relaxed);
		const bool publicRenderScaleRequested = vrRenderScaleRequested;
		const bool publicRenderScaleCanEdit =
			(renderScaleMethodEligible && renderScaleQualitySelected) ||
			publicRenderScaleRequested;
		const bool showSubmitPathDeveloperToggle = globals::state && globals::state->IsDeveloperMode();

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

		if (showSubmitPathDeveloperToggle) {
			const bool submitPathRequested = renderScaleToggleRequested && !vrRenderScaleRequested;
			const bool submitPathCanEdit =
				(renderScaleMethodEligible || submitPathRequested) &&
				!publicRenderScaleRequested &&
				!perfModeRelatchPending;
			const char* submitPathModes[] = { "Disabled", "Enabled" };
			int submitPathMode = submitPathRequested ? 1 : 0;
			{
				auto disabledGuard = Util::DisableGuard(!submitPathCanEdit);
				if (ImGui::SliderInt("Legacy Submit-Stage Only", &submitPathMode, 0, 1, submitPathModes[std::clamp(submitPathMode, 0, 1)])) {
					const bool enableSubmitPath = std::clamp(submitPathMode, 0, 1) != 0;
					const uint32_t targetQualityMode = renderScaleQualitySelected ? renderScaleQualityMode : kDefaultRenderScaleQualityMode;
					if (GetEffectiveUpscalingQualityMode() != targetQualityMode)
						QueueVRUpscalingQualityMode(targetQualityMode);
					if (IsRenderScaleModeRequested() != enableSubmitPath)
						QueueVRRenderScaleModeRequest(enableSubmitPath);
					if (GetPerfModeRequested())
						QueueVRPerfModeRequest(false);
					RequestHistoryReset();
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Developer diagnostic only.");
				ImGui::TextUnformatted("Tests submit-stage upscaling without the VR Render Scale Mode render-target relatch.");
				ImGui::TextUnformatted("Use VR Render Scale Mode for the normal path.");
			}
			if (renderScaleUiState.pausedInMenu)
				ImGui::TextDisabled("Legacy submit-stage upscaling was active in scene and is paused while this menu is open.");
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

			ImGui::SliderFloat("Sharpness", &settings.sharpnessDLSS, 0.0f, 1.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Adjusts post-upscale sharpness for DLSS.");
				ImGui::TextUnformatted("Range: low 0.0 (softest) to high 1.0 (sharpest).");
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
				const auto aaVrsUiState = BuildAAVRSUiState(
					IsAAVRSEligible(upscaleMethod),
					IsAAVRSAdapterEligible(),
					settings.aaVrs,
					aaVrsRuntimeActive);
				ImGui::TextDisabled("Configure foveated upscaling and Variable Rate Shading (VRS) in VR > Foveated / Variable Rate Shading (VRS).");
				ImGui::TextColored(
					fovActive ? ImVec4(0.40f, 0.85f, 0.50f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
					"FOV: %s",
					fovActive ? "active" : "inactive");
				ImGui::TextColored(
					aaVrsUiState.highlight ? ImVec4(0.40f, 0.85f, 0.50f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
					"Variable Rate Shading (VRS): %s",
					aaVrsUiState.statusText);
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
		ImGui::Spacing();
		drawInstructionHeadline("Variable Rate Shading (VRS) mask refinement");
		ImGui::TextUnformatted(kVrsMaskRefinementInstructions);
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

	SanitizeFoveatedSettings(settings);
	const UpscaleMethod upscaleMethod = GetUpscaleMethod();
	const bool foveatedDispatchSupportedForMethod = SupportsFoveatedVendorDispatch(upscaleMethod);
	const bool aaVrsMethodEligible = IsAAVRSEligible(upscaleMethod);
	const bool aaVrsAdapterEligible = IsAAVRSAdapterEligible();
	auto aaVrsUiState = BuildAAVRSUiState(
		aaVrsMethodEligible,
		aaVrsAdapterEligible,
		settings.aaVrs,
		aaVrsRuntimeActive);

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

	{
		Util::BlueFrameStyleWrapper aaVrsStyle(true);
		auto disabledGuard = Util::DisableGuard(!aaVrsUiState.canEnable);
		bool aaVrs = aaVrsUiState.requested;
		ImGui::Checkbox(kFoveatedVrsName, &aaVrs);
		if (aaVrsUiState.canEnable)
			settings.aaVrs = aaVrs;
	}
	aaVrsUiState = BuildAAVRSUiState(
		aaVrsMethodEligible,
		aaVrsAdapterEligible,
		settings.aaVrs,
		aaVrsRuntimeActive);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Enables NVIDIA Variable Rate Shading (VRS) during VR scene pixel shading for foveated upscaling.");
		ImGui::TextUnformatted("Requires active Foveated Upscaling (FOV); non-foveated modes keep Variable Rate Shading disabled.");
		ImGui::TextUnformatted("Uses 1x1 through the active foveated/TAA mask; without FOV + TAA, the foveated feather is included.");
		ImGui::TextUnformatted("Adds one VRS tile of safety padding around the protected mask to avoid coarse-rate flicker at the transition.");
		ImGui::TextUnformatted("Outside the mask, the inner fifth is 2x2 and the rest uses the configured max coarse rate.");
		ImGui::TextUnformatted("Content-aware safety can promote high-contrast, bright, moving, or depth-edge tiles back toward 1x1.");
		ImGui::TextUnformatted("Pass-aware safety keeps unstable passes at 1x1; shadow maps are suspended and VRS is disabled before postprocessing.");
	}

	if (!aaVrsMethodEligible) {
		if (foveatedDispatchSupportedForMethod)
			ImGui::TextDisabled("Enable Foveated Upscaling (FOV) with an active mask to use Foveated Variable Rate Shading (VRS).");
		else
			ImGui::TextDisabled("Foveated Variable Rate Shading (VRS) is available only with DLSS/FSR Foveated Upscaling in VR.");
	} else if (!aaVrsAdapterEligible) {
		ImGui::TextDisabled("Foveated Variable Rate Shading (VRS) requires NVIDIA variable pixel-rate shading support.");
	}

	ImGui::TextColored(
		aaVrsUiState.highlight ? ImVec4(0.40f, 0.85f, 0.50f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
		"Foveated Variable Rate Shading (VRS): %s",
		aaVrsUiState.statusText);
	if (aaVrsUiState.pausedInMenu)
		ImGui::TextDisabled("Foveated Variable Rate Shading (VRS) was active in scene and is paused while this menu is open.");
	else if (aaVrsUiState.requested && !aaVrsUiState.active) {
		const auto aaVrsStatus = aaVrsController.GetStatus();
		if (aaVrsStatus.hasSettings && aaVrsStatus.lastDisableReason && aaVrsStatus.lastDisableReason[0])
			ImGui::TextDisabled("Foveated Variable Rate Shading (VRS) runtime inactive: %s", aaVrsStatus.lastDisableReason);
	}

	if (aaVrsUiState.requested) {
		ImGui::Dummy(ImVec2(0.0f, 3.0f));
		{
			Util::BlueFrameStyleWrapper passAwareStyle(true);
			ImGui::Checkbox("VRS Pass-Aware Safety", &settings.aaVrsPassAware);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Keeps alpha-tested, emissive, decal, particle, sky, grass, distant-tree, and depth/mask utility passes at 1x1.");
			ImGui::TextUnformatted("Coarse rates are used only for passes that look like stable opaque scene shading.");
			ImGui::TextUnformatted("Water has its own full-rate protection toggle below.");
		}

		{
			Util::BlueFrameStyleWrapper contentAwareStyle(true);
			ImGui::Checkbox("VRS Content-Aware Safety", &settings.aaVrsContentAware);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Refines the VRS rate image from scene color, motion vectors, and depth before scene rendering.");
			ImGui::TextUnformatted("High contrast, bright, moving, or depth-edge tiles are promoted back toward 1x1.");
			ImGui::TextUnformatted("The refinement never makes the base foveated mask coarser.");
		}

		{
			Util::BlueFrameStyleWrapper performanceStyle(true);
			ImGui::Checkbox("VRS Performance Mode", &settings.aaVrsPerformanceMode);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses fixed per-eye VRS bands: 0.25=1x1, 0.40=2x1/1x2, 0.70=2x2, outside=4x4.");
			ImGui::TextUnformatted("Stereo tiles still merge conservatively, so eye disagreements can promote a tile back toward 1x1.");
			ImGui::TextUnformatted("This ignores the normal protected FOV-mask size for the base VRS pattern; pass-aware safety still forces risky passes to 1x1.");
		}

		const char* anisotropyItems[] = { "Auto", "2x1", "1x2" };
		int anisotropy = static_cast<int>(std::clamp<uint>(settings.aaVrsPerformanceAnisotropy, 0u, 2u));
		{
			auto anisotropyGuard = Util::DisableGuard(!settings.aaVrsPerformanceMode);
			if (ImGui::Combo("VRS Performance Orientation", &anisotropy, anisotropyItems, IM_ARRAYSIZE(anisotropyItems))) {
				settings.aaVrsPerformanceAnisotropy = static_cast<uint>(std::clamp(anisotropy, 0, 2));
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls the half-rate band in VRS Performance Mode.");
			ImGui::TextUnformatted("Auto chooses 2x1 or 1x2 radially per tile; fixed modes force one anisotropic orientation to reduce stereo disagreements.");
		}

		const char* maxRateItems[] = { "2x2", "4x4" };
		int maxRate = static_cast<int>(std::clamp<uint>(settings.aaVrsMaxRate, 0u, 1u));
		{
			auto maxRateGuard = Util::DisableGuard(settings.aaVrsPerformanceMode);
			if (ImGui::Combo("VRS Max Coarse Rate", &maxRate, maxRateItems, IM_ARRAYSIZE(maxRateItems))) {
				settings.aaVrsMaxRate = static_cast<uint>(std::clamp(maxRate, 0, 1));
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Caps the coarsest rate used outside the protected FOV mask.");
			ImGui::TextUnformatted("2x2 is more conservative; 4x4 has more performance risk and more artifact risk.");
			ImGui::TextUnformatted("Performance mode uses 4x4 for its outer band, with NVAPI fallback to 2x2 if 4x4 is unavailable.");
		}

		{
			auto safeOnlyGuard = Util::DisableGuard(!settings.aaVrsPassAware);
			ImGui::Checkbox("VRS Safe Opaque Only", &settings.aaVrsSafeOpaqueOnly);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Debug safety mode: only lighting passes that survive the pass-aware filter can use coarse rates.");
			ImGui::TextUnformatted("Use this when isolating white flicker or shimmer sources.");
		}

		{
			auto waterGuard = Util::DisableGuard(!settings.aaVrsPassAware || settings.aaVrsSafeOpaqueOnly);
			ImGui::Checkbox("VRS Full-Rate Water", &settings.aaVrsProtectWater);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Keeps water shader passes at 1x1 while VRS pass-aware safety is active.");
			ImGui::TextUnformatted("Water uses animated normals, reflection, and refraction, so coarse rates can shimmer or break edges.");
			ImGui::TextUnformatted("Disabled while VRS Safe Opaque Only is active because that broader debug mode already protects water.");
		}

		ImGui::Checkbox("VRS Pass Telemetry", &settings.aaVrsPassTelemetry);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Shows cumulative counts for pass-aware full-rate decisions.");
		}
		if (settings.aaVrsPassTelemetry) {
			if (ImGui::Button("Reset VRS Pass Counters"))
				ResetAAVRSPassTelemetry();
			DrawAAVRSPassTelemetry();
		}
	} else {
		settings.aaVrsVisualization = false;
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
		const bool aaVrsVisualizationAvailable = aaVrsUiState.requested;
		if (aaVrsVisualizationAvailable) {
			ImGui::Checkbox(kVrsMaskVisualizationName, &settings.aaVrsVisualization);
		} else {
			settings.aaVrsVisualization = false;
		}
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Use this while tuning FOV masks.");
		ImGui::TextUnformatted("Green = upscaling center mask.");
		if (settings.periphery_taa_enable)
			ImGui::TextUnformatted("Gold = TAA ring, blue = outer lightweight ring.");
		else
			ImGui::TextUnformatted("Dark = outside the upscaling FOV mask.");
		if (aaVrsUiState.requested) {
			ImGui::TextUnformatted("VRS Mask Visualization replaces the scene with the current binary rate image; dark = 1x1, magenta = coarser than 1x1.");
			ImGui::TextUnformatted("Content-aware tile promotions are included; per-pass full-rate overrides are applied dynamically during draw calls.");
			if (settings.aaVrsPerformanceMode)
				ImGui::TextUnformatted("Performance mode target: dark includes the fixed 0.25 inner band and any content-aware full-rate promotions.");
			else
				ImGui::TextUnformatted("Target: no magenta visible in your view, using the smallest possible FOV mask size for maximum performance and image quality.");
		}
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

	settings.upscaleMethod = static_cast<uint>(UpscaleMethod::kNONE);
	settings.upscaleMethodNoDLSS = static_cast<uint>(UpscaleMethod::kNONE);
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
	const bool hasLegacySubmitStageUpscalingSetting = o_json.contains("submitStageUpscaling");
	const bool hasLegacyPerfModeSetting = o_json.contains("perfMode");
	settings = o_json;
	if (!hasRenderScaleModeSetting && hasLegacySubmitStageUpscalingSetting) {
		try {
			settings.renderScaleMode = o_json.at("submitStageUpscaling").get<uint>();
		} catch (...) {
			logger::warn("[Upscaling] Loaded legacy submitStageUpscaling setting could not be migrated; using VR Render Scale Mode default.");
		}
	} else if (!hasRenderScaleModeSetting && hasLegacyPerfModeSetting) {
		try {
			settings.renderScaleMode = o_json.at("perfMode").get<uint>();
		} catch (...) {
			logger::warn("[Upscaling] Loaded legacy perfMode setting could not be migrated; using VR Render Scale Mode default.");
		}
	} else if (!hasRenderScaleModeSetting) {
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
	settings.aaVrsVisualization = false;

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
	settings.aaVrsVisualization = false;
	settings.reflexLowLatencyMode = true;
	settings.reflexUseMarkersToOptimize = true;
	settings.reflexLowLatencyBoost = false;
	settings.reflexUseFPSLimit = false;
	SanitizeUpscalingSettings(settings);
	ApplyOpenCompositeUpscalingBlocker(true);
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
		const bool allowPerfModeBootLatchCreate = upscaling.ConsumePerfModeBootLatchCreate();
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
	ApplyDeferredCompositeVRSRuntimeSettings("data load");
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
	if (!a_event)
		return RE::BSEventNotifyControl::kContinue;

	if (globals::game::isVR && IsVRMenuPresentationTailMenuName(a_event->menuName))
		ExtendVRMenuPresentationTail();

	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		g_vrLoadingMenuOpenFromEvent.store(a_event->opening, std::memory_order_relaxed);
		if (a_event->opening) {
			g_vrLoadingTransitionCloseFrame.store(0, std::memory_order_release);
			g_vrLoadingTransitionTailEndFrame.store(0, std::memory_order_release);
			g_vrMenuPresentationTailEndFrame.store(0, std::memory_order_release);
			g_vrObservedProjectedMenuTailEndFrame.store(0, std::memory_order_release);
			LogPendingVRFpsStabilizerLoadSyncRetained(
				globals::features::upscaling,
				"loading menu opened before save-load sync could apply");
		} else if (globals::state) {
			const uint32_t currentFrame = std::max(globals::state->frameCount, 1u);
			g_vrLoadingTransitionCloseFrame.store(currentFrame, std::memory_order_release);
			g_vrLoadingTransitionTailEndFrame.store(
				currentFrame + kVRSaveLoadTransitionTailFrames,
				std::memory_order_release);
		}
		LogVRTransitionDiagnostics(
			globals::features::upscaling,
			a_event->opening ? "loading menu opened" : "loading menu closed",
			true);
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
		logger::error("[Upscaling] UI event source not found; VR upscaling menu/load transition handling disabled");
		return false;
	}

	auto eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!eventSource) {
		logger::error("[Upscaling] MenuOpenCloseEvent source not found; VR upscaling menu/load transition handling disabled");
		return false;
	}

	g_vrLoadingMenuOpenFromEvent.store(ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME), std::memory_order_relaxed);
	eventSource->AddEventSink(&singleton);
	registered.store(true, std::memory_order_release);
	logger::debug("[Upscaling] Registered MenuOpenCloseEventHandler for VR upscaling menu/load transitions");
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

	// Keep vanilla/manual dynamic resolution active. Vendor upscaling replaces
	// the vanilla upsample pass at the image-space hook points below.

	// Performs upscaling in between volumetric lighting and post processing
	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7, 0x206));

	stl::write_vfunc<0x1, UpsampleDynamicResolution_Render>(
		RE::VTABLE_BSImagespaceShaderISUpsampleDynamicResolution[3]);
	stl::write_vfunc<0x1, CopyDynamicFetchDisabled_Render>(
		RE::VTABLE_BSImagespaceShaderCopyDynamicFetchDisabled[3]);
	stl::write_vfunc<0xC, UpsampleDynamicResolution_Dispatch>(
		RE::VTABLE_BSImagespaceShaderISUpsampleDynamicResolution[0]);
	stl::write_vfunc<0xC, CopyDynamicFetchDisabled_Dispatch>(
		RE::VTABLE_BSImagespaceShaderCopyDynamicFetchDisabled[0]);
	if (globals::game::isVR) {
		stl::write_vfunc<0x1, HDRTonemapBlendCinematicFade_Render>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[3]);
		stl::write_vfunc<0x1, TemporalAAUI_Render>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_UI[3]);
		stl::write_vfunc<0x1, LightingCompositeMenu_Render>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeMenu[3]);
		stl::write_vfunc<0xC, HDRTonemapBlendCinematicFade_Dispatch>(
			RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[0]);
		stl::write_vfunc<0xC, TemporalAAUI_Dispatch>(
			RE::VTABLE_BSImagespaceShaderISTemporalAA_UI[0]);
		stl::write_vfunc<0xC, LightingCompositeMenu_Dispatch>(
			RE::VTABLE_BSImagespaceShaderISLightingCompositeMenu[0]);
		stl::write_vfunc<0x1, FullScreenVR_Render>(
			RE::VTABLE_BSImagespaceShaderISFullScreenVR[3]);
		stl::write_vfunc<0xC, FullScreenVR_Dispatch>(
			RE::VTABLE_BSImagespaceShaderISFullScreenVR[0]);
	}

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
	if (IsPerfModeActive() && IsVendorUpscalingMethod(boot.method))
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
	if (IsPerfModeActive())
		return ClampQualityModeUInt(boot.qualityMode);

	return ClampQualityModeUInt(settings.qualityMode);
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

	const bool perfModeActive = IsPerfModeActive();
	if (perfModeActive) {
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
		plan.owner = ResolutionOwner::VendorDynamicResolution;
		plan.outputTarget = plan.upscaleMethod == UpscaleMethod::kDLSS && settings.sharpnessDLSS > 0.0f ?
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

bool Upscaling::IsVRRenderScaleModeActive() const
{
	return IsPerfModeActive();
}

Upscaling::VRRenderScaleStatus Upscaling::GetVRRenderScaleModeStatus() const
{
	if (!REL::Module::IsVR())
		return VRRenderScaleStatus::Disabled;

	const bool renderScaleToggleRequested = GetVRRenderScaleModeRequested();
	const bool vrRenderScaleRequested = GetPerfModeRequested();
	const bool active = IsPerfModeActive();
	const bool relatchPending =
		pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
		perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) ||
		HasPendingVRRenderScaleTransition();
	if (!renderScaleToggleRequested && !active && !relatchPending && !perfMode.HasRestartRequiredChange())
		return VRRenderScaleStatus::Disabled;

	if (GetOpenCompositeUpscalingBlocker().active || IsRenderDocUpscalingBlocked())
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

	if (!vrRenderScaleRequested)
		return VRRenderScaleStatus::SubmitStageOnly;

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
	case VRRenderScaleStatus::SubmitStageOnly:
		return "Legacy submit-stage only";
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

bool Upscaling::IsDeferredCompositePSRequested() const
{
	return globals::game::isVR && settings.aaVrs && settings.experimentalDeferredCompositePS;
}

bool Upscaling::IsDeferredCompositePSRuntimeEnabled() const
{
	return deferredCompositePSRuntimeEnabled;
}

bool Upscaling::IsDeferredCompositePSPending() const
{
	return deferredCompositePSRuntimeEnabled != IsDeferredCompositePSRequested();
}

bool Upscaling::IsAAVRSDeferredCompositeRequested() const
{
	return IsDeferredCompositePSRequested() && settings.aaVrsDeferredComposite;
}

bool Upscaling::IsAAVRSDeferredCompositeRuntimeEnabled() const
{
	return aaVrsDeferredCompositeRuntimeEnabled;
}

bool Upscaling::IsAAVRSDeferredCompositePending() const
{
	return aaVrsDeferredCompositeRuntimeEnabled != IsAAVRSDeferredCompositeRequested();
}

bool Upscaling::IsDeferredCompositePSActive() const
{
	return globals::game::isVR && settings.aaVrs && deferredCompositePSRuntimeEnabled && aaVrsController.IsActive();
}

bool Upscaling::ShouldUseAAVRSForDeferredComposite() const
{
	return IsDeferredCompositePSActive() && aaVrsDeferredCompositeRuntimeEnabled;
}

void Upscaling::ApplyDeferredCompositeVRSRuntimeSettings(const char* a_reason)
{
	const char* reasonPrefix = a_reason && a_reason[0] ? " for " : "";
	const char* reasonText = a_reason && a_reason[0] ? a_reason : "";
	const bool runtimeBlocked = GetOpenCompositeUpscalingBlocker().active || IsRenderDocUpscalingBlocked(false);
	bool nextDeferredCompositePS = IsDeferredCompositePSRequested() && !runtimeBlocked;
	bool nextDeferredCompositeVRS = IsAAVRSDeferredCompositeRequested() && !runtimeBlocked;

	const bool shouldPrewarmDeferredCompositeShaders =
		nextDeferredCompositePS &&
		globals::deferred &&
		globals::d3d::device &&
		globals::state &&
		globals::shaderCache &&
		globals::shaderCache->IsEnabled();

	if (shouldPrewarmDeferredCompositeShaders) {
		auto* deferred = globals::deferred;
		try {
			auto* metadataShader = deferred->GetComputeMainCompositeMetadata();
			auto* metadataShaderInterior = deferred->GetComputeMainCompositeMetadataInterior();
			auto* pixelShader = deferred->GetPixelMainComposite();
			auto* pixelShaderInterior = deferred->GetPixelMainCompositeInterior();
			auto* vertexShader = deferred->GetPixelMainCompositeVS();
			if (!metadataShader || !metadataShaderInterior || !pixelShader || !pixelShaderInterior || !vertexShader) {
				logger::warn(
					"[Upscaling] Deferred composite PS prewarm{}{} did not produce a complete shader set; leaving runtime disabled until the next load/restart.",
					reasonPrefix,
					reasonText);
				nextDeferredCompositePS = false;
				nextDeferredCompositeVRS = false;
			}
		} catch (const std::exception& e) {
			logger::warn(
				"[Upscaling] Deferred composite PS prewarm{}{} failed: {}; leaving runtime disabled until the next load/restart.",
				reasonPrefix,
				reasonText,
				e.what());
			nextDeferredCompositePS = false;
			nextDeferredCompositeVRS = false;
		} catch (...) {
			logger::warn(
				"[Upscaling] Deferred composite PS prewarm{}{} failed with an unknown exception; leaving runtime disabled until the next load/restart.",
				reasonPrefix,
				reasonText);
			nextDeferredCompositePS = false;
			nextDeferredCompositeVRS = false;
		}
	}

	const bool changed =
		deferredCompositePSRuntimeEnabled != nextDeferredCompositePS ||
		aaVrsDeferredCompositeRuntimeEnabled != nextDeferredCompositeVRS;

	deferredCompositePSRuntimeEnabled = nextDeferredCompositePS;
	aaVrsDeferredCompositeRuntimeEnabled = nextDeferredCompositeVRS;

	if (changed) {
		logger::info(
			"[Upscaling] Applied deferred composite VRS runtime settings{}{}: pixelShader={}, colorVRS={}",
			reasonPrefix,
			reasonText,
			BoolText(deferredCompositePSRuntimeEnabled),
			BoolText(aaVrsDeferredCompositeRuntimeEnabled));
	}
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
	const bool activeMatchesRequest = IsPerfModeActive() == a_enabled;
	if (ClampToggleUInt(settings.perfMode) == requested && activeMatchesRequest && !perfMode.HasRestartRequiredChange()) {
		if (renderScaleSettingsChanged) {
			RequestHistoryReset();
			RequestPerfModeRenderTargetRecreate(a_reason, a_origin);
		}
		return;
	}

	settings.perfMode = requested;
	RequestHistoryReset();
	RequestPerfModeRenderTargetRecreate(a_reason, a_origin);
}

void Upscaling::ApplyCSMenuUpscalingTransition(UpscaleMethod a_targetMethod, bool a_renderScaleModeEnabled, uint32_t a_qualityMode, uint32_t a_dlssPreset, const char* a_reason, VRUpscalingTransitionOrigin a_origin)
{
	const bool isVR = globals::game::isVR;
	if (isVR)
		CancelPendingVRFpsStabilizerLoadSync(*this, a_reason ? a_reason : "explicit upscaling transition");

	const bool allowPendingDLSSSelection =
		a_targetMethod == UpscaleMethod::kDLSS &&
		!streamline.featureCheckComplete;
	const bool allowDLSSSelection = streamline.featureDLSS || allowPendingDLSSSelection;
	const int maxMethodValue = allowDLSSSelection ?
		static_cast<int>(UpscaleMethod::kDLSS) :
		static_cast<int>(UpscaleMethod::kFSR);
	const int targetMethodValue = std::clamp(static_cast<int>(a_targetMethod), static_cast<int>(UpscaleMethod::kNONE), maxMethodValue);
	UpscaleMethod targetMethod = static_cast<UpscaleMethod>(targetMethodValue);
	if (GetOpenCompositeUpscalingBlocker().active) {
		targetMethod = UpscaleMethod::kNONE;
		settings.upscaleMethod = static_cast<uint32_t>(UpscaleMethod::kNONE);
		settings.upscaleMethodNoDLSS = static_cast<uint32_t>(UpscaleMethod::kNONE);
	}
	const uint32_t qualityMode = std::min(a_qualityMode, kQualityModeMaxIndex);
	const uint32_t dlssPreset = std::min(a_dlssPreset, kDLSSPresetMaxIndex);
	const bool renderScaleQuality = IsRenderScaleQualityMode(qualityMode);
	const auto previousMethod = GetUpscaleMethod();
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
		if (settingsChanged)
			RequestHistoryReset();
		return;
	}

	if (!methodChanged && !qualityTargetChanged && !renderScaleTargetChanged && dlssPresetChanged) {
		settings.dlssPreset = dlssPreset;
		pendingVRDLSSPreset.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
		clearPendingTransitionTimingIfIdle();
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
			 IsPerfModeActive() != targetPerfMode ||
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

	if (presetChanged || renderScaleModeChanged)
		RequestHistoryReset();

	const uint32_t requestedPerfMode = targetRenderScaleMode ? 1u : 0u;
	if (perfModeRequestPending)
		pendingVRPerfMode.store(kPendingVRUpscalingSettingUnset, std::memory_order_release);
	clearPendingTransitionTimingIfIdle();

	if (renderScaleModeChanged ||
		qualityChanged ||
		ClampToggleUInt(settings.perfMode) != requestedPerfMode ||
		IsPerfModeActive() != targetRenderScaleMode ||
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
		CancelPendingVRFpsStabilizerLoadSync(*this, "sync disabled or non-VR runtime");
		return;
	}

	const uint32_t frame = std::max(a_frame, 1u);
	pendingVRFpsStabilizerSyncLastWaitLogFrame.store(0, std::memory_order_release);
	pendingVRFpsStabilizerSyncFrame.store(frame, std::memory_order_release);
	logger::debug("[Upscaling] VR FPS Stabilizer Sync queued after save-load menu close at frame {}.", frame);
}

void Upscaling::ApplyPendingVRFpsStabilizerLoadSync()
{
	const uint32_t queuedFrame = pendingVRFpsStabilizerSyncFrame.load(std::memory_order_acquire);
	if (queuedFrame == 0)
		return;

	if (!globals::game::isVR || !settings.vrFpsStabilizerSync) {
		CancelPendingVRFpsStabilizerLoadSync(*this, "sync disabled or non-VR runtime");
		return;
	}

	const auto* state = globals::state;
	if (!state || IsLoadingMenuContextActive() || !state->inWorld) {
		LogPendingVRFpsStabilizerLoadSyncWait(*this, "world-ready state");
		return;
	}

	VRFpsStabilizerUpscalingProfiles profiles;
	if (!TryLoadVRFpsStabilizerUpscalingProfiles(profiles)) {
		ClearPendingVRFpsStabilizerLoadSync(*this);
		logger::warn(
			"[Upscaling] VR FPS Stabilizer Sync enabled, but no unconditional Interior/Exterior upscaling profile was found in {} (queuedFrame={}).",
			profiles.path.string(),
			queuedFrame);
		return;
	}

	const bool loadedInterior = Util::IsInterior();
	const auto& profile = loadedInterior ? profiles.interior : profiles.exterior;
	const char* profileName = loadedInterior ? "Interior" : "Exterior";
	if (!profile.HasAnySetting()) {
		ClearPendingVRFpsStabilizerLoadSync(*this);
		logger::warn(
			"[Upscaling] VR FPS Stabilizer Sync found no {} upscaling profile in {} (queuedFrame={}).",
			profileName,
			profiles.path.string(),
			queuedFrame);
		return;
	}

	const auto currentMethod = GetConfiguredUpscaleMethodForTransition();
	const auto target = ResolveVRFpsStabilizerTransitionTarget(*this, profile);

	const bool methodMatches = currentMethod == target.method;
	const bool qualityMatches = GetEffectiveUpscalingQualityMode() == target.qualityMode;
	const bool renderScaleMatches = IsRenderScaleModeRequested() == target.renderScaleMode;
	const bool dlssPresetMatches = target.method != UpscaleMethod::kDLSS || GetEffectiveDLSSPreset() == target.dlssPreset;
	ClearPendingVRFpsStabilizerLoadSync(*this);

	if (methodMatches && qualityMatches && renderScaleMatches && dlssPresetMatches) {
		logger::debug(
			"[Upscaling] VR FPS Stabilizer Sync: {} profile already matched after save-load (queuedFrame={}, appliedFrame={}, method={}, quality={}, dlssProfile={}, renderScale={}).",
			profileName,
			queuedFrame,
			GetCurrentFrameForLog(),
			magic_enum::enum_name(target.method),
			target.qualityMode,
			target.dlssPreset,
			BoolText(target.renderScaleMode));
		return;
	}

	logger::debug(
		"[Upscaling] VR FPS Stabilizer Sync applying {} profile from {}: queuedFrame={}, appliedFrame={}, method {} -> {}, quality {} -> {}, dlssProfile {} -> {}, renderScale {} -> {}.",
		profileName,
		profiles.path.string(),
		queuedFrame,
		GetCurrentFrameForLog(),
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
	LogVRTransitionDiagnostics(*this, "VR FPS Stabilizer Sync applied save-load profile", true);
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

	if (IsPerfModeActive()) {
		const auto displaySize = perfMode.GetDisplayScreenSize();
		const auto renderSize = perfMode.GetRenderScreenSize();
		if (displaySize.x <= 0.0f || displaySize.y <= 0.0f || renderSize.x <= 0.0f || renderSize.y <= 0.0f)
			return false;

		if (UsesFullSizeVRProtectedTarget(a_target))
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

	// RCAS sharpener texture - matches kMAIN format for HDR sharpening
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

	// RCAS sharpener texture is only needed for DLSS.
	if (a_upscalemethod != UpscaleMethod::kDLSS) {
		DestroyTexture(sharpenerTexture);
		DestroySubmitStageDLSSSharpenerTextures();
	}
}

void Upscaling::DestroySubmitStageDLSSSharpenerTextures()
{
	for (auto& texture : submitStageDLSSSharpenerTexture)
		texture.reset();
}

void Upscaling::DestroyCommonUpscalingTextures()
{
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
	       textureReady(motionVectorCopyTexture) &&
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
	if (vrKnownMenuSceneBeforeComposite)
		hasRetiredTextures = true;
	retired.knownMenuSceneBeforeComposite = std::move(vrKnownMenuSceneBeforeComposite);
	if (vrKnownMenuBackgroundComposite)
		hasRetiredTextures = true;
	retired.knownMenuBackgroundComposite = std::move(vrKnownMenuBackgroundComposite);

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
	vrKnownMenuSceneBeforeCompositeFrame = 0;
	vrKnownMenuBackgroundCompositeFrame = 0;
	ClearVRIntermediateTextureCache(cachedVRIntermediateTextures);
	peripheryTAAHistoryReadIndex = 0;
	peripheryTAAHistoryValid = false;

	submitStagePreparedFrame = std::numeric_limits<uint32_t>::max();
	submitStagePreparedFramePresentationOnly = false;
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
	logger::debug("[Upscaling] Armed VR post-load runtime reset");
}

void Upscaling::RequestPerfModeRenderTargetRecreate(const char* a_reason, VRUpscalingTransitionOrigin a_origin)
{
	if (!globals::game::isVR)
		return;

	if (GetOpenCompositeUpscalingBlocker().active)
		return;

	const auto configuredMethod = GetConfiguredUpscaleMethodForTransition();
	const bool perfModeActive = IsPerfModeActive();
	const bool perfModeEligible = perfMode.IsEligible(settings, configuredMethod);
	if (!perfModeActive && !perfModeEligible && !perfMode.HasRestartRequiredChange())
		return;

	auto* state = globals::state;
	const uint32_t relatchDelayFrames = ShouldApplyVRRenderScaleTransitionDuringLoadingMenu(*this, state) ?
		kVRLoadingMenuRelatchDelayFrames :
		kVRUpscalingTransitionApplyDelayFrames;
	const bool requirePostLoadSettle = UsesVRRenderScalePostLoadSettle(a_origin);
	const bool wasPending = pendingPerfModeRenderTargetRecreate.exchange(true, std::memory_order_acq_rel);
	if (!wasPending || requirePostLoadSettle) {
		pendingPerfModeRenderTargetRecreatePostLoadSettle.store(requirePostLoadSettle, std::memory_order_release);
	}
	const uint32_t previousRelatchDelay = pendingPerfModeRenderTargetRecreateDelayFrames.load(std::memory_order_acquire);
	const bool shortenRelatchForLoadingMenu =
		relatchDelayFrames == kVRLoadingMenuRelatchDelayFrames &&
		previousRelatchDelay == kVRUpscalingTransitionApplyDelayFrames;
	const bool updateRelatchDelay =
		!wasPending ||
		relatchDelayFrames > previousRelatchDelay ||
		shortenRelatchForLoadingMenu;
	if (updateRelatchDelay)
		MarkPerfModeRenderTargetRecreateQueued(relatchDelayFrames);
	RequestHistoryReset();
	if (updateRelatchDelay)
		LogVRTransitionDiagnostics(*this, "queued render-target relatch", true);
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

	static bool loggedRelatchPostLoadSettleDiagnostic = false;
	if (ShouldDeferVRRenderScaleRelatchForPostLoadSettle(*this, state)) {
		MarkPerfModeRenderTargetRecreateQueued(kVRRenderScalePostLoadSettleRetryFrames);
		const bool loggedPostLoadSettleDiagnostic = LogVRTransitionDiagnosticOnce(loggedRelatchPostLoadSettleDiagnostic, [&]() {
			const uint32_t currentFrame = std::max(state->frameCount, 1u);
			const uint32_t closeFrame = g_vrLoadingTransitionCloseFrame.load(std::memory_order_acquire);
			const uint32_t lastCompletedWorldFrame = state->lastCompletedWorldRenderFrame;
			logger::debug("[VRRenderScale] Render-target relatch waiting for post-load world-render settle.");
			VR_TRANSITION_DIAG_LOG(
				"[VRTransition] Relatch deferred: waiting for post-load world-render settle (frame={}, closeFrame={}, lastCompletedWorldFrame={}, retryFrames={})",
				currentFrame,
				closeFrame,
				lastCompletedWorldFrame == std::numeric_limits<uint32_t>::max() ? 0u : lastCompletedWorldFrame,
				kVRRenderScalePostLoadSettleRetryFrames);
		});
		LogVRTransitionDiagnostics(*this, "render-target relatch deferred: post-load world settle", loggedPostLoadSettleDiagnostic);
		return false;
	}
	loggedRelatchPostLoadSettleDiagnostic = false;

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

	static bool loggedRelatchApplyDiagnostic = false;
	static bool loggedRelatchBeginTeardownDiagnostic = false;
	static bool loggedRelatchVendorDeferDiagnostic = false;
	static bool loggedRelatchD3DDeferDiagnostic = false;
	static UpscaleMethod lastRelatchDiagnosticMethod = UpscaleMethod::kNONE;
	const auto clearRelatchRetryDiagnostics = [&]() {
		loggedRelatchApplyDiagnostic = false;
		loggedRelatchBeginTeardownDiagnostic = false;
		loggedRelatchVendorDeferDiagnostic = false;
		loggedRelatchD3DDeferDiagnostic = false;
	};
	if (lastRelatchDiagnosticMethod != relatchUpscaleMethod) {
		clearRelatchRetryDiagnostics();
		lastRelatchDiagnosticMethod = relatchUpscaleMethod;
	}

	logger::debug(
		"[VRRenderScale] Applying render-target relatch{}{}",
		a_caller && *a_caller ? " from " : "",
		a_caller && *a_caller ? a_caller : "");
	const bool forceApplyDiagnostic = !loggedRelatchApplyDiagnostic;
	loggedRelatchApplyDiagnostic = true;
	LogVRTransitionDiagnostics(*this, "applying render-target relatch", forceApplyDiagnostic);

	try {
		LogVRTransitionDiagnosticOnce(loggedRelatchBeginTeardownDiagnostic, [&]() {
			VR_TRANSITION_DIAG_LOG("[VRTransition] Relatch step: begin vendor teardown before D3D render-target recreate (method={})", magic_enum::enum_name(relatchUpscaleMethod));
		});
		if (!ResetVRVendorRuntimeResources(true, true)) {
			if (IsSubmitStageDeviceLost() || MarkSubmitStageDeviceLostIfDeviceRemoved("render-target relatch vendor resource teardown")) {
				clearRelatchDelay();
				clearRelatchRetryDiagnostics();
				return false;
			}

			requeueRelatch(kVRUpscalingTransitionApplyDelayFrames, false);
			if (relatchUpscaleMethod == UpscaleMethod::kDLSS)
				pendingDLSSReset.store(true, std::memory_order_release);
			const bool loggedVendorDeferDiagnostic = LogVRTransitionDiagnosticOnce(loggedRelatchVendorDeferDiagnostic, [&]() {
				logger::warn("[VRRenderScale] Render-target relatch deferred because vendor resources are still in use.");
				VR_TRANSITION_DIAG_LOG(
					"[VRTransition] Relatch deferred: vendor resources are still in use; retrying after {} frames",
					kVRUpscalingTransitionApplyDelayFrames);
			});
			LogVRTransitionDiagnostics(*this, "render-target relatch deferred: vendor resources still in use", loggedVendorDeferDiagnostic);
			return false;
		}
		VR_TRANSITION_DIAG_LOG("[VRTransition] Relatch step: vendor teardown complete before D3D render-target recreate");

		perfMode.ResetBootLatch();
		perfModeAllowBootLatchCreate.store(true, std::memory_order_release);
		perfMode.EnsureBootLatch(settings, relatchUpscaleMethod, true);

		const bool relatchRenderScaleActive = perfMode.IsActive(settings, relatchUpscaleMethod);
		const float2 relatchTargetDisplaySize = perfMode.GetDisplayScreenSize();
		const float2 relatchTargetEngineSize = relatchRenderScaleActive ?
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
			VR_TRANSITION_DIAG_LOG("[VRTransition] Relatch step: D3D render-target recreate skipped because dimensions are unchanged");
		} else {
			VR_TRANSITION_DIAG_LOG("[VRTransition] Relatch step: calling D3D render-target recreate");
			if (!Hooks::RecreateRenderTargetsForVRRenderScale()) {
				requeueRelatch(kVRRenderScaleRelatchBusyRetryFrames);
				const bool loggedD3DDeferDiagnostic = LogVRTransitionDiagnosticOnce(loggedRelatchD3DDeferDiagnostic, [&]() {
					logger::warn("[VRRenderScale] Render-target relatch could not run; will retry.");
					VR_TRANSITION_DIAG_LOG(
						"[VRTransition] Relatch deferred: D3D render-target recreate will retry in at least {} frames",
						kVRRenderScaleRelatchBusyRetryFrames);
				});
				LogVRTransitionDiagnostics(*this, "render-target relatch deferred: D3D recreate retry", loggedD3DDeferDiagnostic);
				return false;
			}
			VR_TRANSITION_DIAG_LOG("[VRTransition] Relatch step: D3D render-target recreate complete");
		}

		VR_TRANSITION_DIAG_LOG("[VRTransition] Relatch step: recreating vendor/common resources for {}", magic_enum::enum_name(relatchUpscaleMethod));
		RecreateVendorRuntimeResources(relatchUpscaleMethod, relatchUpscaleMethod != UpscaleMethod::kFSR);

		if (relatchUpscaleMethod == UpscaleMethod::kDLSS) {
			pendingDLSSHistoryReset.store(true, std::memory_order_release);
			pendingDLSSReset.store(false, std::memory_order_release);
			pendingFSRReset.store(false, std::memory_order_release);
			vrDLSSSettingsRelatched.store(true, std::memory_order_release);
		} else if (relatchUpscaleMethod == UpscaleMethod::kFSR) {
			pendingFSRReset.store(true, std::memory_order_release);
			pendingDLSSReset.store(false, std::memory_order_release);
			pendingDLSSHistoryReset.store(false, std::memory_order_release);
			vrDLSSSettingsRelatched.store(false, std::memory_order_release);
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
			if (retryFrames != kVRRenderScaleRelatchBusyRetryFrames) {
				VR_TRANSITION_DIAG_LOG(
					"[VRTransition] Relatch deferred after D3D allocation failure; retrying in at least {} frames",
					retryFrames);
			}
		} else {
			clearRelatchDelay();
			clearRelatchRetryDiagnostics();
		}
		logger::error("[VRRenderScale] Render-target relatch failed: {}", e.what());
		return false;
	} catch (...) {
		if (!MarkSubmitStageDeviceLostIfDeviceRemoved("render-target relatch")) {
			requeueRelatch(kVRRenderScaleRelatchBusyRetryFrames);
		} else {
			clearRelatchDelay();
			clearRelatchRetryDiagnostics();
		}
		logger::error("[VRRenderScale] Render-target relatch failed with an unknown exception");
		return false;
	}

	const uint32_t currentFrame = std::max(state->frameCount, 1u);
	vrRenderScaleResourceTrackingSyncPending.store(true, std::memory_order_release);
	submitStageVendorResumeStartFrame.store(currentFrame, std::memory_order_release);
	submitStageVendorResumeStableFrames.store(0, std::memory_order_release);
	submitStageVendorResumeLastStableFrame.store(0, std::memory_order_release);
	submitStageVendorResumeFrame.store(
		currentFrame + kVRSubmitStageVendorRelatchCooldownFrames,
		std::memory_order_release);
	clearRelatchDelay();
	clearRelatchRetryDiagnostics();
	logger::debug("[VRRenderScale] Applied render-target relatch");
	LogVRTransitionDiagnostics(*this, "applied render-target relatch", true);
	return true;
}

void Upscaling::ClearSubmitStageVendorResumeCooldown()
{
	submitStageVendorResumeFrame.store(0, std::memory_order_release);
	submitStageVendorResumeStartFrame.store(0, std::memory_order_release);
	submitStageVendorResumeStableFrames.store(0, std::memory_order_release);
	submitStageVendorResumeLastStableFrame.store(0, std::memory_order_release);
}

bool Upscaling::ResetVRSubmitStageState(bool a_destroyDLSSResources)
{
	if (!globals::game::isVR)
		return true;

	UnbindUpscalingResources();
	DestroyVRIntermediateTextures();
	DestroyFoveatedResources();

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
	submitStageMirrorFrame = std::numeric_limits<uint32_t>::max();
	submitStageMirrorEyeReady = {};
	submitStageMirrorSourceTexture = nullptr;
	submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	submitStageFoveatedPeripheryTAAEyeReady = {};
	submitStageRuntimeActive.store(false, std::memory_order_relaxed);
	ClearSubmitStageVendorResumeCooldown();
	vrRenderScaleResourceTrackingSyncPending.store(false, std::memory_order_release);
	historyResetTrackingInitialized = false;
	historyResetLatchedFrame = std::numeric_limits<uint32_t>::max();
	historyResetThisFrame = false;
	RequestHistoryReset();
	return dlssResourcesDestroyed;
}

bool Upscaling::ResetVRVendorRuntimeResources(bool a_destroyDLSSResources, bool a_destroyPeripheryTAAResources)
{
	if (!globals::game::isVR)
		return true;

	static bool loggedVendorTeardownRequestDiagnostic = false;
	static bool loggedVendorTeardownFSRWaitDiagnostic = false;
	static bool loggedVendorTeardownSubmitWaitDiagnostic = false;
	static bool loggedVendorTeardownSubmitResetDiagnostic = false;
	static bool hasVendorTeardownRequestSignature = false;
	static bool lastVendorTeardownDestroyDLSS = false;
	static bool lastVendorTeardownDestroyPeriphery = false;
	const auto clearVendorTeardownRetryDiagnostics = [&]() {
		loggedVendorTeardownRequestDiagnostic = false;
		loggedVendorTeardownFSRWaitDiagnostic = false;
		loggedVendorTeardownSubmitWaitDiagnostic = false;
		loggedVendorTeardownSubmitResetDiagnostic = false;
	};
	if (!hasVendorTeardownRequestSignature ||
		lastVendorTeardownDestroyDLSS != a_destroyDLSSResources ||
		lastVendorTeardownDestroyPeriphery != a_destroyPeripheryTAAResources) {
		clearVendorTeardownRetryDiagnostics();
		hasVendorTeardownRequestSignature = true;
		lastVendorTeardownDestroyDLSS = a_destroyDLSSResources;
		lastVendorTeardownDestroyPeriphery = a_destroyPeripheryTAAResources;
	}

	const bool loggedRequestDiagnostic = LogVRTransitionDiagnosticOnce(loggedVendorTeardownRequestDiagnostic, [&]() {
		VR_TRANSITION_DIAG_LOG(
			"[VRTransition] Vendor teardown requested: destroyDLSS={}, destroyPeripheryTAA={}, pendingReset(DLSS={}, FSR={})",
			BoolText(a_destroyDLSSResources),
			BoolText(a_destroyPeripheryTAAResources),
			BoolText(pendingDLSSReset.load(std::memory_order_acquire)),
			BoolText(pendingFSRReset.load(std::memory_order_acquire)));
	});
	LogVRTransitionDiagnostics(*this, "vendor teardown requested", loggedRequestDiagnostic);

	if (!fidelityFX.PollFSRResourceTeardownReady("VR vendor runtime FSR resource teardown")) {
		pendingFSRReset.store(true, std::memory_order_release);
		const bool loggedDeferralDiagnostic = LogVRTransitionDiagnosticOnce(loggedVendorTeardownFSRWaitDiagnostic, [&]() {
			VR_TRANSITION_DIAG_LOG("[VRTransition] Vendor teardown deferred: FSR resources are not idle yet");
		});
		LogVRTransitionDiagnostics(*this, "vendor teardown deferred: FSR not idle", loggedDeferralDiagnostic);
		return false;
	}

	LogVRTransitionDiagnosticOnce(loggedVendorTeardownSubmitResetDiagnostic, [&]() {
		VR_TRANSITION_DIAG_LOG("[VRTransition] Vendor teardown step: FSR teardown poll ready; resetting submit-stage state");
	});
	const bool submitStageReset = ResetVRSubmitStageState(a_destroyDLSSResources);
	if (!submitStageReset) {
		if (a_destroyDLSSResources)
			pendingDLSSReset.store(true, std::memory_order_release);
		const bool loggedDeferralDiagnostic = LogVRTransitionDiagnosticOnce(loggedVendorTeardownSubmitWaitDiagnostic, [&]() {
			VR_TRANSITION_DIAG_LOG("[VRTransition] Vendor teardown deferred: submit-stage DLSS resources are not idle yet");
		});
		LogVRTransitionDiagnostics(*this, "vendor teardown deferred: submit-stage reset not ready", loggedDeferralDiagnostic);
		return false;
	}

	VR_TRANSITION_DIAG_LOG("[VRTransition] Vendor teardown step: destroying FSR/common/periphery resources after idle poll");
	fidelityFX.DestroyFSRResources(false);
	DestroyCommonUpscalingTextures();
	if (a_destroyPeripheryTAAResources)
		DestroyPeripheryTAAResources();
	DisableAAVRSState();
	aaVrsController.ReleaseResources();
	ResetAAVRSTelemetry();
	VR_TRANSITION_DIAG_LOG("[VRTransition] Vendor teardown complete");
	clearVendorTeardownRetryDiagnostics();
	return true;
}

void Upscaling::RecreateVendorRuntimeResources(UpscaleMethod a_upscaleMethod, bool a_recreateTemporalResources)
{
	if (!IsVendorUpscalingMethod(a_upscaleMethod))
		return;

	VR_TRANSITION_DIAG_LOG(
		"[VRTransition] Recreating vendor resources: method={}, recreateTemporal={}",
		magic_enum::enum_name(a_upscaleMethod),
		BoolText(a_recreateTemporalResources));
	CreateUpscalingTextureResources(a_upscaleMethod);
	if (a_recreateTemporalResources && a_upscaleMethod == UpscaleMethod::kFSR)
		fidelityFX.CreateFSRResources();
	LogVRTransitionDiagnostics(*this, "recreated vendor resources", true);
}

bool Upscaling::ApplyPendingVendorRuntimeReset(UpscaleMethod a_upscaleMethod, const char* a_context)
{
	if (!globals::game::isVR)
		return true;

	static bool loggedVendorResetApplyDiagnostic = false;
	static bool loggedInactiveFSRResetDeferralDiagnostic = false;
	static bool loggedInactiveDLSSResetDeferralDiagnostic = false;
	static bool loggedDLSSRebuildDeferralDiagnostic = false;
	static bool loggedFSRRebuildDeferralDiagnostic = false;
	static bool loggedRelatchDeferralDiagnostic = false;
	static bool hasVendorResetRequestSignature = false;
	static UpscaleMethod lastVendorResetMethod = UpscaleMethod::kNONE;
	static std::string lastVendorResetContext;
	const auto clearVendorResetRetryDiagnostics = [&]() {
		loggedVendorResetApplyDiagnostic = false;
		loggedInactiveFSRResetDeferralDiagnostic = false;
		loggedInactiveDLSSResetDeferralDiagnostic = false;
		loggedDLSSRebuildDeferralDiagnostic = false;
		loggedFSRRebuildDeferralDiagnostic = false;
		loggedRelatchDeferralDiagnostic = false;
	};

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
	if (!activeResetPending && !inactiveResetPending) {
		clearVendorResetRetryDiagnostics();
		return true;
	}

	if (IsUpscalingLoadTransitionContextActive(*this)) {
		LogVRTransitionDiagnostics(*this, "vendor runtime reset waiting: load/transition context");
		return true;
	}
	if (pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
		perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire)) {
		const bool loggedRelatchDiagnostic = LogVRTransitionDiagnosticOnce(loggedRelatchDeferralDiagnostic, [&]() {
			VR_TRANSITION_DIAG_LOG(
				"[VRTransition] Deferred vendor runtime reset while render-target relatch owns runtime resources (method={}, pendingReset(DLSS={}, FSR={}), pendingRelatch={}, relatchInProgress={})",
				magic_enum::enum_name(a_upscaleMethod),
				BoolText(dlssResetPending),
				BoolText(fsrResetPending),
				BoolText(pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire)),
				BoolText(perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire)));
		});
		LogVRTransitionDiagnostics(*this, "vendor runtime reset waiting: render-target relatch", loggedRelatchDiagnostic);
		return true;
	}

	const std::string_view context = a_context ? std::string_view(a_context) : std::string_view{};
	if (!hasVendorResetRequestSignature ||
		lastVendorResetMethod != a_upscaleMethod ||
		lastVendorResetContext != context) {
		clearVendorResetRetryDiagnostics();
		hasVendorResetRequestSignature = true;
		lastVendorResetMethod = a_upscaleMethod;
		lastVendorResetContext = std::string(context);
	}
	const bool loggedApplyDiagnostic = LogVRTransitionDiagnosticOnce(loggedVendorResetApplyDiagnostic, [&]() {
		VR_TRANSITION_DIAG_LOG(
			"[VRTransition] Applying {}vendor runtime reset: method={}, activePending={}, inactivePending={}, pendingReset(DLSS={}, FSR={})",
			context,
			magic_enum::enum_name(a_upscaleMethod),
			BoolText(activeResetPending),
			BoolText(inactiveResetPending),
			BoolText(dlssResetPending),
			BoolText(fsrResetPending));
	});
	LogVRTransitionDiagnostics(*this, "applying vendor runtime reset", loggedApplyDiagnostic);
	bool retireDLSS = false;
	bool retireFSR = false;
	bool rebuildDLSS = false;
	bool rebuildFSR = false;

	static bool loggedVendorResetFailure = false;
	static bool loggedVendorResetDeferral = false;
	try {
		retireFSR = includeInactiveVendorReset && !currentMethodFSR && pendingFSRReset.exchange(false, std::memory_order_relaxed);
		if (retireFSR) {
			if (!fidelityFX.PollFSRResourceTeardownReady("inactive FSR resource teardown before vendor runtime reset")) {
				pendingFSRReset.store(true, std::memory_order_release);
				retireFSR = false;
				const bool loggedDeferralDiagnostic = LogVRTransitionDiagnosticOnce(loggedInactiveFSRResetDeferralDiagnostic, [&]() {
					VR_TRANSITION_DIAG_LOG("[VRTransition] Deferred {}runtime reset: inactive FSR resources are still in use", context);
				});
				LogVRTransitionDiagnostics(*this, "vendor runtime reset deferred: inactive FSR not idle", loggedDeferralDiagnostic);
				LogWarnOnceFmt(
					loggedVendorResetDeferral,
					"[Upscaling] Deferred {}runtime reset because inactive FidelityFX resources are still in use",
					context);
				return false;
			}
			UnbindUpscalingResources();
			fidelityFX.DestroyFSRResources(false);
			RequestHistoryReset();
			VR_TRANSITION_DIAG_LOG("[VRTransition] Retired {}inactive FSR resources before runtime reset", context);
			retireFSR = false;
		}

		retireDLSS = includeInactiveVendorReset && !currentMethodDLSS && pendingDLSSReset.exchange(false, std::memory_order_relaxed);
		if (retireDLSS) {
			UnbindUpscalingResources();
			if (!streamline.DestroyDLSSResources()) {
				if (!MarkSubmitStageDeviceLostIfDeviceRemoved("inactive DLSS resource teardown before vendor runtime reset")) {
					pendingDLSSReset.store(true, std::memory_order_release);
					const bool loggedDeferralDiagnostic = LogVRTransitionDiagnosticOnce(loggedInactiveDLSSResetDeferralDiagnostic, [&]() {
						VR_TRANSITION_DIAG_LOG("[VRTransition] Deferred {}runtime reset: inactive DLSS resources are still in use", context);
					});
					LogVRTransitionDiagnostics(*this, "vendor runtime reset deferred: inactive DLSS not idle", loggedDeferralDiagnostic);
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
			VR_TRANSITION_DIAG_LOG("[VRTransition] Retired {}inactive DLSS resources before runtime reset", context);
			retireDLSS = false;
		}

		rebuildDLSS = currentMethodDLSS && pendingDLSSReset.exchange(false, std::memory_order_relaxed);
		rebuildFSR = currentMethodFSR && pendingFSRReset.exchange(false, std::memory_order_relaxed);
		if (!rebuildDLSS && !rebuildFSR) {
			clearVendorResetRetryDiagnostics();
			return true;
		}

		if (rebuildDLSS) {
			UnbindUpscalingResources();
			if (!streamline.DestroyDLSSResources()) {
				if (!MarkSubmitStageDeviceLostIfDeviceRemoved("vendor runtime DLSS resource teardown")) {
					pendingDLSSReset.store(true, std::memory_order_release);
					const bool loggedDeferralDiagnostic = LogVRTransitionDiagnosticOnce(loggedDLSSRebuildDeferralDiagnostic, [&]() {
						VR_TRANSITION_DIAG_LOG("[VRTransition] Deferred {}DLSS rebuild: Streamline resources are still in use", context);
					});
					LogVRTransitionDiagnostics(*this, "vendor runtime reset deferred: DLSS rebuild not idle", loggedDeferralDiagnostic);
					LogWarnOnceFmt(
						loggedVendorResetDeferral,
						"[Upscaling] Deferred rebuild of {}DLSS resources after VR reset because Streamline resources are still in use",
						context);
				}
				rebuildDLSS = false;
				return false;
			}
			RequestHistoryReset();
			VR_TRANSITION_DIAG_LOG("[VRTransition] Rebuilt {}DLSS feature after VR reset", context);
			rebuildDLSS = false;
		} else if (rebuildFSR) {
			if (!fidelityFX.PollFSRResourceTeardownReady("vendor runtime FSR resource teardown")) {
				pendingFSRReset.store(true, std::memory_order_release);
				const bool loggedDeferralDiagnostic = LogVRTransitionDiagnosticOnce(loggedFSRRebuildDeferralDiagnostic, [&]() {
					VR_TRANSITION_DIAG_LOG("[VRTransition] Deferred {}FSR rebuild: FidelityFX resources are still in use", context);
				});
				LogVRTransitionDiagnostics(*this, "vendor runtime reset deferred: FSR rebuild not idle", loggedDeferralDiagnostic);
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
			VR_TRANSITION_DIAG_LOG("[VRTransition] Rebuilt {}FSR resources after VR reset", context);
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

	clearVendorResetRetryDiagnostics();
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

	ApplyDeferredCompositeVRSRuntimeSettings("post-load");

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

void Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
{
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

	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	static bool previousFrameGenMode = false;
	static bool previousFoveatedDispatch = false;
	static bool previousPeripheryTAA = false;
	static bool previousFSRRuntimePathActive = false;
	static bool previousFSRRuntimeFsr4Configured = false;
	static bool previousFSRRuntimeFsr4Active = false;
	static uint32_t previousQualityMode = GetRuntimeQualityMode();
	static uint32_t previousDLSSPreset = std::min<uint>(settings.dlssPreset, kDLSSPresetMaxIndex);
	static uint32_t previousRenderScaleMode = IsRenderScaleModeRequested() ? 1u : 0u;
	static uint32_t previousPerfMode = ClampToggleUInt(settings.perfMode);
	static FoveatedLayoutKey previousFoveatedLayout = makeFoveatedLayoutKey(settings.periphery_taa_enable, settings.periphery_taa_enable && !settings.foveatedPeripheryMaskVisualization);

	bool frameGenModeCurrent = (settings.frameGenerationMode && d3d12SwapChainActive);
	bool frameGenModeChanged = frameGenModeCurrent != previousFrameGenMode;
	bool upscaleModeChanged = (previousUpscaleMode != a_upscalemethod);
	const uint32_t qualityModeCurrent = GetRuntimeQualityMode();
	const uint32_t dlssPresetCurrent = std::min<uint>(settings.dlssPreset, kDLSSPresetMaxIndex);
	const uint32_t renderScaleModeCurrent = IsRenderScaleModeRequested() ? 1u : 0u;
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

	static bool loggedVRResourceChangeRelatchDefer = false;
	static bool loggedVRResourceChangeRelatchSync = false;
	if (!vrRenderScaleRelatchOwnsResourceChange)
		loggedVRResourceChangeRelatchDefer = false;
	if (!vrRenderScaleRelatchCanSyncResourceChange)
		loggedVRResourceChangeRelatchSync = false;

	if (resourceChangeDetected && vrRenderScaleRelatchCanSyncResourceChange) {
		const bool loggedSyncDiagnostic = LogVRTransitionDiagnosticOnce(loggedVRResourceChangeRelatchSync, [&]() {
			VR_TRANSITION_DIAG_LOG(
				"[VRTransition] Synced generic resource-change tracking after render-target relatch handled vendor/D3D resources (method {} -> {}, quality {} -> {}, submitStage {} -> {}, vrRenderScaleLatch {} -> {})",
				magic_enum::enum_name(previousUpscaleMode),
				magic_enum::enum_name(a_upscalemethod),
				previousQualityMode,
				qualityModeCurrent,
				previousRenderScaleMode,
				renderScaleModeCurrent,
				previousPerfMode,
				perfModeCurrent);
		});
		LogVRTransitionDiagnostics(*this, "resource change synced: render-target relatch handled resources", loggedSyncDiagnostic);
		syncResourceChangeTracking();
		vrDLSSSettingsRelatched.store(false, std::memory_order_release);
		vrRenderScaleResourceTrackingSyncPending.store(false, std::memory_order_release);
	} else if (resourceChangeDetected && vrRenderScaleRelatchOwnsResourceChange) {
		const bool loggedDeferDiagnostic = LogVRTransitionDiagnosticOnce(loggedVRResourceChangeRelatchDefer, [&]() {
			VR_TRANSITION_DIAG_LOG(
				"[VRTransition] Deferred generic resource-change handling while render-target relatch owns vendor/D3D resources (method {} -> {}, quality {} -> {}, submitStage {} -> {}, vrRenderScaleLatch {} -> {})",
				magic_enum::enum_name(previousUpscaleMode),
				magic_enum::enum_name(a_upscalemethod),
				previousQualityMode,
				qualityModeCurrent,
				previousRenderScaleMode,
				renderScaleModeCurrent,
				previousPerfMode,
				perfModeCurrent);
		});
		LogVRTransitionDiagnostics(*this, "resource change deferred: render-target relatch owns resources", loggedDeferDiagnostic);
		return;
	} else if (resourceChangeDetected) {
		logger::debug("[Upscaling] Resource change detected - Upscale: {} ({}) -> {} ({}), Quality: {} -> {}, DLSSPreset: {} -> {}, SubmitStage: {} -> {}, VRRenderScaleLatch: {} -> {}, FrameGen: {} -> {} (d3d12Active={}), FSRRuntimePath: {} -> {}",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode), static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod),
			previousQualityMode, qualityModeCurrent, previousDLSSPreset, dlssPresetCurrent, previousRenderScaleMode, renderScaleModeCurrent, previousPerfMode, perfModeCurrent, previousFrameGenMode, frameGenModeCurrent, d3d12SwapChainActive, previousFSRRuntimePathActive, fsrRuntimePathCurrent);
		if (globals::game::isVR && (renderScaleTransitionRelevant || previousUpscaleMode == UpscaleMethod::kDLSS || previousUpscaleMode == UpscaleMethod::kFSR || a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR)) {
			VR_TRANSITION_DIAG_LOG(
				"[VRTransition] Resource change: method {} -> {}, quality {} -> {}, dlssPreset {} -> {}, submitStage {} -> {}, vrRenderScaleLatch {} -> {}, foveatedDispatch {} -> {}, peripheryTAA {} -> {}, fsrRuntimePath {} -> {}, renderScaleRelevant={}",
				magic_enum::enum_name(previousUpscaleMode),
				magic_enum::enum_name(a_upscalemethod),
				previousQualityMode,
				qualityModeCurrent,
				previousDLSSPreset,
				dlssPresetCurrent,
				previousRenderScaleMode,
				renderScaleModeCurrent,
				previousPerfMode,
				perfModeCurrent,
				BoolText(previousFoveatedDispatch),
				BoolText(foveatedDispatchCurrent),
				BoolText(previousPeripheryTAA),
				BoolText(peripheryTAACurrent),
				BoolText(previousFSRRuntimePathActive),
				BoolText(fsrRuntimePathCurrent),
				BoolText(renderScaleTransitionRelevant));
			LogVRTransitionDiagnostics(*this, "resource change detected", true);
		}

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
			VR_TRANSITION_DIAG_LOG("[VRTransition] Creating FSR resources from CheckResources");
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
				VR_TRANSITION_DIAG_LOG("[VRTransition] Destroying FSR resources for quality/runtime resource change");
				fidelityFX.DestroyFSRResources();
				fsrResourcesDestroyedForQuality = true;
				destroyVRQualityResources();
				if (a_upscalemethod == UpscaleMethod::kFSR) {
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
					VR_TRANSITION_DIAG_LOG("[VRTransition] Destroying previous DLSS resources for method change to {}", magic_enum::enum_name(a_upscalemethod));
					if (!streamline.DestroyDLSSResources() &&
						!MarkSubmitStageDeviceLostIfDeviceRemoved("upscale method DLSS resource teardown")) {
						pendingDLSSReset.store(renderScaleTransitionRelevant, std::memory_order_release);
						VR_TRANSITION_DIAG_LOG("[VRTransition] Deferred previous DLSS resource teardown for method change; renderScaleRelevant={}", BoolText(renderScaleTransitionRelevant));
					}
				} else if (previousUpscaleMode == UpscaleMethod::kFSR && !fsrResourcesDestroyedForQuality) {
					if (renderScaleTransitionRelevant &&
						!fidelityFX.PollFSRResourceTeardownReady("upscale method FSR resource teardown")) {
						pendingFSRReset.store(true, std::memory_order_release);
						VR_TRANSITION_DIAG_LOG("[VRTransition] Deferred previous FSR resource teardown for method change; resources are still in use");
					} else {
						VR_TRANSITION_DIAG_LOG("[VRTransition] Destroying previous FSR resources for method change to {} (waitForIdle={})", magic_enum::enum_name(a_upscalemethod), BoolText(!renderScaleTransitionRelevant));
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
			VR_TRANSITION_DIAG_LOG("[VRTransition] Destroying FSR resources for runtime path change");
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
		static bool loggedVRCommonVendorRecreateDefer = false;
		const bool deferForVRRenderScaleRelatch =
			globals::game::isVR &&
			IsVRRenderScaleTransitionSafetyRelevant(*this, a_upscalemethod) &&
			(pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
			 perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) ||
			 HasPendingVRVendorRuntimeReset(*this, a_upscalemethod));

		if (deferForVRRenderScaleRelatch) {
			const bool loggedDeferDiagnostic = LogVRTransitionDiagnosticOnce(loggedVRCommonVendorRecreateDefer, [&]() {
				VR_TRANSITION_DIAG_LOG(
					"[VRTransition] Deferred missing common texture recreation while render-target relatch/reset is pending");
			});
			LogVRTransitionDiagnostics(*this, "deferred missing common texture recreation", loggedDeferDiagnostic);
			return;
		}
		loggedVRCommonVendorRecreateDefer = false;

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
					return;

				requeueMissingCommonRecreate();
				logger::warn(
					"[VRTransition] Deferred missing common texture recreation for {} after failure: {}",
					magic_enum::enum_name(a_upscalemethod),
					e.what());
				LogVRTransitionDiagnostics(*this, "missing common texture recreation deferred after failure", true);
				return;
			}
			throw;
		} catch (...) {
			if (globals::game::isVR && IsVRRenderScaleTransitionSafetyRelevant(*this, a_upscalemethod)) {
				if (MarkSubmitStageDeviceLostIfDeviceRemoved("missing common texture recreation"))
					return;

				requeueMissingCommonRecreate();
				logger::warn(
					"[VRTransition] Deferred missing common texture recreation for {} after an unknown failure",
					magic_enum::enum_name(a_upscalemethod));
				LogVRTransitionDiagnostics(*this, "missing common texture recreation deferred after unknown failure", true);
				return;
			}
			throw;
		}
	}
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

ID3D11PixelShader* Upscaling::GetVRMenuCompositePS()
{
	if (!vrMenuCompositePS) {
		logger::debug("Compiling VRMenuCompositePS.hlsl");
		vrMenuCompositePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/VRMenuCompositePS.hlsl", { { "PSHADER", "" } }, "ps_5_0"));
	}

	return vrMenuCompositePS.get();
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

ID3D11ComputeShader* Upscaling::GetAAVRSVisualizationCS()
{
	if (!aaVrsVisualizationCS) {
		logger::debug("Compiling AAVRSVisualizationCS.hlsl");
		aaVrsVisualizationCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/AAVRSVisualizationCS.hlsl", {}, "cs_5_0"));
	}

	return aaVrsVisualizationCS.get();
}

ID3D11ComputeShader* Upscaling::GetAAVRSRefinementCS()
{
	if (!aaVrsRefinementCS) {
		logger::debug("Compiling AAVRSRefinementCS.hlsl");
		aaVrsRefinementCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/AAVRSRefinementCS.hlsl", {}, "cs_5_0"));
	}

	return aaVrsRefinementCS.get();
}

ID3D11ComputeShader* Upscaling::GetSubmitStageStretchCS()
{
	if (!submitStageStretchCS) {
		logger::debug("Compiling SubmitStageStretchCS.hlsl");
		submitStageStretchCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/SubmitStageStretchCS.hlsl", {}, "cs_5_0"));
	}

	return submitStageStretchCS.get();
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
	       fidelityFX.ShouldUseRuntimeUpscalerForFSR();
}

bool Upscaling::IsFSRRuntimeFsr4PathActive(UpscaleMethod a_upscaleMethod) const
{
	return a_upscaleMethod == UpscaleMethod::kFSR &&
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

bool Upscaling::IsAAVRSEligible(UpscaleMethod a_upscaleMethod) const
{
	if (!globals::game::isVR)
		return false;

	return IsFoveatedVendorDispatchEnabled(a_upscaleMethod);
}

bool Upscaling::IsAAVRSAdapterEligible() const
{
	return fidelityFX.IsNvidiaAdapterDetected();
}

bool Upscaling::BuildAAVRSSettings(AAVRSController::Settings& a_outSettings) const
{
	const auto upscaleMethod = GetRuntimeUpscaleMethod();
	const bool foveatedDispatchEnabled = IsFoveatedVendorDispatchEnabled(upscaleMethod);
	const bool menuPresentationContext = IsVRSceneFeatureMenuPauseContextActive();
	if (!settings.aaVrs || !globals::game::isVR || !foveatedDispatchEnabled || menuPresentationContext)
		return false;

	uint32_t inputWidthPerEye = 0;
	uint32_t inputHeight = 0;
	uint32_t outputWidthPerEye = 0;
	uint32_t outputHeight = 0;
	const auto& regionPlan = runtimeResolutionPlan.foveatedRegion;
	if (regionPlan.IsValid()) {
		inputWidthPerEye = regionPlan.inputWidthPerEye;
		inputHeight = regionPlan.inputHeight;
		outputWidthPerEye = regionPlan.outputWidthPerEye;
		outputHeight = regionPlan.outputHeight;
	} else if (!GetRuntimeFoveatedRegionDimensions(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight)) {
		return false;
	}

	const auto displayStereoLayout = ResolveVRSideBySideStereoLayout(outputWidthPerEye, outputHeight);
	const auto renderStereoLayout = ResolveVRSideBySideStereoLayout(inputWidthPerEye, inputHeight);
	if (!displayStereoLayout.IsValid() || !renderStereoLayout.IsValid())
		return false;
	const uint32_t displayWidth = displayStereoLayout.width;
	const uint32_t displayHeight = displayStereoLayout.height;
	const uint32_t renderWidth = renderStereoLayout.width;
	const uint32_t renderHeight = renderStereoLayout.height;

	const auto activeProfile = GetActiveUpscalingFoveatedProfile();
	if (!activeProfile.available)
		return false;

	const bool useRegionPlan = regionPlan.IsValid();
	const auto activeMaskParams = GetFoveatedMaskProfileParams(settings, activeProfile.usesPeripheryTAAOuterMask);
	const float centerScale = useRegionPlan ? regionPlan.centerScale : activeMaskParams.centerScale;
	const float maskCoverageScale = ResolveActiveFoveatedMaskCoverageScale(activeProfile, regionPlan);
	const float centerHorizontalScale = useRegionPlan ? regionPlan.centerHorizontalScale : activeProfile.centerHorizontalScale;
	const std::array<float2, 2> foveatedCenterOffsets = useRegionPlan ?
		std::array<float2, 2>{ regionPlan.eyes[0].centerOffset, regionPlan.eyes[1].centerOffset } :
		activeProfile.centerOffsets;
	const float vrsMaskScale = ClampFoveatedCenterScale(
		maskCoverageScale + ComputeAAVRSSafetyScalePadding(inputWidthPerEye, inputHeight, centerHorizontalScale));

	AAVRSController::Settings aaVrsSettings{};
	aaVrsSettings.enabled = true;
	aaVrsSettings.stereo = true;
	aaVrsSettings.displayWidth = displayWidth;
	aaVrsSettings.displayHeight = displayHeight;
	aaVrsSettings.renderWidth = renderWidth;
	aaVrsSettings.renderHeight = renderHeight;
	aaVrsSettings.centerScale = centerScale;
	aaVrsSettings.centerHorizontalScale = centerHorizontalScale;
	aaVrsSettings.outerScale = vrsMaskScale;
	aaVrsSettings.coarseOutsideMask = true;
	aaVrsSettings.performanceMode = settings.aaVrsPerformanceMode;
	aaVrsSettings.performanceAnisotropy = std::min<uint32_t>(settings.aaVrsPerformanceAnisotropy, 2u);
	aaVrsSettings.maxRate = settings.aaVrsPerformanceMode ? 1u : std::min<uint32_t>(settings.aaVrsMaxRate, 1u);
	aaVrsSettings.centerOffsets = {
		AAVRSController::CenterOffset{ foveatedCenterOffsets[0].x, foveatedCenterOffsets[0].y },
		AAVRSController::CenterOffset{ foveatedCenterOffsets[1].x, foveatedCenterOffsets[1].y },
	};

	a_outSettings = aaVrsSettings;
	return true;
}

void Upscaling::UpdateAAVRSState()
{
	RefreshRuntimeResolutionState();
	const auto upscaleMethod = GetRuntimeUpscaleMethod();
	const bool requested = settings.aaVrs && globals::game::isVR;
	const auto disableAndReport = [&](const char* reason, bool requestedState, bool preserveRuntimeActiveState = false) {
		aaVrsRuntimeContentAware = false;
		DisableAAVRSState(reason);
		ReportAAVRSTelemetry(requestedState, preserveRuntimeActiveState);
	};

	if (!settings.aaVrs) {
		disableAndReport("Disabled", false);
		return;
	}

	if (!globals::game::isVR) {
		disableAndReport("VR only", false);
		return;
	}

	if (!IsAAVRSEligible(upscaleMethod)) {
		disableAndReport(
			SupportsFoveatedVendorDispatch(upscaleMethod) ? "Foveated inactive" : "Ineligible upscaling method",
			requested);
		return;
	}

	if (!IsAAVRSAdapterEligible()) {
		disableAndReport("NVIDIA Variable Rate Shading (VRS) unavailable", requested);
		return;
	}

	if (IsVRSceneFeatureMenuPauseContextActive()) {
		// UpdateAAVRSState runs before world rendering, so lastWorldRenderFrame
		// cannot be used here without blocking valid scene frames.
		disableAndReport("Game/CS menu context active", requested, true);
		return;
	}

	auto* device = globals::d3d::device;
	auto* context = globals::d3d::context;
	if (!device || !context) {
		disableAndReport("Missing runtime state", requested);
		return;
	}

	AAVRSController::Settings aaVrsSettings{};
	if (!BuildAAVRSSettings(aaVrsSettings)) {
		disableAndReport("Missing runtime state", requested);
		return;
	}

	const bool updated = aaVrsController.Update(aaVrsSettings, device, context);
	aaVrsRuntimeContentAware = updated && ApplyAAVRSContentAwareRefinement(aaVrsSettings);
	ReportAAVRSTelemetry(requested);
}

bool Upscaling::ApplyAAVRSContentAwareRefinement(const AAVRSController::Settings& a_settings)
{
	if (!settings.aaVrsContentAware || !aaVrsController.IsActive())
		return false;

	auto* context = globals::d3d::context;
	auto* renderer = globals::game::renderer;
	auto* shader = GetAAVRSRefinementCS();
	if (!context || !renderer || !shader || !aaVrsRefinementCB)
		return false;

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& motionVectors = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto* depthSRV = Util::GetCurrentSceneDepthSRV(false);
	if (!main.SRV || !motionVectors.SRV || !depthSRV)
		return false;

	const auto status = aaVrsController.GetStatus();
	if (!status.maskWidth || !status.maskHeight)
		return false;

	AAVRSRefinementCB cbData{};
	cbData.renderInfo = {
		static_cast<float>(a_settings.renderWidth),
		static_cast<float>(a_settings.renderHeight),
		a_settings.renderWidth > 0 ? 1.0f / static_cast<float>(a_settings.renderWidth) : 0.0f,
		a_settings.renderHeight > 0 ? 1.0f / static_cast<float>(a_settings.renderHeight) : 0.0f
	};
	cbData.rateInfo = {
		static_cast<float>(status.maskWidth),
		static_cast<float>(status.maskHeight),
		static_cast<float>(AAVRSController::kTileWidth),
		static_cast<float>(AAVRSController::kTileHeight)
	};
	cbData.thresholds = {
		kAAVRSRefinementLumaRangeThreshold,
		kAAVRSRefinementBrightLumaThreshold,
		kAAVRSRefinementMotionPixelsThreshold,
		kAAVRSRefinementDepthRangeThreshold
	};
	aaVrsRefinementCB->Update(cbData);

	ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	ID3D11DepthStencilView* dsv = nullptr;
	context->OMGetRenderTargets(static_cast<UINT>(std::size(rtvs)), rtvs, &dsv);

	ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	context->OMSetRenderTargets(static_cast<UINT>(std::size(nullRTVs)), nullRTVs, nullptr);

	ID3D11Buffer* cb = aaVrsRefinementCB->CB();
	auto state = globals::state;
	if (state && state->frameAnnotations)
		state->BeginPerfEvent("VRS Content-Aware Refinement");
	const bool refined = aaVrsController.RefineContentAware(
		context,
		shader,
		cb,
		main.SRV,
		motionVectors.SRV,
		depthSRV);
	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	context->OMSetRenderTargets(static_cast<UINT>(std::size(rtvs)), rtvs, dsv);
	for (auto* rtv : rtvs) {
		if (rtv)
			rtv->Release();
	}
	if (dsv)
		dsv->Release();

	return refined;
}

void Upscaling::ApplyAAVRSVisualization()
{
	if (!settings.aaVrsVisualization || !aaVrsController.IsActive())
		return;

	RefreshRuntimeResolutionState();
	AAVRSController::Settings aaVrsSettings{};
	if (!BuildAAVRSSettings(aaVrsSettings))
		return;

	auto* context = globals::d3d::context;
	auto* renderer = globals::game::renderer;
	auto* shader = GetAAVRSVisualizationCS();
	auto* rateSRV = aaVrsController.GetRateImageSRV();
	if (!context || !renderer || !shader || !aaVrsVisualizationCB || !rateSRV)
		return;

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	if (!main.UAV)
		return;

	AAVRSVisualizationCB cbData{};
	cbData.renderInfo = {
		static_cast<float>(aaVrsSettings.renderWidth),
		static_cast<float>(aaVrsSettings.renderHeight),
		aaVrsSettings.renderWidth > 0 ? 1.0f / static_cast<float>(aaVrsSettings.renderWidth) : 0.0f,
		aaVrsSettings.renderHeight > 0 ? 1.0f / static_cast<float>(aaVrsSettings.renderHeight) : 0.0f
	};
	cbData.displayInfo = {
		static_cast<float>(aaVrsSettings.displayWidth),
		static_cast<float>(aaVrsSettings.displayHeight),
		aaVrsSettings.stereo ? 2.0f : 1.0f,
		aaVrsSettings.coarseOutsideMask ? 1.0f : 0.0f
	};
	cbData.maskInfo = {
		aaVrsSettings.centerScale,
		aaVrsSettings.outerScale,
		aaVrsSettings.centerHorizontalScale,
		0.0f
	};
	cbData.centerOffsets = {
		aaVrsSettings.centerOffsets[0].x,
		aaVrsSettings.centerOffsets[0].y,
		aaVrsSettings.centerOffsets[1].x,
		aaVrsSettings.centerOffsets[1].y
	};
	cbData.coarseColor = { 1.00f, 0.00f, 1.00f, 1.0f };
	cbData.centerColor = { 0.02f, 0.02f, 0.025f, 1.0f };
	cbData.pad = {
		static_cast<float>(AAVRSController::kTileWidth),
		static_cast<float>(AAVRSController::kTileHeight),
		aaVrsSettings.performanceMode ? 1.0f : 0.0f,
		static_cast<float>(aaVrsSettings.performanceAnisotropy)
	};
	aaVrsVisualizationCB->Update(cbData);

	ID3D11Buffer* cb = aaVrsVisualizationCB->CB();
	ID3D11ShaderResourceView* srvs[1] = { rateSRV };
	ID3D11UnorderedAccessView* uavs[1] = { main.UAV };
	aaVrsController.UnbindShadingRateResource(context);
	context->CSSetShader(shader, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShaderResources(0, 1, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	auto state = globals::state;
	if (state && state->frameAnnotations)
		state->BeginPerfEvent(kVrsMaskVisualizationName);
	context->Dispatch((aaVrsSettings.renderWidth + 7u) >> 3, (aaVrsSettings.renderHeight + 7u) >> 3, 1);
	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	ID3D11Buffer* nullCB[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	context->CSSetShaderResources(0, 1, nullSRV);
	context->CSSetConstantBuffers(0, 1, nullCB);
	context->CSSetShader(nullptr, nullptr, 0);
}

bool Upscaling::ShouldForceFullRateForAAVRSPass(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest)
{
	if (!settings.aaVrs || !globals::game::isVR || !aaVrsController.IsActive())
		return false;
	if (!GuardAAVRSRenderTarget())
		return false;
	if (!settings.aaVrsPassAware)
		return false;

	(void)a_technique;

	auto record = [this](AAVRSPassPolicyReason a_reason) {
		const auto index = static_cast<size_t>(a_reason);
		if (settings.aaVrsPassTelemetry && index < aaVrsPassPolicyCounters.size())
			aaVrsPassPolicyCounters[index].fetch_add(1, std::memory_order_relaxed);
		return true;
	};

	auto* shader = a_pass ? a_pass->shader : nullptr;
	// Handle water before generic full-rate guards so the dedicated water toggle stays authoritative.
	if (shader && shader->shaderType.get() == RE::BSShader::Type::Water) {
		if (settings.aaVrsProtectWater)
			return record(AAVRSPassPolicyReason::WaterShader);
		return settings.aaVrsSafeOpaqueOnly ? record(AAVRSPassPolicyReason::SafeOpaqueOnly) : false;
	}

	if (a_alphaTest)
		return record(AAVRSPassPolicyReason::AlphaTest);

	auto* shaderProperty = a_pass ? a_pass->shaderProperty : nullptr;
	if (shaderProperty) {
		using ShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;
		if (shaderProperty->alpha > 0.0f && shaderProperty->alpha < 0.999f)
			return record(AAVRSPassPolicyReason::ShaderPropertyAlpha);
		if (shaderProperty->flags.any(ShaderPropertyFlag::kVertexAlpha) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kScreendoorAlphaFade) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kPremultAlpha) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kRefraction) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kTempRefraction) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kSoftEffect)) {
			return record(AAVRSPassPolicyReason::ShaderPropertyAlpha);
		}
		if (shaderProperty->flags.any(ShaderPropertyFlag::kDecal) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kDynamicDecal)) {
			return record(AAVRSPassPolicyReason::ShaderPropertyDecal);
		}
		if (shaderProperty->flags.any(ShaderPropertyFlag::kGlowMap) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kOwnEmit) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kExternalEmittance)) {
			return record(AAVRSPassPolicyReason::ShaderPropertyEmissive);
		}
		if (shaderProperty->flags.any(ShaderPropertyFlag::kTreeAnim) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kLODObjects) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kHDLODObjects) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kHairTint) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kBillboard) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kCloudLOD) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kEyeReflect) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kProjectedUV) ||
			shaderProperty->flags.any(ShaderPropertyFlag::kParallaxOcclusion)) {
			return record(AAVRSPassPolicyReason::ShaderPropertyHighFrequency);
		}
	}

	if (!shader)
		return settings.aaVrsSafeOpaqueOnly ? record(AAVRSPassPolicyReason::SafeOpaqueOnly) : false;

	const auto shaderType = shader->shaderType.get();
	switch (shaderType) {
	case RE::BSShader::Type::Effect:
		return record(AAVRSPassPolicyReason::EffectShader);
	case RE::BSShader::Type::Particle:
		return record(AAVRSPassPolicyReason::ParticleShader);
	case RE::BSShader::Type::Grass:
		return record(AAVRSPassPolicyReason::GrassShader);
	case RE::BSShader::Type::DistantTree:
		return record(AAVRSPassPolicyReason::DistantTreeShader);
	case RE::BSShader::Type::BloodSplatter:
		return record(AAVRSPassPolicyReason::BloodSplatterShader);
	case RE::BSShader::Type::Sky:
		return record(AAVRSPassPolicyReason::SkyShader);
	case RE::BSShader::Type::Lighting:
	{
		uint32_t lightingDescriptor = 0;
		if (TryDecodeLightingDescriptor(a_pass, lightingDescriptor)) {
			const uint64_t lightingFlags = lightingDescriptor & 0x00FFFFFFu;
			if (HasShaderFlag(lightingFlags, SIE::ShaderCache::LightingShaderFlags::DoAlphaTest) ||
				HasShaderFlag(lightingFlags, SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask)) {
				return record(AAVRSPassPolicyReason::LightingDescriptor);
			}

			const auto lightingTechnique = static_cast<SIE::ShaderCache::LightingShaderTechniques>((lightingDescriptor >> 24) & 0x3Fu);
			switch (lightingTechnique) {
			case SIE::ShaderCache::LightingShaderTechniques::Glowmap:
			case SIE::ShaderCache::LightingShaderTechniques::Hair:
			case SIE::ShaderCache::LightingShaderTechniques::TreeAnim:
			case SIE::ShaderCache::LightingShaderTechniques::LODObjects:
			case SIE::ShaderCache::LightingShaderTechniques::MultiIndexSparkle:
			case SIE::ShaderCache::LightingShaderTechniques::LODObjectHD:
			case SIE::ShaderCache::LightingShaderTechniques::Eye:
			case SIE::ShaderCache::LightingShaderTechniques::Cloud:
				return record(AAVRSPassPolicyReason::LightingTechnique);
			default:
				break;
			}

			if (settings.aaVrsSafeOpaqueOnly) {
				switch (lightingTechnique) {
				case SIE::ShaderCache::LightingShaderTechniques::None:
				case SIE::ShaderCache::LightingShaderTechniques::Envmap:
				case SIE::ShaderCache::LightingShaderTechniques::Parallax:
				case SIE::ShaderCache::LightingShaderTechniques::Facegen:
				case SIE::ShaderCache::LightingShaderTechniques::FacegenRGBTint:
				case SIE::ShaderCache::LightingShaderTechniques::MTLand:
				case SIE::ShaderCache::LightingShaderTechniques::LODLand:
				case SIE::ShaderCache::LightingShaderTechniques::ParallaxOcc:
				case SIE::ShaderCache::LightingShaderTechniques::MTLandLODBlend:
					break;
				default:
					return record(AAVRSPassPolicyReason::SafeOpaqueOnly);
				}
			}
		} else if (settings.aaVrsSafeOpaqueOnly) {
			return record(AAVRSPassPolicyReason::SafeOpaqueOnly);
		}
		break;
	}
	case RE::BSShader::Type::Utility:
	{
		const uint64_t utilityDescriptor = a_pass ? a_pass->passEnum : 0u;
		if (HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::AlphaTest) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::GrayscaleToAlpha) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::AdditionalAlphaMask) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::DepthWriteDecals) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderDepth) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderNormal) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderNormalFalloff) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderNormalClamp) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderNormalClear) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderShadowmap) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderShadowmapClamped) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderShadowmapPb) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderShadowmask) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderShadowmaskSpot) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderShadowmaskPb) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::RenderShadowmaskDpb) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::TreeAnim) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::LodObject) ||
			HasShaderFlag(utilityDescriptor, SIE::ShaderCache::UtilityShaderFlags::OpaqueEffect)) {
			return record(AAVRSPassPolicyReason::UtilityDescriptor);
		}
		break;
	}
	default:
		break;
	}

	if (settings.aaVrsSafeOpaqueOnly && shaderType != RE::BSShader::Type::Lighting)
		return record(AAVRSPassPolicyReason::SafeOpaqueOnly);

	return false;
}

bool Upscaling::ShouldForceFullRateForAAVRSPhase(AAVRSPassPolicyReason a_reason)
{
	if (!settings.aaVrs || !globals::game::isVR || !aaVrsController.IsActive())
		return false;
	if (!GuardAAVRSRenderTarget())
		return false;
	if (!settings.aaVrsPassAware)
		return false;

	const auto index = static_cast<size_t>(a_reason);
	if (settings.aaVrsPassTelemetry && index < aaVrsPassPolicyCounters.size())
		aaVrsPassPolicyCounters[index].fetch_add(1, std::memory_order_relaxed);
	return true;
}

bool Upscaling::GuardAAVRSRenderTarget()
{
	if (!aaVrsController.IsActive())
		return true;

	return aaVrsController.GuardActiveRenderTarget(globals::d3d::context);
}

void Upscaling::BeginAAVRSFullRateOverride()
{
	if (!globals::game::isVR)
		return;

	aaVrsController.BeginFullRateOverride(globals::d3d::context);
}

void Upscaling::EndAAVRSFullRateOverride()
{
	if (!globals::game::isVR)
		return;

	aaVrsController.EndFullRateOverride(globals::d3d::context);
}

void Upscaling::DisableAAVRSState(const char* a_reason)
{
	aaVrsRuntimeContentAware = false;
	aaVrsController.Disable(globals::d3d::context, a_reason);
}

void Upscaling::ReportAAVRSTelemetry(bool a_requested, bool a_preserveRuntimeActiveState)
{
	const auto status = aaVrsController.GetStatus();
	if (status.active) {
		const bool dimensionsChanged =
			aaVrsTelemetryMaskWidth != status.maskWidth ||
			aaVrsTelemetryMaskHeight != status.maskHeight ||
			aaVrsTelemetryRenderWidth != status.renderWidth ||
			aaVrsTelemetryRenderHeight != status.renderHeight;
		const bool modeChanged =
			aaVrsTelemetryMaxRate != status.maxRate ||
			aaVrsTelemetryPerformanceMode != status.performanceMode ||
			aaVrsTelemetryPerformanceAnisotropy != status.performanceAnisotropy ||
			aaVrsTelemetryContentAware != aaVrsRuntimeContentAware;
		if (!aaVrsTelemetryLoggedActive || !aaVrsRuntimeActive || dimensionsChanged || modeChanged) {
			logger::info(
				"[Upscaling] Foveated Variable Rate Shading (VRS) active: render {}x{}, mask {}x{}, mode={}, orientation={}, contentAware={}, maxRate={}x{}",
				status.renderWidth,
				status.renderHeight,
				status.maskWidth,
				status.maskHeight,
				status.performanceMode ? "performance" : "mask",
				status.performanceMode ? GetAAVRSPerformanceAnisotropyName(status.performanceAnisotropy) : "n/a",
				aaVrsRuntimeContentAware,
				status.maxRate == 0 ? 2 : 4,
				status.maxRate == 0 ? 2 : 4);
		}

		aaVrsRuntimeActive = true;
		aaVrsTelemetryLoggedActive = true;
		aaVrsTelemetryMaskWidth = status.maskWidth;
		aaVrsTelemetryMaskHeight = status.maskHeight;
		aaVrsTelemetryRenderWidth = status.renderWidth;
		aaVrsTelemetryRenderHeight = status.renderHeight;
		aaVrsTelemetryMaxRate = status.maxRate;
		aaVrsTelemetryPerformanceMode = status.performanceMode;
		aaVrsTelemetryPerformanceAnisotropy = status.performanceAnisotropy;
		aaVrsTelemetryContentAware = aaVrsRuntimeContentAware;
		return;
	}

	if (!a_preserveRuntimeActiveState)
		aaVrsRuntimeActive = false;

	if (!a_requested) {
		aaVrsTelemetryInactiveReason.clear();
		return;
	}

	const char* reason = status.lastDisableReason && status.lastDisableReason[0] ?
		status.lastDisableReason :
		aaVrsController.GetLastDisableReason();
	std::string reasonText = reason && reason[0] ? reason : "Inactive";
	if (aaVrsTelemetryInactiveReason != reasonText) {
		logger::info("[Upscaling] Foveated Variable Rate Shading (VRS) requested but inactive: {}", reasonText);
		aaVrsTelemetryInactiveReason = reasonText;
	}
}

void Upscaling::ResetAAVRSTelemetry()
{
	aaVrsRuntimeActive = false;
	aaVrsRuntimeContentAware = false;
	aaVrsTelemetryLoggedActive = false;
	aaVrsTelemetryMaskWidth = 0;
	aaVrsTelemetryMaskHeight = 0;
	aaVrsTelemetryRenderWidth = 0;
	aaVrsTelemetryRenderHeight = 0;
	aaVrsTelemetryMaxRate = 0;
	aaVrsTelemetryPerformanceMode = false;
	aaVrsTelemetryPerformanceAnisotropy = 0;
	aaVrsTelemetryContentAware = false;
	aaVrsTelemetryInactiveReason.clear();
	ResetAAVRSPassTelemetry();
}

void Upscaling::ResetAAVRSPassTelemetry()
{
	for (auto& counter : aaVrsPassPolicyCounters) {
		counter.store(0, std::memory_order_relaxed);
	}
}

void Upscaling::DrawAAVRSPassTelemetry()
{
	if (!ImGui::BeginTable("##AAVRSPassTelemetry", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		return;

	ImGui::TableSetupColumn("Reason");
	ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableHeadersRow();
	for (size_t i = 1; i < static_cast<size_t>(AAVRSPassPolicyReason::Count); ++i) {
		const auto count = aaVrsPassPolicyCounters[i].load(std::memory_order_relaxed);
		if (count == 0)
			continue;

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(GetAAVRSPassPolicyReasonName(static_cast<AAVRSPassPolicyReason>(i)));
		ImGui::TableNextColumn();
		ImGui::Text("%u", count);
	}

	ImGui::EndTable();
}

void Upscaling::SuspendAAVRS()
{
	if (!globals::game::isVR)
		return;

	aaVrsController.Suspend(globals::d3d::context);
}

void Upscaling::ResumeAAVRS()
{
	if (!globals::game::isVR)
		return;

	aaVrsController.Resume(globals::d3d::context);
}

Upscaling::ScopedAAVRSFullRateOverride::ScopedAAVRSFullRateOverride(Upscaling& a_upscaling, bool a_active) :
	upscaling(a_active ? &a_upscaling : nullptr)
{
	if (upscaling)
		upscaling->BeginAAVRSFullRateOverride();
}

Upscaling::ScopedAAVRSFullRateOverride::~ScopedAAVRSFullRateOverride()
{
	if (upscaling)
		upscaling->EndAAVRSFullRateOverride();
}

Upscaling::ScopedAAVRSSuspension::ScopedAAVRSSuspension(Upscaling& a_upscaling, bool a_active) :
	upscaling(a_active ? &a_upscaling : nullptr)
{
	if (upscaling)
		upscaling->SuspendAAVRS();
}

Upscaling::ScopedAAVRSSuspension::~ScopedAAVRSSuspension()
{
	if (upscaling)
		upscaling->ResumeAAVRS();
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
	if (!PreparePerEyeInputs(colorTexture, depthTexture, motionVectors, reactiveMask, transparencyMask, false, true))
		return false;
	if (usePeripheryTAA && !EnsurePeripheryTAAResources(outputWidthPerEye, outputHeight, colorTexture))
		return false;

	const bool resetPeripheryTAA = usePeripheryTAA && (ShouldResetHistoryThisFrame() || !peripheryTAAHistoryValid);
	const uint32_t peripheryTAAReadIndex = peripheryTAAHistoryReadIndex;
	const uint32_t peripheryTAAWriteIndex = 1u - peripheryTAAReadIndex;

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
				return false;
			}
		} catch (const std::exception& e) {
			UnbindUpscalingResources();
			LogWarnOnce(
				loggedFoveatedDispatchFailure,
				"[Upscaling] Foveated dispatch threw; skipping foveated vendor dispatch",
				e);
			return false;
		} catch (...) {
			UnbindUpscalingResources();
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
		if (!vrIntermediateDepth[eyeIndex] || !vrIntermediateDepth[eyeIndex]->resource || !vrIntermediateDepth[eyeIndex]->srv ||
			!vrIntermediateMotionVectors[eyeIndex] || !vrIntermediateMotionVectors[eyeIndex]->resource ||
			!vrIntermediateReactiveMask[eyeIndex] || !vrIntermediateReactiveMask[eyeIndex]->resource ||
			!vrIntermediateTransparencyMask[eyeIndex] || !vrIntermediateTransparencyMask[eyeIndex]->resource) {
			return false;
		}
		if (a_upscaleMethod == UpscaleMethod::kFSR &&
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
	if (settings.sharpnessDLSS <= 0.0f)
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
		if (!rcas.ApplySharpen(sharpenInput.srv.get(), colorOutput->uav.get(), GetDLSSRCASSharpness(settings.sharpnessDLSS), dispatchWidth, dispatchHeight)) {
			LogWarnOnceFmt(
				loggedSharpenerFailure[eyeIndex],
				"[Upscaling] Submit-stage DLSS sharpening skipped for eye {} because RCAS dispatch failed.",
				eyeIndex);
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
			LogVRHMDMaskClearDispatch(
				*this,
				i == 0 ? "PerEyeInputLeft" : "PerEyeInputRight",
				eyeWidthIn,
				eyeHeightIn,
				eyeWidthIn,
				eyeHeightIn,
				depthOffset,
				0,
				0,
				0);

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

bool Upscaling::EncodeSubmitStageVRInputs(ID3D11Resource* colorSource, ID3D11Resource* motionVectors, ID3D11Resource* depthSource,
	uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight)
{
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
		if (!vrIntermediateDepth[eye] || !vrIntermediateDepth[eye]->resource ||
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
		const uint32_t dispatchX = (inputWidthPerEye + 7u) >> 3;
		const uint32_t dispatchY = (inputHeight + 7u) >> 3;

		for (uint32_t eye = 0; eye < 2; ++eye) {
			const auto& sourceEyeRegion = inputStereoLayout.eyes[eye];
			UpscalingDataCB upscalingData{};
			upscalingData.dispatchDim = { static_cast<float>(inputWidthPerEye), static_cast<float>(inputHeight) };
			upscalingData.trueSamplingDim = renderSize;
			upscalingData.invTrueSamplingDim = { renderSize.x > 0.0f ? 1.0f / renderSize.x : 0.0f, renderSize.y > 0.0f ? 1.0f / renderSize.y : 0.0f };
			upscalingData.seamCenterX = renderSize.x * 0.5f;
			upscalingData.seamHalfWidthPx = 2.0f;
			upscalingData.maskDepthThreshold = 1e-6f;
			upscalingData.vrSeamHardening = 1.0f;
			upscalingData.sourceOffset = { static_cast<float>(sourceEyeRegion.minX), 0.0f };
			upscalingData.outputOffset = { 0.0f, 0.0f };
			upscalingDataCB->Update(upscalingData);

			ID3D11UnorderedAccessView* uavs[4] = {
				vrIntermediateReactiveMask[eye]->uav.get(),
				vrIntermediateTransparencyMask[eye]->uav.get(),
				vrIntermediateMotionVectors[eye]->uav.get(),
				upscaleMethod == UpscaleMethod::kFSR ? vrIntermediateLinearDepth[eye]->uav.get() : nullptr
			};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
			context->Dispatch(dispatchX, dispatchY, 1);

			D3D11_BOX srcBox{ sourceEyeRegion.minX, sourceEyeRegion.minY, 0, sourceEyeRegion.MaxX(), sourceEyeRegion.MaxY(), 1 };
			context->CopySubresourceRegion(vrIntermediateDepth[eye]->resource.get(), 0, 0, 0, 0, depthSource, 0, &srcBox);
		}

		ID3D11ShaderResourceView* nullSRV[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullSRV), nullSRV);

		ID3D11UnorderedAccessView* nullUAV[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAV), nullUAV, nullptr);

		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);
		context->CSSetShader(nullptr, nullptr, 0);
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

bool Upscaling::CaptureKnownGameMenuSceneBeforeMenuDraw()
{
	vrKnownMenuSceneBeforeCompositeFrame = 0;
	vrKnownMenuBackgroundCompositeFrame = 0;

	if (!IsVRKnownGameMenuLayerSeparationContextActive(*this))
		return false;

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	if (!state || !renderer || !context)
		return false;

	auto& totalTarget = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTOTAL];
	if (!totalTarget.texture)
		return false;

	D3D11_TEXTURE2D_DESC totalDesc{};
	if (!TryGetTexture2DDesc(totalTarget.texture, totalDesc) ||
		totalDesc.SampleDesc.Count != 1 ||
		totalDesc.Width == 0 ||
		totalDesc.Height == 0) {
		return false;
	}

	if (!EnsureFoveatedTexture(
			vrKnownMenuSceneBeforeComposite,
			totalTarget.texture,
			totalDesc.Width,
			totalDesc.Height,
			false,
			false,
			false,
			false,
			"VRKnownMenuSceneBeforeComposite") ||
		!vrKnownMenuSceneBeforeComposite ||
		!vrKnownMenuSceneBeforeComposite->resource) {
		return false;
	}

	context->CopyResource(vrKnownMenuSceneBeforeComposite->resource.get(), totalTarget.texture);
	if (MarkSubmitStageDeviceLostIfDeviceRemoved("known-menu scene snapshot")) {
		vrKnownMenuSceneBeforeCompositeFrame = 0;
		return false;
	}

	vrKnownMenuSceneBeforeCompositeFrame = state->frameCount;
	return true;
}

bool Upscaling::CaptureKnownGameMenuBackgroundAfterMenuDraw()
{
	vrKnownMenuBackgroundCompositeFrame = 0;

	if (!IsVRKnownGameMenuLayerSeparationContextActive(*this))
		return false;

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	if (!state || !renderer || !context)
		return false;
	if (vrKnownMenuSceneBeforeCompositeFrame != state->frameCount ||
		!vrKnownMenuSceneBeforeComposite ||
		!vrKnownMenuSceneBeforeComposite->resource) {
		return false;
	}

	auto& menuBackground = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMENUBG];
	if (!menuBackground.texture || !menuBackground.SRV)
		return false;

	D3D11_TEXTURE2D_DESC menuDesc{};
	if (!TryGetTexture2DDesc(menuBackground.texture, menuDesc) ||
		menuDesc.SampleDesc.Count != 1 ||
		menuDesc.Width == 0 ||
		menuDesc.Height == 0 ||
		menuDesc.Width != vrKnownMenuSceneBeforeComposite->desc.Width ||
		menuDesc.Height != vrKnownMenuSceneBeforeComposite->desc.Height ||
		menuDesc.Format != vrKnownMenuSceneBeforeComposite->desc.Format) {
		return false;
	}

	if (!EnsureFoveatedTexture(
			vrKnownMenuBackgroundComposite,
			menuBackground.texture,
			menuDesc.Width,
			menuDesc.Height,
			false,
			true,
			false,
			false,
			"VRKnownMenuBackgroundComposite") ||
		!vrKnownMenuBackgroundComposite ||
		!vrKnownMenuBackgroundComposite->resource ||
		!vrKnownMenuBackgroundComposite->srv) {
		return false;
	}

	context->CopyResource(vrKnownMenuBackgroundComposite->resource.get(), menuBackground.texture);
	if (MarkSubmitStageDeviceLostIfDeviceRemoved("known-menu background snapshot")) {
		vrKnownMenuBackgroundCompositeFrame = 0;
		return false;
	}

	vrKnownMenuBackgroundCompositeFrame = state->frameCount;
	return true;
}

bool Upscaling::HasKnownGameMenuSceneSnapshotForSubmit(uint32_t a_frame, ID3D11Texture2D* a_submitSource, const D3D11_TEXTURE2D_DESC& a_submitSourceDesc) const
{
	if (!IsVRKnownGameMenuLayerSeparationContextActive(*this))
		return false;
	if (!a_submitSource ||
		!vrKnownMenuSceneBeforeComposite ||
		!vrKnownMenuSceneBeforeComposite->resource ||
		!vrKnownMenuBackgroundComposite ||
		!vrKnownMenuBackgroundComposite->srv) {
		return false;
	}
	if (vrKnownMenuSceneBeforeCompositeFrame != a_frame ||
		vrKnownMenuBackgroundCompositeFrame != a_frame) {
		return false;
	}

	auto renderer = globals::game::renderer;
	if (!renderer)
		return false;
	auto& totalTarget = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTOTAL];
	if (totalTarget.texture != a_submitSource)
		return false;

	return a_submitSourceDesc.SampleDesc.Count == 1 &&
	       a_submitSourceDesc.Width == vrKnownMenuSceneBeforeComposite->desc.Width &&
	       a_submitSourceDesc.Height == vrKnownMenuSceneBeforeComposite->desc.Height &&
	       a_submitSourceDesc.Format == vrKnownMenuSceneBeforeComposite->desc.Format &&
	       a_submitSourceDesc.Width == vrKnownMenuBackgroundComposite->desc.Width &&
	       a_submitSourceDesc.Height == vrKnownMenuBackgroundComposite->desc.Height &&
	       a_submitSourceDesc.Format == vrKnownMenuBackgroundComposite->desc.Format;
}

bool Upscaling::CompositeKnownGameMenuAfterSubmitStageUpscale(uint32_t eyeIndex, uint32_t eyeWidthOut, uint32_t eyeHeightOut)
{
	if (!globals::game::isVR || eyeIndex >= 2 || !eyeWidthOut || !eyeHeightOut)
		return false;
	if (!IsVRKnownGameMenuLayerSeparationContextActive(*this))
		return false;
	if (IsSubmitStageDeviceLost())
		return false;

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !context || !deferred || !deferred->linearSampler)
		return false;
	if (vrKnownMenuBackgroundCompositeFrame != state->frameCount ||
		!vrKnownMenuBackgroundComposite ||
		!vrKnownMenuBackgroundComposite->srv ||
		!vrIntermediateColorOut[eyeIndex] ||
		!vrIntermediateColorOut[eyeIndex]->rtv) {
		return false;
	}

	const auto& sourceDesc = vrKnownMenuBackgroundComposite->desc;
	if (sourceDesc.SampleDesc.Count != 1 || sourceDesc.Width < 2 || sourceDesc.Height == 0)
		return false;

	auto* vertexShader = GetUpscaleVS();
	auto* pixelShader = GetVRMenuCompositePS();
	if (!vertexShader || !pixelShader || !vrMenuCompositeCB || !vrMenuCompositeBlendState || !upscaleRasterizerState)
		return false;

	VRMenuCompositeCB data{};
	data.sourceScale = { 0.5f, 1.0f };
	data.sourceOffset = { eyeIndex == 1 ? 0.5f : 0.0f, 0.0f };
	vrMenuCompositeCB->Update(data);

	ScopedVRMenuCompositeD3D11State scopedState(context);

	D3D11_VIEWPORT viewport{};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(eyeWidthOut);
	viewport.Height = static_cast<float>(eyeHeightOut);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(vertexShader, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->GSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(pixelShader, nullptr, 0);

	auto* sourceSRV = vrKnownMenuBackgroundComposite->srv.get();
	auto* sampler = deferred->linearSampler;
	auto* constantBuffer = vrMenuCompositeCB->CB();
	context->PSSetShaderResources(0, 1, &sourceSRV);
	context->PSSetSamplers(0, 1, &sampler);
	context->PSSetConstantBuffers(0, 1, &constantBuffer);

	context->RSSetState(upscaleRasterizerState.get());
	context->RSSetViewports(1, &viewport);
	context->OMSetBlendState(vrMenuCompositeBlendState.get(), nullptr, 0xffffffff);
	context->OMSetDepthStencilState(nullptr, 0);
	ID3D11RenderTargetView* outputRTV = vrIntermediateColorOut[eyeIndex]->rtv.get();
	context->OMSetRenderTargets(1, &outputRTV, nullptr);
	context->Draw(3, 0);
	if (MarkSubmitStageDeviceLostIfDeviceRemoved("known-menu final composite"))
		return false;

	static std::array<bool, 2> loggedMenuComposite{};
	if (!loggedMenuComposite[eyeIndex]) {
		logger::debug(
			"[VRMenuComposite] composited separated kMENUBG after submit-stage upscale eye={} source={}x{} finalEye={}x{}",
			eyeIndex,
			sourceDesc.Width,
			sourceDesc.Height,
			eyeWidthOut,
			eyeHeightOut);
		loggedMenuComposite[eyeIndex] = true;
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

	switch (a_phase) {
	case HMDMaskClearPhase::PerEyeInput:
	case HMDMaskClearPhase::PerEyeOutput:
	case HMDMaskClearPhase::SubmitStageOutput:
	case HMDMaskClearPhase::SubmitStageFoveatedOutput:
		return true;
	default:
		return false;
	}
}

void Upscaling::ClearHMDMask(ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
	uint32_t depthWidth, uint32_t depthHeight, uint32_t colorWidth, uint32_t colorHeight, uint32_t depthOffsetX, uint32_t colorOffsetX, uint32_t depthOffsetY, uint32_t colorOffsetY,
	const char* phaseName)
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
		LogVRHMDMaskClearDispatch(
			*this,
			phaseName,
			depthWidth,
			depthHeight,
			colorWidth,
			colorHeight,
			depthOffsetX,
			colorOffsetX,
			depthOffsetY,
			colorOffsetY);

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

	const char* phaseName = "Unknown";
	switch (a_phase) {
	case HMDMaskClearPhase::PerEyeInput:
		phaseName = "PerEyeInput";
		break;
	case HMDMaskClearPhase::PerEyeOutput:
		phaseName = "PerEyeOutput";
		break;
	case HMDMaskClearPhase::SubmitStageOutput:
		phaseName = "SubmitStageOutput";
		break;
	case HMDMaskClearPhase::SubmitStageFoveatedOutput:
		phaseName = "SubmitStageFoveatedOutput";
		break;
	}
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
		colorOffsetY,
		phaseName);
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
	ApplyPendingVRFpsStabilizerLoadSync();
	const auto requestedUpscaleMethod = GetConfiguredUpscaleMethodForTransition();
	ApplyPendingVRUpscalingTransition(requestedUpscaleMethod);
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
	LogVRTransitionDiagnostics(*this);
	auto screenSize = state->screenSize;

	auto screenWidth = static_cast<int>(screenSize.x);
	auto screenHeight = static_cast<int>(screenSize.y);

	const bool vendorUpscalingMethod = IsVendorUpscalingMethod(upscaleMethod);
	RefreshRuntimeResolutionState();
	if (runtimeResolutionPlan.owner == ResolutionOwner::VRRenderScaleMode) {
		const int renderWidth = std::max(1, static_cast<int>(runtimeResolutionPlan.engineRenderSize.x));
		const int renderHeight = std::max(1, static_cast<int>(runtimeResolutionPlan.engineRenderSize.y));
		const int outputWidth = std::max(renderWidth, static_cast<int>(runtimeResolutionPlan.finalOutputSize.x));

		resolutionScale = { 1.0f, 1.0f };
		auto phaseCount = GetJitterPhaseCount(renderWidth, outputWidth);
		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		a_viewport->projectionPosScaleX = -jitter.x / renderWidth;
		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;

		auto& runtimeData = a_viewport->GetRuntimeData();
		SetDynamicResolutionEnabledForUpscaling(false);
		runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
		runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
		runtimeData.dynamicResolutionWidthRatio = 1.0f;
		runtimeData.dynamicResolutionHeightRatio = 1.0f;
		runtimeData.dynamicResolutionLock = 1;
		dynamicResolutionWidthRatio = 1.0f;
		dynamicResolutionHeightRatio = 1.0f;
		UpdateCameraData();

		CheckResources(runtimeResolutionPlan.upscaleMethod);
		RefreshRuntimeResolutionState();
		return;
	}
	if (globals::game::isVR && vendorUpscalingMethod && IsVRMenuPresentationContextActive()) {
		resolutionScale = { 1.0f, 1.0f };
		jitter = { 0.0f, 0.0f };
		a_viewport->projectionPosScaleX = 0.0f;
		a_viewport->projectionPosScaleY = 0.0f;
		PrepareFullResolutionPostProcessing();
		CheckResources(upscaleMethod);
		RefreshRuntimeResolutionState();
		return;
	}
	if (globals::game::isVR &&
		vendorUpscalingMethod &&
		IsVRTransitionPresentationProtectionActive(*this, state) &&
		IsVRLoadingPresentationContextActive(state)) {
		resolutionScale = { 1.0f, 1.0f };
		jitter = { 0.0f, 0.0f };
		a_viewport->projectionPosScaleX = 0.0f;
		a_viewport->projectionPosScaleY = 0.0f;
		PrepareFullResolutionPostProcessing();
		CheckResources(upscaleMethod);
		RefreshRuntimeResolutionState();
		return;
	}

	if (vendorUpscalingMethod) {
		float resolutionScaleBase = GetQualityModeResolutionScale(GetRuntimeQualityMode());

		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase);

		resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
		resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		if (globals::game::isVR)
			a_viewport->projectionPosScaleX = -jitter.x / renderWidth;
		else
			a_viewport->projectionPosScaleX = -2.0f * jitter.x / renderWidth;

		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;
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
		CheckResources(upscaleMethod);
		RefreshRuntimeResolutionState();
		return;
	}

	ApplyDynamicResolutionState(a_viewport);

	// Resource creation uses the runtime dynamic-resolution ratios via ConvertToDynamic.
	CheckResources(upscaleMethod);
	RefreshRuntimeResolutionState();

	// Disable dynamic resolution unless the game explicitly enables it.
	if (!globals::game::isVR)
		runtimeData.dynamicResolutionLock = 1;
}

void Upscaling::ApplyDynamicResolutionState(RE::BSGraphics::State* a_viewport)
{
	if (!a_viewport)
		return;

	auto& runtimeData = a_viewport->GetRuntimeData();
	if (IsPerfModeActive()) {
		SetDynamicResolutionEnabledForUpscaling(false);
		runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
		runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
		runtimeData.dynamicResolutionWidthRatio = 1.0f;
		runtimeData.dynamicResolutionHeightRatio = 1.0f;
		runtimeData.dynamicResolutionLock = 1;
		dynamicResolutionWidthRatio = 1.0f;
		dynamicResolutionHeightRatio = 1.0f;
		UpdateCameraData();
		return;
	}

	auto upscaleMethod = GetRuntimeUpscaleMethod();
	if (!IsVendorUpscalingMethod(upscaleMethod))
		return;

	const bool shouldUnlockDynamicResolution = globals::game::isVR && ShouldUnlockDynamicResolutionForUpscaling(upscaleMethod, resolutionScale);

	if (globals::game::isVR) {
		SetDynamicResolutionEnabledForUpscaling(shouldUnlockDynamicResolution);
		if (shouldUnlockDynamicResolution) {
			runtimeData.dynamicResolutionPreviousWidthRatio = runtimeData.dynamicResolutionWidthRatio;
			runtimeData.dynamicResolutionPreviousHeightRatio = runtimeData.dynamicResolutionHeightRatio;
			runtimeData.dynamicResolutionWidthRatio = resolutionScale.x;
			runtimeData.dynamicResolutionHeightRatio = resolutionScale.y;
			runtimeData.dynamicResolutionLock = 0;
			dynamicResolutionWidthRatio = resolutionScale.x;
			dynamicResolutionHeightRatio = resolutionScale.y;
		} else {
			runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
			runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
			runtimeData.dynamicResolutionWidthRatio = 1.0f;
			runtimeData.dynamicResolutionHeightRatio = 1.0f;
			runtimeData.dynamicResolutionLock = 1;
			dynamicResolutionWidthRatio = 1.0f;
			dynamicResolutionHeightRatio = 1.0f;
		}
		UpdateCameraData();
		return;
	}

	runtimeData.dynamicResolutionPreviousWidthRatio = dynamicResolutionWidthRatio;
	runtimeData.dynamicResolutionPreviousHeightRatio = dynamicResolutionHeightRatio;
	runtimeData.dynamicResolutionWidthRatio = resolutionScale.x;
	runtimeData.dynamicResolutionHeightRatio = resolutionScale.y;
	runtimeData.dynamicResolutionLock = 1;

	dynamicResolutionWidthRatio = resolutionScale.x;
	dynamicResolutionHeightRatio = resolutionScale.y;
}

void Upscaling::PrepareFullResolutionPostProcessing()
{
	auto viewport = globals::game::graphicsState;
	if (!viewport)
		return;

	auto& runtimeData = viewport->GetRuntimeData();
	if (globals::game::isVR)
		SetDynamicResolutionEnabledForUpscaling(false);
	runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
	runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
	runtimeData.dynamicResolutionWidthRatio = 1.0f;
	runtimeData.dynamicResolutionHeightRatio = 1.0f;
	runtimeData.dynamicResolutionLock = 1;
	dynamicResolutionWidthRatio = 1.0f;
	dynamicResolutionHeightRatio = 1.0f;
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
	delete vrMenuCompositeCB;
	vrMenuCompositeCB = new ConstantBuffer(ConstantBufferDesc<VRMenuCompositeCB>(), "Upscaling::VRMenuCompositeCB");
	delete foveatedPeripheryCB;
	foveatedPeripheryCB = new ConstantBuffer(ConstantBufferDesc<FoveatedPeripheryCB>());
	delete foveatedCenterBlendCB;
	foveatedCenterBlendCB = new ConstantBuffer(ConstantBufferDesc<FoveatedCenterBlendCB>());
	delete peripheryTAACB;
	peripheryTAACB = new ConstantBuffer(ConstantBufferDesc<PeripheryTAACB>());
	delete aaVrsVisualizationCB;
	aaVrsVisualizationCB = new ConstantBuffer(ConstantBufferDesc<AAVRSVisualizationCB>());
	delete aaVrsRefinementCB;
	aaVrsRefinementCB = new ConstantBuffer(ConstantBufferDesc<AAVRSRefinementCB>());

	// Create blend state for depth upscaling
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, upscaleBlendState.put()));

	D3D11_BLEND_DESC menuCompositeBlendDesc = {};
	menuCompositeBlendDesc.AlphaToCoverageEnable = false;
	menuCompositeBlendDesc.IndependentBlendEnable = false;
	auto& menuCompositeRT = menuCompositeBlendDesc.RenderTarget[0];
	menuCompositeRT.BlendEnable = true;
	menuCompositeRT.SrcBlend = D3D11_BLEND_ONE;
	menuCompositeRT.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	menuCompositeRT.BlendOp = D3D11_BLEND_OP_ADD;
	menuCompositeRT.SrcBlendAlpha = D3D11_BLEND_ONE;
	menuCompositeRT.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	menuCompositeRT.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	menuCompositeRT.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(device->CreateBlendState(&menuCompositeBlendDesc, vrMenuCompositeBlendState.put()));

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
	upscaleVS = nullptr;                 // com_ptr automatically releases
	vrMenuCompositePS = nullptr;         // com_ptr automatically releases
	foveatedPeripheryCS = nullptr;       // com_ptr automatically releases
	foveatedCenterBlendCS = nullptr;     // com_ptr automatically releases
	peripheryTAACS = nullptr;            // com_ptr automatically releases
	aaVrsVisualizationCS = nullptr;      // com_ptr automatically releases
	aaVrsRefinementCS = nullptr;         // com_ptr automatically releases
	submitStageStretchCS = nullptr;      // com_ptr automatically releases
	vrClearHMDMaskCS = nullptr;          // com_ptr automatically releases
	vrClearHMDMaskCB = nullptr;          // com_ptr automatically releases
	copyDepthToSharedBufferPS = nullptr; // com_ptr automatically releases
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

	const bool vrVendorMenu = globals::game::isVR && IsVendorUpscalingMethod(GetRuntimeUpscaleMethod()) && IsVRMenuPresentationContextActive();
	if (vrVendorMenu) {
		viewport->projectionPosScaleX = 0.0f;
		viewport->projectionPosScaleY = 0.0f;
		PrepareFullResolutionPostProcessing();
	}

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
		submitStageRuntimeActive.store(false, std::memory_order_relaxed);
		return false;
	}

	const bool submitStageSceneActive = IsPerfModePresentationActive();

	const bool menuBlocksSubmitStage =
		globals::game::isVR ? IsVRMenuScenePresentationBlockActive() : IsGameMenuContextActive();
	const bool active = submitStageSceneActive && !menuBlocksSubmitStage;
	submitStageRuntimeActive.store(active, std::memory_order_relaxed);
	return active;
}

bool Upscaling::IsSubmitStageDeviceLost() const
{
	return submitStageDeviceLost.load(std::memory_order_acquire);
}

bool Upscaling::ShouldSuppressVRInSceneOverlaySubmit() const
{
	if (!globals::game::isVR)
		return false;

	if (IsVRRenderScaleTransitionSafetyRelevant(*this) && HasPendingVRRenderScaleTransition())
		return true;

	if (pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) ||
		perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) ||
		vrRenderScaleResourceTrackingSyncPending.load(std::memory_order_acquire)) {
		return true;
	}

	const auto requestedMethod = GetConfiguredUpscaleMethodForTransition();
	const auto runtimeMethod = GetRuntimeUpscaleMethod();
	const bool transitionRelevant =
		IsVRRenderScaleTransitionSafetyRelevant(*this, requestedMethod) ||
		IsVRRenderScaleTransitionSafetyRelevant(*this, runtimeMethod);
	const bool vendorResetPending =
		HasPendingVRVendorRuntimeReset(*this, runtimeMethod) ||
		(requestedMethod != runtimeMethod && HasPendingVRVendorRuntimeReset(*this, requestedMethod));
	if (vendorResetPending ||
		(postLoadRuntimeResetPending.load(std::memory_order_acquire) && transitionRelevant)) {
		return true;
	}

	const uint32_t vendorResumeFrame = submitStageVendorResumeFrame.load(std::memory_order_acquire);
	if (vendorResumeFrame != 0 && transitionRelevant) {
		const uint32_t currentFrame = globals::state ? std::max(globals::state->frameCount, 1u) : 0u;
		if (currentFrame == 0 || currentFrame < vendorResumeFrame)
			return true;
	}

	return false;
}

bool Upscaling::IsVRProtectedFullSizeSubmitTexture(const vr::Texture_t* a_texture) const
{
	if (!globals::game::isVR || !a_texture || !a_texture->handle || a_texture->eType != vr::TextureType_DirectX)
		return false;

	return IsVRProtectedFullSizeRenderTargetTexture(static_cast<ID3D11Texture2D*>(a_texture->handle));
}

void Upscaling::MarkSubmitStageDeviceLost(HRESULT a_result, const char* a_context)
{
	const HRESULT deviceReason = GetD3DDeviceRemovedReason();
	const HRESULT loggedResult = IsD3DDeviceRemovedResult(deviceReason) ? deviceReason : a_result;
	if (!IsD3DDeviceRemovedResult(loggedResult))
		return;

	const bool alreadyMarked = submitStageDeviceLost.exchange(true, std::memory_order_acq_rel);
	submitStageRuntimeActive.store(false, std::memory_order_relaxed);
	submitStagePreparedFrame = std::numeric_limits<uint32_t>::max();
	submitStagePreparedFramePresentationOnly = false;
	submitStageMirrorFrame = std::numeric_limits<uint32_t>::max();
	submitStageMirrorEyeReady = {};
	submitStageMirrorSourceTexture = nullptr;
	submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	submitStageFoveatedPeripheryTAAEyeReady = {};
	pendingDLSSReset.store(false, std::memory_order_release);
	pendingFSRReset.store(false, std::memory_order_release);
	pendingPerfModeRenderTargetRecreate.store(false, std::memory_order_release);
	pendingPerfModeRenderTargetRecreateFrame.store(0, std::memory_order_release);
	pendingPerfModeRenderTargetRecreateDelayFrames.store(0, std::memory_order_release);
	pendingPerfModeRenderTargetRecreatePostLoadSettle.store(false, std::memory_order_release);
	postLoadRuntimeResetPending.store(false, std::memory_order_release);
	ClearSubmitStageVendorResumeCooldown();
	vrRenderScaleResourceTrackingSyncPending.store(false, std::memory_order_release);
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

namespace
{
	struct VRSubmitDiagnosticHistory
	{
		bool initialized = false;
		uint64_t key = 0;
		uint64_t signature = 0;
		std::string path;
		vr::EVREye eye = vr::Eye_Left;
		uint32_t submitFlags = 0;
		uint32_t firstFrame = 0;
		uint32_t lastFrame = 0;
		uint32_t repeatCount = 0;
		uint32_t lastSummaryRepeatCount = 0;
		uint32_t lastSummaryFrame = 0;
		VRTransitionDiagnosticSnapshot snapshot{};
	};

	constexpr uint32_t kVRSubmitRepeatSummaryCount = 900u;
	constexpr uint32_t kVRSubmitRepeatSummaryFrames = 1800u;
	constexpr uint32_t kVRSubmitSignatureReentryFrames = 120u;
	constexpr size_t kVRSubmitDiagnosticHistoryCapacity = 24u;
	std::array<VRSubmitDiagnosticHistory, kVRSubmitDiagnosticHistoryCapacity> g_vrSubmitDiagnostics{};

	uint64_t MixVRSubmitTextureDiagnosticSignature(uint64_t a_signature, const VRTextureDiagnosticInfo& a_info)
	{
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.valid ? 1u : 0u);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.type);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.colorSpace);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.width);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.height);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.format);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.arraySize);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.samples);
		a_signature = MixVRTransitionDiagnosticValue(a_signature, a_info.presentationTarget ? 1u : 0u);
		return a_signature;
	}

	uint64_t MixVRSubmitBoundsDiagnosticSignature(uint64_t a_signature, const VRBoundsDiagnosticInfo& a_info)
	{
		a_signature = MixVRTransitionDiagnosticValue(a_signature, QuantizeDiagnosticFloat(a_info.uMin));
		a_signature = MixVRTransitionDiagnosticValue(a_signature, QuantizeDiagnosticFloat(a_info.vMin));
		a_signature = MixVRTransitionDiagnosticValue(a_signature, QuantizeDiagnosticFloat(a_info.uMax));
		a_signature = MixVRTransitionDiagnosticValue(a_signature, QuantizeDiagnosticFloat(a_info.vMax));
		return a_signature;
	}

	uint64_t BuildVRSubmitDiagnosticSignature(
		const char* a_path,
		vr::EVREye a_eye,
		vr::EVRSubmitFlags a_submitFlags,
		const VRTransitionDiagnosticSnapshot& a_snapshot,
		const VRTextureDiagnosticInfo& a_inputInfo,
		const VRBoundsDiagnosticInfo& a_inputBounds,
		const VRTextureDiagnosticInfo& a_outputInfo,
		const VRBoundsDiagnosticInfo& a_outputBounds)
	{
		uint64_t signature = HashDiagnosticText(DiagnosticText(a_path, "unknown"));
		signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint32_t>(a_eye));
		signature = MixVRTransitionDiagnosticValue(signature, static_cast<uint32_t>(a_submitFlags));
		signature = MixVRTransitionDiagnosticValue(signature, GetVRTransitionRepeatSignature(a_snapshot));
		signature = MixVRTransitionDiagnosticValue(signature, a_snapshot.flags);
		signature = MixVRSubmitTextureDiagnosticSignature(signature, a_inputInfo);
		signature = MixVRSubmitBoundsDiagnosticSignature(signature, a_inputBounds);
		signature = MixVRSubmitTextureDiagnosticSignature(signature, a_outputInfo);
		signature = MixVRSubmitBoundsDiagnosticSignature(signature, a_outputBounds);
		return signature;
	}

	void LogVRSubmitRepeatSummary(VRSubmitDiagnosticHistory& a_history, bool a_final)
	{
		if (!a_history.initialized || a_history.repeatCount <= a_history.lastSummaryRepeatCount)
			return;

		const uint32_t repeated = a_history.repeatCount - a_history.lastSummaryRepeatCount;
		if (repeated == 0)
			return;

		const auto& snapshot = a_history.snapshot;
		VR_TRANSITION_DIAG_LOG(
			"[VRSubmit] {} {} {}: repeated {} additional times over {} frames with unchanged submit path (signature=0x{:X}, lastFrame={}, flags=0x{:X}, req={}, runtime={}, quality={}, renderScaleRelevant={}, pendingRelatch={}, relatchAge={}, relatchDelay={}, closeAge={}, vendorPending={}, hmdDefer={}, projectedDefer={}, submitDeviceLost={})",
			a_history.path.empty() ? "unknown" : a_history.path.c_str(),
			VREyeName(a_history.eye),
			a_final ? "summary" : "still repeating",
			repeated,
			ElapsedFrames(a_history.firstFrame, a_history.lastFrame),
			a_history.signature,
			a_history.lastFrame,
			a_history.submitFlags,
			magic_enum::enum_name(snapshot.requestedMethod),
			magic_enum::enum_name(snapshot.runtimeMethod),
			snapshot.qualityMode,
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::RenderScaleRelevant)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::PendingRelatch)),
			snapshot.relatchAge,
			snapshot.relatchDelay,
			snapshot.closeAge,
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::VendorResetPending)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::HMDMaskDeferred)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::ProjectedMaskDeferred)),
			BoolText(HasDiagnosticFlag(snapshot.flags, VRTransitionDiagnosticFlag::SubmitStageDeviceLost)));
		a_history.lastSummaryRepeatCount = a_history.repeatCount;
		a_history.lastSummaryFrame = a_history.lastFrame;
	}

	VRSubmitDiagnosticHistory& GetVRSubmitDiagnosticHistory(uint64_t a_signature, uint32_t a_frame)
	{
		VRSubmitDiagnosticHistory* emptyHistory = nullptr;
		VRSubmitDiagnosticHistory* oldestHistory = nullptr;
		for (auto& history : g_vrSubmitDiagnostics) {
			if (history.initialized && history.key == a_signature)
				return history;
			if (!history.initialized && !emptyHistory)
				emptyHistory = &history;
			if (history.initialized && (!oldestHistory || history.lastFrame < oldestHistory->lastFrame))
				oldestHistory = &history;
		}

		auto* selectedHistory = emptyHistory ? emptyHistory : oldestHistory;
		if (!selectedHistory)
			selectedHistory = &g_vrSubmitDiagnostics.front();

		if (selectedHistory->initialized)
			LogVRSubmitRepeatSummary(*selectedHistory, true);

		*selectedHistory = {};
		selectedHistory->key = a_signature;
		selectedHistory->firstFrame = std::max(a_frame, 1u);
		selectedHistory->lastFrame = a_frame;
		return *selectedHistory;
	}

	bool ShouldLogVRSubmitSnapshot(
		const char* a_path,
		vr::EVREye a_eye,
		vr::EVRSubmitFlags a_submitFlags,
		const VRTransitionDiagnosticSnapshot& a_snapshot,
		uint64_t a_signature)
	{
		auto& history = GetVRSubmitDiagnosticHistory(a_signature, a_snapshot.frame);
		const bool reenteredAfterGap =
			history.initialized &&
			history.lastFrame != 0 &&
			ElapsedFrames(history.lastFrame, a_snapshot.frame) >= kVRSubmitSignatureReentryFrames;
		if (!history.initialized || reenteredAfterGap) {
			LogVRSubmitRepeatSummary(history, true);
			history.initialized = true;
			history.signature = a_signature;
			history.path = DiagnosticText(a_path, "unknown");
			history.eye = a_eye;
			history.submitFlags = static_cast<uint32_t>(a_submitFlags);
			history.firstFrame = std::max(a_snapshot.frame, 1u);
			history.lastFrame = a_snapshot.frame;
			history.repeatCount = 1;
			history.lastSummaryRepeatCount = 1;
			history.lastSummaryFrame = a_snapshot.frame;
			history.snapshot = a_snapshot;
			return true;
		}

		++history.repeatCount;
		history.lastFrame = a_snapshot.frame;
		history.snapshot = a_snapshot;
		const bool countThreshold =
			history.repeatCount - history.lastSummaryRepeatCount >= kVRSubmitRepeatSummaryCount;
		const bool frameThreshold =
			ElapsedFrames(history.lastSummaryFrame, a_snapshot.frame) >= kVRSubmitRepeatSummaryFrames;
		if (countThreshold || frameThreshold)
			LogVRSubmitRepeatSummary(history, false);

		return false;
	}
}

void Upscaling::LogVRCompositorSubmitPath(vr::EVREye a_eye, const char* a_path, const vr::Texture_t* a_inputTexture,
	const vr::VRTextureBounds_t* a_inputBounds, const vr::Texture_t* a_outputTexture, const vr::VRTextureBounds_t* a_outputBounds, vr::EVRSubmitFlags a_submitFlags) const
{
	VRTransitionDiagnosticSnapshot snapshot{};
	if (!TryBuildVRSubmitPathDiagnosticSnapshot(*this, snapshot))
		return;

	const auto inputInfo = BuildVRTextureDiagnosticInfo(a_inputTexture);
	const auto outputInfo = BuildVRTextureDiagnosticInfo(a_outputTexture);
	const auto inputBounds = BuildVRBoundsDiagnosticInfo(a_inputBounds);
	const auto outputBounds = BuildVRBoundsDiagnosticInfo(a_outputBounds);
	const uint64_t signature = BuildVRSubmitDiagnosticSignature(
		a_path,
		a_eye,
		a_submitFlags,
		snapshot,
		inputInfo,
		inputBounds,
		outputInfo,
		outputBounds);
	if (!ShouldLogVRSubmitSnapshot(a_path, a_eye, a_submitFlags, snapshot, signature))
		return;

	VR_TRANSITION_DIAG_LOG(
		"[VRSubmit] {} frame={} eye={} flags=0x{:X} closeAge={} req={} runtime={} quality={} renderScaleRelevant={} pendingRelatch={} vendorPending={} hmdDefer={} projectedDefer={} input=0x{:X} type={} colorSpace={} directX={} {}x{} fmt={} array={} samples={} inputPresentationRT={} inputBounds=({:.4f},{:.4f})->({:.4f},{:.4f}) output=0x{:X} type={} colorSpace={} directX={} {}x{} fmt={} array={} samples={} outputPresentationRT={} outputBounds=({:.4f},{:.4f})->({:.4f},{:.4f})",
		DiagnosticText(a_path, "unknown"),
		snapshot.frame,
		VREyeName(a_eye),
		static_cast<uint32_t>(a_submitFlags),
		snapshot.closeAge,
		magic_enum::enum_name(snapshot.requestedMethod),
		magic_enum::enum_name(snapshot.runtimeMethod),
		snapshot.qualityMode,
		DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::RenderScaleRelevant),
		DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::PendingRelatch),
		DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::VendorResetPending),
		DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::HMDMaskDeferred),
		DiagnosticFlagText(snapshot, VRTransitionDiagnosticFlag::ProjectedMaskDeferred),
		reinterpret_cast<uintptr_t>(inputInfo.handle),
		inputInfo.type,
		inputInfo.colorSpace,
		BoolText(inputInfo.valid),
		inputInfo.width,
		inputInfo.height,
		inputInfo.format,
		inputInfo.arraySize,
		inputInfo.samples,
		BoolText(inputInfo.presentationTarget),
		inputBounds.uMin,
		inputBounds.vMin,
		inputBounds.uMax,
		inputBounds.vMax,
		reinterpret_cast<uintptr_t>(outputInfo.handle),
		outputInfo.type,
		outputInfo.colorSpace,
		BoolText(outputInfo.valid),
		outputInfo.width,
		outputInfo.height,
		outputInfo.format,
		outputInfo.arraySize,
		outputInfo.samples,
		BoolText(outputInfo.presentationTarget),
		outputBounds.uMin,
		outputBounds.vMin,
		outputBounds.uMax,
		outputBounds.vMax);
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

	if (!IsPresentationUpscalingActive())
		return false;

	RefreshRuntimeResolutionState();
	const auto& resolutionPlan = GetRuntimeResolutionPlan();
	const auto upscaleMethod = resolutionPlan.upscaleMethod;
	if (!IsVendorUpscalingMethod(upscaleMethod))
		return false;
	const auto upscaleMethodName = magic_enum::enum_name(upscaleMethod);
	const uint32_t currentFrame = state->frameCount;
	auto* sourceTexture = static_cast<ID3D11Texture2D*>(a_inputTexture->handle);
	if (IsVRProtectedFullSizeRenderTargetTexture(sourceTexture))
		return false;

	const bool presentationRenderTarget = IsVRPresentationRenderTargetTexture(sourceTexture);
	const bool loadingPresentationContext =
		IsVRTransitionPresentationProtectionActive(*this, state) &&
		IsVRLoadingPresentationContextActive(state);
	const bool currentMenuPresentationContext = IsVRMenuPresentationContextActive();
	const bool menuTextProtectionContext =
		resolutionPlan.menuContextActive ||
		currentMenuPresentationContext;
	const bool submitPresentationContext =
		loadingPresentationContext ||
		presentationRenderTarget;
	const bool menuPresentationContext =
		menuTextProtectionContext ||
		submitPresentationContext;
	const bool submitMenuPresentationContext = IsSubmitStageMenuPresentationContextActive();

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
	const bool vrRenderScaleMode = resolutionPlan.owner == ResolutionOwner::VRRenderScaleMode;
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
	const bool sourceHasPerEyeLayout =
		sourceDesc.ArraySize > 1 ||
		(sourceDesc.ArraySize == 1 && TextureContainsVREyeRegion(sourceDesc.Width, sourceDesc.Height, sourceStereoLayout.eyes[1]));
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
			const auto inputBounds = BuildVRBoundsDiagnosticInfo(a_inputBounds);
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
	const bool vrRenderScaleMenuCanUseVendor =
		vrRenderScaleMode &&
		!presentationRenderTarget &&
		submitMenuPresentationContext &&
		sourceRegion.matchesExpectedSize &&
		(a_inputBounds || sourceHasPerEyeLayout);
	const uint32_t vendorResumeFrame = submitStageVendorResumeFrame.load(std::memory_order_acquire);
	bool transitionPresentationCooldown =
		vendorResumeFrame != 0 &&
		currentFrame < vendorResumeFrame;
	if (vendorResumeFrame != 0 && !transitionPresentationCooldown)
		ClearSubmitStageVendorResumeCooldown();
	const bool foveatedTransitionBypass = ShouldBypassVRFoveatedVendorDispatchForTransition(*this, state);
	auto computePresentationOnly = [&]() {
		const bool transitionPresentationOnly = vrRenderScaleMode && transitionPresentationCooldown;
		return vrRenderScaleMode &&
		       (submitPresentationContext || transitionPresentationOnly || submitBoundsPresentationFallback) &&
		       !vrRenderScaleMenuCanUseVendor;
	};
	bool presentationOnly =
		computePresentationOnly();

	CheckResources(upscaleMethod);
	if (IsSubmitStageDeviceLost())
		return false;

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	const bool submitStagePreparedThisFrame = submitStagePreparedFrame == currentFrame;
	if (!submitStagePreparedThisFrame) {
		if (!ApplyPendingVendorRuntimeReset(upscaleMethod, "submit-stage ")) {
			if (IsSubmitStageDeviceLost())
				return false;

			return false;
		}
		if (IsSubmitStageDeviceLost())
			return false;

		if (!presentationOnly && HasPendingVRVendorRuntimeReset(*this, upscaleMethod))
			return false;

		UpdateHistoryResetState(upscaleMethod);
		LatchHistoryResetForCurrentFrame();
	} else {
		const bool vendorResetPending = HasPendingVRVendorRuntimeReset(*this, upscaleMethod);
		if (vendorResetPending && !presentationOnly)
			return false;
	}

	if (transitionPresentationCooldown) {
		const bool cooldownStableCandidate =
			vrRenderScaleMode &&
			!menuPresentationContext &&
			!submitMenuPresentationContext &&
			!presentationRenderTarget &&
			!submitBoundsPresentationFallback &&
			sourceRegion.matchesExpectedSize &&
			!pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire) &&
			!perfModeRenderTargetRecreateInProgress.load(std::memory_order_acquire) &&
			!HasPendingVRVendorRuntimeReset(*this, upscaleMethod) &&
			!IsVRTransitionPresentationProtectionActive(*this, state) &&
			!ShouldDeferHMDClearMask() &&
			!ShouldDeferVRProjectedMaskRepair(*this, state) &&
			!foveatedTransitionBypass &&
			motionVector.texture &&
			depth.texture;
		if (cooldownStableCandidate) {
			uint32_t stableFrames = submitStageVendorResumeStableFrames.load(std::memory_order_acquire);
			const uint32_t lastStableFrame = submitStageVendorResumeLastStableFrame.load(std::memory_order_acquire);
			if (lastStableFrame != currentFrame) {
				stableFrames = lastStableFrame != 0 && currentFrame == lastStableFrame + 1 ?
					stableFrames + 1 :
					1u;
				submitStageVendorResumeStableFrames.store(stableFrames, std::memory_order_release);
				submitStageVendorResumeLastStableFrame.store(currentFrame, std::memory_order_release);
			}

			const uint32_t cooldownStartFrame = submitStageVendorResumeStartFrame.load(std::memory_order_acquire);
			if (ElapsedFrames(cooldownStartFrame, currentFrame) >= kVRSubmitStageVendorRelatchMinCooldownFrames &&
				stableFrames >= kVRSubmitStageVendorRelatchStableFrames) {
				ClearSubmitStageVendorResumeCooldown();
				transitionPresentationCooldown = false;
				presentationOnly = computePresentationOnly();
				logger::debug(
					"[VRRenderScale] Cleared submit-stage vendor cooldown early after {} stable frames.",
					stableFrames);
				VR_TRANSITION_DIAG_LOG(
					"[VRTransition] Submit-stage vendor cooldown cleared early after {} stable frames",
					stableFrames);
			}
		} else {
			submitStageVendorResumeStableFrames.store(0, std::memory_order_release);
			submitStageVendorResumeLastStableFrame.store(0, std::memory_order_release);
		}
	}

	const bool transitionPresentationOnly = vrRenderScaleMode && transitionPresentationCooldown;
	const bool sceneFeatureMenuPauseContext = IsVRSceneFeatureMenuPauseContextActive();
	const bool foveatedRequested =
		!presentationOnly &&
		!sceneFeatureMenuPauseContext &&
		IsFoveatedVendorDispatchEnabled(upscaleMethod) &&
		!vrRenderScaleMenuCanUseVendor &&
		!foveatedTransitionBypass;
	const bool presentationSourceTooSmall =
		presentationOnly &&
		(sourceDesc.Width < sourceEyeWidthIn || sourceDesc.Height < sourceEyeHeightIn);
	const std::string submitResolvePhase = std::format(
		"resolve:eye={} menu={} textMenu={} submitMenu={} submitPresentation={} presentationRT={} presentationOnly={} boundsFallback={} vendorMenu={} foveated={} cooldown={} sourceTooSmall={}",
		VREyeName(a_eye),
		BoolText(menuPresentationContext),
		BoolText(menuTextProtectionContext),
		BoolText(submitMenuPresentationContext),
		BoolText(submitPresentationContext),
		BoolText(presentationRenderTarget),
		BoolText(presentationOnly),
		BoolText(submitBoundsPresentationFallback),
		BoolText(vrRenderScaleMenuCanUseVendor),
		BoolText(foveatedRequested),
		BoolText(transitionPresentationCooldown),
		BoolText(presentationSourceTooSmall));
	LogVRPresentationPassDiagnostics(
		*this,
		VRPresentationDiagnosticSlot::SubmitStage,
		"SubmitVRUpscaledFrame",
		submitResolvePhase.c_str(),
		false);
	LogVRTransitionDiagnostics(*this);

	if (!presentationOnly && (!motionVector.texture || !depth.texture))
		return false;

	const uint32_t presentationInputWidth = submitBoundsPresentationFallback ? sourceRegion.width : sourceEyeWidthIn;
	const uint32_t presentationInputHeight = submitBoundsPresentationFallback ? sourceRegion.height : sourceEyeHeightIn;

	if (presentationSourceTooSmall) {
		static std::atomic_bool loggedSmallPresentationSource{ false };
		if (!loggedSmallPresentationSource.exchange(true, std::memory_order_acq_rel)) {
			logger::debug(
				"[VRRenderScale] Submit-stage presentation fallback skipped because submitted source {}x{} is smaller than expected eye source {}x{}; using original VR submit.",
				sourceDesc.Width,
				sourceDesc.Height,
				sourceEyeWidthIn,
				sourceEyeHeightIn);
		}
		return false;
	}

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
	} else if (!submitStagePreparedThisFrame || submitStagePreparedFramePresentationOnly) {
		if (!EncodeSubmitStageVRInputs(sourceTexture, motionVector.texture, depth.texture, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut)) {
			if (IsSubmitStageDeviceLost())
				return false;
			return false;
		}

		submitStagePreparedFrame = currentFrame;
		submitStagePreparedFramePresentationOnly = false;
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
		if (vrRenderScaleMenuCanUseVendor)
			return;
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

	const bool useKnownGameMenuSceneSnapshot =
		!presentationOnly &&
		HasKnownGameMenuSceneSnapshotForSubmit(currentFrame, sourceTexture, sourceDesc);
	ID3D11Resource* submitColorSource = useKnownGameMenuSceneSnapshot ? vrKnownMenuSceneBeforeComposite->resource.get() : sourceTexture;
	const UINT submitColorSubresource = useKnownGameMenuSceneSnapshot ? 0u : sourceSubresource;
	context->CopySubresourceRegion(vrIntermediateColorIn[eyeIndex]->resource.get(), 0, 0, 0, 0, submitColorSource, submitColorSubresource, &colorBox);
	if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage source copy"))
		return false;
	if (useKnownGameMenuSceneSnapshot) {
		static std::array<bool, 2> loggedSceneSnapshotSubmit{};
		if (!loggedSceneSnapshotSubmit[eyeIndex]) {
			logger::debug(
				"[VRMenuComposite] using captured scene-only kTOTAL for submit-stage vendor input eye={} source={}x{} box=({},{})->({},{})",
				eyeIndex,
				sourceDesc.Width,
				sourceDesc.Height,
				colorBox.left,
				colorBox.top,
				colorBox.right,
				colorBox.bottom);
			loggedSceneSnapshotSubmit[eyeIndex] = true;
		}
	}

	const auto logSubmitStagePath = [&](const char* path) {
		const uint32_t inputEyeWidth = presentationOnly ? presentationInputWidth : sourceEyeWidthIn;
		const uint32_t inputEyeHeight = presentationOnly ? presentationInputHeight : sourceEyeHeightIn;
		LogVRSubmitStagePathDiagnostics(
			*this,
			path,
			a_eye,
			a_inputBounds,
			sourceDesc,
			colorBox,
			sourceSubresource,
			inputEyeWidth,
			inputEyeHeight,
			eyeWidthOut,
			eyeHeightOut,
			vrRenderScaleMode,
			presentationOnly,
			foveatedRequested,
			presentationRenderTarget);
	};

	const auto presentStretchOutput = [&](uint32_t inputWidth, uint32_t inputHeight, const char* path) {
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
		logSubmitStagePath(path);
		return true;
	};

	if (presentationOnly) {
		return presentStretchOutput(
			presentationInputWidth,
			presentationInputHeight,
			submitBoundsPresentationFallback ? "submit-bounds-stretch-output" :
			                                   (transitionPresentationOnly ? "transition-presentation-output" : "menu-loading-presentation-output"));
	}

	bool submitDLSSSharpening = upscaleMethod == UpscaleMethod::kDLSS && settings.sharpnessDLSS > 0.0f;
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
	if (foveatedRequested) {
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
				"[Upscaling] Submit-stage foveated {} threw for eye {}; falling back to full-eye vendor dispatch: {}",
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
				"[Upscaling] Submit-stage foveated {} threw for eye {}; falling back to full-eye vendor dispatch",
				upscaleMethodName,
				eyeIndex);
			vendorSucceeded = false;
		}
		if (!vendorSucceeded && MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage foveated vendor dispatch"))
			return false;
		if (!vendorSucceeded) {
			static bool loggedFoveatedSubmitFallback[2] = {};
			if (!loggedFoveatedSubmitFallback[eyeIndex]) {
				logger::warn(
					"[Upscaling] Submit-stage foveated {} failed for eye {}; falling back to full-eye vendor dispatch for this frame.",
					upscaleMethodName,
					eyeIndex);
				loggedFoveatedSubmitFallback[eyeIndex] = true;
			}
		}
	}

	if (!vendorSucceeded) {
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

	if (submitDLSSSharpening) {
		if (!ApplySubmitStageDLSSSharpening(eyeIndex, *vendorColorOutput)) {
			if (IsSubmitStageDeviceLost())
				return false;
			context->CopyResource(vrIntermediateColorOut[eyeIndex]->resource.get(), vendorColorOutput->resource.get());
			if (MarkSubmitStageDeviceLostIfDeviceRemoved("submit-stage DLSS sharpening fallback copy"))
				return false;
		}
		if (IsSubmitStageDeviceLost())
			return false;
	}

	const bool knownGameMenuFinalComposite =
		vrRenderScaleMode &&
		!presentationRenderTarget &&
		useKnownGameMenuSceneSnapshot &&
		menuTextProtectionContext &&
		CompositeKnownGameMenuAfterSubmitStageUpscale(eyeIndex, eyeWidthOut, eyeHeightOut);
	if (IsSubmitStageDeviceLost())
		return false;

	clearSubmittedEyeHMDMask();
	if (IsSubmitStageDeviceLost())
		return false;
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
			static bool loggedSubmitStageMirrorSkip = false;
			if (!loggedSubmitStageMirrorSkip) {
				logger::warn(
					"[Upscaling] Desktop mirror writeback skipped because the submit texture is not a compatible full stereo target. source={}x{} array={} format={} outputL={}x{} format={} outputR={}x{} format={}",
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
				loggedSubmitStageMirrorSkip = true;
			}
		}

		a_outputTexture = *a_inputTexture;
		a_outputTexture.handle = vrIntermediateColorOut[eyeIndex]->resource.get();
		a_outputTexture.eType = vr::TextureType_DirectX;
		a_outputBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
		logSubmitStagePath(
			knownGameMenuFinalComposite ?
				(foveatedRequested ? "foveated-vendor-output+known-menu-final-composite" : "full-eye-vendor-output+known-menu-final-composite") :
				(foveatedRequested ? "foveated-vendor-output" : "full-eye-vendor-output"));
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
	logSubmitStagePath("source-texture-output-copy");
	return true;
}

void Upscaling::RequestHistoryReset()
{
	historyResetRequested = true;
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
	const uint32_t frame = globals::state ? std::max(globals::state->frameCount, 1u) : 1u;
	const bool transitionAlreadyQueued = pendingVRUpscalingTransitionFrame.load(std::memory_order_acquire) != 0;
	if (!transitionAlreadyQueued ||
	    UsesVRRenderScalePostLoadSettle(a_origin) ||
	    !UsesVRRenderScalePostLoadSettle(LoadVRUpscalingTransitionOrigin(pendingVRUpscalingTransitionOrigin))) {
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
		IsPerfModeActive() ||
		perfMode.HasRestartRequiredChange();

	if (currentRenderScaleMode != targetRenderScaleMode)
		return true;

	if (effectiveQualityMode != qualityMode && (currentRenderScaleMode || targetRenderScaleMode || IsPerfModeActive() || perfMode.HasRestartRequiredChange()))
		return true;

	return currentPerfMode != targetPerfMode;
}

bool Upscaling::ShouldDeferVRUpscalingTransitionSettings() const
{
	if (!globals::game::isVR)
		return false;

	const auto* state = globals::state;
	if (IsCommunityShadersMenuOpen())
		return true;
	if (IsUpscalingLoadTransitionContextActive(*this, state))
		return true;
	if (ShouldApplyVRRenderScaleTransitionDuringLoadingMenu(*this, state))
		return false;

	return IsKnownGameMenuContextActive();
}

bool Upscaling::ShouldWaitForVRUpscalingTransitionDelay() const
{
	if (!HasPendingVRRenderScaleTransition())
		return false;

	const uint32_t queuedFrame = pendingVRUpscalingTransitionFrame.load(std::memory_order_acquire);
	if (queuedFrame == 0)
		return false;

	if (ShouldApplyVRRenderScaleTransitionDuringLoadingMenu(*this, globals::state))
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
	const bool shortenRelatchForLoadingMenu =
		delayFrames == kVRLoadingMenuRelatchDelayFrames &&
		previousDelay == kVRUpscalingTransitionApplyDelayFrames &&
		ShouldApplyVRRenderScaleTransitionDuringLoadingMenu(*this, globals::state);
	if (previousDelay == 0 || delayFrames > previousDelay || shortenRelatchForLoadingMenu)
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
		if (perfModePending || ClampToggleUInt(settings.perfMode) != 0 || IsPerfModeActive() || perfMode.HasRestartRequiredChange())
			SetPerfModeRequested(false, "VR upscaling deferred transition", false, transitionOrigin);

		if (renderScaleModeChanged)
			RequestHistoryReset();
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
		RequestHistoryReset();
		if (a_upscaleMethod == UpscaleMethod::kDLSS)
			pendingDLSSHistoryReset.store(true, std::memory_order_release);
		if ((qualityChanged || renderScaleModeChanged) && !perfModePending && (IsPerfModeActive() || GetPerfModeRequested()))
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
	RefreshRuntimeResolutionState();
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
			(peripheryTAAPathActive && (
				std::abs(peripheryTAAOuterScale - previousHistoryPeripheryTAAOuterScale) > 1e-4f ||
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

			struct EncodeRegion
			{
				uint32_t minX = 0;
				uint32_t minY = 0;
				uint32_t maxX = 0;
				uint32_t maxY = 0;
				bool valid = false;
			};

			auto includeInputRect = [&](EncodeRegion& region, uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) {
				minX = std::min(minX, eyeWidthIn);
				minY = std::min(minY, eyeHeightIn);
				maxX = std::min(maxX, eyeWidthIn);
				maxY = std::min(maxY, eyeHeightIn);
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

			const bool useRegionEncode = !forceFullVREncode && foveatedDispatchRequested;
			bool dispatchedRegionEncode = false;
			if (useRegionEncode) {
				const bool usePeripheryTAAProfile = IsPeripheryTAAEnabled(upscaleMethod);
				const bool usePeripheryTAAPath = IsPeripheryTAAPathActive(upscaleMethod);
				const auto foveatedProfile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
				const float centerScale = foveatedProfile.centerScale;
				const float centerHorizontalScale = foveatedProfile.centerHorizontalScale;
				const float centerFeather = usePeripheryTAAPath ?
					ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) :
					FoveatedCommon::kCenterFeather;

				if (BuildFoveatedDispatchRects(eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut, true, centerScale, centerFeather, centerHorizontalScale, usePeripheryTAAProfile)) {
					std::array<EncodeRegion, 2> regions{};
					bool allRegionsValid = true;
					for (uint32_t eye = 0; eye < 2; ++eye) {
						const auto& eyePlan = foveatedRectCache.plan.eyes[eye];
						if (!eyePlan.encodeInput.IsValid()) {
							allRegionsValid = false;
							break;
						}

						includeInputRect(regions[eye], eyePlan.encodeInput.minX, eyePlan.encodeInput.minY, eyePlan.encodeInput.maxX, eyePlan.encodeInput.maxY);

						if (!regions[eye].valid) {
							allRegionsValid = false;
							break;
						}
					}

					if (allRegionsValid) {
						for (uint32_t eye = 0; eye < 2; ++eye) {
							dispatchEyeEncode(eye, regions[eye].minX, regions[eye].minY, regions[eye].maxX, regions[eye].maxY);
						}
						dispatchedRegionEncode = true;
						encodedVRFoveatedRegions = true;
					}
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
				settings.sharpnessDLSS > 0.0f &&
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

		// t0: vanilla mask copy, t1: current scene depth, t2: current stencil/HAM mask (VR).
		ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(underwaterMaskPS, nullptr, 0);
		LogVRProjectedMaskRepairDispatch(*this, "UpscaleDepth", viewport.Width, viewport.Height);
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
	if (ShouldDeferVRProjectedMaskRepair(*this, state)) {
		return;
	}

	auto screenSize = state->screenSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		return;
	}
	RefreshRuntimeResolutionState();
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
	// Match the dynamic scene footprint used by the raw depth input. Drawing
	// the full output-sized mask target under submit-stage scaling makes the
	// fullscreen triangle UVs diverge from the dynamic depth coordinates and
	// produces diagonal clear regions at the waterline.
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
	LogVRProjectedMaskRepairDispatch(*this, "SubmitStageUnderwaterMask", viewport.Width, viewport.Height);
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

	if (settings.sharpnessDLSS <= 0.0f)
		return;

	if (!sharpenerTexture)
		return;

	const float currentSharpness = GetDLSSRCASSharpness(settings.sharpnessDLSS);

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	context->OMSetRenderTargets(0, nullptr, nullptr);

	if (dlssUpscaleOutputInSharpenerTexture) {
		if (!main.texture || !sharpenerTexture->resource)
			return;

		if (!main.UAV || !sharpenerTexture->srv || !rcas.ApplySharpen(sharpenerTexture->srv.get(), main.UAV, currentSharpness))
			context->CopyResource(main.texture, sharpenerTexture->resource.get());
	} else {
		if (!main.SRV || !main.texture || !sharpenerTexture->resource || !sharpenerTexture->uav)
			return;

		if (!rcas.ApplySharpen(main.SRV, sharpenerTexture->uav.get(), currentSharpness))
			return;
		context->CopyResource(main.texture, sharpenerTexture->resource.get());
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

bool Upscaling::TryReplaceVanillaDynamicResolutionUpsample(const char* a_passName, DynamicResolutionUpsampleStage a_stage)
{
	auto& upscaling = globals::features::upscaling;
	upscaling.DisableAAVRSState();
	auto upscaleMethod = upscaling.GetRuntimeUpscaleMethod();
	if (IsVendorUpscalingMethod(upscaleMethod) && upscaling.IsUpscalingActive()) {
		const char* stageName = a_stage == DynamicResolutionUpsampleStage::Dispatch ? "Dispatch" : "Render";
		const auto diagnosticSlot = a_stage == DynamicResolutionUpsampleStage::Dispatch ?
			VRPresentationDiagnosticSlot::DynamicUpsampleDispatch :
			VRPresentationDiagnosticSlot::DynamicUpsampleRender;
		const auto logDecision = [&](const char* a_decision, bool a_includeKnownTargets = false) {
			if (!globals::game::isVR)
				return;

			const std::string phase = std::format("{}:{}", stageName, DiagnosticText(a_decision, "unknown"));
			LogVRPresentationPassDiagnostics(upscaling, diagnosticSlot, a_passName, phase.c_str(), a_includeKnownTargets);
		};
		logDecision("entry", true);

		if (globals::game::isVR &&
			IsVRTransitionPresentationProtectionActive(upscaling, globals::state) &&
			IsVRLoadingPresentationContextActive(globals::state)) {
			logDecision("vanilla-loading-presentation-protection");
			return false;
		}

		const bool menuPresentationContext = globals::game::isVR ? IsVRMenuScenePresentationBlockActive() : IsGameMenuContextActive();
		if (menuPresentationContext) {
			logDecision("vanilla-menu-without-submit-stage");
			return false;
		}

		auto context = globals::d3d::context;
		auto renderer = globals::game::renderer;
		if (!context || !renderer) {
			logDecision("vanilla-missing-context");
			return false;
		}

		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		if (!main.texture) {
			logDecision("vanilla-missing-main");
			return false;
		}

		const auto state = globals::state;
		if (!state) {
			logDecision("vanilla-missing-state");
			return false;
		}

		const auto screenSize = state->screenSize;
		const auto renderSize = Util::ConvertToDynamic(screenSize);
		const uint32_t inputWidth = static_cast<uint32_t>(std::max(1.0f, renderSize.x));
		const uint32_t inputHeight = static_cast<uint32_t>(std::max(1.0f, renderSize.y));
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

		const auto tryAcquireSource = [&](ID3D11ShaderResourceView* candidateSRV) {
			if (!candidateSRV)
				return false;

			ID3D11Resource* candidateResource = nullptr;
			candidateSRV->GetResource(&candidateResource);
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

			sourceSRV = candidateSRV;
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

		if (!sourceSRV || !sourceResource || !sourceTexture) {
			static bool loggedMissingSource = false;
			if (!loggedMissingSource) {
				logger::warn(
					"[Upscaling] {} replacement could not find a suitable source SRV t0 for {}x{}; falling back to vanilla pass.",
					a_passName,
					inputWidth,
					inputHeight);
				loggedMissingSource = true;
			}
			releaseSourceSRVs();
			logDecision("vanilla-missing-source");
			return false;
		}

		ID3D11RenderTargetView* outputRTV = nullptr;
		ID3D11DepthStencilView* outputDSV = nullptr;
		context->OMGetRenderTargets(1, &outputRTV, &outputDSV);
		if (!outputRTV) {
			sourceTexture->Release();
			sourceResource->Release();
			releaseSourceSRVs();
			logDecision("vanilla-missing-output-rtv");
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
			logDecision("vanilla-invalid-output-texture");
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

		if (upscaling.IsSubmitStageUpscalingActive()) {
			// Protected UI/fade targets are not final eye images. Keep them on the
			// vanilla path so submit-stage upscaling cannot copy or stretch them as
			// scene presentation sources. In-place interaction passes keep the older
			// contextual fallback for prompt frames.
			const bool inPlacePass = outputTexture == sourceTexture;
			const bool uiRenderTargetPass =
				IsVRProtectedFullSizeRenderTargetTexture(sourceTexture) ||
				IsVRProtectedFullSizeRenderTargetTexture(outputTexture);
			const bool interactionUiContext = !IsKnownGameMenuContextActive();
			if (uiRenderTargetPass || (inPlacePass && interactionUiContext)) {
				logDecision(uiRenderTargetPass ? "vanilla-submit-ui-target-pass" : "vanilla-submit-in-place-pass");
				releaseRefs();
				return false;
			}

			unbindSourceSRV();
			context->OMSetRenderTargets(0, nullptr, nullptr);

			auto copyDynamicRegionToTarget = [&](ID3D11Texture2D* targetTexture) {
				if (!targetTexture)
					return false;
				if (targetTexture == sourceTexture)
					return true;

				D3D11_TEXTURE2D_DESC sourceDesc{};
				D3D11_TEXTURE2D_DESC targetDesc{};
				sourceTexture->GetDesc(&sourceDesc);
				targetTexture->GetDesc(&targetDesc);
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
				context->CopySubresourceRegion(targetTexture, 0, 0, 0, 0, sourceTexture, 0, &sourceBox);
				return true;
			};

			const bool copiedToOutput = copyDynamicRegionToTarget(outputTexture);
			if (copiedToOutput) {
				context->OMSetRenderTargets(1, &outputRTV, outputDSV);
				logDecision("replaced-submit-stage-copy");
				releaseRefs();
				if (globals::game::stateUpdateFlags)
					globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
				return true;
			}

			context->OMSetRenderTargets(1, &outputRTV, outputDSV);
			restoreSourceSRVs();
			logDecision("vanilla-submit-copy-failed");
			releaseRefs();
			return false;
		}

		bool sourceReady = sourceTexture == main.texture;
		if (sourceTexture != main.texture) {
			D3D11_TEXTURE2D_DESC sourceDesc{};
			D3D11_TEXTURE2D_DESC mainDesc{};
			sourceTexture->GetDesc(&sourceDesc);
			main.texture->GetDesc(&mainDesc);

			if (sourceDesc.SampleDesc.Count == mainDesc.SampleDesc.Count &&
				sourceDesc.Format == mainDesc.Format &&
				inputWidth <= sourceDesc.Width &&
				inputHeight <= sourceDesc.Height &&
				inputWidth <= mainDesc.Width &&
				inputHeight <= mainDesc.Height) {
				unbindSourceSRV();
				context->OMSetRenderTargets(0, nullptr, nullptr);
				D3D11_BOX sourceBox{ 0, 0, 0, inputWidth, inputHeight, 1 };
				context->CopySubresourceRegion(main.texture, 0, 0, 0, 0, sourceTexture, 0, &sourceBox);
				sourceReady = true;
			} else {
				static bool loggedSourceMismatch = false;
				if (!loggedSourceMismatch) {
					logger::warn(
						"[Upscaling] Dynamic-resolution upsample replacement could not copy source to main: input={}x{} source={}x{} fmt={} samples={} main={}x{} fmt={} samples={}",
						inputWidth,
						inputHeight,
						sourceDesc.Width,
						sourceDesc.Height,
						static_cast<uint32_t>(sourceDesc.Format),
						sourceDesc.SampleDesc.Count,
						mainDesc.Width,
						mainDesc.Height,
						static_cast<uint32_t>(mainDesc.Format),
						mainDesc.SampleDesc.Count);
					loggedSourceMismatch = true;
				}
			}
		}

		if (!sourceReady) {
			logDecision("vanilla-source-not-ready");
			releaseRefs();
			return false;
		}

		upscaling.Upscale();
		upscaling.UpscaleDepth();
		if (upscaleMethod == UpscaleMethod::kDLSS)
			upscaling.ApplySharpening();

		if (outputTexture != main.texture) {
			D3D11_TEXTURE2D_DESC outputDesc{};
			D3D11_TEXTURE2D_DESC mainDesc{};
			outputTexture->GetDesc(&outputDesc);
			main.texture->GetDesc(&mainDesc);

			if (outputDesc.SampleDesc.Count == mainDesc.SampleDesc.Count &&
				outputDesc.Format == mainDesc.Format &&
				outputDesc.Width >= mainDesc.Width &&
				outputDesc.Height >= mainDesc.Height) {
				context->OMSetRenderTargets(0, nullptr, nullptr);
				D3D11_BOX sourceBox{ 0, 0, 0, mainDesc.Width, mainDesc.Height, 1 };
				context->CopySubresourceRegion(outputTexture, 0, 0, 0, 0, main.texture, 0, &sourceBox);
				context->OMSetRenderTargets(1, &outputRTV, outputDSV);
			} else {
				static bool loggedCopyMismatch = false;
				if (!loggedCopyMismatch) {
					logger::warn(
						"[Upscaling] Dynamic-resolution upsample replacement could not copy output: main={}x{} fmt={} samples={} target={}x{} fmt={} samples={}",
						mainDesc.Width,
						mainDesc.Height,
						static_cast<uint32_t>(mainDesc.Format),
						mainDesc.SampleDesc.Count,
						outputDesc.Width,
						outputDesc.Height,
						static_cast<uint32_t>(outputDesc.Format),
						outputDesc.SampleDesc.Count);
					loggedCopyMismatch = true;
				}
			}
		}
		context->OMSetRenderTargets(1, &outputRTV, outputDSV);

		logDecision("replaced-vendor-upscale");
		releaseRefs();
		if (globals::game::stateUpdateFlags)
			globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
		return true;
	}

	return false;
}

void Upscaling::UpsampleDynamicResolution_Render::thunk(void* a_imageSpaceShader, void* a_shape, void* a_param)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISUpsampleDynamicResolution", DynamicResolutionUpsampleStage::Render))
		return;

	func(a_imageSpaceShader, a_shape, a_param);
}

void Upscaling::FullScreenVR_Render::thunk(void* a_imageSpaceShader, void* a_shape, void* a_param)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISFullScreenVR", DynamicResolutionUpsampleStage::Render))
		return;

	func(a_imageSpaceShader, a_shape, a_param);
}

void Upscaling::CopyDynamicFetchDisabled_Render::thunk(void* a_imageSpaceShader, void* a_shape, void* a_param)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISCopyDynamicFetchDisabled", DynamicResolutionUpsampleStage::Render))
		return;

	func(a_imageSpaceShader, a_shape, a_param);
}

void Upscaling::HDRTonemapBlendCinematicFade_Render::thunk(void* a_imageSpaceShader, void* a_shape, void* a_param)
{
	auto& upscaling = globals::features::upscaling;
	LogVRPresentationAroundCall(
		upscaling,
		VRPresentationDiagnosticSlot::FadeRender,
		"ISHDRTonemapBlendCinematicFade",
		"Render:before",
		"Render:after",
		true,
		[&]() { func(a_imageSpaceShader, a_shape, a_param); });
}

void Upscaling::TemporalAAUI_Render::thunk(void* a_imageSpaceShader, void* a_shape, void* a_param)
{
	auto& upscaling = globals::features::upscaling;
	LogVRPresentationAroundCall(
		upscaling,
		VRPresentationDiagnosticSlot::TemporalAAUIRender,
		"ISTemporalAA_UI",
		"Render:before",
		"Render:after",
		true,
		[&]() { func(a_imageSpaceShader, a_shape, a_param); });
}

void Upscaling::LightingCompositeMenu_Render::thunk(void* a_imageSpaceShader, void* a_shape, void* a_param)
{
	auto& upscaling = globals::features::upscaling;
	LogVRPresentationAroundCall(
		upscaling,
		VRPresentationDiagnosticSlot::LightingCompositeMenuRender,
		"ISLightingCompositeMenu",
		"Render:before",
		"Render:after",
		true,
		[&]() { func(a_imageSpaceShader, a_shape, a_param); });
}

void Upscaling::UpsampleDynamicResolution_Dispatch::thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISUpsampleDynamicResolution", DynamicResolutionUpsampleStage::Dispatch))
		return;

	func(a_imageSpaceShader, a1, a2, a3);
}

void Upscaling::FullScreenVR_Dispatch::thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISFullScreenVR", DynamicResolutionUpsampleStage::Dispatch))
		return;

	func(a_imageSpaceShader, a1, a2, a3);
}

void Upscaling::CopyDynamicFetchDisabled_Dispatch::thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISCopyDynamicFetchDisabled", DynamicResolutionUpsampleStage::Dispatch))
		return;

	func(a_imageSpaceShader, a1, a2, a3);
}

void Upscaling::HDRTonemapBlendCinematicFade_Dispatch::thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
{
	auto& upscaling = globals::features::upscaling;
	LogVRPresentationAroundCall(
		upscaling,
		VRPresentationDiagnosticSlot::FadeDispatch,
		"ISHDRTonemapBlendCinematicFade",
		"Dispatch:before",
		"Dispatch:after",
		true,
		[&]() { func(a_imageSpaceShader, a1, a2, a3); });
}

void Upscaling::TemporalAAUI_Dispatch::thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
{
	auto& upscaling = globals::features::upscaling;
	LogVRPresentationAroundCall(
		upscaling,
		VRPresentationDiagnosticSlot::TemporalAAUIDispatch,
		"ISTemporalAA_UI",
		"Dispatch:before",
		"Dispatch:after",
		true,
		[&]() { func(a_imageSpaceShader, a1, a2, a3); });
}

void Upscaling::LightingCompositeMenu_Dispatch::thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
{
	auto& upscaling = globals::features::upscaling;
	LogVRPresentationAroundCall(
		upscaling,
		VRPresentationDiagnosticSlot::LightingCompositeMenuDispatch,
		"ISLightingCompositeMenu",
		"Dispatch:before",
		"Dispatch:after",
		true,
		[&]() { func(a_imageSpaceShader, a1, a2, a3); });
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	globals::features::upscaling.ConfigureTAA();
	func(a_state);
	globals::features::upscaling.ConfigureUpscaling(a_state);
	globals::features::upscaling.UpdateAAVRSState();
}

void Upscaling::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	auto& upscaling = globals::features::upscaling;
	const bool logPresentationDiagnostics = globals::game::isVR;
	if (logPresentationDiagnostics) {
		LogVRPresentationPassDiagnostics(
			upscaling,
			VRPresentationDiagnosticSlot::MenuDraw,
			"MenuManagerDrawInterface",
			"before-PostDisplay",
			true);
	}
	upscaling.PostDisplay();
	if (logPresentationDiagnostics) {
		LogVRPresentationPassDiagnostics(
			upscaling,
			VRPresentationDiagnosticSlot::MenuDraw,
			"MenuManagerDrawInterface",
			"after-PostDisplay",
			false);
	}
	if (logPresentationDiagnostics) {
		LogVRPresentationPassDiagnostics(
			upscaling,
			VRPresentationDiagnosticSlot::MenuDraw,
			"MenuManagerDrawInterface",
			"before-menu-draw",
			false);
	}
	upscaling.CaptureKnownGameMenuSceneBeforeMenuDraw();

	func(a1);
	upscaling.CaptureKnownGameMenuBackgroundAfterMenuDraw();

	if (globals::game::isVR && upscaling.IsPerfModePresentationActive()) {
		const bool observedProjectedMenu = IsCurrentRenderTargetVRObservedMenuPresentationSeedTexture();
		if (observedProjectedMenu) {
			ExtendVRObservedProjectedMenuTail();
			ExtendVRMenuPresentationTail(kVRObservedMenuPresentationTailFrames);
		} else if (IsVRObservedProjectedMenuTailActive(globals::state) &&
		           IsCurrentRenderTargetVRObservedMenuPresentationFollowTexture()) {
			ExtendVRMenuPresentationTail(kVRObservedMenuPresentationTailFrames);
		}
	}
	if (logPresentationDiagnostics) {
		LogVRPresentationPassDiagnostics(
			upscaling,
			VRPresentationDiagnosticSlot::MenuDraw,
			"MenuManagerDrawInterface",
			"after-menu-draw",
			false);
	}
}

void Upscaling::Main_PostProcessing::thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
{
	auto& upscaling = globals::features::upscaling;
	if (globals::game::isVR) {
		LogVRPresentationPassDiagnostics(
			upscaling,
			VRPresentationDiagnosticSlot::MainPostProcessing,
			"Main_PostProcessing",
			"entry",
			false);
	}
	upscaling.ApplyAAVRSVisualization();
	upscaling.DisableAAVRSState();
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
	const bool menuPresentationContext =
		vendorMethodSelected &&
		globals::game::isVR &&
		(vrScenePresentationBlockActive || loadingTransitionTailActive);
	const bool fullResolutionMenuPresentation = menuPresentationContext;
	const bool loadingTransitionMenuPresentation =
		fullResolutionMenuPresentation &&
		(IsMainOrLoadingMenuContextActive() || loadingTransitionTailActive);
	const bool runNativeVendorAAInMenu =
		fullResolutionMenuPresentation &&
		upscaling.GetRuntimeQualityMode() == 0 &&
		!loadingTransitionMenuPresentation;
	const bool vendorDynamicResolutionActive = vendorMethodSelected && upscaling.IsUpscalingActive();
	const bool presentationUpscalingActive = upscaling.IsPresentationUpscalingActive();
	const bool submitPathDisabledForVendor =
		vendorMethodSelected &&
		globals::game::isVR &&
		!menuPresentationContext &&
		!upscaling.IsPerfModeActive() &&
		!IsSubmitStagePathEnabled();
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

	if (menuPresentationContext && !runNativeVendorAAInMenu && !presentationUpscalingActive) {
		if (upscaling.IsPerfModeActive())
			globals::features::vr.InstallSubmitHook();

		if (upscaling.ShouldUseFrameGenerationThisFrame())
			upscaling.CopySharedD3D12Resources();

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

		if (fullResolutionMenuPresentation)
			upscaling.PrepareFullResolutionPostProcessing();
		else
			upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		func(a_this, a3, a_target, a_4, a_5);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		if (fullResolutionMenuPresentation)
			upscaling.PrepareFullResolutionPostProcessing();
		else
			upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
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

	const bool vrProtectedFullSizeTarget = globals::game::isVR && IsCurrentRenderTargetVRProtectedFullSizeTexture();
	if (!runtimeData.dynamicResolutionLock && !vrProtectedFullSizeTarget) {
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
