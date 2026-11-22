#pragma once

#include <bit>
#include <cstdint>

namespace GrassDrawState
{
	struct Identity
	{
		const void* context = nullptr;
		const void* vertexShader = nullptr;
		const void* pixelShader = nullptr;
		uint32_t frame = 0;
		bool operator==(const Identity&) const = default;
	};

	struct Cache
	{
		Identity identity;
		float alphaReference = 0;
		bool ready = false;
	};

	/** @brief Commit native grass state before feature processing, preserving unowned texture slots. */
	template <class Flags, class Flag, class NativeCommit, class BindTextures>
	void Commit(Cache& cache, const Identity& identity, Flags& flags, Flag alphaFlag,
		uint32_t& resources, uint32_t otherDirty, float alphaReference,
		NativeCommit&& nativeCommit, BindTextures&& bindTextures)
	{
		const auto pendingFlags = flags.underlying();
		const uint32_t pendingResources = resources;
		const bool compatible = cache.ready && cache.identity == identity;
		const bool sameAlpha = compatible && cache.alphaReference == alphaReference &&
		                       (flags.underlying() & ~uint32_t(alphaFlag)) == 0;
		const uint32_t textures = resources & 0x7Fu;
		resources &= ~textures;
		if (sameAlpha)
			flags.reset(alphaFlag);
		const bool clean = compatible && flags.underlying() == 0 && !resources && !otherDirty;
		cache.ready = false;
		try {
			if (!clean)
				nativeCommit();
			// Gaps can contain feature-owned overrides; batch only consecutive dirty slots.
			for (uint32_t pending = textures; pending;) {
				const uint32_t first = std::countr_zero(pending);
				const uint32_t count = std::countr_one(pending >> first);
				bindTextures(first, count);
				pending &= ~(((1u << count) - 1) << first);
			}
		} catch (...) {
			resources |= pendingResources;
			flags.set(static_cast<Flag>(pendingFlags));
			throw;
		}
		cache = { identity, alphaReference, true };
	}
}
