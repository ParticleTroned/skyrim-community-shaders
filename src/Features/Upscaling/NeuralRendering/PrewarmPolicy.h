#pragma once

#include "NativeEvaluationLayout.h"
#include "PipelinePolicy.h"

#include <array>
#include <cstdint>

namespace NeuralRendering
{
	/** Configuration eligibility is shared by execution and controls, independently of logging. */
	template <class Settings>
	[[nodiscard]] const char* PrewarmConfigurationBlocker(const Settings& settings, bool isVR) noexcept
	{
		if (settings.neuralRenderingPeripheralCompression)
			return "peripheral_compression_requires_live_geometry";
		const auto mode = ConfiguredRenderingMode(settings.neuralRenderingMode);
		if (!isVR || mode != RenderingMode::ReducedResolution ||
			settings.neuralRenderingModelResolutionPercent != 100 || settings.neuralRenderingPressureResolutionEnabled ||
			settings.neuralRenderingCentralAreaPercent != 100 || settings.neuralRenderingSingleSubrectScale != 1.0f ||
			RequiresFoveatedMask(mode, settings.neuralRenderingFovOnly, isVR, settings.neuralRenderingRenderscaleFov))
			return "exact_route_not_supported";
		if (settings.neuralCharacterRenderingEnabled || settings.neuralCharacterSceneStrengthsEnabled ||
			!settings.neuralRenderingAutoMask || settings.neuralRenderingUICorrection)
			return "creation_mask_not_available";
		return nullptr;
	}

	/** Exact creation demand; no image, history, or successful-frame identity. */
	struct PrewarmKey
	{
		NativeEvaluationLayout layout{};
		std::uintptr_t device = 0, context = 0;
		std::uint64_t generation = 0;
		std::uint32_t slotMask = 0;
		std::uint32_t networkPreset = 0;
		bool operator==(const PrewarmKey&) const = default;
	};

	/** Bounded optional ownership, separate from successful renderer use. */
	struct PrewarmPolicy
	{
		static constexpr std::uint64_t kUnusedLeaseMs = 30000;
		static constexpr std::uint64_t kRetryIntervalMs = 1000;
		PrewarmKey key{};
		const char* stage = "disabled";
		const char* reason = "disabled";
		std::uint32_t preparedMask = 0, unusedMask = 0, adoptedMask = 0, missedMask = 0;
		std::uint64_t expiresAtMs = 0, retryAtMs = 0;
		bool captured = false, cancelled = false, failed = false, backendOwned = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::uint64_t stages = 0, stageMicroseconds = 0;
		std::array<std::uint64_t, 3> stageCpuMicroseconds{};
#endif

		[[nodiscard]] bool Expired(std::uint64_t now) const noexcept
		{
			return captured && expiresAtMs != 0 && now >= expiresAtMs;
		}

		/** A compositor opportunity is independent of the world simulation frame. */
		[[nodiscard]] bool BeginOpportunity(std::uint64_t opportunity) noexcept
		{
			if (seenOpportunity_ && opportunity == lastOpportunity_)
				return false;
			seenOpportunity_ = true;
			lastOpportunity_ = opportunity;
			return true;
		}

		void Capture(const PrewarmKey& request, std::uint64_t now) noexcept
		{
			key = request;
			captured = true;
			cancelled = failed = backendOwned = false;
			preparedMask = unusedMask = adoptedMask = missedMask = 0;
			expiresAtMs = now + kUnusedLeaseMs;
			retryAtMs = 0;
			stage = "captured";
			reason = "exact_native_request";
		}

		/** Forget the request without clearing failures or releasing in-flight ownership. */
		void CancelRequest() noexcept
		{
			captured = false;
			cancelled = true;
		}

		[[nodiscard]] bool SuppressRetryAfterRetirement(std::uint64_t now) const noexcept
		{
			return captured && (failed || Expired(now));
		}

		void Adopt(std::uint32_t slot, bool compatible) noexcept
		{
			// Real resource preparation transfers backend ownership even before a model exists.
			backendOwned = false;
			const auto bit = 1u << slot;
			if (!(unusedMask & bit))
				return;
			(compatible ? adoptedMask : missedMask) |= bit;
			unusedMask &= ~bit;
			if ((adoptedMask | missedMask) == key.slotMask) {
				stage = missedMask ? "miss" : "adopted";
				reason = missedMask ? "authoritative_request_changed" : "exact_native_reuse";
			}
		}

	private:
		std::uint64_t lastOpportunity_ = 0;
		bool seenOpportunity_ = false;
	};
}
