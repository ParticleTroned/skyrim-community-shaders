#include "Features/Upscaling/NeuralRendering/RoiDescriptor.h"

#include <limits>

int main()
{
	using namespace NeuralRendering;
	constexpr UpscalingDLSS::ViewportCrop crop{
		{ 99, 73 }, { 11, 7, 78, 56 }, { 197, 145 }, { 23, 15, 158, 114 }
	};
	static_assert(crop.MatchesEvaluationExtents(67, 49, 135, 99));
	constexpr UpscalingDLSS::Extent capacity{ 135, 99 };
	constexpr ComputeSubrect provider{ 3, 5, 113, 71 };
	constexpr ComputeSubrect support{ 7, 9, 90, 50 };
	constexpr auto roi = BuildRoiDescriptor(support, provider, capacity, true);
	static_assert(GetRoiDescriptorViolation(roi, provider, capacity).empty());
	static_assert(roi.ownedOutput == provider && roi.inferenceContext == provider);
	static_assert(roi.samplingSupport == support && roi.temporalEnvelope == provider);
	static_assert(roi.samplingSupport->Area() < roi.inferenceContext.Area());
	static_assert(roi.inferenceContext.Area() < capacity.width * capacity.height);
	// Cropped textures use local coordinates; full-eye crop origins are not added again.
	static_assert(MapComputeSubrect(roi.inferenceContext, capacity.width, capacity.height,
					  crop.input.Width(), crop.input.Height()) == ComputeSubrect{ 1, 2, 57, 36 });
	static_assert([=] {
		auto invalid = roi;
		invalid.samplingSupport = ComputeSubrect{ 2, 5, 114, 71 };
		if (GetRoiDescriptorViolation(invalid, provider, capacity).empty())
			return false;
		invalid = roi;
		invalid.inferenceContext = { 0, 0, 135, 99 };
		if (GetRoiDescriptorViolation(invalid, provider, capacity).empty())
			return false;
		invalid = roi;
		invalid.ownedOutput.width -= 1;
		if (GetRoiDescriptorViolation(invalid, provider, capacity).empty())
			return false;
		invalid = roi;
		invalid.temporalEnvelope = support;
		if (GetRoiDescriptorViolation(invalid, provider, capacity).empty())
			return false;
		invalid = roi;
		invalid.allocationCapacity.width += 1;
		if (GetRoiDescriptorViolation(invalid, provider, capacity).empty())
			return false;
		invalid = roi;
		invalid.inferenceContext.baseX = std::numeric_limits<std::uint32_t>::max();
		return !GetRoiDescriptorViolation(invalid, provider, capacity).empty();
	}());
	constexpr auto unknown = BuildRoiDescriptor(std::nullopt, provider, capacity, false);
	static_assert(GetRoiDescriptorViolation(unknown, provider, capacity).empty());
	static_assert(!unknown.samplingSupport && !unknown.temporalEnvelope);
	static_assert(!GetRoiDescriptorViolation({}, {}, capacity).empty());

	StableCharacterComputeSubrect stable{};
	for (std::uint32_t frame = 0; frame < 150; ++frame) {
		const ComputeSubrect required = frame == 1 ? ComputeSubrect{ 0, 0, 135, 99 } : support;
		const auto current = ResolveStableCharacterComputeSubrect(required, capacity.width, capacity.height, stable);
		const auto count = stable.recentCount;
		const auto cursor = stable.recentCursor;
		const auto cooldown = stable.framesSinceContraction;
		for (unsigned replay = 0; replay < 5; ++replay) {
			const auto roles = BuildRoiDescriptor(required, current, capacity, true);
			if (!GetRoiDescriptorViolation(roles, current, capacity).empty() ||
				roles.samplingSupport != required || roles.temporalEnvelope != current ||
				stable.recentCount != count || stable.recentCursor != cursor ||
				stable.framesSinceContraction != cooldown)
				return 1;
		}
	}
}
