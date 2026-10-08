#pragma once

#include <cstdint>

namespace VRHybridCullingSnapshot
{
	struct PipelineOwner
	{
		std::uintptr_t device = 0;
		std::uintptr_t context = 0;
	};

	/** Retained COM owners keep identity stable even when pipeline creation failed. */
	constexpr bool MatchesPipelineOwner(const PipelineOwner& a_retained, const PipelineOwner& a_current)
	{
		return a_retained.device != 0 && a_retained.context != 0 &&
		       a_retained.device == a_current.device && a_retained.context == a_current.context;
	}

	/** A target publication alone neither invalidates shaders nor retries a latched pipeline failure. */
	constexpr bool ShouldRecreatePipeline(const PipelineOwner& a_retained, const PipelineOwner& a_current, bool a_cacheCleared)
	{
		return a_current.device != 0 && a_current.context != 0 &&
		       (a_cacheCleared || !MatchesPipelineOwner(a_retained, a_current));
	}

	/** Identifies an observation boundary, without asserting when depth contents were written. */
	enum class Phase : std::uint8_t
	{
		Unknown,
		NativeDownscale,
		NativeReadback
	};

	enum class Convention : std::uint8_t
	{
		Standard
	};

	enum class MaskPolicy : std::uint8_t
	{
		ZeroIsUntrusted
	};

	/** Retained SRV identity and coordinated resource ownership, not a content-freshness proof. */
	struct Source
	{
		std::uintptr_t view = 0;
		std::uint64_t resourceGeneration = 0;
		std::uint32_t frame = 0;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t textureFormat = 0;
		std::uint32_t viewFormat = 0;
		std::uint32_t sampleCount = 0;
		Phase phase = Phase::Unknown;
		Convention convention = Convention::Standard;
		MaskPolicy maskPolicy = MaskPolicy::ZeroIsUntrusted;
		float nearDepth = 0.0f;
		float farDepth = 1.0f;
	};

	/** A setup invalidation is rejected even while the previous publication remains retained. */
	constexpr bool HasCurrentPublication(const Source& a_source, std::uint64_t a_generation, bool a_current)
	{
		return a_current && a_source.resourceGeneration != 0 && a_source.resourceGeneration == a_generation;
	}

	/** Prepared depth can be consumed only at its original frame and source binding. */
	constexpr bool CanDispatch(const Source& a_source, std::uint64_t a_generation, bool a_current,
		std::uint32_t a_frame, std::uintptr_t a_view)
	{
		return a_source.phase == Phase::NativeDownscale && a_source.view != 0 && a_source.view == a_view &&
		       a_source.frame == a_frame && HasCurrentPublication(a_source, a_generation, a_current);
	}

	/** Match resource provenance across the native one-frame handoff; camera validity is separate. */
	constexpr bool MatchesReadback(const Source& a_producer, const Source& a_consumer)
	{
		return a_producer.phase == Phase::NativeDownscale && a_consumer.phase == Phase::NativeReadback &&
		       a_producer.view != 0 && a_producer.view == a_consumer.view &&
		       a_producer.resourceGeneration != 0 && a_producer.resourceGeneration == a_consumer.resourceGeneration &&
		       a_consumer.frame - a_producer.frame == 1 &&
		       a_producer.width == a_consumer.width && a_producer.height == a_consumer.height &&
		       a_producer.textureFormat == a_consumer.textureFormat && a_producer.viewFormat == a_consumer.viewFormat &&
		       a_producer.sampleCount == a_consumer.sampleCount && a_producer.convention == a_consumer.convention &&
		       a_producer.maskPolicy == a_consumer.maskPolicy && a_producer.nearDepth == a_consumer.nearDepth &&
		       a_producer.farDepth == a_consumer.farDepth;
	}
}
