#include "FidelityFX.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <directx/d3dx12.h>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include "../../State.h"
#include "../../Utils/FileSystem.h"
#include "../HDRDisplay.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

ffxFunctions ffxModule;

std::vector<std::pair<std::string, std::string>> FidelityFX::dllVersions = {};

namespace
{
	constexpr wchar_t kFrameGenerationDllName[] = L"amd_fidelityfx_framegeneration_dx12.dll";
	constexpr wchar_t kLoaderDllName[] = L"amd_fidelityfx_loader_dx12.dll";
	constexpr wchar_t kUpscalerDllName[] = L"amd_fidelityfx_upscaler_dx12.dll";
	constexpr uint32_t kAmdVendorId = 0x1002u;
	constexpr uint32_t kNvidiaVendorId = 0x10DEu;
	constexpr uint32_t kRuntimeFsr315Version = FFX_UPSCALER_MAKE_VERSION(3u, 1u, 5u);

	void* s_fidelityFxDllDirectoryCookie = nullptr;

	bool UseSplitPerEyeFSRContexts()
	{
		return globals::game::isVR;
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

	bool IsLikelyRDNA4Adapter(const DXGI_ADAPTER_DESC& a_desc)
	{
		if (a_desc.VendorId != kAmdVendorId)
			return false;

		std::wstring wideDescription(a_desc.Description);
		const std::string description = ToUpperAscii(stl::utf16_to_utf8(wideDescription).value_or(""));

		if (description.find("RDNA4") != std::string::npos ||
		    description.find("RDNA 4") != std::string::npos ||
		    description.find("NAVI4") != std::string::npos ||
		    description.find("NAVI 4") != std::string::npos) {
			return true;
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
				if (parseEnd != modelText.c_str() && modelNumber >= 9000ul && modelNumber < 10000ul)
					return true;
			}
		}

		// Keep fallbacks for abbreviated naming variants that don't include full numeric model text.
		return description.find("RX 90") != std::string::npos ||
		       description.find("RX90") != std::string::npos ||
		       description.find("RADEON 90") != std::string::npos;
	}

	std::string UpscalerVersionToString(uint32_t a_version)
	{
		const uint32_t major = (a_version >> 22) & 0x3FFu;
		const uint32_t minor = (a_version >> 12) & 0x3FFu;
		const uint32_t patch = a_version & 0xFFFu;
		return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
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
		if (module) {
			return module;
		}

		a_error = GetLastError();
		logger::warn(
			"[FidelityFX] LoadLibraryEx failed for {} with error {}; retrying legacy LoadLibrary",
			stl::utf16_to_utf8(a_path.wstring()).value_or("<unknown>"),
			a_error);

		module = LoadLibraryW(a_path.c_str());
		if (module) {
			a_error = ERROR_SUCCESS;
			return module;
		}

		a_error = GetLastError();
		return nullptr;
	}

