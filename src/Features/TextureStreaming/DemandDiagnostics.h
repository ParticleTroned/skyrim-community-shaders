#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "GeometryDemand.h"
#	include <cmath>
#	include <string>
#	include <string_view>
namespace StreamingTextures
{
	/** Bounded developer observations of projected-bound demand after consumer validation. */
	struct DemandDiagnostics
	{
		static constexpr std::array names{ std::string_view("invalidDemand"), std::string_view("fullDetailDemand"), std::string_view("reducibleDemand") };
		struct Sample
		{
			double requiredEdge = 0, boundRadius = 0;
			std::array<float, 3> boundCenter{};
			std::uint32_t fullWidth = 0, fullHeight = 0, mipLevels = 0, desiredDrop = 0, currentFrame = 0;
			std::size_t consumers = 0;
			DemandContext context;
		};
		std::array<std::uint64_t, names.size()> reasons{};
		std::array<Sample, names.size()> samples{};
		std::array<std::string, names.size()> texturePaths{}, geometryNames{};

		void Observe(const Sample& sample, std::string_view texturePath, std::string_view geometryName)
		{
			const std::size_t reason = !std::isfinite(sample.requiredEdge) || sample.requiredEdge <= 0 ? 0 : sample.desiredDrop ? 2 :
			                                                                                                                      1;
			if (!reasons[reason]++) {
				samples[reason] = sample;
				texturePaths[reason] = texturePath.substr(0, 256);
				geometryNames[reason] = geometryName.substr(0, 128);
			}
		}
	};
}
#endif
