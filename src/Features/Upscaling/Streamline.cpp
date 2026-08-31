#include "Streamline.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <limits>
#include <string>
#include <string_view>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#	include <cstring>
#	include <mutex>
#endif

#include "../../Deferred.h"
#include "../../State.h"
#include "../../Util.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"
#include "ReflexPolicy.h"

namespace
{
	constexpr UINT NVIDIA_VENDOR_ID = 0x10DE;
	constexpr uint32_t kDLSSDiagnosticMaxInitialLogs = 12;
	constexpr uint32_t kDLSSDiagnosticRepeatFrameGap = 300;
	constexpr int32_t kDLSSDiagnosticTextResultCode = std::numeric_limits<int32_t>::min();
	void* s_streamlineDllDirectoryCookie = nullptr;

	struct StreamlineCoreBindings
	{
		PFun_slInit* init = nullptr;
		PFun_slShutdown* shutdown = nullptr;
		PFun_slIsFeatureSupported* isFeatureSupported = nullptr;
		PFun_slIsFeatureLoaded* isFeatureLoaded = nullptr;
		PFun_slSetFeatureLoaded* setFeatureLoaded = nullptr;
		PFun_slEvaluateFeature* evaluateFeature = nullptr;
		PFun_slAllocateResources* allocateResources = nullptr;
		PFun_slFreeResources* freeResources = nullptr;
		PFun_slGetFeatureRequirements* getFeatureRequirements = nullptr;
		PFun_slGetFeatureVersion* getFeatureVersion = nullptr;
		PFun_slUpgradeInterface* upgradeInterface = nullptr;
		PFun_slSetConstants* setConstants = nullptr;
		PFun_slGetNativeInterface* getNativeInterface = nullptr;
		PFun_slGetFeatureFunction* getFeatureFunction = nullptr;
		PFun_slGetNewFrameToken* getNewFrameToken = nullptr;
		PFun_slSetD3DDevice* setD3DDevice = nullptr;

		[[nodiscard]] std::vector<std::string_view> MissingRequired() const
		{
			std::vector<std::string_view> missing;
			const auto require = [&](const void* a_function, std::string_view a_name) {
				if (!a_function)
					missing.push_back(a_name);
			};
			require(reinterpret_cast<const void*>(init), "slInit");
			require(reinterpret_cast<const void*>(shutdown), "slShutdown");
			require(reinterpret_cast<const void*>(isFeatureSupported), "slIsFeatureSupported");
			require(reinterpret_cast<const void*>(isFeatureLoaded), "slIsFeatureLoaded");
			require(reinterpret_cast<const void*>(evaluateFeature), "slEvaluateFeature");
			require(reinterpret_cast<const void*>(freeResources), "slFreeResources");
			require(reinterpret_cast<const void*>(getFeatureRequirements), "slGetFeatureRequirements");
			require(reinterpret_cast<const void*>(upgradeInterface), "slUpgradeInterface");
			require(reinterpret_cast<const void*>(setConstants), "slSetConstants");
			require(reinterpret_cast<const void*>(getFeatureFunction), "slGetFeatureFunction");
			require(reinterpret_cast<const void*>(getNewFrameToken), "slGetNewFrameToken");
			require(reinterpret_cast<const void*>(setD3DDevice), "slSetD3DDevice");
			return missing;
		}
	};

	enum class D3D11IdleFenceResult : uint8_t
	{
		Ready,
		Pending,
		Failed
	};

	enum class DLSSDiagnosticStage : uint8_t
	{
		ResolveViewport,
		FrameToken,
		SetConstants,
		SetOptions,
		Evaluate,
		Count
	};

	const char* GetDLSSDiagnosticStageName(DLSSDiagnosticStage a_stage)
	{
		switch (a_stage) {
		case DLSSDiagnosticStage::ResolveViewport:
			return "ResolveViewport";
		case DLSSDiagnosticStage::FrameToken:
			return "FrameToken";
		case DLSSDiagnosticStage::SetConstants:
			return "SetConstants";
		case DLSSDiagnosticStage::SetOptions:
			return "SetOptions";
		case DLSSDiagnosticStage::Evaluate:
			return "Evaluate";
		default:
			return "Unknown";
		}
	}

	void ReleaseD3D11IdleFence(ID3D11Query*& a_query)
	{
		if (!a_query)
			return;

		a_query->Release();
		a_query = nullptr;
	}

	bool IsHDRDLSSInputFormat(DXGI_FORMAT a_format)
	{
		switch (a_format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return false;
		default:
			return true;
		}
	}

	void EnsureStreamlineDllDirectory(const std::filesystem::path& a_pluginDir)
	{
		if (s_streamlineDllDirectoryCookie)
			return;

		auto kernel32 = GetModuleHandleW(L"kernel32.dll");
		if (!kernel32) {
			logger::warn("[Streamline] Could not get kernel32 module while preparing DLL search path");
			return;
		}

		using AddDllDirectoryFn = void*(WINAPI*)(PCWSTR);
		auto addDllDirectory = reinterpret_cast<AddDllDirectoryFn>(GetProcAddress(kernel32, "AddDllDirectory"));
		if (!addDllDirectory) {
			logger::warn("[Streamline] AddDllDirectory is unavailable; interposer dependency discovery will rely on the DLL load directory and default DLL directories");
			return;
		}

		s_streamlineDllDirectoryCookie = addDllDirectory(a_pluginDir.c_str());
		if (!s_streamlineDllDirectoryCookie) {
			logger::warn(
				"[Streamline] Failed to add Streamline DLL directory {} (error {})",
				stl::utf16_to_utf8(a_pluginDir.wstring()).value_or("<unknown>"),
				GetLastError());
		}
	}

	void ReleaseStreamlineDllDirectory()
	{
		if (!s_streamlineDllDirectoryCookie)
			return;

		auto kernel32 = GetModuleHandleW(L"kernel32.dll");
		using RemoveDllDirectoryFn = BOOL(WINAPI*)(void*);
		auto removeDllDirectory = kernel32 ?
		                              reinterpret_cast<RemoveDllDirectoryFn>(GetProcAddress(kernel32, "RemoveDllDirectory")) :
		                              nullptr;
		if (removeDllDirectory && !removeDllDirectory(s_streamlineDllDirectoryCookie)) {
			logger::warn("[Streamline] Failed to remove the Streamline DLL directory (error {})", GetLastError());
		}
		s_streamlineDllDirectoryCookie = nullptr;
	}

	bool ValidateStreamlineRuntime(const std::filesystem::path& a_pluginDir)
	{
		struct RequiredFile
		{
			const wchar_t* name;
			bool requireStreamlineVersion;
		};
		constexpr std::array requiredFiles{
			RequiredFile{ L"nvngx_dlss.dll", false },
			RequiredFile{ L"sl.common.dll", true },
			RequiredFile{ L"sl.dlss.dll", true },
			RequiredFile{ L"sl.interposer.dll", true },
			RequiredFile{ L"sl.pcl.dll", true },
			RequiredFile{ L"sl.reflex.dll", true },
		};
		const REL::Version expectedVersion(
			CSX_STREAMLINE_RUNTIME_VERSION_MAJOR,
			CSX_STREAMLINE_RUNTIME_VERSION_MINOR,
			CSX_STREAMLINE_RUNTIME_VERSION_PATCH,
			0);

		for (const auto& required : requiredFiles) {
			const auto path = a_pluginDir / required.name;
			std::error_code fileError;
			if (!std::filesystem::is_regular_file(path, fileError)) {
				logger::error(
					"[Streamline] Required runtime file is unavailable: {}",
					stl::utf16_to_utf8(path.wstring()).value_or("<unknown>"));
				return false;
			}
			const auto version = Util::GetDllVersion(path.wstring());
			if (!version) {
				logger::error(
					"[Streamline] Required runtime file has no readable version: {}",
					stl::utf16_to_utf8(path.wstring()).value_or("<unknown>"));
				return false;
			}
			if (required.requireStreamlineVersion && version->compare(expectedVersion) != std::strong_ordering::equal) {
				logger::error(
					"[Streamline] Runtime version mismatch for {}: expected {}, found {}",
					stl::utf16_to_utf8(required.name).value_or("<unknown>"),
					Util::GetFormattedVersion(expectedVersion),
					Util::GetFormattedVersion(*version));
				return false;
			}
		}
		return true;
	}

	StreamlineCoreBindings BindStreamlineCore(HMODULE a_module)
	{
		StreamlineCoreBindings bindings;
		bindings.init = reinterpret_cast<PFun_slInit*>(GetProcAddress(a_module, "slInit"));
		bindings.shutdown = reinterpret_cast<PFun_slShutdown*>(GetProcAddress(a_module, "slShutdown"));
		bindings.isFeatureSupported = reinterpret_cast<PFun_slIsFeatureSupported*>(GetProcAddress(a_module, "slIsFeatureSupported"));
		bindings.isFeatureLoaded = reinterpret_cast<PFun_slIsFeatureLoaded*>(GetProcAddress(a_module, "slIsFeatureLoaded"));
		bindings.setFeatureLoaded = reinterpret_cast<PFun_slSetFeatureLoaded*>(GetProcAddress(a_module, "slSetFeatureLoaded"));
		bindings.evaluateFeature = reinterpret_cast<PFun_slEvaluateFeature*>(GetProcAddress(a_module, "slEvaluateFeature"));
		bindings.allocateResources = reinterpret_cast<PFun_slAllocateResources*>(GetProcAddress(a_module, "slAllocateResources"));
		bindings.freeResources = reinterpret_cast<PFun_slFreeResources*>(GetProcAddress(a_module, "slFreeResources"));
		bindings.getFeatureRequirements = reinterpret_cast<PFun_slGetFeatureRequirements*>(GetProcAddress(a_module, "slGetFeatureRequirements"));
		bindings.getFeatureVersion = reinterpret_cast<PFun_slGetFeatureVersion*>(GetProcAddress(a_module, "slGetFeatureVersion"));
		bindings.upgradeInterface = reinterpret_cast<PFun_slUpgradeInterface*>(GetProcAddress(a_module, "slUpgradeInterface"));
		bindings.setConstants = reinterpret_cast<PFun_slSetConstants*>(GetProcAddress(a_module, "slSetConstants"));
		bindings.getNativeInterface = reinterpret_cast<PFun_slGetNativeInterface*>(GetProcAddress(a_module, "slGetNativeInterface"));
		bindings.getFeatureFunction = reinterpret_cast<PFun_slGetFeatureFunction*>(GetProcAddress(a_module, "slGetFeatureFunction"));
		bindings.getNewFrameToken = reinterpret_cast<PFun_slGetNewFrameToken*>(GetProcAddress(a_module, "slGetNewFrameToken"));
		bindings.setD3DDevice = reinterpret_cast<PFun_slSetD3DDevice*>(GetProcAddress(a_module, "slSetD3DDevice"));
		return bindings;
	}

	HMODULE LoadStreamlineDll(const std::filesystem::path& a_path, DWORD& a_error)
	{
		a_error = ERROR_SUCCESS;

		constexpr DWORD kLoadFlags =
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
			LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
			LOAD_LIBRARY_SEARCH_USER_DIRS;

		auto module = LoadLibraryExW(a_path.c_str(), nullptr, kLoadFlags);
		if (module)
			return module;

		a_error = GetLastError();
		logger::warn("[Streamline] LoadLibraryEx failed for {} with error {}",
			stl::utf16_to_utf8(a_path.wstring()).value_or("<unknown>"),
			a_error);
		return nullptr;
	}

