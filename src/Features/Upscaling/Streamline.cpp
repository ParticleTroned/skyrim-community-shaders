#include "Streamline.h"

#include <algorithm>
#include <cmath>
#include <dxgi.h>
#include <dxgi1_3.h>

// Streamline verifies secondary signatures, whose WinTrust declarations are
// hidden by CommonLib's Windows 7 compatibility target.
#pragma push_macro("NTDDI_VERSION")
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN8
#include <sl_security.h>
#pragma pop_macro("NTDDI_VERSION")

#include <vector>

#include "../../Deferred.h"
#include "../../Hooks.h"
#include "../../State.h"
#include "../../Util.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"
#include "ReflexPolicy.h"

namespace
{
	std::vector<void*> s_streamlineDllDirectoryCookies;

	void EnsureStreamlineDllDirectory(const std::filesystem::path& a_pluginDir)
	{
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

		void* cookie = addDllDirectory(a_pluginDir.c_str());
		if (!cookie) {
			logger::warn(
				"[Streamline] Failed to add Streamline DLL directory {} (error {})",
				stl::utf16_to_utf8(a_pluginDir.wstring()).value_or("<unknown>"),
				GetLastError());
			return;
		}

		s_streamlineDllDirectoryCookies.push_back(cookie);
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

	bool TryGetTexture2DDesc(ID3D11Resource* a_resource, D3D11_TEXTURE2D_DESC& a_desc)
	{
		if (!a_resource)
			return false;

		ID3D11Texture2D* texture = nullptr;
		if (FAILED(a_resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) || !texture)
			return false;

		texture->GetDesc(&a_desc);
		texture->Release();
		return true;
	}

	bool GetDLSSColorBuffersHDR(ID3D11Resource* a_colorIn)
	{
		D3D11_TEXTURE2D_DESC desc{};
		if (!TryGetTexture2DDesc(a_colorIn, desc))
			return true;

		return IsHDRDLSSInputFormat(desc.Format);
	}

	void FlushAndWaitForD3D11Idle(ID3D11DeviceContext* a_context, const char* a_reason)
	{
		if (!a_context)
			return;

		ID3D11Device* device = nullptr;
		a_context->GetDevice(&device);
		if (!device) {
			a_context->Flush();
			return;
		}

		D3D11_QUERY_DESC queryDesc{};
		queryDesc.Query = D3D11_QUERY_EVENT;

		ID3D11Query* query = nullptr;
		const HRESULT createResult = device->CreateQuery(&queryDesc, &query);
		device->Release();

		if (FAILED(createResult) || !query) {
			a_context->Flush();
			return;
		}

		a_context->End(query);
		a_context->Flush();

		const ULONGLONG deadline = GetTickCount64() + 1000;
		BOOL completed = FALSE;
		while (true) {
			const HRESULT dataResult = a_context->GetData(query, &completed, sizeof(completed), 0);
			if (dataResult == S_OK && completed)
				break;

			if (FAILED(dataResult)) {
				logger::debug("[Streamline] D3D11 idle wait failed before {}: 0x{:08X}", a_reason, static_cast<uint32_t>(dataResult));
				break;
			}

			if (GetTickCount64() >= deadline) {
				logger::debug("[Streamline] D3D11 idle wait timed out before {}", a_reason);
				break;
			}

			Sleep(1);
		}

		query->Release();
	}
}

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
std::vector<std::pair<std::string, std::string>> Streamline::dllVersionsDX12 = {};

void Streamline::LoadInterposer()
{
	if (triedInitialization)
		return;
	triedInitialization = true;

	const std::filesystem::path pluginDirectory = std::filesystem::path(pluginDir);
	std::error_code pluginPathError;
	auto pluginDirAbsolute = std::filesystem::absolute(pluginDirectory, pluginPathError);
	if (pluginPathError) {
		logger::warn(
			"[Streamline] Failed to resolve absolute plugin directory {}: {}",
			stl::utf16_to_utf8(pluginDirectory.wstring()).value_or("<unknown>"),
			pluginPathError.message());
		return;
	}
	pluginDirAbsolute = pluginDirAbsolute.lexically_normal();
	const std::filesystem::path interposerPath = pluginDirAbsolute / L"sl.interposer.dll";
	if (!sl::security::verifyEmbeddedSignature(interposerPath.c_str())) {
		logger::critical(
			"[Streamline] Refusing unsigned or invalid interposer at {}",
			stl::utf16_to_utf8(interposerPath.wstring()).value_or("<unknown>"));
		return;
	}
	EnsureStreamlineDllDirectory(pluginDirAbsolute);
	DWORD errorCode = ERROR_SUCCESS;
	interposer = LoadStreamlineDll(interposerPath, errorCode);
	if (interposer == nullptr) {
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		return;
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}

	// Dynamically log all DLL versions in the Streamline plugin directory
	auto& loadedDllVersions = IsDLSSGInstance() ?
	                              Streamline::dllVersionsDX12 :
	                              Streamline::dllVersions;
	loadedDllVersions = Util::EnumerateDllVersions(pluginDirAbsolute);
	for (const auto& [name, versionStr] : loadedDllVersions)
		logger::info("[Streamline] {} version: {}", name, versionStr);

	logger::info("[Streamline] Initializing Streamline");

	sl::Preferences pref{};

	const sl::Feature dx11Features[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL };
	const sl::Feature dx12Features[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };
	if (IsDLSSGInstance()) {
		pref.featuresToLoad = dx12Features;
		pref.numFeaturesToLoad = _countof(dx12Features);
	} else {
		pref.featuresToLoad = dx11Features;
		pref.numFeaturesToLoad = _countof(dx11Features);
	}

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
	const std::wstring pluginDirAbsoluteW = pluginDirAbsolute.wstring();
	const wchar_t* pluginPaths[1]{};
	pluginPaths[0] = pluginDirAbsoluteW.c_str();
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;
	logger::info("[Streamline] Plugin search path: {}", pluginDirAbsolute.string());

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = renderAPI;
	pref.flags = sl::PreferenceFlags::eUseManualHooking;
	if (IsDLSSGInstance())
		pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	// Hook up all of the functions exported by the SL Interposer Library
	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
	slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
	slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slAllocateResources = (PFun_slAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
	slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
	slSetTag = (PFun_slSetTagCompat*)GetProcAddress(interposer, "slSetTag");
	slSetTagForFrame = (PFun_slSetTagForFrame*)GetProcAddress(interposer, "slSetTagForFrame");
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureVersion = (PFun_slGetFeatureVersion*)GetProcAddress(interposer, "slGetFeatureVersion");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

	std::vector<const char*> missingExports;
	auto requireExport = [&](const char* a_name, auto a_export) {
		if (!a_export)
			missingExports.push_back(a_name);
	};
	requireExport("slInit", slInit);
	requireExport("slShutdown", slShutdown);
	requireExport("slIsFeatureSupported", slIsFeatureSupported);
	requireExport("slIsFeatureLoaded", slIsFeatureLoaded);
	requireExport("slSetFeatureLoaded", slSetFeatureLoaded);
	requireExport("slEvaluateFeature", slEvaluateFeature);
	requireExport("slAllocateResources", slAllocateResources);
	requireExport("slFreeResources", slFreeResources);
	if (IsDLSSGInstance())
		requireExport("slSetTagForFrame", slSetTagForFrame);
	else
		requireExport("slSetTag", slSetTag);
	requireExport("slGetFeatureRequirements", slGetFeatureRequirements);
	requireExport("slGetFeatureVersion", slGetFeatureVersion);
	requireExport("slUpgradeInterface", slUpgradeInterface);
	requireExport("slSetConstants", slSetConstants);
	requireExport("slGetNativeInterface", slGetNativeInterface);
	requireExport("slGetFeatureFunction", slGetFeatureFunction);
	requireExport("slGetNewFrameToken", slGetNewFrameToken);
	requireExport("slSetD3DDevice", slSetD3DDevice);

	if (!missingExports.empty()) {
		logger::critical("[Streamline] Interposer is missing required exports; refusing to initialize.");
		for (const auto* exportName : missingExports)
			logger::critical("[Streamline] Missing export {}", exportName);
		FreeLibrary(interposer);
		interposer = nullptr;
		return;
	}

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
	} else {
		initialized = true;
		featureDLSS = false;
		featureDLSSG = false;
		featureReflex = false;
		featurePCL = false;
		reflexSupportedOnCurrentAdapter = false;
		dlssOptionsCache = {};
		reflexOptionsCache = {};
		ResetFrameTracking();
		ResetDLSSGState();
		logger::info("[Streamline] Successfully initialized Streamline");
	}
}

bool Streamline::RequiresDLSSGPresentBoundary() const
{
	return IsDLSSGInstance() &&
	       (dlssgState.optionsApplied || dlssgState.optionsEnabled ||
			   dlssgState.optionsTransitionPending || !dlssgState.tagsCleared);
}

bool Streamline::Shutdown()
{
	if (!initialized)
		return true;

	// DLSS-G options and tag lifetimes are ordered at the proxy Present boundary.
	// Shutdown is best-effort here rather than issuing unordered cleanup work.
	if (RequiresDLSSGPresentBoundary()) {
		logger::warn(
			"[Streamline DX12] Shutting down while DLSS-G provider work remains unconsumed.");
	}
	if (!slShutdown)
		return false;
	const sl::Result result = slShutdown();
	const bool shutdownComplete =
		result == sl::Result::eOk ||
		result == sl::Result::eErrorNotInitialized ||
		result == sl::Result::eErrorInitNotCalled;
	if (!shutdownComplete) {
		logger::warn(
			"[Streamline {}] slShutdown failed: {}",
			IsDLSSGInstance() ? "DX12" : "DX11",
			magic_enum::enum_name(result));
		return false;
	}

	initialized = false;
	featureDLSS = false;
	featureDLSSG = false;
	featureReflex = false;
	featurePCL = false;
	ResetFrameTracking();
	ResetDLSSGState();
	// Proxied COM objects can retain vtables in the interposer until D3D teardown.
	// Keep the module mapped; the process loader releases it after object destruction.
	return true;
}

void Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
{
	logger::info("[Streamline {}] Checking features", IsDLSSGInstance() ? "DX12" : "DX11");
	auto disableFeatures = [&]() {
		featureDLSS = false;
		featureDLSSG = false;
		featureReflex = false;
		featurePCL = false;
		reflexSupportedOnCurrentAdapter = false;
		isRTXBelow40series = false;
		InvalidateDLSSOptionsCache();
		reflexOptionsCache = {};
		ResetFrameTracking();
		ResetDLSSGState();
	};

	if (!a_adapter) {
		logger::warn("[Streamline] Cannot check features without a DXGI adapter.");
		disableFeatures();
		return;
	}
	if (!initialized || !slIsFeatureLoaded || !slGetFeatureRequirements || !slIsFeatureSupported) {
		logger::warn("[Streamline] Cannot check features because Streamline is not initialized.");
		disableFeatures();
		return;
	}

	DXGI_ADAPTER_DESC adapterDesc{};
	const HRESULT adapterDescResult = a_adapter->GetDesc(&adapterDesc);
	if (FAILED(adapterDescResult)) {
		logger::warn(
			"[Streamline] Cannot check features because the DXGI adapter description query failed: 0x{:08X}",
			static_cast<unsigned>(adapterDescResult));
		disableFeatures();
		return;
	}
	reflexSupportedOnCurrentAdapter = adapterDesc.VendorId == kNvidiaVendorId;

	sl::AdapterInfo adapterInfo{};
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
		const sl::Result supportResult = slIsFeatureSupported(feature, adapterInfo);
		outAvailable = supportResult == sl::Result::eOk;
		if (!outAvailable) {
			logger::info(
				"[Streamline] {} is not supported on the selected adapter: {}",
				featureName,
				magic_enum::enum_name(supportResult));
		}
	};

