#pragma once

namespace RE
{
	class BSRenderPass;
}

namespace CharacterCategoryAuthoring
{
	/** Author actor categories independently of the SSS feature's loaded state. */
	void Update(RE::BSRenderPass* a_pass);
}
