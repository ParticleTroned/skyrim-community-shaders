#pragma once

#include <Windows.h>
#include <cstdint>

namespace Util
{
	/** @brief Share native group ownership with nested grass readers; native writers must run outside this guard. */
	class GrassGroupLock
	{
	public:
		explicit GrassGroupLock(volatile std::uint32_t& word) : word(reinterpret_cast<volatile LONG*>(&word))
		{
			if (depth++ == 0)
				while (InterlockedCompareExchange(this->word, 1, 0) != 0)
					Sleep(0);
		}
		~GrassGroupLock()
		{
			if (--depth == 0)
				InterlockedExchange(word, 0);
		}
		GrassGroupLock(const GrassGroupLock&) = delete;
		GrassGroupLock& operator=(const GrassGroupLock&) = delete;

	private:
		volatile LONG* word;
		inline static thread_local uint32_t depth = 0;
	};
}
