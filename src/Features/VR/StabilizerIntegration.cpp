#include "StabilizerIntegration.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "StabilizerSettings.h"
#include "Utils/FileSystem.h"
#include "VRAPI/VRFpsStabilizerInterface001.h"

#include <array>
#include <atomic>
#include <fstream>
#include <mutex>

namespace VRFpsStabilizer
{
	namespace
	{
		std::mutex stateMutex;
		std::atomic_bool pluginLoaded{ false };
		ReloadStatus status;
		std::array<bool, 2> needsReload{};
		VRFpsStabilizerPluginApi::IVRFpsStabilizerInterface001* stabilizerApi = nullptr;

		void CompleteReload(ConfigFile file, std::string failure)
		{
			std::scoped_lock lock(stateMutex);
			status.pending = false;
			needsReload[static_cast<size_t>(file)] = !failure.empty();
			status.restartRequired = needsReload[0] || needsReload[1];
			status.message = failure.empty() ? "Reload completed. Saved rules will run when their events or conditions apply." : std::move(failure);
			if (status.restartRequired)
				status.message += " Settings still need a successful reload or a restart.";
			++status.revision;
			logger::info("VR FPS Stabilizer: {}", status.message);
		}

		bool QueueReload(ConfigFile file, std::string& error)
		{
			if (!IsLoaded()) {
				error = kNotLoadedMessage;
				return false;
			}
			if (status.pending) {
				error = "A Stabilizer reload is already pending. Wait for it to finish.";
				return false;
			}
			if (!stabilizerApi) {
				error = "Live reload requires a VR FPS Stabilizer version with the revision 1 interface.";
				return false;
			}
			const auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				error = "The game task queue is unavailable.";
				return false;
			}
			status.pending = true;
			status.message = "Reload pending on the game thread...";
			try {
				tasks->AddTask([file] {
					std::string failure;
					try {
						std::string contents;
						if (!ReadIni(ConfigPath(file), contents, failure)) {
							failure = "Reload refused: " + failure;
						} else if (file == ConfigFile::Main) {
							stabilizerApi->loadConfig();
							if (!globals::features::upscaling.RefreshVRFpsStabilizerSessionConfig(failure))
								failure = "Stabilizer reloaded, but CSX could not refresh its profiles: " + failure;
						} else {
							stabilizerApi->loadLocationConfig();
						}
					} catch (const std::exception& e) {
						failure = std::string("Stabilizer reload failed: ") + e.what();
					} catch (...) {
						failure = "Stabilizer reload failed with an unknown exception.";
					}
					CompleteReload(file, std::move(failure));
				});
			} catch (const std::exception& e) {
				status.pending = false;
				error = std::string("Could not queue Stabilizer reload: ") + e.what();
				return false;
			} catch (...) {
				status.pending = false;
				error = "Could not queue Stabilizer reload: unknown task queue failure.";
				return false;
			}
			return true;
		}
	}

	void Initialize()
	{
		const bool moduleLoaded = REL::Module::IsVR() && GetModuleHandleW(L"VRFpsStabilizer.dll") != nullptr;
		VRFpsStabilizerPluginApi::IVRFpsStabilizerInterface001* api = nullptr;
		unsigned int build = 0;
		std::string failure;
		try {
			api = VRFpsStabilizerPluginApi::GetInterface();
			build = api ? api->getBuildNumber() : 0;
		} catch (const std::exception& e) {
			api = nullptr;
			failure = std::string("Stabilizer interface failed: ") + e.what();
		} catch (...) {
			api = nullptr;
			failure = "Stabilizer interface failed with an unknown exception.";
		}
		std::scoped_lock lock(stateMutex);
		pluginLoaded.store(moduleLoaded || api != nullptr, std::memory_order_release);
		stabilizerApi = api;
		status.available = stabilizerApi != nullptr;
		status.build = build;
		status.message = std::move(failure);
		if (!status.message.empty()) {
			logger::info("VR FPS Stabilizer: {}", status.message);
		}
		logger::info("VR FPS Stabilizer loaded: {}; revision 1 interface: {} (build {})", IsLoaded(), status.available ? "available" : "unavailable", status.build);
	}

	bool IsLoaded()
	{
		return pluginLoaded.load(std::memory_order_acquire);
	}

	ReloadStatus Status()
	{
		std::scoped_lock lock(stateMutex);
		auto snapshot = status;
		snapshot.loaded = IsLoaded();
		return snapshot;
	}

	std::filesystem::path ConfigPath(ConfigFile file)
	{
		const auto filename = file == ConfigFile::Main ? "VRFpsStabilizer.ini" : "VRFpsStabilizerLocation.ini";
		const auto defaultPath = Util::PathHelpers::GetDataPath() / "SKSE" / "Plugins" / filename;
		std::error_code ec;
		if (std::filesystem::exists(defaultPath, ec) && !ec)
			return defaultPath;
		const auto current = std::filesystem::current_path(ec);
		if (!ec) {
			const auto alternate = current / "SKSE" / "Plugins" / filename;
			if (std::filesystem::exists(alternate, ec) && !ec)
				return alternate;
		}
		return defaultPath;
	}

	bool ReadIni(const std::filesystem::path& path, std::string& contents, std::string& error)
	{
		contents.clear();
		error.clear();
		std::error_code ec;
		const auto size = std::filesystem::file_size(path, ec);
		if (ec || size > kMaxIniBytes) {
			error = std::format("Cannot read {}: {}", path.string(), ec ? ec.message() : "file exceeds 4 MiB");
			return false;
		}
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			error = std::format("Cannot open {}.", path.string());
			return false;
		}
		std::array<char, 4096> buffer{};
		while (input) {
			input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			contents.append(buffer.data(), static_cast<size_t>(input.gcount()));
			if (contents.size() > kMaxIniBytes) {
				error = "INI grew beyond the 4 MiB limit.";
				return false;
			}
		}
		if (input.bad()) {
			error = std::format("Reading {} failed.", path.string());
			return false;
		}
		return IniDocument::ValidateText(contents, error);
	}

	bool Load(ConfigFile file, IniDocument& document, std::string& error)
	{
		std::string contents;
		if (!ReadIni(ConfigPath(file), contents, error))
			return false;
		document = { contents, contents };
		return true;
	}

	bool Save(ConfigFile file, IniDocument& document, std::string& error)
	{
		error.clear();
		if (!REL::Module::IsVR()) {
			error = "VR FPS Stabilizer settings require Skyrim VR.";
			return false;
		}
		if (!IsLoaded()) {
			error = kNotLoadedMessage;
			return false;
		}
		if (!IniDocument::ValidateText(document.text, error) ||
			(file == ConfigFile::Main && !ValidateSettings(document, error)))
			return false;
		std::scoped_lock lock(stateMutex);
		if (status.pending) {
			error = "Wait for the pending Stabilizer reload before saving again.";
			return false;
		}
		const auto path = ConfigPath(file);
		std::string current;
		if (!ReadIni(path, current, error))
			return false;
		if (current != document.original) {
			error = "The INI changed since it was opened. Reload it before saving to preserve external edits.";
			return false;
		}
		if (!Util::FileHelpers::WriteTextFileAtomic(path, document.text, error, false))
			return false;
		document.original = document.text;
		++status.revision;
		needsReload[static_cast<size_t>(file)] = true;
		status.restartRequired = true;
		std::string reloadError;
		if (!QueueReload(file, reloadError))
			status.message = "Saved to INI. " + reloadError + " Restart Skyrim VR to apply.";
		return true;
	}

	bool RequestReload(ConfigFile file, std::string& error)
	{
		error.clear();
		std::scoped_lock lock(stateMutex);
		return QueueReload(file, error);
	}
}
