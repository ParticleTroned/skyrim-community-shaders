#include <Windows.h>

namespace
{
	bool denyInspection{};
	DWORD ProbeAttributes(LPCWSTR path)
	{
		if (!denyInspection)
			return GetFileAttributesW(path);
		SetLastError(ERROR_ACCESS_DENIED);
		return INVALID_FILE_ATTRIBUTES;
	}
}

#define GetFileAttributesW ProbeAttributes
#include "Compatibility.h"
#undef GetFileAttributesW
#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std::literals;
namespace fs = std::filesystem;

namespace
{
	std::list<std::string> errors;
	std::vector<std::string> logged;
	fs::path gameData;

	struct StartupFailure : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

namespace logger
{
	void error(std::string_view, const std::string& message) { logged.push_back(message); }
}
namespace stl
{
	[[noreturn]] void report_and_fail(std::string_view message)
	{
		throw StartupFailure(std::string(message));
	}

	std::optional<std::string> utf16_to_utf8(const wchar_t* name)
	{
		std::string result;
		while (*name)
			result += static_cast<char>(*name++);
		return result;
	}
}
namespace Util::PathHelpers
{
	fs::path GetDataPath() { return gameData; }
}

namespace
{
#include "plugin_compatibility_under_test.h"

	struct Fixture
	{
		fs::path root, oldCurrentPath;
		Fixture()
		{
			wchar_t temporary[MAX_PATH]{};
			const auto length = GetTempPathW(MAX_PATH, temporary);
			Require(length > 0 && length < MAX_PATH, "temporary path unavailable or truncated");
			root = fs::path(temporary) / std::format("csx-plugin-compatibility-{}-{}", GetCurrentProcessId(), GetTickCount64());
			Require(fs::create_directory(root), "test fixture must be newly created");
			gameData = root / L"Game with spaces" / L"Data";
			fs::create_directories(gameData / L"SKSE" / L"Plugins");
			fs::create_directory(root / L"unrelated working directory");
			oldCurrentPath = fs::current_path();
			fs::current_path(root / L"unrelated working directory");
		}
		~Fixture()
		{
			std::error_code error;
			fs::current_path(oldCurrentPath, error);
			fs::remove_all(root, error);
		}
	};

	template <class Action>
	void ExpectStartupFailure(Action action, std::string_view expected)
	{
		bool reported{};
		try {
			action();
		} catch (const StartupFailure& error) {
			const std::string_view text(error.what());
			reported = text.find(expected) != std::string_view::npos &&
			           text.find("CSX cannot start") != std::string_view::npos &&
			           text.find("restart Skyrim") != std::string_view::npos;
		}
		Require(reported, "startup errors must immediately use the native fatal-reporting path");
	}

	void Run()
	{
		Fixture fixture;
		const auto plugins = gameData / L"SKSE" / L"Plugins";
		const auto& shadow = Compatibility::shadowFixes;
		Require(std::ranges::count_if(Compatibility::incompatiblePlugins, [&](const auto& plugin) {
			return std::wstring_view(plugin.dll) == shadow.dll;
		}) == 1,
			"Shadow Fixes must have exactly one blacklist entry");
		auto probe = Compatibility::ProbePlugin(shadow, plugins);
		Require(!probe.present && !probe.error, "absent plugin reported as conflict or probe failure");
		probe = Compatibility::ProbePlugin(shadow, fixture.root / L"absent" / L"plugins");
		Require(!probe.present && !probe.error, "missing parent directory is not a probe failure");
		CheckIncompatiblePlugins();
		Require(errors.empty() && logged.empty(), "clean startup acquired errors");

		const auto installed = plugins / L"sHaDoWfIxEs.DlL";
		fs::create_directory(installed);
		probe = Compatibility::ProbePlugin(shadow, plugins);
		Require(!probe.present && !probe.error, "directory with a DLL name must not be blacklisted");
		fs::remove(installed);
		std::ofstream(installed) << "Not a loadable DLL";
		Require(LoadLibraryW(installed.c_str()) == nullptr, "legacy probe must miss an unloadable DLL");
		ExpectStartupFailure([] { CheckIncompatiblePlugins(); }, shadow.reason);
		Require(errors.size() == 1 && logged.size() == 1, "installed mixed-case DLL was missed from another CWD");
		Require(!Compatibility::IsLoaded(shadow), "detection must never load the conflicting DLL");
		Require(errors.front().find("ShadowFixes.dll") != std::string::npos &&
					errors.front().find("Remove it to use CSX") != std::string::npos,
			"conflict must name the plugin and removal instruction");
		CheckIncompatiblePlugins();
		Require(errors.size() == 1 && logged.size() == 1, "rechecks duplicated the retained conflict");
		fs::remove(installed);
		errors.clear();
		logged.clear();

		const Compatibility::IncompatiblePlugin loaded{ L"KeRnEl32.DlL" };
		const auto legacy = LoadLibraryW((fixture.root / L"missing" / loaded.dll).c_str());
		if (legacy)
			FreeLibrary(legacy);
		Require(!legacy, "legacy path probe should miss an already-loaded module outside that path");
		denyInspection = true;
		probe = Compatibility::ProbePlugin(loaded, fixture.root / L"missing");
		Require(probe.present && !probe.error, "loaded modules must take precedence over file-inspection errors");
		probe = Compatibility::ProbePlugin(shadow, plugins);
		Require(!probe.present && probe.error, "inspection failure must not masquerade as absence");
		ExpectStartupFailure([] { CheckIncompatiblePlugins(); }, "Cannot check incompatible DLL");
		Require(errors.size() == 1 && logged.size() == 1, "inspection failure must be logged before stopping startup");
		denyInspection = false;

		for (const auto& plugin : Compatibility::incompatiblePlugins) {
			errors.clear();
			logged.clear();
			const auto file = plugins / plugin.dll;
			std::ofstream(file) << "unloadable conflict";
			ExpectStartupFailure([] { CheckIncompatiblePlugins(); }, "Incompatible DLL");
			Require(errors.size() == 1 && logged.size() == 1, "each blacklisted DLL must stop startup immediately");
			fs::remove(file);
		}
	}

}

int main()
{
	try {
		Run();
		std::cout << "Plugin compatibility detection and immediate startup reporting tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
