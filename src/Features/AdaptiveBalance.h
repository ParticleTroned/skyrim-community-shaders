#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "LinearLighting.h"

namespace RE
{
	class TESForm;
}

struct AdaptiveBalance : Feature
{
	static constexpr std::string_view kFeatureName = "Adaptive Balance";
	static constexpr std::string_view kFeatureShortName = "AdaptiveBalance";
	static constexpr const char* kFeatureDisplayNameKey = "feature.adaptive_balance.name";

	static AdaptiveBalance* GetSingleton()
	{
		static AdaptiveBalance singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return std::string(kFeatureName); }
	virtual std::string GetDisplayName() override { return T(kFeatureDisplayNameKey, kFeatureName.data()); }
	virtual inline std::string GetShortName() override { return std::string(kFeatureShortName); }
	virtual std::string_view GetSettingsBannerAssetPath() const override { return "FeatureBanners/AdaptiveBalance.png"; }
	virtual inline bool IsCore() const override { return true; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.adaptive_balance.description", "Blends scene lighting and final image balance by location and exterior time of day."),
			{ T("feature.adaptive_balance.key_feature_1", "Separate exterior day and night scene/image profiles"),
				T("feature.adaptive_balance.key_feature_2", "Separate interior, dungeon, and dwelling profiles"),
				T("feature.adaptive_balance.key_feature_3", "Optional per-location overrides with COC codes"),
				T("feature.adaptive_balance.key_feature_4", "Per-profile brightness, dual bloom, saturation, and contrast control") }
		};
	}

	virtual void DrawSettingsHeaderControls() override;

	enum class Profile : uint32_t
	{
		ExteriorDay,
		ExteriorNight,
		Interior,
		Dungeon,
		Dwelling,
		Count
	};

	static constexpr std::size_t kProfileCount = static_cast<std::size_t>(Profile::Count);

	struct ProfileSettings
	{
		float brightness = 1.0f;
		float imageBrightness = 1.0f;
		float bloom = 1.0f;
		float advancedBloom = 0.0f;
		float saturation = 1.0f;
		float contrast = 1.0f;
		bool advanced = false;

		float directionalLightMult = 1.0f;
		float pointLightMult = 1.0f;
		float ambientMult = 1.0f;
		float emitColorMult = 1.0f;
		float glowmapMult = 1.0f;
		float effectLightingMult = 1.0f;

		float skyGammaOffset = 0.0f;
		float fogGammaOffset = 0.0f;
		float fogAlphaGammaOffset = 0.0f;
		float waterGammaOffset = 0.0f;
		float vlGammaOffset = 0.0f;
	};

	struct LocationOverride
	{
		std::string key;
		std::string name;
		std::string type = "Location";
		std::string cocCode;
		ProfileSettings profile;
	};

	struct LocationOverrideTarget
	{
		std::string key;
		std::string name;
		std::string type;
		std::string cocCode;
		Profile defaultProfile = Profile::Interior;
	};

	struct Settings
	{
		bool enabled = false;
		float dayStartHour = 9.0f;
		float nightStartHour = 21.0f;
		float transitionHours = 1.0f;
		std::array<ProfileSettings, kProfileCount> profiles{};
		std::vector<LocationOverride> locationOverrides;
	} settings;

	struct ImageAdjustments
	{
		float imageBrightness = 1.0f;
		float bloom = 1.0f;
		float advancedBloom = 0.0f;
		float saturation = 1.0f;
		float contrast = 1.0f;
	};

	static constexpr const char* kDefaultGlobalPresetName = "Default";
	static constexpr const char* kDefaultLocationOverridePresetName = "Default";
	static constexpr const char* kDefaultFullPresetName = "Default";
	static constexpr std::size_t kInvalidLocationOverrideIndex = static_cast<std::size_t>(-1);

	struct LocationOverrideCache
	{
		uint32_t locationFormID = 0;
		uint32_t cellFormID = 0;
		uint64_t lookupVersion = 0;
		std::size_t overrideIndex = kInvalidLocationOverrideIndex;
		bool valid = false;
	};

	std::string globalPresetName = kDefaultGlobalPresetName;
	std::string globalPresetStatus;
	std::string selectedLocationOverrideKey;
	std::string locationOverridePresetName = kDefaultLocationOverridePresetName;
	std::string locationOverridePresetStatus;
	std::string fullPresetName = kDefaultFullPresetName;
	std::string fullPresetStatus;
	std::string locationOverrideEditKey;
	std::optional<ProfileSettings> locationOverrideEditProfile;
	bool advancedControlsOpen = false;
	mutable std::unordered_map<std::string, std::size_t> locationOverrideLookup;
	mutable LocationOverrideCache locationOverrideCache;
	mutable uint64_t locationOverrideLookupVersion = 0;
	mutable bool locationOverrideLookupDirty = true;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	bool IsRuntimeEnabled() const;
	LinearLighting::Settings GetEffectiveLinearLightingSettings(const LinearLighting::Settings& a_linearLightingSettings, bool a_linearLightingEnabled) const;
	ImageAdjustments GetEffectiveImageAdjustments() const;

	struct ActiveProfileBlend
	{
		const ProfileSettings* from = nullptr;
		const ProfileSettings* to = nullptr;
		float factor = 0.0f;
	};

	LinearLighting::Settings GetNeutralLinearLightingSettings() const;
	LinearLighting::Settings ApplyProfile(const LinearLighting::Settings& a_base, const ProfileSettings& a_profile) const;
	LinearLighting::Settings LerpSettings(const LinearLighting::Settings& a_a, const LinearLighting::Settings& a_b, float a_t) const;
	static ImageAdjustments GetImageAdjustments(const ProfileSettings& a_profile);
	static ImageAdjustments LerpImageAdjustments(const ImageAdjustments& a_a, const ImageAdjustments& a_b, float a_t);
	ActiveProfileBlend GetActiveProfileBlend() const;
	Profile GetInteriorProfile() const;
	Profile GetCurrentProfileForUI() const;
	const LocationOverride* GetActiveLocationOverride() const;
	float GetExteriorNightFactor() const;
	std::string GetContextLabel() const;
	static const char* GetProfileName(Profile a_profile);
	void DrawExteriorTimeSettings();
	void DrawProfile(Profile a_profile);
	void DrawProfileSettings(ProfileSettings& a_profile, const char* a_sectionTitle = "Profile Values");
	void SetAdvancedControlsOpen(bool a_open);
	void DrawGlobalPresetControls();
	void DrawLocationOverrides();
	void DrawLocationOverridePresetControls();
	void DrawFullPresetControls();
	void SaveCurrentLocationOverride();
	void ClearLocationOverrideSelection();
	void ResetLocationOverrideEdit();
	ProfileSettings* GetLocationOverrideEditProfile(LocationOverride& a_locationOverride);
	bool ExportGlobalPreset();
	bool ImportGlobalPreset();
	bool ExportLocationOverrides();
	bool ImportLocationOverrides();
	bool ExportFullPreset();
	bool ImportFullPreset();
	std::optional<LocationOverrideTarget> GetCurrentLocationOverrideTarget() const;
	LocationOverride* FindLocationOverride(const std::string& a_key);
	const LocationOverride* FindLocationOverride(const std::string& a_key) const;
	std::size_t FindLocationOverrideIndexByKey(const std::string& a_key) const;
	std::size_t FindLocationOverrideIndexByForm(const RE::TESForm* a_form) const;
	std::size_t ResolveLocationOverrideIndex() const;
	void NormalizeLocationOverrides();
	void MarkLocationOverrideLookupDirty();
	void EnsureLocationOverrideLookup() const;
};
