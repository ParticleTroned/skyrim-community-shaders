#include "Features/Upscaling/NeuralRendering/NgxCapabilityInfo.h"
#include "Features/Upscaling/NvidiaProject.h"
#include "neural_sdk_parameters_under_test.h"

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
#include <string_view>
#include <vector>

namespace NeuralRendering
{
	using NVSDK_NGX_Result = std::uint32_t;
	constexpr NVSDK_NGX_Result kSuccess = 1, kFailure = 0xBAD00010;
	constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_FeatureNotSupported = 0xBAD00001;
	constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_OutOfDate = 0xBAD00011;
	constexpr bool NVSDK_NGX_FAILED(NVSDK_NGX_Result value) { return (value & 0xFFF00000) == 0xBAD00000; }
	constexpr unsigned NVSDK_NGX_ENGINE_TYPE_CUSTOM = 0;
	constexpr int E_INVALIDARG = -1;
	std::vector<std::string> calls;
	NVSDK_NGX_Result providerInitResult = kSuccess, providerCapabilityResult = kSuccess, providerAvailabilityResult = kSuccess;
	NVSDK_NGX_Result providerAllocationResult = kSuccess, providerDestroyResult = kSuccess, providerShutdownResult = kSuccess;
	int providerAvailable = 1;
	int providerNeedsUpdatedDriver = 0;
	NVSDK_NGX_Result providerDriverQuery = kSuccess;
	bool partialAllocation = false;
	std::string initializedProject;
	std::wstring featurePath;
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
			if (std::string_view(key) == NVSDK_NGX_Parameter_DLSSNR_NeedsUpdatedDriver) {
				*value = providerNeedsUpdatedDriver;
				return providerDriverQuery;
			}
			if (std::string_view(key) == NVSDK_NGX_Parameter_DLSSNR_Available) {
				calls.emplace_back("available");
				*value = providerAvailable;
				return providerAvailabilityResult;
			}
			return kFailure;
		}
		NVSDK_NGX_Result Get(const char* key, unsigned* value)
		{
			*value = std::string_view(key) == NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMajor ? 616u : 92u;
			return providerDriverQuery;
		}
	} capabilityMap{ true }, parameterMap;
	struct NVSDK_NGX_FeatureCommonInfo
	{
		struct Paths
		{
			const wchar_t* const* Path = nullptr;
			unsigned Length = 0;
		} PathListInfo;
	};
	NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_with_ProjectID(const char* project, unsigned engine,
		const char*, const std::filesystem::path::value_type*, ID3D12Device*, const NVSDK_NGX_FeatureCommonInfo* info)
	{
		calls.emplace_back("init");
		if (engine != NVSDK_NGX_ENGINE_TYPE_CUSTOM || info->PathListInfo.Length != 1)
			throw std::runtime_error("incorrect SDK initialization contract");
		initializedProject = project;
		featurePath = info->PathListInfo.Path[0];
		return providerInitResult;
	}
	NVSDK_NGX_Result NVSDK_NGX_D3D12_GetCapabilityParameters(NVSDK_NGX_Parameter** output)
	{
		calls.emplace_back("capabilities");
		*output = &capabilityMap;
		return providerCapabilityResult;
	}
	NVSDK_NGX_Result NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter** output)
	{
		calls.emplace_back("allocate");
		*output = providerAllocationResult == kSuccess || partialAllocation ? &parameterMap : nullptr;
		return providerAllocationResult;
	}
	NVSDK_NGX_Result NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter* map)
	{
		calls.emplace_back(map->capabilities ? "destroy_caps" : "destroy_params");
		return providerDestroyResult;
	}
	NVSDK_NGX_Result NVSDK_NGX_D3D12_Shutdown1(ID3D12Device*)
	{
		calls.emplace_back("shutdown");
		return providerShutdownResult;
	}
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
			std::wstring featureDirectory;
			std::array<const wchar_t*, 1> featurePaths{};
			NVSDK_NGX_FeatureCommonInfo featureInfo{};
		};
		std::recursive_mutex mutex_;
		std::atomic<bool> abandonRequested_ = false;
		bool abandoned_ = false;
		RuntimeStatus status_ = RuntimeStatus::NotProbed;
		RuntimeFailureStage failureStage_ = RuntimeFailureStage::None;
		RuntimeTrust trust_ = RuntimeTrust::Unknown;
		std::filesystem::path path_;
		std::string hash_, version_, detail_;
		std::optional<int> evaluationSuccessDetail_;
		std::optional<int> tuningLimits_, networkSelectionSupported_;
		NgxDriverRequirements driverRequirements_;
		std::unique_ptr<Backend> backend_;
		void* device_ = nullptr;
		void* parameters_ = nullptr;
		std::array<void*, 4> featureHandles_{}, featureConfigurations_{};
		std::atomic<unsigned> firstEvaluationResetMask_ = 0;
		unsigned ngxResult_ = 0;
		std::uint64_t successfulFrames_ = 0;
		bool probeLogEmitted_ = false, initializationLogEmitted_ = false;
		bool featureCreateLogEmitted_ = false, featureEvaluateLogEmitted_ = false;
		bool ProbeLocked(const std::filesystem::path&)
		{
			backend_ = std::make_unique<Backend>();
			path_ = std::filesystem::current_path() / "Testing/NgxSdk/nvngx_dlssnr.dll";
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
		bool ShutdownLocked();
	};

