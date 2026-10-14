#pragma once

#include <memory>
#include <utility>
#include <winrt/base.h>

namespace Util::ShaderLifetime
{
	/** Own a shader wrapper and its driver object through every cache consumer. */
	template <class Wrapper, class Resource>
	std::shared_ptr<Wrapper> Share(std::unique_ptr<Wrapper> a_wrapper, winrt::com_ptr<Resource> a_resource)
	{
		struct Owner
		{
			winrt::com_ptr<Resource> resource;
			std::unique_ptr<Wrapper> wrapper;
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
