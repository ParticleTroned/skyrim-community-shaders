#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace NrReplay::DescriptorGuard
{
	enum class Api : std::size_t
	{
		Merged,
		Independent,
		Texture,
		Surface,
		CaptureUav,
		Count
	};
	struct Slot
	{
		std::string_view name;
		std::size_t cacheRva;
		unsigned interfaceId;
	};
	inline constexpr std::array<Slot, static_cast<std::size_t>(Api::Count)> kSlots{ {
		{ "NvAPI_D3D12_GetCudaMergedTextureSamplerObject", 0x1157cb0, 0x329fe6e0 },
		{ "NvAPI_D3D12_GetCudaIndependentDescriptorObject", 0x1157cb8, 0x0ddac234 },
		{ "NvAPI_D3D12_GetCudaTextureObject", 0x1157cc0, 0x80403fc9 },
		{ "NvAPI_D3D12_GetCudaSurfaceObject", 0x1157cc8, 0x48f5b2ee },
		{ "NvAPI_D3D12_CaptureUAVInfo", 0x11576d8, 0x6e5ea9db },
	} };
	using Counts = std::array<std::uint64_t, kSlots.size()>;

	/** Rejects deferred submission after any observed steady descriptor cache miss. */
	class Counters
	{
	public:
		/** Starts new sample accounting without clearing a prior failed admission. */
		void BeginSample() noexcept { sample_.fill(0); }

		/** Observes a call without changing its arguments, result, or forwarding policy. */
		bool Observe(Api api, bool warmup) noexcept
		{
			const auto index = static_cast<std::size_t>(api);
			if (index >= kSlots.size() || !Increment(total_[index]) || !Increment(sample_[index])) {
				healthy_ = false;
				return false;
			}
			if (warmup) {
				warmupObserved_ = true;
			} else {
				(void)Increment(steadyCalls_);
				healthy_ = false;
			}
			return healthy_;
		}

		[[nodiscard]] bool Healthy() const noexcept { return healthy_; }
		[[nodiscard]] bool WarmupObserved() const noexcept { return warmupObserved_; }
		[[nodiscard]] const Counts& SampleCounts() const noexcept { return sample_; }
		[[nodiscard]] const Counts& TotalCounts() const noexcept { return total_; }
		[[nodiscard]] std::uint64_t SteadyCalls() const noexcept { return steadyCalls_; }

	private:
		static bool Increment(std::uint64_t& value) noexcept
		{
			if (value == std::numeric_limits<std::uint64_t>::max())
				return false;
			++value;
			return true;
		}
		Counts total_{}, sample_{};
		std::uint64_t steadyCalls_ = 0;
		bool healthy_ = true, warmupObserved_ = false;
	};
}
