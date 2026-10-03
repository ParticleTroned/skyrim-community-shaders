#include "Features/VR/StabilizerIntegration.h"
#include "Features/VR/StabilizerSettings.h"
#include "VRAPI/VRFpsStabilizerInterface001.h"

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <source_location>
#include <stdexcept>
#include <vector>

namespace
{
	void Require(bool value, const std::source_location where = std::source_location::current())
	{
		if (!value)
			throw std::runtime_error(std::format("Failed at {}:{}", where.file_name(), where.line()));
	}
	struct Provider : VRFpsStabilizerPluginApi::IVRFpsStabilizerInterface001
	{
		unsigned int mainLoads = 0, locationLoads = 0, resets = 0;
		bool fail = false;
		bool failHandshake = false;
		unsigned int getBuildNumber() override
		{
			if (failHandshake)
				throw std::runtime_error("test handshake failure");
			return 1413;
		}
		void loadConfig() override
		{
			++mainLoads;
			if (fail)
				throw std::runtime_error("test provider failure");
		}
		void loadLocationConfig() override { ++locationLoads; }
		void ResetIniSettings() override { ++resets; }
	} provider;
	bool providerPresent = true;
	bool queuePresent = true;
	bool rejectWrites = false;
	std::vector<std::function<void()>> queue;
	std::filesystem::path testRoot;
	unsigned int dispatchCount = 0;
}

namespace REL
{
	struct Module
	{
		static inline bool vr = true;
		static bool IsVR() { return vr; }
	};
}
namespace SKSE
{
	struct Messaging
	{
		bool Dispatch(unsigned int type, void* data, unsigned int size, const char* receiver) const
		{
			++dispatchCount;
			Require(type == 0xF43A9D7C && size == sizeof(VRFpsStabilizerPluginApi::VRFpsStabilizerMessage));
			Require(std::string_view(receiver) == "VRFpsStabilizerPlugin");
			if (providerPresent)
				static_cast<VRFpsStabilizerPluginApi::VRFpsStabilizerMessage*>(data)->GetApiFunction = [](unsigned int revision) -> void* {
					Require(revision == 1);
					return &provider;
				};
			return providerPresent;
		}
	};
	const Messaging* GetMessagingInterface()
	{
		static Messaging messaging;
		return &messaging;
	}
	struct Tasks
	{
		void AddTask(std::function<void()> task) const { queue.push_back(std::move(task)); }
	};
	const Tasks* GetTaskInterface()
	{
		static Tasks tasks;
		return queuePresent ? &tasks : nullptr;
	}
}
#include "VRAPI/VRFpsStabilizerInterface001.cpp"

namespace globals::features
{
	struct Upscaling
	{
		struct Profile
		{
			bool hasRenderScaleMode = true;
			bool renderScaleMode = false;
		};
		struct Config
		{
			uint64_t revision = 0;
			Profile interior, exterior;
		} config;
		std::atomic_uint32_t pendingVRFpsStabilizerSyncFrame{ 1 };
		bool IsVRFpsStabilizerSyncActive() const { return true; }
		int GetConfiguredUpscaleMethodForTransition() const { return 1; }
		uint32_t GetEffectiveUpscalingQualityMode() const { return 2; }
		Config GetVRFpsStabilizerSessionConfig() const { return config; }
		unsigned int refreshes = 0;
		bool RefreshVRFpsStabilizerSessionConfig(std::string&)
		{
			++refreshes;
			return true;
		}
	} upscaling;
}
using Upscaling = globals::features::Upscaling;
namespace Util
{
	bool IsInterior() { return false; }
}
Upscaling::Profile ResolveVRFpsStabilizerTransitionTarget(const Upscaling&, Upscaling::Profile profile)
{
	return profile;
}
#include "stabilizer_intent_under_test.h"
namespace Util::PathHelpers
{
	std::filesystem::path GetDataPath() { return testRoot; }
}
namespace Util::FileHelpers
{
	bool WriteTextFileAtomic(const std::filesystem::path& path, std::string_view contents, std::string& error, bool fallback)
	{
		Require(!fallback);
		if (rejectWrites) {
			error = "test write failure";
			return false;
		}
		std::ofstream output(path, std::ios::binary);
		output << contents;
		return static_cast<bool>(output);
	}
}
namespace logger
{
	template <class... Args>
	void info(const char*, Args&&...)
	{}
}
#include "stabilizer_integration_under_test.h"

