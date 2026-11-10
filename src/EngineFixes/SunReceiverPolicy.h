#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace SunShadowReceiver::Policy
{
	static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
	static_assert(std::atomic_ref<std::uint32_t>::required_alignment <= alignof(std::uint32_t));
	static_assert(std::atomic_ref<const std::uint32_t>::required_alignment <= alignof(std::uint32_t));

	enum class Reason : std::uint32_t
	{
		candidate,
		nonPrimaryPass,
		unavailable,
		identityChanged,
		notReceiver,
		excludedLod,
		shadowDisabled,
		noSun,
		alreadyPresent,
		invalidBounds,
		outsideRange,
		count
	};
	inline constexpr std::array<std::string_view, static_cast<std::size_t>(Reason::count)> ReasonNames{
		"candidate", "non_primary_pass", "unavailable", "identity_changed", "not_receiver",
		"excluded_lod", "shadow_disabled", "no_sun", "already_present", "invalid_bounds", "outside_range"
	};
	inline constexpr std::uint64_t ReceiveShadows = std::uint64_t{ 1 } << 8;
	inline constexpr std::uint64_t ExcludedLod = (std::uint64_t{ 1 } << 33) | (std::uint64_t{ 1 } << 34) |
	                                             (std::uint64_t{ 1 } << 52) | (std::uint64_t{ 1 } << 63);

	struct Input
	{
		bool primaryPass{}, available{}, identityStable{}, shadowsEnabled{};
		std::uint64_t flags{};
		std::uintptr_t lightData{};
		std::uint32_t activeMask{}, sunMask{};
		std::array<float, 3> center{}, camera{};
		float radius{}, shadowDistance{};
	};
	struct Decision
	{
		Reason reason = Reason::unavailable;
		double distanceSquared{}, rangeSquared{};
		bool rangeValid = false;
	};

	/** Match receiver eligibility; malformed bounds fail closed before any native write. */
	inline Decision Evaluate(const Input& input) noexcept
	{
		Decision result;
		auto reject = [&](Reason reason) { result.reason = reason; return result; };
		if (!input.primaryPass)
			return reject(Reason::nonPrimaryPass);
		if (!input.available)
			return reject(Reason::unavailable);
		if (!input.identityStable)
			return reject(Reason::identityChanged);
		if (!(input.flags & ReceiveShadows))
			return reject(Reason::notReceiver);
		if (input.flags & ExcludedLod)
			return reject(Reason::excludedLod);
		if (!input.shadowsEnabled)
			return reject(Reason::shadowDisabled);
		if (!input.sunMask)
			return reject(Reason::noSun);
		if (input.activeMask & input.sunMask)
			return reject(Reason::alreadyPresent);
		if (!std::isfinite(input.radius) || input.radius < 0 ||
			!std::isfinite(input.shadowDistance) || input.shadowDistance <= 0)
			return reject(Reason::invalidBounds);
		for (std::size_t i = 0; i < input.center.size(); ++i) {
			if (!std::isfinite(input.center[i]) || !std::isfinite(input.camera[i]))
				return reject(Reason::invalidBounds);
			const double delta = double(input.center[i]) - input.camera[i];
			result.distanceSquared += delta * delta;
		}
		const double range = double(input.shadowDistance) + input.radius;
		result.rangeSquared = range * range;
		result.rangeValid = true;
		return reject(result.distanceSquared < result.rangeSquared ? Reason::candidate : Reason::outsideRange);
	}

	/** Preserve the native rule: any matching sun bit already supplies reception. */
	template <class T>
	std::uint32_t SunMask(const T* lights, std::uint32_t count, T sun, bool vr) noexcept
	{
		if (!lights || !sun)
			return 0;
		std::uint32_t mask = 0;
		for (std::uint32_t i = 0; i < count && i < 32; ++i)
			if (lights[i] == sun) {
				mask |= std::uint32_t{ 1 } << i;
				if (vr)
					break;
			}
		return mask;
	}

	/** Read the native mask atomically without synchronizing unrelated engine state. */
	inline std::uint32_t ReadMask(const std::uint32_t& mask) noexcept
	{
		return std::atomic_ref<const std::uint32_t>(mask).load(std::memory_order_relaxed);
	}

	/** Called only at native pass generation; observe mode never mutates the mask. */
	inline bool Apply(std::uint32_t& mask, std::uintptr_t lightData, const Input& input, const Decision& decision, bool repair) noexcept
	{
		if (!repair || decision.reason != Reason::candidate || !lightData || lightData != input.lightData)
			return false;
		// A changed mask invalidates this decision; never retry it against newer state.
		auto expected = input.activeMask;
		return std::atomic_ref<std::uint32_t>(mask).compare_exchange_strong(
			expected, input.activeMask | input.sunMask, std::memory_order_relaxed);
	}
}
