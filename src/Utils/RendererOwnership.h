#pragma once

#include <Windows.h>

namespace Util
{
	/** @brief Own the native renderer transaction; try-only callers never wait for rendering. */
	class RendererOwnership
	{
	public:
		explicit RendererOwnership(CRITICAL_SECTION* a_lock, bool a_wait = false) noexcept : lock(a_lock)
		{
			if (!lock)
				return;
			if (a_wait)
				EnterCriticalSection(lock);
			else if (!TryEnterCriticalSection(lock))
				lock = nullptr;
		}

		~RendererOwnership()
		{
			if (lock)
				LeaveCriticalSection(lock);
		}

		RendererOwnership(const RendererOwnership&) = delete;
		RendererOwnership& operator=(const RendererOwnership&) = delete;
		explicit operator bool() const noexcept { return lock != nullptr; }

	private:
		CRITICAL_SECTION* lock;
	};
}