	bool TryGetTexture2DDesc(ID3D11Resource* a_resource, D3D11_TEXTURE2D_DESC& a_desc)
	{
		if (!a_resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
			return false;

		texture->GetDesc(&a_desc);
		return true;
	}

	bool GetDLSSColorBuffersHDR(ID3D11Resource* a_colorIn)
	{
		D3D11_TEXTURE2D_DESC desc{};
		if (!TryGetTexture2DDesc(a_colorIn, desc))
			return true;

		return IsHDRDLSSInputFormat(desc.Format);
	}

	uint64_t ComputeConstantsIdentity(const sl::Constants& a_constants) noexcept
	{
		uint64_t hash = 14695981039346656037ull;
		const auto mix = [&](uint32_t a_value) {
			hash ^= a_value;
			hash *= 1099511628211ull;
		};
		const auto mixFloat = [&](float a_value) {
			mix(std::bit_cast<uint32_t>(a_value));
		};
		const auto mixMatrix = [&](const sl::float4x4& a_matrix) {
			for (uint32_t row = 0; row < 4; ++row) {
				mixFloat(a_matrix[row].x);
				mixFloat(a_matrix[row].y);
				mixFloat(a_matrix[row].z);
				mixFloat(a_matrix[row].w);
			}
		};

		mixMatrix(a_constants.cameraViewToClip);
		mixMatrix(a_constants.clipToCameraView);
		mixMatrix(a_constants.clipToLensClip);
		mixMatrix(a_constants.clipToPrevClip);
		mixMatrix(a_constants.prevClipToClip);
		mixFloat(a_constants.jitterOffset.x);
		mixFloat(a_constants.jitterOffset.y);
		mixFloat(a_constants.mvecScale.x);
		mixFloat(a_constants.mvecScale.y);
		mixFloat(a_constants.cameraPinholeOffset.x);
		mixFloat(a_constants.cameraPinholeOffset.y);
		mixFloat(a_constants.cameraPos.x);
		mixFloat(a_constants.cameraPos.y);
		mixFloat(a_constants.cameraPos.z);
		mixFloat(a_constants.cameraUp.x);
		mixFloat(a_constants.cameraUp.y);
		mixFloat(a_constants.cameraUp.z);
		mixFloat(a_constants.cameraRight.x);
		mixFloat(a_constants.cameraRight.y);
		mixFloat(a_constants.cameraRight.z);
		mixFloat(a_constants.cameraFwd.x);
		mixFloat(a_constants.cameraFwd.y);
		mixFloat(a_constants.cameraFwd.z);
		mixFloat(a_constants.cameraNear);
		mixFloat(a_constants.cameraFar);
		mixFloat(a_constants.cameraFOV);
		mixFloat(a_constants.cameraAspectRatio);
		mixFloat(a_constants.motionVectorsInvalidValue);
		mix(static_cast<uint32_t>(a_constants.depthInverted));
		mix(static_cast<uint32_t>(a_constants.cameraMotionIncluded));
		mix(static_cast<uint32_t>(a_constants.motionVectors3D));
		mix(static_cast<uint32_t>(a_constants.reset));
		mix(static_cast<uint32_t>(a_constants.orthographicProjection));
		mix(static_cast<uint32_t>(a_constants.motionVectorsDilated));
		mix(static_cast<uint32_t>(a_constants.motionVectorsJittered));
		mixFloat(a_constants.minRelativeLinearDepthObjectSeparation);
		return hash;
	}

	std::string FormatExtent(const sl::Extent& a_extent)
	{
		return std::format("top={} left={} width={} height={}", a_extent.top, a_extent.left, a_extent.width, a_extent.height);
	}

	std::string DescribeTextureResource(ID3D11Resource* a_resource)
	{
		if (!a_resource)
			return "null";

		D3D11_TEXTURE2D_DESC desc{};
		if (!TryGetTexture2DDesc(a_resource, desc)) {
			return std::format("ptr=0x{:X} non-Texture2D", reinterpret_cast<std::uintptr_t>(a_resource));
		}

		return std::format(
			"ptr=0x{:X} {}x{} fmt={} mips={} array={} samples={} bind=0x{:X} misc=0x{:X} usage={} cpu=0x{:X}",
			reinterpret_cast<std::uintptr_t>(a_resource),
			desc.Width,
			desc.Height,
			magic_enum::enum_name(desc.Format),
			desc.MipLevels,
			desc.ArraySize,
			desc.SampleDesc.Count,
			desc.BindFlags,
			desc.MiscFlags,
			magic_enum::enum_name(desc.Usage),
			desc.CPUAccessFlags);
	}

	bool ShouldLogDLSSDiagnostics()
	{
		return globals::state && globals::state->IsDeveloperMode();
	}

	std::string FormatDLSSDiagnosticResult(int32_t a_resultCode, std::string_view a_resultLabel)
	{
		if (!a_resultLabel.empty())
			return std::string(a_resultLabel);

		return std::format("{}", a_resultCode);
	}

	int32_t QuantizeDLSSDiagnosticFloat(float a_value)
	{
		if (!std::isfinite(a_value))
			return 0;

		const double scaled = static_cast<double>(a_value) * 1000000.0;
		if (scaled > static_cast<double>(std::numeric_limits<int32_t>::max()))
			return std::numeric_limits<int32_t>::max();
		if (scaled < static_cast<double>(std::numeric_limits<int32_t>::min()))
			return std::numeric_limits<int32_t>::min();

		return static_cast<int32_t>(std::lround(scaled));
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	constexpr std::size_t kDLSSDevBenchTraceIdentityCapacity = 32;
	thread_local uint64_t g_dlssDevBenchCompositorCycleToken = 0;

	struct DLSSDevBenchTraceIdentityRecord
	{
		bool valid = false;
		uint64_t lastUse = 0;
		Streamline::DLSSDevBenchTraceCall call{};
	};

	struct DLSSDevBenchTraceState
	{
		std::mutex mutex;
		std::atomic_uint64_t activeSessionID{ 0 };
		std::atomic_uint64_t droppedRecords{ 0 };
		uint64_t nextSessionID = 1;
		uint64_t sessionID = 0;
		uint64_t qpcFrequency = 0;
		uint64_t totalRecords = 0;
		uint64_t constantsCacheReuses = 0;
		uint64_t setConstantsCalls = 0;
		uint64_t evaluateCalls = 0;
		uint64_t duplicatedConstantsFailures = 0;
		uint64_t evaluateFailures = 0;
		uint64_t identityUseCounter = 0;
		std::size_t recordWriteIndex = 0;
		std::size_t recordCount = 0;
		bool lastDuplicatedConstantsFailureFound = false;
		bool lastEvaluateFailureFound = false;
		Streamline::DLSSDevBenchTraceRecord lastDuplicatedConstantsFailure{};
		Streamline::DLSSDevBenchTraceRecord lastEvaluateFailure{};
		std::array<Streamline::DLSSDevBenchTraceRecord, Streamline::kDLSSDevBenchTraceCapacity> records{};
		std::array<DLSSDevBenchTraceIdentityRecord, kDLSSDevBenchTraceIdentityCapacity> acceptedConstants{};
		std::array<DLSSDevBenchTraceIdentityRecord, kDLSSDevBenchTraceIdentityCapacity> evaluations{};

		DLSSDevBenchTraceState()
		{
			LARGE_INTEGER frequency{};
			if (QueryPerformanceFrequency(&frequency))
				qpcFrequency = static_cast<uint64_t>(frequency.QuadPart);
		}
	};
	DLSSDevBenchTraceState g_dlssDevBenchTraceState;

	DLSSDevBenchTraceState& GetDLSSDevBenchTraceState()
	{
		return g_dlssDevBenchTraceState;
	}

	void ClearDLSSDevBenchTraceLocked(DLSSDevBenchTraceState& a_state)
	{
		a_state.droppedRecords.store(0, std::memory_order_release);
		a_state.totalRecords = 0;
		a_state.constantsCacheReuses = 0;
		a_state.setConstantsCalls = 0;
		a_state.evaluateCalls = 0;
		a_state.duplicatedConstantsFailures = 0;
		a_state.evaluateFailures = 0;
		a_state.identityUseCounter = 0;
		a_state.recordWriteIndex = 0;
		a_state.recordCount = 0;
		a_state.lastDuplicatedConstantsFailureFound = false;
		a_state.lastEvaluateFailureFound = false;
		a_state.lastDuplicatedConstantsFailure = {};
		a_state.lastEvaluateFailure = {};
		a_state.records = {};
		a_state.acceptedConstants = {};
		a_state.evaluations = {};
	}

	template <class T, std::size_t N>
	T* FindDLSSDevBenchTraceIdentity(
		std::array<T, N>& a_records,
		const Streamline::DLSSDevBenchTraceSignature& a_signature)
	{
		for (auto& record : a_records) {
			if (record.valid &&
				record.call.signature.frameToken == a_signature.frameToken &&
				record.call.signature.resolvedViewport == a_signature.resolvedViewport) {
				return &record;
			}
		}
		return nullptr;
	}

	template <class T, std::size_t N>
	T& SelectDLSSDevBenchTraceIdentitySlot(std::array<T, N>& a_records)
	{
		for (auto& record : a_records) {
			if (!record.valid)
				return record;
		}

		return *std::min_element(
			a_records.begin(),
			a_records.end(),
			[](const T& a_left, const T& a_right) {
				return a_left.lastUse < a_right.lastUse;
			});
	}

	uint64_t BuildDLSSDevBenchChangedFieldMask(
		const Streamline::DLSSDevBenchTraceSignature& a_previous,
		const Streamline::DLSSDevBenchTraceSignature& a_current)
	{
		using Field = Streamline::DLSSDevBenchTraceSignatureField;
		static_assert(static_cast<uint8_t>(Field::Count) <= 64);
		uint64_t mask = 0;
		const auto changed = [&](Field a_field, bool a_changed) {
			if (a_changed)
				mask |= uint64_t{ 1 } << static_cast<uint8_t>(a_field);
		};

		changed(Field::Frame, a_previous.frame != a_current.frame);
		changed(Field::FrameToken, a_previous.frameToken != a_current.frameToken || a_previous.frameTokenAddress != a_current.frameTokenAddress);
		changed(Field::RequestedViewport, a_previous.requestedViewport != a_current.requestedViewport);
		changed(Field::ResolvedViewport, a_previous.resolvedViewport != a_current.resolvedViewport);
		changed(Field::EyeIndex, a_previous.eyeIndex != a_current.eyeIndex);
		changed(Field::ViewportRole, a_previous.viewportRole != a_current.viewportRole);
		changed(Field::OutputWidth, a_previous.outputWidth != a_current.outputWidth);
		changed(Field::OutputHeight, a_previous.outputHeight != a_current.outputHeight);
		changed(Field::QualityMode, a_previous.qualityMode != a_current.qualityMode);
		changed(Field::DLSSPreset, a_previous.dlssPreset != a_current.dlssPreset);
		changed(Field::ExtentInLeft, a_previous.extentInLeft != a_current.extentInLeft);
		changed(Field::ExtentInTop, a_previous.extentInTop != a_current.extentInTop);
		changed(Field::ExtentInWidth, a_previous.extentInWidth != a_current.extentInWidth);
		changed(Field::ExtentInHeight, a_previous.extentInHeight != a_current.extentInHeight);
		changed(Field::ExtentOutLeft, a_previous.extentOutLeft != a_current.extentOutLeft);
		changed(Field::ExtentOutTop, a_previous.extentOutTop != a_current.extentOutTop);
		changed(Field::ExtentOutWidth, a_previous.extentOutWidth != a_current.extentOutWidth);
		changed(Field::ExtentOutHeight, a_previous.extentOutHeight != a_current.extentOutHeight);
		changed(Field::ViewportScaleX, a_previous.viewportScaleXQ != a_current.viewportScaleXQ);
		changed(Field::ViewportScaleY, a_previous.viewportScaleYQ != a_current.viewportScaleYQ);
		changed(Field::PinholeOffsetX, a_previous.pinholeOffsetXQ != a_current.pinholeOffsetXQ);
		changed(Field::PinholeOffsetY, a_previous.pinholeOffsetYQ != a_current.pinholeOffsetYQ);
		changed(Field::JitterX, a_previous.jitterXQ != a_current.jitterXQ);
		changed(Field::JitterY, a_previous.jitterYQ != a_current.jitterYQ);
		changed(Field::HistoryReset, a_previous.historyResetRequested != a_current.historyResetRequested);
		changed(Field::ColorBuffersHDR, a_previous.colorBuffersHDR != a_current.colorBuffersHDR);
		changed(Field::SubmitStageVR, a_previous.submitStageVRDLSS != a_current.submitStageVRDLSS);
		changed(Field::ColorInput, a_previous.colorIn != a_current.colorIn);
		changed(Field::ColorOutput, a_previous.colorOut != a_current.colorOut);
		changed(Field::Depth, a_previous.depth != a_current.depth);
		changed(Field::MotionVectors, a_previous.motionVectors != a_current.motionVectors);
		changed(Field::ReactiveMask, a_previous.reactiveMask != a_current.reactiveMask);
		changed(Field::TransparencyMask, a_previous.transparencyMask != a_current.transparencyMask);
		changed(Field::CameraViewToClip, a_previous.constants.cameraViewToClip != a_current.constants.cameraViewToClip);
		changed(Field::ClipToCameraView, a_previous.constants.clipToCameraView != a_current.constants.clipToCameraView);
		changed(Field::ClipToLensClip, a_previous.constants.clipToLensClip != a_current.constants.clipToLensClip);
		changed(Field::ClipToPrevClip, a_previous.constants.clipToPrevClip != a_current.constants.clipToPrevClip);
		changed(Field::PrevClipToClip, a_previous.constants.prevClipToClip != a_current.constants.prevClipToClip);
		changed(Field::ConstantsJitterOffset, a_previous.constants.jitterOffset != a_current.constants.jitterOffset);
		changed(Field::MotionVectorScale, a_previous.constants.motionVectorScale != a_current.constants.motionVectorScale);
		changed(Field::CameraPinholeOffset, a_previous.constants.cameraPinholeOffset != a_current.constants.cameraPinholeOffset);
		changed(Field::CameraPosition, a_previous.constants.cameraPosition != a_current.constants.cameraPosition);
		changed(Field::CameraUp, a_previous.constants.cameraUp != a_current.constants.cameraUp);
		changed(Field::CameraRight, a_previous.constants.cameraRight != a_current.constants.cameraRight);
		changed(Field::CameraForward, a_previous.constants.cameraForward != a_current.constants.cameraForward);
		changed(Field::CameraNear, a_previous.constants.cameraNear != a_current.constants.cameraNear);
		changed(Field::CameraFar, a_previous.constants.cameraFar != a_current.constants.cameraFar);
		changed(Field::CameraFOV, a_previous.constants.cameraFOV != a_current.constants.cameraFOV);
		changed(Field::CameraAspectRatio, a_previous.constants.cameraAspectRatio != a_current.constants.cameraAspectRatio);
		changed(Field::MotionVectorsInvalidValue, a_previous.constants.motionVectorsInvalidValue != a_current.constants.motionVectorsInvalidValue);
		changed(Field::DepthInverted, a_previous.constants.depthInverted != a_current.constants.depthInverted);
		changed(Field::CameraMotionIncluded, a_previous.constants.cameraMotionIncluded != a_current.constants.cameraMotionIncluded);
		changed(Field::MotionVectors3D, a_previous.constants.motionVectors3D != a_current.constants.motionVectors3D);
		changed(Field::Reset, a_previous.constants.reset != a_current.constants.reset);
		changed(Field::OrthographicProjection, a_previous.constants.orthographicProjection != a_current.constants.orthographicProjection);
		changed(Field::MotionVectorsDilated, a_previous.constants.motionVectorsDilated != a_current.constants.motionVectorsDilated);
		changed(Field::MotionVectorsJittered, a_previous.constants.motionVectorsJittered != a_current.constants.motionVectorsJittered);
		changed(Field::MinRelativeLinearDepthObjectSeparation, a_previous.constants.minRelativeLinearDepthObjectSeparation != a_current.constants.minRelativeLinearDepthObjectSeparation);
		return mask;
	}

	template <std::size_t N, class T>
	void CopyDLSSDevBenchFloatBits(const T& a_source, std::array<uint32_t, N>& a_target)
	{
		static_assert(sizeof(T) == sizeof(a_target));
		std::memcpy(a_target.data(), &a_source, sizeof(a_source));
	}

	uint32_t GetDLSSDevBenchFloatBits(float a_value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &a_value, sizeof(bits));
		return bits;
	}

	Streamline::DLSSDevBenchTraceSignature BuildDLSSDevBenchTraceSignature(
		const Streamline::DLSSFrameConstantsCache& a_constants,
		const Streamline::DLSSDispatchDiagnostics* a_diagnostics,
		sl::FrameToken* a_frameToken,
		const sl::Constants* a_streamlineConstants)
	{
		Streamline::DLSSDevBenchTraceSignature signature{};
		signature.traceSessionID = GetDLSSDevBenchTraceState().activeSessionID.load(std::memory_order_acquire);
		signature.frame = a_constants.frame;
		signature.frameToken = a_frameToken ? static_cast<uint32_t>(*a_frameToken) : 0u;
		signature.frameTokenAddress = reinterpret_cast<uint64_t>(a_frameToken);
		signature.requestedViewport = a_diagnostics ? static_cast<uint32_t>(a_diagnostics->requestedViewport) : a_constants.viewport;
		signature.resolvedViewport = a_constants.viewport;
		signature.eyeIndex = a_constants.eyeIndex;
		signature.viewportRole = a_constants.viewportRole;
		signature.outputWidth = a_constants.outputWidth;
		signature.outputHeight = a_constants.outputHeight;
		signature.qualityMode = a_constants.qualityMode;
		signature.dlssPreset = a_constants.dlssPreset;
		signature.extentInLeft = a_diagnostics ? a_diagnostics->extentIn.left : 0u;
		signature.extentInTop = a_diagnostics ? a_diagnostics->extentIn.top : 0u;
		signature.extentInWidth = a_constants.extentInWidth;
		signature.extentInHeight = a_constants.extentInHeight;
		signature.extentOutLeft = a_diagnostics ? a_diagnostics->extentOut.left : 0u;
		signature.extentOutTop = a_diagnostics ? a_diagnostics->extentOut.top : 0u;
		signature.extentOutWidth = a_constants.extentOutWidth;
		signature.extentOutHeight = a_constants.extentOutHeight;
		signature.viewportScaleXQ = a_constants.viewportScaleXQ;
		signature.viewportScaleYQ = a_constants.viewportScaleYQ;
		signature.pinholeOffsetXQ = a_constants.pinholeOffsetXQ;
		signature.pinholeOffsetYQ = a_constants.pinholeOffsetYQ;
		signature.jitterXQ = a_constants.jitterXQ;
		signature.jitterYQ = a_constants.jitterYQ;
		signature.historyResetRequested = a_constants.historyResetRequested;
		if (a_diagnostics) {
			signature.colorBuffersHDR = a_diagnostics->colorBuffersHDR;
			signature.submitStageVRDLSS = a_diagnostics->submitStageVRDLSS;
			signature.colorIn = reinterpret_cast<uint64_t>(a_diagnostics->colorIn);
			signature.colorOut = reinterpret_cast<uint64_t>(a_diagnostics->colorOut);
			signature.depth = reinterpret_cast<uint64_t>(a_diagnostics->depth);
			signature.motionVectors = reinterpret_cast<uint64_t>(a_diagnostics->motionVectors);
			signature.reactiveMask = reinterpret_cast<uint64_t>(a_diagnostics->reactiveMask);
			signature.transparencyMask = reinterpret_cast<uint64_t>(a_diagnostics->transparencyMask);
		}
		if (a_streamlineConstants) {
			auto& constants = signature.constants;
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->cameraViewToClip, constants.cameraViewToClip);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->clipToCameraView, constants.clipToCameraView);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->clipToLensClip, constants.clipToLensClip);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->clipToPrevClip, constants.clipToPrevClip);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->prevClipToClip, constants.prevClipToClip);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->jitterOffset, constants.jitterOffset);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->mvecScale, constants.motionVectorScale);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->cameraPinholeOffset, constants.cameraPinholeOffset);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->cameraPos, constants.cameraPosition);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->cameraUp, constants.cameraUp);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->cameraRight, constants.cameraRight);
			CopyDLSSDevBenchFloatBits(a_streamlineConstants->cameraFwd, constants.cameraForward);
			constants.cameraNear = GetDLSSDevBenchFloatBits(a_streamlineConstants->cameraNear);
			constants.cameraFar = GetDLSSDevBenchFloatBits(a_streamlineConstants->cameraFar);
			constants.cameraFOV = GetDLSSDevBenchFloatBits(a_streamlineConstants->cameraFOV);
			constants.cameraAspectRatio = GetDLSSDevBenchFloatBits(a_streamlineConstants->cameraAspectRatio);
			constants.motionVectorsInvalidValue = GetDLSSDevBenchFloatBits(a_streamlineConstants->motionVectorsInvalidValue);
			constants.minRelativeLinearDepthObjectSeparation = GetDLSSDevBenchFloatBits(a_streamlineConstants->minRelativeLinearDepthObjectSeparation);
			constants.depthInverted = static_cast<uint8_t>(a_streamlineConstants->depthInverted);
			constants.cameraMotionIncluded = static_cast<uint8_t>(a_streamlineConstants->cameraMotionIncluded);
			constants.motionVectors3D = static_cast<uint8_t>(a_streamlineConstants->motionVectors3D);
			constants.reset = static_cast<uint8_t>(a_streamlineConstants->reset);
			constants.orthographicProjection = static_cast<uint8_t>(a_streamlineConstants->orthographicProjection);
			constants.motionVectorsDilated = static_cast<uint8_t>(a_streamlineConstants->motionVectorsDilated);
			constants.motionVectorsJittered = static_cast<uint8_t>(a_streamlineConstants->motionVectorsJittered);
		}
		return signature;
	}

	void RecordDLSSDevBenchTrace(
		Streamline::DLSSDevBenchTraceStage a_stage,
		int32_t a_resultCode,
		const Streamline::DLSSDispatchDiagnostics* a_diagnostics,
		const Streamline::DLSSDevBenchTraceSignature& a_current) noexcept
	{
		auto& state = GetDLSSDevBenchTraceState();
		const uint64_t traceSessionID = a_current.traceSessionID;
		if (!traceSessionID || state.activeSessionID.load(std::memory_order_acquire) != traceSessionID)
			return;
		const auto countDroppedRecord = [&]() noexcept {
			if (state.activeSessionID.load(std::memory_order_acquire) == traceSessionID)
				state.droppedRecords.fetch_add(1, std::memory_order_relaxed);
		};

		LARGE_INTEGER timestamp{};
		QueryPerformanceCounter(&timestamp);
		try {
			std::unique_lock lock(state.mutex, std::try_to_lock);
			if (!lock.owns_lock()) {
				countDroppedRecord();
				return;
			}
			if (state.activeSessionID.load(std::memory_order_acquire) != traceSessionID)
				return;

			Streamline::DLSSDevBenchTraceRecord record{};
			auto& current = record.current;
			current.sequence = ++state.totalRecords;
			current.timestampQPC = static_cast<uint64_t>(timestamp.QuadPart);
			current.compositorCycleToken = g_dlssDevBenchCompositorCycleToken;
			current.threadID = GetCurrentThreadId();
			current.resultCode = a_resultCode;
			current.stage = a_stage;
			current.signature = a_current;
			const std::string_view label = a_diagnostics && a_diagnostics->label ? a_diagnostics->label : "DLSS Evaluate";
			const auto labelLength = std::min(label.size(), current.label.size() - 1u);
			std::copy_n(label.data(), labelLength, current.label.data());

			if (auto* previous = FindDLSSDevBenchTraceIdentity(state.acceptedConstants, a_current)) {
				record.previousConstantsFound = true;
				record.previousConstants = previous->call;
				record.constantsChangedFieldMask = BuildDLSSDevBenchChangedFieldMask(previous->call.signature, a_current);
				previous->lastUse = ++state.identityUseCounter;
			}
			if (auto* previous = FindDLSSDevBenchTraceIdentity(state.evaluations, a_current)) {
				record.previousEvaluationFound = true;
				record.previousEvaluation = previous->call;
				record.evaluationChangedFieldMask = BuildDLSSDevBenchChangedFieldMask(previous->call.signature, a_current);
				previous->lastUse = ++state.identityUseCounter;
			}

			if (a_stage == Streamline::DLSSDevBenchTraceStage::ConstantsCacheReuse) {
				++state.constantsCacheReuses;
			} else if (a_stage == Streamline::DLSSDevBenchTraceStage::SetConstants) {
				++state.setConstantsCalls;
				if (a_resultCode == static_cast<int32_t>(sl::Result::eErrorDuplicatedConstants)) {
					++state.duplicatedConstantsFailures;
					state.lastDuplicatedConstantsFailureFound = true;
					state.lastDuplicatedConstantsFailure = record;
				}
			} else if (a_stage == Streamline::DLSSDevBenchTraceStage::Evaluate) {
				++state.evaluateCalls;
				if (a_resultCode != static_cast<int32_t>(sl::Result::eOk)) {
					++state.evaluateFailures;
					state.lastEvaluateFailureFound = true;
					state.lastEvaluateFailure = record;
				}
			}

			if (a_stage == Streamline::DLSSDevBenchTraceStage::SetConstants &&
				a_resultCode == static_cast<int32_t>(sl::Result::eOk)) {
				auto* accepted = FindDLSSDevBenchTraceIdentity(state.acceptedConstants, a_current);
				if (!accepted)
					accepted = &SelectDLSSDevBenchTraceIdentitySlot(state.acceptedConstants);
				accepted->valid = true;
				accepted->lastUse = ++state.identityUseCounter;
				accepted->call = current;
			}
			if (a_stage == Streamline::DLSSDevBenchTraceStage::Evaluate) {
				auto* evaluation = FindDLSSDevBenchTraceIdentity(state.evaluations, a_current);
				if (!evaluation)
					evaluation = &SelectDLSSDevBenchTraceIdentitySlot(state.evaluations);
				evaluation->valid = true;
				evaluation->lastUse = ++state.identityUseCounter;
				evaluation->call = current;
			}

			state.records[state.recordWriteIndex] = record;
			state.recordWriteIndex = (state.recordWriteIndex + 1u) % state.records.size();
			state.recordCount = std::min(state.recordCount + 1u, state.records.size());
		} catch (...) {
			countDroppedRecord();
		}
	}
