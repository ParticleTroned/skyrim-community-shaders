#include "Features/SeasonalWeatherConfig.h"
#include "Utils/SettingsValidation.h"

#include <limits>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Reject(const nlohmann::json& input)
	{
		try {
			SeasonalWeatherConfig::Parse(input);
		} catch (const std::invalid_argument&) {
			return;
		}
		throw std::runtime_error("Invalid seasonal configuration was accepted");
	}
}

int main()
{
	using namespace SeasonalWeatherPolicy;
	const auto& weights = DefaultWeights;
	Require(Multiplier(weights, 0, Category::Snow) == 4, "Winter snow weighting");
	Require(Multiplier(weights, 6, Category::Snow) == 0.1f, "Summer snow weighting");
	Require(Multiplier(weights, 12, Category::Snow) == Multiplier(weights, 0, Category::Snow), "Year wrap");
	Require(Multiplier(weights, -0.5f, Category::Snow) == Multiplier(weights, 11.5f, Category::Snow), "Negative calendar wrap");
	auto distinguishMonths = weights;
	distinguishMonths.front()[3] = 7;
	Require(Multiplier(distinguishMonths, -0.0000001f, Category::Snow) == 7, "Tiny negative month must not index beyond December");
	Require(Multiplier(weights, 0.5f, Category::Snow) == 3.5f, "Monthly interpolation");
	Require(Multiplier(weights, 0, Category::Unchanged) == 1, "Unclassified weather must be retained");
	Require(Multiplier(weights, std::numeric_limits<float>::quiet_NaN(), Category::Snow) == 1, "Invalid clock fallback");

	const std::array<double, 4> regionalWeights{ 40, 20, 20, 20 };
	std::array<double, 4> winter{};
	for (size_t i = 0; i < winter.size(); ++i)
		winter[i] = regionalWeights[i] * Multiplier(weights, 0, static_cast<Category>(i));
	const auto chances = Percentages(winter);
	Require(chances == std::vector<uint32_t>({ 21, 15, 4, 60 }), "Regional normalization must preserve category weights");
	Require(std::accumulate(chances.begin(), chances.end(), 0u) == 100, "Native probability sum");
	const std::array<double, 3> zeros{ 0, 0, 0 };
	Require(Percentages(zeros).empty(), "Zero total must retain native selection");
	const std::array<double, 4> excluded{ 0, 1, 1, 1 };
	Require(Percentages(excluded) == std::vector<uint32_t>({ 0, 34, 33, 33 }), "Zero entries must remain excluded");
	const std::array<double, 2> ratio{ 1, 3 };
	Require(Percentages(ratio) == std::vector<uint32_t>({ 25, 75 }), "Relative weights within a category");
	const std::array<double, 2> invalid{ 1, std::numeric_limits<double>::infinity() };
	Require(Percentages(invalid).empty(), "Nonfinite native weight fallback");
	const std::array<double, 2> negative{ 1, -1 };
	Require(Percentages(negative).empty(), "Negative native weight fallback");
	Require(Percentages(std::vector<double>(MaxCandidates + 1, 1)).empty(), "Bounded candidate allocation");

	using namespace SeasonalWeatherConfig;
	const auto defaults = Serialize(Settings{});
	Require(!Parse(defaults).Enabled, "Opt-in by default");
	Require(Parse(defaults).SkipGlobalControlledRegions, "External controller ownership by default");
	Require(Serialize(Parse(defaults)) == defaults, "Settings round trip");
	Require(Parse({ { "Enabled", true } }).Enabled, "Partial settings restore defaults");
	Reject({ { "Enabled", 1 } });
	Reject({ { "Unknown", true } });
	Reject({ { "MonthlyWeights", nlohmann::json::array() } });
	Reject({ { "WeatherCategories", { { { "Weather", "0x1~Skyrim.esm" }, { "Category", 1.5 } } } } });
	Reject({ { "SnowAssignments", { { { "Region", "0x1~Skyrim.esm" }, { "Weight", 1 } } } } });
	Reject({ { "ExcludedWorldspaces", { std::string("Tamriel\0suffix", 14) } } });
	Reject({ { "ExcludedWorldspaces", { std::string(256, 'x') } } });
	Reject({ { "ExcludedWorldspaces", std::vector<std::string>(MaxAssignments + 1, "Tamriel") } });
	const nlohmann::json floatRange{ { "type", "number" }, { "minimum", 0.01 }, { "maximum", 0.95 } };
	const auto noStringRule = [](const std::string&, const nlohmann::json&) {};
	Util::ValidateSettingsSchema<float>(0.01f, floatRange, "floatMinimum", noStringRule);
	Util::ValidateSettingsSchema<float>(0.95f, floatRange, "floatMaximum", noStringRule);
	auto badWeights = defaults;
	badWeights["MonthlyWeights"][0][3] = MaxMultiplier + 1;
	Reject(badWeights);
	badWeights["MonthlyWeights"][0][3] = std::numeric_limits<double>::quiet_NaN();
	Reject(badWeights);
	Reject({ { "ExcludedRegions", { "" } } });
	Reject({ { "ExcludedRegions", { "duplicate", "duplicate" } } });
	Reject({ { "SnowAssignments", { { { "Region", "0x1~Skyrim.esm" }, { "Weather", "0x2~Skyrim.esm" }, { "Weight", 0 } } } } });
	Reject({ { "SnowAssignments", { { { "Region", "missing-weather" }, { "Weight", 1 } } } } });
	Reject({ { "WeatherCategories", { { { "Weather", "0x1~Skyrim.esm" }, { "Category", 5 } } } } });
	const auto assignments = nlohmann::json::array({ { { "Region", "0x1~Skyrim.esm" }, { "Weather", "0x2~Skyrim.esm" }, { "Weight", 20 } } });
	Require(Parse({ { "SnowAssignments", assignments } }).SnowAssignments.size() == 1, "Portable regional assignment round trip");
	auto minimumWeight = assignments;
	minimumWeight[0]["Weight"] = MinAssignmentWeight;
	const auto minimumSettings = Parse({ { "SnowAssignments", minimumWeight } });
	Require(Parse(Serialize(minimumSettings)).SnowAssignments.front().Weight == MinAssignmentWeight, "Minimum assignment weight must survive menu and disk round trips");
	auto duplicates = assignments;
	duplicates.push_back(assignments.front());
	Reject({ { "SnowAssignments", duplicates } });
	duplicates[1]["Region"] = "0X000001~SKYRIM.ESM";
	duplicates[1]["Weather"] = "0x000002~skyrim.esm";
	Reject({ { "SnowAssignments", duplicates } });
	Reject({ { "ExcludedRegions", { "0x000001~Skyrim.esm", "0X1~SKYRIM.ESM" } } });
	Reject({ { "ExcludedWorldspaces", { "Tamriel", "TAMRIEL" } } });
	Reject({ { "WeatherCategories", { { { "Weather", "0x000001~Skyrim.esm" }, { "Category", 0 } },
										{ { "Weather", "0X1~SKYRIM.ESM" }, { "Category", 3 } } } } });
	Reject({ { "ExcludedRegions", { "0x0~Skyrim.esm" } } });
	Reject({ { "ExcludedRegions", { "0x1~Bad\nName.esp" } } });
	Reject({ { "ExcludedRegions", { "0x1~Bad|Name.esp" } } });
	Require(FormKey("0X000AbC~Skyrim.ESM") == "0xabc~skyrim.esm", "Portable keys must share a canonical identity");
	Require(Parse({ { "ExcludedWorldspaces", { "Tamriel" } } }).ExcludedWorldspaces.front() == "tamriel", "Editor exclusions use canonical identity");
	Require(Serialize(Settings{}) == defaults, "Rejected changes must not alter defaults");
}
