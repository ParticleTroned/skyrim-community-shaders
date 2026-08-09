#include "FidelityFX.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <directx/d3dx12.h>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <vector>

#include "../../ShaderCache.h"
#include "../../State.h"
#include "../../Utils/FileSystem.h"
#include "../HDRDisplay.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

ffxFunctions ffxModule;

std::vector<std::pair<std::string, std::string>> FidelityFX::dllVersions = {};

FidelityFX::~FidelityFX()
{
	ResetFSRIdleFence();
}

namespace
{
	constexpr wchar_t kFrameGenerationDllName[] = L"amd_fidelityfx_framegeneration_dx12.dll";
	constexpr wchar_t kLoaderDllName[] = L"amd_fidelityfx_loader_dx12.dll";
	constexpr uint32_t kAmdVendorId = 0x1002u;
	constexpr uint32_t kNvidiaVendorId = 0x10DEu;

	void* s_fidelityFxDllDirectoryCookie = nullptr;

	void ReleaseD3D11IdleFence(ID3D11Query*& a_query)
	{
		if (!a_query)
			return;

		a_query->Release();
		a_query = nullptr;
	}

	FidelityFX::LifecycleResult BeginOrPollD3D11IdleFence(ID3D11DeviceContext* a_context, ID3D11Query*& a_query, const char* a_reason)
	{
		if (!a_context) {
			ReleaseD3D11IdleFence(a_query);
			return FidelityFX::LifecycleResult::Failed;
		}

		const auto pollFence = [&]() {
			BOOL completed = FALSE;
			const HRESULT result = a_context->GetData(a_query, &completed, sizeof(completed), 0);
			if (result == S_OK && completed) {
				ReleaseD3D11IdleFence(a_query);
				return FidelityFX::LifecycleResult::Ready;
			}
			if (result == S_FALSE || result == S_OK)
				return FidelityFX::LifecycleResult::Pending;

			logger::debug("[FidelityFX] D3D11 idle fence poll failed before {}: 0x{:08X}", a_reason, static_cast<uint32_t>(result));
			ReleaseD3D11IdleFence(a_query);
			return FidelityFX::LifecycleResult::Failed;
		};

		if (a_query)
			return pollFence();

		ID3D11Device* device = nullptr;
		a_context->GetDevice(&device);
		if (!device)
			return FidelityFX::LifecycleResult::Failed;

		D3D11_QUERY_DESC queryDesc{};
		queryDesc.Query = D3D11_QUERY_EVENT;
		const HRESULT createResult = device->CreateQuery(&queryDesc, &a_query);
		device->Release();
		if (FAILED(createResult) || !a_query) {
			logger::debug("[FidelityFX] D3D11 idle fence creation failed before {}: 0x{:08X}", a_reason, static_cast<uint32_t>(createResult));
			return FidelityFX::LifecycleResult::Failed;
		}

		a_context->End(a_query);
		a_context->Flush();
		return pollFence();
	}

	bool IsD3DDeviceLossReason(HRESULT a_result)
	{
		return a_result == DXGI_ERROR_DEVICE_REMOVED ||
		       a_result == DXGI_ERROR_DEVICE_RESET ||
		       a_result == DXGI_ERROR_DEVICE_HUNG ||
		       a_result == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
		       a_result == DXGI_ERROR_INVALID_CALL;
	}

	constexpr bool IsTerminalRuntimeQuarantineResult(FidelityFX::LifecycleResult a_result) noexcept
	{
		return a_result == FidelityFX::LifecycleResult::Failed ||
		       a_result == FidelityFX::LifecycleResult::DeviceLost ||
		       a_result == FidelityFX::LifecycleResult::RuntimeDeviceLost;
	}

	constexpr FidelityFX::LifecycleResult NormalizeRuntimeQuarantineResult(FidelityFX::LifecycleResult a_result) noexcept
	{
		return a_result == FidelityFX::LifecycleResult::DeviceLost ||
		               a_result == FidelityFX::LifecycleResult::RuntimeDeviceLost ?
		           a_result :
		           FidelityFX::LifecycleResult::Failed;
	}

	std::string GetFidelityFxPathText(const std::filesystem::path& a_path)
	{
		return stl::utf16_to_utf8(a_path.wstring()).value_or("<unprintable path>");
	}

	bool TryGetTexture2DDesc(ID3D11Resource* a_resource, D3D11_TEXTURE2D_DESC& a_outDesc)
	{
		if (!a_resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
			return false;

		texture->GetDesc(&a_outDesc);
		return true;
	}

	bool TryGetCurrentAdapterDesc(DXGI_ADAPTER_DESC& a_outDesc)
	{
		if (!globals::d3d::device)
			return false;

		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (FAILED(globals::d3d::device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()))))
			return false;

		winrt::com_ptr<IDXGIAdapter> adapter;
		if (FAILED(dxgiDevice->GetAdapter(adapter.put())))
			return false;

		a_outDesc = {};
		if (FAILED(adapter->GetDesc(&a_outDesc)))
			return false;