#endif

	bool ShouldEmitDLSSDiagnostic(
		DLSSDiagnosticStage a_stage,
		const Streamline::DLSSDispatchDiagnostics* a_diagnostics,
		int32_t a_resultCode,
		std::string_view a_resultLabel)
	{
		if (!a_diagnostics)
			return false;

		struct ThrottleState
		{
			bool valid = false;
			uint32_t count = 0;
			uint32_t lastFrame = 0;
			uint32_t requestedViewport = 0;
			uint32_t resolvedViewport = 0;
			uint32_t outputWidth = 0;
			uint32_t outputHeight = 0;
			uint32_t qualityMode = 0;
			uint32_t dlssPreset = 0;
			uint32_t viewportRole = 0;
			uint32_t extentInWidth = 0;
			uint32_t extentInHeight = 0;
			uint32_t extentOutWidth = 0;
			uint32_t extentOutHeight = 0;
			int32_t viewportScaleXQ = 0;
			int32_t viewportScaleYQ = 0;
			int32_t pinholeOffsetXQ = 0;
			int32_t pinholeOffsetYQ = 0;
			bool croppedViewport = false;
			int32_t resultCode = 0;
			std::string resultLabel;
			std::string label;
		};

		static std::array<ThrottleState, static_cast<size_t>(DLSSDiagnosticStage::Count) * 2> throttle{};
		const uint32_t boundedEye = globals::game::isVR ? std::min(a_diagnostics->eyeIndex, 1u) : 0u;
		const size_t index = static_cast<size_t>(a_stage) * 2u + boundedEye;
		auto& state = throttle[index];
		const char* label = a_diagnostics->label ? a_diagnostics->label : "DLSS Evaluate";
		const int32_t viewportScaleXQ = QuantizeDLSSDiagnosticFloat(a_diagnostics->viewportScaleX);
		const int32_t viewportScaleYQ = QuantizeDLSSDiagnosticFloat(a_diagnostics->viewportScaleY);
		const int32_t pinholeOffsetXQ = QuantizeDLSSDiagnosticFloat(a_diagnostics->pinholeOffsetX);
		const int32_t pinholeOffsetYQ = QuantizeDLSSDiagnosticFloat(a_diagnostics->pinholeOffsetY);
		const bool signatureChanged =
			!state.valid ||
			state.requestedViewport != static_cast<uint32_t>(a_diagnostics->requestedViewport) ||
			state.resolvedViewport != static_cast<uint32_t>(a_diagnostics->resolvedViewport) ||
			state.outputWidth != a_diagnostics->outputWidth ||
			state.outputHeight != a_diagnostics->outputHeight ||
			state.qualityMode != a_diagnostics->qualityMode ||
			state.dlssPreset != a_diagnostics->dlssPreset ||
			state.viewportRole != static_cast<uint32_t>(a_diagnostics->viewportRole) ||
			state.extentInWidth != a_diagnostics->extentIn.width ||
			state.extentInHeight != a_diagnostics->extentIn.height ||
			state.extentOutWidth != a_diagnostics->extentOut.width ||
			state.extentOutHeight != a_diagnostics->extentOut.height ||
			state.viewportScaleXQ != viewportScaleXQ ||
			state.viewportScaleYQ != viewportScaleYQ ||
			state.pinholeOffsetXQ != pinholeOffsetXQ ||
			state.pinholeOffsetYQ != pinholeOffsetYQ ||
			state.croppedViewport != a_diagnostics->croppedViewport ||
			state.resultCode != a_resultCode ||
			state.resultLabel != a_resultLabel ||
			state.label != label;

		if (signatureChanged) {
			state = {};
			state.valid = true;
			state.requestedViewport = static_cast<uint32_t>(a_diagnostics->requestedViewport);
			state.resolvedViewport = static_cast<uint32_t>(a_diagnostics->resolvedViewport);
			state.outputWidth = a_diagnostics->outputWidth;
			state.outputHeight = a_diagnostics->outputHeight;
			state.qualityMode = a_diagnostics->qualityMode;
			state.dlssPreset = a_diagnostics->dlssPreset;
			state.viewportRole = static_cast<uint32_t>(a_diagnostics->viewportRole);
			state.extentInWidth = a_diagnostics->extentIn.width;
			state.extentInHeight = a_diagnostics->extentIn.height;
			state.extentOutWidth = a_diagnostics->extentOut.width;
			state.extentOutHeight = a_diagnostics->extentOut.height;
			state.viewportScaleXQ = viewportScaleXQ;
			state.viewportScaleYQ = viewportScaleYQ;
			state.pinholeOffsetXQ = pinholeOffsetXQ;
			state.pinholeOffsetYQ = pinholeOffsetYQ;
			state.croppedViewport = a_diagnostics->croppedViewport;
			state.resultCode = a_resultCode;
			state.resultLabel = a_resultLabel;
			state.label = label;
		}

		const uint32_t frame = a_diagnostics->frame;
		const bool emit =
			signatureChanged ||
			state.count < kDLSSDiagnosticMaxInitialLogs ||
			(frame != 0 && state.lastFrame != 0 && frame - state.lastFrame >= kDLSSDiagnosticRepeatFrameGap);

		++state.count;
		if (emit)
			state.lastFrame = frame;

		return emit;
	}

	void LogDLSSDispatchDiagnostics(
		DLSSDiagnosticStage a_stage,
		int32_t a_resultCode,
		std::string_view a_resultLabel,
		const Streamline::DLSSDispatchDiagnostics* a_diagnostics)
	{
		if (!a_diagnostics)
			return;

		if (!ShouldLogDLSSDiagnostics())
			return;

		if (!ShouldEmitDLSSDiagnostic(a_stage, a_diagnostics, a_resultCode, a_resultLabel))
			return;

		const auto& upscaling = globals::features::upscaling;
		const auto& plan = upscaling.GetRuntimeResolutionPlan();
		const char* label = a_diagnostics->label ? a_diagnostics->label : "DLSS Evaluate";
		const auto* frameToken = a_diagnostics->frameToken;
		const uint32_t frame = a_diagnostics->frame;
		const std::string result = FormatDLSSDiagnosticResult(a_resultCode, a_resultLabel);

		logger::debug(
			"[Streamline][DLSSDiag] stage={} result={} label='{}' frame={} eye={} role={} requestedViewport={} resolvedViewport={} frameToken=0x{:X} quality={} preset={} hdr={} output={}x{} extentIn=[{}] extentOut=[{}] viewportScale={:.6f}x{:.6f} croppedViewport={} pinhole={:.6f},{:.6f} jitter={:.6f},{:.6f} historyReset={} submitStageVR={} presentationActive={} renderScaleActive={} foveatedConfigured={} peripheryTAAConfigured={} optionsCache(valid={} viewport={} output={}x{} quality={} preset={} hdr={} legacy={}) plan(owner={} method={} quality={} display={}x{} render={}x{} final={}x{} foveated={} peripheryTAA={} menu={} knownMenu={} loading={})",
			GetDLSSDiagnosticStageName(a_stage),
			result,
			label,
			frame,
			a_diagnostics->eyeIndex,
			magic_enum::enum_name(a_diagnostics->viewportRole),
			static_cast<uint32_t>(a_diagnostics->requestedViewport),
			static_cast<uint32_t>(a_diagnostics->resolvedViewport),
			reinterpret_cast<std::uintptr_t>(frameToken),
			a_diagnostics->qualityMode,
			a_diagnostics->dlssPreset,
			a_diagnostics->colorBuffersHDR,
			a_diagnostics->outputWidth,
			a_diagnostics->outputHeight,
			FormatExtent(a_diagnostics->extentIn),
			FormatExtent(a_diagnostics->extentOut),
			a_diagnostics->viewportScaleX,
			a_diagnostics->viewportScaleY,
			a_diagnostics->croppedViewport,
			a_diagnostics->pinholeOffsetX,
			a_diagnostics->pinholeOffsetY,
			a_diagnostics->jitterX,
			a_diagnostics->jitterY,
			a_diagnostics->historyResetRequested,
			a_diagnostics->submitStageVRDLSS,
			a_diagnostics->presentationUpscalingActive,
			a_diagnostics->renderScaleActive,
			a_diagnostics->foveatedDispatchEnabled,
			a_diagnostics->peripheryTAAEnabled,
			a_diagnostics->optionsCacheValid,
			a_diagnostics->optionsCacheViewport,
			a_diagnostics->optionsCacheOutputWidth,
			a_diagnostics->optionsCacheOutputHeight,
			a_diagnostics->optionsCacheQualityMode,
			a_diagnostics->optionsCacheDLSSPreset,
			a_diagnostics->optionsCacheHDR,
			a_diagnostics->optionsCacheLegacyProfile,
			magic_enum::enum_name(plan.owner),
			magic_enum::enum_name(plan.upscaleMethod),
			plan.qualityMode,
			static_cast<uint32_t>(plan.trueHMDDisplaySize.x),
			static_cast<uint32_t>(plan.trueHMDDisplaySize.y),
			static_cast<uint32_t>(plan.engineRenderSize.x),
			static_cast<uint32_t>(plan.engineRenderSize.y),
			static_cast<uint32_t>(plan.finalOutputSize.x),
			static_cast<uint32_t>(plan.finalOutputSize.y),
			plan.foveatedActive,
			plan.peripheryTAAActive,
			plan.menuContextActive,
			plan.knownMenuContextActive,
			plan.loadingMenuActive);

		logger::debug(
			"[Streamline][DLSSDiag] resources label='{}' frame={} eye={} colorIn=[{}] colorOut=[{}] depth=[{}] mvec=[{}] reactive=[{}] transparency=[{}]",
			label,
			frame,
			a_diagnostics->eyeIndex,
			DescribeTextureResource(a_diagnostics->colorIn),
			DescribeTextureResource(a_diagnostics->colorOut),
			DescribeTextureResource(a_diagnostics->depth),
			DescribeTextureResource(a_diagnostics->motionVectors),
			DescribeTextureResource(a_diagnostics->reactiveMask),
			DescribeTextureResource(a_diagnostics->transparencyMask));
	}

	void LogDLSSDispatchDiagnostics(DLSSDiagnosticStage a_stage, sl::Result a_result, const Streamline::DLSSDispatchDiagnostics* a_diagnostics)
	{
		const auto resultLabel = magic_enum::enum_name(a_result);
		LogDLSSDispatchDiagnostics(a_stage, static_cast<int32_t>(a_result), resultLabel, a_diagnostics);
	}

	void LogDLSSDispatchDiagnostics(DLSSDiagnosticStage a_stage, const char* a_result, const Streamline::DLSSDispatchDiagnostics* a_diagnostics)
	{
		LogDLSSDispatchDiagnostics(
			a_stage,
			kDLSSDiagnosticTextResultCode,
			a_result ? std::string_view(a_result) : std::string_view(),
			a_diagnostics);
	}

	D3D11IdleFenceResult BeginOrPollD3D11IdleFence(ID3D11DeviceContext* a_context, ID3D11Query*& a_query, const char* a_reason)
	{
		if (!a_context) {
			ReleaseD3D11IdleFence(a_query);
			return D3D11IdleFenceResult::Ready;
		}

		const auto pollFence = [&]() {
			BOOL completed = FALSE;
			const HRESULT dataResult = a_context->GetData(a_query, &completed, sizeof(completed), 0);
			if (dataResult == S_OK && completed) {
				ReleaseD3D11IdleFence(a_query);
				return D3D11IdleFenceResult::Ready;
			}

			if (dataResult == S_FALSE || dataResult == S_OK)
				return D3D11IdleFenceResult::Pending;

			logger::debug("[Streamline] D3D11 idle fence poll failed before {}: 0x{:08X}", a_reason, static_cast<uint32_t>(dataResult));
			ReleaseD3D11IdleFence(a_query);
			return D3D11IdleFenceResult::Failed;
		};

		if (a_query)
			return pollFence();

		ID3D11Device* device = nullptr;
		a_context->GetDevice(&device);
		if (!device) {
			a_context->Flush();
			return D3D11IdleFenceResult::Ready;
		}

		D3D11_QUERY_DESC queryDesc{};
		queryDesc.Query = D3D11_QUERY_EVENT;

		const HRESULT createResult = device->CreateQuery(&queryDesc, &a_query);
		device->Release();

		if (FAILED(createResult) || !a_query) {
			a_context->Flush();
			logger::debug("[Streamline] D3D11 idle fence creation failed before {}: 0x{:08X}", a_reason, static_cast<uint32_t>(createResult));
			return D3D11IdleFenceResult::Failed;
		}

		a_context->End(a_query);
		a_context->Flush();
		return pollFence();
	}

}

Streamline::~Streamline()
{
	Shutdown();
}

#ifdef DEVBENCH_BRIDGE_ENABLED
bool Streamline::StartDLSSDevBenchTrace()
{
	auto& state = GetDLSSDevBenchTraceState();
	std::scoped_lock lock(state.mutex);
	if (state.activeSessionID.load(std::memory_order_acquire))
		return false;

	ClearDLSSDevBenchTraceLocked(state);
	state.sessionID = state.nextSessionID++;
	state.activeSessionID.store(state.sessionID, std::memory_order_release);
	return true;
}

bool Streamline::StopDLSSDevBenchTrace(uint64_t a_expectedSessionID)
{
	auto& state = GetDLSSDevBenchTraceState();
	std::scoped_lock lock(state.mutex);
	const uint64_t activeSessionID =
		state.activeSessionID.load(std::memory_order_acquire);
	if (!activeSessionID ||
		(a_expectedSessionID && activeSessionID != a_expectedSessionID)) {
		return false;
	}
	state.activeSessionID.store(0, std::memory_order_release);
	return true;
}

bool Streamline::ResetDLSSDevBenchTrace()
{
	auto& state = GetDLSSDevBenchTraceState();
	if (state.activeSessionID.load(std::memory_order_acquire))
		return false;

	std::scoped_lock lock(state.mutex);
	if (state.activeSessionID.load(std::memory_order_acquire))
		return false;

	ClearDLSSDevBenchTraceLocked(state);
	state.sessionID = 0;
	return true;
}

bool Streamline::IsDLSSDevBenchTraceActive() const noexcept
{
	return GetDLSSDevBenchTraceState().activeSessionID.load(std::memory_order_acquire) != 0;
}

Streamline::DLSSDevBenchTraceSnapshot Streamline::GetDLSSDevBenchTraceSnapshot(bool a_includeRecords) const
{
	auto& state = GetDLSSDevBenchTraceState();
	std::scoped_lock lock(state.mutex);

	DLSSDevBenchTraceSnapshot snapshot{};
	snapshot.active = state.activeSessionID.load(std::memory_order_acquire) != 0;
	snapshot.sessionID = state.sessionID;
	snapshot.timestampQPCFrequency = state.qpcFrequency;
	snapshot.retainedRecords = state.recordCount;
	snapshot.totalRecords = state.totalRecords;
	snapshot.overwrittenRecords = state.totalRecords - state.recordCount;
	snapshot.droppedRecords = state.droppedRecords.load(std::memory_order_acquire);
	snapshot.constantsCacheReuses = state.constantsCacheReuses;
	snapshot.setConstantsCalls = state.setConstantsCalls;
	snapshot.evaluateCalls = state.evaluateCalls;
	snapshot.duplicatedConstantsFailures = state.duplicatedConstantsFailures;
	snapshot.evaluateFailures = state.evaluateFailures;
	snapshot.lastDuplicatedConstantsFailureFound = state.lastDuplicatedConstantsFailureFound;
	snapshot.lastEvaluateFailureFound = state.lastEvaluateFailureFound;
	if (state.lastDuplicatedConstantsFailureFound) {
		snapshot.lastDuplicatedConstantsFailureSequence =
			state.lastDuplicatedConstantsFailure.current.sequence;
	}
	if (state.lastEvaluateFailureFound)
		snapshot.lastEvaluateFailureSequence = state.lastEvaluateFailure.current.sequence;
	if (a_includeRecords) {
		if (state.lastDuplicatedConstantsFailureFound)
			snapshot.lastDuplicatedConstantsFailure = state.lastDuplicatedConstantsFailure;
		if (state.lastEvaluateFailureFound)
			snapshot.lastEvaluateFailure = state.lastEvaluateFailure;
		snapshot.records.reserve(state.recordCount);
		const std::size_t firstIndex = state.recordCount == state.records.size() ? state.recordWriteIndex : 0u;
		for (std::size_t offset = 0; offset < state.recordCount; ++offset) {
			snapshot.records.push_back(state.records[(firstIndex + offset) % state.records.size()]);
		}
	}
	return snapshot;
}

uint64_t Streamline::SetDLSSDevBenchCompositorCycleContext(uint64_t a_compositorCycleToken) noexcept
{
	const uint64_t previous = g_dlssDevBenchCompositorCycleToken;
	g_dlssDevBenchCompositorCycleToken = a_compositorCycleToken;
	return previous;
}
#endif

