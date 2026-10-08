#include "Features/VR/StabilizerSettings.h"
#include "VRAPI/VRFpsStabilizerInterface001.h"

#include <iostream>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace
{
	int checks = 0;
	void Require(bool success, const std::source_location location = std::source_location::current())
	{
		++checks;
		if (!success)
			throw std::runtime_error("Failed at line " + std::to_string(location.line()));
	}
}

int main()
{
	using namespace VRFpsStabilizer;
	try {
		std::string error;
		const std::string original = "\xEF\xBB\xBF[Settings] # heading\r\n EnableLog = 0  # keep\r\nUnknown = raw\r\nWithGrassWorlds=  # empty\r\n[Level0]\r\niMaxDesired:Particles = 1500\r\n[Conditional]\r\nExterior,Raining|CS>SSGI = 0";
		IniDocument document{ original, original };
		Require(document.Get("settings", "enablelog") == "0");
		Require(document.Get("Settings", "WithGrassWorlds") == "");
		Require(document.Set("Settings", "EnableLog", "1", error));
		auto expected = original;
		expected.replace(expected.find("= 0"), 3, "= 1");
		Require(document.text == expected);
		Require(document.original == original);
		Require(document.Set("Settings", "WithGrassWorlds", "Example.esp|123456", error));
		Require(document.Get("Settings", "WithGrassWorlds") == "Example.esp|123456");
		Require(document.text.find("# empty") != std::string::npos);
		Require(document.Set("Settings", "AutoConfigEnabled", "0", error));
		Require(document.Get("Settings", "AutoConfigEnabled") == "0");
		Require(document.text.ends_with("Exterior,Raining|CS>SSGI = 0"));
		Require(document.text.starts_with("\xEF\xBB\xBF"));
		Require(document.SetBody("Level0", "iMaxDesired:Particles = 1200\r\n", error));
		Require(document.Body("Level0") == "iMaxDesired:Particles = 1200\r\n");
		Require(document.Entries("Level0").size() == 1);
		const auto before = document.text;
		Require(!document.SetBody("Level0", "[Settings]\nEnableLog=1", error));
		Require(!document.Set("Settings", "EnableLog", "1\n[Other]", error));
		Require(document.text == before);

		IniDocument duplicate{ "", "[Settings]\nx=1 # one\nx = 2 ; two\n" };
		Require(duplicate.Get("Settings", "x") == "2");
		Require(duplicate.Set("Settings", "x", "3", error));
		Require(duplicate.text == "[Settings]\nx=3 # one\nx = 3 ; two\n");
		duplicate.text = "[Level0]\na=1\n[Level0]\na=2\n";
		Require(!duplicate.SetBody("Level0", "a=3", error));
		Require(!document.SetBody("bad]\n[Settings", "x=1", error));
		IniDocument headerAtEnd{ "[Level0]", "[Level0]" };
		Require(headerAtEnd.SetBody("Level0", "iMaxDesired:Particles=1000", error));
		Require(headerAtEnd.Get("Level0", "iMaxDesired:Particles") == "1000");
		IniDocument commentedHeader{ "", "[Settings]\r\nx=1\r\n[Level0] # keep" };
		Require(commentedHeader.SetBody("Level0", "iMaxDesired:Particles=900", error));
		Require(commentedHeader.text == "[Settings]\r\nx=1\r\n[Level0] # keep\r\niMaxDesired:Particles=900\r\n");
		IniDocument repeatedRows{ "", "[Level0]\nAlpha=1\nbeta=2\nALPHA=3\n" };
		const auto rows = repeatedRows.Entries("Level0");
		Require(rows.size() == 2 && rows[0].first == "Alpha" && rows[0].second == "3" && rows[1].second == "2");
		error = "previous edit failed";
		Require(repeatedRows.Set("Level0", "beta", "4", error) && error.empty());
		Require(!IniDocument::ValidateText(std::string("a\0b", 3), error));
		Require(!IniDocument::ValidateText(std::string(kMaxIniBytes + 1, 'a'), error));
		Require(!IniDocument::ValidateText("\xFF\xFE", error));

		IniDocument limits{ "[Settings]\nMinTargetFrameTime=8.5\nMaxTargetFrameTime=9.5\nCPULateStartCheckFrameCount=30\nCPULateStartMinFrameCountToReduce=4\n", "" };
		limits.text = limits.original;
		Require(ValidateSettings(limits, error));
		Require(limits.Set("Settings", "MinTargetFrameTime", "10", error));
		Require(!ValidateSettings(limits, error));
		Require(limits.Set("Settings", "MaxTargetFrameTime", "11", error));
		Require(ValidateSettings(limits, error));
		Require(limits.Set("Settings", "CPULateStartCheckFrameCount", "101", error));
		Require(!ValidateSettings(limits, error));
		Require(limits.Set("Settings", "CPULateStartCheckFrameCount", "3", error));
		Require(!ValidateSettings(limits, error));
		Require(limits.Set("Settings", "CPULateStartCheckFrameCount", "30", error));
		Require(ValidateSettings(limits, error));
		Require(limits.Set("Settings", "MinTargetFrameTime", "nan", error));
		Require(!ValidateSettings(limits, error));
		Require(!ValidateSetting(*FindSetting("CheckSleepDuration"), "11", error));
		Require(!ValidateSetting(*FindSetting("AutoConfigEnabled"), "0.5", error));
		Require(!ValidateSetting(*FindSetting("CheckSleepDuration"), "1e2", error));
		Require(!ValidateSetting(*FindSetting("CheckSleepDuration"), "12.0", error));
		Require(!ValidateSetting(*FindSetting("CheckSleepDuration"), "", error));
		Require(!ValidateSetting(*FindSetting("MinTargetFrameTime"), " ", error));
		Require(!ValidateSetting(*FindSetting("MinDynamicResolutionRatio"), "1.1", error));
		Require(ValidateSetting(*FindSetting("WithGrassWorlds"), "Test.esp|123ABC, Example.esm|000001", error));
		Require(ValidateSetting(*FindSetting("WithGrassWorlds"), "", error));
		Require(ValidateSetting(*FindSetting("WithGrassWorlds"), "Test.ESP|000012", error));
		Require(!ValidateSetting(*FindSetting("WithGrassWorlds"), "Test.esp|xyz", error));
		Require(!ValidateSetting(*FindSetting("WithGrassWorlds"), "Test.esp|123,", error));
		Require(!ValidateSetting(*FindSetting("WithGrassWorlds"), "../Test.esp|123", error));
		Require(!ValidateSetting(*FindSetting("WithGrassWorlds"), "Test.esp|", error));
		IniDocument invalidPartner{ "[Settings]\nCPULateStartCheckFrameCount=101\nCPULateStartMinFrameCountToReduce=4\n", "" };
		invalidPartner.text = invalidPartner.original;
		Require(invalidPartner.Set("Settings", "CPULateStartMinFrameCountToReduce", "5", error));
		Require(!ValidateSettings(invalidPartner, error));
		for (const auto& setting : kSettings)
			Require(ValidateSetting(setting, setting.suggested, error));

		static_assert(!std::has_virtual_destructor_v<VRFpsStabilizerPluginApi::IVRFpsStabilizerInterface001>);
		static_assert(sizeof(VRFpsStabilizerPluginApi::VRFpsStabilizerMessage) == sizeof(void*));
		static_assert(VRFpsStabilizerPluginApi::VRFpsStabilizerMessage::kMessage_GetInterface == 0xF43A9D7C);
		std::cout << checks << " Stabilizer configuration checks passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
