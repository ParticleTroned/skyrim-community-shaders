#pragma once

namespace VRMenuPositioningPolicy
{
	template <class T>
	constexpr T SelectEffectiveValue(bool a_layoutUnlocked, T a_savedValue, T a_lockedValue)
	{
		return a_layoutUnlocked ? a_savedValue : a_lockedValue;
	}

	constexpr bool UseFixedWorld(int a_positioningMethod)
	{
		return a_positioningMethod == 1;
	}

	constexpr bool ShouldLockDesktopCanvas(bool a_isVR, bool a_layoutUnlocked)
	{
		return a_isVR && !a_layoutUnlocked;
	}

	constexpr bool ShouldAllowOverlayDrag(bool a_layoutUnlocked, bool a_dragEnabled)
	{
		return a_layoutUnlocked && a_dragEnabled;
	}

	constexpr bool ShouldReanchorOnOpen(bool a_layoutUnlocked)
	{
		return !a_layoutUnlocked;
	}

	constexpr bool ShouldTrackHMDYaw(bool a_layoutUnlocked)
	{
		return !a_layoutUnlocked;
	}
}
