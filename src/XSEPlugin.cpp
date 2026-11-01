#include "MenuDevBenchBridge.h"
#include "PerformanceTuningDevBenchBridge.h"
#include "ProfilerDevBenchBridge.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/NvPerf.h"
#endif
#include "Api/AcceptedDrawService.h"
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
#include "CSEditor/EditorWindow.h"
#include "Compatibility.h"
#include "Deferred.h"
#include "Diagnostics/D3DTextureLifetimeDevBenchBridge.h"
#include "Diagnostics/EngineStutterMonitor.h"
#include "Diagnostics/FrameGenerationDevBenchBridge.h"
#include "Diagnostics/SunReceiverTrace.h"
#include "Features/InteriorSun.h"
#include "Features/LightLimitFix.h"
#include "Features/Skylighting.h"
#include "Features/Upscaling.h"
#include "Features/VolumetricLighting.h"
#include "FrameAnnotations.h"
#include "Globals.h"
#include "Hooks.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "Menu/ThemeManager.h"
#include "SceneSettingsManager.h"
#include "ScreenshotDevBenchBridge.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/CharacterCategoryAuthoring.h"
#include "Utils/NvApiDrs.h"
#include "WeatherManager.h"

#include "ENB/ENBSeriesAPI.h"

#define DLLEXPORT __declspec(dllexport)

std::list<std::string> errors;

bool Load();

[[noreturn]] void PushStartupError(std::string message)
{
	logger::error("{}", message);
	errors.push_back(std::move(message));
	stl::report_and_fail(std::format("CSX cannot start.\n\n{}\n\nResolve this error and restart Skyrim.", errors.back()));
}

void CommunityShadersAPIMessageHandler(SKSE::MessagingInterface::Message* message)
{
	CSX::Api::HandleServiceRegistryMessage(message);
}

void RegisterCommunityShadersAPIMessageListener()
{
	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging) {
		PushStartupError("SKSE messaging interface unavailable while registering CSX API message listener. Check CSX.log for details.");
	}

	if (!messaging->RegisterListener(nullptr, CommunityShadersAPIMessageHandler)) {
		PushStartupError("Failed to register CSX API message listener. Check CSX.log for details.");
	}

	logger::info("Registered versioned CSXR API listener for currently loaded plugins");
}

