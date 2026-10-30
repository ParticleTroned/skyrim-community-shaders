#include "Features/VRDepthCullingSubmissionPolicy.h"
#include "Features/VRDepthCullingTemporal.h"
#include "Features/VRDepthCullingTemporalPolicy.h"
#include <iostream>
#include <random>
#include <vector>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

namespace
{
	using namespace VRDepthCullingTemporalPolicy;
	using VRDepthCullingTemporal::Mode;

	bool CoversTemporalValidityBoundary()
	{
		return !IsViewCoherent(false, 1.0, 0.0f) &&
		       IsViewCoherent(true, kMinimumCoherentRotationCosine, kMaximumCoherentTranslationSquared) &&
		       !IsViewCoherent(true, kMinimumCoherentRotationCosine - 1e-12, 0.0f) &&
		       !IsViewCoherent(true, 1.0, kMaximumCoherentTranslationSquared + 1e-6f) &&
		       !IsViewCoherent(true, std::numeric_limits<double>::quiet_NaN(), 0.0f);
	}

	bool CoversOBBBoundingSphere()
	{
		OBBTransform transform{};
		transform.entry[0][0] = 3.0f;
		transform.entry[1][1] = 4.0f;
		transform.entry[2][2] = 12.0f;
		transform.entry[0][3] = 10.0f;
		transform.entry[1][3] = -20.0f;
		transform.entry[2][3] = 30.0f;
		transform.entry[3][3] = 1.0f;

		BoundingSphere sphere{};
		if (!TryBuildBoundingSphere(transform, sphere))
			return false;
		if (sphere.center[0] != 10.0f || sphere.center[1] != -20.0f || sphere.center[2] != 30.0f || sphere.radius != 13.0f)
			return false;

		transform.entry[0][1] = 3.0f;
		if (!TryBuildBoundingSphere(transform, sphere) ||
			std::abs(sphere.radius - 14.0f) > 1e-5f) {
			return false;
		}

		transform.entry[2][2] = std::numeric_limits<float>::infinity();
		return !TryBuildBoundingSphere(transform, sphere);
	}

	bool CoversMotionExpansion()
	{
		MotionEnvelope translationEnvelope{};
		MotionEnvelope halfTurnEnvelope{};
		MotionEnvelope combinedEnvelope{};
		float translationOnly = 0.0f;
		float halfTurn = 0.0f;
		float combinedMotion = 0.0f;
		const bool valid =
			TryBuildMotionEnvelope(1.0, 4.0f, translationEnvelope) &&
			TryBuildMotionEnvelope(-1.0, 0.0f, halfTurnEnvelope) &&
			TryBuildMotionEnvelope(-1.0, 4.0f, combinedEnvelope) &&
			TryCalculateMotionExpansion(100.0f, translationEnvelope, translationOnly) &&
			TryCalculateMotionExpansion(100.0f, halfTurnEnvelope, halfTurn) &&
			TryCalculateMotionExpansion(100.0f, combinedEnvelope, combinedMotion);
		return std::abs(translationOnly - 2.0f) < 1e-5f &&
		       std::abs(halfTurn - 20.0f) < 1e-5f &&
		       std::abs(combinedMotion - 26.0f) < 1e-5f &&
		       valid &&
		       !TryBuildMotionEnvelope(1.0, -1.0f, combinedEnvelope) &&
		       !TryCalculateMotionExpansion(-1.0f, combinedEnvelope, combinedMotion) &&
		       CalculateRiskScore(5.0f, 100.0f) == 0.25f;
	}

