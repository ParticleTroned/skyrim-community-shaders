#include "CSEditor.h"

#include "State.h"
#include "Util.h"
#include "Utils/FileSystem.h"
#include "Utils/UI.h"

#include "CSEditor/EditorWindow.h"
#include "CSEditor/Weather/CellLightingWidget.h"
#include "CSEditor/Weather/ImageSpaceWidget.h"
#include "CSEditor/Weather/LensFlareWidget.h"
#include "CSEditor/Weather/LightingTemplateWidget.h"
#include "CSEditor/Weather/PrecipitationWidget.h"
#include "CSEditor/Weather/ReferenceEffectWidget.h"
#include "CSEditor/Weather/VolumetricLightingWidget.h"
#include "CSEditor/Weather/WeatherWidget.h"
#include "CSEditor/WeatherUtils.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace
{
	constexpr const char* kJsonExtension = ".json";

	bool HasJsonExtension(const std::filesystem::path& path)
	{
		return _stricmp(path.extension().string().c_str(), kJsonExtension) == 0;
	}

	struct OverrideLoadStats
	{
		size_t applied = 0;
		size_t skipped = 0;
		size_t failed = 0;
	};

	bool LoadOverrideJson(const std::filesystem::directory_entry& entry, json& out,
		OverrideLoadStats& stats, std::string_view label)
	{
		std::string errorMessage;
		if (Util::FileHelpers::ReadJsonFile(entry.path(), out, errorMessage) !=
			Util::FileHelpers::JsonFileReadResult::Success) {
			logger::warn("Failed to read {} override file ({}): {}", label, entry.path().string(), errorMessage);
			stats.failed++;
			return false;
		}

		if (!out.is_object()) {
			logger::warn("Skipping {} override file with non-object JSON: {}", label, entry.path().string());
			stats.skipped++;
			return false;
		}

		return true;
	}

	void LogOverrideStats(std::string_view label, const OverrideLoadStats& stats)
	{
		if (stats.applied > 0 || stats.failed > 0) {
			logger::info("Applied saved {} overrides: applied={}, skipped={}, failed={}",
				label, stats.applied, stats.skipped, stats.failed);
		}
	}

	template <class WidgetType, class FormType>
	bool ApplyWidgetOverride(FormType* form, const json& settingsJson)
	{
		if (!form || !settingsJson.is_object())
			return false;

		WidgetType widget(form);
		if (!widget.form)
			return false;

		widget.CacheFormData();
		widget.RememberBaseline();
		widget.js = settingsJson;
		try {
			widget.LoadSettings();
		} catch (const std::exception& e) {
			logger::error("Failed to apply saved CS Editor override for {}: {}", widget.GetEditorID(), e.what());
			return false;
		}
		return true;
	}

	template <class WidgetType, class FormType>
	OverrideLoadStats ApplySavedWidgetOverrides(const char* folderName, const char* label,
		bool warnUnmatched = true)
	{
		OverrideLoadStats stats;
		const auto folderPath = Util::PathHelpers::GetCommunityShaderPath() / folderName;
		std::error_code ec;
		if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec))
			return stats;

		try {
			for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
				if (!entry.is_regular_file() || !HasJsonExtension(entry.path()))
					continue;

				json settingsJson;
				if (!LoadOverrideJson(entry, settingsJson, stats, label))
					continue;

				const auto formKey = entry.path().stem().string();
				auto* form = WeatherUtils::FindFormByEditorIDOrFileKey<FormType>(formKey);
				if (!form) {
					if (warnUnmatched) {
						logger::warn("{} override file has no matching form: {}", label, entry.path().string());
						stats.failed++;
					} else {
						stats.skipped++;
					}
					continue;
				}

				if (ApplyWidgetOverride<WidgetType>(form, settingsJson))
					stats.applied++;
				else
					stats.failed++;
			}
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Error scanning {} override directory ({}): {}", label, folderPath.string(), e.what());
			stats.failed++;
		}

		LogOverrideStats(label, stats);
		return stats;
	}

	bool HasWeatherRecordOverrides(const json& weatherData)
	{
		if (!weatherData.is_object())
			return false;

		static constexpr std::string_view kRecordKeys[] = {
			"weatherProperties",
			"weatherColors",
			"fogProperties",
			"atmosphereColors",
			"dalc",
			"clouds",
			"precipitationDataRef",
			"referenceEffectRef",
		};

		for (const auto key : kRecordKeys) {
			if (weatherData.contains(key))
				return true;
		}

		for (size_t i = 0; i < ColorTimes::kTotal; i++) {
			if (weatherData.contains(std::format("imageSpaceRef_{}", i)) ||
				weatherData.contains(std::format("volumetricLightingRef_{}", i))) {
				return true;
			}
		}

		return false;
	}
}

