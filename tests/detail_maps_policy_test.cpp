#include "Features/DetailMapsConfig.h"
#include "Features/DetailMapsPolicy.h"
#include <array>
#include <iostream>
#include <stdexcept>

int main()
{
	try {
		using nlohmann::json;
		DetailMapsConfig::Settings settings;
		settings.Tiling = 3;
		settings.NormalStrength = 2;
		const json original = settings;
		settings = DetailMapsConfig::Patch(settings, { { "Enabled", false } });
		if (settings.Enabled || settings.Tiling != 3 || settings.NormalStrength != 2)
			throw std::runtime_error("Disabling detail changed the saved appearance");
		settings = DetailMapsConfig::Patch(settings, { { "Enabled", true } });
		if (json(settings) != original)
			throw std::runtime_error("Enabling detail did not restore the appearance");
		for (const json& invalid : { json{ { "Enabled", 1 } }, json{ { "Tiling", "1" } }, json{ { "Tiling", 0 } },
				 json{ { "FadeStart", 1300 } }, json{ { "Unknown", true } }, json{ { "NormalStrength", -1 } },
				 json{ { "Tiling", std::numeric_limits<double>::infinity() } }, json::array() }) {
			bool rejected = false;
			try {
				settings = DetailMapsConfig::Patch(settings, invalid);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			if (!rejected || json(settings) != original)
				throw std::runtime_error("Invalid settings edit was not rejected atomically");
		}
		if (DetailMapsConfig::Patch(settings, { { "Tiling", 0.1 } }).Tiling != 0.1f)
			throw std::runtime_error("Minimum tiling value was rejected");
		settings.Tiling = std::numeric_limits<float>::quiet_NaN();
		settings.FadeStart = 9000;
		settings.FadeEnd = -1;
		DetailMapsConfig::Clamp(settings);
		if (settings.Tiling != 1 || settings.FadeStart != 8000 || settings.FadeEnd != 8001)
			throw std::runtime_error("Saved settings were not sanitized");
		for (const auto& field : DetailMapsConfig::Numbers) {
			const auto schema = DetailMapsConfig::Properties().at(field.name);
			if (schema.at("minimum") != field.minimum || schema.at("maximum") != field.maximum)
				throw std::runtime_error("DevBench ranges disagree with settings ranges");
		}
		using namespace DetailMapsPolicy;
		struct Case
		{
			const char* path;
			const char* pattern;
			bool expected;
		};
		const std::array cases{
			Case{ R"(textures\wall.dds)", R"(textures\*.dds)", true },
			Case{ R"(textures\a\wall.dds)", R"(textures\*.dds)", false },
			Case{ R"(textures\wall.dds)", R"(textures\**\wall.dds)", true },
			Case{ R"(textures\a\b\wall.dds)", R"(textures\**\wall.dds)", true },
			Case{ R"(textures\a\brick01_d.dds)", R"(**\brick0?_d.dds)", true },
			Case{ R"(textures\a\brick001_d.dds)", R"(**\brick0?_d.dds)", false },
			Case{ R"(textures\wood.dds)", R"(**\*wood*)", true },
			Case{ R"(textures\oldwood.dds)", R"(**\wood.dds)", false },
			Case{ R"(textures\wood.dds)", R"(textures\wood)", false },
			Case{ R"(textures\wood.dds)", R"(textures\**)", true },
			Case{ R"(a\b)", "a?b", false },
			Case{ "ab", "a**b", true },
			Case{ "a", "**", true },
			Case{ "", "**", true },
			Case{ "", "", false },
			Case{ R"(a\wall.dds)", R"(**\**\wall.dds)", true }
		};
		for (const auto& test : cases)
			if (PathMatcher(test.path).Matches(test.pattern) != test.expected)
				throw std::runtime_error(std::string("Pattern mismatch: ") + test.pattern + " / " + test.path);
		if (NormalizePath("Data/Textures/PBR/MyMod/Stone.DDS") != R"(textures\mymod\stone.dds)" ||
			NormalizePath("MyMod/Stone.DDS") != R"(textures\mymod\stone.dds)")
			throw std::runtime_error("PBR and legacy paths must select the same rules");
		if (PathMatcher(std::string(MaxPathLength + 1, 'a')).Matches("**") ||
			PathMatcher("a").Matches(std::string(MaxPatternLength + 1, '*')))
			throw std::runtime_error("Unbounded pattern input accepted");
		PathMatcher reusable(R"(textures\stone.dds)");
		if (!reusable.Matches("**") || reusable.Matches("wood*") || !reusable.Matches(R"(**\stone.dds)"))
			throw std::runtime_error("Previous pattern contaminated later matches");
		for (uint32_t technique = 0; technique < 20; ++technique) {
			const bool expected = technique == 0 || technique == 1 || technique == 2 || technique == 3 ||
			                      technique == 7 || technique == 11 || technique == 12 || technique == 14;
			const auto descriptor = (technique << 24) | (1u << 4);
			if (IsEligible(descriptor, false) != expected)
				throw std::runtime_error("Lighting eligibility differs from the pack contract");
			for (uint32_t excluded : { 1u << 2, 1u << 6, 1u << 18, 1u << 30, 1u << 31, ShaderFlag })
				if (IsEligible(descriptor | excluded, true))
					throw std::runtime_error("Excluded draw was admitted");
		}
		if (IsEligible(0, false) || !IsEligible(0, true))
			throw std::runtime_error("Forward eligibility ignored the first-person restriction");
		std::cout << "Settings toggles, atomic validation, " << cases.size() << " path cases, input limits and lighting eligibility passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