	bool CoversBoundedPrioritySelection()
	{
		CandidateSet<3> candidates;
		candidates.Add({ 5, 1.0f, false });
		candidates.Add({ 4, 2.0f, false });
		candidates.Add({ 3, 3.0f, false });
		candidates.Add({ 2, 0.5f, true });
		candidates.Add({ 1, 1.5f, false });
		candidates.Add({ 0, std::numeric_limits<float>::quiet_NaN(), true });
		if (candidates.Size() != 3)
			return false;

		bool foundDirect = false;
		bool foundHighScore = false;
		bool foundMiddleScore = false;
		for (std::size_t index = 0; index < candidates.Size(); ++index) {
			foundDirect |= candidates[index].index == 2;
			foundHighScore |= candidates[index].index == 3;
			foundMiddleScore |= candidates[index].index == 4;
		}
		if (!foundDirect || !foundHighScore || !foundMiddleScore)
			return false;

		CandidateSet<2> ties;
		ties.Add({ 5, 1.0f, false });
		ties.Add({ 4, 1.0f, false });
		ties.Add({ 3, 1.0f, false });
		bool foundThree = false;
		bool foundFour = false;
		for (std::size_t index = 0; index < ties.Size(); ++index) {
			foundThree |= ties[index].index == 3;
			foundFour |= ties[index].index == 4;
		}
		return foundThree && foundFour;
	}

	Relevance ReferenceRelevance(float radius, float distance, const MotionEnvelope& envelope, float planeDistance)
	{
		float expansion = 0;
		if (!TryCalculateMotionExpansion(distance, envelope, expansion))
			return Relevance::Invalid;
		const float expanded = radius + expansion;
		if (!std::isfinite(expanded) || expanded < radius)
			return Relevance::Invalid;
		if (radius >= planeDistance)
			return Relevance::Direct;
		return expanded >= planeDistance ? Relevance::Expanded : Relevance::Excluded;
	}

	bool CoversLazyExpansion()
	{
		const float max = std::numeric_limits<float>::max();
		const float inf = std::numeric_limits<float>::infinity();
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const std::array radii{ 0.0f, 1.0f, 1e20f, max * 0.5f, std::nextafter(max, 0.0f), max };
		const std::array distances{ -1.0f, 0.0f, 1.0f, 1e20f, max, inf, nan };
		const std::array<MotionEnvelope, 13> envelopes{ { { 0, 0 }, { 1, 2 }, { 0x1p64, 2 },
			{ max, 2 }, { max, 0 }, { -1, 1 }, { 1, -1 }, { nan, 1 }, { 1, nan }, { inf, 1 }, { 1, inf },
			{ 1e300, 1e300 }, { 0, 4 } } };
		for (float radius : radii)
			for (float distance : distances)
				for (auto envelope : envelopes) {
					for (float plane : { -1.0f, 1.0f, 100.0f, max }) {
						unsigned calls = 0;
						const auto actual = ClassifySphere(radius, distance, envelope, [&](float r) { return r >= plane; }, [&] { ++calls; });
						if (actual != ReferenceRelevance(radius, distance, envelope, plane) || calls > 1)
							return false;
					}
				}
		unsigned calls = 0;
		if (ClassifySphere(1, 100, { 2, 1 }, [](float) { return true; }, [&] { ++calls; }) != Relevance::Direct || calls)
			return false;
		if (ClassifySphere(1, 100, { 2, 1 }, [](float r) { return r >= 2; }, [&] { ++calls; }) != Relevance::Expanded || calls != 1)
			return false;
		if (ClassifySphere(1, 100, { 2, 1 }, [](float r) { return r >= 100; }, [&] { ++calls; }) != Relevance::Excluded || calls != 2)
			return false;

		std::mt19937 random(0xC011);
		std::uniform_real_distribution<float> values(-10000, 10000);
		for (unsigned batch = 0; batch < 50; ++batch) {
			CandidateSet<kBalancedPromotionBudget> reference, actual;
			for (unsigned index = 0; index < kMaximumObjects; ++index) {
				OBBTransform transform{};
				for (auto& row : transform.entry)
					for (float& value : row) value = values(random);
				if (index % 7 == 0)
					transform.entry[0][0] = nan;
				BoundingSphere sphere{};
				if (!TryBuildBoundingSphere(transform, sphere))
					continue;
				const float distance = sphere.center[0] * sphere.center[0] + sphere.center[1] * sphere.center[1] + sphere.center[2] * sphere.center[2];
				const float plane = values(random) * 4;
				const MotionEnvelope envelope{ static_cast<double>(batch), 0.1 };
				const auto expected = ReferenceRelevance(sphere.radius, distance, envelope, plane);
				const auto observed = ClassifySphere(sphere.radius, distance, envelope, [&](float r) { return r >= plane; }, [] {});
				if (observed != expected)
					return false;
				if (expected == Relevance::Direct || expected == Relevance::Expanded)
					reference.Add({ index, CalculateRiskScore(sphere.radius, distance), expected == Relevance::Direct });
				if (observed == Relevance::Direct || observed == Relevance::Expanded)
					actual.Add({ index, CalculateRiskScore(sphere.radius, distance), observed == Relevance::Direct });
			}
			if (reference.Size() != actual.Size())
				return false;
			for (std::size_t i = 0; i < reference.Size(); ++i)
				if (reference[i].index != actual[i].index)
					return false;
		}
		CandidateSet<kBalancedPromotionBudget> ties;
		for (unsigned i = 1000; i-- > 0;) ties.Add({ i, 1, true });
		for (std::size_t i = 0; i < ties.Size(); ++i)
			if (ties[i].index >= kBalancedPromotionBudget)
				return false;
		return ties.Size() == kBalancedPromotionBudget;
	}

