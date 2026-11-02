#include "Features/Upscaling/NeuralRendering/NetworkSelection.h"
#include "Features/Upscaling/NeuralRendering/NgxFeaturePath.h"
#include "Features/Upscaling/NeuralRendering/TuningLimits.h"
#include "NeuralRenderingReference.generated.h"

#include <array>
#include <atomic>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace REL
{
	struct Version
	{
		std::array<unsigned, 4> values;
		constexpr Version(unsigned major, unsigned minor, unsigned patch = 0, unsigned build = 0) : values{ major, minor, patch, build } {}
		auto operator<=>(const Version&) const = default;
		unsigned major() const { return values[0]; }
		unsigned minor() const { return values[1]; }
	};
}
namespace Util
{
	std::optional<REL::Version> installedVersion;
	auto GetDllVersion(const std::wstring&) { return installedVersion; }
	std::string GetFormattedVersion(const REL::Version& version)
	{
		return std::format("{}.{}.{}.{}", version.values[0], version.values[1], version.values[2], version.values[3]);
	}
}
namespace NeuralRendering
{
#include "neural_runtime_minimum_under_test.h"
	constexpr int INVALID_HANDLE_VALUE = -1;
	constexpr int GENERIC_READ = 1, FILE_SHARE_READ = 2, OPEN_EXISTING = 3, FILE_ATTRIBUTE_NORMAL = 4, FILE_FLAG_SEQUENTIAL_SCAN = 8;
	int CreateFileW(const wchar_t*, int, int, void*, int, int, void*) { return 1; }
	unsigned GetLastError() { return 0; }
	struct ScopedHandle
	{
		int value = INVALID_HANDLE_VALUE;
		explicit ScopedHandle(int handle = INVALID_HANDLE_VALUE) : value(handle) {}
		int Get() const { return value; }
		int Release()
		{
			const auto handle = value;
			value = INVALID_HANDLE_VALUE;
			return handle;
		}
		void Reset(int handle) { value = handle; }
	};
	std::string installedHash;
	bool hashReadable = true;
	bool ComputeFileSha256(int, std::string& hash, std::string& error)
	{
		if (!hashReadable) {
			error = "unreadable provider";
			return false;
		}
		hash = installedHash;
		return true;
	}
	std::filesystem::path ResolveRuntimePath(const std::filesystem::path& path) { return path; }
	bool featureDirectoryResolved = true;
	bool ResolveNgxFeatureDirectory(int, std::wstring& directory, std::string& error)
	{
		if (!featureDirectoryResolved) {
			error = "physical provider path unavailable";
			return false;
		}
		directory = L"physical-provider-directory";
		return true;
	}
	struct InstalledRuntimeMetadata
	{
		static std::uint64_t AllocationVersion(const REL::Version&) { return 1; }
	};
	enum class RuntimeStatus
	{
		Ready,
		NotFound,
		HashFailed,
		LoadFailed,
		VersionRejected
	};
	enum class RuntimeFailureStage
	{
		None,
		Discovery,
		Hash,
		Version
	};
	enum class RuntimeTrust
	{
		Unknown,
		VersionAccepted
	};
	struct Runtime
	{
		struct Backend
		{
			ScopedHandle providerFile;
			std::wstring featureDirectory;
		};
		static constexpr const char* kMissingRuntimeNotice = "Missing NR DLL";
		std::atomic<bool> abandonRequested_ = false;
		bool abandoned_ = false, probeLogEmitted_ = false;
		std::filesystem::path path_;
		std::unique_ptr<Backend> backend_;
		std::string hash_, version_, detail_;
		std::uint64_t allocationVersion_ = 0;
		std::optional<TuningLimits> tuningLimits_;
		std::optional<bool> networkSelectionSupported_;
		RuntimeStatus status_ = RuntimeStatus::NotFound;
		RuntimeFailureStage failureStage_ = RuntimeFailureStage::None;
		RuntimeTrust trust_ = RuntimeTrust::Unknown;
		ProviderIdentity identity_;
		unsigned ngxResult_ = 0;
		bool ShutdownLocked()
		{
			identity_ = {};
			hash_.clear();
			return true;
		}
		void SetFailureLocked(RuntimeStatus status, RuntimeFailureStage stage, std::string detail)
		{
			status_ = status;
			failureStage_ = stage;
			detail_ = std::move(detail);
		}
		void LogOnceLocked(bool&, const char*, bool) {}
		bool ProbeLocked(const std::filesystem::path&);
	};
#include "neural_runtime_probe_under_test.h"
}

