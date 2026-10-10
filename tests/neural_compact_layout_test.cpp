#include "Features/Upscaling/NeuralRendering/CompactInputLayout.h"
#include <limits>

#define CHECK(condition)     \
	do {                     \
		if (!(condition))    \
			return __LINE__; \
	} while (false)

int main()
{
	using namespace NeuralRendering;
	for (const auto rect : { ComputeSubrect{ 400, 600, 100, 120 }, ComputeSubrect{ 0, 0, 256, 256 },
			 ComputeSubrect{ 500, 700, 400, 200 }, ComputeSubrect{ 600, 600, 200, 500 } }) {
		auto roi = BuildRoiDescriptor(rect, rect, { 1008, 1120 }, true);
		const auto baseline = BuildNativeEvaluationLayout({ 1008, 1120 }, { 1008, 1120 }, { 1008, 1120 }, {}, rect,
			{ .valid = true, .x = 1008, .y = 1120 }, false);
		const auto compact = BuildCompactInputLayout(roi, baseline);
		CHECK(compact && compact->source.Fits(1008, 1120));
		CHECK(ContainsComputeSubrect(compact->source, rect));
		CHECK(compact->native.creation.input == compact->native.creation.output);
		CHECK(compact->native.motionVectorScale == baseline.motionVectorScale);
		CHECK(compact->roi.ownedOutput.baseX + compact->source.baseX == rect.baseX);
		CHECK(compact->roi.ownedOutput.baseY + compact->source.baseY == rect.baseY);
		CHECK(compact->roi.ownedOutput.Area() == rect.Area());
		CHECK(compact->native.output.valid.Area() == compact->source.Area());
		CHECK(GetNativeEvaluationLayoutViolation(compact->native, false).empty());
		CHECK(compact->roi.compactSource == compact->source && !compact->roi.temporalEnvelope);
		const auto retained = BuildCompactInputLayout(roi, baseline, 768);
		CHECK(retained && retained->source.width == 768 && retained->source.height == 768);
		CHECK(retained->roi.ownedOutput.Area() == rect.Area());
		CHECK(ContainsComputeSubrect(retained->source, rect));
		CHECK(!BuildCompactInputLayout(roi, baseline, 128));
		CHECK(!BuildCompactInputLayout(roi, baseline, UINT32_MAX));
		auto invalid = baseline;
		invalid.depth.backing.width /= 2;
		CHECK(!BuildCompactInputLayout(roi, invalid));
		invalid = baseline;
		invalid.controlMask = baseline.output;
		CHECK(!BuildCompactInputLayout(roi, invalid));
		roi.samplingSupport = ComputeSubrect{ 999, 999, 100, 100 };
		CHECK(!BuildCompactInputLayout(roi, baseline));
	}
	const auto large = ComputeSubrect{ 0, 0, 800, 800 };
	CHECK(!BuildCompactInputLayout(BuildRoiDescriptor(large, large, { 1008, 1120 }, true),
		BuildNativeEvaluationLayout({ 1008, 1120 }, { 1008, 1120 }, { 1008, 1120 }, {}, large, { .valid = true, .x = 1008, .y = 1120 }, false)));
	const auto choose = [](const CompactInputRetention& state, ComputeSubrect rect) {
		return state.Select(BuildRoiDescriptor(rect, rect, { 1008, 1120 }, true),
			BuildNativeEvaluationLayout({ 1008, 1120 }, { 1008, 1120 }, { 1008, 1120 }, {}, rect,
				{ .valid = true, .x = 1008, .y = 1120 }, false));
	};
	CompactInputRetention retention;
	const ComputeSubrect small{ 224, 512, 128, 128 };
	const ComputeSubrect current{ 192, 448, 768, 512 };
	const ComputeSubrect pending{ 64, 0, 896, 1120 };
	for (unsigned frame = 0; frame < 100; ++frame) {
		CHECK(!choose(retention, pending));
		retention.Commit(std::nullopt);
	}
	CHECK(!retention.fullCoordinates && retention.minimumSide == 0);
	const auto first = choose(retention, small);
	CHECK(first && first->source.width == 256);
	retention.Commit(first->source);
	// Speculative candidates and failed allocations cannot change retention.
	CHECK(!choose(retention, pending) && choose(retention, small));
	const auto grown = choose(retention, current);
	CHECK(grown && grown->source.width == 768);
	retention.Commit(grown->source);
	CHECK(choose(retention, small)->source.width == 768);
	CHECK(!choose(retention, pending));
	retention.Commit(std::nullopt);
	for (unsigned frame = 0; frame < 100; ++frame)
		CHECK(!choose(retention, frame % 2 ? current : pending));
	CHECK(!choose(retention, small));
	CompactInputRetention otherSlot;
	CHECK(choose(otherSlot, small));
	retention = {};
	CHECK(choose(retention, small)->source.width == 256);
	return 0;
}
