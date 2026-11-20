#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

namespace NeuralRendering
{
	/** @brief Main-thread combat and menu values without retained engine pointers. */
	struct CombatStateSnapshot
	{
		std::optional<bool> inCombat;
		bool gamePaused = true;
		std::uint64_t sampledAtMs = 0;
		bool combatInterrupted = false;
		bool gameplayInterrupted = false;
	};

	/** @brief Transfers a coherent main-thread sample to render admission. */
	class CombatState
	{
	public:
		/** @brief Records main-thread time and retains interruptions until consumed. */
		void Publish(std::optional<bool> a_inCombat, bool a_gamePaused, std::uint64_t a_nowMs)
		{
			std::scoped_lock lock(mutex);
			snapshot.inCombat = a_inCombat;
			snapshot.gamePaused = a_gamePaused;
			snapshot.sampledAtMs = a_nowMs;
			snapshot.combatInterrupted |= !a_inCombat || *a_inCombat;
			snapshot.gameplayInterrupted |= a_gamePaused;
		}

		/** @brief Consumes pending interruptions once per render frame; callers serialize consumers. */
		[[nodiscard]] CombatStateSnapshot ConsumeSnapshot()
		{
			std::scoped_lock lock(mutex);
			const auto result = snapshot;
			snapshot.combatInterrupted = false;
			snapshot.gameplayInterrupted = false;
			return result;
		}

	private:
		std::mutex mutex;
		CombatStateSnapshot snapshot;
	};
}
