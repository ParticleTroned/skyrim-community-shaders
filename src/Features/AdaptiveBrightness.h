#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "LinearLighting.h"

namespace RE
{
	class TESForm;
}

struct AdaptiveBrightness : Feature
{
	static AdaptiveBrightness* GetSingleton()
	{
		static AdaptiveBrightness singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Adaptive Brightness"; }
	virtual inline std::string GetShortName() override { return "AdaptiveBrightness"; }
	virtual inline bool IsCore() const override { return true; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Adaptive Brightness adjusts scene brightness by location and exterior time of day without enabling full Linear Lighting.",
			{ "Separate exterior day and night brightness",
				"Separate interior, dungeon, and dwelling profiles",
				"Optional per-location overrides with COC codes",
				"Advanced per-profile light and atmosphere controls" }
		};
	}

	virtual bool SupportsVR() override { return true; };

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

	static constexpr const char* kDefaultLocationOverridePresetName = "AdaptiveBrightnessPreset";
	static constexpr std::size_t kInvalidLocationOverrideIndex = static_cast<std::size_t>(-1);

	struct LocationOverrideCache
	{
		uint32_t locationFormID = 0;
		uint32_t cellFormID = 0;
		uint64_t lookupVersion = 0;
		std::size_t overrideIndex = kInvalidLocationOverrideIndex;
		bool valid = false;
	};

	std::string selectedLocationOverrideKey;
	std::string locationOverridePresetName = kDefaultLocationOverridePresetName;
	std::string locationOverridePresetStatus;
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

	LinearLighting::Settings GetNeutralLinearLightingSettings() const;
	LinearLighting::Settings ApplyProfile(const LinearLighting::Settings& a_base, const ProfileSettings& a_profile) const;
	LinearLighting::Settings LerpSettings(const LinearLighting::Settings& a_a, const LinearLighting::Settings& a_b, float a_t) const;
	Profile GetInteriorProfile() const;
	Profile GetCurrentProfileForUI() const;
	const LocationOverride* GetActiveLocationOverride() const;
	float GetExteriorNightFactor() const;
	std::string GetContextLabel() const;
	static const char* GetProfileName(Profile a_profile);
	void DrawProfile(Profile a_profile);
	void DrawProfileSettings(ProfileSettings& a_profile);
	void SetAdvancedControlsOpen(bool a_open);
	void DrawLocationOverrides();
	void DrawLocationOverridePresetControls();
	void SaveCurrentLocationOverride();
	void ClearLocationOverrideSelection();
	void ResetLocationOverrideEdit();
	ProfileSettings* GetLocationOverrideEditProfile(LocationOverride& a_locationOverride);
	bool ExportLocationOverrides();
	bool ImportLocationOverrides();
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