void LoggingCallback(sl::LogType type, const char* msg)
{
	// Remove trailing newlines from the raw message
	std::string rawMsg(msg);
	while (!rawMsg.empty() && (rawMsg.back() == '\n' || rawMsg.back() == '\r'))
		rawMsg.pop_back();

	// Remove leading bracketed metadata
	const char* p = msg;
	while (*p == '[') {
		const char* close = strchr(p, ']');
		if (!close)
			break;
		p = close + 1;
		// Skip whitespace after each bracketed section
		while (*p == ' ' || *p == '\t') ++p;
	}
	// Now p points to the first non-bracketed section (file/line info or message)
	std::string cleanMsg(p);
	// Trim leading/trailing whitespace and newlines
	size_t start = cleanMsg.find_first_not_of(" \t\r\n");
	size_t end = cleanMsg.find_last_not_of(" \t\r\n");
	if (start != std::string::npos && end != std::string::npos)
		cleanMsg = cleanMsg.substr(start, end - start + 1);
	else
		cleanMsg.clear();

	// If the cleaned message is empty or only bracketed tokens, log the raw message
	bool onlyBrackets = true;
	for (char c : cleanMsg) {
		if (c != '[' && c != ']' && c != ' ' && c != '\t') {
			onlyBrackets = false;
			break;
		}
	}
	if (cleanMsg.empty() || onlyBrackets) {
		logger::info("[StreamlineSDK:RAW] {}", rawMsg);
		return;
	}

	// Use a clear prefix
	const char* prefix = "[StreamlineSDK]";
	switch (type) {
	case sl::LogType::eInfo:
		logger::info("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eWarn:
		logger::warn("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eError:
		logger::error("{} {}", prefix, cleanMsg);
		break;
	}
}

std::vector<std::pair<std::string, std::string>> Streamline::dllVersions = {};

bool Streamline::LoadInterposer()
{
	std::scoped_lock lifecycleLock(lifecycleMutex);
	if (lifecycleState.load(std::memory_order_acquire) == LifecycleState::Initialized)
		return true;
	if (triedInitialization.exchange(true, std::memory_order_acq_rel))
		return false;

	lifecycleState.store(LifecycleState::Initializing, std::memory_order_release);
	featureCheckComplete = false;
	initialized = false;
	featureDLSS = false;
	featureReflex = false;
	featurePCL = false;
	reflexSupportedOnCurrentAdapter = false;
	adapterSupportsDLSS = false;
	adapterSupportsReflex = false;
	adapterSupportsPCL = false;
	boundDeviceIdentity = nullptr;
	frameGenerationQuarantinedByReflex = false;

	const std::filesystem::path pluginDir = std::filesystem::path(Streamline::PluginDir);
	std::error_code pluginPathError;
	auto pluginDirAbsolute = std::filesystem::absolute(pluginDir, pluginPathError);
	if (pluginPathError)
		pluginDirAbsolute = pluginDir;
	if (!ValidateStreamlineRuntime(pluginDirAbsolute)) {
		featureCheckComplete = true;
		lifecycleState.store(LifecycleState::Unavailable, std::memory_order_release);
		return false;
	}
	const std::filesystem::path interposerPath = pluginDirAbsolute / L"sl.interposer.dll";
	EnsureStreamlineDllDirectory(pluginDirAbsolute);
	DWORD errorCode = ERROR_SUCCESS;
	HMODULE candidateInterposer = LoadStreamlineDll(interposerPath, errorCode);
	if (candidateInterposer == nullptr) {
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		featureCheckComplete = true;
		ReleaseStreamlineDllDirectory();
		lifecycleState.store(LifecycleState::Unavailable, std::memory_order_release);
		return false;
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(candidateInterposer));
	}

	// Dynamically log all DLL versions in the Streamline plugin directory
	Streamline::dllVersions = Util::EnumerateDllVersions(pluginDirAbsolute);
	for (const auto& [name, versionStr] : Streamline::dllVersions)
		logger::info("[Streamline] {} version: {}", name, versionStr);

	logger::info("[Streamline] Initializing Streamline");

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL };

	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	// Set log level from settings
	switch (globals::features::upscaling.settings.streamlineLogLevel) {
	case 2:
		pref.logLevel = sl::LogLevel::eVerbose;
		break;
	case 1:
		pref.logLevel = sl::LogLevel::eDefault;
		break;
	case 0:
	default:
		pref.logLevel = sl::LogLevel::eOff;
		break;
	}
	pref.logMessageCallback = LoggingCallback;
	pref.showConsole = false;
	static std::wstring pluginDirAbsoluteW;
	pluginDirAbsoluteW = pluginDirAbsolute.wstring();
	static const wchar_t* pluginPaths[1]{};
	pluginPaths[0] = pluginDirAbsoluteW.c_str();
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;
	logger::info("[Streamline] Plugin search path: {}", pluginDirAbsolute.string());

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = sl::RenderAPI::eD3D11;
	pref.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	const auto bindings = BindStreamlineCore(candidateInterposer);
	const auto missing = bindings.MissingRequired();
	if (!missing.empty()) {
		std::string missingList;
		for (const auto name : missing) {
			if (!missingList.empty())
				missingList += ", ";
			missingList += name;
		}
		logger::critical("[Streamline] Interposer is missing required exports: {}", missingList);
		FreeLibrary(candidateInterposer);
		ReleaseStreamlineDllDirectory();
		featureCheckComplete = true;
		lifecycleState.store(LifecycleState::Unavailable, std::memory_order_release);
		return false;
	}

	if (SL_FAILED(res, bindings.init(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline: {}", magic_enum::enum_name(res));
		FreeLibrary(candidateInterposer);
		ReleaseStreamlineDllDirectory();
		featureCheckComplete = true;
		lifecycleState.store(LifecycleState::Unavailable, std::memory_order_release);
		return false;
	}

	interposer = candidateInterposer;
	slInit = bindings.init;
	slShutdown = bindings.shutdown;
	slIsFeatureSupported = bindings.isFeatureSupported;
	slIsFeatureLoaded = bindings.isFeatureLoaded;
	slSetFeatureLoaded = bindings.setFeatureLoaded;
	slEvaluateFeature = bindings.evaluateFeature;
	slAllocateResources = bindings.allocateResources;
	slFreeResources = bindings.freeResources;
	slGetFeatureRequirements = bindings.getFeatureRequirements;
	slGetFeatureVersion = bindings.getFeatureVersion;
	slUpgradeInterface = bindings.upgradeInterface;
	slSetConstants = bindings.setConstants;
	slGetNativeInterface = bindings.getNativeInterface;
	slGetFeatureFunction = bindings.getFeatureFunction;
	slGetNewFrameToken = bindings.getNewFrameToken;
	slSetD3DDevice = bindings.setD3DDevice;
	initialized = true;
	InvalidateDLSSOptionsCache();
	reflexOptionsCache = {};
	lastReflexSleepFrame = UINT32_MAX;
	lifecycleState.store(LifecycleState::Initialized, std::memory_order_release);
	logger::info("[Streamline] Successfully initialized Streamline");
	return true;
}

void Streamline::Shutdown()
{
	std::scoped_lock lifecycleLock(lifecycleMutex);
	const auto state = lifecycleState.load(std::memory_order_acquire);
	if (state == LifecycleState::Uninitialized || state == LifecycleState::ShuttingDown)
		return;

	lifecycleState.store(LifecycleState::ShuttingDown, std::memory_order_release);
	ResetDLSSIdleFences();
	if (initialized && slShutdown) {
		const sl::Result shutdownResult = slShutdown();
		if (shutdownResult != sl::Result::eOk) {
			logger::warn("[Streamline] Shutdown returned {}", magic_enum::enum_name(shutdownResult));
		}
	}

	initialized = false;
	featureDLSS = false;
	featureReflex = false;
	featurePCL = false;
	reflexSupportedOnCurrentAdapter = false;
	adapterSupportsDLSS = false;
	adapterSupportsReflex = false;
	adapterSupportsPCL = false;
	boundDeviceIdentity = nullptr;
	frameGenerationQuarantinedByReflex = false;
	slDLSSGetOptimalSettings = nullptr;
	slDLSSGetState = nullptr;
	slDLSSSetOptions = nullptr;
	slReflexGetState = nullptr;
	slReflexSleep = nullptr;
	slReflexSetOptions = nullptr;
	slPCLSetMarker = nullptr;
	slInit = nullptr;
	slShutdown = nullptr;
	slIsFeatureSupported = nullptr;
	slIsFeatureLoaded = nullptr;
	slSetFeatureLoaded = nullptr;
	slEvaluateFeature = nullptr;
	slAllocateResources = nullptr;
	slFreeResources = nullptr;
	slGetFeatureRequirements = nullptr;
	slGetFeatureVersion = nullptr;
	slUpgradeInterface = nullptr;
	slSetConstants = nullptr;
	slGetNativeInterface = nullptr;
	slGetFeatureFunction = nullptr;
	slGetNewFrameToken = nullptr;
	slSetD3DDevice = nullptr;
	if (interposer) {
		FreeLibrary(interposer);
		interposer = nullptr;
	}
	ReleaseStreamlineDllDirectory();
	featureCheckComplete = true;
	lifecycleState.store(LifecycleState::Uninitialized, std::memory_order_release);
}

bool Streamline::TryUpgradeInterface(void** a_interface)
{
	if (!initialized || !slUpgradeInterface || !a_interface || !*a_interface)
		return false;

	void* candidate = *a_interface;
	const sl::Result result = slUpgradeInterface(&candidate);
	if (result != sl::Result::eOk || !candidate) {
		logger::error("[Streamline] Interface upgrade failed: {}", magic_enum::enum_name(result));
		featureDLSS = false;
		featureReflex = false;
		featurePCL = false;
		featureCheckComplete = true;
		return false;
	}
	*a_interface = candidate;
	return true;
}

bool Streamline::TrySetD3DDevice(ID3D11Device* a_device)
{
	if (!initialized || !slSetD3DDevice || !a_device)
		return false;
	if (boundDeviceIdentity == a_device)
		return true;

	const sl::Result result = slSetD3DDevice(a_device);
	if (result != sl::Result::eOk) {
		logger::error("[Streamline] D3D device binding failed: {}", magic_enum::enum_name(result));
		featureDLSS = false;
		featureReflex = false;
		featurePCL = false;
		featureCheckComplete = true;
		return false;
	}

	boundDeviceIdentity = a_device;
	featureCheckComplete = false;
	InvalidateDLSSOptionsCache();
	ResetFrameTracking();
	return true;
}

void Streamline::MarkAdapterUnavailable(const char* a_reason)
{
	adapterSupportsDLSS = false;
	adapterSupportsReflex = false;
	adapterSupportsPCL = false;
	featureDLSS = false;
	featureReflex = false;
	featurePCL = false;
	reflexSupportedOnCurrentAdapter = false;
	featureCheckComplete = true;
	if (a_reason && *a_reason)
		logger::info("[Streamline] NVIDIA features unavailable: {}", a_reason);
}

bool Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
{
	featureCheckComplete = false;
	adapterSupportsDLSS = false;
	adapterSupportsReflex = false;
	adapterSupportsPCL = false;
	featureDLSS = false;
	featureReflex = false;
	featurePCL = false;
	if (!initialized || !a_adapter) {
		MarkAdapterUnavailable("no initialized runtime or resolved adapter");
		return false;
	}
	logger::info("[Streamline] Checking features");
	DXGI_ADAPTER_DESC adapterDesc{};
	if (FAILED(a_adapter->GetDesc(&adapterDesc))) {
		MarkAdapterUnavailable("adapter description query failed");
		return false;
	}
	reflexSupportedOnCurrentAdapter = adapterDesc.VendorId == NVIDIA_VENDOR_ID;
	if (!reflexSupportedOnCurrentAdapter) {
		MarkAdapterUnavailable("resolved adapter is not NVIDIA");
		return false;
	}

	sl::AdapterInfo adapterInfo;
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	auto checkFeatureAvailability = [&](sl::Feature feature, const char* featureName, bool& outAvailable) {
		outAvailable = false;
		bool loaded = false;
		if (SL_FAILED(result, slIsFeatureLoaded(feature, loaded))) {
			logger::warn("[Streamline] {} load-state query failed: {}", featureName, magic_enum::enum_name(result));
			return;
		}
		if (!loaded) {
			logger::info("[Streamline] {} feature is not loaded", featureName);
			sl::FeatureRequirements featureRequirements;
			sl::Result requirementsResult = slGetFeatureRequirements(feature, featureRequirements);
			if (requirementsResult != sl::Result::eOk) {
				logger::info("[Streamline] {} feature failed to load due to: {}", featureName, magic_enum::enum_name(requirementsResult));
			}
			return;
		}

		logger::info("[Streamline] {} feature is loaded", featureName);
		outAvailable = slIsFeatureSupported(feature, adapterInfo) == sl::Result::eOk;
	};

	checkFeatureAvailability(sl::kFeatureDLSS, "DLSS", adapterSupportsDLSS);
	if (reflexSupportedOnCurrentAdapter) {
		checkFeatureAvailability(sl::kFeatureReflex, "Reflex", adapterSupportsReflex);
		checkFeatureAvailability(sl::kFeaturePCL, "PCL", adapterSupportsPCL);
	}

	if (adapterSupportsDLSS) {
		isRTXBelow40series = IsRTXAndBelow40Series(adapterDesc);

		if (isRTXBelow40series)
			logger::info("[Streamline] Older RTX GPU detected, DLSS 4.0 will be used instead of DLSS 4.5");
		else
			logger::info("[Streamline] Newer RTX GPU detected, DLSS 4.5 will be used instead of DLSS 4.0");
	}

	logger::info("[Streamline] DLSS {} supported by the adapter", adapterSupportsDLSS ? "is" : "is not");
	if (reflexSupportedOnCurrentAdapter) {
		logger::info("[Streamline] Reflex {} supported by the adapter", adapterSupportsReflex ? "is" : "is not");
		logger::info("[Streamline] PCL {} supported by the adapter", adapterSupportsPCL ? "is" : "is not");
	} else {
		logger::info("[Streamline] Reflex/PCL disabled on non-NVIDIA adapter");
	}
	InvalidateDLSSOptionsCache();
	reflexOptionsCache = {};
	lastReflexSleepFrame = UINT32_MAX;
	return true;
}

bool Streamline::PostDevice()
{
	slDLSSGetOptimalSettings = nullptr;
	slDLSSGetState = nullptr;
	slDLSSSetOptions = nullptr;
	slReflexGetState = nullptr;
	slReflexSleep = nullptr;
	slReflexSetOptions = nullptr;
	slPCLSetMarker = nullptr;
	featureDLSS = false;
	featureReflex = false;
	featurePCL = false;
	if (!initialized || !slGetFeatureFunction || !boundDeviceIdentity) {
		featureCheckComplete = true;
		logger::error("[Streamline] Feature binding skipped because core activation is incomplete");
		return false;
	}

	const auto bindFeatureFn = [&](sl::Feature feature, const char* functionName, void*& fn) {
		fn = nullptr;
		const sl::Result bindResult = slGetFeatureFunction(feature, functionName, fn);
		if (bindResult != sl::Result::eOk || !fn) {
			logger::warn("[Streamline] {} bind failed with {}", functionName, magic_enum::enum_name(bindResult));
			return false;
		}
		return true;
	};

	if (adapterSupportsDLSS) {
		bool dlssFunctionsBound = true;
		dlssFunctionsBound &= bindFeatureFn(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		dlssFunctionsBound &= bindFeatureFn(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		dlssFunctionsBound &= bindFeatureFn(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
		featureDLSS = dlssFunctionsBound;
		if (!featureDLSS)
			logger::error("[Streamline] DLSS support was reported but its required interface is incomplete");
	}

	if (reflexSupportedOnCurrentAdapter) {
		if (slSetFeatureLoaded) {
			const auto requestFeatureLoad = [&](sl::Feature feature, const char* featureName) {
				const sl::Result loadResult = slSetFeatureLoaded(feature, true);
				if (loadResult != sl::Result::eOk)
					logger::warn("[Streamline] Failed to request {} load: {}", featureName, magic_enum::enum_name(loadResult));
			};

			if (adapterSupportsReflex)
				requestFeatureLoad(sl::kFeatureReflex, "Reflex");
			if (adapterSupportsPCL)
				requestFeatureLoad(sl::kFeaturePCL, "PCL");
		}

		if (adapterSupportsReflex) {
			bool reflexFunctionsBound = true;
			reflexFunctionsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexGetState", (void*&)slReflexGetState);
			reflexFunctionsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
			reflexFunctionsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
			featureReflex = reflexFunctionsBound;
		}

		if (adapterSupportsReflex && !featureReflex) {
			logger::warn("[Streamline] Reflex functions are missing; Reflex runtime controls will be disabled");
		} else if (featureReflex) {
			logger::info("[Streamline] Reflex runtime controls are available");
		} else {
			logger::info("[Streamline] Reflex is not supported by the current adapter");
		}

		if (adapterSupportsPCL)
			featurePCL = bindFeatureFn(sl::kFeaturePCL, "slPCLSetMarker", (void*&)slPCLSetMarker);
		if (adapterSupportsPCL && !featurePCL) {
			logger::warn("[Streamline] PCL marker function is unavailable; marker optimization requests will be ignored");
		} else if (featurePCL) {
			logger::info("[Streamline] PCL marker interface is available");
		} else {
			logger::info("[Streamline] PCL is not supported by the current adapter");
		}
	} else if (!reflexSupportedOnCurrentAdapter) {
		logger::info("[Streamline] Skipping Reflex/PCL binding on non-NVIDIA adapter");
	}

	InvalidateDLSSOptionsCache();
	reflexOptionsCache = {};
	lastReflexSleepFrame = UINT32_MAX;
	featureCheckComplete = true;
	return !adapterSupportsDLSS || featureDLSS;
}

/**
 * @brief Resolves one coherently published Streamline token for a logical frame.
 */
std::optional<Streamline::FrameTokenSnapshot> Streamline::AcquireFrameToken(
	uint32_t a_frame,
	const char* a_consumer)
{
	if (!initialized || !slGetNewFrameToken || !globals::state)
		return std::nullopt;

	return frameTokenCoordinator.Resolve(
		a_frame,
		[this, a_consumer](uint32_t a_requestedFrame)
			-> std::optional<sl::FrameToken*> {
			sl::FrameToken* acquiredToken = nullptr;
			const uint32_t requestedFrame = a_requestedFrame;
			if (SL_FAILED(result, slGetNewFrameToken(acquiredToken, &requestedFrame))) {
				logger::error(
					"[Streamline] Could not get {} frame token for frame {}: {}",
					a_consumer ? a_consumer : "unknown",
					a_requestedFrame,
					magic_enum::enum_name(result));
				return std::nullopt;
			}

			if (!acquiredToken || static_cast<uint32_t>(*acquiredToken) != a_requestedFrame) {
				logger::error(
					"[Streamline] Rejected incoherent {} frame token for frame {}: token={}",
					a_consumer ? a_consumer : "unknown",
					a_requestedFrame,
					acquiredToken ? static_cast<uint32_t>(*acquiredToken) : 0u);
				return std::nullopt;
			}

			return acquiredToken;
		});
}

bool Streamline::CheckFrameConstants(sl::ViewportHandle p_viewport, sl::FrameToken* frameToken, uint32_t eyeIndex, float viewportScaleX, float viewportScaleY, float pinholeOffsetX, float pinholeOffsetY, const DLSSDispatchDiagnostics* diagnostics
#ifdef DEVBENCH_BRIDGE_ENABLED
	,
	DLSSDevBenchTraceSignature* outFrameConstantsSignature
#endif
)
{
	if (!globals::features::upscaling.streamline.initialized)
		return false;

	if (!frameToken) {
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::FrameToken, "unavailable", diagnostics);
		return false;
	}

	// In VR, we need to set constants for each viewport/eye separately
	// In non-VR, this is called once per frame
	auto state = globals::state;
	auto& upscaling = globals::features::upscaling;
	if (!state)
		return false;
	bool applyCroppedConstantsCorrection = false;
	float clampedViewportScaleX = std::clamp(viewportScaleX, 1e-4f, 1.0f);
	float clampedViewportScaleY = std::clamp(viewportScaleY, 1e-4f, 1.0f);
	float clampedPinholeOffsetX = std::isfinite(pinholeOffsetX) ? std::clamp(pinholeOffsetX, -1.0f, 1.0f) : 0.0f;
	float clampedPinholeOffsetY = std::isfinite(pinholeOffsetY) ? std::clamp(pinholeOffsetY, -1.0f, 1.0f) : 0.0f;
	if (!globals::game::isVR) {
		clampedViewportScaleX = 1.0f;
		clampedViewportScaleY = 1.0f;
		clampedPinholeOffsetX = 0.0f;
		clampedPinholeOffsetY = 0.0f;
	}

	sl::Constants slConstants = {};

	// Calculate aspect ratio for the SINGLE EYE
	float2 fullOutputSize = upscaling.GetRuntimeResolutionPlan().finalOutputSize;
	if (fullOutputSize.x <= 0.0f || fullOutputSize.y <= 0.0f)
		fullOutputSize = state->screenSize;
	float eyeWidth = fullOutputSize.x * (globals::game::isVR ? 0.5f : 1.0f);
	float eyeHeight = fullOutputSize.y;
	slConstants.cameraAspectRatio = (eyeWidth * clampedViewportScaleX) / (eyeHeight * clampedViewportScaleY);

	slConstants.cameraFOV = Util::GetVerticalFOVRad();
	slConstants.cameraNear = *globals::game::cameraNear;
	slConstants.cameraFar = *globals::game::cameraFar;

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse(eyeIndex).Transpose();
	auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered(eyeIndex).Transpose();

	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
	slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
	slConstants.depthInverted = sl::Boolean::eFalse;

	if (globals::game::isVR) {
		const bool isCroppedViewport = clampedViewportScaleX < 0.999f || clampedViewportScaleY < 0.999f;
		applyCroppedConstantsCorrection = isCroppedViewport;
		if (applyCroppedConstantsCorrection) {
			const float invScaleX = 1.0f / clampedViewportScaleX;
			const float invScaleY = 1.0f / clampedViewportScaleY;

			// Match projection to the cropped DLSS viewport so temporal reprojection
			// operates in the same clip space as color/depth/mvec inputs.
			slConstants.cameraViewToClip[0].x *= invScaleX;
			slConstants.cameraViewToClip[0].y *= invScaleX;
			slConstants.cameraViewToClip[0].z *= invScaleX;
			slConstants.cameraViewToClip[0].w *= invScaleX;
			slConstants.cameraViewToClip[1].x *= invScaleY;
			slConstants.cameraViewToClip[1].y *= invScaleY;
			slConstants.cameraViewToClip[1].z *= invScaleY;
			slConstants.cameraViewToClip[1].w *= invScaleY;

			// cameraFOV is vertical; scale by cropped Y region.
			slConstants.cameraFOV = 2.0f * atanf(clampedViewportScaleY * tanf(slConstants.cameraFOV * 0.5f));
			slConstants.cameraPinholeOffset = {
				clampedPinholeOffsetX / clampedViewportScaleX,
				clampedPinholeOffsetY / clampedViewportScaleY
			};
		}

		// VR: compute clipToCameraView / clipToPrevClip / prevClipToClip from Skyrim's per-eye matrices.
		// recalculateCameraMatrices() uses a single static prev-frame slot -- unusable for two viewports.
		sl::matrixFullInvert(slConstants.clipToCameraView, slConstants.cameraViewToClip);

		auto currViewProj = globals::game::frameBufferCached.GetCameraViewProjUnjittered(eyeIndex).Transpose();
		auto prevViewProj = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered(eyeIndex).Transpose();

		sl::float4x4 currViewProjSL = *(sl::float4x4*)&currViewProj;
		sl::float4x4 prevViewProjSL = *(sl::float4x4*)&prevViewProj;

		sl::float4x4 invCurrViewProj;
		sl::matrixFullInvert(invCurrViewProj, currViewProjSL);
		sl::matrixMul(slConstants.clipToPrevClip, invCurrViewProj, prevViewProjSL);

		if (applyCroppedConstantsCorrection) {
			const float invScaleX = 1.0f / clampedViewportScaleX;
			const float invScaleY = 1.0f / clampedViewportScaleY;
			const float leftFactors[4] = { clampedViewportScaleX, clampedViewportScaleY, 1.0f, 1.0f };
			const float rightFactors[4] = { invScaleX, invScaleY, 1.0f, 1.0f };

			// Conjugate clipToPrevClip into cropped clip-space basis:
			// CTP_cropped = inv(S) * CTP * S
			float* ctpValues = &slConstants.clipToPrevClip[0].x;
			for (uint32_t row = 0; row < 4; ++row) {
				for (uint32_t col = 0; col < 4; ++col) {
					ctpValues[row * 4 + col] *= leftFactors[row] * rightFactors[col];
				}
			}
		}

		sl::matrixFullInvert(slConstants.prevClipToClip, slConstants.clipToPrevClip);
	} else {
		recalculateCameraMatrices(slConstants);
	}

	auto jitter = upscaling.jitter;
	slConstants.jitterOffset = { -jitter.x, -jitter.y };
	const bool requestHistoryReset = upscaling.ShouldResetHistoryThisFrame();
	slConstants.reset = requestHistoryReset ? sl::Boolean::eTrue : sl::Boolean::eFalse;

	if (globals::game::isVR && applyCroppedConstantsCorrection) {
		slConstants.mvecScale = { 1.0f / clampedViewportScaleX, 1.0f / clampedViewportScaleY };
	} else {
		slConstants.mvecScale = { 1.0f, 1.0f };
	}
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	const auto makeFrameConstantsSignature = [&]() {
		DLSSFrameConstantsCache signature{};
		signature.valid = true;
		signature.frame = diagnostics ? diagnostics->frame : state->frameCount;
		signature.frameToken = reinterpret_cast<std::uintptr_t>(frameToken);
		signature.viewport = static_cast<uint32_t>(p_viewport);
		signature.eyeIndex = eyeIndex;
		signature.viewportRole = diagnostics ? static_cast<uint32_t>(diagnostics->viewportRole) : static_cast<uint32_t>(DLSSViewportRole::FullEye);
		signature.outputWidth = diagnostics ? diagnostics->outputWidth : 0u;
		signature.outputHeight = diagnostics ? diagnostics->outputHeight : 0u;
		signature.qualityMode = diagnostics ? diagnostics->qualityMode : 0u;
		signature.dlssPreset = diagnostics ? diagnostics->dlssPreset : 0u;
		signature.extentInWidth = diagnostics ? diagnostics->extentIn.width : 0u;
		signature.extentInHeight = diagnostics ? diagnostics->extentIn.height : 0u;
		signature.extentInLeft = diagnostics ? diagnostics->extentIn.left : 0u;
		signature.extentInTop = diagnostics ? diagnostics->extentIn.top : 0u;
		signature.extentOutWidth = diagnostics ? diagnostics->extentOut.width : 0u;
		signature.extentOutHeight = diagnostics ? diagnostics->extentOut.height : 0u;
		signature.extentOutLeft = diagnostics ? diagnostics->extentOut.left : 0u;
		signature.extentOutTop = diagnostics ? diagnostics->extentOut.top : 0u;
		signature.viewportScaleXQ = QuantizeDLSSDiagnosticFloat(clampedViewportScaleX);
		signature.viewportScaleYQ = QuantizeDLSSDiagnosticFloat(clampedViewportScaleY);
		signature.pinholeOffsetXQ = QuantizeDLSSDiagnosticFloat(clampedPinholeOffsetX);
		signature.pinholeOffsetYQ = QuantizeDLSSDiagnosticFloat(clampedPinholeOffsetY);
		signature.jitterXQ = QuantizeDLSSDiagnosticFloat(upscaling.jitter.x);
		signature.jitterYQ = QuantizeDLSSDiagnosticFloat(upscaling.jitter.y);
		signature.historyResetRequested = requestHistoryReset;
		signature.constantsIdentity = ComputeConstantsIdentity(slConstants);
		return signature;
	};
	const auto frameConstantsMatch = [](const DLSSFrameConstantsCache& a_cached, const DLSSFrameConstantsCache& a_signature) {
		return a_cached.valid &&
		       a_cached.frame == a_signature.frame &&
		       a_cached.frameToken == a_signature.frameToken &&
		       a_cached.viewport == a_signature.viewport &&
		       a_cached.eyeIndex == a_signature.eyeIndex &&
		       a_cached.viewportRole == a_signature.viewportRole &&
		       a_cached.outputWidth == a_signature.outputWidth &&
		       a_cached.outputHeight == a_signature.outputHeight &&
		       a_cached.qualityMode == a_signature.qualityMode &&
		       a_cached.dlssPreset == a_signature.dlssPreset &&
		       a_cached.extentInWidth == a_signature.extentInWidth &&
		       a_cached.extentInHeight == a_signature.extentInHeight &&
		       a_cached.extentInLeft == a_signature.extentInLeft &&
		       a_cached.extentInTop == a_signature.extentInTop &&
		       a_cached.extentOutWidth == a_signature.extentOutWidth &&
		       a_cached.extentOutHeight == a_signature.extentOutHeight &&
		       a_cached.extentOutLeft == a_signature.extentOutLeft &&
		       a_cached.extentOutTop == a_signature.extentOutTop &&
		       a_cached.viewportScaleXQ == a_signature.viewportScaleXQ &&
		       a_cached.viewportScaleYQ == a_signature.viewportScaleYQ &&
		       a_cached.pinholeOffsetXQ == a_signature.pinholeOffsetXQ &&
		       a_cached.pinholeOffsetYQ == a_signature.pinholeOffsetYQ &&
		       a_cached.jitterXQ == a_signature.jitterXQ &&
		       a_cached.jitterYQ == a_signature.jitterYQ &&
		       a_cached.historyResetRequested == a_signature.historyResetRequested &&
		       a_cached.constantsIdentity == a_signature.constantsIdentity;
	};
	const bool canAcceptDuplicateConstants =
		diagnostics &&
		diagnostics->submitStageVRDLSS &&
		(diagnostics->viewportRole == DLSSViewportRole::FullEye ||
			diagnostics->viewportRole == DLSSViewportRole::SubmitStageFoveatedCenter);
	DLSSFrameConstantsCache frameConstantsSignature{};
#ifdef DEVBENCH_BRIDGE_ENABLED
	const bool collectDevBenchTrace = outFrameConstantsSignature || IsDLSSDevBenchTraceActive();
	if (canAcceptDuplicateConstants || collectDevBenchTrace)
#else
	if (canAcceptDuplicateConstants)
#endif
		frameConstantsSignature = makeFrameConstantsSignature();
#ifdef DEVBENCH_BRIDGE_ENABLED
	DLSSDevBenchTraceSignature devBenchTraceSignature{};
	if (collectDevBenchTrace) {
		devBenchTraceSignature = BuildDLSSDevBenchTraceSignature(
			frameConstantsSignature,
			diagnostics,
			frameToken,
			&slConstants);
		if (outFrameConstantsSignature)
			*outFrameConstantsSignature = devBenchTraceSignature;
	}
#endif
	const auto hasCachedFrameConstantsSignature = [&]() {
		if (!canAcceptDuplicateConstants)
			return false;

		for (const auto& cachedSignature : dlssFrameConstantsCache) {
			if (frameConstantsMatch(cachedSignature, frameConstantsSignature))
				return true;
		}
		return false;
	};
	if (hasCachedFrameConstantsSignature()) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (IsDLSSDevBenchTraceActive()) {
			RecordDLSSDevBenchTrace(
				DLSSDevBenchTraceStage::ConstantsCacheReuse,
				static_cast<int32_t>(sl::Result::eOk),
				diagnostics,
				devBenchTraceSignature);
		}
#endif
		lastDLSSFailureDuplicatedConstants = false;
		return true;
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	const sl::Result res = slSetConstants(slConstants, *frameToken, p_viewport);
	if (IsDLSSDevBenchTraceActive()) {
		RecordDLSSDevBenchTrace(
			DLSSDevBenchTraceStage::SetConstants,
			static_cast<int32_t>(res),
			diagnostics,
			devBenchTraceSignature);
	}
	if (res != sl::Result::eOk) {
#else
	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, p_viewport))) {
#endif
		const bool duplicatedConstants = res == sl::Result::eErrorDuplicatedConstants;
		lastDLSSFailureDuplicatedConstants = duplicatedConstants;
		const auto resultLabel = magic_enum::enum_name(res);
		if (diagnostics) {
			if (ShouldEmitDLSSDiagnostic(DLSSDiagnosticStage::SetConstants, diagnostics, static_cast<int32_t>(res), resultLabel)) {
				logger::error(
					"[Streamline] Could not set constants for eye {}: result={} label='{}' role={} viewport={} frame={} extentIn={}x{} extentOut={}x{} output={}x{} scale={:.6f}x{:.6f} pinhole={:.6f},{:.6f} duplicateConstants={}",
					eyeIndex,
					FormatDLSSDiagnosticResult(static_cast<int32_t>(res), resultLabel),
					diagnostics->label ? diagnostics->label : "DLSS Evaluate",
					magic_enum::enum_name(diagnostics->viewportRole),
					static_cast<uint32_t>(p_viewport),
					diagnostics->frame,
					diagnostics->extentIn.width,
					diagnostics->extentIn.height,
					diagnostics->extentOut.width,
					diagnostics->extentOut.height,
					diagnostics->outputWidth,
					diagnostics->outputHeight,
					diagnostics->viewportScaleX,
					diagnostics->viewportScaleY,
					diagnostics->pinholeOffsetX,
					diagnostics->pinholeOffsetY,
					lastDLSSFailureDuplicatedConstants);
			}
		} else {
			logger::error("[Streamline] Could not set constants for eye {}", eyeIndex);
		}
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::SetConstants, res, diagnostics);
		return false;
	}

	if (canAcceptDuplicateConstants) {
		auto* targetSlot = &dlssFrameConstantsCache[static_cast<uint32_t>(p_viewport) % dlssFrameConstantsCache.size()];
		for (auto& cachedSignature : dlssFrameConstantsCache) {
			if (!cachedSignature.valid ||
				(cachedSignature.viewport == frameConstantsSignature.viewport &&
					cachedSignature.eyeIndex == frameConstantsSignature.eyeIndex &&
					cachedSignature.viewportRole == frameConstantsSignature.viewportRole)) {
				targetSlot = &cachedSignature;
				break;
			}
		}
		*targetSlot = frameConstantsSignature;
	}
	return true;
}

bool Streamline::IsRTXAndBelow40Series(const DXGI_ADAPTER_DESC& a_adapterDesc) const
{
	UINT vendorId = a_adapterDesc.VendorId;
	UINT deviceId = a_adapterDesc.DeviceId;

	// Check if NVIDIA
	if (vendorId != 0x10DE)
		return false;

	// RTX 30 series (Ampere) - 0x2200-0x25FF
	if (deviceId >= 0x2200 && deviceId <= 0x2600)
		return true;

	// RTX 20 series (Turing with RT cores) - 0x1E00-0x1FFF
	if (deviceId >= 0x1E00 && deviceId <= 0x1FFF)
		return true;

	return false;
}

bool Streamline::SetDLSSOptions(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t width, uint32_t height, bool colorBuffersHDR, uint32_t qualityMode, uint32_t dlssPreset, const DLSSDispatchDiagnostics* diagnostics)
{
	if (!slDLSSSetOptions)
		return false;

	// Map custom render-scale presets to the nearest supported DLSS mode.
	qualityMode = std::min(qualityMode, Upscaling::kQualityModeMaxIndex);
	dlssPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);

	bool useLegacyProfile = isRTXBelow40series;
	auto& cache = GetDLSSOptionsCache(viewportRole, eyeIndex, qualityMode, dlssPreset);
	const uint32_t viewportKey = static_cast<uint32_t>(p_viewport);
	if (cache.valid &&
		cache.viewport == viewportKey &&
		cache.outputWidth == width &&
		cache.outputHeight == height &&
		cache.qualityMode == qualityMode &&
		cache.dlssPreset == dlssPreset &&
		cache.isHDR == colorBuffersHDR &&
		cache.useLegacyProfile == useLegacyProfile) {
		return true;
	}

	sl::DLSSOptions dlssOptions{};
	switch (qualityMode) {
	case 1:
	case 2:
	case 3:
		dlssOptions.mode = sl::DLSSMode::eMaxQuality;
		break;
	case 4:
		dlssOptions.mode = sl::DLSSMode::eBalanced;
		break;
	case 5:
		dlssOptions.mode = sl::DLSSMode::eMaxPerformance;
		break;
	case 6:
		dlssOptions.mode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssOptions.mode = sl::DLSSMode::eDLAA;
		break;
	}

	dlssOptions.outputWidth = width;
	dlssOptions.outputHeight = height;
	dlssOptions.colorBuffersHDR = colorBuffersHDR ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	dlssOptions.useAutoExposure = sl::Boolean::eTrue;

	sl::DLSSPreset selectedPreset = sl::DLSSPreset::ePresetK;
	switch (dlssPreset) {
	case Upscaling::kDLSSPresetJ:
		selectedPreset = sl::DLSSPreset::ePresetJ;
		break;
	case Upscaling::kDLSSPresetK:
		selectedPreset = sl::DLSSPreset::ePresetK;
		break;
	case Upscaling::kDLSSPresetL:
		selectedPreset = sl::DLSSPreset::ePresetL;
		break;
	case Upscaling::kDLSSPresetM:
		selectedPreset = sl::DLSSPreset::ePresetM;
		break;
	case Upscaling::kDLSSPresetF:
		selectedPreset = sl::DLSSPreset::ePresetF;
		break;
	case Upscaling::kDLSSPresetE:
		selectedPreset = sl::DLSSPreset::ePresetE;
		break;
	default:
		selectedPreset = sl::DLSSPreset::ePresetK;
		break;
	}

	dlssOptions.dlaaPreset = selectedPreset;
	dlssOptions.ultraQualityPreset = selectedPreset;
	dlssOptions.qualityPreset = selectedPreset;
	dlssOptions.balancedPreset = selectedPreset;
	dlssOptions.performancePreset = selectedPreset;
	dlssOptions.ultraPerformancePreset = selectedPreset;

	dlssOptions.preExposure = 1.0f;
	dlssOptions.sharpness = 0.0f;

	if (SL_FAILED(result, slDLSSSetOptions(p_viewport, dlssOptions))) {
		logger::critical("[Streamline] Could not enable DLSS for viewport {} eye {}: {}",
			static_cast<uint32_t>(p_viewport),
			eyeIndex,
			magic_enum::enum_name(result));
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::SetOptions, result, diagnostics);
		cache.valid = false;
		return false;
	}

	cache.valid = true;
	cache.viewport = viewportKey;
	cache.outputWidth = width;
	cache.outputHeight = height;
	cache.qualityMode = qualityMode;
	cache.dlssPreset = dlssPreset;
	cache.isHDR = colorBuffersHDR;
	cache.useLegacyProfile = useLegacyProfile;
	if (p_viewport == viewport) {
		activeDLSSViewportResourcesAllocated[0] = true;
	} else if (p_viewport == viewportRight) {
		activeDLSSViewportResourcesAllocated[1] = true;
	} else {
		for (auto& roleSlots : vrDLSSViewportSlots) {
			for (auto& slot : roleSlots) {
				for (uint32_t eye = 0; eye < 2; ++eye) {
					if (slot.viewport[eye] == p_viewport) {
						slot.resourcesAllocated[eye] = true;
						return true;
					}
				}
			}
		}
	}
	return true;
}

