#include "Runtime.h"
#include "../NvidiaProject.h"
#include "D3D12Interop.h"
#include "DevelopmentDiagnostics.h"
#include "DirectProvider.h"
#include "Globals.h"
#include "NgxFeaturePath.h"
#include "Util.h"
#include "Utils/CryptoHash.h"
#include "Utils/GraphicsDriverInfo.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <d3d11.h>
#include <d3d12.h>
#include <exception>
#include <format>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssnr.h>
#include <utility>
#include <wrl/client.h>

namespace NeuralRendering
{
	namespace
	{
		constexpr wchar_t kRuntimeName[] = L"nvngx_dlssnr.dll";
		constexpr REL::Version kMinimumRuntimeVersion{ 310, 8, 0, 0 };
		constexpr auto kFeatureDlssNr = NVSDK_NGX_Feature_DLSSNR;
		class ScopedHandle
		{
		public:
			explicit ScopedHandle(HANDLE a_handle = INVALID_HANDLE_VALUE) : handle_(a_handle) {}
			~ScopedHandle()
			{
				if (handle_ && handle_ != INVALID_HANDLE_VALUE)
					CloseHandle(handle_);
			}

			ScopedHandle(const ScopedHandle&) = delete;
			ScopedHandle& operator=(const ScopedHandle&) = delete;

			[[nodiscard]] HANDLE Get() const { return handle_; }
			[[nodiscard]] HANDLE Release()
			{
				const HANDLE handle = handle_;
				handle_ = INVALID_HANDLE_VALUE;
				return handle;
			}
			void Reset(HANDLE a_handle)
			{
				if (handle_ && handle_ != INVALID_HANDLE_VALUE)
					CloseHandle(handle_);
				handle_ = a_handle;
			}

		private:
			HANDLE handle_ = INVALID_HANDLE_VALUE;
		};

		std::filesystem::path ResolveRuntimePath(const std::filesystem::path& a_explicitPath)
		{
			std::error_code error;
			const auto dataPath = Util::PathHelpers::GetDataPath();
			const auto expected =
				dataPath / L"Shaders/Upscaling/Streamline" / kRuntimeName;
			if (!a_explicitPath.empty()) {
				auto candidate = a_explicitPath;
				if (std::filesystem::is_directory(candidate, error))
					candidate /= kRuntimeName;
				error.clear();
				const auto candidateAbsolute =
					std::filesystem::absolute(candidate, error).lexically_normal().wstring();
				if (error)
					return {};
				const auto expectedAbsolute =
					std::filesystem::absolute(expected, error).lexically_normal().wstring();
				if (error || _wcsicmp(candidateAbsolute.c_str(), expectedAbsolute.c_str()) != 0)
					return {};
			}
			error.clear();
			if (std::filesystem::is_regular_file(expected, error))
				return expected;
			return {};
		}

		struct InstalledRuntimeMetadata
		{
			bool installed = false;
			bool networkSelectionSupported = false;
			TuningLimits tuningLimits;
			std::uint64_t allocationVersion = 0;

			template <class Version>
			static std::uint64_t AllocationVersion(const Version& version) noexcept
			{
				return (std::uint64_t(version.major()) << 48) | (std::uint64_t(version.minor()) << 32) |
				       (std::uint64_t(version.patch()) << 16) | std::uint64_t(version.build());
			}
		};

		InstalledRuntimeMetadata DiscoverInstalledRuntimeMetadata()
		{
			InstalledRuntimeMetadata metadata;
			try {
				const auto path = ResolveRuntimePath({});
				metadata.installed = !path.empty();
				if (metadata.installed) {
					if (const auto version = Util::GetDllVersion(path.wstring())) {
						metadata.allocationVersion = InstalledRuntimeMetadata::AllocationVersion(*version);
						metadata.tuningLimits = GetTuningLimitsForVersion(version->major(), version->minor());
						metadata.networkSelectionSupported = NeuralRendering::SupportsNetworkSelection(version->major(), version->minor());
					}
				}
			} catch (const std::exception& error) {
				logger::warn("[DLSSNR] optional runtime discovery failed: {}", error.what());
			}
			return metadata;
		}

		const InstalledRuntimeMetadata& GetInstalledRuntimeMetadata()
		{
			static const auto metadata = DiscoverInstalledRuntimeMetadata();
			return metadata;
		}

		bool ComputeFileSha256(HANDLE a_file, std::string& a_hash, std::string& a_error)
		{
			try {
				a_hash = Util::CryptoHash::ToHex(Util::CryptoHash::Sha256File(a_file));
				return true;
			} catch (const std::exception& error) {
				a_error = error.what();
				return false;
			}
		}

		bool ResolveNgxFeatureDirectory(HANDLE file, std::wstring& directory, std::string& error)
		{
			const auto required = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (!required || required > 32768) {
				error = std::format("could not size the physical NR runtime path: Windows error {}", GetLastError());
				return false;
			}
			std::wstring path(required, L'\0');
			const auto written = GetFinalPathNameByHandleW(file, path.data(), required, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (!written || written >= required) {
				error = std::format("could not resolve the physical NR runtime path: Windows error {}", GetLastError());
				return false;
			}
			path.resize(written);
			path = NormalizeNgxFeaturePath(std::move(path));
			if (path.empty()) {
				error = "NGX requires an ordinary drive or UNC path for the installed NR runtime";
				return false;
			}
			// NGX must discover the same physical provider whose open handle prevents replacement.
			directory = std::filesystem::path(path).parent_path().wstring();
			if (directory.empty()) {
				error = "the physical NR runtime path has no feature directory";
				return false;
			}
			return true;
		}

	}

	struct Runtime::Backend
	{
		ScopedHandle providerFile;
		DirectProvider provider;
		bool sdkInitialized = false;
		NVSDK_NGX_Parameter* capabilities = nullptr;
		std::wstring featureDirectory;
		std::array<const wchar_t*, 1> featurePaths{};
		NVSDK_NGX_FeatureCommonInfo featureInfo{};
		std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFeatureSlotCount> scratch;
		std::array<std::size_t, kFeatureSlotCount> scratchBytes{};
	};

	Runtime::Runtime() = default;

	bool Runtime::FeatureConfiguration::Matches(const NativeEvaluationLayout& a_layout) const
	{
		return valid && colorBacking == a_layout.color.backing &&
		       creation == a_layout.creation && featureUpscaling == a_layout.featureUpscaling && nativeControlMask == a_layout.nativeControlMask;
	}

	Runtime& Runtime::Instance()
	{
		static Runtime instance;
		return instance;
	}

	Runtime::~Runtime()
	{
		try {
			std::scoped_lock lock(mutex_);
			if (abandonRequested_.load(std::memory_order_acquire) || !ShutdownLocked())
				AbandonLocked();
		} catch (...) {
			// Raw NGX ownership must leak if teardown cannot prove it is safe.
			abandonRequested_.store(true, std::memory_order_release);
			AbandonLocked();
		}
	}

