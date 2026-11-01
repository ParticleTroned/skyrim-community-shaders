#pragma once

// Sidecar manifest for Data/ShaderCache. Missing or malformed manifests
// cannot prove source and compile-input validity and require recompilation.

#include "Utils/ShaderContentReuse.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#	include <Windows.h>
#endif

namespace Util::ShaderCacheManifest
{
	class Manifest
	{
	public:
		static constexpr int kSchemaVersion = 2;

		void Load(const std::filesystem::path& a_manifestPath)
		{
			std::lock_guard lock(mutex);
			path = a_manifestPath;
			entries.clear();
			contentKeys.clear();
			dirty = false;

			std::ifstream stream(a_manifestPath, std::ios::binary);
			if (!stream.is_open())
				return;

			nlohmann::json json;
			try {
				stream >> json;
			} catch (...) {
				return;
			}

			if (!json.is_object() ||
				!json.contains("schemaVersion") ||
				!json["schemaVersion"].is_number_integer() ||
				json["schemaVersion"] != kSchemaVersion ||
				!json.contains("entries") ||
				!json["entries"].is_object())
				return;

			for (const auto& [key, value] : json["entries"].items()) {
				if (value.is_string())
					entries[key] = value.get<std::string>();
			}
			if (const auto keys = json.find("contentKeys"); keys != json.end() && keys->is_object()) {
				for (const auto& [key, value] : keys->items()) {
					if (auto record = ShaderContentReuse::Parse(value); record && entries.contains(key))
						contentKeys.emplace(key, std::move(*record));
				}
			}
		}

		std::optional<std::string> Get(const std::string& a_relativePath) const
		{
			std::lock_guard lock(mutex);
			const auto it = entries.find(a_relativePath);
			return it == entries.end() ? std::nullopt : std::optional{ it->second };
		}

		/** Returns the content identity bound to this entry's persisted bytecode. */
		std::optional<ShaderContentReuse::Record> GetContentKey(const std::string& a_relativePath) const
		{
			std::lock_guard lock(mutex);
			const auto it = contentKeys.find(a_relativePath);
			return it == contentKeys.end() ? std::nullopt : std::optional{ it->second };
		}

		/** Updates digest and optional bytecode binding together; unkeyed writes invalidate old bindings. */
		void Set(const std::string& a_relativePath, const std::string& a_digestHex,
			std::optional<ShaderContentReuse::Record> a_contentKey = std::nullopt)
		{
			std::lock_guard lock(mutex);
			const auto oldKey = contentKeys.find(a_relativePath);
			const bool sameKey = a_contentKey ?
			                         oldKey != contentKeys.end() && oldKey->second == *a_contentKey :
			                         oldKey == contentKeys.end();
			if (auto it = entries.find(a_relativePath); it != entries.end() && it->second == a_digestHex && sameKey)
				return;
			entries[a_relativePath] = a_digestHex;
			if (a_contentKey)
				contentKeys.insert_or_assign(a_relativePath, std::move(*a_contentKey));
			else
				contentKeys.erase(a_relativePath);
			dirty = true;
		}

		bool Erase(const std::string& a_relativePath)
		{
			std::lock_guard lock(mutex);
			contentKeys.erase(a_relativePath);
			if (entries.erase(a_relativePath) == 0)
				return false;

			dirty = true;
			return true;
		}

		void Clear()
		{
			std::lock_guard lock(mutex);
			entries.clear();
			contentKeys.clear();
			dirty = false;
		}

		size_t PruneIf(const std::function<bool(const std::string&)>& a_shouldRemove)
		{
			std::lock_guard lock(mutex);
			size_t removed = 0;
			for (auto it = entries.begin(); it != entries.end();) {
				if (a_shouldRemove(it->first)) {
					contentKeys.erase(it->first);
					it = entries.erase(it);
					++removed;
					dirty = true;
				} else {
					++it;
				}
			}
			return removed;
		}

		bool Save()
		{
			std::lock_guard lock(mutex);
			if (!dirty || path.empty())
				return true;

			std::filesystem::path temporaryPath;
			std::error_code error;
			try {
				nlohmann::json json;
				json["schemaVersion"] = kSchemaVersion;
				json["entries"] = entries;
				for (const auto& [key, record] : contentKeys)
					json["contentKeys"][key] = ShaderContentReuse::Serialize(record);

				const auto parentPath = path.parent_path();
				if (!parentPath.empty()) {
					std::filesystem::create_directories(parentPath, error);
					if (error)
						return false;
				}

				temporaryPath = parentPath /
				                std::format("{}.{}.tmp", path.filename().string(), std::random_device{}());
				std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
				if (!stream.is_open())
					return false;
				stream << json.dump();
				stream.close();
				if (!stream) {
					std::filesystem::remove(temporaryPath, error);
					return false;
				}
			} catch (...) {
				if (!temporaryPath.empty())
					std::filesystem::remove(temporaryPath, error);
				return false;
			}

#ifdef _WIN32
			// std::filesystem::rename does not replace an existing file on
			// Windows. MoveFileEx keeps repeated runtime updates atomic.
			if (!MoveFileExW(
					temporaryPath.c_str(),
					path.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				std::filesystem::remove(temporaryPath, error);
				return false;
			}
#else
			std::filesystem::rename(temporaryPath, path, error);
			if (error) {
				std::filesystem::remove(temporaryPath, error);
				return false;
			}
#endif

			dirty = false;
			return true;
		}

	private:
		mutable std::mutex mutex;
		std::filesystem::path path;
		std::unordered_map<std::string, std::string> entries;
		std::unordered_map<std::string, ShaderContentReuse::Record> contentKeys;
		bool dirty = false;
	};
}
