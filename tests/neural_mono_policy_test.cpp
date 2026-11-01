#include "Features/Upscaling/NeuralRendering/MonoPolicy.h"
#include "Features/Upscaling/NeuralRendering/MemoryResolutionPolicy.h"
#include <cassert>
#include <cmath>
#include <limits>

int main()
{
	using namespace NeuralRendering;
	static_assert(AllowsNeuralFrame(true, true, true, false, false, false, false, false, false));
	for (unsigned blocked = 0; blocked != 9; ++blocked) {
		bool gate[]{ true, true, true, false, false, false, false, false, false };
		gate[blocked] = !gate[blocked];
		assert(!AllowsNeuralFrame(gate[0], gate[1], gate[2], gate[3], gate[4], gate[5], gate[6], gate[7], gate[8]));
	}
	MonoSettings settings;
	settings.intensity = std::numeric_limits<float>::quiet_NaN();
	settings.horizontalScale = std::numeric_limits<float>::infinity();
	settings.maximumDistanceMeters = -1;
	settings.regionOffset = { -4.0f, 7.0f };
	settings.modelResolutionPercent = UINT32_MAX;
	settings.categoryMask = settings.actorGroupMask = UINT32_MAX;
	settings.exposureSource = UINT32_MAX;
	Sanitize(settings);
	assert(settings.intensity == 0.8f && settings.horizontalScale == 1.0f && settings.maximumDistanceMeters == 0.0f);
	assert(settings.regionOffset[0] == -0.5f && settings.regionOffset[1] == 0.5f);
	assert(settings.modelResolutionPercent == 100 && settings.categoryMask == 0x3e && settings.actorGroupMask == 0x1f && settings.exposureSource == 2);
	assert((BuildModelResolutionExtent(1919, 1079, 30) == UpscalingDLSS::Extent{ 576, 324 }));
	assert(!BuildModelResolutionExtent(UINT32_MAX, 1080, 100).IsValid());
	assert(!BuildModelResolutionExtent(1920, 1080, 29).IsValid());
	for (unsigned width : { 1u, 127u, 128u, 1919u, 16384u }) {
		for (unsigned percent : { 25u, 55u, 100u }) {
			settings.regionPercent = percent;
			for (float offset : { -0.5f, 0.0f, 0.5f }) {
				settings.regionOffset = { offset, -offset };
				const auto region = BuildMonoRegion(width, 1080, settings);
				assert(region.Fits(width, 1080));
				if (percent == 100) assert((region == ComputeSubrect{ 0, 0, width, 1080 }));
			}
		}
	}
	CharacterFocusState focus;
	assert(ResolveCharacterFocusFade(0, 0.0f, true, false, 3, focus) == 0);
	assert(ResolveCharacterFocusFade(1, 2.0f, true, false, 3, focus) == 0);
	assert(ResolveCharacterFocusFade(4, 2.0f, true, false, 3, focus) > 0);
	const auto held = focus.fade;
	assert(ResolveCharacterFocusFade(4, 0.0f, true, false, 3, focus) == held);
	assert(ResolveCharacterFocusFade(5, std::numeric_limits<float>::quiet_NaN(), true, false, 3, focus) == 0);
	assert(CharacterFocusDistance({ 900, 500, 1020, 580 }, 1920, 1080, 0.5f, {}) == 0.0f);
	MemoryResolutionPolicy pressure;
	pressure.SetEnabled(true);
	assert(pressure.EffectivePercent(100) == 100 && pressure.OnRetired(100, 1000));
	assert(pressure.EffectivePercent(100) == 90 && pressure.EffectivePercent(50) == 50);
	MemoryBudgetSample healthy{ 16000ull * MemoryRecoveryPolicy::kMiB, 8000ull * MemoryRecoveryPolicy::kMiB, true };
	for (std::uint64_t time = 2000; time <= 32000; time += 250) {
		healthy.sampledAtMs = time;
		(void)pressure.ObserveSuccess(100, healthy, time, 0, false, true);
	}
	assert(pressure.EffectivePercent(100) == 100);
	assert(pressure.RejectRestoration(90) && pressure.EffectivePercent(100) == 90);
}