		return true;
	}

	std::string ToUpperAscii(std::string a_value)
	{
		std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char c) {
			return static_cast<char>(std::toupper(c));
		});
		return a_value;
	}

	FidelityFX::Fsr4AdapterSupport ClassifyFsr4AdapterSupport(const DXGI_ADAPTER_DESC& a_desc)
	{
		if (a_desc.VendorId != kAmdVendorId)
			return FidelityFX::Fsr4AdapterSupport::Unsupported;

		std::wstring wideDescription(a_desc.Description);
		const std::string description = ToUpperAscii(stl::utf16_to_utf8(wideDescription).value_or(""));
		if (description.find("RADEON HD") != std::string::npos)
			return FidelityFX::Fsr4AdapterSupport::Unsupported;

		// FSR 4.1.1 supports discrete RDNA 3/RX 7000 and RDNA 4/RX 9000
		// adapters. A generic RDNA 3 marker is insufficient because it can also
		// describe unsupported integrated hardware.
		const bool isKnownRdna3DiscreteDie =
			description.find("NAVI31") != std::string::npos ||
			description.find("NAVI 31") != std::string::npos ||
			description.find("NAVI32") != std::string::npos ||
			description.find("NAVI 32") != std::string::npos ||
			description.find("NAVI33") != std::string::npos ||
			description.find("NAVI 33") != std::string::npos;
		if (isKnownRdna3DiscreteDie)
			return FidelityFX::Fsr4AdapterSupport::RadeonRx7000;

		if (description.find("RDNA4") != std::string::npos ||
			description.find("RDNA 4") != std::string::npos ||
			description.find("NAVI4") != std::string::npos ||
			description.find("NAVI 4") != std::string::npos) {
			return FidelityFX::Fsr4AdapterSupport::RadeonRx9000;
		}

		size_t searchPosition = 0;
		while (searchPosition < description.length()) {
			while (searchPosition < description.length() && !std::isdigit(static_cast<unsigned char>(description[searchPosition])))
				searchPosition++;

			const size_t modelStart = searchPosition;
			while (searchPosition < description.length() && std::isdigit(static_cast<unsigned char>(description[searchPosition])))
				searchPosition++;

			if (searchPosition > modelStart) {
				const std::string modelText = description.substr(modelStart, searchPosition - modelStart);
				char* parseEnd = nullptr;
				const unsigned long modelNumber = std::strtoul(modelText.c_str(), &parseEnd, 10);
				const bool isRadeon7000Series = modelNumber >= 7000ul && modelNumber < 8000ul;
				const bool isRadeon9000Series = modelNumber >= 9000ul && modelNumber < 10000ul;
				if (parseEnd != modelText.c_str() && isRadeon7000Series)
					return FidelityFX::Fsr4AdapterSupport::RadeonRx7000;
				if (parseEnd != modelText.c_str() && isRadeon9000Series)
					return FidelityFX::Fsr4AdapterSupport::RadeonRx9000;
			}
		}

		// Keep fallbacks for abbreviated naming variants that don't include full numeric model text.
		if (description.find("RX 70") != std::string::npos ||
			description.find("RX70") != std::string::npos ||
			description.find("RADEON 70") != std::string::npos) {
			return FidelityFX::Fsr4AdapterSupport::RadeonRx7000;
		}
		if (description.find("RX 90") != std::string::npos ||
			description.find("RX90") != std::string::npos ||
			description.find("RADEON 90") != std::string::npos) {
			return FidelityFX::Fsr4AdapterSupport::RadeonRx9000;
		}

		return FidelityFX::Fsr4AdapterSupport::Unsupported;
	}

	std::string UpscalerVersionToString(uint32_t a_version)
	{
		const uint32_t major = (a_version >> 22) & 0x3FFu;
		const uint32_t minor = (a_version >> 12) & 0x3FFu;
		const uint32_t patch = a_version & 0xFFFu;
		return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
	}

	const std::string& PendingFsrDispatchLabel()
	{
		static const std::string label = "Pending FSR dispatch";
		return label;
	}

	const std::string& HostFsrFallbackLabel()
	{
		static const std::string label = std::format("{} fallback", FidelityFX::GetHostFsrSdkLabel());
		return label;
	}

	bool RuntimeProviderNameMatchesVersion(const std::string& a_providerName, uint32_t a_version)
	{
		return !a_providerName.empty() &&
		       a_providerName.find(UpscalerVersionToString(a_version)) != std::string::npos;
	}

	bool RuntimeProviderIdMatchesVersion(uint64_t a_providerId, uint32_t a_version)
	{
		return a_providerId != 0 &&
		       static_cast<uint32_t>(a_providerId & 0xFFFFFFFFull) == a_version;
	}

	bool RuntimeProviderMatchesVersion(uint64_t a_providerId, const std::string& a_providerName, uint32_t a_version)
	{
		return RuntimeProviderIdMatchesVersion(a_providerId, a_version) ||
		       RuntimeProviderNameMatchesVersion(a_providerName, a_version);
	}

	std::string RuntimeProviderDisplayName(uint64_t a_providerId, const std::string& a_providerName)
	{
		if (!a_providerName.empty())
			return a_providerName;
		if (a_providerId != 0)
			return std::format("id 0x{:X}", a_providerId);

		return {};
	}

	void RuntimeFfxMessage(uint32_t a_type, const wchar_t* a_message)
	{
		const std::string message = stl::utf16_to_utf8(a_message ? a_message : L"").value_or("unknown FidelityFX runtime message");
		if (a_type == FFX_API_MESSAGE_TYPE_ERROR) {
			logger::error("[FidelityFX] {}", message);
		} else {
			logger::warn("[FidelityFX] {}", message);
		}
	}

	void EnsureFidelityFxDllDirectory(const std::filesystem::path& a_pluginDir)
	{
		if (s_fidelityFxDllDirectoryCookie) {
			return;
		}

		auto kernel32 = GetModuleHandleW(L"kernel32.dll");
		if (!kernel32) {
			logger::warn("[FidelityFX] Could not get kernel32 module while preparing FidelityFX DLL search path");
			return;
		}

		using AddDllDirectoryFn = void*(WINAPI*)(PCWSTR);
		auto addDllDirectory = reinterpret_cast<AddDllDirectoryFn>(GetProcAddress(kernel32, "AddDllDirectory"));
		if (!addDllDirectory) {
			logger::warn("[FidelityFX] AddDllDirectory is unavailable; FidelityFX provider discovery will rely on explicit DLL loads");
			return;
		}

		s_fidelityFxDllDirectoryCookie = addDllDirectory(a_pluginDir.c_str());
		if (!s_fidelityFxDllDirectoryCookie) {
			logger::warn(
				"[FidelityFX] Failed to add FidelityFX DLL directory {} (error {})",
				stl::utf16_to_utf8(a_pluginDir.wstring()).value_or("<unknown>"),
				GetLastError());
		}
	}

	HMODULE LoadFidelityFxDll(const std::filesystem::path& a_path, DWORD& a_error)
	{
		a_error = ERROR_SUCCESS;

		constexpr DWORD kLoadFlags =
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
			LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
			LOAD_LIBRARY_SEARCH_USER_DIRS;

		auto module = LoadLibraryExW(a_path.c_str(), nullptr, kLoadFlags);
		if (!module)
			a_error = GetLastError();
		return module;
	}

	bool FidelityFxDllExists(const std::filesystem::path& a_path, std::error_code& a_error)
	{
		a_error.clear();
		return std::filesystem::is_regular_file(a_path, a_error);
	}

	ffxReturnCode_t CreateRuntimeUpscalerContextProtected(
		ffx::Context* a_context,
		ffxCreateContextDescHeader* a_desc,
		bool& a_crashed)
	{
		a_crashed = false;
		ffxReturnCode_t result = FFX_API_RETURN_ERROR;
		__try {
			if (ffxModule.CreateContext)
				result = ffxModule.CreateContext(a_context, a_desc, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	ffxReturnCode_t DestroyRuntimeUpscalerContextProtected(ffx::Context* a_context, bool& a_crashed)
	{
		a_crashed = false;
		ffxReturnCode_t result = FFX_API_RETURN_ERROR;
		__try {
			if (ffxModule.DestroyContext)
				result = ffxModule.DestroyContext(a_context, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	ffxReturnCode_t QueryRuntimeUpscalerProtected(ffx::Context* a_context, ffxQueryDescHeader* a_desc, bool& a_crashed)
	{
		a_crashed = false;
		ffxReturnCode_t result = FFX_API_RETURN_ERROR;
		__try {
			if (ffxModule.Query)
				result = ffxModule.Query(a_context, a_desc);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	bool QueryRuntimeUpscalerVersionId(ID3D12Device* a_device, uint32_t a_requestedVersion, uint64_t& a_versionId, std::string& a_versionName, bool& a_callCrashed)
	{
		a_versionId = 0;
		a_versionName.clear();
		a_callCrashed = false;

		if (!a_device || !ffxModule.Query) {
			return false;
		}

		uint64_t versionCount = 0;
		ffxQueryDescGetVersions countQuery{};
		countQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
		countQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
		countQuery.device = a_device;
		countQuery.outputCount = &versionCount;

		bool queryCrashed = false;
		auto countResult = QueryRuntimeUpscalerProtected(nullptr, &countQuery.header, queryCrashed);
		if (queryCrashed) {
			a_callCrashed = true;
			logger::critical("[FidelityFX] Runtime upscaler version-count query faulted.");
			return false;
		}
		if (countResult != FFX_API_RETURN_OK || versionCount == 0) {
			logger::warn(
				"[FidelityFX] Runtime upscaler version query failed or returned no versions (code {}, count {})",
				static_cast<uint32_t>(countResult),
				versionCount);
			return false;
		}

		std::vector<uint64_t> versionIds(versionCount, 0);
		std::vector<const char*> versionNames(versionCount, nullptr);

		uint64_t returnedVersionCount = versionCount;
		ffxQueryDescGetVersions versionsQuery{};
		versionsQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
		versionsQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
		versionsQuery.device = a_device;
		versionsQuery.outputCount = &returnedVersionCount;
		versionsQuery.versionIds = versionIds.data();
		versionsQuery.versionNames = versionNames.data();

		auto versionsResult = QueryRuntimeUpscalerProtected(nullptr, &versionsQuery.header, queryCrashed);
		if (queryCrashed) {
			a_callCrashed = true;
			logger::critical("[FidelityFX] Runtime upscaler version enumeration faulted.");
			return false;
		}
		if (versionsResult != FFX_API_RETURN_OK) {
			logger::warn("[FidelityFX] Runtime upscaler version enumeration failed with code {}", static_cast<uint32_t>(versionsResult));
			return false;
		}

		const auto requestedVersion = UpscalerVersionToString(a_requestedVersion);
		const auto resultCount = std::min<size_t>(versionIds.size(), returnedVersionCount);

		logger::debug(
			"[FidelityFX] Runtime upscaler reported {} version provider(s); requested FSR version {}",
			resultCount,
			requestedVersion);

		for (size_t i = 0; i < resultCount; ++i) {
			const std::string versionName = versionNames[i] ? versionNames[i] : "";
			logger::debug(
				"[FidelityFX] Runtime upscaler version provider {}: '{}' (id 0x{:X})",
				i,
				versionName.empty() ? "(unnamed)" : versionName,
				versionIds[i]);

			if (versionIds[i] == a_requestedVersion ||
				RuntimeProviderIdMatchesVersion(versionIds[i], a_requestedVersion) ||
				RuntimeProviderNameMatchesVersion(versionName, a_requestedVersion)) {
				a_versionId = versionIds[i];
				a_versionName = versionName;
				return true;
			}
		}

		logger::warn(
			"[FidelityFX] Runtime upscaler did not report requested FSR version {}; falling back to upscaler version descriptor",
			requestedVersion);
		return false;
	}

	std::string FfxCreateResultText(bool a_attempted, ffxReturnCode_t a_result)
	{
		if (!a_attempted) {
			return "not attempted";
		}

		return std::format("code {}", static_cast<uint32_t>(a_result));
	}

	enum class RuntimeUpscalerCreateAttempt : uint8_t
	{
		kGenericOverrideWithUpscalerVersion,
		kUpscalerVersionDescriptor,
		kDefaultProvider
	};

	struct RuntimeUpscalerCreateAttemptResult
	{
		RuntimeUpscalerCreateAttempt attempt;
		bool enabled;
		bool attempted = false;
		ffxReturnCode_t result = FFX_API_RETURN_ERROR;
	};

	ffxReturnCode_t TryCreateRuntimeUpscalerContext(
		ffx::Context& a_context,
		RuntimeUpscalerCreateAttempt a_attempt,
		ffx::CreateContextDescUpscale& a_createDesc,
		ffx::CreateBackendDX12Desc& a_backendDesc,
		ffx::CreateContextDescUpscaleVersion& a_versionDesc,
		ffx::CreateContextDescOverrideVersion& a_overrideVersionDesc,
		bool& a_callCrashed,
		bool& a_ownershipIndeterminate)
	{
		a_callCrashed = false;
		a_ownershipIndeterminate = false;
		// Never overwrite a provider handle whose destruction previously failed.
		// Losing that handle would make later GPU-idle retirement impossible.
		if (a_context)
			return FFX_API_RETURN_ERROR;
		a_createDesc.header.pNext = nullptr;
		a_backendDesc.header.pNext = nullptr;
		a_versionDesc.header.pNext = nullptr;
		a_overrideVersionDesc.header.pNext = nullptr;

		switch (a_attempt) {
		case RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion:
			a_createDesc.header.pNext = &a_backendDesc.header;
			a_backendDesc.header.pNext = &a_versionDesc.header;
			a_versionDesc.header.pNext = &a_overrideVersionDesc.header;
			break;
		case RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor:
			a_createDesc.header.pNext = &a_versionDesc.header;
			a_versionDesc.header.pNext = &a_backendDesc.header;
			break;
		case RuntimeUpscalerCreateAttempt::kDefaultProvider:
			a_createDesc.header.pNext = &a_backendDesc.header;
			break;
		}

		ffx::Context createdContext = nullptr;
		const auto result = CreateRuntimeUpscalerContextProtected(&createdContext, &a_createDesc.header, a_callCrashed);
		if (a_callCrashed) {
			a_context = createdContext;
			a_ownershipIndeterminate = true;
			return FFX_API_RETURN_ERROR;
		}
		if (result == FFX_API_RETURN_OK) {
			if (createdContext) {
				a_context = createdContext;
				return result;
			}

			a_ownershipIndeterminate = true;
			return FFX_API_RETURN_ERROR;
		} else if (createdContext) {
			const auto retainedContext = createdContext;
			bool destroyCrashed = false;
			if (DestroyRuntimeUpscalerContextProtected(&createdContext, destroyCrashed) != FFX_API_RETURN_OK || destroyCrashed) {
				// A provider may return a live handle together with an error. Retain
				// it if cleanup fails instead of silently orphaning vendor state.
				a_context = retainedContext;
				a_ownershipIndeterminate = true;
			}
		}

		return result;
	}

	D3D11_TEXTURE2D_DESC MakeSharedTextureDesc(const D3D11_TEXTURE2D_DESC& a_sourceDesc, uint32_t a_width, uint32_t a_height, UINT a_bindFlags)
	{
		D3D11_TEXTURE2D_DESC desc = a_sourceDesc;
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.BindFlags = a_bindFlags;
		desc.MiscFlags = 0;
		return desc;
	}

	bool SameTextureDesc(const D3D11_TEXTURE2D_DESC& a_left, const D3D11_TEXTURE2D_DESC& a_right)
	{
		return a_left.Width == a_right.Width &&
		       a_left.Height == a_right.Height &&
		       a_left.MipLevels == a_right.MipLevels &&
		       a_left.ArraySize == a_right.ArraySize &&
		       a_left.Format == a_right.Format &&
		       a_left.SampleDesc.Count == a_right.SampleDesc.Count &&
		       a_left.SampleDesc.Quality == a_right.SampleDesc.Quality &&
		       a_left.Usage == a_right.Usage &&
		       a_left.BindFlags == a_right.BindFlags &&
		       a_left.CPUAccessFlags == a_right.CPUAccessFlags &&
		       a_left.MiscFlags == a_right.MiscFlags;
	}

	template <size_t N>
	void ResetWrappedResourceArray(std::unique_ptr<WrappedResource> (&a_resources)[N])
	{
		for (auto& resource : a_resources)
			resource.reset();
	}

	FfxErrorCode CreateHostFsr3ContextProtected(
		FfxFsr3Context* a_context,
		FfxFsr3ContextDescription* a_description,
		bool& a_crashed)
	{
		a_crashed = false;
		FfxErrorCode result = FFX_ERROR_BACKEND_API_ERROR;
		__try {
			result = ffxFsr3ContextCreate(a_context, a_description);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	FfxErrorCode GetHostFsr3InterfaceProtected(
		FfxInterface* a_interface,
		FfxDevice a_device,
		void* a_scratchBuffer,
		size_t a_scratchBufferSize,
		uint32_t a_contextCount,
		bool& a_crashed)
	{
		a_crashed = false;
		FfxErrorCode result = FFX_ERROR_BACKEND_API_ERROR;
		__try {
			result = ffxGetInterfaceDX11(a_interface, a_device, a_scratchBuffer, a_scratchBufferSize, a_contextCount);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	FfxErrorCode DestroyHostFsr3ContextProtected(FfxFsr3Context* a_context, bool& a_crashed)
	{
		a_crashed = false;
		FfxErrorCode result = FFX_ERROR_BACKEND_API_ERROR;
		__try {
			result = ffxFsr3ContextDestroy(a_context);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
		}
		return result;
	}

	bool DispatchHostFsr3UpscaleProtected(FfxFsr3Context& a_context, FfxFsr3DispatchUpscaleDescription& a_dispatchParameters, bool& a_crashed)
	{
		a_crashed = false;
		bool dispatchOk = true;

		__try {
			dispatchOk = ffxFsr3ContextDispatchUpscale(&a_context, &a_dispatchParameters) == FFX_OK;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_crashed = true;
			dispatchOk = false;
		}

		return dispatchOk;
	}

	bool DispatchRuntimeUpscalerProtected(ffx::Context& a_context, ffx::DispatchDescUpscale& a_dispatchParameters, bool& a_faulted)
	{
		a_faulted = false;
		bool dispatchOk = true;

		__try {
			dispatchOk = ffx::Dispatch(a_context, a_dispatchParameters) == ffx::ReturnCode::Ok;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			a_faulted = true;
			dispatchOk = false;
		}

		return dispatchOk;
	}

	struct FsrSharpeningSettings
	{
		bool enabled = false;
		float sharpness = 0.0f;
	};

	FsrSharpeningSettings ResolveFsrSharpeningSettings(float a_sharpness)
	{
		const float sharpness = std::isfinite(a_sharpness) ? std::clamp(a_sharpness, 0.0f, 1.0f) : 0.0f;
		return { sharpness > 0.0f, sharpness };
	}
}

FidelityFX::LifecycleResult FidelityFX::RecordFSRDeviceStatus() noexcept
{
	auto* device = globals::d3d::device;
	if (!device) {
		return IsD3DDeviceLossReason(fsrLastDeviceRemovedReason) ?
			LifecycleResult::DeviceLost : LifecycleResult::Failed;
	}

	const HRESULT reason = device->GetDeviceRemovedReason();
	if (IsD3DDeviceLossReason(reason))
		fsrLastDeviceRemovedReason = reason;
	else if (!IsD3DDeviceLossReason(fsrLastDeviceRemovedReason))
		fsrLastDeviceRemovedReason = reason;
	return IsD3DDeviceLossReason(fsrLastDeviceRemovedReason) ? LifecycleResult::DeviceLost : LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::RecordRuntimeUpscalerDeviceStatus() noexcept
{
	const auto primaryResult = RecordFSRDeviceStatus();
	if (primaryResult != LifecycleResult::Ready)
		return primaryResult;

	auto* device = globals::features::upscaling.dx12SwapChain.d3d12Device.get();
	if (!device) {
		return IsD3DDeviceLossReason(runtimeUpscalerLastDeviceRemovedReason) ?
			LifecycleResult::RuntimeDeviceLost : LifecycleResult::Failed;
	}

	const HRESULT reason = device->GetDeviceRemovedReason();
	if (IsD3DDeviceLossReason(reason))
		runtimeUpscalerLastDeviceRemovedReason = reason;
	else if (!IsD3DDeviceLossReason(runtimeUpscalerLastDeviceRemovedReason))
		runtimeUpscalerLastDeviceRemovedReason = reason;
	return IsD3DDeviceLossReason(runtimeUpscalerLastDeviceRemovedReason) ? LifecycleResult::RuntimeDeviceLost : LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::ResolveFSRLifecycleFailure(const char* a_operation)
{
	const auto result = RecordFSRDeviceStatus();
	if (result == LifecycleResult::DeviceLost) {
		logger::error("[FidelityFX] D3D11 device removal detected during {}. reason=0x{:08X}",
			a_operation && *a_operation ? a_operation : "FSR lifecycle work",
			static_cast<uint32_t>(fsrLastDeviceRemovedReason));
		return result;
	}
	return LifecycleResult::Failed;
}

FidelityFX::LifecycleResult FidelityFX::ResolveRuntimeUpscalerLifecycleFailure(const char* a_operation)
{
	const auto result = RecordRuntimeUpscalerDeviceStatus();
	if (result == LifecycleResult::DeviceLost) {
		logger::error("[FidelityFX] Primary D3D11 device removal detected during {}. reason=0x{:08X}",
			a_operation && *a_operation ? a_operation : "runtime upscaler lifecycle work",
			static_cast<uint32_t>(fsrLastDeviceRemovedReason));
		return result;
	}
	if (result == LifecycleResult::RuntimeDeviceLost) {
		logger::error("[FidelityFX] Runtime-provider D3D12 device removal detected during {}. reason=0x{:08X}",
			a_operation && *a_operation ? a_operation : "runtime upscaler lifecycle work",
			static_cast<uint32_t>(runtimeUpscalerLastDeviceRemovedReason));
		QuarantineRuntimeUpscalerForSession("runtime-provider D3D12 device removal");
		for (uint32_t i = 0; i < std::size(runtimeUpscalerContexts); ++i) {
			if (runtimeUpscalerContexts[i])
				runtimeUpscalerContextIndeterminate[i] = true;
		}
		runtimeUpscalerQuarantineRetirement = LifecycleResult::RuntimeDeviceLost;
		return result;
	}
	return LifecycleResult::Failed;
}

bool FidelityFX::IsRuntimeUpscalerOwnershipDetached() const noexcept
{
	return runtimeUpscalerSessionQuarantined &&
	       runtimeUpscalerQuarantineRetirement == LifecycleResult::RuntimeDeviceLost;
}

void FidelityFX::QuarantineHostFSRState(const char* a_reason)
{
	if (fsrHostStateQuarantined)
		return;
	fsrHostStateQuarantined = true;
	logger::critical("[FidelityFX] Quarantined indeterminate host FSR state after {}; retained contexts and backend scratch will not be reused or destroyed this session.",
		a_reason && *a_reason ? a_reason : "an unknown lifecycle exception");
}

void FidelityFX::QuarantineHostFSRContext(uint32_t a_contextIndex, const char* a_reason)
{
	if (a_contextIndex < fsrContextValid.size()) {
		fsrContextValid[a_contextIndex] = false;
		fsrContextIndeterminate[a_contextIndex] = true;
	}
	QuarantineHostFSRState(a_reason);
}

FidelityFX::LifecycleResult FidelityFX::GetQuarantinedHostFSRResult(const char* a_operation)
{
	if (!fsrHostStateQuarantined)
		return LifecycleResult::Failed;
	return ResolveFSRLifecycleFailure(a_operation);
}

FidelityFX::LifecycleResult FidelityFX::DestroyTrackedHostFSRContexts(const char* a_operation)
{
	if (fsrHostStateQuarantined)
		return GetQuarantinedHostFSRResult(a_operation);

	for (uint32_t i = 0; i < fsrContextValid.size(); ++i) {
		if (!fsrContextValid[i])
			continue;

		bool crashed = false;
		const auto destroyResult = DestroyHostFsr3ContextProtected(&fsrContext[i], crashed);
		if (!crashed && destroyResult == FFX_OK) {
			fsrContext[i] = {};
			fsrContextValid[i] = false;
			fsrContextIndeterminate[i] = false;
			continue;
		}

		const auto failureResult = ResolveFSRLifecycleFailure(a_operation);
		logger::critical("[FidelityFX] FSR3 context {} destruction {} without proving release; retaining its context and backend scratch memory.",
			i,
			crashed ? "faulted" : "returned an error");
		QuarantineHostFSRContext(i, crashed ? "an FSR3 context destruction fault" : "an FSR3 context destruction error");
		return failureResult;
	}
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::ReleaseHostFSRResources()
{
	const bool anyValid = std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; });
	const bool anyIndeterminate = std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; });
	if (fsrHostStateQuarantined || anyValid || anyIndeterminate) {
		logger::critical("[FidelityFX] Refusing to release quarantined or live host FSR ownership.");
		return GetQuarantinedHostFSRResult("host FSR ownership release");
	}

	for (auto& context : fsrContext)
		context = {};
	fsrContextCount = 0;
	fsrContextMaxRenderWidth = 0;
	fsrContextMaxRenderHeight = 0;
	fsrContextDisplayWidth = 0;
	fsrContextDisplayHeight = 0;
	if (fsrScratchBuffer) {
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
	}
	return LifecycleResult::Ready;
}

void FidelityFX::LoadFFX()
{
	ResetRuntimeUpscalerTracking(true);
	featureFSR3FG = false;
	featureRuntimeUpscaler = false;

	const auto pluginDir = (Util::PathHelpers::GetDataPath() / "Shaders" / "Upscaling" / "FidelityFX").lexically_normal();
	if (!pluginDir.is_absolute()) {
		logger::error("[FidelityFX] Refusing non-absolute DLL directory '{}'", GetFidelityFxPathText(pluginDir));
		return;
	}

	EnsureFidelityFxDllDirectory(pluginDir);

	const std::filesystem::path framegenPath = pluginDir / kFrameGenerationDllName;
	const std::filesystem::path loaderPath = pluginDir / kLoaderDllName;
	const std::filesystem::path upscalerPath = pluginDir / RuntimeUpscalerDllName.data();

	FidelityFX::dllVersions = Util::EnumerateDllVersions(pluginDir);
	for (const auto& [name, versionStr] : FidelityFX::dllVersions)
		logger::info("[FidelityFX] {} version: {}", name, versionStr);

	const auto loadDll = [](const char* a_label, const std::filesystem::path& a_path, HMODULE& a_module) {
		if (a_module) {
			logger::info("[FidelityFX] {} loaded from '{}'", a_label, GetFidelityFxPathText(a_path));
			return true;
		}

		std::error_code fileError;
		if (!FidelityFxDllExists(a_path, fileError)) {
			if (fileError) {
				logger::error("[FidelityFX] Failed to inspect {} at '{}': {}", a_label, GetFidelityFxPathText(a_path), fileError.message());
			} else {
				logger::warn("[FidelityFX] {} is missing at '{}'", a_label, GetFidelityFxPathText(a_path));
			}
			return false;
		}

		DWORD loadError = ERROR_SUCCESS;
		a_module = LoadFidelityFxDll(a_path, loadError);
		if (!a_module) {
			logger::error("[FidelityFX] {} exists but failed to load from '{}' (Win32 error {})", a_label, GetFidelityFxPathText(a_path), loadError);
			return false;
		}

		logger::info("[FidelityFX] {} loaded from '{}'", a_label, GetFidelityFxPathText(a_path));
		return true;
	};

	const bool loaderLoaded = loadDll("Loader DLL", loaderPath, module);
	if (loaderLoaded) {
		ffxModule = {};
		ffxLoadFunctions(&ffxModule, module);
	}

	const bool loaderReady = loaderLoaded &&
	                         ffxModule.CreateContext &&
	                         ffxModule.DestroyContext &&
	                         ffxModule.Configure &&
	                         ffxModule.Query &&
	                         ffxModule.Dispatch;
	if (loaderLoaded && !loaderReady)
		logger::error("[FidelityFX] Loader DLL is missing one or more required API exports at '{}'", GetFidelityFxPathText(loaderPath));

	if (!loaderReady) {
		const auto reportSkippedDll = [](const char* a_label, const std::filesystem::path& a_path) {
			std::error_code fileError;
			if (FidelityFxDllExists(a_path, fileError)) {
				logger::warn("[FidelityFX] {} exists at '{}' but was not loaded because the loader is unavailable", a_label, GetFidelityFxPathText(a_path));
			} else if (fileError) {
				logger::error("[FidelityFX] Failed to inspect {} at '{}': {}", a_label, GetFidelityFxPathText(a_path), fileError.message());
			} else {
				logger::warn("[FidelityFX] {} is missing at '{}'", a_label, GetFidelityFxPathText(a_path));
			}
		};

		reportSkippedDll("Frame generation DLL", framegenPath);
		reportSkippedDll("Runtime upscaler DLL", upscalerPath);
		return;
	}

	featureFSR3FG = loadDll("Frame generation DLL", framegenPath, frameGenerationModule);
	featureRuntimeUpscaler = loadDll("Runtime upscaler DLL", upscalerPath, runtimeUpscalerModule);
}

bool FidelityFX::HasRuntimeUpscalerSupportCheckResult() const
{
	return runtimeUpscalerSupportCheckKnown;
}

bool FidelityFX::IsRuntimeUpscalerSupportConfirmed() const
{
	return runtimeUpscalerSupportCheckKnown && runtimeUpscalerSupportConfirmed;
}

bool FidelityFX::IsRuntimeUpscalerProviderMatchingRequestedVersion() const
{
	if (!runtimeUpscalerSupportCheckKnown || !runtimeUpscalerSupportConfirmed)
		return false;

	if (runtimeUpscalerProviderMatchedVersionName.empty() && runtimeUpscalerProviderMatchedVersionId == 0)
		return false;

	const uint32_t requestedVersion = runtimeUpscalerRequestedVersion ? runtimeUpscalerRequestedVersion : GetPreferredRuntimeUpscalerVersion();
	return RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, requestedVersion);
}

bool FidelityFX::IsRuntimeUpscalerFailureLatched() const
{
	return runtimeUpscalerFailureLatched || runtimeUpscalerSessionQuarantined;
}

bool FidelityFX::IsRuntimeFsr4FailureLatched() const
{
	return runtimeFsr4FailureLatched;
}

const std::string& FidelityFX::GetHostFsrSdkLabel()
{
	static const std::string label = std::format("Host FSR3 SDK {}", UpscalerVersionToString(Fsr3Version));
	return label;
}

const std::string& FidelityFX::GetRuntimeUpscalerLabel(uint32_t a_version)
{
	static const std::string runtimeFsr3Label = std::format("Runtime FSR3 {} ({})", UpscalerVersionToString(Fsr3Version), RuntimeUpscalerDllNameUtf8);
	static const std::string runtimeFsr4Label = std::format("Runtime FSR4 ({})", RuntimeUpscalerDllNameUtf8);

	if (a_version == Fsr3Version)
		return runtimeFsr3Label;
	if (a_version == FFX_UPSCALER_VERSION)
		return runtimeFsr4Label;

	thread_local std::string fallbackLabel;
	fallbackLabel = std::format("Runtime FSR {} ({})", UpscalerVersionToString(a_version), RuntimeUpscalerDllNameUtf8);
	return fallbackLabel;
}

const std::string& FidelityFX::GetRuntimeUpscalerLastFramePathLabel() const
{
	if (!runtimeUpscalerLastFramePathValid)
		return PendingFsrDispatchLabel();

	switch (runtimeUpscalerLastFramePath) {
	case RuntimeUpscalerFramePath::kHostFsr31:
		return GetHostFsrSdkLabel();
	case RuntimeUpscalerFramePath::kRuntimeFsr31:
		return GetRuntimeUpscalerLabel(Fsr3Version);
	case RuntimeUpscalerFramePath::kRuntimeFsr4:
		return GetRuntimeUpscalerLabel(FFX_UPSCALER_VERSION);
	case RuntimeUpscalerFramePath::kHostFsr31Fallback:
		return HostFsrFallbackLabel();
	case RuntimeUpscalerFramePath::kInactive:
	default:
		return PendingFsrDispatchLabel();
	}
}

const std::string& FidelityFX::GetConfiguredFsrPathLabel() const
{
	if (runtimeUpscalerSessionQuarantined || runtimeUpscalerFailureLatched)
		return HostFsrFallbackLabel();

	if (runtimeFsr4FailureLatched) {
		if (ShouldUseRuntimeUpscalerForFSR())
			return GetRuntimeUpscalerLabel(Fsr3Version);

		return HostFsrFallbackLabel();
	}

	if (runtimeUpscalerSupportCheckKnown && !runtimeUpscalerSupportConfirmed)
		return HostFsrFallbackLabel();

	if (ShouldRequestRuntimeFsr4())
		return GetRuntimeUpscalerLabel(FFX_UPSCALER_VERSION);

	if (ShouldUseRuntimeUpscalerForFSR())
		return GetRuntimeUpscalerLabel(Fsr3Version);

	return GetHostFsrSdkLabel();
}

const std::string& FidelityFX::GetDisplayedFsrPathLabel() const
{
	const uint32_t currentFrame = globals::state ? globals::state->frameCount : 0;
	if (runtimeUpscalerLastFramePathValid && runtimeUpscalerLastFrameIndex == currentFrame)
		return GetRuntimeUpscalerLastFramePathLabel();

	return GetConfiguredFsrPathLabel();
}

std::string FidelityFX::GetRuntimeUpscalerProviderName() const
{
	return RuntimeProviderDisplayName(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName);
}

std::string FidelityFX::GetRuntimeUpscalerRequestedVersionString() const
{
	const uint32_t requestedVersion = runtimeUpscalerRequestedVersion ? runtimeUpscalerRequestedVersion : GetPreferredRuntimeUpscalerVersion();
	return UpscalerVersionToString(requestedVersion);
}

void FidelityFX::ResetRuntimeUpscalerTracking(bool a_invalidateProviderCache)
{
	runtimeUpscalerFailureLatched = false;
	runtimeFsr4FailureLatched = false;
	runtimeInteropFailureLogged = false;
	runtimeFallbackResetDispatchesRemaining = 0;
	runtimeResumeResetDispatchesRemaining = 0;
	runtimeUpscalerLastFramePathValid = false;
	runtimeUpscalerLastFrameIndex = 0;
	runtimeUpscalerLastFramePath = RuntimeUpscalerFramePath::kInactive;

	if (!a_invalidateProviderCache)
		return;

	runtimeUpscalerSupportCheckKnown = false;
	runtimeUpscalerSupportConfirmed = false;
	runtimeUpscalerProviderMatchedVersionId = 0;
	runtimeUpscalerProviderMatchedVersionName.clear();
}

void FidelityFX::LatchRuntimeFsr4Failure()
{
	if (runtimeFsr4FailureLatched)
		return;

	runtimeFsr4FailureLatched = true;
	logger::warn("[FidelityFX] Runtime FSR4 path failed; selecting the host fallback without creating another DX12 runtime provider.");
}

void FidelityFX::QuarantineRuntimeUpscalerForSession(const char* a_reason)
{
	runtimeUpscalerFailureLatched = true;
	if (runtimeUpscalerSessionQuarantined)
		return;

	runtimeUpscalerSessionQuarantined = true;
	runtimeUpscalerQuarantineRetirement = LifecycleResult::Pending;
	runtimeUpscalerQuarantineFrameValid = globals::state != nullptr;
	runtimeUpscalerQuarantineFrame = globals::state ? globals::state->frameCount : 0;
	logger::warn(
		"[FidelityFX] Quarantined the DX12 runtime upscaler for this game session after {}; using {}. Restart the game to retry the runtime provider.",
		a_reason && *a_reason ? a_reason : "a provider failure",
		GetHostFsrSdkLabel());
}

FidelityFX::RuntimeUpscalerFramePath FidelityFX::GetRuntimeUpscalerProviderFramePath(uint32_t a_requestedVersion) const
{
	if (RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, FFX_UPSCALER_VERSION))
		return RuntimeUpscalerFramePath::kRuntimeFsr4;

	if (RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, Fsr3Version))
		return RuntimeUpscalerFramePath::kRuntimeFsr31;

	return a_requestedVersion == FFX_UPSCALER_VERSION ? RuntimeUpscalerFramePath::kRuntimeFsr4 : RuntimeUpscalerFramePath::kRuntimeFsr31;
}

void FidelityFX::RecordRuntimeUpscalerFramePath(RuntimeUpscalerFramePath a_path)
{
	const uint32_t frame = globals::state ? globals::state->frameCount : 0;
	if (!runtimeUpscalerLastFramePathValid || runtimeUpscalerLastFrameIndex != frame) {
		runtimeUpscalerLastFramePathValid = true;
		runtimeUpscalerLastFrameIndex = frame;
		runtimeUpscalerLastFramePath = a_path;
		return;
	}

	if (static_cast<uint8_t>(a_path) > static_cast<uint8_t>(runtimeUpscalerLastFramePath))
		runtimeUpscalerLastFramePath = a_path;
}

void FidelityFX::SetupFrameGeneration()
{
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { swapChain.swapChainDesc.Width, swapChain.swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
	createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(swapChain.swapChainDesc.Format);

	ffx::CreateBackendDX12Desc backendDesc{};
	backendDesc.device = swapChain.d3d12Device.get();

	if (ffx::CreateContext(frameGenContext, nullptr, createFg, backendDesc) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to create frame generation context!");
}

FidelityFX::FrameGenerationPresentResult FidelityFX::Present(bool a_useFrameGeneration, bool a_isHDR)
{
	auto& upscaling = globals::features::upscaling;
	auto& swapChain = globals::features::upscaling.dx12SwapChain;
	static bool loggedIncompleteFrameGenResources = false;

	if (a_useFrameGeneration) {
		const bool hasFrameGenerationResources =
			swapChain.uiBufferWrapped &&
			swapChain.uiBufferWrapped->resource.get() &&
			swapChain.depthBufferShared12 &&
			swapChain.depthBufferShared12->resource.get() &&
			swapChain.motionVectorBufferShared12 &&
			swapChain.motionVectorBufferShared12->resource.get() &&
			swapChain.frameIndex < 2 &&
			swapChain.commandLists[swapChain.frameIndex];
		if (!hasFrameGenerationResources) {
			if (!loggedIncompleteFrameGenResources) {
				logger::error("[FidelityFX] Frame generation requested without complete shared interop resources; presenting without frame generation.");
				loggedIncompleteFrameGenResources = true;
			}
			a_useFrameGeneration = false;
		} else {
			loggedIncompleteFrameGenResources = false;
		}
	}

	auto* hdr = globals::features::hdrDisplay.loaded ? &globals::features::hdrDisplay : nullptr;
	float peakNits = hdr ? static_cast<float>(hdr->settings.hdrPeakNits) : 1000.0f;
	peakNits = std::clamp(peakNits, 1.0f, 10000.0f);

	const bool hdrParamsChanged =
		(a_isHDR != prevHDRActive) ||
		(a_isHDR && std::abs(peakNits - prevPeakNits) > 1.0f);

	prevHDRActive = a_isHDR;
	prevPeakNits = peakNits;

	hdrPeakNits.store(peakNits, std::memory_order_seq_cst);
	isHDRActive.store(a_isHDR, std::memory_order_seq_cst);
	needsReset.store(hdrParamsChanged, std::memory_order_seq_cst);

	ffx::ConfigureDescFrameGeneration configParameters{};

	if (a_useFrameGeneration) {
		configParameters.frameGenerationEnabled = true;

		configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			const bool hdrActive = FidelityFX::isHDRActive.load(std::memory_order_seq_cst);
			if (hdrActive) {
				params->backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ;
				params->minMaxLuminance[0] = 0.0f;
				params->minMaxLuminance[1] = FidelityFX::hdrPeakNits.load(std::memory_order_seq_cst);
			} else {
				params->backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
			}
			if (FidelityFX::needsReset.exchange(false, std::memory_order_seq_cst))
				params->reset = true;
			return ffxModule.Dispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
		};

		configParameters.frameGenerationCallbackUserContext = &frameGenContext;
	} else {
		configParameters.frameGenerationEnabled = false;
		configParameters.frameGenerationCallbackUserContext = nullptr;
		configParameters.frameGenerationCallback = nullptr;
	}

	configParameters.HUDLessColor = FfxApiResource({});
	configParameters.presentCallback = nullptr;
	configParameters.presentCallbackUserContext = nullptr;

	static uint64_t frameID = 0;
	if (hdrParamsChanged && a_useFrameGeneration)
		frameID += 2;

	configParameters.frameID = frameID;
	configParameters.swapChain = swapChain.swapChain;
	configParameters.onlyPresentGenerated = false;
	configParameters.flags = 0;
	configParameters.allowAsyncWorkloads = true;

	const auto* viewport = globals::game::graphicsState;
	const float2 displaySize{
		static_cast<float>(viewport ? viewport->screenWidth : 0),
		static_cast<float>(viewport ? viewport->screenHeight : 0)
	};
	const auto renderSize = displaySize * upscaling.resolutionScale;

	configParameters.generationRect.left = 0;
	configParameters.generationRect.top = 0;
	configParameters.generationRect.width = swapChain.swapChainDesc.Width;
	configParameters.generationRect.height = swapChain.swapChainDesc.Height;

	const bool frameGenerationConfigured =
		ffx::Configure(frameGenContext, configParameters) == ffx::ReturnCode::Ok;
	if (!frameGenerationConfigured)
		logger::critical("[FidelityFX] Failed to configure frame generation!");

	ffx::ConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 uiConfig{};
	if (a_useFrameGeneration) {
		uiConfig.uiResource = ffxApiGetResourceDX12(swapChain.uiBufferWrapped->resource.get());
		uiConfig.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA |
		                 FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING;
	} else {
		uiConfig.uiResource = FfxApiResource({});
		uiConfig.flags = 0;
	}

	const bool uiCompositionConfigured =
		ffx::Configure(swapChainContext, uiConfig) == ffx::ReturnCode::Ok;
	if (!uiCompositionConfigured)
		logger::critical("[FidelityFX] Failed to configure UI composition!");

	bool frameGenerationPrepared = !a_useFrameGeneration;
	if (a_useFrameGeneration && frameGenerationConfigured && uiCompositionConfigured) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = swapChain.commandLists[swapChain.frameIndex].get();
		dispatchParameters.motionVectorScale.x = renderSize.x;
		dispatchParameters.motionVectorScale.y = renderSize.y;
		dispatchParameters.renderSize.width = static_cast<uint32_t>(renderSize.x);
		dispatchParameters.renderSize.height = static_cast<uint32_t>(renderSize.y);
		dispatchParameters.jitterOffset.x = -upscaling.jitter.x;
		dispatchParameters.jitterOffset.y = -upscaling.jitter.y;
		dispatchParameters.frameTimeDelta = RE::GetSecondsSinceLastFrame() * 1000.f;
		dispatchParameters.cameraFar = *globals::game::cameraFar;
		dispatchParameters.cameraNear = *globals::game::cameraNear;
		dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
		dispatchParameters.frameID = frameID;
		dispatchParameters.depth = ffxApiGetResourceDX12(swapChain.depthBufferShared12->resource.get());
		dispatchParameters.motionVectors = ffxApiGetResourceDX12(swapChain.motionVectorBufferShared12->resource.get());

		ffx::DispatchDescFrameGenerationPrepareCameraInfo cameraConfig{};

		auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();

		cameraConfig.cameraRight[0] = viewMatrix._11;
		cameraConfig.cameraRight[1] = viewMatrix._12;
		cameraConfig.cameraRight[2] = viewMatrix._13;

		cameraConfig.cameraUp[0] = viewMatrix._21;
		cameraConfig.cameraUp[1] = viewMatrix._22;
		cameraConfig.cameraUp[2] = viewMatrix._23;

		cameraConfig.cameraForward[0] = viewMatrix._31;
		cameraConfig.cameraForward[1] = viewMatrix._32;
		cameraConfig.cameraForward[2] = viewMatrix._33;

		cameraConfig.cameraPosition[0] = globals::game::frameBufferCached.GetCameraPosAdjust().x;
		cameraConfig.cameraPosition[1] = globals::game::frameBufferCached.GetCameraPosAdjust().y;
		cameraConfig.cameraPosition[2] = globals::game::frameBufferCached.GetCameraPosAdjust().z;

		frameGenerationPrepared =
			ffx::Dispatch(frameGenContext, dispatchParameters, cameraConfig) ==
			ffx::ReturnCode::Ok;
		if (!frameGenerationPrepared)
			logger::critical("[FidelityFX] Failed to dispatch frame generation!");
	}

	frameID++;
	const bool successful =
		frameGenerationConfigured &&
		uiCompositionConfigured &&
		frameGenerationPrepared;
	return { successful, a_useFrameGeneration && successful };
}

FidelityFX::LifecycleResult FidelityFX::CreateFSRResources()
{
	auto state = globals::state;
	if (!state || !globals::d3d::device || !globals::d3d::context) {
		logger::critical("[FidelityFX] Missing global state when creating FSR resources.");
		return LifecycleResult::Failed;
	}
	if (fsrHostStateQuarantined)
		return GetQuarantinedHostFSRResult("host FSR resource creation");
	if (std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		QuarantineHostFSRState("indeterminate FSR3 context tracking before creation");
		return GetQuarantinedHostFSRResult("host FSR resource creation");
	}
	if (RecordFSRDeviceStatus() == LifecycleResult::DeviceLost)
		return LifecycleResult::DeviceLost;

	const auto* viewport = globals::game::graphicsState;
	const float2 displaySize{
		static_cast<float>(viewport ? viewport->screenWidth : 0),
		static_cast<float>(viewport ? viewport->screenHeight : 0)
	};
	const auto renderSize = Util::ConvertToDynamic(displaySize);

	const uint32_t displayWidth = static_cast<uint32_t>(displaySize.x);
	const uint32_t displayHeight = static_cast<uint32_t>(displaySize.y);
	const uint32_t renderWidth = static_cast<uint32_t>(renderSize.x);
	const uint32_t renderHeight = static_cast<uint32_t>(renderSize.y);
	const uint32_t numContexts = 1u;
	if (!displayWidth || !displayHeight || !renderWidth || !renderHeight) {
		logger::critical("[FidelityFX] Cannot create FSR resources with zero-sized render or display bounds.");
		return LifecycleResult::Failed;
	}

	const bool hasTrackedHostResources =
		fsrScratchBuffer || fsrContextCount != 0 ||
		std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; }) ||
		std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; });
	if (hasTrackedHostResources) {
		if (AreFSRResourcesCompatible(renderWidth, renderHeight, displayWidth, displayHeight, numContexts)) {
			logger::debug("[FidelityFX] Retaining compatible host FSR resources.");
			return LifecycleResult::Ready;
		}

		const auto teardownResult = PollFSRResourceTeardownReady("incompatible host FSR replacement");
		if (teardownResult != LifecycleResult::Ready)
			return teardownResult;
		const auto destroyResult = DestroyFSRResources(false);
		if (destroyResult != LifecycleResult::Ready) {
			logger::warn("[FidelityFX] Cannot replace incompatible host FSR resources because their retirement failed.");
			return destroyResult;
		}
	}

	if (!IsRuntimeUpscalerOwnershipDetached()) {
		const auto runtimeIdleResult = PollRuntimeUpscalerTeardownReady("host FSR resource creation");
		if (runtimeIdleResult != LifecycleResult::Ready)
			return runtimeIdleResult;
		const auto contextDestroyResult = DestroyRuntimeUpscalerContexts(false);
		if (contextDestroyResult != LifecycleResult::Ready)
			return contextDestroyResult;
		const auto resourceDestroyResult = DestroyRuntimeUpscalerResources(false);
		if (resourceDestroyResult != LifecycleResult::Ready)
			return resourceDestroyResult;
		ReleaseIdleRuntimeUpscalerInterop();
		ResetRuntimeUpscalerTracking(true);
	}

	auto fsrDevice = ffxGetDeviceDX11_Fsr31(globals::d3d::device);

	const size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(numContexts);
	fsrScratchBuffer = calloc(scratchBufferSize, 1);
	if (!fsrScratchBuffer) {
		logger::critical("[FidelityFX] Failed to allocate FSR3 scratch buffer memory!");
		return LifecycleResult::Failed;
	}
	memset(fsrScratchBuffer, 0, scratchBufferSize);
	fsrContextValid.fill(false);
	fsrContextIndeterminate.fill(false);
	fsrContextCount = 0;
	fsrContextMaxRenderWidth = 0;
	fsrContextMaxRenderHeight = 0;
	fsrContextDisplayWidth = 0;
	fsrContextDisplayHeight = 0;

	FfxInterface fsrInterface{};
	bool interfaceCrashed = false;
	const auto interfaceResult = GetHostFsr3InterfaceProtected(
		&fsrInterface,
		fsrDevice,
		fsrScratchBuffer,
		scratchBufferSize,
		numContexts,
		interfaceCrashed);
	if (interfaceCrashed) {
		// The SDK may have partially initialized backend state inside the scratch
		// allocation before faulting. Keep it alive for the remainder of the
		// session because freeing it could invalidate unknown provider ownership.
		QuarantineHostFSRState("an FSR3 backend interface initialization fault");
		logger::critical("[FidelityFX] FSR3 backend interface initialization faulted; retaining and quarantining its scratch memory.");
		return GetQuarantinedHostFSRResult("FSR3 backend interface initialization");
	}
	if (interfaceResult != FFX_OK) {
		logger::critical("[FidelityFX] Failed to initialize FSR3 backend interface!");
		const auto failureResult = ResolveFSRLifecycleFailure("FSR3 backend interface initialization");
		const auto releaseResult = ReleaseHostFSRResources();
		return releaseResult == LifecycleResult::Ready ? failureResult : releaseResult;
	}

	for (uint32_t i = 0; i < numContexts; ++i) {
		fsrContext[i] = {};
		FfxFsr3ContextDescription contextDescription{};
		contextDescription.maxRenderSize.width = renderWidth;
		contextDescription.maxRenderSize.height = renderHeight;
		contextDescription.maxUpscaleSize.width = displayWidth;
		contextDescription.maxUpscaleSize.height = displayHeight;
		contextDescription.displaySize.width = displayWidth;
		contextDescription.displaySize.height = displayHeight;
		contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE | FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
		contextDescription.backendInterfaceUpscaling = fsrInterface;

		bool createCrashed = false;
		const auto createResult = CreateHostFsr3ContextProtected(&fsrContext[i], &contextDescription, createCrashed);
		if (createCrashed || createResult != FFX_OK) {
			const auto failureResult = ResolveFSRLifecycleFailure("FSR3 context creation");
			QuarantineHostFSRContext(i, createCrashed ? "an FSR3 context creation fault" : "an FSR3 context creation error");
			fsrContextCount = std::max(fsrContextCount, i + 1);
			fsrContextMaxRenderWidth = renderWidth;
			fsrContextMaxRenderHeight = renderHeight;
			fsrContextDisplayWidth = displayWidth;
			fsrContextDisplayHeight = displayHeight;
			logger::critical(
				"[FidelityFX] FSR3 context {} creation {}; retaining its context storage and backend scratch memory because SDK ownership is indeterminate.",
				i,
				createCrashed ? "faulted" : "failed");
			return failureResult;
		}
		fsrContextValid[i] = true;
	}

	fsrContextCount = numContexts;
	fsrContextMaxRenderWidth = renderWidth;
	fsrContextMaxRenderHeight = renderHeight;
	fsrContextDisplayWidth = displayWidth;
	fsrContextDisplayHeight = displayHeight;
	logger::info("[FidelityFX] Created {} FSR3 context(s) (Display: {}x{}, Render: {}x{})",
		numContexts, displayWidth, displayHeight, renderWidth, renderHeight);
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::DestroyRuntimeUpscalerContexts(bool a_waitForIdle)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::Ready;

	if (std::ranges::any_of(runtimeUpscalerContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		if (IsTerminalRuntimeQuarantineResult(runtimeUpscalerQuarantineRetirement))
			return runtimeUpscalerQuarantineRetirement;
		const auto result = ResolveRuntimeUpscalerLifecycleFailure("indeterminate runtime upscaler context destruction");
		runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(result);
		return runtimeUpscalerQuarantineRetirement;
	}
	if (a_waitForIdle) {
		const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime upscaler context destruction");
		if (idleResult != LifecycleResult::Ready)
			return idleResult;
	}

	for (uint32_t i = 0; i < std::size(runtimeUpscalerContexts); ++i) {
		if (!runtimeUpscalerContexts[i])
			continue;
		const auto retainedContext = runtimeUpscalerContexts[i];
		bool destroyCrashed = false;
		const auto destroyResult = DestroyRuntimeUpscalerContextProtected(&runtimeUpscalerContexts[i], destroyCrashed);
		if (destroyCrashed || destroyResult != FFX_API_RETURN_OK) {
			runtimeUpscalerContexts[i] = retainedContext;
			runtimeUpscalerContextIndeterminate[i] = true;
			logger::critical(
				"[FidelityFX] Runtime upscaler context {} destruction {} without proving release; retaining its handle and referenced resources.",
				i,
				destroyCrashed ? "faulted" : "failed");
			QuarantineRuntimeUpscalerForSession("an indeterminate runtime context destruction");
			const auto result = ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler context destruction");
			runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(result);
			return result;
		}
		runtimeUpscalerContexts[i] = nullptr;
		runtimeUpscalerContextIndeterminate[i] = false;
	}
	runtimeUpscalerContextCount = 0;
	runtimeUpscalerMaxRenderWidth = 0;
	runtimeUpscalerMaxRenderHeight = 0;
	runtimeUpscalerMaxDisplayWidth = 0;
	runtimeUpscalerMaxDisplayHeight = 0;
	runtimeUpscalerRequestedVersion = 0;
	return LifecycleResult::Ready;
}

bool FidelityFX::HasRuntimeUpscalerResources() const
{
	bool hasResources =
		runtimeUpscalerContextCount != 0 ||
		pendingRuntimeTeardownD3D11FenceValue != 0 ||
		pendingRuntimeTeardownD3D12FenceValue != 0 ||
		runtimeD3D11Fence.get() != nullptr ||
		runtimeD3D12Fence.get() != nullptr;
	for (bool indeterminate : runtimeUpscalerContextIndeterminate)
		hasResources = hasResources || indeterminate;
	for (const auto& context : runtimeUpscalerContexts)
		hasResources = hasResources || context != nullptr;
	for (const auto& context : runtimeCommandContexts) {
		hasResources = hasResources || context.commandAllocator || context.commandList || context.fenceValue != 0;
	}
	for (uint32_t i = 0; i < std::size(runtimeColorShared); ++i) {
		hasResources = hasResources || runtimeColorShared[i] || runtimeDepthShared[i] || runtimeMotionShared[i] ||
		               runtimeReactiveShared[i] || runtimeTransparencyShared[i] || runtimeOutputShared[i];
	}
	return hasResources;
}

bool FidelityFX::AreRuntimeUpscalerContextsCompatible(
	uint32_t a_fullRenderWidth,
	uint32_t a_fullRenderHeight,
	uint32_t a_fullDisplayWidth,
	uint32_t a_fullDisplayHeight,
	uint32_t a_contextCount,
	uint32_t a_requestedVersion) const
{
	if (!a_fullRenderWidth || !a_fullRenderHeight || !a_fullDisplayWidth || !a_fullDisplayHeight ||
		a_contextCount == 0 || a_contextCount > std::size(runtimeUpscalerContexts))
		return false;
	if (std::ranges::any_of(runtimeUpscalerContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; }))
		return false;
	for (uint32_t i = 0; i < a_contextCount; ++i) {
		if (!runtimeUpscalerContexts[i])
			return false;
	}
	return runtimeUpscalerContextCount == a_contextCount &&
	       runtimeUpscalerMaxRenderWidth == a_fullRenderWidth &&
	       runtimeUpscalerMaxRenderHeight == a_fullRenderHeight &&
	       runtimeUpscalerMaxDisplayWidth == a_fullDisplayWidth &&
	       runtimeUpscalerMaxDisplayHeight == a_fullDisplayHeight &&
	       runtimeUpscalerRequestedVersion == a_requestedVersion;
}

FidelityFX::LifecycleResult FidelityFX::PollRuntimeUpscalerTeardownIdle(const char* a_reason)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::RuntimeDeviceLost;

	const char* reason = a_reason && *a_reason ? a_reason : "runtime upscaler teardown";
	auto& swapChain = globals::features::upscaling.dx12SwapChain;
	if (!HasRuntimeUpscalerResources()) {
		pendingRuntimeTeardownD3D11FenceValue = 0;
		pendingRuntimeTeardownD3D12FenceValue = 0;
		return LifecycleResult::Ready;
	}
	if (!swapChain.d3d11Context || !swapChain.commandQueue || !runtimeD3D11Fence || !runtimeD3D12Fence) {
		logger::debug("[FidelityFX] Deferring runtime idle proof before {} because the interop fence topology is incomplete.", reason);
		return LifecycleResult::Pending;
	}

	try {
		if (pendingRuntimeTeardownD3D12FenceValue == 0) {
			if (pendingRuntimeTeardownD3D11FenceValue == 0) {
				pendingRuntimeTeardownD3D11FenceValue = runtimeFenceValue++;
				DX::ThrowIfFailed(swapChain.d3d11Context->Signal(runtimeD3D11Fence.get(), pendingRuntimeTeardownD3D11FenceValue));
				swapChain.d3d11Context->Flush();
			}

			const uint64_t d3d11Completed = runtimeD3D12Fence->GetCompletedValue();
			if (d3d11Completed == std::numeric_limits<uint64_t>::max())
				return ResolveRuntimeUpscalerLifecycleFailure(reason);
			if (d3d11Completed < pendingRuntimeTeardownD3D11FenceValue)
				return LifecycleResult::Pending;
			pendingRuntimeTeardownD3D11FenceValue = 0;

			pendingRuntimeTeardownD3D12FenceValue = runtimeFenceValue++;
			DX::ThrowIfFailed(swapChain.commandQueue->Signal(runtimeD3D12Fence.get(), pendingRuntimeTeardownD3D12FenceValue));
		}

		const uint64_t completed = runtimeD3D12Fence->GetCompletedValue();
		if (completed == std::numeric_limits<uint64_t>::max())
			return ResolveRuntimeUpscalerLifecycleFailure(reason);
		if (completed < pendingRuntimeTeardownD3D12FenceValue)
			return LifecycleResult::Pending;
		pendingRuntimeTeardownD3D12FenceValue = 0;

		for (auto& context : runtimeCommandContexts) {
			if (context.fenceValue != 0 && completed >= context.fenceValue)
				context.fenceValue = 0;
		}
	} catch (const std::exception& e) {
		logger::warn("[FidelityFX] Failed to poll runtime upscaler idle before {}: {}", reason, e.what());
		const auto result = ResolveRuntimeUpscalerLifecycleFailure(reason);
		if (result != LifecycleResult::RuntimeDeviceLost) {
			pendingRuntimeTeardownD3D11FenceValue = 0;
			pendingRuntimeTeardownD3D12FenceValue = 0;
		}
		return result;
	} catch (...) {
		logger::warn("[FidelityFX] Failed to poll runtime upscaler idle before {}.", reason);
		const auto result = ResolveRuntimeUpscalerLifecycleFailure(reason);
		if (result != LifecycleResult::RuntimeDeviceLost) {
			pendingRuntimeTeardownD3D11FenceValue = 0;
			pendingRuntimeTeardownD3D12FenceValue = 0;
		}
		return result;
	}
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::PollRuntimeUpscalerTeardownReady(const char* a_reason)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::RuntimeDeviceLost;
	if (std::ranges::any_of(runtimeUpscalerContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		if (IsTerminalRuntimeQuarantineResult(runtimeUpscalerQuarantineRetirement))
			return runtimeUpscalerQuarantineRetirement;
		const auto result = ResolveRuntimeUpscalerLifecycleFailure(a_reason && *a_reason ? a_reason : "indeterminate runtime upscaler teardown");
		runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(result);
		return runtimeUpscalerQuarantineRetirement;
	}
	if (!HasRuntimeUpscalerResources()) {
		pendingRuntimeTeardownD3D11FenceValue = 0;
		pendingRuntimeTeardownD3D12FenceValue = 0;
		return LifecycleResult::Ready;
	}
	return PollRuntimeUpscalerTeardownIdle(a_reason);
}

void FidelityFX::ResetRuntimeCommandContexts()
{
	for (auto& commandContext : runtimeCommandContexts) {
		commandContext.commandList = nullptr;
		commandContext.commandAllocator = nullptr;
		commandContext.fenceValue = 0;
	}
	runtimeCommandContextCursor = 0;
}

void FidelityFX::ReleaseIdleRuntimeUpscalerInterop()
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return;
	ResetRuntimeCommandContexts();
	pendingRuntimeTeardownD3D11FenceValue = 0;
	pendingRuntimeTeardownD3D12FenceValue = 0;
	runtimeD3D11Fence = nullptr;
	runtimeD3D12Fence = nullptr;
	runtimeFenceValue = 1;
	runtimeUpscalerQuarantineFrameValid = false;
	runtimeUpscalerQuarantineFrame = 0;
}

FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeCommandContexts()
{
	auto& swapChain = globals::features::upscaling.dx12SwapChain;
	if (!swapChain.d3d12Device)
		return LifecycleResult::Pending;

	const bool allContextsReady = std::ranges::all_of(runtimeCommandContexts, [](const RuntimeCommandContext& a_context) {
		return a_context.commandAllocator && a_context.commandList;
	});
	if (allContextsReady)
		return LifecycleResult::Ready;
	if (std::ranges::any_of(runtimeCommandContexts, [](const RuntimeCommandContext& a_context) { return a_context.fenceValue != 0; })) {
		logger::debug("[FidelityFX] Deferring runtime command-pool repair while work remains in flight.");
		return LifecycleResult::Pending;
	}

	std::array<RuntimeCommandContext, kRuntimeCommandContextCount> pendingContexts{};
	try {
		for (auto& commandContext : pendingContexts) {
			DX::ThrowIfFailed(swapChain.d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandContext.commandAllocator.put())));
			DX::ThrowIfFailed(swapChain.d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandContext.commandAllocator.get(), nullptr, IID_PPV_ARGS(commandContext.commandList.put())));
			DX::ThrowIfFailed(commandContext.commandList->Close());
		}
	} catch (const std::exception& e) {
		logger::error("[FidelityFX] Failed to create runtime upscaler command contexts: {}", e.what());
		return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler command-context creation");
	} catch (...) {
		logger::error("[FidelityFX] Failed to create runtime upscaler command contexts.");
		return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler command-context creation");
	}

	runtimeCommandContexts = std::move(pendingContexts);
	runtimeCommandContextCursor = 0;
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::AcquireRuntimeCommandContext(RuntimeCommandContext*& a_commandContext, uint32_t a_requiredFreeContexts)
{
	a_commandContext = nullptr;
	if (!runtimeD3D12Fence)
		return LifecycleResult::Pending;
	const auto ensureResult = EnsureRuntimeCommandContexts();
	if (ensureResult != LifecycleResult::Ready)
		return ensureResult;

	const uint64_t completedValue = runtimeD3D12Fence->GetCompletedValue();
	if (completedValue == std::numeric_limits<uint64_t>::max()) {
		logger::critical("[FidelityFX] Runtime shared fence reported D3D12 device removal while acquiring a command context.");
		return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler command-context acquisition");
	}
	const uint32_t contextCount = static_cast<uint32_t>(runtimeCommandContexts.size());
	std::array<uint32_t, kRuntimeCommandContextCount> availableIndices{};
	uint32_t availableCount = 0;
	static bool loggedCommandPoolExhausted = false;
	for (uint32_t i = 0; i < contextCount; ++i) {
		const uint32_t index = (runtimeCommandContextCursor + i) % contextCount;
		auto& commandContext = runtimeCommandContexts[index];
		if (!commandContext.commandAllocator || !commandContext.commandList)
			continue;
		if (commandContext.fenceValue != 0 && completedValue < commandContext.fenceValue)
			continue;

		availableIndices[availableCount++] = index;
	}

	const uint32_t requiredFreeContexts = std::clamp(a_requiredFreeContexts, 1u, contextCount);
	if (availableCount < requiredFreeContexts) {
		if (!loggedCommandPoolExhausted) {
			logger::warn("[FidelityFX] Deferring runtime upscaler dispatch because the command context pool is still in flight.");
			loggedCommandPoolExhausted = true;
		}
		return LifecycleResult::Pending;
	}

	const uint32_t selectedIndex = availableIndices[0];
	auto& selectedContext = runtimeCommandContexts[selectedIndex];
	selectedContext.fenceValue = 0;
	runtimeCommandContextCursor = (selectedIndex + 1) % contextCount;
	loggedCommandPoolExhausted = false;
	a_commandContext = &selectedContext;
	return LifecycleResult::Ready;
}

