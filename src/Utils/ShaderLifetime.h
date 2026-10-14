#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <winrt/base.h>

namespace Util::ShaderLifetime
{
	template <class Wrapper>
	struct TrailingBytesDeleter
	{
		void operator()(Wrapper* a_wrapper) const noexcept
		{
			std::destroy_at(a_wrapper);
			delete[] reinterpret_cast<std::uint8_t*>(a_wrapper);
		}
	};

	/** Value-initialize a wrapper followed by contiguous bytecode, with matching array ownership. */
	template <class Wrapper>
	std::unique_ptr<Wrapper, TrailingBytesDeleter<Wrapper>> MakeWithTrailingBytes(std::size_t a_size)
	{
		static_assert(alignof(Wrapper) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
		if (a_size > std::numeric_limits<std::size_t>::max() - sizeof(Wrapper))
			throw std::bad_array_new_length();
		auto storage = std::unique_ptr<std::uint8_t[]>(new std::uint8_t[sizeof(Wrapper) + a_size]);
		auto* wrapper = std::construct_at(reinterpret_cast<Wrapper*>(storage.get()));
		storage.release();
		return std::unique_ptr<Wrapper, TrailingBytesDeleter<Wrapper>>(wrapper);
	}

	/** Own a shader wrapper and its driver object through every cache consumer. */
	template <class Wrapper, class Deleter, class Resource>
	std::shared_ptr<Wrapper> Share(std::unique_ptr<Wrapper, Deleter> a_wrapper, winrt::com_ptr<Resource> a_resource)
	{
		struct Owner
		{
			winrt::com_ptr<Resource> resource;
			std::unique_ptr<Wrapper, Deleter> wrapper;
		};
		auto owner = std::make_shared<Owner>(std::move(a_resource), std::move(a_wrapper));
		auto* wrapper = owner->wrapper.get();
		return { std::move(owner), wrapper };
	}

	/** Retain the last custom wrapper installed in native state; access is renderer-serialized. */
	template <class Wrapper>
	class RetainedBinding
	{
	public:
		/** Replace the native pointer before releasing its previous custom owner. */
		void Set(Wrapper*& a_native, std::shared_ptr<Wrapper> a_shader)
		{
			a_native = a_shader.get();
			owner = std::move(a_shader);
		}

	private:
		std::shared_ptr<Wrapper> owner;
	};
}
