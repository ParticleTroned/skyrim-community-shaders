#include "Features/Upscaling/NeuralRendering/MemoryResolutionPolicy.h"
#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	NeuralRendering::MemoryBudgetSample ResolutionSample(std::uint64_t time)
	{
		using NeuralRendering::MemoryRecoveryPolicy;
		return { 16384 * MemoryRecoveryPolicy::kMiB, 8192 * MemoryRecoveryPolicy::kMiB, true, 0, false, time };
	}

	void CheckMemoryResolution()
	{
		using namespace NeuralRendering;
		MemoryResolutionPolicy reset;
		reset.MarkResourcesRetired(1000);
		Require(!reset.retired && reset.EffectivePercent(100) == 100, "Disabled retirement changed adaptive state");
		reset.SetEnabled(true);
		reset.MarkResourcesRetired(1000);
		Require(reset.retired && reset.EffectivePercent(100) == 100 && reset.reductions == 0,
			"An ordinary reset lowered quality without pressure");
		Require(!reset.WhileWaiting(100, ResolutionSample(5999), 5999, true), "Reset skipped the blocked-admission dwell");
		Require(!reset.WhileWaiting(100, ResolutionSample(6000), 6000, false), "Reset lowered quality despite healthy admission");
		Require(reset.WhileWaiting(100, ResolutionSample(6000), 6000, true) && reset.EffectivePercent(100) == 90,
			"Retired reset capacity prevented a smaller blocked retry");

		MemoryResolutionPolicy policy;
		Require(!policy.enabled && policy.EffectivePercent(67) == 67, "Automatic reduction must be opt-in");
		Require(!policy.OnRetired(67, 0) && !policy.WhileWaiting(67, ResolutionSample(5000), 5000, true),
			"Disabled adaptation changed the requested quality");
		for (std::uint32_t requested = 30; requested <= 100; ++requested) {
			MemoryResolutionPolicy item;
			item.SetEnabled(true);
			Require(item.EffectivePercent(requested) == requested, "Enabling adaptation changed quality without pressure");
			Require(item.OnRetired(requested, 0) == (requested > 30), "Retirement changed an unexpected scale");
			Require(item.EffectivePercent(requested) == std::max(30u, requested - 10), "Reduction ignored the actual requested scale");
			for (std::uint64_t time = 5000; time <= 50000; time += 5000)
				(void)item.WhileWaiting(requested, ResolutionSample(time), time, true);
			Require(item.EffectivePercent(requested) == 30, "Repeated pressure escaped the minimum model resolution");
			Require(!item.WhileWaiting(requested, ResolutionSample(55000), 55000, true), "Minimum scale reported another reduction");
		}
		policy.SetEnabled(true);
		for (const auto invalid : { 0u, 29u, 101u, std::numeric_limits<std::uint32_t>::max() }) {
			Require(policy.EffectivePercent(invalid) == invalid && !policy.OnRetired(invalid, 0),
				"Adaptive policy concealed invalid settings from renderer validation");
		}
		Require(!policy.WhileWaiting(100, ResolutionSample(5000), 5000, true), "Reduced capacity before retirement");
		Require(policy.OnRetired(100, 1000) && policy.EffectivePercent(100) == 90, "Safe retirement did not select a smaller retry");
		Require(!policy.WhileWaiting(100, ResolutionSample(5999), 5999, true), "Waiting reduction ignored its dwell");
		Require(!policy.WhileWaiting(100, ResolutionSample(6000), 6000, false), "Healthy admission reduced quality");
		Require(!policy.WhileWaiting(100, ResolutionSample(5499), 6000, true), "Stale sample lowered the waiting scale");
		Require(!policy.WhileWaiting(100, ResolutionSample(6001), 6000, true), "Future sample lowered the waiting scale");
		Require(policy.WhileWaiting(100, ResolutionSample(6000), 6000, true) && policy.EffectivePercent(100) == 80,
			"Blocked fresh admission did not select the next smaller retry");
		Require(!policy.WhileWaiting(100, ResolutionSample(6000), 6000, true), "One waiting sample reduced multiple steps");
		Require(!policy.WhileWaiting(100, ResolutionSample(5999), 5999, true), "Clock reversal skipped waiting dwell");
		Require(policy.EffectivePercent(67) == 67, "Pressure ceiling exceeded a lower requested setting");
		Require(policy.OnRetired(67, 7000) && policy.EffectivePercent(67) == 57, "Pressure step used the ceiling instead of actual quality");
		policy.SetEnabled(false);
		Require(policy.EffectivePercent(67) == 67 && policy.ceilingPercent == 100 && !policy.healthyWindow,
			"Disabling adaptation retained its quality override");
		policy.SetEnabled(true);
		Require(policy.EffectivePercent(100) == 100, "Explicit re-enable retained the preceding pressure episode");

		Require(policy.OnRetired(67, 0), "Restoration setup failed");
		for (std::uint64_t time = 0; time < MemoryResolutionPolicy::kRestoreWindowMs; time += 250)
			Require(!policy.ObserveSuccess(67, ResolutionSample(time), time, 0, false, true), "Quality restored before sustained active headroom");
		Require(policy.ObserveSuccess(67, ResolutionSample(30000), 30000, 0, false, true) && policy.EffectivePercent(67) == 67,
			"Sustained active headroom did not restore the requested quality");
		Require(!policy.ObserveSuccess(67, ResolutionSample(30250), 30250, 0, false, true), "Restoration exceeded requested quality");

		MemoryResolutionPolicy rejected;
		rejected.SetEnabled(true);
		Require(rejected.OnRetired(100, 0), "Rejected restoration setup failed");
		for (std::uint64_t time = 0; time < 30000; time += 250)
			(void)rejected.ObserveSuccess(100, ResolutionSample(time), time, 0, false, true);
		Require(rejected.ObserveSuccess(100, ResolutionSample(30000), 30000, 0, false, true) && rejected.EffectivePercent(100) == 100,
			"Healthy restoration did not propose a larger candidate");
		const MemoryBudgetSample replacementPeak{ 8192 * MemoryRecoveryPolicy::kMiB, 5500 * MemoryRecoveryPolicy::kMiB, true, 0, false, 30250 };
		MemoryRecoveryPolicy workingRecovery;
		auto preview = workingRecovery;
		Require(!preview.Admit(replacementPeak, 2048 * MemoryRecoveryPolicy::kMiB, 30250), "Full replacement peak unexpectedly fit");
		Require(workingRecovery.phase == MemoryRecoveryPhase::Ready && workingRecovery.suspensions == 0,
			"Candidate preflight suspended the working recovery policy");
		Require(rejected.RejectRestoration(90) && rejected.EffectivePercent(100) == 90 && !rejected.healthyWindow,
			"Rejected restoration did not retain working quality");
		Require(workingRecovery.Admit(replacementPeak, 0, 30250) && workingRecovery.phase == MemoryRecoveryPhase::Ready,
			"Rejected growth prevented the healthy existing model from rendering");
		for (std::uint64_t time = 30500; time < 60500; time += 250)
			Require(!rejected.ObserveSuccess(100, ResolutionSample(time), time, 0, false, true), "Rejected growth retried without earning another healthy window");
		Require(rejected.ObserveSuccess(100, ResolutionSample(60500), 60500, 0, false, true), "Rejected growth could never be reconsidered");
		for (const auto invalid : { 0u, 29u, 100u, 101u, std::numeric_limits<std::uint32_t>::max() })
			Require(!rejected.RejectRestoration(invalid) && rejected.EffectivePercent(100) == 100,
				"Invalid restoration rejection changed quality");
		rejected.SetEnabled(false);
		Require(!rejected.RejectRestoration(90) && rejected.EffectivePercent(100) == 100, "Disabled restoration changed requested quality");

		MemoryResolutionPolicy delayed;
		delayed.SetEnabled(true);
		Require(delayed.OnRetired(100, 0), "Delayed sample test setup failed");
		Require(!delayed.ObserveSuccess(100, ResolutionSample(0), 500, 0, false, true) && delayed.healthySinceMs == 500,
			"A budget sample predating successful rendering counted as active recovery");
		for (std::uint64_t time = 750; time < 30500; time += 250)
			Require(!delayed.ObserveSuccess(100, ResolutionSample(time == 750 ? 500 : time), time, 0, false, true), "Delayed sample restored quality before active continuity");
		Require(delayed.ObserveSuccess(100, ResolutionSample(30500), 30500, 0, false, true), "Delayed sample continuity never restored quality");
		MemoryResolutionPolicy gradual;
		gradual.SetEnabled(true);
		Require(gradual.OnRetired(100, 0) && gradual.OnRetired(100, 1), "Gradual restoration setup failed");
		for (std::uint64_t time = 0; time <= 60000; time += 250) {
			const bool changed = gradual.ObserveSuccess(100, ResolutionSample(time), time, 0, false, true);
			Require(changed == (time == 30000), "Restoration reused a previous healthy window");
		}
		Require(gradual.EffectivePercent(100) == 90 && gradual.ObserveSuccess(100, ResolutionSample(60250), 60250, 0, false, true) &&
					gradual.EffectivePercent(100) == 100,
			"Each restoration step must earn a complete new healthy window");

		for (std::uint32_t blocker = 0; blocker < 8; ++blocker) {
			MemoryResolutionPolicy interrupted;
			interrupted.SetEnabled(true);
			Require(interrupted.OnRetired(100, 0), "Interrupted restoration setup failed");
			for (std::uint64_t time = 0; time <= 15000; time += 250)
				Require(!interrupted.ObserveSuccess(100, ResolutionSample(time), time, 0, false, true), "Restoration advanced early");
			auto blocked = ResolutionSample(15250);
			if (blocker == 0)
				blocked.valid = false;
			if (blocker == 1)
				blocked.usageBytes = blocked.budgetBytes * 3 / 4;
			if (blocker == 2) {
				blocked.budgetBytes = 4096 * MemoryRecoveryPolicy::kMiB;
				blocked.usageBytes = 2560 * MemoryRecoveryPolicy::kMiB;
			}
			if (blocker == 3)
				blocked.sampledAtMs = 14749;
			if (blocker == 4)
				blocked.sampledAtMs = 15251;
			Require(!interrupted.ObserveSuccess(100, blocked, 15250, blocker == 5 ? 1 : 0, blocker == 6, blocker != 7) &&
						!interrupted.healthyWindow,
				"Invalid, pressured, reserved, conserving or recovering input did not break restoration continuity");
			for (std::uint64_t time = 15500; time < 45500; time += 250)
				Require(!interrupted.ObserveSuccess(100, ResolutionSample(time), time, 0, false, true), "Interrupted headroom counted toward restoration");
			Require(interrupted.ObserveSuccess(100, ResolutionSample(45500), 45500, 0, false, true), "Fresh restored continuity never recovered quality");
		}

		MemoryResolutionPolicy gaps;
		gaps.SetEnabled(true);
		Require(gaps.OnRetired(100, 0), "Gap test setup failed");
		for (std::uint64_t time = 0; time <= 15000; time += 250)
			(void)gaps.ObserveSuccess(100, ResolutionSample(time), time, 0, false, true);
		Require(!gaps.ObserveSuccess(100, ResolutionSample(20000), 20000, 0, false, true) && gaps.healthySinceMs == 20000,
			"Inactive rendering counted toward the restoration window");
		for (std::uint64_t time = 20001; time <= 20500; ++time)
			Require(!gaps.ObserveSuccess(100, ResolutionSample(20000), time, 0, false, true), "Repeated samples advanced restoration");
		Require(gaps.lastHealthySampleMs == 20000, "Duplicate budget samples gained observation time");
		Require(!gaps.ObserveSuccess(100, ResolutionSample(19999), 20001, 0, false, true) && gaps.healthySinceMs == 20001,
			"Reversed observation time retained an old healthy window");
		gaps.ClearHealthyWindow();
		Require(!gaps.healthyWindow && gaps.EffectivePercent(100) == 90, "History reset erased pressure quality memory");
	}
	void CheckGeometry()
	{
		using namespace NeuralRendering;
		using UpscalingDLSS::Extent;
		using UpscalingDLSS::ViewportCrop;
		constexpr ViewportCrop crop{
			.fullInput = { 200, 120 },
			.input = { 30, 20, 131, 87 },
			.fullOutput = { 200, 120 },
			.output = { 30, 20, 131, 87 }
		};
		constexpr Extent source{ 101, 67 };
		constexpr ComputeSubrect region{ 7, 5, 21, 13 };
		struct Case
		{
			std::uint32_t percent;
			Extent model;
			ComputeSubrect modelRegion;
		};
		for (const auto& item : { Case{ 30, { 31, 21 }, { 2, 1, 7, 5 } }, Case{ 33, { 34, 23 }, { 2, 1, 8, 6 } },
				 Case{ 50, { 51, 34 }, { 3, 2, 12, 8 } },
				 Case{ 67, { 68, 45 }, { 4, 3, 15, 10 } },
				 Case{ 100, { 101, 67 }, region } }) {
			const auto geometry = BuildModelResolutionGeometry(crop, source, region, item.percent);
			Require(geometry.has_value(), "Valid offset FOV model geometry was rejected");
			Require(geometry->sourceSize == source && geometry->sourceRegion == region, "Projection changed the source region");
			Require(geometry->modelSize == item.model && geometry->modelRegion == item.modelRegion, "Projection lost outward ROI coverage");
			Require(geometry->nativeCrop.IsIdentity() && geometry->nativeCrop.fullInput == item.model &&
						geometry->nativeCrop.fullOutput == item.model,
				"Native evaluation must use one identity model grid");
			for (float normalizedMotion : { -0.02f, 0.0f, 0.0125f }) {
				const float expectedX = normalizedMotion * crop.fullInput.width * item.model.width / source.width;
				const float expectedY = normalizedMotion * crop.fullInput.height * item.model.height / source.height;
				const float projectedX = normalizedMotion * geometry->motionNormalization[0] * item.model.width;
				const float projectedY = normalizedMotion * geometry->motionNormalization[1] * item.model.height;
				Require(std::abs(projectedX - expectedX) < 1e-6f && std::abs(projectedY - expectedY) < 1e-6f,
					"Motion normalization converted full-eye pixel motion more than once");
			}
		}
		constexpr ViewportCrop upscaledCrop{
			.fullInput = { 100, 60 },
			.input = { 15, 10, 66, 44 },
			.fullOutput = { 200, 120 },
			.output = { 30, 20, 131, 87 }
		};
		const auto upscaled = BuildModelResolutionGeometry(upscaledCrop, source, region, 30, { 51, 34 });
		Require(upscaled && upscaled->modelSize == Extent{ 31, 21 } && upscaled->modelGuideSize == Extent{ 16, 11 } &&
					upscaled->nativeCrop.MatchesEvaluationExtents(16, 11, 31, 21),
			"Lower-resolution guides rejected final-scene scaling");
		Require(upscaled->motionNormalization == std::array{ 100.0f / 51.0f, 60.0f / 34.0f }, "Guide motion used the colour normalization");
		Require(!BuildModelResolutionGeometry(upscaledCrop, source, region, 50, { 52, 34 }), "Mismatched guide extent was admitted");
		Require(!BuildModelResolutionGeometry(upscaledCrop, source, region, 50, { 0, 34 }), "Incomplete guide extent was admitted");
		const ModelResolutionHistory history{ upscaledCrop, 30 };
		Require(history == ModelResolutionHistory{ upscaledCrop, 30 }, "Continuous model input changed history identity");
		Require(history != ModelResolutionHistory{ upscaledCrop, 31 }, "Scale change retained temporal identity");
		auto movedCrop = upscaledCrop;
		++movedCrop.input.left;
		++movedCrop.input.right;
		Require(history != ModelResolutionHistory{ movedCrop, 30 }, "Moving the original crop retained temporal identity");
		const auto full = BuildModelResolutionGeometry(ViewportCrop::Identity(101, 67, 101, 67), source, { 0, 0, 101, 67 }, 67);
		Require(full && full->motionNormalization == std::array{ 1.0f, 1.0f }, "Uncropped views must retain normalized motion");

		Require(!BuildModelResolutionGeometry(crop, { 100, 67 }, region, 67), "Mismatched evaluation extent was admitted");
		for (const auto invalid : { ComputeSubrect{}, ComputeSubrect{ 100, 0, 2, 1 }, ComputeSubrect{ 0, 66, 1, 2 },
				 ComputeSubrect{ std::numeric_limits<std::uint32_t>::max(), 0, 1, 1 } })
			Require(!BuildModelResolutionGeometry(crop, source, invalid, 67), "Out-of-bounds source ROI was admitted");
		auto invalidCrop = crop;
		invalidCrop.fullInput.width = 16385;
		invalidCrop.fullOutput.width = 16385;
		Require(!BuildModelResolutionGeometry(invalidCrop, source, region, 67), "Oversized full-eye coordinates were admitted");
		invalidCrop = crop;
		invalidCrop.fullOutput.width = 201;
		Require(!BuildModelResolutionGeometry(invalidCrop, source, region, 67), "Unequal input/output domains were admitted");
		invalidCrop = crop;
		++invalidCrop.output.left;
		++invalidCrop.output.right;
		Require(!BuildModelResolutionGeometry(invalidCrop, source, region, 67), "Mismatched input/output crop origins were admitted");
		for (std::uint32_t percent : { 0u, 29u, 101u, std::numeric_limits<std::uint32_t>::max() })
			Require(!BuildModelResolutionGeometry(crop, source, region, percent), "Invalid model scale reached projected geometry");
	}

	void CheckCentralAreaGeometry()
	{
		using namespace NeuralRendering;
		const auto crop = UpscalingDLSS::ViewportCrop::Identity(1024, 768, 1024, 768);
		CentralArea area{};
		Require(area.Valid() && !area.Active() && BuildCentralAreaSupport(crop, area) == ComputeSubrect{ 0, 0, 1024, 768 },
			"Default central area must retain the complete route");
		area = { 25, 0, 1.0f, {}, { 1024, 768 } };
		Require(BuildCentralAreaSupport(crop, area) == ComputeSubrect{ 384, 288, 256, 192 },
			"Hard central mask support changed its shared FOV shape extents");
		const auto hard = BuildCentralAreaSupport(crop, area);
		area.featherPixels = 64;
		const auto feathered = BuildCentralAreaSupport(crop, area);
		Require(feathered.baseX < hard.baseX && feathered.baseY < hard.baseY && feathered.Area() > hard.Area(),
			"Feather support was omitted from the inference bounds");
		area.finalOutput = { 2048, 1536 };
		Require(BuildCentralAreaSupport(crop, area).Area() < feathered.Area(),
			"Feather pixels must use output dimensions rather than the NR grid");
		area.horizontalScale = 2;
		area.offset = { -0.4f, 0.4f };
		Require(BuildCentralAreaSupport(crop, area).Fits(1024, 768), "Shifted and stretched mask escaped the eye");
		for (const auto percent : { 0u, 24u, 101u, std::numeric_limits<std::uint32_t>::max() }) {
			auto invalid = area;
			invalid.percent = percent;
			Require(!invalid.Valid() && !BuildCentralAreaSupport(crop, invalid).IsValid(), "Invalid central percentage reached support geometry");
		}
		for (const auto extent : { UpscalingDLSS::Extent{ 0, 768 }, UpscalingDLSS::Extent{ 1024, 0 }, UpscalingDLSS::Extent{ 16385, 768 } }) {
			auto invalid = area;
			invalid.finalOutput = extent;
			Require(!invalid.Valid(), "Invalid feather reference dimensions reached shaders");
		}
		for (const auto value : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
			auto invalid = area;
			invalid.offset[0] = value;
			Require(!invalid.Valid(), "Nonfinite centre reached the shader");
			invalid = area;
			invalid.horizontalScale = value;
			Require(!invalid.Valid(), "Nonfinite horizontal scale reached the shader");
		}
		area.featherPixels = 257;
		Require(!area.Valid(), "Out-of-range feather reached support geometry");
	}

}

