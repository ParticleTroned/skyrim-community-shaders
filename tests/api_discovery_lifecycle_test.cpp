#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Model SKSE's append-order delivery and idempotent wildcard registration;
// all CSX registration operations come from XSEPlugin.cpp.
struct Consumer
{
	bool reachable = false;
	bool discovered = false;
};
std::vector<Consumer*> loaded;
std::vector<std::function<void()>> lifecycle;
bool failApiRegistration = false;
bool failLifecycleRegistration = false;
bool messagingAvailable = true;
bool registryReady = false;
bool startupError = false;
struct StartupFailure : std::runtime_error
{
	using std::runtime_error::runtime_error;
};
void RefreshDuringPostLoad();
void MessageHandler() { RefreshDuringPostLoad(); }
void CommunityShadersAPIMessageHandler() {}
[[noreturn]] void PushStartupError(const char* message)
{
	startupError = true;
	throw StartupFailure(message);
}
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
			if (sender) {
				if (failLifecycleRegistration)
					return false;
				lifecycle.push_back(handler);
			} else {
				if (failApiRegistration)
					return false;
				for (auto* consumer : loaded) consumer->reachable = true;
			}
			return true;
		}
	} messaging;
	MessagingInterface* GetMessagingInterface() { return messagingAvailable ? &messaging : nullptr; }
}
#include "api_discovery_under_test.h"

static void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

static void Reset()
{
	loaded.clear();
	lifecycle.clear();
	failApiRegistration = failLifecycleRegistration = false;
	messagingAvailable = true;
	registryReady = startupError = false;
}

template <class Action>
static void ExpectStartupFailure(Action action, std::string_view expected)
{
	bool reported = false;
	try {
		action();
	} catch (const StartupFailure& error) {
		reported = std::string_view(error.what()).find(expected) != std::string_view::npos;
	}
	Require(reported && startupError, "API registration failure must stop startup immediately with its cause");
}

static void CheckDiscovery()
{
	for (bool hasEarly : { false, true }) {
		Reset();
		Consumer early, late;
		auto add = [](Consumer& consumer) {
			loaded.push_back(&consumer);
			lifecycle.push_back([&consumer] { consumer.discovered = consumer.reachable && registryReady; });
		};
		if (hasEarly)
			add(early);
		Require(RegisterDuringLoad(), "Load must register API and lifecycle listeners");
		Require(registryReady && (!hasEarly || early.reachable), "early consumers must be reachable before lifecycle delivery");
		add(late);
		Require(!late.reachable, "late consumers must wait for the wildcard refresh");
		for (const auto& callback : lifecycle) callback();
		Require((!hasEarly || early.discovered) && late.discovered && !startupError,
			"API discovery must finish before the PostPostLoad shader-registration freeze");
	}
}

static void CheckFailures()
{
	Reset();
	failApiRegistration = true;
	ExpectStartupFailure([] { RegisterDuringLoad(); }, "Failed to register CSX API message listener");
	Require(lifecycle.empty(), "failed API registration must not install lifecycle callbacks");

	Reset();
	failLifecycleRegistration = true;
	Require(!RegisterDuringLoad() && lifecycle.empty(), "SKSE lifecycle registration failure must reject Load");

	Reset();
	messagingAvailable = false;
	Require(!RegisterDuringLoad() && !registryReady && lifecycle.empty(), "Load must reject a missing interface before initializing the registry");
	ExpectStartupFailure([] { RegisterCommunityShadersAPIMessageListener(); }, "SKSE messaging interface unavailable");

	for (bool missingInterface : { false, true }) {
		Reset();
		Require(RegisterDuringLoad(), "PostLoad failure fixture must initialize successfully");
		Consumer late;
		loaded.push_back(&late);
		messagingAvailable = !missingInterface;
		failApiRegistration = !missingInterface;
		ExpectStartupFailure([] { lifecycle.front()(); }, missingInterface ?
															  "SKSE messaging interface unavailable" :
															  "Failed to register CSX API message listener");
		Require(!late.reachable && !late.discovered, "failed PostLoad refresh must not expose late consumers");
	}
}

int main()
{
	try {
		CheckDiscovery();
		CheckFailures();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