	bool QueryRuntimeUpscalerVersionId(ID3D12Device* a_device, uint32_t a_requestedVersion, uint64_t& a_versionId, std::string& a_versionName)
	{
		a_versionId = 0;
		a_versionName.clear();

		if (!a_device || !ffxModule.Query) {
			return false;
		}

		uint64_t versionCount = 0;
		ffxQueryDescGetVersions countQuery{};
		countQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
		countQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
		countQuery.device = a_device;
		countQuery.outputCount = &versionCount;

		auto countResult = ffxModule.Query(nullptr, &countQuery.header);
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

		auto versionsResult = ffxModule.Query(nullptr, &versionsQuery.header);
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
		kGenericOverrideOnly,
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
		ffx::CreateContextDescOverrideVersion& a_overrideVersionDesc)
	{
		a_context = nullptr;
		a_createDesc.header.pNext = nullptr;
		a_backendDesc.header.pNext = nullptr;
		a_versionDesc.header.pNext = nullptr;
		a_overrideVersionDesc.header.pNext = nullptr;

		switch (a_attempt) {
		case RuntimeUpscalerCreateAttempt::kGenericOverrideOnly:
			a_createDesc.header.pNext = &a_backendDesc.header;
			a_backendDesc.header.pNext = &a_overrideVersionDesc.header;
			break;
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
		const auto result = ffxModule.CreateContext(&createdContext, &a_createDesc.header, nullptr);
		if (result == FFX_API_RETURN_OK) {
			if (createdContext) {
				a_context = createdContext;
				return result;
			}

			return FFX_API_RETURN_ERROR;
		} else if (createdContext && ffxModule.DestroyContext) {
			(void)ffxModule.DestroyContext(&createdContext, nullptr);
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
	void DeleteWrappedResourceArray(WrappedResource* (&a_resources)[N])
	{
		for (auto*& resource : a_resources) {
			delete resource;
			resource = nullptr;
		}
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
}

void FidelityFX::LoadFFX()
{
	const std::filesystem::path pluginDir = std::filesystem::absolute(std::filesystem::path(FidelityFX::PluginDir));

	ResetRuntimeUpscalerTracking(true);
	EnsureFidelityFxDllDirectory(pluginDir);

	const std::filesystem::path framegenPath = pluginDir / kFrameGenerationDllName;
	const std::filesystem::path loaderPath = pluginDir / kLoaderDllName;
	const std::filesystem::path upscalerPath = pluginDir / kUpscalerDllName;

	const bool framegenDllExists = std::filesystem::exists(framegenPath);
	const bool upscalerDllExists = std::filesystem::exists(upscalerPath);
	DWORD framegenLoadError = ERROR_SUCCESS;
	DWORD upscalerLoadError = ERROR_SUCCESS;
	DWORD loaderLoadError = ERROR_SUCCESS;

	if (!module) {
		module = LoadFidelityFxDll(loaderPath, loaderLoadError);
	}
	if (!frameGenerationModule && framegenDllExists) {
		frameGenerationModule = LoadFidelityFxDll(framegenPath, framegenLoadError);
	}
	if (!runtimeUpscalerModule && upscalerDllExists) {
		runtimeUpscalerModule = LoadFidelityFxDll(upscalerPath, upscalerLoadError);
	}

	featureFSR3FG = frameGenerationModule != nullptr;
	featureRuntimeUpscaler = runtimeUpscalerModule != nullptr;

	FidelityFX::dllVersions = Util::EnumerateDllVersions(pluginDir);
	for (const auto& [name, versionStr] : FidelityFX::dllVersions)
		logger::info("[FidelityFX] {} version: {}", name, versionStr);

	if (module) {
		ffxLoadFunctions(&ffxModule, module);
		logger::info("[FidelityFX] Loader DLL loaded successfully from plugin directory");
	} else {
		logger::error("[FidelityFX] Failed to load {} from plugin directory (Win32 error {})",
			stl::utf16_to_utf8(kLoaderDllName).value_or("loader DLL"),
			loaderLoadError);
	}

	if (featureFSR3FG) {
		logger::info("[FidelityFX] Frame generation DLL loaded and available");
	} else if (framegenDllExists) {
		logger::warn("[FidelityFX] Frame generation DLL found but failed to load (Win32 error {}) - FSR3 frame generation disabled",
			framegenLoadError);
	} else {
		logger::warn("[FidelityFX] Frame generation DLL not found - FSR3 frame generation disabled");
	}

	if (featureRuntimeUpscaler) {
		logger::info("[FidelityFX] Runtime upscaler DLL loaded; runtime availability will be verified during context creation");
	} else if (upscalerDllExists) {
		logger::warn("[FidelityFX] Runtime upscaler DLL found but failed to load (Win32 error {}) - runtime FSR path disabled",
			upscalerLoadError);
	} else {
		logger::warn("[FidelityFX] Runtime upscaler DLL not found - runtime FSR path disabled");
	}
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
		return true;

	const uint32_t requestedVersion = runtimeUpscalerRequestedVersion ? runtimeUpscalerRequestedVersion : GetPreferredRuntimeUpscalerVersion();
	return RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, requestedVersion);
}

bool FidelityFX::IsRuntimeUpscalerFailureLatched() const
{
	return runtimeUpscalerFailureLatched;
}

bool FidelityFX::IsRuntimeFsr4FailureLatched() const
{
	return runtimeFsr4FailureLatched;
}

const char* FidelityFX::GetRuntimeUpscalerLastFramePathLabel() const
{
	if (!runtimeUpscalerLastFramePathValid)
		return "Pending FSR dispatch";

	switch (runtimeUpscalerLastFramePath) {
	case RuntimeUpscalerFramePath::kHostFsr31:
		return "Host FSR 3.1.5";
	case RuntimeUpscalerFramePath::kRuntimeFsr31:
		return "Runtime FSR 3.1.5";
	case RuntimeUpscalerFramePath::kRuntimeFsr4:
		return "Runtime FSR 4.1";
	case RuntimeUpscalerFramePath::kHostFsr31Fallback:
		return "Host FSR 3.1.5 fallback";
	case RuntimeUpscalerFramePath::kInactive:
	default:
		return "Pending FSR dispatch";
	}
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

void FidelityFX::LatchRuntimeUpscalerFailure()
{
	if (runtimeUpscalerFailureLatched)
		return;

	runtimeUpscalerFailureLatched = true;
	logger::warn("[FidelityFX] Runtime upscaler path latched off after failure; using host FSR 3.1.5 until runtime resources are reset, FSR resources are rebuilt, or the method changes.");
}

void FidelityFX::LatchRuntimeFsr4Failure()
{
	if (runtimeFsr4FailureLatched)
		return;

	runtimeFsr4FailureLatched = true;
	logger::warn("[FidelityFX] Runtime FSR4 path failed; falling back to runtime FSR 3.1.5 until runtime resources are reset, FSR resources are rebuilt, or the method changes.");
}

FidelityFX::RuntimeUpscalerFramePath FidelityFX::GetRuntimeUpscalerProviderFramePath(uint32_t a_requestedVersion) const
{
	if (RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, FFX_UPSCALER_VERSION))
		return RuntimeUpscalerFramePath::kRuntimeFsr4;

	if (RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, kRuntimeFsr315Version))
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

void FidelityFX::Present(bool a_useFrameGeneration, bool a_isHDR)
{
	auto& upscaling = globals::features::upscaling;
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

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

	auto state = globals::state;
	auto renderSize = state->screenSize * upscaling.resolutionScale;

	configParameters.generationRect.left = 0;
	configParameters.generationRect.top = 0;
	configParameters.generationRect.width = swapChain.swapChainDesc.Width;
	configParameters.generationRect.height = swapChain.swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok)
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

	if (ffx::Configure(swapChainContext, uiConfig) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to configure UI composition!");

	if (a_useFrameGeneration) {
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

		if (ffx::Dispatch(frameGenContext, dispatchParameters, cameraConfig) != ffx::ReturnCode::Ok)
			logger::critical("[FidelityFX] Failed to dispatch frame generation!");
	}

	frameID++;
	isFrameGenActive = a_useFrameGeneration;
}

void FidelityFX::CreateFSRResources()
{
	auto state = globals::state;
	if (!state) {
		logger::critical("[FidelityFX] Missing global state when creating FSR resources.");
		fsrContextCount = 0;
		return;
	}

	const bool splitPerEyeContexts = UseSplitPerEyeFSRContexts();

	WaitForRuntimeUpscalerIdle();
	DestroyRuntimeUpscalerContexts(false);
	DestroyRuntimeUpscalerResources(false);

	if (fsrScratchBuffer) {
		logger::warn("[FidelityFX] FSR resources already created, skipping allocation");
		return;
	}

	ResetRuntimeUpscalerTracking(true);

	auto fsrDevice = ffxGetDeviceDX11_Fsr31(globals::d3d::device);

	const uint32_t numContexts = splitPerEyeContexts ? 2u : 1u;
	const size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(numContexts);
	fsrScratchBuffer = calloc(scratchBufferSize, 1);
	if (!fsrScratchBuffer) {
		logger::critical("[FidelityFX] Failed to allocate FSR3 scratch buffer memory!");
		fsrContextCount = 0;
		return;
	}
	memset(fsrScratchBuffer, 0, scratchBufferSize);

	FfxInterface fsrInterface{};
	if (ffxGetInterfaceDX11(&fsrInterface, fsrDevice, fsrScratchBuffer, scratchBufferSize, numContexts) != FFX_OK) {
		logger::critical("[FidelityFX] Failed to initialize FSR3 backend interface!");
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
		fsrContextCount = 0;
		return;
	}

	auto screenSize = state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	const uint32_t displayWidth = static_cast<uint32_t>(splitPerEyeContexts ? screenSize.x / 2 : screenSize.x);
	const uint32_t displayHeight = static_cast<uint32_t>(screenSize.y);
	const uint32_t renderWidth = static_cast<uint32_t>(splitPerEyeContexts ? renderSize.x / 2 : renderSize.x);
	const uint32_t renderHeight = static_cast<uint32_t>(renderSize.y);

	for (uint32_t i = 0; i < numContexts; ++i) {
		FfxFsr3ContextDescription contextDescription{};
		contextDescription.maxRenderSize.width = renderWidth;
		contextDescription.maxRenderSize.height = renderHeight;
		contextDescription.maxUpscaleSize.width = displayWidth;
		contextDescription.maxUpscaleSize.height = displayHeight;
		contextDescription.displaySize.width = displayWidth;
		contextDescription.displaySize.height = displayHeight;
		contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE | FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
		contextDescription.backendInterfaceUpscaling = fsrInterface;

		if (ffxFsr3ContextCreate(&fsrContext[i], &contextDescription) != FFX_OK) {
			logger::critical("[FidelityFX] Failed to initialize FSR3 context for eye {}!", i);
			for (uint32_t j = 0; j < i; ++j)
				ffxFsr3ContextDestroy(&fsrContext[j]);
			free(fsrScratchBuffer);
			fsrScratchBuffer = nullptr;
			fsrContextCount = 0;
			return;
		}
	}

	fsrContextCount = numContexts;
	logger::info("[FidelityFX] Created {} FSR3 contexts (Display: {}x{}, Render: {}x{}, SplitPerEye={})",
		numContexts, displayWidth, displayHeight, renderWidth, renderHeight, splitPerEyeContexts);
}

void FidelityFX::DestroyRuntimeUpscalerContexts(bool a_waitForIdle)
{
	if (a_waitForIdle)
		WaitForRuntimeUpscalerIdle();

	for (uint32_t i = 0; i < std::size(runtimeUpscalerContexts); ++i) {
		if (runtimeUpscalerContexts[i] && ffx::DestroyContext(runtimeUpscalerContexts[i]) != ffx::ReturnCode::Ok)
			logger::warn("[FidelityFX] Failed to destroy runtime upscaler context {} cleanly.", i);
		runtimeUpscalerContexts[i] = nullptr;
	}

	runtimeUpscalerContextCount = 0;
	runtimeUpscalerMaxRenderWidth = 0;
	runtimeUpscalerMaxRenderHeight = 0;
	runtimeUpscalerMaxDisplayWidth = 0;
	runtimeUpscalerMaxDisplayHeight = 0;
	runtimeUpscalerRequestedVersion = 0;
}

void FidelityFX::WaitForRuntimeUpscalerIdle()
{
	auto& swapChain = globals::features::upscaling.dx12SwapChain;
	if (!runtimeD3D12Fence && !runtimeD3D11Fence)
		return;

	auto waitForFence = [&](uint64_t a_value) {
		if (!runtimeD3D12Fence)
			return true;
		if (runtimeD3D12Fence->GetCompletedValue() >= a_value)
			return true;

		winrt::handle fenceEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr));
		if (!fenceEvent)
			return false;
		if (FAILED(runtimeD3D12Fence->SetEventOnCompletion(a_value, fenceEvent.get())))
			return false;

		return WaitForSingleObject(fenceEvent.get(), 5000) == WAIT_OBJECT_0;
	};

	try {
		if (swapChain.d3d11Context && runtimeD3D11Fence && runtimeD3D12Fence) {
			const uint64_t d3d11FenceValue = runtimeFenceValue++;
			DX::ThrowIfFailed(swapChain.d3d11Context->Signal(runtimeD3D11Fence.get(), d3d11FenceValue));
			swapChain.d3d11Context->Flush();
			if (!waitForFence(d3d11FenceValue))
				logger::warn("[FidelityFX] Timed out waiting for runtime upscaler D3D11 work before teardown.");
		} else if (globals::d3d::context) {
			globals::d3d::context->Flush();
		}

		if (swapChain.commandQueue && runtimeD3D12Fence) {
			const uint64_t d3d12FenceValue = runtimeFenceValue++;
			DX::ThrowIfFailed(swapChain.commandQueue->Signal(runtimeD3D12Fence.get(), d3d12FenceValue));
			if (!waitForFence(d3d12FenceValue))
				logger::warn("[FidelityFX] Timed out waiting for runtime upscaler D3D12 work before teardown.");
		}
	} catch (const std::exception& e) {
		logger::warn("[FidelityFX] Failed to wait for runtime upscaler idle before teardown: {}", e.what());
	} catch (...) {
		logger::warn("[FidelityFX] Failed to wait for runtime upscaler idle before teardown.");
	}
}

void FidelityFX::DestroyRuntimeUpscalerResources(bool a_waitForIdle)
{
	if (a_waitForIdle)
		WaitForRuntimeUpscalerIdle();

	DeleteWrappedResourceArray(runtimeColorShared);
	DeleteWrappedResourceArray(runtimeDepthShared);
	DeleteWrappedResourceArray(runtimeMotionShared);
	DeleteWrappedResourceArray(runtimeReactiveShared);
	DeleteWrappedResourceArray(runtimeTransparencyShared);
	DeleteWrappedResourceArray(runtimeOutputShared);

	runtimeColorSharedDesc = {};
	runtimeDepthSharedDesc = {};
	runtimeMotionSharedDesc = {};
	runtimeReactiveSharedDesc = {};
	runtimeTransparencySharedDesc = {};
	runtimeOutputSharedDesc = {};
}

void FidelityFX::ResetRuntimeUpscalerResources(bool a_invalidateProviderCache)
{
	WaitForRuntimeUpscalerIdle();
	DestroyRuntimeUpscalerContexts(false);
	DestroyRuntimeUpscalerResources(false);
	ResetRuntimeUpscalerTracking(a_invalidateProviderCache);
}

void FidelityFX::DestroyFSRResources()
{
	const uint32_t numContexts = fsrContextCount;
	if (numContexts == 0 && fsrScratchBuffer)
		logger::warn("[FidelityFX] DestroyFSRResources called with unknown context count; skipping context destruction to avoid mismatched teardown.");
	for (uint32_t i = 0; i < numContexts; ++i) {
		if (ffxFsr3ContextDestroy(&fsrContext[i]) != FFX_OK)
			logger::critical("[FidelityFX] Failed to destroy FSR3 context for eye {}!", i);
	}
	fsrContextCount = 0;

	if (fsrScratchBuffer) {
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
	}

	WaitForRuntimeUpscalerIdle();
	DestroyRuntimeUpscalerContexts(false);
	DestroyRuntimeUpscalerResources(false);

	runtimeD3D11Fence = nullptr;
	runtimeD3D12Fence = nullptr;
	runtimeFenceValue = 1;
	runtimeCommandFrameIndex = 0;
	fsrDispatchCrashLogged = false;
	ResetRuntimeUpscalerTracking(true);
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

bool FidelityFX::IsRuntimeFsr4AutoEligible() const
{
	DXGI_ADAPTER_DESC adapterDesc{};
	if (!TryGetCurrentAdapterDesc(adapterDesc))
		return false;

	return adapterDesc.VendorId == kAmdVendorId && IsLikelyRDNA4Adapter(adapterDesc);
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
	if (runtimeUpscalerFailureLatched)
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
	return ShouldRequestRuntimeFsr4() ? FFX_UPSCALER_VERSION : kRuntimeFsr315Version;
}

bool FidelityFX::EnsureRuntimeUpscalerInterop()
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

	auto clearRuntimeFenceState = [&]() {
		runtimeD3D11Fence = nullptr;
		runtimeD3D12Fence = nullptr;
	};

	auto clearRuntimeD3D12State = [&]() {
		swapChain.d3d12Device = nullptr;
		swapChain.commandQueue = nullptr;
		swapChain.commandAllocators[0] = nullptr;
		swapChain.commandAllocators[1] = nullptr;
		swapChain.commandLists[0] = nullptr;
		swapChain.commandLists[1] = nullptr;
	};

	if (!globals::d3d::device) {
		logFailureOnce("validate D3D11 device", "global D3D11 device is unavailable");
		return false;
	}
	if (!globals::d3d::context) {
		logFailureOnce("validate D3D11 context", "global D3D11 context is unavailable");
		return false;
	}

	if (!swapChain.d3d11Device) {
		if (!checkHr("query ID3D11Device5 from game device", globals::d3d::device->QueryInterface(IID_PPV_ARGS(swapChain.d3d11Device.put()))))
			return false;
	}
	if (!swapChain.d3d11Context) {
		if (!checkHr("query ID3D11DeviceContext4 from game context", globals::d3d::context->QueryInterface(IID_PPV_ARGS(swapChain.d3d11Context.put()))))
			return false;
	}

	if (!swapChain.d3d12Device) {
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (!checkHr("query IDXGIDevice from game D3D11 device", globals::d3d::device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()))))
			return false;

