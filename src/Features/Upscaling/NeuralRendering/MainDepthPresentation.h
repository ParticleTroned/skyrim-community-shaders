#pragma once

#include <cstdint>
#include <limits>

namespace NeuralRendering
{
	/** Identifies the main depth resource actually reconstructed this frame. */
	struct MainDepthPresentationProof
	{
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
		std::uintptr_t resourceIdentity = 0;

		/** Native depth already uses the output grid; scaled depth needs producer proof. */
		[[nodiscard]] constexpr bool SupportsOutputLayout(
			std::uint32_t inputWidth, std::uint32_t inputHeight,
			std::uint32_t outputWidth, std::uint32_t outputHeight,
			std::uint32_t currentFrame, std::uintptr_t currentResourceIdentity) const noexcept
		{
			if (!inputWidth || !inputHeight || !outputWidth || !outputHeight ||
				inputWidth > outputWidth || inputHeight > outputHeight ||
				!currentResourceIdentity || currentFrame == std::numeric_limits<std::uint32_t>::max())
				return false;
			return (inputWidth == outputWidth && inputHeight == outputHeight) ||
			       (frame == currentFrame && resourceIdentity == currentResourceIdentity);
		}
	};
}
