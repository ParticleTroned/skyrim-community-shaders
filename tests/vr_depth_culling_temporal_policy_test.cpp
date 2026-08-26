#include "Features/VRDepthCullingTemporalPolicy.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace
{
	using namespace VRDepthCullingTemporalPolicy;

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
}

int main()
{
	const bool passed = CoversTemporalValidityBoundary() &&
	                    CoversOBBBoundingSphere() &&
	                    CoversMotionExpansion() &&
	                    CoversBoundedPrioritySelection();
	return passed ? 0 : 1;
}