void CSEditor::DataLoaded()
{
	s_dataAvailable = true;
	ApplySavedEditorOverrides();
}

bool CSEditor::CanOpenEditor()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* state = globals::state;
	return player && player->parentCell && s_dataAvailable && state &&
	       !state->isLoadingMenuOpen && !state->isMainMenuOpen;
}

bool CSEditor::HasWidgetJsonFiles()
{
	if (s_checkedWidgetJsonFiles)
		return s_hasWidgetJsonFiles;

	const auto communityShaderPath = Util::PathHelpers::GetCommunityShaderPath();
	for (const auto folderName : Widget::kSaveFolderNames) {
		const auto widgetSettingsPath = communityShaderPath / std::filesystem::path(std::string(folderName));
		std::error_code ec;
		const bool isDirectory = std::filesystem::is_directory(widgetSettingsPath, ec);
		if (ec) {
			// A missing folder is the normal case (the user simply has no saved
			// widgets for this category), so don't treat it as a warning.
			if (ec != std::errc::no_such_file_or_directory)
				logger::warn("[CSEditor] Failed to inspect widget settings path '{}': {}", widgetSettingsPath.string(), ec.message());
			continue;
		}
		if (!isDirectory)
			continue;

		for (std::filesystem::directory_iterator it(widgetSettingsPath, ec), end; !ec && it != end; it.increment(ec)) {
			std::error_code entryEc;
			const bool isRegularFile = it->is_regular_file(entryEc);
			if (entryEc) {
				logger::warn("[CSEditor] Failed to inspect widget settings file '{}': {}", it->path().string(), entryEc.message());
				continue;
			}
			if (isRegularFile && HasJsonExtension(it->path())) {
				logger::info("[CSEditor] Detected widget settings in '{}'", widgetSettingsPath.string());
				s_hasWidgetJsonFiles = true;
				s_checkedWidgetJsonFiles = true;
				return true;
			}
		}
		if (ec) {
			logger::warn("[CSEditor] Failed to scan widget settings path '{}': {}", widgetSettingsPath.string(), ec.message());
			continue;
		}
	}

	s_checkedWidgetJsonFiles = true;
	return false;
}

bool CSEditor::ShouldPreloadEditorResources()
{
	return s_dataAvailable && !s_resourcesInitialized && CanOpenEditor() && HasWidgetJsonFiles();
}

void CSEditor::EnsureDataLoaded()
{
	if (!s_dataAvailable)
		return;

	if (!s_resourcesInitialized) {
		EditorWindow::GetSingleton()->EnsureResources();
		s_resourcesInitialized = true;
	}
}

void CSEditor::OpenEditorWindow()
{
	if (!CanOpenEditor())
		return;

	EnsureDataLoaded();
	EditorWindow::GetSingleton()->open = true;
}

void CSEditor::ToggleEditorWindow()
{
	auto* editorWindow = EditorWindow::GetSingleton();
	if (!editorWindow)
		return;

	if (!editorWindow->open && !CanOpenEditor())
		return;
	if (!editorWindow->open)
		EnsureDataLoaded();
	editorWindow->open = !editorWindow->open;
}

