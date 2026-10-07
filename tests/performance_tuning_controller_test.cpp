#include "Menu/PerformanceTuningController.h"
#include "Utils/RuntimeToggle.h"

#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct State
	{
		uint32_t frameCount = 0;
		bool pendingPostLoadRuntimeReset = false;
		bool IsSaveLoadSafeModeActive() const { return false; }
		bool IsEngineSaveLoadActivityActive() const { return false; }
		bool IsMainOrLoadingMenuOpen() const { return false; }
	};

	struct DeferredFeature
	{
		Util::RuntimeToggle enabled{ true };
		uint32_t applied = 1;
		int quality = 7;
		bool reject = false;
		bool IsPerformanceToggleEnabled() const { return enabled.Get(); }
		nlohmann::json CapturePerformanceCostMeasurementState() const
		{
			return { { "Enabled", enabled.Get() }, { "Quality", quality } };
		}
		void SetPerformanceToggleEnabled(bool value)
		{
			if (!reject)
				enabled.Set(value);
		}
		void RestorePerformanceToggleState(const nlohmann::json& state)
		{
			enabled.Set(state.at("Enabled").get<bool>());
			quality = state.at("Quality").get<int>();
		}
	};
}

int main()
try {
	using namespace PerformanceTuningController;
	using nlohmann::json;
	DeferredFeature feature;
	State state;
	feature.enabled.Apply(feature.applied, &state);
	std::optional<DisabledConfiguration> saved;
	Require(SetEnabled(feature, false, saved), "a deferred disable request must be accepted");
	Require(!feature.enabled.Get() && feature.applied == 1 && saved.has_value(), "pending disable must retain original settings");
	const auto original = *saved;
	Require(SetEnabled(feature, false, saved) && saved->enabledState == original.enabledState, "a repeated request must not overwrite the enabled snapshot");
	Require(!feature.enabled.Apply(feature.applied, &state), "the controller must not bypass the per-frame runtime guard");
	++state.frameCount;
	Require(feature.enabled.Apply(feature.applied, &state) && feature.applied == 0, "the disabled preference must apply on the next safe frame");
	feature.quality = 11;
	Require(SetEnabled(feature, true, saved), "restore must accept a deferred enable request");
	Require(feature.enabled.Get() && feature.applied == 0 && feature.quality == 11 && !saved, "reenabling must preserve edits made while disabled");
	++state.frameCount;
	feature.enabled.Apply(feature.applied, &state);
	Require(feature.applied == 1, "the restored preference must reach the renderer");

	feature.enabled.Set(false);
	Require(SetEnabled(feature, true, saved) && feature.quality == 11, "enable without a snapshot must retain tuning values");
	feature.reject = true;
	const auto before = feature.CapturePerformanceCostMeasurementState();
	Require(!SetEnabled(feature, false, saved), "a rejected toggle must report failure");
	Require(feature.CapturePerformanceCostMeasurementState() == before && !saved, "rejected disable must leave no snapshot or settings change");

	const json enabled = { { "effects", { { "a", true }, { "b", false } } }, { "quality", 7 } };
	const json disabled = { { "effects", { { "a", false }, { "b", false } } }, { "quality", 7 } };
	json edited = disabled;
	edited["effects"]["b"] = true;
	edited["quality"] = 13;
	edited["newSetting"] = 2;
	const auto restored = RestoreToggleChanges(enabled, disabled, edited);
	Require(restored.at("effects").at("a") == true && restored.at("effects").at("b") == true, "mixed toggles must restore owned changes and retain user edits");
	Require(restored.at("quality") == 13 && restored.at("newSetting") == 2, "restoration must retain unrelated and new fields");
	Require(RestoreToggleChanges(enabled, disabled, disabled) == enabled, "unchanged disabled settings must restore exactly");
	Require(RestoreToggleChanges(json(3), json(0), json(5)) == 5, "an edited scalar must not be overwritten");

	Require(NextBatchAction(0, 2, false, false, false, 0) == BatchAction::StartNext, "a new batch must start its first comparison");
	Require(NextBatchAction(1, 2, true, false, false, 0) == BatchAction::Wait, "a running comparison must own the batch");
	Require(NextBatchAction(1, 2, false, true, false, 5) == BatchAction::Wait, "batch comparisons must respect cooldown");
	Require(NextBatchAction(1, 2, false, true, false, 0) == BatchAction::StartNext, "completed comparison must advance once ready");
	Require(NextBatchAction(2, 2, false, true, false, 10) == BatchAction::Finish, "the last comparison must finish without waiting for an unused cooldown");
	Require(NextBatchAction(1, 2, false, true, true, 0) == BatchAction::Fail, "failed comparison must stop the batch");
	Require(NextBatchAction(1, 2, false, false, false, 0) == BatchAction::Fail, "lost comparison state must fail closed");
	Require(NextBatchAction(0, 0, false, false, false, 0) == BatchAction::Fail, "empty batch must fail closed");
	Require(NextBatchAction(3, 2, false, true, false, 0) == BatchAction::Fail, "invalid batch index must fail closed");
	std::cout << "Deferred toggle, restoration, rejection and batch sequencing checks passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
