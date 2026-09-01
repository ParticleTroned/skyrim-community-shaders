#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <ClibUtil/detail/SimpleIni.h>

using namespace std::literals;

#undef CP_UTF8
namespace REX::W32
{
	inline constexpr unsigned CP_UTF8 = 65001;
	using ::MultiByteToWideChar;
	using ::WideCharToMultiByte;
}

namespace RE
{
	struct Setting
	{
		enum class Type
		{
			kBool,
			kInteger,
			kUnsignedInteger,
			kFloat,
			kString
		};
		union Data
		{
			bool b = false;
			std::int32_t i;
			std::uint32_t u;
			float f;
		} data;
		Type type = Type::kBool;
		Type GetType() const { return type; }
	};
	struct INISettingCollection
	{
		std::map<std::string, Setting*> settings;
		Setting* GetSetting(const std::string& a_name)
		{
			auto it = settings.find(a_name);
			return it == settings.end() ? nullptr : it->second;
		}
	};
	struct INIPrefSettingCollection : INISettingCollection
	{};
}

namespace globals::game
{
	inline RE::INISettingCollection* iniSettingCollection = nullptr;
	inline RE::INIPrefSettingCollection* iniPrefSettingCollection = nullptr;
	inline RE::INISettingCollection* gameSettingCollection = nullptr;
}

namespace logger
{
	inline unsigned warnings = 0;
	template <class... Args>
	void warn(const char*, Args&&...)
	{
		++warnings;
	}
	template <class... Args>
	void info(const char*, Args&&...)
	{}
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
}

#include "game_setting_persistence_under_test.h"

namespace REL
{
	struct Module
	{
		static inline bool vr = true;
		static bool IsVR() { return vr; }
	};
}

namespace Util
{
	void EnableBooleanSettings(const std::map<std::string, GameSetting>& a_settings, const std::string&)
	{
		for (const auto& [name, metadata] : a_settings) {
			VisitGameSettingValue(name, [](auto& value) { value = true; });
		}
	}
}

struct MenuOpenCloseEventHandler
{
	static void Register() {}
};

struct json
{
	unsigned CubemapResolution = 128;
};

struct DynamicCubemaps
{
	using Settings = json;
	Settings settings;
	static constexpr unsigned kPerformanceCubemapResolution = 128;
	static constexpr unsigned kQualityCubemapResolution = 256;
	bool gameSettingsInitialized = false;
	bool recompileFlag = false;
	std::map<std::string, Util::GameSetting> iniVRCubeMapSettings;
	std::map<std::string, Util::GameSetting> hiddenVRCubeMapSettings;
	std::string GetName() { return "Dynamic Cubemaps"; }
	void RefreshActiveCubemapResolution() {}
	void LoadSettings(json&);
	void DataLoaded();
	void OnSettingsSaved();
};

#include "cubemap_game_settings_under_test.h"

namespace
{
	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	template <class F>
	void ExpectFailure(F&& a_action)
	{
		try {
			a_action();
		} catch (const std::runtime_error&) {
			return;
		}
		throw std::runtime_error("A persistence failure must propagate to the settings-save caller");
	}

	const std::filesystem::path iniPath{ Util::CS_SETTINGS_PATH };
	constexpr auto silhouetteName = "bAutoWaterSilhouetteReflections:Water";
	constexpr auto detailName = "bForceHighDetailReflections:Water";

	void WriteIni(std::string_view a_text)
	{
		std::filesystem::create_directories(iniPath.parent_path());
		std::ofstream output(iniPath, std::ios::binary | std::ios::trunc);
		output << a_text;
		output.close();
		Check(static_cast<bool>(output), "Could not create the INI fixture");
	}

	std::string ReadIni()
	{
		std::ifstream input(iniPath, std::ios::binary);
		return { std::istreambuf_iterator<char>(input), {} };
	}