	if (IsDLSSGInstance())
		checkFeatureAvailability(sl::kFeatureDLSS_G, "DLSS-G", featureDLSSG);
	else
		checkFeatureAvailability(sl::kFeatureDLSS, "DLSS", featureDLSS);
	if (featureDLSSG) {
		sl::FeatureRequirements requirements{};
		const sl::Result requirementsResult =
			slGetFeatureRequirements(sl::kFeatureDLSS_G, requirements);
		const auto requirementFlags = static_cast<uint32_t>(requirements.flags);
		const auto d3d12Flag = static_cast<uint32_t>(
			sl::FeatureRequirementFlags::eD3D12Supported);
		if (requirementsResult != sl::Result::eOk ||
			(requirementFlags & d3d12Flag) == 0) {
			logger::warn(
				"[Streamline DX12] DLSS-G does not report D3D12 support: {}",
				magic_enum::enum_name(requirementsResult));
			featureDLSSG = false;
		}
	}
	if (reflexSupportedOnCurrentAdapter) {
		checkFeatureAvailability(sl::kFeatureReflex, "Reflex", featureReflex);
		checkFeatureAvailability(sl::kFeaturePCL, "PCL", featurePCL);
	} else {
		featureReflex = false;
		featurePCL = false;
	}

	if (!IsDLSSGInstance() && featureDLSS) {
		isRTXBelow40series = IsRTXAndBelow40Series(adapterDesc);

		if (isRTXBelow40series)
			logger::info("[Streamline] Older RTX GPU detected, DLSS 4.0 will be used instead of DLSS 4.5");
		else
			logger::info("[Streamline] Newer RTX GPU detected, DLSS 4.5 will be used instead of DLSS 4.0");
	}