		winrt::com_ptr<IDXGIAdapter> adapter;
		if (!checkHr("get DXGI adapter for runtime D3D12 device", dxgiDevice->GetAdapter(adapter.put())))
			return false;

		if (!checkHr("create runtime D3D12 device", D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(swapChain.d3d12Device.put())))) {
			clearRuntimeD3D12State();
			return false;
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;

		if (!checkHr("create runtime D3D12 command queue", swapChain.d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(swapChain.commandQueue.put())))) {
			clearRuntimeD3D12State();
			return false;
		}

		for (uint32_t i = 0; i < std::size(swapChain.commandAllocators); ++i) {
			const std::string allocatorStep = std::format("create runtime D3D12 command allocator {}", i);
			if (!checkHr(allocatorStep.c_str(), swapChain.d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(swapChain.commandAllocators[i].put())))) {
				clearRuntimeD3D12State();
				return false;
			}
			const std::string listStep = std::format("create runtime D3D12 command list {}", i);
			if (!checkHr(listStep.c_str(), swapChain.d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, swapChain.commandAllocators[i].get(), nullptr, IID_PPV_ARGS(swapChain.commandLists[i].put())))) {
				clearRuntimeD3D12State();
				return false;
			}
			const std::string closeStep = std::format("close initial runtime D3D12 command list {}", i);
			if (!checkHr(closeStep.c_str(), swapChain.commandLists[i]->Close())) {
				clearRuntimeD3D12State();
				return false;
			}
		}
	}

	if (!runtimeD3D12Fence || !runtimeD3D11Fence) {
		clearRuntimeFenceState();

		winrt::handle sharedFenceHandle;
		if (!checkHr("create runtime D3D12 shared fence", swapChain.d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(runtimeD3D12Fence.put())))) {
			clearRuntimeFenceState();
			return false;
		}
		if (!checkHr("create runtime D3D12 shared fence handle", swapChain.d3d12Device->CreateSharedHandle(runtimeD3D12Fence.get(), nullptr, GENERIC_ALL, nullptr, sharedFenceHandle.put()))) {
			clearRuntimeFenceState();
			return false;
		}
		if (!checkHr("open runtime shared fence on D3D11 device", swapChain.d3d11Device->OpenSharedFence(sharedFenceHandle.get(), IID_PPV_ARGS(runtimeD3D11Fence.put())))) {
			clearRuntimeFenceState();
			return false;
		}

		runtimeFenceValue = 1;
		runtimeCommandFrameIndex = 0;
	}

	if (!swapChain.d3d11Device.get() ||
	    !swapChain.d3d11Context.get() ||
	    !swapChain.d3d12Device.get() ||
	    !swapChain.commandQueue.get() ||
	    !swapChain.commandAllocators[0].get() ||
	    !swapChain.commandAllocators[1].get() ||
	    !swapChain.commandLists[0].get() ||
	    !swapChain.commandLists[1].get() ||
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
		if (!swapChain.commandAllocators[0].get())
			appendMissing("D3D12 command allocator 0");
		if (!swapChain.commandAllocators[1].get())
			appendMissing("D3D12 command allocator 1");
		if (!swapChain.commandLists[0].get())
			appendMissing("D3D12 command list 0");
		if (!swapChain.commandLists[1].get())
			appendMissing("D3D12 command list 1");
		if (!runtimeD3D11Fence.get())
			appendMissing("D3D11 shared fence");
		if (!runtimeD3D12Fence.get())
			appendMissing("D3D12 shared fence");

		logFailureOnce("validate runtime interop objects", std::format("missing {}", missing));
		return false;
	}

	return true;
}

