#include "Features/Upscaling/NeuralRendering/DriverSupport.h"
#include "Features/Upscaling/NeuralRendering/NgxCapabilityInfo.h"
#include "neural_sdk_parameters_under_test.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace NeuralRendering
{
	using NVSDK_NGX_Result = std::uint32_t;
	constexpr NVSDK_NGX_Result kSuccess = 1, kFailure = 0xBAD00010;
	constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_OutOfDate = 0xBAD0000C;
	constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_InvalidParameter = 0xBAD00005;
	constexpr auto kRuntimeName = L"nvngx_dlssnr.dll";
	constexpr int E_INVALIDARG = -1;
	constexpr bool NVSDK_NGX_FAILED(NVSDK_NGX_Result value) { return (value & 0xFFF00000) == 0xBAD00000; }

	std::vector<std::string> calls;
	DriverSupport installedDriver{ kMinimumNvidiaDriverRelease };
	struct Behavior
	{
		NVSDK_NGX_Result init = kSuccess, capability = kSuccess, populate = kSuccess, allocate = kSuccess;
		NVSDK_NGX_Result destroy = kSuccess, shutdown = kSuccess;
		NVSDK_NGX_Result driverQuery = kSuccess, majorQuery = kSuccess, minorQuery = kSuccess;
		int needsUpdate = 0;
		bool partialAllocation = false, partialCapability = false, nullAllocation = false, unload = true;
	} behavior;
	struct ID3D12Device
	{
		unsigned references = 0;
		void AddRef() { ++references; }
		void Release() { --references; }
	};
	struct NVSDK_NGX_Parameter
	{
		bool capabilities = false;
		NVSDK_NGX_Result Get(const char* key, int* value)
		{
			if (std::string_view(key) != NVSDK_NGX_Parameter_DLSSNR_NeedsUpdatedDriver)
				throw std::runtime_error("Legacy admission unexpectedly queried SDK feature discovery");
			*value = behavior.needsUpdate;
			return behavior.driverQuery;
		}
		NVSDK_NGX_Result Get(const char* key, unsigned* value)
		{
			if (std::string_view(key) == NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMajor) {
				*value = 616;
				return behavior.majorQuery;
			}
			if (std::string_view(key) == NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMinor) {
				*value = 92;
				return behavior.minorQuery;
			}
			return kFailure;
		}
	} capabilityMap{ true }, parameterMap;

	struct DirectProvider
	{
		bool active = false;
		unsigned allocations = 0, liveMaps = 0;
		NVSDK_NGX_Result Initialize(const std::filesystem::path& path, int, ID3D12Device*,
			const std::filesystem::path&, std::string& detail)
		{
			calls.emplace_back("direct_init");
			if (path.parent_path().filename() != L"PhysicalMod" || path.filename() != kRuntimeName)
				throw std::runtime_error("Legacy route did not use the locked physical provider");
			active = behavior.init == kSuccess;
			detail = "legacy fixture initialized";
			return behavior.init;
		}
		NVSDK_NGX_Result AllocateParameters(NVSDK_NGX_Parameter** map, std::string& detail)
		{
			const bool capability = allocations++ == 0;
			calls.emplace_back(capability ? "allocate_caps" : "allocate_params");
			const auto result = capability ? behavior.capability : behavior.allocate;
			*map = result == kSuccess || (capability ? behavior.partialCapability : behavior.partialAllocation) ? (capability ? &capabilityMap : &parameterMap) : nullptr;
			if (!capability && behavior.nullAllocation)
				*map = nullptr;
			if (*map)
				++liveMaps;
			detail = "fixture allocator";
			return result;
		}
		NVSDK_NGX_Result PopulateParameters(NVSDK_NGX_Parameter* map)
		{
			calls.emplace_back(map->capabilities ? "populate_caps" : "populate_params");
			return behavior.populate;
		}
		NVSDK_NGX_Result DestroyParameters(NVSDK_NGX_Parameter* map)
		{
			calls.emplace_back(map->capabilities ? "destroy_caps" : "destroy_params");
			if (behavior.destroy == kSuccess)
				--liveMaps;
			return behavior.destroy;
		}
		NVSDK_NGX_Result ShutdownDevice(ID3D12Device*)
		{
			if (liveMaps != 0)
				throw std::runtime_error("Provider shutdown preceded parameter retirement");
			if (!active)
				return kSuccess;
			calls.emplace_back("direct_shutdown");
			if (behavior.shutdown == kSuccess)
				active = false;
			return behavior.shutdown;
		}
		bool Unload()
		{
			if (active || liveMaps != 0)
				throw std::runtime_error("Unsafe provider unload");
			calls.emplace_back("unload");
			return behavior.unload;
		}
	};
	enum class RuntimeStatus
	{
		NotProbed,
		Ready,
		Initialized,
		InitializationFailed,
		ParameterAllocationFailed,
		ShutdownFailed
	};
	enum class RuntimeFailureStage
	{
		None,
		Initialization,
		ParameterAllocation,
		Shutdown
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
			NVSDK_NGX_Parameter* capabilities = nullptr;
			DirectProvider provider;
			struct File
			{
				int Get() { return 1; }
			} providerFile;
			std::wstring featureDirectory;
		};
		std::recursive_mutex mutex_;
		std::atomic<bool> abandonRequested_ = false;
		bool abandoned_ = false;
		RuntimeStatus status_ = RuntimeStatus::NotProbed;
		RuntimeFailureStage failureStage_ = RuntimeFailureStage::None;
		RuntimeTrust trust_ = RuntimeTrust::Unknown;
		std::filesystem::path path_;
		std::string hash_, version_, detail_;
		std::string initializationRoute_ = "NGX_D3D12_DIRECT_PROVIDER", discoveryDetail_;
		std::optional<int> evaluationSuccessDetail_, tuningLimits_, networkSelectionSupported_;
		NgxDriverRequirements driverRequirements_;
		std::unique_ptr<Backend> backend_;
		void* device_ = nullptr;
		void* parameters_ = nullptr;
		std::array<void*, 4> featureHandles_{}, featureConfigurations_{};
		std::atomic<unsigned> firstEvaluationResetMask_ = 0;
		unsigned ngxResult_ = 0;
		std::uint64_t successfulFrames_ = 0;
		bool probeLogEmitted_ = false, initializationLogEmitted_ = false, featureCreateLogEmitted_ = false, featureEvaluateLogEmitted_ = false;
		bool ProbeLocked(const std::filesystem::path&)
		{
			backend_ = std::make_unique<Backend>();
			path_ = std::filesystem::current_path() / "Testing/VirtualMod/nvngx_dlssnr.dll";
			backend_->featureDirectory = (std::filesystem::current_path() / "Testing/PhysicalMod").wstring();
			hash_ = "user-provider";
			version_ = "310.9.16";
			trust_ = RuntimeTrust::VersionAccepted;
			status_ = RuntimeStatus::Ready;
			return true;
		}
		void SetFailureLocked(RuntimeStatus status, RuntimeFailureStage stage, std::string detail, unsigned result = 0)
		{
			status_ = status;
			failureStage_ = stage;
			detail_ = std::move(detail);
			ngxResult_ = result;
		}
		void LogOnceLocked(bool&, const char*, bool) {}
		bool ResetFeaturesLocked()
		{
			calls.emplace_back("retire_features");
			return true;
		}
		void AbandonLocked() { abandoned_ = true; }
		bool Initialize(ID3D12Device*, const std::filesystem::path&);
		static const DriverSupport& GetDriverSupport() noexcept { return installedDriver; }
		bool ShutdownLocked();
	};
