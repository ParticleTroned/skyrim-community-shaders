#pragma once

#include "Utils/Input.h"
#include <functional>
#include <vector>

class Menu;
struct OverlayFeature;
struct ImVec2;

/**
 * @brief Specialized renderer component for overlay and frame management
 *
 * This class was extracted from Menu.cpp to handle all overlay-related rendering
 * responsibilities including VR setup, shader compilation status, feature overlays,
 * A/B testing, and ImGui frame lifecycle management.
 *
 * The renderer uses a callback-based architecture to maintain separation of concerns
 * while accessing necessary Menu functionality through provided function objects.
 *
 * @note This class is designed as a stateless utility with static methods to avoid
 *       coupling to Menu's internal state while providing clean separation of concerns.
 */
class OverlayRenderer
{
public:
	/**
	 * @brief Main overlay rendering entry point
	 *
	 * Coordinates all overlay rendering activities including VR setup, input processing,
	 * shader compilation status display, feature overlays, A/B testing, and ImGui frame
	 * management. Uses callback functions to access Menu functionality while maintaining
	 * architectural separation.
	 *
	 * @param menu Reference to the Menu instance for accessing settings and state
	 * @param processInputEventQueue Callback to process queued input events
	 * @param drawSettings Callback to render the main settings interface
	 * @param keyIdToString Callback to convert key codes to human-readable strings
	 * @param cachedFontSize Current cached font size for reload detection
	 * @param currentFontSize Current font size setting for comparison
	 */
	static void RenderOverlay(
		Menu& menu,
		const std::function<void()>& processInputEventQueue,
		const std::function<void()>& drawSettings,
		const std::function<const char*(std::vector<InputCombo>)>& keyIdToString,
		float& cachedFontSize,
		float currentFontSize);
	static bool MoveWindowBelowShaderCompilationStatus(ImVec2& position, const ImVec2& windowSize, const ImVec2& pivot);
	static ImVec2 GetDefaultVRSettingsWindowSize(bool a_excludeShaderCompilationWindow = true);
	static float GetDefaultVRLeftAnchorX(float a_windowWidth);

private:
	static void HandleVRSetup();
	static bool ShouldSkipRendering(const Menu& menu, bool hasDrawableFeatureOverlay);
	static std::vector<OverlayFeature*> CollectDrawableFeatureOverlays(const Menu& menu);
	static bool ShouldDrawFeatureOverlay(const OverlayFeature& overlay, const Menu& menu);
	static void HandleFontReload(Menu& menu, float& cachedFontSize, float currentFontSize);
	static bool ApplyVROverlayDisplaySize();
	static void InitializeImGuiFrame(Menu& menu);
	static void RenderShaderCompilationStatus(const std::function<const char*(std::vector<InputCombo>)>& keyIdToString);
	static void RenderShaderBlockingStatus();
	static void RenderFirstTimeSetupOverlay();
	static void RenderFeatureOverlays(const std::vector<OverlayFeature*>& overlays);
	static void HandleABTesting();
	static void FinalizeImGuiFrame(const std::vector<OverlayFeature*>& overlays);
};
