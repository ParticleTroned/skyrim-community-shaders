#include "Features/Upscaling/NeuralRendering/CharacterSettings.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace RE
{
	struct BGSKeyword
	{};
	struct TESRace
	{
		std::uint32_t formId = 0;
		TESRace* morphRace = nullptr;
		std::vector<BGSKeyword*> keywords;
		std::uint32_t GetFormID() const { return formId; }
		bool HasKeyword(const BGSKeyword* keyword) const { return std::find(keywords.begin(), keywords.end(), keyword) != keywords.end(); }
	};
	enum class FormType
	{
		Armor,
		Weapon,
		Ammo,
		Other
	};
	struct TESForm
	{
		FormType type = FormType::Other;
		template <class... T>
		bool Is(T... types) const
		{
			return ((type == types) || ...);
		}
	};
	struct NiAVObject
	{
		NiAVObject* parent = nullptr;
	};
	struct NiAlphaProperty
	{
		bool blend = false, test = false;
		bool GetAlphaBlending() const { return blend; }
		bool GetAlphaTesting() const { return test; }
	};
	struct BSGeometry : NiAVObject
	{
		struct
		{
			std::shared_ptr<NiAlphaProperty> alphaProperty;
		} data;
		auto& GetGeometryRuntimeData() { return data; }
	};
	struct BSShaderMaterial
	{
		enum class Feature
		{
			kNone,
			kFaceGen,
			kFaceGenRGBTint,
			kHairTint
		};
		Feature feature = Feature::kNone;
		Feature GetFeature() const { return feature; }
	};
	struct BSShaderProperty
	{
		enum class EShaderPropertyFlag
		{
			kFace,
			kFaceGenRGBTint,
			kHairTint
		};
		struct Flags
		{
			int bits = 0;
			bool any(EShaderPropertyFlag flag) const { return (bits & (1 << int(flag))) != 0; }
		} flags;
		BSShaderMaterial material;
		bool lighting = true;
		const void* GetRTTI() const { return lighting ? reinterpret_cast<const void*>(1) : nullptr; }
		const BSShaderMaterial* GetBaseMaterial() const { return &material; }
	};
	struct BSLightingShaderProperty : BSShaderProperty
	{
		float alpha = 1.0f;
	};
	struct BipedAnim
	{
		struct OwnerHandle
		{
			void* owner = nullptr;
			struct Pointer
			{
				void* value;
				void* get() const { return value; }
			};
			Pointer get() const { return { owner }; }
		} actorRef;
		struct Object
		{
			TESForm* item = nullptr;
			std::shared_ptr<NiAVObject> partClone;
		};
		std::array<Object, 42> objects{};
	};
	struct Actor
	{
		std::shared_ptr<BipedAnim> biped = std::make_shared<BipedAnim>();
		Actor() { biped->actorRef.owner = this; }
		TESForm* skin = nullptr;
		NiAVObject* face = nullptr;
		const auto& GetBiped() const { return biped; }
		TESForm* GetSkin() const { return skin; }
		NiAVObject* GetFaceNodeSkinned() const { return face; }
	};
}
namespace globals::rtti
{
	inline const struct
	{
		const void* get() const { return reinterpret_cast<const void*>(1); }
	} BSLightingShaderPropertyRTTI;
}

namespace
{
#include "character_material_under_test.h"

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void EquipmentClassification()
	{
		using Category = NeuralRendering::CharacterCategory;
		RE::Actor actor;
		RE::TESForm armor{ RE::FormType::Armor }, weapon{ RE::FormType::Weapon }, ammo{ RE::FormType::Ammo };
		auto root = std::make_shared<RE::NiAVObject>();
		RE::NiAVObject child{ root.get() };
		RE::BSGeometry geometry;
		geometry.parent = &child;
		actor.biped->objects[9] = { &armor, root };
		RE::BSLightingShaderProperty property;
		auto classify = [&] { return ClassifyCharacterMaterial(&property, &geometry, &actor, true); };
		Require(classify().category == Category::Armor, "Worn shield/clothing hierarchy not selected");
		actor.biped->actorRef.owner = nullptr;
		Require(classify().category == Category::None, "Unowned equipment tree did not fail closed");
		RE::Actor other;
		actor.biped->actorRef.owner = &other;
		Require(classify().category == Category::None, "Another actor's equipment tree was accepted");
		actor.biped->actorRef.owner = &actor;
		Require(ClassifyCharacterMaterial(&property, &geometry, &actor, false).category == Category::None, "Disabled equipment performs classification");
		actor.skin = &armor;
		Require(classify().category == Category::None, "Race skin armour misclassified as worn armour");
		Require(ClassifyCharacterMaterial(&property, &geometry, &actor, false, true).category == Category::Skin,
			"Owned creature body must use Skin without requiring equipment selection");
		geometry.parent = nullptr;
		Require(ClassifyCharacterMaterial(&property, &geometry, &actor, false, true).category == Category::None,
			"Unrelated geometry must not inherit creature body ownership");
		geometry.parent = &child;
		actor.skin = nullptr;
		actor.biped->objects[9].item = &weapon;
		Require(classify().category == Category::Weapons, "Attached weapon not selected");
		Require(ClassifyCharacterMaterial(&property, &geometry, &actor, false, true).category == Category::None,
			"Creature-body selection must not include weapons");
		actor.biped->objects[9].item = &ammo;
		Require(classify().category == Category::Weapons, "Attached ammunition not selected");
		geometry.parent = nullptr;
		Require(classify().category == Category::None, "Unrelated geometry inherits equipped item category");
		geometry.parent = &child;
		property.material.feature = RE::BSShaderMaterial::Feature::kHairTint;
		Require(classify().category == Category::Hair, "Equipment overrides hair material");
		property.material.feature = RE::BSShaderMaterial::Feature::kFaceGenRGBTint;
		RE::NiAVObject face;
		actor.face = &face;
		Require(classify().category == Category::Skin, "Equipment overrides exposed skin");
		property.material.feature = RE::BSShaderMaterial::Feature::kNone;
		geometry.data.alphaProperty = std::make_shared<RE::NiAlphaProperty>();
		geometry.data.alphaProperty->test = true;
		Require(classify().category == Category::Weapons, "Opaque alpha-tested equipment rejected");
		geometry.data.alphaProperty->blend = true;
		Require(classify().category == Category::None, "Blended equipment admitted");
		geometry.data.alphaProperty.reset();
		property.alpha = 0.5f;
		Require(classify().category == Category::None, "Implicit translucent equipment admitted");
		property.alpha = 1.0f;
		property.lighting = false;
		Require(classify().category == Category::None, "Non-lighting equipment admitted");
		property.lighting = true;
		child.parent = &child;
		Require(classify().category == Category::None, "Cyclic ancestry broadened equipment mask");
		child.parent = root.get();
		actor.biped.reset();
		Require(classify().category == Category::None, "Missing equipment tree did not fail closed");
	}

