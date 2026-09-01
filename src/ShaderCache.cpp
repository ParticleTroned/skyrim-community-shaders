#include "ShaderCache.h"
#include "Api/ShaderCompatibilityRegistry.h"
#include "BuildProvenance.h"

#include "Globals.h"
#include "ShaderCacheDisablePolicy.h"
#include "ShaderFileWatcher.h"
#include "Util.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "Deferred.h"
#include "Feature.h"
#include "State.h"
#include "Utils/ContentHash.h"
#include "Utils/GenerationClaim.h"
#include "Utils/ShaderCacheManifest.h"
#include "Utils/ShaderCachePack.h"

#include "Features/DynamicCubemaps.h"
#include "Features/Upscaling.h"

#include "Plugin.h"

namespace SIE
{
	namespace
	{
		bool IsSaveLoadSafeModeActive()
		{
			auto* state = globals::state;
			return state && state->IsSaveLoadSafeModeActive();
		}

		struct IncludeParseEntry
		{
			std::chrono::system_clock::time_point selfMTime;
			std::string includeRootKey;
			std::vector<std::filesystem::path> includes;
			std::optional<Util::ContentHash::Hash128> selfContentHash;
		};

		std::string NormalizedPathKey(const std::filesystem::path& a_path)
		{
			std::string key = a_path.lexically_normal().string();
#ifdef _WIN32
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char a_char) { return static_cast<char>(std::tolower(a_char)); });
#endif
			return key;
		}

		void FoldClosureFingerprint(
			Util::ContentHash::Hash128* a_fingerprint,
			const std::string& a_key,
			std::chrono::system_clock::time_point a_mtime)
		{
			if (!a_fingerprint)
				return;
			const int64_t ticks = a_mtime.time_since_epoch().count();
			*a_fingerprint = Util::ContentHash::CombineHashes(
				*a_fingerprint,
				Util::ContentHash::CombineHashes(
					Util::ContentHash::HashString(a_key),
					Util::ContentHash::HashBytes(&ticks, sizeof(ticks))));
		}

		std::chrono::system_clock::time_point GetMaxShaderMTimeInternal(
			const std::filesystem::path& a_path,
			const std::filesystem::path& a_shadersRoot,
			std::unordered_map<std::string, IncludeParseEntry>& a_parseCache,
			std::mutex& a_parseCacheMutex,
			std::unordered_map<std::string, std::chrono::system_clock::time_point>& a_callResults,
			Util::ContentHash::Hash128* a_fingerprint = nullptr)
		{
			const std::string key = NormalizedPathKey(a_path);
			const std::string includeRootKey = NormalizedPathKey(a_shadersRoot);
			if (auto it = a_callResults.find(key); it != a_callResults.end())
				return it->second;

			// In-progress marker: include cycles resolve to min() and fall out of the max reduction.
			a_callResults[key] = std::chrono::system_clock::time_point::min();

			std::error_code ec;
			const auto selfMTime = std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(a_path, ec));
			if (ec) {
				// Unreadable source should force recompilation instead of serving a stale disk cache entry.
				const auto now = std::chrono::system_clock::now();
				a_callResults[key] = now;
				FoldClosureFingerprint(a_fingerprint, key, now);
				return now;
			}
			FoldClosureFingerprint(a_fingerprint, key, selfMTime);

			std::vector<std::filesystem::path> includes;
			bool cached = false;
			{
				std::lock_guard lock(a_parseCacheMutex);
				if (auto it = a_parseCache.find(key);
					it != a_parseCache.end() &&
					it->second.selfMTime == selfMTime &&
					it->second.includeRootKey == includeRootKey) {
					includes = it->second.includes;
					cached = true;
				}
			}

			if (!cached) {
				std::ifstream ifs(a_path);
				if (!ifs.is_open()) {
					const auto now = std::chrono::system_clock::now();
					a_callResults[key] = now;
					return now;
				}

				std::string line;
				while (std::getline(ifs, line)) {
					size_t pos = line.find_first_not_of(" \t");
					if (pos == std::string::npos || line[pos] != '#')
						continue;

					pos = line.find_first_not_of(" \t", pos + 1);
					if (pos == std::string::npos || line.compare(pos, 7, "include") != 0)
						continue;

					// Accept both quoted and angle-bracket includes; under-tracking either
					// form risks serving a stale cache, which the textual scan must never do.
					const size_t afterInclude = pos + 7;
					const size_t firstDelim = line.find_first_of("\"<", afterInclude);
					if (firstDelim == std::string::npos)
						continue;

					const char closeDelim = line[firstDelim] == '"' ? '"' : '>';
					const size_t secondDelim = line.find(closeDelim, firstDelim + 1);
					if (secondDelim == std::string::npos || secondDelim == firstDelim + 1)
						continue;

					const std::string includeName = line.substr(firstDelim + 1, secondDelim - firstDelim - 1);

					std::error_code rootEc, parentEc;
					std::filesystem::path includePath = a_shadersRoot / includeName;
					if (!std::filesystem::is_regular_file(includePath, rootEc)) {
						includePath = a_path.parent_path() / includeName;
						if (!std::filesystem::is_regular_file(includePath, parentEc))
							continue;
					}

					includes.push_back(std::move(includePath));
				}

				std::lock_guard lock(a_parseCacheMutex);
				a_parseCache[key] = IncludeParseEntry{ selfMTime, includeRootKey, includes };
			}

			auto maxTime = selfMTime;
			for (const auto& includePath : includes)
				maxTime = std::max(maxTime, GetMaxShaderMTimeInternal(
												includePath,
												a_shadersRoot,
												a_parseCache,
												a_parseCacheMutex,
												a_callResults,
												a_fingerprint));

			a_callResults[key] = maxTime;
			return maxTime;
		}

		std::unordered_map<std::string, IncludeParseEntry> g_shaderIncludeParseCache;
		std::mutex g_shaderIncludeParseCacheMutex;

		std::chrono::system_clock::time_point GetMaxShaderMTime(
			const std::filesystem::path& a_path,
			const std::filesystem::path& a_shadersRoot,
			Util::ContentHash::Hash128* a_fingerprint = nullptr)
		{
			std::unordered_map<std::string, std::chrono::system_clock::time_point> callResults;
			return GetMaxShaderMTimeInternal(
				a_path,
				a_shadersRoot,
				g_shaderIncludeParseCache,
				g_shaderIncludeParseCacheMutex,
				callResults,
				a_fingerprint);
		}

		struct ClosureDigestEntry
		{
			Util::ContentHash::Hash128 closureFingerprint;
			Util::ContentHash::Hash128 digest;
		};

		std::unordered_map<std::string, ClosureDigestEntry> g_shaderClosureDigestCache;
		std::mutex g_shaderClosureDigestCacheMutex;

		std::optional<Util::ContentHash::Hash128> GetShaderContentDigestInternal(
			const std::filesystem::path& a_path,
			const std::filesystem::path& a_shadersRoot,
			std::unordered_map<std::string, IncludeParseEntry>& a_parseCache,
			std::mutex& a_parseCacheMutex,
			std::unordered_map<std::string, std::optional<Util::ContentHash::Hash128>>& a_callResults)
		{
			const std::string key = NormalizedPathKey(a_path);
			const std::string includeRootKey = NormalizedPathKey(a_shadersRoot);
			if (auto it = a_callResults.find(key); it != a_callResults.end())
				return it->second;

			// Include cycles contribute nothing extra to their own digest.
			a_callResults[key] = std::nullopt;

			std::error_code error;
			const auto selfMTime = std::chrono::clock_cast<std::chrono::system_clock>(
				std::filesystem::last_write_time(a_path, error));

			std::optional<Util::ContentHash::Hash128> selfHash;
			std::vector<std::filesystem::path> includes;
			{
				std::lock_guard lock(a_parseCacheMutex);
				if (auto it = a_parseCache.find(key);
					it != a_parseCache.end() && it->second.includeRootKey == includeRootKey) {
					includes = it->second.includes;
					if (!error &&
						it->second.selfMTime == selfMTime &&
						it->second.selfContentHash.has_value()) {
						selfHash = it->second.selfContentHash;
					}
				}
			}

			if (!selfHash) {
				selfHash = Util::ContentHash::HashFile(a_path);
				if (selfHash && !error) {
					std::lock_guard lock(a_parseCacheMutex);
					if (auto it = a_parseCache.find(key);
						it != a_parseCache.end() && it->second.includeRootKey == includeRootKey) {
						it->second.selfMTime = selfMTime;
						it->second.selfContentHash = selfHash;
					}
				}
			}
			if (!selfHash)
				return std::nullopt;

			std::vector<std::pair<std::string, std::filesystem::path>> sortedIncludes;
			sortedIncludes.reserve(includes.size());
			for (const auto& includePath : includes)
				sortedIncludes.emplace_back(NormalizedPathKey(includePath), includePath);
			std::sort(
				sortedIncludes.begin(),
				sortedIncludes.end(),
				[](const auto& a_left, const auto& a_right) { return a_left.first < a_right.first; });

			auto combined = *selfHash;
			for (const auto& include : sortedIncludes) {
				if (auto childHash = GetShaderContentDigestInternal(
						include.second,
						a_shadersRoot,
						a_parseCache,
						a_parseCacheMutex,
						a_callResults)) {
					combined = Util::ContentHash::CombineHashes(combined, *childHash);
				}
			}

			a_callResults[key] = combined;
			return combined;
		}

		std::optional<Util::ContentHash::Hash128> GetShaderContentDigest(
			const std::filesystem::path& a_path,
			const std::filesystem::path& a_shadersRoot)
		{
			Util::ContentHash::Hash128 fingerprint{};
			GetMaxShaderMTime(a_path, a_shadersRoot, &fingerprint);

			const std::string rootKey = NormalizedPathKey(a_path);
			{
				std::lock_guard lock(g_shaderClosureDigestCacheMutex);
				if (auto it = g_shaderClosureDigestCache.find(rootKey);
					it != g_shaderClosureDigestCache.end() &&
					it->second.closureFingerprint == fingerprint) {
					return it->second.digest;
				}
			}

			std::unordered_map<std::string, std::optional<Util::ContentHash::Hash128>> callResults;
			const auto result = GetShaderContentDigestInternal(
				a_path,
				a_shadersRoot,
				g_shaderIncludeParseCache,
				g_shaderIncludeParseCacheMutex,
				callResults);
			if (result) {
				std::lock_guard lock(g_shaderClosureDigestCacheMutex);
				g_shaderClosureDigestCache[rootKey] = ClosureDigestEntry{ fingerprint, *result };
			}
			return result;
		}

		std::vector<std::string> GetShaderDependencyPaths(
			const std::filesystem::path& a_path,
			const std::filesystem::path& a_shadersRoot)
		{
			GetMaxShaderMTime(a_path, a_shadersRoot);
			std::vector<std::filesystem::path> queue{ a_path };
			std::unordered_set<std::string> visited;
			std::vector<std::string> dependencies;
			while (!queue.empty()) {
				const auto current = queue.back();
				queue.pop_back();
				const auto key = NormalizedPathKey(current);
				if (!visited.insert(key).second)
					continue;

				std::vector<std::filesystem::path> includes;
				{
					std::lock_guard lock(g_shaderIncludeParseCacheMutex);
					if (const auto it = g_shaderIncludeParseCache.find(key);
						it != g_shaderIncludeParseCache.end())
						includes = it->second.includes;
				}

				for (const auto& include : includes) {
					std::error_code error;
					const auto canonical = std::filesystem::weakly_canonical(include, error);
					dependencies.push_back((error ? include : canonical).string());
					queue.push_back(include);
				}
			}
			std::ranges::sort(dependencies);
			dependencies.erase(std::ranges::unique(dependencies).begin(), dependencies.end());
			return dependencies;
		}

		const std::filesystem::path& ShaderSourceRoot()
		{
			static const std::filesystem::path root = L"Data/Shaders";
			return root;
		}

		std::string GetManifestKey(const std::wstring& a_diskPath)
		{
			static constexpr std::wstring_view prefix = L"Data/ShaderCache/";
			const std::wstring_view relativePath = a_diskPath.starts_with(prefix) ?
			                                           std::wstring_view(a_diskPath).substr(prefix.size()) :
			                                           std::wstring_view(a_diskPath);
			auto key = Util::WStringToString(std::wstring(relativePath));
			std::replace(key.begin(), key.end(), '\\', '/');
			return key;
		}

		Util::ShaderCacheManifest::Manifest& GetShaderCacheManifest()
		{
			static Util::ShaderCacheManifest::Manifest manifest;
			static std::once_flag loaded;
			std::call_once(loaded, [] {
				manifest.Load(L"Data/ShaderCache/Manifest.json");
			});
			return manifest;
		}

		struct GlobalCompileStateSnapshot
		{
			bool developerMode;
			bool isVR;
			bool partialPrecision;
			bool avoidFlowControl;
			std::shared_ptr<const State::ShaderDefinesSnapshot> shaderDefines;
			Util::ContentHash::Hash128 digest;
		};

		Util::ContentHash::Hash128 GetGlobalCompileStateDigest(
			const GlobalCompileStateSnapshot& a_snapshot)
		{
			std::string state;
			if (a_snapshot.developerMode)
				state += "D3DCOMPILE_SKIP_OPTIMIZATION;D3DCOMPILE_DEBUG;";
			if (a_snapshot.isVR)
				state += "VR;";
			if (a_snapshot.partialPrecision)
				state += "D3DCOMPILE_PARTIAL_PRECISION;";
			if (a_snapshot.avoidFlowControl)
				state += "D3DCOMPILE_AVOID_FLOW_CONTROL;";
			state += "ShaderCacheABI=";
			state += BuildProvenance::GetShaderCacheAbiId();
			state += ';';
			state += a_snapshot.shaderDefines->canonicalText;
			return Util::ContentHash::HashString(state);
		}

		GlobalCompileStateSnapshot CaptureGlobalCompileState()
		{
			auto* state = globals::state;
			GlobalCompileStateSnapshot snapshot{
				state->IsDeveloperMode(),
				REL::Module::IsVR(),
				state->enablePartialPrecision.load(std::memory_order_relaxed),
				state->enableAvoidFlowControl.load(std::memory_order_relaxed),
				state->GetShaderDefinesSnapshot(),
				{}
			};
			snapshot.digest = GetGlobalCompileStateDigest(snapshot);
			return snapshot;
		}

		constexpr uint64_t kManifestFlushBatchSize = 25;
		std::atomic<uint64_t> g_manifestWriteCount = 0;
		std::atomic<uint64_t> g_diskCacheGeneration = 0;
		std::shared_mutex g_diskCacheMutationMutex;

		uint64_t GetDiskCacheGeneration()
		{
			return g_diskCacheGeneration.load(std::memory_order_acquire);
		}

		void AdvanceDiskCacheGeneration()
		{
			g_diskCacheGeneration.fetch_add(1, std::memory_order_acq_rel);
		}

		void DiscardShaderCacheManifestLocked()
		{
			GetShaderCacheManifest().Clear();
			g_manifestWriteCount.store(0, std::memory_order_relaxed);
		}

		void ReloadShaderCacheManifestLocked()
		{
			GetShaderCacheManifest().Load(L"Data/ShaderCache/Manifest.json");
			g_manifestWriteCount.store(0, std::memory_order_relaxed);
		}

		void FlushShaderCacheManifestLocked()
		{
			if (!GetShaderCacheManifest().Save())
				logger::warn("Failed to flush Data/ShaderCache/Manifest.json");
		}

		void FlushShaderCacheManifest()
		{
			std::shared_lock lock{ g_diskCacheMutationMutex };
			FlushShaderCacheManifestLocked();
		}

		void RecordShaderDigest(
			const std::wstring& a_diskPath,
			const std::filesystem::path& a_shaderPath,
			const Util::ContentHash::Hash128& a_compileStateDigest)
		{
			auto& manifest = GetShaderCacheManifest();
			const auto manifestKey = GetManifestKey(a_diskPath);
			const auto digest = GetShaderContentDigest(a_shaderPath, ShaderSourceRoot());
			if (!digest) {
				// The blob has already replaced any prior cache entry. Do not
				// leave an old authoritative digest attached to the new bytes;
				// removing it deliberately restores the legacy mtime fallback.
				if (manifest.Erase(manifestKey))
					FlushShaderCacheManifestLocked();
				return;
			}

			const auto combined = Util::ContentHash::CombineHashes(
				*digest,
				a_compileStateDigest);
			manifest.Set(manifestKey, combined.ToHex());

			const auto writeCount = g_manifestWriteCount.fetch_add(1, std::memory_order_relaxed) + 1;
			if (writeCount % kManifestFlushBatchSize == 0)
				FlushShaderCacheManifestLocked();
		}

		struct ShaderPackIdentity
		{
			std::string logicalKey;
			std::string exactKey;
			std::string metadata;
		};

		bool ManagedShaderPackFilesPresent()
		{
			std::error_code error;
			for (const auto* path : {
					 L"Data/ShaderCache/Optimized.A.csxpack",
					 L"Data/ShaderCache/Optimized.B.csxpack",
					 L"Data/ShaderCache/Developer.A.csxpack",
					 L"Data/ShaderCache/Developer.B.csxpack" }) {
				error.clear();
				if (!std::filesystem::is_regular_file(path, error) || error)
					return false;
			}
			return true;
		}

		struct ManagedPackSet
		{
			Util::ShaderCachePack::Store optimized{
				L"Data/ShaderCache/Optimized.A.csxpack",
				L"Data/ShaderCache/Optimized.B.csxpack",
				Util::ShaderCachePack::Lane::Optimized
			};
			Util::ShaderCachePack::Store developer{
				L"Data/ShaderCache/Developer.A.csxpack",
				L"Data/ShaderCache/Developer.B.csxpack",
				Util::ShaderCachePack::Lane::Developer
			};
			std::once_flag opened;
			std::atomic_bool optimizedAvailable{ false };
			std::atomic_bool developerAvailable{ false };
		};

		ManagedPackSet& ManagedPacks()
		{
			static ManagedPackSet packs;
			return packs;
		}

		void QuarantineShaderPackLane(bool a_developerMode, std::string_view a_cause)
		{
			auto& packs = ManagedPacks();
			auto& available = a_developerMode ? packs.developerAvailable : packs.optimizedAvailable;
			if (available.exchange(false, std::memory_order_acq_rel)) {
				logger::error(
					"Quarantined {} managed shader pack for this process; using loose/source fallback: {}",
					a_developerMode ? "developer" : "optimized", a_cause);
			}
		}

		Util::ShaderCachePack::Store* GetShaderPackStore(bool a_developerMode)
		{
			if (!ManagedShaderPackFilesPresent())
				return nullptr;

			auto& packs = ManagedPacks();
			std::call_once(packs.opened, [&] {
				try {
					std::string optimizedError;
					std::string developerError;
					const bool optimizedOpen = packs.optimized.Open(&optimizedError);
					const bool developerOpen = packs.developer.Open(&developerError);
					packs.optimizedAvailable.store(optimizedOpen, std::memory_order_release);
					packs.developerAvailable.store(developerOpen, std::memory_order_release);
					if (!optimizedOpen || !developerOpen) {
						packs.optimizedAvailable.store(false, std::memory_order_release);
						packs.developerAvailable.store(false, std::memory_order_release);
						logger::warn(
							"Managed shader pack set unavailable; retaining loose-cache compatibility as a unit (optimized='{}', developer='{}')",
							optimizedError, developerError);
						return;
					}
					logger::info(
						"Opened managed shader pack set (optimized generation {}, developer generation {})",
						packs.optimized.GetStats().activeGeneration,
						packs.developer.GetStats().activeGeneration);
				} catch (const std::exception& e) {
					packs.optimizedAvailable.store(false, std::memory_order_release);
					packs.developerAvailable.store(false, std::memory_order_release);
					logger::error("Managed shader pack initialization failed; using loose/source fallback: {}", e.what());
				} catch (...) {
					packs.optimizedAvailable.store(false, std::memory_order_release);
					packs.developerAvailable.store(false, std::memory_order_release);
					logger::error("Managed shader pack initialization failed; using loose/source fallback");
				}
			});
			if (!(a_developerMode ? packs.developerAvailable : packs.optimizedAvailable).load(std::memory_order_acquire))
				return nullptr;
			return a_developerMode ? &packs.developer : &packs.optimized;
		}

		std::optional<ShaderPackIdentity> BuildShaderPackIdentity(
			const std::wstring& a_diskPath,
			const std::filesystem::path& a_shaderPath,
			const Util::ContentHash::Hash128& a_compileStateDigest)
		{
			const auto sourceDigest = GetShaderContentDigest(a_shaderPath, ShaderSourceRoot());
			if (!sourceDigest)
				return std::nullopt;
			const auto family = a_shaderPath.stem().string();
			const auto compatibility = CSX::Api::GetShaderCompatibilityRequirementSet(family, a_shaderPath.string());
			const auto contentContract = Util::ContentHash::CombineHashes(*sourceDigest, a_compileStateDigest).ToHex();
			ShaderPackIdentity identity;
			identity.logicalKey = std::format("{}|compat={}", GetManifestKey(a_diskPath), compatibility.digest);
			identity.exactKey = std::format("{}|content={}", identity.logicalKey, contentContract);
			identity.metadata = std::format(
				"schema=1\nsource={}\ncompile={}\ncompatibility-length={}\n{}",
				sourceDigest->ToHex(), a_compileStateDigest.ToHex(), compatibility.canonical.size(), compatibility.canonical);
			return identity;
		}

		ID3DBlob* LoadShaderBlobFromPack(
			Util::ShaderCachePack::Store& a_store,
			bool a_developerMode,
			const std::wstring& a_diskPath,
			const std::filesystem::path& a_shaderPath,
			const Util::ContentHash::Hash128& a_compileStateDigest)
		{
			try {
				const auto identity = BuildShaderPackIdentity(a_diskPath, a_shaderPath, a_compileStateDigest);
				if (!identity)
					return nullptr;
				std::string error;
				const auto entry = a_store.Find(identity->exactKey, &error);
				if (!entry) {
					if (!error.empty())
						QuarantineShaderPackLane(a_developerMode, error);
					return nullptr;
				}
				ID3DBlob* blob = nullptr;
				if (FAILED(D3DCreateBlob(entry->bytecode.size(), &blob)) || !blob)
					return nullptr;
				std::memcpy(blob->GetBufferPointer(), entry->bytecode.data(), entry->bytecode.size());
				return blob;
			} catch (const std::exception& e) {
				QuarantineShaderPackLane(a_developerMode, e.what());
				logger::warn("Managed shader pack read failed for {}; compiling from source: {}", Util::WStringToString(a_diskPath), e.what());
				return nullptr;
			} catch (...) {
				QuarantineShaderPackLane(a_developerMode, "unknown read failure");
				logger::warn("Managed shader pack read failed for {}; compiling from source", Util::WStringToString(a_diskPath));
				return nullptr;
			}
		}

		bool SaveShaderBlobToPack(
			ID3DBlob* a_shaderBlob,
			bool a_developerMode,
			const std::wstring& a_diskPath,
			const std::filesystem::path& a_shaderPath,
			const Util::ContentHash::Hash128& a_compileStateDigest)
		{
			try {
				auto* store = GetShaderPackStore(a_developerMode);
				const auto identity = BuildShaderPackIdentity(a_diskPath, a_shaderPath, a_compileStateDigest);
				if (!store || !identity)
					return false;
				Util::ShaderCachePack::Entry entry{
					.logicalKey = identity->logicalKey,
					.exactKey = identity->exactKey,
					.metadata = identity->metadata,
					.bytecode = {},
				};
				const auto* begin = static_cast<const std::byte*>(a_shaderBlob->GetBufferPointer());
				entry.bytecode.assign(begin, begin + a_shaderBlob->GetBufferSize());
				std::string error;
				if (!store->Append(entry, &error)) {
					QuarantineShaderPackLane(a_developerMode, error);
					logger::error("Failed to append shader pack record for {}: {}", Util::WStringToString(a_diskPath), error);
					return false;
				}
				logger::debug("Saved shader record to {} pack: {}", a_developerMode ? "developer" : "optimized", identity->exactKey);
				return true;
			} catch (const std::exception& e) {
				QuarantineShaderPackLane(a_developerMode, e.what());
				logger::error("Failed to persist managed shader pack record for {}: {}", Util::WStringToString(a_diskPath), e.what());
				return false;
			} catch (...) {
				QuarantineShaderPackLane(a_developerMode, "unknown write failure");
				logger::error("Failed to persist managed shader pack record for {}", Util::WStringToString(a_diskPath));
				return false;
			}
		}

		void CompactShaderPacksIfNeeded()
		{
			for (const bool developerMode : { false, true }) {
				try {
					auto* store = GetShaderPackStore(developerMode);
					if (!store || !store->ShouldCompact())
						continue;
					const auto before = store->GetStats();
					std::string error;
					if (!store->Compact(&error)) {
						QuarantineShaderPackLane(developerMode, error);
						continue;
					}
					const auto after = store->GetStats();
					logger::info(
						"Compacted {} shader pack generation {} -> {} (superseded {} bytes, fragmentation {:.1f}%)",
						developerMode ? "developer" : "optimized",
						before.activeGeneration,
						after.activeGeneration,
						before.supersededBytes,
						before.Fragmentation() * 100.0);
				} catch (const std::exception& e) {
					QuarantineShaderPackLane(developerMode, e.what());
				} catch (...) {
					QuarantineShaderPackLane(developerMode, "unknown compaction failure");
				}
			}
		}

		bool ManagedShaderPacksAvailable()
		{
			return GetShaderPackStore(false) && GetShaderPackStore(true);
		}

		bool ResetManagedShaderPacks()
		{
			bool success = true;
			for (const bool developerMode : { false, true }) {
				auto* store = GetShaderPackStore(developerMode);
				if (!store)
					continue;
				std::string error;
				if (!store->Reset(&error)) {
					success = false;
					QuarantineShaderPackLane(developerMode, error);
					logger::error("Failed to reset {} shader pack: {}", developerMode ? "developer" : "optimized", error);
				}
			}
			return success;
		}

		bool RemoveLooseDiskCacheEntries()
		{
			std::error_code error;
			if (!std::filesystem::exists(L"Data/ShaderCache", error))
				return !error;
			bool success = true;
			for (const auto& entry : std::filesystem::directory_iterator(L"Data/ShaderCache", error)) {
				if (error)
					return false;
				if (entry.is_regular_file(error)) {
					const auto filename = entry.path().filename();
					if (entry.path().extension() == L".csxpack" ||
						filename == L"Info.ini" || filename == L"Manifest.json" || filename == L"PackManifest.json")
						continue;
				}
				std::filesystem::remove_all(entry.path(), error);
				if (error) {
					success = false;
					logger::error("Failed to remove legacy shader-cache entry {}: {}", Util::WStringToString(entry.path().wstring()), error.message());
					error.clear();
				}
			}
			return success;
		}

		bool SaveShaderBlobToDisk(
			ID3DBlob* a_shaderBlob,
			bool a_developerMode,
			const std::wstring& a_diskPath,
			const std::filesystem::path& a_shaderPath,
			const Util::ContentHash::Hash128& a_compileStateDigest,
			const Util::ContentHash::Hash128& a_packCompileStateDigest,
			uint64_t a_diskCacheGeneration)
		{
			std::shared_lock lock{ g_diskCacheMutationMutex };
			if (a_diskCacheGeneration != GetDiskCacheGeneration()) {
				logger::debug(
					"Skipped stale shader-cache write to {} after a cache transition",
					Util::WStringToString(a_diskPath));
				return false;
			}

			if (GetShaderPackStore(a_developerMode)) {
				if (!SaveShaderBlobToPack(a_shaderBlob, a_developerMode, a_diskPath, a_shaderPath, a_packCompileStateDigest))
					return false;
				return true;
			}

			std::error_code error;
			std::filesystem::create_directories(
				std::filesystem::path(a_diskPath).parent_path(),
				error);
			if (error) {
				logger::error(
					"Failed to create shader cache folder for {}: {}",
					Util::WStringToString(a_diskPath),
					error.message());
				return false;
			}

			const HRESULT saveResult = D3DWriteBlobToFile(a_shaderBlob, a_diskPath.c_str(), true);
			if (FAILED(saveResult)) {
				logger::error("Failed to save shader to {}", Util::WStringToString(a_diskPath));
				return false;
			}

			logger::debug("Saved shader to {}", Util::WStringToString(a_diskPath));
			RecordShaderDigest(a_diskPath, a_shaderPath, a_compileStateDigest);
			return true;
		}
	}

	// Custom include handler to track all includes during shader compilation
	class TrackingIncludeHandler : public ID3DInclude
	{
	public:
		// Captured include paths (normalized)
		std::vector<std::string> includes;
		// Owned buffers for include contents; kept alive for the lifetime of this handler
		std::vector<std::vector<char>> buffers;
		std::filesystem::path baseDir;

		TrackingIncludeHandler(const std::filesystem::path& base) :
			baseDir(base) {}

		HRESULT Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID /*pParentData*/, LPCVOID* ppData, UINT* pBytes) override
		{
			(void)IncludeType;
			try {
				std::filesystem::path includePath = baseDir / pFileName;
				// Normalize path to reduce duplicates (weakly_canonical may throw)
				std::error_code ec;
				auto canonical = std::filesystem::weakly_canonical(includePath, ec);
				std::string pathStr = (ec ? includePath.string() : canonical.string());
				// On Windows, normalize to lowercase for comparison
#ifdef _WIN32
				std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(), [](unsigned char c) { return std::tolower(c); });
#endif
				includes.push_back(pathStr);

				// Read file into owned buffer
				std::ifstream ifs(pathStr, std::ios::binary | std::ios::ate);
				if (!ifs)
					return E_FAIL;
				std::streamsize size = ifs.tellg();
				if (size < 0)
					return E_FAIL;
				ifs.seekg(0, std::ios::beg);
				std::vector<char> buf(static_cast<size_t>(size));
				if (size > 0) {
					if (!ifs.read(buf.data(), size))
						return E_FAIL;
				}
				buffers.push_back(std::move(buf));
				const auto& storage = buffers.back();
				*ppData = storage.empty() ? nullptr : storage.data();
				*pBytes = static_cast<UINT>(storage.size());
				return S_OK;
			} catch (...) {
				return E_FAIL;
			}
		}

		HRESULT Close(LPCVOID /*pData*/) override
		{
			// Buffers are owned by this handler; no action required on Close.
			return S_OK;
		}
	};

	namespace SShaderCache
	{
		static void GetShaderDefines(const RE::BSShader&, uint32_t, D3D_SHADER_MACRO*);
		static std::string GetShaderString(ShaderClass, const RE::BSShader&, uint32_t, bool = false);
		/**
		 * @brief Resolve image-space shader descriptor when applicable.
		 *
		 * If @p shader is an image-space shader, attempts to map it to a
		 * runtime image-space descriptor via GetImagespaceShaderDescriptor and
		 * returns true on success. If the shader is not image-space the
		 * function returns true and leaves @p descriptor unchanged. Returns
		 * false only when the shader is image-space and no valid descriptor
		 * could be resolved.
		 *
		 * This helper is used by the shader loading and caching code paths to
		 * determine whether an image-space shader can be loaded or cached. If
		 * this function returns false the caller should skip loading/compiling
		 * and caching that shader.
		 *
		 * @param shader The shader to resolve (may be an image-space shader).
		 * @param[out] descriptor Resolved descriptor for image-space shaders.
		 * @return True if descriptor is valid or not applicable, false on failure.
		 */
		static bool ResolveImageSpaceDescriptor(const RE::BSShader& shader, uint32_t& descriptor);
		/**
		@brief Get the BSShader::Type from the ShaderString
		@param a_key The key generated from GetShaderString
		@return A string with a valid BSShader::Type
		*/
		static std::string GetTypeFromShaderString(const std::string&);
		constexpr const char* VertexShaderProfile = "vs_5_0";
		constexpr const char* PixelShaderProfile = "ps_5_0";
		constexpr const char* ComputeShaderProfile = "cs_5_0";

		static std::wstring GetShaderPath(const std::string_view& name)
		{
			return std::format(L"Data/Shaders/{}.hlsl", std::wstring(name.begin(), name.end()));
		}

		static const char* GetShaderProfile(ShaderClass shaderClass)
		{
			switch (shaderClass) {
			case ShaderClass::Vertex:
				return VertexShaderProfile;
			case ShaderClass::Pixel:
				return PixelShaderProfile;
			case ShaderClass::Compute:
				return ComputeShaderProfile;
			}
			return nullptr;
		}

		uint32_t GetTechnique(uint32_t descriptor)
		{
			return 0x3F & (descriptor >> 24);
		}

		static void GetLightingShaderDefines(uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			static REL::Relocation<void(uint32_t, D3D_SHADER_MACRO*)> VanillaGetLightingShaderDefines(RELOCATION_ID(101631, 108698));
			VanillaGetLightingShaderDefines(descriptor, defines.data());

			size_t lastIndex = std::ranges::find_if(defines, [](const D3D_SHADER_MACRO& macro) { return macro.Name == nullptr; }) - defines.begin();

			if (descriptor & static_cast<uint32_t>(ShaderCache::LightingShaderFlags::Deferred)) {
				defines[lastIndex++] = { "DEFERRED", nullptr };
			}
			if ((descriptor & static_cast<uint32_t>(ShaderCache::LightingShaderFlags::TruePbr)) != 0) {
				defines[lastIndex++] = { "TRUE_PBR", nullptr };
				if ((descriptor & static_cast<uint32_t>(ShaderCache::LightingShaderFlags::AnisoLighting)) != 0) {
					defines[lastIndex++] = { "GLINT", nullptr };
				}
			}

			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded && feature->HasShaderDefine(RE::BSShader::Type::Lighting)) {
					defines[lastIndex++] = { feature->GetShaderDefineName().data(), nullptr };
				}
			}

			defines[lastIndex] = { nullptr, nullptr };
		}

		static void GetBloodSplaterShaderDefines(uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			size_t lastIndex = 0;
			if (descriptor == static_cast<uint32_t>(ShaderCache::BloodSplatterShaderTechniques::Splatter)) {
				defines[lastIndex++] = { "SPLATTER", nullptr };
			} else if (descriptor == static_cast<uint32_t>(ShaderCache::BloodSplatterShaderTechniques::Flare)) {
				defines[lastIndex++] = { "FLARE", nullptr };
			}

			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded && feature->HasShaderDefine(RE::BSShader::Type::BloodSplatter)) {
					defines[lastIndex++] = { feature->GetShaderDefineName().data(), nullptr };
				}
			}

			defines[lastIndex] = { nullptr, nullptr };
		}

		static void GetDistantTreeShaderDefines(uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			const auto technique = descriptor & 1;
			size_t lastIndex = 0;
			if (technique == static_cast<uint32_t>(ShaderCache::DistantTreeShaderTechniques::Depth)) {
				defines[lastIndex++] = { "RENDER_DEPTH", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::DistantTreeShaderFlags::AlphaTest)) {
				defines[lastIndex++] = { "DO_ALPHA_TEST", nullptr };
			}

			if (descriptor & static_cast<uint32_t>(ShaderCache::DistantTreeShaderFlags::Deferred)) {
				defines[lastIndex++] = { "DEFERRED", nullptr };
			}

			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded && feature->HasShaderDefine(RE::BSShader::Type::DistantTree)) {
					defines[lastIndex++] = { feature->GetShaderDefineName().data(), nullptr };
				}
			}

			defines[lastIndex] = { nullptr, nullptr };
		}

		static void GetSkyShaderDefines(uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			using enum ShaderCache::SkyShaderTechniques;

			const auto technique = static_cast<ShaderCache::SkyShaderTechniques>(descriptor & 255);
			size_t lastIndex = 0;
			switch (technique) {
			case SunOcclude:
				{
					defines[lastIndex++] = { "OCCLUSION", nullptr };
					break;
				}
			case SunGlare:
				{
					defines[lastIndex++] = { "TEX", nullptr };
					defines[lastIndex++] = { "DITHER", nullptr };
					break;
				}
			case MoonAndStarsMask:
				{
					defines[lastIndex++] = { "TEX", nullptr };
					defines[lastIndex++] = { "MOONMASK", nullptr };
					break;
				}
			case Stars:
				{
					defines[lastIndex++] = { "HORIZFADE", nullptr };
					break;
				}
			case Clouds:
				{
					defines[lastIndex++] = { "TEX", nullptr };
					defines[lastIndex++] = { "CLOUDS", nullptr };
					break;
				}
			case CloudsLerp:
				{
					defines[lastIndex++] = { "TEX", nullptr };
					defines[lastIndex++] = { "CLOUDS", nullptr };
					defines[lastIndex++] = { "TEXLERP", nullptr };
					break;
				}
			case CloudsFade:
				{
					defines[lastIndex++] = { "TEX", nullptr };
					defines[lastIndex++] = { "CLOUDS", nullptr };
					defines[lastIndex++] = { "TEXFADE", nullptr };
					break;
				}
			case Texture:
				{
					defines[lastIndex++] = { "TEX", nullptr };
					break;
				}
			case Sky:
				{
					defines[lastIndex++] = { "DITHER", nullptr };
					break;
				}
			}

			uint32_t flags = descriptor >> 8;

			if (flags) {
				defines[lastIndex++] = { "DEFERRED", nullptr };
			}

			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded && feature->HasShaderDefine(RE::BSShader::Type::Sky)) {
					defines[lastIndex++] = { feature->GetShaderDefineName().data(), nullptr };
				}
			}

			defines[lastIndex] = { nullptr, nullptr };
		}

		static void GetGrassShaderDefines(uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			const auto technique = descriptor & 0b1111;
			size_t lastIndex = 0;
			if (technique == static_cast<uint32_t>(ShaderCache::GrassShaderTechniques::RenderDepth)) {
				defines[lastIndex++] = { "RENDER_DEPTH", nullptr };
			} else if (technique == static_cast<uint32_t>(ShaderCache::GrassShaderTechniques::TruePbr)) {
				defines[lastIndex++] = { "TRUE_PBR", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::GrassShaderFlags::AlphaTest)) {
				defines[lastIndex++] = { "DO_ALPHA_TEST", nullptr };
			}

			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded && feature->HasShaderDefine(RE::BSShader::Type::Grass)) {
					defines[lastIndex++] = { feature->GetShaderDefineName().data(), nullptr };
				}
			}

			defines[lastIndex] = { nullptr, nullptr };
		}

		static void GetParticleShaderDefines(uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			using enum ShaderCache::ParticleShaderTechniques;

			const auto technique = static_cast<ShaderCache::ParticleShaderTechniques>(descriptor);
			size_t lastIndex = 0;
			switch (technique) {
			case ParticlesGryColor:
				{
					defines[lastIndex++] = { "GRAYSCALE_TO_COLOR", nullptr };
					break;
				}
			case ParticlesGryAlpha:
				{
					defines[lastIndex++] = { "GRAYSCALE_TO_ALPHA", nullptr };
					break;
				}
			case ParticlesGryColorAlpha:
				{
					defines[lastIndex++] = { "GRAYSCALE_TO_COLOR", nullptr };
					defines[lastIndex++] = { "GRAYSCALE_TO_ALPHA", nullptr };
					break;
				}
			case EnvCubeSnow:
				{
					defines[lastIndex++] = { "ENVCUBE", nullptr };
					defines[lastIndex++] = { "SNOW", nullptr };
					break;
				}
			case EnvCubeRain:
				{
					defines[lastIndex++] = { "ENVCUBE", nullptr };
					defines[lastIndex++] = { "RAIN", nullptr };
					break;
				}
			}

			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded && feature->HasShaderDefine(RE::BSShader::Type::Particle)) {
					defines[lastIndex++] = { feature->GetShaderDefineName().data(), nullptr };
				}
			}

			defines[lastIndex] = { nullptr, nullptr };
		}

		static void GetEffectShaderDefines(uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			size_t lastIndex = 0;

			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Vc)) {
				defines[lastIndex++] = { "VC", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::TexCoord)) {
				defines[lastIndex++] = { "TEXCOORD", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::TexCoordIndex)) {
				defines[lastIndex++] = { "TEXCOORD_INDEX", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Skinned)) {
				defines[lastIndex++] = { "SKINNED", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Normals)) {
				defines[lastIndex++] = { "NORMALS", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::BinormalTangent)) {
				defines[lastIndex++] = { "BINORMAL_TANGENT", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Texture)) {
				defines[lastIndex++] = { "TEXTURE", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::IndexedTexture)) {
				defines[lastIndex++] = { "INDEXED_TEXTURE", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Falloff)) {
				defines[lastIndex++] = { "FALLOFF", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::AddBlend)) {
				defines[lastIndex++] = { "ADDBLEND", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::MultBlend)) {
				defines[lastIndex++] = { "MULTBLEND", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Particles)) {
				defines[lastIndex++] = { "PARTICLES", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::StripParticles)) {
				defines[lastIndex++] = { "STRIP_PARTICLES", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Blood)) {
				defines[lastIndex++] = { "BLOOD", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Membrane)) {
				defines[lastIndex++] = { "MEMBRANE", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Lighting)) {
				defines[lastIndex++] = { "LIGHTING", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::ProjectedUv)) {
				defines[lastIndex++] = { "PROJECTED_UV", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Soft)) {
				defines[lastIndex++] = { "SOFT", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::GrayscaleToColor)) {
				defines[lastIndex++] = { "GRAYSCALE_TO_COLOR", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::GrayscaleToAlpha)) {
				defines[lastIndex++] = { "GRAYSCALE_TO_ALPHA", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::IgnoreTexAlpha)) {
				defines[lastIndex++] = { "IGNORE_TEX_ALPHA", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::MultBlendDecal)) {
				defines[lastIndex++] = { "MULTBLEND_DECAL", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::AlphaTest)) {
				defines[lastIndex++] = { "ALPHA_TEST", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::SkyObject)) {
				defines[lastIndex++] = { "SKY_OBJECT", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::MsnSpuSkinned)) {
				defines[lastIndex++] = { "MSN_SPU_SKINNED", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::MotionVectorsNormals)) {
				defines[lastIndex++] = { "MOTIONVECTORS_NORMALS", nullptr };
			}

			if (descriptor & static_cast<uint32_t>(ShaderCache::EffectShaderFlags::Deferred)) {
				defines[lastIndex++] = { "DEFERRED", nullptr };
			}

			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded && feature->HasShaderDefine(RE::BSShader::Type::Effect)) {
					defines[lastIndex++] = { feature->GetShaderDefineName().data(), nullptr };
				}
			}

			defines[lastIndex] = { nullptr, nullptr };
		}

		static void GetWaterShaderDefines(uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			size_t lastIndex = 0;
			defines[lastIndex++] = { "WATER", nullptr };
			defines[lastIndex++] = { "FOG", nullptr };

			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::Vc)) {
				defines[lastIndex++] = { "VC", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::NormalTexCoord)) {
				defines[lastIndex++] = { "NORMAL_TEXCOORD", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::Reflections)) {
				defines[lastIndex++] = { "REFLECTIONS", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::Refractions)) {
				defines[lastIndex++] = { "REFRACTIONS", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::Depth)) {
				defines[lastIndex++] = { "DEPTH", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::Interior)) {
				defines[lastIndex++] = { "INTERIOR", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::Wading)) {
				defines[lastIndex++] = { "WADING", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::VertexAlphaDepth)) {
				defines[lastIndex++] = { "VERTEX_ALPHA_DEPTH", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::Cubemap)) {
				defines[lastIndex++] = { "CUBEMAP", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::Flowmap)) {
				defines[lastIndex++] = { "FLOWMAP", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(ShaderCache::WaterShaderFlags::BlendNormals)) {
				defines[lastIndex++] = { "BLEND_NORMALS", nullptr };
			}

			const auto technique = (descriptor >> 11) & 0xF;
			if (technique == static_cast<uint32_t>(ShaderCache::WaterShaderTechniques::Underwater)) {
				defines[lastIndex++] = { "UNDERWATER", nullptr };
			} else if (technique == static_cast<uint32_t>(ShaderCache::WaterShaderTechniques::Lod)) {
				defines[lastIndex++] = { "LOD", nullptr };
			} else if (technique == static_cast<uint32_t>(ShaderCache::WaterShaderTechniques::Stencil)) {
				defines[lastIndex++] = { "STENCIL", nullptr };
			} else if (technique == static_cast<uint32_t>(ShaderCache::WaterShaderTechniques::Simple)) {
				defines[lastIndex++] = { "SIMPLE", nullptr };
			} else if (technique < 8) {
				static constexpr std::array<const char*, 8> numLightDefines = { { "0", "1", "2", "3", "4",
					"5", "6", "7" } };
				defines[lastIndex++] = { "SPECULAR", nullptr };
				defines[lastIndex++] = { "NUM_SPECULAR_LIGHTS", numLightDefines[technique] };
			}

			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded && feature->HasShaderDefine(RE::BSShader::Type::Water)) {
					defines[lastIndex++] = { feature->GetShaderDefineName().data(), nullptr };
				}
			}

			defines[lastIndex] = { nullptr, nullptr };
		}

		static void GetUtilityShaderDefines(uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			using enum ShaderCache::UtilityShaderFlags;

			size_t lastIndex = 0;

			if (descriptor & static_cast<uint32_t>(Vc)) {
				defines[lastIndex++] = { "VC", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(Texture)) {
				defines[lastIndex++] = { "TEXTURE", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(Skinned)) {
				defines[lastIndex++] = { "SKINNED", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(Normals)) {
				defines[lastIndex++] = { "NORMALS", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(AlphaTest)) {
				defines[lastIndex++] = { "ALPHA_TEST", nullptr };
			}

			if (descriptor & static_cast<uint32_t>(LodLandscape)) {
				if (descriptor &
					(static_cast<uint32_t>(RenderShadowmask) |
						static_cast<uint32_t>(RenderShadowmaskSpot))) {
					defines[lastIndex++] = { "FOCUS_SHADOW", nullptr };
				} else {
					defines[lastIndex++] = { "LOD_LANDSCAPE", nullptr };
				}
			}

			if ((descriptor & static_cast<uint32_t>(RenderNormal)) &&
				!(descriptor & static_cast<uint32_t>(RenderNormalClear))) {
				defines[lastIndex++] = { "RENDER_NORMAL", nullptr };

			} else if (!(descriptor & static_cast<uint32_t>(RenderNormal)) &&
					   (descriptor & static_cast<uint32_t>(RenderNormalClear))) {
				defines[lastIndex++] = { "RENDER_NORMAL_CLEAR", nullptr };

			} else if ((descriptor & static_cast<uint32_t>(RenderNormal)) &&
					   (descriptor & static_cast<uint32_t>(RenderNormalClear))) {
				defines[lastIndex++] = { "STENCIL_ABOVE_WATER", nullptr };
			}

			if (descriptor & static_cast<uint32_t>(RenderNormalFalloff)) {
				defines[lastIndex++] = { "RENDER_NORMAL_FALLOFF", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(RenderNormalClamp)) {
				defines[lastIndex++] = { "RENDER_NORMAL_CLAMP", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(RenderDepth)) {
				defines[lastIndex++] = { "RENDER_DEPTH", nullptr };
			}

			if (descriptor & static_cast<uint32_t>(OpaqueEffect)) {
				defines[lastIndex++] = { "OPAQUE_EFFECT", nullptr };

				if (!(descriptor & static_cast<uint32_t>(RenderShadowmap)) &&
					(descriptor & static_cast<uint32_t>(AdditionalAlphaMask))) {
					defines[lastIndex++] = { "ADDITIONAL_ALPHA_MASK", nullptr };
				}
				if (descriptor & static_cast<uint32_t>(GrayscaleToAlpha)) {
					defines[lastIndex++] = { "GRAYSCALE_TO_ALPHA", nullptr };
				}
			} else {
				if (descriptor & static_cast<uint32_t>(RenderShadowmap)) {
					defines[lastIndex++] = { "RENDER_SHADOWMAP", nullptr };

					if (descriptor & static_cast<uint32_t>(RenderShadowmapPb)) {
						defines[lastIndex++] = { "RENDER_SHADOWMAP_PB", nullptr };
					}
				} else if (descriptor &
						   static_cast<uint32_t>(AdditionalAlphaMask)) {
					defines[lastIndex++] = { "ADDITIONAL_ALPHA_MASK", nullptr };
				}
				if (descriptor & static_cast<uint32_t>(RenderShadowmapClamped)) {
					defines[lastIndex++] = { "RENDER_SHADOWMAP_CLAMPED", nullptr };
				}
			}

			if (descriptor & static_cast<uint32_t>(GrayscaleMask)) {
				defines[lastIndex++] = { "GRAYSCALE_MASK", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(RenderShadowmask)) {
				defines[lastIndex++] = { "RENDER_SHADOWMASK", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(RenderShadowmaskSpot)) {
				defines[lastIndex++] = { "RENDER_SHADOWMASKSPOT", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(RenderShadowmaskPb)) {
				defines[lastIndex++] = { "RENDER_SHADOWMASKPB", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(RenderShadowmaskDpb)) {
				defines[lastIndex++] = { "RENDER_SHADOWMASKDPB", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(RenderBaseTexture)) {
				defines[lastIndex++] = { "RENDER_BASE_TEXTURE", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(TreeAnim)) {
				defines[lastIndex++] = { "TREE_ANIM", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(LodObject)) {
				defines[lastIndex++] = { "LOD_OBJECT", nullptr };
			}
			if (descriptor & static_cast<uint32_t>(LocalMapFogOfWar)) {
				defines[lastIndex++] = { "LOCALMAP_FOGOFWAR", nullptr };
			}

			if (descriptor & (static_cast<uint32_t>(RenderShadowmask) |
								 static_cast<uint32_t>(RenderShadowmaskDpb) |
								 static_cast<uint32_t>(RenderShadowmaskPb) |
								 static_cast<uint32_t>(RenderShadowmaskSpot))) {
				static constexpr std::array<const char*, 5> shadowFilters = { { "0", "1", "2",
					"3", "4" } };
				const size_t shadowFilterIndex = std::clamp((descriptor >> 17) & 0b111, 0u, 4u);
				defines[lastIndex++] = { "SHADOWFILTER", shadowFilters[shadowFilterIndex] };
			} else if ((!(descriptor & static_cast<uint32_t>(OpaqueEffect)) &&
						   (descriptor &
							   static_cast<uint32_t>(RenderShadowmap))) ||
					   (descriptor & static_cast<uint32_t>(RenderDepth))) {
				if (descriptor & static_cast<uint32_t>(DepthWriteDecals)) {
					defines[lastIndex++] = { "DEPTH_WRITE_DECALS", nullptr };
				}
			} else {
				if (descriptor & (static_cast<uint32_t>(DepthWriteDecals) |
									 static_cast<uint32_t>(DebugColor))) {
					defines[lastIndex++] = { "DEBUG_COLOR", nullptr };
				}
				if (descriptor & static_cast<uint32_t>(DebugShadowSplit)) {
					defines[lastIndex++] = { "DEBUG_SHADOWSPLIT", nullptr };
				}
			}

			defines[lastIndex++] = { "SHADOWSPLITCOUNT", "3" };

			if ((descriptor & 0x14000) != 0x14000 &&
				((descriptor & 0x20004000) == 0x4000 || (descriptor & 0x1E02000) == 0x2000) &&
				!(descriptor & 0x80) && (descriptor & 0x14000) != 0x10000) {
				defines[lastIndex++] = { "NO_PIXEL_SHADER", nullptr };
			}

			defines[lastIndex++] = { nullptr, nullptr };
		}

		static void GetImagespaceShaderDefines(const RE::BSShader& shader, std::span<D3D_SHADER_MACRO> defines)
		{
			auto& isShader = const_cast<RE::BSImagespaceShader&>(static_cast<const RE::BSImagespaceShader&>(shader));
			auto* macros = reinterpret_cast<RE::BSImagespaceShader::ShaderMacro*>(defines.data());
			isShader.GetShaderMacros(macros);
			size_t lastIndex = std::ranges::find_if(defines, [](const D3D_SHADER_MACRO& macro) { return macro.Name == nullptr; }) - defines.begin();
			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded && feature->HasShaderDefine(RE::BSShader::Type::ImageSpace)) {
					defines[lastIndex++] = { feature->GetShaderDefineName().data(), nullptr };
					auto options = feature->GetShaderDefineOptions();
					if (!options.empty()) {
						for (auto& option : options) {
							const char* definition = option.second.empty() ? nullptr : option.second.data();
							defines[lastIndex++] = { option.first.data(), definition };
						}
					}
				}
			}
			defines[lastIndex] = { nullptr, nullptr };
			return;
		}

		static void GetShaderDefines(const RE::BSShader& shader, uint32_t descriptor, std::span<D3D_SHADER_MACRO> defines)
		{
			switch (shader.shaderType.get()) {
			case RE::BSShader::Type::Grass:
				GetGrassShaderDefines(descriptor, defines);
				break;
			case RE::BSShader::Type::Sky:
				GetSkyShaderDefines(descriptor, defines);
				break;
			case RE::BSShader::Type::Water:
				GetWaterShaderDefines(descriptor, defines);
				break;
			case RE::BSShader::Type::BloodSplatter:
				GetBloodSplaterShaderDefines(descriptor, defines);
				break;
			case RE::BSShader::Type::ImageSpace:
				GetImagespaceShaderDefines(shader, defines);
				break;
			case RE::BSShader::Type::Lighting:
				GetLightingShaderDefines(descriptor, defines);
				break;
			case RE::BSShader::Type::DistantTree:
				GetDistantTreeShaderDefines(descriptor, defines);
				break;
			case RE::BSShader::Type::Particle:
				GetParticleShaderDefines(descriptor, defines);
				break;
			case RE::BSShader::Type::Effect:
				GetEffectShaderDefines(descriptor, defines);
				break;
			case RE::BSShader::Type::Utility:
				GetUtilityShaderDefines(descriptor, defines);
				break;
			}
		}

		static std::array<std::array<std::unordered_map<std::string, int32_t>,
							  static_cast<size_t>(ShaderClass::Total)>,
			static_cast<size_t>(RE::BSShader::Type::Total)>
		GetVariableIndices()
		{
			std::array<std::array<std::unordered_map<std::string, int32_t>,
						   static_cast<size_t>(ShaderClass::Total)>,
				static_cast<size_t>(RE::BSShader::Type::Total)>
				result;

			auto& lightingVS =
				result[static_cast<size_t>(RE::BSShader::Type::Lighting)][static_cast<size_t>(ShaderClass::Vertex)];
			lightingVS = {
				{ "World", 0 },
				{ "PreviousWorld", 1 },
				{ "EyePosition", 2 },
				{ "LandBlendParams", 3 },
				{ "TreeParams", 4 },
				{ "WindTimers", 5 },
				{ "TextureProj", 6 },
				{ "IndexScale", 7 },
				{ "WorldMapOverlayParameters", 8 },
				{ "LeftEyeCenter", 9 },
				{ "RightEyeCenter", 10 },
				{ "TexcoordOffset", 11 },
				{ "HighDetailRange", 12 },
				{ "FogParam", 13 },
				{ "FogNearColor", 14 },
				{ "FogFarColor", 15 },
				{ "Bones", 16 },
			};

			const auto& lightingPSConstants = ShaderConstants::LightingPS::Get();

			auto& lightingPS = result[static_cast<size_t>(RE::BSShader::Type::Lighting)]
									 [static_cast<size_t>(ShaderClass::Pixel)];

			lightingPS = {
				{ "NumLightNumShadowLight", lightingPSConstants.NumLightNumShadowLight },
				{ "PointLightPosition", lightingPSConstants.PointLightPosition },
				{ "PointLightColor", lightingPSConstants.PointLightColor },
				{ "DirLightDirection", lightingPSConstants.DirLightDirection },
				{ "DirLightColor", lightingPSConstants.DirLightColor },
				{ "DirectionalAmbient", lightingPSConstants.DirectionalAmbient },
				{ "AmbientSpecularTintAndFresnelPower", lightingPSConstants.AmbientSpecularTintAndFresnelPower },
				{ "MaterialData", lightingPSConstants.MaterialData },
				{ "EmitColor", lightingPSConstants.EmitColor },
				{ "AlphaTestRef", lightingPSConstants.AlphaTestRef },
				{ "ShadowLightMaskSelect", lightingPSConstants.ShadowLightMaskSelect },
				{ "VPOSOffset", lightingPSConstants.VPOSOffset },
				{ "ProjectedUVParams", lightingPSConstants.ProjectedUVParams },
				{ "ProjectedUVParams2", lightingPSConstants.ProjectedUVParams2 },
				{ "ProjectedUVParams3", lightingPSConstants.ProjectedUVParams3 },
				{ "SplitDistance", lightingPSConstants.SplitDistance },
				{ "SSRParams", lightingPSConstants.SSRParams },
				{ "WorldMapOverlayParametersPS", lightingPSConstants.WorldMapOverlayParametersPS },
				{ "ShadowSampleParam", lightingPSConstants.ShadowSampleParam },      // VR only
				{ "EndSplitDistances", lightingPSConstants.EndSplitDistances },      // VR only
				{ "StartSplitDistances", lightingPSConstants.StartSplitDistances },  // VR only
				{ "DephBiasParam", lightingPSConstants.DephBiasParam },              // VR only
				{ "ShadowLightParam", lightingPSConstants.ShadowLightParam },        // VR only
				{ "ShadowMapProj", lightingPSConstants.ShadowMapProj },              // VR only
				{ "AmbientColor", lightingPSConstants.AmbientColor },
				{ "FogColor", lightingPSConstants.FogColor },
				{ "ColourOutputClamp", lightingPSConstants.ColourOutputClamp },
				{ "EnvmapData", lightingPSConstants.EnvmapData },
				{ "ParallaxOccData", lightingPSConstants.ParallaxOccData },
				{ "TintColor", lightingPSConstants.TintColor },
				{ "LODTexParams", lightingPSConstants.LODTexParams },
				{ "SpecularColor", lightingPSConstants.SpecularColor },
				{ "SparkleParams", lightingPSConstants.SparkleParams },
				{ "MultiLayerParallaxData", lightingPSConstants.MultiLayerParallaxData },
				{ "LightingEffectParams", lightingPSConstants.LightingEffectParams },
				{ "IBLParams", lightingPSConstants.IBLParams },
				{ "LandscapeTexture1to4IsSnow", lightingPSConstants.LandscapeTexture1to4IsSnow },
				{ "LandscapeTexture5to6IsSnow", lightingPSConstants.LandscapeTexture5to6IsSnow },
				{ "LandscapeTexture1to4IsSpecPower", lightingPSConstants.LandscapeTexture1to4IsSpecPower },
				{ "LandscapeTexture5to6IsSpecPower", lightingPSConstants.LandscapeTexture5to6IsSpecPower },
				{ "SnowRimLightParameters", lightingPSConstants.SnowRimLightParameters },
				{ "CharacterLightParams", lightingPSConstants.CharacterLightParams },
				{ "InvWorldMat", lightingPSConstants.InvWorldMat },            // VR only
				{ "PreviousWorldMat", lightingPSConstants.PreviousWorldMat },  // VR only

				{ "PBRFlags", lightingPSConstants.PBRFlags },
				{ "PBRParams1", lightingPSConstants.PBRParams1 },
				{ "LandscapeTexture2PBRParams", lightingPSConstants.LandscapeTexture2PBRParams },
				{ "LandscapeTexture3PBRParams", lightingPSConstants.LandscapeTexture3PBRParams },
				{ "LandscapeTexture4PBRParams", lightingPSConstants.LandscapeTexture4PBRParams },
				{ "LandscapeTexture5PBRParams", lightingPSConstants.LandscapeTexture5PBRParams },
				{ "LandscapeTexture6PBRParams", lightingPSConstants.LandscapeTexture6PBRParams },
				{ "PBRParams2", lightingPSConstants.PBRParams2 },
				{ "LandscapeTexture1GlintParameters", lightingPSConstants.LandscapeTexture1GlintParameters },
				{ "LandscapeTexture2GlintParameters", lightingPSConstants.LandscapeTexture2GlintParameters },
				{ "LandscapeTexture3GlintParameters", lightingPSConstants.LandscapeTexture3GlintParameters },
				{ "LandscapeTexture4GlintParameters", lightingPSConstants.LandscapeTexture4GlintParameters },
				{ "LandscapeTexture5GlintParameters", lightingPSConstants.LandscapeTexture5GlintParameters },
				{ "LandscapeTexture6GlintParameters", lightingPSConstants.LandscapeTexture6GlintParameters },
				{ "MaterialObjectRGBScale", lightingPSConstants.MaterialObjectRGBScale },
			};

			auto& bloodSplatterVS = result[static_cast<size_t>(RE::BSShader::Type::BloodSplatter)]
										  [static_cast<size_t>(ShaderClass::Vertex)];
			bloodSplatterVS = {
				{ "WorldViewProj", 0 },
				{ "LightLoc", 1 },
				{ "Ctrl", 2 },
			};

			auto& bloodSplatterPS = result[static_cast<size_t>(RE::BSShader::Type::BloodSplatter)]
										  [static_cast<size_t>(ShaderClass::Pixel)];
			bloodSplatterPS = {
				{ "Alpha", 0 },
			};

			auto& distantTreeVS = result[static_cast<size_t>(RE::BSShader::Type::DistantTree)]
										[static_cast<size_t>(ShaderClass::Vertex)];

			distantTreeVS = {
				{ "InstanceData", 0 },
				{ "WorldViewProj", 1 },
				{ "World", 2 },
				{ "PreviousWorld", 3 },
				{ "FogParam", 4 },
				{ "FogNearColor", 5 },
				{ "FogFarColor", 6 },
				{ "DiffuseDir", 7 },
				{ "IndexScale", 8 },
			};

			auto& distantTreePS = result[static_cast<size_t>(RE::BSShader::Type::DistantTree)]
										[static_cast<size_t>(ShaderClass::Pixel)];
			distantTreePS = {
				{ "DiffuseColor", 0 },
				{ "AmbientColor", 1 },
			};

			auto& skyVS = result[static_cast<size_t>(RE::BSShader::Type::Sky)]
								[static_cast<size_t>(ShaderClass::Vertex)];
			skyVS = {
				{ "WorldViewProj", 0 },
				{ "World", 1 },
				{ "PreviousWorld", 2 },
				{ "BlendColor", 3 },
				{ "EyePosition", 4 },
				{ "TexCoordOff", 5 },
				{ "VParams", 6 },
			};

			auto& skyPS = result[static_cast<size_t>(RE::BSShader::Type::Sky)]
								[static_cast<size_t>(ShaderClass::Pixel)];
			skyPS = {
				{ "PParams", 0 },
			};

			auto& grassVS = result[static_cast<size_t>(RE::BSShader::Type::Grass)]
								  [static_cast<size_t>(ShaderClass::Vertex)];
			grassVS = {
				{ "WorldViewProj", 0 },
				{ "WorldView", 1 },
				{ "World", 2 },
				{ "PreviousWorld", 3 },
				{ "FogNearColor", 4 },
				{ "WindVector", 5 },
				{ "WindTimer", 6 },
				{ "DirLightDirection", 7 },
				{ "PreviousWindTimer", 8 },
				{ "DirLightColor", 9 },
				{ "AlphaParam1", 10 },
				{ "AmbientColor", 11 },
				{ "AlphaParam2", 12 },
				{ "ScaleMask", 13 },
			};

			if (REL::Module::IsVR()) {
				grassVS.insert({ "Padding", 14 });
			} else {
				grassVS.insert({ "ShadowClampValue", 14 });
			}

			const auto& grassPSConstants = ShaderConstants::GrassPS::Get();

			auto& grassPS = result[static_cast<size_t>(RE::BSShader::Type::Grass)]
								  [static_cast<size_t>(ShaderClass::Pixel)];
			grassPS = {
				{ "PBRFlags", grassPSConstants.PBRFlags },
				{ "PBRParams1", grassPSConstants.PBRParams1 },
				{ "PBRParams2", grassPSConstants.PBRParams2 },
			};

			auto& particleVS = result[static_cast<size_t>(RE::BSShader::Type::Particle)]
									 [static_cast<size_t>(ShaderClass::Vertex)];
			particleVS = {
				{ "WorldViewProj", 0 },
				{ "PrevWorldViewProj", 1 },
				{ "PrecipitationOcclusionWorldViewProj", 2 },
				{ "fVars0", 3 },
				{ "fVars1", 4 },
				{ "fVars2", 5 },
				{ "fVars3", 6 },
				{ "fVars4", 7 },
				{ "Color1", 8 },
				{ "Color2", 9 },
				{ "Color3", 10 },
				{ "Velocity", 11 },
				{ "Acceleration", 12 },
				{ "ScaleAdjust", 13 },
				{ "Wind", 14 },
			};

			auto& particlePS = result[static_cast<size_t>(RE::BSShader::Type::Particle)]
									 [static_cast<size_t>(ShaderClass::Pixel)];
			particlePS = {
				{ "ColorScale", 0 },
				{ "TextureSize", 1 },
			};

			auto& effectVS = result[static_cast<size_t>(RE::BSShader::Type::Effect)]
								   [static_cast<size_t>(ShaderClass::Vertex)];
			effectVS = {
				{ "World", 0 },
				{ "PreviousWorld", 1 },
				{ "Bones", 2 },
				{ "EyePosition", 3 },
				{ "FogParam", 4 },
				{ "FogNearColor", 5 },
				{ "FogFarColor", 6 },
				{ "FalloffData", 7 },
				{ "SoftMateralVSParams", 8 },
				{ "TexcoordOffset", 9 },
				{ "TexcoordOffsetMembrane", 10 },
				{ "SubTexOffset", 11 },
				{ "PosAdjust", 12 },
				{ "MatProj", 13 },
			};

			const auto& effectPSConstants = ShaderConstants::EffectPS::Get();

			auto& effectPS = result[static_cast<size_t>(RE::BSShader::Type::Effect)]
								   [static_cast<size_t>(ShaderClass::Pixel)];
			effectPS = {
				{ "PropertyColor", effectPSConstants.PropertyColor },
				{ "AlphaTestRef", effectPSConstants.AlphaTestRef },
				{ "MembraneRimColor", effectPSConstants.MembraneRimColor },
				{ "MembraneVars", effectPSConstants.MembraneVars },
				{ "PLightPositionX", effectPSConstants.PLightPositionX },
				{ "PLightPositionY", effectPSConstants.PLightPositionY },
				{ "PLightPositionZ", effectPSConstants.PLightPositionZ },
				{ "PLightingRadiusInverseSquared", effectPSConstants.PLightingRadiusInverseSquared },
				{ "PLightColorR", effectPSConstants.PLightColorR },
				{ "PLightColorG", effectPSConstants.PLightColorG },
				{ "PLightColorB", effectPSConstants.PLightColorB },
				{ "DLightColor", effectPSConstants.DLightColor },
				{ "VPOSOffset", effectPSConstants.VPOSOffset },
				{ "CameraDataEffect", effectPSConstants.CameraData },
				{ "FilteringParam", effectPSConstants.FilteringParam },
				{ "BaseColor", effectPSConstants.BaseColor },
				{ "BaseColorScale", effectPSConstants.BaseColorScale },
				{ "LightingInfluence", effectPSConstants.LightingInfluence },

				{ "ExtendedFlags", effectPSConstants.ExtendedFlags },
			};

			auto& waterVS = result[static_cast<size_t>(RE::BSShader::Type::Water)]
								  [static_cast<size_t>(ShaderClass::Vertex)];
			waterVS = {
				{ "WorldViewProj", 0 },
				{ "World", 1 },
				{ "PreviousWorld", 2 },
				{ "QPosAdjust", 3 },
				{ "ObjectUV", 4 },
				{ "NormalsScroll0", 5 },
				{ "NormalsScroll1", 6 },
				{ "NormalsScale", 7 },
				{ "VSFogParam", 8 },
				{ "VSFogNearColor", 9 },
				{ "VSFogFarColor", 10 },
				{ "CellTexCoordOffset", 11 },
			};

			if (!REL::Module::IsVR()) {
				waterVS.insert(
					{
						{ "SubTexOffset", 12 },
						{ "PosAdjust", 13 },
						{ "MatProj", 14 },
					});
			}

			auto& waterPS = result[static_cast<size_t>(RE::BSShader::Type::Water)]
								  [static_cast<size_t>(ShaderClass::Pixel)];
			waterPS = {
				{ "TextureProj", 0 },
				{ "ShallowColor", 1 },
				{ "DeepColor", 2 },
				{ "ReflectionColor", 3 },
				{ "FresnelRI", 4 },
				{ "BlendRadius", 5 },
				{ "PosAdjust", 6 },
				{ "ReflectPlane", 7 },
				{ "CameraDataWater", 8 },
				{ "ProjData", 9 },
				{ "VarAmounts", 10 },
				{ "FogParam", 11 },
				{ "FogNearColor", 12 },
				{ "FogFarColor", 13 },
				{ "SunDir", 14 },
				{ "SunColor", 15 },
				{ "NumLights", 16 },
				{ "LightPos", 17 },
				{ "LightColor", 18 },
				{ "WaterParams", 19 },
				{ "DepthControl", 20 },
				{ "SSRParams", 21 },
				{ "SSRParams2", 22 },
				{ "NormalsAmplitude", 23 },
				{ "VPOSOffset", 24 },
			};

			auto& utilityVS = result[static_cast<size_t>(RE::BSShader::Type::Utility)]
									[static_cast<size_t>(ShaderClass::Vertex)];
			utilityVS = {
				{ "World", 0 },
				{ "TexcoordOffset", 1 },
				{ "EyePos", 2 },
				{ "HighDetailRange", 3 },
				{ "ParabolaParam", 4 },
				{ "ShadowFadeParam", 5 },
				{ "TreeParams", 6 },
				{ "WaterParams", 7 },
				{ "Bones", 8 },
			};

			auto& utilityPS = result[static_cast<size_t>(RE::BSShader::Type::Utility)]
									[static_cast<size_t>(ShaderClass::Pixel)];
			utilityPS = {
				{ "AlphaTestRef", 0 },
				{ "RefractionPower", 1 },
				{ "DebugColor", 2 },
				{ "BaseColor", 3 },
				{ "PropertyColor", 4 },
				{ "FocusShadowMapProj", 5 },
				{ "ShadowMapProj", 6 },
				{ "ShadowSampleParam", 7 },
				{ "ShadowLightParam", 8 },
			};

			if (!REL::Module::IsVR()) {
				utilityPS.insert(
					{
						{ "ShadowFadeParam", 9 },
						{ "VPOSOffset", 10 },
						{ "EndSplitDistances", 11 },
						{ "StartSplitDistances", 12 },
						{ "FocusShadowFadeParam", 13 },
					});
			} else {
				utilityPS.insert(
					{
						{ "StereoClipRects", 9 },  // VR only
						{ "ShadowFadeParam", 10 },
						{ "VPOSOffset", 11 },
						{ "EndSplitDistances", 12 },
						{ "StartSplitDistances", 13 },
						{ "FocusShadowFadeParam", 14 },
					});
			}

			return result;
		}

		static int32_t GetVariableIndex(ShaderClass shaderClass, const RE::BSShader& shader, const char* name)
		{
			if (shader.shaderType == RE::BSShader::Type::ImageSpace) {
				const auto& imagespaceShader = static_cast<const RE::BSImagespaceShader&>(shader);

				if (shaderClass == ShaderClass::Vertex) {
					for (size_t nameIndex = 0; nameIndex < imagespaceShader.vsConstantNames.size();
						++nameIndex) {
						if (std::string_view(imagespaceShader.vsConstantNames[static_cast<uint32_t>(nameIndex)].c_str()) ==
							name) {
							return static_cast<int32_t>(nameIndex);
						}
					}
				} else if (shaderClass == ShaderClass::Pixel || shaderClass == ShaderClass::Compute) {
					for (size_t nameIndex = 0; nameIndex < imagespaceShader.psConstantNames.size(); ++nameIndex) {
						if (std::string_view(imagespaceShader.psConstantNames[static_cast<uint32_t>(nameIndex)].c_str()) == name) {
							return static_cast<int32_t>(nameIndex);
						}
					}
				}
			} else {
				static auto variableNames = GetVariableIndices();

				const auto& names = variableNames[static_cast<size_t>(shader.shaderType.get())]
												 [static_cast<size_t>(shaderClass)];
				auto it = names.find(name);
				if (it != names.cend()) {
					return it->second;
				}
			}
			return -1;
		}

		static std::string MergeDefinesString(std::array<D3D_SHADER_MACRO, 64>& defines, bool a_sort = false)
		{
			std::string result;
			// D3D_SHADER_MACRO stores C-string pointers. Compare the pointed-to
			// names so cache keys remain deterministic across processes, and keep
			// the unused null entries at the end.
			if (a_sort)
				std::sort(std::begin(defines), std::end(defines), [](const D3D_SHADER_MACRO& a, const D3D_SHADER_MACRO& b) {
					if (a.Name == nullptr || b.Name == nullptr)
						return a.Name != nullptr;
					return std::strcmp(a.Name, b.Name) < 0;
				});
			for (const auto& def : defines) {
				if (def.Name != nullptr) {
					result += def.Name;
					if (def.Definition != nullptr && !std::string_view(def.Definition).empty()) {
						result += "=";
						result += def.Definition;
					}
					result += ' ';
				} else {
					break;
				}
			}
			return result;
		}

		static void AddAttribute(uint64_t& desc, RE::BSGraphics::Vertex::Attribute attribute)
		{
			desc |= ((1ull << (44 + attribute)) | (1ull << (54 + attribute)) |
					 (0b1111ull << (4 * attribute + 4)));
		}

		template <size_t MaxOffsetsSize>
		static void ReflectConstantBuffers(ID3D11ShaderReflection& reflector,
			std::array<size_t, 3>& bufferSizes,
			std::array<int8_t, MaxOffsetsSize>& constantOffsets,
			uint64_t& vertexDesc,
			ShaderClass shaderClass, uint32_t descriptor, const RE::BSShader& shader)
		{
			D3D11_SHADER_DESC desc;
			if (FAILED(reflector.GetDesc(&desc))) {
				logger::error("Failed to get shader descriptor for {} shader {}::{:X}",
					magic_enum::enum_name(shaderClass), magic_enum::enum_name(shader.shaderType.get()),
					descriptor);
				return;
			}

			if (shaderClass == ShaderClass::Vertex) {
				vertexDesc = 0b1111;
				bool hasTexcoord2 = false;
				bool hasTexcoord3 = false;
				for (uint32_t inputIndex = 0; inputIndex < desc.InputParameters; ++inputIndex) {
					D3D11_SIGNATURE_PARAMETER_DESC inputDesc;
					if (FAILED(reflector.GetInputParameterDesc(inputIndex, &inputDesc))) {
						logger::error(
							"Failed to get input parameter {} descriptor for {} shader {}::{:X}",
							inputIndex, magic_enum::enum_name(shaderClass),
							magic_enum::enum_name(shader.shaderType.get()),
							descriptor);
					} else {
						std::string_view semanticName = inputDesc.SemanticName;
						if (semanticName == "POSITION" && inputDesc.SemanticIndex == 0) {
							AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_POSITION);
						} else if (semanticName == "TEXCOORD" &&
								   inputDesc.SemanticIndex == 0) {
							AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_TEXCOORD0);
						} else if (semanticName == "TEXCOORD" && inputDesc.SemanticIndex == 1) {
							AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_TEXCOORD1);
						} else if (semanticName == "NORMAL" &&
								   inputDesc.SemanticIndex == 0) {
							AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_NORMAL);
						} else if (semanticName == "BINORMAL" && inputDesc.SemanticIndex == 0) {
							AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_BINORMAL);
						} else if (semanticName == "COLOR" &&
								   inputDesc.SemanticIndex == 0) {
							AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_COLOR);
						} else if (semanticName == "BLENDWEIGHT" && inputDesc.SemanticIndex == 0) {
							AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_SKINNING);
						} else if (semanticName == "TEXCOORD" && inputDesc.SemanticIndex >= 4 &&
								   inputDesc.SemanticIndex <= 7) {
							AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_INSTANCEDATA);
						} else if (semanticName == "TEXCOORD" &&
								   inputDesc.SemanticIndex == 2) {
							hasTexcoord2 = true;
						} else if (semanticName == "TEXCOORD" && inputDesc.SemanticIndex == 3) {
							hasTexcoord3 = true;
						}
					}
				}
				if (hasTexcoord2) {
					if (hasTexcoord3) {
						AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_LANDDATA);
					} else {
						AddAttribute(vertexDesc, RE::BSGraphics::Vertex::VA_EYEDATA);
					}
				}
			}

			if (desc.ConstantBuffers <= 0) {
				return;
			}

			auto mapBufferConsts =
				[&](const char* bufferName, size_t& bufferSize) {
					auto bufferReflector = reflector.GetConstantBufferByName(bufferName);
					if (bufferReflector == nullptr) {
						logger::trace("Buffer {} not found for {} shader {}::{:X}",
							bufferName, magic_enum::enum_name(shaderClass),
							magic_enum::enum_name(shader.shaderType.get()),
							descriptor);
						return;
					}

					D3D11_SHADER_BUFFER_DESC bufferDesc;
					if (FAILED(bufferReflector->GetDesc(&bufferDesc))) {
						logger::trace("Failed to get buffer {} descriptor for {} shader {}::{:X}",
							bufferName, magic_enum::enum_name(shaderClass),
							magic_enum::enum_name(shader.shaderType.get()),
							descriptor);
						return;
					}

					for (uint32_t i = 0; i < bufferDesc.Variables; i++) {
						ID3D11ShaderReflectionVariable* var = bufferReflector->GetVariableByIndex(i);

						D3D11_SHADER_VARIABLE_DESC varDesc;
						if (FAILED(var->GetDesc(&varDesc))) {
							logger::trace("Failed to get variable descriptor for {} shader {}::{:X}",
								magic_enum::enum_name(shaderClass), magic_enum::enum_name(shader.shaderType.get()),
								descriptor);
							continue;
						}

						const auto variableIndex =
							GetVariableIndex(shaderClass, shader, varDesc.Name);
						const bool variableFound = variableIndex != -1;
						if (variableFound) {
							constantOffsets[variableIndex] = (int8_t)(varDesc.StartOffset / 4);
						} else {
							logger::trace("Unknown variable name {} in {} shader {}::{:X}",
								varDesc.Name, magic_enum::enum_name(shaderClass),
								magic_enum::enum_name(shader.shaderType.get()),
								descriptor);
						}

						if (shader.shaderType == RE::BSShader::Type::ImageSpace) {
							D3D11_SHADER_TYPE_DESC varTypeDesc;
							var->GetType()->GetDesc(&varTypeDesc);
							if (varTypeDesc.Elements > 0) {
								if (!variableFound) {
									const std::string arrayName =
										std::format("{}[{}]", varDesc.Name, varTypeDesc.Elements);
									const auto variableArrayIndex =
										GetVariableIndex(shaderClass, shader, arrayName.c_str());
									if (variableArrayIndex != -1) {
										constantOffsets[variableArrayIndex] = static_cast<int8_t>(varDesc.StartOffset / 4);
									} else {
										logger::debug("Unknown variable name {} in {} shader {}::{:X}",
											arrayName, magic_enum::enum_name(shaderClass),
											magic_enum::enum_name(shader.shaderType.get()), descriptor);
									}
								} else {
									const auto elementSize = varDesc.Size / varTypeDesc.Elements;
									for (uint32_t arrayIndex = 1; arrayIndex < varTypeDesc.Elements;
										++arrayIndex) {
										const std::string varName =
											std::format("{}[{}]", varDesc.Name, arrayIndex);
										const auto variableArrayElementIndex =
											GetVariableIndex(shaderClass, shader, varName.c_str());
										if (variableArrayElementIndex != -1) {
											constantOffsets[variableArrayElementIndex] =
												static_cast<int8_t>((varDesc.StartOffset + elementSize * arrayIndex) / 4);
										} else {
											logger::debug(
												"Unknown variable name {} in {} shader {}::{:X}", varName,
												magic_enum::enum_name(shaderClass),
												magic_enum::enum_name(shader.shaderType.get()),
												descriptor);
										}
									}
								}
							}
						}
					}

					bufferSize = ((bufferDesc.Size + 15) & ~15) / 16;
				};

			mapBufferConsts("PerTechnique", bufferSizes[0]);
			mapBufferConsts("PerMaterial", bufferSizes[1]);
			mapBufferConsts("PerGeometry", bufferSizes[2]);
		}

		std::wstring GetDiskPath(
			const std::string_view& name,
			uint32_t descriptor,
			ShaderClass shaderClass,
			std::string_view customDefines)
		{
			const auto suffixNarrow = Util::GetShaderDefinesSuffix(customDefines);
			const std::wstring suffix(suffixNarrow.begin(), suffixNarrow.end());

			const auto wname = std::wstring(name.begin(), name.end());
			switch (shaderClass) {
			case ShaderClass::Pixel:
				return std::format(L"Data/ShaderCache/{}/{:X}{}.pso", wname, descriptor, suffix);
			case ShaderClass::Vertex:
				return std::format(L"Data/ShaderCache/{}/{:X}{}.vso", wname, descriptor, suffix);
			case ShaderClass::Compute:
				return std::format(L"Data/ShaderCache/{}/{:X}{}.cso", wname, descriptor, suffix);
			}
			return {};
		}

		std::wstring GetDiskPath(const std::string_view& name, uint32_t descriptor, ShaderClass shaderClass)
		{
			const auto shaderDefines = globals::state->GetShaderDefinesSnapshot();
			return GetDiskPath(name, descriptor, shaderClass, shaderDefines->canonicalText);
		}

		static std::string GetShaderString(ShaderClass shaderClass, const RE::BSShader& shader, uint32_t descriptor, bool hashkey)
		{
			auto sourceShaderFile = shader.fxpFilename;
			std::array<D3D_SHADER_MACRO, 64> defines{};
			SIE::SShaderCache::GetShaderDefines(shader, descriptor, std::span{ defines });
			std::string result;
			if (hashkey)  // generate hashkey so don't include descriptor
				result = fmt::format("{}:{}:{}", sourceShaderFile, magic_enum::enum_name(shaderClass), SIE::SShaderCache::MergeDefinesString(defines, true));
			else
				result = fmt::format("{}:{}:{:X}:{}", sourceShaderFile, magic_enum::enum_name(shaderClass), descriptor, SIE::SShaderCache::MergeDefinesString(defines, true));
			return result;
		}

		std::string GetTypeFromShaderString(const std::string& a_key)
		{
			std::string type = "";
			std::string::size_type pos = a_key.find(':');
			if (pos != std::string::npos)
				type = a_key.substr(0, pos);
			if (type.starts_with("IS") || type == "ReflectionsRayTracing")
				type = "ImageSpace";  // fix type for image space shaders
			return type;
		}

		static ID3DBlob* CompileShader(
			ShaderClass shaderClass,
			const RE::BSShader& shader,
			uint32_t descriptor,
			bool useDiskCache,
			ShaderFileDependencyTracker* dependencyTracker,
			std::optional<uint64_t> a_taskGeneration = std::nullopt)
		{
			if (!SShaderCache::ResolveImageSpaceDescriptor(shader, descriptor)) {
				return nullptr;
			}

			auto compileState = CaptureGlobalCompileState();
			const auto packCompileStateDigest = compileState.digest;
			const auto shaderSourcePath = GetShaderPath(
				shader.shaderType == RE::BSShader::Type::ImageSpace ?
					static_cast<const RE::BSImagespaceShader&>(shader).originalShaderName :
					shader.fxpFilename);
			const auto compatibility = CSX::Api::GetShaderCompatibilityRequirementSet(
				shader.fxpFilename,
				Util::WStringToString(shaderSourcePath));
			compileState.digest = Util::ContentHash::CombineHashes(
				compileState.digest,
				Util::ContentHash::HashString(compatibility.canonical));
			const uint64_t diskCacheGeneration = GetDiskCacheGeneration();
			auto& cache = ShaderCache::Instance();
			auto key = SShaderCache::GetShaderString(shaderClass, shader, descriptor, true);

			// Atomically check the shaderMap and either:
			//  - return the blob if already Completed (cache hit),
			//  - wait if another thread is compiling (Pending),
			//  - claim the slot with Pending if nobody started yet.
			auto [claimResult, cachedBlob] = cache.ClaimCompilation(key, a_taskGeneration);
			if (claimResult == ShaderCache::ClaimResult::CacheHit) {
				cache.IncCacheHitTasks(a_taskGeneration);
				return cachedBlob;
			}
			if (claimResult == ShaderCache::ClaimResult::RejectedStale) {
				return nullptr;
			}

			const auto type = shader.shaderType.get();

			// check diskcache
			auto diskPath = GetDiskPath(
				shader.fxpFilename,
				descriptor,
				shaderClass,
				compileState.shaderDefines->canonicalText);
			ID3DBlob* shaderBlob = nullptr;
			auto* managedPack = useDiskCache ? GetShaderPackStore(compileState.developerMode) : nullptr;
			if (managedPack) {
				shaderBlob = LoadShaderBlobFromPack(
					*managedPack,
					compileState.developerMode,
					diskPath,
					shaderSourcePath,
					packCompileStateDigest);
				if (shaderBlob) {
					logger::debug("Loaded shader from managed pack: {}", Util::WStringToString(diskPath));
					cache.AddCompletedShader(
						shaderClass, shader, descriptor, shaderBlob, diskPath, compileState.digest, /*fromDisk=*/true);
					return shaderBlob;
				}
				managedPack = GetShaderPackStore(compileState.developerMode);
			}

			if (Util::ShaderCachePack::ShouldReadLooseBlob(useDiskCache, managedPack != nullptr) &&
				std::filesystem::exists(diskPath)) {
				// Determine whether the disk-cached shader is still valid.
				bool diskCacheOutdated = false;
				if (!IsSaveLoadSafeModeActive()) {
					bool decidedByDigest = false;
					if (dependencyTracker && std::filesystem::exists(shaderSourcePath)) {
						dependencyTracker->RegisterDependencies(
							Util::WStringToString(shaderSourcePath),
							GetShaderDependencyPaths(shaderSourcePath, ShaderSourceRoot()));
					}

					// A manifest entry is authoritative. Older or damaged
					// manifests simply fall through to the existing mtime path.
					if (const auto recordedDigest =
							GetShaderCacheManifest().Get(GetManifestKey(diskPath))) {
						if (std::filesystem::exists(shaderSourcePath)) {
							if (const auto sourceDigest = GetShaderContentDigest(
									shaderSourcePath,
									ShaderSourceRoot())) {
								decidedByDigest = true;
								const auto combined = Util::ContentHash::CombineHashes(
									*sourceDigest,
									compileState.digest);
								diskCacheOutdated = *recordedDigest != combined.ToHex();
								if (diskCacheOutdated) {
									logger::debug(
										"[ShaderCacheEntry] result=stale reason=content-contract source={} cache={}",
										Util::WStringToString(shaderSourcePath),
										Util::WStringToString(diskPath));
								} else {
									logger::debug(
										"[ShaderCacheEntry] result=reused reason=content-contract source={} cache={}",
										Util::WStringToString(shaderSourcePath),
										Util::WStringToString(diskPath));
								}
							}
						}
					}

					if (!decidedByDigest && cache.UseFileWatcher()) {
						// File watcher tracks runtime changes in memory: compare disk-cache mtime against tracked source mtime.
						auto diskCacheTime = std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(diskPath));
						diskCacheOutdated = cache.ShaderModifiedSince(shader.fxpFilename, diskCacheTime);
						if (diskCacheOutdated)
							logger::debug("Diskcached shader {} older than {}", SIE::SShaderCache::GetShaderString(shaderClass, shader, descriptor, true), std::format("{:%Y%m%d%H%M}", diskCacheTime));
					} else if (!decidedByDigest && cache.IsSkipUnchangedShaders()) {
						// No file watcher: compare the disk cache mtime against the newest source/include mtime.
						std::error_code ec;
						const auto diskCacheTime = std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(diskPath, ec));
						if (ec) {
							logger::debug("Failed to read disk cache mtime for {}: {}", Util::WStringToString(diskPath), ec.message());
						} else {
							if (std::filesystem::exists(shaderSourcePath)) {
								const auto sourceTime = GetMaxShaderMTime(shaderSourcePath, std::filesystem::path(shaderSourcePath).parent_path());
								if (sourceTime > diskCacheTime) {
									diskCacheOutdated = true;
									logger::debug("Disk-cached shader {} outdated: source or include is newer than cache", SIE::SShaderCache::GetShaderString(shaderClass, shader, descriptor, true));
								}
							}
						}
					}
				}

				if (diskCacheOutdated) {
					// Fall through to recompile from source.
				} else if (FAILED(D3DReadFileToBlob(diskPath.c_str(), &shaderBlob))) {
					logger::error("Failed to load {} shader {}::{:X}", magic_enum::enum_name(shaderClass), magic_enum::enum_name(type), descriptor);

					if (shaderBlob != nullptr) {
						shaderBlob->Release();
					}
				} else {
					logger::debug("Loaded shader from {}", Util::WStringToString(diskPath));
					if (!cache.AddCompletedShader(
							shaderClass,
							shader,
							descriptor,
							shaderBlob,
							diskPath,
							compileState.digest,
							/*fromDisk=*/true,
							a_taskGeneration)) {
						shaderBlob->Release();
						return nullptr;
					}
					return shaderBlob;
				}
			}

			// prepare preprocessor defines
			std::array<D3D_SHADER_MACRO, 64> defines{};
			auto lastIndex = 0;
			if (shaderClass == ShaderClass::Vertex) {
				defines[lastIndex++] = { "VSHADER", nullptr };
			} else if (shaderClass == ShaderClass::Pixel) {
				defines[lastIndex++] = { "PSHADER", nullptr };
			} else if (shaderClass == ShaderClass::Compute) {
				defines[lastIndex++] = { "CSHADER", nullptr };
			}
			if (compileState.developerMode) {
				defines[lastIndex++] = { "D3DCOMPILE_SKIP_OPTIMIZATION", nullptr };
				defines[lastIndex++] = { "D3DCOMPILE_DEBUG", nullptr };
			}
			if (compileState.isVR)
				defines[lastIndex++] = { "VR", nullptr };
			if (!compileState.shaderDefines->defines.empty()) {
				for (const auto& [name, definition] : compileState.shaderDefines->defines)
					defines[lastIndex++] = { name.c_str(), definition.c_str() };
			}
			defines[lastIndex] = { nullptr, nullptr };  // do final entry
			GetShaderDefines(shader, descriptor, std::span{ defines }.subspan(lastIndex));

			const std::wstring path = shaderSourcePath;
			auto pathString = Util::WStringToString(path);
			if (!std::filesystem::exists(path)) {
				logger::error("Failed to compile {} shader {}::{:X}: {} does not exist", magic_enum::enum_name(shaderClass), magic_enum::enum_name(type), descriptor, pathString);
				cache.RecordCompileFailure(key, pathString, pathString + " does not exist");
				cache.AddCompletedShader(
					shaderClass,
					shader,
					descriptor,
					nullptr,
					diskPath,
					compileState.digest,
					false,
					a_taskGeneration);
				return nullptr;
			}
			cache.IncSourceCompileTasks(a_taskGeneration);
			logger::debug("Compiling {} {}:{}:{:X} to {}", pathString, magic_enum::enum_name(type), magic_enum::enum_name(shaderClass), descriptor, MergeDefinesString(defines));

			// compile shaders — match Utils/D3D.cpp CompileShader flag policy (strictness, optional toggles, validation).
			ID3DBlob* errorBlob = nullptr;
			uint32_t flags = !compileState.developerMode ? (D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3) : D3DCOMPILE_DEBUG;
			if (compileState.partialPrecision) {
				flags |= D3DCOMPILE_PARTIAL_PRECISION;
			}
			if (compileState.avoidFlowControl) {
				flags |= D3DCOMPILE_AVOID_FLOW_CONTROL;
			}
			if (useDiskCache) {
				flags |= D3DCOMPILE_SKIP_VALIDATION;
			}

			// Disk-cache hits return before this point, so latch the phase at the
			// first real compiler invocation rather than at task completion.
			cache.MarkCompilationPhaseStarted(a_taskGeneration);

			// Track includes
			TrackingIncludeHandler includeHandler(std::filesystem::path(path).parent_path());
			const HRESULT compileResult = D3DCompileFromFile(path.c_str(), defines.data(), &includeHandler, "main",
				GetShaderProfile(shaderClass), flags, 0, &shaderBlob, &errorBlob);
			// If the include handler captured any includes, register them so the watcher
			// can invalidate dependents even if this compilation fails. Do NOT clear
			// mappings when there are no captured includes to avoid removing prior
			// dependency information on transient failures.
			if (dependencyTracker && !includeHandler.includes.empty()) {
				dependencyTracker->RegisterDependencies(Util::WStringToString(path), includeHandler.includes);
			}

			if (FAILED(compileResult)) {
				std::string errorText;
				if (errorBlob != nullptr) {
					// Compiler blobs are sized byte buffers and need not be NUL-terminated.
					errorText.assign(static_cast<char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
					if (!errorText.empty() && errorText.back() == '\0')
						errorText.pop_back();
					logger::error("Failed to compile {} shader {}::{:X}:\n{}",
						magic_enum::enum_name(shaderClass), magic_enum::enum_name(type), descriptor, errorText);
					errorBlob->Release();
				} else {
					logger::error("Failed to compile {} shader {}::{:X}",
						magic_enum::enum_name(shaderClass), magic_enum::enum_name(type), descriptor);
				}
				cache.RecordCompileFailure(key, pathString, errorText);
				if (shaderBlob != nullptr) {
					shaderBlob->Release();
				}

				cache.AddCompletedShader(
					shaderClass,
					shader,
					descriptor,
					nullptr,
					diskPath,
					compileState.digest,
					false,
					a_taskGeneration);
				return nullptr;
			}
			if (errorBlob)
				logger::debug("Shader logs:\n{}", static_cast<char*>(errorBlob->GetBufferPointer()));
			logger::debug("Compiled shader {}:{}:{:X}", magic_enum::enum_name(type), magic_enum::enum_name(shaderClass), descriptor);

			// strip debug info
			if (!compileState.developerMode) {
				ID3DBlob* strippedShaderBlob = nullptr;

				const uint32_t stripFlags = D3DCOMPILER_STRIP_DEBUG_INFO |
				                            D3DCOMPILER_STRIP_TEST_BLOBS |
				                            D3DCOMPILER_STRIP_PRIVATE_DATA;

				D3DStripShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), stripFlags, &strippedShaderBlob);
				std::swap(shaderBlob, strippedShaderBlob);
				strippedShaderBlob->Release();
			}

			try {
				cache.PersistCompiledShaderBlob(
					shaderBlob,
					compileState.developerMode,
					diskPath,
					path,
					compileState.digest,
					packCompileStateDigest,
					diskCacheGeneration);
			} catch (const std::exception& e) {
				logger::error("Shader compiled successfully but persistence failed for {}: {}", Util::WStringToString(diskPath), e.what());
			} catch (...) {
				logger::error("Shader compiled successfully but persistence failed for {}", Util::WStringToString(diskPath));
			}
			if (!cache.AddCompletedShader(
					shaderClass,
					shader,
					descriptor,
					shaderBlob,
					diskPath,
					compileState.digest,
					false,
					a_taskGeneration)) {
				shaderBlob->Release();
				return nullptr;
			}
			return shaderBlob;
		}

		std::unique_ptr<RE::BSGraphics::VertexShader> CreateVertexShader(ID3DBlob& shaderData,
			const RE::BSShader& shader, uint32_t descriptor)
		{
			static const auto perTechniqueBuffersArray =
				REL::Relocation<ID3D11Buffer**>(RELOCATION_ID(524755, 411371));
			static const auto perMaterialBuffersArray =
				REL::Relocation<ID3D11Buffer**>(RELOCATION_ID(524757, 411373));
			static const auto perGeometryBuffersArray =
				REL::Relocation<ID3D11Buffer**>(RELOCATION_ID(524759, 411375));
			static const auto bufferData = REL::Relocation<void*>(RELOCATION_ID(524965, 411446));

			auto rawPtr =
				new uint8_t[sizeof(RE::BSGraphics::VertexShader) + shaderData.GetBufferSize()];
			auto shaderPtr = new (rawPtr) RE::BSGraphics::VertexShader;
			memcpy(rawPtr + sizeof(RE::BSGraphics::VertexShader), shaderData.GetBufferPointer(),
				shaderData.GetBufferSize());
			std::unique_ptr<RE::BSGraphics::VertexShader> newShader{ shaderPtr };
			newShader->byteCodeSize = (uint32_t)shaderData.GetBufferSize();
			newShader->id = descriptor;
			newShader->vertexDesc = 0;

			winrt::com_ptr<ID3D11ShaderReflection> reflector;
			const auto reflectionResult = D3DReflect(shaderData.GetBufferPointer(), shaderData.GetBufferSize(),
				IID_PPV_ARGS(&reflector));
			if (FAILED(reflectionResult)) {
				logger::error("Failed to reflect vertex shader {}::{:X}", magic_enum::enum_name(shader.shaderType.get()),
					descriptor);
			} else {
				std::array<size_t, 3> bufferSizes = { 0, 0, 0 };
				std::fill(newShader->constantTable.begin(), newShader->constantTable.end(), static_cast<uint8_t>(0));
				ReflectConstantBuffers(*reflector.get(), bufferSizes, newShader->constantTable, newShader->vertexDesc,
					ShaderClass::Vertex, descriptor, shader);
				if (bufferSizes[0] != 0) {
					newShader->constantBuffers[0].buffer =
						(REX::W32::ID3D11Buffer*)perTechniqueBuffersArray.get()[bufferSizes[0]];
				} else {
					newShader->constantBuffers[0].buffer = nullptr;
					newShader->constantBuffers[0].data = bufferData.get();
				}
				if (bufferSizes[1] != 0) {
					newShader->constantBuffers[1].buffer =
						(REX::W32::ID3D11Buffer*)perMaterialBuffersArray.get()[bufferSizes[1]];
				} else {
					newShader->constantBuffers[1].buffer = nullptr;
					newShader->constantBuffers[1].data = bufferData.get();
				}
				if (bufferSizes[2] != 0) {
					newShader->constantBuffers[2].buffer =
						(REX::W32::ID3D11Buffer*)perGeometryBuffersArray.get()[bufferSizes[2]];
				} else {
					newShader->constantBuffers[2].buffer = nullptr;
					newShader->constantBuffers[2].data = bufferData.get();
				}
			}

			return newShader;
		}

		std::unique_ptr<RE::BSGraphics::PixelShader> CreatePixelShader(ID3DBlob& shaderData,
			const RE::BSShader& shader, uint32_t descriptor)
		{
			static const auto perTechniqueBuffersArray =
				REL::Relocation<ID3D11Buffer**>(RELOCATION_ID(524761, 411377));
			static const auto perMaterialBuffersArray =
				REL::Relocation<ID3D11Buffer**>(RELOCATION_ID(524763, 411379));
			static const auto perGeometryBuffersArray =
				REL::Relocation<ID3D11Buffer**>(RELOCATION_ID(524765, 411381));
			static const auto bufferData = REL::Relocation<void*>(RELOCATION_ID(524967, 411448));

			auto newShader = std::make_unique<RE::BSGraphics::PixelShader>();
			newShader->id = descriptor;

			winrt::com_ptr<ID3D11ShaderReflection> reflector;
			const auto reflectionResult = D3DReflect(shaderData.GetBufferPointer(),
				shaderData.GetBufferSize(), IID_PPV_ARGS(&reflector));
			if (FAILED(reflectionResult)) {
				logger::error("Failed to reflect vertex shader {}::{:X}", magic_enum::enum_name(shader.shaderType.get()),
					descriptor);
			} else {
				std::array<size_t, 3> bufferSizes = { 0, 0, 0 };
				std::ranges::fill(newShader->constantTable, (int8_t)0);
				uint64_t dummy;
				ReflectConstantBuffers(*reflector.get(), bufferSizes, newShader->constantTable,
					dummy,
					ShaderClass::Pixel, descriptor, shader);
				if (bufferSizes[0] != 0) {
					newShader->constantBuffers[0].buffer =
						(REX::W32::ID3D11Buffer*)perTechniqueBuffersArray.get()[bufferSizes[0]];
				} else {
					newShader->constantBuffers[0].buffer = nullptr;
					newShader->constantBuffers[0].data = bufferData.get();
				}
				if (bufferSizes[1] != 0) {
					newShader->constantBuffers[1].buffer =
						(REX::W32::ID3D11Buffer*)perMaterialBuffersArray.get()[bufferSizes[1]];
				} else {
					newShader->constantBuffers[1].buffer = nullptr;
					newShader->constantBuffers[1].data = bufferData.get();
				}
				if (bufferSizes[2] != 0) {
					newShader->constantBuffers[2].buffer =
						(REX::W32::ID3D11Buffer*)perGeometryBuffersArray.get()[bufferSizes[2]];
				} else {
					newShader->constantBuffers[2].buffer = nullptr;
					newShader->constantBuffers[2].data = bufferData.get();
				}
			}

			return newShader;
		}

		std::unique_ptr<RE::BSGraphics::ComputeShader> CreateComputeShader([[maybe_unused]] ID3DBlob& shaderData,
			[[maybe_unused]] const RE::BSShader& shader, uint32_t descriptor)
		{
			auto newShader = std::make_unique<RE::BSGraphics::ComputeShader>();
			newShader->id = descriptor;
			return newShader;
		}

		static bool GetImagespaceShaderDescriptor(const RE::BSImagespaceShader& imagespaceShader, uint32_t& descriptor)
		{
			using enum RE::ImageSpaceManager::ImageSpaceEffectEnum;

			static const ankerl::unordered_dense::map<std::string_view, uint32_t> descriptors{
				// { "BSImagespaceShaderISBlur", RE::ImageSpaceManager::GetCurrentIndex(ISBlur) },
				// { "BSImagespaceShaderBlur3", RE::ImageSpaceManager::GetCurrentIndex(ISBlur3) },
				// { "BSImagespaceShaderBlur5", RE::ImageSpaceManager::GetCurrentIndex(ISBlur5) },
				// { "BSImagespaceShaderBlur7", RE::ImageSpaceManager::GetCurrentIndex(ISBlur7) },
				// { "BSImagespaceShaderBlur9", RE::ImageSpaceManager::GetCurrentIndex(ISBlur9) },
				// { "BSImagespaceShaderBlur11", RE::ImageSpaceManager::GetCurrentIndex(ISBlur11) },
				// { "BSImagespaceShaderBlur13", RE::ImageSpaceManager::GetCurrentIndex(ISBlur13) },
				// { "BSImagespaceShaderBlur15", RE::ImageSpaceManager::GetCurrentIndex(ISBlur15) },
				// { "BSImagespaceShaderBrightPassBlur3", RE::ImageSpaceManager::GetCurrentIndex(ISBrightPassBlur3) },
				// { "BSImagespaceShaderBrightPassBlur5", RE::ImageSpaceManager::GetCurrentIndex(ISBrightPassBlur5) },
				// { "BSImagespaceShaderBrightPassBlur7", RE::ImageSpaceManager::GetCurrentIndex(ISBrightPassBlur7) },
				// { "BSImagespaceShaderBrightPassBlur9", RE::ImageSpaceManager::GetCurrentIndex(ISBrightPassBlur9) },
				// { "BSImagespaceShaderBrightPassBlur11", RE::ImageSpaceManager::GetCurrentIndex(ISBrightPassBlur11) },
				// { "BSImagespaceShaderBrightPassBlur13", RE::ImageSpaceManager::GetCurrentIndex(ISBrightPassBlur13) },
				// { "BSImagespaceShaderBrightPassBlur15", RE::ImageSpaceManager::GetCurrentIndex(ISBrightPassBlur15) },
				// { "BSImagespaceShaderNonHDRBlur3", RE::ImageSpaceManager::GetCurrentIndex(ISNonHDRBlur3) },
				// { "BSImagespaceShaderNonHDRBlur5", RE::ImageSpaceManager::GetCurrentIndex(ISNonHDRBlur5) },
				// { "BSImagespaceShaderNonHDRBlur7", RE::ImageSpaceManager::GetCurrentIndex(ISNonHDRBlur7) },
				// { "BSImagespaceShaderNonHDRBlur9", RE::ImageSpaceManager::GetCurrentIndex(ISNonHDRBlur9) },
				// { "BSImagespaceShaderNonHDRBlur11", RE::ImageSpaceManager::GetCurrentIndex(ISNonHDRBlur11) },
				// { "BSImagespaceShaderNonHDRBlur13", RE::ImageSpaceManager::GetCurrentIndex(ISNonHDRBlur13) },
				// { "BSImagespaceShaderNonHDRBlur15", RE::ImageSpaceManager::GetCurrentIndex(ISNonHDRBlur15) },
				// { "BSImagespaceShaderISBasicCopy", RE::ImageSpaceManager::GetCurrentIndex(ISBasicCopy) },
				// { "BSImagespaceShaderISSimpleColor", RE::ImageSpaceManager::GetCurrentIndex(ISSimpleColor) },
				// { "BSImagespaceShaderApplyReflections", RE::ImageSpaceManager::GetCurrentIndex(ISApplyReflections) },
				// { "BSImagespaceShaderISExp", RE::ImageSpaceManager::GetCurrentIndex(ISExp) },
				// { "BSImagespaceShaderISDisplayDepth", RE::ImageSpaceManager::GetCurrentIndex(ISDisplayDepth) },
				// { "BSImagespaceShaderAlphaBlend", RE::ImageSpaceManager::GetCurrentIndex(ISAlphaBlend) },
				// { "BSImagespaceShaderWaterFlow", RE::ImageSpaceManager::GetCurrentIndex(ISWaterFlow) },
				{ "BSImagespaceShaderISWaterBlend", RE::ImageSpaceManager::GetCurrentIndex(ISWaterBlend) },
				// { "BSImagespaceShaderGreyScale", RE::ImageSpaceManager::GetCurrentIndex(ISCopyGrayScale) },
				// { "BSImagespaceShaderCopy", RE::ImageSpaceManager::GetCurrentIndex(ISCopy) },
				// { "BSImagespaceShaderCopyScaleBias", RE::ImageSpaceManager::GetCurrentIndex(ISCopyScaleBias) },
				// { "BSImagespaceShaderCopyCustomViewport",
				//  RE::ImageSpaceManager::GetCurrentIndex(ISCopyCustomViewport) },
				// { "BSImagespaceShaderCopyTextureMask", RE::ImageSpaceManager::GetCurrentIndex(ISCopyTextureMask) },
				// { "BSImagespaceShaderCopyDynamicFetchDisabled",
				//  RE::ImageSpaceManager::GetCurrentIndex(ISCopyDynamicFetchDisabled) },
				{ "BSImagespaceShaderISCompositeVolumetricLighting",
					RE::ImageSpaceManager::GetCurrentIndex(ISCompositeVolumetricLighting) },
				{ "BSImagespaceShaderISCompositeLensFlare",
					RE::ImageSpaceManager::GetCurrentIndex(ISCompositeLensFlare) },
				{ "BSImagespaceShaderISCompositeLensFlareVolumetricLighting",
					RE::ImageSpaceManager::GetCurrentIndex(ISCompositeLensFlareVolumetricLighting) },
				// { "BSImagespaceShaderISDebugSnow", RE::ImageSpaceManager::GetCurrentIndex(ISDebugSnow) },
				{ "BSImagespaceShaderDepthOfField", RE::ImageSpaceManager::GetCurrentIndex(ISDepthOfField) },
				{ "BSImagespaceShaderDepthOfFieldFogged",
					RE::ImageSpaceManager::GetCurrentIndex(ISDepthOfFieldFogged) },
				{ "BSImagespaceShaderDepthOfFieldMaskedFogged",
					RE::ImageSpaceManager::GetCurrentIndex(ISDepthOfFieldMaskedFogged) },
				// { "BSImagespaceShaderDistantBlur", RE::ImageSpaceManager::GetCurrentIndex(ISDistantBlur) },
				// { "BSImagespaceShaderDistantBlurFogged",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISDistantBlurFogged) },
				// { "BSImagespaceShaderDistantBlurMaskedFogged",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISDistantBlurMaskedFogged) },
				// { "BSImagespaceShaderDoubleVision", RE::ImageSpaceManager::GetCurrentIndex(ISDoubleVision) },
				{ "BSImagespaceShaderISDownsample", RE::ImageSpaceManager::GetCurrentIndex(ISDownsample) },
				{ "BSImagespaceShaderISDownsampleIgnoreBrightest",
					RE::ImageSpaceManager::GetCurrentIndex(ISDownsampleIgnoreBrightest) },
				// { "BSImagespaceShaderISUpsampleDynamicResolution",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISUpsampleDynamicResolution) },
				{ "BSImageSpaceShaderVolumetricLighting",
					RE::ImageSpaceManager::GetCurrentIndex(ISVolumetricLighting) },
				{ "BSImagespaceShaderHDRDownSample4", RE::ImageSpaceManager::GetCurrentIndex(ISHDRDownSample4) },
				{ "BSImagespaceShaderHDRDownSample4LightAdapt",
					RE::ImageSpaceManager::GetCurrentIndex(ISHDRDownSample4LightAdapt) },
				{ "BSImagespaceShaderHDRDownSample4LumClamp",
					RE::ImageSpaceManager::GetCurrentIndex(ISHDRDownSample4LumClamp) },
				{ "BSImagespaceShaderHDRDownSample4RGB2Lum",
					RE::ImageSpaceManager::GetCurrentIndex(ISHDRDownSample4RGB2Lum) },
				{ "BSImagespaceShaderHDRDownSample16", RE::ImageSpaceManager::GetCurrentIndex(ISHDRDownSample16) },
				{ "BSImagespaceShaderHDRDownSample16LightAdapt",
					RE::ImageSpaceManager::GetCurrentIndex(ISHDRDownSample16LightAdapt) },
				{ "BSImagespaceShaderHDRDownSample16Lum",
					RE::ImageSpaceManager::GetCurrentIndex(ISHDRDownSample16Lum) },
				{ "BSImagespaceShaderHDRDownSample16LumClamp",
					RE::ImageSpaceManager::GetCurrentIndex(ISHDRDownSample16LumClamp) },
				{ "BSImagespaceShaderHDRTonemapBlendCinematic",
					RE::ImageSpaceManager::GetCurrentIndex(ISHDRTonemapBlendCinematic) },
				{ "BSImagespaceShaderHDRTonemapBlendCinematicFade",
					RE::ImageSpaceManager::GetCurrentIndex(ISHDRTonemapBlendCinematicFade) },
				// { "BSImagespaceShaderISIBLensFlares", RE::ImageSpaceManager::GetCurrentIndex(ISIBLensFlares) },

				// Those cause issue because of typo in shader name in vanilla code but at the same time they are not used by vanilla game.
				// { "BSImagespaceShaderISLightingComposite",
				//  RE::ImageSpaceManager::GetCurrentIndex(ISLightingComposite) },
				// { "BSImagespaceShaderISLightingCompositeMenu",
				//  RE::ImageSpaceManager::GetCurrentIndex(ISLightingCompositeMenu) },
				// { "BSImagespaceShaderISLightingCompositeNoDirectionalLight",
				//  RE::ImageSpaceManager::GetCurrentIndex(ISLightingCompositeNoDirectionalLight) },

				// { "BSImagespaceShaderLocalMap", RE::ImageSpaceManager::GetCurrentIndex(ISLocalMap) },
				// { "BSISWaterBlendHeightmaps", RE::ImageSpaceManager::GetCurrentIndex(ISWaterBlendHeightmaps) },
				// { "BSISWaterDisplacementClearSimulation",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISWaterDisplacementClearSimulation) },
				// { "BSISWaterDisplacementNormals",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISWaterDisplacementNormals) },
				// { "BSISWaterDisplacementRainRipple",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISWaterDisplacementRainRipple) },
				// { "BSISWaterDisplacementTexOffset",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISWaterDisplacementTexOffset) },
				// { "BSISWaterWadingHeightmap", RE::ImageSpaceManager::GetCurrentIndex(ISWaterWadingHeightmap) },
				// { "BSISWaterRainHeightmap", RE::ImageSpaceManager::GetCurrentIndex(ISWaterRainHeightmap) },
				// { "BSISWaterSmoothHeightmap", RE::ImageSpaceManager::GetCurrentIndex(ISWaterSmoothHeightmap) },
				// { "BSISWaterWadingHeightmap", RE::ImageSpaceManager::GetCurrentIndex(ISWaterWadingHeightmap) },
				// { "BSImagespaceShaderMap", RE::ImageSpaceManager::GetCurrentIndex(ISMap) },
				// { "BSImagespaceShaderMap", RE::ImageSpaceManager::GetCurrentIndex(ISMap) },
				// { "BSImagespaceShaderWorldMap", RE::ImageSpaceManager::GetCurrentIndex(ISWorldMap) },
				// { "BSImagespaceShaderWorldMapNoSkyBlur",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISWorldMapNoSkyBlur) },
				// { "BSImagespaceShaderISMinify", RE::ImageSpaceManager::GetCurrentIndex(ISMinify) },
				// { "BSImagespaceShaderISMinifyContrast", RE::ImageSpaceManager::GetCurrentIndex(ISMinifyContrast) },
				// { "BSImagespaceShaderNoiseNormalmap", RE::ImageSpaceManager::GetCurrentIndex(ISNoiseNormalmap) },
				// { "BSImagespaceShaderNoiseScrollAndBlend",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISNoiseScrollAndBlend) },
				// { "BSImagespaceShaderRadialBlur",
				// 	RE::ImageSpaceManager::GetCurrentIndex(ISRadialBlur) },
				// { "BSImagespaceShaderRadialBlurHigh", RE::ImageSpaceManager::GetCurrentIndex(ISRadialBlurHigh) },
				// { "BSImagespaceShaderRadialBlurMedium", RE::ImageSpaceManager::GetCurrentIndex(ISRadialBlurMedium) },
				{ "BSImagespaceShaderRefraction", RE::ImageSpaceManager::GetCurrentIndex(ISRefraction) },
				{ "BSImagespaceShaderISSAOCompositeSAO", RE::ImageSpaceManager::GetCurrentIndex(ISSAOCompositeSAO) },
				{ "BSImagespaceShaderISSAOCompositeFog", RE::ImageSpaceManager::GetCurrentIndex(ISSAOCompositeFog) },
				{ "BSImagespaceShaderISSAOCompositeSAOFog", RE::ImageSpaceManager::GetCurrentIndex(ISSAOCompositeSAOFog) },
				// { "BSImagespaceShaderISSAOCameraZ", RE::ImageSpaceManager::GetCurrentIndex(ISSAOCameraZ) },
				// { "BSImagespaceShaderISSILComposite", RE::ImageSpaceManager::GetCurrentIndex(ISSILComposite) },
				// { "BSImagespaceShaderISSnowSSS", RE::ImageSpaceManager::GetCurrentIndex(ISSnowSSS) },
				// { "BSImagespaceShaderISSAOBlurH", RE::ImageSpaceManager::GetCurrentIndex(ISSAOBlurH) },
				// { "BSImagespaceShaderISSAOBlurV", RE::ImageSpaceManager::GetCurrentIndex(ISSAOBlurV) },
				// { "BSImagespaceShaderISUnderwaterMask", RE::ImageSpaceManager::GetCurrentIndex(ISUnderwaterMask) },
				{ "BSImagespaceShaderISApplyVolumetricLighting", RE::ImageSpaceManager::GetCurrentIndex(ISApplyVolumetricLighting) },
				{ "BSImagespaceShaderReflectionsRayTracing", RE::ImageSpaceManager::GetCurrentIndex(ISReflectionsRayTracing) },
				//{ "BSImagespaceShaderReflectionsDebugSpecMask", RE::ImageSpaceManager::GetCurrentIndex(ISReflectionsDebugSpecMask) },

				{ "BSImagespaceShaderVolumetricLightingRaymarchCS", 256 },
				{ "BSImagespaceShaderVolumetricLightingGenerateCS", 257 },
				{ "BSImagespaceShaderVolumetricLightingBlurHCS", RE::ImageSpaceManager::GetCurrentIndex(ISVolumetricLightingBlurHCS) },
				{ "BSImagespaceShaderVolumetricLightingBlurVCS", RE::ImageSpaceManager::GetCurrentIndex(ISVolumetricLightingBlurVCS) },

				// VR only shaders
				// Disable BSImagespaceShaderCopyDepthBuffer since we don't have it REed and it causes issues with cache and upscaling
				// https://github.com/doodlum/skyrim-community-shaders/issues/1552
				// { "BSImagespaceShaderCopyDepthBuffer", RE::ImageSpaceManager::GetCurrentIndex(ISCopyDepthBuffer) },
				// { "BSImagespaceShaderCopyDepthBuffer_DR", RE::ImageSpaceManager::GetCurrentIndex(ISCopyDepthBuffer_DR) },
				// { "BSImagespaceShaderCopyDepthBufferTargetSize", RE::ImageSpaceManager::GetCurrentIndex(ISCopyDepthBufferTargetSize) },
				{ "BSImagespaceShaderISDownsampleHierarchicalDepthBufferCS", RE::ImageSpaceManager::GetCurrentIndex(ISDownsampleHierarchicalDepthBufferCS) },
				{ "BSImagespaceShaderISDiffScaleDownsampleDepthBufferCS", RE::ImageSpaceManager::GetCurrentIndex(ISDiffScaleDownsampleDepthBufferCS) },
				{ "BSImagespaceShaderISFullScreenVR", RE::ImageSpaceManager::GetCurrentIndex(ISFullScreenVR) },
				{ "BSImagespaceShaderISTransformLvl7PreTest", RE::ImageSpaceManager::GetCurrentIndex(ISTransformLvl7PreTest) },
				{ "BSImagespaceShaderISLvl6PreTest", RE::ImageSpaceManager::GetCurrentIndex(ISLvl6PreTest) },
				{ "BSImagespaceShaderISLvl5PreTest", RE::ImageSpaceManager::GetCurrentIndex(ISLvl5PreTest) },
				{ "BSImagespaceShaderISLvl4PreTest", RE::ImageSpaceManager::GetCurrentIndex(ISLvl4PreTest) },
				{ "BSImagespaceShaderISLvl3PreTest", RE::ImageSpaceManager::GetCurrentIndex(ISLvl3PreTest) },
				{ "BSImagespaceShaderISLvl2PreTest", RE::ImageSpaceManager::GetCurrentIndex(ISLvl2PreTest) },
				{ "BSImagespaceShaderISLvl1PreTest", RE::ImageSpaceManager::GetCurrentIndex(ISLvl1PreTest) },
				{ "BSImagespaceShaderISLvl0PreTest", RE::ImageSpaceManager::GetCurrentIndex(ISLvl0PreTest) },
				{ "BSImagespaceShaderISSetupPreTest", RE::ImageSpaceManager::GetCurrentIndex(ISSetupPreTest) },
			};

			auto it = descriptors.find(imagespaceShader.name);
			if (it == descriptors.cend()) {
				return false;
			}
			descriptor = it->second;
			return true;
		}

		static bool ResolveImageSpaceDescriptor(const RE::BSShader& shader, uint32_t& descriptor)
		{
			if (shader.shaderType == RE::BSShader::Type::ImageSpace) {
				const auto& isShader = static_cast<const RE::BSImagespaceShader&>(shader);
				return GetImagespaceShaderDescriptor(isShader, descriptor);
			}
			return true;
		}
	}

	RE::BSGraphics::VertexShader* ShaderCache::GetVertexShader(const RE::BSShader& shader,
		uint32_t descriptor)
	{
		if (!SShaderCache::ResolveImageSpaceDescriptor(shader, descriptor)) {
			return nullptr;
		}

		auto state = globals::state;
		const bool developerMode = state->IsDeveloperMode();
		if (globals::game::isVR && strcmp(shader.fxpFilename, "OBBOcclusionTesting") == 0)
			// use vanilla shader
			return nullptr;

		if (!((ShaderCache::IsSupportedShader(shader) || developerMode && state->IsShaderEnabled(shader)) && state->enableVShaders)) {
			return nullptr;
		}

		// Developer diagnostics and bounded smart clears share the tracking hook.
		// Outside those short-lived modes this remains a single relaxed atomic read.
		if (developerMode || activeShaderCaptureFramesRemaining.load(std::memory_order_relaxed) > 0)
			TrackActiveShader(ShaderClass::Vertex, shader, descriptor);

		if (developerMode) {
			auto key = SIE::SShaderCache::GetShaderString(ShaderClass::Vertex, shader, descriptor, true);
			if (blockedKeyIndex != -1 && !blockedKey.empty() && key == blockedKey) {
				if (std::find(blockedIDs.begin(), blockedIDs.end(), descriptor) == blockedIDs.end()) {
					blockedIDs.push_back(descriptor);
					logger::debug("Skipping blocked shader {:X}:{} total: {}", descriptor, blockedKey, blockedIDs.size());
				}
				return nullptr;
			}
		}

		{
			std::lock_guard lockGuard(vertexShadersMutex);
			auto& typeCache = vertexShaders[static_cast<size_t>(shader.shaderType.underlying())];
			auto it = typeCache.find(descriptor);
			if (it != typeCache.end()) {
				return it->second.get();
			}
		}
		if (IsSaveLoadSafeModeActive()) {
			return nullptr;
		}

		if (ShouldUseAsyncCompilation()) {
			compilationSet.Add({ ShaderClass::Vertex, shader, descriptor });
		} else {
			return MakeAndAddVertexShader(shader, descriptor);
		}

		return nullptr;
	}

	RE::BSGraphics::PixelShader* ShaderCache::GetPixelShader(const RE::BSShader& shader,
		uint32_t descriptor)
	{
		auto state = globals::state;
		const bool developerMode = state->IsDeveloperMode();
		if (globals::game::isVR && strcmp(shader.fxpFilename, "OBBOcclusionTesting") == 0)
			// use vanilla shader
			return nullptr;

		if (!((ShaderCache::IsSupportedShader(shader) || developerMode && state->IsShaderEnabled(shader)) && state->enablePShaders)) {
			return nullptr;
		}

		if (!SShaderCache::ResolveImageSpaceDescriptor(shader, descriptor)) {
			return nullptr;
		}

		if (developerMode || activeShaderCaptureFramesRemaining.load(std::memory_order_relaxed) > 0)
			TrackActiveShader(ShaderClass::Pixel, shader, descriptor);

		if (developerMode) {
			auto key = SIE::SShaderCache::GetShaderString(ShaderClass::Pixel, shader, descriptor, true);
			if (blockedKeyIndex != -1 && !blockedKey.empty() && key == blockedKey) {
				if (std::find(blockedIDs.begin(), blockedIDs.end(), descriptor) == blockedIDs.end()) {
					blockedIDs.push_back(descriptor);
					logger::debug("Skipping blocked shader {:X}:{} total: {}", descriptor, blockedKey, blockedIDs.size());
				}
				return nullptr;
			}
		}

		{
			std::lock_guard lockGuard(pixelShadersMutex);
			auto& typeCache = pixelShaders[static_cast<size_t>(shader.shaderType.underlying())];
			auto it = typeCache.find(descriptor);
			if (it != typeCache.end()) {
				return it->second.get();
			}
		}
		if (IsSaveLoadSafeModeActive()) {
			return nullptr;
		}

		if (ShouldUseAsyncCompilation()) {
			compilationSet.Add({ ShaderClass::Pixel, shader, descriptor });
		} else {
			return MakeAndAddPixelShader(shader, descriptor);
		}

		return nullptr;
	}

	RE::BSGraphics::ComputeShader* ShaderCache::GetComputeShader(const RE::BSShader& shader,
		uint32_t descriptor)
	{
		auto state = globals::state;
		const bool developerMode = state->IsDeveloperMode();
		if (!((ShaderCache::IsSupportedShader(shader) || developerMode && state->IsShaderEnabled(shader)) && state->enableCShaders)) {
			return nullptr;
		}

		if (!SShaderCache::ResolveImageSpaceDescriptor(shader, descriptor)) {
			return nullptr;
		}

		if (developerMode || activeShaderCaptureFramesRemaining.load(std::memory_order_relaxed) > 0)
			TrackActiveShader(ShaderClass::Compute, shader, descriptor);

		if (developerMode) {
			auto key = SIE::SShaderCache::GetShaderString(ShaderClass::Compute, shader, descriptor, true);
			if (blockedKeyIndex != -1 && !blockedKey.empty() && key == blockedKey) {
				if (std::find(blockedIDs.begin(), blockedIDs.end(), descriptor) == blockedIDs.end()) {
					blockedIDs.push_back(descriptor);
					logger::debug("Skipping blocked shader {:X}:{} total: {}", descriptor, blockedKey, blockedIDs.size());
				}
				return nullptr;
			}
		}

		{
			std::lock_guard lockGuard(computeShadersMutex);
			auto& typeCache = computeShaders[static_cast<size_t>(shader.shaderType.underlying())];
			auto it = typeCache.find(descriptor);
			if (it != typeCache.end()) {
				return it->second.get();
			}
		}
		if (IsSaveLoadSafeModeActive()) {
			return nullptr;
		}

		if (ShouldUseAsyncCompilation()) {
			compilationSet.Add({ ShaderClass::Compute, shader, descriptor });
		} else {
			return MakeAndAddComputeShader(shader, descriptor);
		}

		return nullptr;
	}

	ShaderCache::~ShaderCache()
	{
		StopFileWatcher();

		// Stop dispatch before closing deferred-persistence intake. This lets
		// compilations already in flight enqueue their completed blobs first.
		managementJthread.request_stop();
		if (managementJthread.joinable())
			managementJthread.join();
		compilationPool.purge();
		if (!compilationPool.wait_for(std::chrono::milliseconds(1000))) {
			logger::info("Shader compilation tasks are still finishing during cache shutdown");
			compilationPool.wait();
		}

		acceptDeferredDiskWrites.store(false, std::memory_order_release);
		// Shader blobs are independent of the savegame. Once all compilation
		// producers are stopped, finish persisting them even if teardown began
		// before the load grace window expired.
		SetSaveLoadDiskPersistenceBlocked(false);
		if (deferredDiskWriterJthread.joinable()) {
			deferredDiskWriterJthread.request_stop();
			deferredDiskWritesCV.notify_all();
			deferredDiskWriterJthread.join();
		}
		{
			std::lock_guard lock{ deferredDiskWritesMutex };
			if (!deferredDiskWrites.empty()) {
				logger::warn(
					"Discarding {} deferred shader-cache writes during shutdown",
					deferredDiskWrites.size());
			}
			deferredDiskWrites.clear();
			deferredManifestFlushPending = false;
		}

		Clear();
		FlushShaderCacheManifest();
	}

	void ShaderCache::Clear()
	{
		compilationSet.BumpGeneration();

		{
			std::unique_lock diskCacheLock{ g_diskCacheMutationMutex };
			AdvanceDiskCacheGeneration();
		}
		{
			std::lock_guard lockGuardV(vertexShadersMutex);
			for (auto& shaders : vertexShaders) {
				for (auto& [id, shader] : shaders) {
					shader->shader->Release();
				}
				shaders.clear();
			}
		}
		{
			std::lock_guard lockGuardP(pixelShadersMutex);
			for (auto& shaders : pixelShaders) {
				for (auto& [id, shader] : shaders) {
					shader->shader->Release();
				}
				shaders.clear();
			}
		}
		{
			std::lock_guard lockGuardC(computeShadersMutex);
			for (auto& shaders : computeShaders) {
				for (auto& [id, shader] : shaders) {
					shader->shader->Release();
				}
				shaders.clear();
			}
		}
		{
			std::unique_lock lockM{ mapMutex };
			shaderMap.clear();
			deferredEvictions.clear();
			deferredEvictionCount.store(0, std::memory_order_relaxed);
		}
		mapCV.notify_all();
		{
			std::unique_lock lockH{ hlslMapMutex };
			hlslToShaderMap.clear();
		}
		compilationSet.Clear();
		globals::deferred->ClearShaderCache();
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature->loaded) {
				feature->ClearShaderCache();
			}
		}
	}

	template <typename ShaderType, typename MutexType>
	void ReleaseShader(ShaderType& shaders,
		MutexType& mutex, RE::BSShader::Type type, uint32_t descriptor)
	{
		std::lock_guard<MutexType> lockGuard(mutex);

		if (static_cast<size_t>(type) < shaders.size()) {
			auto& shaderMap = shaders[static_cast<size_t>(type)];
			auto shaderIt = shaderMap.find(descriptor);
			if (shaderIt != shaderMap.end()) {
				auto& shaderPtr = shaderIt->second;
				if (shaderPtr && shaderPtr->shader) {
					shaderPtr->shader->Release();
				}
				shaderMap.erase(shaderIt);
			}
		}
	}

	void ShaderCache::EvictShader(
		const std::string& a_key,
		RE::BSShader::Type a_type,
		uint32_t a_descriptor,
		ShaderClass a_shaderClass)
	{
		// Never hold mapMutex while acquiring compilationMutex: CompilationSet::Add
		// takes those locks in the opposite order when it checks GetCompletedShader().
		{
			std::unique_lock lockM{ mapMutex };
			shaderMap.erase(a_key);
		}

		switch (a_shaderClass) {
		case ShaderClass::Vertex:
			ReleaseShader(vertexShaders, vertexShadersMutex, a_type, a_descriptor);
			break;
		case ShaderClass::Pixel:
			ReleaseShader(pixelShaders, pixelShadersMutex, a_type, a_descriptor);
			break;
		case ShaderClass::Compute:
			ReleaseShader(computeShaders, computeShadersMutex, a_type, a_descriptor);
			break;
		default:
			logger::warn("Unexpected shader class: {}", static_cast<int>(a_shaderClass));
			break;
		}

		logger::debug("Marking recompile for shader: {}", a_key);
	}

	void ShaderCache::DeleteScopedDiskCacheEntries(const std::vector<std::wstring>& a_diskPaths)
	{
		if (a_diskPaths.empty())
			return;

		// The exclusive disk mutation lock drains any write already in progress. Advancing
		// the generation then prevents deferred or waiting writes from resurrecting a blob
		// selected by this clear. Keep compilation bookkeeping stable while files disappear.
		std::scoped_lock lockD{ compilationSet.compilationMutex, g_diskCacheMutationMutex };
		if (!isDiskCache.load(std::memory_order_relaxed))
			return;

		AdvanceDiskCacheGeneration();
		auto& manifest = GetShaderCacheManifest();
		bool manifestChanged = false;
		for (const auto& diskPath : a_diskPaths) {
			const auto diskPathString = Util::WStringToString(diskPath);
			std::error_code error;
			const bool removed = std::filesystem::remove(diskPath, error);
			if (error) {
				logger::warn("Error while trying to delete {}: {}", diskPathString, error.message());
			} else if (removed) {
				logger::debug("Deleted {}", diskPathString);
			}

			if (manifest.Erase(GetManifestKey(diskPath)))
				manifestChanged = true;
		}

		if (manifestChanged)
			FlushShaderCacheManifestLocked();
	}

	bool ShaderCache::Clear(const std::string& a_path)
	{
		std::string lowerFilePath = Util::FixFilePath(a_path);

		// Step 1: Lock hlslMapMutex to find and copy the relevant entries
		std::set<hlslRecord> entries;
		{
			std::unique_lock lockH{ hlslMapMutex };
			auto it = hlslToShaderMap.find(lowerFilePath);

			if (it == hlslToShaderMap.end()) {
				return false;
			}

			entries = it->second;  // Copy the entries
			hlslToShaderMap.erase(it);
		}

		std::vector<hlslRecord> immediateEvictions;
		immediateEvictions.reserve(entries.size());
		// A pending entry keeps its disk blob until the in-flight reader resolves.
		for (auto& entry : entries) {
			if (TryDeferEviction(entry))
				continue;
			EvictShader(entry.key, entry.type, entry.descriptor, entry.shaderClass);
			immediateEvictions.push_back(entry);
		}

		if (!entries.empty()) {
			{
				std::scoped_lock lockD{
					compilationSet.compilationMutex,
					g_diskCacheMutationMutex
				};
				AdvanceDiskCacheGeneration();

				auto& manifest = GetShaderCacheManifest();
				bool manifestChanged = false;
				for (const auto& entry : immediateEvictions) {
					const auto& filePath = entry.diskPath;
					const auto filePathString = Util::WStringToString(filePath);
					std::error_code error;
					const bool removed = std::filesystem::remove(filePath, error);
					if (error) {
						logger::warn(
							"Error while trying to delete {}: {}",
							filePathString,
							error.message());
					} else if (removed) {
						logger::debug("Deleted {}", filePathString);
					}

					if (manifest.Erase(GetManifestKey(filePath)))
						manifestChanged = true;
				}
				if (manifestChanged)
					FlushShaderCacheManifestLocked();
			}

			logger::debug("Marked {} entries for recompile due to change to {}", entries.size(), a_path);
			compilationSet.Clear();
		}

		return true;
	}

	void ShaderCache::Clear(RE::BSShader::Type a_type)
	{
		compilationSet.BumpGeneration();

		logger::debug("Clearing cache for {}", magic_enum::enum_name(a_type));
		std::lock_guard lockGuardV(vertexShadersMutex);
		{
			for (auto& [id, shader] : vertexShaders[static_cast<size_t>(a_type)]) {
				shader->shader->Release();
			}
			vertexShaders[static_cast<size_t>(a_type)].clear();
		}
		std::lock_guard lockGuardP(pixelShadersMutex);
		{
			for (auto& [id, shader] : pixelShaders[static_cast<size_t>(a_type)]) {
				shader->shader->Release();
			}
			pixelShaders[static_cast<size_t>(a_type)].clear();
		}
		std::lock_guard lockGuardC(computeShadersMutex);
		{
			for (auto& [id, shader] : computeShaders[static_cast<size_t>(a_type)]) {
				shader->shader->Release();
			}
			computeShaders[static_cast<size_t>(a_type)].clear();
		}
		ClearShaderMap(a_type);
		compilationSet.Clear();
	}

	void ShaderCache::RequestClear()
	{
		pendingClear.store(true, std::memory_order_release);
	}

	void ShaderCache::ProcessPendingClear()
	{
		if (pendingClear.exchange(false, std::memory_order_acq_rel)) {
			Clear();
		}
	}

	namespace
	{
		struct ShaderCacheResultTraits
		{
			static bool IsPending(const ShaderCacheResult& a_entry) { return a_entry.status == ShaderCompilationTask::Status::Pending; }
			static bool IsCompleted(const ShaderCacheResult& a_entry) { return a_entry.status == ShaderCompilationTask::Status::Completed; }
			static bool HasPayload(const ShaderCacheResult& a_entry) { return a_entry.blob != nullptr; }
			static uint64_t GetGeneration(const ShaderCacheResult& a_entry) { return a_entry.generation; }
		};
	}

	bool ShaderCache::AddCompletedShader(
		ShaderClass shaderClass,
		const RE::BSShader& shader,
		uint32_t descriptor,
		ID3DBlob* a_blob,
		const std::wstring& a_diskPath,
		const Util::ContentHash::Hash128& a_compileStateDigest,
		bool fromDisk,
		std::optional<uint64_t> a_taskGeneration)
	{
		auto key = SIE::SShaderCache::GetShaderString(shaderClass, shader, descriptor, true);
		auto keyWithDescriptor = SIE::SShaderCache::GetShaderString(shaderClass, shader, descriptor, false);

		Util::GenerationClaim::PublishOutcome outcome;
		uint64_t liveGeneration = 0;
		{
			std::unique_lock lockM{ mapMutex };
			liveGeneration = compilationSet.generation.load(std::memory_order_acquire);
			outcome = Util::GenerationClaim::TryPublish<ShaderCacheResultTraits>(
				shaderMap,
				key,
				a_taskGeneration,
				liveGeneration,
				a_blob != nullptr,
				[&](uint64_t a_generation, bool a_success) {
					return ShaderCacheResult{
						a_blob,
						a_success ? ShaderCompilationTask::Status::Completed : ShaderCompilationTask::Status::Failed,
						system_clock::now(),
						fromDisk,
						a_generation
					};
				});
		}

		if (outcome != Util::GenerationClaim::PublishOutcome::Published) {
			if (outcome == Util::GenerationClaim::PublishOutcome::RejectedStaleCleanedPending) {
				mapCV.notify_all();
			}
			logger::debug(
				"Discarding stale-generation shader (task gen {}, current {}): {}",
				a_taskGeneration.value_or(liveGeneration),
				liveGeneration,
				keyWithDescriptor);
			ApplyDeferredEviction(key);
			return false;
		}

		const auto status = a_blob ? ShaderCompilationTask::Status::Completed : ShaderCompilationTask::Status::Failed;
		logger::debug("Adding {} shader to map: {}", magic_enum::enum_name(status), keyWithDescriptor);
		mapCV.notify_all();  // wake threads waiting on a Pending→Completed/Failed transition
		const std::wstring path = SIE::SShaderCache::GetShaderPath(
			shader.shaderType == RE::BSShader::Type::ImageSpace ?
				static_cast<const RE::BSImagespaceShader&>(shader).originalShaderName :
				shader.fxpFilename);
		auto pathString = Util::WStringToString(path);
		// Always create or update an hlsl->shader record so failing compiles are
		// trackable and can be invalidated by the file watcher. This allows
		// Clear(path) to find failed shaders and mark them for recompilation.
		std::string lowerFilePath = Util::FixFilePath(pathString);
		{
			std::unique_lock lockH{ hlslMapMutex };
			auto it = hlslToShaderMap.find(lowerFilePath);
			hlslRecord newRecord{
				key,
				shader.shaderType.get(),
				descriptor,
				shaderClass,
				a_diskPath,
				a_compileStateDigest,
				CaptureGlobalCompileState().digest,
				globals::state && globals::state->IsDeveloperMode()
			};

			if (it != hlslToShaderMap.end()) {
				auto& entries = it->second;

				// Find and remove existing record with the same key
				auto existingRecord = std::find_if(entries.begin(), entries.end(),
					[&](const hlslRecord& r) { return r.key == key; });

				if (existingRecord != entries.end()) {
					entries.erase(existingRecord);  // Remove the old record
				}

				// Insert the new or updated record
				entries.insert(newRecord);
			} else {
				// Create a new entry in hlslToShaderMap for this file path
				hlslToShaderMap.emplace(lowerFilePath, std::set<hlslRecord>{ newRecord });
			}
		}

		const bool evicted = ApplyDeferredEviction(key);

		return a_blob != nullptr && !evicted;
	}

	std::pair<ShaderCache::ClaimResult, ID3DBlob*> ShaderCache::ClaimCompilation(
		const std::string& key,
		std::optional<uint64_t> a_taskGeneration)
	{
		std::unique_lock lockM{ mapMutex };

		for (;;) {
			const auto liveGeneration = compilationSet.generation.load(std::memory_order_acquire);
			if (a_taskGeneration && *a_taskGeneration != liveGeneration) {
				logger::debug(
					"Discarding stale-generation shader claim (task gen {}, current {}): {}",
					*a_taskGeneration,
					liveGeneration,
					key);
				return { ClaimResult::RejectedStale, nullptr };
			}
			if (deferredEvictions.contains(key)) {
				logger::debug("Shader eviction in progress, waiting: {}", key);
				mapCV.wait(lockM);
				continue;
			}
			using Util::GenerationClaim::ClaimOutcome;
			auto [outcome, entry] = Util::GenerationClaim::TryClaim<ShaderCacheResultTraits>(
				shaderMap,
				key,
				a_taskGeneration,
				liveGeneration,
				[](uint64_t a_generation) {
					return ShaderCacheResult{
						nullptr,
						ShaderCompilationTask::Status::Pending,
						system_clock::now(),
						false,
						a_generation
					};
				});

			if (outcome == ClaimOutcome::CacheHit) {
				logger::debug("Shader already compiled; using cache: {}", key);
				return { ClaimResult::CacheHit, entry->second.blob };
			}
			if (outcome == ClaimOutcome::MustWait) {
				logger::debug("Shader compilation in progress, waiting: {}", key);
				mapCV.wait(lockM);
				continue;
			}
			if (outcome == ClaimOutcome::RejectedStale) {
				return { ClaimResult::RejectedStale, nullptr };
			}
			return { ClaimResult::Claimed, nullptr };
		}
	}

	void ShaderCache::ResolvePendingFailure(
		const std::string& key,
		std::optional<uint64_t> a_taskGeneration)
	{
		auto outcome = Util::GenerationClaim::PublishOutcome::RejectedStale;
		{
			std::unique_lock lockM{ mapMutex };
			auto it = shaderMap.find(key);
			if (it != shaderMap.end() && it->second.status == ShaderCompilationTask::Status::Pending) {
				outcome = Util::GenerationClaim::TryPublish<ShaderCacheResultTraits>(
					shaderMap,
					key,
					a_taskGeneration,
					compilationSet.generation.load(std::memory_order_acquire),
					false,
					[](uint64_t a_generation, bool) {
						return ShaderCacheResult{
							nullptr,
							ShaderCompilationTask::Status::Failed,
							system_clock::now(),
							false,
							a_generation
						};
					});
			}
		}
		if (outcome != Util::GenerationClaim::PublishOutcome::RejectedStale) {
			mapCV.notify_all();
		}
		ApplyDeferredEviction(key);
	}

	ID3DBlob* ShaderCache::GetCompletedShader(const std::string& a_key)
	{
		std::string type = SIE::SShaderCache::GetTypeFromShaderString(a_key);
		UpdateShaderModifiedTime(type);
		std::scoped_lock lockM{ mapMutex };
		if (!shaderMap.empty() && shaderMap.contains(a_key)) {
			if (ShaderModifiedSince(type, shaderMap.at(a_key).compileTime)) {
				logger::debug("Shader {} compiled {} before changes at {}",
					a_key,
					std::format("{:%H:%M:%S}", shaderMap.at(a_key).compileTime),
					std::format("{:%H:%M:%S}", GetModifiedShaderMapTime(type)));
				return nullptr;
			}
			auto status = shaderMap.at(a_key).status;
			if (status != ShaderCompilationTask::Status::Pending)
				return shaderMap.at(a_key).blob;
		}
		return nullptr;
	}

	ID3DBlob* ShaderCache::GetCompletedShader(ShaderClass shaderClass, const RE::BSShader& shader,
		uint32_t descriptor)
	{
		auto key = SIE::SShaderCache::GetShaderString(shaderClass, shader, descriptor, true);
		return GetCompletedShader(key);
	}

	ID3DBlob* ShaderCache::GetCompletedShader(const ShaderCompilationTask& a_task)
	{
		auto key = a_task.GetString();
		return GetCompletedShader(key);
	}

	bool ShaderCache::IsShaderLoadedFromDisk(const std::string& a_key)
	{
		std::scoped_lock lockM{ mapMutex };
		auto it = shaderMap.find(a_key);
		if (it != shaderMap.end())
			return it->second.loadedFromDisk;
		return false;
	}

	ShaderCompilationTask::Status ShaderCache::GetShaderStatus(const std::string& a_key)
	{
		std::scoped_lock lockM{ mapMutex };
		if (!shaderMap.empty() && shaderMap.contains(a_key)) {
			return shaderMap.at(a_key).status;
		}
		return ShaderCompilationTask::Status::Pending;
	}

	std::string ShaderCache::GetShaderStatsString(bool a_timeOnly, bool a_elapsedOnly)
	{
		return compilationSet.GetStatsString(a_timeOnly, a_elapsedOnly);
	}

	inline bool ShaderCache::IsShaderSourceAvailable(const RE::BSShader& shader)
	{
		const std::wstring path = SIE::SShaderCache::GetShaderPath(shader.fxpFilename);

		std::string strPath;
		std::transform(path.begin(), path.end(), std::back_inserter(strPath), [](wchar_t c) {
			return (char)c;
		});
		try {
			return std::filesystem::exists(path);
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Error accessing {} : {}", strPath, e.what());
			return false;
		}
	}

	bool ShaderCache::IsCompiling()
	{
		return compilationSet.totalTasks && compilationSet.completedTasks + compilationSet.failedTasks < compilationSet.totalTasks;
	}

	void ShaderCache::TryCompleteStartupCompilationPhase()
	{
		if (!ShaderCompilationSchedulingPolicy::ShouldCompleteStartupCompilation(
				menuLoaded.load(std::memory_order_acquire),
				IsCompiling()))
			return;

		if (!startupCompilationComplete.exchange(true, std::memory_order_acq_rel))
			logger::info("Shader compiler scheduling transitioned from startup-cooperative to in-game normal priority");
	}

	void ShaderCache::StopCompilation()
	{
		if (IsCompiling()) {
			logger::info("Stopping {} remaining shader compilation tasks", compilationSet.totalTasks - compilationSet.completedTasks - compilationSet.failedTasks);
		}
		ssource.request_stop();            // signals any legacy stop_token users
		managementJthread.request_stop();  // stops management thread + in-flight compilations
		compilationSet.Clear();
	}

	bool ShaderCache::IsEnabled() const
	{
		return isEnabled.load(std::memory_order_acquire);
	}

	bool ShaderCache::IsEnableRequested() const
	{
		return enableRequested.load(std::memory_order_acquire);
	}

	void ShaderCache::SetEnabled(bool value)
	{
		if (value) {
			const bool enableAlreadyRequested =
				enableRequested.exchange(true, std::memory_order_acq_rel);
			pendingDisableAfterVRNativeRestore.store(false, std::memory_order_release);
			isEnabled.store(true, std::memory_order_release);

			if (globals::game::isVR) {
				auto& upscaling = globals::features::upscaling;
				if (ShaderCacheDisablePolicy::ShouldRequestRelatchOnEnable({
						.enableAlreadyRequested = enableAlreadyRequested,
						.vrRenderScaleRequested = upscaling.IsRenderScaleModeRequested(),
						.vrRenderScaleLatched = upscaling.IsVRRenderScaleModeLatched(),
					})) {
					upscaling.RequestPerfModeRenderTargetRecreate(
						"custom shaders re-enabled",
						Upscaling::VRUpscalingTransitionOrigin::CSMenu);
				}
			}
			return;
		}

		const bool vrRenderScaleRelevant = globals::game::isVR &&
		                                   (globals::features::upscaling.IsRenderScaleModeRequested() ||
											   globals::features::upscaling.IsVRRenderScaleModeLatched() ||
											   globals::features::upscaling.GetVRRenderScaleModeStatus() ==
												   Upscaling::VRRenderScaleStatus::PendingRelatch);
		enableRequested.store(false, std::memory_order_release);

		const auto disableAction = ShaderCacheDisablePolicy::ResolveDisableRequest({
			.shaderCacheEnabled = IsEnabled(),
			.vrNativeRestoreRequired = vrRenderScaleRelevant,
		});
		if (disableAction ==
			ShaderCacheDisablePolicy::DisableRequestAction::DeferUntilNativeRestore) {
			pendingDisableAfterVRNativeRestore.store(true, std::memory_order_release);
			globals::features::upscaling.RequestPerfModeRenderTargetRecreate(
				"custom shaders disabled; restore native VR targets",
				Upscaling::VRUpscalingTransitionOrigin::CSMenu);
			logger::info("Deferring custom shader disable until native VR render targets are restored");
			return;
		}

		pendingDisableAfterVRNativeRestore.store(false, std::memory_order_release);
		isEnabled.store(false, std::memory_order_release);
	}

	void ShaderCache::ServicePendingDisable()
	{
		// Status resolution crosses render-scale controller state; stable frames
		// must stop at the atomic pending flag.
		const bool pendingDisable =
			pendingDisableAfterVRNativeRestore.load(std::memory_order_acquire);
		if (!pendingDisable)
			return;

		const bool enableStillRequested = IsEnableRequested();
		const auto action = ShaderCacheDisablePolicy::ResolvePendingDisable({
			.pendingDisable = pendingDisable,
			.enableRequested = enableStillRequested,
			.nativeTargetsRestored = false,
		});
		if (action == ShaderCacheDisablePolicy::PendingDisableAction::Cancel) {
			pendingDisableAfterVRNativeRestore.store(false, std::memory_order_release);
			return;
		}

		auto& upscaling = globals::features::upscaling;
		if (upscaling.GetVRRenderScaleModeStatus() !=
			Upscaling::VRRenderScaleStatus::Disabled) {
			return;
		}

		pendingDisableAfterVRNativeRestore.store(false, std::memory_order_release);
		isEnabled.store(false, std::memory_order_release);
		logger::info("Native VR render targets restored; custom shaders disabled");
	}

	bool ShaderCache::IsAsync() const
	{
		return isAsync;
	}

	void ShaderCache::SetAsync(bool value)
	{
		isAsync = value;
	}

	bool ShaderCache::ShouldUseAsyncCompilation() const
	{
		return isAsync && !IsSaveLoadSafeModeActive();
	}

	bool ShaderCache::IsDump() const
	{
		return isDump;
	}

	void ShaderCache::SetDump(bool value)
	{
		isDump = value;
	}

	bool ShaderCache::IsDiskCache() const
	{
		return isDiskCache.load(std::memory_order_acquire);
	}

	void ShaderCache::SetDiskCache(bool value)
	{
		std::unique_lock lock{ g_diskCacheMutationMutex };
		if (isDiskCache.load(std::memory_order_relaxed) == value)
			return;

		isDiskCache.store(value, std::memory_order_release);
		if (!value)
			AdvanceDiskCacheGeneration();
	}

	void ShaderCache::PersistCompiledShaderBlob(
		ID3DBlob* a_shaderBlob,
		bool a_developerMode,
		const std::wstring& a_diskPath,
		const std::filesystem::path& a_shaderPath,
		const Util::ContentHash::Hash128& a_compileStateDigest,
		const Util::ContentHash::Hash128& a_packCompileStateDigest,
		uint64_t a_diskCacheGeneration)
	{
		if (!a_shaderBlob ||
			!acceptDeferredDiskWrites.load(std::memory_order_acquire) ||
			!IsDiskCacheActive())
			return;

		const bool managedPack = GetShaderPackStore(a_developerMode) != nullptr;
		if (!managedPack && !saveLoadDiskPersistenceBlocked.load(std::memory_order_acquire)) {
			SaveShaderBlobToDisk(
				a_shaderBlob,
				a_developerMode,
				a_diskPath,
				a_shaderPath,
				a_compileStateDigest,
				a_packCompileStateDigest,
				a_diskCacheGeneration);
			return;
		}

		DeferredDiskWrite deferredWrite;
		a_shaderBlob->AddRef();
		deferredWrite.shaderBlob.Attach(a_shaderBlob);
		deferredWrite.developerMode = a_developerMode;
		deferredWrite.diskPath = a_diskPath;
		deferredWrite.shaderPath = a_shaderPath;
		deferredWrite.compileStateDigest = a_compileStateDigest;
		deferredWrite.packCompileStateDigest = a_packCompileStateDigest;
		deferredWrite.diskCacheGeneration = a_diskCacheGeneration;
		{
			std::lock_guard lock{ deferredDiskWritesMutex };
			if (!acceptDeferredDiskWrites.load(std::memory_order_acquire))
				return;

			const auto existing = std::find_if(
				deferredDiskWrites.begin(),
				deferredDiskWrites.end(),
				[&](const DeferredDiskWrite& a_write) {
					return a_write.diskCacheGeneration == a_diskCacheGeneration &&
				           a_write.developerMode == a_developerMode &&
				           a_write.diskPath == a_diskPath;
				});
			if (existing != deferredDiskWrites.end())
				*existing = std::move(deferredWrite);
			else if (deferredDiskWrites.size() < kMaximumDeferredDiskWrites)
				deferredDiskWrites.push_back(std::move(deferredWrite));
			else if (!deferredDiskWriteLimitReported.exchange(true, std::memory_order_acq_rel))
				logger::warn(
					"Shader-cache persistence queue reached its {}-record bound; additional records remain usable in memory but will not be persisted",
					kMaximumDeferredDiskWrites);
		}
		deferredDiskWritesCV.notify_one();
	}

	void ShaderCache::SetSaveLoadDiskPersistenceBlocked(bool a_blocked)
	{
		const bool wasBlocked =
			saveLoadDiskPersistenceBlocked.exchange(a_blocked, std::memory_order_acq_rel);
		if (a_blocked || !wasBlocked)
			return;

		// Synchronize with the writer's wait transition so the falling edge cannot
		// be lost between evaluating its predicate and going to sleep.
		{
			std::lock_guard lock{ deferredDiskWritesMutex };
		}
		deferredDiskWritesCV.notify_all();
	}

	bool ShaderCache::IsSkipUnchangedShaders() const
	{
		return isSkipUnchangedShaders;
	}

	void ShaderCache::SetSkipUnchangedShaders(bool value)
	{
		isSkipUnchangedShaders = value;
	}

	static const std::filesystem::path& DiskCachePath()
	{
		static const std::filesystem::path path{ L"Data/ShaderCache" };
		return path;
	}

	static const std::filesystem::path& PreviousDiskCachePath()
	{
		static const std::filesystem::path path{ L"Data/ShaderCache.Previous" };
		return path;
	}

	static const std::filesystem::path& SwapDiskCachePath()
	{
		static const std::filesystem::path path{ L"Data/ShaderCache.Swap" };
		return path;
	}

	static bool PathExists(const std::filesystem::path& path)
	{
		std::error_code ec;
		const bool exists = std::filesystem::exists(path, ec);
		return exists && !ec;
	}

	static bool HasDiskCacheInfo(const std::filesystem::path& cachePath)
	{
		return PathExists(cachePath / L"Info.ini");
	}

	static bool RemovePath(const std::filesystem::path& path, std::string_view label)
	{
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
		if (ec) {
			logger::error("Failed to remove {} shader cache path {}: {}", label, Util::WStringToString(path.wstring()), ec.message());
			return false;
		}
		return true;
	}

	static bool MoveDirectory(const std::filesystem::path& source, const std::filesystem::path& destination, std::string_view label)
	{
		std::error_code ec;
		std::filesystem::rename(source, destination, ec);
		if (!ec)
			return true;

		logger::warn("Failed to move {} shader cache from {} to {}: {}", label,
			Util::WStringToString(source.wstring()), Util::WStringToString(destination.wstring()), ec.message());
		return false;
	}

	static bool LoadDiskCacheInfo(const std::filesystem::path& cachePath, CSimpleIniA& ini)
	{
		ini.SetUnicode();
		if (ini.LoadFile((cachePath / L"Info.ini").c_str()) < 0)
			return false;
		return true;
	}

	static std::vector<Util::CacheInvalidation::FeatureState> GetCurrentFeatureStates()
	{
		std::vector<Util::CacheInvalidation::FeatureState> featureStates;
		for (auto* feature : Feature::GetFeatureList()) {
			featureStates.push_back({ feature->GetShortName(), feature->GetDisplayName(), feature->loaded,
				feature->version, std::string(feature->GetShaderDefineName()),
				std::string(feature->GetShaderCacheAbiVersion()) });
		}
		return featureStates;
	}

	static std::map<std::string, Util::CacheInvalidation::CacheIniEntry> GetCacheEntries(
		const CSimpleIniA& ini,
		const std::vector<Util::CacheInvalidation::FeatureState>& featureStates)
	{
		std::map<std::string, Util::CacheInvalidation::CacheIniEntry> cacheEntries;
		for (const auto& featureState : featureStates) {
			Util::CacheInvalidation::CacheIniEntry entry;
			entry.enabled = ini.GetBoolValue(featureState.shortName.c_str(), "Enabled", false);
			if (auto version = ini.GetValue(featureState.shortName.c_str(), "Version"))
				entry.version = version;
			if (auto shaderAbi = ini.GetValue(featureState.shortName.c_str(), "ShaderCacheABI"))
				entry.shaderAbi = shaderAbi;
			cacheEntries[featureState.shortName] = entry;
		}
		return cacheEntries;
	}

	static std::vector<Util::CacheInvalidation::CacheMismatch> ClassifyCacheInfo(const CSimpleIniA& ini)
	{
		std::optional<std::string> cachedPluginVersion;
		if (auto pluginVersion = ini.GetValue("Cache", "PluginVersion"))
			cachedPluginVersion = pluginVersion;
		std::optional<std::string> cachedShaderAbi;
		if (auto shaderAbi = ini.GetValue("Cache", "ShaderCacheABI"))
			cachedShaderAbi = shaderAbi;
		std::optional<std::string> cachedShaderCompiler;
		if (auto shaderCompiler = ini.GetValue("Cache", "ShaderCompilerIdentity"))
			cachedShaderCompiler = shaderCompiler;
		// Packed DXBC is keyed by source, compile-state ABI, and external
		// compatibility requirements. The local compiler producer is useful
		// provenance for loose caches, but it is not a packed-bytecode validity
		// requirement and must not invalidate a shipped portable pack.
		if (ManagedShaderPacksAvailable())
			cachedShaderCompiler = BuildProvenance::GetShaderCompilerIdentity();

		const auto featureStates = GetCurrentFeatureStates();
		const auto cacheEntries = GetCacheEntries(ini, featureStates);
		return Util::CacheInvalidation::ClassifyMismatches(
			std::string{ Plugin::VERSION_LABEL }, cachedPluginVersion,
			std::string{ BuildProvenance::GetShaderCacheAbiId() }, cachedShaderAbi,
			BuildProvenance::GetShaderCompilerIdentity(), cachedShaderCompiler,
			featureStates, cacheEntries);
	}

	static std::vector<std::string> GetDefinesForMismatches(
		const std::vector<Util::CacheInvalidation::CacheMismatch>& mismatches,
		Util::CacheInvalidation::CacheMismatch::Kind kind)
	{
		const auto featureStates = GetCurrentFeatureStates();
		std::vector<std::string> defines;
		for (const auto& mismatch : mismatches) {
			if (mismatch.kind != kind)
				continue;

			const auto stateIt = std::find_if(featureStates.begin(), featureStates.end(),
				[&](const Util::CacheInvalidation::FeatureState& featureState) {
					return featureState.shortName == mismatch.shortName;
				});
			if (stateIt != featureStates.end())
				defines.push_back(stateIt->define);
		}
		return defines;
	}

	static bool OnlyEnabledFlips(const std::vector<Util::CacheInvalidation::CacheMismatch>& mismatches)
	{
		return std::all_of(mismatches.begin(), mismatches.end(),
			[](const Util::CacheInvalidation::CacheMismatch& mismatch) {
				return mismatch.kind == Util::CacheInvalidation::CacheMismatch::Kind::EnabledFlip;
			});
	}

	static bool HasMissingOrFailedFeature(
		const std::vector<Util::CacheInvalidation::CacheMismatch>& mismatches,
		bool allowExpectedRuntimeDisable = false)
	{
		return std::any_of(mismatches.begin(), mismatches.end(),
			[allowExpectedRuntimeDisable](const Util::CacheInvalidation::CacheMismatch& mismatch) {
				if (mismatch.kind != Util::CacheInvalidation::CacheMismatch::Kind::EnabledFlip || mismatch.nowPresent)
					return false;

				if (allowExpectedRuntimeDisable) {
					const auto& features = Feature::GetFeatureList();
					const auto featureIt = std::ranges::find_if(features,
						[&](Feature* feature) { return feature->GetShortName() == mismatch.shortName; });
					if (featureIt != features.end() && (*featureIt)->IsRuntimeDisabledByMissingDependency())
						return false;
				}

				auto* state = globals::state;
				return !state || !state->IsFeatureDisabled(mismatch.shortName);
			});
	}

	static bool AreRestorablePreviousCacheMismatches(const std::vector<Util::CacheInvalidation::CacheMismatch>& mismatches)
	{
		return !mismatches.empty() && OnlyEnabledFlips(mismatches) && !HasMissingOrFailedFeature(mismatches);
	}

	enum class PreviousCacheInfoValidation
	{
		RequireReadableInfo,
		ValidatedBeforeMove,
	};

	static bool SetPreviousCacheRestoreCandidate(
		std::vector<Util::CacheInvalidation::CacheMismatch> mismatches,
		bool& previousDiskCacheAvailable,
		std::vector<Util::CacheInvalidation::CacheMismatch>& previousCacheMismatches,
		PreviousCacheInfoValidation a_infoValidation)
	{
		if (!AreRestorablePreviousCacheMismatches(mismatches)) {
			logger::info("Previous shader cache is not a compatible feature-toggle restore candidate");
			return false;
		}

		if (a_infoValidation == PreviousCacheInfoValidation::RequireReadableInfo) {
			if (!HasDiskCacheInfo(PreviousDiskCachePath())) {
				logger::info("Previous shader cache restore candidate rejected because cache info is missing");
				return false;
			}

			CSimpleIniA previousInfo;
			if (!LoadDiskCacheInfo(PreviousDiskCachePath(), previousInfo)) {
				logger::warn("Previous shader cache restore candidate rejected because cache info could not be read");
				return false;
			}
		}

		// ValidateDiskCache already loaded the active Info.ini before moving that
		// cache into the rollback slot. Avoid reopening the destination immediately
		// in that path because MO2 may not expose a moved virtual path synchronously.
		// Later discovery and restore paths still require readable rollback metadata.
		previousCacheMismatches = std::move(mismatches);
		previousDiskCacheAvailable = true;
		return true;
	}

	static bool PartialInvalidation(const std::vector<std::string>& defines)
	{
		std::unique_lock diskCacheLock{ g_diskCacheMutationMutex };
		AdvanceDiskCacheGeneration();

		size_t deleted = 0;
		size_t kept = 0;
		const auto plan = Util::CacheInvalidation::PlanCacheFamilies(
			DiskCachePath(), L"Data/Shaders", defines);
		const bool ok = plan.has_value() && Util::CacheInvalidation::ApplyCacheFamilyPlan(
												*plan, &deleted, &kept);
		if (ok) {
			auto& manifest = GetShaderCacheManifest();
			const auto removedEntries = manifest.PruneIf([](const std::string& a_key) {
				const auto relativePath = std::filesystem::u8path(a_key);
				if (relativePath.empty() ||
					relativePath.is_absolute() ||
					relativePath.has_root_name() ||
					relativePath.has_root_directory())
					return true;
				for (const auto& component : relativePath) {
					if (component == L"..")
						return true;
				}

				std::error_code error;
				return !std::filesystem::is_regular_file(DiskCachePath() / relativePath, error) || error;
			});
			if (removedEntries > 0)
				FlushShaderCacheManifestLocked();
			std::string affectedFamilies;
			for (const auto& family : plan->affected) {
				if (!affectedFamilies.empty())
					affectedFamilies += ',';
				affectedFamilies += Util::WStringToString(family.filename().wstring());
			}
			std::string unclassifiedFamilies;
			for (const auto& family : plan->unclassified) {
				if (!unclassifiedFamilies.empty())
					unclassifiedFamilies += ',';
				unclassifiedFamilies += Util::WStringToString(family.filename().wstring());
			}
			logger::info(
				"[ShaderCacheAction] action=partial-invalidation result=success affectedFamilies={} unclassifiedFamilies={} deletedFamilies={} retainedFamilies={}",
				affectedFamilies,
				unclassifiedFamilies,
				deleted,
				kept);
		} else {
			logger::warn(
				"[ShaderCacheAction] action=partial-invalidation result=unavailable reason=dependency-plan-failed fallback=full-wipe");
		}
		return ok;
	}

	void ShaderCache::DeleteActiveDiskCache()
	{
		std::scoped_lock lock{ compilationSet.compilationMutex, g_diskCacheMutationMutex };
		AdvanceDiskCacheGeneration();
		if (ManagedShaderPacksAvailable()) {
			const bool reset = ResetManagedShaderPacks();
			const bool removedLoose = RemoveLooseDiskCacheEntries();
			DiscardShaderCacheManifestLocked();
			logger::info("Cleared managed shader packs in place (reset={}, legacyCleanup={})", reset, removedLoose);
			return;
		}
		if (RemovePath(DiskCachePath(), "active")) {
			DiscardShaderCacheManifestLocked();
			logger::info("Deleted active disk cache");
		}
	}

	void ShaderCache::DeleteDiskCache()
	{
		std::scoped_lock lock{ compilationSet.compilationMutex, g_diskCacheMutationMutex };
		AdvanceDiskCacheGeneration();
		if (ManagedShaderPacksAvailable()) {
			const bool reset = ResetManagedShaderPacks();
			const bool removedLoose = RemoveLooseDiskCacheEntries();
			const bool removedPrevious = RemovePath(PreviousDiskCachePath(), "previous");
			const bool removedSwap = RemovePath(SwapDiskCachePath(), "temporary");
			DiscardShaderCacheManifestLocked();
			logger::info(
				"Cleared managed shader cache in place (reset={}, legacyCleanup={}, previousCleanup={}, swapCleanup={})",
				reset, removedLoose, removedPrevious, removedSwap);
			diskCacheHeld = false;
			featureSetChanged = false;
			featureSetRevertPending = false;
			featureSetCacheBackedUp = false;
			featureSetCacheSelectivelySeeded = false;
			previousDiskCacheAvailable = false;
			cacheMismatches.clear();
			previousCacheMismatches.clear();
			heldMismatchDefines.clear();
			return;
		}
		const bool removedActive = RemovePath(DiskCachePath(), "active");
		const bool removedPrevious = RemovePath(PreviousDiskCachePath(), "previous");
		const bool removedSwap = RemovePath(SwapDiskCachePath(), "temporary");

		if (removedActive)
			DiscardShaderCacheManifestLocked();

		if (removedActive && removedPrevious && removedSwap)
			logger::info("Deleted disk cache and rollback cache");

		diskCacheHeld = false;
		featureSetChanged = false;
		featureSetRevertPending = false;
		featureSetCacheBackedUp = false;
		featureSetCacheSelectivelySeeded = false;
		previousDiskCacheAvailable = false;
		cacheMismatches.clear();
		previousCacheMismatches.clear();
		heldMismatchDefines.clear();
	}

	static std::string CacheFamilyNames(const std::vector<std::filesystem::path>& paths)
	{
		std::string result;
		for (const auto& path : paths) {
			if (!result.empty())
				result += ',';
			result += Util::WStringToString(path.filename().wstring());
		}
		return result;
	}

	static bool CopyCacheFamily(
		const std::filesystem::path& source,
		const std::filesystem::path& destination,
		size_t& copiedFiles,
		uintmax_t& copiedBytes)
	{
		std::error_code error;
		std::filesystem::create_directories(destination, error);
		if (error)
			return false;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(source, error)) {
			if (error)
				return false;
			const auto relative = std::filesystem::relative(entry.path(), source, error);
			if (error)
				return false;
			const auto target = destination / relative;
			if (entry.is_directory()) {
				std::filesystem::create_directories(target, error);
				if (error)
					return false;
			} else if (entry.is_regular_file()) {
				const auto bytes = entry.file_size(error);
				if (error)
					return false;
				std::filesystem::copy_file(
					entry.path(), target,
					std::filesystem::copy_options::overwrite_existing,
					error);
				if (error)
					return false;
				++copiedFiles;
				copiedBytes += bytes;
			}
		}
		return !error;
	}

	static bool SeedActiveCacheFromPrevious(
		const std::vector<std::string>& changedDefines,
		bool& outRetainedAnyFamily)
	{
		outRetainedAnyFamily = false;
		const auto plan = Util::CacheInvalidation::PlanCacheFamilies(
			PreviousDiskCachePath(), L"Data/Shaders", changedDefines);
		if (!plan) {
			logger::warn(
				"[ShaderCacheAction] action=selective-seed result=unavailable reason=dependency-plan-failed fallback=empty-active-cache");
			return false;
		}

		size_t copiedFiles = 0;
		uintmax_t copiedBytes = 0;
		for (const auto& family : plan->retained) {
			if (!CopyCacheFamily(
					family,
					DiskCachePath() / family.filename(),
					copiedFiles,
					copiedBytes)) {
				logger::warn(
					"[ShaderCacheAction] action=selective-seed result=failed family={} fallback=empty-active-cache",
					Util::WStringToString(family.filename().wstring()));
				return false;
			}
		}

		std::error_code error;
		const auto previousManifest = PreviousDiskCachePath() / L"Manifest.json";
		if (std::filesystem::is_regular_file(previousManifest, error) && !error) {
			std::filesystem::copy_file(
				previousManifest,
				DiskCachePath() / L"Manifest.json",
				std::filesystem::copy_options::overwrite_existing,
				error);
			if (error) {
				logger::warn(
					"[ShaderCacheAction] action=selective-seed result=failed reason=manifest-copy error={} fallback=empty-active-cache",
					error.message());
				return false;
			}
		}

		ReloadShaderCacheManifestLocked();
		auto& manifest = GetShaderCacheManifest();
		manifest.PruneIf([](const std::string& key) {
			std::error_code entryError;
			return !std::filesystem::is_regular_file(DiskCachePath() / std::filesystem::u8path(key), entryError) || entryError;
		});
		FlushShaderCacheManifestLocked();
		logger::info(
			"[ShaderCacheAction] action=selective-seed result=success affectedFamilies={} unclassifiedFamilies={} retainedFamilies={} copiedFiles={} copiedBytes={}",
			CacheFamilyNames(plan->affected),
			CacheFamilyNames(plan->unclassified),
			CacheFamilyNames(plan->retained),
			copiedFiles,
			copiedBytes);
		outRetainedAnyFamily = !plan->retained.empty();
		return true;
	}

	bool ShaderCache::BackupActiveDiskCache(const std::vector<std::string>& a_changedDefines)
	{
		std::scoped_lock lock{ compilationSet.compilationMutex, g_diskCacheMutationMutex };
		if (!HasDiskCacheInfo(DiskCachePath())) {
			logger::warn("Cannot back up shader cache: active cache info is missing");
			return false;
		}
		AdvanceDiskCacheGeneration();

		// Preserve any entries below the normal batch threshold before this
		// directory becomes the rollback cache.
		FlushShaderCacheManifestLocked();

		if (!RemovePath(SwapDiskCachePath(), "temporary"))
			return false;

		const bool hadPreviousCache = PathExists(PreviousDiskCachePath());
		if (hadPreviousCache && !MoveDirectory(PreviousDiskCachePath(), SwapDiskCachePath(), "previous to temporary"))
			return false;

		if (!MoveDirectory(DiskCachePath(), PreviousDiskCachePath(), "active to previous")) {
			if (hadPreviousCache)
				MoveDirectory(SwapDiskCachePath(), PreviousDiskCachePath(), "temporary back to previous");
			return false;
		}

		try {
			std::filesystem::create_directories(DiskCachePath());
		} catch (std::filesystem::filesystem_error const& ex) {
			logger::error("Failed to create new shader cache folder: {}", ex.what());
			const bool activeCacheRestored =
				MoveDirectory(PreviousDiskCachePath(), DiskCachePath(), "previous back to active");
			if (hadPreviousCache)
				MoveDirectory(SwapDiskCachePath(), PreviousDiskCachePath(), "temporary back to previous");
			if (!activeCacheRestored)
				ReloadShaderCacheManifestLocked();
			RefreshPreviousDiskCacheInfo();
			return false;
		}

		if (hadPreviousCache)
			RemovePath(SwapDiskCachePath(), "temporary");

		// The complete old generation remains available for rollback. Seed the
		// new active generation with only families proven unaffected by the
		// changed feature defines, so a Water-only transition does not rebuild
		// Lighting, Grass, or any other retained family.
		DiscardShaderCacheManifestLocked();
		const bool selectiveSeedSucceeded = SeedActiveCacheFromPrevious(
			a_changedDefines,
			featureSetCacheSelectivelySeeded);
		if (!selectiveSeedSucceeded) {
			RemovePath(DiskCachePath(), "partially seeded active");
			std::error_code error;
			std::filesystem::create_directories(DiskCachePath(), error);
			DiscardShaderCacheManifestLocked();
			if (error)
				logger::error("Failed to recreate active shader cache after selective seed failure: {}", error.message());
		}
		RefreshPreviousDiskCacheInfo();
		logger::info("Saved complete previous shader cache for feature rollback");
		return true;
	}

	void ShaderCache::RefreshPreviousDiskCacheInfo()
	{
		previousDiskCacheAvailable = false;
		previousCacheMismatches.clear();

		if (!HasDiskCacheInfo(PreviousDiskCachePath()))
			return;

		CSimpleIniA ini;
		if (!LoadDiskCacheInfo(PreviousDiskCachePath(), ini)) {
			logger::warn("Previous shader cache exists but its cache info could not be read");
			return;
		}

		auto mismatches = ClassifyCacheInfo(ini);
		if (mismatches.empty())
			return;

		if (!OnlyEnabledFlips(mismatches)) {
			logger::info("Previous shader cache is not offered for restore because versions changed");
			return;
		}

		if (HasMissingOrFailedFeature(mismatches)) {
			logger::info("Previous shader cache is not offered for restore because a cached feature is missing or failed to load");
			return;
		}

		previousCacheMismatches = std::move(mismatches);
		previousDiskCacheAvailable = true;
	}

	void ShaderCache::ValidateDiskCache()
	{
		// Compaction is intentionally bounded to the early shader startup phase.
		// It is based on superseded committed bytes, never on total pack size.
		CompactShaderPacksIfNeeded();

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile((DiskCachePath() / L"Info.ini").c_str());
		cacheMismatches.clear();
		diskCacheHeld = false;
		featureSetChanged = false;
		featureSetRevertPending = false;
		featureSetCacheBackedUp = false;
		featureSetCacheSelectivelySeeded = false;
		previousDiskCacheAvailable = false;
		previousCacheMismatches.clear();
		heldMismatchDefines.clear();

		const bool managedPacks = ManagedShaderPacksAvailable();
		if (!managedPacks)
			RefreshPreviousDiskCacheInfo();
		cacheMismatches = ClassifyCacheInfo(ini);
		heldMismatchDefines = GetDefinesForMismatches(cacheMismatches, CacheMismatch::Kind::EnabledFlip);
		const auto scopedAbiMismatchDefines = GetDefinesForMismatches(cacheMismatches, CacheMismatch::Kind::FeatureShaderAbi);

		if (cacheMismatches.empty()) {
			logger::info("[ShaderCacheAction] action=startup-validation result=reuse reason=contracts-match");
			return;
		}

		for (const auto& mismatch : cacheMismatches)
			logger::info("Disk cache mismatch: {} - {}", mismatch.feature, mismatch.detail);

		if (managedPacks) {
			// Exact pack identities include source, ABI, compile flags,
			// defines, and applicable external compatibility contracts. A changed
			// contract is therefore a narrow lookup miss; no directory rotation or
			// blanket deletion is necessary.
			logger::info(
				"[ShaderCacheAction] action=startup-invalidation result=pack-identity-selection mismatchCount={} reason=exact-record-contract",
				cacheMismatches.size());
			WriteDiskCacheInfo();
			cacheMismatches.clear();
			heldMismatchDefines.clear();
			return;
		}

		const bool onlyEnabledFlips = OnlyEnabledFlips(cacheMismatches);
		if (onlyEnabledFlips) {
			if (HasMissingOrFailedFeature(cacheMismatches, true)) {
				diskCacheHeld = true;
				logger::info("Disk cache HELD (not deleted): a previously cached feature is missing or failed to load; compiling memory-only this session");
				return;
			}

			featureSetChanged = true;
			featureSetRevertPending = false;
			if (BackupActiveDiskCache(heldMismatchDefines)) {
				diskCacheHeld = false;
				featureSetCacheBackedUp = true;
				// We just moved the pre-change active cache into the rollback slot.
				// In this enabled-flip-only path, that cache is the valid restore target
				// for the previous boot configuration even if the immediate compatibility
				// refresh has not derived the UI state yet.
				const bool previousRestoreAvailable =
					SetPreviousCacheRestoreCandidate(
						cacheMismatches,
						previousDiskCacheAvailable,
						previousCacheMismatches,
						PreviousCacheInfoValidation::ValidatedBeforeMove);
				WriteDiskCacheInfo();
				const auto activeCachePlan = featureSetCacheSelectivelySeeded ?
				                                 "compiling affected shaders in the selectively seeded active cache" :
				                                 "rebuilding the active cache after selective seeding was unavailable";
				if (previousRestoreAvailable)
					logger::info("Feature set changed: {}; previous cache is available for restore", activeCachePlan);
				else
					logger::info("Feature set changed: {}; previous cache was saved but is not currently available for restore", activeCachePlan);
			} else {
				diskCacheHeld = true;
				featureSetCacheBackedUp = false;
				featureSetCacheSelectivelySeeded = false;
				logger::warn("Feature set changed but previous cache backup failed; preserving the active cache and compiling memory-only");
			}
			return;
		}

		const bool onlyScopedFeatureAbis = std::all_of(cacheMismatches.begin(), cacheMismatches.end(),
			[](const CacheMismatch& mismatch) {
				return mismatch.kind == CacheMismatch::Kind::FeatureShaderAbi;
			});
		if (onlyScopedFeatureAbis && PartialInvalidation(scopedAbiMismatchDefines)) {
			WriteDiskCacheInfo();
		} else {
			logger::warn(
				"[ShaderCacheAction] action=startup-invalidation result=full-wipe mismatchCount={} reason=global-contract-or-compiler",
				cacheMismatches.size());
			DeleteActiveDiskCache();
		}
	}

	void ShaderCache::CommitFeatureSetChange()
	{
		if (!featureSetChanged)
			return;

		const bool committedFeatureSetBackup = featureSetCacheBackedUp;
		auto committedPreviousCacheMismatches = committedFeatureSetBackup ? cacheMismatches : std::vector<CacheMismatch>{};

		if (!featureSetCacheBackedUp && !PartialInvalidation(heldMismatchDefines))
			DeleteActiveDiskCache();

		diskCacheHeld = false;
		const uint64_t diskCacheGeneration = GetDiskCacheGeneration();

		std::vector<std::pair<std::filesystem::path, hlslRecord>> records;
		{
			std::scoped_lock lockH{ hlslMapMutex };
			for (const auto& [sourcePath, shaderRecords] : hlslToShaderMap) {
				for (const auto& record : shaderRecords)
					records.emplace_back(sourcePath, record);
			}
		}

		std::set<std::wstring> savedPaths;
		for (const auto& [sourcePath, record] : records) {
			if (!savedPaths.insert(record.diskPath).second)
				continue;

			auto shaderBlob = GetCompletedShader(record.key);
			if (!shaderBlob || IsShaderLoadedFromDisk(record.key))
				continue;

			SaveShaderBlobToDisk(
				shaderBlob,
				record.developerMode,
				record.diskPath,
				sourcePath,
				record.compileStateDigest,
				record.packCompileStateDigest,
				diskCacheGeneration);
		}
		FlushShaderCacheManifest();

		heldMismatchDefines.clear();
		WriteDiskCacheInfo();
		featureSetChanged = false;
		featureSetRevertPending = false;
		featureSetCacheBackedUp = false;
		featureSetCacheSelectivelySeeded = false;
		cacheMismatches.clear();
		RefreshPreviousDiskCacheInfo();
		if (committedFeatureSetBackup && !previousDiskCacheAvailable &&
			SetPreviousCacheRestoreCandidate(
				std::move(committedPreviousCacheMismatches),
				previousDiskCacheAvailable,
				previousCacheMismatches,
				PreviousCacheInfoValidation::RequireReadableInfo)) {
			logger::info("Previous shader cache restore retained from feature-change backup");
		}
		logger::info("Feature set change committed: rebuilt disk cache for the current feature set");
	}

	bool ShaderCache::RestorePreviousDiskCache()
	{
		if (ManagedShaderPacksAvailable()) {
			logger::warn("Cannot restore a legacy previous-cache directory while managed shader packs are active");
			return false;
		}
		const bool hadPreviousRestoreCandidate = previousDiskCacheAvailable;
		auto retainedPreviousCacheMismatches = previousCacheMismatches;

		RefreshPreviousDiskCacheInfo();
		if (!previousDiskCacheAvailable && hadPreviousRestoreCandidate &&
			SetPreviousCacheRestoreCandidate(
				std::move(retainedPreviousCacheMismatches),
				previousDiskCacheAvailable,
				previousCacheMismatches,
				PreviousCacheInfoValidation::RequireReadableInfo)) {
			logger::info("Previous shader cache restore retained from feature-change backup");
		}
		if (!previousDiskCacheAvailable) {
			logger::warn("Cannot restore previous shader cache: no compatible previous cache is available");
			return false;
		}
		if (IsCompiling()) {
			logger::warn("Cannot restore previous shader cache while shader compilation is still running");
			return false;
		}
		if (!globals::state) {
			logger::warn("Cannot restore previous shader cache: state is not available");
			return false;
		}

		CSimpleIniA previousInfo;
		if (!LoadDiskCacheInfo(PreviousDiskCachePath(), previousInfo)) {
			logger::warn("Cannot restore previous shader cache: previous cache info could not be read");
			return false;
		}

		{
			std::scoped_lock lock{ compilationSet.compilationMutex, g_diskCacheMutationMutex };
			AdvanceDiskCacheGeneration();

			// The current active cache may become the new rollback cache. Persist
			// its last partial manifest batch before moving the directory.
			FlushShaderCacheManifestLocked();

			if (!RemovePath(SwapDiskCachePath(), "temporary"))
				return false;

			const bool activeExists = PathExists(DiskCachePath());
			if (activeExists && !MoveDirectory(DiskCachePath(), SwapDiskCachePath(), "active to temporary"))
				return false;

			if (!MoveDirectory(PreviousDiskCachePath(), DiskCachePath(), "previous to active")) {
				const bool activeCacheRestored =
					activeExists &&
					MoveDirectory(SwapDiskCachePath(), DiskCachePath(), "temporary back to active");
				if (!activeCacheRestored)
					ReloadShaderCacheManifestLocked();
				return false;
			}

			if (activeExists) {
				if (!RemovePath(PreviousDiskCachePath(), "previous")) {
					logger::warn("Previous shader cache was restored, but the current cache could not replace the rollback slot");
				} else if (!MoveDirectory(SwapDiskCachePath(), PreviousDiskCachePath(), "temporary to previous")) {
					logger::warn("Previous shader cache was restored, but the current cache could not be saved as the new rollback slot");
					RemovePath(SwapDiskCachePath(), "temporary");
				}
			}

			// The active directory now contains a different cache. Reload its
			// sidecar before any later write or destructor flush can overwrite it
			// with metadata from the cache that moved to the rollback slot.
			ReloadShaderCacheManifestLocked();
		}

		for (auto* feature : Feature::GetFeatureList()) {
			const auto shortName = feature->GetShortName();
			const bool enabledInPreviousCache = previousInfo.GetBoolValue(shortName.c_str(), "Enabled", false);
			globals::state->SetFeatureDisabled(shortName, !enabledInPreviousCache);
		}
		globals::state->Save();

		featureSetChanged = false;
		featureSetRevertPending = true;
		featureSetCacheBackedUp = false;
		featureSetCacheSelectivelySeeded = false;
		diskCacheHeld = false;
		heldMismatchDefines.clear();
		cacheMismatches.clear();
		RefreshPreviousDiskCacheInfo();
		logger::info("Previous shader cache restored: restart to load it");
		return true;
	}

	void ShaderCache::AcceptCacheRebuild()
	{
		if (!diskCacheHeld)
			return;

		if (!PartialInvalidation(heldMismatchDefines))
			DeleteActiveDiskCache();

		heldMismatchDefines.clear();
		WriteDiskCacheInfo();
		diskCacheHeld = false;
		featureSetChanged = false;
		featureSetRevertPending = false;
		featureSetCacheBackedUp = false;
		featureSetCacheSelectivelySeeded = false;
		cacheMismatches.clear();
		RefreshPreviousDiskCacheInfo();
		Clear();
		logger::info("Cache rebuild accepted: rebuilding disk cache for the current feature set");
	}

	void ShaderCache::WriteDiskCacheInfo()
	{
		const uint64_t diskCacheGeneration = GetDiskCacheGeneration();
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetValue("Cache", "PluginVersion", Plugin::VERSION_LABEL.data());
		ini.SetValue("Cache", "BuildId", BuildProvenance::GetBuildId().data());
		ini.SetValue("Cache", "ArtifactSHA256", BuildProvenance::GetArtifactSha256().c_str());
		ini.SetValue("Cache", "ShaderCacheABI", BuildProvenance::GetShaderCacheAbiId().data());
		ini.SetValue("Cache", "ShaderCompilerIdentity", BuildProvenance::GetShaderCompilerIdentity().c_str());
		globals::state->WriteDiskCacheInfo(ini);

		std::shared_lock lock{ g_diskCacheMutationMutex };
		if (diskCacheGeneration != GetDiskCacheGeneration()) {
			logger::debug("Skipped stale shader cache Info.ini write after a cache transition");
			return;
		}

		std::error_code error;
		std::filesystem::create_directories(DiskCachePath(), error);
		if (error) {
			logger::error("Failed to create shader cache folder: {}", error.message());
			return;
		}

		if (ini.SaveFile((DiskCachePath() / L"Info.ini").c_str()) < 0) {
			logger::error("Failed to save shader cache Info.ini");
			return;
		}
		logger::info("Saved disk cache info (plugin version: {}, Build ID: {}, shader ABI: {})",
			Plugin::VERSION_LABEL, BuildProvenance::GetBuildId(), BuildProvenance::GetShaderCacheAbiId());
	}

	static bool IsEnvVarTruthy(const char* a_name)
	{
		char buffer[16] = {};
		const DWORD len = GetEnvironmentVariableA(a_name, buffer, sizeof(buffer));
		if (len == 0 || len >= sizeof(buffer))
			return false;

		std::string value(buffer, len);
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});

		return value == "1" || value == "true";
	}

	ShaderCache::ShaderCache()
	{
		if (IsEnvVarTruthy("OPENSHADERS_BACKGROUND_COMPILE")) {
			backgroundCompilation = true;
			logger::info("OPENSHADERS_BACKGROUND_COMPILE set; starting shaders in background compilation mode");
		}

		dependencyTracker = std::make_unique<ShaderFileDependencyTracker>();
		logger::debug("ShaderCache initialized: {} startup threads, {} background threads, {} pool threads",
			(int)compilationThreadCount, (int)backgroundCompilationThreadCount, (int)compilationPool.get_thread_count());
		// Management thread runs on a dedicated jthread, not in the compilation pool,
		// so it doesn't consume a pool slot that could be used for shader compilation.
		managementJthread = std::jthread([this](std::stop_token stoken) {
			ManageCompilationSet(stoken);
		});
		deferredDiskWriterJthread = std::jthread([this](std::stop_token stoken) {
			ProcessDeferredDiskWrites(stoken);
		});
	}

	bool ShaderCache::UseFileWatcher() const
	{
		return useFileWatcher;
	}

	void ShaderCache::SetFileWatcher(bool value)
	{
		auto oldValue = useFileWatcher;
		useFileWatcher = value;
		if (useFileWatcher && !oldValue)
			StartFileWatcher();
		else if (!useFileWatcher && oldValue)
			StopFileWatcher();
	}

	void ShaderCache::StartFileWatcher()
	{
		logger::info("Starting FileWatcher");
		if (!fileWatcher) {
			fileWatcher = new efsw::FileWatcher();
			listener = new UpdateListener(dependencyTracker.get());
			// Add a folder to watch, and get the efsw::WatchID
			// Reporting the files and directories changes to the instance of the listener
			watchID = fileWatcher->addWatch("Data\\Shaders", listener, true);
			// Start watching asynchronously the directories
			fileWatcher->watch();
			std::string pathStr = "";
			for (auto path : fileWatcher->directories()) {
				pathStr += std::format("{}; ", path);
			}
			logger::debug("ShaderCache watching for changes in {}", pathStr);
			// Capture listener by value so the thread does not race with StopFileWatcher()
			// nulling this->listener before the thread has had a chance to start.
			auto* capturedListener = listener;
			capturedListener->fileWatcherThread = std::jthread([capturedListener]() {
				capturedListener->processQueue();
			});
		} else {
			logger::debug("ShaderCache already enabled");
		}
	}

	void ShaderCache::StopFileWatcher()
	{
		logger::info("Stopping FileWatcher");
		// Set flag first so processQueue()'s loop condition becomes false before we join.
		useFileWatcher = false;
		if (fileWatcher) {
			fileWatcher->removeWatch(watchID);
			fileWatcher = nullptr;
		}
		if (listener) {
			// ~jthread() calls request_stop() + join(); processQueue() exits when
			// UseFileWatcher() returns false (set above).
			delete listener;
			listener = nullptr;
		}
	}

	bool ShaderCache::UpdateShaderModifiedTime(const std::string& a_type, boolean a_forceUpdate)
	{
		if (!UseFileWatcher())
			return false;
		// Validate the shader type
		if (a_type.empty() || !magic_enum::enum_cast<RE::BSShader::Type>(a_type, magic_enum::case_insensitive).has_value()) {
			return false;  // Invalid type
		}

		std::lock_guard lockGuard(modifiedMapMutex);

		// Check for force update
		if (a_forceUpdate) {
			// Set an artificial timestamp far in the future (100 years)
			auto futureTime = std::chrono::system_clock::now() + std::chrono::hours(24 * 365 * 100);
			modifiedShaderMap.insert_or_assign(a_type, futureTime);
			return true;
		}

		// Otherwise, update with the actual file time
		std::filesystem::path filePath{ SIE::SShaderCache::GetShaderPath(a_type) };
		if (std::filesystem::exists(filePath)) {
			auto fileTime = std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(filePath));

			// Update only if timestamp has changed
			if (!modifiedShaderMap.contains(a_type) || modifiedShaderMap.at(a_type) != fileTime) {
				modifiedShaderMap.insert_or_assign(a_type, fileTime);
				return true;
			}
		}
		return false;
	}

	bool ShaderCache::ShaderModifiedSince(const std::string& a_type, std::chrono::system_clock::time_point a_current)
	{
		if (!UseFileWatcher())
			return false;
		// Validate the shader type
		if (a_type.empty() || !magic_enum::enum_cast<RE::BSShader::Type>(a_type, magic_enum::case_insensitive).has_value()) {
			return false;  // Invalid type
		}

		std::lock_guard lockGuard(modifiedMapMutex);

		// Check if the shader type exists in the map and if its modification time is newer than a_current
		return !modifiedShaderMap.empty() && modifiedShaderMap.contains(a_type) && modifiedShaderMap.at(a_type) > a_current;
	}

	RE::BSGraphics::VertexShader* ShaderCache::MakeAndAddVertexShader(const RE::BSShader& shader,
		uint32_t descriptor, std::optional<uint64_t> a_taskGeneration)
	{
		if (const auto shaderBlob =
				SShaderCache::CompileShader(ShaderClass::Vertex, shader, descriptor, IsDiskCacheActive(), dependencyTracker.get(), a_taskGeneration)) {
			if (IsTaskStale(a_taskGeneration)) {
				return nullptr;
			}

			auto device = globals::d3d::device;

			auto newShader = SShaderCache::CreateVertexShader(*shaderBlob, shader,
				descriptor);

			std::lock_guard lockGuard(vertexShadersMutex);
			if (IsTaskStale(a_taskGeneration)) {
				logger::debug(
					"Discarding stale-generation vertex shader {}::{:X}",
					magic_enum::enum_name(shader.shaderType.get()),
					descriptor);
				return nullptr;
			}

			const auto result = device->CreateVertexShader(shaderBlob->GetBufferPointer(),
				newShader->byteCodeSize, nullptr, reinterpret_cast<ID3D11VertexShader**>(&newShader->shader));
			if (FAILED(result)) {
				logger::error("Failed to create vertex shader {}::{:X}",
					magic_enum::enum_name(shader.shaderType.get()), descriptor);
				if (newShader->shader != nullptr) {
					newShader->shader->Release();
				}
			} else {
				return vertexShaders[static_cast<size_t>(shader.shaderType.get())]
				    .insert_or_assign(descriptor, std::move(newShader))
				    .first->second.get();
			}
		}
		return nullptr;
	}

	RE::BSGraphics::PixelShader* ShaderCache::MakeAndAddPixelShader(const RE::BSShader& shader,
		uint32_t descriptor, std::optional<uint64_t> a_taskGeneration)
	{
		if (const auto shaderBlob =
				SShaderCache::CompileShader(ShaderClass::Pixel, shader, descriptor, IsDiskCacheActive(), dependencyTracker.get(), a_taskGeneration)) {
			if (IsTaskStale(a_taskGeneration)) {
				return nullptr;
			}

			auto device = globals::d3d::device;

			auto newShader = SShaderCache::CreatePixelShader(*shaderBlob, shader,
				descriptor);

			std::lock_guard lockGuard(pixelShadersMutex);
			if (IsTaskStale(a_taskGeneration)) {
				logger::debug(
					"Discarding stale-generation pixel shader {}::{:X}",
					magic_enum::enum_name(shader.shaderType.get()),
					descriptor);
				return nullptr;
			}
			const auto result = device->CreatePixelShader(shaderBlob->GetBufferPointer(),
				shaderBlob->GetBufferSize(), nullptr, reinterpret_cast<ID3D11PixelShader**>(&newShader->shader));
			if (FAILED(result)) {
				logger::error("Failed to create pixel shader {}::{:X}",
					magic_enum::enum_name(shader.shaderType.get()),
					descriptor);
				if (newShader->shader != nullptr) {
					newShader->shader->Release();
				}
			} else {
				return pixelShaders[static_cast<size_t>(shader.shaderType.get())]
				    .insert_or_assign(descriptor, std::move(newShader))
				    .first->second.get();
			}
		}
		return nullptr;
	}

	RE::BSGraphics::ComputeShader* ShaderCache::MakeAndAddComputeShader(const RE::BSShader& shader,
		uint32_t descriptor, std::optional<uint64_t> a_taskGeneration)
	{
		if (const auto shaderBlob =
				SShaderCache::CompileShader(ShaderClass::Compute, shader, descriptor, IsDiskCacheActive(), dependencyTracker.get(), a_taskGeneration)) {
			if (IsTaskStale(a_taskGeneration)) {
				return nullptr;
			}

			auto device = globals::d3d::device;

			auto newShader = SShaderCache::CreateComputeShader(*shaderBlob, shader,
				descriptor);

			std::lock_guard lockGuard(computeShadersMutex);
			if (IsTaskStale(a_taskGeneration)) {
				logger::debug(
					"Discarding stale-generation compute shader {}::{:X}",
					magic_enum::enum_name(shader.shaderType.get()),
					descriptor);
				return nullptr;
			}
			const auto result = device->CreateComputeShader(shaderBlob->GetBufferPointer(),
				shaderBlob->GetBufferSize(), nullptr, reinterpret_cast<ID3D11ComputeShader**>(&newShader->shader));
			if (FAILED(result)) {
				logger::error("Failed to create pixel shader {}::{:X}",
					magic_enum::enum_name(shader.shaderType.get()),
					descriptor);
				if (newShader->shader != nullptr) {
					newShader->shader->Release();
				}
			} else {
				return computeShaders[static_cast<size_t>(shader.shaderType.get())]
				    .insert_or_assign(descriptor, std::move(newShader))
				    .first->second.get();
			}
		}
		return nullptr;
	}

	std::string ShaderCache::GetDefinesString(const RE::BSShader& shader, uint32_t descriptor)
	{
		std::array<D3D_SHADER_MACRO, 64> defines{};
		SIE::SShaderCache::GetShaderDefines(shader, descriptor, std::span{ defines });

		return SIE::SShaderCache::MergeDefinesString(defines, true);
	}

	uint64_t ShaderCache::GetCachedHitTasks()
	{
		return compilationSet.cacheHitTasks;
	}
	uint64_t ShaderCache::GetCompletedTasks()
	{
		return compilationSet.completedTasks;
	}
	uint64_t ShaderCache::GetFailedTasks()
	{
		return compilationSet.failedTasks;
	}

	uint64_t ShaderCache::GetCurrentFailedCount()
	{
		std::scoped_lock lock(mapMutex);
		uint64_t count = 0;
		for (const auto& [key, result] : shaderMap) {
			if (result.status == ShaderCompilationTask::Status::Failed) {
				++count;
			}
		}
		return count;
	}

	void ShaderCache::RecordCompileFailure(std::string a_key, std::string a_path, std::string a_error)
	{
		constexpr size_t kMaxErrorLength = 2000;
		if (a_error.size() > kMaxErrorLength)
			a_error.resize(kMaxErrorLength);

		CompileFailure failure{
			.key = std::move(a_key),
			.path = std::move(a_path),
			.error = std::move(a_error),
			.epoch = static_cast<uint64_t>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count()),
			.frame = globals::state ? globals::state->frameCountAtomic.load(std::memory_order_relaxed) : 0u,
		};
		std::lock_guard lock(compileFailuresMutex);
		if (recentCompileFailures.size() >= kMaxRecentCompileFailures)
			recentCompileFailures.pop_front();
		recentCompileFailures.push_back(std::move(failure));
	}

	uint64_t ShaderCache::GetTotalTasks()
	{
		return compilationSet.totalTasks;
	}
	uint64_t ShaderCache::GetDiskHitTasks()
	{
		return compilationSet.diskHitTasks;
	}
	uint64_t ShaderCache::GetSourceCompileTasks()
	{
		return compilationSet.sourceCompileTasks;
	}
	void ShaderCache::IncCacheHitTasks(std::optional<uint64_t> a_taskGeneration)
	{
		compilationSet.IncCacheHit(a_taskGeneration);
	}
	void ShaderCache::IncSourceCompileTasks(std::optional<uint64_t> a_taskGeneration)
	{
		compilationSet.IncSourceCompile(a_taskGeneration);
	}
	void ShaderCache::MarkCompilationPhaseStarted(std::optional<uint64_t> a_taskGeneration)
	{
		compilationSet.MarkPhaseStarted(a_taskGeneration);
	}

	bool ShaderCache::IsHideErrors()
	{
		return hideError;
	}

	int ShaderCache::GetHeavyTasksInFlight()
	{
		return static_cast<int>(compilationSet.heavyTasksInFlight.load(std::memory_order_relaxed));
	}

	uint64_t ShaderCache::GetSlowTasks()
	{
		return compilationSet.slowTasks.load(std::memory_order_relaxed);
	}

	uint64_t ShaderCache::GetVerySlowTasks()
	{
		return compilationSet.verySlowTasks.load(std::memory_order_relaxed);
	}

	std::vector<CompilationSet::SlowTaskRecord> CompilationSet::GetTopSlowTasks(size_t n) const
	{
		std::lock_guard lock(slowTasksMutex);
		// Partial sort to get the N highest without fully sorting the whole vector.
		std::vector<SlowTaskRecord> result = slowTaskRecords;
		if (result.size() > n) {
			std::partial_sort(result.begin(), result.begin() + n, result.end(),
				[](const SlowTaskRecord& a, const SlowTaskRecord& b) { return a.elapsedMs > b.elapsedMs; });
			result.resize(n);
		} else {
			std::sort(result.begin(), result.end(),
				[](const SlowTaskRecord& a, const SlowTaskRecord& b) { return a.elapsedMs > b.elapsedMs; });
		}
		return result;
	}

	std::vector<CompilationSet::SlowTaskRecord> ShaderCache::GetTopSlowTasks(size_t n)
	{
		return compilationSet.GetTopSlowTasks(n);
	}

	std::vector<CompilationSet::SlowTaskRecord> CompilationSet::GetAllTaskRecords() const
	{
		std::lock_guard lock(slowTasksMutex);
		return slowTaskRecords;
	}

	std::vector<CompilationSet::SlowTaskRecord> ShaderCache::GetAllTaskRecords()
	{
		return compilationSet.GetAllTaskRecords();
	}

	int64_t ShaderCache::GetLastResetQpc()
	{
		return compilationSet.GetLastResetQpc();
	}

	int64_t ShaderCache::GetQpcFrequency()
	{
		return compilationSet.GetQpcFrequency();
	}

	bool ShaderCache::ExportCompileTrace(const std::filesystem::path& a_path)
	{
		const auto records = compilationSet.GetAllTaskRecords();
		if (records.empty()) {
			logger::warn("ExportCompileTrace: no task records for the current build");
			return false;
		}

		const int64_t frequency = compilationSet.GetQpcFrequency();
		int64_t baselineQpc = records.front().startQpc;
		for (const auto& record : records) {
			const int64_t queueWaitQpc = static_cast<int64_t>(
				record.queueWaitMs * static_cast<double>(frequency) / 1000.0);
			baselineQpc = std::min(baselineQpc, record.startQpc - queueWaitQpc);
		}
		const auto qpcToUs = [frequency, baselineQpc](int64_t a_qpc) {
			return static_cast<double>(a_qpc - baselineQpc) * 1'000'000.0 /
			       static_cast<double>(frequency);
		};

		nlohmann::json events = nlohmann::json::array();
		const uint32_t processId = GetCurrentProcessId();
		std::unordered_set<uint32_t> namedThreads;
		for (const auto& record : records) {
			if (namedThreads.insert(record.threadId).second) {
				events.push_back({ { "name", "thread_name" },
					{ "ph", "M" },
					{ "pid", processId },
					{ "tid", record.threadId },
					{ "args", { { "name", "Shader Compile Worker" } } } });
			}
		}

		for (const auto& record : records) {
			const double startUs = qpcToUs(record.startQpc);
			if (record.queueWaitMs > 0.0) {
				events.push_back({ { "name", "queue_wait" },
					{ "cat", "shader_compile" },
					{ "ph", "X" },
					{ "ts", startUs - record.queueWaitMs * 1000.0 },
					{ "dur", record.queueWaitMs * 1000.0 },
					{ "pid", processId },
					{ "tid", record.threadId } });
			}
			events.push_back({ { "name", record.key },
				{ "cat", "shader_compile" },
				{ "ph", "X" },
				{ "ts", startUs },
				{ "dur", record.elapsedMs * 1000.0 },
				{ "pid", processId },
				{ "tid", record.threadId },
				{ "args", { { "priority", record.priority },
							  { "defineCount", record.defineCount },
							  { "sourceSizeBytes", record.sourceSizeBytes } } } });
		}

		try {
			if (!a_path.parent_path().empty()) {
				std::filesystem::create_directories(a_path.parent_path());
			}
			std::ofstream file(a_path);
			if (!file.is_open()) {
				logger::warn("ExportCompileTrace: failed to open {} for writing", a_path.string());
				return false;
			}
			file << events.dump(2);
			file.flush();
			if (!file.good()) {
				logger::warn("ExportCompileTrace: write to {} failed", a_path.string());
				return false;
			}
		} catch (const std::exception& e) {
			logger::warn("ExportCompileTrace: failed writing {}: {}", a_path.string(), e.what());
			return false;
		}

		logger::info("ExportCompileTrace: wrote {} task records to {}", records.size(), a_path.string());
		return true;
	}

	std::optional<CompilationSet::ParallelismStats> CompilationSet::GetParallelismStats() const
	{
		std::vector<SlowTaskRecord> records;
		{
			std::lock_guard lock(slowTasksMutex);
			if (slowTaskRecords.empty()) {
				return std::nullopt;
			}
			records = slowTaskRecords;
		}

		ParallelismStats stats;
		stats.sampleCount = records.size();
		for (const auto& rec : records) {
			stats.workMs += rec.elapsedMs;
			stats.spanMs = std::max(stats.spanMs, rec.elapsedMs);
			stats.avgQueueWaitMs += rec.queueWaitMs;
			stats.maxQueueWaitMs = std::max(stats.maxQueueWaitMs, rec.queueWaitMs);
		}
		stats.avgQueueWaitMs /= static_cast<double>(stats.sampleCount);

		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		int64_t endTime = completionTime.load(std::memory_order_relaxed);
		if (endTime == 0) {
			endTime = now.QuadPart;
		}
		stats.makespanMs = static_cast<double>(endTime - lastReset.QuadPart) * 1000.0 / frequency.QuadPart;

		if (stats.spanMs > 0.0) {
			stats.avgParallelism = stats.workMs / stats.spanMs;
		}
		if (stats.makespanMs > 0.0) {
			stats.infiniteCoreEfficiency = stats.spanMs / stats.makespanMs;
			stats.infiniteCoreGapPercent = std::max(0.0, 100.0 * (1.0 - stats.infiniteCoreEfficiency));
		}

		return stats;
	}

	std::optional<CompilationSet::ParallelismStats> ShaderCache::GetParallelismStats()
	{
		return compilationSet.GetParallelismStats();
	}

	void ShaderCache::ClearShaderMap(RE::BSShader::Type a_type)
	{
		std::string_view shaderTypeStr = magic_enum::enum_name(a_type);

		{
			std::unique_lock lockM{ SIE::ShaderCache::mapMutex };
			logger::debug("Clearing shaderMap of {}", shaderTypeStr);
			for (auto it = shaderMap.begin(); it != shaderMap.end();) {
				auto typeInKey = SIE::SShaderCache::GetTypeFromShaderString(it->first);
				if (typeInKey == shaderTypeStr) {
					it = shaderMap.erase(it);
				} else {
					++it;
				}
			}
		}
		mapCV.notify_all();
	}

	void ShaderCache::InsertModifiedShaderMap(const std::string& a_shader, std::chrono::time_point<std::chrono::system_clock> a_time)
	{
		std::lock_guard lockGuard(modifiedMapMutex);
		modifiedShaderMap.insert_or_assign(a_shader, a_time);
	}

	std::chrono::time_point<std::chrono::system_clock> ShaderCache::GetModifiedShaderMapTime(const std::string& a_shader)
	{
		std::lock_guard lockGuard(modifiedMapMutex);
		return modifiedShaderMap.at(a_shader);
	}

	void ShaderCache::ToggleErrorMessages()
	{
		hideError = !hideError;
	}

	void ShaderCache::IterateShaderBlock(bool a_forward)
	{
		// Try to use active shaders list if available in developer mode
		if (globals::state->IsDeveloperMode()) {
			std::lock_guard lockActive(activeShadersMutex);
			if (!activeShaders.empty()) {
				// Build sorted list of active shader keys
				std::vector<std::string> keys;
				keys.reserve(activeShaders.size());
				for (const auto& [key, _] : activeShaders) {
					keys.push_back(key);
				}
				std::sort(keys.begin(), keys.end());

				// Find current position or start
				int currentIdx = -1;
				if (!blockedKey.empty()) {
					auto it = std::find(keys.begin(), keys.end(), blockedKey);
					if (it != keys.end()) {
						currentIdx = static_cast<int>(std::distance(keys.begin(), it));
					}
				}

				// Calculate next index
				int targetIdx = 0;
				if (currentIdx >= 0) {
					targetIdx = a_forward ? (currentIdx + 1) % static_cast<int>(keys.size()) : (currentIdx - 1 + static_cast<int>(keys.size())) % static_cast<int>(keys.size());
				} else {
					targetIdx = a_forward ? 0 : static_cast<int>(keys.size()) - 1;
				}

				blockedKey = keys[targetIdx];
				blockedKeyIndex = -2;  // Set to -2 for dev selections to distinguish from shaderMap indices
				blockedIDs.clear();
				logger::debug("Blocking active shader ({}/{}) {}", targetIdx + 1, keys.size(), blockedKey);
				return;
			}
		}

		// Fallback to original behavior with full shader map
		std::scoped_lock lockM{ mapMutex };
		auto targetIndex = a_forward ? 0 : shaderMap.size() - 1;           // default start or last element
		if (blockedKeyIndex >= 0 && shaderMap.size() > blockedKeyIndex) {  // grab next element
			targetIndex = (blockedKeyIndex + (a_forward ? 1 : -1)) % shaderMap.size();
		}
		auto index = 0;
		for (auto& [key, value] : shaderMap) {
			if (index++ == targetIndex) {
				blockedKey = key;
				blockedKeyIndex = -1;
				blockedIDs.clear();
				logger::debug("Blocking shader ({}/{}) {}", blockedKeyIndex + 1, shaderMap.size(), blockedKey);
				return;
			}
		}
	}

	void ShaderCache::DisableShaderBlocking()
	{
		blockedKey = "";
		blockedKeyIndex = -1;
		blockedIDs.clear();
		logger::debug("Stopped blocking shaders");
	}

	void ShaderCache::TrackActiveShader(ShaderClass shaderClass, const RE::BSShader& shader, uint32_t descriptor)
	{
		const bool developerMode = globals::state->IsDeveloperMode();
		const bool capturing =
			activeShaderCaptureFramesRemaining.load(std::memory_order_relaxed) > 0 &&
			std::this_thread::get_id() == activeShaderCaptureThread.load(std::memory_order_relaxed);
		if (!developerMode && !capturing)
			return;

		auto key = SIE::SShaderCache::GetShaderString(shaderClass, shader, descriptor, true);
		std::lock_guard lock(activeShadersMutex);

		auto initializeInfo = [&](ActiveShaderInfo& info) {
			info.key = key;
			info.shaderType = shader.shaderType.get();
			info.shaderClass = shaderClass;
			info.descriptor = descriptor;
			// Compiled blobs are keyed on fxpFilename even for ImageSpace shaders.
			info.diskPath = SIE::SShaderCache::GetDiskPath(shader.fxpFilename, descriptor, shaderClass);
			info.isActive = true;
			info.drawCalls = 1;
			info.lastUsed = std::chrono::steady_clock::now();
		};

		if (developerMode) {
			auto& info = activeShaders[key];
			if (info.key.empty())
				initializeInfo(info);
			else {
				info.isActive = true;
				info.drawCalls++;
				info.lastUsed = std::chrono::steady_clock::now();
			}

			if (capturing)
				capturedShaders.try_emplace(key, info);
		} else if (capturing) {
			// Normal gameplay captures must not populate the persistent developer map:
			// ResetFrameShaderTracking intentionally does nothing outside developer mode.
			auto [it, inserted] = capturedShaders.try_emplace(key);
			if (inserted)
				initializeInfo(it->second);
		}
	}

	void ShaderCache::ResetFrameShaderTracking()
	{
		if (!globals::state->IsDeveloperMode())
			return;

		std::lock_guard lock(activeShadersMutex);

		// Mark all shaders as inactive for this frame
		// Keep shaders that were used recently (within last 60 frames / ~1 second at 60fps)
		auto now = std::chrono::steady_clock::now();
		auto timeout = std::chrono::seconds(1);

		for (auto it = activeShaders.begin(); it != activeShaders.end();) {
			auto& info = it->second;
			info.isActive = false;
			info.drawCalls = 0;

			// Remove shaders that haven't been used recently
			if (now - info.lastUsed > timeout) {
				it = activeShaders.erase(it);
			} else {
				++it;
			}
		}
	}

	std::vector<ShaderCache::ActiveShaderInfo> ShaderCache::GetActiveShaders() const
	{
		std::lock_guard lock(activeShadersMutex);
		std::vector<ActiveShaderInfo> result;
		result.reserve(activeShaders.size());

		for (const auto& [key, info] : activeShaders) {
			result.push_back(info);
		}

		return result;
	}

	void ShaderCache::ManageCompilationSet(std::stop_token stoken)
	{
		managementThread = GetCurrentThread();
		// This coordinator only dequeues and dispatches work. Keeping it merely
		// below-normal avoids starving task publication when Skyrim runs High.
		SetThreadPriority(managementThread, THREAD_PRIORITY_BELOW_NORMAL);
		while (!stoken.stop_requested()) {
			const auto& task = compilationSet.WaitTake(stoken);
			if (!task.has_value())
				break;  // exit because thread told to end
			compilationPool.detach_task([this, stoken, t = task.value()] { ProcessCompilationSet(stoken, t); });
		}
	}

	void ShaderCache::ProcessDeferredDiskWrites(std::stop_token stoken)
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
		for (;;) {
			std::deque<DeferredDiskWrite> writes;
			bool flushManifest = false;
			{
				std::unique_lock lock{ deferredDiskWritesMutex };
				deferredDiskWritesCV.wait(lock, stoken, [this, stoken] {
					const bool hasPendingWork =
						!deferredDiskWrites.empty() || deferredManifestFlushPending;
					const bool persistenceBlocked =
						saveLoadDiskPersistenceBlocked.load(std::memory_order_acquire);
					return stoken.stop_requested() ||
					       (hasPendingWork && !persistenceBlocked);
				});
				if (stoken.stop_requested() &&
					(deferredDiskWrites.empty() ||
						saveLoadDiskPersistenceBlocked.load(std::memory_order_acquire))) {
					break;
				}

				writes.swap(deferredDiskWrites);
				flushManifest = deferredManifestFlushPending;
				deferredManifestFlushPending = false;
			}

			std::size_t savedWrites = 0;
			std::size_t skippedWrites = 0;
			bool optimizedPackWritten = false;
			bool developerPackWritten = false;
			while (!writes.empty()) {
				if (saveLoadDiskPersistenceBlocked.load(std::memory_order_acquire))
					break;

				auto write = std::move(writes.front());
				writes.pop_front();
				if (!IsDiskCacheActive()) {
					++skippedWrites;
					continue;
				}

				try {
					if (SaveShaderBlobToDisk(
							write.shaderBlob.Get(),
							write.developerMode,
							write.diskPath,
							write.shaderPath,
							write.compileStateDigest,
							write.packCompileStateDigest,
							write.diskCacheGeneration)) {
						++savedWrites;
						if (GetShaderPackStore(write.developerMode)) {
							if (write.developerMode)
								developerPackWritten = true;
							else
								optimizedPackWritten = true;
						}
					} else {
						++skippedWrites;
					}
				} catch (const std::exception& e) {
					++skippedWrites;
					logger::error(
						"Failed deferred shader-cache write to {}: {}",
						Util::WStringToString(write.diskPath),
						e.what());
				} catch (...) {
					++skippedWrites;
					logger::error(
						"Failed deferred shader-cache write to {} due to an unknown error",
						Util::WStringToString(write.diskPath));
				}
			}

			for (const auto [developerMode, written] : {
					 std::pair{ false, optimizedPackWritten }, std::pair{ true, developerPackWritten } }) {
				if (!written)
					continue;
				try {
					std::string checkpointError;
					if (auto* store = GetShaderPackStore(developerMode); store && !store->Checkpoint(&checkpointError)) {
						QuarantineShaderPackLane(developerMode, checkpointError);
						logger::error("Failed to checkpoint {} shader pack batch: {}", developerMode ? "developer" : "optimized", checkpointError);
					}
				} catch (const std::exception& e) {
					QuarantineShaderPackLane(developerMode, e.what());
					logger::error("Failed to checkpoint {} shader pack batch: {}", developerMode ? "developer" : "optimized", e.what());
				}
			}

			const bool stopping = stoken.stop_requested();
			if (!writes.empty()) {
				if (stopping) {
					logger::warn(
						"Discarding {} deferred shader-cache writes because disk persistence became blocked during shutdown",
						writes.size());
				} else {
					std::lock_guard lock{ deferredDiskWritesMutex };
					while (!writes.empty()) {
						deferredDiskWrites.push_front(std::move(writes.back()));
						writes.pop_back();
					}
					deferredManifestFlushPending =
						deferredManifestFlushPending || flushManifest || savedWrites != 0;
				}
			} else if (flushManifest || savedWrites != 0) {
				if (saveLoadDiskPersistenceBlocked.load(std::memory_order_acquire) && !stopping) {
					std::lock_guard lock{ deferredDiskWritesMutex };
					deferredManifestFlushPending = true;
				} else {
					FlushShaderCacheManifest();
				}
			}

			if (savedWrites != 0 || skippedWrites != 0) {
				logger::debug(
					"Deferred shader-cache persistence completed: {} saved, {} skipped",
					savedWrites,
					skippedWrites);
			}
		}
	}

	void ShaderCache::ProcessCompilationSet(std::stop_token stoken, SIE::ShaderCompilationTask task)
	{
		const SKSE::stl::scope_exit releaseSlot([this]() noexcept { compilationSet.ReleaseDispatchSlot(); });

		if (stoken.stop_requested()) {
			return;
		}

		const auto taskKey = task.GetString();

		// The large blocking startup batch yields to the desktop even when another
		// plugin raises Skyrim's process class. Once DataLoaded releases the game,
		// explicitly restore each reused pool worker to normal relative priority;
		// in-game recompiles are constrained by the smaller background thread limit.
		static std::atomic_bool priorityWarningLogged = false;
		using namespace ShaderCompilationSchedulingPolicy;
		const auto priorityMode = SelectWorkerThreadPriorityMode(
			SelectCompilationPhase(startupCompilationComplete.load(std::memory_order_acquire)));
		const bool priorityApplied =
			priorityMode == WorkerThreadPriorityMode::CooperativeBackground ?
				Util::SetCurrentThreadCooperativeBackgroundPriority() :
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL) != FALSE;
		if (!priorityApplied &&
			!priorityWarningLogged.exchange(true, std::memory_order_acq_rel)) {
			logger::warn(
				"Shader compiler workers could not apply the requested {} CPU priority; continuing with the priority available to the current process class.",
				priorityMode == WorkerThreadPriorityMode::CooperativeBackground ?
					"startup-cooperative" :
					"in-game normal");
		}

		LARGE_INTEGER start, end, freq;
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&start);
		const double queueWaitMs = task.GetEnqueuedQpc() > 0 ?
		                               static_cast<double>(start.QuadPart - task.GetEnqueuedQpc()) * 1000.0 / freq.QuadPart :
		                               0.0;

		try {
			task.Perform();
		} catch (const std::exception& e) {
			logger::error("Unhandled exception compiling shader task {}: {}", taskKey, e.what());
			ResolvePendingFailure(taskKey, task.GetGeneration());
		} catch (...) {
			logger::error("Unhandled non-standard exception compiling shader task {}", taskKey);
			ResolvePendingFailure(taskKey, task.GetGeneration());
		}

		QueryPerformanceCounter(&end);
		const double elapsedMs = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
		// Use saturating math: without a lock, Clear() can zero totalTasks while completedTasks
		// still reads high briefly, which would otherwise underflow uint64_t (logs as ~2^64-1).
		const uint64_t total = compilationSet.totalTasks.load(std::memory_order_relaxed);
		const uint64_t done = compilationSet.completedTasks.load(std::memory_order_relaxed) +
		                      compilationSet.failedTasks.load(std::memory_order_relaxed);
		// This task has already finished running, but Complete(task) has not yet updated the counters.
		// Include the current task in the local progress snapshot so the logged remaining count is accurate.
		const uint64_t doneIncludingCurrent = (done < total) ? (done + 1) : total;
		const uint64_t remaining = (total > doneIncludingCurrent) ? (total - doneIncludingCurrent) : 0;

		// Proxy for permutation complexity: descriptor low 32 bits from GetId(); popcount = active defines.
		// Shader file size provides a secondary signal for source complexity.
		const auto descriptorComplexity = std::popcount(static_cast<uint32_t>(task.GetId()));
		uintmax_t sourceBytes = 0;
		{
			std::error_code ec;
			sourceBytes = std::filesystem::file_size(task.GetSourcePath(), ec);
			if (ec) {
				sourceBytes = 0;
			}
		}

		// Debug: full per-task record for post-mortem straggler analysis.
		logger::debug("[ShaderTiming] {:.0f}ms | queue_wait={:.0f}ms | remaining={} | defines={} | src={}B | prio={} | tid={} | {}",
			elapsedMs, queueWaitMs, remaining, descriptorComplexity, sourceBytes,
			task.GetPriority(), GetCurrentThreadId(), taskKey);

		constexpr double kSlowMs = 2000.0;
		constexpr double kVerySlowMs = 8000.0;

		// Clear() increments the generation before clearing these records. Checking
		// under the record lock prevents an old worker from appending afterward.
		bool recordedForCurrentGeneration = false;
		{
			std::lock_guard lock(compilationSet.slowTasksMutex);
			if (compilationSet.IsCurrentGeneration(task)) {
				compilationSet.slowTaskRecords.push_back({ taskKey, elapsedMs, queueWaitMs, task.GetPriority(),
					static_cast<int>(descriptorComplexity), sourceBytes, GetCurrentThreadId(), start.QuadPart });
				if (elapsedMs >= kVerySlowMs) {
					compilationSet.verySlowTasks++;
					compilationSet.slowTasks++;
				} else if (elapsedMs >= kSlowMs) {
					compilationSet.slowTasks++;
				}
				recordedForCurrentGeneration = true;
			}
		}

		if (recordedForCurrentGeneration && elapsedMs >= kVerySlowMs) {
			logger::info("[ShaderTiming] Very slow {:.0f}ms | queue_wait={:.0f}ms | remaining={} | defines={} | src={}B | prio={} | {}",
				elapsedMs, queueWaitMs, remaining, descriptorComplexity, sourceBytes, task.GetPriority(), taskKey);
		} else if (recordedForCurrentGeneration && elapsedMs >= kSlowMs) {
			logger::debug("[ShaderTiming] Slow {:.0f}ms | queue_wait={:.0f}ms | remaining={} | defines={} | src={}B | prio={} | {}",
				elapsedMs, queueWaitMs, remaining, descriptorComplexity, sourceBytes, task.GetPriority(), taskKey);
		}

		if (stoken.stop_requested()) {
			return;
		}

		compilationSet.Complete(task);
		ApplyDeferredEviction(taskKey, true);
	}

	ShaderCompilationTask::ShaderCompilationTask(ShaderClass aShaderClass,
		const RE::BSShader& aShader,
		uint32_t aDescriptor) :
		shaderClass(aShaderClass),
		shader(aShader), descriptor(aDescriptor),
		cachedPriority(ComputePriority(aShaderClass, aShader, aDescriptor))
	{}

	void ShaderCompilationTask::Perform() const
	{
		ZoneScoped;
		ZoneText(GetString().c_str(), GetString().size());

		if (shaderClass == ShaderClass::Vertex) {
			ShaderCache::Instance().MakeAndAddVertexShader(shader, descriptor, GetGeneration());
		} else if (shaderClass == ShaderClass::Pixel) {
			ShaderCache::Instance().MakeAndAddPixelShader(shader, descriptor, GetGeneration());
		} else if (shaderClass == ShaderClass::Compute) {
			ShaderCache::Instance().MakeAndAddComputeShader(shader, descriptor, GetGeneration());
		}
	}

	size_t ShaderCompilationTask::GetId() const
	{
		return MakeId(shaderClass, shader.shaderType.get(), descriptor);
	}

	size_t ShaderCompilationTask::MakeId(ShaderClass shaderClass, RE::BSShader::Type shaderType, uint32_t descriptor)
	{
		return descriptor + (static_cast<size_t>(shaderType) << 32) +
		       (static_cast<size_t>(shaderClass) << 60);
	}

	std::string ShaderCompilationTask::GetString() const
	{
		return SIE::SShaderCache::GetShaderString(shaderClass, shader, descriptor, true);
	}

	std::wstring ShaderCompilationTask::GetSourcePath() const
	{
		return SIE::SShaderCache::GetShaderPath(
			shader.shaderType == RE::BSShader::Type::ImageSpace ?
				static_cast<const RE::BSImagespaceShader&>(shader).originalShaderName :
				shader.fxpFilename);
	}

	bool ShaderCompilationTask::operator==(const ShaderCompilationTask& other) const
	{
		return GetId() == other.GetId();
	}

	int ShaderCompilationTask::ComputePriority(ShaderClass shaderClass, const RE::BSShader& shader, uint32_t descriptor)
	{
		int priority = 0;
		const auto type = shader.shaderType.get();

		// Base priority by shader type — Lighting is consistently the slowest
		// (123KB source, 12s+ compile), followed by Effect (~31KB, up to 12s).
		switch (type) {
		case RE::BSShader::Type::Lighting:
			priority += 1000;
			break;
		case RE::BSShader::Type::Effect:
			priority += 500;
			break;
		case RE::BSShader::Type::Water:
			priority += 300;
			break;
		default:
			break;
		}

		// Pixel shaders compile significantly slower than vertex shaders
		if (shaderClass == ShaderClass::Pixel)
			priority += 200;

		// More active descriptor bits → more #defines → more code paths for the compiler
		priority += std::popcount(descriptor) * 30;

		// Known heavy Lighting techniques and flags from straggler analysis
		if (type == RE::BSShader::Type::Lighting) {
			const auto technique = static_cast<ShaderCache::LightingShaderTechniques>(0x3F & (descriptor >> 24));

			// LANDSCAPE techniques (MTLand, MTLandLODBlend) are among the heaviest
			// due to multi-texture blending codegen — regularly 60-130s compile times
			if (technique == ShaderCache::LightingShaderTechniques::MTLand ||
				technique == ShaderCache::LightingShaderTechniques::MTLandLODBlend)
				priority += 500;
			if (technique == ShaderCache::LightingShaderTechniques::Parallax ||
				technique == ShaderCache::LightingShaderTechniques::ParallaxOcc)
				priority += 300;
			if (technique == ShaderCache::LightingShaderTechniques::Eye)
				priority += 200;
			if (technique == ShaderCache::LightingShaderTechniques::MultilayerParallax)
				priority += 200;

			// TRUE_PBR and ANISO_LIGHTING are the dominant cost drivers,
			// especially in combination with LANDSCAPE (115-130s observed)
			if (descriptor & static_cast<uint32_t>(ShaderCache::LightingShaderFlags::TruePbr))
				priority += 500;
			if (descriptor & static_cast<uint32_t>(ShaderCache::LightingShaderFlags::AnisoLighting))
				priority += 300;
			// Deferred adds extra codegen overhead
			if (descriptor & static_cast<uint32_t>(ShaderCache::LightingShaderFlags::Deferred))
				priority += 200;

			// LANDSCAPE + TRUE_PBR combination triggers extreme register pressure
			// (6x unrolled texture layers * PBR params = 30+ textures, 180s+ compile)
			if ((technique == ShaderCache::LightingShaderTechniques::MTLand ||
					technique == ShaderCache::LightingShaderTechniques::MTLandLODBlend) &&
				(descriptor & static_cast<uint32_t>(ShaderCache::LightingShaderFlags::TruePbr)))
				priority += 500;
		}

		return priority;
	}

	std::optional<ShaderCompilationTask> CompilationSet::WaitTake(std::stop_token stoken)
	{
		std::unique_lock lock(compilationMutex);
		auto shaderCache = globals::shaderCache;
		if (!conditionVariable.wait(
				lock, stoken,
				[this, &shaderCache]() { return !availableTasks.empty() &&
			                                    // Use < (not <=) so dispatch never exceeds the limit.
			                                    static_cast<int32_t>(dispatchedTasksInFlight.load(std::memory_order_relaxed)) <
			                                        (!shaderCache->backgroundCompilation.load(std::memory_order_relaxed) ? shaderCache->compilationThreadCount : shaderCache->backgroundCompilationThreadCount); })) {
			/*Woke up because of a stop request. */
			return std::nullopt;
		}
		// Session state is managed by Add(), Complete(), and Forget(). This branch is
		// retained as a safety net but will not trigger because totalTasks is incremented
		// before the conditionVariable notification.
		if (!shaderCache->IsCompiling()) {
			QueryPerformanceCounter(&lastReset);
			lastResetQpc.store(lastReset.QuadPart, std::memory_order_relaxed);
			lastCalculation = lastReset;
		}

		// Startup policy: keep dispatching the hardest queued work first.
		// This preserves the existing priority score while preventing light tasks
		// from bypassing queued heavy shaders and stretching the tail.
		auto bestIt = availableTasks.end();
		if (!availableTasks.empty()) {
			bestIt = std::prev(availableTasks.end());
		}

		if (bestIt == availableTasks.end()) {
			return std::nullopt;
		}

		ShaderCompilationTask task = *bestIt;
		availableTasks.erase(bestIt);

		if (task.GetPriority() >= kHeavyPriorityThreshold) {
			heavyTasksInFlight.fetch_add(1, std::memory_order_relaxed);
		}

		tasksInProgress.insert(task);
		dispatchedTasksInFlight.fetch_add(1, std::memory_order_relaxed);
		return task;
	}

	void CompilationSet::ReleaseDispatchSlot()
	{
		{
			// Pair the decrement with WaitTake's predicate lock so the wake cannot
			// be lost between its capacity check and entering the wait.
			std::scoped_lock lock(compilationMutex);
			dispatchedTasksInFlight.fetch_sub(1, std::memory_order_relaxed);
		}
		conditionVariable.notify_one();
	}

	void CompilationSet::Add(const ShaderCompilationTask& task)
	{
		std::unique_lock lock(compilationMutex);
		auto inProgressIt = tasksInProgress.find(task);
		auto processedIt = processedTasks.find(task);
		if (inProgressIt == tasksInProgress.end() && processedIt == processedTasks.end() && !globals::shaderCache->GetCompletedShader(task)) {
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			auto queuedTask = task;
			queuedTask.SetEnqueuedQpc(now.QuadPart);
			queuedTask.SetGeneration(generation.load(std::memory_order_relaxed));
			auto [_, wasAdded] = availableTasks.insert(queuedTask);
			if (wasAdded) {
				// Increment counters inside the lock so that WaitTake, which reads
				// IsCompiling() after waking up, sees the updated totalTasks and
				// does NOT incorrectly treat the new work as a "fresh start" and
				// reset the session clock via its !IsCompiling() branch.
				// Only the first task after Clear() starts a session here. A drained queue
				// is ambiguous: later requests may all be disk hits, so rearming here would
				// emit a false completion for every small cache-hit burst. MarkPhaseStarted() rearms
				// only after it confirms a real compile; Forget() handles smart clears.
				if (totalTasks.load(std::memory_order_relaxed) == 0) {
					lastReset = now;
					lastResetQpc.store(lastReset.QuadPart, std::memory_order_relaxed);
					lastCalculation = lastReset;
				}

				totalTasks++;
				totalPriorityWeight += static_cast<uint64_t>(task.GetPriority()) + 1;
			}
			lock.unlock();
			if (wasAdded) {
				conditionVariable.notify_one();
			}
		}
	}

	void CompilationSet::Complete(const ShaderCompilationTask& task)
	{
		auto& cache = ShaderCache::Instance();
		auto key = task.GetString();
		auto shaderBlob = cache.GetCompletedShader(task);

		bool shouldLogCompletion = false;
		double completionTimeMs = 0.0;
		uint64_t completedSnapshot = 0;
		uint64_t failedSnapshot = 0;
		uint64_t totalSnapshot = 0;

		// Determine whether this task was resolved from the disk cache or actually compiled.
		bool wasDiskHit = cache.IsShaderLoadedFromDisk(key);

		// Perform all completion operations under one mutex acquisition
		{
			std::scoped_lock lock(compilationMutex);

			// A detached worker may finish after Clear(). Its counters belong to an
			// invalidated batch and must not corrupt the active session.
			if (!IsCurrentGeneration(task)) {
				return;
			}

			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);

			// Update task counters
			if (shaderBlob) {
				logger::debug("Compiling Task succeeded: {}", key);
				completedTasks++;
			} else {
				logger::debug("Compiling Task failed: {}", key);
				failedTasks++;
			}
			completedPriorityWeight += static_cast<uint64_t>(task.GetPriority()) + 1;

			// Track disk-cache hits separately so ETA can use compilation-only timing.
			if (wasDiskHit) {
				diskHitTasks++;
				diskHitPriorityWeight += static_cast<uint64_t>(task.GetPriority()) + 1;
			}

			// Track heavy task completion for P-core concurrency limiting
			if (task.GetPriority() >= kHeavyPriorityThreshold) {
				auto current = heavyTasksInFlight.load(std::memory_order_relaxed);
				while (current > 0 &&
					   !heavyTasksInFlight.compare_exchange_weak(current, current - 1,
						   std::memory_order_relaxed,
						   std::memory_order_relaxed)) {
				}
			}

			// Update timing
			totalTime.QuadPart += now.QuadPart - lastCalculation.QuadPart;
			lastCalculation = now;

			// Mixed batches log even when their last task is a disk hit. A disk-only
			// batch never starts a source-compilation phase and stays silent.
			if (completionTime.load(std::memory_order_relaxed) == 0 && completedTasks + failedTasks >= totalTasks) {
				completionTime.store(now.QuadPart, std::memory_order_relaxed);
				completionTimeMs = static_cast<double>(now.QuadPart - lastReset.QuadPart) * 1000.0 / frequency.QuadPart;
				shouldLogCompletion = compilationPhaseStarted.load(std::memory_order_relaxed);
				completedSnapshot = completedTasks.load(std::memory_order_relaxed);
				failedSnapshot = failedTasks.load(std::memory_order_relaxed);
				totalSnapshot = totalTasks.load(std::memory_order_relaxed);
			}

			// Update task tracking
			processedTasks.insert(task);
			tasksInProgress.erase(task);
		}

		// Keep info logging outside the lock and only for phases that performed
		// real compiler work; disk-cache-only startup remains quiet.
		if (shouldLogCompletion) {
			logger::info("Shader compilation completed: {}/{} tasks ({} failed) in {}",
				completedSnapshot, totalSnapshot, failedSnapshot, GetHumanTime(completionTimeMs));
			FlushShaderCacheManifest();
			cache.TryCompleteStartupCompilationPhase();
		}

		conditionVariable.notify_one();
	}

	void CompilationSet::IncCacheHit(std::optional<uint64_t> a_taskGeneration)
	{
		std::scoped_lock lock(compilationMutex);
		if (!a_taskGeneration || *a_taskGeneration == generation.load(std::memory_order_relaxed)) {
			cacheHitTasks.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void CompilationSet::IncSourceCompile(std::optional<uint64_t> a_taskGeneration)
	{
		std::scoped_lock lock(compilationMutex);
		if (!a_taskGeneration || *a_taskGeneration == generation.load(std::memory_order_relaxed)) {
			sourceCompileTasks.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void CompilationSet::MarkPhaseStarted(std::optional<uint64_t> a_taskGeneration)
	{
		bool shouldLog = false;
		uint64_t queuedAtPhaseStart = 0;
		{
			std::scoped_lock lock(compilationMutex);
			if (a_taskGeneration && *a_taskGeneration != generation.load(std::memory_order_relaxed)) {
				return;
			}

			// A real compile after a completed batch unambiguously begins a new phase.
			if (completionTime.load(std::memory_order_relaxed) != 0) {
				QueryPerformanceCounter(&lastReset);
				lastResetQpc.store(lastReset.QuadPart, std::memory_order_relaxed);
				lastCalculation = lastReset;
				totalTime = { 0 };
				completionTime.store(0, std::memory_order_relaxed);
				compilationPhaseStarted.store(false, std::memory_order_relaxed);
				compilationPhaseStart = { 0 };
			}

			if (!compilationPhaseStarted.load(std::memory_order_relaxed)) {
				QueryPerformanceCounter(&compilationPhaseStart);
				compilationPhaseStarted.store(true, std::memory_order_release);
				shouldLog = true;
				queuedAtPhaseStart = totalTasks.load(std::memory_order_relaxed);
			}
		}

		if (shouldLog) {
			logger::info("Shader compilation started ({} tasks queued)", queuedAtPhaseStart);
		}
	}

	void CompilationSet::BumpGeneration()
	{
		std::scoped_lock lock(compilationMutex);
		generation.fetch_add(1, std::memory_order_release);
	}

	void CompilationSet::Clear()
	{
		std::scoped_lock lock(compilationMutex);
		availableTasks.clear();
		tasksInProgress.clear();
		processedTasks.clear();
		totalTasks = 0;
		completedTasks = 0;
		failedTasks = 0;
		cacheHitTasks = 0;
		diskHitTasks = 0;
		sourceCompileTasks = 0;
		diskHitPriorityWeight = 0;
		compilationPhaseStarted = false;
		compilationPhaseStart = { 0 };
		generation.fetch_add(1, std::memory_order_relaxed);
		slowTasks = 0;
		verySlowTasks = 0;
		totalPriorityWeight = 0;
		completedPriorityWeight = 0;
		heavyTasksInFlight = 0;
		QueryPerformanceCounter(&lastReset);
		lastResetQpc.store(lastReset.QuadPart, std::memory_order_relaxed);
		QueryPerformanceCounter(&lastCalculation);
		completionTime = { 0 };  // Reset completion time
		totalTime = { 0 };
		{
			std::lock_guard slowLock(slowTasksMutex);
			slowTaskRecords.clear();
		}
	}

	void CompilationSet::Forget(const std::unordered_set<size_t>& a_taskIds)
	{
		if (a_taskIds.empty())
			return;

		auto matches = [&a_taskIds](const ShaderCompilationTask& task) {
			return a_taskIds.contains(task.GetId());
		};

		std::scoped_lock lock(compilationMutex);
		// Queued and in-flight work remain valid. Only completed bookkeeping can
		// block a safely evicted permutation from being requested again.
		const size_t erasedProcessed = std::erase_if(processedTasks, matches);

		// Smart clear resurrects processed tasks without calling Clear(), so Add()
		// sees cumulative counters rather than a zero-task session. Rearm only when
		// at least one completed task was actually forgotten and the prior session
		// had finished. In-flight work is preserved until Complete() retires it.
		if (erasedProcessed > 0 && completionTime.load(std::memory_order_relaxed) != 0) {
			QueryPerformanceCounter(&lastReset);
			lastResetQpc.store(lastReset.QuadPart, std::memory_order_relaxed);
			lastCalculation = lastReset;
			totalTime = { 0 };
			completionTime.store(0, std::memory_order_relaxed);
			compilationPhaseStarted.store(false, std::memory_order_relaxed);
			compilationPhaseStart = { 0 };
		}
	}

	bool CompilationSet::IsInProgress(size_t a_taskId)
	{
		std::scoped_lock lock(compilationMutex);
		return std::any_of(tasksInProgress.begin(), tasksInProgress.end(), [a_taskId](const ShaderCompilationTask& task) {
			return task.GetId() == a_taskId;
		});
	}

	std::string CompilationSet::GetHumanTime(double a_totalMs)
	{
		return Util::FormatDuration(a_totalMs);
	}

	double CompilationSet::GetEta()
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		const int64_t endQpc = (completionTime.load(std::memory_order_relaxed) != 0) ? completionTime.load(std::memory_order_relaxed) : now.QuadPart;

		// Helper: given elapsed time and done/total priority weights, return remaining ms (or 0).
		auto weightedEta = [](double elapsedMs, double doneW, double totalW) -> double {
			if (elapsedMs <= 0.0 || doneW <= 0.0 || totalW <= 0.0)
				return 0.0;
			double fraction = doneW / totalW;
			return std::max(elapsedMs / fraction - elapsedMs, 0.0);
		};

		const uint64_t diskWeight = diskHitPriorityWeight.load(std::memory_order_relaxed);
		const uint64_t totalWeight = totalPriorityWeight.load(std::memory_order_relaxed);
		const uint64_t doneWeight = completedPriorityWeight.load(std::memory_order_relaxed);

		if (diskWeight > 0) {
			// There are disk-cache hits in this session.
			if (!compilationPhaseStarted.load(std::memory_order_acquire)) {
				// Compilations haven't started yet (still loading from disk cache).
				// We have no compilation rate to extrapolate from, so return 0 to
				// avoid a wildly wrong ETA based purely on the fast disk-hit rate.
				return 0.0;
			}

			// At least one actual compilation has completed.  Use compilation-phase
			// timing so that fast disk loads at the start of the session don't inflate
			// the apparent progress rate and produce an underestimated ETA.
			const int64_t phaseStart = compilationPhaseStart.QuadPart;  // visible due to acquire above
			double compilationElapsedMs = static_cast<double>(endQpc - phaseStart) * 1000.0 / frequency.QuadPart;

			// Exclude disk-hit weight from both numerator and denominator so the
			// rate reflects only the actual compilation speed.
			double compiledDone = static_cast<double>(doneWeight > diskWeight ? doneWeight - diskWeight : 0);
			double compiledTotal = static_cast<double>(totalWeight > diskWeight ? totalWeight - diskWeight : 0);
			return weightedEta(compilationElapsedMs, compiledDone, compiledTotal);
		}

		// No disk hits: fall back to the original whole-session ETA.
		// Priority-weighted so heavy tasks completing early don't inflate the estimate.
		double elapsedMs = static_cast<double>(endQpc - lastReset.QuadPart) * 1000.0 / frequency.QuadPart;
		return weightedEta(elapsedMs, static_cast<double>(doneWeight), static_cast<double>(totalWeight));
	}

	std::string CompilationSet::GetStatsString(bool a_timeOnly, bool a_elapsedOnly)
	{
		// Calculate elapsed time since compilation started
		LARGE_INTEGER currentTime;
		QueryPerformanceCounter(&currentTime);

		// Use completion time if compilation is finished, otherwise current time
		int64_t endTime = (completionTime.load(std::memory_order_relaxed) != 0) ? completionTime.load(std::memory_order_relaxed) : currentTime.QuadPart;
		double totalMs = static_cast<double>(endTime - lastReset.QuadPart) * 1000.0 / frequency.QuadPart;

		if (a_timeOnly) {
			if (a_elapsedOnly) {
				// Only elapsed
				return GetHumanTime(totalMs);
			} else {
				// Elapsed + estimated
				return fmt::format("{}/{}",
					GetHumanTime(totalMs),
					GetHumanTime(GetEta() + totalMs));
			}
		}

		return fmt::format("{}/{} (successful/total)\tfailed: {}\tdeduplicated: {}\tdisk cache: {}\tsource compiles: {}\nElapsed/Estimated Time: {}/{}",
			(std::uint64_t)completedTasks,
			(std::uint64_t)totalTasks,
			(std::uint64_t)failedTasks,
			(std::uint64_t)cacheHitTasks,
			(std::uint64_t)diskHitTasks,
			(std::uint64_t)sourceCompileTasks,
			GetHumanTime(totalMs),
			GetHumanTime(GetEta() + totalMs));
	}

	UpdateListener::UpdateListener(ShaderFileDependencyTracker* deps_) :
		deps(deps_) {}

	void UpdateListener::UpdateCache(const std::filesystem::path& filePath, SIE::ShaderCache* cache, bool& fileDone)
	{
		fileDone = true;
		// Skip directories
		if (std::filesystem::is_directory(filePath)) {
			return;
		}
		// Extract file components
		const std::string extension = filePath.extension().string();
		const std::string shaderTypeString = filePath.stem().string();
		std::chrono::time_point<std::chrono::system_clock> modifiedTime{};
		auto shaderType = magic_enum::enum_cast<RE::BSShader::Type>(shaderTypeString, magic_enum::case_insensitive);
		// Check if the file exists and get its modified time
		if (std::filesystem::exists(filePath)) {
			modifiedTime = std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(filePath));
		} else {
			return;
		}

		// Ensure the file is not a directory and is a valid shader file (.hlsl)
		std::string lowerExtension = extension;
		std::transform(lowerExtension.begin(), lowerExtension.end(), lowerExtension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (!std::filesystem::is_directory(filePath) && lowerExtension == ".hlsl") {
			// Update cache with the modified shader
			cache->InsertModifiedShaderMap(shaderTypeString, modifiedTime);

			// Attempt to mark the shader for recompilation
			bool foundPath = cache->Clear(filePath.string());

			if (!foundPath) {
				// File was not found in the the map so check its shader type
				std::string parentDirName = filePath.parent_path().filename().string();
				std::transform(parentDirName.begin(), parentDirName.end(), parentDirName.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

				// Check if the parent directory name matches "shaders" in a case-insensitive way
				if (lowerExtension == ".hlsl" && parentDirName == "shaders" && shaderType.has_value()) {
					cache->Clear(shaderType.value());
				} else {
					// Independent programs are not stored in the engine replacement
					// cache. Deleting every engine blob cannot refresh them and only
					// creates unrelated recompilation work.
					logger::info(
						"[ShaderCacheAction] action=file-change result=not-persistent-engine-entry path={} diskCacheAction=none",
						filePath.string());
				}
			}
		}
		// Handle include file changes (.hlsli) by invalidating dependents
		else if (!std::filesystem::is_directory(filePath) && lowerExtension == ".hlsli") {
			// Normalize to absolute canonical path to match how dependencies are tracked
			std::error_code ec;
			auto canonicalPath = std::filesystem::weakly_canonical(filePath, ec);
			std::string pathStr = (ec ? filePath.string() : canonicalPath.string());
			// On Windows, normalize to lowercase to match TrackingIncludeHandler
#ifdef _WIN32
			std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
			// Invalidate all .hlsl files that depend on this .hlsli
			auto dependents = deps->GetDependents(pathStr);
			for (const auto& hlsl : dependents) {
				cache->Clear(hlsl);
			}
			if (dependents.empty()) {
				logger::info(
					"[ShaderCacheAction] action=include-change result=no-loaded-dependent path={} diskCacheAction=none",
					filePath.string());
			}
		}
		// Indicate that file processing is not yet complete
		fileDone = false;
	}

	void UpdateListener::processQueue()
	{
		// File observation is latency-sensitive but not CPU-intensive. Do not inherit
		// the compiler workers' Idle priority under a High-priority game process.
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
		std::unique_lock lock(actionMutex, std::defer_lock);
		auto cache = globals::shaderCache;
		while (cache->UseFileWatcher()) {
			lock.lock();
			if (!queue.empty()) {
				if (IsSaveLoadSafeModeActive()) {
					lock.unlock();
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					continue;
				}

				for (fileAction fAction : queue) {
					const std::filesystem::path filePath = std::filesystem::path(std::format("{}\\{}", fAction.dir, fAction.filename));
					bool fileDone = false;
					switch (fAction.action) {
					case efsw::Actions::Add:
						logger::debug("Detected Added path {}", filePath.string());
						UpdateCache(filePath, cache, fileDone);
						break;
					case efsw::Actions::Delete:
						logger::debug("Detected Deleted path {}", filePath.string());
						break;
					case efsw::Actions::Modified:
						if (!std::filesystem::is_directory(filePath)) {
							logger::debug("Detected Changed path {}", filePath.string());
						}
						UpdateCache(filePath, cache, fileDone);
						break;
					case efsw::Actions::Moved:
						logger::debug("Detected Moved path {}", filePath.string());
						break;
					default:
						logger::error("Filewatcher received invalid action {}", magic_enum::enum_name(fAction.action));
					}
					if (fileDone)
						continue;
				}
				queue.clear();
			}
			lock.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		queue.clear();
	}

	void UpdateListener::handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action, std::string oldFilename)
	{
		std::lock_guard lock(actionMutex);
		if (queue.empty() || (queue.back().action != action && queue.back().filename != filename)) {
			// only add if not a duplicate; esfw is very spammy
			queue.push_back({ watchid, dir, filename, action, oldFilename });
		}
	}
}
