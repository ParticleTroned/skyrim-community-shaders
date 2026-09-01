#pragma once

#include "Feature.h"

struct FoliageLighting : Feature
{
public:
	static constexpr float kAmbientAmountMin = 0.0f;
	static constexpr float kAmbientAmountMax = 1.0f;

	struct alignas(16) Settings
	{
		uint EnableFoliageScattering = 1;
		uint EnableFoliageAmbientBoost = 0;
		uint EnableFoliageAmbientFlip = 1;
		float FoliageAmbientAmount = 0.25f;
		uint EnableGrassScattering = 1;
		uint pad[3]{};
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(offsetof(Settings, EnableFoliageAmbientBoost) == sizeof(uint));
	static_assert(offsetof(Settings, EnableFoliageAmbientFlip) == sizeof(uint) * 2);
	static_assert(offsetof(Settings, FoliageAmbientAmount) == sizeof(uint) * 3);
	static_assert(offsetof(Settings, EnableGrassScattering) == sizeof(uint) * 4);
	static_assert(sizeof(Settings) == 32);

	virtual std::string GetName() override { return "Foliage Lighting"; }
	virtual std::string GetShortName() override { return "FoliageLighting"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kFoliage; }
	virtual bool IsCore() const override { return true; }
	virtual bool SupportsVR() override { return true; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Foliage Lighting adds inexpensive transmission and ambient controls for animated tree foliage and grass.",
			{ "View-dependent tree foliage transmission",
				"Stereo-stable ambient backface sampling in VR",
				"Independent grass scattering control" }
		};
	}

	virtual void DrawSettingsHeaderControls() override;
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	Settings GetCommonBufferData() const;
	/** @return Whether the persisted Foliage Lighting master switch is enabled. */
	bool IsEnabled() const { return enabled.load(std::memory_order_acquire); }
	/** @return Whether Foliage Lighting is contributing to the current frame. */
	bool IsRuntimeEnabled() const { return loaded && IsEnabled(); }
	/** Enables or disables all contributions without discarding detailed tuning. */
	void SetEnabled(bool a_enabled) { enabled.store(a_enabled, std::memory_order_release); }

	Settings settings;

private:
	std::atomic_bool enabled = true;

	static Settings GetDisabledSettings();
	static void SanitizeSettings(Settings& a_settings);
};
