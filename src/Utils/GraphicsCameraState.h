#pragma once

#include "RE/S/State.h"

#include <cstddef>
#include <cstdint>

namespace Util::GraphicsCamera
{
	/** Flat camera-table layout omitted by the universal CommonLib build. */
	struct FlatStateEntry
	{
		RE::NiCamera* referenceCamera;
		std::uint64_t padding08;
		RE::BSGraphics::ViewData viewData;
		RE::NiPoint3 posAdjust;
		RE::NiPoint3 currentPosAdjust;
		RE::NiPoint3 previousPosAdjust;
		bool useJitter;
		std::uint32_t numData;
		std::uint32_t padding28C;
	};
	static_assert(offsetof(FlatStateEntry, viewData) == 0x10);
	static_assert(offsetof(FlatStateEntry, posAdjust) == 0x260);
	static_assert(offsetof(FlatStateEntry, previousPosAdjust) == 0x278);
	static_assert(sizeof(FlatStateEntry) == 0x290);

	/** Stereo camera-table prefix; the engine owns the per-eye view array. */
	struct StereoStateEntry
	{
		RE::NiCamera* referenceCamera;
		RE::BSTArray<RE::BSGraphics::ViewData> viewData;
	};
	static_assert(offsetof(StereoStateEntry, viewData) == 0x08);
}
