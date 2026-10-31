#include "Features/MeshBlendingLandscapeSnapshot.h"

#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
	namespace Landscape = CSX::MeshBlendingLandscapePolicy;
	bool countAllocations = false;
	std::size_t allocationCount = 0u;

	Landscape::CapturedTexture Texture(std::string_view form, std::string_view editor, std::string_view diffuse, std::string_view material)
	{
		Landscape::CapturedTexture result;
		result.present = true;
		result.form = Landscape::CanonicalFormKey(form);
		result.editorID = Landscape::NormalizeIdentity(editor);
		result.diffuse = Landscape::NormalizeTexturePath(diffuse);
		if (!material.empty()) {
			result.materialCount = 1u;
			result.materials[0].name = Landscape::NormalizeIdentity(material);
		}
		return result;
	}

	Landscape::SnapshotHeader Header(std::uint64_t serial = 1u)
	{
		return { 71u, serial,
			{ 0x1000u, 0x2000u, 0x3000u, 0x4000u, Landscape::MaterialLayout::kVanilla, 2u, 1u, 0u,
				{ 0x5000u, 0x6000u, 0u, 0u, 0u, 0u } } };
	}

	std::array<Landscape::CapturedTexture, Landscape::LayerCount> Textures()
	{
		return { Texture("0x123~Terrain.esp", "SnowLayer", "Landscape/Snow.dds", "Snow"),
			Texture("0x456~Terrain.esp", "StoneLayer", "Landscape/Stone.dds", "Stone") };
	}

	bool TestLandscapeSelectorPolicy()
	{
		using CSX::MeshBlendingLandscapePolicy::HasLandscapeSelector;
		using CSX::MeshBlendingPolicy::TrimAsciiSpaces;

		return !HasLandscapeSelector({}, {}, {}) &&
		       !HasLandscapeSelector("   ", " ", "  ") &&
		       HasLandscapeSelector(" 0x1~Plugin.esp ", {}, {}) &&
		       HasLandscapeSelector({}, " EditorID ", {}) &&
		       HasLandscapeSelector({}, {}, " textures/example.dds ") &&
		       TrimAsciiSpaces("  value  ") == "value" &&
		       TrimAsciiSpaces("   ").empty();
	}

	bool TestCanonicalLandscapePolicy()
	{
		using CSX::MeshBlendingLandscapePolicy::CanonicalizeLandscapeSelectors;

		for (const std::string_view diffuse : { "/", "./", "data/", "textures/", "////" }) {
			const auto canonical = CanonicalizeLandscapeSelectors({}, {}, diffuse);
			if (!canonical.DiffuseCollapsed() || canonical.HasSelector()) {
				return false;
			}
		}

		const auto valid = CanonicalizeLandscapeSelectors(
			" 0x1~Plugin.esp ",
			" EditorID ",
			" Data//Textures/Landscape/Test.DDS/ ");
		const auto stable = CanonicalizeLandscapeSelectors(valid.form, valid.editorID, valid.diffuse);
		return valid.HasSelector() && valid.form == "0x1~Plugin.esp" &&
		       valid.editorID == "EditorID" && valid.diffuse == "textures/landscape/test.dds" &&
		       valid.form == stable.form && valid.editorID == stable.editorID && valid.diffuse == stable.diffuse;
	}

	bool TestPortableForms()
	{
		Landscape::Policy policy;
		policy.assignments = {
			{ "Ash", "0X000123~TERRAIN.ESP", {}, {} },
			{ "Mud", "0x000ABC~LIGHT.ESL", {}, {} },
			{ "Gravel", "0xABCDEF~FULL.ESM", {}, {} }
		};
		const auto compiled = Landscape::CompilePolicy(policy);
		return Landscape::CanonicalFormKey("0x123~terrain.esp") == "0x123~terrain.esp" &&
		       Landscape::ClassifyTexture(Texture("0x123~Terrain.esp", {}, {}, "Stone"), *compiled) == Landscape::MaterialKind::kAsh &&
		       Landscape::ClassifyTexture(Texture("0xABC~Light.esl", {}, {}, "Stone"), *compiled) == Landscape::MaterialKind::kMud &&
		       Landscape::ClassifyTexture(Texture("0xABCDEF~Full.esm", {}, {}, "Stone"), *compiled) == Landscape::MaterialKind::kGravel &&
		       Landscape::CanonicalFormKey("0x1000000~Full.esm").empty() &&
		       Landscape::CanonicalFormKey("0x0~Full.esm").empty() &&
		       Landscape::CanonicalFormKey("0x12garbage~Full.esm").empty() &&
		       Landscape::CanonicalFormKey("0x123~").empty();
	}

	bool TestDescriptorIsolation()
	{
		static_assert((Landscape::ClassMask & 0x000007FFu) == 0u);
		std::uint32_t shader = CSX::MeshBlendingPolicy::EnableMask | 0x100u;
		std::uint32_t feature = 0xFFFFFFFFu;
		CSX::MeshBlendingPolicy::ClearDrawDescriptors(shader, feature, Landscape::ClassMask);
		return shader == 0x100u && feature == (0xFFFFFFFFu & ~Landscape::ClassMask);
	}

	bool TestSelectorPrecedenceAndParents()
	{
		Landscape::Policy policy;
		policy.assignments = {
			{ "Unknown", "0x123~Terrain.esp", {}, {} },
			{ "Grass", {}, "Layer", {} },
			{ "Sand", {}, {}, "Data\\Textures\\Landscape\\test.dds" }
		};
		policy.surfaceMaterials = { { "0x000AAA~Materials.esm", "Glass" } };
		const auto compiled = Landscape::CompilePolicy(policy);
		auto texture = Texture("0x123~Terrain.esp", "Layer", "Landscape/test.dds", "Snow");
		if (Landscape::ClassifyTexture(texture, *compiled) != Landscape::MaterialKind::kUnknown)
			return false;
		texture.form.clear();
		if (Landscape::ClassifyTexture(texture, *compiled) != Landscape::MaterialKind::kGrass)
			return false;
		texture.editorID.clear();
		if (Landscape::ClassifyTexture(texture, *compiled) != Landscape::MaterialKind::kSand)
			return false;
		texture.diffuse.clear();
		texture.materialCount = 2u;
		texture.materials[1].form = Landscape::CanonicalFormKey("0xAAA~Materials.esm");
		if (Landscape::ClassifyTexture(texture, *compiled) != Landscape::MaterialKind::kGlass)
			return false;
		texture.materialChainValid = false;
		if (Landscape::ClassifyTexture(texture, *compiled) != Landscape::MaterialKind::kUnknown)
			return false;
		texture.materialChainValid = true;
		texture.materials[1].form.clear();
		return Landscape::ClassifyTexture(texture, *compiled) == Landscape::MaterialKind::kSnow &&
		       Landscape::ClassifyName("sandstone") == Landscape::MaterialKind::kStone &&
		       Landscape::ClassifyName("softwood") == Landscape::MaterialKind::kWood;
	}

	bool TestPairPolicy()
	{
		const auto textures = Textures();
		const auto defaults = Landscape::CompilePolicy({});
		const auto expected = (2u << Landscape::ClassShift) | (1u << (Landscape::ClassShift + 2u));
		if (Landscape::ClassifyQuadrant(textures, *defaults) != expected)
			return false;
		Landscape::Policy policy;
		policy.blendPairsExplicit = true;
		if (Landscape::ClassifyQuadrant(textures, *Landscape::CompilePolicy(policy)) != 0u)
			return false;
		policy.blendPairs = { { "Snow", "Stone" } };
		if (Landscape::ClassifyQuadrant(textures, *Landscape::CompilePolicy(policy)) != expected)
			return false;
		auto reversed = textures;
		std::swap(reversed[0], reversed[1]);
		if (Landscape::ClassifyQuadrant(reversed, *Landscape::CompilePolicy(policy)) != ((1u << Landscape::ClassShift) | (2u << (Landscape::ClassShift + 2u))))
			return false;
		policy.blendPairs = { { "Soft", "Hard" } };
		return Landscape::ClassifyQuadrant(textures, *Landscape::CompilePolicy(policy)) == expected;
	}

	bool TestSnapshotOwnershipAndRevocation()
	{
		const auto header = Header();
		const auto words = Landscape::EncodeSnapshot(header, Textures());
		if (!words)
			return false;
		Landscape::SnapshotHeader decoded;
		if (!Landscape::ReadSnapshotHeader(*words, decoded))
			return false;
		const auto publication = Landscape::EncodePublication(header);
		if (!Landscape::MatchesPublication(publication, decoded, header.draw, header.session))
			return false;
		// Every recorded object stays live; removing the owner's token revokes the old snapshot.
		if (Landscape::MatchesPublication({}, decoded, header.draw, header.session))
			return false;
		const auto replacement = Landscape::EncodePublication(Header(2u));
		if (Landscape::MatchesPublication(replacement, decoded, header.draw, header.session))
			return false;
		if (Landscape::MatchesPublication(publication, decoded, header.draw, header.session + 1u))
			return false;
		for (const auto member : { &Landscape::DrawIdentity::root, &Landscape::DrawIdentity::geometry,
				 &Landscape::DrawIdentity::shaderProperty, &Landscape::DrawIdentity::material }) {
			auto clone = header.draw;
			++(clone.*member);
			if (Landscape::MatchesPublication(publication, decoded, clone, header.session))
				return false;
		}
		auto rebound = header.draw;
		++rebound.textures[0];
		if (Landscape::MatchesPublication(publication, decoded, rebound, header.session))
			return false;
		auto shifted = header;
		shifted.draw.childSlots = 4u;
		shifted.draw.childIndex = 3u;
		const auto shiftedWords = Landscape::EncodeSnapshot(shifted, Textures());
		if (!shiftedWords || !Landscape::ReadSnapshotHeader(*shiftedWords, decoded) ||
			!Landscape::MatchesPublication(publication, decoded, shifted.draw, header.session))
			return false;
		shifted.draw.childIndex = 2u;
		return !Landscape::MatchesPublication(publication, decoded, shifted.draw, header.session);
	}

	bool TestSnapshotValidation()
	{
		const auto encoded = Landscape::EncodeSnapshot(Header(), Textures());
		if (!encoded)
			return false;
		std::array<Landscape::TextureView, Landscape::LayerCount> views{};
		if (!Landscape::DecodeTextureViews(*encoded, views) || views[0].form != "0x123~terrain.esp" || views[1].materials[0].name != "stone")
			return false;
		for (std::size_t length = 0u; length < encoded->size(); ++length) {
			if (Landscape::DecodeTextureViews(std::span(*encoded).first(length), views))
				return false;
		}
		for (const auto index : { 0u, 1u, 2u, 3u, 19u }) {
			auto malformed = *encoded;
			++malformed[index];
			if (Landscape::DecodeTextureViews(malformed, views))
				return false;
		}
		auto malformed = *encoded;
		Landscape::WriteWord(malformed, Landscape::SnapshotHeaderWords + 2u, 9u);
		if (Landscape::DecodeTextureViews(malformed, views))
			return false;
		malformed = *encoded;
		Landscape::WriteWord(malformed, Landscape::SnapshotHeaderWords + 3u, 0xFFFFFFFFu);
		if (Landscape::DecodeTextureViews(malformed, views))
			return false;
		auto oversized = Textures();
		oversized[0].form.assign(Landscape::MaximumIdentityLength + 1u, 'x');
		if (Landscape::EncodeSnapshot(Header(), oversized))
			return false;
		for (auto& texture : oversized) {
			texture = {};
			texture.materialCount = Landscape::MaximumMaterialDepth;
			for (auto& material : texture.materials)
				material.form = material.editorID = material.name = std::string(Landscape::MaximumIdentityLength, 'x');
		}
		return !Landscape::EncodeSnapshot(Header(), oversized);
	}

	bool TestEvictionAndPolicyRefresh()
	{
		const auto textures = Textures();
		const auto initial = Landscape::CompilePolicy({});
		Landscape::ClassificationCache<1u, 4u> cache;
		std::uint32_t classes = 0u;
		const auto expected = Landscape::ClassifyQuadrant(textures, *initial);
		for (std::uint64_t token = 1u; token <= 5u; ++token) {
			const auto header = Header(token);
			const auto words = Landscape::EncodeSnapshot(header, textures);
			if (!words || !cache.Resolve(*words, header, *initial, classes) || classes != expected)
				return false;
		}
		// Five live publications share four cache slots; all recover from their immutable owners.
		for (std::uint64_t token = 1u; token <= 5u; ++token) {
			const auto header = Header(token);
			const auto words = Landscape::EncodeSnapshot(header, textures);
			if (!cache.Resolve(*words, header, *initial, classes) || classes != expected)
				return false;
		}
		const auto header = Header();
		const auto words = Landscape::EncodeSnapshot(header, textures);
		Landscape::Policy changed;
		changed.assignments = { { "Stone", "0x123~Terrain.esp", {}, {} } };
		const auto staged = Landscape::CompilePolicy(changed);
		if (!cache.Resolve(*words, header, *initial, classes) || classes != expected)
			return false;
		if (!cache.Resolve(*words, header, *staged, classes) || classes == expected || classes != Landscape::ClassifyQuadrant(textures, *staged))
			return false;
		changed.blendPairsExplicit = true;
		const auto denied = Landscape::CompilePolicy(changed);
		if (!cache.Resolve(*words, header, *denied, classes) || classes != 0u)
			return false;
		allocationCount = 0u;
		countAllocations = true;
		const bool hit = cache.Resolve(*words, header, *denied, classes);
		const bool miss = cache.Resolve(*words, header, *initial, classes);
		countAllocations = false;
		return hit && miss && classes == expected && allocationCount == 0u;
	}
}

