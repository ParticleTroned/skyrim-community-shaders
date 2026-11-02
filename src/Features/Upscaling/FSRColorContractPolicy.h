#pragma once

#include <cstdint>
#include <limits>

namespace FSRColorContractPolicy
{
	inline constexpr std::uint64_t kHighDynamicRangeInputBit = 1ull << 0;
	inline constexpr std::uint64_t kAutoExposureBit = 1ull << 1;
	inline constexpr std::uint64_t kFlagMask =
		kHighDynamicRangeInputBit | kAutoExposureBit;
	inline constexpr std::uint64_t kContextValidBit = 1ull << 63;
	inline constexpr std::uint64_t kRevisionShift = 2;
	inline constexpr std::uint64_t kMaximumRevision =
		std::numeric_limits<std::uint64_t>::max() >> kRevisionShift;

	struct Requested
	{
		std::uint64_t revision = 1;
		bool highDynamicRangeInput = true;
		bool autoExposure = true;
		bool operator==(const Requested&) const = default;
	};

	struct UpdatePlan
	{
		bool revisionMatched = false;
		bool changed = false;
		std::uint64_t desiredState = 0;
		std::uint64_t resultingRevision = 0;
	};

	enum class ReplacementState : std::uint8_t
	{
		None,
		Deferred,
		Ready,
	};

	[[nodiscard]] constexpr std::uint64_t Flags(
		bool a_highDynamicRangeInput,
		bool a_autoExposure) noexcept
	{
		return (a_highDynamicRangeInput ? kHighDynamicRangeInputBit : 0) |
		       (a_autoExposure ? kAutoExposureBit : 0);
	}

	[[nodiscard]] constexpr std::uint64_t Pack(const Requested& a_request) noexcept
	{
		return (a_request.revision << kRevisionShift) |
		       Flags(a_request.highDynamicRangeInput, a_request.autoExposure);
	}

	[[nodiscard]] constexpr Requested Decode(std::uint64_t a_state) noexcept
	{
		return {
			a_state >> kRevisionShift,
			(a_state & kHighDynamicRangeInputBit) != 0,
			(a_state & kAutoExposureBit) != 0,
		};
	}

	inline constexpr std::uint64_t kDefaultState = Pack({});
	inline constexpr std::uint64_t kDefaultFlags = kFlagMask;

	[[nodiscard]] constexpr UpdatePlan PlanUpdate(
		std::uint64_t a_currentState,
		std::uint64_t a_expectedRevision,
		bool a_highDynamicRangeInput,
		bool a_autoExposure) noexcept
	{
		const auto current = Decode(a_currentState);
		if (current.revision != a_expectedRevision)
			return { false, false, a_currentState, current.revision };
		if (current.highDynamicRangeInput == a_highDynamicRangeInput &&
			current.autoExposure == a_autoExposure) {
			return { true, false, a_currentState, current.revision };
		}

		const auto nextRevision = current.revision == kMaximumRevision ?
		                              1ull :
		                              current.revision + 1ull;
		return {
			true,
			true,
			Pack({ nextRevision, a_highDynamicRangeInput, a_autoExposure }),
			nextRevision,
		};
	}

	[[nodiscard]] constexpr std::uint64_t ContextState(
		std::uint64_t a_requestedState) noexcept
	{
		return kContextValidBit | (a_requestedState & kFlagMask);
	}

	[[nodiscard]] constexpr bool ContextMatches(
		std::uint64_t a_contextState,
		std::uint64_t a_requestedState) noexcept
	{
		return (a_contextState & kContextValidBit) != 0 &&
		       (a_contextState & kFlagMask) == (a_requestedState & kFlagMask);
	}

	/** A request change cannot replace a provider context between stereo eyes. */
	[[nodiscard]] constexpr bool CanReuseContext(
		std::uint64_t a_contextState,
		std::uint64_t a_requestedState,
		bool a_dispatchedThisFrame) noexcept
	{
		return ContextMatches(a_contextState, a_requestedState) || a_dispatchedThisFrame;
	}

	[[nodiscard]] constexpr ReplacementState GetReplacementState(
		std::uint64_t a_hostContextState,
		std::uint32_t a_hostLastDispatchFrame,
		std::uint64_t a_runtimeContextState,
		std::uint32_t a_runtimeLastDispatchFrame,
		std::uint64_t a_requestedState,
		std::uint32_t a_currentFrame) noexcept
	{
		const bool hostMismatch =
			(a_hostContextState & kContextValidBit) != 0 &&
			!ContextMatches(a_hostContextState, a_requestedState);
		const bool runtimeMismatch =
			(a_runtimeContextState & kContextValidBit) != 0 &&
			!ContextMatches(a_runtimeContextState, a_requestedState);
		if (!hostMismatch && !runtimeMismatch)
			return ReplacementState::None;

		const bool dispatchedThisFrame =
			a_currentFrame != 0 &&
			((hostMismatch && a_hostLastDispatchFrame == a_currentFrame) ||
				(runtimeMismatch && a_runtimeLastDispatchFrame == a_currentFrame));
		return dispatchedThisFrame ? ReplacementState::Deferred : ReplacementState::Ready;
	}
}
