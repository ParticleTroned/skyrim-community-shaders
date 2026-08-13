#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace Util::CacheInvalidation
{
	struct CacheMismatch
	{
		enum class Kind
		{
			PluginVersion,
			FeatureVersion,
			EnabledFlip,
		};

		Kind kind;
		std::string shortName;
		std::string feature;
		std::string detail;
		bool nowPresent = false;
		bool nowFailed = false;
	};

	struct FeatureState
	{
		std::string shortName;
		std::string name;
		bool loaded = false;
		std::string version;
		std::string define;
		bool failedToLoad = false;
	};

	struct CacheIniEntry
	{
		bool enabled = false;
		std::optional<std::string> version;
	};

	inline std::vector<CacheMismatch> ClassifyMismatches(
		const std::string& currentPluginVersion,
		const std::optional<std::string>& cachedPluginVersion,
		const std::vector<FeatureState>& features,
		const std::map<std::string, CacheIniEntry>& cacheEntries)
	{
		std::vector<CacheMismatch> mismatches;
		if (!cachedPluginVersion) {
			mismatches.push_back({ CacheMismatch::Kind::PluginVersion, "Plugin", "Plugin", "no plugin version found in cache" });
		} else if (*cachedPluginVersion != currentPluginVersion) {
			mismatches.push_back({ CacheMismatch::Kind::PluginVersion, "Plugin", "Plugin",
				std::format("version changed (current: {}, cached: {})", currentPluginVersion, *cachedPluginVersion) });
		}

		for (const auto& feature : features) {
			const auto it = cacheEntries.find(feature.shortName);
			const bool enabledInCache = it != cacheEntries.end() && it->second.enabled;
			if (enabledInCache != feature.loaded) {
				mismatches.push_back({ CacheMismatch::Kind::EnabledFlip, feature.shortName, feature.name,
					feature.loaded ?
						"installed/enabled now, but the cache was built without it" :
						"the cache was built with it, but it is now uninstalled or disabled at boot",
					feature.loaded, !feature.loaded && feature.failedToLoad });
				continue;
			}

			if (feature.loaded) {
				const auto& cachedVersion = it->second.version;
				if (!cachedVersion || *cachedVersion != feature.version) {
					mismatches.push_back({ CacheMismatch::Kind::FeatureVersion, feature.shortName, feature.name,
						std::format("version changed (installed: {}, cached: {})", feature.version,
							cachedVersion ? *cachedVersion : "<none>") });
				}
			}
		}

		return mismatches;
	}

	inline bool HasFailedFeature(const std::vector<CacheMismatch>& mismatches)
	{
		return std::ranges::any_of(mismatches,
			[](const CacheMismatch& mismatch) {
				return mismatch.kind == CacheMismatch::Kind::EnabledFlip && !mismatch.nowPresent && mismatch.nowFailed;
			});
	}

	inline std::optional<bool> RootShaderReferencesToken(
		const std::filesystem::path& root,
		const std::string& token,
		const std::filesystem::path& shadersRoot)
	{
		try {
			static const std::regex includeRe(R"#(^\s*#\s*include\s+"([^"]+)")#");
			std::set<std::filesystem::path> visited;
			std::vector<std::filesystem::path> queue{ root };
			while (!queue.empty()) {
				auto file = queue.back().lexically_normal();
				queue.pop_back();
				if (!visited.insert(file).second) {
					continue;
				}

				std::ifstream stream(file);
				if (!stream) {
					return std::nullopt;
				}

				std::string line;
				while (std::getline(stream, line)) {
					for (size_t pos = line.find(token); pos != std::string::npos; pos = line.find(token, pos + 1)) {
						const auto isIdent = [](char c) {
							return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
						};
						const bool beforeOk = pos == 0 || !isIdent(line[pos - 1]);
						const bool afterOk = pos + token.size() >= line.size() || !isIdent(line[pos + token.size()]);
						if (beforeOk && afterOk) {
							return true;
						}
					}

					std::smatch match;
					if (std::regex_search(line, match, includeRe)) {
						auto byRoot = shadersRoot / match[1].str();
						auto byLocal = file.parent_path() / match[1].str();
						if (std::filesystem::exists(byRoot)) {
							queue.push_back(byRoot);
						} else if (std::filesystem::exists(byLocal)) {
							queue.push_back(byLocal);
						}
					}
				}
			}

			return false;
		} catch (...) {
			return std::nullopt;
		}
	}

	inline bool TryPartialInvalidation(
		const std::filesystem::path& cacheRoot,
		const std::filesystem::path& shadersRoot,
		const std::vector<std::string>& defines,
		size_t* outDeleted = nullptr,
		size_t* outKept = nullptr,
		bool* outDestructivePartialFailure = nullptr)
	{
		if (outDeleted) {
			*outDeleted = 0;
		}
		if (outKept) {
			*outKept = 0;
		}
		if (outDestructivePartialFailure) {
			*outDestructivePartialFailure = false;
		}

		bool deletionStarted = false;
		try {
			if (defines.empty()) {
				return false;
			}
			for (const auto& define : defines) {
				if (define.empty()) {
					return false;
				}
			}

			// Cache-directory names for ImageSpace techniques do not always equal
			// their source filename. Index every real IS root plus the shared Utility
			// root, and memoize dependency scans used by remapped techniques.
			std::vector<std::filesystem::path> imageSpaceRoots;
			bool hasImageSpaceUtility = false;
			for (const auto& entry : std::filesystem::directory_iterator(shadersRoot)) {
				if (!entry.is_regular_file() || entry.path().extension() != L".hlsl") {
					continue;
				}
				const auto stem = entry.path().stem().wstring();
				if (stem.starts_with(L"IS") || stem == L"Utility") {
					imageSpaceRoots.push_back(entry.path());
					hasImageSpaceUtility = hasImageSpaceUtility || stem == L"Utility";
				}
			}

			std::map<std::pair<std::filesystem::path, std::string>, std::optional<bool>> referenceCache;
			const auto referencesDefine = [&](const std::filesystem::path& root, const std::string& define) -> const std::optional<bool>& {
				auto [it, inserted] = referenceCache.try_emplace({ root, define });
				if (inserted) {
					it->second = RootShaderReferencesToken(root, define, shadersRoot);
				}
				return it->second;
			};

			// Finish every source/include scan before touching the cache. A scan
			// failure therefore leaves the active cache intact for rollback.
			std::vector<std::filesystem::path> toDelete;
			size_t kept = 0;
			for (const auto& entry : std::filesystem::directory_iterator(cacheRoot)) {
				if (!entry.is_directory()) {
					continue;
				}

				const auto directoryName = entry.path().filename().wstring();
				const auto root = shadersRoot / (directoryName + L".hlsl");
				bool affected = false;
				const bool isImageSpace = directoryName.starts_with(L"IS") || directoryName == L"ReflectionsRayTracing";
				if (isImageSpace) {
					if (!hasImageSpaceUtility) {
						return false;
					}
					bool techniqueResolved = false;
					for (const auto& imageSpaceRoot : imageSpaceRoots) {
						const auto sourceName = imageSpaceRoot.stem().wstring();
						const bool isUtility = sourceName == L"Utility";
						const bool matchesTechnique = directoryName.starts_with(sourceName) ||
						                              (sourceName.starts_with(L"IS") && directoryName.starts_with(sourceName.substr(2)));
						if (!isUtility && !matchesTechnique) {
							continue;
						}
						techniqueResolved = techniqueResolved || matchesTechnique;
						for (const auto& define : defines) {
							const auto& refs = referencesDefine(imageSpaceRoot, define);
							if (!refs.has_value()) {
								return false;
							}
							if (*refs) {
								affected = true;
								break;
							}
						}
						if (affected) {
							break;
						}
					}
					// An unknown remapping cannot be proven independent of the define.
					affected = affected || !techniqueResolved;
				} else if (std::filesystem::exists(root)) {
					for (const auto& define : defines) {
						const auto& refs = referencesDefine(root, define);
						if (!refs.has_value()) {
							return false;
						}
						if (*refs) {
							affected = true;
							break;
						}
					}
				} else {
					return false;
				}

				if (affected) {
					toDelete.push_back(entry.path());
				} else {
					++kept;
				}
			}

			const auto countEntries = [](const std::filesystem::path& path) -> std::optional<size_t> {
				std::error_code error;
				std::filesystem::recursive_directory_iterator it(path, error);
				const std::filesystem::recursive_directory_iterator end;
				if (error) {
					return std::nullopt;
				}

				size_t count = 0;
				while (it != end) {
					++count;
					it.increment(error);
					if (error) {
						return std::nullopt;
					}
				}
				return count;
			};

			std::map<std::filesystem::path, size_t> entryCounts;
			for (const auto& path : toDelete) {
				const auto count = countEntries(path);
				if (!count) {
					return false;
				}
				entryCounts.emplace(path, *count);
			}

			size_t deleted = 0;
			for (const auto& path : toDelete) {
				deletionStarted = true;
				std::error_code removeError;
				std::filesystem::remove_all(path, removeError);
				if (!removeError) {
					++deleted;
					if (outDeleted) {
						*outDeleted = deleted;
					}
					continue;
				}

				std::error_code existsError;
				const bool stillExists = std::filesystem::exists(path, existsError);
				const auto after = !existsError && stillExists ? countEntries(path) : std::optional<size_t>{ 0 };
				const bool inspectionUncertain = existsError || !after;
				const bool partiallyRemoved = !inspectionUncertain && (!stillExists || *after < entryCounts.at(path));
				if (outDestructivePartialFailure) {
					*outDestructivePartialFailure = deleted > 0 || partiallyRemoved || inspectionUncertain;
				}
				return false;
			}

			if (outDeleted) {
				*outDeleted = deleted;
			}
			if (outKept) {
				*outKept = kept;
			}
			return true;
		} catch (...) {
			if (outDestructivePartialFailure && deletionStarted) {
				*outDestructivePartialFailure = true;
			}
			return false;
		}
	}
}
