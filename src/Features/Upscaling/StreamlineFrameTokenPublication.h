#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace StreamlineFrameTokenPublication
{
	enum class ResetScope
	{
		Lifecycle,
		DispatchFailure,
	};

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

		/** Publishes one frame/token pair, reused until a newer frame or lifecycle reset. */
		template <class Acquire>
		[[nodiscard]] std::optional<Snapshot> Resolve(
			std::uint32_t a_frame,
			Acquire&& a_acquire)
		{
			std::lock_guard lock(mutex_);
			if (published_ && published_->frame == a_frame && published_->token) {
				return Snapshot{ published_->frame, *published_->token, false };
			}
			if (published_ && IsOlderFrame(a_frame, published_->frame))
				return std::nullopt;

			auto token = std::forward<Acquire>(a_acquire)(a_frame);
			if (!token)
				return std::nullopt;

			published_ = Published{ a_frame, *token };
			return Snapshot{ a_frame, *token, true };
		}

		/** Lifecycle resets invalidate the token; frame ordering survives every reset. */
		void Reset(ResetScope a_scope = ResetScope::Lifecycle)
		{
			if (a_scope == ResetScope::DispatchFailure)
				return;
			std::lock_guard lock(mutex_);
			if (published_)
				published_->token.reset();
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
			std::optional<Token> token;
		};

		std::mutex mutex_;
		std::optional<Published> published_;
	};
}
