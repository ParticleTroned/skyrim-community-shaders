#pragma once

#include <cstddef>
#include <utility>

namespace winrt
{
	template <class T>
	class com_ptr
	{
	public:
		com_ptr() = default;
		~com_ptr() { delete value; }
		com_ptr(const com_ptr&) = delete;
		com_ptr& operator=(const com_ptr&) = delete;
		com_ptr(com_ptr&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
		com_ptr& operator=(com_ptr&& other) noexcept
		{
			if (this != &other) {
				delete value;
				value = std::exchange(other.value, nullptr);
			}
			return *this;
		}
		com_ptr& operator=(std::nullptr_t)
		{
			delete value;
			value = nullptr;
			return *this;
		}
		explicit operator bool() const { return value != nullptr; }
		T* get() const { return value; }
		T** put()
		{
			*this = nullptr;
			return &value;
		}

	private:
		T* value = nullptr;
	};
}
