#pragma once

#include "VRSubmitTemporalSnapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace VRSubmitStereoBatch
{
	template <class Proof>
	[[nodiscard]] bool IsValidDispatchProof(const Proof& a_proof) noexcept
	{
		return a_proof.valid && a_proof.frame != 0 && a_proof.serial != 0 && a_proof.path != decltype(a_proof.path){};
	}

	/** Retains a completed stereo batch and its original successful-dispatch evidence. */
	template <class Resource, class Texture, class Region, class Proof = std::nullptr_t>
	struct State
	{
		using DispatchProof = Proof;

		bool ready = false;
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
		std::uint64_t compositorCycle = 0;
		std::uint32_t generation = 0;
		std::uint32_t inputWidth = 0;
		std::uint32_t inputHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		Texture* sourceTexture = nullptr;
		std::array<Resource*, 2> colorIn{};
		std::array<Resource*, 2> depthIn{};
		std::array<Resource*, 2> motionVectorsIn{};
		std::array<Resource*, 2> reactiveMaskIn{};
		std::array<Resource*, 2> transparencyMaskIn{};
		std::array<Resource*, 2> colorOut{};
		DispatchProof dispatchProof{};

		[[nodiscard]] bool HasValidDispatchProof() const noexcept
		{
			return ready && frame != std::numeric_limits<std::uint32_t>::max() &&
			       IsValidDispatchProof(dispatchProof) && dispatchProof.frame == (frame == 0 ? 1 : frame);
		}

		[[nodiscard]] bool Matches(
			std::uint32_t a_frame,
			std::uint64_t a_compositorCycle,
			std::uint32_t a_generation,
			std::uint32_t a_inputWidth,
			std::uint32_t a_inputHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight,
			Texture* a_sourceTexture,
			const std::array<Region, 2>& a_regions) const
		{
			if (!ready || !VRSubmitTemporalSnapshot::MatchesProducer(frame, compositorCycle, a_frame, a_compositorCycle) || generation != a_generation ||
				inputWidth != a_inputWidth || inputHeight != a_inputHeight ||
				outputWidth != a_outputWidth || outputHeight != a_outputHeight ||
				sourceTexture != a_sourceTexture) {
				return false;
			}

			for (std::uint32_t eye = 0; eye < a_regions.size(); ++eye) {
				const auto& region = a_regions[eye];
				if (colorIn[eye] != region.color || depthIn[eye] != region.depth ||
					motionVectorsIn[eye] != region.motionVectors || reactiveMaskIn[eye] != region.reactiveMask ||
					transparencyMaskIn[eye] != region.transparencyCompositionMask || colorOut[eye] != region.output) {
					return false;
				}
			}
			return true;
		}

		void Record(
			std::uint32_t a_frame,
			std::uint64_t a_compositorCycle,
			std::uint32_t a_generation,
			std::uint32_t a_inputWidth,
			std::uint32_t a_inputHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight,
			Texture* a_sourceTexture,
			const std::array<Region, 2>& a_regions,
			const DispatchProof& a_dispatchProof = {})
		{
			ready = true;
			frame = a_frame;
			compositorCycle = a_compositorCycle;
			generation = a_generation;
			inputWidth = a_inputWidth;
			inputHeight = a_inputHeight;
			outputWidth = a_outputWidth;
			outputHeight = a_outputHeight;
			sourceTexture = a_sourceTexture;
			dispatchProof = a_dispatchProof;
			for (std::uint32_t eye = 0; eye < a_regions.size(); ++eye) {
				const auto& region = a_regions[eye];
				colorIn[eye] = region.color;
				depthIn[eye] = region.depth;
				motionVectorsIn[eye] = region.motionVectors;
				reactiveMaskIn[eye] = region.reactiveMask;
				transparencyMaskIn[eye] = region.transparencyCompositionMask;
				colorOut[eye] = region.output;
			}
		}
	};
}
