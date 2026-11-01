"""Exercise production editor label boundaries and unchanged search/undo identities."""

import argparse
from pathlib import Path
import re

from extract_adaptive_balance_toggle import function


def extract(root, output):
    source = (root / "src/CSEditor/WeatherUtils.cpp").read_text(encoding="utf-8-sig")
    header = (root / "src/CSEditor/WeatherUtils.h").read_text(encoding="utf-8-sig")
    editor = (root / "src/CSEditor/EditorWindow.cpp").read_text(encoding="utf-8-sig")
    widget = (root / "src/CSEditor/Widget.cpp").read_text(encoding="utf-8-sig")
    weather = (root / "src/WeatherManager.cpp").read_text(encoding="utf-8-sig")
    weather_header = (root / "src/WeatherManager.h").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    tracker = "template <typename T>\n" + function(header, "class DebouncedTracker") + ";\n"
    (output / "editor_localization_tracker.h").write_text(tracker, encoding="utf-8")
    parts = ["static Widget* g_currentWidget = nullptr;", 'constexpr std::string_view kScopeSep = "::";']
    for marker, prefix in (("static void PushUndoWithPreviousValue(", "template <class T>\n"),
                           ("auto DrawWithWidgetHighlight(", "template <class DrawFn>\n"),
                           ("static std::string WidgetScopedKey(", ""),
                           ("static std::string_view UnscopeKey(", "")):
        parts.append(prefix + function(source, marker))
    if "const char* WeatherUtils::TranslateControlLabel(" in source:
        parts.extend([function(source, "const char* WeatherUtils::TranslateControlLabel("),
                      function(source, "std::string WeatherUtils::BuildLocalizedControlLabel(")])
    parts.append("namespace WeatherUtils {\nstatic DebouncedTracker<float> s_floatTracker;\n"
                 "template <class T, class DrawFn>\n" + function(source, "\tbool DrawTrackedSlider(")
                 + function(source, "\tbool DrawSliderFloat(") + "}\n")
    parts.append("namespace TOD {\nenum Period { Sunrise, Day, Sunset, Night, Count };\n"
                 + function(source, "\tconst char* GetPeriodName(")
                 + (function(source, "\tconst char* GetPeriodLabel(") if "GetPeriodLabel(" in source else "") + "}\n")
    period_call = next(line for line in source.splitlines() if "const char* name = GetPeriod" in line)
    parts.append("namespace TOD {\nconst char* RenderedPeriod(int i) {\n" + period_call + "\nreturn name;\n}\n}\n")
    condition = next(line.strip() for line in editor.splitlines() if "if (!weatherTooltipShownThisFrame &&" in line)
    parts.append("bool WeatherTooltipVisible(std::string m_selectedCategory) {\nbool weatherTooltipShownThisFrame = false;\n"
                 + condition + " return true; }\nreturn false;\n}\n")
    plain_text = next(line.strip() for line in editor.splitlines() if 'T("cs_editor.categories", "Categories")' in line and 'ImGui::Text' in line)
    parts.append("void DrawCategoryHeading() {\n" + plain_text + "\n}\n")
    draw_header = function(widget, "void Widget::DrawWidgetHeader(")
    cache = function(draw_header, "\tif (searchBuffer[0] != '\\0')")
    parts.append("void Widget::UpdateSearchForTest() {\n" + cache + "\n}\n")
    parts.append(function(source, "bool ContainsStringIgnoreCase("))
    parts.append(function(widget, "void Widget::NavigateToSearchResult("))
    (output / "editor_localization_under_test.h").write_text("\n".join(parts), encoding="utf-8")
    declaration = re.search(r"bool HasWeatherSettings\([^;]+;", weather_header)
    if declaration:
        weather_parts = "struct WeatherManager {\nstd::map<std::string, int> perWeatherSettingsCache;\n"
        weather_parts += "static std::string GetWeatherKey(RE::TESWeather* weather) { return weather->key; }\n"
        weather_parts += declaration.group(0) + "\n};\n"
        weather_parts += function(weather, "bool WeatherManager::HasWeatherSettings(")
    else:
        weather_parts = "// The baseline has no callable status query.\n"
    (output / "editor_weather_status_under_test.h").write_text(weather_parts, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