void FidelityFX::ResetFSRIdleFence()
{
	ReleaseD3D11IdleFence(pendingFSRResourceFreeIdleFence);
}

bool FidelityFX::HasFSRResources() const
{
	if (fsrHostStateQuarantined ||
		std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; }) ||
		fsrContextCount == 0 || fsrContextCount > fsrContextValid.size() || !fsrScratchBuffer) {
		return false;
	}
	for (uint32_t i = 0; i < fsrContextCount; ++i) {
		if (!fsrContextValid[i])
			return false;
	}
	for (uint32_t i = fsrContextCount; i < fsrContextValid.size(); ++i) {
		if (fsrContextValid[i])
			return false;
	}
	return true;
}

bool FidelityFX::AreFSRResourcesCompatible(uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight, uint32_t a_contextCount) const
{
	return HasFSRResources() && fsrContextCount == a_contextCount &&
	       a_renderWidth != 0 && a_renderHeight != 0 && a_displayWidth != 0 && a_displayHeight != 0 &&
	       a_renderWidth <= fsrContextMaxRenderWidth && a_renderHeight <= fsrContextMaxRenderHeight &&
	       a_displayWidth == fsrContextDisplayWidth && a_displayHeight == fsrContextDisplayHeight;
}

bool FidelityFX::HasFSRResourcesPendingTeardown() const
{
	return fsrHostStateQuarantined || fsrContextCount != 0 || fsrScratchBuffer ||
	       std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; }) ||
	       std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; }) ||
	       (!IsRuntimeUpscalerOwnershipDetached() && HasRuntimeUpscalerResources());
}

