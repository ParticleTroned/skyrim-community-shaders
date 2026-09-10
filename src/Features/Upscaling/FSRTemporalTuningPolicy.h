#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace FSRTemporalTuningPolicy
{
	/** Optional FSR 3.1 reconstruction overrides; disabled preserves provider defaults. */
	struct Settings
	{
		bool enabled = false;
		float velocityFactor = 1.0f;
		float reactivenessScale = 1.0f;
		float shadingChangeScale = 1.0f;
		float accumulationAddedPerFrame = 0.333f;
		float minimumDisocclusionAccumulation = -0.333f;
		bool operator==(const Settings&) const = default;
	};

	inline constexpr float kMaximumResponseScale = 4.0f;

	/** Bounds unbounded SDK response multipliers for the supported user interface. */
	[[nodiscard]] inline bool IsValid(const Settings& a_settings) noexcept
	{
		const auto bounded = [](float value, float minimum, float maximum) {
			return std::isfinite(value) && value >= minimum && value <= maximum;
		};
		return bounded(a_settings.velocityFactor, 0.0f, 1.0f) &&
		       bounded(a_settings.reactivenessScale, 0.0f, kMaximumResponseScale) &&
		       bounded(a_settings.shadingChangeScale, 0.0f, kMaximumResponseScale) &&
		       bounded(a_settings.accumulationAddedPerFrame, 0.0f, 1.0f) &&
		       bounded(a_settings.minimumDisocclusionAccumulation, -1.0f, 1.0f);
	}

	/** Only documented FSR 3.1.4/3.1.5 providers support this complete override set. */
	[[nodiscard]] constexpr bool SupportsProvider(std::uint64_t a_providerId) noexcept
	{
		const auto version = static_cast<std::uint32_t>(a_providerId);
		const auto major = (version >> 22) & 0x3FFu;
		const auto minor = (version >> 12) & 0x3FFu;
		const auto patch = version & 0xFFFu;
		return major == 3 && minor == 1 && (patch == 4 || patch == 5);
	}

	/** Editing inactive values must not recreate a provider context. */
	[[nodiscard]] constexpr bool Equivalent(const Settings& a_left, const Settings& a_right) noexcept
	{
		return (!a_left.enabled && !a_right.enabled) || a_left == a_right;
	}

	/** A profile change cannot replace contexts between sequential eye dispatches. */
	[[nodiscard]] constexpr bool CanReuseContextProfile(std::uint64_t a_contextRevision,
		std::uint64_t a_requestedRevision, bool a_dispatchedThisFrame) noexcept
	{
		return a_contextRevision == a_requestedRevision || a_dispatchedThisFrame;
	}

	/** Supplies public configure values in velocity/reactivity/shading/accumulation order. */
	[[nodiscard]] constexpr std::array<float, 5> Values(const Settings& a_settings) noexcept
	{
		return { a_settings.velocityFactor, a_settings.reactivenessScale,
			a_settings.shadingChangeScale, a_settings.accumulationAddedPerFrame,
			a_settings.minimumDisocclusionAccumulation };
	}

	enum class Status : std::uint8_t
	{
		Inactive,
		Pending,
		VendorDefaults,
		UnsupportedProvider,
		Applied,
		Rejected,
		Faulted
	};

	[[nodiscard]] constexpr const char* StatusLabel(Status a_status) noexcept
	{
		switch (a_status) {
		case Status::Inactive:
			return "inactive";
		case Status::Pending:
			return "pending";
		case Status::VendorDefaults:
			return "vendor_defaults";
		case Status::UnsupportedProvider:
			return "unsupported_provider";
		case Status::Applied:
			return "applied";
		case Status::Rejected:
			return "rejected_vendor_defaults";
		case Status::Faulted:
			return "provider_fault";
		}
		return "unknown";
	}

	/** A rejected request remains disabled for the same provider until settings change. */
	struct RejectedRequest
	{
		bool valid = false;
		Settings settings{};
		std::uint64_t providerId = 0;
		[[nodiscard]] bool Matches(const Settings& a_settings, std::uint64_t a_providerId) const noexcept
		{
			return valid && settings == a_settings && providerId == a_providerId;
		}
	};

	struct ConfigureResult
	{
		bool success = true;
		bool faulted = false;
		std::int32_t code = 0;
		std::uint32_t context = 0;
		std::uint32_t key = 0;
	};

	/** Stops at the first rejected key; callers must discard the entire fresh context set. */
	template <class Configure>
	[[nodiscard]] ConfigureResult ConfigureFreshContexts(
		const Settings& a_settings, std::uint32_t a_contextCount, Configure a_configure)
	{
		if (!IsValid(a_settings) || a_contextCount == 0 || a_contextCount > 2)
			return { false, false, -1 };
		if (!a_settings.enabled)
			return {};
		const auto values = Values(a_settings);
		for (std::uint32_t context = 0; context < a_contextCount; ++context) {
			for (std::uint32_t key = 0; key < values.size(); ++key) {
				auto result = a_configure(context, key, values[key]);
				if (!result.success || result.faulted) {
					result.success = false;
					result.context = context;
					result.key = key;
					return result;
				}
			}
		}
		return {};
	}
}
