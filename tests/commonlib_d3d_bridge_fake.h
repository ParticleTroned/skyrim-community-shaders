#pragma once

// Policy fixtures use the same fake pointer types on both sides of the bridge.
namespace REX::W32
{
	template <class T>
	T* AsReal(T* a_pointer) noexcept
	{
		return a_pointer;
	}

	template <class T>
	T* AsW32(T* a_pointer) noexcept
	{
		return a_pointer;
	}
}