#include "neural_ngx_initialize_under_test.h"
#include "neural_ngx_shutdown_under_test.h"
}

namespace
{
	using namespace NeuralRendering;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void Reset()
	{
		calls.clear();
		behavior = {};
		installedDriver = { kMinimumNvidiaDriverRelease };
	}
	void Run()
	{
		const auto data = std::filesystem::current_path() / "Testing/LegacyNr/data";
		ID3D12Device device;
		for (const auto release : { 61656u, 61691u, 61692u, 61700u }) {
			Reset();
			installedDriver.installedRelease = release;
			Runtime runtime;
			const bool supported = release >= kMinimumNvidiaDriverRelease;
			Require(runtime.Initialize(&device, data) == supported, "Driver baseline changed");
			if (!supported)
				Require(calls.empty() && runtime.ngxResult_ == NVSDK_NGX_Result_FAIL_OutOfDate && device.references == 0,
					"Outdated driver reached the provider");
			else
				Require(runtime.ShutdownLocked(), "Supported driver failed retirement");
		}
		Reset();
		Runtime healthy;
		Require(healthy.Initialize(&device, data), "Legacy initialization failed");
		Require(calls == std::vector<std::string>{ "direct_init", "allocate_caps", "populate_caps", "allocate_params" },
			"Legacy initialization gained SDK discovery or changed ownership order");
		Require(healthy.initializationRoute_ == "NGX_D3D12_DIRECT_PROVIDER" && healthy.discoveryDetail_.find("not attempted") != std::string::npos,
			"Legacy route misreported SDK discovery");
		Require(healthy.Initialize(&device, data) && calls.size() == 4 && device.references == 1, "Healthy provider initialized twice");
		healthy.evaluationSuccessDetail_ = 1;
		Require(healthy.ShutdownLocked() && device.references == 0 && !healthy.backend_ && !healthy.evaluationSuccessDetail_,
			"Healthy provider retained ownership");
		Require(calls == std::vector<std::string>{ "direct_init", "allocate_caps", "populate_caps", "allocate_params",
							 "retire_features", "destroy_params", "destroy_caps", "direct_shutdown", "unload" },
			"Parameters, native device or modules retired in the wrong order");

		for (unsigned failure = 0; failure < 6; ++failure) {
			Reset();
			Runtime runtime;
			if (failure == 0)
				behavior.init = kFailure;
			if (failure == 1) {
				behavior.capability = kFailure;
				behavior.partialCapability = true;
			}
			if (failure == 2)
				behavior.populate = kFailure;
			if (failure == 3) {
				behavior.allocate = kFailure;
				behavior.partialAllocation = true;
			}
			if (failure == 4)
				behavior.nullAllocation = true;
			if (failure == 5)
				behavior.capability = kFailure;
			Require(!runtime.Initialize(&device, data), "Failed legacy admission was accepted");
			Require(device.references == 0 && !runtime.backend_ && !runtime.parameters_, "Rollback leaked provider ownership");
			Require(runtime.hash_ == "user-provider" && runtime.version_ == "310.9.16", "Rollback lost provider identity");
			Require(runtime.ngxResult_ == (failure == 4 ? NVSDK_NGX_Result_FAIL_InvalidParameter : kFailure), "Rollback lost the primary result");
			if (failure == 3)
				Require(std::find(calls.begin(), calls.end(), "destroy_params") != calls.end(), "Partial allocation was leaked");
			if (failure == 1)
				Require(std::find(calls.begin(), calls.end(), "destroy_caps") != calls.end(), "Partial capability allocation was leaked");
		}
		for (const bool majorKnown : { false, true }) {
			for (const bool minorKnown : { false, true }) {
				Reset();
				Runtime runtime;
				behavior.needsUpdate = 1;
				behavior.majorQuery = majorKnown ? kSuccess : kFailure;
				behavior.minorQuery = minorKnown ? kSuccess : kFailure;
				Require(!runtime.Initialize(&device, data) && runtime.ngxResult_ == NVSDK_NGX_Result_FAIL_OutOfDate && device.references == 0,
					"Explicit provider driver requirement was ignored");
				const auto& requirements = runtime.driverRequirements_;
				Require(requirements.needsUpdate == true && requirements.minimumMajor.has_value() == majorKnown &&
							requirements.minimumMinor.has_value() == minorKnown && requirements.majorQuery == behavior.majorQuery && requirements.minorQuery == behavior.minorQuery,
					"Driver queries exposed unavailable values");
				Require(std::find(calls.begin(), calls.end(), "allocate_params") == calls.end(),
					"Driver rejection allocated feature parameters");
				Require(runtime.detail_.find(std::format("minimum={}.{}", majorKnown ? "616" : "unknown", minorKnown ? "92" : "unknown")) != std::string::npos,
					"Driver rejection lost its minimum version");
			}
		}
		Reset();
		Runtime unknown;
		installedDriver = {};
		behavior.needsUpdate = 1;
		behavior.driverQuery = kFailure;
		Require(unknown.Initialize(&device, data) && !unknown.driverRequirements_.needsUpdate.has_value(),
			"Unknown driver information was mistaken for an explicit rejection");
		Require(unknown.ShutdownLocked() && device.references == 0, "Unknown driver prevented retirement");

		for (unsigned failure = 0; failure < 3; ++failure) {
			Reset();
			Runtime runtime;
			Require(runtime.Initialize(&device, data), "Retirement fixture failed initialization");
			if (failure == 0)
				behavior.destroy = kFailure;
			if (failure == 1)
				behavior.shutdown = kFailure;
			if (failure == 2)
				behavior.unload = false;
			Require(!runtime.ShutdownLocked() && device.references == 1 && runtime.backend_, "Failed teardown released owned state");
			behavior.destroy = behavior.shutdown = kSuccess;
			behavior.unload = true;
			Require(runtime.ShutdownLocked() && device.references == 0, "Retained teardown could not complete");
			Require(std::count(calls.begin(), calls.end(), "direct_init") == 1, "Teardown retry initialized a second provider");
			if (failure == 2)
				Require(std::count(calls.begin(), calls.end(), "direct_shutdown") == 1, "Unload retry repeated native shutdown");
		}
		Reset();
		Runtime rollbackFailed;
		behavior.needsUpdate = 1;
		behavior.shutdown = kFailure;
		Require(!rollbackFailed.Initialize(&device, data) && device.references == 1 && rollbackFailed.backend_,
			"Failed driver-rejection rollback released ownership");
		Require(rollbackFailed.detail_.find("requires an updated NVIDIA driver") != std::string::npos &&
					rollbackFailed.detail_.find("rollback failed") != std::string::npos && rollbackFailed.driverRequirements_.needsUpdate == true,
			"Rollback failure hid the driver requirement");
		behavior.shutdown = kSuccess;
		Require(rollbackFailed.ShutdownLocked() && device.references == 0, "Driver rollback could not finish");
		Reset();
		Runtime nullDevice;
		Require(!nullDevice.Initialize(nullptr, data) && calls.empty(), "Null device reached provider initialization");
	}
}
int main()
{
	try {
		Run();
		std::cout << "Legacy NR initialization, allocator ownership, driver admission and rollback passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
