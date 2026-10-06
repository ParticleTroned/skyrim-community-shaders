#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace Util
{
	/** @brief Retain feature changes while engine state is unsafe to reconcile. */
	template <class State>
	bool IsRuntimeToggleBlocked(const State* state)
	{
		return !state || state->IsSaveLoadSafeModeActive() || state->IsEngineSaveLoadActivityActive() ||
		       state->IsMainOrLoadingMenuOpen() || state->pendingPostLoadRuntimeReset;
	}

	/** @brief Thread-safe preference with one render-owned application per frame. */
	class RuntimeToggle
	{
	public:
		explicit RuntimeToggle(bool enabled) : requested(enabled) {}
		bool Get() const { return requested.load(std::memory_order_acquire); }
		void Set(bool enabled) { requested.store(enabled, std::memory_order_release); }

		template <class Flag, class State>
		bool Apply(Flag& enabled, const State* state)
		{
			if (!state || frame == state->frameCount)
				return false;
			frame = state->frameCount;
			if (IsRuntimeToggleBlocked(state))
				return false;
			const auto target = static_cast<Flag>(Get());
			const bool changed = enabled != target;
			enabled = target;
			return changed;
		}

	private:
		std::atomic<bool> requested;
		std::optional<uint32_t> frame;
	};
}
