#include "Deferred.h"
#include "Features/Upscaling.h"
#include "FrameAnnotations.h"
#include "Globals.h"
#include "Hooks.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "SceneSettingsManager.h"
#include "ShaderCache.h"
#include "State.h"

#include "ENB/ENBSeriesAPI.h"

#define DLLEXPORT __declspec(dllexport)

std::list<std::string> errors;

bool Load();

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
	logger::info("Loaded {} {}", Plugin::NAME, Plugin::VERSION.string());
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

void MessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kPostPostLoad:
		{
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

				// Now validate disk cache after features have had a chance to modify their state
				shaderCache->ValidateDiskCache();

				// Persist cache info immediately — plugin/feature versions are known now and do
				// not depend on shader compilation. This makes the disk cache incrementally
				// reusable: shaders saved this session are validated on the next launch even if
				// the (potentially multi-thousand-shader) compile is interrupted or the game is
				// closed before it finishes. Previously this only ran after a full compile.
				if (shaderCache->IsDiskCache())
					shaderCache->WriteDiskCacheInfo();

				if (shaderCache->UseFileWatcher())
					shaderCache->StartFileWatcher();
			}

			break;
		}
	case SKSE::MessagingInterface::kDataLoaded:
		{
			for (auto it = errors.begin(); it != errors.end(); ++it) {
				auto& errorMessage = *it;
				RE::DebugMessageBox(std::format("Community Shaders\n{}, will disable all hooks and features", errorMessage).c_str());
			}

			if (errors.empty()) {
				globals::OnDataLoaded();
				EngineFix::InstallOnDataLoadedFixes();
				FrameAnnotations::OnDataLoaded();

				auto shaderCache = globals::shaderCache;
				shaderCache->menuLoaded = true;

				while (shaderCache->IsCompiling() && !shaderCache->backgroundCompilation && !globals::game::quitGame) {
					std::this_thread::sleep_for(100ms);
				}

				if (globals::game::quitGame) {
					logger::info("Game was closed, skipping feature DataLoaded methods");
					break;
				}

				if (shaderCache->IsDiskCache()) {
					shaderCache->WriteDiskCacheInfo();
				}

				Feature::ForEachLoadedFeature("DataLoaded", [](Feature* feature) { feature->DataLoaded(); });

				// TEMP autonomous-test harness (inert unless CS_AUTOLOAD_SAVE is set). REMOVE before shipping.
				char csAutoLoad[256]{};
				if (GetEnvironmentVariableA("CS_AUTOLOAD_SAVE", csAutoLoad, sizeof(csAutoLoad)) > 0) {
					std::string saveName = csAutoLoad;
					std::thread([saveName] {
						std::this_thread::sleep_for(12s);
						if (auto* task = SKSE::GetTaskInterface()) {
							task->AddTask([saveName] {
								auto* slm = RE::BGSSaveLoadManager::GetSingleton();
								if (!slm)
									return;
								slm->PopulateSaveList();
								logger::info("[AutoLoad] Load('{}')", saveName);
								slm->Load(saveName.c_str());
							});
						}
					}).detach();

					// TEMP: CS_FG_SEQ drives a scripted FG toggle sequence to exercise the six transitions
					// (off->FSR, FSR->off, off->FSR, FSR->DLSSG, DLSSG->FSR, FSR->off) — Skyrim's DirectInput
					// menu can't be driven by the harness, so this stands in for a user clicking the FG
					// toggles. CRITICAL: each toggle must land in STABLE ACTIVE GAMEPLAY (not paused / menu /
					// loading), so the swapchain-owner recreate runs on a live frame — that is the real
					// in-gameplay path. The gameplay check runs on the game thread (AddTask) so it reads UI
					// state safely; the worker thread only sleeps and dispatches. Toggles are spaced 12s apart
					// so each recreate fully settles before the next. REMOVE before shipping.
					char seq[8]{};
					if (GetEnvironmentVariableA("CS_FG_SEQ", seq, sizeof(seq)) > 0) {
						std::thread([] {
							using FG = Upscaling::FrameGenMethod;
							static std::atomic<bool> s_inGameplay{ false };
							auto pollGameplay = [] {
								if (auto* task = SKSE::GetTaskInterface())
									task->AddTask([] {
										auto* ui = globals::game::ui;
										s_inGameplay.store(ui && !ui->GameIsPaused() &&
											!globals::state->IsMainOrLoadingMenuOpen(ui));
									});
							};
							// Wait for ~3s of continuous stable gameplay before the first toggle.
							int stable = 0;
							for (int i = 0; i < 900 && stable < 3; ++i) {
								std::this_thread::sleep_for(1s);
								pollGameplay();
								std::this_thread::sleep_for(50ms);  // let the AddTask run
								stable = s_inGameplay.load() ? stable + 1 : 0;
							}
							logger::info("[FGSeq] stable gameplay reached (stable={}) — starting toggles", stable);

							struct Step
							{
								bool fg;
								uint32_t method;
								const char* name;
							};
							static const Step steps[] = {
								{ true, (uint32_t)FG::kFSR, "off->FSR" },
								{ false, (uint32_t)FG::kFSR, "FSR->off" },
								{ true, (uint32_t)FG::kFSR, "off->FSR(2)" },
								{ true, (uint32_t)FG::kDLSSG, "FSR->DLSSG" },
								{ true, (uint32_t)FG::kFSR, "DLSSG->FSR" },
								{ false, (uint32_t)FG::kFSR, "FSR->off(2)" },
							};
							for (const auto& s : steps) {
								const Step st = s;
								if (auto* task = SKSE::GetTaskInterface()) {
									task->AddTask([st] {
										auto& u = globals::features::upscaling;
										u.settings.frameGenMethod = st.method;
										u.settings.frameGeneration = st.fg;
										logger::info("[FGSeq] {} -> frameGeneration={} method={}", st.name, st.fg, st.method);
									});
								}
								std::this_thread::sleep_for(12s);  // let the recreate settle before the next toggle
							}
							logger::info("[FGSeq] sequence complete");
						}).detach();
					}
				}
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

	auto messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener("SKSE", MessageHandler);

	globals::OnInit();
	globals::ReInit();

	auto state = globals::state;

	// Initialize i18n system (loads English fallback and discovers available locales)
	I18n::GetSingleton()->Init();

	state->Load();
	state->LoadTheme();  // Load theme settings from SettingsTheme.json

	// Initialize theme system - create default themes and discover existing ones
	globals::menu->CreateDefaultThemes();  // Creates JSON files if they don't exist
	auto themeManager = ThemeManager::GetSingleton();
	themeManager->DiscoverThemes();  // Discover all available themes

	auto log = spdlog::default_logger();
	log->set_level(state->GetLogLevel());

	const std::array incompatibleDLLs = {
		L"Data/SKSE/Plugins/ShaderTools.dll",
		L"Data/SKSE/Plugins/SSEShaderTools.dll",
		L"Data/SKSE/Plugins/SkyrimUpscaler.dll",
		L"Data/SKSE/Plugins/EVLaS.dll",
		L"Data/SKSE/Plugins/AELAS.dll",
		L"Data/SKSE/Plugins/SSEReShadeHelper.dll",
		L"Data/SKSE/Plugins/TAASharpen.dll",
		L"Data/SKSE/Plugins/NVIDIA_Reflex.dll",
		L"Data/SKSE/Plugins/MARA.dll"
	};

	for (const auto dll : incompatibleDLLs) {
		if (LoadLibrary(dll)) {
			auto errorMessage = std::format("Incompatible DLL {} detected", stl::utf16_to_utf8(dll).value_or("<unicode conversion error>"s));
			logger::error("{}", errorMessage);
			errors.push_back(errorMessage);
		}
	}

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