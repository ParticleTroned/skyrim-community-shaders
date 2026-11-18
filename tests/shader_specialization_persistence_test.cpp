#include "Utils/ShaderSpecializationRequests.h"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>

namespace globals::game
{
	bool isVR = true;
}
namespace logger
{
	bool fail = false;
	template <class... T>
	void info(T&&...)
	{}
	template <class... T>
	void warn(T&&...)
	{
		if (fail)
			throw std::runtime_error("injected logger failure");
	}
}
void OutputDebugStringA(const char*) {}
namespace Util::PathHelpers
{
	std::filesystem::path GetCommunityShaderPath() { return {}; }
}
namespace Util::FileHelpers
{
	bool fail = false, throwUnknown = false;
	int writes = 0;
	std::string persisted;
	std::function<void()> duringWrite;
	bool WriteTextFileAtomic(const std::filesystem::path&, std::string_view text, std::string& error, bool allowDirectFallback)
	{
		assert(!allowDirectFallback);
		++writes;
		if (throwUnknown)
			throw 1;
		if (fail) {
			error = "injected disk failure";
			return false;
		}
		if (duringWrite)
			duringWrite();
		persisted = text;
		return true;
	}
}

namespace
{
#include "discovery_history_under_test.h"
}

int main()
{
	using namespace Util::FileHelpers;
	DiscoveryHistory history;
	history.Flush();
	assert(writes == 0);
	history.runtime = "VR";
	const auto add = [&](uint32_t descriptor) {
		std::lock_guard lock(history.mutex);
		if (history.requests.Add({ Util::SpecializationFamily::Lighting, descriptor }))
			++history.revision;
	};
	add(1);
	add(2);
	history.Flush();
	assert(writes == 1 && history.savedRevision == 2 && !history.persistenceFailed);
	add(2);
	history.Flush();
	assert(writes == 1);
	add(3);
	duringWrite = [&] { add(4); };
	history.Flush();
	assert(history.savedRevision == 3 && history.revision == 4);
	assert(nlohmann::json::parse(persisted)["requests"].size() == 3);
	duringWrite = {};
	history.Flush();
	assert(history.savedRevision == 4);
	assert(nlohmann::json::parse(persisted)["requests"].size() == 4);
	const auto previous = persisted;
	add(5);
	fail = logger::fail = true;
	history.Flush();
	assert(history.persistenceFailed && history.savedRevision == 4 && persisted == previous);
	fail = logger::fail = false;
	throwUnknown = true;
	history.Flush();
	assert(history.persistenceFailed && history.savedRevision == 4 && persisted == previous);
	throwUnknown = false;
	history.Flush();
	assert(!history.persistenceFailed && history.savedRevision == 5);
	assert(nlohmann::json::parse(persisted)["requests"].size() == 5);
	add(6);
	history.limitWarningPending = true;
	logger::fail = true;
	history.Flush();
	logger::fail = false;
	assert(!history.persistenceFailed && history.savedRevision == 6);
	std::cout << "Discovery persistence: coalescing, concurrent discoveries, atomic-only writes, failure recovery and final dirty flush passed\n";
}