	void RunTests()
	{
		RE::Setting silhouette;
		RE::Setting detail;
		RE::INISettingCollection ini;
		RE::INIPrefSettingCollection prefs;
		globals::game::iniSettingCollection = &ini;
		globals::game::iniPrefSettingCollection = &prefs;
		ini.settings[silhouetteName] = &silhouette;
		prefs.settings[detailName] = &detail;
		DynamicCubemaps cubemaps;
		for (const auto* name : { silhouetteName, detailName })
			cubemaps.iniVRCubeMapSettings[name] = { "", "", 0, true, false, true };
		const auto& settings = cubemaps.iniVRCubeMapSettings;

		WriteIni("[Water]\nbAutoWaterSilhouetteReflections=\"0\"\nbAutoWaterSilhouetteReflections=1\nbForceHighDetailReflections='1'\n[Other]\nKeep=first\nKeep=second\nQuoted=\"unchanged\"\n");
		const auto absoluteIniPath = std::filesystem::absolute(iniPath);
		Check(GetPrivateProfileIntW(L"Water", L"bAutoWaterSilhouetteReflections", 9, absoluteIniPath.c_str()) == 0,
			"The native INI reference must read the first quoted duplicate");
		silhouette.data.b = true;
		Util::LoadGameSettings(settings);
		Check(!silhouette.data.b && detail.data.b, "Quoted values and first duplicate selection must match native INI reads");
		Util::SaveGameSettings(settings);
		Check(ReadIni().find("first") != std::string::npos && ReadIni().find("second") != std::string::npos,
			"Saving managed settings must preserve unrelated duplicate entries");
		Check(ReadIni().find("\"unchanged\"") != std::string::npos, "Unrelated quoted values must retain their quotes");

		const std::wstring wideIni = L"[Water]\nbAutoWaterSilhouetteReflections=0\nbForceHighDetailReflections=1\n[Other]\nKeep=\u03A9\n";
		WriteIni(std::string("\xff\xfe", 2) + std::string(reinterpret_cast<const char*>(wideIni.data()), wideIni.size() * sizeof(wchar_t)));
		silhouette.data.b = true;
		Util::LoadGameSettings(settings);
		Check(!silhouette.data.b, "Existing UTF-16 INI files must load without manual conversion");
		Util::SaveGameSettings(settings);
		Check(ReadIni().starts_with("\xff\xfe"), "Saving must preserve UTF-16 encoding");
		wchar_t wideValue[32]{};
		GetPrivateProfileStringW(L"Other", L"Keep", L"", wideValue, 32, absoluteIniPath.c_str());
		Check(std::wstring_view(wideValue) == L"\u03A9", "Unrelated Unicode values must survive the save");
		WriteIni("\xff\xfe\x41"sv);
		ExpectFailure([&] { Util::SaveGameSettings(settings); });
		Check(ReadIni() == "\xff\xfe\x41"sv, "Incomplete UTF-16 data must not be overwritten");
		WriteIni("\xff\xfe\x00\xd8"sv);
		ExpectFailure([&] { Util::SaveGameSettings(settings); });
		Check(ReadIni() == "\xff\xfe\x00\xd8"sv, "Unpaired UTF-16 surrogates must not be replaced silently");
		std::filesystem::remove(iniPath);
		silhouette.data.b = false;
		detail.data.b = false;
		logger::warnings = 0;

		Util::LoadGameSettings(settings);
		Check(logger::warnings == 0 && !silhouette.data.b && !detail.data.b,
			"Missing optional INI must retain current values without warnings");
		cubemaps.DataLoaded();
		Check(silhouette.data.b && detail.data.b, "First startup must enable reflection defaults");
		silhouette.data.b = false;
		cubemaps.OnSettingsSaved();
		Check(std::filesystem::is_regular_file(iniPath), "Save must create the missing directory and INI");
		silhouette.data.b = true;
		detail.data.b = false;
		Util::LoadGameSettings(settings);
		Check(!silhouette.data.b && detail.data.b, "Both collections must round-trip their boolean values");

		json config;
		cubemaps.gameSettingsInitialized = false;
		silhouette.data.b = true;
		cubemaps.LoadSettings(config);
		Check(silhouette.data.b, "INI load must wait until startup defaults have been installed");
		cubemaps.DataLoaded();
		Check(!silhouette.data.b && detail.data.b, "Saved false must survive startup defaults");
		silhouette.data.b = true;
		cubemaps.LoadSettings(config);
		Check(!silhouette.data.b, "Explicit settings reload must apply saved INI values");

		WriteIni("; user comment\n[Other]\nKeep=untouched\n[Water]\nOtherWater=7\nbAutoWaterSilhouetteReflections=0\n");
		detail.data.b = false;
		Util::LoadGameSettings(settings);
		Check(!detail.data.b && logger::warnings == 0, "Missing keys must retain current values quietly");
		cubemaps.OnSettingsSaved();
		const auto preserved = ReadIni();
		Check(preserved.find("user comment") != std::string::npos && preserved.find("untouched") != std::string::npos &&
				  preserved.find("OtherWater") != std::string::npos,
			"Save must preserve unrelated entries and comments");

		WriteIni("[Water]\nbAutoWaterSilhouetteReflections=garbage\nbForceHighDetailReflections=TrUe\n");
		Util::LoadGameSettings(settings);
		Check(!silhouette.data.b && detail.data.b && logger::warnings == 1,
			"Invalid values must warn and retain the old value without blocking a valid sibling");

		const auto beforeFailure = ReadIni();
		Check(SetFileAttributesW(iniPath.c_str(), FILE_ATTRIBUTE_READONLY), "Could not mark INI read-only");
		ExpectFailure([&] { cubemaps.OnSettingsSaved(); });
		Check(SetFileAttributesW(iniPath.c_str(), FILE_ATTRIBUTE_NORMAL), "Could not restore INI attributes");
		Check(ReadIni() == beforeFailure, "Failed writes must preserve the existing file");

		HANDLE lock = CreateFileW(iniPath.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		Check(lock != INVALID_HANDLE_VALUE, "Could not lock INI fixture");
		const auto previousWarnings = logger::warnings;
		Util::LoadGameSettings(settings);
		ExpectFailure([&] { cubemaps.OnSettingsSaved(); });
		CloseHandle(lock);
		Check(logger::warnings > previousWarnings && ReadIni() == beforeFailure,
			"Unreadable INI must warn on load and prevent save from discarding existing entries");

		ini.settings.erase(silhouetteName);
		ExpectFailure([&] { cubemaps.OnSettingsSaved(); });
		Check(ReadIni() == beforeFailure, "Missing engine settings must fail before touching the INI");
		ini.settings[silhouetteName] = &silhouette;

		auto scalarSettings = settings;
		RE::Setting signedValue;
		signedValue.type = RE::Setting::Type::kInteger;
		signedValue.data.i = INT32_MIN;
		RE::Setting unsignedValue;
		unsignedValue.type = RE::Setting::Type::kUnsignedInteger;
		unsignedValue.data.u = UINT32_MAX;
		RE::Setting floatValue;
		floatValue.type = RE::Setting::Type::kFloat;
		floatValue.data.f = 0.123456789f;
		const auto expectedFloat = floatValue.data.f;
		ini.settings["iValue:Test"] = &signedValue;
		ini.settings["uValue:Test"] = &unsignedValue;
		ini.settings["fValue:Test"] = &floatValue;
		for (const auto* name : { "iValue:Test", "uValue:Test", "fValue:Test" })
			scalarSettings[name] = {};
		scalarSettings["bOffset:Test"] = { "", "", 123, true, false, true };
		Util::SaveGameSettings(scalarSettings);
		signedValue.data.i = 0;
		unsignedValue.data.u = 0;
		floatValue.data.f = 0;
		Util::LoadGameSettings(scalarSettings);
		Check(signedValue.data.i == INT32_MIN && unsignedValue.data.u == UINT32_MAX && floatValue.data.f == expectedFloat,
			"Scalar settings must round-trip without losing range or float precision");
		Check(ReadIni().find("bOffset") == std::string::npos, "Offset-only settings must not be persisted");

		WriteIni("[Test]\niValue=-2147483649\nuValue=-1\nfValue=nan\n");
		Util::LoadGameSettings(scalarSettings);
		Check(signedValue.data.i == INT32_MIN && unsignedValue.data.u == UINT32_MAX && floatValue.data.f == expectedFloat,
			"Out-of-range and non-finite values must not modify engine state");
		floatValue.data.f = std::numeric_limits<float>::infinity();
		ExpectFailure([&] { Util::SaveGameSettings(scalarSettings); });
		floatValue.data.f = expectedFloat;

		WriteIni("[Water]\0bAutoWaterSilhouetteReflections=0\n"sv);
		const auto binaryContents = ReadIni();
		ExpectFailure([&] { Util::SaveGameSettings(settings); });
		Check(ReadIni() == binaryContents, "Unsupported text encodings must not be silently overwritten");
		WriteIni("[Water]\nbAutoWaterSilhouetteReflections=0\n");

		const auto strictOriginal = ReadIni();
		const auto replaceLock = CreateFileW(iniPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Check(replaceLock != INVALID_HANDLE_VALUE, "Strict replacement test must lock the destination against deletion");
		std::string strictError;
		const bool strictSaved = Util::FileHelpers::WriteTextFileAtomic(iniPath, "replacement", strictError, false);
		CloseHandle(replaceLock);
		Check(!strictSaved && !strictError.empty() && ReadIni() == strictOriginal,
			"Strict atomic writes must preserve the original when replacement fails");

		globals::game::iniSettingCollection = nullptr;
		globals::game::iniPrefSettingCollection = nullptr;
		ExpectFailure([&] { Util::SaveGameSettings(settings); });
		REL::Module::vr = false;
		cubemaps.OnSettingsSaved();
		cubemaps.LoadSettings(config);
		cubemaps.DataLoaded();
	}
}

int main()
{
	const auto originalDirectory = std::filesystem::current_path();
	const auto testDirectory = originalDirectory / std::format("game-setting-persistence-{}", GetCurrentProcessId());
	try {
		Check(std::filesystem::create_directory(testDirectory), "Test directory must be new");
		std::filesystem::current_path(testDirectory);
		RunTests();
		std::filesystem::current_path(originalDirectory);
		std::filesystem::remove_all(testDirectory);
		std::cout << "Game setting persistence tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::filesystem::current_path(originalDirectory);
		std::cerr << error.what() << "\nEvidence retained at " << testDirectory << '\n';
		return 1;
	}
}