#if defined(_MSC_VER)
#	define TEST_NOINLINE __declspec(noinline)
#else
#	define TEST_NOINLINE __attribute__((noinline))
#endif

TEST_NOINLINE void* operator new(std::size_t a_size)
{
	if (countAllocations)
		++allocationCount;
	if (void* result = std::malloc(a_size == 0u ? 1u : a_size))
		return result;
	throw std::bad_alloc();
}

TEST_NOINLINE void operator delete(void* a_pointer) noexcept { std::free(a_pointer); }
TEST_NOINLINE void operator delete(void* a_pointer, std::size_t) noexcept { std::free(a_pointer); }

#undef TEST_NOINLINE

int main()
{
	const std::array tests{
		std::pair{ "effective LAND selectors", &TestLandscapeSelectorPolicy },
		std::pair{ "canonical LAND selector paths", &TestCanonicalLandscapePolicy },
		std::pair{ "LAND descriptor ownership", &TestDescriptorIsolation },
		std::pair{ "portable form selectors before game data exists", &TestPortableForms },
		std::pair{ "selector precedence and bounded material parents", &TestSelectorPrecedenceAndParents },
		std::pair{ "six-layer pair policy", &TestPairPolicy },
		std::pair{ "owner revocation, cloned owners, sessions and texture replacement", &TestSnapshotOwnershipAndRevocation },
		std::pair{ "metadata bounds, corruption and truncation", &TestSnapshotValidation },
		std::pair{ "collision recovery, staged policy refresh and allocation-free reads", &TestEvictionAndPolicyRefresh }
	};
	for (const auto& [name, run] : tests) {
		if (!run()) {
			std::fprintf(stderr, "FAIL: %s\n", name);
			return 1;
		}
		std::printf("PASS: %s\n", name);
	}
	return 0;
}
