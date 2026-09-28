#pragma once

#include "../DLSSViewportCrop.h"
#include "CharacterComputeSubrect.h"

#include <optional>
#include <string_view>

namespace NeuralRendering
{
	/** Output-crop-local texels, owned by the enclosing preparation/execution identity. */
	struct RoiDescriptor
	{
		/** Conservative, already guarded sampling enclosure; never exact mask occupancy. */
		std::optional<ComputeSubrect> samplingSupport;
		/** Exclusive private-output copy domain; the exact character mask selects final pixels. */
		ComputeSubrect ownedOutput{};
		/** Dense native evaluation domain, including spatial context and retained headroom. */
		ComputeSubrect inferenceContext{};
		/** Retained spatial envelope, independent of whether native temporal history resets. */
		std::optional<ComputeSubrect> temporalEnvelope;
		/** Full output resource extent; neither useful support nor evaluated area. */
		UpscalingDLSS::Extent allocationCapacity{};
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Current context may be smaller than its retained envelope in the C experiment. */
		bool currentContextApplied = false;
#endif

		bool operator==(const RoiDescriptor&) const = default;
	};

	/** Adapts the existing disjoint provider policy without adding padding or aging history. */
	[[nodiscard]] inline constexpr RoiDescriptor BuildRoiDescriptor(
		std::optional<ComputeSubrect> a_samplingSupport,
		const ComputeSubrect& a_provider, UpscalingDLSS::Extent a_capacity,
		bool a_hasTemporalEnvelope) noexcept
	{
		return {
			.samplingSupport = a_samplingSupport,
			.ownedOutput = a_provider,
			.inferenceContext = a_provider,
			.temporalEnvelope = a_hasTemporalEnvelope ? std::optional{ a_provider } : std::nullopt,
			.allocationCapacity = a_capacity,
		};
	}

	/** Rejects role drift before allocation; overlapping read halos remain unqualified. */
	[[nodiscard]] inline constexpr std::string_view GetRoiDescriptorViolation(
		const RoiDescriptor& a_roi, const ComputeSubrect& a_provider,
		UpscalingDLSS::Extent a_capacity) noexcept
	{
		if (a_roi.allocationCapacity != a_capacity ||
			!a_roi.inferenceContext.Fits(a_capacity.width, a_capacity.height))
			return "ROI inference context exceeds or mismatches its allocation capacity";
		if (a_roi.inferenceContext != a_provider || a_roi.ownedOutput != a_provider)
			return "ROI ownership and inference must retain the disjoint provider rectangle";
		if (a_roi.samplingSupport && !ContainsComputeSubrect(a_roi.ownedOutput, *a_roi.samplingSupport))
			return "ROI sampling support exceeds its owned output";
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_roi.currentContextApplied) {
			if (!a_roi.samplingSupport || !a_roi.temporalEnvelope ||
				!a_roi.temporalEnvelope->Fits(a_capacity.width, a_capacity.height) ||
				!ContainsComputeSubrect(*a_roi.temporalEnvelope, a_roi.inferenceContext))
				return "Experimental current context requires proven support and a containing temporal envelope";
		} else
#endif
			if (a_roi.temporalEnvelope && *a_roi.temporalEnvelope != a_roi.inferenceContext)
			return "ROI temporal envelope must retain the evaluated provider rectangle";
		return {};
	}
}
