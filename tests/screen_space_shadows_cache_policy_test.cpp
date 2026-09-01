#include "Features/ScreenSpaceShadowsCachePolicy.h"

#include <array>
#include <cstdint>

namespace
{
	struct Variant
	{
		bool valid = false;
		unsigned int sampleCount = 0;
		bool usesTerrainBlendingDepth = false;
		std::uint64_t lastUse = 0;
	};

	constexpr bool CoversVariantSelection()
	{
		using namespace ScreenSpaceShadowsCachePolicy;
		if (SelectShaderLookupAction(true, false) !=
				ShaderLookupAction::ReturnCached ||
			SelectShaderLookupAction(true, true) !=
				ShaderLookupAction::ReturnCached ||
			SelectShaderLookupAction(false, false) !=
				ShaderLookupAction::Compile ||
			SelectShaderLookupAction(false, true) !=
				ShaderLookupAction::ReturnFailure) {
			return false;
		}
		if (ActiveVariantCount(false, 8) != 2 ||
			ActiveVariantCount(false, 2) != 2 ||
			ActiveVariantCount(true, 8) != 8 ||
			ActiveVariantCount(true, 2) != 2) {
			return false;
		}

		std::array<Variant, 8> variants{};
		variants[0] = { true, 24, false, 4 };
		variants[1] = { true, 24, true, 3 };
		if (SelectVariantIndex(variants, 8, 24, false) != 0 ||
			SelectVariantIndex(variants, 8, 24, true) != 1 ||
			SelectVariantIndex(variants, 8, 32, false) != 2) {
			return false;
		}

		for (std::size_t index = 0; index < variants.size(); ++index) {
			variants[index] = {
				true,
				static_cast<unsigned int>((index + 1) * 8),
				false,
				static_cast<std::uint64_t>(index + 10),
			};
		}
		variants[5].lastUse = 1;
		if (SelectVariantIndex(variants, 8, 72, false) != 5)
			return false;

		for (auto& variant : variants)
			variant = {};
		return SelectVariantIndex(variants, 8, 8, false) == 0 &&
		       SelectVariantIndex(variants, 0, 8, false) == kNoVariant;
	}

	static_assert(CoversVariantSelection());
}

int main() {}