	if (IsDLSSGInstance())
		logger::info("[Streamline DX12] DLSS-G {} available", featureDLSSG ? "is" : "is not");
	else
		logger::info("[Streamline DX11] DLSS {} available", featureDLSS ? "is" : "is not");
	if (reflexSupportedOnCurrentAdapter) {
		logger::info("[Streamline] Reflex {} available", featureReflex ? "is" : "is not");
		logger::info("[Streamline] PCL {} available", featurePCL ? "is" : "is not");
	} else {
		logger::info("[Streamline] Reflex/PCL disabled on non-NVIDIA adapter");
	}
	InvalidateDLSSOptionsCache();
	reflexOptionsCache = {};
	ResetFrameTracking();
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

	if (!IsDLSSGInstance() && featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}
	if (IsDLSSGInstance() && featureDLSSG) {
		slDLSSGGetState = nullptr;
		slDLSSGSetOptions = nullptr;
		const sl::Result getStateResult = slGetFeatureFunction(
			sl::kFeatureDLSS_G,
			"slDLSSGGetState",
			reinterpret_cast<void*&>(slDLSSGGetState));
		const sl::Result setOptionsResult = slGetFeatureFunction(
			sl::kFeatureDLSS_G,
			"slDLSSGSetOptions",
			reinterpret_cast<void*&>(slDLSSGSetOptions));
		if (getStateResult != sl::Result::eOk ||
			setOptionsResult != sl::Result::eOk ||
			!slDLSSGGetState || !slDLSSGSetOptions) {
			logger::error(
				"[Streamline DX12] Required DLSS-G functions are unavailable (getState={}, setOptions={})",
				magic_enum::enum_name(getStateResult),
				magic_enum::enum_name(setOptionsResult));
			featureDLSSG = false;
			ResetDLSSGState();
		} else {
			sl::DLSSGState state{};
			const sl::Result stateResult = slDLSSGGetState(viewport, state, nullptr);
			if (stateResult != sl::Result::eOk) {
				logger::warn(
					"[Streamline DX12] Initial DLSS-G state probe failed; frame generation will remain unavailable until a successful state query: {}",
					magic_enum::enum_name(stateResult));
			} else {
				dlssgState.status = state.status;
				dlssgState.minimumDimension = state.minWidthOrHeight;
				dlssgState.maximumFramesToGenerate = std::min(
					state.numFramesToGenerateMax,
					Upscaling::kDLSSGMaximumGeneratedFrames);
				logger::info(
					"[Streamline DX12] DLSS-G supports up to {} generated frame(s), minimum dimension {}",
					dlssgState.maximumFramesToGenerate,
					dlssgState.minimumDimension);
			}
		}
	}

	const bool reflexFeatureSupported = featureReflex;
	const bool pclFeatureSupported = featurePCL;
	slReflexGetState = nullptr;
	slReflexSleep = nullptr;
	slReflexSetOptions = nullptr;
	slPCLSetMarker = nullptr;
	featureReflex = false;
	featurePCL = false;

	if (slGetFeatureFunction && reflexSupportedOnCurrentAdapter) {
		if (slSetFeatureLoaded) {
			// A load request may restore a selected feature, but it must never
			// promote one rejected by the adapter support query.
			const auto requestFeatureLoad = [&](sl::Feature feature, const char* featureName) {
				const sl::Result loadResult = slSetFeatureLoaded(feature, true);
				if (loadResult != sl::Result::eOk)
					logger::warn("[Streamline] Failed to request {} load: {}", featureName, magic_enum::enum_name(loadResult));
			};

			if (reflexFeatureSupported)
				requestFeatureLoad(sl::kFeatureReflex, "Reflex");
			if (pclFeatureSupported)
				requestFeatureLoad(sl::kFeaturePCL, "PCL");
		}

		const auto bindFeatureFn = [&](sl::Feature feature, const char* functionName, void*& fn) {
			fn = nullptr;
			const sl::Result bindResult = slGetFeatureFunction(feature, functionName, fn);
			if (bindResult != sl::Result::eOk)
				logger::warn("[Streamline] {} bind failed with {}", functionName, magic_enum::enum_name(bindResult));
			return bindResult == sl::Result::eOk && fn != nullptr;
		};

		// Support and all required entry points are independent prerequisites.
		if (reflexFeatureSupported) {
			bool reflexFnsBound = true;
			reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexGetState", (void*&)slReflexGetState);
			reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
			reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
			featureReflex = reflexFnsBound && slReflexSetOptions && slReflexSleep;
		}

		if (!reflexFeatureSupported) {
			logger::info("[Streamline] Reflex is not supported; runtime controls will be disabled");
		} else if (!featureReflex) {
			logger::warn("[Streamline] Reflex functions are missing; Reflex runtime controls will be disabled");
		} else {
			logger::info("[Streamline] Reflex runtime controls are available");
		}

		if (pclFeatureSupported) {
			const bool pclFnBound = bindFeatureFn(
				sl::kFeaturePCL,
				"slPCLSetMarker",
				reinterpret_cast<void*&>(slPCLSetMarker));
			featurePCL = pclFnBound && slPCLSetMarker;
		}
		if (!pclFeatureSupported) {
			logger::info("[Streamline] PCL is not supported; marker requests will be ignored");
		} else if (!featurePCL) {
			logger::warn("[Streamline] PCL marker function is unavailable; marker optimization requests will be ignored");
		} else {
			logger::info("[Streamline] PCL marker interface is available");
		}
	} else if (!reflexSupportedOnCurrentAdapter) {
		logger::info("[Streamline] Skipping Reflex/PCL binding on non-NVIDIA adapter");
	}

	InvalidateDLSSOptionsCache();
	reflexOptionsCache = {};
	ResetFrameTracking();
}

bool Streamline::SetD3DDevice(void* a_device)
{
	if (!initialized || !slSetD3DDevice || !a_device)
		return false;

	const sl::Result result = slSetD3DDevice(a_device);
	if (result != sl::Result::eOk) {
		logger::error(
			"[Streamline {}] slSetD3DDevice failed: {}",
			IsDLSSGInstance() ? "DX12" : "DX11",
			magic_enum::enum_name(result));
		return false;
	}
	return true;
}

bool Streamline::UpgradeInterface(void** a_interface, std::string_view a_name)
{
	if (!initialized || !slUpgradeInterface || !a_interface || !*a_interface)
		return false;

	void* const originalInterface = *a_interface;
	void* upgradedInterface = originalInterface;
	const sl::Result result = slUpgradeInterface(&upgradedInterface);
	if (result != sl::Result::eOk || !upgradedInterface) {
		logger::error(
			"[Streamline {}] Could not upgrade {}: {}",
			IsDLSSGInstance() ? "DX12" : "DX11",
			a_name,
			magic_enum::enum_name(result));
		if (upgradedInterface && upgradedInterface != originalInterface)
			static_cast<IUnknown*>(upgradedInterface)->Release();
		return false;
	}
	*a_interface = upgradedInterface;
	return true;
}

