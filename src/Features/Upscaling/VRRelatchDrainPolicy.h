#pragma once

#include <cstdint>

namespace VRRelatchDrainPolicy
{
	/** Render-thread proof of drain completion for one immutable provider revision. */
	class Proof
	{
	public:
		[[nodiscard]] constexpr bool Matches(std::uint64_t a_epoch) const noexcept
		{
			return a_epoch != 0 && epoch == a_epoch && revision == providerRevision;
		}

		[[nodiscard]] constexpr bool IsReady(std::uint64_t a_epoch) const noexcept
		{
			return Matches(a_epoch) && ready;
		}

		/** Returns true only when the caller must issue a new drain fence. */
		[[nodiscard]] constexpr bool Begin(std::uint64_t a_epoch) noexcept
		{
			if (a_epoch == 0 || Matches(a_epoch))
				return false;
			epoch = a_epoch;
			revision = providerRevision;
			ready = false;
			return true;
		}

		constexpr void MarkReady(std::uint64_t a_epoch) noexcept
		{
			if (Matches(a_epoch))
				ready = true;
		}

		constexpr void Cancel() noexcept
		{
			epoch = 0;
			revision = 0;
			ready = false;
		}

		/** Provider dispatch or ownership mutation revokes even a completed proof. */
		constexpr void Invalidate() noexcept
		{
			if (++providerRevision == 0)
				providerRevision = 1;
			Cancel();
		}

	private:
		std::uint64_t providerRevision = 1;
		std::uint64_t epoch = 0;
		std::uint64_t revision = 0;
		bool ready = false;
	};
}