int Streamline::FindVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t qualityMode, uint32_t dlssPreset) const
{
	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	for (uint32_t slot = 0; slot < kVRDLSSViewportSlotCount; ++slot) {
		const auto& viewportSlot = vrDLSSViewportSlots[roleIndex][slot];
		if (viewportSlot.valid &&
			viewportSlot.qualityMode == clampedQualityMode &&
			viewportSlot.dlssPreset == clampedPreset) {
			return static_cast<int>(slot);
		}
	}

	return -1;
}

bool Streamline::TryResolveExistingVRDLSSViewport(
	DLSSViewportRole a_viewportRole,
	uint32_t a_eyeIndex,
	uint32_t a_qualityMode,
	uint32_t a_dlssPreset,
	uint32_t a_outputWidth,
	uint32_t a_outputHeight,
	ID3D11Resource* a_colorInput,
	sl::ViewportHandle& a_viewport) const
{
	if (!globals::game::isVR || a_eyeIndex >= 2 ||
		!initialized || !featureDLSS || !slEvaluateFeature || !slDLSSSetOptions ||
		!globals::d3d::context || !a_colorInput || !a_outputWidth || !a_outputHeight) {
		return false;
	}
	if (static_cast<uint32_t>(a_viewportRole) >= kVRDLSSViewportRoleCount)
		return false;

	const uint32_t roleIndex = GetDLSSViewportRoleIndex(a_viewportRole);
	if (pendingDLSSResourceFreeIdleFence ||
		pendingVRDLSSSlotRecycleIdleFences[roleIndex]) {
		return false;
	}

	const uint32_t qualityMode = std::min<uint32_t>(
		a_qualityMode,
		Upscaling::kQualityModeMaxIndex);
	const uint32_t dlssPreset = Upscaling::ClampDLSSPresetUInt(a_dlssPreset);
	const int slotIndex = FindVRDLSSViewportSlot(
		a_viewportRole,
		qualityMode,
		dlssPreset);
	if (slotIndex < 0)
		return false;

	const auto& slot = vrDLSSViewportSlots[roleIndex][slotIndex];
	const auto resolvedViewport = slot.viewport[a_eyeIndex];
	if (!IsVRDLSSViewportResourceCompatible(
			slot,
			a_eyeIndex,
			qualityMode,
			dlssPreset,
			a_outputWidth,
			a_outputHeight,
			a_colorInput)) {
		return false;
	}

	a_viewport = resolvedViewport;
	return true;
}