	bool CoversSubmissionLifetime()
	{
		using namespace VRDepthCullingSubmissionPolicy;
		const Batch batch{ 1, 2, 3, 200, 0 };
		Submission stamp{ batch, 10, 4, 6, UINT32_MAX, true };
		if (Match(stamp, batch, 10, 4, 6) != Reason::None || Age(stamp.frame, 0) != 1)
			return false;
		if (Match(stamp, batch, 11, 4, 6) != Reason::BatchMismatch)
			return false;
		for (unsigned field = 0; field < 5; ++field) {
			auto recycled = batch;
			switch (field) {
			case 0:
				++recycled.culler;
				break;
			case 1:
				++recycled.transforms;
				break;
			case 2:
				++recycled.results;
				break;
			case 3:
				++recycled.count;
				break;
			case 4:
				++recycled.selector;
				break;
			}
			if (Match(stamp, recycled, 10, 4, 6) != Reason::BatchMismatch)
				return false;
		}
		if (Match(stamp, batch, 10, 5, 6) != Reason::EpochMismatch || Match(stamp, batch, 10, 4, 8) != Reason::EpochMismatch ||
			Match(stamp, batch, 10, 4, 7) != Reason::EpochMismatch)
			return false;
		stamp.produced = false;
		if (Match(stamp, batch, 10, 4, 6) != Reason::MissingProducer || Match(stamp, {}, 10, 4, 6) != Reason::InvalidBatch)
			return false;
		for (auto invalid : { Batch{ 1, 2, 0, 200, 0 }, Batch{ 1, 2, 3, 0, 0 }, Batch{ 1, 2, 3, 4097, 0 }, Batch{ 1, 2, 3, 200, 2 } })
			if (IsValid(invalid))
				return false;
		// Native viewports retain fractional eye widths at arbitrary dynamic scales.
		if (!IsSupportedStereoLayout(4096, 2048, 4096, 2048) || !IsSupportedStereoLayout(4096, 2048, 2048, 1024) ||
			!IsSupportedStereoLayout(2048, 1024, 1024, 512) || !IsSupportedStereoLayout(4096, 2048, 2047, 1024) ||
			!IsSupportedStereoLayout(4096, 2048, 2047.75f, 1023.5f) || !IsSupportedRasterExtent(775, 430))
			return false;
		for (auto layout : { std::array<unsigned, 4>{ 0, 0, 0, 0 }, { 4095, 2048, 2048, 1024 },
				 { 4096, 2048, 4098, 1024 }, { 4096, 2048, 4096, 2049 },
				 { 4096, 2048, 0, 1024 }, { 4096, 2048, 2048, 0 } })
			if (IsSupportedStereoLayout(layout[0], layout[1], static_cast<float>(layout[2]), static_cast<float>(layout[3])))
				return false;
		for (float invalid : { -1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
			if (IsSupportedStereoLayout(4096, 2048, invalid, 1024) || IsSupportedStereoLayout(4096, 2048, 2048, invalid) ||
				IsSupportedRasterExtent(invalid, 430) || IsSupportedRasterExtent(775, invalid))
				return false;
		}
		for (auto width : { 1550u, 2016u, 3100u, 4032u }) {
			for (auto scale : { .33333334f, .5f, .6666667f, .77f, 1.0f }) {
				if (!IsSupportedStereoLayout(width, 2240, width * scale, 2240 * scale) ||
					!IsSupportedRasterExtent(width / 2, 1120))
					return false;
			}
		}
		Eye eye{};
		eye.projection[0] = eye.projection[5] = 1;
		eye.viewport = { 0, 1, 1, 0 };
		eye.depthRange = { 0, 1 };
		if (!IsValid(eye) || !SameProjection(eye, eye))
			return false;
		auto other = eye;
		other.projection[0] += 1e-6f;
		if (!SameProjection(eye, other))
			return false;
		other.projection[0] += .01f;
		if (SameProjection(eye, other))
			return false;
		other = eye;
		other.depthRange = { 1, 0 };
		if (IsValid(other))
			return false;
		other = eye;
		other.viewport[1] = .5f;
		if (IsValid(other))
			return false;
		other = eye;
		other.position[2] = std::numeric_limits<float>::quiet_NaN();
		return !IsValid(other) && !IsCameraCut(1, 100) && !IsCameraCut(.99, 100) &&
		       IsCameraCut(.5, 0) && IsCameraCut(1, 1024.0f * 1024 + 1);
	}

	bool CoversLiveProjectionChanges()
	{
		using namespace VRDepthCullingSubmissionPolicy;
		constexpr Scene interior{ 1, 0 }, exterior{ 0, 2 }, otherInterior{ 3, 0 }, otherWorld{ 0, 4 };
		static_assert(IsValid(interior) && IsValid(exterior) && !IsValid(Scene{}) && !IsValid(Scene{ 1, 2 }));
		static_assert(interior == interior && interior != otherInterior && exterior != otherWorld && interior != exterior);
		const Frustum original{ { -1, 1, 1, -1, 5, 10000 }, false };
		if (!IsValid(original) || !SameProjection(original, original))
			return false;
		for (std::size_t plane = 0; plane < original.planes.size(); ++plane) {
			auto changed = original;
			changed.planes[plane] += .25f;
			if (!IsValid(changed) || SameProjection(original, changed))
				return false;
		}
		auto changed = original;
		changed.orthographic = true;
		if (SameProjection(original, changed))
			return false;
		changed.planes[4] = 0;
		if (!IsValid(changed))
			return false;
		changed.orthographic = false;
		if (IsValid(changed))
			return false;
		for (std::size_t plane = 0; plane < original.planes.size(); ++plane) {
			for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
					 -std::numeric_limits<float>::infinity() }) {
				changed = original;
				changed.planes[plane] = invalid;
				if (IsValid(changed) || SameProjection(original, changed) || SameProjection(changed, original))
					return false;
			}
		}
		for (auto invalid : { Frustum{}, Frustum{ { 1, -1, 1, -1, 5, 10000 }, false },
				 Frustum{ { -1, 1, -1, 1, 5, 10000 }, false }, Frustum{ { -1, 1, 1, -1, -1, 10000 }, false },
				 Frustum{ { -1, 1, 1, -1, 5, 5 }, false } })
			if (IsValid(invalid) || SameProjection(original, invalid))
				return false;

