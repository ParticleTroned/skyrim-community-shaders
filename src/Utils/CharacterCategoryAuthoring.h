#pragma once

#include <dxgiformat.h>

namespace RE
{
	class BSRenderPass;
}

namespace CharacterCategoryAuthoring
{
	/** Preserve the original 16-bit vertex AO lane alongside exact categories. */
	inline constexpr DXGI_FORMAT kTargetFormat = DXGI_FORMAT_R16G16_UNORM;

	/** Author exact actor categories for the current deferred lighting draw. */
	void Update(RE::BSRenderPass* a_pass);
	/** Freeze categories and depth after opaque geometry and before decals. */
	void Capture();
}
