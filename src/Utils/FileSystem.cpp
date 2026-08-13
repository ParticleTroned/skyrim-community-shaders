#include "FileSystem.h"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <psapi.h>
#include <system_error>
#include <utility>

namespace
{
	std::atomic_uint64_t atomicWriteSequence = 0;

	std::string GetWindowsErrorMessage(DWORD error)
	{
		return std::system_category().message(static_cast<int>(error));
	}

	void CloseTemporaryFileAfterFailure(
		HANDLE file,
		std::string& errorMessage)
	{
		if (!CloseHandle(file)) {
			errorMessage += std::format(
				" Closing the temporary file also failed: {}.",
				GetWindowsErrorMessage(GetLastError()));
		}
	}

	void RemoveTemporaryFile(
		const std::filesystem::path& path,
		std::string& errorMessage)
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
		if (ec) {
			errorMessage += std::format(
				" Temporary file cleanup failed for '{}': {}.",
				path.string(),
				ec.message());
		}
	}
}

namespace Util
{
	// Path helper utilities implementation
	namespace PathHelpers
	{
		std::filesystem::path GetDataPath()
		{
			try {
				// Get the current process (game) executable path
				wchar_t buffer[MAX_PATH];
				DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
				if (length == 0 || length == MAX_PATH) {
					throw std::runtime_error("Failed to get module filename");
				}

				auto executablePath = std::filesystem::path(buffer);

				auto gamePath = executablePath.parent_path();
				auto dataPath = gamePath / "Data";
				return dataPath;
			} catch (const std::exception& e) {
				// Fallback to current_path if Windows API method fails
				logger::warn("Failed to get game path via Windows API, falling back to current_path: {}", e.what());
				return std::filesystem::current_path() / "Data";
			}
		}

		std::filesystem::path GetCommunityShaderPath()
		{
			return GetDataPath() / "SKSE" / "Plugins" / "CommunityShaders";
		}

		std::filesystem::path GetImGuiIniPath()
		{
			return GetDataPath() / "SKSE" / "Plugins" / "CommunityShaders_ImGui.ini";
		}

		std::filesystem::path GetInterfacePath()
		{
			return GetDataPath() / "Interface" / "CommunityShaders";
		}

		std::filesystem::path GetFontsPath()
		{
			return GetInterfacePath() / "Fonts";
		}

		std::filesystem::path GetIconsPath()
		{
			return GetInterfacePath() / "Icons";
		}

		std::filesystem::path GetCursorsPath()
		{
			return GetInterfacePath() / "Cursors";
		}

		std::filesystem::path GetSettingsUserPath()
		{
			return GetCommunityShaderPath() / "SettingsUser.json";
		}

		std::filesystem::path GetSettingsTestPath()
		{
			return GetCommunityShaderPath() / "SettingsTest.json";
		}

		std::filesystem::path GetSettingsDefaultPath()
		{
			return GetCommunityShaderPath() / "SettingsDefault.json";
		}

		std::filesystem::path GetSettingsThemePath()
		{
			return GetCommunityShaderPath() / "SettingsTheme.json";
		}

		std::filesystem::path GetThemesPath()
		{
			return GetCommunityShaderPath() / "Themes";
		}

		std::filesystem::path GetTranslationsPath()
		{
			return GetCommunityShaderPath() / "Translations";
		}

		std::filesystem::path GetOverridesPath()
		{
			return GetCommunityShaderPath() / "Overrides";
		}

		std::filesystem::path GetUserOverridesPath()
		{
			return GetOverridesPath() / "User";
		}

		std::filesystem::path GetAppliedOverridesPath()
		{
			return GetCommunityShaderPath() / "AppliedOverrides.json";
		}

		std::filesystem::path GetSceneSettingsPath()
		{
			return GetCommunityShaderPath() / "SceneSettings";
		}

		std::filesystem::path GetUnifiedWaterCachePath()
		{
			return GetCommunityShaderPath() / "UnifiedWaterCache";
		}

		std::filesystem::path GetShadersPath()
		{
			return GetDataPath() / "Shaders";
		}

		std::filesystem::path GetFeaturesPath()
		{
			return GetShadersPath() / "Features";
		}

