#pragma once

namespace RE
{
	class BSRenderPass;
}

namespace CharacterCategoryAuthoring
{
	/** Resolve immutable keyword identities after game forms have loaded. */
	void DataLoaded();
	/** Author actor categories independently of the SSS feature's loaded state. */
	void Update(RE::BSRenderPass* a_pass);
}