#include "neural_ngx_initialize_under_test.h"
#include "neural_ngx_shutdown_under_test.h"
}

namespace
{
	using namespace NeuralRendering;
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	void Reset()
	{
		calls.clear();
		providerInitResult = providerCapabilityResult = providerAvailabilityResult = providerAllocationResult = providerDestroyResult = providerShutdownResult = kSuccess;
		providerAvailable = 1;
		providerNeedsUpdatedDriver = 0;
		providerDriverQuery = kSuccess;
		partialAllocation = false;
	}
	void Run()
	{
		const auto data = std::filesystem::current_path() / "Testing/NgxSdk/data";
		Reset();
		ID3D12Device device;
		Runtime healthy;
		Require(healthy.Initialize(&device, data), "SDK initialization failed");
		Require(healthy.driverRequirements_.needsUpdate == false && healthy.driverRequirements_.minimumMajor == 616u && healthy.driverRequirements_.minimumMinor == 92u,
			"Provider-reported driver requirements were lost");
		Require(initializedProject == NvidiaProject::kId && featurePath == healthy.path_.parent_path(), "SDK used the wrong project or feature path");
		Require(calls == std::vector<std::string>{ "init", "capabilities", "available", "allocate" }, "SDK startup order changed");
		Require(healthy.Initialize(&device, data) && calls.size() == 4 && device.references == 1, "Healthy SDK was initialized twice");
		healthy.evaluationSuccessDetail_ = 1;
		Require(healthy.ShutdownLocked() && device.references == 0 && !healthy.backend_ && !healthy.evaluationSuccessDetail_,
			"Healthy SDK ownership or deferred evaluation detail was not retired");
		Require(calls == std::vector<std::string>{ "init", "capabilities", "available", "allocate", "retire_features", "destroy_params", "destroy_caps", "shutdown" }, "SDK teardown order changed");

		for (unsigned failure = 0; failure < 4; ++failure) {
			Reset();
			Runtime runtime;
			if (failure == 0)
				providerCapabilityResult = kFailure;
			if (failure == 1)
				providerAvailabilityResult = kFailure;
			if (failure == 2)
				providerAvailable = 0;
			if (failure == 3) {
				providerAllocationResult = kFailure;
				partialAllocation = true;
			}
			Require(!runtime.Initialize(&device, data), "SDK startup failure was accepted");
			Require(device.references == 0 && !runtime.backend_ && !runtime.parameters_, "SDK rollback leaked owned maps or device");
			Require(runtime.hash_ == "user-provider" && runtime.version_ == "310.9.16", "SDK rollback lost provider identity");
			Require(runtime.ngxResult_ == (failure == 2 ? NVSDK_NGX_Result_FAIL_FeatureNotSupported : kFailure), "SDK rollback lost the primary result");
			Require(calls.back() == "shutdown", "SDK rollback did not shut down the owning device");
			if (failure == 3)
				Require(calls[calls.size() - 3] == "destroy_params", "Partial SDK map was not destroyed before shutdown");
		}

		Reset();
		Runtime oldDriver;
		providerNeedsUpdatedDriver = 1;
		Require(!oldDriver.Initialize(&device, data) && oldDriver.ngxResult_ == NVSDK_NGX_Result_FAIL_OutOfDate,
			"An explicit provider driver-update requirement was ignored");
		Require(oldDriver.driverRequirements_.needsUpdate == true && oldDriver.driverRequirements_.minimumMinor == 92u && device.references == 0,
			"Driver rejection lost requirements during rollback or retained device ownership");
		Reset();
		Runtime unknownDriver;
		providerDriverQuery = kFailure;
		Require(unknownDriver.Initialize(&device, data) && !unknownDriver.driverRequirements_.needsUpdate.has_value(),
			"An unsupported driver query was mistaken for an incompatible GPU");
		Require(unknownDriver.ShutdownLocked(), "Unknown driver query prevented safe shutdown");
		Reset();
		Runtime initFailed;
		providerInitResult = kFailure;
		Require(!initFailed.Initialize(&device, data) && calls == std::vector<std::string>{ "init" } && device.references == 0, "Failed SDK init acquired device ownership");

		Reset();
		Runtime rollbackFailed;
		providerAvailabilityResult = providerShutdownResult = kFailure;
		Require(!rollbackFailed.Initialize(&device, data) && device.references == 1 && rollbackFailed.backend_, "Failed SDK shutdown released live ownership");
		Require(rollbackFailed.detail_.find("rollback failed") != std::string::npos, "Rollback failure hid its primary diagnostic");
		providerShutdownResult = kSuccess;
		Require(rollbackFailed.ShutdownLocked() && device.references == 0, "Retained SDK could not finish safe teardown");

		Reset();
		Runtime nullDevice;
		Require(!nullDevice.Initialize(nullptr, data) && calls.empty(), "Null device reached NVIDIA SDK");
	}
}

int main()
{
	try {
		Run();
		std::cout << "NGX SDK initialization, identity, rollback and teardown passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
