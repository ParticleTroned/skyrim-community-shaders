#pragma once

#include "MonoPolicy.h"
#include "NativeEvaluationLayout.h"

namespace NeuralRendering
{
	struct MonoPrewarmKey
	{
		NativeEvaluationLayout layout{};
		std::uintptr_t device = 0, context = 0;
		std::uint32_t sourceWidth = 0, sourceHeight = 0, sourceFormat = 0;
		std::uint32_t activeWidth = 0, activeHeight = 0, mode = 0, network = 0;
		bool operator==(const MonoPrewarmKey&) const = default;
	};
	[[nodiscard]] inline const char* MonoPrewarmConfigurationBlocker(const MonoSettings& settings) noexcept
	{
		if (!settings.enabled || !settings.prewarm) return "disabled";
		if (settings.charactersOnly || settings.sceneCategoryStrengths) return "live_actor_or_category_layout";
		if (settings.regionPercent != 100) return "restricted_source_domain";
		if (settings.pressureResolution) return "adaptive_resolution_requires_live_admission";
		return nullptr;
	}
	struct MonoPrewarmPolicy
	{
		static constexpr std::uint64_t kLeaseMs = 30000, kRetryMs = 1000;
		MonoPrewarmKey key{};
		const char* stage = "disabled";
		std::uint64_t expiresAtMs = 0, retryAtMs = 0;
		std::uint32_t lastOpportunity = UINT32_MAX;
		bool captured = false, unused = false, failed = false, suppressed = false;
		bool Begin(std::uint32_t frame, std::uint64_t now) noexcept
		{
			if (lastOpportunity == frame || now < retryAtMs || suppressed) return false;
			lastOpportunity = frame; retryAtMs = now + kRetryMs; return true;
		}
		void Capture(const MonoPrewarmKey& request, std::uint64_t now) noexcept
		{
			key = request; captured = true; unused = failed = false;
			expiresAtMs = now + kLeaseMs; stage = "preparing";
		}
		[[nodiscard]] bool Expired(std::uint64_t now) const noexcept { return captured && now >= expiresAtMs; }
		void Prepared() noexcept { unused = true; stage = "ready_at_menu"; }
		bool Adopt(const MonoPrewarmKey& request) noexcept
		{
			if (!unused) return false;
			unused = false; captured = false;
			const bool exact = key == request;
			stage = exact ? "adopted_exact_configuration" : "authoritative_configuration_changed";
			return exact;
		}
		void Cancel(bool suppress = false) noexcept
		{
			captured = unused = false; suppressed = suppress;
			stage = suppress ? "unused_lease_expired" : "cancelled";
		}
	};
}