	void Runtime::SetFailureLocked(
		RuntimeStatus a_status, RuntimeFailureStage a_stage, std::string a_detail,
		std::uint32_t a_ngxResult)
	{
		status_ = a_status;
		failureStage_ = a_stage;
		evaluationSuccessDetail_.reset();
		detail_ = std::move(a_detail);
		ngxResult_ = a_ngxResult;

		switch (a_stage) {
		case RuntimeFailureStage::Discovery:
		case RuntimeFailureStage::Hash:
		case RuntimeFailureStage::Version:
		case RuntimeFailureStage::Load:
		case RuntimeFailureStage::Identity:
			LogOnceLocked(probeLogEmitted_, "probe", false);
			break;
		case RuntimeFailureStage::Initialization:
		case RuntimeFailureStage::ParameterAllocation:
			LogOnceLocked(initializationLogEmitted_, "initialize", false);
			break;
		case RuntimeFailureStage::FeatureCreate:
			LogOnceLocked(featureCreateLogEmitted_, "create", false);
			break;
		case RuntimeFailureStage::FeatureConfiguration:
		case RuntimeFailureStage::FeatureEvaluate:
			LogOnceLocked(featureEvaluateLogEmitted_, "evaluate", false);
			break;
		default:
			break;
		}
	}

	void Runtime::LogOnceLocked(
		bool& a_emitted, const char* a_operation, bool a_succeeded)
	{
		if (a_emitted)
			return;
		a_emitted = true;
		if (a_succeeded) {
			logger::info("[DLSSNR] {} succeeded: {}", a_operation, DetailLocked());
		} else {
			logger::error(
				"[DLSSNR] {} failed at {}: {} (status={}, ngx=0x{:08X})",
				a_operation, ToString(failureStage_), detail_, ToString(status_), ngxResult_);
		}
	}

	bool Runtime::IsInstalled() noexcept
	{
		return GetInstalledRuntimeMetadata().installed;
	}

	const DriverSupport& Runtime::GetDriverSupport() noexcept
	{
		static const DriverSupport unavailable{ std::nullopt, static_cast<std::uint32_t>(E_PENDING) };
		if (!globals::d3d::device)
			return unavailable;
		static const auto support = []() -> DriverSupport {
			Microsoft::WRL::ComPtr<IDXGIDevice> device;
			auto result = globals::d3d::device->QueryInterface(IID_PPV_ARGS(device.GetAddressOf()));
			if (FAILED(result))
				return { std::nullopt, static_cast<std::uint32_t>(result) };
			Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
			result = device->GetAdapter(adapter.GetAddressOf());
			if (FAILED(result))
				return { std::nullopt, static_cast<std::uint32_t>(result) };
			DXGI_ADAPTER_DESC description{};
			result = adapter->GetDesc(&description);
			if (FAILED(result))
				return { std::nullopt, static_cast<std::uint32_t>(result) };
			if (description.VendorId != 0x10DE)
				return { std::nullopt, static_cast<std::uint32_t>(DXGI_ERROR_UNSUPPORTED) };
			LARGE_INTEGER version{};
			result = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version);
			if (FAILED(result))
				return { std::nullopt, static_cast<std::uint32_t>(result) };
			const auto release = Util::DecodeNvidiaDriverRelease(version.LowPart);
			return { release, static_cast<std::uint32_t>(release ? S_OK : E_FAIL) };
		}();
		return support;
	}

	bool Runtime::ProbeLocked(const std::filesystem::path& a_explicitPath)
	{
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_ || !ShutdownLocked())
			return false;
		path_ = ResolveRuntimePath(a_explicitPath);
		if (path_.empty()) {
			SetFailureLocked(RuntimeStatus::NotFound, RuntimeFailureStage::Discovery, kMissingRuntimeNotice);
			return false;
		}
		backend_ = std::make_unique<Backend>();
		ScopedHandle verifiedFile(CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
		if (verifiedFile.Get() == INVALID_HANDLE_VALUE) {
			SetFailureLocked(RuntimeStatus::HashFailed, RuntimeFailureStage::Hash,
				std::format("could not lock NR runtime against writes/deletion: {}", GetLastError()));
			return false;
		}
		std::string hashError;
		if (!ComputeFileSha256(verifiedFile.Get(), hash_, hashError)) {
			SetFailureLocked(RuntimeStatus::HashFailed, RuntimeFailureStage::Hash, std::move(hashError));
			return false;
		}
		std::string pathError;
		if (!ResolveNgxFeatureDirectory(verifiedFile.Get(), backend_->featureDirectory, pathError)) {
			SetFailureLocked(RuntimeStatus::LoadFailed, RuntimeFailureStage::Discovery, std::move(pathError));
			return false;
		}
		const auto version = Util::GetDllVersion(path_.wstring());
		if (!version || *version < kMinimumRuntimeVersion) {
			SetFailureLocked(RuntimeStatus::VersionRejected, RuntimeFailureStage::Version,
				"NR runtime requires a readable version resource of 310.8 or newer");
			return false;
		}
		version_ = Util::GetFormattedVersion(*version);
		allocationVersion_ = InstalledRuntimeMetadata::AllocationVersion(*version);
		tuningLimits_ = GetTuningLimitsForVersion(version->major(), version->minor());
		networkSelectionSupported_ = NeuralRendering::SupportsNetworkSelection(version->major(), version->minor());
		backend_->providerFile.Reset(verifiedFile.Release());
		trust_ = RuntimeTrust::VersionAccepted;
		status_ = RuntimeStatus::Ready;
		failureStage_ = RuntimeFailureStage::None;
		ngxResult_ = 0;
		detail_ = std::format("user-supplied NR runtime admitted hash={} version={}", hash_, version_);
		LogOnceLocked(probeLogEmitted_, "probe", true);
		return true;
	}

	bool Runtime::Probe(const std::filesystem::path& a_explicitPath)
	{
		std::scoped_lock lock(mutex_);
		return ProbeLocked(a_explicitPath);
	}

