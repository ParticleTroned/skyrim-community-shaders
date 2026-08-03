#include "Menu/PerformanceTuningJson.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

TEST_CASE(
	"Float-backed settings tolerate JSON round-trip precision only",
	"[performance-tuning][defaults][json]")
{
	const nlohmann::json savedDefaults = {
		{ "Skylighting", { { "MinDiffuseVisibility", 0.1 } } }
	};
	const nlohmann::json floatRoundTrip = {
		{ "Skylighting",
			{ { "MinDiffuseVisibility",
				static_cast<double>(static_cast<float>(0.1)) } } }
	};
	const nlohmann::json meaningfullyClamped = {
		{ "Skylighting", { { "MinDiffuseVisibility", 0.1001 } } }
	};

	REQUIRE(
		PerformanceTuning::AreJsonValuesEquivalent(
			savedDefaults,
			floatRoundTrip));
	REQUIRE_FALSE(
		PerformanceTuning::AreJsonValuesEquivalent(
			savedDefaults,
			meaningfullyClamped));
}

TEST_CASE(
	"Defaults equivalence retains exact structure and non-numeric values",
	"[performance-tuning][defaults][json]")
{
	const nlohmann::json expected = {
		{ "Enable", true },
		{ "Nested", { { "Mode", "High" } } }
	};

	auto changedValue = expected;
	changedValue["Enable"] = false;
	auto addedValue = expected;
	addedValue["Extra"] = 1;

	REQUIRE_FALSE(
		PerformanceTuning::AreJsonValuesEquivalent(
			expected,
			changedValue));
	REQUIRE_FALSE(
		PerformanceTuning::AreJsonValuesEquivalent(
			expected,
			addedValue));
}

TEST_CASE(
	"Mixed floating point and integer settings require exact numeric values",
	"[performance-tuning][defaults][json]")
{
	const nlohmann::json exactFloat = 3.0;
	const nlohmann::json exactInteger = 3;
	const nlohmann::json fractionalFloat = 3.0000001;
	const nlohmann::json roundedLargeFloat = 9007199254740992.0;
	const nlohmann::json distinctLargeInteger = 9007199254740993ULL;

	REQUIRE(
		PerformanceTuning::AreJsonValuesEquivalent(
			exactFloat,
			exactInteger));
	REQUIRE_FALSE(
		PerformanceTuning::AreJsonValuesEquivalent(
			fractionalFloat,
			exactInteger));
	REQUIRE_FALSE(
		PerformanceTuning::AreJsonValuesEquivalent(
			roundedLargeFloat,
			distinctLargeInteger));
}
