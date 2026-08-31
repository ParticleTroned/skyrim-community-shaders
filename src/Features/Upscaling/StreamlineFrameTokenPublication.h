#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace StreamlineFrameTokenPublication
{
	template <class Token>
	class Coordinator
	{
	public:
		struct Snapshot
		{
			std::uint32_t frame = 0;
			Token token{};
			bool acquired = false;
		};

		/** Returns one coherent frame/token pair, acquiring it at most once per frame. */
		template <class Acquire>
		[[nodiscard]] std::optional<Snapshot> Resolve(
			std::uint32_t a_frame,
			Acquire&& a_acquire)
		{
			std::lock_guard lock(mutex_);
			if (published_ && published_->frame == a_frame) {
				return Snapshot{ published_->frame, published_->token, false };
			}
			if (published_ && IsOlderFrame(a_frame, published_->frame))
				return std::nullopt;

			auto token = std::forward<Acquire>(a_acquire)(a_frame);
			if (!token)
				return std::nullopt;

			published_ = Published{ a_frame, *token };
			return Snapshot{ a_frame, *token, true };
		}

		/** Invalidates the published pair under the acquisition lock. */
		void Reset()
		{
			std::lock_guard lock(mutex_);
			published_.reset();
		}

	private:
		static constexpr bool IsOlderFrame(
			std::uint32_t a_candidate,
			std::uint32_t a_published) noexcept
		{
			return a_candidate != a_published &&
			       a_published - a_candidate < 0x80000000u;
		}

		struct Published
		{
			std::uint32_t frame = 0;
			Token token{};
		};

		std::mutex mutex_;
		std::optional<Published> published_;
	};
}
