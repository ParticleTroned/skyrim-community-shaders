#define NOMINMAX
#include "Utils/StringUtils.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <imgui.h>
#include <iostream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <windows.h>

using json = nlohmann::json;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ImVec4, x, y, z, w)
static void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
namespace logger
{
	template <class... T>
	void warn(const char*, T&&...)
	{}
	template <class... T>
	void info(const char*, T&&...)
	{}
}
namespace Util
{
	bool IEquals(const std::string& left, const std::string& right) { return _stricmp(left.c_str(), right.c_str()) == 0; }
}
namespace Util::FileHelpers
{
	int directWriteAttempts = 0;
	bool WriteTextFileDirect(const std::filesystem::path&, std::string_view, std::string&)
	{
		++directWriteAttempts;
		throw std::runtime_error("Theme writes must never truncate the destination as a fallback");
	}
	bool WriteTextFileAtomic(const std::filesystem::path&, std::string_view, std::string&, bool = true, bool = true);
#include "ui_atomic_write.h"
#include "ui_filename.h"
}
struct ThemeManager
{
	std::filesystem::path directory;
	int refreshes = 0;
	bool failRefresh = false;
	bool IsPresetTheme(const std::string& name) const { return _stricmp(name.c_str(), "Default") == 0; }
	std::filesystem::path GetThemesDirectory() const { return directory; }
	void RefreshThemes()
	{
		if (failRefresh)
			throw std::runtime_error("Discovery failed");
		++refreshes;
	}
	bool SaveTheme(const std::string&, const json&, const std::string&, const std::string&, std::string&, bool = true);
};
#include "ui_theme_save.h"
struct EditorWindow
{
	static constexpr std::size_t maxRecordMarkers = 10;
#include "ui_editor_settings.h"
	Settings settings;
	json j;
	std::unordered_map<std::string, std::string> markerLabelBuffers;
	int saves = 0;
	void Save() { ++saves; }
	void SaveSettings();
	void LoadSettings();
	void SanitizeSettings();
	bool AddRecordMarker();
	std::string RenameRecordMarker(const std::string&, const std::string&);
	void DeleteRecordMarker(const std::string&);
};
#include "ui_editor_serialization.h"
static json GetWidgetTypeSizesJson() { return json::object(); }
static void SetWidgetTypeSizesFromJson(const json&) {}
#include "ui_editor_persistence.h"
#include "ui_markers.h"
static std::string Bytes(const std::filesystem::path& file)
{
	std::ifstream input(file, std::ios::binary);
	return { std::istreambuf_iterator<char>(input), {} };
}
static void CheckThemes()
{
	const auto directory = std::filesystem::temp_directory_path() / std::format("csx-theme-test-{}-{}", GetCurrentProcessId(), GetTickCount64());
	ThemeManager manager{ directory };
	const auto file = directory / "Custom.json";
	std::string error;
	Require(manager.SaveTheme("Custom", { { "scale", 1 } }, "Custom", "Description", error), "initial theme save succeeds");
	Require(error.empty() && manager.refreshes == 1, "successful save refreshes discovery");
	const auto original = Bytes(file);
	Require(json::parse(original)["Theme"]["scale"] == 1, "saved theme is complete JSON");
	Require(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY), "mark destination read-only");
	const bool saved = manager.SaveTheme("Custom", { { "scale", 2 } }, "Custom", "Changed", error);
	const auto retained = Bytes(file);
	SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
	Require(!saved && !error.empty(), "replacement failure is reported");
	Require(retained == original && manager.refreshes == 1, "failed save retains exact original and discovery");
	Require(Util::FileHelpers::directWriteAttempts == 0, "theme saves explicitly disable destructive write fallback");
	Require(manager.SaveTheme("Custom", { { "scale", 3 } }, "Custom", "Updated", error), "subsequent save recovers");
	Require(json::parse(Bytes(file))["Theme"]["scale"] == 3, "successful replacement contains new values");
	Require(!manager.SaveTheme("default", {}, "", "", error), "built-in names are protected");
	Require(!manager.SaveTheme("", {}, "", "", error), "empty names are rejected");
	Require(!manager.SaveTheme(" default. ", {}, "", "", error), "sanitized built-in names are protected");
	const auto savedCustom = Bytes(file);
	Require(!manager.SaveTheme("custom", {}, "", "", error, false), "creation never overwrites an existing case-insensitive name");
	Require(Bytes(file) == savedCustom, "failed creation retains the prior theme bytes");
	Require(manager.SaveTheme("Fresh", {}, "", "", error, false), "creation succeeds for a new file");
	Require(!Util::FileHelpers::WriteTextFileAtomic(file, "truncated", error, true, false) && Bytes(file) == savedCustom, "create-only policy also prevents destructive fallback");
	Require(Util::FileHelpers::directWriteAttempts == 0, "create-only writes never enter fallback");
	manager.failRefresh = true;
	Require(!manager.SaveTheme("Other", {}, "", "", error) && !error.empty(), "discovery exceptions become feedback");
	for (const auto& entry : std::filesystem::directory_iterator(directory))
		Require(entry.path().extension() == ".json", "no staging files remain");
	std::filesystem::remove_all(directory);
}
static void CheckAtomicJsonWrites()
{
	const auto root = std::filesystem::temp_directory_path() / std::format("csx-json-write-test-{}-{}", GetCurrentProcessId(), GetTickCount64());
	const auto directory = root / std::filesystem::path(u8"Shared_\u00e9_\u6f22");
	Require(std::filesystem::create_directories(directory), "JSON writes own a fresh Unicode test directory");
	const auto file = directory / "Settings.json";
	auto temporary = file;
	temporary += ".tmp";
	auto backup = file;
	backup += ".bak";
	std::ofstream(temporary) << "user temporary bytes";
	std::ofstream(backup) << "user backup bytes";
	std::string error;
	Require(Util::FileHelpers::WriteTextFileAtomic(file, json{ { "revision", 1 } }.dump(), error, false), "atomic JSON writes support Unicode destinations");
	Require(json::parse(Bytes(file))["revision"] == 1 && error.empty(), "atomic writes produce complete JSON");
	Require(Util::FileHelpers::WriteTextFileAtomic(file, json{ { "revision", 2 } }.dump(), error, false), "atomic JSON writes replace existing settings");
	const auto original = Bytes(file);
	Require(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY), "mark saved JSON read-only");
	const bool saved = Util::FileHelpers::WriteTextFileAtomic(file, json{ { "revision", 3 } }.dump(), error, false);
	const auto retained = Bytes(file);
	SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
	Require(!saved && !error.empty() && retained == original, "a failed write preserves existing JSON and reports the error");
	Require(Bytes(temporary) == "user temporary bytes" && Bytes(backup) == "user backup bytes", "atomic writes never touch unrelated .tmp or .bak files");
	Require(Util::FileHelpers::directWriteAttempts == 0, "disabling direct fallback prevents destructive writes");
	for (const auto& entry : std::filesystem::directory_iterator(directory))
		Require(entry.path() == file || entry.path() == temporary || entry.path() == backup, "atomic writes leave no staging files after success or failure");
	std::filesystem::remove_all(root);
}