bool FidelityFX::EnsureRuntimeUpscalerContexts(uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight, uint32_t a_contextCount, uint32_t a_requestedVersion)
{
	auto recordRuntimeProviderResult = [&](bool a_supported) {
		runtimeUpscalerSupportCheckKnown = true;
		runtimeUpscalerSupportConfirmed = a_supported;
		runtimeUpscalerProviderMatchedVersionId = 0;
		runtimeUpscalerProviderMatchedVersionName.clear();

		if (!a_supported || !runtimeUpscalerContexts[0] || !ffxModule.Query)
			return;

		ffxQueryGetProviderVersion providerQuery{};
		providerQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
		providerQuery.header.pNext = nullptr;
		providerQuery.versionId = 0;
		providerQuery.versionName = nullptr;

		if (ffxModule.Query(&runtimeUpscalerContexts[0], &providerQuery.header) == FFX_API_RETURN_OK) {
			runtimeUpscalerProviderMatchedVersionId = providerQuery.versionId;
			if (providerQuery.versionName)
				runtimeUpscalerProviderMatchedVersionName = providerQuery.versionName;
		}
	};

	if (!a_fullRenderWidth || !a_fullRenderHeight || !a_fullDisplayWidth || !a_fullDisplayHeight) {
		recordRuntimeProviderResult(false);
		return false;
	}
	if (a_contextCount == 0 || a_contextCount > std::size(runtimeUpscalerContexts)) {
		recordRuntimeProviderResult(false);
		return false;
	}
	if (!EnsureRuntimeUpscalerInterop()) {
		recordRuntimeProviderResult(false);
		return false;
	}
	if (!ffxModule.CreateContext || !ffxModule.DestroyContext) {
		recordRuntimeProviderResult(false);
		return false;
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

	if (!needsRecreate && runtimeUpscalerContextCount == a_contextCount)
		return true;

	DestroyRuntimeUpscalerContexts();

	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	ffx::CreateBackendDX12Desc backendDesc{};
	backendDesc.device = swapChain.d3d12Device.get();

	uint64_t runtimeVersionId = 0;
	std::string runtimeVersionName;
	const bool hasRuntimeVersionOverride = QueryRuntimeUpscalerVersionId(swapChain.d3d12Device.get(), a_requestedVersion, runtimeVersionId, runtimeVersionName);
	if (hasRuntimeVersionOverride) {
		logger::info(
			"[FidelityFX] Runtime upscaler will request FSR version {} through generic override '{}' (id 0x{:X})",
			UpscalerVersionToString(a_requestedVersion),
			runtimeVersionName.empty() ? "(unnamed)" : runtimeVersionName,
			runtimeVersionId);
	}

	bool createdContextWithGenericVersionOverride = false;
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

		std::array<RuntimeUpscalerCreateAttemptResult, 4> attempts{ {
			{ RuntimeUpscalerCreateAttempt::kGenericOverrideOnly, hasRuntimeVersionOverride },
			{ RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion, hasRuntimeVersionOverride },
			{ RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor, true },
			{ RuntimeUpscalerCreateAttempt::kDefaultProvider, a_requestedVersion == FFX_UPSCALER_VERSION },
		} };

		bool contextCreated = false;
		for (auto& attempt : attempts) {
			if (!attempt.enabled)
				continue;

			attempt.attempted = true;
			attempt.result = TryCreateRuntimeUpscalerContext(
				runtimeUpscalerContexts[i],
				attempt.attempt,
				createDesc,
				backendDesc,
				versionDesc,
				overrideVersionDesc);

			if (attempt.result != FFX_API_RETURN_OK)
				continue;

			contextCreated = true;
			if (attempt.attempt == RuntimeUpscalerCreateAttempt::kGenericOverrideOnly) {
				createdContextWithGenericVersionOverride = true;
			} else if (attempt.attempt == RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion) {
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

			logger::error("[FidelityFX] Failed to create runtime upscaler context {} for FSR version {}. Generic override: {}, generic override + upscaler descriptor: {}, upscaler version descriptor: {}, default provider: {} (Render: {}x{}, Display: {}x{}).",
				i,
				UpscalerVersionToString(a_requestedVersion),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kGenericOverrideOnly),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kGenericOverrideWithUpscalerVersion),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kUpscalerVersionDescriptor),
				getAttemptResult(RuntimeUpscalerCreateAttempt::kDefaultProvider),
				a_fullRenderWidth,
				a_fullRenderHeight,
				a_fullDisplayWidth,
				a_fullDisplayHeight);
			DestroyRuntimeUpscalerContexts();
			recordRuntimeProviderResult(false);
			return false;
		}
	}

	runtimeUpscalerContextCount = a_contextCount;
	runtimeUpscalerMaxRenderWidth = a_fullRenderWidth;
	runtimeUpscalerMaxRenderHeight = a_fullRenderHeight;
	runtimeUpscalerMaxDisplayWidth = a_fullDisplayWidth;
	runtimeUpscalerMaxDisplayHeight = a_fullDisplayHeight;
	runtimeUpscalerRequestedVersion = a_requestedVersion;
	recordRuntimeProviderResult(true);

	if ((runtimeUpscalerProviderMatchedVersionId != 0 || !runtimeUpscalerProviderMatchedVersionName.empty()) &&
	    !RuntimeProviderMatchesVersion(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName, a_requestedVersion)) {
		logger::warn(
			"[FidelityFX] Runtime upscaler provider '{}' does not match requested FSR version {}; reporting actual provider path.",
			RuntimeProviderDisplayName(runtimeUpscalerProviderMatchedVersionId, runtimeUpscalerProviderMatchedVersionName),
			UpscalerVersionToString(a_requestedVersion));
	}

	if (createdContextWithGenericVersionOverride) {
		logger::info("[FidelityFX] Runtime upscaler context creation used the generic FSR version override path.");
	}
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
	return true;
}