int main()
{
	try {
		using namespace NeuralRendering;
		for (std::uint32_t percent = 0; percent <= 150; ++percent) {
			const bool valid = percent >= 30 && percent <= 100;
			Require(IsValidModelResolutionPercent(percent) == valid, "Model scale validation changed its supported interval");
			for (auto mode : { RenderingMode::FullResolution, RenderingMode::Foveated, RenderingMode::ReducedResolution })
				Require(EffectiveModelResolutionPercent(mode, percent) == (valid ? percent : 100), "Rendering route lost a valid model scale");
		}
		const auto maximum = std::numeric_limits<std::uint32_t>::max();
		Require(!IsValidModelResolutionPercent(maximum), "Unbounded model scale was admitted");
		Require(EffectiveModelResolutionPercent(static_cast<RenderingMode>(maximum), 50) == 100, "Unknown rendering mode applied model reduction");
		for (const auto dimensions : { std::array{ 1u, 1u }, std::array{ 101u, 67u }, std::array{ 2448u, 2448u }, std::array{ 16384u, 16384u } }) {
			for (std::uint32_t percent : { 30u, 33u, 50u, 67u, 75u, 100u }) {
				const auto extent = BuildModelResolutionExtent(dimensions[0], dimensions[1], percent);
				Require(extent.width > 0 && extent.height > 0, "Valid model extent became empty");
				Require(extent.width <= dimensions[0] && extent.height <= dimensions[1], "Model extent exceeded its source");
				Require(std::uint64_t(extent.width) * 100 >= std::uint64_t(dimensions[0]) * percent &&
							std::uint64_t(extent.width - 1) * 100 < std::uint64_t(dimensions[0]) * percent,
					"Model width does not conservatively round up");
				Require(std::uint64_t(extent.height) * 100 >= std::uint64_t(dimensions[1]) * percent &&
							std::uint64_t(extent.height - 1) * 100 < std::uint64_t(dimensions[1]) * percent,
					"Model height does not conservatively round up");
			}
		}
		for (const auto dimensions : { std::array{ 0u, 1u }, std::array{ 1u, 0u }, std::array{ 16385u, 100u }, std::array{ 100u, 16385u }, std::array{ maximum, maximum } }) {
			const auto extent = BuildModelResolutionExtent(dimensions[0], dimensions[1], 50);
			Require(extent.width == 0 && extent.height == 0, "Invalid source dimensions reached model allocation");
		}
		for (std::uint32_t percent : { 0u, 29u, 101u, maximum }) {
			const auto extent = BuildModelResolutionExtent(100, 100, percent);
			Require(extent.width == 0 && extent.height == 0, "Invalid model scale reached allocation");
		}
		CheckMemoryResolution();
		CheckGeometry();
		CheckCentralAreaGeometry();
		std::cout << "PASS: NR model scale boundaries, all routes, conservative extents and overflow rejection\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
