#pragma once

#include "Feature.h"

/**
 * @brief Abstract base class for all features that provide an in-game overlay.
 *
 * Inherit from OverlayFeature if your feature draws an ImGui overlay. Some overlays are
 * user-toggled HUD windows, while others are transient status messages. The renderer uses
 * this interface to decide whether an ImGui frame is needed before asking a feature to draw.
 */
struct OverlayFeature : Feature
{
	/**
	 * @brief Draw the overlay for this feature.
	 *
	 * This method should render the overlay UI using ImGui. It will only be called when
	 * IsOverlayVisible() returns true and, for user-toggled overlays, the global overlay
	 * toggle is enabled.
	 */
	virtual void DrawOverlay() = 0;

	/**
	 * @brief Whether this overlay has anything to draw this frame.
	 *
	 * This should mirror the feature's real draw predicate, not just a configuration setting.
	 * If false, the overlay will not be drawn and it will not keep ImGui rendering alive.
	 */
	virtual bool IsOverlayVisible() const = 0;

	/**
	 * @brief Whether this overlay is controlled by the global overlay toggle.
	 *
	 * User-facing HUD overlays should return true. Transient status overlays such as build
	 * progress or diagnostics can use the default false so they appear only when their own
	 * IsOverlayVisible() predicate says they are active.
	 */
	virtual bool RequiresGlobalOverlayToggle() const { return false; }

	/**
	 * @brief Name of the root ImGui window drawn by this overlay.
	 *
	 * Overlays that need renderer-level routing can return their window name here.
	 */
	virtual const char* GetOverlayWindowName() const { return nullptr; }

	/**
	 * @brief Whether the overlay should be hidden from the desktop mirror when routed to VR.
	 *
	 * This is used for headset HUDs that are submitted to the VR overlay texture but should
	 * not also appear in the desktop ImGui frame.
	 */
	virtual bool HideFromDesktopWhenSubmittedToVR() const { return false; }

	/**
	 * @brief Get the category for UI grouping. Overlays default to "Utility".
	 *
	 * Subclasses may override this to provide a different category.
	 */
	virtual std::string_view GetCategory() const override { return FeatureCategories::kUtility; }
};
