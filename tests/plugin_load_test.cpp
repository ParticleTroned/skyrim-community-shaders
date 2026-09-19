#include <cstddef>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	struct Fixture
	{
		bool interfacePresent = true;
		bool poolSucceeds = true;
		bool loadResult = true;
		bool initializing = false;
		bool initialized = false;
		int loggerInitializations = 0;
		int loggerReplacements = 0;
		int initCalls = 0;
		int loadCalls = 0;
		int poolAttempts = 0;
		int poolAllocations = 0;
		int commonlibAllocations = 0;
		int pluginAllocations = 0;
		std::size_t requestedCapacity = 0;
		std::size_t capacity = 0;
		std::vector<std::string> log;
	} fixture;

	void Check(bool condition, std::string_view message)
	{
		if (!condition)
			throw std::runtime_error(std::string(message));
	}
}

namespace Plugin
{
	constexpr std::string_view NAME = "CommunityShaders";
	constexpr std::string_view BUILD_LABEL = "startup-test";
}

namespace REX::W32
{
	bool IsDebuggerPresent() { return true; }
}

void InitializeLog()
{
	++fixture.loggerInitializations;
	fixture.log.clear();
}

namespace logger
{
	template <class... Args>
	void info(std::format_string<Args...> format, Args&&... args)
	{
		fixture.log.push_back(std::format(format, std::forward<Args>(args)...));
	}
}

namespace BuildProvenance
{
	void LogRuntimeIdentity() { fixture.log.emplace_back("Build ID: startup-test-identity"); }
}

namespace SKSE
{
	struct LoadInterface
	{};
	struct InitInfo
	{
		bool log = true;
		bool trampoline = false;
		std::size_t trampolineSize = 0;
	};
	struct TrampolineInterface
	{
		void* AllocateFromBranchPool(std::size_t capacity) const
		{
			++fixture.poolAttempts;
			fixture.requestedCapacity = capacity;
			return fixture.poolSucceeds ? &fixture : nullptr;
		}
	};
	struct Trampoline
	{
		void set_trampoline(void* memory, std::size_t capacity)
		{
			Check(memory == &fixture, "The SKSE pool allocation must be retained");
			++fixture.poolAllocations;
			fixture.capacity = capacity;
		}
		void create(std::size_t capacity)
		{
			++(fixture.initializing ? fixture.commonlibAllocations : fixture.pluginAllocations);
			fixture.capacity = capacity;
		}
	};

	LoadInterface loadInterface;
	const TrampolineInterface* GetTrampolineInterface()
	{
		static const TrampolineInterface interface;
		return fixture.interfacePresent ? &interface : nullptr;
	}
	Trampoline& GetTrampoline()
	{
		static Trampoline trampoline;
		return trampoline;
	}

	// CommonLib initializes logging by default and allocates only when its
	// trampoline interface exists; CSX must supply the missing-interface path.
	void Init(const LoadInterface* interface, InitInfo info = {})
	{
		Check(interface == &loadInterface, "The SKSE load interface must be forwarded");
		++fixture.initCalls;
		if (info.log) {
			++fixture.loggerReplacements;
			fixture.log.clear();
		}
		fixture.initializing = true;
		if (info.trampoline) {
			if (const auto trampolineInterface = GetTrampolineInterface()) {
				if (auto memory = trampolineInterface->AllocateFromBranchPool(info.trampolineSize))
					GetTrampoline().set_trampoline(memory, info.trampolineSize);
				else
					GetTrampoline().create(info.trampolineSize);
			}
		}
		fixture.initializing = false;
		fixture.initialized = true;
	}
}

bool Load()
{
	Check(fixture.initialized, "SKSE must be initialized before plugin loading");
	Check(fixture.capacity != 0, "The trampoline must exist before plugin loading");
	++fixture.loadCalls;
	return fixture.loadResult;
}

#define SKSE_PLUGIN_LOAD(...) bool SKSEPlugin_Load(__VA_ARGS__)
#include "plugin_load_under_test.h"

void TestLoad(bool interfacePresent, bool poolSucceeds, bool loadResult)
{
	fixture = {};
	fixture.interfacePresent = interfacePresent;
	fixture.poolSucceeds = poolSucceeds;
	fixture.loadResult = loadResult;
	Check(SKSEPlugin_Load(&SKSE::loadInterface) == loadResult, "Plugin load failure must propagate");
	Check(fixture.initCalls == 1 && fixture.loadCalls == 1, "Startup must initialize and load once");
	Check(fixture.loggerInitializations == 1 && fixture.loggerReplacements == 0, "CommonLib must preserve CSX's logger");
	const std::vector<std::string> expectedLog{
		"Loaded CommunityShaders startup-test",
		"Build ID: startup-test-identity"
	};
	Check(fixture.log == expectedLog, "Startup version and Build ID must survive initialization");
	Check(fixture.capacity == kTrampolineCapacity, "The complete hook capacity must be allocated");
	Check(fixture.poolAttempts == (interfacePresent ? 1 : 0), "Only an available SKSE pool may be queried");
	if (interfacePresent)
		Check(fixture.requestedCapacity == kTrampolineCapacity, "SKSE must receive the requested hook capacity");
	Check(fixture.poolAllocations == (interfacePresent && poolSucceeds ? 1 : 0), "Successful SKSE allocations must be reused");
	Check(fixture.commonlibAllocations == (interfacePresent && !poolSucceeds ? 1 : 0), "CommonLib must own the exhausted-pool fallback");
	Check(fixture.pluginAllocations == (!interfacePresent ? 1 : 0), "CSX must allocate only when the SKSE interface is absent");
}

int main()
{
	try {
		for (const bool loadResult : { true, false }) {
			TestLoad(true, true, loadResult);
			TestLoad(true, false, loadResult);
			TestLoad(false, false, loadResult);
		}
		std::cout << "Plugin startup: 6 cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
