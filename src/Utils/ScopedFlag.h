#pragma once

#include <cstdint>

namespace Util
{
	/** Replace selected flag bits for a scope, preserving changes to unrelated bits. */
	class ScopedFlag
	{
	public:
		ScopedFlag(std::uint32_t& flags, std::uint32_t mask, bool enabled) noexcept : flags(flags), mask(mask), saved(flags & mask)
		{
			flags = (flags & ~mask) | (enabled ? mask : 0);
		}
		~ScopedFlag() { flags = (flags & ~mask) | saved; }
		ScopedFlag(const ScopedFlag&) = delete;
		ScopedFlag& operator=(const ScopedFlag&) = delete;

	private:
		std::uint32_t& flags;
		std::uint32_t mask, saved;
	};
}