		// The cached matrices can agree while only the right live eye changes.
		Eye cached{};
		cached.projection[0] = cached.projection[5] = 1;
		std::array<Frustum, 2> live{ original, original };
		live[1].planes[0] -= .25f;
		if (!SameProjection(cached, cached) || !SameProjection(original, live[0]) || SameProjection(original, live[1]))
			return false;
		auto invalid = cached;
		invalid.projection[2] = std::numeric_limits<float>::quiet_NaN();
		return !SameProjection(cached, invalid) && !SameProjection(invalid, cached);
	}

	bool CoversReadbackProjectionDomain()
	{
		using namespace VRDepthCullingSubmissionPolicy;
		const Frustum left{ { -1.5414544f, 1.0504926f, 1.4074042f, -1.4120144f, 13, 353840 }, false };
		const Frustum right{ { -1.0501256f, 1.541309f, 1.4079398f, -1.4104933f, 13, 353840 }, false };
		const Frustum depthPassLeft{ { -.794258f, .794258f, .794258f, -.794258f, 13, 353840 }, false };
		ReadbackProjection pending{ { left, right }, 123, 4, 6 };
		const auto producer = std::exchange(pending, {});
		if (!Matches(producer, 123, 4, 6) || Matches(pending, 123, 4, 6) ||
			Matches(producer, 456, 4, 6) || Matches(producer, 123, 5, 6) || Matches(producer, 123, 4, 8))
			return false;
		// The depth pass's temporary left frustum must never become the comparison baseline.
		if (SameProjection(depthPassLeft, left) || !SameProjection(producer.frusta[0], left) || !SameProjection(producer.frusta[1], right))
			return false;
		for (std::size_t eye = 0; eye < 2; ++eye) {
			auto next = producer;
			next.frusta[eye].planes[1] += .1f;
			if (!Matches(next, 123, 4, 6) || SameProjection(producer.frusta[eye], next.frusta[eye]))
				return false;
			next.frusta[eye].planes[4] = std::numeric_limits<float>::quiet_NaN();
			if (Matches(next, 123, 4, 6))
				return false;
		}
		auto changingPolicy = producer;
		changingPolicy.policyEpoch = 7;
		return !Matches(changingPolicy, 123, 4, 7) && !Matches(ReadbackProjection{}, 0, 0, 0);
	}

	bool CoversConsumedRecovery()
	{
		using namespace VRDepthCullingSubmissionPolicy;
		std::array<std::uint32_t, 4098> results{};
		results.front() = results.back() = 0xDEADBEEF;
		results[1] = 9;
		const auto nativeResults = results;
		const Batch batch{ 1, 2, reinterpret_cast<std::uintptr_t>(&results[1]), 4096, 0 };
		CandidateSet<kBalancedPromotionBudget> candidates;
		for (std::uint32_t index = 0; index < batch.count; ++index)
			candidates.Add({ index, static_cast<float>(index), true });
		Handoff handoff;
		handoff.Publish({ batch, 0, 4, 6, 10, true });
		auto consumed = handoff.Consume();
		for (std::size_t index = 0; index < static_cast<std::size_t>(Reason::Count); ++index) {
			const auto reason = static_cast<Reason>(index);
			const bool recoverable = reason == Reason::None || reason == Reason::CompatibleResourceChange ||
			                         reason == Reason::ProjectionChanged || reason == Reason::SceneChanged || reason == Reason::CameraCut;
			results = nativeResults;
			const auto promoted = ApplyRecovery(reason, consumed, batch, 4, 6, { &results[1], 4096 }, candidates);
			if (CanRecover(reason) != recoverable || promoted != (recoverable ? kBalancedPromotionBudget : 0) ||
				results.front() != nativeResults.front() || results.back() != nativeResults.back())
				return false;
			for (std::uint32_t slot = 0; slot < batch.count; ++slot) {
				const auto expected = recoverable && slot >= batch.count - kBalancedPromotionBudget ? 1 : nativeResults[slot + 1];
				if (results[slot + 1] != expected)
					return false;
			}
		}
		results = nativeResults;
		if (CanRecover(Reason::Count) || CanRecover(static_cast<Reason>(255)))
			return false;
		// Persistently unavailable metadata must not erase useful native occlusion each frame.
		Handoff retry;
		for (int frame = 0; frame < 100; ++frame) {
			retry.Publish({ batch, 0, 4, 6, static_cast<std::uint32_t>(frame), true });
			if (ApplyRecovery(Reason::MissingDepth, retry.Consume(), batch, 4, 6, { &results[1], 4096 }, candidates) || results != nativeResults)
				return false;
		}
		auto empty = handoff.Consume();
		if (Match(empty.stamp, batch, empty.sequence, 4, 6) != Reason::MissingProducer ||
			ApplyRecovery(Reason::CameraCut, empty, batch, 4, 6, { &results[1], 4096 }, candidates) || results != nativeResults)
			return false;
		// Recycled addresses and equal counts still require a new publication in this lifetime.
		if (Match(consumed.stamp, batch, empty.sequence, 4, 6) != Reason::BatchMismatch)
			return false;
		handoff.Publish({ batch, 0, 4, 6, 12, true });
		handoff.Invalidate();
		empty = handoff.Consume();
		if (ApplyRecovery(Reason::SceneChanged, empty, batch, 4, 6, { &results[1], 4096 }, candidates) || results != nativeResults)
			return false;
		handoff.Publish({ batch, 0, 4, 6, 13, true });
		consumed = handoff.Consume();
		if (ApplyRecovery(Reason::CameraCut, consumed, batch, 5, 6, { &results[1], 4096 }, candidates) ||
			ApplyRecovery(Reason::CameraCut, consumed, batch, 4, 8, { &results[1], 4096 }, candidates) ||
			ApplyRecovery(Reason::CameraCut, consumed, batch, 4, 6, { &results[2], 4096 }, candidates) ||
			ApplyRecovery(Reason::CameraCut, consumed, batch, 4, 6, { &results[1], 4095 }, candidates) || results != nativeResults)
			return false;
		CandidateSet<kBalancedPromotionBudget> invalid;
		invalid.Add({ 1, 1, true });
		invalid.Add({ batch.count, 2, true });
		if (ApplyRecovery(Reason::SceneChanged, consumed, batch, 4, 6, { &results[1], 4096 }, invalid) || results != nativeResults)
			return false;
		CandidateSet<kBalancedPromotionBudget> duplicate;
		duplicate.Add({ 0, 3, true });
		duplicate.Add({ 1, 2, true });
		duplicate.Add({ 1, 1, true });
		return ApplyRecovery(Reason::SceneChanged, consumed, batch, 4, 6, { &results[1], 4096 }, duplicate) == 1 && results[1] == 9 && results[2] == 1;
	}

	constexpr bool CoversMutuallyExclusiveModes()
	{
		return VRDepthCullingTemporal::SelectMode(false) == Mode::Balanced &&
		       VRDepthCullingTemporal::SelectMode(true) == Mode::Legacy &&
		       static_cast<int>(Mode::Balanced) == 0 &&
		       static_cast<int>(Mode::Legacy) == 2 &&
		       VRDepthCullingTemporal::NormalizeMode(Mode::Balanced) == Mode::Balanced &&
		       VRDepthCullingTemporal::NormalizeMode(Mode::Legacy) == Mode::Legacy &&
		       VRDepthCullingTemporal::NormalizeMode(static_cast<Mode>(1)) == Mode::Balanced &&
		       VRDepthCullingTemporal::NormalizeMode(static_cast<Mode>(-1)) == Mode::Balanced &&
		       VRDepthCullingTemporal::NormalizeMode(static_cast<Mode>(3)) == Mode::Balanced &&
		       std::string_view(VRDepthCullingTemporal::GetModeName(Mode::Balanced)) == "balanced" &&
		       std::string_view(VRDepthCullingTemporal::GetModeName(Mode::Legacy)) == "legacy";
	}

	static_assert(CoversMutuallyExclusiveModes());
}

int main()
{
	const bool passed = CoversTemporalValidityBoundary() &&
	                    CoversOBBBoundingSphere() &&
	                    CoversMotionExpansion() &&
	                    CoversLazyExpansion() &&
	                    CoversSubmissionLifetime() &&
	                    CoversLiveProjectionChanges() &&
	                    CoversReadbackProjectionDomain() &&
	                    CoversConsumedRecovery() &&
	                    CoversBoundedPrioritySelection() &&
	                    CoversMutuallyExclusiveModes();
	if (!passed)
		std::cerr << "Temporal culling regression failed\n";
	return passed ? 0 : 1;
}
