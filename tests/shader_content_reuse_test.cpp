#include "Api/ShaderCompatibilityRegistry.h"
#include "Utils/ContentHash.h"
#include "Utils/ShaderCacheManifest.h"
#include "Utils/ShaderCachePack.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <d3dcompiler.h>
#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <shared_mutex>
#include <winrt/base.h>

namespace logger
{
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
	template <class... Args>
	void info(const char*, Args&&...)
	{}
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}
namespace Util
{
	std::string WStringToString(const std::wstring& a_value) { return std::filesystem::path(a_value).string(); }
	namespace FileHelpers
	{
		bool failWrite = false;
		bool WriteTextFileAtomic(const std::filesystem::path& a_path, std::string_view a_bytes, std::string& a_error, bool)
		{
			if (failWrite) {
				a_error = "injected write failure";
				return false;
			}
			std::filesystem::create_directories(a_path.parent_path());
			std::ofstream stream(a_path, std::ios::binary | std::ios::trunc);
			stream.write(a_bytes.data(), a_bytes.size());
			stream.close();
			return static_cast<bool>(stream);
		}
	}
}
namespace BuildProvenance
{
	std::string abi = "global-v1";
	const std::string& GetShaderCacheAbiId() { return abi; }
}
namespace REL
{
	struct Module
	{
		static inline bool vr = false;
		static bool IsVR() { return vr; }
	};
}
struct State
{
	struct ShaderDefinesSnapshot
	{
		std::string canonicalText;
	};
	bool developer = false;
	std::atomic_bool enablePartialPrecision = false, enableAvoidFlowControl = false;
	std::shared_ptr<ShaderDefinesSnapshot> defines = std::make_shared<ShaderDefinesSnapshot>();
	bool IsDeveloperMode() const { return developer; }
	auto GetShaderDefinesSnapshot() const { return defines; }
};
struct Feature
{
	bool loaded = true;
	std::string abi = "feature-v1";
	std::string GetShaderCacheAbiVersion() const { return abi; }
	std::string GetShortName() const { return "Test"; }
	static std::vector<Feature*>& GetFeatureList()
	{
		static std::vector<Feature*> list;
		return list;
	}
};
namespace globals
{
	State* state;
}

namespace
{
	using namespace Util;
	using ShaderContentReuse::Identity;
	ShaderCacheManifest::Manifest cacheManifest;
	ShaderCachePack::Store* activePack = nullptr;
	bool managed = false, quarantined = false;
	std::shared_mutex g_diskCacheMutationMutex;
	std::mutex g_standaloneCacheIOMutex;
	std::atomic<uint64_t> diskGeneration = 0, g_manifestWriteCount = 0;
	constexpr uint64_t kManifestFlushBatchSize = 25;
	ContentHash::Hash128 testSourceDigest = ContentHash::HashString("source-v1");

	const std::filesystem::path& DiskCachePath()
	{
		static const std::filesystem::path path = "Data/ShaderCache";
		return path;
	}
	const std::filesystem::path& PreviousDiskCachePath()
	{
		static const std::filesystem::path path = "Data/ShaderCache.Previous";
		return path;
	}
	uint64_t GetDiskCacheGeneration() { return diskGeneration; }
	bool ManagedShaderPackLayoutPresent() { return managed; }
	auto* GetShaderPackStore(bool) { return quarantined ? nullptr : activePack; }
	void QuarantineShaderPackLane(bool, std::string_view) { quarantined = true; }
	auto& GetShaderCacheManifest() { return cacheManifest; }
	void FlushShaderCacheManifestLocked() { assert(cacheManifest.Save()); }
	std::optional<ContentHash::Hash128> GetShaderContentDigest(const std::filesystem::path&, const std::filesystem::path&) { return testSourceDigest; }
	std::filesystem::path ShaderSourceRoot() { return "Data/Shaders"; }
	std::string GetShaderPackFamily(const std::wstring&) { return "Lighting"; }

	struct ShaderCache
	{
		bool active = true, skipUnchanged = true;
		static ShaderCache& Instance()
		{
			static ShaderCache cache;
			return cache;
		}
		bool IsDiskCacheActive() const { return active; }
		bool IsSkipUnchangedShaders() const { return skipUnchanged; }
	};

	// clang-format off
#include "content_blob_io.h"
#include "content_verified_read.h"
#include "content_record_digest.h"
#include "content_pack_metadata.h"
#include "content_pack_reuse.h"
#include "content_persist.h"
#include "content_abi_snapshot.h"
	// clang-format on