int Streamline::ChooseVRDLSSViewportSlotForAllocation(DLSSViewportRole viewportRole) const
{
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	for (uint32_t slot = 0; slot < kVRDLSSViewportSlotCount; ++slot) {
		if (!vrDLSSViewportSlots[roleIndex][slot].valid)
			return static_cast<int>(slot);
	}

	uint32_t lruSlot = 0;
	uint64_t lruCounter = vrDLSSViewportSlots[roleIndex][0].lastUse;
	for (uint32_t slot = 1; slot < kVRDLSSViewportSlotCount; ++slot) {
		if (vrDLSSViewportSlots[roleIndex][slot].lastUse < lruCounter) {
			lruCounter = vrDLSSViewportSlots[roleIndex][slot].lastUse;
			lruSlot = slot;
		}
	}

	return static_cast<int>(lruSlot);
}

bool Streamline::FreeDLSSViewportResources(sl::ViewportHandle a_viewport, uint32_t a_eyeIndex, bool a_logFailures)
{
	if (!slDLSSSetOptions || !slFreeResources)
		return true;

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;

	const sl::Result optionsResult = slDLSSSetOptions(a_viewport, dlssOptions);
	if (a_logFailures && optionsResult != sl::Result::eOk) {
		logger::debug("[Streamline] DLSS off failed for viewport {} eye {}: {}",
			static_cast<uint32_t>(a_viewport),
			a_eyeIndex,
			magic_enum::enum_name(optionsResult));
	}

	const sl::Result freeResult = slFreeResources(sl::kFeatureDLSS, a_viewport);
	if (a_logFailures && freeResult != sl::Result::eOk) {
		logger::debug("[Streamline] DLSS resource free failed for viewport {} eye {}: {}",
			static_cast<uint32_t>(a_viewport),
			a_eyeIndex,
			magic_enum::enum_name(freeResult));
	}
	return freeResult == sl::Result::eOk;
}

bool Streamline::FreeVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t slotIndex, bool logFailures)
{
	if (slotIndex >= kVRDLSSViewportSlotCount)
		return true;

	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	auto& slot = vrDLSSViewportSlots[roleIndex][slotIndex];
	if (!slot.valid)
		return true;

	bool slotResourcesFreed = true;
	for (uint32_t eye = 0; eye < 2; ++eye) {
		slot.resourcesAllocated[eye] = slot.resourcesAllocated[eye] || slot.optionsCache[eye].valid;
		const bool shouldLogFailures = logFailures || slot.optionsCache[eye].valid;
		if (slot.resourcesAllocated[eye]) {
			const bool eyeFreed = FreeDLSSViewportResources(slot.viewport[eye], eye, shouldLogFailures);
			slotResourcesFreed = eyeFreed && slotResourcesFreed;
			if (eyeFreed)
				slot.resourcesAllocated[eye] = false;
		}
		slot.optionsCache[eye] = {};
	}

	if (slot.resourcesAllocated[0] || slot.resourcesAllocated[1])
		return false;

	slot.valid = false;
	slot.qualityMode = 0;
	slot.dlssPreset = 0;
	slot.lastUse = 0;
	return slotResourcesFreed;
}

Streamline::DLSSViewportPreparationResult Streamline::PrepareVRDLSSViewport(
	DLSSViewportRole viewportRole,
	uint32_t qualityMode,
	uint32_t dlssPreset)
{
	if (!globals::game::isVR)
		return DLSSViewportPreparationResult::Ready;

	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	// Full-eye and foveated-center caches advance independently. A cache hit
	// in one role must never consume the fence that protects another role's
	// LRU victim, or that other role will restart its drain indefinitely.
	auto& pendingSlotRecycleIdleFence = pendingVRDLSSSlotRecycleIdleFences[roleIndex];
	int slotIndex = FindVRDLSSViewportSlot(viewportRole, clampedQualityMode, clampedPreset);
	if (slotIndex >= 0) {
		// A latest-wins request can supersede a pending miss with a cache hit.
		// Drain that abandoned fence without delaying the already-resident target.
		if (pendingSlotRecycleIdleFence) {
			if (auto context = globals::d3d::context) {
				const auto idleFenceResult = BeginOrPollD3D11IdleFence(
					context,
					pendingSlotRecycleIdleFence,
					"superseded VR DLSS viewport slot recycle");
				if (idleFenceResult == D3D11IdleFenceResult::Failed)
					return DLSSViewportPreparationResult::Failed;
			} else {
				ReleaseD3D11IdleFence(pendingSlotRecycleIdleFence);
			}
		}
		return DLSSViewportPreparationResult::Ready;
	}

	slotIndex = ChooseVRDLSSViewportSlotForAllocation(viewportRole);
	if (slotIndex < 0)
		slotIndex = 0;

	auto& slot = vrDLSSViewportSlots[roleIndex][slotIndex];
	if (slot.valid) {
		if (auto context = globals::d3d::context) {
			const auto idleFenceResult = BeginOrPollD3D11IdleFence(context, pendingSlotRecycleIdleFence, "VR DLSS viewport slot recycle");
			if (idleFenceResult == D3D11IdleFenceResult::Pending) {
				static bool loggedSlotRecyclePending = false;
				if (!loggedSlotRecyclePending) {
					logger::warn("[Streamline] Deferring VR DLSS viewport preparation because the previous slot is still in flight.");
					loggedSlotRecyclePending = true;
				}
				nonVRDLSSOptionsCache.valid = false;
				return DLSSViewportPreparationResult::Pending;
			}
			if (idleFenceResult == D3D11IdleFenceResult::Failed) {
				static bool loggedSlotRecycleFenceFailure = false;
				if (!loggedSlotRecycleFenceFailure) {
					logger::warn("[Streamline] VR DLSS viewport preparation failed because the slot recycle fence could not be queried.");
					loggedSlotRecycleFenceFailure = true;
				}
				nonVRDLSSOptionsCache.valid = false;
				return DLSSViewportPreparationResult::Failed;
			}
		} else {
			ReleaseD3D11IdleFence(pendingSlotRecycleIdleFence);
		}
		if (!FreeVRDLSSViewportSlot(viewportRole, static_cast<uint32_t>(slotIndex), true)) {
			static bool loggedSlotRecycleFreeFailure = false;
			if (!loggedSlotRecycleFreeFailure) {
				logger::warn("[Streamline] VR DLSS viewport preparation failed because the previous slot resources could not be released.");
				loggedSlotRecycleFreeFailure = true;
			}
			nonVRDLSSOptionsCache.valid = false;
			return DLSSViewportPreparationResult::Failed;
		}
	}

	slot.valid = true;
	slot.qualityMode = clampedQualityMode;
	slot.dlssPreset = clampedPreset;
	slot.lastUse = 0;
	slot.resourcesAllocated[0] = false;
	slot.resourcesAllocated[1] = false;
	for (auto& optionsCache : slot.optionsCache)
		optionsCache = {};

	const uint32_t viewportBase =
		kVRDLSSSlotViewportBase +
		(roleIndex * kVRDLSSSlotViewportRoleStride) +
		(static_cast<uint32_t>(slotIndex) * kVRDLSSSlotViewportEyeStride);
	slot.viewport[0] = sl::ViewportHandle(viewportBase);
	slot.viewport[1] = sl::ViewportHandle(viewportBase + 1);
	return DLSSViewportPreparationResult::Ready;
}

bool Streamline::IsVRDLSSViewportResourceCompatible(
	const VRDLSSViewportSlot& a_slot,
	uint32_t a_eyeIndex,
	uint32_t a_qualityMode,
	uint32_t a_dlssPreset,
	uint32_t a_outputWidth,
	uint32_t a_outputHeight,
	ID3D11Resource* a_colorInput) const noexcept
{
	if (!a_slot.valid || a_eyeIndex >= 2 || !a_outputWidth ||
		!a_outputHeight || !a_colorInput) {
		return false;
	}

	const auto qualityMode =
		std::min<uint32_t>(a_qualityMode, Upscaling::kQualityModeMaxIndex);
	const auto dlssPreset = Upscaling::ClampDLSSPresetUInt(a_dlssPreset);
	const auto& options = a_slot.optionsCache[a_eyeIndex];
	return a_slot.qualityMode == qualityMode &&
	       a_slot.dlssPreset == dlssPreset &&
	       a_slot.resourcesAllocated[a_eyeIndex] && options.valid &&
	       options.viewport ==
	           static_cast<uint32_t>(a_slot.viewport[a_eyeIndex]) &&
	       options.outputWidth == a_outputWidth &&
	       options.outputHeight == a_outputHeight &&
	       options.qualityMode == qualityMode &&
	       options.dlssPreset == dlssPreset &&
	       options.isHDR == GetDLSSColorBuffersHDR(a_colorInput) &&
	       options.useLegacyProfile == isRTXBelow40series;
}

bool Streamline::HasCompleteVRDLSSViewportResources(
	DLSSViewportRole a_viewportRole,
	uint32_t a_qualityMode,
	uint32_t a_dlssPreset,
	uint32_t a_outputWidth,
	uint32_t a_outputHeight,
	ID3D11Resource* a_colorInput) const noexcept
{
	if (!globals::game::isVR || !initialized || !featureDLSS ||
		static_cast<uint32_t>(a_viewportRole) >= kVRDLSSViewportRoleCount ||
		!a_outputWidth || !a_outputHeight || !a_colorInput) {
		return false;
	}

	const auto roleIndex = GetDLSSViewportRoleIndex(a_viewportRole);
	if (pendingDLSSResourceFreeIdleFence ||
		pendingVRDLSSSlotRecycleIdleFences[roleIndex]) {
		return false;
	}

	const auto qualityMode =
		std::min<uint32_t>(a_qualityMode, Upscaling::kQualityModeMaxIndex);
	const auto dlssPreset = Upscaling::ClampDLSSPresetUInt(a_dlssPreset);
	const int slotIndex = FindVRDLSSViewportSlot(
		a_viewportRole,
		qualityMode,
		dlssPreset);
	if (slotIndex < 0)
		return false;

	const auto& slot = vrDLSSViewportSlots[roleIndex][slotIndex];
	for (uint32_t eye = 0; eye < 2; ++eye) {
		if (!IsVRDLSSViewportResourceCompatible(
				slot,
				eye,
				qualityMode,
				dlssPreset,
				a_outputWidth,
				a_outputHeight,
				a_colorInput)) {
			return false;
		}
	}
	return true;
}

bool Streamline::IsDLSSRuntimeReady() const noexcept
{
	return lifecycleState.load(std::memory_order_acquire) == LifecycleState::Initialized &&
	       initialized.load(std::memory_order_acquire) &&
	       featureCheckComplete.load(std::memory_order_acquire) &&
	       featureDLSS.load(std::memory_order_acquire) &&
	       boundDeviceIdentity &&
	       slEvaluateFeature && slSetConstants && slGetNewFrameToken &&
	       slDLSSGetOptimalSettings && slDLSSGetState && slDLSSSetOptions;
}

bool Streamline::ResolveDLSSViewport(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset, sl::ViewportHandle& outViewport)
{
	outViewport = p_viewport;
	if (!globals::game::isVR)
		return true;

	if (PrepareVRDLSSViewport(viewportRole, qualityMode, dlssPreset) != DLSSViewportPreparationResult::Ready)
		return false;

	const uint32_t eye = eyeIndex > 0 ? 1u : 0u;
	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	const int slotIndex = FindVRDLSSViewportSlot(viewportRole, clampedQualityMode, clampedPreset);
	if (slotIndex < 0) {
		nonVRDLSSOptionsCache.valid = false;
		return false;
	}

	auto& activeSlot = vrDLSSViewportSlots[roleIndex][slotIndex];
	activeSlot.lastUse = ++vrDLSSViewportUseCounter;
	outViewport = activeSlot.viewport[eye];
	return true;
}

Streamline::DLSSOptionsCache& Streamline::GetDLSSOptionsCache(DLSSViewportRole viewportRole, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset)
{
	if (!globals::game::isVR)
		return nonVRDLSSOptionsCache;

	const uint32_t eye = eyeIndex > 0 ? 1u : 0u;
	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);

	const int slotIndex = FindVRDLSSViewportSlot(viewportRole, clampedQualityMode, clampedPreset);
	if (slotIndex >= 0)
		return vrDLSSViewportSlots[roleIndex][slotIndex].optionsCache[eye];

	// Fallback for unexpected ordering; keeps behavior deterministic and forces option re-apply.
	nonVRDLSSOptionsCache.valid = false;
	return nonVRDLSSOptionsCache;
}

void Streamline::InvalidateDLSSOptionsCache()
{
	nonVRDLSSOptionsCache = {};
	dlssFrameConstantsCache = {};
	for (auto& roleSlots : vrDLSSViewportSlots) {
		for (auto& slot : roleSlots) {
			for (auto& optionsCache : slot.optionsCache)
				optionsCache = {};
		}
	}
}

void Streamline::ResetDLSSIdleFences()
{
	ReleaseD3D11IdleFence(pendingDLSSResourceFreeIdleFence);
	for (auto& pendingSlotRecycleIdleFence : pendingVRDLSSSlotRecycleIdleFences)
		ReleaseD3D11IdleFence(pendingSlotRecycleIdleFence);
}

void Streamline::ResetFrameTracking()
{
	frameTokenCoordinator.Reset();
	dlssFrameConstantsCache = {};
}

bool Streamline::HasDLSSResourcesPendingTeardown() const
{
	if (pendingDLSSResourceFreeIdleFence ||
		std::ranges::any_of(pendingVRDLSSSlotRecycleIdleFences, [](const auto* a_fence) { return a_fence != nullptr; })) {
		return true;
	}

	// If DLSS is not active/available in this process, cached slot metadata
	// should not trigger a teardown cooldown by itself.
	if (!initialized || !featureDLSS)
		return false;

	if (activeDLSSViewportResourcesAllocated[0] || activeDLSSViewportResourcesAllocated[1])
		return true;

	if (nonVRDLSSOptionsCache.valid)
		return true;

	for (const auto& roleSlots : vrDLSSViewportSlots) {
		for (const auto& slot : roleSlots) {
			if (slot.valid)
				return true;

			if (slot.resourcesAllocated[0] || slot.resourcesAllocated[1])
				return true;

			for (const auto& optionsCache : slot.optionsCache) {
				if (optionsCache.valid)
					return true;
			}
		}
	}

	return false;
}

