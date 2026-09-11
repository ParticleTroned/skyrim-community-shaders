#include "Features/OCUEffectFoveationPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using ocu_effect_foveation::Snapshot;
using namespace ocu_effect_foveation;
using OCUEffectFoveation::FreshnessPolicy;

Snapshot Active(std::uint64_t frame = 1)
{
	Snapshot s{};
	s.structSize = sizeof(s);
	s.version = Version;
	s.mode = Mode::EyeTracked;
	s.shape = Shape::UVRadialHalfExtent;
	s.frameId = frame;
	s.publicationQpc = 1000000;
	s.qpcFrequency = 1000000;
	s.centerUV[0][0] = .35f;
	s.centerUV[0][1] = .45f;
	s.centerUV[1][0] = .6f;
	s.centerUV[1][1] = .55f;
	s.innerRadius = .6f;
	s.midRadius = .8f;
	return s;
}

int main()
{
	int checks = 0;
	auto require = [&](bool condition, const char* label) {
		++checks;
		if (!condition)
			throw std::runtime_error(label);
	};
	try {
		FreshnessPolicy fresh;
		auto initial = fresh.Consume(Active(), 1000000, 1000000);
		require(initial.Active(), "Valid zero-age gaze must activate immediately");
		require(initial.policy[3] == 1.0f, "Moving gaze selects native-position temporal sampling");
		{
			FreshnessPolicy fixed;
			auto profile = Active();
			profile.mode = Mode::Fixed;
			require(fixed.Consume(profile, 1000000, 1000000).policy[3] == 0.0f,
				"Fixed sampling must not opt into the moving-eye estimator");
		}
		require(initial.centers[0] == .35f && initial.centers[2] == .6f && initial.policy[0] == .6f && initial.policy[1] == .8f,
			"Must use both eyes and the supplied inner/mid radii");
		require(!fresh.Consume(Active(), 1000000, 1000000).Active(), "Same provider publication cannot drive a new renderer frame");
		require(fresh.Consume(Active(50), 1000000, 1000000).Active(), "Skipped provider frame IDs must recover immediately");
		require(!fresh.Consume(Active(49), 1000000, 1000000).Active(), "Regressed frame ID must not revive a stale gaze");
		{
			FreshnessPolicy age;
			require(age.Consume(Active(), 1100000, 1000000).Active(), "Exactly 100ms current publication is allowed");
			require(!age.Consume(Active(2), 1100001, 1000000).Active(), "Older than 100ms is rejected");
			require(age.Consume(Active(3), 1000001, 1000000).Active(), "No recovery delay after stale publication");
		}
		auto rejected = [&](Snapshot s, const char* label, std::int64_t now = 1000000, std::int64_t frequency = 1000000) {
			FreshnessPolicy policy;
			require(!policy.Consume(s, now, frequency).Active(), label);
		};
		auto s = Active();
		s.structSize -= 8;
		rejected(s, "Short ABI");
		s = Active();
		++s.version;
		rejected(s, "Unknown ABI version");
		s = Active();
		s.shape = static_cast<Shape>(99);
		rejected(s, "Unknown shape");
		s = Active();
		s.mode = Mode::Disabled;
		rejected(s, "Disabled is native");
		s = Active();
		s.mode = static_cast<Mode>(99);
		rejected(s, "Unknown mode");
		s = Active();
		s.frameId = 0;
		rejected(s, "No active frame");
		s = Active();
		s.qpcFrequency = 999999;
		rejected(s, "QPC frequency must match local clock");
		rejected(Active(), "Zero frequency", 1000000, 0);
		rejected(Active(), "Negative frequency", 1000000, -1);
		rejected(Active(), "Future publication", 999999);
		s = Active();
		s.publicationQpc = 0;
		rejected(s, "Missing publication clock");
		s = Active();
		s.innerRadius = std::numeric_limits<float>::quiet_NaN();
		rejected(s, "NaN radius");
		s = Active();
		s.midRadius = std::numeric_limits<float>::infinity();
		rejected(s, "Infinite radius");
		s = Active();
		s.innerRadius = 0;
		rejected(s, "Invalid inner radius");
		s = Active();
		s.midRadius = .5f;
		rejected(s, "Inverted rings");
		s = Active();
		s.midRadius = 1.6f;
		rejected(s, "Oversized outer ring");
		for (int eye = 0; eye < 2; ++eye)
			for (int axis = 0; axis < 2; ++axis) {
				s = Active();
				s.centerUV[eye][axis] = -0.01f;
				rejected(s, "Center below eye UV range");
				s = Active();
				s.centerUV[eye][axis] = 1.01f;
				rejected(s, "Center above eye UV range");
				s = Active();
				s.centerUV[eye][axis] = std::numeric_limits<float>::quiet_NaN();
				rejected(s, "Nonfinite center");
			}
		{
			FreshnessPolicy fixed;
			s = Active();
			s.mode = Mode::Fixed;
			s.midRadius = s.innerRadius;
			require(fixed.Consume(s, 1000000, 1000000).Active(), "Valid fixed profile with equal radii must work");
		}
		{
			FreshnessPolicy zeroSample;
			s = Active();
			s.gazeSampleTime = 0;
			s.predictedDisplayTime = 0;
			require(zeroSample.Consume(s, 1000000, 1000000).Active(), "XR sample times are not the publication validity clock");
		}
		std::cout << "PASS: " << checks << " ABI/profile/freshness assertions\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
