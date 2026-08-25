#pragma once

// Disk shader-cache invalidation logic, kept free of game/SKSE dependencies.
// Unknown or failed paths degrade to invalidating more, never less: serving a
// blob compiled under a different feature set is silent corruption.

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
			ShaderAbi,
			ShaderCompiler,
			FeatureVersion,
			EnabledFlip,
		};

		Kind kind;
		std::string shortName;
		std::string feature;
		std::string detail;
		bool nowPresent = false;
	};

	struct FeatureState
	{
		std::string shortName;
		std::string name;
		bool loaded = false;
		std::string version;
		std::string define;
	};

	struct CacheIniEntry
	{
		bool enabled = false;
		std::optional<std::string> version;
	};

	inline std::vector<CacheMismatch> ClassifyMismatches(
		const std::string& currentPluginVersion,
		const std::optional<std::string>& cachedPluginVersion,
		const std::string& currentShaderAbi,
		const std::optional<std::string>& cachedShaderAbi,
		const std::string& currentShaderCompiler,
		const std::optional<std::string>& cachedShaderCompiler,
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

		if (!cachedShaderAbi) {
			mismatches.push_back({ CacheMismatch::Kind::ShaderAbi, "ShaderABI", "Shader cache ABI", "no shader cache ABI found in cache" });
		} else if (*cachedShaderAbi != currentShaderAbi) {
			mismatches.push_back({ CacheMismatch::Kind::ShaderAbi, "ShaderABI", "Shader cache ABI",
				std::format("contract changed (current: {}, cached: {})", currentShaderAbi, *cachedShaderAbi) });
		}

		// Compiler identity is present only for caches produced locally at runtime.
		// Shipped precompiled caches deliberately omit it; their packaged artifact
		// and Build ID provide provenance, and bytecode does not require the local
		// d3dcompiler DLL to match the build host.
		if (cachedShaderCompiler && *cachedShaderCompiler != currentShaderCompiler) {
			mismatches.push_back({ CacheMismatch::Kind::ShaderCompiler, "ShaderCompiler", "Shader compiler",
				std::format("compiler changed (current: {}, cached: {})", currentShaderCompiler, *cachedShaderCompiler) });
		}

		for (const auto& feature : features) {
			const auto it = cacheEntries.find(feature.shortName);
			const bool enabledInCache = it != cacheEntries.end() && it->second.enabled;
			if (enabledInCache != feature.loaded) {
				mismatches.push_back({ CacheMismatch::Kind::EnabledFlip, feature.shortName, feature.name,
					feature.loaded ?
						"installed/enabled now, but the cache was built without it" :
						"the cache was built with it, but it is now uninstalled or disabled at boot",
					feature.loaded });
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
				if (!visited.insert(file).second)
					continue;

				std::ifstream stream(file);
				if (!stream)
					return std::nullopt;

				std::string line;
				while (std::getline(stream, line)) {
					for (size_t pos = line.find(token); pos != std::string::npos; pos = line.find(token, pos + 1)) {
						const auto isIdent = [](char c) {
							return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
						};
						const bool beforeOk = pos == 0 || !isIdent(line[pos - 1]);
						const bool afterOk = pos + token.size() >= line.size() || !isIdent(line[pos + token.size()]);
						if (beforeOk && afterOk)
							return true;
					}

					std::smatch m;
					if (std::regex_search(line, m, includeRe)) {
						const auto byRoot = shadersRoot / m[1].str();
						const auto byLocal = file.parent_path() / m[1].str();
						if (std::filesystem::exists(byRoot))
							queue.push_back(byRoot);
						else if (std::filesystem::exists(byLocal))
							queue.push_back(byLocal);
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
		size_t* outKept = nullptr)
	{
		try {
			for (const auto& define : defines) {
				if (define.empty())
					return false;
			}

			// ImageSpace cache directories use runtime technique names rather than source stems.
			std::vector<std::filesystem::path> imageSpaceRoots;
			for (const auto& entry : std::filesystem::directory_iterator(shadersRoot)) {
				if (entry.is_regular_file() && entry.path().extension() == L".hlsl" &&
					(entry.path().stem().wstring().starts_with(L"IS") || entry.path().stem() == L"Utility")) {
					imageSpaceRoots.push_back(entry.path());
				}
			}
			std::map<std::pair<std::filesystem::path, std::string>, std::optional<bool>> referenceCache;
			const auto referencesDefine = [&](const std::filesystem::path& root, const std::string& define) -> const std::optional<bool>& {
				auto [it, inserted] = referenceCache.try_emplace({ root, define });
				if (inserted)
					it->second = RootShaderReferencesToken(root, define, shadersRoot);
				return it->second;
			};

			size_t deleted = 0;
			size_t kept = 0;

			for (const auto& entry : std::filesystem::directory_iterator(cacheRoot)) {
				if (!entry.is_directory())
					continue;

				const auto dirName = entry.path().filename().wstring();
				const auto root = shadersRoot / (dirName + L".hlsl");
				bool affected = false;
				const bool isImageSpace = dirName.starts_with(L"IS") || dirName == L"ReflectionsRayTracing";
				if (isImageSpace) {
					bool sourceResolved = false;
					for (const auto& imageSpaceRoot : imageSpaceRoots) {
						const auto sourceName = imageSpaceRoot.stem().wstring();
						const bool isUtility = sourceName == L"Utility";
						const bool matchesTechnique = dirName.starts_with(sourceName) ||
						                              (sourceName.starts_with(L"IS") && dirName.starts_with(sourceName.substr(2)));
						if (!isUtility && !matchesTechnique)
							continue;

						sourceResolved = sourceResolved || matchesTechnique;
						for (const auto& define : defines) {
							const auto& refs = referencesDefine(imageSpaceRoot, define);
							if (!refs.has_value())
								return false;
							if (*refs) {
								affected = true;
								break;
							}
						}
						if (affected)
							break;
					}
					// Unknown remaps cannot be proven independent of the changed feature.
					affected = affected || !sourceResolved;
				} else if (std::filesystem::exists(root)) {
					for (const auto& define : defines) {
						const auto& refs = referencesDefine(root, define);
						if (!refs.has_value())
							return false;
						if (*refs) {
							affected = true;
							break;
						}
					}
				} else {
					return false;
				}

				if (affected) {
					std::filesystem::remove_all(entry.path());
					++deleted;
				} else {
					++kept;
				}
			}

			if (outDeleted)
				*outDeleted = deleted;
			if (outKept)
				*outKept = kept;
			return true;
		} catch (...) {
			return false;
		}
	}
}