bool Streamline::EvaluateDLSS(sl::ViewportHandle vp, uint32_t eyeIndex,
	ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth,
	float pinholeOffsetX, float pinholeOffsetY, const char* label, DLSSViewportRole viewportRole,
	bool useAuthoritativeProfile, uint32_t authoritativeQualityMode, uint32_t authoritativeDLSSPreset)
{
	auto context = globals::d3d::context;
	if (!initialized || !featureDLSS || !slEvaluateFeature || !context ||
		!colorIn || !colorOut || !depth || !mvec || !reactiveMask || !transparencyMask)
		return false;
	if (globals::game::isVR && eyeIndex > 1)
		return false;

	sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, 0 };
	sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, 0 };
	sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
	sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
	sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
	sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

	auto& upscaling = globals::features::upscaling;
	auto state = globals::state;
	const bool vendorLifecycleMutationDeferred =
		globals::game::isVR &&
		upscaling.ShouldDeferVRVendorLifecycleMutation();
	const bool existingProviderOnly =
		vendorLifecycleMutationDeferred || useAuthoritativeProfile;
	const auto existingProvider =
		vendorLifecycleMutationDeferred && !useAuthoritativeProfile ?
			upscaling.GetExistingVRVendorProviderSnapshot() :
			Upscaling::VRExistingVendorProviderSnapshot{};
	float viewportScaleX = 1.0f;
	float viewportScaleY = 1.0f;
	if (state) {
		const auto& resolutionPlan = upscaling.GetRuntimeResolutionPlan();
		auto fullOutputSize = resolutionPlan.finalOutputSize;
		if (fullOutputSize.x <= 0.0f || fullOutputSize.y <= 0.0f)
			fullOutputSize = state->screenSize;

		const float fullOutputWidth = globals::game::isVR ? (fullOutputSize.x * 0.5f) : fullOutputSize.x;
		const float fullOutputHeight = fullOutputSize.y;
		if (fullOutputWidth > 0.0f && fullOutputHeight > 0.0f) {
			viewportScaleX = std::clamp(static_cast<float>(extentOut.width) / fullOutputWidth, 1e-4f, 1.0f);
			viewportScaleY = std::clamp(static_cast<float>(extentOut.height) / fullOutputHeight, 1e-4f, 1.0f);
		}
	}

	const bool colorBuffersHDR = GetDLSSColorBuffersHDR(colorIn);
	const bool useExistingDLSSProfile =
		existingProvider.valid &&
		existingProvider.method == Upscaling::UpscaleMethod::kDLSS;
	uint32_t qualityMode = 0;
	uint32_t dlssPreset = Upscaling::kDLSSPresetK;
	if (useAuthoritativeProfile) {
		qualityMode = std::min(authoritativeQualityMode, Upscaling::kQualityModeMaxIndex);
		dlssPreset = Upscaling::ClampDLSSPresetUInt(authoritativeDLSSPreset);
	} else if (useExistingDLSSProfile) {
		qualityMode = existingProvider.qualityMode;
		dlssPreset = existingProvider.dlssPreset;
	} else {
		qualityMode = std::min(upscaling.GetRuntimeQualityMode(), Upscaling::kQualityModeMaxIndex);
		dlssPreset = upscaling.GetRuntimeDLSSPreset();
	}
	const sl::ViewportHandle requestedViewport = vp;
	const bool submitStageVRDLSS =
		globals::game::isVR &&
		upscaling.IsPresentationUpscalingActive();

#ifdef DEVBENCH_BRIDGE_ENABLED
	const bool collectDLSSDiagnostics = ShouldLogDLSSDiagnostics() || IsDLSSDevBenchTraceActive();
#else
	const bool collectDLSSDiagnostics = ShouldLogDLSSDiagnostics();
#endif
	DLSSDispatchDiagnostics diagnostics{};
	DLSSDispatchDiagnostics* diagnosticsPtr = &diagnostics;
	diagnostics.label = label ? label : "DLSS Evaluate";
	diagnostics.frame = state ? state->frameCount : 0u;
	diagnostics.eyeIndex = eyeIndex;
	diagnostics.requestedViewport = requestedViewport;
	diagnostics.resolvedViewport = vp;
	diagnostics.extentIn = extentIn;
	diagnostics.extentOut = extentOut;
	diagnostics.outputWidth = outputWidth;
	diagnostics.outputHeight = extentOut.height;
	diagnostics.qualityMode = qualityMode;
	diagnostics.dlssPreset = dlssPreset;
	diagnostics.viewportRole = viewportRole;
	diagnostics.viewportScaleX = viewportScaleX;
	diagnostics.viewportScaleY = viewportScaleY;
	diagnostics.croppedViewport = viewportScaleX < 0.999f || viewportScaleY < 0.999f;
	diagnostics.pinholeOffsetX = pinholeOffsetX;
	diagnostics.pinholeOffsetY = pinholeOffsetY;
	diagnostics.submitStageVRDLSS = submitStageVRDLSS;
	diagnostics.colorIn = colorIn;
	diagnostics.colorOut = colorOut;
	diagnostics.depth = depth;
	diagnostics.motionVectors = mvec;
	diagnostics.reactiveMask = reactiveMask;
	diagnostics.transparencyMask = transparencyMask;
	if (collectDLSSDiagnostics) {
		diagnostics.jitterX = upscaling.jitter.x;
		diagnostics.jitterY = upscaling.jitter.y;
		diagnostics.colorBuffersHDR = colorBuffersHDR;
		diagnostics.presentationUpscalingActive = upscaling.IsPresentationUpscalingActive();
		diagnostics.renderScaleActive = upscaling.IsVRRenderScaleModeActive();
		diagnostics.foveatedDispatchEnabled = upscaling.IsFoveatedVendorDispatchEnabled(upscaling.GetRuntimeUpscaleMethod());
		diagnostics.peripheryTAAEnabled = upscaling.IsPeripheryTAAEnabled(upscaling.GetRuntimeUpscaleMethod());
		diagnostics.historyResetRequested = upscaling.ShouldResetHistoryThisFrame();
	}
	const auto updateOptionsCacheDiagnostics = [&]() {
		if (!collectDLSSDiagnostics)
			return;

		const auto& optionsCache = GetDLSSOptionsCache(viewportRole, eyeIndex, qualityMode, dlssPreset);
		diagnostics.optionsCacheValid = optionsCache.valid;
		diagnostics.optionsCacheViewport = optionsCache.viewport;
		diagnostics.optionsCacheOutputWidth = optionsCache.outputWidth;
		diagnostics.optionsCacheOutputHeight = optionsCache.outputHeight;
		diagnostics.optionsCacheQualityMode = optionsCache.qualityMode;
		diagnostics.optionsCacheDLSSPreset = optionsCache.dlssPreset;
		diagnostics.optionsCacheHDR = optionsCache.isHDR;
		diagnostics.optionsCacheLegacyProfile = optionsCache.useLegacyProfile;
	};

	if (existingProviderOnly) {
		if (!TryResolveExistingVRDLSSViewport(
				viewportRole,
				eyeIndex,
				qualityMode,
				dlssPreset,
				outputWidth,
				extentOut.height,
				colorIn,
				vp)) {
			LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::ResolveViewport, "lifecycle-gated", diagnosticsPtr);
			return false;
		}
	} else if (!ResolveDLSSViewport(viewportRole, vp, eyeIndex, qualityMode, dlssPreset, vp)) {
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::ResolveViewport, "unavailable", diagnosticsPtr);
		return false;
	}
	diagnostics.resolvedViewport = vp;
	updateOptionsCacheDiagnostics();

#ifdef DEVBENCH_BRIDGE_ENABLED
	DLSSDevBenchTraceSignature devBenchFrameConstantsSignature{};
	auto* devBenchFrameConstantsSignaturePtr = IsDLSSDevBenchTraceActive() ? &devBenchFrameConstantsSignature : nullptr;
#endif
	const auto frameTokenSnapshot = AcquireFrameToken(diagnostics.frame, "dlss");
	if (!frameTokenSnapshot) {
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::FrameToken, "unavailable", diagnosticsPtr);
		return false;
	}
	auto* const frameToken = frameTokenSnapshot->token;
	diagnostics.frame = frameTokenSnapshot->frame;
	diagnostics.frameToken = frameToken;
	if (!CheckFrameConstants(
			vp,
			frameToken,
			eyeIndex,
			viewportScaleX,
			viewportScaleY,
			pinholeOffsetX,
			pinholeOffsetY,
			diagnosticsPtr
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			devBenchFrameConstantsSignaturePtr
#endif
			))
		return false;
	if (!existingProviderOnly &&
		!SetDLSSOptions(viewportRole, vp, eyeIndex, outputWidth, extentOut.height, colorBuffersHDR, qualityMode, dlssPreset, diagnosticsPtr))
		return false;
	updateOptionsCacheDiagnostics();

	// These markers currently surround only a DLSS evaluation, not Skyrim's
	// complete render submission. Keep marker-driven scheduling disabled on every
	// runtime until CSX can publish an authoritative full-frame marker sequence.
	const bool emitPCLMarkers = ReflexPolicy::ResolveCSXMarkerOptimization(
		featureReflex,
		featurePCL,
		upscaling.settings.reflexUseMarkersToOptimize &&
			reflexOptionsCache.useMarkersToOptimize)
	                                .enabled;
	const auto emitPCLMarker = [&](sl::PCLMarker marker, const char* stageName, uint32_t stageIndex) {
		if (!emitPCLMarkers || !slPCLSetMarker || !frameToken)
			return;
		const sl::Result markerResult = slPCLSetMarker(marker, *frameToken);
		if (markerResult != sl::Result::eOk) {
			static bool markerErrorLogged[2][2] = { { false, false }, { false, false } };
			const uint32_t logIdx = globals::game::isVR ? std::min(eyeIndex, 1u) : 0u;
			const uint32_t boundedStageIndex = std::min(stageIndex, 1u);
			if (markerErrorLogged[logIdx][boundedStageIndex])
				return;
			markerErrorLogged[logIdx][boundedStageIndex] = true;
			logger::warn(
				"[Streamline] slPCLSetMarker({}) failed{}: {}",
				stageName,
				globals::game::isVR ? std::format(" for eye {}", eyeIndex) : "",
				magic_enum::enum_name(markerResult));
		}
	};

	sl::ResourceTag tags[] = {
		{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extentOut },
		{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn }
	};

	sl::ViewportHandle view(vp);
	const sl::BaseStructure* inputs[] = {
		&view,
		&tags[0],
		&tags[1],
		&tags[2],
		&tags[3],
		&tags[4],
		&tags[5]
	};

	if (state && state->frameAnnotations) {
		if (globals::game::isVR) {
			char buf[32];
			snprintf(buf, sizeof(buf), "DLSS Evaluate Eye %u", eyeIndex);
			state->BeginPerfEvent(buf);
		} else {
			state->BeginPerfEvent("DLSS Evaluate");
		}
	}

	emitPCLMarker(sl::PCLMarker::eRenderSubmitStart, "DLSS-EvaluateStart", 0);
	sl::Result evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);
	emitPCLMarker(sl::PCLMarker::eRenderSubmitEnd, "DLSS-EvaluateEnd", 1);

#ifdef DEVBENCH_BRIDGE_ENABLED
	if (devBenchFrameConstantsSignaturePtr) {
		RecordDLSSDevBenchTrace(
			DLSSDevBenchTraceStage::Evaluate,
			static_cast<int32_t>(evalResult),
			diagnosticsPtr,
			devBenchFrameConstantsSignature);
	}
#endif

	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	if (evalResult != sl::Result::eOk) {
		LogDLSSDispatchDiagnostics(DLSSDiagnosticStage::Evaluate, evalResult, diagnosticsPtr);
		static sl::ViewportHandle lastLoggedEvalErrorViewport[2] = {};
		static sl::Result lastLoggedEvalErrorResult[2] = {};
		uint32_t logIdx = globals::game::isVR ? std::min(eyeIndex, 1u) : 0;
		if (lastLoggedEvalErrorViewport[logIdx] != vp || lastLoggedEvalErrorResult[logIdx] != evalResult) {
			lastLoggedEvalErrorViewport[logIdx] = vp;
			lastLoggedEvalErrorResult[logIdx] = evalResult;
			D3D11_TEXTURE2D_DESC colorInDesc{};
			D3D11_TEXTURE2D_DESC colorOutDesc{};
			TryGetTexture2DDesc(colorIn, colorInDesc);
			TryGetTexture2DDesc(colorOut, colorOutDesc);
			logger::error(
				"[Streamline] slEvaluateFeature failed{} result={} viewport={} colorIn={}x{} fmt={} colorOut={}x{} fmt={} extentIn={}x{} extentOut={}x{}",
				globals::game::isVR ? std::format(" for eye {}", eyeIndex) : "",
				static_cast<int>(evalResult),
				static_cast<uint32_t>(vp),
				colorInDesc.Width,
				colorInDesc.Height,
				static_cast<uint32_t>(colorInDesc.Format),
				colorOutDesc.Width,
				colorOutDesc.Height,
				static_cast<uint32_t>(colorOutDesc.Format),
				extentIn.width,
				extentIn.height,
				extentOut.width,
				extentOut.height);
		}
	}

	return evalResult == sl::Result::eOk;
}

bool Streamline::UpscaleRegion(uint32_t eyeIndex, ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth, uint32_t outputHeight,
	float pinholeOffsetX, float pinholeOffsetY)
{
	if (!initialized || !featureDLSS || !colorIn || !colorOut || !depth || !mvec || !reactiveMask || !transparencyMask)
		return false;

	sl::ViewportHandle vp = (globals::game::isVR && eyeIndex == 1) ? viewportRight : viewport;
	sl::Extent extentIn{ 0u, 0u, renderWidth, renderHeight };
	sl::Extent extentOut{ 0u, 0u, outputWidth, outputHeight };

	return EvaluateDLSS(vp, eyeIndex, colorIn, colorOut, depth, mvec, reactiveMask, transparencyMask, extentIn, extentOut, outputWidth, pinholeOffsetX, pinholeOffsetY, "UpscaleRegion");
}