FidelityFX::LifecycleResult FidelityFX::RetireRuntimeUpscalerWhileHostFSRQuarantined(const char* a_operation)
{
	const auto hostResult = GetQuarantinedHostFSRResult(a_operation);
	if (hostResult == LifecycleResult::DeviceLost || IsRuntimeUpscalerOwnershipDetached())
		return hostResult;
	const auto idleResult = PollRuntimeUpscalerTeardownReady(a_operation);
	if (idleResult != LifecycleResult::Ready)
		return idleResult;
	const auto contextResult = DestroyRuntimeUpscalerContexts(false);
	if (contextResult != LifecycleResult::Ready)
		return contextResult;
	const auto resourceResult = DestroyRuntimeUpscalerResources(false);
	if (resourceResult != LifecycleResult::Ready)
		return resourceResult;
	ReleaseIdleRuntimeUpscalerInterop();
	ResetRuntimeUpscalerTracking(true);
	return hostResult;
}

FidelityFX::LifecycleResult FidelityFX::PollFSRResourceTeardownReady(const char* a_reason)
{
	const char* reason = a_reason && *a_reason ? a_reason : "FSR resource teardown";
	if (fsrHostStateQuarantined)
		return RetireRuntimeUpscalerWhileHostFSRQuarantined(reason);
	if (!HasFSRResourcesPendingTeardown()) {
		ResetFSRIdleFence();
		return LifecycleResult::Ready;
	}

	const bool hasHostResources = fsrContextCount != 0 || fsrScratchBuffer ||
		std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; }) ||
		std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; });
	if (hasHostResources) {
		auto result = BeginOrPollD3D11IdleFence(globals::d3d::context, pendingFSRResourceFreeIdleFence, reason);
		if (result == LifecycleResult::Failed)
			result = ResolveFSRLifecycleFailure(reason);
		if (result != LifecycleResult::Ready)
			return result;
	} else {
		ResetFSRIdleFence();
	}

	if (!IsRuntimeUpscalerOwnershipDetached()) {
		const auto runtimeResult = PollRuntimeUpscalerTeardownReady(reason);
		if (runtimeResult != LifecycleResult::Ready)
			return runtimeResult;
	}
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::DestroyRuntimeUpscalerResources(bool a_waitForIdle)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::Ready;
	if (std::ranges::any_of(runtimeUpscalerContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		logger::critical("[FidelityFX] Retaining runtime shared resources referenced by an indeterminate provider context.");
		return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler shared-resource destruction with indeterminate context ownership");
	}
	if (a_waitForIdle) {
		const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime upscaler shared-resource destruction");
		if (idleResult != LifecycleResult::Ready)
			return idleResult;
	}

	ResetWrappedResourceArray(runtimeColorShared);
	ResetWrappedResourceArray(runtimeDepthShared);
	ResetWrappedResourceArray(runtimeMotionShared);
	ResetWrappedResourceArray(runtimeReactiveShared);
	ResetWrappedResourceArray(runtimeTransparencyShared);
	ResetWrappedResourceArray(runtimeOutputShared);

	runtimeColorSharedDesc = {};
	runtimeDepthSharedDesc = {};
	runtimeMotionSharedDesc = {};
	runtimeReactiveSharedDesc = {};
	runtimeTransparencySharedDesc = {};
	runtimeOutputSharedDesc = {};
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::ResetRuntimeUpscalerResources(bool a_invalidateProviderCache)
{
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::Ready;
	const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime upscaler reset");
	if (idleResult != LifecycleResult::Ready)
		return idleResult;
	const auto contextResult = DestroyRuntimeUpscalerContexts(false);
	if (contextResult != LifecycleResult::Ready)
		return contextResult;
	const auto resourceResult = DestroyRuntimeUpscalerResources(false);
	if (resourceResult != LifecycleResult::Ready)
		return resourceResult;
	ReleaseIdleRuntimeUpscalerInterop();
	ResetRuntimeUpscalerTracking(a_invalidateProviderCache);
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::RetireQuarantinedRuntimeUpscalerResources()
{
	if (!runtimeUpscalerSessionQuarantined)
		return LifecycleResult::Ready;
	if (IsRuntimeUpscalerOwnershipDetached())
		return LifecycleResult::RuntimeDeviceLost;
	if (!HasRuntimeUpscalerResources()) {
		ReleaseIdleRuntimeUpscalerInterop();
		runtimeUpscalerQuarantineRetirement = LifecycleResult::Ready;
		return LifecycleResult::Ready;
	}
	if (runtimeUpscalerQuarantineRetirement != LifecycleResult::Pending)
		return runtimeUpscalerQuarantineRetirement;
	if (runtimeUpscalerQuarantineFrameValid && globals::state &&
		globals::state->frameCount == runtimeUpscalerQuarantineFrame) {
		return LifecycleResult::Pending;
	}

	const auto idleResult = PollRuntimeUpscalerTeardownReady("quarantined runtime upscaler retirement");
	if (idleResult != LifecycleResult::Ready) {
		if (idleResult != LifecycleResult::Pending)
			runtimeUpscalerQuarantineRetirement = idleResult;
		return idleResult;
	}
	const auto contextResult = DestroyRuntimeUpscalerContexts(false);
	if (contextResult != LifecycleResult::Ready) {
		runtimeUpscalerQuarantineRetirement = contextResult;
		return contextResult;
	}
	const auto resourceResult = DestroyRuntimeUpscalerResources(false);
	if (resourceResult != LifecycleResult::Ready) {
		runtimeUpscalerQuarantineRetirement = resourceResult;
		return resourceResult;
	}
	ReleaseIdleRuntimeUpscalerInterop();
	runtimeUpscalerQuarantineRetirement = LifecycleResult::Ready;
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::DestroyFSRResources(bool a_waitForIdle)
{
	if (fsrHostStateQuarantined)
		return RetireRuntimeUpscalerWhileHostFSRQuarantined("FSR resource teardown");
	if (std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		QuarantineHostFSRState("indeterminate FSR3 context tracking during teardown");
		return GetQuarantinedHostFSRResult("FSR resource teardown");
	}
	if (a_waitForIdle) {
		const auto idleResult = PollFSRResourceTeardownReady("FSR resource teardown");
		if (idleResult != LifecycleResult::Ready)
			return idleResult;
	}
	ResetFSRIdleFence();

	if (fsrContextCount > fsrContextValid.size()) {
		logger::critical("[FidelityFX] Refusing FSR teardown because the tracked host context count is invalid.");
		QuarantineHostFSRState("invalid tracked host FSR context count");
		return RetireRuntimeUpscalerWhileHostFSRQuarantined("FSR resource teardown");
	}
	if (!fsrScratchBuffer && std::ranges::any_of(fsrContextValid, [](bool a_valid) { return a_valid; })) {
		logger::critical("[FidelityFX] Refusing FSR teardown because live host contexts have no retained backend scratch buffer.");
		for (uint32_t i = 0; i < fsrContextValid.size(); ++i) {
			if (fsrContextValid[i])
				fsrContextIndeterminate[i] = true;
		}
		QuarantineHostFSRState("live host FSR contexts without backend scratch ownership");
		return RetireRuntimeUpscalerWhileHostFSRQuarantined("FSR resource teardown");
	}

	const auto hostDestroyResult = DestroyTrackedHostFSRContexts("FSR resource teardown");
	if (hostDestroyResult != LifecycleResult::Ready)
		return hostDestroyResult;
	const auto hostReleaseResult = ReleaseHostFSRResources();
	if (hostReleaseResult != LifecycleResult::Ready)
		return hostReleaseResult;

	if (!IsRuntimeUpscalerOwnershipDetached()) {
		const auto contextResult = DestroyRuntimeUpscalerContexts(false);
		if (contextResult != LifecycleResult::Ready)
			return contextResult;
		const auto resourceResult = DestroyRuntimeUpscalerResources(false);
		if (resourceResult != LifecycleResult::Ready)
			return resourceResult;
		ReleaseIdleRuntimeUpscalerInterop();
		ResetRuntimeUpscalerTracking(true);
	}
	fsrDispatchCrashLogged = false;
	return LifecycleResult::Ready;
}

bool FidelityFX::IsAmdAdapterDetected() const
{
	DXGI_ADAPTER_DESC adapterDesc{};
	if (TryGetCurrentAdapterDesc(adapterDesc))
		return adapterDesc.VendorId == kAmdVendorId;

	return false;
}

bool FidelityFX::IsNvidiaAdapterDetected() const
{
	DXGI_ADAPTER_DESC adapterDesc{};
	if (TryGetCurrentAdapterDesc(adapterDesc))
		return adapterDesc.VendorId == kNvidiaVendorId;

	return false;
}

bool FidelityFX::IsRuntimeUpscalerPresent() const
{
	if (!featureRuntimeUpscaler || !runtimeUpscalerModule || !module)
		return false;
	if (!ffxModule.CreateContext || !ffxModule.DestroyContext || !ffxModule.Dispatch || !ffxModule.Query)
		return false;

	return true;
}

FidelityFX::Fsr4AdapterSupport FidelityFX::GetFsr4AdapterSupport(const DXGI_ADAPTER_DESC& a_adapterDesc)
{
	return ClassifyFsr4AdapterSupport(a_adapterDesc);
}

FidelityFX::Fsr4AdapterSupport FidelityFX::GetFsr4AdapterSupport() const
{
	DXGI_ADAPTER_DESC adapterDesc{};
	if (!TryGetCurrentAdapterDesc(adapterDesc))
		return Fsr4AdapterSupport::Unsupported;

	return GetFsr4AdapterSupport(adapterDesc);
}

bool FidelityFX::IsRuntimeFsr4AutoEligible() const
{
	return GetFsr4AdapterSupport() != Fsr4AdapterSupport::Unsupported;
}

bool FidelityFX::IsRuntimeFsr4Available() const
{
	if (!IsRuntimeUpscalerPresent())
		return false;

	return IsRuntimeFsr4AutoEligible();
}

bool FidelityFX::ShouldUseRuntimeUpscalerForFSR() const
{
	return IsRuntimeUpscalerPresent() && IsAmdAdapterDetected();
}

FfxResource ffxGetResource(ID3D11Resource* dx11Resource,
	[[maybe_unused]] wchar_t const* ffxResName,
	FfxResourceStates state = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ)
{
	FfxResource resource = {};
	resource.resource = reinterpret_cast<void*>(const_cast<ID3D11Resource*>(dx11Resource));
	resource.state = state;
	resource.description = GetFfxResourceDescriptionDX11(dx11Resource);

#ifdef _DEBUG
	if (ffxResName) {
		wcscpy_s(resource.name, ffxResName);
	}
#endif

	return resource;
}

bool FidelityFX::CanUseRuntimeUpscalerPath()
{
	if (runtimeUpscalerSessionQuarantined || runtimeUpscalerFailureLatched)
		return false;
	return true;
}

bool FidelityFX::ShouldRequestRuntimeFsr4() const
{
	return globals::features::upscaling.settings.fsr4RuntimeEnable &&
	       !runtimeFsr4FailureLatched &&
	       IsRuntimeFsr4Available();
}

uint32_t FidelityFX::GetPreferredRuntimeUpscalerVersion() const
{
	return ShouldRequestRuntimeFsr4() ? FFX_UPSCALER_VERSION : Fsr3Version;
}

FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerInterop()
{
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	auto logFailureOnce = [&](const char* a_step, const std::string& a_detail) {
		if (runtimeInteropFailureLogged)
			return;

		runtimeInteropFailureLogged = true;
		logger::error("[FidelityFX] DX11->DX12 runtime interop failed at {}: {}", a_step, a_detail);
	};

	auto logHrFailureOnce = [&](const char* a_step, HRESULT a_result) {
		logFailureOnce(a_step, std::format("HRESULT 0x{:08X}", static_cast<uint32_t>(a_result)));
	};

	auto checkHr = [&](const char* a_step, HRESULT a_result) {
		if (SUCCEEDED(a_result))
			return true;

		logHrFailureOnce(a_step, a_result);
		return false;
	};

	auto clearRuntimeD3D12State = [&]() {
		swapChain.d3d12Device = nullptr;
		swapChain.commandQueue = nullptr;
		ResetRuntimeCommandContexts();
	};

	if (!globals::d3d::device) {
		logFailureOnce("validate D3D11 device", "global D3D11 device is unavailable");
		return LifecycleResult::Pending;
	}
	if (!globals::d3d::context) {
		logFailureOnce("validate D3D11 context", "global D3D11 context is unavailable");
		return LifecycleResult::Pending;
	}
	if (HasRuntimeUpscalerResources() &&
		(!swapChain.d3d11Device || !swapChain.d3d11Context || !swapChain.d3d12Device || !swapChain.commandQueue ||
			!runtimeD3D11Fence || !runtimeD3D12Fence)) {
		logFailureOnce("validate tracked runtime ownership", "required interop state disappeared while provider resources remain live");
		QuarantineRuntimeUpscalerForSession("loss of tracked DX11/DX12 interop state");
		const auto result = ResolveRuntimeUpscalerLifecycleFailure("runtime interop ownership validation");
		runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(result);
		return result;
	}

	if (!swapChain.d3d11Device) {
		if (!checkHr("query ID3D11Device5 from game device", globals::d3d::device->QueryInterface(IID_PPV_ARGS(swapChain.d3d11Device.put()))))
			return ResolveRuntimeUpscalerLifecycleFailure("D3D11 device interop query");
	}
	if (!swapChain.d3d11Context) {
		if (!checkHr("query ID3D11DeviceContext4 from game context", globals::d3d::context->QueryInterface(IID_PPV_ARGS(swapChain.d3d11Context.put()))))
			return ResolveRuntimeUpscalerLifecycleFailure("D3D11 context interop query");
	}

	if (!swapChain.d3d12Device) {
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (!checkHr("query IDXGIDevice from game D3D11 device", globals::d3d::device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()))))
			return ResolveRuntimeUpscalerLifecycleFailure("DXGI device interop query");

		winrt::com_ptr<IDXGIAdapter> adapter;
		if (!checkHr("get DXGI adapter for runtime D3D12 device", dxgiDevice->GetAdapter(adapter.put())))
			return ResolveRuntimeUpscalerLifecycleFailure("DXGI adapter interop query");

		if (!checkHr("create runtime D3D12 device", D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(swapChain.d3d12Device.put())))) {
			clearRuntimeD3D12State();
			return ResolveRuntimeUpscalerLifecycleFailure("runtime D3D12 device creation");
		}

	}

	if (!swapChain.commandQueue) {
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;

		if (!checkHr("create runtime D3D12 command queue", swapChain.d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(swapChain.commandQueue.put())))) {
			clearRuntimeD3D12State();
			return ResolveRuntimeUpscalerLifecycleFailure("runtime D3D12 command-queue creation");
		}
	}

	if (!runtimeD3D12Fence || !runtimeD3D11Fence) {
		if (HasRuntimeUpscalerResources()) {
			logFailureOnce("repair runtime shared-fence topology", "runtime ownership is already tracked");
			return LifecycleResult::Failed;
		}

		winrt::com_ptr<ID3D12Fence> pendingD3D12Fence;
		winrt::com_ptr<ID3D11Fence> pendingD3D11Fence;
		winrt::handle sharedFenceHandle;
		if (!checkHr("create runtime D3D12 shared fence", swapChain.d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(pendingD3D12Fence.put())))) {
			return ResolveRuntimeUpscalerLifecycleFailure("runtime shared-fence creation");
		}
		if (!checkHr("create runtime D3D12 shared fence handle", swapChain.d3d12Device->CreateSharedHandle(pendingD3D12Fence.get(), nullptr, GENERIC_ALL, nullptr, sharedFenceHandle.put()))) {
			return ResolveRuntimeUpscalerLifecycleFailure("runtime shared-fence handle creation");
		}
		if (!checkHr("open runtime shared fence on D3D11 device", swapChain.d3d11Device->OpenSharedFence(sharedFenceHandle.get(), IID_PPV_ARGS(pendingD3D11Fence.put())))) {
			return ResolveRuntimeUpscalerLifecycleFailure("runtime D3D11 shared-fence opening");
		}

		runtimeD3D12Fence = std::move(pendingD3D12Fence);
		runtimeD3D11Fence = std::move(pendingD3D11Fence);
		runtimeFenceValue = 1;
		ResetRuntimeCommandContexts();
	}
	const auto commandContextResult = EnsureRuntimeCommandContexts();
	if (commandContextResult != LifecycleResult::Ready) {
		logFailureOnce("create dedicated runtime command pool", "one or more command contexts are unavailable");
		return commandContextResult;
	}

	if (!swapChain.d3d11Device.get() ||
		!swapChain.d3d11Context.get() ||
		!swapChain.d3d12Device.get() ||
		!swapChain.commandQueue.get() ||
		!runtimeD3D11Fence.get() ||
		!runtimeD3D12Fence.get()) {
		std::string missing;
		auto appendMissing = [&](const char* a_name) {
			if (!missing.empty())
				missing += ", ";
			missing += a_name;
		};

		if (!swapChain.d3d11Device.get())
			appendMissing("D3D11 device");
		if (!swapChain.d3d11Context.get())
			appendMissing("D3D11 context");
		if (!swapChain.d3d12Device.get())
			appendMissing("D3D12 device");
		if (!swapChain.commandQueue.get())
			appendMissing("D3D12 command queue");
		if (!runtimeD3D11Fence.get())
			appendMissing("D3D11 shared fence");
		if (!runtimeD3D12Fence.get())
			appendMissing("D3D12 shared fence");

		logFailureOnce("validate runtime interop objects", std::format("missing {}", missing));
		return LifecycleResult::Pending;
	}

	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerContexts(uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight, uint32_t a_contextCount, uint32_t a_requestedVersion)
{
	auto recordRuntimeProviderResult = [&](bool a_supported) {
		runtimeUpscalerSupportCheckKnown = true;
		runtimeUpscalerSupportConfirmed = a_supported;
		runtimeUpscalerProviderMatchedVersionId = 0;
		runtimeUpscalerProviderMatchedVersionName.clear();
	};
	if (std::ranges::any_of(runtimeUpscalerContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; })) {
		recordRuntimeProviderResult(false);
		QuarantineRuntimeUpscalerForSession("indeterminate runtime-provider ownership");
		const auto result = ResolveRuntimeUpscalerLifecycleFailure("indeterminate runtime-provider ownership");
		runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(result);
		return result;
	}

	if (!a_fullRenderWidth || !a_fullRenderHeight || !a_fullDisplayWidth || !a_fullDisplayHeight) {
		recordRuntimeProviderResult(false);
		return LifecycleResult::Pending;
	}
	if (a_contextCount == 0 || a_contextCount > std::size(runtimeUpscalerContexts)) {
		recordRuntimeProviderResult(false);
		return LifecycleResult::Failed;
	}
	const auto interopResult = EnsureRuntimeUpscalerInterop();
	if (interopResult != LifecycleResult::Ready)
		return interopResult;
	if (!ffxModule.CreateContext || !ffxModule.DestroyContext || !ffxModule.Query) {
		recordRuntimeProviderResult(false);
		return LifecycleResult::Failed;
	}

	bool allContextsValid = true;
	for (uint32_t i = 0; i < a_contextCount; ++i) {
		if (!runtimeUpscalerContexts[i]) {
			allContextsValid = false;
			break;
		}
	}

	const bool needsRecreate =
		!allContextsValid ||
		runtimeUpscalerContextCount != a_contextCount ||
		runtimeUpscalerMaxRenderWidth != a_fullRenderWidth ||
		runtimeUpscalerMaxRenderHeight != a_fullRenderHeight ||
		runtimeUpscalerMaxDisplayWidth != a_fullDisplayWidth ||
		runtimeUpscalerMaxDisplayHeight != a_fullDisplayHeight ||
		runtimeUpscalerRequestedVersion != a_requestedVersion;

	if (!needsRecreate && runtimeUpscalerContextCount == a_contextCount) {
		const bool providerPreviouslyValidated =
			runtimeUpscalerSupportCheckKnown &&
			runtimeUpscalerSupportConfirmed &&
			IsRuntimeUpscalerProviderMatchingRequestedVersion();
		if (providerPreviouslyValidated)
			return LifecycleResult::Ready;

		logger::warn("[FidelityFX] Refusing to reuse runtime contexts whose provider identity was not validated.");
	}

	const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime upscaler context recreation");
	if (idleResult != LifecycleResult::Ready)
		return idleResult;
	const auto contextDestroyResult = DestroyRuntimeUpscalerContexts(false);
	if (contextDestroyResult != LifecycleResult::Ready)
		return contextDestroyResult;

	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	ffx::CreateBackendDX12Desc backendDesc{};
	backendDesc.device = swapChain.d3d12Device.get();

	uint64_t runtimeVersionId = 0;
	std::string runtimeVersionName;
	bool versionQueryCrashed = false;
	const bool hasRuntimeVersionOverride = QueryRuntimeUpscalerVersionId(
		swapChain.d3d12Device.get(), a_requestedVersion, runtimeVersionId, runtimeVersionName, versionQueryCrashed);
	if (versionQueryCrashed) {
		recordRuntimeProviderResult(false);
		QuarantineRuntimeUpscalerForSession("a runtime-provider version query fault");
		const auto result = ResolveRuntimeUpscalerLifecycleFailure("runtime-provider version query fault");
		runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(result);
		return result;
	}
	if (hasRuntimeVersionOverride) {
		logger::info(
			"[FidelityFX] Runtime upscaler will request FSR version {} through generic override '{}' (id 0x{:X})",
			UpscalerVersionToString(a_requestedVersion),
			runtimeVersionName.empty() ? "(unnamed)" : runtimeVersionName,
			runtimeVersionId);
	}

	bool createdContextWithGenericVersionAndUpscalerDescriptor = false;
	bool createdContextWithUpscalerVersionDescriptor = false;
	bool createdContextWithDefaultProvider = false;

	for (uint32_t i = 0; i < a_contextCount; ++i) {
		ffx::CreateContextDescUpscale createDesc{};
		createDesc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
		createDesc.maxRenderSize = { a_fullRenderWidth, a_fullRenderHeight };
		createDesc.maxUpscaleSize = { a_fullDisplayWidth, a_fullDisplayHeight };
		createDesc.fpMessage = RuntimeFfxMessage;

		ffx::CreateContextDescUpscaleVersion versionDesc{};
		versionDesc.version = a_requestedVersion;

		ffx::CreateContextDescOverrideVersion overrideVersionDesc{};
		overrideVersionDesc.versionId = runtimeVersionId;

		std::array<RuntimeUpscalerCreateAttemptResult, 3> attempts{ {
			{ RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion, hasRuntimeVersionOverride },
			{ RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor, true },
			{ RuntimeUpscalerCreateAttempt::kDefaultProvider, a_requestedVersion == FFX_UPSCALER_VERSION },
		} };

		bool contextCreated = false;
		for (auto& attempt : attempts) {
			if (!attempt.enabled)
				continue;

			attempt.attempted = true;
			bool createCrashed = false;
			bool ownershipIndeterminate = false;
			attempt.result = TryCreateRuntimeUpscalerContext(
				runtimeUpscalerContexts[i],
				attempt.attempt,
				createDesc,
				backendDesc,
				versionDesc,
				overrideVersionDesc,
				createCrashed,
				ownershipIndeterminate);
			if (createCrashed || ownershipIndeterminate) {
				runtimeUpscalerContextIndeterminate[i] = true;
				recordRuntimeProviderResult(false);
				QuarantineRuntimeUpscalerForSession(createCrashed ? "a runtime context creation fault" : "indeterminate runtime context creation");
				logger::critical(
					"[FidelityFX] Runtime context {} creation left provider ownership indeterminate; retaining all related state for this session.",
					i);
				const auto result = ResolveRuntimeUpscalerLifecycleFailure("runtime context creation");
				runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(result);
				return result;
			}

			if (attempt.result != FFX_API_RETURN_OK)
				continue;

			contextCreated = true;
			runtimeUpscalerContextIndeterminate[i] = false;
			if (attempt.attempt == RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion) {
				createdContextWithGenericVersionAndUpscalerDescriptor = true;
			} else if (attempt.attempt == RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor) {
				createdContextWithUpscalerVersionDescriptor = true;
			} else if (attempt.attempt == RuntimeUpscalerCreateAttempt::kDefaultProvider) {
				createdContextWithDefaultProvider = true;
			}
			break;
		}

		if (!contextCreated) {
			const auto getAttemptResult = [&](RuntimeUpscalerCreateAttempt a_attempt) {
				const auto iter = std::find_if(attempts.begin(), attempts.end(), [&](const RuntimeUpscalerCreateAttemptResult& a_result) {
					return a_result.attempt == a_attempt;
				});

				return iter != attempts.end() ? FfxCreateResultText(iter->attempted, iter->result) : std::string("not attempted");
			};

			logger::error("[FidelityFX] Failed to create runtime upscaler context {} for FSR version {}. Override + upscaler descriptor: {}, upscaler version descriptor: {}, default provider: {} (Render: {}x{}, Display: {}x{}).",
				i,
				UpscalerVersionToString(a_requestedVersion),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kDefaultProvider),
				a_fullRenderWidth,
				a_fullRenderHeight,
				a_fullDisplayWidth,
				a_fullDisplayHeight);
			const auto cleanupResult = DestroyRuntimeUpscalerContexts(false);
			recordRuntimeProviderResult(false);
			if (cleanupResult != LifecycleResult::Ready)
				logger::critical("[FidelityFX] Retained a rejected runtime provider handle because vendor destruction failed; the runtime path will remain unavailable.");
			return cleanupResult == LifecycleResult::Ready ? LifecycleResult::Failed : cleanupResult;
		}
	}

	runtimeUpscalerContextCount = a_contextCount;
	runtimeUpscalerMaxRenderWidth = a_fullRenderWidth;
	runtimeUpscalerMaxRenderHeight = a_fullRenderHeight;
	runtimeUpscalerMaxDisplayWidth = a_fullDisplayWidth;
	runtimeUpscalerMaxDisplayHeight = a_fullDisplayHeight;
	runtimeUpscalerRequestedVersion = a_requestedVersion;

	uint64_t selectedProviderVersionId = 0;
	std::string selectedProviderVersionName;
	for (uint32_t i = 0; i < a_contextCount; ++i) {
		ffxQueryGetProviderVersion providerQuery{};
		providerQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
		providerQuery.versionId = 0;
		providerQuery.versionName = nullptr;

		bool providerQueryCrashed = false;
		const auto queryResult = QueryRuntimeUpscalerProtected(&runtimeUpscalerContexts[i], &providerQuery.header, providerQueryCrashed);
		if (providerQueryCrashed) {
			runtimeUpscalerContextIndeterminate[i] = true;
			recordRuntimeProviderResult(false);
			QuarantineRuntimeUpscalerForSession("a runtime provider-identity query fault");
			logger::critical("[FidelityFX] Runtime context {} provider-identity query faulted; retaining its context and resources.", i);
			const auto result = ResolveRuntimeUpscalerLifecycleFailure("runtime provider-identity query fault");
			runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(result);
			return result;
		}
		const std::string providerVersionName = providerQuery.versionName ? providerQuery.versionName : "";
		const bool providerIdentityKnown = queryResult == FFX_API_RETURN_OK &&
		                                   (providerQuery.versionId != 0 || !providerVersionName.empty());
		const bool providerVersionMatches = providerIdentityKnown &&
		                                    RuntimeProviderMatchesVersion(providerQuery.versionId, providerVersionName, a_requestedVersion);
		const bool providerMatchesOtherContexts = i == 0 ||
		                                          (providerQuery.versionId == selectedProviderVersionId && providerVersionName == selectedProviderVersionName);
		if (!providerIdentityKnown || !providerVersionMatches || !providerMatchesOtherContexts) {
			auto providerName = RuntimeProviderDisplayName(providerQuery.versionId, providerVersionName);
			if (providerName.empty())
				providerName = "unknown";
			const char* reason = !providerIdentityKnown ? "identity is unknown" :
			                     !providerVersionMatches ? "does not match the requested version" :
			                                                "differs from the other runtime contexts";
			logger::error("[FidelityFX] Runtime context {} provider '{}' {} for FSR version {} (query code {}, Render: {}x{}, Display: {}x{}).",
				i, providerName, reason, UpscalerVersionToString(a_requestedVersion), static_cast<uint32_t>(queryResult),
				a_fullRenderWidth, a_fullRenderHeight, a_fullDisplayWidth, a_fullDisplayHeight);
			const auto cleanupResult = DestroyRuntimeUpscalerContexts(false);
			if (cleanupResult != LifecycleResult::Ready)
				QuarantineRuntimeUpscalerForSession("failure to retire a rejected runtime provider");
			recordRuntimeProviderResult(false);
			return cleanupResult == LifecycleResult::Ready ? LifecycleResult::Failed : cleanupResult;
		}

		if (i == 0) {
			selectedProviderVersionId = providerQuery.versionId;
			selectedProviderVersionName = providerVersionName;
		}
	}

	recordRuntimeProviderResult(true);
	runtimeUpscalerProviderMatchedVersionId = selectedProviderVersionId;
	runtimeUpscalerProviderMatchedVersionName = selectedProviderVersionName;
	if (createdContextWithGenericVersionAndUpscalerDescriptor) {
		logger::info("[FidelityFX] Runtime upscaler context creation used the generic FSR version override path with the upscaler version descriptor.");
	}
	if (createdContextWithUpscalerVersionDescriptor) {
		logger::info("[FidelityFX] Runtime upscaler context creation used the upscaler FSR version descriptor.");
	}
	if (createdContextWithDefaultProvider) {
		logger::warn("[FidelityFX] Runtime upscaler context creation succeeded only through the default provider path after explicit FSR version requests failed; reporting the actual provider path.");
	}

	if (runtimeUpscalerProviderMatchedVersionName.empty()) {
		logger::info("[FidelityFX] Created {} runtime upscaler context(s) for FSR version {} (Render: {}x{}, Display: {}x{}).",
			a_contextCount,
			UpscalerVersionToString(a_requestedVersion),
			a_fullRenderWidth,
			a_fullRenderHeight,
			a_fullDisplayWidth,
			a_fullDisplayHeight);
	} else {
		logger::info("[FidelityFX] Created {} runtime upscaler context(s) using provider '{}' (id 0x{:X}) for FSR version {} (Render: {}x{}, Display: {}x{}).",
			a_contextCount,
			RuntimeProviderDisplayName(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName),
			runtimeUpscalerProviderMatchedVersionId,
			UpscalerVersionToString(a_requestedVersion),
			a_fullRenderWidth,
			a_fullRenderHeight,
			a_fullDisplayWidth,
			a_fullDisplayHeight);
	}
	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::EnsureRuntimeUpscalerSharedResources(uint32_t a_contextCount, uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight,
	const D3D11_TEXTURE2D_DESC& a_colorDesc,
	const D3D11_TEXTURE2D_DESC& a_depthDesc,
	const D3D11_TEXTURE2D_DESC& a_motionDesc,
	const D3D11_TEXTURE2D_DESC& a_reactiveDesc,
	const D3D11_TEXTURE2D_DESC& a_transparencyDesc,
	const D3D11_TEXTURE2D_DESC& a_outputDesc)
{
	const auto interopResult = EnsureRuntimeUpscalerInterop();
	if (interopResult != LifecycleResult::Ready)
		return interopResult;
	if (a_contextCount == 0 || a_contextCount > std::size(runtimeColorShared))
		return LifecycleResult::Failed;

	const D3D11_TEXTURE2D_DESC desiredColorDesc = MakeSharedTextureDesc(a_colorDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredDepthDesc = MakeSharedTextureDesc(a_depthDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredMotionDesc = MakeSharedTextureDesc(a_motionDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredReactiveDesc = MakeSharedTextureDesc(a_reactiveDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredTransparencyDesc = MakeSharedTextureDesc(a_transparencyDesc, a_fullRenderWidth, a_fullRenderHeight, 0);
	const D3D11_TEXTURE2D_DESC desiredOutputDesc = MakeSharedTextureDesc(a_outputDesc, a_fullDisplayWidth, a_fullDisplayHeight, D3D11_BIND_UNORDERED_ACCESS);

	bool missingRequiredResource = false;
	for (uint32_t i = 0; i < a_contextCount; ++i) {
		if (!runtimeColorShared[i] ||
			!runtimeDepthShared[i] ||
			!runtimeMotionShared[i] ||
			!runtimeReactiveShared[i] ||
			!runtimeTransparencyShared[i] ||
			!runtimeOutputShared[i] ||
			!runtimeColorShared[i]->resource11 ||
			!runtimeDepthShared[i]->resource11 ||
			!runtimeMotionShared[i]->resource11 ||
			!runtimeReactiveShared[i]->resource11 ||
			!runtimeTransparencyShared[i]->resource11 ||
			!runtimeOutputShared[i]->resource11 ||
			!runtimeColorShared[i]->resource.get() ||
			!runtimeDepthShared[i]->resource.get() ||
			!runtimeMotionShared[i]->resource.get() ||
			!runtimeReactiveShared[i]->resource.get() ||
			!runtimeTransparencyShared[i]->resource.get() ||
			!runtimeOutputShared[i]->resource.get()) {
			missingRequiredResource = true;
			break;
		}
	}

	const bool needsRecreate =
		missingRequiredResource ||
		!SameTextureDesc(runtimeColorSharedDesc, desiredColorDesc) ||
		!SameTextureDesc(runtimeDepthSharedDesc, desiredDepthDesc) ||
		!SameTextureDesc(runtimeMotionSharedDesc, desiredMotionDesc) ||
		!SameTextureDesc(runtimeReactiveSharedDesc, desiredReactiveDesc) ||
		!SameTextureDesc(runtimeTransparencySharedDesc, desiredTransparencyDesc) ||
		!SameTextureDesc(runtimeOutputSharedDesc, desiredOutputDesc);

	if (!needsRecreate) {
		// Retain bounded unused slots until the next proven-idle teardown; a
		// cross-API dispatch may still reference them.
		return LifecycleResult::Ready;
	}

	const auto idleResult = PollRuntimeUpscalerTeardownReady("runtime shared-resource recreation");
	if (idleResult != LifecycleResult::Ready)
		return idleResult;
	const auto destroyResult = DestroyRuntimeUpscalerResources(false);
	if (destroyResult != LifecycleResult::Ready)
		return destroyResult;

	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	try {
		for (uint32_t i = 0; i < a_contextCount; ++i) {
			runtimeColorShared[i] = std::make_unique<WrappedResource>(desiredColorDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get(), std::format("Runtime FSR color context {}", i));
			runtimeDepthShared[i] = std::make_unique<WrappedResource>(desiredDepthDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get(), std::format("Runtime FSR depth context {}", i));
			runtimeMotionShared[i] = std::make_unique<WrappedResource>(desiredMotionDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get(), std::format("Runtime FSR motion context {}", i));
			runtimeReactiveShared[i] = std::make_unique<WrappedResource>(desiredReactiveDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get(), std::format("Runtime FSR reactive context {}", i));
			runtimeTransparencyShared[i] = std::make_unique<WrappedResource>(desiredTransparencyDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get(), std::format("Runtime FSR transparency context {}", i));
			runtimeOutputShared[i] = std::make_unique<WrappedResource>(desiredOutputDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get(), std::format("Runtime FSR output context {}", i));
		}
	} catch (const std::exception& e) {
		logger::error("[FidelityFX] Failed to create runtime shared resources: {}", e.what());
		const auto failureResult = ResolveRuntimeUpscalerLifecycleFailure("runtime shared-resource creation");
		if (failureResult == LifecycleResult::Failed) {
			const auto cleanupResult = DestroyRuntimeUpscalerResources(false);
			if (cleanupResult != LifecycleResult::Ready)
				return cleanupResult;
		}
		return failureResult;
	} catch (...) {
		logger::error("[FidelityFX] Failed to create runtime shared resources.");
		const auto failureResult = ResolveRuntimeUpscalerLifecycleFailure("runtime shared-resource creation");
		if (failureResult == LifecycleResult::Failed) {
			const auto cleanupResult = DestroyRuntimeUpscalerResources(false);
			if (cleanupResult != LifecycleResult::Ready)
				return cleanupResult;
		}
		return failureResult;
	}

	runtimeColorSharedDesc = desiredColorDesc;
	runtimeDepthSharedDesc = desiredDepthDesc;
	runtimeMotionSharedDesc = desiredMotionDesc;
	runtimeReactiveSharedDesc = desiredReactiveDesc;
	runtimeTransparencySharedDesc = desiredTransparencyDesc;
	runtimeOutputSharedDesc = desiredOutputDesc;

	return LifecycleResult::Ready;
}

FidelityFX::LifecycleResult FidelityFX::DispatchRuntimeUpscalerSingle(uint32_t a_contextIndex, ID3D11Resource* a_color, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_output,
	uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight,
	float a_motionVectorScaleX, float a_motionVectorScaleY, float a_sharpness)
{
	if (a_contextIndex >= runtimeUpscalerContextCount || !runtimeUpscalerContexts[a_contextIndex])
		return LifecycleResult::Failed;
	if (!a_color || !a_depth || !a_motionVectors || !a_reactiveMask || !a_transparencyCompositionMask || !a_output)
		return LifecycleResult::Failed;
	if (!a_renderWidth || !a_renderHeight || !a_displayWidth || !a_displayHeight)
		return LifecycleResult::Failed;

	D3D11_TEXTURE2D_DESC colorDesc{};
	D3D11_TEXTURE2D_DESC depthDesc{};
	D3D11_TEXTURE2D_DESC motionDesc{};
	D3D11_TEXTURE2D_DESC reactiveDesc{};
	D3D11_TEXTURE2D_DESC transparencyDesc{};
	D3D11_TEXTURE2D_DESC outputDesc{};
	if (!TryGetTexture2DDesc(a_color, colorDesc) ||
		!TryGetTexture2DDesc(a_depth, depthDesc) ||
		!TryGetTexture2DDesc(a_motionVectors, motionDesc) ||
		!TryGetTexture2DDesc(a_reactiveMask, reactiveDesc) ||
		!TryGetTexture2DDesc(a_transparencyCompositionMask, transparencyDesc) ||
		!TryGetTexture2DDesc(a_output, outputDesc)) {
		return LifecycleResult::Pending;
	}

	const auto sharedResourceResult = EnsureRuntimeUpscalerSharedResources(
			runtimeUpscalerContextCount,
			runtimeUpscalerMaxRenderWidth,
			runtimeUpscalerMaxRenderHeight,
			runtimeUpscalerMaxDisplayWidth,
			runtimeUpscalerMaxDisplayHeight,
			colorDesc,
			depthDesc,
			motionDesc,
			reactiveDesc,
			transparencyDesc,
			outputDesc);
	if (sharedResourceResult != LifecycleResult::Ready)
		return sharedResourceResult;

	auto& swapChain = globals::features::upscaling.dx12SwapChain;
	auto& upscaling = globals::features::upscaling;
	auto state = globals::state;

	if (!swapChain.d3d11Context || !swapChain.commandQueue || !runtimeD3D11Fence || !runtimeD3D12Fence)
		return LifecycleResult::Pending;

	auto isValidShared = [](const std::unique_ptr<WrappedResource>& a_resource) {
		return a_resource && a_resource->resource11 && a_resource->resource.get();
	};
	if (!isValidShared(runtimeColorShared[a_contextIndex]) ||
		!isValidShared(runtimeDepthShared[a_contextIndex]) ||
		!isValidShared(runtimeMotionShared[a_contextIndex]) ||
		!isValidShared(runtimeReactiveShared[a_contextIndex]) ||
		!isValidShared(runtimeTransparencyShared[a_contextIndex]) ||
		!isValidShared(runtimeOutputShared[a_contextIndex])) {
		return LifecycleResult::Failed;
	}

	RuntimeCommandContext* commandContext = nullptr;
	const auto acquireResult = AcquireRuntimeCommandContext(commandContext);
	if (acquireResult != LifecycleResult::Ready)
		return acquireResult;
	auto* commandAllocator = commandContext->commandAllocator.get();
	auto* commandList = commandContext->commandList.get();
	if (!commandAllocator || !commandList)
		return LifecycleResult::Failed;

	const bool annotateDispatch = state && state->frameAnnotations;
	if (annotateDispatch) {
		state->BeginPerfEvent("FSR Runtime Dispatch");
	}

	bool dispatchOk = false;
	bool commandListSubmitted = false;
	bool commandFenceRecorded = false;
	try {
		auto copyIntoShared = [&](const char* a_label, ID3D11Resource* a_source, const D3D11_TEXTURE2D_DESC& a_sourceDesc, const std::unique_ptr<WrappedResource>& a_destination, const D3D11_TEXTURE2D_DESC& a_destinationDesc) {
			if (!a_source || !a_destination || !a_destination->resource11) {
				logger::error("[FidelityFX] Cannot copy runtime {} input: source or shared destination is missing.", a_label);
				return false;
			}
			if (a_sourceDesc.Format != a_destinationDesc.Format ||
				a_sourceDesc.SampleDesc.Count != a_destinationDesc.SampleDesc.Count) {
				logger::error(
					"[FidelityFX] Cannot copy runtime {} input: source format/sample {}x{} differs from shared {}x{}.",
					a_label,
					static_cast<uint32_t>(a_sourceDesc.Format),
					a_sourceDesc.SampleDesc.Count,
					static_cast<uint32_t>(a_destinationDesc.Format),
					a_destinationDesc.SampleDesc.Count);
				return false;
			}

			const uint32_t copyWidth = std::min(a_sourceDesc.Width, a_destinationDesc.Width);
			const uint32_t copyHeight = std::min(a_sourceDesc.Height, a_destinationDesc.Height);
			if (!copyWidth || !copyHeight) {
				logger::error(
					"[FidelityFX] Cannot copy runtime {} input with zero extent (source {}x{}, shared {}x{}).",
					a_label,
					a_sourceDesc.Width,
					a_sourceDesc.Height,
					a_destinationDesc.Width,
					a_destinationDesc.Height);
				return false;
			}

			D3D11_BOX sourceBox{};
			sourceBox.left = 0;
			sourceBox.top = 0;
			sourceBox.front = 0;
			sourceBox.right = copyWidth;
			sourceBox.bottom = copyHeight;
			sourceBox.back = 1;
			swapChain.d3d11Context->CopySubresourceRegion(a_destination->resource11.get(), 0, 0, 0, 0, a_source, 0, &sourceBox);
			return true;
		};

		if (!copyIntoShared("color", a_color, colorDesc, runtimeColorShared[a_contextIndex], runtimeColorSharedDesc) ||
			!copyIntoShared("depth", a_depth, depthDesc, runtimeDepthShared[a_contextIndex], runtimeDepthSharedDesc) ||
			!copyIntoShared("motion-vector", a_motionVectors, motionDesc, runtimeMotionShared[a_contextIndex], runtimeMotionSharedDesc) ||
			!copyIntoShared("reactive-mask", a_reactiveMask, reactiveDesc, runtimeReactiveShared[a_contextIndex], runtimeReactiveSharedDesc) ||
			!copyIntoShared("transparency", a_transparencyCompositionMask, transparencyDesc, runtimeTransparencyShared[a_contextIndex], runtimeTransparencySharedDesc)) {
			dispatchOk = false;
		} else {
			const uint64_t d3d11SubmitFence = runtimeFenceValue++;
			DX::ThrowIfFailed(swapChain.d3d11Context->Signal(runtimeD3D11Fence.get(), d3d11SubmitFence));
			DX::ThrowIfFailed(swapChain.commandQueue->Wait(runtimeD3D12Fence.get(), d3d11SubmitFence));

			DX::ThrowIfFailed(commandAllocator->Reset());
			DX::ThrowIfFailed(commandList->Reset(commandAllocator, nullptr));

			std::array<D3D12_RESOURCE_BARRIER, 6> beginBarriers = {
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeColorShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeDepthShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeMotionShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeReactiveShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeTransparencyShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeOutputShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			commandList->ResourceBarrier(static_cast<UINT>(beginBarriers.size()), beginBarriers.data());

			ffx::DispatchDescUpscale dispatchParameters{};
			dispatchParameters.commandList = commandList;
			dispatchParameters.color = ffxApiGetResourceDX12(runtimeColorShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.depth = ffxApiGetResourceDX12(runtimeDepthShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.motionVectors = ffxApiGetResourceDX12(runtimeMotionShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.exposure = FfxApiResource({});
			dispatchParameters.reactive = ffxApiGetResourceDX12(runtimeReactiveShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.transparencyAndComposition = ffxApiGetResourceDX12(runtimeTransparencyShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			dispatchParameters.output = ffxApiGetResourceDX12(runtimeOutputShared[a_contextIndex]->resource.get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
			dispatchParameters.jitterOffset = { -upscaling.jitter.x, -upscaling.jitter.y };
			dispatchParameters.motionVectorScale = { a_motionVectorScaleX, a_motionVectorScaleY };
			dispatchParameters.renderSize = { a_renderWidth, a_renderHeight };
			dispatchParameters.upscaleSize = { a_displayWidth, a_displayHeight };
			const auto sharpening = ResolveFsrSharpeningSettings(a_sharpness);
			dispatchParameters.enableSharpening = sharpening.enabled;
			dispatchParameters.sharpness = sharpening.sharpness;
			dispatchParameters.frameTimeDelta = *globals::game::deltaTime * 1000.f;
			dispatchParameters.preExposure = 1.0f;
			dispatchParameters.reset = upscaling.ShouldResetHistoryThisFrame();
			dispatchParameters.cameraNear = *globals::game::cameraNear;
			dispatchParameters.cameraFar = *globals::game::cameraFar;
			dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
			dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
			dispatchParameters.flags = 0;
			const bool runtimeFallbackReset = runtimeFallbackResetDispatchesRemaining > 0;
			const bool runtimeResumeReset = runtimeResumeResetDispatchesRemaining > 0;
			dispatchParameters.reset = dispatchParameters.reset || runtimeFallbackReset || runtimeResumeReset;

			bool dispatchFaulted = false;
			dispatchOk = DispatchRuntimeUpscalerProtected(runtimeUpscalerContexts[a_contextIndex], dispatchParameters, dispatchFaulted);
			if (dispatchFaulted) {
				commandContext->fenceValue = 0;
				runtimeUpscalerContextIndeterminate[a_contextIndex] = true;
				runtimeFallbackResetDispatchesRemaining = std::max(runtimeFallbackResetDispatchesRemaining, runtimeUpscalerContextCount);
				QuarantineRuntimeUpscalerForSession("a runtime upscaler dispatch fault");
				if (swapChain.d3d12Device) {
					const HRESULT removedReason = swapChain.d3d12Device->GetDeviceRemovedReason();
					if (FAILED(removedReason)) {
						logger::critical("[FidelityFX] Runtime D3D12 device was removed during the provider dispatch fault (HRESULT 0x{:08X}).", static_cast<uint32_t>(removedReason));
					}
				}
				logger::critical("[FidelityFX] Runtime upscaler dispatch faulted for context {}; the provider will not be used again this session.", a_contextIndex);
				if (annotateDispatch)
					state->EndPerfEvent();
				const auto result = ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler dispatch fault");
				runtimeUpscalerQuarantineRetirement = NormalizeRuntimeQuarantineResult(result);
				return result;
			}
			if (dispatchOk && runtimeFallbackReset)
				runtimeFallbackResetDispatchesRemaining--;
			if (dispatchOk && runtimeResumeReset)
				runtimeResumeResetDispatchesRemaining--;
			if (!dispatchOk) {
				logger::error("[FidelityFX] Runtime upscaler dispatch failed for context {}.", a_contextIndex);
			}

			std::array<D3D12_RESOURCE_BARRIER, 6> endBarriers = {
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeColorShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeDepthShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeMotionShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeReactiveShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeTransparencyShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(runtimeOutputShared[a_contextIndex]->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
			};
			commandList->ResourceBarrier(static_cast<UINT>(endBarriers.size()), endBarriers.data());

			DX::ThrowIfFailed(commandList->Close());

			ID3D12CommandList* commandListsToExecute[] = { commandList };
			swapChain.commandQueue->ExecuteCommandLists(1, commandListsToExecute);
			commandListSubmitted = true;

			const uint64_t d3d12SubmitFence = runtimeFenceValue++;
			DX::ThrowIfFailed(swapChain.commandQueue->Signal(runtimeD3D12Fence.get(), d3d12SubmitFence));
			commandContext->fenceValue = d3d12SubmitFence;
			commandFenceRecorded = true;
			DX::ThrowIfFailed(swapChain.d3d11Context->Wait(runtimeD3D11Fence.get(), d3d12SubmitFence));

			if (dispatchOk) {
				if (outputDesc.Format != runtimeOutputSharedDesc.Format ||
					outputDesc.SampleDesc.Count != runtimeOutputSharedDesc.SampleDesc.Count) {
					logger::error(
						"[FidelityFX] Cannot copy runtime output: destination format/sample {}x{} differs from shared {}x{}.",
						static_cast<uint32_t>(outputDesc.Format),
						outputDesc.SampleDesc.Count,
						static_cast<uint32_t>(runtimeOutputSharedDesc.Format),
						runtimeOutputSharedDesc.SampleDesc.Count);
					dispatchOk = false;
				} else {
					const uint32_t copyWidth = std::min({ a_displayWidth, outputDesc.Width, runtimeOutputSharedDesc.Width });
					const uint32_t copyHeight = std::min({ a_displayHeight, outputDesc.Height, runtimeOutputSharedDesc.Height });
					if (!copyWidth || !copyHeight) {
						dispatchOk = false;
					} else {
						D3D11_BOX outputBox{};
						outputBox.left = 0;
						outputBox.top = 0;
						outputBox.front = 0;
						outputBox.right = copyWidth;
						outputBox.bottom = copyHeight;
						outputBox.back = 1;
						swapChain.d3d11Context->CopySubresourceRegion(a_output, 0, 0, 0, 0, runtimeOutputShared[a_contextIndex]->resource11.get(), 0, &outputBox);
					}
				}
			}
		}
	} catch (const std::exception& e) {
		logger::error("[FidelityFX] Runtime upscaler dispatch path failed for context {}: {}", a_contextIndex, e.what());
		dispatchOk = false;
	} catch (...) {
		logger::error("[FidelityFX] Runtime upscaler dispatch path failed for context {}.", a_contextIndex);
		dispatchOk = false;
	}

	if (commandListSubmitted && !commandFenceRecorded) {
		const uint64_t rescueFence = runtimeFenceValue++;
		if (swapChain.commandQueue && runtimeD3D12Fence && SUCCEEDED(swapChain.commandQueue->Signal(runtimeD3D12Fence.get(), rescueFence))) {
			commandContext->fenceValue = rescueFence;
		} else {
			QuarantineRuntimeUpscalerForSession("an untracked runtime command submission");
		}
	}

	if (!dispatchOk && swapChain.d3d12Device) {
		const HRESULT removedReason = swapChain.d3d12Device->GetDeviceRemovedReason();
		if (FAILED(removedReason)) {
			logger::critical("[FidelityFX] Runtime D3D12 device was removed after dispatch failure (HRESULT 0x{:08X}).", static_cast<uint32_t>(removedReason));
			for (uint32_t i = 0; i < std::size(runtimeUpscalerContexts); ++i)
				runtimeUpscalerContextIndeterminate[i] = runtimeUpscalerContexts[i] != nullptr;
			QuarantineRuntimeUpscalerForSession("runtime-provider D3D12 device removal");
		}
	}

	if (annotateDispatch)
		state->EndPerfEvent();

	if (dispatchOk)
		return LifecycleResult::Ready;
	if (runtimeUpscalerContextIndeterminate[a_contextIndex])
		return runtimeUpscalerQuarantineRetirement;
	return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler dispatch");
}

bool FidelityFX::UpscaleRegion(uint32_t a_contextIndex, ID3D11Resource* a_color, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_output,
	uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight,
	float a_motionVectorScaleX, float a_motionVectorScaleY, float a_sharpness)
{
	if (!a_color || !a_depth || !a_motionVectors || !a_reactiveMask || !a_transparencyCompositionMask || !a_output ||
		!a_renderWidth || !a_renderHeight || !a_displayWidth || !a_displayHeight) {
		return false;
	}

	const bool runtimeFsr4Requested = ShouldRequestRuntimeFsr4();
	const bool runtimeRequested = runtimeFsr4Requested || ShouldUseRuntimeUpscalerForFSR();
	const uint32_t requestedRuntimeVersion = runtimeFsr4Requested ? FFX_UPSCALER_VERSION : Fsr3Version;
	const uint32_t runtimeContextCount = 1u;
	const bool shaderCompilationActive = globals::shaderCache && globals::shaderCache->IsCompiling();
	const auto* viewport = globals::game::graphicsState;
	const float2 fullScreenSize{
		static_cast<float>(viewport ? viewport->screenWidth : 0),
		static_cast<float>(viewport ? viewport->screenHeight : 0)
	};
	const auto renderSize = Util::ConvertToDynamic(fullScreenSize);
	const uint32_t fullDisplayWidth = static_cast<uint32_t>(fullScreenSize.x);
	const uint32_t fullDisplayHeight = static_cast<uint32_t>(fullScreenSize.y);
	const uint32_t requestedFullRenderWidth = static_cast<uint32_t>(renderSize.x);
	const uint32_t requestedFullRenderHeight = static_cast<uint32_t>(renderSize.y);
	const uint32_t fullRenderWidth = runtimeFsr4Requested ? fullDisplayWidth : requestedFullRenderWidth;
	const uint32_t fullRenderHeight = runtimeFsr4Requested ? fullDisplayHeight : requestedFullRenderHeight;
	const bool existingRuntimeCompatible =
		runtimeUpscalerContextCount == runtimeContextCount &&
		runtimeUpscalerContexts[0] &&
		!runtimeUpscalerContextIndeterminate[0] &&
		runtimeUpscalerMaxRenderWidth == fullRenderWidth &&
		runtimeUpscalerMaxRenderHeight == fullRenderHeight &&
		runtimeUpscalerMaxDisplayWidth == fullDisplayWidth &&
		runtimeUpscalerMaxDisplayHeight == fullDisplayHeight &&
		runtimeUpscalerRequestedVersion == requestedRuntimeVersion &&
		runtimeUpscalerSupportCheckKnown &&
		runtimeUpscalerSupportConfirmed &&
		IsRuntimeUpscalerProviderMatchingRequestedVersion();
	const bool runtimePathEligible = runtimeRequested && CanUseRuntimeUpscalerPath();
	const bool runtimeDeferredForShaderCompilation =
		runtimePathEligible && shaderCompilationActive && !existingRuntimeCompatible;
	if (runtimeDeferredForShaderCompilation) {
		if (runtimeResumeResetDispatchesRemaining == 0)
			runtimeFallbackResetDispatchesRemaining = std::max(runtimeFallbackResetDispatchesRemaining, runtimeContextCount);
		runtimeResumeResetDispatchesRemaining = std::max(runtimeResumeResetDispatchesRemaining, runtimeContextCount);
	}
	const bool runtimeSelected =
		runtimePathEligible && fullRenderWidth && fullRenderHeight && fullDisplayWidth && fullDisplayHeight &&
		(!shaderCompilationActive || existingRuntimeCompatible);
	static bool loggedRuntimeDeferredForShaderCompilation = false;
	if (runtimeDeferredForShaderCompilation) {
		if (!loggedRuntimeDeferredForShaderCompilation) {
			logger::info("[FidelityFX] Deferring required DX12 runtime context creation while shader compilation is active; using {}.", GetHostFsrSdkLabel());
			loggedRuntimeDeferredForShaderCompilation = true;
		}
	} else {
		loggedRuntimeDeferredForShaderCompilation = false;
	}

	if (runtimeSelected) {
		auto tryRuntimeUpscaler = [&](uint32_t a_requestedVersion, uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight) {
			try {
				const auto contextResult = EnsureRuntimeUpscalerContexts(
					a_fullRenderWidth,
					a_fullRenderHeight,
					fullDisplayWidth,
					fullDisplayHeight,
					runtimeContextCount,
					a_requestedVersion);
				if (contextResult != LifecycleResult::Ready)
					return contextResult;

				const auto dispatchResult = DispatchRuntimeUpscalerSingle(
						a_contextIndex,
						a_color,
						a_depth,
						a_motionVectors,
						a_reactiveMask,
						a_transparencyCompositionMask,
						a_output,
						a_renderWidth,
						a_renderHeight,
						a_displayWidth,
						a_displayHeight,
						a_motionVectorScaleX,
						a_motionVectorScaleY,
						a_sharpness);
				if (dispatchResult == LifecycleResult::Ready)
					RecordRuntimeUpscalerFramePath(GetRuntimeUpscalerProviderFramePath(a_requestedVersion));
				return dispatchResult;
			} catch (const std::exception& e) {
				logger::error("[FidelityFX] Runtime upscaler setup/dispatch for FSR version {} threw an exception: {}",
					UpscalerVersionToString(a_requestedVersion),
					e.what());
			} catch (...) {
				logger::error("[FidelityFX] Runtime upscaler setup/dispatch for FSR version {} threw an unknown exception.",
					UpscalerVersionToString(a_requestedVersion));
			}

			return ResolveRuntimeUpscalerLifecycleFailure("runtime upscaler setup/dispatch");
		};

		const auto runtimeResult = tryRuntimeUpscaler(requestedRuntimeVersion, fullRenderWidth, fullRenderHeight);
		if (runtimeResult == LifecycleResult::Ready)
			return true;

		runtimeFallbackResetDispatchesRemaining = std::max(runtimeFallbackResetDispatchesRemaining, runtimeContextCount);
		runtimeResumeResetDispatchesRemaining = std::max(runtimeResumeResetDispatchesRemaining, runtimeContextCount);
		if (runtimeResult == LifecycleResult::DeviceLost || runtimeResult == LifecycleResult::RuntimeDeviceLost)
			return false;
		if (runtimeResult == LifecycleResult::Pending) {
			// A busy command slot or an in-progress teardown is transient. Use the
			// independent host path for this frame without poisoning the provider.
		} else {
			if (runtimeFsr4Requested)
				LatchRuntimeFsr4Failure();
			QuarantineRuntimeUpscalerForSession(runtimeFsr4Requested ? "an FSR4 setup or dispatch failure" : "an FSR3 runtime setup or dispatch failure");
		}

		if (std::ranges::any_of(runtimeUpscalerContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; }))
			return false;
	}

	if (!runtimeRequested) {
		runtimeFallbackResetDispatchesRemaining = 0;
		runtimeResumeResetDispatchesRemaining = 0;
	}

	static bool loggedHostFsrResourceMismatch = false;
	if (fsrHostStateQuarantined ||
		std::ranges::any_of(fsrContextIndeterminate, [](bool a_indeterminate) { return a_indeterminate; }) ||
		!fsrScratchBuffer ||
		a_contextIndex >= fsrContextCount ||
		a_contextIndex >= fsrContextValid.size() ||
		!fsrContextValid[a_contextIndex] ||
		a_renderWidth > fsrContextMaxRenderWidth ||
		a_renderHeight > fsrContextMaxRenderHeight ||
		a_displayWidth > fsrContextDisplayWidth ||
		a_displayHeight > fsrContextDisplayHeight) {
		if (!loggedHostFsrResourceMismatch) {
			logger::warn(
				"[FidelityFX] Host FSR resources are unavailable or incompatible for context {} (render {}x{} of {}x{}, display {}x{} of {}x{}).",
				a_contextIndex,
				a_renderWidth,
				a_renderHeight,
				fsrContextMaxRenderWidth,
				fsrContextMaxRenderHeight,
				a_displayWidth,
				a_displayHeight,
				fsrContextDisplayWidth,
				fsrContextDisplayHeight);
			loggedHostFsrResourceMismatch = true;
		}
		return false;
	}
	loggedHostFsrResourceMismatch = false;

	auto context = globals::d3d::context;
	auto state = globals::state;
	if (!context || !state)
		return false;

	auto& upscaling = globals::features::upscaling;
	auto jitter = upscaling.jitter;
	const auto fallbackFramePath =
		runtimeRequested ? RuntimeUpscalerFramePath::kHostFsr31Fallback : RuntimeUpscalerFramePath::kHostFsr31;
	RecordRuntimeUpscalerFramePath(fallbackFramePath);

	if (state->frameAnnotations) {
		state->BeginPerfEvent("FSR Dispatch");
	}

	FfxFsr3DispatchUpscaleDescription dispatchParameters{};
	dispatchParameters.commandList = ffxGetCommandListDX11(context);
	dispatchParameters.color = ffxGetResource(a_color, L"FSR3_Input_OutputColor");
	dispatchParameters.depth = ffxGetResource(a_depth, L"FSR3_InputDepth");
	dispatchParameters.motionVectors = ffxGetResource(a_motionVectors, L"FSR3_InputMotionVectors");
	dispatchParameters.exposure = ffxGetResource(nullptr, L"FSR3_InputExposure");
	dispatchParameters.upscaleOutput = ffxGetResource(a_output, L"FSR3_OutputColor");
	dispatchParameters.reactive = ffxGetResource(a_reactiveMask, L"FSR3_InputReactiveMap");
	dispatchParameters.transparencyAndComposition = ffxGetResource(a_transparencyCompositionMask, L"FSR3_TransparencyAndCompositionMap");
	dispatchParameters.motionVectorScale.x = a_motionVectorScaleX;
	dispatchParameters.motionVectorScale.y = a_motionVectorScaleY;
	dispatchParameters.renderSize.width = a_renderWidth;
	dispatchParameters.renderSize.height = a_renderHeight;
	dispatchParameters.upscaleSize.width = a_displayWidth;
	dispatchParameters.upscaleSize.height = a_displayHeight;
	dispatchParameters.jitterOffset.x = -jitter.x;
	dispatchParameters.jitterOffset.y = -jitter.y;
	dispatchParameters.frameTimeDelta = *globals::game::deltaTime * 1000.f;
	dispatchParameters.cameraFar = *globals::game::cameraFar;
	dispatchParameters.cameraNear = *globals::game::cameraNear;
	const auto sharpening = ResolveFsrSharpeningSettings(a_sharpness);
	dispatchParameters.enableSharpening = sharpening.enabled;
	dispatchParameters.sharpness = sharpening.sharpness;
	dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
	dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
	const bool runtimeFallbackReset = runtimeRequested && runtimeFallbackResetDispatchesRemaining > 0;
	dispatchParameters.reset = globals::features::upscaling.ShouldResetHistoryThisFrame() || runtimeFallbackReset;
	dispatchParameters.preExposure = 1.0f;
	dispatchParameters.flags = 0;

	bool hostDispatchCrashed = false;
	const bool dispatchOK = DispatchHostFsr3UpscaleProtected(fsrContext[a_contextIndex], dispatchParameters, hostDispatchCrashed);
	if (dispatchOK && runtimeFallbackReset)
		runtimeFallbackResetDispatchesRemaining--;
	if (!dispatchOK && !hostDispatchCrashed) {
		logger::critical("[FidelityFX] Failed to dispatch region upscaling for context {}!", a_contextIndex);
	}
	if (hostDispatchCrashed) {
		fsrContextIndeterminate[a_contextIndex] = true;
		fsrHostStateQuarantined = true;
		if (globals::d3d::device) {
			const HRESULT removedReason = globals::d3d::device->GetDeviceRemovedReason();
			if (FAILED(removedReason))
				logger::critical("[FidelityFX] Host D3D11 device was removed during the FSR3 dispatch fault (HRESULT 0x{:08X}).", static_cast<uint32_t>(removedReason));
		}
		if (!fsrDispatchCrashLogged) {
			logger::critical("[FidelityFX] Region FSR3 dispatch crashed for context {}; retaining the indeterminate SDK state and disabling host FSR for this session.", a_contextIndex);
			fsrDispatchCrashLogged = true;
		}
	}

	if (state->frameAnnotations)
		state->EndPerfEvent();

	return dispatchOK;
}

bool FidelityFX::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_depth, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness)
{
	auto state = globals::state;
	if (!a_upscalingTexture || !a_depth || !a_reactiveMask || !a_transparencyCompositionMask || !a_motionVectors || !state)
		return false;

	const auto* viewport = globals::game::graphicsState;
	const float2 screenSize{
		static_cast<float>(viewport ? viewport->screenWidth : 0),
		static_cast<float>(viewport ? viewport->screenHeight : 0)
	};
	const auto renderSize = Util::ConvertToDynamic(screenSize);

	const bool dispatched = UpscaleRegion(
			0,
			a_upscalingTexture,
			a_depth,
			a_motionVectors,
			a_reactiveMask,
			a_transparencyCompositionMask,
			a_upscalingTexture,
			static_cast<uint32_t>(renderSize.x),
			static_cast<uint32_t>(renderSize.y),
			static_cast<uint32_t>(screenSize.x),
			static_cast<uint32_t>(screenSize.y),
			renderSize.x,
			renderSize.y,
			a_sharpness);
	if (!dispatched) {
		logger::error("[FidelityFX] Upscale dispatch failed.");
	}
	return dispatched;
}
