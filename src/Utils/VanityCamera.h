#pragma once

namespace Util
{
	/**
	 * @brief Keeps Skyrim's automatic idle camera suppressed while at least one lease is active.
	 *
	 * Acquire and release this lease on the game UI thread. The first lease saves the
	 * current delay and the final lease restores that exact value.
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

		/** @return True when this lease owns an active suppression request. */
		bool Acquire();
		void Release();

		[[nodiscard]] bool IsActive() const { return active; }

	private:
		bool active = false;
	};
}