	void ActorGroups()
	{
		using Group = NeuralRendering::CharacterActorGroup;
		using Reason = NeuralRendering::CharacterActorGroupReason;
		RE::BGSKeyword npc, animal, troll, creature, undead, daedra, dragon, construct;
		actorRaceKeywords = { &npc, &animal, &troll, { &creature, &undead, &daedra, &dragon, &construct } };
		RE::TESRace nord{ 0x13746, nullptr, { &npc } };
		RE::TESRace vampire{ 0x88794, &nord, { &npc, &undead } };
		RE::TESRace elf{ 0x13743, nullptr, { &npc } };
		RE::TESRace khajiit{ 0x13745, nullptr, { &npc } };
		RE::TESRace wolf{ 0x1320A, nullptr, { &animal, &creature } };
		RE::TESRace trollRace{ 0x13205, nullptr, { &animal, &creature, &troll } };
		RE::TESRace werewolf{ 0xCDD84, nullptr, { &creature } };
		RE::TESRace unknown{ 0x01001234 };
		Require(ClassifyActorRace(&nord).group == Group::Humans, "Human race not recognized");
		Require(ClassifyActorRace(&vampire).group == Group::Humans, "Human vampire lost racial group");
		Require(ClassifyActorRace(&elf).group == Group::OtherHumanoids, "Elf not classified as humanoid");
		Require(ClassifyActorRace(&khajiit).group == Group::OtherHumanoids, "Khajiit must not become an animal");
		Require(ClassifyActorRace(&wolf).group == Group::Animals, "Animal/creature overlap must prefer animal");
		Require(ClassifyActorRace(&trollRace).group == Group::Creatures && ClassifyActorRace(&trollRace).reason == Reason::TrollKeyword,
			"Troll keyword must override animal");
		Require(ClassifyActorRace(&werewolf).group == Group::Creatures, "Werewolf not classified as creature");
		Require(ClassifyActorRace(&unknown).group == Group::Other && ClassifyActorRace(&unknown).reason == Reason::UnclassifiedRace,
			"Unknown race must remain explicit");
		Require(ClassifyActorRace(nullptr).reason == Reason::MissingRace, "Missing race not reported");
		for (auto* keyword : { &undead, &daedra, &dragon, &construct }) {
			unknown.keywords = { keyword };
			Require(ClassifyActorRace(&unknown).group == Group::Creatures, "Non-animal creature keyword ignored");
		}
		unknown.morphRace = &nord;
		unknown.keywords = { &creature };
		Require(ClassifyActorRace(&unknown).group == Group::Creatures, "Creature must not inherit humanoid skin ancestry");
		unknown.keywords.clear();
		Require(ClassifyActorRace(&unknown).group == Group::Humans, "Custom human morph descendant not recognized");
		unknown.morphRace = &unknown;
		Require(!HasHumanMorphLineage(&unknown), "Cyclic morph lineage did not stop");
		unknown.keywords = { &animal };
		Require(GetActorGroup(1, &unknown).group == Group::Animals, "Initial cached classification incorrect");
		unknown.keywords = { &creature };
		Require(GetActorGroup(1, &unknown).group == Group::Animals, "One frame used different race classifications");
		Require(GetActorGroup(2, &unknown).group == Group::Creatures, "Runtime keyword change was not refreshed");
		Require(GetActorGroup(2, &nord).group == Group::Humans, "Race transformation reused the old race entry");
	}
}
int main()
{
	try {
		EquipmentClassification();
		ActorGroups();
		std::cout << "Actor equipment classification passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