	struct Fixture
	{
		std::filesystem::path oldWorkingDirectory = std::filesystem::current_path();
		std::filesystem::path root = std::filesystem::temp_directory_path() /
		                             ("csx-content-reuse-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		Fixture()
		{
			std::filesystem::create_directories(root);
			std::filesystem::current_path(root);
			std::filesystem::create_directories(DiskCachePath() / "Lighting");
			std::filesystem::create_directories(PreviousDiskCachePath() / "Lighting");
			cacheManifest.Load(DiskCachePath() / "Manifest.json");
			g_previousManifest.reset();
			++diskGeneration;
			activePack = nullptr;
			managed = quarantined = false;
			ShaderCache::Instance().active = ShaderCache::Instance().skipUnchanged = true;
		}
		~Fixture()
		{
			activePack = nullptr;
			std::filesystem::current_path(oldWorkingDirectory);
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}
	};

	std::string_view Bytes(ID3DBlob* a_blob)
	{
		return { static_cast<const char*>(a_blob->GetBufferPointer()), a_blob->GetBufferSize() };
	}
	winrt::com_ptr<ID3DBlob> Blob()
	{
		constexpr std::string_view text = "float4 main() : SV_Target { return 1; }";
		winrt::com_ptr<ID3DBlob> blob, errors;
		assert(SUCCEEDED(D3DCompile(text.data(), text.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, blob.put(), errors.put())));
		return blob;
	}
	Identity CodeIdentity(std::string_view a_code = "code")
	{
		return { ContentHash::HashString(a_code).ToHex(), ContentHash::HashString("abi").ToHex(),
			CSX::Api::GetShaderCompatibilityRequirementSet("Lighting", "Lighting.hlsl").digest };
	}

	void ManifestLifecycle()
	{
		Fixture fixture;
		const auto record = ShaderContentReuse::Bind(CodeIdentity(), std::string_view("a\0b", 3));
		cacheManifest.Set("Lighting/a.pso", "digest", record);
		assert(cacheManifest.Save());
		ShaderCacheManifest::Manifest loaded;
		loaded.Load(DiskCachePath() / "Manifest.json");
		assert(loaded.GetContentKey("Lighting/a.pso") == record);
		assert(loaded.Get("Lighting/a.pso") == "digest");
		loaded.Set("Lighting/a.pso", "digest");
		assert(!loaded.GetContentKey("Lighting/a.pso"));
		loaded.Set("Lighting/a.pso", "digest", record);
		assert(loaded.Erase("Lighting/a.pso"));
		assert(!loaded.GetContentKey("Lighting/a.pso"));
		loaded.Set("Lighting/a.pso", "digest", record);
		assert(loaded.PruneIf([](const auto&) { return true; }) == 1);
		assert(!loaded.GetContentKey("Lighting/a.pso"));
		loaded.Set("Lighting/a.pso", "digest", record);
		loaded.Clear();
		assert(!loaded.GetContentKey("Lighting/a.pso"));
		loaded.Load(DiskCachePath() / "Manifest.json");
		loaded.Load("missing.json");
		assert(!loaded.GetContentKey("Lighting/a.pso"));

		auto json = nlohmann::json{ { "schemaVersion", 2 }, { "entries", { { "legacy", "digest" } } },
			{ "contentKeys", { { "legacy", "unbound-key" }, { "orphan", ShaderContentReuse::Serialize(record) } } } };
		std::ofstream("legacy.json") << json.dump();
		loaded.Load("legacy.json");
		assert(loaded.Get("legacy") == "digest" && !loaded.GetContentKey("legacy") && !loaded.GetContentKey("orphan"));
		for (const auto* field : { "key", "abi", "compatibility", "bytecodeHash" }) {
			auto invalid = ShaderContentReuse::Serialize(record);
			invalid[field] = 42;
			assert(!ShaderContentReuse::Parse(invalid));
			invalid[field] = "not-a-digest";
			assert(!ShaderContentReuse::Parse(invalid));
		}
	}