void CSEditor::ApplySavedEditorOverrides()
{
	Widget::BeginWeatherReinitBatch();
	const SKSE::stl::scope_exit endWeatherReinitBatch([]() noexcept {
		Widget::EndWeatherReinitBatch();
	});

	ApplySavedWidgetOverrides<LightingTemplateWidget, RE::BGSLightingTemplate>("Lighting Templates", "lighting-template");
	ApplySavedWidgetOverrides<ImageSpaceWidget, RE::TESImageSpace>("ImageSpaces", "imagespace");
	ApplySavedWidgetOverrides<VolumetricLightingWidget, RE::BGSVolumetricLighting>("Volumetric Lighting", "volumetric-lighting");
	ApplySavedWidgetOverrides<PrecipitationWidget, RE::BGSShaderParticleGeometryData>("Precipitation", "precipitation");
	ApplySavedWidgetOverrides<ReferenceEffectWidget, RE::BGSReferenceEffect>("Visual Effects", "visual-effect");
	ApplySavedWidgetOverrides<LensFlareWidget, RE::BGSLensFlare>("Other Editor Widgets", "lens-flare", false);
	ApplySavedWidgetOverrides<CellLightingWidget, RE::TESObjectCELL>("Cell Lighting", "cell-lighting");

	ApplySavedWeatherOverrides();
}

void CSEditor::ApplySavedWeatherOverrides()
{
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler)
		return;

	const auto weathersPath = Util::PathHelpers::GetCommunityShaderPath() / "Weathers";
	std::error_code ec;
	if (!std::filesystem::exists(weathersPath, ec) || !std::filesystem::is_directory(weathersPath, ec))
		return;

	std::unordered_map<std::string, RE::TESWeather*> weatherByKey;
	auto& weatherArray = dataHandler->GetFormArray<RE::TESWeather>();
	weatherByKey.reserve(weatherArray.size());
	for (auto* weather : weatherArray) {
		if (!weather)
			continue;

		const auto fileKey = Util::GetFormFileKey(weather);
		weatherByKey.emplace(fileKey, weather);
		weatherByKey.emplace(std::format("Form_{}", fileKey), weather);
		if (const auto editorID = Util::GetFormEditorID(weather); !editorID.empty())
			weatherByKey.emplace(editorID, weather);
	}

	size_t appliedCount = 0;
	size_t skippedCount = 0;
	size_t failedCount = 0;
	try {
		for (const auto& entry : std::filesystem::directory_iterator(weathersPath)) {
			if (!entry.is_regular_file() || !HasJsonExtension(entry.path()))
				continue;

			json weatherData;
			std::string errorMessage;
			if (Util::FileHelpers::ReadJsonFile(entry.path(), weatherData, errorMessage) !=
				Util::FileHelpers::JsonFileReadResult::Success) {
				logger::warn("Failed to read weather override file ({}): {}", entry.path().string(), errorMessage);
				failedCount++;
				continue;
			}

			if (!weatherData.is_object()) {
				logger::warn("Skipping weather override file with non-object JSON: {}", entry.path().string());
				failedCount++;
				continue;
			}

			if (!HasWeatherRecordOverrides(weatherData)) {
				skippedCount++;
				continue;
			}

			const auto weatherKey = entry.path().stem().string();
			auto weatherIt = weatherByKey.find(weatherKey);
			if (weatherIt == weatherByKey.end()) {
				logger::warn("Weather override file has no matching weather record: {}", entry.path().string());
				failedCount++;
				continue;
			}

			if (WeatherWidget::ApplySavedSettings(weatherIt->second, weatherData))
				appliedCount++;
			else
				failedCount++;
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error scanning weather override directory ({}): {}", weathersPath.string(), e.what());
		return;
	}

	if (appliedCount > 0 || failedCount > 0) {
		logger::info("Applied saved weather record overrides: applied={}, skippedFeatureOnly={}, failed={}",
			appliedCount, skippedCount, failedCount);
	}
}

int8_t LerpInt8_t(const int8_t oldValue, const int8_t newVal, const float lerpValue)
{
	int lerpedValue = (int)std::lerp(oldValue, newVal, lerpValue);
	return (int8_t)std::clamp(lerpedValue, -128, 127);
}

uint8_t LerpUint8_t(const uint8_t oldValue, const uint8_t newVal, const float lerpValue)
{
	int lerpedValue = (int)std::lerp(oldValue, newVal, lerpValue);
	return (uint8_t)std::clamp(lerpedValue, 0, 255);
}

void LerpColor(const RE::TESWeather::Data::Color3& oldColor, RE::TESWeather::Data::Color3& newColor, const float changePct)
{
	newColor.red = LerpInt8_t(oldColor.red, newColor.red, changePct);
	newColor.green = LerpInt8_t(oldColor.green, newColor.green, changePct);
	newColor.blue = LerpInt8_t(oldColor.blue, newColor.blue, changePct);
}

