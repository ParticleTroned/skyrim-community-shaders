#include "Features/Upscaling/NeuralRendering/DriverSupport.h"
#include "Features/Upscaling/NeuralRendering/NgxCapabilityInfo.h"
#include "Features/Upscaling/NvidiaProject.h"
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
#include <string_view>
#include <vector>

namespace NeuralRendering
{
	using NVSDK_NGX_Result = std::uint32_t;
	constexpr NVSDK_NGX_Result kSuccess = 1, kFailure = 0xBAD00010;
	constexpr auto NVSDK_NGX_Result_Success = kSuccess;
	constexpr auto kRuntimeName = L"nvngx_dlssnr.dll";
	constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_FeatureNotFound = 0xBAD00004;
	constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_UnsupportedParameter = kFailure;
	constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_FeatureNotSupported = 0xBAD00001;
	constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_OutOfDate = 0xBAD0000C;
	constexpr bool NVSDK_NGX_FAILED(NVSDK_NGX_Result value) { return (value & 0xFFF00000) == 0xBAD00000; }
	constexpr unsigned NVSDK_NGX_ENGINE_TYPE_CUSTOM = 0;
	constexpr int E_INVALIDARG = -1;
	namespace logger
	{
		template <class... T>
		void warn(const char*, const T&...)
		{}
	}
	std::vector<std::string> calls;
	NVSDK_NGX_Result directResult = 0xBAD00004, directShutdownResult = 1;
	bool directUnloadResult = true;
	NVSDK_NGX_Result providerInitResult = kSuccess, providerCapabilityResult = kSuccess, providerAvailabilityResult = kSuccess;
	NVSDK_NGX_Result providerAllocationResult = kSuccess, providerDestroyResult = kSuccess, providerShutdownResult = kSuccess;
	int providerAvailable = 1;
	int providerNeedsUpdatedDriver = 0;
	NVSDK_NGX_Result providerDriverQuery = kSuccess, providerMajorQuery = kSuccess, providerMinorQuery = kSuccess;
	NVSDK_NGX_Result providerFeatureQuery = kFailure, providerFeatureResult = kSuccess;
	bool partialAllocation = false;
	std::string initializedProject;
	std::wstring featurePath;
	DriverSupport installedDriver{ kMinimumNvidiaDriverRelease };
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
			if (std::string_view(key) == NVSDK_NGX_Parameter_DLSSNR_FeatureInitResult) {
				*value = static_cast<int>(providerFeatureResult);
				return providerFeatureQuery;
			}
			return kFailure;
		}
		NVSDK_NGX_Result Get(const char* key, unsigned* value)
		{
			if (std::string_view(key) == NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMajor) {
				*value = 616u;
				return providerMajorQuery;
			}
			if (std::string_view(key) == NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMinor) {
				*value = 92u;
				return providerMinorQuery;
			}
			return kFailure;
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
		const char*, const wchar_t*, ID3D12Device*, const NVSDK_NGX_FeatureCommonInfo* info)
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
	struct DirectProvider
	{
		bool active = false;
		NVSDK_NGX_Result Initialize(const std::filesystem::path& path, int, ID3D12Device*, const std::filesystem::path&, NVSDK_NGX_Parameter* map, std::string& detail)
		{
			calls.emplace_back("direct_init");
			if (path.parent_path().wstring() != featurePath || path.filename() != kRuntimeName || map != &capabilityMap)
				throw std::runtime_error("fallback changed provider identity or parameter ownership");
			active = directResult == kSuccess;
			detail = "direct provider result";
			return directResult;
		}
		NVSDK_NGX_Result ShutdownDevice(ID3D12Device*)
		{
			if (!active)
				return kSuccess;
			calls.emplace_back("direct_shutdown");
			if (directShutdownResult == kSuccess)
				active = false;
			return directShutdownResult;
		}
		bool Unload()
		{
			if (active)
				return false;
			return directUnloadResult;
		}
	};
	struct Runtime
	{
		struct Backend
		{
			NVSDK_NGX_Parameter* capabilities = nullptr;
			DirectProvider provider;
			bool sdkInitialized = false;
			struct File
			{
				int Get() { return 1; }
			} providerFile;
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
		std::string initializationRoute_ = "NGX_D3D12_SDK", discoveryDetail_;
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
			backend_->featureDirectory = (std::filesystem::current_path() / "Testing/PhysicalMod/Streamline").wstring();
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
		providerDriverQuery = providerMajorQuery = providerMinorQuery = kSuccess;
		providerFeatureQuery = kFailure;
		providerFeatureResult = kSuccess;
		partialAllocation = false;
		directResult = NVSDK_NGX_Result_FAIL_FeatureNotFound;
		directShutdownResult = kSuccess;
		directUnloadResult = true;
		installedDriver = { kMinimumNvidiaDriverRelease };
	}
	void Run()
	{
		const auto data = std::filesystem::current_path() / "Testing/NgxSdk/data";
		Reset();
		ID3D12Device device;
		for (const auto release : { 61656u, 61691u, 61692u, 61700u, 62101u }) {
			Reset();
			installedDriver.installedRelease = release;
			Runtime runtime;
			const bool supported = release >= kMinimumNvidiaDriverRelease;
			Require(runtime.Initialize(&device, data) == supported, "Rendering-adapter driver boundary is incorrect");
			if (!supported) {
				Require(calls.empty() && device.references == 0 && !runtime.device_ && !runtime.parameters_,
					"Outdated driver reached NGX or acquired SDK ownership");
				Require(runtime.ngxResult_ == NVSDK_NGX_Result_FAIL_OutOfDate && runtime.detail_ == installedDriver.Message(),
					"Driver rejection lost the actionable UI diagnostic");
				Require(runtime.detail_.find("616.92") != std::string::npos && runtime.detail_.find("NVIDIA app") != std::string::npos &&
							runtime.detail_.find("restart the game") != std::string::npos,
					"Driver warning must give the required version and update/restart instructions");
			} else {
				Require(installedDriver.Message().empty() && runtime.ShutdownLocked(), "Supported driver was warned or failed teardown");
			}
		}
		Reset();
		Require(DriverSupport{ 61656u }.Message().find("Installed driver: 616.56") != std::string::npos,
			"Driver warning must display the detected installed version");
		installedDriver = {};
		Runtime undetectedDriver;
		Require(!installedDriver.RequiresUpdate() && !installedDriver.Message().empty() && undetectedDriver.Initialize(&device, data),
			"Undetected driver must show a requirement without being labeled outdated or bypassing NGX");
		Require(undetectedDriver.ShutdownLocked() && device.references == 0, "Undetected driver did not retire SDK ownership");
		Reset();
		Runtime healthy;
		Require(healthy.Initialize(&device, data), "SDK initialization failed");
		Require(healthy.driverRequirements_.needsUpdate == false && healthy.driverRequirements_.minimumMajor == 616u && healthy.driverRequirements_.minimumMinor == 92u,
			"Provider-reported driver requirements were lost");
		Require(healthy.detail_.find("NGX SDK route initialized") != std::string::npos &&
					healthy.detail_.find("actual provider identity unverified") != std::string::npos,
			"SDK dispatch must not attest the provider's identity or origin");
		Require(initializedProject == NvidiaProject::kId && featurePath == healthy.backend_->featureDirectory &&
					featurePath != healthy.path_.parent_path(),
			"SDK must use the verified physical provider directory instead of the virtual Data path");
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
			Require(runtime.ngxResult_ == (failure == 1 || failure == 2 ? directResult : kFailure), "SDK rollback lost the primary result");
			Require(calls.back() == "shutdown", "SDK rollback did not shut down the owning device");
			if (failure == 3)
				Require(calls[calls.size() - 3] == "destroy_params", "Partial SDK map was not destroyed before shutdown");
		}

		Reset();
		Runtime missingFeature;
		providerAvailable = 0;
		providerFeatureQuery = kSuccess;
		providerFeatureResult = 0xBAD00004;
		Require(!missingFeature.Initialize(&device, data) && missingFeature.ngxResult_ == providerFeatureResult && device.references == 0,
			"A provider discovery failure must retain FeatureNotFound instead of reporting an unsupported GPU");
		Reset();
		Runtime fallback;
		providerAvailable = 0;
		directResult = kSuccess;
		Require(fallback.Initialize(&device, data) && fallback.initializationRoute_ == "NGX_D3D12_DIRECT_PROVIDER",
			"Missing SDK feature did not initialize the admitted provider directly");
		Require(fallback.discoveryDetail_.find("available=0") != std::string::npos, "Fallback lost the original SDK result");
		directShutdownResult = kFailure;
		Require(!fallback.ShutdownLocked() && device.references == 1 && fallback.parameters_, "Provider shutdown failure released SDK ownership");
		directShutdownResult = kSuccess;
		Require(fallback.ShutdownLocked() && device.references == 0, "Direct route could not retire safely");
		Require(std::find(calls.begin(), calls.end(), "direct_shutdown") < std::find(calls.begin(), calls.end(), "destroy_params"),
			"SDK maps were destroyed while the provider was still initialized");
		Reset();
		Runtime unloadFailure;
		providerAvailable = 0;
		directResult = kSuccess;
		Require(unloadFailure.Initialize(&device, data), "Unload failure fixture did not initialize");
		directUnloadResult = false;
		Require(!unloadFailure.ShutdownLocked() && device.references == 1 && unloadFailure.backend_ && !unloadFailure.parameters_,
			"Failed import restoration released retained ownership");
		directUnloadResult = true;
		Require(unloadFailure.ShutdownLocked() && device.references == 0 && std::count(calls.begin(), calls.end(), "shutdown") == 1,
			"Retried import restoration repeated SDK shutdown or leaked the device");
		for (const auto failure : { 0xBAD00002u, NVSDK_NGX_Result_FAIL_OutOfDate }) {
			Reset();
			Runtime genuineFailure;
			providerAvailable = 0;
			providerFeatureQuery = kSuccess;
			providerFeatureResult = failure;
			directResult = kSuccess;
			Require(!genuineFailure.Initialize(&device, data) && genuineFailure.ngxResult_ == failure &&
						std::find(calls.begin(), calls.end(), "direct_init") == calls.end() && device.references == 0,
				"Genuine NGX platform or out-of-date failure was bypassed");
		}
		for (const bool available : { false, true }) {
			for (const bool availabilityQuerySucceeds : { false, true }) {
				Reset();
				Runtime providerOldDriver;
				providerAvailable = available;
				providerAvailabilityResult = availabilityQuerySucceeds ? kSuccess : kFailure;
				providerFeatureQuery = kSuccess;
				providerFeatureResult = NVSDK_NGX_Result_FAIL_FeatureNotFound;
				providerNeedsUpdatedDriver = 1;
				directResult = kSuccess;
				Require(!providerOldDriver.Initialize(&device, data) && providerOldDriver.ngxResult_ == NVSDK_NGX_Result_FAIL_OutOfDate,
					"An explicit provider driver-update requirement was ignored");
				Require(providerOldDriver.driverRequirements_.needsUpdate == true && providerOldDriver.driverRequirements_.updateQuery == kSuccess &&
							providerOldDriver.driverRequirements_.minimumMajor == 616u && providerOldDriver.driverRequirements_.minimumMinor == 92u &&
							device.references == 0 && !providerOldDriver.backend_ && !providerOldDriver.parameters_,
					"Driver rejection lost requirements during rollback or retained SDK ownership");
				Require(std::find(calls.begin(), calls.end(), "direct_init") == calls.end() &&
							std::find(calls.begin(), calls.end(), "allocate") == calls.end() && calls.back() == "shutdown",
					"Driver rejection attempted an alternate route or allocated feature parameters");
				Require(providerOldDriver.detail_.find("minimum=616.92") != std::string::npos &&
							providerOldDriver.detail_.find("query=0x00000001") != std::string::npos &&
							providerOldDriver.detail_.find("restart the game") != std::string::npos,
					"Provider driver rejection lost its actionable diagnostic");
				if (!available || !availabilityQuerySucceeds)
					Require(providerOldDriver.discoveryDetail_.find("featureResult=0xBAD00004") != std::string::npos &&
								providerOldDriver.detail_.find(providerOldDriver.discoveryDetail_) != std::string::npos,
						"Driver rejection lost the original NGX discovery result");
			}
		}
		for (const bool majorKnown : { false, true }) {
			for (const bool minorKnown : { false, true }) {
				Reset();
				Runtime incompleteDriverRequirement;
				providerAvailable = 0;
				providerNeedsUpdatedDriver = 1;
				providerMajorQuery = majorKnown ? kSuccess : kFailure;
				providerMinorQuery = minorKnown ? kSuccess : kFailure;
				directResult = kSuccess;
				Require(!incompleteDriverRequirement.Initialize(&device, data) && device.references == 0,
					"An explicit driver requirement depended on optional minimum-version queries");
				const auto& requirements = incompleteDriverRequirement.driverRequirements_;
				Require(requirements.minimumMajor.has_value() == majorKnown && requirements.minimumMinor.has_value() == minorKnown &&
							requirements.majorQuery == providerMajorQuery && requirements.minorQuery == providerMinorQuery,
					"Failed minimum-version queries exposed unwritten values or lost their results");
				Require(incompleteDriverRequirement.detail_.find(std::format("minimum={}.{}", majorKnown ? "616" : "unknown", minorKnown ? "92" : "unknown")) != std::string::npos &&
							std::find(calls.begin(), calls.end(), "direct_init") == calls.end(),
					"Driver rejection invented a minimum version or reached the direct provider");
			}
		}
		Reset();
		Runtime unknownDriver;
		providerDriverQuery = kFailure;
		Require(unknownDriver.Initialize(&device, data) && !unknownDriver.driverRequirements_.needsUpdate.has_value(),
			"An unsupported driver query was mistaken for an incompatible GPU");
		Require(unknownDriver.ShutdownLocked(), "Unknown driver query prevented safe shutdown");
		Reset();
		Runtime unknownDirectDriver;
		providerAvailable = 0;
		providerNeedsUpdatedDriver = 1;
		providerDriverQuery = kFailure;
		directResult = kSuccess;
		Require(unknownDirectDriver.Initialize(&device, data) && !unknownDirectDriver.driverRequirements_.needsUpdate.has_value(),
			"A failed driver query must not turn its unwritten value into an explicit update requirement");
		Require(unknownDirectDriver.ShutdownLocked() && device.references == 0, "Unknown direct driver query retained SDK ownership");
		Reset();
		Runtime driverRollbackFailure;
		providerAvailable = 0;
		providerNeedsUpdatedDriver = 1;
		providerShutdownResult = kFailure;
		directResult = kSuccess;
		Require(!driverRollbackFailure.Initialize(&device, data) && device.references == 1 && driverRollbackFailure.backend_,
			"Failed driver-rejection rollback released live SDK ownership");
		Require(driverRollbackFailure.driverRequirements_.needsUpdate == true &&
					driverRollbackFailure.detail_.find("requires an updated NVIDIA driver") != std::string::npos &&
					driverRollbackFailure.detail_.find("rollback failed") != std::string::npos &&
					std::find(calls.begin(), calls.end(), "direct_init") == calls.end(),
			"Failed rollback hid the driver rejection or attempted a second route");
		providerShutdownResult = kSuccess;
		Require(driverRollbackFailure.ShutdownLocked() && device.references == 0, "Driver-rejection rollback could not finish safe teardown");
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