static void CheckMarkers()
{
	EditorWindow editor;
	auto& markers = editor.settings.recordMarkers;
	markers.clear();
	Require(editor.AddRecordMarker() && editor.AddRecordMarker() && editor.AddRecordMarker(), "create markers");
	markers["New marker 2"] = { 1, 0, 0, 1 };
	editor.DeleteRecordMarker("New marker 1");
	Require(editor.AddRecordMarker() && markers.size() == 3, "add after deletion fills unused name");
	Require(markers.at("New marker 2").x == 1, "existing marker is not overwritten");
	editor.settings.markedRecords["record"] = "New marker 0";
	editor.markerLabelBuffers["New marker 0"] = "stale old";
	editor.markerLabelBuffers["Renamed"] = "stale destination";
	Require(editor.RenameRecordMarker("New marker 0", "Renamed").empty(), "rename succeeds");
	Require(editor.settings.markedRecords.at("record") == "Renamed", "rename updates record references");
	Require(!editor.markerLabelBuffers.contains("Renamed") && !editor.markerLabelBuffers.contains("New marker 0"), "rename invalidates both edit buffers");
	Require(!editor.RenameRecordMarker("Renamed", "New marker 2").empty(), "duplicate rename rejected");
	Require(!editor.RenameRecordMarker("Renamed", "").empty(), "empty rename rejected");
	Require(editor.RenameRecordMarker("Renamed", "New marker 0").empty(), "rename back succeeds without stale draft");
	editor.DeleteRecordMarker("New marker 0");
	Require(editor.settings.markedRecords.empty(), "deletion removes dangling references");
	while (markers.size() < EditorWindow::maxRecordMarkers) Require(editor.AddRecordMarker(), "fill marker capacity");
	Require(!editor.AddRecordMarker(), "marker capacity enforced");
}
static void CheckEditorScale()
{
	for (const float scale : { 0.5f, 0.75f, 1.0f, 1.35f, 2.0f }) {
		EditorWindow editor;
		editor.settings.editorUIScale = scale;
		editor.SaveSettings();
		EditorWindow reopened;
		reopened.j = json::parse(editor.j.dump());
		reopened.LoadSettings();
		Require(reopened.settings.editorUIScale == scale, "editor scale survives serialized settings reload");
	}
	EditorWindow editor;
	editor.j = { { "autoApplyChanges", false } };
	editor.LoadSettings();
	Require(editor.settings.editorUIScale == 1.0f, "legacy editor settings default to 100 percent");
	for (const auto [saved, expected] : { std::pair{ -1.0f, 0.5f }, std::pair{ 9.0f, 2.0f } }) {
		editor.j = { { "editorUIScale", saved } };
		editor.LoadSettings();
		Require(editor.settings.editorUIScale == expected, "editor scale is clamped on load");
	}
	editor.settings.editorUIScale = std::numeric_limits<float>::quiet_NaN();
	editor.SanitizeSettings();
	Require(editor.settings.editorUIScale == 1.0f, "non-finite editor scale falls back to 100 percent");
}
int main()
{
	try {
		CheckThemes();
		CheckAtomicJsonWrites();
		CheckMarkers();
		CheckEditorScale();
		std::cout << "UI persistence regressions passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
