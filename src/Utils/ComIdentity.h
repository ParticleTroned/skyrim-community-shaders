#pragma once

#include <unknwn.h>
#include <wrl/client.h>

namespace Util
{
	/** Retain canonical COM identity, or return null when it cannot be resolved. */
	[[nodiscard]] inline Microsoft::WRL::ComPtr<IUnknown> GetComIdentity(IUnknown* object) noexcept
	{
		Microsoft::WRL::ComPtr<IUnknown> identity;
		if (object && FAILED(object->QueryInterface(IID_PPV_ARGS(&identity))))
			identity.Reset();
		return identity;
	}

	/** Compare canonical COM identities; null objects never match. */
	[[nodiscard]] inline bool SameIdentity(IUnknown* left, IUnknown* right) noexcept
	{
		if (!left || !right)
			return false;
		if (left == right)
			return true;
		const auto leftIdentity = GetComIdentity(left);
		return leftIdentity && leftIdentity == GetComIdentity(right);
	}
}
