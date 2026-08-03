#pragma once

#include "Feature.h"

struct CSEditor : Feature
{
public:
	static CSEditor* GetSingleton()
	{
		static CSEditor singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "CS Editor"; }
	virtual std::string GetDisplayName() override { return "CS Editor"; }
	virtual inline std::string GetShortName() override { return "CSEditor"; }
	virtual inline std::string_view GetShaderDefineName() override { return "CS_EDITOR"; }
	virtual inline std::string_view GetCategory() const override { return FeatureCategories::kUtility; }
	virtual bool SupportsVR() override { return true; }
	virtual bool IsCore() const override { return true; }
	virtual bool IsInMenu() const override { return true; }

	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { "Development tool for inspecting, editing, and previewing renderer-facing data in-game.",
			{ "Provides weather editing functionality",
				"Includes dynamic saving and loading of vanilla post processing and weather settings.",
				"Real-time editing and previewing of effects" } };
	}

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;
	virtual void Prepass() override;

	void DrawLauncherButton();

	static void OpenEditorWindow();
	static void ToggleEditorWindow();
	static void UpdateWeatherLockAndTime();

	void LerpWeather(RE::TESWeather*, RE::TESWeather*, float);

private:
	static inline bool s_dataAvailable = false;
	static inline bool s_resourcesInitialized = false;
	static inline bool s_checkedWidgetJsonFiles = false;
	static inline bool s_hasWidgetJsonFiles = false;

	static bool CanOpenEditor();
	static bool HasWidgetJsonFiles();
	static bool ShouldPreloadEditorResources();
	static void EnsureDataLoaded();
	static void ApplySavedEditorOverrides();
	static void ApplySavedWeatherOverrides();
};
