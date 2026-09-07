#include "Api/EditorDevBenchBridge.h"
#include "Api/EditorService.h"
#include "Api/FeatureDevBenchBridge.h"
#include "Api/FeatureService.h"
#include "Api/ProfilerApiDevBenchBridge.h"
#include "Api/ProfilerService.h"
#include "Api/RuntimeThreadAffinity.h"
#include "Api/ServiceRegistryProvider.h"
#include "Api/ShaderCompatibilityRegistry.h"
#include "Api/ShaderDevBenchBridge.h"
#include "Api/UpscalingDevBenchBridge.h"
#include "Api/WeatherDevBenchBridge.h"
#include "Api/WeatherService.h"
#include "BuildProvenance.h"
#include "Compatibility.h"
#include "Deferred.h"
#include "Features/InteriorSun.h"
#include "Features/LightLimitFix.h"
#include "Features/Upscaling.h"
#include "FrameAnnotations.h"
#include "Globals.h"
#include "Hooks.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "MenuDevBenchBridge.h"
#include "PerformanceTuningDevBenchBridge.h"
#include "ProfilerDevBenchBridge.h"
#include "SceneSettingsManager.h"
#include "ScreenshotDevBenchBridge.h"
#include "ShaderCache.h"
#include "State.h"
#include "VRAPI/CSpluginapi.h"
#include "WeatherManager.h"

#include "ENB/ENBSeriesAPI.h"

#include <atomic>

#define DLLEXPORT __declspec(dllexport)

std::list<std::string> errors;

bool Load();

namespace
{
	constexpr std::size_t kTrampolineCapacity = 1 << 12;
	std::atomic_bool g_initialGameEntryConsumed{ false };

	void PushStartupError(std::string errorMessage)
	{
		logger::error("{}", errorMessage);
		errors.push_back(std::move(errorMessage));
	}

	void CommunityShadersAPIMessageHandler(SKSE::MessagingInterface::Message* a_message)
	{
		// Preserve the legacy CSAP handler exactly while exposing the parallel
		// versioned CSXR registry through the same SKSE listener.
		CSPluginAPI::ModMessageHandler(a_message);
		CSX::Api::HandleServiceRegistryMessage(a_message);
	}

	bool RegisterCommunityShadersAPIMessageListener()
	{
		auto messaging = SKSE::GetMessagingInterface();
		if (!messaging) {
			PushStartupError("SKSE messaging interface unavailable while registering CSX API message listener. Check CommunityShaders.log for details.");
			return false;
		}

		CSX::Api::InitializeServiceRegistryProvider();
		if (!messaging->RegisterListener(nullptr, CommunityShadersAPIMessageHandler)) {
			PushStartupError("Failed to register CSX API message listener. Check CommunityShaders.log for details.");
			return false;
		}

		logger::info("Registered legacy CSAP and versioned CSXR API message listener before PostLoad dispatch");
		return true;
	}

	void ResetRuntimeStateAfterGameLoad()
	{
		if (globals::state) {
			globals::state->pendingPostLoadRuntimeReset = true;
		}
		globals::game::quitGame.store(false, std::memory_order_release);
		globals::OnDataLoaded();
		WeatherManager::GetSingleton()->ClearCache();
		globals::features::lightLimitFix.Reset();
		globals::features::interiorSun.isInteriorWithSun = false;
		globals::features::upscaling.RequestPostLoadRuntimeReset();
	}
}

void InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		util::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = a_level;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
#ifndef NDEBUG
	while (!REX::W32::IsDebuggerPresent()) {};
#endif
	InitializeLog();
	logger::info("Loaded {} {}", Plugin::NAME, Plugin::VERSION_LABEL);
	BuildProvenance::LogRuntimeIdentity();
	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(kTrampolineCapacity);
	return Load();
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}

void MessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kPostLoad:
		{
			// Establish the API owner from an actual SKSE game-thread task. The
			// lifecycle callback itself is not a reliable thread-affinity oracle.
			CSX::Api::ScheduleRuntimeMainThreadBinding();
			CSX::Api::InitializeEditorService();
			CSX::Api::InitializeFeatureService();
			CSX::Api::InitializeProfilerService();
			CSX::Api::InitializeWeatherService();
			// Publish diagnostic adapters before cache validation and shader
			// compilation. If DevBench's PostLoad listener runs later, the
			// PostPostLoad attempt below provides the deterministic retry.
			CSX::Api::ProfilerApiDevBenchBridge::Install();
			ScreenshotDevBenchBridge::Install();
			CSX::Api::UpscalingDevBenchBridge::Install();
			CSX::Api::WeatherDevBenchBridge::Install();
			CSX::Api::EditorDevBenchBridge::Install();
			CSX::Api::FeatureDevBenchBridge::Install();
			CSX::Api::ShaderDevBenchBridge::Install();
			break;
		}
	case SKSE::MessagingInterface::kPostPostLoad:
		{
			if (errors.empty()) {
				ScreenshotDevBenchBridge::Install();
				CSX::Api::ProfilerApiDevBenchBridge::Install();
				// DevBench publishes its interface from its own PostLoad listener. If
				// CSX's listener ran first, this is the first deterministic retry after
				// all PostLoad listeners have completed.
				CSX::Api::UpscalingDevBenchBridge::Install();
				CSX::Api::WeatherDevBenchBridge::Install();
				CSX::Api::EditorDevBenchBridge::Install();
				CSX::Api::FeatureDevBenchBridge::Install();
				CSX::Api::ShaderDevBenchBridge::Install();
				Deferred::Hooks::Install();
				Hooks::Install();
				EngineFix::InstallOnPostPostLoadFixes();
				FrameAnnotations::OnPostPostLoad();

				auto shaderCache = globals::shaderCache;

				// Run feature PostPostLoad() first so features can disable themselves if needed
				Feature::ForEachLoadedFeature("PostPostLoad", [](Feature* feature) { feature->PostPostLoad(); });

				// Temporary adapter for the existing Horizon Fix integration. Future
				// external shader providers register their own identity through
				// csx.shader.compatibility instead of requiring a CSX exception.
				if (GetModuleHandleW(L"HorizonFix.dll")) {
					const CSX::ShaderCompatibilityAPI::Scope001 scope{
						.structSize = sizeof(CSX::ShaderCompatibilityAPI::Scope001),
						.kind = CSX::ShaderCompatibilityAPI::ScopeKind::kShaderFamily,
						.value = "Water",
					};
					const CSX::ShaderCompatibilityAPI::Registration001 registration{
						.structSize = sizeof(CSX::ShaderCompatibilityAPI::Registration001),
						.identity = "legacy.horizonfix.water",
						.owner = "CSX legacy adapter",
						.displayVersion = "detected",
						.contractMajor = 1,
						.currentMinor = 0,
						.minimumCompatibleMinor = 0,
						.maximumCompatibleMinor = 0,
						.resourceFingerprint = "",
						.scopes = &scope,
						.scopeCount = 1,
					};
					const auto result = CSX::Api::GetShaderCompatibilityRegistry().Register(registration);
					if (result.status != CSX::ShaderCompatibilityAPI::Status::kSuccess)
						logger::warn("Horizon Fix shader compatibility registration failed: {}", result.message);
				}

				// Register scene settings event handler (Interior Only transitions)
				SceneSettingsManager::MenuOpenCloseEventHandler::Register();

				// External shader-facing requirements must become immutable before
				// any cache compatibility decision or shader compilation.
				CSX::Api::FreezeShaderCompatibilityRegistrations();

				// Now validate disk cache after features and external providers have
				// had a chance to declare their shader-facing state.
				shaderCache->ValidateDiskCache();

				if (shaderCache->UseFileWatcher())
					shaderCache->StartFileWatcher();
			}

			break;
		}
	case SKSE::MessagingInterface::kDataLoaded:
		{
			for (auto it = errors.begin(); it != errors.end(); ++it) {
				auto& errorMessage = *it;
				RE::DebugMessageBox(std::format("CSX\n{}\nAll hooks and features are disabled.", errorMessage).c_str());
			}

			if (errors.empty()) {
				globals::OnDataLoaded();
				EngineFix::InstallOnDataLoadedFixes();
				FrameAnnotations::OnDataLoaded();

				auto shaderCache = globals::shaderCache;
				while (shaderCache->IsCompiling() &&
					   !shaderCache->backgroundCompilation.load(std::memory_order_relaxed) &&
					   !globals::game::quitGame.load(std::memory_order_relaxed)) {
					std::this_thread::sleep_for(100ms);
				}
				// Entering background mode releases this wait before the initial batch
				// drains. Mark DataLoaded separately and let the last completion perform
				// the priority transition in that path.
				shaderCache->menuLoaded = true;
				shaderCache->TryCompleteStartupCompilationPhase();

				if (globals::game::quitGame.load(std::memory_order_relaxed)) {
					logger::info("Game was closed, skipping feature DataLoaded methods");
					break;
				}

				if (shaderCache->HasFeatureSetChanges()) {
					shaderCache->CommitFeatureSetChange();
				} else if (shaderCache->IsDiskCacheActive()) {
					shaderCache->WriteDiskCacheInfo();
				}

				Feature::ForEachLoadedFeature("DataLoaded", [](Feature* feature) { feature->DataLoaded(); });
				CSX::Api::InitializeProfilerService();
				CSX::Api::InitializeWeatherService();
				CSX::Api::EditorDevBenchBridge::Install();
				CSX::Api::InitializeFeatureService();
				CSX::Api::FeatureDevBenchBridge::Install();
				ProfilerDevBenchBridge::Install();
				MenuDevBenchBridge::Install();
				PerformanceTuningDevBenchBridge::Install();
				ScreenshotDevBenchBridge::Install();
				CSX::Api::ProfilerApiDevBenchBridge::Install();
				CSX::Api::UpscalingDevBenchBridge::Install();
				CSX::Api::WeatherDevBenchBridge::Install();
				CSX::Api::ShaderDevBenchBridge::Install();
				globals::state->startupMenuInitializationComplete.store(true, std::memory_order_release);
			}

			break;
		}
	case SKSE::MessagingInterface::kPreLoadGame:
		{
			if (errors.empty()) {
				const bool initialProcessSaveLoad =
					!g_initialGameEntryConsumed.load(std::memory_order_acquire);
				globals::features::upscaling.NotifyGamePreLoadStarted(
					initialProcessSaveLoad);
				globals::features::upscaling.ArmVRVendorWorkGate(
					Upscaling::VRVendorWorkGateSource::PreLoadGame,
					"SKSE pre-load game");
				if (globals::state) {
					const uint32_t frame = globals::state->frameCount;
					globals::state->BeginSaveLoadSafeMode(frame);
					globals::state->BeginPersistentMutationBlock(frame);
				}
			}

			break;
		}
	case SKSE::MessagingInterface::kSaveGame:
		{
			if (errors.empty() && globals::state) {
				const uint32_t frame = globals::state->frameCount;
				globals::state->ExtendSaveLoadSafeMode(frame, State::kSaveLoadSafeModeGraceFrames);
				globals::state->ExtendPersistentMutationBlock(frame, State::kSaveMutationBlockGraceFrames);
			}

			break;
		}
	case SKSE::MessagingInterface::kPostLoadGame:
	case SKSE::MessagingInterface::kNewGame:
		{
			if (errors.empty()) {
				const bool newGame = message->type == SKSE::MessagingInterface::kNewGame;
				const bool firstGameEntry =
					!g_initialGameEntryConsumed.exchange(true, std::memory_order_acq_rel);
				// New Game owns the separate RaceSex/startup presentation path.
				// Consume the process-lifetime entry latch, but reserve this
				// compositor hold for the first save loaded in this process.
				const bool initialProcessSaveLoad = !newGame && firstGameEntry;
				globals::features::upscaling.NotifyGameLoadStarted(
					newGame,
					initialProcessSaveLoad);
				if (globals::state) {
					const uint32_t frame = globals::state->frameCount;
					globals::state->ExtendSaveLoadSafeMode(frame, State::kSaveLoadSafeModeGraceFrames);
					globals::state->ExtendPersistentMutationBlock(frame, State::kSaveMutationBlockGraceFrames);
				}
				ResetRuntimeStateAfterGameLoad();
				logger::info("Handled {}", newGame ? "kNewGame" : "kPostLoadGame");
			}

			break;
		}
	}
}

