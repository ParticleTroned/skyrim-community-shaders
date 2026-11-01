#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace NeuralRendering
{
	/** Mutually exclusive actor types; users may enable any combination. */
	enum class CharacterActorGroup : std::uint32_t
	{
		Humans,
		OtherHumanoids,
		Creatures,
		Animals,
		Other,
		Count,
	};

	enum class CharacterActorGroupReason : std::uint32_t
	{
		HumanLineage,
		NpcKeyword,
		TrollKeyword,
		AnimalKeyword,
		CreatureKeyword,
		UnclassifiedRace,
		MissingRace,
		Count,
	};

	inline constexpr std::size_t kCharacterActorGroupCount = static_cast<std::size_t>(CharacterActorGroup::Count);
	inline constexpr std::size_t kCharacterActorGroupReasonCount = static_cast<std::size_t>(CharacterActorGroupReason::Count);
	inline constexpr std::array<std::string_view, kCharacterActorGroupCount> kCharacterActorGroupKeys{
		"humans", "otherHumanoids", "creatures", "animals", "other"
	};
	inline constexpr std::array<std::string_view, kCharacterActorGroupReasonCount> kCharacterActorGroupReasonKeys{
		"human_lineage", "npc_keyword", "troll_keyword", "animal_keyword", "creature_keyword", "unclassified_race", "missing_race"
	};

	struct CharacterActorGroupClassification
	{
		CharacterActorGroup group = CharacterActorGroup::Other;
		CharacterActorGroupReason reason = CharacterActorGroupReason::MissingRace;
	};

	struct CharacterRaceTraits
	{
		bool present = false;
		bool humanLineage = false;
		bool npc = false;
		bool animal = false;
		bool troll = false;
		bool creature = false;
	};

	/** Specific NPC/troll identities precede overlapping animal/creature keywords. */
	[[nodiscard]] constexpr CharacterActorGroupClassification ClassifyCharacterActorGroup(CharacterRaceTraits traits) noexcept
	{
		using Group = CharacterActorGroup;
		using Reason = CharacterActorGroupReason;
		if (!traits.present)
			return {};
		if (traits.npc || (traits.humanLineage && !traits.animal && !traits.creature && !traits.troll)) {
			return traits.humanLineage ? CharacterActorGroupClassification{ Group::Humans, Reason::HumanLineage } :
			                             CharacterActorGroupClassification{ Group::OtherHumanoids, Reason::NpcKeyword };
		}
		if (traits.troll)
			return { Group::Creatures, Reason::TrollKeyword };
		if (traits.animal)
			return { Group::Animals, Reason::AnimalKeyword };
		if (traits.creature)
			return { Group::Creatures, Reason::CreatureKeyword };
		return { Group::Other, Reason::UnclassifiedRace };
	}
}
