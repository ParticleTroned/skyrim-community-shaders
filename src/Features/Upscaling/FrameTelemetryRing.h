#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace UpscalingTelemetry
{
	inline void SaturatingIncrement(std::uint32_t& a_value) noexcept
	{
		if (a_value != std::numeric_limits<std::uint32_t>::max())
			++a_value;
	}

	template <class Snapshot, std::size_t Capacity>
	class FrameTelemetryRing
	{
		static_assert(Capacity > 0);

	public:
		[[nodiscard]] Snapshot* Find(std::uint32_t a_frame) noexcept
		{
			for (auto& snapshot : snapshots_) {
				if (snapshot.valid && snapshot.frame == a_frame)
					return &snapshot;
			}
			return nullptr;
		}

		[[nodiscard]] const Snapshot* Find(std::uint32_t a_frame) const noexcept
		{
			for (const auto& snapshot : snapshots_) {
				if (snapshot.valid && snapshot.frame == a_frame)
					return &snapshot;
			}
			return nullptr;
		}

		Snapshot& GetOrCreate(std::uint32_t a_frame) noexcept
		{
			if (auto* existing = Find(a_frame))
				return *existing;

			for (auto& snapshot : snapshots_) {
				if (!snapshot.valid)
					return Initialize(snapshot, a_frame);
			}

			auto& recycled = snapshots_[nextRecycleIndex_];
			nextRecycleIndex_ = (nextRecycleIndex_ + 1) % Capacity;
			return Initialize(recycled, a_frame);
		}

	private:
		static Snapshot& Initialize(
			Snapshot& a_snapshot,
			std::uint32_t a_frame) noexcept
		{
			a_snapshot = {};
			a_snapshot.valid = true;
			a_snapshot.frame = a_frame;
			return a_snapshot;
		}

		std::array<Snapshot, Capacity> snapshots_{};
		std::size_t nextRecycleIndex_ = 0;
	};
}