int main()
{
	using namespace VRFpsStabilizer;
	testRoot = std::filesystem::current_path() / "stabilizer-integration-fixture";
	try {
		auto& upscaling = globals::features::upscaling;
		Require(!HasPendingVRFpsStabilizerRenderScaleIntent(upscaling));
		upscaling.config.exterior.renderScaleMode = true;
		++upscaling.config.revision;
		Require(HasPendingVRFpsStabilizerRenderScaleIntent(upscaling));
		upscaling.config.exterior.renderScaleMode = false;
		++upscaling.config.revision;
		Require(!HasPendingVRFpsStabilizerRenderScaleIntent(upscaling));
		Require(!std::filesystem::exists(testRoot));
		std::filesystem::create_directories(testRoot / "SKSE" / "Plugins");
		const auto main = ConfigPath();
		const auto locations = ConfigPath(ConfigFile::Locations);
		std::ofstream(main) << "[Settings]\nEnableLog=0\n";
		std::ofstream(locations) << "[Settings]\nEnabled=1\n";
		REL::Module::vr = false;
		Initialize();
		Require(!Status().available && dispatchCount == 0);
		REL::Module::vr = true;
		Initialize();
		Require(Status().available && Status().build == 1413 && dispatchCount == 1);
		IniDocument document;
		std::string error;
		Require(Load(ConfigFile::Main, document, error));
		Require(document.Set("Settings", "EnableLog", "1", error));
		Require(Save(ConfigFile::Main, document, error));
		Require(Status().pending && provider.mainLoads == 0 && queue.size() == 1);
		Require(!RequestReload(ConfigFile::Locations, error));
		queue.front()();
		queue.clear();
		Require(!Status().pending && !Status().restartRequired && provider.mainLoads == 1);
		Require(globals::features::upscaling.refreshes == 1);
		Require(RequestReload(ConfigFile::Locations, error));
		queue.front()();
		queue.clear();
		Require(provider.locationLoads == 1 && globals::features::upscaling.refreshes == 1 && provider.resets == 0);
		std::ofstream(main) << "[Settings]\nEnableLog=0\n# external\n";
		Require(!Save(ConfigFile::Main, document, error));
		Require(error.find("changed since") != error.npos && queue.empty());
		Require(Load(ConfigFile::Main, document, error));
		Require(document.Set("Settings", "EnableLog", "1", error));
		rejectWrites = true;
		Require(!Save(ConfigFile::Main, document, error) && queue.empty());
		rejectWrites = false;
		provider.fail = true;
		Require(Save(ConfigFile::Main, document, error));
		queue.front()();
		queue.clear();
		Require(Status().restartRequired && !Status().pending);
		Require(RequestReload(ConfigFile::Locations, error));
		queue.front()();
		queue.clear();
		Require(Status().restartRequired);
		provider.fail = false;
		Require(RequestReload(ConfigFile::Main, error));
		queue.front()();
		queue.clear();
		Require(!Status().restartRequired);
		const auto loadsBefore = provider.mainLoads;
		Require(RequestReload(ConfigFile::Main, error));
		std::filesystem::remove(main);
		queue.front()();
		queue.clear();
		Require(Status().restartRequired && !Status().pending && provider.mainLoads == loadsBefore);
		std::ofstream(main, std::ios::binary) << document.original;
		Require(RequestReload(ConfigFile::Main, error));
		queue.front()();
		queue.clear();
		Require(!Status().restartRequired);
		queuePresent = false;
		Require(!RequestReload(ConfigFile::Main, error));
		Require(!Status().pending);
		provider.failHandshake = true;
		Initialize();
		Require(!Status().available && Status().build == 0 && Status().message.find("handshake failure") != std::string::npos);
		provider.failHandshake = false;
		providerPresent = false;
		Initialize();
		Require(!Status().available);
		Require(document.Set("Settings", "EnableLog", "0", error));
		Require(Save(ConfigFile::Main, document, error));
		Require(Status().restartRequired && !Status().pending && queue.empty());
		std::filesystem::remove_all(testRoot);
		std::cout << "Stabilizer interface, save, queue and reload integration checks passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << "\nFixture retained: " << testRoot << '\n';
		return 1;
	}
}
