#include "Features/VR/VRRenderScaleFrameBoundaryPolicy.h"

#include <initializer_list>

namespace
{
	using namespace VRRenderScaleFrameBoundaryPolicy;
	constexpr PairIdentity kIdentity{ 9, 12, 41, 7 };

	constexpr bool RequiresCompletedStereo()
	{
		PairCompletion pair{ kIdentity };
		if (CanServiceCompletedPair(pair, kIdentity))
			return false;
		RecordEyeCompletion(pair, kIdentity.token, 1);
		if (CanServiceCompletedPair(pair, kIdentity))
			return false;
		RecordEyeCompletion(pair, kIdentity.token, 0);
		return CanServiceCompletedPair(pair, kIdentity);
	}

	constexpr bool RejectsAmbiguousCompletion()
	{
		PairCompletion pair{ kIdentity };
		RecordEyeCompletion(pair, kIdentity.token, 0);
		RecordEyeCompletion(pair, kIdentity.token, 0);
		RecordEyeCompletion(pair, kIdentity.token, 1);
		if (CanServiceCompletedPair(pair, kIdentity))
			return false;
		pair = { kIdentity };
		RecordEyeCompletion(pair, kIdentity.token, 0);
		RecordEyeCompletion(pair, kIdentity.token, 1);
		RecordEyeCompletion(pair, kIdentity.token, 2);
		return !CanServiceCompletedPair(pair, kIdentity);
	}

	constexpr bool IgnoresForeignNestedWork()
	{
		PairCompletion pair{ kIdentity };
		RecordEyeCompletion(pair, kIdentity.token, 0);
		RecordEyeCompletion(pair, kIdentity.token + 1, 1);
		RecordEyeCompletion(pair, 0, 1);
		if (CanServiceCompletedPair(pair, kIdentity))
			return false;
		RecordEyeCompletion(pair, kIdentity.token, 1);
		return CanServiceCompletedPair(pair, kIdentity);
	}

	constexpr bool RejectsChangedBoundary()
	{
		PairCompletion pair{ kIdentity };
		RecordEyeCompletion(pair, kIdentity.token, 0);
		RecordEyeCompletion(pair, kIdentity.token, 1);
		for (const auto changed : {
				 PairIdentity{ 10, 12, 41, 7 },
				 PairIdentity{ 9, 13, 41, 7 },
				 PairIdentity{ 9, 12, 42, 7 },
				 PairIdentity{ 9, 12, 41, 8 } }) {
			if (CanServiceCompletedPair(pair, changed))
				return false;
		}
		return CanServiceCompletedPair(pair, kIdentity);
	}

	constexpr bool RejectsMissingIdentity()
	{
		for (const auto identity : {
				 PairIdentity{},
				 PairIdentity{ 0, 12, 41, 7 },
				 PairIdentity{ 9, 0, 41, 7 },
				 PairIdentity{ 9, 12, 0, 7 },
				 PairIdentity{ 9, 12, 41, 0 } }) {
			PairCompletion pair{ identity };
			RecordEyeCompletion(pair, identity.token, 0);
			RecordEyeCompletion(pair, identity.token, 1);
			if (CanServiceCompletedPair(pair, identity))
				return false;
		}
		return true;
	}

	static_assert(RequiresCompletedStereo());
	static_assert(RejectsAmbiguousCompletion());
	static_assert(IgnoresForeignNestedWork());
	static_assert(RejectsChangedBoundary());
	static_assert(RejectsMissingIdentity());
}

int main()
{
	return RequiresCompletedStereo() && RejectsAmbiguousCompletion() &&
	               IgnoresForeignNestedWork() && RejectsChangedBoundary() &&
	               RejectsMissingIdentity() ?
	           0 :
	           1;
}
