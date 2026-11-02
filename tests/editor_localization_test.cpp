#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

static std::map<std::string, std::string> translations, defaults;
struct I18n
{
	std::string locale = "pl";
	static I18n* GetSingleton()
	{
		static I18n value;
		return &value;
	}
	std::string GetCurrentLocale() const { return locale; }
};
bool ContainsStringIgnoreCase(std::string_view, std::string_view);
const char* T(const char* key, const char* fallback)
{
	if (const auto found = translations.find(key); found != translations.end())
		return found->second.c_str();
	return defaults.try_emplace(key, fallback).first->second.c_str();
}
namespace ImGui
{
	inline bool active = false, changed = false;
	inline double time = 1.0;
	inline float editedValue = 1.0f;
	inline std::string drawnLabel, drawnText;
	bool IsItemActive() { return active; }
	bool IsItemHovered() { return true; }
	double GetTime() { return time; }
	void Text(const char* format, ...)
	{
		// Do not invoke C varargs UB: a plain translated string containing '%' is data.
		if (std::strchr(format, '%'))
			throw std::runtime_error("translated text was interpreted as a printf format");
		drawnText = format;
	}
	void TextUnformatted(const char* text) { drawnText = text; }
}
namespace Util::Widgets
{
	bool SliderFloat(const char* label, float* value, float, float, const char*)
	{
		ImGui::drawnLabel = label;
		if (ImGui::changed)
			*value = ImGui::editedValue;
		return ImGui::changed;
	}
}
struct Widget
{
	struct SearchResult
	{
		std::string displayName, tabName, settingId;
	};
	std::vector<SearchResult> searchResults;
	std::string searchResultsForQuery, searchResultsForLocale;
	char searchBuffer[64] = {};
	std::string activeTabOverride, highlightedSetting, highlightedDisplaySetting;
	float highlightStartTime = 0;
	bool scrollToHighlighted = false;
	int searches = 0;
	std::vector<SearchResult> CollectSearchableSettings()
	{
		++searches;
		return { { "Wind Speed", "Basic", "Wind Speed" }, { "Ambient Color", "Basic", "Ambient Color" } };
	}
	void UpdateSearchForTest();
	void NavigateToSearchResult(const SearchResult&);
	float property = 0.25f;
	std::string searched, highlighted;
	bool MatchesSearch(const std::string& key)
	{
		searched = key;
		return true;
	}
	std::string GetStableIdentity() const { return "Weather:Test.esp:123"; }
	template <class Draw>
	auto DrawWithHighlight(const std::string& key, Draw draw)
	{
		highlighted = key;
		return draw();
	}
};
struct EditorWindow
{
	std::vector<float> undo;
	static EditorWindow* GetSingleton()
	{
		static EditorWindow value;
		return &value;
	}
	void PushUndoState(Widget* widget) { undo.push_back(widget->property); }
};
struct PaletteWindow
{
	std::vector<std::pair<std::string, float>> usage;
	static PaletteWindow* GetSingleton()
	{
		static PaletteWindow value;
		return &value;
	}
	void TrackValueUsage(std::string key, float value) { usage.emplace_back(std::move(key), value); }
};
namespace WeatherUtils
{
	const char* TranslateControlLabel(std::string_view);
	std::string BuildLocalizedControlLabel(const std::string&);
}
namespace RE
{
	struct TESWeather
	{
		std::string key;
	};
}
#include "editor_localization_tracker.h"
#include "editor_localization_under_test.h"
#include "editor_weather_status_under_test.h"