	bool Runtime::Initialize(ID3D12Device* a_device, const std::filesystem::path& a_dataPath)
	{
		std::scoped_lock lock(mutex_);
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		if (!a_device) {
			SetFailureLocked(RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
				"D3D12 device is null", static_cast<std::uint32_t>(E_INVALIDARG));
			return false;
		}
		if (status_ == RuntimeStatus::Initialized && device_ == a_device)
			return true;
		if (device_ && !ShutdownLocked())
			return false;
		if (status_ != RuntimeStatus::Ready && !ProbeLocked({}))
			return false;
		const auto& driver = GetDriverSupport();
		if (driver.RequiresUpdate()) {
			SetFailureLocked(RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
				driver.Message(), static_cast<std::uint32_t>(NVSDK_NGX_Result_FAIL_OutOfDate));
			return false;
		}
		std::error_code directoryError;
		auto writablePath = a_dataPath;
		if (writablePath.empty())
			writablePath = std::filesystem::temp_directory_path(directoryError) / L"CommunityShaders-NGX";
		if (!directoryError)
			std::filesystem::create_directories(writablePath, directoryError);
		if (directoryError) {
			SetFailureLocked(RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
				std::format("could not create NGX data directory: {}", directoryError.message()));
			return false;
		}
		backend_->featurePaths[0] = backend_->featureDirectory.c_str();
		backend_->featureInfo.PathListInfo = { backend_->featurePaths.data(), 1 };
		const auto initializeResult = NVSDK_NGX_D3D12_Init_with_ProjectID(
			NvidiaProject::kId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, NvidiaProject::kEngineVersion,
			writablePath.c_str(), a_device, &backend_->featureInfo);
		if (NVSDK_NGX_FAILED(initializeResult)) {
			SetFailureLocked(RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
				std::format("NGX SDK initialization failed 0x{:08X}", static_cast<std::uint32_t>(initializeResult)),
				static_cast<std::uint32_t>(initializeResult));
			return false;
		}
		device_ = a_device;
		backend_->sdkInitialized = true;
		a_device->AddRef();
		const auto rollback = [this]() {
			const auto primaryStatus = status_;
			const auto primaryStage = failureStage_;
			const auto primaryDetail = detail_;
			const auto primaryResult = ngxResult_;
			const auto providerPath = path_;
			const auto providerHash = hash_;
			const auto providerVersion = version_;
			const auto providerTrust = trust_;
			const auto driverRequirements = driverRequirements_;
			const auto initializationRoute = initializationRoute_;
			const auto discoveryDetail = discoveryDetail_;
			if (ShutdownLocked()) {
				path_ = providerPath;
				hash_ = providerHash;
				version_ = providerVersion;
				trust_ = providerTrust;
				driverRequirements_ = driverRequirements;
				initializationRoute_ = initializationRoute;
				discoveryDetail_ = discoveryDetail;
				status_ = primaryStatus;
				failureStage_ = primaryStage;
				detail_ = primaryDetail;
				ngxResult_ = primaryResult;
			} else {
				detail_ = std::format("{}; NGX rollback failed: {}", primaryDetail, detail_);
			}
		};
		const auto capabilityResult = NVSDK_NGX_D3D12_GetCapabilityParameters(&backend_->capabilities);
		if (NVSDK_NGX_FAILED(capabilityResult) || !backend_->capabilities) {
			SetFailureLocked(RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
				"NGX SDK capability map allocation failed", static_cast<std::uint32_t>(capabilityResult));
			rollback();
			return false;
		}
		int needsUpdatedDriver = 0;
		const auto driverQuery = backend_->capabilities->Get(NVSDK_NGX_Parameter_DLSSNR_NeedsUpdatedDriver, &needsUpdatedDriver);
		driverRequirements_.updateQuery = static_cast<std::uint32_t>(driverQuery);
		if (!NVSDK_NGX_FAILED(driverQuery))
			driverRequirements_.needsUpdate = needsUpdatedDriver != 0;
		unsigned minimumMajor = 0, minimumMinor = 0;
		const auto majorQuery = backend_->capabilities->Get(NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMajor, &minimumMajor);
		const auto minorQuery = backend_->capabilities->Get(NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMinor, &minimumMinor);
		driverRequirements_.majorQuery = static_cast<std::uint32_t>(majorQuery);
		driverRequirements_.minorQuery = static_cast<std::uint32_t>(minorQuery);
		if (!NVSDK_NGX_FAILED(majorQuery))
			driverRequirements_.minimumMajor = minimumMajor;
		if (!NVSDK_NGX_FAILED(minorQuery))
			driverRequirements_.minimumMinor = minimumMinor;
		int available = 0;
		const auto availabilityResult = backend_->capabilities->Get(NVSDK_NGX_Parameter_DLSSNR_Available, &available);
		const bool sdkAvailable = !NVSDK_NGX_FAILED(availabilityResult) && available != 0;
		if (sdkAvailable && driverRequirements_.needsUpdate == true) {
			SetFailureLocked(RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
				std::format("NGX reports that NR requires an updated NVIDIA driver; minimum={}.{}",
					driverRequirements_.minimumMajor ? std::to_string(*driverRequirements_.minimumMajor) : "unknown",
					driverRequirements_.minimumMinor ? std::to_string(*driverRequirements_.minimumMinor) : "unknown"),
				static_cast<std::uint32_t>(NVSDK_NGX_Result_FAIL_OutOfDate));
			rollback();
			return false;
		}
		if (!sdkAvailable) {
			int featureResult = 0;
			const auto featureQuery = backend_->capabilities->Get(NVSDK_NGX_Parameter_DLSSNR_FeatureInitResult, &featureResult);
			const auto featureInitializationResult = static_cast<NVSDK_NGX_Result>(featureResult);
			const auto failureResult = !NVSDK_NGX_FAILED(featureQuery) && NVSDK_NGX_FAILED(featureInitializationResult) ? featureInitializationResult :
			                           NVSDK_NGX_FAILED(availabilityResult)                                             ? availabilityResult :
			                                                                                                              NVSDK_NGX_Result_FAIL_FeatureNotSupported;
			discoveryDetail_ = std::format("NGX SDK NR availability query=0x{:08X} available={} featureQuery=0x{:08X} featureResult={}",
				static_cast<std::uint32_t>(availabilityResult), available,
				static_cast<std::uint32_t>(featureQuery), NVSDK_NGX_FAILED(featureQuery) ? "unavailable" : std::format("0x{:08X}", static_cast<std::uint32_t>(featureResult)));
			if (failureResult != NVSDK_NGX_Result_FAIL_FeatureNotFound &&
				failureResult != NVSDK_NGX_Result_FAIL_FeatureNotSupported &&
				failureResult != NVSDK_NGX_Result_FAIL_UnsupportedParameter) {
				SetFailureLocked(RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
					discoveryDetail_, static_cast<std::uint32_t>(failureResult));
				rollback();
				return false;
			}
		}
		NVSDK_NGX_Parameter* parameters = nullptr;
		const auto allocationResult = NVSDK_NGX_D3D12_AllocateParameters(&parameters);
		parameters_ = parameters;
		if (NVSDK_NGX_FAILED(allocationResult) || !parameters) {
			SetFailureLocked(RuntimeStatus::ParameterAllocationFailed, RuntimeFailureStage::ParameterAllocation,
				std::format("NGX SDK parameter allocation failed 0x{:08X}", static_cast<std::uint32_t>(allocationResult)),
				static_cast<std::uint32_t>(allocationResult));
			rollback();
			return false;
		}
		if (!sdkAvailable) {
			initializationRoute_ = "NGX_D3D12_DIRECT_PROVIDER";
			logger::warn("[DLSSNR] {}; trying the admitted user provider directly", discoveryDetail_);
			std::string providerDetail;
			const auto providerResult = backend_->provider.Initialize(
				std::filesystem::path(backend_->featureDirectory) / kRuntimeName, backend_->providerFile.Get(),
				a_device, writablePath, backend_->capabilities, providerDetail);
			if (NVSDK_NGX_FAILED(providerResult)) {
				SetFailureLocked(RuntimeStatus::InitializationFailed, RuntimeFailureStage::Initialization,
					std::format("{}; {}", discoveryDetail_, providerDetail), static_cast<std::uint32_t>(providerResult));
				rollback();
				return false;
			}
			detail_ = std::format("{}; {}", discoveryDetail_, providerDetail);
		} else {
			detail_ = std::format("official NGX SDK initialized NR with CSX project {}", NvidiaProject::kId);
		}
		status_ = RuntimeStatus::Initialized;
		failureStage_ = RuntimeFailureStage::None;
		ngxResult_ = static_cast<std::uint32_t>(allocationResult);
		LogOnceLocked(initializationLogEmitted_, "initialize", true);
		return true;
	}

