#pragma once
#include "Buffer.h"
#include "Features/VR/OpenVRDetection.h"
#include "Menu.h"
#include "OverlayFeature.h"
#include "Utils/Input.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <d3d11.h>
#include <d3d11_1.h>
#include <imgui_impl_dx11.h>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <openvr.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

using namespace DirectX::SimpleMath;

namespace RE
{
	class BSOpenVR;
}

namespace VRDepthCullingTemporal
{
	enum class Mode;
}

// Backwards compatibility aliases
using ControllerDevice = InputDeviceType;
using ButtonCombo = InputCombo;

/**
 * @brief Main VR feature class providing VR-specific optimizations and overlay UI system
 *
 * This class extends OverlayFeature to provide comprehensive VR support including:
 * - Performance optimizations (depth buffer culling, occlusion culling)
 * - VR overlay system for in-game UI interaction
 * - Controller input processing and button combo mapping
 * - Overlay positioning and manipulation (HMD-relative, controller-relative, fixed world)
 * - Drag-and-drop overlay repositioning
 *
 * The VR class follows the singleton pattern and integrates with the OpenVR API
 * to provide seamless VR experience within the CSX framework.
 *
 * @example
 * ```cpp
 * // Get the VR singleton instance
 * VR* vr = VR::GetSingleton();
 *
 * // Check if VR is supported
 * if (vr->SupportsVR()) {
 *     // Configure VR settings
 *     vr->settings.EnableDepthBufferCulling = true;
 *     vr->settings.VRMenuScale = 1.2f;
 * }
 * ```
 */
struct VR : OverlayFeature
{
public:
	//=============================================================================
	// NESTED TYPES AND CONSTANTS
	//=============================================================================

	/**
	 * @brief Configuration constants for VR feature defaults and limits
	 *
	 * These constants define the default values and valid ranges for various
	 * VR settings to ensure consistent behavior and prevent invalid configurations.
	 */
	struct Config
	{
		static constexpr int kOverlayWidth = 1920;
		static constexpr int kOverlayHeight = 1080;
		static constexpr float kOverlayAspect = static_cast<float>(kOverlayHeight) / static_cast<float>(kOverlayWidth);
		// Logical HMD menu layout is slightly less tall than the backing texture so the
		// panel can fill more horizontal space while preserving a stable in-headset ratio.
		static constexpr float kHMDMenuHeightScale = 1.4f;
		static constexpr float kHMDMenuAspect = kOverlayAspect * kHMDMenuHeightScale;
		// HMD presentation is intentionally taller than the controller texture so the menu does not read as a wide panel in-headset.
		static constexpr float kHMDOverlayHeightScale = 1.5f;
		static constexpr int kHMDOverlayWidth = kOverlayWidth;
		static constexpr int kHMDOverlayHeight = static_cast<int>(kOverlayHeight * kHMDOverlayHeightScale);
		static constexpr float kHMDOverlayAspect = static_cast<float>(kHMDOverlayHeight) / static_cast<float>(kHMDOverlayWidth);

		static inline Matrix CreateOverlayScaleMatrix(float scale, float aspect = kOverlayAspect)
		{
			return Matrix::CreateScale(scale, scale * aspect, scale);
		}

		static inline Matrix CreateHMDOverlayScaleMatrix(float scale)
		{
			return CreateOverlayScaleMatrix(scale, kHMDOverlayAspect);
		}

		static constexpr float kDefaultMenuScale = 2.0f;      ///< Default overlay scale factor
		static constexpr float kMinMenuScale = 0.5f;          ///< Minimum allowed overlay scale
		static constexpr float kMaxMenuScale = 2.0f;          ///< Maximum allowed overlay scale
		static constexpr float kMinMenuOffset = -5.0f;        ///< Minimum configurable overlay offset in metres
		static constexpr float kMaxMenuOffset = 5.0f;         ///< Maximum configurable overlay offset in metres
		static constexpr float kDefaultComboTimeout = 3.0f;   ///< Default timeout for button combos (seconds)
		static constexpr float kDefaultMouseDeadzone = 0.1f;  ///< Default thumbstick deadzone for mouse input
		static constexpr float kDefaultMouseSpeed = 10.0f;    ///< Default mouse speed multiplier
		static constexpr float kDefaultWandAimPitchTrimDegrees = 0.0f;
		static constexpr float kMinWandAimPitchTrimDegrees = -90.0f;
		static constexpr float kMaxWandAimPitchTrimDegrees = 90.0f;
		static constexpr int kDefaultAutoHideSeconds = 30;  ///< Default auto-hide timeout for overlay messages
		static constexpr int kMaxAutoHideSeconds = 300;     ///< Maximum auto-hide timeout (5 minutes)
		static constexpr float kMinStereoBlendDepthSigma = 0.001f;
		static constexpr float kMaxStereoBlendDepthSigma = 0.1f;
		static constexpr float kDefaultStereoBlendDepthSigma = 0.01f;
		static constexpr float kMinStereoBlendMaxFactor = 0.0f;
		static constexpr float kMaxStereoBlendMaxFactor = 0.25f;
		static constexpr float kDefaultStereoBlendMaxFactor = 0.05f;
		static constexpr float kMinStereoBlendColorThreshold = 0.0f;
		static constexpr float kMaxStereoBlendColorThreshold = 0.2f;
		static constexpr float kDefaultStereoBlendColorThreshold = 0.02f;