int main(int argc, char** argv)
{
	const std::string_view selection = argc > 1 ? argv[1] : "all";
	translations = { { "cs_editor.wind_speed", "Wiatr 100%" },
		{ "cs_editor.tod_day", "Dzien" }, { "cs_editor.category_weather", "Pogoda" },
		{ "cs_editor.categories", "Kategorie 100%" } };
	if (selection == "all" || selection == "controls") {
		Widget widget;
		ImGui::active = ImGui::changed = true;
		ImGui::editedValue = 0.75f;
		assert(WeatherUtils::DrawSliderFloat("Wind Speed", widget.property, 0, 1, &widget, "%.3f"));
		assert(ImGui::drawnLabel.starts_with("Wiatr 100%"));
		assert(widget.searched == "Wind Speed" && widget.highlighted == "Wind Speed");
		assert(widget.property == 0.75f);
		assert(EditorWindow::GetSingleton()->undo == std::vector<float>{ 0.25f });
		translations["cs_editor.wind_speed"] = "Windgeschwindigkeit";
		ImGui::editedValue = 0.9f;
		WeatherUtils::DrawSliderFloat("Wind Speed", widget.property, 0, 1, &widget, "%.3f");
		assert(ImGui::drawnLabel.starts_with("Windgeschwindigkeit"));
		assert(EditorWindow::GetSingleton()->undo.size() == 1);
		ImGui::active = ImGui::changed = false;
		ImGui::time = 4.0;
		WeatherUtils::DrawSliderFloat("Wind Speed", widget.property, 0, 1, &widget, "%.3f");
		const auto& usage = PaletteWindow::GetSingleton()->usage;
		assert(usage.size() == 1 && usage[0].first == "Wind Speed" && usage[0].second == 0.9f);
		WeatherUtils::DrawSliderFloat("##Wind Speed", widget.property, 0, 1, &widget, "%.3f");
		assert(ImGui::drawnLabel == "##Wind Speed" && widget.searched == "Wind Speed");
	}
	if (selection == "all" || selection == "period") {
		assert(std::string_view(TOD::RenderedPeriod(1)) == "Dzien");
		assert(std::string_view(TOD::GetPeriodName(1)) == "Day");
	}
	if (selection == "all" || selection == "category") {
		assert(WeatherTooltipVisible("Weather"));
		assert(!WeatherTooltipVisible("Pogoda"));
	}
	if (selection == "all" || selection == "text") {
		DrawCategoryHeading();
		assert(ImGui::drawnText == "Kategorie 100%");
	}
	if (selection == "all" || selection == "search") {
		Widget widget;
		translations["cs_editor.wind_speed"] = "Wiatr";
		std::copy_n("Wiatr", sizeof("Wiatr"), widget.searchBuffer);
		widget.UpdateSearchForTest();
		assert(widget.searchResults.size() == 1 && widget.searches == 1);
		widget.NavigateToSearchResult(widget.searchResults[0]);
		assert(widget.activeTabOverride == "Basic" && widget.highlightedSetting == "Wind Speed");
		widget.UpdateSearchForTest();
		assert(widget.searches == 1);
		translations["cs_editor.wind_speed"] = "Windgeschwindigkeit";
		I18n::GetSingleton()->locale = "de";
		widget.UpdateSearchForTest();
		assert(widget.searchResults.empty() && widget.searches == 2);
		std::copy_n("Wind Speed", sizeof("Wind Speed"), widget.searchBuffer);
		widget.UpdateSearchForTest();
		assert(widget.searchResults.size() == 1);
	}
#ifndef EDITOR_BASELINE_NO_WEATHER_QUERY
	if (selection == "all" || selection == "weather") {
		WeatherManager manager;
		RE::TESWeather weather{ "123~Test.esp" }, other{ "456~Test.esp" };
		assert(!manager.HasWeatherSettings(nullptr) && !manager.HasWeatherSettings(&weather));
		manager.perWeatherSettingsCache.emplace(weather.key, 7);
		assert(manager.HasWeatherSettings(&weather) && !manager.HasWeatherSettings(&other));
		assert(manager.perWeatherSettingsCache.size() == 1);
		manager.perWeatherSettingsCache.clear();
		assert(!manager.HasWeatherSettings(&weather));
	}
#endif
	std::cout << "Editor localization, stable identities and native weather status passed\n";
}