void LerpColor(const RE::Color& oldColor, RE::Color& newColor, const float changePct)
{
	newColor.red = LerpUint8_t(oldColor.red, newColor.red, changePct);
	newColor.green = LerpUint8_t(oldColor.green, newColor.green, changePct);
	newColor.blue = LerpUint8_t(oldColor.blue, newColor.blue, changePct);
}

void LerpDirectional(RE::BGSDirectionalAmbientLightingColors::Directional& oldColor, RE::BGSDirectionalAmbientLightingColors::Directional& newColor, const float changePct)
{
	LerpColor(oldColor.x.max, newColor.x.max, changePct);
	LerpColor(oldColor.x.min, newColor.x.min, changePct);
	LerpColor(oldColor.y.max, newColor.y.max, changePct);
	LerpColor(oldColor.y.min, newColor.y.min, changePct);
	LerpColor(oldColor.z.max, newColor.z.max, changePct);
	LerpColor(oldColor.z.min, newColor.z.min, changePct);
}

void CSEditor::DrawLauncherButton()
{
	auto* state = globals::state;
	const bool canOpen = loaded && CanOpenEditor();
	ImGui::BeginDisabled(!canOpen);
	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
	if (ImGui::Button("Open CS Editor", { -1, 0 }))
		OpenEditorWindow();
	ImGui::PopStyleVar();
	ImGui::EndDisabled();

	if (!canOpen) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (!loaded) {
				ImGui::TextUnformatted("CS Editor is not loaded.");
			} else if (!state) {
				ImGui::TextUnformatted("CS Editor is not ready.");
			} else if (state->isLoadingMenuOpen) {
				ImGui::TextUnformatted("CS Editor cannot be opened during a loading screen.");
			} else {
				ImGui::TextUnformatted("Enter the game world to open CS Editor.");
			}
		}
	}
}

void CSEditor::Prepass()
{
	if (ShouldPreloadEditorResources()) {
		EnsureDataLoaded();
	}
	EditorWindow::GetSingleton()->UpdateOpenState();
	UpdateWeatherLockAndTime();
}

void CSEditor::UpdateWeatherLockAndTime()
{
	auto editorWindow = EditorWindow::GetSingleton();
	if (editorWindow->IsWeatherLocked()) {
		auto lockedWeather = editorWindow->GetLockedWeather();
		auto sky = globals::game::sky;
		if (sky && lockedWeather && sky->currentWeather != lockedWeather) {
			sky->ForceWeather(lockedWeather, false);
		}
	}

	editorWindow->UpdateTimeState();
}

