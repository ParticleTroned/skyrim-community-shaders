#include "Features/VR/WandInteractionPolicy.h"

namespace
{
	using namespace WandInteractionPolicy;

	constexpr Candidate Hit(float a_distance, bool a_moved = false)
	{
		return { true, a_moved, a_distance };
	}

	constexpr bool CoversCaptureAndPreference()
	{
		return SelectActiveHand(Hand::Primary, Hand::Secondary, Hand::Secondary, {}, Hit(0.5f)) == Hand::Primary &&
		       SelectActiveHand(Hand::None, Hand::Secondary, Hand::Primary, Hit(0.3f), Hit(0.8f)) == Hand::Secondary &&
		       SelectActiveHand(Hand::None, Hand::Secondary, Hand::Primary, Hit(0.3f), {}) == Hand::Primary;
	}

	constexpr bool CoversStableTakeover()
	{
		return SelectActiveHand(Hand::None, Hand::None, Hand::Primary, Hit(0.7f), Hit(0.4f, true)) == Hand::Secondary &&
		       SelectActiveHand(Hand::None, Hand::None, Hand::Primary, Hit(0.7f, true), Hit(0.4f, true)) == Hand::Primary &&
		       SelectActiveHand(Hand::None, Hand::None, Hand::Primary, Hit(0.7f), Hit(0.4f)) == Hand::Primary;
	}

	constexpr bool CoversInitialAndMissSelection()
	{
		return SelectActiveHand(Hand::None, Hand::None, Hand::None, Hit(0.9f), Hit(0.4f)) == Hand::Secondary &&
		       SelectActiveHand(Hand::None, Hand::None, Hand::Primary, {}, Hit(0.4f)) == Hand::Secondary &&
		       SelectActiveHand(Hand::None, Hand::None, Hand::None, {}, {}) == Hand::None;
	}

	static_assert(CoversCaptureAndPreference());
	static_assert(CoversStableTakeover());
	static_assert(CoversInitialAndMissSelection());
}

int main()
{
	return CoversCaptureAndPreference() && CoversStableTakeover() && CoversInitialAndMissSelection() ? 0 : 1;
}
