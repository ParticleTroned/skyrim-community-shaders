#pragma once

#include <Unknwn.h>

namespace CSX::NvidiaComIdentity
{
	[[nodiscard]] inline bool IsSame(IUnknown* a_left, IUnknown* a_right) noexcept
	{
		if (a_left == a_right)
			return a_left != nullptr;
		if (!a_left || !a_right)
			return false;

		IUnknown* leftIdentity = nullptr;
		IUnknown* rightIdentity = nullptr;
		const HRESULT leftResult = a_left->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&leftIdentity));
		const HRESULT rightResult = a_right->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&rightIdentity));
		const bool same = SUCCEEDED(leftResult) && SUCCEEDED(rightResult) &&
		                  leftIdentity == rightIdentity;
		if (leftIdentity)
			leftIdentity->Release();
		if (rightIdentity)
			rightIdentity->Release();
		return same;
	}
}