bool FidelityFX::EnsureRuntimeUpscalerSharedResources(uint32_t a_contextCount, uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight,
	const D3D11_TEXTURE2D_DESC& a_colorDesc,
	const D3D11_TEXTURE2D_DESC& a_depthDesc,
	const D3D11_TEXTURE2D_DESC& a_motionDesc,
	const D3D11_TEXTURE2D_DESC& a_reactiveDesc,
	const D3D11_TEXTURE2D_DESC& a_transparencyDesc,
	const D3D11_TEXTURE2D_DESC& a_outputDesc)
{
	if (!EnsureRuntimeUpscalerInterop())
		return false;
	if (a_contextCount == 0 || a_contextCount > std::size(runtimeColorShared))
		return false;

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
		for (uint32_t i = a_contextCount; i < std::size(runtimeColorShared); ++i) {
			delete runtimeColorShared[i];
			runtimeColorShared[i] = nullptr;
			delete runtimeDepthShared[i];
			runtimeDepthShared[i] = nullptr;
			delete runtimeMotionShared[i];
			runtimeMotionShared[i] = nullptr;
			delete runtimeReactiveShared[i];
			runtimeReactiveShared[i] = nullptr;
			delete runtimeTransparencyShared[i];
			runtimeTransparencyShared[i] = nullptr;
			delete runtimeOutputShared[i];
			runtimeOutputShared[i] = nullptr;
		}
		return true;
	}

	DestroyRuntimeUpscalerResources();

	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	try {
		for (uint32_t i = 0; i < a_contextCount; ++i) {
			runtimeColorShared[i] = new WrappedResource(desiredColorDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			runtimeDepthShared[i] = new WrappedResource(desiredDepthDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			runtimeMotionShared[i] = new WrappedResource(desiredMotionDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			runtimeReactiveShared[i] = new WrappedResource(desiredReactiveDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			runtimeTransparencyShared[i] = new WrappedResource(desiredTransparencyDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
			runtimeOutputShared[i] = new WrappedResource(desiredOutputDesc, swapChain.d3d11Device.get(), swapChain.d3d12Device.get());
		}
	} catch (const std::exception& e) {
		logger::error("[FidelityFX] Failed to create runtime shared resources: {}", e.what());
		DestroyRuntimeUpscalerResources();
		return false;
	} catch (...) {
		logger::error("[FidelityFX] Failed to create runtime shared resources.");
		DestroyRuntimeUpscalerResources();
		return false;
	}

	runtimeColorSharedDesc = desiredColorDesc;
	runtimeDepthSharedDesc = desiredDepthDesc;
	runtimeMotionSharedDesc = desiredMotionDesc;
	runtimeReactiveSharedDesc = desiredReactiveDesc;
	runtimeTransparencySharedDesc = desiredTransparencyDesc;
	runtimeOutputSharedDesc = desiredOutputDesc;

	return true;
}

bool FidelityFX::DispatchRuntimeUpscalerSingle(uint32_t a_contextIndex, ID3D11Resource* a_color, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_output,
	uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight,
	float a_motionVectorScaleX, float a_motionVectorScaleY, float a_sharpness)
{
	if (a_contextIndex >= runtimeUpscalerContextCount || !runtimeUpscalerContexts[a_contextIndex])
		return false;
	if (!a_color || !a_depth || !a_motionVectors || !a_reactiveMask || !a_transparencyCompositionMask || !a_output)
		return false;
	if (!a_renderWidth || !a_renderHeight || !a_displayWidth || !a_displayHeight)
		return false;

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
		return false;
	}

	if (!EnsureRuntimeUpscalerSharedResources(
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
			outputDesc)) {
		return false;
	}

	auto& swapChain = globals::features::upscaling.dx12SwapChain;
	auto& upscaling = globals::features::upscaling;
	auto state = globals::state;

	if (!swapChain.d3d11Context || !swapChain.commandQueue || !runtimeD3D11Fence || !runtimeD3D12Fence)
		return false;

	auto isValidShared = [](WrappedResource* a_resource) {
		return a_resource && a_resource->resource11 && a_resource->resource.get();
	};
	if (!isValidShared(runtimeColorShared[a_contextIndex]) ||
		!isValidShared(runtimeDepthShared[a_contextIndex]) ||
		!isValidShared(runtimeMotionShared[a_contextIndex]) ||
		!isValidShared(runtimeReactiveShared[a_contextIndex]) ||
		!isValidShared(runtimeTransparencyShared[a_contextIndex]) ||
		!isValidShared(runtimeOutputShared[a_contextIndex])) {
		return false;
	}

	const uint32_t commandIndex = runtimeCommandFrameIndex % std::size(swapChain.commandLists);
	runtimeCommandFrameIndex++;

	auto* commandAllocator = swapChain.commandAllocators[commandIndex].get();
	auto* commandList = swapChain.commandLists[commandIndex].get();
	if (!commandAllocator || !commandList)
		return false;

	const bool annotateDispatch = state && state->frameAnnotations;
	if (annotateDispatch) {
		if (globals::game::isVR) {
			char buf[32];
			snprintf(buf, sizeof(buf), "FSR Runtime Eye %u", a_contextIndex);
			state->BeginPerfEvent(buf);
		} else {
			state->BeginPerfEvent("FSR Runtime Dispatch");
		}
	}

	bool dispatchOk = false;
	try {
		auto copyIntoShared = [&](ID3D11Resource* a_source, WrappedResource* a_destination, uint32_t a_width, uint32_t a_height, uint32_t a_maxWidth, uint32_t a_maxHeight) {
			if (!a_source || !a_destination || !a_destination->resource11)
				return false;

			const uint32_t copyWidth = std::min(a_width, a_maxWidth);
			const uint32_t copyHeight = std::min(a_height, a_maxHeight);
			if (!copyWidth || !copyHeight)
				return false;

			D3D11_BOX sourceBox{};
			sourceBox.left = 0;
			sourceBox.top = 0;
			sourceBox.front = 0;
			sourceBox.right = copyWidth;
			sourceBox.bottom = copyHeight;
			sourceBox.back = 1;
			swapChain.d3d11Context->CopySubresourceRegion(a_destination->resource11, 0, 0, 0, 0, a_source, 0, &sourceBox);
			return true;
		};

		if (!copyIntoShared(a_color, runtimeColorShared[a_contextIndex], colorDesc.Width, colorDesc.Height, runtimeColorSharedDesc.Width, runtimeColorSharedDesc.Height) ||
			!copyIntoShared(a_depth, runtimeDepthShared[a_contextIndex], depthDesc.Width, depthDesc.Height, runtimeDepthSharedDesc.Width, runtimeDepthSharedDesc.Height) ||
			!copyIntoShared(a_motionVectors, runtimeMotionShared[a_contextIndex], motionDesc.Width, motionDesc.Height, runtimeMotionSharedDesc.Width, runtimeMotionSharedDesc.Height) ||
			!copyIntoShared(a_reactiveMask, runtimeReactiveShared[a_contextIndex], reactiveDesc.Width, reactiveDesc.Height, runtimeReactiveSharedDesc.Width, runtimeReactiveSharedDesc.Height) ||
			!copyIntoShared(a_transparencyCompositionMask, runtimeTransparencyShared[a_contextIndex], transparencyDesc.Width, transparencyDesc.Height, runtimeTransparencySharedDesc.Width, runtimeTransparencySharedDesc.Height)) {
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
			dispatchParameters.enableSharpening = true;
			dispatchParameters.sharpness = a_sharpness;
			dispatchParameters.frameTimeDelta = *globals::game::deltaTime * 1000.f;
			dispatchParameters.preExposure = 1.0f;
			dispatchParameters.reset = upscaling.ShouldResetHistoryThisFrame();
			dispatchParameters.cameraNear = *globals::game::cameraNear;
			dispatchParameters.cameraFar = *globals::game::cameraFar;
			dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
			dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
			dispatchParameters.flags = 0;
			const bool runtimeFallbackReset = runtimeFallbackResetDispatchesRemaining > 0;
			dispatchParameters.reset = dispatchParameters.reset || runtimeFallbackReset;

			dispatchOk = ffx::Dispatch(runtimeUpscalerContexts[a_contextIndex], dispatchParameters) == ffx::ReturnCode::Ok;
			if (dispatchOk && runtimeFallbackReset)
				runtimeFallbackResetDispatchesRemaining--;
			if (!dispatchOk) {
				logger::error("[FidelityFX] Runtime upscaler dispatch failed for eye {}.", a_contextIndex);
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

			const uint64_t d3d12SubmitFence = runtimeFenceValue++;
			DX::ThrowIfFailed(swapChain.commandQueue->Signal(runtimeD3D12Fence.get(), d3d12SubmitFence));
			DX::ThrowIfFailed(swapChain.d3d11Context->Wait(runtimeD3D11Fence.get(), d3d12SubmitFence));

			if (dispatchOk) {
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
					swapChain.d3d11Context->CopySubresourceRegion(a_output, 0, 0, 0, 0, runtimeOutputShared[a_contextIndex]->resource11, 0, &outputBox);
				}
			}
		}
	} catch (const std::exception& e) {
		logger::error("[FidelityFX] Runtime upscaler dispatch path failed for eye {}: {}", a_contextIndex, e.what());
		dispatchOk = false;
	} catch (...) {
		logger::error("[FidelityFX] Runtime upscaler dispatch path failed for eye {}.", a_contextIndex);
		dispatchOk = false;
	}

	if (annotateDispatch)
		state->EndPerfEvent();

	return dispatchOk;
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
	const uint32_t requestedRuntimeVersion = runtimeFsr4Requested ? FFX_UPSCALER_VERSION : kRuntimeFsr315Version;
	const uint32_t runtimeContextCount = UseSplitPerEyeFSRContexts() ? 2u : 1u;
	const bool runtimeSelected = runtimeRequested && CanUseRuntimeUpscalerPath();

	if (runtimeSelected) {
		auto state = globals::state;
		if (!state)
			return false;

		const auto renderSize = Util::ConvertToDynamic(state->screenSize);
		const bool splitPerEyeContexts = UseSplitPerEyeFSRContexts();
		const uint32_t fullDisplayWidth = static_cast<uint32_t>(splitPerEyeContexts ? state->screenSize.x / 2.0f : state->screenSize.x);
		const uint32_t fullDisplayHeight = static_cast<uint32_t>(state->screenSize.y);
		const uint32_t requestedFullRenderWidth = static_cast<uint32_t>(splitPerEyeContexts ? renderSize.x / 2.0f : renderSize.x);
		const uint32_t requestedFullRenderHeight = static_cast<uint32_t>(renderSize.y);
		const uint32_t fullRenderWidth = runtimeFsr4Requested ? fullDisplayWidth : requestedFullRenderWidth;
		const uint32_t fullRenderHeight = runtimeFsr4Requested ? fullDisplayHeight : requestedFullRenderHeight;

		auto tryRuntimeUpscaler = [&](uint32_t a_requestedVersion, uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight) {
			try {
				if (EnsureRuntimeUpscalerContexts(a_fullRenderWidth, a_fullRenderHeight, fullDisplayWidth, fullDisplayHeight, runtimeContextCount, a_requestedVersion) &&
				    DispatchRuntimeUpscalerSingle(
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
					    a_sharpness)) {
					RecordRuntimeUpscalerFramePath(GetRuntimeUpscalerProviderFramePath(a_requestedVersion));
					return true;
				}
			} catch (const std::exception& e) {
				logger::error("[FidelityFX] Runtime upscaler setup/dispatch for FSR version {} threw an exception: {}",
					UpscalerVersionToString(a_requestedVersion),
					e.what());
			} catch (...) {
				logger::error("[FidelityFX] Runtime upscaler setup/dispatch for FSR version {} threw an unknown exception.",
					UpscalerVersionToString(a_requestedVersion));
			}

			return false;
		};

		if (tryRuntimeUpscaler(requestedRuntimeVersion, fullRenderWidth, fullRenderHeight))
			return true;

		// Try runtime FSR 3.1.5 before giving up on amd_fidelityfx_upscaler_dx12.dll.
		if (runtimeFsr4Requested && ShouldUseRuntimeUpscalerForFSR()) {
			LatchRuntimeFsr4Failure();
			runtimeFallbackResetDispatchesRemaining = std::max(runtimeFallbackResetDispatchesRemaining, runtimeContextCount);
			if (tryRuntimeUpscaler(kRuntimeFsr315Version, requestedFullRenderWidth, requestedFullRenderHeight))
				return true;
		}

		if (!runtimeUpscalerFailureLatched) {
			runtimeFallbackResetDispatchesRemaining = std::max(runtimeFallbackResetDispatchesRemaining, runtimeContextCount);
		}
		LatchRuntimeUpscalerFailure();
	}

	if (!runtimeRequested)
		runtimeFallbackResetDispatchesRemaining = 0;

	if (!fsrScratchBuffer || a_contextIndex >= fsrContextCount)
		return false;

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
		if (globals::game::isVR) {
			char buf[32];
			snprintf(buf, sizeof(buf), "FSR Dispatch Eye %u", a_contextIndex);
			state->BeginPerfEvent(buf);
		} else {
			state->BeginPerfEvent("FSR Dispatch");
		}
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
	dispatchParameters.enableSharpening = true;
	dispatchParameters.sharpness = a_sharpness;
	dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
	dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
	const bool runtimeFallbackReset = runtimeRequested && runtimeFallbackResetDispatchesRemaining > 0;
	if (runtimeFallbackReset)
		runtimeFallbackResetDispatchesRemaining--;
	dispatchParameters.reset = globals::features::upscaling.ShouldResetHistoryThisFrame() || runtimeFallbackReset;
	dispatchParameters.preExposure = 1.0f;
	dispatchParameters.flags = 0;

	bool hostDispatchCrashed = false;
	const bool dispatchOK = DispatchHostFsr3UpscaleProtected(fsrContext[a_contextIndex], dispatchParameters, hostDispatchCrashed);
	if (!dispatchOK && !hostDispatchCrashed) {
		logger::critical("[FidelityFX] Failed to dispatch region upscaling for eye {}!", a_contextIndex);
	}
	if (hostDispatchCrashed) {
		if (!fsrDispatchCrashLogged) {
			logger::critical("[FidelityFX] Region FSR3 dispatch crashed for eye {} - this may be caused by RenderDoc capture interfering with FSR operations. Try disabling RenderDoc capture.", a_contextIndex);
			fsrDispatchCrashLogged = true;
		}
	}

	if (state->frameAnnotations)
		state->EndPerfEvent();

	return dispatchOK;
}

void FidelityFX::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness)
{
	auto renderer = globals::game::renderer;
	auto state = globals::state;
	if (!renderer || !state)
		return;

	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	const auto screenSize = state->screenSize;
	const auto renderSize = Util::ConvertToDynamic(screenSize);

	auto& upscaling = globals::features::upscaling;
	const bool splitPerEyeContexts = UseSplitPerEyeFSRContexts();

	if (splitPerEyeContexts) {
		upscaling.PreparePerEyeInputs(a_upscalingTexture);

		const uint32_t eyeDisplayWidth = static_cast<uint32_t>(screenSize.x / 2.0f);
		const uint32_t eyeDisplayHeight = static_cast<uint32_t>(screenSize.y);
		const uint32_t eyeRenderWidth = static_cast<uint32_t>(renderSize.x / 2.0f);
		const uint32_t eyeRenderHeight = static_cast<uint32_t>(renderSize.y);

		for (uint32_t i = 0; i < 2; ++i) {
			if (!UpscaleRegion(
					i,
					upscaling.vrIntermediateColorIn[i]->resource.get(),
					upscaling.vrIntermediateLinearDepth[i]->resource.get(),
					upscaling.vrIntermediateMotionVectors[i]->resource.get(),
					upscaling.vrIntermediateReactiveMask[i]->resource.get(),
					upscaling.vrIntermediateTransparencyMask[i]->resource.get(),
					upscaling.vrIntermediateColorOut[i]->resource.get(),
					eyeRenderWidth,
					eyeRenderHeight,
					eyeDisplayWidth,
					eyeDisplayHeight,
					renderSize.x / 2.0f,
					renderSize.y,
					a_sharpness)) {
				logger::error("[FidelityFX] Upscale dispatch failed for VR eye {}.", i);
			}
		}

		upscaling.FinalizePerEyeOutputs(a_upscalingTexture);
		return;
	}

	if (!UpscaleRegion(
			0,
			a_upscalingTexture,
			depthTexture.texture,
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
			a_sharpness)) {
		logger::error("[FidelityFX] Upscale dispatch failed.");
	}
}
