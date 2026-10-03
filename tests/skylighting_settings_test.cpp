#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

struct Skylighting
{
	struct Settings
	{
		float MaxZenith = 3.1415926f / 2.0f;
	};
};

#include "skylighting_settings_under_test.h"

int main()
{
	const float defaultAngle = Skylighting::Settings{}.MaxZenith;
	struct TestCase
	{
		const char* name;
		float input;
		float expected;
	};
	const TestCase cases[]{
		{ "negative", -1.0f, 0.0f },
		{ "above horizon", 2.0f, defaultAngle },
		{ "NaN", std::numeric_limits<float>::quiet_NaN(), defaultAngle },
		{ "positive infinity", std::numeric_limits<float>::infinity(), defaultAngle },
		{ "negative infinity", -std::numeric_limits<float>::infinity(), defaultAngle },
		{ "vertical", 0.0f, 0.0f },
		{ "intermediate", 0.7f, 0.7f },
		{ "default", defaultAngle, defaultAngle },
	};

	for (const auto& test : cases) {
		Skylighting::Settings settings{ test.input };
		NormalizeSettingsForRuntime(settings);
		if (!std::isfinite(settings.MaxZenith) || settings.MaxZenith != test.expected) {
			std::cerr << test.name << ": got " << settings.MaxZenith << ", expected " << test.expected << '\n';
			return 1;
		}
		NormalizeSettingsForRuntime(settings);
		if (settings.MaxZenith != test.expected)
			return 2;
	}

	std::cout << std::size(cases) << " Skylighting angle cases passed\n";
}