	bool Runtime::PrepareScratchLocked(std::uint32_t a_slot)
	{
		auto* parameters = static_cast<NVSDK_NGX_Parameter*>(parameters_);
		std::size_t scratchBytes = 0;
		const auto scratchResult = backend_->provider.scratchSize(kFeatureDlssNr, parameters, &scratchBytes);
		if (NVSDK_NGX_FAILED(scratchResult)) {
			SetFailureLocked(RuntimeStatus::FeatureCreateFailed, RuntimeFailureStage::FeatureCreate,
				"NGX SDK scratch-size query failed", static_cast<std::uint32_t>(scratchResult));
			return false;
		}
		if (scratchBytes) {
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;
			heap.CreationNodeMask = heap.VisibleNodeMask = 1;
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = scratchBytes;
			desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
			desc.SampleDesc.Count = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			const auto result = static_cast<ID3D12Device*>(device_)->CreateCommittedResource(
				&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr, IID_PPV_ARGS(backend_->scratch[a_slot].ReleaseAndGetAddressOf()));
			if (FAILED(result)) {
				SetFailureLocked(RuntimeStatus::FeatureCreateFailed, RuntimeFailureStage::FeatureCreate,
					"NGX SDK scratch allocation failed", static_cast<std::uint32_t>(result));
				return false;
			}
			backend_->scratch[a_slot]->SetName(std::format(L"NeuralRendering::ScratchSlot{}", a_slot).c_str());
		}
		backend_->scratchBytes[a_slot] = scratchBytes;
		parameters->Set(NVSDK_NGX_Parameter_Scratch, backend_->scratch[a_slot].Get());
		parameters->Set(NVSDK_NGX_Parameter_Scratch_SizeInBytes, static_cast<unsigned long long>(scratchBytes));
		return true;
	}

	bool Runtime::PrepareFeature(
		ID3D12GraphicsCommandList* a_commandList, std::uint32_t a_slot,
		const NativeEvaluationLayout& a_layout, ID3D12Resource* a_color,
		ID3D12Resource* a_controlMask, bool a_providerBlending,
		RuntimeExecutionEvidence* a_evidence, std::uint32_t a_networkPreset)
	{
		if (a_evidence)
			*a_evidence = {};
		std::scoped_lock lock(mutex_);
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		if (status_ != RuntimeStatus::Initialized || !a_commandList || a_slot >= kFeatureSlotCount ||
			!GetNativeEvaluationLayoutViolation(a_layout, a_controlMask != nullptr).empty() ||
			(a_providerBlending && (!a_color || !a_controlMask || a_layout.color != a_layout.output))) {
			SetFailureLocked(RuntimeStatus::FeatureCreateFailed, RuntimeFailureStage::FeatureCreate,
				"Feature 18 creation prerequisites are invalid", static_cast<std::uint32_t>(E_INVALIDARG));
			return false;
		}
		return PrepareFeatureLocked(a_commandList, a_slot, a_layout, a_color, a_controlMask, a_providerBlending, a_evidence, a_networkPreset);
	}

	bool Runtime::PrepareFeatureLocked(
		ID3D12GraphicsCommandList* a_commandList, std::uint32_t a_slot,
		const NativeEvaluationLayout& a_layout, ID3D12Resource* a_color,
		ID3D12Resource* a_controlMask, bool a_providerBlending,
		RuntimeExecutionEvidence* a_evidence, std::uint32_t a_networkPreset)
	{
		// Creation receives native presets, including zero for provider-default fallback paths.
		if (a_networkPreset != SanitizeNetworkSelection(a_networkPreset) ||
			(!SupportsNetworkSelection() && a_networkPreset != kNetworkDefault)) {
			SetFailureLocked(RuntimeStatus::FeatureCreateFailed, RuntimeFailureStage::FeatureConfiguration,
				"Feature 18 network preset is unsupported by the installed runtime", static_cast<std::uint32_t>(E_INVALIDARG));
			return false;
		}
		const bool configurationChanged = !featureConfigurations_[a_slot].Matches(a_layout) ||
		                                  featureConfigurations_[a_slot].networkPreset != a_networkPreset ||
		                                  featureConfigurations_[a_slot].providerBlending != a_providerBlending;
		if (featureHandles_[a_slot] && configurationChanged) {
			if (a_evidence)
				a_evidence->createReason = "live_configuration_change_rejected";
			SetFailureLocked(
				RuntimeStatus::FeatureConfigurationChanged,
				RuntimeFailureStage::FeatureConfiguration,
				std::format(
					"Feature 18 slot {} configuration changed while its handle is live; caller must prove GPU idle and reset the slot",
					a_slot),
				static_cast<std::uint32_t>(E_PENDING));
			return false;
		}

		if (!featureHandles_[a_slot]) {
			auto* parameters = static_cast<NVSDK_NGX_Parameter*>(parameters_);
			parameters->Reset();
			parameters->Set(NVSDK_NGX_Parameter_Width, a_layout.creation.output.width);
			parameters->Set(NVSDK_NGX_Parameter_Height, a_layout.creation.output.height);
			parameters->Set(NVSDK_NGX_Parameter_OutWidth, a_layout.creation.output.width);
			parameters->Set(NVSDK_NGX_Parameter_OutHeight, a_layout.creation.output.height);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Width, a_layout.creation.output.width);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Height, a_layout.creation.output.height);

			parameters->Set(
				NVSDK_NGX_Parameter_DLSSNR_ScalingRatio,
				a_layout.ScalingRatio());
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Hint_Render_Preset, a_networkPreset);

			if (a_providerBlending && !a_layout.nativeControlMask) {
				parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ControlMask, static_cast<ID3D12Resource*>(nullptr));
				parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UIAlpha, a_controlMask);
				parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Backbuffer, a_color);
				parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UICorrection, 1);
				parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UseAutoMask, 1);
			}
			if (!PrepareScratchLocked(a_slot))
				return false;
			NVSDK_NGX_Handle* handle = nullptr;
			const auto createStarted = a_evidence ? DiagnosticNow() : std::chrono::steady_clock::time_point{};
			if (a_evidence) {
				a_evidence->createAttempted = true;
				a_evidence->createReason = "missing_feature";
			}

#ifdef DEVBENCH_BRIDGE_ENABLED
			++lifecycleCounts_.creates;
