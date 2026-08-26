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
		       !IsViewCoherent(true, 1.0, kMaximumCoherentTranslationSquared + 1e-6f);
	}

	bool CoversModeSelection()
	{
		return ShouldEvaluateEnvelope(Mode::Balanced, false) &&
		       !ShouldEvaluateEnvelope(Mode::Balanced, true) &&
		       !ShouldEvaluateEnvelope(Mode::Performance, false) &&
		       !ShouldEvaluateEnvelope(Mode::Performance, true);
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

		transform.entry[2][2] = std::numeric_limits<float>::infinity();
		return !TryBuildBoundingSphere(transform, sphere);
	}

	bool CoversMotionExpansion()
	{
		const float translationOnly = CalculateMotionExpansion(100.0f, 1.0, 4.0f);
		const float halfTurn = CalculateMotionExpansion(100.0f, -1.0, 0.0f);
		return std::abs(translationOnly - 2.0f) < 1e-5f &&
		       std::abs(halfTurn - 20.0f) < 1e-5f &&
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
		return foundDirect && foundHighScore && foundMiddleScore;
	}
}

int main()
{
	const bool passed = CoversTemporalValidityBoundary() &&
	                    CoversModeSelection() &&
	                    CoversOBBBoundingSphere() &&
	                    CoversMotionExpansion() &&
	                    CoversBoundedPrioritySelection();
	return passed ? 0 : 1;
}