void CSEditor::LerpWeather(RE::TESWeather* oldWeather, RE::TESWeather* newWeather, float currentWeatherPct)
{
	if (!oldWeather || !newWeather) {
		// Avoid dereferencing null pointers; nothing to lerp.
		return;
	}

	//// Precipitation
	newWeather->data.precipitationBeginFadeIn = LerpUint8_t(oldWeather->data.precipitationBeginFadeIn, newWeather->data.precipitationBeginFadeIn, currentWeatherPct);
	newWeather->data.precipitationEndFadeOut = LerpUint8_t(oldWeather->data.precipitationEndFadeOut, newWeather->data.precipitationEndFadeOut, currentWeatherPct);

	//// Sun
	newWeather->data.sunDamage = LerpUint8_t(oldWeather->data.sunDamage, newWeather->data.sunDamage, currentWeatherPct);
	newWeather->data.sunGlare = LerpUint8_t(oldWeather->data.sunGlare, newWeather->data.sunGlare, currentWeatherPct);

	//// Lightning
	newWeather->data.thunderLightningBeginFadeIn = LerpUint8_t(oldWeather->data.thunderLightningBeginFadeIn, newWeather->data.thunderLightningBeginFadeIn, currentWeatherPct);
	newWeather->data.thunderLightningEndFadeOut = LerpUint8_t(oldWeather->data.thunderLightningEndFadeOut, newWeather->data.thunderLightningEndFadeOut, currentWeatherPct);
	newWeather->data.thunderLightningFrequency = (int8_t)LerpUint8_t((uint8_t)oldWeather->data.thunderLightningFrequency, (uint8_t)newWeather->data.thunderLightningFrequency, currentWeatherPct);
	LerpColor(oldWeather->data.lightningColor, newWeather->data.lightningColor, currentWeatherPct);

	//// Trans delta
	newWeather->data.transDelta = LerpUint8_t(oldWeather->data.transDelta, newWeather->data.transDelta, currentWeatherPct);

	//// Visual Effects
	newWeather->data.visualEffectBegin = LerpUint8_t(oldWeather->data.visualEffectBegin, newWeather->data.visualEffectBegin, currentWeatherPct);
	newWeather->data.visualEffectEnd = LerpUint8_t(oldWeather->data.visualEffectEnd, newWeather->data.visualEffectEnd, currentWeatherPct);

	//// Wind
	newWeather->data.windDirection = LerpUint8_t(oldWeather->data.windDirection, newWeather->data.windDirection, currentWeatherPct);
	newWeather->data.windDirectionRange = LerpUint8_t(oldWeather->data.windDirectionRange, newWeather->data.windDirectionRange, currentWeatherPct);
	newWeather->data.windSpeed = LerpUint8_t(oldWeather->data.windSpeed, newWeather->data.windSpeed, currentWeatherPct);

	//// Fog
	newWeather->fogData.dayFar = std::lerp(oldWeather->fogData.dayFar, newWeather->fogData.dayFar, currentWeatherPct);
	newWeather->fogData.dayMax = std::lerp(oldWeather->fogData.dayMax, newWeather->fogData.dayMax, currentWeatherPct);
	newWeather->fogData.dayNear = std::lerp(oldWeather->fogData.dayNear, newWeather->fogData.dayNear, currentWeatherPct);
	newWeather->fogData.dayPower = std::lerp(oldWeather->fogData.dayPower, newWeather->fogData.dayPower, currentWeatherPct);

	newWeather->fogData.nightFar = std::lerp(oldWeather->fogData.nightFar, newWeather->fogData.nightFar, currentWeatherPct);
	newWeather->fogData.nightMax = std::lerp(oldWeather->fogData.nightMax, newWeather->fogData.nightMax, currentWeatherPct);
	newWeather->fogData.nightNear = std::lerp(oldWeather->fogData.nightNear, newWeather->fogData.nightNear, currentWeatherPct);
	newWeather->fogData.nightPower = std::lerp(oldWeather->fogData.nightPower, newWeather->fogData.nightPower, currentWeatherPct);

	//// Weather colors
	for (size_t i = 0; i < RE::TESWeather::ColorTypes::kTotal; i++) {
		for (size_t j = 0; j < RE::TESWeather::ColorTime::kTotal; j++) {
			LerpColor(oldWeather->colorData[i][j], newWeather->colorData[i][j], currentWeatherPct);
		}
	}

	//// DALC
	for (size_t i = 0; i < RE::TESWeather::ColorTime::kTotal; i++) {
		auto& newDALC = newWeather->directionalAmbientLightingColors[i];
		auto& oldDALC = oldWeather->directionalAmbientLightingColors[i];

		LerpColor(oldDALC.specular, newDALC.specular, currentWeatherPct);
		newWeather->directionalAmbientLightingColors[i].fresnelPower = std::lerp(oldDALC.fresnelPower, newDALC.fresnelPower, currentWeatherPct);
		LerpDirectional(oldDALC.directional, newDALC.directional, currentWeatherPct);
	}

	//// Clouds
	for (size_t i = 0; i < RE::TESWeather::kTotalLayers; i++) {
		for (size_t j = 0; j < RE::TESWeather::ColorTime::kTotal; j++) {
			LerpColor(oldWeather->cloudColorData[i][j], newWeather->cloudColorData[i][j], currentWeatherPct);
			newWeather->cloudAlpha[i][j] = std::lerp(oldWeather->cloudAlpha[i][j], newWeather->cloudAlpha[i][j], currentWeatherPct);
		}

		newWeather->cloudLayerSpeedY[i] = LerpInt8_t(oldWeather->cloudLayerSpeedY[i], newWeather->cloudLayerSpeedY[i], currentWeatherPct);
		newWeather->cloudLayerSpeedX[i] = LerpInt8_t(oldWeather->cloudLayerSpeedX[i], newWeather->cloudLayerSpeedX[i], currentWeatherPct);
	}
}