		// Default HMD overlay offset values (in meters, relative to HMD)
		static constexpr float kDefaultHMDOffsetX = 0.26f;   ///< Default horizontal offset from HMD
		static constexpr float kDefaultHMDOffsetY = -0.04f;  ///< Default vertical offset from HMD
		static constexpr float kDefaultHMDOffsetZ = -2.25f;  ///< Default depth offset from HMD

		// Default controller overlay offset values (in meters, relative to controller)
		static constexpr float kDefaultControllerOffsetX = 0.22f;  ///< Default horizontal offset from controller
		static constexpr float kDefaultControllerOffsetY = 0.15f;  ///< Default vertical offset from controller
		static constexpr float kDefaultControllerOffsetZ = 0.20f;  ///< Default depth offset from controller

		[[nodiscard]] static float SanitizeMenuOffset(float a_value, float a_fallback) noexcept
		{
			return std::isfinite(a_value) ?
			           std::clamp(a_value, kMinMenuOffset, kMaxMenuOffset) :
			           a_fallback;
		}
	};

	//=============================================================================
	// FEATURE BASE CLASS OVERRIDES
	//=============================================================================

	virtual inline std::string GetName() override { return "VR"; }
	virtual inline std::string GetShortName() override { return "VR"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Provides VR-specific optimizations and enhancements for CSX, improving performance and visual quality in virtual reality environments.",
			{ "Depth buffer culling optimization for VR performance",
				"Configurable occlusion culling parameters",
				"VR-specific rendering pipeline improvements",
				"Performance optimizations for dual-eye rendering",
				"Enhanced VR compatibility across all shader features" }
		};
	}

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	virtual bool SupportsVR() override { return true; }
	virtual bool IsCore() const override { return true; }

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;
	virtual void EarlyPrepass() override;

	void UpdateDepthBufferCulling();
	/** Select one effective depth-culling policy and synchronize persisted toggles. */
	void SetDepthCullingMode(VRDepthCullingTemporal::Mode a_mode);
	/** Return the effective policy represented by the persisted toggles. */
	[[nodiscard]] VRDepthCullingTemporal::Mode GetDepthCullingMode() const;
	/** Select Performance Mode and clear Legacy Mode when enabled. */
	void SetDepthCullingPerformanceMode(bool a_enabled);
	/** Select the native-result Legacy path and clear Performance Mode when enabled. */
	void SetDepthCullingLegacyMode(bool a_enabled);
	/** Normalize persisted toggles and publish one effective temporal policy. */
	void ApplyDepthCullingMode();
	void TryApplyDepthBufferCullingCacheRefresh();
	void DrawStereoBlend();
	bool EnsureStereoBlendResources();
	static bool AnyScreenSpaceEffectActive();

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool a_advanced) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override;
	virtual bool IsPerformanceCostMeasurementEnabled() const override;
	virtual bool UsesTotalPerformanceCostMeasurement() const override { return true; }
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override;
	virtual json CapturePerformanceCostMeasurementState() const override;
	virtual void RestorePerformanceCostMeasurementState(const json& a_state) override;

	virtual std::string_view GetCategory() const override { return FeatureCategories::kUtility; }

	//=============================================================================
	// OVERLAY FEATURE OVERRIDES
	//=============================================================================

	virtual void DrawOverlay() override;
	virtual bool IsOverlayVisible() const override;

	//=============================================================================
	// SETTINGS STRUCTURE
	//=============================================================================

	/**
	 * @brief Configuration settings for the VR feature
	 *
	 * This structure contains all user-configurable settings for VR functionality,
	 * including performance optimizations, overlay positioning, input mapping, and
	 * visual customization options. Settings are automatically validated and clamped
	 * to valid ranges when loaded or modified.
	 */
	struct Settings
	{
		static constexpr uint32_t kButtonBY = 1;
		static constexpr uint32_t kButtonGrip = 2;
		static constexpr uint32_t kButtonXA = 7;
		static constexpr uint32_t kButtonJoystickTrigger = 32;

		// Performance optimization settings
		bool EnableDepthBufferCullingExterior = true;  ///< Master depth-culling option; enabled in exteriors
		bool EnableDepthBufferCullingInterior = true;  ///< Also enable depth culling in interiors
		bool DepthCullingPerformanceMode = false;      ///< Accept native stale results instead of bounded recovery
		bool DepthCullingLegacyMode = false;           ///< Use native results without temporal pose capture or recovery
		float MinOccludeeBoxExtent = 10.0f;            ///< Minimum bounding box size for occlusion culling

		// Post-composite VR stereo consistency pass. Default-off because it is a global final-color blend.
		bool EnableStereoBlend = false;
		float StereoBlendDepthSigma = Config::kDefaultStereoBlendDepthSigma;
		float StereoBlendMaxFactor = Config::kDefaultStereoBlendMaxFactor;
		float StereoBlendColorThreshold = Config::kDefaultStereoBlendColorThreshold;

		// Foveated detail budgets. These use the active Upscaling FOV profile.
		bool EnableLightingFoveation = true;
		bool EnableLightingFoveationHardCutoff = false;
		bool EnableSSRFoveation = true;
		bool EnableSSRFoveationHardCutoff = false;
		bool EnableWaterParallaxFoveation = true;
		bool EnableWaterParallaxFoveationHardCutoff = false;
		bool EnableWetternessFoveation = true;
		bool EnableWetternessFoveationHardCutoff = false;
		bool EnableDynamicCubemapFoveation = true;
		bool EnableDynamicCubemapVisibilityThrottle = false;

		// VR Menu Overlay positioning settings
		bool UnlockMenuPositionAndSize = false;         ///< Allow custom desktop and headset menu placement
		float VRMenuScale = Config::kDefaultMenuScale;  ///< Scale factor for overlay UI (0.5-2.0)
		int VRMenuPositioningMethod = 1;                ///< 0 = HMD relative, 1 = Fixed world position

		/**
		 * @brief Defines how overlays are attached and positioned in VR space
		 */
		enum class OverlayAttachMode
		{
			HMDOnly = 0,         ///< Overlay attached to HMD only
			ControllerOnly = 1,  ///< Overlay attached to controller only
			Both = 2,            ///< Overlay can be attached to both HMD and controller
			None = 3             ///< Overlay display disabled
		};
		OverlayAttachMode attachMode = OverlayAttachMode::HMDOnly;              ///< Current overlay attachment mode
		ControllerDevice VRMenuAttachController = ControllerDevice::Secondary;  ///< Which controller to attach overlay to

		// HMD overlay offset settings (in meters)
		float VRMenuOffsetX = Config::kDefaultHMDOffsetX;  ///< Horizontal offset from HMD
		float VRMenuOffsetY = Config::kDefaultHMDOffsetY;  ///< Vertical offset from HMD
		float VRMenuOffsetZ = Config::kDefaultHMDOffsetZ;  ///< Depth offset from HMD

		// Controller overlay offset settings (in meters)
		float VRMenuControllerOffsetX = Config::kDefaultControllerOffsetX;  ///< Horizontal offset from controller
		float VRMenuControllerOffsetY = Config::kDefaultControllerOffsetY;  ///< Vertical offset from controller
		float VRMenuControllerOffsetZ = Config::kDefaultControllerOffsetZ;  ///< Depth offset from controller

		// Input and interaction settings
		bool VRMenuControllerDiagnosticsTestMode = false;     ///< Enable controller diagnostics mode
		float mouseDeadzone = Config::kDefaultMouseDeadzone;  ///< Thumbstick deadzone for mouse input (0.0-1.0)
		float mouseSpeed = Config::kDefaultMouseSpeed;        ///< Mouse speed multiplier (0.1-50.0)

		enum class MenuOverlayPath
		{
			Auto = 0,
			IVROverlay = 1,
			InScene = 2
		};
		MenuOverlayPath menuOverlayPath = MenuOverlayPath::Auto;  ///< Runtime path used to present the menu in the headset
		bool KeepDesktopWindowFocusedForVRMenu = true;            ///< Keep the game window centered and foregrounded while the VR menu is open
		bool StabilizeRenderScaleDesktopMirror = false;           ///< Publish render-scale eye outputs to the desktop mirror

		// CSX menu navigation settings
		bool UseRuntimeDefaultMenuNavigation = true;                              ///< Use mouse navigation by default until the user selects a mode explicitly
		bool EnableWandPointing = true;                                           ///< True uses wand/ray-cast navigation, false uses mouse/thumbstick navigation
		float WandAimPitchTrimDegrees = Config::kDefaultWandAimPitchTrimDegrees;  ///< Optional local pitch trim after resolving the runtime aim component

		// Visual customization
		std::array<float, 4> dragHighlightColor = { 1.0f, 1.0f, 0.0f, 0.3f };  ///< RGBA color for drag highlight

		static std::vector<ButtonCombo> DefaultVRMenuOpenKeys()
		{
			return {
				ButtonCombo::Primary(kButtonXA),
				ButtonCombo::Primary(kButtonBY)
			};
		}

		static std::vector<ButtonCombo> DefaultVRMenuCloseKeys()
		{
			return {
				ButtonCombo::Both(kButtonGrip)
			};
		}

		static std::vector<ButtonCombo> DefaultVROverlayOpenKeys()
		{
			return {};
		}

		static std::vector<ButtonCombo> DefaultVROverlayCloseKeys()
		{
			return {};
		}

		// Key binding configurations
		std::vector<ButtonCombo> VRMenuOpenKeys = DefaultVRMenuOpenKeys();          ///< Button combos to open VR menu
		std::vector<ButtonCombo> VRMenuCloseKeys = DefaultVRMenuCloseKeys();        ///< Button combos to close VR menu
		std::vector<ButtonCombo> VROverlayOpenKeys = DefaultVROverlayOpenKeys();    ///< Button combos to show the Performance Overlay
		std::vector<ButtonCombo> VROverlayCloseKeys = DefaultVROverlayCloseKeys();  ///< Button combos to hide the Performance Overlay

		// General interaction settings
		float comboTimeout = Config::kDefaultComboTimeout;       ///< Timeout for button combo sequences (1.0-10.0 seconds)
		int kAutoHideSeconds = Config::kDefaultAutoHideSeconds;  ///< Auto-hide timeout for overlay messages (>0 shows overlay, <=0 hides it)
		bool EnableDragToReposition = false;                     ///< Allow drag-and-drop overlay repositioning

		float VRMenuAutoResetDistance = 1000.0f;  // Default: 1000 units ≈ 14.3 meters

		/**
		 * @brief Validates if the current menu scale is within acceptable range
		 * @return true if scale is between kMinMenuScale and kMaxMenuScale
		 */
		bool IsMenuScaleValid() const
		{
			return VRMenuScale >= Config::kMinMenuScale && VRMenuScale <= Config::kMaxMenuScale;
		}

		/**
		 * @brief Validates if the current attach mode is valid
		 * @return true if attach mode is within valid enum range
		 */
		bool IsAttachModeValid() const
		{
			return attachMode >= OverlayAttachMode::HMDOnly && attachMode <= OverlayAttachMode::None;
		}

		/**
		 * @brief Clamps all settings to their valid ranges
		 *
		 * This method ensures all numeric settings are within acceptable bounds,
		 * automatically correcting any out-of-range values that might have been
		 * loaded from configuration files or set programmatically.
		 */
		void ClampToValidRanges()
		{
			VRMenuScale = std::isfinite(VRMenuScale) ?
			                  std::clamp(VRMenuScale, Config::kMinMenuScale, Config::kMaxMenuScale) :
			                  Config::kDefaultMenuScale;
			VRMenuPositioningMethod = std::clamp(VRMenuPositioningMethod, 0, 1);
			attachMode = static_cast<OverlayAttachMode>(std::clamp(
				static_cast<int>(attachMode),
				static_cast<int>(OverlayAttachMode::HMDOnly),
				static_cast<int>(OverlayAttachMode::None)));
			VRMenuAttachController = static_cast<ControllerDevice>(std::clamp(
				static_cast<int>(VRMenuAttachController),
				static_cast<int>(ControllerDevice::Primary),
				static_cast<int>(ControllerDevice::Secondary)));
			VRMenuOffsetX = Config::SanitizeMenuOffset(VRMenuOffsetX, Config::kDefaultHMDOffsetX);
			VRMenuOffsetY = Config::SanitizeMenuOffset(VRMenuOffsetY, Config::kDefaultHMDOffsetY);
			VRMenuOffsetZ = Config::SanitizeMenuOffset(VRMenuOffsetZ, Config::kDefaultHMDOffsetZ);
			VRMenuControllerOffsetX = Config::SanitizeMenuOffset(VRMenuControllerOffsetX, Config::kDefaultControllerOffsetX);
			VRMenuControllerOffsetY = Config::SanitizeMenuOffset(VRMenuControllerOffsetY, Config::kDefaultControllerOffsetY);
			VRMenuControllerOffsetZ = Config::SanitizeMenuOffset(VRMenuControllerOffsetZ, Config::kDefaultControllerOffsetZ);
			mouseDeadzone = std::clamp(mouseDeadzone, 0.0f, 1.0f);
			mouseSpeed = std::clamp(mouseSpeed, 0.1f, 50.0f);
			WandAimPitchTrimDegrees = std::clamp(
				WandAimPitchTrimDegrees,
				Config::kMinWandAimPitchTrimDegrees,
				Config::kMaxWandAimPitchTrimDegrees);
			comboTimeout = std::clamp(comboTimeout, 1.0f, 10.0f);
			kAutoHideSeconds = std::clamp(kAutoHideSeconds, 0, Config::kMaxAutoHideSeconds);
			menuOverlayPath = std::clamp(menuOverlayPath, MenuOverlayPath::Auto, MenuOverlayPath::InScene);
			StereoBlendDepthSigma = std::clamp(StereoBlendDepthSigma, Config::kMinStereoBlendDepthSigma, Config::kMaxStereoBlendDepthSigma);
			StereoBlendMaxFactor = std::clamp(StereoBlendMaxFactor, Config::kMinStereoBlendMaxFactor, Config::kMaxStereoBlendMaxFactor);
			StereoBlendColorThreshold = std::clamp(StereoBlendColorThreshold, Config::kMinStereoBlendColorThreshold, Config::kMaxStereoBlendColorThreshold);
		}
	};

	Settings settings;  ///< Current VR configuration settings

	//=============================================================================
	// VR-SPECIFIC PUBLIC API
	//=============================================================================

	void UpdateVROverlayPosition();
	bool UseFixedWorldMenuPositioning() const;
	/** Return the locked or saved headset presentation mode. */
	[[nodiscard]] Settings::OverlayAttachMode GetEffectiveMenuAttachMode() const;
	/** Return the controller selected for the effective headset presentation. */
	[[nodiscard]] ControllerDevice GetEffectiveMenuAttachController() const;
	/** Return the locked default or saved custom headset menu scale. */
	[[nodiscard]] float GetEffectiveMenuScale() const;
	/** Return the locked or saved HMD-relative offset. */
	[[nodiscard]] Vector3 GetEffectiveHMDMenuOffset() const;
	/** Return the controller-relative offset for the effective presentation. */
	[[nodiscard]] Vector3 GetEffectiveControllerMenuOffset() const;
	/** @brief Enables or restores the safe lock for desktop and headset menu placement. */
	void SetMenuLayoutUnlocked(bool a_unlocked);
	void UpdateVROverlayControllerPosition();

	void ProcessVREvents(std::vector<Menu::KeyEvent>& vrEvents);

	// Wand pointing methods
	enum class OverlayType
	{
		HMD,
		Controller
	};
	struct PresentedMenuSurface
	{
		Vector3 topLeft = Vector3::Zero;
		Vector3 topRight = Vector3::Zero;
		Vector3 bottomLeft = Vector3::Zero;
		bool valid = false;
	};
	bool ComputeWandIntersection(vr::TrackedDeviceIndex_t controllerIndex, ImVec2& outUV);
	bool ComputeWandIntersectionForOverlayType(OverlayType type, vr::TrackedDeviceIndex_t controllerIndex, ImVec2& outUV);
	void PublishPresentedMenuSurface(OverlayType type, const Matrix& worldMatrix);
	bool TryGetPresentedMenuSurface(OverlayType type, PresentedMenuSurface& outSurface) const;
	void InvalidatePresentedMenuSurfaces();
	ControllerDevice GetWandPointingControllerDevice() const;
	vr::TrackedDeviceIndex_t GetWandPointingControllerIndex() const;
	void UpdateCursorFromWandPointing(bool a_forceCursorUpdate = false, ControllerDevice a_preferredController = ControllerDevice::Both);
	bool UpdateWandPoseOwnershipSignal();
	bool IsWandControllerIntersecting(ControllerDevice a_controller) const;
	bool TryCaptureWandController(ControllerDevice a_controller);
	void ReleaseWandControllerCapture(ControllerDevice a_controller);
	/**
	 * @brief Returns the wand that generated this ImGui frame's left click.
	 * @return Both when the click has no unambiguous VR provenance.
	 */
	[[nodiscard]] ControllerDevice GetImGuiLeftClickWandController() const;
	/** @brief Discards click provenance when ImGui's queued input events are discarded. */
	void DiscardQueuedImGuiClickOwners();
	void TriggerWandHaptic(ControllerDevice a_controller, float a_duration);
	void UpdateWandHoverFeedback();
	void ResetWandPointingRuntimeState();
	void UpdateOverlayMenuStateFromInput();
	void ProcessVRButtonEvent(const Menu::KeyEvent& event);
	void UpdateControllerState(const Menu::KeyEvent& event);
	void ProcessThumbstickScroll(RE::VRControllerState& controllerState, size_t thumbstickIndex, float deadzone, ImGuiIO& io);
	void ProcessThumbstickScrollMouseNavigation(RE::VRControllerState& controllerState, size_t thumbstickIndex, float deadzone, ImGuiIO& io);
	void ProcessControllerInputForWandPointingPath(bool testMode, float mouseDeadzone, ImGuiIO& io);
	void ProcessControllerInputForMouseNavigationPath(bool testMode, float mouseDeadzone, float mouseSpeed, ImGuiIO& io);
	void ProcessControllerInputForImGui();
	void ResetComboRecordingState();
	void ReleaseMenuImGuiInputState();
	void ResetMenuInputRuntimeState();
	void RequestFixedWorldMenuReanchor();

	void EnsureOverlayInitialized();
	void DestroyOverlay();
	bool GetMenuCanvasSize(uint32_t& a_width, uint32_t& a_height) const;
	void RecreateOverlayTexturesIfNeeded(bool needsControllerTexture = true);
	void SubmitOverlayFrame();
	void SubmitCaptureIndicator(bool a_visible);
	void HideOverlaysIfPresent();
	void UpdateMenuDesktopWindowManagement(bool force = false);
	void ReleaseMenuDesktopWindowManagement();

	/**
	 * @brief Context for rendering VR overlays with render target management
	 */
	struct OverlayRenderContext
	{
		vr::IVROverlay* gameOverlay;
		vr::IVROverlay* cleanOverlay;
		RE::BSOpenVR* openvr;
		ID3D11RenderTargetView* oldRTV = nullptr;
		float clearColor[4] = { 0, 0, 0, 0 };

		bool IsValid() const;

		void SaveRenderTarget()
		{
			globals::d3d::context->OMGetRenderTargets(1, &oldRTV, nullptr);
		}

		void RestoreRenderTarget()
		{
			globals::d3d::context->OMSetRenderTargets(1, &oldRTV, nullptr);
			if (oldRTV) {
				oldRTV->Release();
				oldRTV = nullptr;
			}
		}

		void RenderToTexture(ID3D11RenderTargetView* targetRTV)
		{
			globals::d3d::context->OMSetRenderTargets(1, &targetRTV, nullptr);
			globals::d3d::context->ClearRenderTargetView(targetRTV, clearColor);
			ImGui::Render();
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}
	};

	void SubmitHMDOverlay(OverlayRenderContext& context);
	void SubmitControllerOverlay(OverlayRenderContext& context);
	void HideAllOverlays(vr::IVROverlay* gameOverlay);
	bool ShouldShowAutoHideOverlay() const;
	void MarkAutoHideOverlayPresented();
	bool ShouldPresentOverlayInHeadset() const;
	bool ShouldUseInSceneOverlay() const;
	bool ShouldRenderCaptureIndicatorInScene() const
	{
		return IsOpenCompositeRuntime() && captureIndicatorVisible.load(std::memory_order_acquire);
	}
	bool CanOpenMenuFromWorld() const;

	void UpdateOverlayDrag();
	bool CanPerformDrag();
	void UpdateActiveDrag();
	void TryStartNewDrag();
	void SetFixedOverlayToCurrentHMD();
	void UpdateFixedWorldPositioning();
	void UpdateFixedWorldPositioning(const Matrix& a_hmdWorld);
	bool ShouldHighlightOverlayWindow() const { return overlayDragState.dragging; }

	//=============================================================================
	// PUBLIC MEMBER VARIABLES
	//=============================================================================

	// OpenVR overlay handles and DirectX 11 rendering resources
	vr::VROverlayHandle_t menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
	vr::VROverlayHandle_t menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
	vr::VROverlayHandle_t captureIndicatorOverlayHandle = vr::k_ulOverlayHandleInvalid;
	std::atomic_bool captureIndicatorVisible{ false };
	winrt::com_ptr<ID3D11Texture2D> captureIndicatorTexture;
	winrt::com_ptr<ID3D11Texture2D> menuTexture;
	winrt::com_ptr<ID3D11RenderTargetView> menuRTV;
	winrt::com_ptr<ID3D11ShaderResourceView> menuSamplingSRV;
	winrt::com_ptr<ID3D11Texture2D> menuControllerTexture;
	winrt::com_ptr<ID3D11RenderTargetView> menuControllerRTV;
	winrt::com_ptr<ID3D11ShaderResourceView> menuControllerSamplingSRV;
	mutable double autoHideOverlayStartTimeSecs = 0.0;  ///< Starts after the welcome frame can first reach the headset

	// Post-composite stereo blend resources, created lazily when the advanced option is enabled.
	winrt::com_ptr<ID3D11ComputeShader> stereoBlendCS;
	eastl::unique_ptr<Texture2D> stereoBlendCopyTex = nullptr;
	eastl::unique_ptr<ConstantBuffer> stereoBlendCB = nullptr;

	struct alignas(16) StereoBlendCB
	{
		float FrameDim[2];
		float RcpFrameDim[2];
		float DepthSigma;
		float MaxBlendFactor;
		float ColorDiffThreshold;
		float pad;
	};
	STATIC_ASSERT_ALIGNAS_16(StereoBlendCB);

	// Engine hook integration points
	bool* gDepthBufferCulling = nullptr;
	float* gMinOccludeeBoxExtent = nullptr;
	std::atomic<bool> depthCullingCacheRefreshPending = false;
	std::atomic<bool> depthCullingCacheRefreshCompleted = false;

	// VR Controller state and logging
	struct VRControllerEventLog
	{
		int device;
		int keyCode;
		int value;
		bool pressed;
		double heldTime;
		std::string heldSource;
		float thumbstickX = 0.0f;
		float thumbstickY = 0.0f;
		std::string controllerRole;
	};

	std::vector<VRControllerEventLog> vrControllerEventLog;
	RE::VRControllerState primaryControllerState;
	RE::VRControllerState secondaryControllerState;
	bool lastKnownLeftHandedMode = false;

	struct OverlayWorldPosition
	{
		Matrix m = Matrix::Identity;
		bool initialized = false;
	} fixedWorldOverlayPosition;
	OverlayWorldPosition savedUnlockedFixedWorldOverlayPosition;  ///< Custom anchor retained while the recoverable lock is active
	bool fixedWorldOverlayReanchorRequested = true;

	struct OverlayDragState
	{
		bool dragging = false;
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		bool isPrimary = false;
		bool isSecondary = false;
		Matrix initialControllerMatrix = Matrix::Identity;
		Matrix initialOverlayMatrix = Matrix::Identity;
		Matrix grabOffset = Matrix::Identity;
		bool intersecting = false;

		enum class DragMode
		{
			None,
			FixedWorld,
			HMD,
			Controller
		} mode = DragMode::None;

		Vector3 initialHMDOffset = Vector3::Zero;
		Vector3 initialControllerOffset = Vector3::Zero;
		float initialHMDScale = 1.0f;
		Matrix startControllerMatrix = Matrix::Identity;
	} overlayDragState;

	struct ComboSequence
	{
		std::vector<uint32_t> sequence;
		double startTime = 0.0;
		size_t currentIndex = 0;
		bool active = false;
	};
	ComboSequence menuOpenCombo;
	ComboSequence menuCloseCombo;

	enum class ComboType
	{
		None,
		MenuOpen,
		MenuClose,
		OverlayOpen,
		OverlayClose
	};

	bool isCapturingCombo = false;
	ComboType currentComboType = ComboType::None;
	const char* currentComboName = nullptr;
	std::vector<ButtonCombo> recordedCombo;
	double comboStartTime = 0.0;
	double comboTimeout = 3.0;

	// Button controller recording state for UI settings
	std::unordered_map<uint32_t, ControllerDevice> recordingButtonControllers;

	bool desktopWindowManagementApplied = false;
	bool desktopWindowWasTopmost = false;
	HWND desktopWindowManagedHandle = nullptr;
	double lastDesktopWindowManagementAttemptSecs = 0.0;

	// OpenVR runtime and compatibility information
	VRDetection::OpenVRDetectionResult openVRInfo;

	RE::NiPoint3 savedPlayerWorldPos = RE::NiPoint3();  // Used for auto-reset distance check

	// Wand pointing state
	struct WandIntersectionState
	{
		bool isIntersecting = false;
		bool isActivelyDrivingCursor = false;
		bool usingOCUAimPose = false;
		bool usingPresentedSurface = false;
		ImVec2 uvCoordinates = ImVec2(0.0f, 0.0f);
		OverlayType overlayType = OverlayType::HMD;
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		Vector3 rayOrigin = Vector3::Zero;
		Vector3 rayDirection = Vector3::Zero;
	} wandState;
	struct WandHandState
	{
		bool poseValid = false;
		bool isIntersecting = false;
		bool moved = false;
		bool hasScreenPosition = false;
		bool usingOCUAimPose = false;
		bool usingPresentedSurface = false;
		ImVec2 uvCoordinates = ImVec2(0.0f, 0.0f);
		ImVec2 screenPosition = ImVec2(0.0f, 0.0f);
		OverlayType overlayType = OverlayType::HMD;
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		Vector3 rayOrigin = Vector3::Zero;
		Vector3 rayDirection = Vector3::Zero;
		float hitDistance = (std::numeric_limits<float>::max)();
	};
	std::array<WandHandState, 2> wandHandStates{};
	std::array<PresentedMenuSurface, 2> presentedMenuSurfaces{};
	ControllerDevice activeWandController = ControllerDevice::Both;
	ControllerDevice capturedWandController = ControllerDevice::Both;
	std::uint32_t lastWandHoveredID = 0;
	ControllerDevice lastWandHoveredController = ControllerDevice::Both;

	bool customVRCursorVisible = false;
	ImVec2 customVRCursorPos = ImVec2(-FLT_MAX, -FLT_MAX);
	OverlayType customVRCursorOverlayType = OverlayType::HMD;

	struct InSceneResources
	{
		winrt::com_ptr<ID3D11DeviceContext1> immediateContext;
		winrt::com_ptr<ID3DDeviceContextState> overlayContextState;
		winrt::com_ptr<ID3D11VertexShader> vs;
		winrt::com_ptr<ID3D11PixelShader> ps;
		winrt::com_ptr<ID3D11InputLayout> inputLayout;
		winrt::com_ptr<ID3D11Buffer> vb;
		winrt::com_ptr<ID3D11Buffer> ib;
		winrt::com_ptr<ID3D11Buffer> cb;
		winrt::com_ptr<ID3D11BlendState> blendState;
		winrt::com_ptr<ID3D11DepthStencilState> depthState;
		winrt::com_ptr<ID3D11RasterizerState> rasterizerState;
		winrt::com_ptr<ID3D11SamplerState> sampler;
		winrt::com_ptr<ID3D11ShaderResourceView> menuSRV;
		winrt::com_ptr<ID3D11ShaderResourceView> menuControllerSRV;
		winrt::com_ptr<ID3D11ComputeShader> submitCompositeCS;
		winrt::com_ptr<ID3D11Buffer> submitCompositeCB;
		winrt::com_ptr<ID3D11ComputeShader> submitIndicatorCS;
		winrt::com_ptr<ID3D11Buffer> submitIndicatorCB;
		ID3D11Texture2D* cachedMenuTexture = nullptr;
		ID3D11Texture2D* cachedMenuControllerTexture = nullptr;

		struct CachedRTV
		{
			ID3D11Texture2D* texture = nullptr;
			winrt::com_ptr<ID3D11RenderTargetView> rtv;
		};
		struct SubmitCopy
		{
			D3D11_TEXTURE2D_DESC sourceDesc{};
			D3D11_TEXTURE2D_DESC pendingSourceDesc{};
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11UnorderedAccessView> uav;
			bool pendingCreate = false;
		};
		CachedRTV cachedEyeRTVs[2];
		SubmitCopy submitCopies[2];
		vr::TrackedDevicePose_t cachedRenderPoses[vr::k_unMaxTrackedDeviceCount]{};
		uint32_t cachedPoseFrame = 0;
		bool cachedPosesValid = false;

		bool initialized = false;
		bool submitHookInstalled = false;
	} inSceneResources;

	struct InSceneCB
	{
		Matrix wvp;
	};

	struct alignas(16) SubmitCompositeCB
	{
		uint32_t targetSize[2];
		uint32_t dispatchOrigin[2];
		uint32_t dispatchSize[2];
		uint32_t padding[2];
		float quadPixels[8];
		float quadInvW[4];
		float menuMipLevel;
		float padding2[3];
	};
	STATIC_ASSERT_ALIGNAS_16(SubmitCompositeCB);

	struct alignas(16) SubmitIndicatorCB
	{
		uint32_t targetSize[2];
		uint32_t dispatchOrigin[2];
		uint32_t dispatchSize[2];
		float centrePixels[2];
		float radiusPixels;
		float padding[3];
	};
	STATIC_ASSERT_ALIGNAS_16(SubmitIndicatorCB);