int main()
{
	using namespace NeuralRendering;
	const auto require = [](bool passed, const char* message) {
		if (!passed)
			throw std::runtime_error(message);
	};
	require(NormalizeNgxFeaturePath(LR"(\\?\D:\Mods\Neural Rendering\nvngx_dlssnr.dll)") == LR"(D:\Mods\Neural Rendering\nvngx_dlssnr.dll)",
		"NGX needs an ordinary physical drive path");
	require(NormalizeNgxFeaturePath(LR"(\\?\UNC\server\mods\nvngx_dlssnr.dll)") == LR"(\\server\mods\nvngx_dlssnr.dll)" &&
				NormalizeNgxFeaturePath(LR"(\\?\unc\server\mods\nvngx_dlssnr.dll)") == LR"(\\server\mods\nvngx_dlssnr.dll)",
		"NGX needs an ordinary UNC path without losing its server prefix");
	require(NormalizeNgxFeaturePath(LR"(D:\Mods\nvngx_dlssnr.dll)") == LR"(D:\Mods\nvngx_dlssnr.dll)" &&
				NormalizeNgxFeaturePath(LR"(\\?\Volume{test}\nvngx_dlssnr.dll)").empty(),
		"Ordinary paths are retained and unsupported extended namespaces fail explicitly");
	const std::string otherHash(64, '0');
	std::string uppercaseReference(kProviderReference.sha256);
	for (auto& c : uppercaseReference)
		if (c >= 'a' && c <= 'f')
			c -= 'a' - 'A';
	require(ClassifyProviderRelease(uppercaseReference, kProviderReference.sha256) == ProviderReleaseMatch::PinnedReference,
		"Digest comparison must accept uppercase hex");
	for (const auto hash : { std::string{}, std::string(63, 'a'), std::string(65, 'a'), std::string(64, 'x') })
		require(ClassifyProviderRelease(hash, kProviderReference.sha256) == ProviderReleaseMatch::Unknown &&
					ClassifyProviderRelease(kProviderReference.sha256, hash) == ProviderReleaseMatch::Unknown,
			"Missing or malformed digests cannot establish identity");
	for (const auto version : { REL::Version{ 310, 8 }, REL::Version{ 310, 9, 16 }, REL::Version{ 311, 0 } }) {
		for (const auto hash : { kProviderReference.sha256, std::string_view(otherHash) }) {
			Util::installedVersion = version;
			installedHash = hash;
			Runtime runtime;
			require(runtime.ProbeLocked("nvngx_dlssnr.dll"), "Compatible user runtime was rejected");
			require(runtime.hash_ == hash && runtime.trust_ == RuntimeTrust::VersionAccepted,
				"Reference identity must not change metadata admission");
			const auto expectedMatch = hash == kProviderReference.sha256 ? ProviderReleaseMatch::PinnedReference : ProviderReleaseMatch::NoMatch;
			require(runtime.identity_.releaseMatch == expectedMatch && runtime.identity_.initialization == ProviderInitialization::NotAttempted,
				"Metadata admission must distinguish reference identity from native initialization");
			require(runtime.detail_.find(ToString(expectedMatch)) != std::string::npos &&
						runtime.detail_.find("signature not checked") != std::string::npos &&
						runtime.detail_.find(kProviderReference.source) != std::string::npos,
				"Probe diagnostics must identify their evidence without inventing signature verification");
			require(runtime.backend_->featureDirectory == L"physical-provider-directory", "Admission lost the physical NGX search directory");
			require(runtime.networkSelectionSupported_ == (version.major() == 310 && version.minor() == 9),
				"Admission changed the version-specific preset policy");
		}
	}
	Util::installedVersion = REL::Version{ 310, 7 };
	featureDirectoryResolved = false;
	Runtime unresolvedPath;
	require(!unresolvedPath.ProbeLocked("nvngx_dlssnr.dll") && unresolvedPath.status_ == RuntimeStatus::LoadFailed &&
				unresolvedPath.detail_ == "physical provider path unavailable",
		"An unresolved physical provider must not silently fall back to a virtual NGX search path");
	featureDirectoryResolved = true;
	Runtime old;
	require(!old.ProbeLocked("nvngx_dlssnr.dll") && old.status_ == RuntimeStatus::VersionRejected, "Legacy minimum version was lost");
	Util::installedVersion.reset();
	Runtime unknown;
	require(!unknown.ProbeLocked("nvngx_dlssnr.dll") && unknown.status_ == RuntimeStatus::VersionRejected, "Missing version must fail explicitly");
	Runtime absent;
	require(!absent.ProbeLocked({}) && absent.detail_ == Runtime::kMissingRuntimeNotice, "Missing DLL notice was lost");
	hashReadable = false;
	Runtime unreadable;
	require(!unreadable.ProbeLocked("nvngx_dlssnr.dll") && unreadable.status_ == RuntimeStatus::HashFailed, "File read failure was concealed");
	require(unreadable.identity_.releaseMatch == ProviderReleaseMatch::Unknown, "Failed hashing claimed a release identity");
	hashReadable = true;
	installedHash = kProviderReference.sha256;
	Runtime rejectedReference;
	require(!rejectedReference.ProbeLocked("nvngx_dlssnr.dll") &&
				rejectedReference.identity_.releaseMatch == ProviderReleaseMatch::PinnedReference,
		"A known digest must not override missing version metadata or lose its independent evidence");
	require(!rejectedReference.ProbeLocked({}) && rejectedReference.identity_.releaseMatch == ProviderReleaseMatch::Unknown,
		"A new failed probe retained stale release evidence");
}