		std::filesystem::path GetCurrentModuleRealPath()
		{
			try {
				HMODULE selfModule = nullptr;
				if (!GetModuleHandleExW(
						GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						reinterpret_cast<LPCWSTR>(&GetCurrentModuleRealPath),
						&selfModule)) {
					return {};
				}
				wchar_t buffer[MAX_PATH]{};
				DWORD size = GetModuleFileNameExW(GetCurrentProcess(), selfModule, buffer, MAX_PATH);
				if (size == 0 || size == MAX_PATH) {
					throw std::runtime_error("Failed to get module filename");
				}
				return std::filesystem::path(buffer);
			} catch (const std::exception& e) {
				logger::error("GetCurrentModuleRealPath: Exception caught: {}", e.what());
				return {};
			}
		}

		std::filesystem::path GetRootRealPath()
		{
			static std::filesystem::path cachedPath = []() {
				std::filesystem::path dllPath = GetCurrentModuleRealPath();
				if (dllPath.empty())
					return std::filesystem::path{};
				return dllPath.parent_path().parent_path().parent_path();
			}();
			return cachedPath;
		}

		std::filesystem::path GetShadersRealPath()
		{
			return GetRootRealPath() / "Shaders";
		}

		std::filesystem::path GetThemesRealPath()
		{
			return GetRootRealPath() / "SKSE" / "Plugins" / "CommunityShaders" / "Themes";
		}

		std::filesystem::path GetFeaturesRealPath()
		{
			return GetShadersRealPath() / "Features";
		}

		std::filesystem::path GetRealPathFromDataRelative(const std::filesystem::path& dataRelativePath)
		{
			std::filesystem::path root = GetRootRealPath();
			if (root.empty())
				return {};
			auto it = dataRelativePath.begin();
			// Skip leading "Data" — MO2 maps the game's Data directory to the mod's physical root
			if (it != dataRelativePath.end() && _wcsicmp(it->c_str(), L"Data") == 0)
				++it;
			for (; it != dataRelativePath.end(); ++it)
				root /= *it;
			return root;
		}

		std::filesystem::path GetFeatureIniPath(const std::string& featureName)
		{
			return GetFeaturesPath() / (featureName + ".ini");
		}

		std::filesystem::path GetFeatureShaderPath(const std::string& featureName)
		{
			return GetFeaturesPath() / featureName;
		}

		std::filesystem::path GetLogPath()
		{
			auto path = logger::log_directory();
			if (!path) {
				return {};
			}
			*path /= std::format("{}.log", std::string(Plugin::NAME));
			return *path;
		}
	}

	// File system utilities implementation
	namespace FileHelpers
	{
		DeletionResult SafeDelete(const std::string& path, const std::string& description)
		{
			DeletionResult result;
			result.deletedDescription = description + ": " + path;

			if (path.empty() || !std::filesystem::exists(path)) {
				result.success = true;  // Consider non-existent files as successfully "deleted"
				return result;
			}

			try {
				if (std::filesystem::is_directory(path)) {
					std::filesystem::remove_all(path);
				} else {
					std::filesystem::remove(path);
				}
				result.success = true;
				logger::info("Deleted {}: {}", description, path);
			} catch (const std::filesystem::filesystem_error& e) {
				result.success = false;
				result.errorMessage = e.what();
				logger::error("Failed to delete {}: {} - {}", description, path, e.what());
			}

			return result;
		}

		void EnsureDirectoryExists(const std::filesystem::path& path)
		{
			std::error_code ec;
			std::filesystem::create_directories(path, ec);
			if (ec) {
				logger::warn("Failed to create directory '{}': {}", path.string(), ec.message());
			}
		}

		std::string SanitizeFileName(std::string name)
		{
			// Trim
			constexpr std::string_view trimLeadingChars = " \t\r\n\v\f-";
			auto first = name.find_first_not_of(trimLeadingChars);
			if (first == std::string::npos)
				return "";
			constexpr std::string_view trimTrailingChars = " \t\r\n\v\f.";
			auto last = name.find_last_not_of(trimTrailingChars);
			if (last == std::string::npos)
				last = first;
			name = name.substr(first, last - first + 1);

			// Replace invalid characters
			std::replace_if(name.begin(), name.end(), [](char c) {
				auto u = static_cast<unsigned char>(c);
				// Only perform "illegal" checks if it's a standard ASCII character (0-127)
				if (u < 128u) {
					return c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
					       c == '"' || c == '<' || c == '>' || c == '|' ||
					       u < 32u || u == 127u;
				}
				return false; }, '_');

			// Windows reserved device names
			static constexpr const char* reserved[] = {
				"CON", "PRN", "AUX", "NUL",
				"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
				"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
			};

			for (const char* r : reserved) {
				if (Util::IEquals(name, r)) {
					name += '_';
					break;
				}
			}

			// Limit length
			if (name.length() > 255u) {
				name = name.substr(0, 255u);
			}

			return name;
		}

