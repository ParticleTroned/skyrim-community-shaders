#pragma once

#include <atomic>

namespace Util
{
	/** @brief Applies lease ownership before the game-thread camera update. */
	void UpdateVanityCameraSuppression();

	/**
	 * @brief Keeps Skyrim's automatic idle camera suppressed while at least one lease is active.
	 *
	 * Ownership is thread-safe. The first lease saves the current delay and the
	 * final lease restores it and starts a fresh idle interval.
	 * Camera updates run in the main update hook without changing camera
	 * permission. Reacquiring is idempotent and never submits a task.
	 */
	class VanityCameraSuppressionLease
	{
	public:
		VanityCameraSuppressionLease() = default;
		~VanityCameraSuppressionLease();

		VanityCameraSuppressionLease(const VanityCameraSuppressionLease&) = delete;
		VanityCameraSuppressionLease& operator=(const VanityCameraSuppressionLease&) = delete;
		VanityCameraSuppressionLease(VanityCameraSuppressionLease&&) = delete;
		VanityCameraSuppressionLease& operator=(VanityCameraSuppressionLease&&) = delete;

		/** @return True when owned; false until the game thread finds the INI setting. */
		bool Acquire();
		void Release();

		[[nodiscard]] bool IsActive() const { return active.load(std::memory_order_relaxed); }

	private:
		std::atomic<bool> active = false;
	};
}
