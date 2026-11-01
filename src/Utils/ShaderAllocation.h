#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

namespace Util::ShaderAllocation
{
	/// Owns wrapper storage only; driver-object Release remains with the caller.
	template <class Wrapper>
	struct TrailingBytesDeleter
	{
		void operator()(Wrapper* a_wrapper) const noexcept
		{
			if (!a_wrapper)
				return;

			std::destroy_at(a_wrapper);
			delete[] reinterpret_cast<std::uint8_t*>(a_wrapper);
		}
	};

	template <class Wrapper>
	using TrailingBytesOwner = std::unique_ptr<Wrapper, TrailingBytesDeleter<Wrapper>>;

	/// Value-initializes a wrapper followed immediately by uninitialized trailing bytes.
	template <class Wrapper>
	TrailingBytesOwner<Wrapper> MakeWithTrailingBytes(std::size_t a_size)
	{
		static_assert(alignof(Wrapper) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
		if (a_size > (std::numeric_limits<std::size_t>::max)() - sizeof(Wrapper))
			throw std::bad_array_new_length();

		auto storage = std::unique_ptr<std::uint8_t[]>(new std::uint8_t[sizeof(Wrapper) + a_size]);
		auto* wrapper = std::construct_at(reinterpret_cast<Wrapper*>(storage.get()));
		storage.release();
		return TrailingBytesOwner<Wrapper>{ wrapper };
	}
}
