#pragma once
#include "Features/InverseSquareLighting/Common.h"

#include <nlohmann/json.hpp>

#include <filesystem>

struct LightEditor
{
	bool disableInvSqLights = false;
	bool disableRegularLights = false;
	bool shadowsOnly = false;

	void DrawSettings();
	void GatherLights();
	void ResetOverrides();
	void RestoreDefaultSettings();

	bool ApplyOverrides(RE::NiLight* niLight, ISLCommon::RuntimeLightDataExt* runtimeData) const;

private:
	struct LightInfo
	{
		bool isSelected = false;
		uint32_t id = 0;
		void* ptr = nullptr;
		uint32_t index = 0;
		std::string name;
		bool isRef = false;
		bool isAttached = false;
		bool isOther = false;
		bool isSpotlight = false;
		bool isShadow = false;
		bool hasPosition = false;
		RE::NiPoint3 position = {};

		bool operator==(const LightInfo& other) const noexcept
		{
			if (isRef != other.isRef ||
				isAttached != other.isAttached ||
				isOther != other.isOther) {
				return false;
			}

			// References and attached bulbs survive a 3D rebuild by owner identity. The
			// NiLight pointer does not: Disable/Enable recreates it.
			if (isRef || isAttached)
				return id == other.id && index == other.index;

			return ptr == other.ptr;
		}
	};

	struct LightDisplayInfo
	{
		RE::FormID ownerFormId = 0;
		std::string ownerEditorId;
		RE::FormID baseObjectFormId = 0;
		std::string ownerLastEditedBy;
		std::string cellEditorId;
		RE::FormID lighFormId = 0;
		std::string lighEditorId;
		RE::NiPoint3 pos = {};
	};

	struct LightSettings
	{
		stl::enumeration<ISLCommon::TES_LIGHT_FLAGS_EXT, uint32_t> tesFlags;
		ISLCommon::RuntimeLightDataExt data = {};
		RE::NiPoint3 pos = {};
	};

	bool showAttachedLights = false;
	bool showEffectLights = false;
	int32_t waitFrames = 0;
	uint32_t totalLightCount = 0;
	uint32_t activeShadowLightCount = 0;

	enum class FilterOption
	{
		RefLights,
		AttachedLights,
		OtherLights,
		Count
	};

	const char* FilterOptionLabels[3] = {
		"Ref Lights",
		"Attached Lights",
		"Other Lights"
	};

	enum class SortOption
	{
		None,
		Distance,
		FormID,
		EditorID,
		Count
	};

	const char* SortOptionLabels[4] = {
		"None",
		"Distance",
		"FormID",
		"EditorID"
	};

	FilterOption filterOption = FilterOption::RefLights;
	SortOption sortOption = SortOption::Distance;

	std::vector<LightInfo> lights = {};
	std::unordered_map<RE::TESObjectREFR*, uint32_t> lightsAttached = {};

	LightInfo selected = {};
	LightInfo previous = {};

	LightDisplayInfo displayInfo = {};
	LightSettings original = {};
	LightSettings current = {};
	float inverseSquareRadius = 0.0f;

	struct LPLightInfo
	{
		std::string configPath;
		std::string lightEDID;
		std::string ownerModelPath;
		std::string ownerEditorId;
		bool isLPLight = false;
		bool ignoreScale = false;
		bool hasAuthoredState = false;
		float fadeFactor = 1.0f;
		float radiusFactor = 1.0f;
		float sizeFactor = 1.0f;
		ISLCommon::RuntimeLightDataExt runtimeSnapshot = {};
	};

	LPLightInfo lpInfo;
	RE::NiPointer<RE::NiLight> activeNiLight;
	RE::ObjectRefHandle activeRefr;
	RE::TESObjectLIGH* activeLigh = nullptr;
	bool activeIsRef = false;

	void SortLights();
	void RestoreOriginal();

	static std::string GetLightName(LightInfo& lightInfo);
	static LPLightInfo ParseLPLightName(const std::string& name);
	static std::string UpdateLPFlags(const std::string& existingFlags, bool inverseSquare, bool linear);
	static bool MatchesLPFilters(const nlohmann::json& lightEntry, RE::TESObjectREFR* refr);
	bool LoadLightPlacerConfig(
		nlohmann::json& configArray,
		std::filesystem::path& filePath) const;
	nlohmann::json* FindUniqueLightPlacerData(
		nlohmann::json& configArray,
		RE::TESObjectREFR* refr) const;
	bool LoadLightPlacerAuthoredState(RE::TESObjectREFR* refr);
	float GetLightPlacerFadeFactor() const;
	float GetLightPlacerRadiusFactor() const;
	float GetLightPlacerSizeFactor() const;
	void ApplyCurrentRuntimeData(ISLCommon::RuntimeLightDataExt& runtimeData) const;
	bool SaveToLightPlacer();

	void UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight);
};
