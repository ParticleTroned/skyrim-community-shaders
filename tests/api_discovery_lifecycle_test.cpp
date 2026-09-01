#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Model SKSE's documented append-order delivery and idempotent wildcard
// registration; all CSX registration operations come from XSEPlugin.cpp.
struct Consumer
{
	bool reachable = false;
	bool discovered = false;
};
std::vector<Consumer*> loaded;
std::vector<std::function<void()>> lifecycle;
bool failRegistration = false;
bool registryReady = false;
bool startupError = false;
void RefreshDuringPostLoad();
void MessageHandler() { RefreshDuringPostLoad(); }
void CommunityShadersAPIMessageHandler() {}
void PushStartupError(const char*) { startupError = true; }
namespace logger
{
	void info(const char*) {}
	void error(const char*) {}
}
namespace CSX::Api
{
	void InitializeServiceRegistryProvider() { registryReady = true; }
}
namespace SKSE
{
	struct MessagingInterface
	{
		bool RegisterListener(const char* sender, void (*handler)())
		{
			if (failRegistration)
				return false;
			if (sender)
				lifecycle.push_back(handler);
			else
				for (auto* consumer : loaded) consumer->reachable = true;
			return true;
		}
	} messaging;
	MessagingInterface* GetMessagingInterface() { return &messaging; }
}
#include "api_discovery_under_test.h"

int main()
{
	for (bool hasEarly : { false, true }) {
		loaded.clear();
		lifecycle.clear();
		registryReady = false;
		startupError = false;
		Consumer early, late;
		auto add = [](Consumer& consumer) {
			loaded.push_back(&consumer);
			lifecycle.push_back([&consumer] { consumer.discovered = consumer.reachable && registryReady; });
		};
		if (hasEarly)
			add(early);
		if (!RegisterDuringLoad())
			return 1;
		add(late);
		for (const auto& callback : lifecycle) callback();
		if ((hasEarly && !early.discovered) || !late.discovered || startupError) {
			std::cerr << "API discovery failed before the PostPostLoad shader-registration freeze\n";
			return 1;
		}
	}
	failRegistration = true;
	startupError = false;
	if (RegisterDuringLoad() || !startupError) {
		std::cerr << "Registration failure was hidden during Load\n";
		return 1;
	}
	return 0;
}