#endif
			const auto createResult = backend_->provider.create(
				a_commandList, kFeatureDlssNr, parameters, &handle);
			if (a_evidence) {
				a_evidence->createCpuMicroseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
					DiagnosticNow() - createStarted)
						.count());
				a_evidence->createSucceeded = !NVSDK_NGX_FAILED(createResult) && handle;
				a_evidence->createResult = static_cast<std::uint32_t>(createResult);
			}
			ngxResult_ = static_cast<std::uint32_t>(createResult);

			if (NVSDK_NGX_FAILED(createResult) || !handle) {
				// A failed NGX feature create may still transfer a handle. Keep it
				// owned so bounded teardown can release or intentionally abandon it.
				if (handle)
					featureHandles_[a_slot] = handle;
				SetFailureLocked(
					RuntimeStatus::FeatureCreateFailed,
					RuntimeFailureStage::FeatureCreate,
					std::format(
						"Feature 18 create failed slot={} result=0x{:08X} trust={}",
						a_slot, ngxResult_, ToString(trust_)),
					ngxResult_);
				return false;
			}

			featureHandles_[a_slot] = handle;
			featureConfigurations_[a_slot] = {
				.networkPreset = a_networkPreset,
				.colorBacking = a_layout.color.backing,
				.creation = a_layout.creation,
				.featureUpscaling = a_layout.featureUpscaling,
				.providerBlending = a_providerBlending,
				.nativeControlMask = a_layout.nativeControlMask,
				.valid = true,
			};
			evaluationSuccessDetail_.reset();
			detail_ = std::format(
				"Feature 18 created slot={} upscaling={} color={}x{} guides={}x{} output={}x{}",
				a_slot, a_layout.featureUpscaling, a_layout.color.backing.width, a_layout.color.backing.height,
				a_layout.creation.input.width, a_layout.creation.input.height, a_layout.creation.output.width, a_layout.creation.output.height);
			LogOnceLocked(featureCreateLogEmitted_, "create", true);
			firstEvaluationResetMask_.fetch_or(1u << a_slot, std::memory_order_relaxed);
		}

		return true;
	}

	void Runtime::AcknowledgePresentedFeatures(std::uint32_t a_slotMask)
	{
		if (firstEvaluationResetMask_.load(std::memory_order_relaxed) & a_slotMask)
			firstEvaluationResetMask_.fetch_and(~a_slotMask, std::memory_order_relaxed);
	}

	bool Runtime::Execute(
		ID3D12GraphicsCommandList* a_commandList, std::uint32_t a_slot,
		ID3D12Resource* a_color, ID3D12Resource* a_depth,
		ID3D12Resource* a_motionVectors, ID3D12Resource* a_output,
		ID3D12Resource* a_controlMask,
		const NativeEvaluationLayout& a_layout, const Tuning& a_tuning, bool a_reset,
		bool* a_evaluationAttempted,
		RuntimeExecutionEvidence* a_evidence,
		D3D12Interop* a_timingInterop,
		std::uint32_t a_timingRegion, bool a_providerBlending)
	{
		if (a_evaluationAttempted)
			*a_evaluationAttempted = false;
		if (a_evidence)
			*a_evidence = {};
		std::scoped_lock lock(mutex_);
		const bool hasControlMask = a_controlMask != nullptr;
		const bool controlMaskContractValid = a_layout.nativeControlMask ? hasControlMask : a_providerBlending ? hasControlMask && a_tuning.useAutoMask && a_layout.color == a_layout.output :
		                                                                                                         hasControlMask != a_tuning.useAutoMask;
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		const auto layoutViolation = GetNativeEvaluationLayoutViolation(a_layout, hasControlMask);
		if (status_ != RuntimeStatus::Initialized || !a_commandList ||
			a_slot >= kFeatureSlotCount || !a_color || !a_depth ||
			!a_motionVectors || !a_output || !layoutViolation.empty() ||
			!Util::IsFinite(a_tuning.intensity) ||
			!Util::IsFinite(a_tuning.localToneStrength) ||
			!Util::IsFinite(a_tuning.localStructureStrength) ||
			!Util::IsFinite(a_tuning.skinStructureStrength) ||
			!Util::IsFinite(a_tuning.singleSubrectScale) ||
			a_tuning.style > kMaximumStyle ||
			a_tuning.singleSubrectScale < 0.25f ||
			a_tuning.singleSubrectScale > 1.0f ||
			a_tuning.uiCorrection || !controlMaskContractValid) {
			SetFailureLocked(
				RuntimeStatus::FeatureEvaluateFailed,
				RuntimeFailureStage::FeatureEvaluate,
				controlMaskContractValid ?
					(layoutViolation.empty() ? "Feature 18 evaluation arguments are invalid" : std::string(layoutViolation)) :
					"Feature 18 control-mask presence, dimensions, and automatic-mask mode are inconsistent",
				static_cast<std::uint32_t>(E_INVALIDARG));
			return false;
		}

		if (!PrepareFeatureLocked(a_commandList, a_slot, a_layout, a_color, a_controlMask, a_providerBlending, a_evidence, a_tuning.networkPreset))
			return false;
		a_reset = a_reset || (firstEvaluationResetMask_.load(std::memory_order_relaxed) & (1u << a_slot)) != 0;

		auto* parameters = static_cast<NVSDK_NGX_Parameter*>(parameters_);
		parameters->Reset();
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ScalingRatio, a_layout.ScalingRatio());
		parameters->Set(NVSDK_NGX_Parameter_Scratch, backend_->scratch[a_slot].Get());
		parameters->Set(NVSDK_NGX_Parameter_Scratch_SizeInBytes, static_cast<unsigned long long>(backend_->scratchBytes[a_slot]));
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Color, a_color);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Depth, a_depth);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MVec, a_motionVectors);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Output, a_output);
		const auto& colorSubrect = a_layout.color.valid;
		const auto& guideSubrect = a_layout.depth.valid;
		const auto& motionSubrect = a_layout.motion.valid;
		const auto& outputSubrect = a_layout.output.valid;
		const auto& controlMaskSubrect = a_layout.controlMask.valid;
		if (a_providerBlending && !a_layout.nativeControlMask) {
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ControlMask, static_cast<ID3D12Resource*>(nullptr));
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UIAlpha, a_controlMask);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Backbuffer, a_color);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectBaseX, outputSubrect.baseX);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectBaseY, outputSubrect.baseY);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectWidth, outputSubrect.width);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectHeight, outputSubrect.height);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectBaseX, outputSubrect.baseX);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectBaseY, outputSubrect.baseY);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectWidth, outputSubrect.width);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectHeight, outputSubrect.height);
		} else if (hasControlMask) {
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ControlMask, a_controlMask);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectBaseX, controlMaskSubrect.baseX);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectBaseY, controlMaskSubrect.baseY);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectWidth, controlMaskSubrect.width);
			parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectHeight, controlMaskSubrect.height);
		}
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ColorSubrectBaseX, colorSubrect.baseX);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ColorSubrectBaseY, colorSubrect.baseY);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ColorSubrectWidth, colorSubrect.width);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_ColorSubrectHeight, colorSubrect.height);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_DepthSubrectBaseX, guideSubrect.baseX);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_DepthSubrectBaseY, guideSubrect.baseY);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_DepthSubrectWidth, guideSubrect.width);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_DepthSubrectHeight, guideSubrect.height);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MVecSubrectBaseX, motionSubrect.baseX);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MVecSubrectBaseY, motionSubrect.baseY);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MVecSubrectWidth, motionSubrect.width);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MVecSubrectHeight, motionSubrect.height);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_OutputSubrectBaseX, outputSubrect.baseX);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_OutputSubrectBaseY, outputSubrect.baseY);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_OutputSubrectWidth, outputSubrect.width);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_OutputSubrectHeight, outputSubrect.height);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MVecScaleX, a_layout.motionVectorScale[0]);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MVecScaleY, a_layout.motionVectorScale[1]);
		parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, a_layout.jitterPixels[0]);
		parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, a_layout.jitterPixels[1]);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_DepthInverted, a_tuning.depthInverted ? 1 : 0);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Enabled, 1);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Reset, a_reset ? 1 : 0);
		const auto limits = GetTuningLimits(a_tuning.networkPreset);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Intensity, std::clamp(a_tuning.intensity, 0.0f, limits.intensity));
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_LocalToneStrength, std::clamp(a_tuning.localToneStrength, 0.0f, limits.localToneStrength));
		parameters->Set(
			NVSDK_NGX_Parameter_DLSSNR_LocalStructureStrength, std::clamp(a_tuning.localStructureStrength, 0.0f, limits.localStructureStrength));
		parameters->Set(
			NVSDK_NGX_Parameter_DLSSNR_SkinStructureStrength, std::clamp(a_tuning.skinStructureStrength, 0.0f, limits.skinStructureStrength));
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UseAutoMask, a_tuning.useAutoMask ? 1 : 0);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MaskBlendMode,
			a_layout.nativeControlMask && a_tuning.useAutoMask ? NVSDK_NGX_DLSSNR_Mask_Blend_Mode_Min : NVSDK_NGX_DLSSNR_Mask_Blend_Mode_None);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Style, a_tuning.style);
		parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UICorrection, a_providerBlending && !a_layout.nativeControlMask ? 1 : 0);

		if (a_evaluationAttempted)
			*a_evaluationAttempted = true;
		if (a_timingInterop)
			a_timingInterop->BeginEvaluationTiming(a_commandList, a_timingRegion);
		const auto evaluateStarted = a_evidence ? DiagnosticNow() : std::chrono::steady_clock::time_point{};
		if (a_evidence)
			a_evidence->evaluateAttempted = true;
		const auto evaluateResult = backend_->provider.evaluate(
			a_commandList, static_cast<NVSDK_NGX_Handle*>(featureHandles_[a_slot]),
			parameters, nullptr);
		if (a_evidence) {
			a_evidence->evaluateCpuMicroseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
				DiagnosticNow() - evaluateStarted)
					.count());
			a_evidence->evaluateSucceeded = !NVSDK_NGX_FAILED(evaluateResult);
			a_evidence->evaluateResult = static_cast<std::uint32_t>(evaluateResult);
		}
		if (a_timingInterop)
			a_timingInterop->EndEvaluationTiming(a_commandList, a_timingRegion);
		ngxResult_ = static_cast<std::uint32_t>(evaluateResult);

		if (NVSDK_NGX_FAILED(evaluateResult)) {
			SetFailureLocked(
				RuntimeStatus::FeatureEvaluateFailed,
				RuntimeFailureStage::FeatureEvaluate,
				std::format(
					"Feature 18 evaluate failed slot={} result=0x{:08X} trust={}",
					a_slot, ngxResult_, ToString(trust_)),
				ngxResult_);
			return false;
		}

		++successfulFrames_;
		status_ = RuntimeStatus::Initialized;
		failureStage_ = RuntimeFailureStage::None;
		detail_.clear();
		evaluationSuccessDetail_ = EvaluationSuccessDetail{
			.slot = a_slot,
			.featureUpscaling = a_layout.featureUpscaling,
			.color = a_layout.color.backing,
			.guides = a_layout.creation.input,
			.output = a_layout.creation.output,
			.controlMask = a_layout.controlMask.backing,
			.outputSubrect = outputSubrect,
		};
		LogOnceLocked(featureEvaluateLogEmitted_, "evaluate", true);
		return true;
	}

	bool Runtime::ResetFeatureLocked(std::uint32_t a_slot)
	{
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		if (a_slot >= kFeatureSlotCount) {
			SetFailureLocked(
				RuntimeStatus::FeatureReleaseFailed,
				RuntimeFailureStage::FeatureRelease,
				"Feature 18 release slot is out of range",
				static_cast<std::uint32_t>(E_INVALIDARG));
			return false;
		}
		if (evaluationSuccessDetail_ && evaluationSuccessDetail_->slot == a_slot)
			evaluationSuccessDetail_.reset();
		if (!featureHandles_[a_slot]) {
			featureConfigurations_[a_slot] = {};
			if (backend_) {
				backend_->scratch[a_slot].Reset();
				backend_->scratchBytes[a_slot] = 0;
			}
			firstEvaluationResetMask_.fetch_and(~(1u << a_slot), std::memory_order_relaxed);
			return true;
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		++lifecycleCounts_.releases;
#endif
		const auto releaseResult = backend_->provider.release(
			static_cast<NVSDK_NGX_Handle*>(featureHandles_[a_slot]));
		ngxResult_ = static_cast<std::uint32_t>(releaseResult);

		if (NVSDK_NGX_FAILED(releaseResult)) {
			SetFailureLocked(
				RuntimeStatus::FeatureReleaseFailed,
				RuntimeFailureStage::FeatureRelease,
				std::format(
					"Feature 18 release failed slot={} result=0x{:08X}",
					a_slot, ngxResult_),
				ngxResult_);
			return false;
		}

		featureHandles_[a_slot] = nullptr;
		featureConfigurations_[a_slot] = {};
		if (backend_) {
			backend_->scratch[a_slot].Reset();
			backend_->scratchBytes[a_slot] = 0;
		}
		firstEvaluationResetMask_.fetch_and(~(1u << a_slot), std::memory_order_relaxed);
		if (device_) {
			status_ = RuntimeStatus::Initialized;
			failureStage_ = RuntimeFailureStage::None;
		}
		return true;
	}

	bool Runtime::HasCompatibleFeature(std::uint32_t slot, const NativeEvaluationLayout& layout, bool providerBlending,
		std::uint32_t networkPreset) const
	{
		std::scoped_lock lock(mutex_);
		return slot < kFeatureSlotCount && featureHandles_[slot] &&
		       featureConfigurations_[slot].networkPreset == networkPreset &&
		       featureConfigurations_[slot].Matches(layout) &&
		       featureConfigurations_[slot].providerBlending == providerBlending;
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	std::pair<std::string, std::string> Runtime::GetInitializationRoute() const
	{
		std::scoped_lock lock(mutex_);
		return { initializationRoute_, discoveryDetail_ };
	}

	std::array<std::uint32_t, Runtime::kFeatureSlotCount> Runtime::GetFeatureNetworkPresets() const
	{
		std::scoped_lock lock(mutex_);
		std::array<std::uint32_t, kFeatureSlotCount> result;
		for (std::size_t slot = 0; slot < result.size(); ++slot)
			result[slot] = featureHandles_[slot] && featureConfigurations_[slot].valid ? featureConfigurations_[slot].networkPreset : UINT32_MAX;
		return result;
	}

	Runtime::LifecycleCounts Runtime::GetLifecycleCounts() const
	{
		std::scoped_lock lock(mutex_);
		return lifecycleCounts_;
	}
#endif

	std::uint32_t Runtime::GetResidentFeatureMask() const
	{
		std::scoped_lock lock(mutex_);
		std::uint32_t mask = 0;
		for (std::size_t index = 0; index < featureHandles_.size(); ++index)
			if (featureHandles_[index])
				mask |= 1u << index;
		return mask;
	}

	bool Runtime::ResetFeature(std::uint32_t a_slot)
	{
		std::scoped_lock lock(mutex_);
		return ResetFeatureLocked(a_slot);
	}

	bool Runtime::ResetFeaturesLocked(bool a_stopOnFailure)
	{
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_)
			return false;
		bool succeeded = true;
		struct Failure
		{
			RuntimeStatus status;
			RuntimeFailureStage stage;
			std::string detail;
			std::uint32_t result;
		};
		std::optional<Failure> firstFailure;
		for (std::uint32_t slot = 0; slot < kFeatureSlotCount; ++slot) {
			if (!ResetFeatureLocked(slot)) {
				if (a_stopOnFailure)
					return false;
				if (!firstFailure)
					firstFailure = Failure{ status_, failureStage_, detail_, ngxResult_ };
				succeeded = false;
			}
		}
		if (firstFailure) {
			status_ = firstFailure->status;
			failureStage_ = firstFailure->stage;
			detail_ = std::move(firstFailure->detail);
			ngxResult_ = firstFailure->result;
		}
		if (succeeded)
			successfulFrames_ = 0;
		return succeeded;
	}

	bool Runtime::ResetFeatures()
	{
		std::scoped_lock lock(mutex_);
		return ResetFeaturesLocked();
	}

	bool Runtime::ReleasePassResources()
	{
		std::scoped_lock lock(mutex_);
		if (abandonRequested_.load(std::memory_order_acquire) || abandoned_ ||
			status_ != RuntimeStatus::Initialized || failureStage_ != RuntimeFailureStage::None)
			return false;
		if (!device_ || !backend_ || !parameters_) {
			SetFailureLocked(
				RuntimeStatus::FeatureReleaseFailed,
				RuntimeFailureStage::FeatureRelease,
				"Pass resource release requires a fully initialized NGX runtime");
			return false;
		}
		if (!ResetFeaturesLocked(true) || abandonRequested_.load(std::memory_order_acquire))
			return false;

		// Parameter bindings must not outlive the caller's retired shared textures.
		static_cast<NVSDK_NGX_Parameter*>(parameters_)->Reset();
		detail_ = "NGX Feature 18 pass resources released; initialized runtime retained";
		return true;
	}

	bool Runtime::ShutdownLocked()
	{
		if (abandonRequested_.load(std::memory_order_acquire)) {
			AbandonLocked();
			return false;
		}
		if (abandoned_)
			return true;
		if (device_) {
			if (!ResetFeaturesLocked())
				return false;
			const auto providerResult = backend_->provider.ShutdownDevice(static_cast<ID3D12Device*>(device_));
			if (NVSDK_NGX_FAILED(providerResult)) {
				SetFailureLocked(RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
					"direct NR provider shutdown failed; ownership retained", static_cast<std::uint32_t>(providerResult));
				return false;
			}
			for (unsigned index = 0; index < 2; ++index) {
				auto* map = index == 0 ? static_cast<NVSDK_NGX_Parameter*>(parameters_) : backend_->capabilities;
				if (!map)
					continue;
				const auto result = NVSDK_NGX_D3D12_DestroyParameters(map);
				if (NVSDK_NGX_FAILED(result)) {
					SetFailureLocked(RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
						"NGX SDK parameter destruction failed", static_cast<std::uint32_t>(result));
					return false;
				}
				if (index == 0)
					parameters_ = nullptr;
				else
					backend_->capabilities = nullptr;
			}
			const auto result = backend_->sdkInitialized ? NVSDK_NGX_D3D12_Shutdown1(static_cast<ID3D12Device*>(device_)) : NVSDK_NGX_Result_Success;
			if (NVSDK_NGX_FAILED(result)) {
				SetFailureLocked(RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
					"NGX SDK shutdown failed", static_cast<std::uint32_t>(result));
				return false;
			}
			backend_->sdkInitialized = false;
			if (!backend_->provider.Unload()) {
				SetFailureLocked(RuntimeStatus::ShutdownFailed, RuntimeFailureStage::Shutdown,
					"direct NR provider import restoration or unload failed; module retained");
				return false;
			}
			static_cast<ID3D12Device*>(device_)->Release();
			device_ = nullptr;
		}
		backend_.reset();
		path_.clear();
		hash_.clear();
		version_.clear();
		tuningLimits_.reset();
		networkSelectionSupported_.reset();
		driverRequirements_ = {};
		initializationRoute_ = "NGX_D3D12_SDK";
		discoveryDetail_.clear();
		detail_.clear();
		evaluationSuccessDetail_.reset();
		featureHandles_ = {};
		featureConfigurations_ = {};
		firstEvaluationResetMask_.store(0, std::memory_order_relaxed);
		status_ = RuntimeStatus::NotProbed;
		trust_ = RuntimeTrust::Unknown;
		failureStage_ = RuntimeFailureStage::None;
		ngxResult_ = 0;
		successfulFrames_ = 0;
		probeLogEmitted_ = false;
		initializationLogEmitted_ = false;
		featureCreateLogEmitted_ = false;
		featureEvaluateLogEmitted_ = false;
		abandoned_ = false;
		return true;
	}

	void Runtime::AbandonLocked() noexcept
	{
		if (abandoned_)
			return;
		// Unsafe GPU ownership remains resident until process exit.
		(void)backend_.release();
		device_ = nullptr;
		parameters_ = nullptr;
		featureHandles_ = {};
		featureConfigurations_ = {};
		firstEvaluationResetMask_.store(0, std::memory_order_relaxed);
		failureStage_ = RuntimeFailureStage::UnsafeAbandon;
		status_ = RuntimeStatus::UnsafeAbandoned;
		evaluationSuccessDetail_.reset();
		abandoned_ = true;
		try {
			detail_ = "NGX SDK objects intentionally retained after unsafe teardown";
			logger::critical("[DLSSNR] unsafe ownership abandoned: {}", detail_);
		} catch (...) {
		}
	}

	bool Runtime::Shutdown()
	{
		std::scoped_lock lock(mutex_);
		return ShutdownLocked();
	}

	void Runtime::AbandonUnsafe() noexcept
	{
		abandonRequested_.store(true, std::memory_order_release);
		try {
			std::scoped_lock lock(mutex_);
			AbandonLocked();
		} catch (...) {
		}
	}

	RuntimeStatus Runtime::Status() const
	{
		std::scoped_lock lock(mutex_);
		return status_;
	}

	RuntimeTrust Runtime::Trust() const
	{
		std::scoped_lock lock(mutex_);
		return trust_;
	}

	RuntimeFailureStage Runtime::FailureStage() const
	{
		std::scoped_lock lock(mutex_);
		return failureStage_;
	}

	std::filesystem::path Runtime::Path() const
	{
		std::scoped_lock lock(mutex_);
		return path_;
	}

	std::string Runtime::Hash() const
	{
		std::scoped_lock lock(mutex_);
		return hash_;
	}

	TuningLimits Runtime::GetTuningLimits(std::uint32_t selection) const
	{
		std::scoped_lock lock(mutex_);
		const auto defaults = tuningLimits_ ? *tuningLimits_ : GetInstalledRuntimeMetadata().tuningLimits;
		return GetTuningLimitsForNetwork(defaults, GetNetworkPreset(selection));
	}

	bool Runtime::SupportsNetworkSelection() const
	{
		std::scoped_lock lock(mutex_);
		if (networkSelectionSupported_)
			return *networkSelectionSupported_;
		return GetInstalledRuntimeMetadata().networkSelectionSupported;
	}

	std::uint32_t Runtime::GetNetworkPreset(std::uint32_t selection) const
	{
		return ResolveNetworkPreset(selection, SupportsNetworkSelection());
	}

	std::string Runtime::Version() const
	{
		std::scoped_lock lock(mutex_);
		return version_;
	}

	std::uint64_t Runtime::AllocationVersion() const
	{
		std::scoped_lock lock(mutex_);
		return allocationVersion_ ? allocationVersion_ : GetInstalledRuntimeMetadata().allocationVersion;
	}

	std::string Runtime::DetailLocked() const
	{
		if (!evaluationSuccessDetail_)
			return detail_;
		const auto& detail = *evaluationSuccessDetail_;
		return std::format(
			"Feature 18 evaluated slot={} upscaling={} color={}x{} guides={}x{} output={}x{} controlMask={}x{} subrect=({},{} {}x{})",
			detail.slot, detail.featureUpscaling, detail.color.width, detail.color.height,
			detail.guides.width, detail.guides.height, detail.output.width, detail.output.height,
			detail.controlMask.width, detail.controlMask.height, detail.outputSubrect.baseX, detail.outputSubrect.baseY,
			detail.outputSubrect.width, detail.outputSubrect.height);
	}

	std::string Runtime::Detail() const
	{
		std::scoped_lock lock(mutex_);
		return DetailLocked();
	}

	std::uint32_t Runtime::NgxResult() const
	{
		std::scoped_lock lock(mutex_);
		return ngxResult_;
	}

	NgxDriverRequirements Runtime::DriverRequirements() const
	{
		std::scoped_lock lock(mutex_);
		return driverRequirements_;
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	NgxAllocationStatistics Runtime::QueryAllocationStatistics() const
	{
		std::scoped_lock lock(mutex_);
		return QueryAllocationStatisticsLocked();
	}
#endif

	NativeMemorySnapshot Runtime::QueryMemorySnapshot() const
	{
		std::scoped_lock lock(mutex_);
		NativeMemorySnapshot snapshot;
		if (!backend_ || !device_ || status_ != RuntimeStatus::Initialized || abandoned_ || abandonRequested_.load(std::memory_order_acquire))
			return snapshot;
		for (std::size_t slot = 0; slot < kFeatureSlotCount; ++slot) {
			snapshot.scratchBytes[slot] = backend_->scratchBytes[slot];
			if (featureHandles_[slot])
				snapshot.residentMask |= 1u << slot;
		}
		snapshot.provider = QueryAllocationStatisticsLocked();
		snapshot.sampledAtMs = GetTickCount64();
		snapshot.valid = true;
		return snapshot;
	}

	NgxAllocationStatistics Runtime::QueryAllocationStatisticsLocked() const
	{
		NgxAllocationStatistics statistics;
		if (!backend_ || !backend_->capabilities || !device_ || abandoned_ || abandonRequested_.load(std::memory_order_acquire))
			return statistics;
		const auto started = std::chrono::steady_clock::now();
		const auto queryStatistics = [&] {
			void* callback = nullptr;
			const auto query = backend_->capabilities->Get(NVSDK_NGX_Parameter_DLSSNRGetStatsCallback, &callback);
			statistics.callbackQuery = static_cast<std::uint32_t>(query);
			if (NVSDK_NGX_FAILED(query) || !callback)
				return;
			const auto result = reinterpret_cast<PFN_NVSDK_NGX_DLSSNR_GetStatsCallback>(callback)(backend_->capabilities);
			statistics.callbackResult = static_cast<std::uint32_t>(result);
			if (NVSDK_NGX_FAILED(result))
				return;
			unsigned long long allocated = 0;
			const auto sizeResult = backend_->capabilities->Get(NVSDK_NGX_Parameter_SizeInBytes, &allocated);
			statistics.sizeQuery = static_cast<std::uint32_t>(sizeResult);
			if (!NVSDK_NGX_FAILED(sizeResult))
				statistics.allocatedBytes = allocated;
		};
		try {
			queryStatistics();
		} catch (...) {
			statistics.queryThrew = true;
			statistics.allocatedBytes.reset();
		}
		statistics.queryMicroseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - started)
				.count());
		return statistics;
	}

	CapacityFailure Runtime::CreationCapacityFailure() const
	{
		std::scoped_lock lock(mutex_);
		if (failureStage_ != RuntimeFailureStage::FeatureCreate || abandoned_ || abandonRequested_.load(std::memory_order_acquire))
			return CapacityFailure::Unsafe;
		if (ngxResult_ == NVSDK_NGX_Result_FAIL_OutOfGPUMemory || ngxResult_ == static_cast<std::uint32_t>(E_OUTOFMEMORY))
			return CapacityFailure::Pressure;
		if (ngxResult_ == NVSDK_NGX_Result_FAIL_FeatureNotSupported || ngxResult_ == NVSDK_NGX_Result_FAIL_UnsupportedParameter)
			return CapacityFailure::Unsupported;
		return CapacityFailure::Unsafe;
	}

	std::uint64_t Runtime::SuccessfulFrames() const
	{
		std::scoped_lock lock(mutex_);
		return successfulFrames_;
	}

	const char* ToString(RuntimeStatus a_status)
	{
		switch (a_status) {
		case RuntimeStatus::NotProbed:
			return "not-probed";
		case RuntimeStatus::NotFound:
			return "not-found";
		case RuntimeStatus::HashFailed:
			return "hash-failed";
		case RuntimeStatus::VersionRejected:
			return "version-rejected";
		case RuntimeStatus::LoadFailed:
			return "load-failed";
		case RuntimeStatus::MissingExport:
			return "missing-export";
		case RuntimeStatus::IdentityFailed:
			return "identity-failed";
		case RuntimeStatus::Ready:
			return "ready";
		case RuntimeStatus::InitializationFailed:
			return "initialization-failed";
		case RuntimeStatus::ParameterAllocationFailed:
			return "parameter-allocation-failed";
		case RuntimeStatus::Initialized:
			return "initialized";
		case RuntimeStatus::FeatureConfigurationChanged:
			return "feature-configuration-changed";
		case RuntimeStatus::FeatureCreateFailed:
			return "feature-create-failed";
		case RuntimeStatus::FeatureEvaluateFailed:
			return "feature-evaluate-failed";
		case RuntimeStatus::FeatureReleaseFailed:
			return "feature-release-failed";
		case RuntimeStatus::ShutdownFailed:
			return "shutdown-failed";
		case RuntimeStatus::UnsafeAbandoned:
			return "unsafe-abandoned";
		}
		return "unknown";
	}

	const char* ToString(RuntimeTrust a_trust)
	{
		switch (a_trust) {
		case RuntimeTrust::Unknown:
			return "unknown";
		case RuntimeTrust::VersionAccepted:
			return "version-accepted";
		}
		return "unknown";
	}

	const char* ToString(RuntimeFailureStage a_stage)
	{
		switch (a_stage) {
		case RuntimeFailureStage::None:
			return "none";
		case RuntimeFailureStage::Discovery:
			return "discovery";
		case RuntimeFailureStage::Hash:
			return "hash";
		case RuntimeFailureStage::Version:
			return "version";
		case RuntimeFailureStage::Load:
			return "load";
		case RuntimeFailureStage::Identity:
			return "identity";
		case RuntimeFailureStage::Initialization:
			return "initialization";
		case RuntimeFailureStage::ParameterAllocation:
			return "parameter-allocation";
		case RuntimeFailureStage::FeatureConfiguration:
			return "feature-configuration";
		case RuntimeFailureStage::FeatureCreate:
			return "feature-create";
		case RuntimeFailureStage::FeatureEvaluate:
			return "feature-evaluate";
		case RuntimeFailureStage::FeatureRelease:
			return "feature-release";
		case RuntimeFailureStage::Shutdown:
			return "shutdown";
		case RuntimeFailureStage::UnsafeAbandon:
			return "unsafe-abandon";
		}
		return "unknown";
	}

}
