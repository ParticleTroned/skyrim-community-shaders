#pragma once

#include "StabilizerConfig.h"
#include <cstdint>
#include <filesystem>
#include <string>

namespace VRFpsStabilizer
{
	inline constexpr const char* kNotLoadedMessage = "VR FPS Stabilizer is not loaded. Enable it in your mod manager and restart Skyrim VR to use these controls.";
	enum class ConfigFile
	{
		Main,
		Locations
	};
	struct ReloadStatus
	{
		bool loaded = false;
		bool available = false;
		bool pending = false;
		bool restartRequired = false;
		unsigned int build = 0;
		uint64_t revision = 0;
		std::string message;
	};

	/** Acquire the optional external interface from the SKSE PostPostLoad listener. */
	void Initialize();
	/** True only when the companion DLL was detected at PostPostLoad, independently of live reload support. */
	bool IsLoaded();
	/** Register the diagnostic-only configuration adapter when DevBench is built in. */
	void InstallDevBench();
	/** Thread-safe status for the UI and diagnostic adapter. */
	ReloadStatus Status();
	/** Resolve the installed virtual INI path, never the downloaded beta package. */
	std::filesystem::path ConfigPath(ConfigFile file = ConfigFile::Main);
	/** Read a bounded text INI without changing it. */
	bool ReadIni(const std::filesystem::path& path, std::string& contents, std::string& error);
	bool Load(ConfigFile file, IniDocument& document, std::string& error);
	/** Save only if the loaded bytes still match, then request reload on the game thread. */
	bool Save(ConfigFile file, IniDocument& document, std::string& error);
	/** Queue the author's reload call; completion means the void API returned, not visual verification. */
	bool RequestReload(ConfigFile file, std::string& error);
	/** Draw the grouped INI editor for the selected non-profile page. */
	void DrawSettings(const char* group);
	/** Report a pending editor draft so navigation cannot hide edits to another INI view. */
	bool HasUnsavedSettings(ConfigFile file);
	/** Display shared reload availability, pending state and completion. */
	void DrawStatus();
}
