#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace PerformanceTuningController
{
	inline constexpr double kFeatureBatchMaximumSeconds = 300.0;
	inline constexpr double kFeatureBatchCooldownSeconds = 0.0;
	inline constexpr double kFeatureBatchRestorationMarginSeconds = 1.0;
	/** Per-state minimum waits retained with the measurement through cancellation. */
	struct FeatureCostWaits
	{
		double current, comparison, restoration;
		/** Include both measured states without changing their sampling duration. */
		double TotalSeconds(double measurementSeconds) const { return current + comparison + restoration + 2 * measurementSeconds; }
	};
	/** Compress generic waits while preserving a feature's explicit temporal requirements. */
	inline FeatureCostWaits BatchFeatureWaits(double enabled, double disabled, double defaultWait)
	{
		if (!std::isfinite(enabled) || enabled < 0 || !std::isfinite(disabled) || disabled < 0)
			return { defaultWait, std::max(3.0, defaultWait), defaultWait };
		return enabled == defaultWait && disabled == defaultWait ? FeatureCostWaits{ 0.5, 0.5, 0.5 } :
		                                                           FeatureCostWaits{ enabled, disabled, enabled };
	}
	/** Reserve the feature's restoration period and one update margin inside the batch budget. */
	inline bool BatchNeedsRestoration(double elapsed, double restoration)
	{
		return elapsed >= kFeatureBatchMaximumSeconds - restoration - kFeatureBatchRestorationMarginSeconds;
	}

	struct DisabledConfiguration
	{
		nlohmann::json enabledState;
		nlohmann::json disabledState;
	};

	/** Distinguishes incomplete native restoration from an application failure. */
	struct SettingsRollbackError : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	/** Roll back native state and toggle history; successful replacements discard only their own history. */
	template <class Feature, class Apply>
	bool ApplyTransaction(const std::vector<Feature*>& features,
		std::unordered_map<std::string, std::optional<DisabledConfiguration>>& configurations, Apply&& apply, bool replaceToggleHistory = false)
	{
		const auto previousConfigurations = configurations;
		std::vector<nlohmann::json> previous;
		previous.reserve(features.size());
		for (auto* feature : features)
			previous.push_back(feature->CapturePerformanceCostMeasurementState());
		const auto restore = [&] {
			std::exception_ptr failure;
			for (auto index = features.size(); index > 0; --index) {
				try {
					features[index - 1]->RestorePerformanceToggleState(previous[index - 1]);
				} catch (...) {
					if (!failure)
						failure = std::current_exception();
				}
			}
			configurations = previousConfigurations;
			if (failure) {
				try {
					std::rethrow_exception(failure);
				} catch (const std::exception& error) {
					throw SettingsRollbackError(std::string("Feature settings rollback failed: ") + error.what());
				} catch (...) {
					throw SettingsRollbackError("Feature settings rollback failed unexpectedly.");
				}
			}
		};
		try {
			if (apply()) {
				if (replaceToggleHistory)
					for (auto* feature : features)
						configurations.erase(feature->GetShortName());
				return true;
			}
		} catch (...) {
			restore();
			throw;
		}
		restore();
		return false;
	}

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
