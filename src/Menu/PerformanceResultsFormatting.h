#pragma once

#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>

namespace PerformanceResults::ReportFormatting
{
	using Json = nlohmann::json;
	inline std::string Text(const Json& value)
	{
		return value.is_null() ? "Unavailable" : value.is_string() ? value.get<std::string>() :
		                                                             value.dump();
	}

	inline std::string Display(const Json& value)
	{
		if (!value.is_number_float())
			return Text(value);
		std::ostringstream number;
		const auto magnitude = std::abs(value.get<double>());
		if (magnitude > 0 && magnitude < 0.000001)
			number << std::scientific << std::setprecision(3);
		else
			number << std::fixed << std::setprecision(magnitude > 0 && magnitude < 0.01 ? 6 : 3);
		number << value.get<double>();
		return number.str();
	}

	inline std::string Escape(std::string_view text)
	{
		std::string result;
		for (char c : text) {
			switch (c) {
			case '&':
				result += "&amp;";
				break;
			case '<':
				result += "&lt;";
				break;
			case '>':
				result += "&gt;";
				break;
			case '"':
				result += "&quot;";
				break;
			case '\'':
				result += "&#39;";
				break;
			default:
				result += c;
				break;
			}
		}
		return result;
	}

	inline const char* Semantics(const Json& record)
	{
		return record.at("kind") == "quick_scan" ?
		           "Feature rows show instrumented self-time per frame. CSX and fpsVR whole-frame timings are listed separately; shared and uninstrumented work is excluded from feature rows." :
		           "Current minus comparison (Off/None), with standard error. Negative FPS values mean fewer frames per second with the feature enabled. Independent feature costs must not be added together.";
	}

}