public:
	//=============================================================================
	// PRIVATE IMPLEMENTATION
	//=============================================================================

	void DetectOpenVRInfo();
	bool IsOpenVRCompatible() const;
	bool IsOpenCompositeRuntime() const { return openVRInfo.runtimeType == VRDetection::RuntimeType::OpenComposite; }
	bool CanUseWandPointing() const { return !settings.UseRuntimeDefaultMenuNavigation && settings.EnableWandPointing; }
	void InitInSceneResources();
	void EnsureInSceneOverlaySubmitCopyResources();
	void RenderInSceneOverlay(
		vr::EVREye eye,
		ID3D11Texture2D* targetTexture,
		const vr::VRTextureBounds_t* bounds,
		ID3D11RenderTargetView* targetRTV = nullptr,
		bool* overlayComposited = nullptr);
	void CompositeInSceneOverlaySubmitTexture(
		vr::EVREye eye,
		ID3D11Texture2D* targetTexture,
		ID3D11UnorderedAccessView* targetUAV,
		const D3D11_TEXTURE2D_DESC& targetDesc,
		const vr::VRTextureBounds_t* bounds,
		bool* overlayComposited = nullptr);
	void CompositeCaptureIndicatorSubmitTexture(
		ID3D11UnorderedAccessView* targetUAV,
		const D3D11_TEXTURE2D_DESC& targetDesc,
		const vr::VRTextureBounds_t* bounds,
		bool* indicatorComposited = nullptr);
	bool PrepareInSceneOverlaySubmitTexture(vr::EVREye eye, const vr::Texture_t* inputTexture, const vr::VRTextureBounds_t* bounds, vr::Texture_t& outputTexture);
	bool InstallSubmitHook(bool a_enableProcessing = true);
	bool GetGripPressed(bool isLeft, bool isRight) const;
};
