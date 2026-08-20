#pragma once

#include <limits>

namespace WandInteractionPolicy
{
	enum class Hand
	{
		None,
		Primary,
		Secondary
	};

	struct Candidate
	{
		bool hit = false;
		bool moved = false;
		float distance = std::numeric_limits<float>::max();
	};

	constexpr const Candidate& GetCandidate(Hand a_hand, const Candidate& a_primary, const Candidate& a_secondary)
	{
		return a_hand == Hand::Secondary ? a_secondary : a_primary;
	}

	constexpr Hand OtherHand(Hand a_hand)
	{
		return a_hand == Hand::Primary ? Hand::Secondary :
		       a_hand == Hand::Secondary ? Hand::Primary :
		                                  Hand::None;
	}

	constexpr Hand SelectActiveHand(
		Hand a_captured,
		Hand a_preferred,
		Hand a_current,
		const Candidate& a_primary,
		const Candidate& a_secondary)
	{
		if (a_captured != Hand::None)
			return a_captured;

		if (a_preferred != Hand::None && GetCandidate(a_preferred, a_primary, a_secondary).hit)
			return a_preferred;

		if (a_current != Hand::None) {
			const auto& current = GetCandidate(a_current, a_primary, a_secondary);
			const Hand otherHand = OtherHand(a_current);
			const auto& other = GetCandidate(otherHand, a_primary, a_secondary);
			if (current.hit) {
				if (other.hit && other.moved && !current.moved)
					return otherHand;
				return a_current;
			}
			if (other.hit)
				return otherHand;
		}

		if (a_primary.hit && a_secondary.hit)
			return a_primary.distance <= a_secondary.distance ? Hand::Primary : Hand::Secondary;
		if (a_primary.hit)
			return Hand::Primary;
		if (a_secondary.hit)
			return Hand::Secondary;
		return Hand::None;
	}
}
