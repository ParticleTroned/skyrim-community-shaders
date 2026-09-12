#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Bloom.h"
#include "Buffer.h"
#include "LinearLighting.h"
#include "SharedLighting.h"
#include "WaterAppearance.h"

namespace RE
{
	class BGSLocation;
	class TESForm;
	class TESWorldSpace;
}

struct AdaptiveBrightness : Feature
{
	static constexpr std::string_view kFeatureName = "Adaptive Balance";
	static constexpr std::string_view kFeatureShortName = "AdaptiveBrightness";
	static constexpr std::string_view kFeatureDisplayName = kFeatureName;

	static AdaptiveBrightness* GetSingleton()
	{
		static AdaptiveBrightness singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return std::string(kFeatureName); }
	virtual inline std::string GetShortName() override { return std::string(kFeatureShortName); }
	virtual inline std::string GetDisplayName() override { return std::string(kFeatureDisplayName); }
	virtual inline std::string_view GetShaderDefineName() override { return "ADAPTIVE_BALANCE"; }
	virtual bool HasShaderDefine(RE::BSShader::Type a_shaderType) override
	{
		return a_shaderType == RE::BSShader::Type::Lighting ||
		       a_shaderType == RE::BSShader::Type::Water ||
		       a_shaderType == RE::BSShader::Type::ImageSpace;
	}
	virtual inline bool IsCore() const override { return true; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Balances scene lighting, atmosphere and bloom by location and exterior time of day.",
			{ "Separate exterior day and night balance profiles",
				"Separate interior, dungeon, and dwelling profiles",
				"Hierarchical worldspace, region, city, location, and cell profiles with COC codes",
				"Unified global and per-profile Lighting, Bloom, Water, and wind adjustment layers" }
		};
	}

	virtual bool SupportsVR() override { return true; }
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

	struct WaterWindSettings
	{
		bool overrideEnabled = false;
		bool enabled = false;
		float calmWaveMultiplier = 1.0f;
		float strongWindWaveMultiplier = 1.0f;
	};

	struct ProfileSettings
	{
		float brightness = 1.0f;
		bool advanced = false;
		bool bloomAdvanced = false;
		bool waterAdvanced = false;

		float skyBrightnessMult = 1.0f;
		float directionalLightMult = 1.0f;
		float pointLightMult = 1.0f;
		float linearPointLightMult = 1.0f;
		float spotlightMult = 1.0f;
		float linearSpotlightMult = 1.0f;
		float omnidirectionalBulbMult = 1.0f;
		float linearOmnidirectionalBulbMult = 1.0f;
		float ambientMult = 1.0f;
		float emitColorMult = 1.0f;
		float glowmapMult = 1.0f;
		float effectLightingMult = 1.0f;

		float skyGammaOffset = 0.0f;
		float fogGammaOffset = 0.0f;
		float fogAlphaGammaOffset = 0.0f;
		float waterGammaOffset = 0.0f;
		float vlGammaOffset = 0.0f;

		Bloom::Profile bloom;
		WaterAppearance::Profile water;
		WaterWindSettings waterWind;

		static ProfileSettings AdjustmentDefaults();
		static ProfileSettings GlobalDefaults();
	};

	struct LocationOverride
	{
		std::string key;
		std::string name;
		std::string type = "Location";
		std::string cocCode;
		ProfileSettings profile;
		bool layered = false;
	};

	struct LocationOverrideTarget
	{
		std::string key;
		std::string name;
		std::string type;
		std::string cocCode;
		Profile defaultProfile = Profile::Interior;
	};

	struct CurrentLocationOverrideTargets
	{
		std::optional<LocationOverrideTarget> worldspace;
		std::optional<LocationOverrideTarget> region;
		std::optional<LocationOverrideTarget> city;
		std::optional<LocationOverrideTarget> location;
		std::optional<LocationOverrideTarget> cell;
	};

	struct Settings
	{
		bool enabled = true;
		float dayStartHour = 9.0f;
		float nightStartHour = 21.0f;
		float transitionHours = 1.0f;
		ProfileSettings globalProfile = ProfileSettings::GlobalDefaults();
		std::array<ProfileSettings, kProfileCount> profiles{};
		std::vector<LocationOverride> locationOverrides;
	} settings;

	struct alignas(16) PerFrameData
	{
		float skyBrightness;
		float directionalLightMult;
		float pointLightMult;
		float linearPointLightMult;
		float spotlightMult;
		float linearSpotlightMult;
		float omnidirectionalBulbMult;
		float linearOmnidirectionalBulbMult;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrameData);
	static_assert(sizeof(PerFrameData) == 32);

	struct alignas(16) VanillaPointLightData
	{
		uint32_t pointLightFlags[8];
	};
	STATIC_ASSERT_ALIGNAS_16(VanillaPointLightData);
	static_assert(sizeof(VanillaPointLightData) == 32);

	static constexpr uint32_t kLightingPointLightCBRegister = 3;
	static constexpr uint32_t kWaterPointLightCBRegister = 7;

	ConstantBuffer* vanillaPointLightCB = nullptr;

	struct EffectiveLinearLightingSettings
	{
		LinearLighting::Settings settings;
		bool hasColorAdjustments = false;
	};

	static constexpr const char* kDefaultGlobalPresetName = "Default";
	static constexpr const char* kDefaultLocationOverridePresetName = "Default";
	static constexpr const char* kDefaultFullPresetName = "Default";
	static constexpr std::size_t kInvalidLocationOverrideIndex = static_cast<std::size_t>(-1);

	struct LocationLayerCache
	{
		uint32_t worldspaceFormID = 0;
		uint32_t locationFormID = 0;
		uint32_t cellFormID = 0;
		uint64_t lookupVersion = 0;
		std::vector<const LocationOverride*> layers;
		bool valid = false;
	};

	enum class ContextProfileScope : std::size_t
	{
		Worldspace,
		Region,
		City,
		Count
	};

	struct ProfileTabSyncState
	{
		std::string key;
		bool initialized = false;
		int lastDrawFrame = -1;
	};

	enum class ContextSection
	{
		Profiles,
		Locations
	};

	std::string globalPresetName = kDefaultGlobalPresetName;
	std::string globalPresetStatus;
	std::string selectedLocationOverrideKey;
	std::string locationOverridePresetName = kDefaultLocationOverridePresetName;
	std::string locationOverridePresetStatus;
	std::array<std::string, static_cast<std::size_t>(ContextProfileScope::Count)> contextPresetNames{
		kDefaultLocationOverridePresetName,
		kDefaultLocationOverridePresetName,
		kDefaultLocationOverridePresetName
	};
	std::array<std::string, static_cast<std::size_t>(ContextProfileScope::Count)> contextPresetStatuses{};
	std::string fullPresetName = kDefaultFullPresetName;
	std::string fullPresetStatus;
	std::string locationOverrideEditKey;
	std::optional<ProfileSettings> locationOverrideEditProfile;
	ProfileTabSyncState profileTabSyncState{};
	std::string contextSectionSyncKey;
	bool contextSectionSyncInitialized = false;
	int contextSectionLastDrawFrame = -1;
	mutable std::unordered_map<std::string, std::size_t> locationOverrideLookup;
	mutable LocationLayerCache locationLayerCache;
	mutable uint64_t locationOverrideLookupVersion = 0;
	mutable bool locationOverrideLookupDirty = true;
	mutable float smoothedWaterWindSpeed = 0.0f;
	mutable uint32_t smoothedWaterWindFrame = 0;
	mutable bool waterWindSmoothingInitialized = false;
	bool performanceCostMeasurementEnabled = true;

	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool a_advanced) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override { return performanceCostMeasurementEnabled && IsRuntimeAvailable(); }
	virtual bool UsesTotalPerformanceCostMeasurement() const override { return true; }
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override;
	virtual bool IsPerformanceCostMeasurementReady() const override { return IsRuntimeAvailable(); }
	virtual const char* GetPerformanceCostMeasurementWaitText() const override { return "Waiting for a gameplay cell"; }

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void SetupResources() override;
	virtual void PostPostLoad() override;

	bool IsRuntimeAvailable() const;
	bool IsAdjustmentRuntimeActive() const;
	bool IsRuntimeEnabled() const;
	PerFrameData GetCommonBufferData() const;
	bool NeedsVanillaPointLightData() const;
	void UpdateVanillaPointLightData(RE::BSRenderPass* a_pass, uint32_t a_lightCount, uint32_t a_bufferRegister);
	EffectiveLinearLightingSettings GetEffectiveLinearLightingSettings(
		const LinearLighting::Settings& a_linearLightingSettings,
		bool a_linearLightingEnabled) const;
	SharedLightingSettings GetEffectiveSharedLightingSettings() const;
	Bloom::Settings GetEffectiveBloomSettings() const;
	WaterAppearance::Settings GetEffectiveWaterAppearanceSettings() const;

	struct ActiveProfileBlend
	{
		const ProfileSettings* from = nullptr;
		const ProfileSettings* to = nullptr;
		float factor = 0.0f;
	};

	LinearLighting::Settings GetNeutralLinearLightingSettings() const;
	LinearLighting::Settings ApplyProfile(const LinearLighting::Settings& a_base, const ProfileSettings& a_profile) const;
	LinearLighting::Settings LerpSettings(const LinearLighting::Settings& a_a, const LinearLighting::Settings& a_b, float a_t) const;
	SharedLightingSettings ApplyProfile(const SharedLightingSettings& a_base, const ProfileSettings& a_profile) const;
	SharedLightingSettings LerpSettings(const SharedLightingSettings& a_a, const SharedLightingSettings& a_b, float a_t) const;
	Bloom::Profile ApplyProfile(const Bloom::Profile& a_base, const ProfileSettings& a_profile) const;
	WaterAppearance::Profile ApplyProfile(const WaterAppearance::Profile& a_base, const ProfileSettings& a_profile) const;
	WaterWindSettings ApplyProfile(const WaterWindSettings& a_base, const ProfileSettings& a_profile) const;
	float GetSmoothedWaterWindSpeed() const;
	void ResetWaterWindSmoothing() const;
	ActiveProfileBlend GetActiveProfileBlend() const;
	const std::vector<const LocationOverride*>& GetActiveLocationLayers() const;
	Profile GetInteriorProfile() const;
	Profile GetCurrentProfileForUI() const;
	const LocationOverride* GetActiveLocationOverride() const;
	const LocationOverride* GetActiveWorldspaceOverride() const;
	std::optional<ContextProfileScope> GetCurrentContextOverrideScope(const LocationOverride* a_locationOverride) const;
	float GetExteriorNightFactor() const;
	std::string GetContextLabel() const;
	static const char* GetProfileName(Profile a_profile);
	std::optional<Profile> SyncSelectedProfileTabToContext();
	std::optional<ContextSection> SyncContextSection();
	void DrawExteriorTimeSettings();
	void DrawProfile(Profile a_profile, bool a_allowEdits = true);
	void DrawLocationOverrideProfileEditor(
		LocationOverride& a_locationOverride,
		const char* a_sectionTitle,
		bool a_showAdvancedControls,
		bool a_allowEdits,
		const char* a_saveLabel,
		bool a_closeWhenFinished);
	void DrawCurrentContextProfileTab(
		ContextProfileScope a_scope,
		bool a_showAdvancedControls,
		bool a_allowEdits,
		bool a_select);
	void DrawContextProfilePresetControls(
		ContextProfileScope a_scope,
		const LocationOverrideTarget& a_target,
		const LocationOverride* a_locationOverride,
		bool a_allowEdits);
	void DrawProfileSettings(
		ProfileSettings& a_profile,
		const char* a_sectionTitle = "Profile Values",
		bool a_showAdvancedControls = true,
		bool a_allowEdits = true);
	void DrawProfileControlTabs(
		ProfileSettings& a_profile,
		const char* a_tabBarID,
		bool a_showAdvancedControls,
		bool a_globalLayer,
		bool a_allowEdits);
	void DrawGlobalSettings(bool a_showAdvancedControls);
	void DrawLightingSettings(ProfileSettings& a_profile, bool a_showAdvancedControls, bool a_globalLayer);
	void DrawBloomSettings(ProfileSettings& a_profile, bool a_showAdvancedControls, bool a_globalLayer);
	void DrawWaterSettings(ProfileSettings& a_profile, bool a_showAdvancedControls, bool a_globalLayer);
	void DrawWaterWindSettings(ProfileSettings& a_profile, bool a_globalLayer);
	void DrawGlobalPresetControls();
	void DrawLocationOverrides(bool a_includePresetControls = true, bool a_showAdvancedControls = true, bool a_allowEdits = true);
	void DrawLocationOverridePresetControls();
	void DrawFullPresetControls();
	void SaveCurrentLocationOverride(const LocationOverrideTarget& a_target);
	void ClearLocationOverrideSelection();
	void InvalidateProfileTabSync();
	void ResetLocationOverrideEdit();
	ProfileSettings* GetLocationOverrideEditProfile(LocationOverride& a_locationOverride);
	bool ExportGlobalPreset();
	bool ImportGlobalPreset();
	bool ExportLocationOverrides();
	bool ImportLocationOverrides();
	bool ExportContextProfile(
		ContextProfileScope a_scope,
		const LocationOverrideTarget& a_target,
		const LocationOverride& a_locationOverride);
	bool ImportContextProfile(ContextProfileScope a_scope, const LocationOverrideTarget& a_target);
	bool ExportFullPreset();
	bool ImportFullPreset();
	CurrentLocationOverrideTargets GetCurrentLocationOverrideTargets() const;
	const LocationOverride* GetInheritedLocationOverride(const LocationOverrideTarget& a_target) const;
	LocationOverride* FindLocationOverride(const std::string& a_key);
	const LocationOverride* FindLocationOverride(const std::string& a_key) const;
	std::size_t FindLocationOverrideIndexByKey(const std::string& a_key) const;
	std::size_t FindLocationOverrideIndexByForm(const RE::TESForm* a_form) const;
	std::size_t ResolveWorldspaceHierarchyOverrideIndex(const RE::TESWorldSpace* a_worldspace) const;
	std::size_t ResolveLocationHierarchyOverrideIndex(const RE::BGSLocation* a_location) const;
	void NormalizeLocationOverrides();
	void MarkLocationOverrideLookupDirty();
	void EnsureLocationOverrideLookup() const;

	struct Hooks;
};
