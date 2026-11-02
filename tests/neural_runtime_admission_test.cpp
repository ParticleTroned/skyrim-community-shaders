#include "Features/Upscaling/NeuralRendering/NetworkSelection.h"
#include "Features/Upscaling/NeuralRendering/TuningLimits.h"

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
	int CreateFileW(const std::filesystem::path::value_type*, int, int, void*, int, int, void*) { return 1; }
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
	struct InstalledRuntimeMetadata
	{
		static std::uint64_t AllocationVersion(const REL::Version&) { return 1; }
	};
	enum class RuntimeStatus
	{
		Ready,
		NotFound,
		HashFailed,
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
		unsigned ngxResult_ = 0;
		bool ShutdownLocked() { return true; }
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
	for (const auto version : { REL::Version{ 310, 8 }, REL::Version{ 310, 9, 16 }, REL::Version{ 311, 0 } }) {
		for (const char* hash : { "sdk-reference", "different-user-supplied-runtime" }) {
			Util::installedVersion = version;
			installedHash = hash;
			Runtime runtime;
			require(runtime.ProbeLocked("nvngx_dlssnr.dll"), "Compatible user runtime was rejected");
			require(runtime.hash_ == hash && runtime.trust_ == RuntimeTrust::VersionAccepted,
				"Runtime fingerprint must remain diagnostic, without claiming package verification");
			require(runtime.networkSelectionSupported_ == (version.major() == 310 && version.minor() == 9),
				"Admission changed the version-specific preset policy");
		}
	}
	Util::installedVersion = REL::Version{ 310, 7 };
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
}
