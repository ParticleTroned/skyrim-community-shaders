#include <array>
#include <iostream>
#include <stdexcept>

namespace RE
{
	enum class FormType
	{
		Static,
		Land,
		Tree
	};
	struct Form
	{
		FormType type = FormType::Static;
		FormType GetFormType() const { return type; }
	};
	struct TESObjectREFR
	{
		bool actor = false;
		Form* base = nullptr;
		bool IsActor() const { return actor; }
		const Form* GetBaseObject() const { return base; }
	};
	struct NiAVObject
	{
		virtual ~NiAVObject() = default;
		NiAVObject* parent = nullptr;
		TESObjectREFR* reference = nullptr;
		void* controllers = nullptr;
		void* GetControllers() const { return controllers; }
	};
	struct BSShaderMaterial
	{
		enum class Type
		{
			kLighting,
			kEffect
		};
		Type type = Type::kLighting;
		Type GetType() const { return type; }
	};
	struct BSShaderProperty : NiAVObject
	{
		enum class EShaderPropertyFlag
		{
			kTreeAnim,
			kBillboard,
			kProjectedUV,
			kRefraction,
			kTempRefraction
		};
		BSShaderMaterial* material = nullptr;
		struct Flags
		{
			bool protectedFlag = false;
			template <class... T>
			bool any(T...) const
			{
				return protectedFlag;
			}
		} flags;
	};
	struct BSGeometry : NiAVObject
	{
		struct Data
		{
			struct Pointer
			{
				BSShaderProperty* value = nullptr;
				BSShaderProperty* get() const { return value; }
			} shaderProperty;
			void* skinInstance = nullptr;
		} data;
		const Data& GetGeometryRuntimeData() const { return data; }
	};
	struct BSDynamicTriShape : BSGeometry
	{};
}
template <class To, class From>
To netimmerse_cast(From* object)
{
	return dynamic_cast<To>(object);
}
namespace Util
{
	RE::TESObjectREFR* GetOwnUserData(const RE::NiAVObject* object) { return object->reference; }
}
namespace C
{
#include "shadow_culling_caster.h"
}
void Require(bool condition)
{
	if (!condition)
		throw std::runtime_error("Shadow caster classification regression");
}
int main()
{
	try {
		RE::BSShaderMaterial material;
		RE::BSShaderProperty property;
		property.material = &material;
		RE::Form tree{ RE::FormType::Tree };
		RE::TESObjectREFR actor{ true }, nonActor{ false, &tree };
		RE::NiAVObject root;
		root.reference = &actor;
		RE::BSGeometry geometry;
		geometry.parent = &root;
		geometry.data = { { &property }, &geometry };
		auto result = C::Classify(geometry);
		Require(result.actor && result.actorReference == &actor && result.actorRoot == &root && !result.protectedGeometry);
		// A nearer non-actor owner wins, even when the mesh is skinned under an actor ancestor.
		geometry.reference = &nonActor;
		result = C::Classify(geometry);
		Require(!result.actor && !result.actorReference && result.dynamic);
		geometry.reference = nullptr;
		root.reference = &nonActor;
		Require(!C::Classify(geometry).actorReference);
		root.reference = &actor;
		property.flags.protectedFlag = true;
		Require(C::Classify(geometry).protectedGeometry);
		property.flags.protectedFlag = false;
		property.controllers = &property;
		Require(C::Classify(geometry).protectedGeometry);
		property.controllers = nullptr;
		material.type = RE::BSShaderMaterial::Type::kEffect;
		Require(C::Classify(geometry).protectedGeometry);
		material.type = RE::BSShaderMaterial::Type::kLighting;
		property.material = nullptr;
		Require(C::Classify(geometry).protectedGeometry);
		property.material = &material;
		geometry.data.shaderProperty.value = nullptr;
		Require(C::Classify(geometry).protectedGeometry);
		geometry.data.shaderProperty.value = &property;
		std::array<RE::NiAVObject, 32> chain;
		for (unsigned i = 1; i < chain.size(); ++i)
			chain[i - 1].parent = &chain[i];
		geometry.parent = &chain.front();
		chain.back().reference = &actor;
		Require(!C::Classify(geometry).actorReference);
		chain[30].reference = &actor;
		Require(C::Classify(geometry).actorReference == &actor);
		std::cout << "Shadow caster ownership and protection passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
