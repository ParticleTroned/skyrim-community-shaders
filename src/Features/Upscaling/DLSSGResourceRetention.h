#pragma once

/** @brief Tracks DLSS-G allocation ownership independently of resettable frame state. */
class DLSSGResourceRetention
{
public:
	struct ReleaseConditions
	{
		bool userEnabled = true;
		bool providerAvailable = false;
		bool presentCompleted = false;
		bool stateAvailable = false;
		bool synchronized = false;
		bool requiresPresentBoundary = true;
	};

	enum class ReleaseResult
	{
		kDeferred,
		kDrainFailed,
		kReleaseFailed,
		kReleased,
	};

	/** @brief Records successful options without dropping ownership during pauses. */
	void OnOptionsSucceeded(bool a_enabled) noexcept { retained |= a_enabled; }
	/** @brief Drops ownership only after Streamline confirms successful shutdown. */
	void OnShutdownSucceeded() noexcept { retained = false; }
	[[nodiscard]] bool HasRetainedResources() const noexcept { return retained; }

	/** @brief Drains before freeing; failed or deferred attempts retain ownership for retry. */
	template <class Drain, class Release>
	[[nodiscard]] ReleaseResult TryRelease(
		const ReleaseConditions& a_conditions,
		Drain&& a_drain,
		Release&& a_release)
	{
		if (!retained || a_conditions.userEnabled || !a_conditions.providerAvailable ||
			!a_conditions.presentCompleted || !a_conditions.stateAvailable ||
			!a_conditions.synchronized || a_conditions.requiresPresentBoundary)
			return ReleaseResult::kDeferred;
		if (!a_drain())
			return ReleaseResult::kDrainFailed;
		if (!a_release())
			return ReleaseResult::kReleaseFailed;
		retained = false;
		return ReleaseResult::kReleased;
	}

private:
	bool retained = false;
};