bool Streamline::GetNativeInterface(
	void* a_proxyInterface,
	void** a_nativeInterface,
	std::string_view a_name)
{
	if (a_nativeInterface)
		*a_nativeInterface = nullptr;
	if (!initialized || !slGetNativeInterface || !a_proxyInterface || !a_nativeInterface)
		return false;

	const sl::Result result = slGetNativeInterface(
		a_proxyInterface,
		a_nativeInterface);
	if (result != sl::Result::eOk || !*a_nativeInterface) {
		logger::error(
			"[Streamline {}] Could not obtain native {}: {}",
			IsDLSSGInstance() ? "DX12" : "DX11",
			a_name,
			magic_enum::enum_name(result));
		*a_nativeInterface = nullptr;
		return false;
	}
	return true;
}

/**
 * @brief Updates and sets camera and frame constants for the current Streamline frame.
 *
 * Populates and submits camera parameters, projection matrices, motion vector settings, and other per-frame constants to the Streamline SDK for the current frame. Uses cached framebuffer data and global state to ensure correct configuration for upscaling and frame generation features.
 */
bool Streamline::EnsureFrameToken()
{
	if (!initialized || !slGetNewFrameToken || !globals::state)
		return false;

	const uint32_t currentFrame = globals::state->frameCount;
	const std::lock_guard lock(frameTokenMutex);
	for (const auto& slot : frameTokens) {
		if (slot.Matches(currentFrame)) {
			latestFrameTokenFrame = currentFrame;
			return true;
		}
	}

	sl::FrameToken* token = nullptr;
	uint32_t requestedFrame = currentFrame;
	if (SL_FAILED(result, slGetNewFrameToken(token, &requestedFrame))) {
		logger::error("[Streamline] Could not get frame token: {}", magic_enum::enum_name(result));
		if (latestFrameTokenFrame == currentFrame)
			latestFrameTokenFrame = UINT32_MAX;
		return false;
	}
	if (!token) {
		logger::error("[Streamline] Frame token request succeeded without a token");
		if (latestFrameTokenFrame == currentFrame)
			latestFrameTokenFrame = UINT32_MAX;
		return false;
	}
	frameTokens[nextFrameTokenSlot] = { token, currentFrame };
	nextFrameTokenSlot = (nextFrameTokenSlot + 1) % frameTokens.size();
	latestFrameTokenFrame = currentFrame;
	return true;
}

sl::FrameToken* Streamline::GetFrameTokenForFrame(uint32_t a_frame) const
{
	const std::lock_guard lock(frameTokenMutex);
	for (const auto& slot : frameTokens) {
		if (slot.Matches(a_frame)) {
			return slot.token;
		}
	}
	return nullptr;
}

uint32_t Streamline::GetLatestFrameTokenFrame() const
{
	const std::lock_guard lock(frameTokenMutex);
	for (const auto& slot : frameTokens) {
		if (slot.Matches(latestFrameTokenFrame)) {
			return latestFrameTokenFrame;
		}
	}
	return UINT32_MAX;
}

bool Streamline::CheckFrameConstants(sl::ViewportHandle p_viewport)
{
	if (!initialized || !slSetConstants || !globals::state ||
		!globals::game::cameraNear || !globals::game::cameraFar)
		return false;
	const uint32_t currentFrame = globals::state->frameCount;
	const uint32_t currentViewport = static_cast<uint32_t>(p_viewport);
	if (constantsFrame == currentFrame && constantsViewport == currentViewport)
		return constantsResult;
	constantsFrame = currentFrame;
	constantsViewport = currentViewport;
	constantsResult = false;

	if (!EnsureFrameToken())
		return false;
	auto* const frameToken = GetFrameTokenForFrame(currentFrame);
	if (!frameToken)
		return false;

	sl::Constants slConstants = {};

	const auto* gameViewport = globals::game::graphicsState;
	const float screenWidth = gameViewport ? static_cast<float>(gameViewport->screenWidth) : 0.0f;
	const float screenHeight = gameViewport ? static_cast<float>(gameViewport->screenHeight) : 0.0f;
	slConstants.cameraAspectRatio = screenHeight > 0.0f ? (screenWidth / screenHeight) : 1.0f;

	slConstants.cameraFOV = Util::GetVerticalFOVRad();
	slConstants.cameraNear = *globals::game::cameraNear;
	slConstants.cameraFar = *globals::game::cameraFar;

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
	auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust();
	slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
	slConstants.depthInverted = sl::Boolean::eFalse;

	recalculateCameraMatrices(slConstants);

	auto& upscaling = globals::features::upscaling;
	upscaling.PrepareHistoryResetForCurrentFrame();
	auto jitter = upscaling.jitter;
	slConstants.jitterOffset = { -jitter.x, -jitter.y };
	slConstants.reset = upscaling.ShouldResetHistoryThisFrame() ? sl::Boolean::eTrue : sl::Boolean::eFalse;

	slConstants.mvecScale = { 1.0f, 1.0f };
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, p_viewport))) {
		logger::error("[Streamline] Could not set constants");
		return false;
	}

	constantsResult = true;
	return constantsResult;
}

bool Streamline::IsRTXAndBelow40Series(
	const DXGI_ADAPTER_DESC& a_adapterDesc)
{
	const UINT vendorId = a_adapterDesc.VendorId;
	const UINT deviceId = a_adapterDesc.DeviceId;

	// Check if NVIDIA
	if (vendorId != kNvidiaVendorId)
		return false;

	// RTX 30 series (Ampere) - 0x2200-0x25FF
	if (deviceId >= 0x2200 && deviceId <= 0x25FF)
		return true;

	// RTX 20 series (Turing with RT cores) - 0x1E00-0x1FFF
	if (deviceId >= 0x1E00 && deviceId <= 0x1FFF)
		return true;

	return false;
}

