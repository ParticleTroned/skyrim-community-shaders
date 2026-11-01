#pragma once

#include "ModelResolutionPolicy.h"
#include "NativeEvaluationLayout.h"
#include "PipelinePolicy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace NeuralRendering
{
	/** Configuration eligibility is shared by execution and controls, independently of logging. */
	template <class Settings>
	[[nodiscard]] const char* PrewarmConfigurationBlocker(const Settings& settings, bool isVR) noexcept
	{
		if (!IsValidModelResolutionPercent(settings.neuralRenderingModelResolutionPercent) ||
			settings.neuralRenderingPeripheralCompressionPercent > kMaximumPeripheralCompressionPercent)
			return "invalid_model_geometry_policy";
		const auto mode = ConfiguredRenderingMode(settings.neuralRenderingMode);
		if (!isVR || mode != RenderingMode::ReducedResolution ||
			settings.neuralRenderingCentralAreaPercent != 100 || settings.neuralRenderingSingleSubrectScale != 1.0f ||
			RequiresFoveatedMask(mode, settings.neuralRenderingFovOnly, isVR, settings.neuralRenderingRenderscaleFov))
			return "exact_route_not_supported";
		if (settings.neuralCharacterRenderingEnabled || settings.neuralCharacterSceneStrengthsEnabled ||
			!settings.neuralRenderingAutoMask || settings.neuralRenderingUICorrection)
			return "creation_mask_not_available";
		return nullptr;
	}

	/** Authoritative full-view source geometry; the renderer resolves the effective model policy. */
	struct PrewarmRequest
	{
		UpscalingDLSS::Extent source{};
		std::uintptr_t device = 0, context = 0;
		std::uint64_t generation = 0;
		std::uint32_t slotMask = 0, networkPreset = 0;
		std::uint32_t modelResolutionPercent = 100, peripheralCompressionPercent = 0;
		bool pressureResolutionEnabled = false;
	};

	/** Exact creation demand; no image, history, or successful-frame identity. */
	struct PrewarmKey
	{
		NativeEvaluationLayout layout{};
		std::uintptr_t device = 0, context = 0;
		std::uint64_t generation = 0;
		std::uint32_t slotMask = 0;
		std::uint32_t networkPreset = 0;
		UpscalingDLSS::Extent source{};
		bool modelProjection = false;
		bool operator==(const PrewarmKey&) const = default;
	};

	/** Conservative stereo transport overlap, including source storage and any model proxies. */
	[[nodiscard]] inline std::optional<std::uint64_t> PrewarmTransportBytes(const PrewarmKey& key, UpscalingDLSS::Extent colorWork) noexcept
	{
		const auto valid = [](UpscalingDLSS::Extent extent) {
			return extent.IsValid() && extent.width <= kMaximumNeuralImageDimension && extent.height <= kMaximumNeuralImageDimension;
		};
		if (!valid(key.source) || !valid(colorWork) || !valid(key.layout.color.backing) ||
			!valid(key.layout.creation.input) || !valid(key.layout.creation.output))
			return std::nullopt;
		const auto pixels = [](UpscalingDLSS::Extent extent) { return std::uint64_t(extent.width) * extent.height; };
		const auto nativePixels = std::max({ pixels(key.layout.color.backing),
			pixels(key.layout.creation.input), pixels(key.layout.creation.output) });
		// Bound RGBA32 colour/output, R32 depth and RG32 motion, without assuming proxy sharing.
		constexpr std::uint64_t transportBytesPerPixel = 16 + 4 + 8 + 16;
		const auto sourcePixels = pixels(key.source);
		const auto proxies = key.modelProjection ? nativePixels * transportBytesPerPixel + sourcePixels * 16 : 0;
		return 2 * ((sourcePixels + nativePixels) * transportBytesPerPixel + proxies + pixels(colorWork) * 32);
	}

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
