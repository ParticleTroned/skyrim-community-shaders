#pragma once
#include "CharacterActorGroups.h"
#include <array>
#include <cstdint>
namespace NeuralRendering
{
	enum class CharacterCategory : std::uint32_t { None = 0, Face = 1, Skin = 2, Hair = 3, Armor = 4, Weapons = 5 };
	enum class CharacterClassificationRejection : std::uint32_t
	{
		Player, BlendedMaterial, AlphaTestAndBlend, AmbiguousFaceGen, UnsupportedMaterial, Count
	};
	namespace CharacterPolicy
	{
		inline constexpr std::uint32_t kMaximumObservationsPerFrame = 4096;
		inline constexpr std::uint32_t kAllCategoryMask = 0x3eu;
		inline constexpr std::array kCategories{ CharacterCategory::Face, CharacterCategory::Skin,
			CharacterCategory::Hair, CharacterCategory::Armor, CharacterCategory::Weapons };
		inline constexpr std::size_t kCategoryCount = kCategories.size();
		[[nodiscard]] constexpr std::uint32_t CategoryBit(CharacterCategory category) noexcept
		{
			return category >= CharacterCategory::Face && category <= CharacterCategory::Weapons ? 1u << static_cast<std::uint32_t>(category) : 0u;
		}
	}
}
