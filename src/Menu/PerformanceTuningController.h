#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>

namespace PerformanceTuningController
{
	struct DisabledConfiguration
	{
		nlohmann::json enabledState;
		nlohmann::json disabledState;
	};

	/** Restore only toggle-owned values that the user has not subsequently edited. */
	inline nlohmann::json RestoreToggleChanges(const nlohmann::json& enabled, const nlohmann::json& disabled, const nlohmann::json& current)
	{
		if (enabled == disabled)
			return current;
		if (current == disabled)
			return enabled;
		if (!enabled.is_object() || !disabled.is_object() || !current.is_object())
			return current;
		auto restored = current;
		for (const auto& [key, value] : enabled.items()) {
			if (disabled.contains(key) && current.contains(key))
				restored[key] = RestoreToggleChanges(value, disabled.at(key), current.at(key));
		}
		return restored;
	}

	/** Verify requested state rather than delayed render state, rolling back rejected changes. */
	template <class Feature>
	bool SetEnabled(Feature& feature, bool enabled, std::optional<DisabledConfiguration>& saved)
	{
		if (feature.IsPerformanceToggleEnabled() == enabled)
			return true;
		const auto current = feature.CapturePerformanceCostMeasurementState();
		if (!enabled) {
			feature.SetPerformanceToggleEnabled(false);
			saved = DisabledConfiguration{ current, feature.CapturePerformanceCostMeasurementState() };
		} else if (saved) {
			feature.RestorePerformanceToggleState(RestoreToggleChanges(saved->enabledState, saved->disabledState, current));
		} else {
			feature.SetPerformanceToggleEnabled(true);
		}
		if (feature.IsPerformanceToggleEnabled() != enabled) {
			feature.RestorePerformanceToggleState(current);
			if (!enabled)
				saved.reset();
			return false;
		}
		if (enabled)
			saved.reset();
		return true;
	}

	enum class BatchAction
	{
		Wait,
		StartNext,
		Finish,
		Fail
	};

	/** Advance only after the prior comparison completed and its cooldown elapsed. */
	inline BatchAction NextBatchAction(std::size_t started, std::size_t total, bool measurementActive, bool previousComplete, bool previousFailed, double cooldown)
	{
		if (total == 0 || started > total)
			return BatchAction::Fail;
		if (measurementActive)
			return BatchAction::Wait;
		if (started > 0 && (!previousComplete || previousFailed))
			return BatchAction::Fail;
		if (started == total)
			return BatchAction::Finish;
		return cooldown > 0.0 ? BatchAction::Wait : BatchAction::StartNext;
	}
}
