#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include "Features/SnowCover/Foliage.h"

struct State
{
#include "snow_flags_under_test.h"

	struct
	{
		uint32_t ExtraShaderDescriptor = 0;
	} permutationData;
};

namespace globals
{
	State storage;
	State* state = &storage;
}

namespace RE
{
	enum class FormType
	{
		None,
		ActorCharacter,
		Static,
		Tree,
		Flora
	};
	struct TESModel
	{
		const char* path = nullptr;
		const char* GetModel() const { return path; }
	};
	struct Base : TESModel
	{
		FormType type = FormType::Tree;
		FormType GetFormType() const { return type; }
		template <class T>
		T* As()
		{
			return static_cast<T*>(this);
		}
		bool IsBoundAnimObject() const { return false; }
	};
	struct TESObjectREFR
	{
		Base* base = nullptr;
		FormType GetFormType() const { return FormType::Static; }
		bool CanBeMoved() const { return false; }
		Base* GetBaseObject() const { return base; }
	};
	struct BSShaderProperty
	{
		enum class EShaderPropertyFlag : uint32_t
		{
			kTwoSided = 1,
			kSoftLighting = 2
		};
		struct
		{
			uint32_t bits = 0;
			bool any(EShaderPropertyFlag flag) const { return (bits & static_cast<uint32_t>(flag)) != 0; }
		} flags;
	};
	struct NiAlphaProperty
	{
		bool testing = true;
		bool GetAlphaTesting() const { return testing; }
	};
	struct Geometry
	{
		std::string name;
		TESObjectREFR* reference = nullptr;
		NiAlphaProperty alpha;
		struct RuntimeData
		{
			struct
			{
				NiAlphaProperty* value;
				NiAlphaProperty* get() const { return value; }
			} alphaProperty;
		} data{ { &alpha } };
		RuntimeData& GetGeometryRuntimeData() { return data; }
		TESObjectREFR* GetUserData() const { return reference; }
		bool HasAnimation() const { return false; }
	};
	struct BSShader
	{
		enum class Type
		{
			Lighting,
			Other
		};
		struct
		{
			Type value = Type::Lighting;
			Type get() const { return value; }
		} shaderType;
	};
	struct BSRenderPass
	{
		BSShader* shader;
		Geometry* geometry;
		BSShaderProperty* shaderProperty = nullptr;
	};
}

namespace SnowEnvironment
{
	bool IsHarvestableFlora(RE::TESObjectREFR*) { return false; }
	// clang-format off
#include "snow_equals_under_test.h"
#include "snow_lod_tree_under_test.h"
#include "snow_foliage_under_test.h"
	// clang-format on
}

struct SnowCover
{
	std::atomic_bool active = true;
	struct
	{
		bool EnableSnowCover = true;
	} wsettings;
	struct
	{
		bool AffectHavok = false, AffectFloraTint = true, EnableRooftops = false, EnableBoundsCulling = false;
		float RooftopSnow = 0;
	} settings;
	bool texturesLoaded = true;
	std::unordered_set<std::string_view> blacklist, whitelist;
	bool IsBuilding(RE::TESObjectREFR*) const { return false; }
	bool IsSnowFree(const RE::BSRenderPass*, uint32_t) const { return false; }
	uint32_t BeginPass(const RE::BSRenderPass* pass);
	void EndPass(uint32_t previous);
};

#include "snow_mask_under_test.h"
#include "snow_pass_under_test.h"

int main()
{
	const auto require = [](bool value) {
		if (!value)
			throw std::runtime_error("Snow per-draw flags leaked or failed restoration");
	};
	using Flag = State::ExtraShaderDescriptors;
	constexpr uint32_t foliage = static_cast<uint32_t>(Flag::SnowFoliage);
	constexpr uint32_t inWorld = static_cast<uint32_t>(Flag::InWorld);
	require((PassMask & foliage) != 0);
	SnowCover snow;
	RE::BSShader shader;
	RE::BSShaderProperty property;
	RE::Base base;
	RE::TESObjectREFR reference{ &base };
	RE::Geometry leaf{ "leaf", &reference }, bark{ "bark", &reference };
	bark.alpha.testing = false;
	RE::BSRenderPass pass{ &shader, &leaf, &property };
	require(SnowEnvironment::IsFoliageCard(&pass));
	base.type = RE::FormType::Flora;
	require(SnowEnvironment::IsFoliageCard(&pass));
	base.type = RE::FormType::Static;
	base.path = "plants/shrub.nif";
	require(!SnowEnvironment::IsFoliageCard(&pass));
	property.flags.bits = 1;
	require(SnowEnvironment::IsFoliageCard(&pass));
	for (const auto* path : { "architecture/fence.nif", "armor/hair.nif", "clutter/cloth.nif", "landscape/rock.nif" }) {
		base.path = path;
		require(!SnowEnvironment::IsFoliageCard(&pass));
	}
	property.flags.bits = 2;
	require(SnowEnvironment::IsFoliageCard(&pass));
	property.flags.bits = 1;
	leaf.reference = nullptr;
	leaf.name = "objHD-LargeRef";
	require(SnowEnvironment::IsFoliageCard(&pass));
	leaf.name = "unrelated";
	require(!SnowEnvironment::IsFoliageCard(&pass));
	leaf.reference = &reference;
	base.type = RE::FormType::Tree;
	property.flags.bits = 0;
	pass.shaderProperty = nullptr;
	require(!SnowEnvironment::IsFoliageCard(&pass));
	pass.shaderProperty = &property;
	leaf.data.alphaProperty.value = nullptr;
	require(!SnowEnvironment::IsFoliageCard(&pass));
	leaf.data.alphaProperty.value = &leaf.alpha;
	require(!SnowEnvironment::IsFoliageCard(nullptr));
	pass.geometry = nullptr;
	require(!SnowEnvironment::IsFoliageCard(&pass));
	pass.geometry = &leaf;
	auto& flags = globals::state->permutationData.ExtraShaderDescriptor;
	flags = inWorld;
	const auto previous = snow.BeginPass(&pass);
	require(flags == (inWorld | foliage));
	pass.geometry = &bark;
	const auto nested = snow.BeginPass(&pass);
	require(flags == inWorld);
	snow.EndPass(nested);
	require(flags == (inWorld | foliage));
	for (int inactive = 0; inactive < 6; ++inactive) {
		snow.active = inactive != 0;
		snow.wsettings.EnableSnowCover = inactive != 1;
		snow.texturesLoaded = inactive != 2;
		pass.shader = inactive == 3 ? nullptr : &shader;
		shader.shaderType.value = inactive == 4 ? RE::BSShader::Type::Other : RE::BSShader::Type::Lighting;
		pass.geometry = inactive == 5 ? nullptr : &leaf;
		const auto saved = snow.BeginPass(&pass);
		require(flags == inWorld);
		snow.EndPass(saved);
		require(flags == (inWorld | foliage));
	}
	const auto saved = snow.BeginPass(nullptr);
	require(flags == inWorld);
	snow.EndPass(saved);
	snow.EndPass(previous);
	require(flags == inWorld);
}