bool Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors)
{
	auto state = globals::state;

	auto renderer = globals::game::renderer;
	if (!state || !renderer)
		return false;

	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	auto& upscaling = globals::features::upscaling;
	if (globals::game::isVR && upscaling.IsPresentationUpscalingActive()) {
		upscaling.dlssUpscaleOutputInSharpenerTexture = false;
		return false;
	}

	auto screenSize = upscaling.GetRuntimeResolutionPlan().finalOutputSize;
	auto renderSize = upscaling.GetRuntimeResolutionPlan().engineRenderSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f)
		screenSize = state->screenSize;
	if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
		renderSize = Util::ConvertToDynamic(screenSize);
	auto& mainTarget = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	const bool isVR = globals::game::isVR;
	const bool sharpenerOutputReady =
		upscaling.sharpenerTexture &&
		upscaling.sharpenerTexture->resource &&
		(isVR || upscaling.sharpenerTexture->resource.get() != a_upscalingTexture) &&
		(!isVR || upscaling.sharpenerTexture->uav);

	// Flat DLSS receives kMAIN as its color input, so writing directly back to it
	// would alias the Streamline input and output tags. VR first isolates each eye's
	// input and can retain its direct combined-target path when sharpening is off.
	if (!isVR) {
		static bool loggedMissingSharpenerOutput = false;
		if (!sharpenerOutputReady) {
			if (!loggedMissingSharpenerOutput) {
				logger::error("[Upscaling] DLSS dispatch skipped because a distinct intermediate output is unavailable.");
				loggedMissingSharpenerOutput = true;
			}
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return false;
		}
		loggedMissingSharpenerOutput = false;
	}

	const bool useSharpenerOutput =
		sharpenerOutputReady &&
		upscaling.ShouldRouteDLSSMainPassThroughSharpener();
	ID3D11Resource* colorOut = useSharpenerOutput ? upscaling.sharpenerTexture->resource.get() : a_upscalingTexture;
	ID3D11UnorderedAccessView* colorOutUAV = useSharpenerOutput ? upscaling.sharpenerTexture->uav.get() : mainTarget.UAV;
	const bool outputToSharpener = useSharpenerOutput;

	// VR: Combined-buffer mode with extent offsets causes temporal ghosting on the right eye
	// because DLSS's internal history buffers use extent offsets as indices.
	// Per-eye isolation with extents at {0,0} is required.
	if (globals::game::isVR) {
		auto context = globals::d3d::context;
		uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
		uint32_t eyeHeightOut = (uint32_t)screenSize.y;
		uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
		uint32_t eyeHeightIn = (uint32_t)renderSize.y;
		const uint32_t contractGeneration =
			upscaling.IsVRRenderScaleModeLatched() ?
				upscaling.GetActiveVRRenderScaleContractGeneration() :
				0u;
		const bool vendorLifecycleMutationDeferred =
			upscaling.ShouldDeferVRVendorLifecycleMutation();
		const auto existingProvider =
			vendorLifecycleMutationDeferred ?
				upscaling.GetExistingVRVendorProviderSnapshot() :
				Upscaling::VRExistingVendorProviderSnapshot{};
		if (vendorLifecycleMutationDeferred &&
			!upscaling.AreActiveVRIntermediateTexturesCompatible(
				Upscaling::UpscaleMethod::kDLSS,
				eyeWidthIn,
				eyeHeightIn,
				eyeWidthOut,
				eyeHeightOut,
				a_upscalingTexture,
				a_motionVectors,
				a_reactiveMask,
				a_transparencyCompositionMask,
				contractGeneration)) {
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return false;
		}
		if (vendorLifecycleMutationDeferred) {
			if (!existingProvider.valid ||
				existingProvider.method != Upscaling::UpscaleMethod::kDLSS ||
				existingProvider.renderEyeWidth != eyeWidthIn ||
				existingProvider.renderEyeHeight != eyeHeightIn ||
				existingProvider.displayEyeWidth != eyeWidthOut ||
				existingProvider.displayEyeHeight != eyeHeightOut) {
				upscaling.dlssUpscaleOutputInSharpenerTexture = false;
				return false;
			}
			const uint32_t qualityMode = existingProvider.qualityMode;
			const uint32_t dlssPreset = existingProvider.dlssPreset;
			for (uint32_t eye = 0; eye < 2; ++eye) {
				sl::ViewportHandle resolvedViewport{};
				if (!TryResolveExistingVRDLSSViewport(
						DLSSViewportRole::FullEye,
						eye,
						qualityMode,
						dlssPreset,
						eyeWidthOut,
						eyeHeightOut,
						upscaling.vrIntermediateColorIn[eye]->resource.get(),
						resolvedViewport)) {
					upscaling.dlssUpscaleOutputInSharpenerTexture = false;
					return false;
				}
			}
		}
		const bool useAuthoritativeExistingProfile =
			vendorLifecycleMutationDeferred &&
			existingProvider.valid &&
			existingProvider.method == Upscaling::UpscaleMethod::kDLSS;
		const uint32_t authoritativeQualityMode =
			useAuthoritativeExistingProfile ? existingProvider.qualityMode : 0u;
		const uint32_t authoritativeDLSSPreset =
			useAuthoritativeExistingProfile ?
				existingProvider.dlssPreset :
				Upscaling::kDLSSPresetK;

		// Split the combined stereo inputs up front. The direct left-eye path still
		// uses the native depth buffer, but isolated-output fallback needs valid
		// per-eye depth for both eyes.
		if (!upscaling.PreparePerEyeInputs(
				a_upscalingTexture,
				depthTexture.texture,
				a_motionVectors,
				a_reactiveMask,
				a_transparencyCompositionMask,
				false,
				true)) {
			static bool loggedPrepareFailure = false;
			if (!loggedPrepareFailure) {
				logger::warn("[Streamline] VR DLSS/DLAA skipped because per-eye input preparation failed.");
				loggedPrepareFailure = true;
			}
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return false;
		}

		const bool perEyeResourcesReady = upscaling.AreVRPerEyeUpscalingResourcesReady(true, false);
		if (!perEyeResourcesReady) {
			static bool loggedMissingResource = false;
			if (!loggedMissingResource) {
				logger::warn("[Streamline] VR DLSS/DLAA skipped because prepared per-eye resources are incomplete.");
				loggedMissingResource = true;
			}
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return false;
		}

		sl::Extent extentIn{ 0, 0, eyeWidthIn, eyeHeightIn };
		sl::Extent extentOut{ 0, 0, eyeWidthOut, eyeHeightOut };
		auto presentStretchFallback = [&]() {
			bool stretched = true;
			for (uint32_t i = 0; i < 2; ++i)
				stretched = upscaling.StretchSubmitStageEyeOutput(i, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut) && stretched;
			if (stretched)
				upscaling.FinalizePerEyeOutputs(colorOut);
			return stretched;
		};

		const bool canUseDirectEye0 =
			perEyeResourcesReady;

		if (!canUseDirectEye0) {
			bool allEvaluated = true;
			for (uint32_t i = 0; i < 2; ++i) {
				sl::ViewportHandle vp = (i == 1) ? viewportRight : viewport;
				const bool eyeEvaluated = EvaluateDLSS(vp, i,
					upscaling.vrIntermediateColorIn[i]->resource.get(), upscaling.vrIntermediateColorOut[i]->resource.get(),
					upscaling.vrIntermediateDepth[i]->resource.get(), upscaling.vrIntermediateMotionVectors[i]->resource.get(),
					upscaling.vrIntermediateReactiveMask[i]->resource.get(), upscaling.vrIntermediateTransparencyMask[i]->resource.get(),
					extentIn, extentOut, eyeWidthOut,
					0.0f,
					0.0f,
					"VR prepared per-eye",
					DLSSViewportRole::FullEye,
					useAuthoritativeExistingProfile,
					authoritativeQualityMode,
					authoritativeDLSSPreset);
				upscaling.RecordVRDLSSFullEyeEvaluation(i, eyeEvaluated);
				allEvaluated &= eyeEvaluated;
			}

			bool fallbackPresented = false;
			if (allEvaluated) {
				upscaling.FinalizePerEyeOutputs(colorOut);
			} else {
				upscaling.RequestHistoryReset();
				fallbackPresented = presentStretchFallback();
				static bool loggedVREvaluateFailure = false;
				static bool loggedVRStretchFallbackFailure = false;
				if (fallbackPresented) {
					if (!loggedVREvaluateFailure) {
						logger::warn("[Streamline] VR DLSS/DLAA evaluate did not complete for both eyes; using full-size stretch fallback for this frame.");
						loggedVREvaluateFailure = true;
					}
				} else if (!loggedVRStretchFallbackFailure) {
					logger::warn("[Streamline] VR DLSS/DLAA evaluate did not complete for both eyes and stretch fallback failed; keeping the current scene texture.");
					loggedVRStretchFallbackFailure = true;
				}
			}
			upscaling.dlssUpscaleOutputInSharpenerTexture = outputToSharpener && (allEvaluated || fallbackPresented);
			return allEvaluated;
		}

		// PreparePerEyeInputs already copied both depth halves into distinct resources
		// before either evaluation. Eye 0 only writes color, so the prepared right-eye
		// depth remains stable and does not need a second full-eye copy here.
		const bool canRestoreDirectEye0Output =
			!outputToSharpener &&
			upscaling.vrIntermediateColorOut[0] &&
			upscaling.vrIntermediateColorOut[0]->resource;
		if (canRestoreDirectEye0Output) {
			D3D11_BOX leftOutBackup = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
			context->CopySubresourceRegion(upscaling.vrIntermediateColorOut[0]->resource.get(), 0, 0, 0, 0, colorOut, 0, &leftOutBackup);
		}

		// Eye 0 writes directly to combined output.
		const bool leftEvaluated = EvaluateDLSS(viewport, 0,
			upscaling.vrIntermediateColorIn[0]->resource.get(), colorOut,
			depthTexture.texture, upscaling.vrIntermediateMotionVectors[0]->resource.get(),
			upscaling.vrIntermediateReactiveMask[0]->resource.get(), upscaling.vrIntermediateTransparencyMask[0]->resource.get(),
			extentIn, extentOut, eyeWidthOut,
			0.0f,
			0.0f,
			"VR direct eye0 combined",
			DLSSViewportRole::FullEye,
			useAuthoritativeExistingProfile,
			authoritativeQualityMode,
			authoritativeDLSSPreset);
		upscaling.RecordVRDLSSFullEyeEvaluation(0, leftEvaluated);

		// Eye 1 writes to intermediate, then copy into right half of combined output.
		const bool rightEvaluated = EvaluateDLSS(viewportRight, 1,
			upscaling.vrIntermediateColorIn[1]->resource.get(), upscaling.vrIntermediateColorOut[1]->resource.get(),
			upscaling.vrIntermediateDepth[1]->resource.get(), upscaling.vrIntermediateMotionVectors[1]->resource.get(),
			upscaling.vrIntermediateReactiveMask[1]->resource.get(), upscaling.vrIntermediateTransparencyMask[1]->resource.get(),
			extentIn, extentOut, eyeWidthOut,
			0.0f,
			0.0f,
			"VR direct eye1 intermediate",
			DLSSViewportRole::FullEye,
			useAuthoritativeExistingProfile,
			authoritativeQualityMode,
			authoritativeDLSSPreset);
		upscaling.RecordVRDLSSFullEyeEvaluation(1, rightEvaluated);

		if (leftEvaluated && rightEvaluated) {
			if (depthTexture.depthSRV) {
				upscaling.ClearVRDirectUpscaledEyeOutput(0, colorOutUAV, depthTexture.depthSRV, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut);
				upscaling.ClearVRDirectUpscaledEyeOutput(1, upscaling.vrIntermediateColorOut[1]->uav.get(), depthTexture.depthSRV, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut);
			}

			D3D11_BOX rightOut = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
			context->CopySubresourceRegion(colorOut, 0, eyeWidthOut, 0, 0, upscaling.vrIntermediateColorOut[1]->resource.get(), 0, &rightOut);
		}

		bool fallbackPresented = false;
		if (!leftEvaluated || !rightEvaluated) {
			upscaling.RequestHistoryReset();
			fallbackPresented = presentStretchFallback();
			static bool loggedVRDirectEvaluateFailure = false;
			static bool loggedVRDirectStretchFallbackFailure = false;
			if (fallbackPresented) {
				if (!loggedVRDirectEvaluateFailure) {
					logger::warn("[Streamline] VR DLSS/DLAA direct-eye evaluate failed; using full-size stretch fallback for this frame.");
					loggedVRDirectEvaluateFailure = true;
				}
			} else if (!loggedVRDirectStretchFallbackFailure) {
				logger::warn("[Streamline] VR DLSS/DLAA direct-eye evaluate failed and stretch fallback failed; keeping the current scene texture.");
				loggedVRDirectStretchFallbackFailure = true;
			}
			if (!fallbackPresented && canRestoreDirectEye0Output) {
				D3D11_BOX leftOutBackup = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
				context->CopySubresourceRegion(colorOut, 0, 0, 0, 0, upscaling.vrIntermediateColorOut[0]->resource.get(), 0, &leftOutBackup);
			}
		}

		upscaling.dlssUpscaleOutputInSharpenerTexture = outputToSharpener && ((leftEvaluated && rightEvaluated) || fallbackPresented);
		return leftEvaluated && rightEvaluated;

	} else {
		// Non-VR: Simple full-texture upscale
		sl::Extent extentIn{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
		sl::Extent extentOut{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

		const bool evaluated = EvaluateDLSS(viewport, 0,
			a_upscalingTexture, colorOut,
			depthTexture.texture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask,
			extentIn, extentOut, (uint)screenSize.x,
			0.0f,
			0.0f,
			"Non-VR main");
		upscaling.dlssUpscaleOutputInSharpenerTexture = outputToSharpener && evaluated;
		if (!evaluated) {
			upscaling.RequestHistoryReset();
			static bool loggedEvaluateFailure = false;
			if (!loggedEvaluateFailure) {
				logger::warn("[Streamline] DLSS/DLAA evaluate failed; keeping the current scene texture instead of sharpening stale output.");
				loggedEvaluateFailure = true;
			}
		}
		return evaluated;
	}
}
/**
 * @brief Releases DLSS resources and disables DLSS for the current viewport.
 *
 * Sets the DLSS mode to off and frees all DLSS-related resources associated with the viewport.
 */
Streamline::DLSSResourceTeardownResult Streamline::DestroyDLSSResources()
{
	const bool hasTrackedViewportOwnership = [&]() {
		if (activeDLSSViewportResourcesAllocated[0] ||
			activeDLSSViewportResourcesAllocated[1] ||
			nonVRDLSSOptionsCache.valid) {
			return true;
		}
		for (const auto& roleSlots : vrDLSSViewportSlots) {
			for (const auto& slot : roleSlots) {
				if (slot.valid ||
					slot.resourcesAllocated[0] ||
					slot.resourcesAllocated[1] ||
					slot.optionsCache[0].valid ||
					slot.optionsCache[1].valid) {
					return true;
				}
			}
		}
		return false;
	}();
	if (!initialized || !featureDLSS || !slDLSSSetOptions || !slFreeResources) {
		ResetDLSSIdleFences();
		if (hasTrackedViewportOwnership) {
			static bool loggedUnavailableTeardownOwnership = false;
			if (!loggedUnavailableTeardownOwnership) {
				logger::error("[Streamline] Refusing to report DLSS teardown complete while tracked viewport ownership cannot be released.");
				loggedUnavailableTeardownOwnership = true;
			}
			return DLSSResourceTeardownResult::Failed;
		}
		InvalidateDLSSOptionsCache();
		activeDLSSViewportResourcesAllocated = {};
		ResetFrameTracking();
		return DLSSResourceTeardownResult::Ready;
	}

	if (auto context = globals::d3d::context) {
		const auto idleFenceResult = BeginOrPollD3D11IdleFence(context, pendingDLSSResourceFreeIdleFence, "DLSS resource free");
		if (idleFenceResult == D3D11IdleFenceResult::Pending) {
			static bool loggedDLSSResourceFreePending = false;
			if (!loggedDLSSResourceFreePending) {
				logger::warn("[Streamline] Deferring DLSS resource free because the D3D11 queue did not become idle.");
				loggedDLSSResourceFreePending = true;
			}
			return DLSSResourceTeardownResult::Pending;
		}
		if (idleFenceResult == D3D11IdleFenceResult::Failed)
			return DLSSResourceTeardownResult::Failed;
	} else {
		ResetDLSSIdleFences();
	}

	bool activeViewportResourcesFreed = true;
	if (activeDLSSViewportResourcesAllocated[0]) {
		const bool leftFreed = FreeDLSSViewportResources(viewport, 0, true);
		activeViewportResourcesFreed = leftFreed && activeViewportResourcesFreed;
		if (leftFreed)
			activeDLSSViewportResourcesAllocated[0] = false;
	}

	if (globals::game::isVR) {
		if (activeDLSSViewportResourcesAllocated[1]) {
			const bool rightFreed = FreeDLSSViewportResources(viewportRight, 1, true);
			activeViewportResourcesFreed = rightFreed && activeViewportResourcesFreed;
			if (rightFreed)
				activeDLSSViewportResourcesAllocated[1] = false;
		}
		for (uint32_t roleIndex = 0; roleIndex < kVRDLSSViewportRoleCount; ++roleIndex) {
			for (uint32_t slotIndex = 0; slotIndex < kVRDLSSViewportSlotCount; ++slotIndex) {
				const bool slotFreed = FreeVRDLSSViewportSlot(static_cast<DLSSViewportRole>(roleIndex), slotIndex, false);
				activeViewportResourcesFreed = slotFreed && activeViewportResourcesFreed;
			}
		}
	}

	ResetDLSSIdleFences();
	InvalidateDLSSOptionsCache();
	vrDLSSViewportUseCounter = 0;
	ResetFrameTracking();
	return activeViewportResourcesFreed ?
	           DLSSResourceTeardownResult::Ready :
	           DLSSResourceTeardownResult::FailedAfterMutation;
}

bool Streamline::EnsureReflexDisabledForFrameGeneration()
{
	if (!initialized || !reflexSupportedOnCurrentAdapter || !featureReflex || !slReflexSetOptions)
		return true;
	if (frameGenerationQuarantinedByReflex.load(std::memory_order_acquire))
		return false;

	const bool reflexAlreadyOff = reflexOptionsCache.valid &&
	                              reflexOptionsCache.mode == sl::ReflexMode::eOff &&
	                              reflexOptionsCache.frameLimitUs == 0 &&
	                              !reflexOptionsCache.useMarkersToOptimize;
	if (reflexAlreadyOff)
		return true;

	sl::ReflexOptions disableOptions{};
	disableOptions.mode = sl::ReflexMode::eOff;
	disableOptions.frameLimitUs = 0;
	disableOptions.useMarkersToOptimize = false;
	if (SL_FAILED(result, slReflexSetOptions(disableOptions))) {
		frameGenerationQuarantinedByReflex.store(true, std::memory_order_release);
		logger::error(
			"[Streamline] Failed to disable Reflex before Frame Generation: {}. Frame Generation is quarantined until restart.",
			magic_enum::enum_name(result));
		return false;
	}

	reflexOptionsCache.valid = true;
	reflexOptionsCache.mode = disableOptions.mode;
	reflexOptionsCache.frameLimitUs = disableOptions.frameLimitUs;
	reflexOptionsCache.useMarkersToOptimize = disableOptions.useMarkersToOptimize;
	return true;
}

void Streamline::UpdateReflex()
{
	if (!initialized || !reflexSupportedOnCurrentAdapter || !featureReflex || !slReflexSetOptions)
		return;

	const auto& upscaling = globals::features::upscaling;
	const bool reflexBlockedByFrameGeneration = upscaling.IsFrameGenerationDx12PathActive();
	if (reflexBlockedByFrameGeneration) {
		(void)EnsureReflexDisabledForFrameGeneration();
		lastReflexSleepFrame = UINT32_MAX;
		return;
	}

	auto& settings = globals::features::upscaling.settings;

	sl::ReflexOptions options{};
	if (!settings.reflexLowLatencyMode) {
		options.mode = sl::ReflexMode::eOff;
	} else {
		options.mode = settings.reflexLowLatencyBoost ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency;
	}

	const float originalReflexFPSLimit = settings.reflexFPSLimit;
	float reflexFPSLimit = originalReflexFPSLimit;
	if (!std::isfinite(reflexFPSLimit)) {
		reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = reflexFPSLimit;
		logger::warn("[Streamline] reflexFPSLimit is not finite ({}), using {}", originalReflexFPSLimit, reflexFPSLimit);
	}
	const float fpsLimit = std::clamp(reflexFPSLimit, 20.0f, 240.0f);
	options.frameLimitUs = settings.reflexUseFPSLimit ? static_cast<uint32_t>(std::lround(1000000.0 / static_cast<double>(fpsLimit))) : 0u;
	const auto markerOptimization = ReflexPolicy::ResolveCSXMarkerOptimization(
		featureReflex,
		featurePCL,
		settings.reflexUseMarkersToOptimize);
	options.useMarkersToOptimize = markerOptimization.enabled;

	if (!reflexOptionsCache.valid ||
		reflexOptionsCache.mode != options.mode ||
		reflexOptionsCache.frameLimitUs != options.frameLimitUs ||
		reflexOptionsCache.useMarkersToOptimize != options.useMarkersToOptimize) {
		if (SL_FAILED(result, slReflexSetOptions(options))) {
			logger::error("[Streamline] Failed to apply Reflex options: {}", magic_enum::enum_name(result));
		} else {
			reflexOptionsCache.valid = true;
			reflexOptionsCache.mode = options.mode;
			reflexOptionsCache.frameLimitUs = options.frameLimitUs;
			reflexOptionsCache.useMarkersToOptimize = options.useMarkersToOptimize;
			logger::info(
				"[Streamline] Applied Reflex options: mode={} frameLimitUs={} markersRequested={} markersAvailable={} markersEffective={}",
				magic_enum::enum_name(options.mode),
				options.frameLimitUs,
				settings.reflexUseMarkersToOptimize,
				markerOptimization.available,
				options.useMarkersToOptimize);
		}
	}

	if (!slReflexSleep)
		return;

	if (options.mode == sl::ReflexMode::eOff && options.frameLimitUs == 0)
		return;

	const uint32_t currentFrame = globals::state ? globals::state->frameCount : 0;
	if (lastReflexSleepFrame == currentFrame)
		return;

	const auto frameTokenSnapshot = AcquireFrameToken(currentFrame, "reflex");
	if (!frameTokenSnapshot)
		return;

	lastReflexSleepFrame = currentFrame;
	if (SL_FAILED(result, slReflexSleep(*frameTokenSnapshot->token))) {
		logger::warn("[Streamline] Reflex sleep call failed: {}", magic_enum::enum_name(result));
	}
}