		AtomicWriteResult WriteTextFileAtomic(
			const std::filesystem::path& path,
			std::string_view contents)
		{
			if (path.empty() || path.filename().empty())
				return { false, "Atomic write requires a destination filename." };

			const auto parentPath = path.parent_path();
			if (!parentPath.empty()) {
				std::error_code ec;
				std::filesystem::create_directories(parentPath, ec);
				if (ec) {
					return {
						false,
						std::format(
							"Failed to create destination directory '{}': {}",
							parentPath.string(),
							ec.message())
					};
				}
			}

			std::filesystem::path temporaryPath;
			HANDLE temporaryFile = INVALID_HANDLE_VALUE;
			DWORD createError = ERROR_SUCCESS;
			const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

			for (std::uint32_t attempt = 0; attempt < 32; ++attempt) {
				temporaryPath = path;
				temporaryPath += std::format(
					".tmp.{}.{}.{}",
					GetCurrentProcessId(),
					timestamp,
					atomicWriteSequence.fetch_add(1, std::memory_order_relaxed));

				temporaryFile = CreateFileW(
					temporaryPath.c_str(),
					GENERIC_WRITE,
					0,
					nullptr,
					CREATE_NEW,
					FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
					nullptr);
				if (temporaryFile != INVALID_HANDLE_VALUE)
					break;

				createError = GetLastError();
				if (createError != ERROR_FILE_EXISTS && createError != ERROR_ALREADY_EXISTS)
					break;
			}

			if (temporaryFile == INVALID_HANDLE_VALUE) {
				return {
					false,
					std::format(
						"Failed to create temporary file for '{}': {}",
						path.string(),
						GetWindowsErrorMessage(createError))
				};
			}

			std::size_t offset = 0;
			while (offset < contents.size()) {
				const auto remaining = contents.size() - offset;
				const auto writeSize = static_cast<DWORD>(
					std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
				DWORD bytesWritten = 0;
				const bool writeSucceeded = WriteFile(
												temporaryFile,
												contents.data() + offset,
												writeSize,
												&bytesWritten,
												nullptr) != FALSE;
				if (!writeSucceeded || bytesWritten == 0) {
					const DWORD writeError = writeSucceeded ? ERROR_WRITE_FAULT : GetLastError();
					std::string errorMessage = std::format(
						"Failed to write temporary file for '{}': {}",
						path.string(),
						GetWindowsErrorMessage(writeError));
					CloseTemporaryFileAfterFailure(temporaryFile, errorMessage);
					RemoveTemporaryFile(temporaryPath, errorMessage);
					return { false, std::move(errorMessage) };
				}
				offset += bytesWritten;
			}

			if (!FlushFileBuffers(temporaryFile)) {
				const DWORD flushError = GetLastError();
				std::string errorMessage = std::format(
					"Failed to flush temporary file for '{}': {}",
					path.string(),
					GetWindowsErrorMessage(flushError));
				CloseTemporaryFileAfterFailure(temporaryFile, errorMessage);
				RemoveTemporaryFile(temporaryPath, errorMessage);
				return { false, std::move(errorMessage) };
			}

			if (!CloseHandle(temporaryFile)) {
				const DWORD closeError = GetLastError();
				std::string errorMessage = std::format(
					"Failed to close temporary file for '{}': {}",
					path.string(),
					GetWindowsErrorMessage(closeError));
				RemoveTemporaryFile(temporaryPath, errorMessage);
				return { false, std::move(errorMessage) };
			}

			if (!MoveFileExW(
					temporaryPath.c_str(),
					path.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				const DWORD replaceError = GetLastError();
				std::string errorMessage = std::format(
					"Failed to atomically replace file '{}': {}",
					path.string(),
					GetWindowsErrorMessage(replaceError));
				RemoveTemporaryFile(temporaryPath, errorMessage);
				return { false, std::move(errorMessage) };
			}

			return { true, {} };
		}

		AtomicWriteResult WriteJsonFileAtomic(
			const std::filesystem::path& path,
			const nlohmann::json& json,
			int indent)
		{
			if (path.empty() || path.filename().empty())
				return { false, "Atomic JSON write requires a destination filename." };

			std::string serialized;
			try {
				serialized = json.dump(indent);
			} catch (const std::exception& e) {
				return { false, std::format("Failed to serialize JSON for '{}': {}", path.string(), e.what()) };
			}
			return WriteTextFileAtomic(path, serialized);
		}
	}
}

std::vector<SettingsDiffEntry> Util::FileSystem::DiffJson(const nlohmann::json& userJson, const nlohmann::json& testJson, float epsilon)
{
	std::vector<SettingsDiffEntry> diffEntries;

	try {
		auto diff = nlohmann::json::diff(userJson, testJson);

		for (const auto& change : diff) {
			try {
				std::string op = change.value("op", "");
				std::string path = change.value("path", "");
				std::string aVal, bVal;

				if (op == "replace") {
					auto aJson = userJson.at(nlohmann::json::json_pointer(path));
					auto bJson = testJson.at(nlohmann::json::json_pointer(path));

					// If both values are numbers, check if difference is within epsilon (double precision)
					if (aJson.is_number() && bJson.is_number()) {
						double aDouble = aJson.get<double>();
						double bDouble = bJson.get<double>();
						if (std::abs(aDouble - bDouble) < static_cast<double>(epsilon)) {
							continue;  // Skip insignificant numeric differences
						}
					}

					aVal = aJson.dump();
					bVal = bJson.dump();
				} else if (op == "add") {
					aVal = "(none)";
					bVal = testJson.at(nlohmann::json::json_pointer(path)).dump();
				} else if (op == "remove") {
					aVal = userJson.at(nlohmann::json::json_pointer(path)).dump();
					bVal = "(none)";
				} else {
					logger::warn("Unknown JSON diff operation '{}' at path '{}'", op, path);
					continue;
				}

				diffEntries.push_back({ path, aVal, bVal });
			} catch (const std::exception& e) {
				logger::warn("Failed to process JSON diff change: {}", e.what());
				// Continue processing other changes
			}
		}
	} catch (const std::exception& e) {
		logger::warn("Failed to compute JSON diff: {}", e.what());
	}

	return diffEntries;
}

std::vector<SettingsDiffEntry> Util::FileSystem::LoadJsonDiff(const std::filesystem::path& userPath, const std::filesystem::path& testPath, float epsilon)
{
	std::vector<SettingsDiffEntry> diffEntries;

	try {
		if (!std::filesystem::exists(userPath)) {
			logger::warn("User config file does not exist: {}", userPath.string());
			return diffEntries;
		}

		if (!std::filesystem::exists(testPath)) {
			logger::warn("Test config file does not exist: {}", testPath.string());
			return diffEntries;
		}

		std::ifstream userFile(userPath);
		std::ifstream testFile(testPath);

		if (!userFile.is_open()) {
			logger::warn("Failed to open user config file: {}", userPath.string());
			return diffEntries;
		}

		if (!testFile.is_open()) {
			logger::warn("Failed to open test config file: {}", testPath.string());
			return diffEntries;
		}

		nlohmann::json userJson, testJson;

		try {
			userFile >> userJson;
		} catch (const std::exception& e) {
			logger::warn("Failed to parse user config JSON from '{}': {}", userPath.string(), e.what());
			return diffEntries;
		}

		try {
			testFile >> testJson;
		} catch (const std::exception& e) {
			logger::warn("Failed to parse test config JSON from '{}': {}", testPath.string(), e.what());
			return diffEntries;
		}

		// Use shared diffing logic
		return DiffJson(userJson, testJson, epsilon);
	} catch (const std::exception& e) {
		logger::warn("Failed to load JSON diff from '{}' and '{}': {}", userPath.string(), testPath.string(), e.what());
	}

	return diffEntries;
}