void ResetRuntimeStateAfterGameLoad()
{
	globals::features::skylighting.QueueResetSkylighting();
	globals::features::volumetricLighting.RequestRuntimeReset();
	globals::features::upscaling.RequestPostLoadRuntimeReset();
	if (globals::state)
		globals::state->pendingPostLoadRuntimeReset.store(true, std::memory_order_release);
	globals::game::quitGame.store(false, std::memory_order_release);
	globals::OnDataLoaded();
	globals::weatherManager->ClearCache();
	globals::features::lightLimitFix.Reset();
	globals::features::interiorSun.isInteriorWithSun = false;
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
	SKSE::AllocTrampoline(1 << 10);
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

namespace
{
	void CheckIncompatiblePlugins()
	{
		for (const auto& plugin : Compatibility::incompatiblePlugins) {
			const auto probe = Compatibility::ProbePlugin(plugin, L"Data/SKSE/Plugins");
			if (!probe.present && !probe.error)
				continue;
			const auto name = stl::utf16_to_utf8(plugin.dll).value_or("<invalid plugin name>");
			const auto message = probe.error ? std::format("Cannot check incompatible plugin {}: {}", name, probe.error.message()) :
			                                   std::format("Incompatible plugin {} detected{}. Remove it to use CSX.", name,
												   plugin.reason.empty() ? std::string{} : std::format(" ({})", plugin.reason));
			if (std::ranges::find(errors, message) == errors.end()) {
				logger::error("{}", message);
				errors.push_back(message);
			}
		}
	}

	void RegisterRuntimeServices()
	{
		CSX::Api::ScheduleRuntimeMainThreadBinding();
		CSX::Api::InitializeEditorService();
		CSX::Api::InitializeFeatureService();
		CSX::Api::InitializeProfilerService();
		CSX::Api::InitializeWeatherService();
		CSX::Api::RegisterAcceptedDrawService();
		CSX::Api::ProfilerApiDevBenchBridge::Install();
		CSX::Api::EditorDevBenchBridge::Install();
		CSX::Api::FeatureDevBenchBridge::Install();
		CSX::Api::ShaderDevBenchBridge::Install();
		CSX::Api::UpscalingDevBenchBridge::Install();
		CSX::Api::WeatherDevBenchBridge::Install();
		ScreenshotDevBenchBridge::Install();
		ProfilerDevBenchBridge::Install();
		PerformanceTuningDevBenchBridge::Install();
		MenuDevBenchBridge::Install();
		D3DTextureLifetimeDevBenchBridge::Install();
#ifdef DEVBENCH_BRIDGE_ENABLED
		NvPerf::InstallDevBench();
#endif
	}
}

void MessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kPostLoad:
		{
			CheckIncompatiblePlugins();
			if (!errors.empty())
				break;
			RegisterCommunityShadersAPIMessageListener();
			RegisterRuntimeServices();
			CSX::Diagnostics::FrameGenerationDevBenchBridge::Install();
			break;
		}
	case SKSE::MessagingInterface::kPostPostLoad:
		{
			if (errors.empty())
				RegisterRuntimeServices();
			CSX::Diagnostics::FrameGenerationDevBenchBridge::Install();
			if (errors.empty()) {
				Deferred::Hooks::Install();
				Hooks::Install();
				EngineFix::InstallOnPostPostLoadFixes();
				FrameAnnotations::OnPostPostLoad();

				auto shaderCache = globals::shaderCache;

				// Run feature PostPostLoad() first so features can disable themselves if needed
				Feature::ForEachLoadedFeature("PostPostLoad", [](Feature* feature) { feature->PostPostLoad(); });

				// Register scene settings event handler (Interior Only transitions)
				SceneSettingsManager::MenuOpenCloseEventHandler::Register();

				CSX::Api::FreezeShaderCompatibilityRegistrations();
#ifdef DEVBENCH_BRIDGE_ENABLED
				Diagnostics::SunReceiverTrace::Install();
#endif
				// Now validate disk cache after features have had a chance to modify their state
				shaderCache->ValidateDiskCache();

				if (shaderCache->UseFileWatcher())
					shaderCache->StartFileWatcher();
			}

			break;
		}
	case SKSE::MessagingInterface::kDataLoaded:
		{
			CSX::Diagnostics::FrameGenerationDevBenchBridge::Install();
			for (auto it = errors.begin(); it != errors.end(); ++it) {
				auto& errorMessage = *it;
				RE::DebugMessageBox(std::format("Community Shaders\n{}, will disable all hooks and features", errorMessage).c_str());
			}

			if (errors.empty()) {
				globals::OnDataLoaded();
				RegisterRuntimeServices();
#ifdef DEVBENCH_BRIDGE_ENABLED
				CSX::Diagnostics::Stutters::Install();
#endif
				CharacterCategoryAuthoring::DataLoaded();
				EngineFix::InstallOnDataLoadedFixes();
				FrameAnnotations::OnDataLoaded();

				auto shaderCache = globals::shaderCache;
				shaderCache->menuLoaded.store(true, std::memory_order_relaxed);

				while (shaderCache->IsCompiling() && !shaderCache->backgroundCompilation.load(std::memory_order_relaxed) && !globals::game::quitGame) {
					std::this_thread::sleep_for(100ms);
				}

				if (globals::game::quitGame) {
					logger::info("Game was closed, skipping feature DataLoaded methods");
					break;
				}

				shaderCache->WriteDiskCacheInfoWhenReady();

				Feature::ForEachLoadedFeature("DataLoaded", [](Feature* feature) { feature->DataLoaded(); });
				globals::state->startupMenuInitializationComplete.store(true, std::memory_order_release);
			}

			break;
		}
	case SKSE::MessagingInterface::kPreLoadGame:
		{
			globals::state->BeginSaveLoadSafeMode(globals::state->frameCount);
			EditorWindow::ResetWeatherLock(true);
			PerformanceTuningRenderer::CancelActiveMeasurements(
				PerformanceTuningRenderer::CancelMode::ClearSession);
			break;
		}
	case SKSE::MessagingInterface::kSaveGame:
		{
			globals::state->NotifyOrdinarySave(globals::state->frameCount);
			break;
		}
	case SKSE::MessagingInterface::kPostLoadGame:
	case SKSE::MessagingInterface::kNewGame:
		{
			if (message->type == SKSE::MessagingInterface::kNewGame)
				EditorWindow::ResetWeatherLock(false);

			PerformanceTuningRenderer::CancelActiveMeasurements(
				PerformanceTuningRenderer::CancelMode::ClearSession);
			if (errors.empty()) {
				globals::state->ExtendSaveLoadSafeMode(globals::state->frameCount);
				ResetRuntimeStateAfterGameLoad();
				if (message->type == SKSE::MessagingInterface::kPostLoadGame)
					Util::RequestGameLoadTransition();
				logger::info("Handled {}", message->type == SKSE::MessagingInterface::kPostLoadGame ? "kPostLoadGame" : "kNewGame");
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

	auto privateProfileRedirectorVersion = Util::GetDllVersion(L"Data/SKSE/Plugins/PrivateProfileRedirector.dll");
	if (privateProfileRedirectorVersion.has_value() && privateProfileRedirectorVersion.value().compare(REL::Version(0, 6, 2)) == std::strong_ordering::less) {
		stl::report_and_fail("Old version of PrivateProfileRedirector detected, 0.6.2+ required if using it."sv);
	}

	// NVIDIA latches application-profile overrides when the first graphics device is created.
	Util::NvApiDrs::EnsureSkyrimSEDLSSGAllowed();

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging) {
		logger::error("SKSE messaging interface unavailable");
		return false;
	}
	CSX::Api::InitializeServiceRegistryProvider();
	RegisterCommunityShadersAPIMessageListener();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		logger::error("Failed to register SKSE message listener");
		return false;
	}

	globals::OnInit();
	globals::ReInit();

	auto state = globals::state;

	// Initialize i18n system (loads English fallback and discovers available locales)
	I18n::GetSingleton()->Init();

	state->Load(State::ConfigMode::USER, true, State::SettingsApplyMode::StartupHydration);
	state->LoadTheme();  // Load theme settings from SettingsTheme.json

	// Initialize theme system - create default themes and discover existing ones
	globals::menu->CreateDefaultThemes();  // Creates JSON files if they don't exist
	auto themeManager = ThemeManager::GetSingleton();
	themeManager->DiscoverThemes();  // Discover all available themes

	auto log = spdlog::default_logger();
	log->set_level(state->GetLogLevel());

	CheckIncompatiblePlugins();

	auto pushMissingDllError = [&](std::string_view dllName) {
		auto errorMessage = std::format("Required DLL {} was missing", dllName);
		logger::error("{}", errorMessage);
		errors.push_back(errorMessage);
	};

	if (!LoadLibrary(L"Data/SKSE/Plugins/EngineFixes.dll")) {
		pushMissingDllError(stl::utf16_to_utf8(L"Data/SKSE/Plugins/EngineFixes.dll").value_or("<unicode conversion error>"s));
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