bool Streamline::SetDLSSOptions(sl::ViewportHandle p_viewport, uint32_t width, uint32_t height, bool colorBuffersHDR)
{
	if (!slDLSSSetOptions)
		return false;

	auto& settings = globals::features::upscaling.settings;
	const uint32_t qualityMode = std::min<uint32_t>(settings.qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t dlssPreset = std::min<uint32_t>(settings.dlssPreset, Upscaling::kDLSSPresetMaxIndex);
	const bool useLegacyProfile = isRTXBelow40series;
	auto& cache = dlssOptionsCache;
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
		logger::critical("[Streamline] Could not enable DLSS for viewport {}: {}",
			static_cast<uint32_t>(p_viewport),
			magic_enum::enum_name(result));
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
	return true;
}

void Streamline::InvalidateDLSSOptionsCache()
{
	dlssOptionsCache = {};
}

void Streamline::ResetFrameTracking()
{
	{
		const std::lock_guard lock(frameTokenMutex);
		frameTokens = {};
		nextFrameTokenSlot = 0;
		latestFrameTokenFrame = UINT32_MAX;
	}
	constantsFrame = UINT32_MAX;
	constantsViewport = UINT32_MAX;
	constantsResult = false;
	lastReflexSleepFrame = UINT32_MAX;
	dlssgSimulationStartFrame = UINT32_MAX;
}

bool Streamline::EvaluateDLSS(sl::ViewportHandle vp,
	ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth)
{
	auto context = globals::d3d::context;
	if (!initialized || !featureDLSS || !slEvaluateFeature || !slSetTag || !context ||
		!colorIn || !colorOut || !depth || !mvec || !reactiveMask || !transparencyMask) {
		return false;
	}

	sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, 0 };
	sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, 0 };
	sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
	sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
	sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
	sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

	const bool colorBuffersHDR = GetDLSSColorBuffersHDR(colorIn);

	if (!CheckFrameConstants(vp))
		return false;
	const uint32_t currentFrame =
		globals::state ? globals::state->frameCount : UINT32_MAX;
	auto* const frameToken = GetFrameTokenForFrame(currentFrame);
	if (!frameToken)
		return false;
	if (!SetDLSSOptions(vp, outputWidth, extentOut.height, colorBuffersHDR))
		return false;

	// These markers surround only DLSS evaluation, not Skyrim's complete frame.
	// Do not expose them to marker-driven Reflex scheduling until the integration
	// can publish an authoritative full-frame sequence.
	const bool emitPCLMarkers = ReflexPolicy::ResolveCSMarkerOptimization(
		featureReflex,
		featurePCL,
		globals::features::upscaling.settings.reflexUseMarkersToOptimize &&
			reflexOptionsCache.useMarkersToOptimize)
	                                .enabled;
	const auto emitPCLMarker = [&](sl::PCLMarker marker, const char* stageName, uint32_t stageIndex) {
		if (!emitPCLMarkers || !slPCLSetMarker || !frameToken)
			return;
		const sl::Result markerResult = slPCLSetMarker(marker, *frameToken);
		if (markerResult != sl::Result::eOk) {
			static bool markerErrorLogged[2] = { false, false };
			const uint32_t boundedStageIndex = std::min(stageIndex, 1u);
			if (markerErrorLogged[boundedStageIndex])
				return;
			markerErrorLogged[boundedStageIndex] = true;
			logger::warn(
				"[Streamline] slPCLSetMarker({}) failed: {}",
				stageName,
				magic_enum::enum_name(markerResult));
		}
	};

	sl::ResourceTag tags[] = {
		{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentIn },
		{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentOut },
		{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn }
	};

	const sl::Result tagResult = slSetTag(vp, tags, _countof(tags), context);
	if (tagResult != sl::Result::eOk) {
		static sl::ViewportHandle lastLoggedTagErrorViewport{};
		static sl::Result lastLoggedTagErrorResult{};
		if (lastLoggedTagErrorViewport != vp || lastLoggedTagErrorResult != tagResult) {
			lastLoggedTagErrorViewport = vp;
			lastLoggedTagErrorResult = tagResult;
			logger::error(
				"[Streamline] slSetTag failed result={} viewport={} extentIn={}x{} extentOut={}x{}",
				static_cast<int>(tagResult),
				static_cast<uint32_t>(vp),
				extentIn.width,
				extentIn.height,
				extentOut.width,
				extentOut.height);
		}
		return false;
	}

	sl::ViewportHandle view(vp);
	const sl::BaseStructure* inputs[] = { &view };

	auto state = globals::state;
	if (state && state->frameAnnotations) {
		state->BeginPerfEvent("DLSS Evaluate");
	}

	emitPCLMarker(sl::PCLMarker::eRenderSubmitStart, "DLSS-EvaluateStart", 0);
	sl::Result evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);
	emitPCLMarker(sl::PCLMarker::eRenderSubmitEnd, "DLSS-EvaluateEnd", 1);

	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	if (evalResult != sl::Result::eOk) {
		static sl::ViewportHandle lastLoggedEvalErrorViewport{};
		static sl::Result lastLoggedEvalErrorResult{};
		if (lastLoggedEvalErrorViewport != vp || lastLoggedEvalErrorResult != evalResult) {
			lastLoggedEvalErrorViewport = vp;
			lastLoggedEvalErrorResult = evalResult;
			D3D11_TEXTURE2D_DESC colorInDesc{};
			D3D11_TEXTURE2D_DESC colorOutDesc{};
			TryGetTexture2DDesc(colorIn, colorInDesc);
			TryGetTexture2DDesc(colorOut, colorOutDesc);
			logger::error(
				"[Streamline] slEvaluateFeature failed result={} viewport={} colorIn={}x{} fmt={} colorOut={}x{} fmt={} extentIn={}x{} extentOut={}x{}",
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

bool Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors)
{
	auto renderer = globals::game::renderer;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	const auto* gameViewport = globals::game::graphicsState;
	const float2 baseSize{
		static_cast<float>(gameViewport ? gameViewport->screenWidth : 0),
		static_cast<float>(gameViewport ? gameViewport->screenHeight : 0)
	};
	const auto renderSize = Util::ConvertToDynamic(baseSize);
	auto& upscaling = globals::features::upscaling;
	const bool hasDistinctIntermediate =
		upscaling.sharpenerTexture &&
		upscaling.sharpenerTexture->resource &&
		upscaling.sharpenerTexture->srv &&
		upscaling.sharpenerTexture->resource.get() != a_upscalingTexture;
	static bool loggedMissingSharpenerTexture = false;
	if (!hasDistinctIntermediate) {
		if (!loggedMissingSharpenerTexture) {
			logger::error("[Streamline] DLSS requires a distinct intermediate output; skipping evaluate until resources are rebuilt.");
			loggedMissingSharpenerTexture = true;
		}
		upscaling.dlssSharpenerOutputValid = false;
		return false;
	}
	loggedMissingSharpenerTexture = false;

	// Streamline requires distinct input and output resources. Always resolve the
	// intermediate into kMAIN after evaluation, with or without RCAS sharpening.
	ID3D11Resource* colorOut = upscaling.sharpenerTexture->resource.get();
	upscaling.dlssSharpenerOutputValid = false;

	// Simple full-texture upscale.
	const sl::Extent extentIn{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
	const sl::Extent extentOut{ 0, 0, (uint)baseSize.x, (uint)baseSize.y };

	const bool evaluated = EvaluateDLSS(this->viewport,
		a_upscalingTexture, colorOut,
		depthTexture.texture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask,
		extentIn, extentOut, (uint)baseSize.x);
	upscaling.dlssSharpenerOutputValid = evaluated;
	static bool loggedEvaluateFailure = false;
	if (!evaluated) {
		if (!loggedEvaluateFailure) {
			logger::warn("[Streamline] DLSS/DLAA evaluate failed; keeping the current scene texture instead of sharpening stale output.");
			loggedEvaluateFailure = true;
		}
	} else {
		loggedEvaluateFailure = false;
	}
	return evaluated;
}

void Streamline::UpdateReflex()
{
	if (!initialized || !globals::state)
		return;

	const uint32_t currentFrame = globals::state->frameCount;
	const auto& upscaling = globals::features::upscaling;
	const bool dlssgMarkerPath =
		IsDLSSGInstance() && upscaling.UsesDLSSGFrameGeneration();

	const auto applyReflexOptionsIfChanged = [&](const sl::ReflexOptions& options, const char* onFailMessage) {
		if (reflexOptionsCache.valid &&
			reflexOptionsCache.mode == options.mode &&
			reflexOptionsCache.frameLimitUs == options.frameLimitUs &&
			reflexOptionsCache.useMarkersToOptimize == options.useMarkersToOptimize) {
			return false;
		}

		if (SL_FAILED(result, slReflexSetOptions(options))) {
			logger::error("[Streamline] {}: {}", onFailMessage, magic_enum::enum_name(result));
			return false;
		}

		reflexOptionsCache.valid = true;
		reflexOptionsCache.mode = options.mode;
		reflexOptionsCache.frameLimitUs = options.frameLimitUs;
		reflexOptionsCache.useMarkersToOptimize = options.useMarkersToOptimize;
		return true;
	};

	const bool reflexBlockedByFrameGeneration =
		!IsDLSSGInstance() && upscaling.IsFrameGenerationDx12PathActive();
	if (reflexBlockedByFrameGeneration) {
		if (reflexSupportedOnCurrentAdapter && featureReflex && slReflexSetOptions) {
			sl::ReflexOptions disabledOptions{};
			disabledOptions.mode = sl::ReflexMode::eOff;
			applyReflexOptionsIfChanged(
				disabledOptions,
				"Failed to disable Reflex while frame-generation DX12 path is active");
		}
		lastReflexSleepFrame = UINT32_MAX;
		return;
	}

	if (reflexSupportedOnCurrentAdapter && featureReflex && slReflexSetOptions) {
		auto& settings = globals::features::upscaling.settings;
		sl::ReflexOptions options{};
		const bool dlssgRequiresReflex =
			IsDLSSGInstance() && upscaling.ShouldUseFrameGenerationThisFrame();
		options.mode = !settings.reflexLowLatencyMode && !dlssgRequiresReflex ?
		                   sl::ReflexMode::eOff :
		                   (settings.reflexLowLatencyBoost ?
								   sl::ReflexMode::eLowLatencyWithBoost :
								   sl::ReflexMode::eLowLatency);

		const float originalReflexFPSLimit = settings.reflexFPSLimit;
		float reflexFPSLimit = originalReflexFPSLimit;
		if (!std::isfinite(reflexFPSLimit)) {
			reflexFPSLimit = 60.0f;
			settings.reflexFPSLimit = reflexFPSLimit;
			logger::warn(
				"[Streamline] reflexFPSLimit is not finite ({}), using {}",
				originalReflexFPSLimit,
				reflexFPSLimit);
		}
		const float fpsLimit = std::clamp(reflexFPSLimit, 20.0f, 240.0f);
		options.frameLimitUs = settings.reflexUseFPSLimit ?
		                           static_cast<uint32_t>(std::lround(
									   1000000.0 / static_cast<double>(fpsLimit))) :
		                           0u;
		const auto markerOptimization = ReflexPolicy::ResolveCSMarkerOptimization(
			featureReflex,
			featurePCL,
			settings.reflexUseMarkersToOptimize);
		options.useMarkersToOptimize = markerOptimization.enabled;

		if (applyReflexOptionsIfChanged(options, "Failed to apply Reflex options")) {
			logger::info(
				"[Streamline] Applied Reflex options: mode={} frameLimitUs={} markersRequested={} markersAvailable={} markersEffective={}",
				magic_enum::enum_name(options.mode),
				options.frameLimitUs,
				settings.reflexUseMarkersToOptimize,
				markerOptimization.available,
				options.useMarkersToOptimize);
		}
	}

	if (!EnsureFrameToken())
		return;
	auto* const frameToken = GetFrameTokenForFrame(currentFrame);
	if (!frameToken)
		return;

	// PCL and sleep have independent failure domains. A pacing failure must not
	// suppress the marker cycle that Streamline uses for interpolation.
	if (lastReflexSleepFrame != currentFrame) {
		lastReflexSleepFrame = currentFrame;
		if (reflexSupportedOnCurrentAdapter && featureReflex && slReflexSleep) {
			const sl::Result sleepResult = slReflexSleep(*frameToken);
			if (sleepResult != sl::Result::eOk) {
				logger::warn(
					"[Streamline] Reflex sleep call failed: {}",
					magic_enum::enum_name(sleepResult));
			}
		}
	}

	if (dlssgMarkerPath && dlssgSimulationStartFrame != currentFrame) {
		dlssgSimulationStartFrame =
			EmitPCLMarkerForFrame(
				sl::PCLMarker::eSimulationStart,
				"SimulationStart",
				currentFrame) ?
				currentFrame :
				UINT32_MAX;
	}
}

bool Streamline::IsDLSSGReflexReadyForCurrentFrame() const
{
	return globals::state && IsDLSSGFrameReady(globals::state->frameCount);
}

bool Streamline::IsDLSSGFrameReady(uint32_t a_frame) const
{
	return IsDLSSGInstance() && GetFrameTokenForFrame(a_frame) &&
	       dlssgSimulationStartFrame == a_frame;
}

bool Streamline::EmitPCLMarker(
	sl::PCLMarker a_marker,
	std::string_view a_name,
	sl::FrameToken* a_token)
{
	if (!featurePCL || !slPCLSetMarker || !a_token)
		return false;

	const sl::Result result = slPCLSetMarker(a_marker, *a_token);
	if (result != sl::Result::eOk) {
		const uint32_t markerIndex = static_cast<uint32_t>(a_marker);
		const uint32_t markerBit = markerIndex < 32 ? (1u << markerIndex) : 0;
		if (markerBit == 0 || (pclMarkerFailureLogMask & markerBit) == 0) {
			pclMarkerFailureLogMask |= markerBit;
			logger::warn(
				"[Streamline {}] PCL marker {} failed: {}",
				IsDLSSGInstance() ? "DX12" : "DX11",
				a_name,
				magic_enum::enum_name(result));
		}
		return false;
	}
	const uint32_t markerIndex = static_cast<uint32_t>(a_marker);
	if (markerIndex < 32)
		pclMarkerFailureLogMask &= ~(1u << markerIndex);
	return true;
}

bool Streamline::EmitPCLMarkerForFrame(
	sl::PCLMarker a_marker,
	std::string_view a_name,
	uint32_t a_frame)
{
	auto* const frameToken = GetFrameTokenForFrame(a_frame);
	if (!frameToken) {
		logger::error(
			"[Streamline DX12] Refusing {} for render frame {}; latest token frame is {}.",
			a_name,
			a_frame,
			GetLatestFrameTokenFrame());
		return false;
	}
	return EmitPCLMarker(a_marker, a_name, frameToken);
}

bool Streamline::ConfigureDLSSG(
	bool a_enabled,
	uint32_t a_width,
	uint32_t a_height,
	uint32_t a_renderWidth,
	uint32_t a_renderHeight)
{
	if (!IsDLSSGInstance() || !initialized || !featureDLSSG || !slDLSSGSetOptions) {
		ResetDLSSGState();
		return false;
	}

	const bool enableRequested = a_enabled;
	const auto& swapChain = globals::features::upscaling.dx12SwapChain;
	if (!a_width)
		a_width = swapChain.swapChainDesc.Width;
	if (!a_height)
		a_height = swapChain.swapChainDesc.Height;
	if (!a_renderWidth)
		a_renderWidth = a_width;
	if (!a_renderHeight)
		a_renderHeight = a_height;
	if (a_width == 0 || a_height == 0 || a_renderWidth == 0 ||
		a_renderHeight == 0)
		a_enabled = false;
	if (dlssgState.minimumDimension != 0 &&
		(std::min(a_width, a_height) < dlssgState.minimumDimension)) {
		a_enabled = false;
	}
	if (dlssgState.maximumFramesToGenerate == 0)
		a_enabled = false;

	sl::DLSSGOptions options{};
	options.mode = a_enabled ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
	options.numFramesToGenerate =
		dlssgState.maximumFramesToGenerate == 0 ?
			1u :
			std::clamp<uint32_t>(
				globals::features::upscaling.settings.dlssgFramesToGenerate,
				1u,
				dlssgState.maximumFramesToGenerate);

	const sl::Result result = slDLSSGSetOptions(viewport, options);
	if (result != sl::Result::eOk) {
		if (!loggedDLSSGOptionsFailure) {
			loggedDLSSGOptionsFailure = true;
			logger::error(
				"[Streamline DX12] slDLSSGSetOptions({}) failed: {}",
				a_enabled ? "on" : "off",
				magic_enum::enum_name(result));
		}
		// Preserve the last successfully applied mode. A failed disable must keep
		// swap-chain mutation gates closed until Streamline confirms it is off.
		dlssgState.active = false;
		return false;
	}
	loggedDLSSGOptionsFailure = false;

	dlssgState.optionsEnabled = a_enabled;
	dlssgState.optionsTransitionPending =
		dlssgState.optionsApplied != a_enabled;
	if (!a_enabled) {
		dlssgState.framesActuallyPresented = 0;
		dlssgState.active = false;
	}
	return !enableRequested || a_enabled;
}

Streamline::DLSSGTagResult Streamline::ClearDLSSGResourceTags(uint32_t a_frame)
{
	auto* const frameToken = GetFrameTokenForFrame(a_frame);
	if (!IsDLSSGInstance() || !initialized || !featureDLSSG ||
		!slSetTagForFrame || !frameToken) {
		return DLSSGTagResult::kFailed;
	}

	sl::ResourceTag nullTags[] = {
		{ nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent },
		{ nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent },
		{ nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent },
		{ nullptr, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent }
	};
	// All-null eValidUntilPresent tags require no command list and therefore
	// remain available on fail-closed Present paths with broken host interop.
	const sl::Result result = slSetTagForFrame(
		*frameToken,
		viewport,
		nullTags,
		_countof(nullTags),
		nullptr);
	if (result != sl::Result::eOk) {
		if (!loggedDLSSGNullTagFailure) {
			loggedDLSSGNullTagFailure = true;
			logger::error(
				"[Streamline DX12] Could not clear DLSS-G resource tags: {}",
				magic_enum::enum_name(result));
		}
		return DLSSGTagResult::kFailed;
	}
	loggedDLSSGNullTagFailure = false;
	return DLSSGTagResult::kCleared;
}

Streamline::DLSSGTagResult Streamline::TagDLSSGResources(
	ID3D12GraphicsCommandList* a_commandList,
	ID3D12Resource* a_depth,
	ID3D12Resource* a_motionVectors,
	ID3D12Resource* a_hudLessColor,
	ID3D12Resource* a_uiColorAndAlpha,
	uint32_t a_renderWidth,
	uint32_t a_renderHeight,
	uint32_t a_frame)
{
	auto* const frameToken = GetFrameTokenForFrame(a_frame);
	if (!IsDLSSGInstance() || !initialized || !featureDLSSG ||
		!slSetTagForFrame || !frameToken) {
		return DLSSGTagResult::kFailed;
	}

	if (!a_depth || !a_motionVectors || !a_hudLessColor || !a_uiColorAndAlpha) {
		return ClearDLSSGResourceTags(a_frame);
	}
	if (!a_commandList)
		return DLSSGTagResult::kFailed;

	const auto depthDesc = a_depth->GetDesc();
	const auto motionVectorDesc = a_motionVectors->GetDesc();
	const auto sceneDesc = a_hudLessColor->GetDesc();
	const auto uiDesc = a_uiColorAndAlpha->GetDesc();
	const bool validDimensions =
		depthDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
		motionVectorDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
		sceneDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
		uiDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
		depthDesc.Width > 0 && depthDesc.Height > 0 &&
		depthDesc.Width == motionVectorDesc.Width &&
		depthDesc.Height == motionVectorDesc.Height &&
		sceneDesc.Width > 0 && sceneDesc.Height > 0 &&
		sceneDesc.Width == uiDesc.Width &&
		sceneDesc.Height == uiDesc.Height &&
		uiDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
		a_renderWidth > 0 && a_renderHeight > 0 &&
		a_renderWidth <= depthDesc.Width &&
		a_renderHeight <= depthDesc.Height &&
		a_renderWidth <= motionVectorDesc.Width &&
		a_renderHeight <= motionVectorDesc.Height;
	if (!validDimensions) {
		if (!loggedDLSSGResourceValidationFailure) {
			loggedDLSSGResourceValidationFailure = true;
			logger::error("[Streamline DX12] DLSS-G resources have incompatible dimensions, types, or UI format.");
		}
		return ClearDLSSGResourceTags(a_frame);
	}
	loggedDLSSGResourceValidationFailure = false;

	const sl::Extent renderExtent{
		0,
		0,
		a_renderWidth,
		a_renderHeight
	};
	const sl::Extent outputExtent{
		0,
		0,
		static_cast<uint32_t>(sceneDesc.Width),
		static_cast<uint32_t>(sceneDesc.Height)
	};
	sl::Resource depth{
		sl::ResourceType::eTex2d,
		a_depth,
		D3D12_RESOURCE_STATE_COMMON
	};
	sl::Resource motionVectors{
		sl::ResourceType::eTex2d,
		a_motionVectors,
		D3D12_RESOURCE_STATE_COMMON
	};
	sl::Resource hudLessColor{
		sl::ResourceType::eTex2d,
		a_hudLessColor,
		D3D12_RESOURCE_STATE_COMMON
	};
	sl::Resource uiColorAndAlpha{
		sl::ResourceType::eTex2d,
		a_uiColorAndAlpha,
		D3D12_RESOURCE_STATE_COMMON
	};
	sl::ResourceTag tags[] = {
		{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent },
		{ &motionVectors, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent },
		{ &hudLessColor, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &outputExtent },
		{ &uiColorAndAlpha, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &outputExtent }
	};

	// A failed multi-tag call can leave provider ownership ambiguous. Close the
	// lifetime gate before submission and reopen it only after a null-tag Present.
	dlssgState.tagsCleared = false;
	const sl::Result result = slSetTagForFrame(
		*frameToken,
		viewport,
		tags,
		_countof(tags),
		a_commandList);
	if (result != sl::Result::eOk) {
		if (!loggedDLSSGTagFailure) {
			loggedDLSSGTagFailure = true;
			logger::error(
				"[Streamline DX12] DLSS-G resource tagging failed: {}",
				magic_enum::enum_name(result));
		}
		return ClearDLSSGResourceTags(a_frame);
	}
	loggedDLSSGTagFailure = false;
	return DLSSGTagResult::kTagged;
}

void Streamline::RequestDLSSGDisable()
{
	dlssgState.disablePending = true;
	dlssgState.active = false;
}

Streamline::DLSSGPresentSync Streamline::UpdateDLSSGStateAfterPresent(
	bool a_generationRequested,
	bool a_providerBoundaryCompleted,
	bool a_outputAccepted,
	bool a_nullTagsSubmitted)
{
	DLSSGPresentSync sync{};
	if (!IsDLSSGInstance() || !featureDLSSG || !slDLSSGGetState) {
		ResetDLSSGState();
		return sync;
	}
	// Streamline consumes options and eValidUntilPresent tags before forwarding
	// to native DXGI, independently of whether DXGI displays the frame.
	if (a_providerBoundaryCompleted) {
		dlssgState.optionsApplied = dlssgState.optionsEnabled;
		dlssgState.optionsTransitionPending = false;
		if (a_nullTagsSubmitted)
			dlssgState.tagsCleared = true;
	}

	sl::DLSSGState state{};
	const sl::Result result = slDLSSGGetState(viewport, state, nullptr);
	if (result != sl::Result::eOk) {
		if (!loggedDLSSGStateFailure) {
			loggedDLSSGStateFailure = true;
			logger::warn(
				"[Streamline DX12] slDLSSGGetState after Present failed: {}",
				magic_enum::enum_name(result));
		}
		RequestDLSSGDisable();
		return sync;
	}
	loggedDLSSGStateFailure = false;
	if (state.inputsProcessingCompletionFence &&
		state.lastPresentInputsProcessingCompletionFenceValue != 0) {
		sync.inputsCompletionFence.copy_from(
			static_cast<ID3D12Fence*>(state.inputsProcessingCompletionFence));
		sync.inputsCompletionValue =
			state.lastPresentInputsProcessingCompletionFenceValue;
	}

	dlssgState.status = state.status;
	dlssgState.minimumDimension = state.minWidthOrHeight;
	dlssgState.maximumFramesToGenerate = std::min(
		state.numFramesToGenerateMax,
		Upscaling::kDLSSGMaximumGeneratedFrames);
	dlssgState.framesActuallyPresented = state.numFramesActuallyPresented;
	if (state.status != sl::DLSSGStatus::eOk ||
		dlssgState.maximumFramesToGenerate == 0) {
		RequestDLSSGDisable();
	} else if (a_providerBoundaryCompleted && !a_generationRequested &&
			   !dlssgState.optionsEnabled && dlssgState.tagsCleared) {
		dlssgState.disablePending = false;
	}
	const bool providerReady =
		state.status == sl::DLSSGStatus::eOk &&
		dlssgState.maximumFramesToGenerate != 0 &&
		!dlssgState.disablePending;
	if (a_outputAccepted && a_generationRequested && dlssgState.optionsEnabled &&
		providerReady &&
		state.numFramesActuallyPresented <= 1) {
		if (dlssgState.consecutiveNoGeneratedPresents < UINT32_MAX) {
			const uint32_t missingPresents =
				++dlssgState.consecutiveNoGeneratedPresents;
			if (missingPresents == kDLSSGDiagnosticFrameThreshold) {
				logger::warn(
					"[Streamline DX12] DLSS-G reported no interpolated output for {} consecutive requested Presents (status 0x{:X}).",
					missingPresents,
					static_cast<unsigned>(state.status));
			}
		}
	} else {
		dlssgState.consecutiveNoGeneratedPresents = 0;
	}
	dlssgState.active =
		a_outputAccepted && a_generationRequested &&
		dlssgState.optionsEnabled &&
		providerReady &&
		state.numFramesActuallyPresented > 1;
	return sync;
}

void Streamline::OnDLSSGAPIError(const sl::APIError& a_error)
{
	if (a_error.hres == S_OK)
		return;

	auto& pendingError =
		globals::features::upscaling.streamlineDX12.dlssgAPIError;
	HRESULT pending = pendingError.load(std::memory_order_acquire);
	for (;;) {
		// Preserve the first failure. A failure must still replace an earlier
		// non-failing status such as DXGI_STATUS_OCCLUDED.
		if (FAILED(pending) ||
			(pending != S_OK && SUCCEEDED(a_error.hres))) {
			return;
		}
		if (pendingError.compare_exchange_weak(
				pending,
				a_error.hres,
				std::memory_order_acq_rel,
				std::memory_order_acquire)) {
			return;
		}
	}
}

HRESULT Streamline::ConsumeDLSSGAPIError()
{
	return dlssgAPIError.exchange(S_OK, std::memory_order_acq_rel);
}

void Streamline::ResetDLSSGState()
{
	dlssgState = {};
	dlssgState.maximumFramesToGenerate = 0;
	loggedDLSSGOptionsFailure = false;
	loggedDLSSGResourceValidationFailure = false;
	loggedDLSSGTagFailure = false;
	loggedDLSSGNullTagFailure = false;
	loggedDLSSGStateFailure = false;
	dlssgAPIError.store(S_OK, std::memory_order_release);
}

/**
 * @brief Releases DLSS resources and disables DLSS for the current viewport.
 *
 * Sets the DLSS mode to off and frees all DLSS-related resources associated with the viewport.
 */
void Streamline::DestroyDLSSResources()
{
	if (!initialized || !featureDLSS || !slDLSSSetOptions || !slFreeResources) {
		InvalidateDLSSOptionsCache();
		ResetFrameTracking();
		return;
	}

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;

	if (auto context = globals::d3d::context)
		FlushAndWaitForD3D11Idle(context, "DLSS resource free");

	const auto freeViewport = [&](sl::ViewportHandle a_viewport) {
		const sl::Result optionsResult = slDLSSSetOptions(a_viewport, dlssOptions);
		if (optionsResult != sl::Result::eOk) {
			logger::debug("[Streamline] DLSS off failed for viewport {}: {}",
				static_cast<uint32_t>(a_viewport),
				magic_enum::enum_name(optionsResult));
		}

		const sl::Result freeResult = slFreeResources(sl::kFeatureDLSS, a_viewport);
		if (freeResult != sl::Result::eOk) {
			logger::debug("[Streamline] DLSS resource free failed for viewport {}: {}",
				static_cast<uint32_t>(a_viewport),
				magic_enum::enum_name(freeResult));
		}
	};

	freeViewport(viewport);

	InvalidateDLSSOptionsCache();
	ResetFrameTracking();
}
