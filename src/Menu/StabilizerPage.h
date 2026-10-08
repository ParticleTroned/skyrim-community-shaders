#pragma once

#include "ExternalSettingsPage.h"

namespace MenuUI
{
	/** The optional Stabilizer's INI editors, using the shared feature-page layout. */
	struct StabilizerPage final : ExternalSettingsPage
	{
		static StabilizerPage& Get();
		std::string GetName() override { return "VR FPS Stabilizer"; }
		std::string GetShortName() override { return "VRFpsStabilizer"; }
		std::string_view GetCategory() const override { return FeatureCategories::kUtility; }
		bool SupportsVR() override { return true; }
		std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
		SettingsHeaderStatus GetSettingsHeaderStatus() const override;
		void DrawSettingsEnabledControl() override;
		void DrawSettings() override;
		std::string_view GetSettingsFooterText() const override;
		bool IsAvailable() const override;
		bool HasUnsavedChanges() const override;
		SettingsFooter GetSettingsFooter() override;
	};
}
