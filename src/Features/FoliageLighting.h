#pragma once

#include <atomic>

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
	virtual std::string GetDisplayName() override { return T("feature.foliage_lighting.name", "Foliage Lighting"); }
	virtual std::string GetShortName() override { return "FoliageLighting"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual bool IsCore() const override { return true; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.foliage_lighting.description", "Foliage Lighting adds inexpensive transmission and ambient controls for animated tree foliage and grass."),
			{ T("feature.foliage_lighting.key_feature_1", "View-dependent tree foliage transmission"),
				T("feature.foliage_lighting.key_feature_2", "Consistent ambient backface sampling"),
				T("feature.foliage_lighting.key_feature_3", "Independent grass scattering control") }
		};
	}

	virtual void DrawSettingsHeaderControls() override;
	virtual void DrawSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual PerformanceTuningConfig GetPerformanceTuningConfig() const override
	{
		return { 17,
			T("menu.performance_tuning.feature.foliage_lighting.comparison_label", "Off"),
			T("menu.performance_tuning.feature.foliage_lighting.comparison_details", "all Foliage Lighting contributions to tree foliage and grass are switched off.") };
	}
	virtual json GetPerformanceTuningUserSettingsMask() const override
	{
		return {
			{ "Enabled", true },
			{ "EnableFoliageScattering", true },
			{ "EnableFoliageAmbientBoost", true },
			{ "EnableFoliageAmbientFlip", true },
			{ "EnableGrassScattering", true }
		};
	}
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override;
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override { SetEnabled(a_enabled); }
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
	bool HasEnabledContribution() const;
	void DrawFoliageScatteringSetting();
	void DrawFoliageAmbientBoostSetting(bool a_truePBRActive);
	void DrawFoliageAmbientFlipSetting();
	void DrawGrassScatteringSetting();
	static void SanitizeSettings(Settings& a_settings);
};
