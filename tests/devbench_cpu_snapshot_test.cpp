#include "Api/AcceptedDrawRegistry.h"
#include "Diagnostics/DevBenchCpuSnapshot.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace CSX::FeatureAPI;
namespace
{
	std::string scratch;
	bool failSettings = false;
	bool invalidJson = false;
	bool failDescriptor = false;
	bool failState = false;
	bool failConstraint = false;
	int settingsCalls = 0;
	Interface001 MakeAPI()
	{
		Interface001 api;
		api.GetSnapshot = [](const void*, Snapshot001* output) {
			output->available = 1;
			output->stateRevision = 42;
			output->featureCount = 2;
			return failState ? Status::kWrongThread : Status::kSuccess;
		};
		api.GetFeatureDescriptor = [](const void*, std::uint32_t index, FeatureDescriptor001* output) {
			scratch = index == 0 ? "Upscaling" : "Disabled";
			output->shortName = scratch.c_str();
			output->loaded = index == 0;
			output->disabledAtBoot = index == 1;
			output->hasFeatureSettings = 1;
			return failDescriptor ? Status::kUnavailable : Status::kSuccess;
		};
		api.GetFeatureSettings = [](const void*, const char* name, SettingsSnapshot001* output) {
			if (std::string(name) != "Upscaling")
				throw std::runtime_error("wrong settings feature");
			++settingsCalls;
			scratch = "API scratch invalidated";
			output->settingsJson = invalidJson ? "invalid" : R"({"Upscaling":{"Enabled":false,"Sharpness":0}})";
			return failSettings ? Status::kUnavailable : Status::kSuccess;
		};
		api.GetConstraintCount = [](const void*) -> std::uint32_t { return 1; };
		api.GetConstraintDescriptor = [](const void*, std::uint32_t, ConstraintDescriptor001* output) {
			output->sourceFeatureShortName = "Source";
			output->targetFeatureShortName = "Upscaling";
			output->targetSettingPath = "Enabled";
			output->forcedValueJson = "false";
			output->reason = "test constraint";
			return failConstraint ? Status::kUnavailable : Status::kSuccess;
		};
		return api;
	}
}

int main()
{
	try {
		auto require = [](bool condition, const char* message) {
			if (!condition)
				throw std::runtime_error(message);
		};
		const auto api = MakeAPI();
		CSX::Api::AcceptedDrawRegistry registry;
		std::array<CSX::Api::AcceptedDrawRegistry::ObserverSnapshot, 8> observers;
		auto callback = +[](const CSXAcceptedDrawAPI::Draw*, void*) {};
		uint64_t subscription = 0;
		require(registry.Register(callback, nullptr, &subscription) == CSXAcceptedDrawAPI::Success, "register observer fixture");
		require(registry.InspectObservers(observers), "explicit observer inspection available");
		require(observers[0].subscription == subscription && observers[0].callbackAddress == reinterpret_cast<uintptr_t>(callback),
			"retain actual callback identity for module/stack attribution");
		require(registry.Inspect().events == 0 && registry.Inspect().callbacks == 0, "inspection must not dispatch or replay");
		require(registry.Unregister(subscription) == CSXAcceptedDrawAPI::Success, "unregister fixture");
		require(registry.InspectObservers(observers) && observers[0].subscription == 0, "never report a removed observer active");
		bool reentrantRejected = false;
		struct CallbackContext
		{
			CSX::Api::AcceptedDrawRegistry* registry;
			bool* rejected;
		} context{ &registry, &reentrantRejected };
		auto inspectingCallback = +[](const CSXAcceptedDrawAPI::Draw*, void* user) {
			auto& state = *static_cast<CallbackContext*>(user);
			std::array<CSX::Api::AcceptedDrawRegistry::ObserverSnapshot, 8> snapshot;
			*state.rejected = !state.registry->InspectObservers(snapshot);
		};
		require(registry.Register(inspectingCallback, &context, &subscription) == CSXAcceptedDrawAPI::Success, "register inspection fixture");
		registry.Dispatch({}, +[](ID3D11DeviceContext*, const CSXAcceptedDrawAPI::Arguments&) {});
		require(reentrantRejected, "observer callback must not acquire lifecycle lock");
		require(registry.Unregister(subscription) == CSXAcceptedDrawAPI::Success, "unregister inspection fixture");
		const auto result = CSX::Diagnostics::CaptureFeatureSettings(api);
		require(result["stateRevision"] == 42, "retain service revision");
		require(result["features"][0]["shortName"] == "Upscaling", "copy shared API strings before next call");
		require(result["features"][0]["settings"]["Upscaling"]["Enabled"] == false, "retain false effective setting");
		require(result["features"][0]["settings"]["Upscaling"]["Sharpness"] == 0, "retain zero effective setting");
		require(result["features"][1]["settings"].is_null() && result["features"][1]["disabledAtBoot"] == true,
			"unloaded settings unavailable, not fabricated defaults");
		require(settingsCalls == 1, "do not inspect unavailable settings");
		require(result["constraints"][0]["forcedValue"] == false, "retain active constraint and typed forced value");
		for (auto* failure : { &failSettings, &invalidJson, &failDescriptor, &failState, &failConstraint }) {
			*failure = true;
			bool rejected = false;
			try {
				(void)CSX::Diagnostics::CaptureFeatureSettings(api);
			} catch (const std::exception&) {
				rejected = true;
			}
			*failure = false;
			require(rejected, "never report an incomplete snapshot as complete");
		}
		std::cout << "DevBench CPU snapshot tests passed\n";
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
