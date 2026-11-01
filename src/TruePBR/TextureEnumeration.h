#pragma once

#include <RE/B/BSShaderProperty.h>
#include <cstdint>

namespace PBRTextures
{
	/** Visit every registered PBR terrain texture, retaining native visitation for other materials. */
	std::int32_t ForEach(RE::BSShaderProperty& property, RE::BSShaderProperty::ForEachVisitor& visitor);
}