	void LooseAndRollback()
	{
		Fixture fixture;
		const auto blob = Blob();
		const auto identity = CodeIdentity();
		const auto oldCompile = ContentHash::HashString("old-defines");
		const auto newCompile = ContentHash::HashString("new-defines");
		const std::wstring path = L"Data/ShaderCache/Lighting/1.pso";
		const auto generation = GetDiskCacheGeneration();
		assert(SaveShaderBlobToDisk(blob.get(), false, path, "Lighting.hlsl", oldCompile, oldCompile, testSourceDigest, generation, identity));
		assert(cacheManifest.Save());
		// The previous lane is immutable within a cache generation.
		const auto previous = PreviousDiskCachePath() / "Lighting/1.pso";
		std::filesystem::copy_file(path, previous);
		std::filesystem::copy_file(DiskCachePath() / "Manifest.json", PreviousDiskCachePath() / "Manifest.json");
		ContentReuseSource origin;
		assert(FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation, origin));
		assert(origin == ContentReuseSource::ActiveCache);
		auto changed = identity;
		changed.abi = ContentHash::HashString("different-abi").ToHex();
		assert(!FindReusableShaderBlob(changed, false, path, "Lighting.hlsl", newCompile, generation, origin));
		changed = CodeIdentity("different-code");
		assert(!FindReusableShaderBlob(changed, false, path, "Lighting.hlsl", newCompile, generation, origin));
		changed = identity;
		changed.compatibility = CryptoHash::Sha256Hex("different-provider");
		assert(!FindReusableShaderBlob(changed, false, path, "Lighting.hlsl", newCompile, generation, origin));
		ShaderCache::Instance().skipUnchanged = false;
		assert(!FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation, origin));
		ShaderCache::Instance().skipUnchanged = true;
		ShaderCache::Instance().active = false;
		assert(!FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation, origin));
		ShaderCache::Instance().active = true;
		assert(!FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation - 1, origin));

		const auto timestamp = std::filesystem::last_write_time(path);
		std::string wrong(Bytes(blob.get()));
		wrong.back() ^= 1;
		std::ofstream(std::filesystem::path(path), std::ios::binary | std::ios::trunc).write(wrong.data(), wrong.size());
		std::filesystem::last_write_time(path, timestamp);
		assert(ReadCachedShaderBlob(path));
		assert(!ReadVerifiedCachedShaderBlob(path));
		assert(FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation, origin));
		assert(origin == ContentReuseSource::PreviousCache);
		managed = true;
		assert(!FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation, origin));
		managed = false;

		const auto before = cacheManifest.GetContentKey("Lighting/1.pso");
		FileHelpers::failWrite = true;
		assert(!SaveShaderBlobToDisk(blob.get(), false, path, "Lighting.hlsl", newCompile, newCompile, testSourceDigest, generation, changed));
		assert(cacheManifest.GetContentKey("Lighting/1.pso") == before);
		FileHelpers::failWrite = false;
		++diskGeneration;
		assert(!SaveShaderBlobToDisk(blob.get(), false, path, "Lighting.hlsl", newCompile, newCompile, testSourceDigest, generation, changed));
		ShaderCacheManifest::Manifest previousManifest;
		previousManifest.Load(PreviousDiskCachePath() / "Manifest.json");
		previousManifest.Clear();
		previousManifest.Set("Lighting/1.pso", "legacy");
		assert(previousManifest.Save());
		assert(!FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, GetDiskCacheGeneration(), origin));
		assert(!FindReusableShaderBlob(identity, false, L"Data/ShaderCache/../outside.pso", "Lighting.hlsl", newCompile, GetDiskCacheGeneration(), origin));
	}

	void ManagedRecords()
	{
		Fixture fixture;
		const auto blob = Blob();
		const auto identity = CodeIdentity();
		const auto oldCompile = ContentHash::HashString("old-defines");
		const auto newCompile = ContentHash::HashString("new-defines");
		const std::wstring path = L"Data/ShaderCache/Lighting/1.pso";
		const auto a = fixture.root / "a.csxpack", b = fixture.root / "b.csxpack";
		std::ofstream(a, std::ios::binary).close();
		std::ofstream(b, std::ios::binary).close();
		ShaderCachePack::PackSetId setId{};
		setId[0] = std::byte{ 1 };
		ShaderCachePack::Store store(a, b, ShaderCachePack::Lane::Optimized, setId);
		std::string error;
		assert(store.InitializeEmptyFilesAndOpen(&error));
		activePack = &store;
		managed = true;
		const auto generation = GetDiskCacheGeneration();
		assert(SaveShaderBlobToDisk(blob.get(), false, path, "Lighting.hlsl", oldCompile, oldCompile, testSourceDigest, generation, identity));
		assert(store.Checkpoint(&error));
		winrt::com_ptr<ID3DBlob> exact;
		exact.attach(LoadShaderBlobFromPack(store, false, path, "Lighting.hlsl", oldCompile));
		assert(exact && !quarantined);
		ContentReuseSource origin;
		assert(FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation, origin));
		assert(origin == ContentReuseSource::ActiveCache);
		assert(!std::filesystem::exists(path));

		const auto other = CodeIdentity("changed-code");
		assert(SaveShaderBlobToDisk(blob.get(), false, path, "Lighting.hlsl", newCompile, newCompile, testSourceDigest, generation, other));
		assert(store.Checkpoint(&error));
		store.Close();
		assert(store.Open(&error));
		assert(FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation, origin));
		assert(store.Compact(&error));
		assert(FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation, origin));

		auto invalid = identity;
		invalid.compatibility = CryptoHash::Sha256Hex("changed-provider");
		assert(!SaveShaderBlobToDisk(blob.get(), false, path, "Lighting.hlsl", oldCompile, oldCompile, testSourceDigest, generation, invalid));
		const auto old = BuildShaderPackIdentity(path, "Lighting.hlsl", oldCompile, testSourceDigest);
		auto tampered = store.Find(old->exactKey, &error);
		assert(tampered);
		auto metadata = nlohmann::json::parse(tampered->metadata);
		metadata["contentReuse"]["bytecodeHash"] = std::string(64, '0');
		tampered->metadata = metadata.dump();
		assert(store.Append(*tampered, &error));
		assert(!FindReusableShaderBlob(identity, false, path, "Lighting.hlsl", newCompile, generation, origin));
		assert(quarantined);
	}

	void AbiAndCompatibility()
	{
		State state;
		globals::state = &state;
		Feature feature;
		Feature::GetFeatureList() = { &feature };
		const auto enabled = CaptureGlobalCompileState();
		feature.loaded = false;
		const auto disabled = CaptureGlobalCompileState();
		assert(enabled.digest != disabled.digest && enabled.reuseAbiDigest == disabled.reuseAbiDigest);
		feature.abi = "feature-v2";
		assert(CaptureGlobalCompileState().reuseAbiDigest != disabled.reuseAbiDigest);
		feature.abi = "feature-v1";
		REL::Module::vr = true;
		assert(CaptureGlobalCompileState().reuseAbiDigest != disabled.reuseAbiDigest);
		REL::Module::vr = false;
		BuildProvenance::abi = "global-v2";
		assert(CaptureGlobalCompileState().reuseAbiDigest != disabled.reuseAbiDigest);
		BuildProvenance::abi = "global-v1";
		Feature::GetFeatureList().clear();

		CSX::Api::ShaderCompatibilityRegistration provider;
		provider.identity = "org.example.test";
		provider.contractMajor = 1;
		provider.currentMinor = provider.minimumCompatibleMinor = 1;
		provider.maximumCompatibleMinor = 3;
		provider.resourceFingerprint = "bindings-v1";
		auto oldSet = CSX::Api::BuildShaderCompatibilityRequirementSet({ provider });
		provider.currentMinor = provider.minimumCompatibleMinor = 2;
		auto newSet = CSX::Api::BuildShaderCompatibilityRequirementSet({ provider });
		const auto blob = Blob();
		auto identity = CodeIdentity();
		identity.compatibility = oldSet.digest;
		auto metadata = nlohmann::json{
			{ "schemaVersion", 3 }, { "contentContract", "old-defines" },
			{ "compatibilityDomain", oldSet.domainCanonical }, { "compatibilityRequirementSet", oldSet.canonical },
			{ "compatibilityRanges", SerializeCompatibilityRanges(oldSet) },
			{ "contentReuse", ShaderContentReuse::Serialize(ShaderContentReuse::Bind(identity, Bytes(blob.get()))) }
		};
		ShaderPackIdentity request;
		request.contentContract = "new-defines";
		request.compatibility = newSet;
		identity.compatibility = newSet.digest;
		assert(!IsCompatibleShaderPackMetadata(metadata.dump(), request));
		assert(IsCompatibleShaderPackMetadata(metadata.dump(), request, identity));
		provider.currentMinor = provider.minimumCompatibleMinor = provider.maximumCompatibleMinor = 4;
		request.compatibility = CSX::Api::BuildShaderCompatibilityRequirementSet({ provider });
		assert(!IsCompatibleShaderPackMetadata(metadata.dump(), request, identity));
		metadata["contentReuse"]["compatibility"] = std::string(64, '0');
		request.compatibility = newSet;
		assert(!IsCompatibleShaderPackMetadata(metadata.dump(), request, identity));
	}
}

int main()
{
	ManifestLifecycle();
	LooseAndRollback();
	ManagedRecords();
	AbiAndCompatibility();
}