bool Load()
{
	if (ENB_API::RequestENBAPI()) {
		logger::info("ENB detected, disabling all hooks and features");
		return true;
	}

	if (REL::Module::IsVR()) {
		REL::IDDB::get().IsVRAddressLibraryAtLeastVersion("0.207.0", true);
	}

	auto privateProfileRedirectorVersion = Util::GetDllVersion(L"Data/SKSE/Plugins/PrivateProfileRedirector.dll");
	if (privateProfileRedirectorVersion.has_value() && privateProfileRedirectorVersion.value().compare(REL::Version(0, 6, 2)) == std::strong_ordering::less) {
		stl::report_and_fail("Old version of PrivateProfileRedirector detected, 0.6.2+ required if using it."sv);
	}

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging) {
		logger::error("SKSE messaging interface unavailable");
		return false;
	}
	if (!RegisterCommunityShadersAPIMessageListener())
		return false;

	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		logger::error("Failed to register SKSE message listener");
		return false;
	}

	globals::OnInit();
	globals::ReInit();

	auto state = globals::state;
	state->Load(
		State::ConfigMode::USER,
		true,
		State::SettingsApplyMode::StartupHydration);
	state->LoadTheme();  // Load theme settings from SettingsTheme.json

	// Initialize theme system - create default themes and discover existing ones
	globals::menu->CreateDefaultThemes();  // Creates JSON files if they don't exist
	auto themeManager = ThemeManager::GetSingleton();
	themeManager->DiscoverThemes();  // Discover all available themes

	auto log = spdlog::default_logger();
	log->set_level(state->GetLogLevel());

	for (const auto& plugin : Compatibility::incompatiblePlugins) {
		if (LoadLibrary(plugin.dll)) {
			auto dllName = stl::utf16_to_utf8(plugin.dll).value_or("<unicode conversion error>"s);
			auto errorMessage = plugin.reason.empty() ?
			                        std::format("Incompatible DLL {} detected. Remove it to use CSX.", dllName) :
			                        std::format("Incompatible DLL {} detected ({}). Remove it to use CSX.", dllName, plugin.reason);
			logger::error("{}", errorMessage);
			errors.push_back(errorMessage);
		}
	}

	auto pushMissingDllError = [&](std::string_view dllName) {
		auto errorMessage = std::format("Required DLL {} was missing. Install it to use CSX.", dllName);
		logger::error("{}", errorMessage);
		errors.push_back(errorMessage);
	};

	// Engine Fixes: VR accepts either EngineFixesVR.dll or the EngineFixes.dll NG
	if (REL::Module::IsVR()) {
		if (!LoadLibrary(L"Data/SKSE/Plugins/EngineFixesVR.dll") && !LoadLibrary(L"Data/SKSE/Plugins/EngineFixes.dll")) {
			pushMissingDllError("EngineFixesVR.dll or EngineFixes.dll");
		}
	} else {
		if (!LoadLibrary(L"Data/SKSE/Plugins/EngineFixes.dll")) {
			pushMissingDllError(stl::utf16_to_utf8(L"Data/SKSE/Plugins/EngineFixes.dll").value_or("<unicode conversion error>"s));
		}
	}

	// Empty RequiredDLLs array, if necessary we can add a dll here in the future without needing to modify the plugin loading logic.
	const std::array<LPCWSTR, 0> requiredDLLs{};

	for (const auto dll : requiredDLLs) {
		if (!LoadLibrary(dll)) {
			pushMissingDllError(stl::utf16_to_utf8(dll).value_or("<unicode conversion error>"s));
		}
	}

	if (errors.empty()) {
		Hooks::InstallEarlyHooks();
		logger::info("Calling feature Load methods");
		Feature::ForEachLoadedFeature("Load", [](Feature* feature) { feature->Load(); });
	}

	return true;
}
