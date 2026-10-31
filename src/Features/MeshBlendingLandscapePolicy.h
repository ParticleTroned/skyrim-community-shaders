#pragma once

#include "MeshBlendingPolicy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace CSX::MeshBlendingLandscapePolicy
{
	inline constexpr std::size_t LayerCount = 6u;
	inline constexpr std::size_t MaximumMaterialDepth = 8u;
	inline constexpr std::size_t MaximumIdentityLength = 512u;
	inline constexpr std::uint32_t ClassShift = 11u;
	inline constexpr std::uint32_t ClassBits = 2u;
	inline constexpr std::uint32_t ClassMask = 0x007FF800u;

	[[nodiscard]] inline std::string NormalizeTexturePath(std::string_view a_path)
	{
		std::string normalized;
		normalized.reserve(a_path.size() + 9u);
		MeshBlendingPolicy::detail::VisitNormalizedPath(a_path, "textures", [&](char a_character) {
			normalized.push_back(a_character);
			return true;
		});
		return normalized;
	}

	[[nodiscard]] constexpr bool HasLandscapeSelector(
		std::string_view a_form,
		std::string_view a_editorID,
		std::string_view a_diffuse) noexcept
	{
		return !MeshBlendingPolicy::TrimAsciiSpaces(a_form).empty() ||
		       !MeshBlendingPolicy::TrimAsciiSpaces(a_editorID).empty() ||
		       !MeshBlendingPolicy::TrimAsciiSpaces(a_diffuse).empty();
	}

	struct CanonicalLandscapeSelectors
	{
		std::string form;
		std::string editorID;
		std::string diffuse;
		bool diffuseWasSupplied = false;

		[[nodiscard]] bool DiffuseCollapsed() const noexcept
		{
			return diffuseWasSupplied && diffuse.empty();
		}

		[[nodiscard]] bool HasSelector() const noexcept
		{
			return !form.empty() || !editorID.empty() || !diffuse.empty();
		}
	};

	[[nodiscard]] inline CanonicalLandscapeSelectors CanonicalizeLandscapeSelectors(
		std::string_view a_form,
		std::string_view a_editorID,
		std::string_view a_diffuse)
	{
		const auto trimmedDiffuse = MeshBlendingPolicy::TrimAsciiSpaces(a_diffuse);
		return {
			.form = std::string(MeshBlendingPolicy::TrimAsciiSpaces(a_form)),
			.editorID = std::string(MeshBlendingPolicy::TrimAsciiSpaces(a_editorID)),
			.diffuse = NormalizeTexturePath(trimmedDiffuse),
			.diffuseWasSupplied = !trimmedDiffuse.empty(),
		};
	}

	enum class MaterialKind : std::uint8_t
	{
		kUnknown,
		kSoft,
		kSnow,
		kDirt,
		kAsh,
		kGravel,
		kMud,
		kSand,
		kGrass,
		kGroundCover,
		kHard,
		kIce,
		kStone,
		kBoulder,
		kWood,
		kGlass,
		kMetal
	};

	enum class MaterialClass : std::uint8_t
	{
		kUnknown = 0u,
		kHard = 1u,
		kSoft = 2u
	};

	inline constexpr std::array<std::string_view, 17u> MaterialNames{
		"Unknown", "Soft", "Snow", "Dirt", "Ash", "Gravel", "Mud", "Sand", "Grass",
		"GroundCover", "Hard", "Ice", "Stone", "Boulder", "Wood", "Glass", "Metal"
	};

	[[nodiscard]] inline std::string NormalizeIdentity(std::string_view a_value)
	{
		std::string result(a_value);
		std::transform(result.begin(), result.end(), result.begin(), MeshBlendingPolicy::LowerAscii);
		return result;
	}

	/** Canonical plugin-local identity; compilation does not require loaded game forms. */
	[[nodiscard]] inline std::string CanonicalFormKey(std::string_view a_value)
	{
		a_value = MeshBlendingPolicy::TrimAsciiSpaces(a_value);
		const auto separator = a_value.find('~');
		if (a_value.size() > MaximumIdentityLength || separator == std::string_view::npos ||
			separator != a_value.rfind('~') || separator <= 2u || separator + 1u == a_value.size() ||
			a_value[0] != '0' || MeshBlendingPolicy::LowerAscii(a_value[1]) != 'x')
			return {};
		if (std::any_of(a_value.begin(), a_value.end(), [](unsigned char character) { return character < 0x20u || character == 0x7Fu; }))
			return {};
		std::uint32_t localID = 0u;
		const auto parsed = std::from_chars(a_value.data() + 2u, a_value.data() + separator, localID, 16);
		if (parsed.ec != std::errc{} || parsed.ptr != a_value.data() + separator || localID == 0u || localID > 0x00FFFFFFu)
			return {};
		std::array<char, 8u> digits{};
		const auto formatted = std::to_chars(digits.data(), digits.data() + digits.size(), localID, 16);
		return std::string("0x").append(digits.data(), formatted.ptr).append("~").append(NormalizeIdentity(a_value.substr(separator + 1u)));
	}

	[[nodiscard]] constexpr std::string_view CanonicalMaterialKind(MaterialKind a_kind) noexcept
	{
		const auto index = static_cast<std::size_t>(a_kind);
		return index < MaterialNames.size() ? MaterialNames[index] : MaterialNames[0];
	}

	[[nodiscard]] inline std::optional<MaterialKind> ParseMaterialKind(std::string_view a_value)
	{
		auto normalized = NormalizeIdentity(a_value);
		std::erase_if(normalized, [](char character) { return character == ' ' || character == '-' || character == '_'; });
		for (std::size_t index = 0u; index < MaterialNames.size(); ++index) {
			if (NormalizeIdentity(MaterialNames[index]) == normalized)
				return static_cast<MaterialKind>(index);
		}
		return std::nullopt;
	}

	[[nodiscard]] constexpr MaterialClass GetMaterialClass(MaterialKind a_kind) noexcept
	{
		if (a_kind >= MaterialKind::kSoft && a_kind <= MaterialKind::kGroundCover)
			return MaterialClass::kSoft;
		if (a_kind >= MaterialKind::kHard && a_kind <= MaterialKind::kMetal)
			return MaterialClass::kHard;
		return MaterialClass::kUnknown;
	}

	struct LandscapeAssignment
	{
		std::string Kind;
		std::string Form;
		std::string EditorID;
		std::string Diffuse;
		bool operator==(const LandscapeAssignment&) const = default;
	};

	struct SurfaceMaterialRule
	{
		std::string Material;
		std::string Kind;
		bool operator==(const SurfaceMaterialRule&) const = default;
	};

	struct BlendPairRule
	{
		std::string Source;
		std::string Receiver;
		bool operator==(const BlendPairRule&) const = default;
	};

	struct Policy
	{
		std::vector<LandscapeAssignment> assignments;
		std::vector<SurfaceMaterialRule> surfaceMaterials;
		std::vector<BlendPairRule> blendPairs;
		bool blendPairsExplicit = false;
		bool operator==(const Policy&) const = default;
	};

	struct CompiledPolicy
	{
		std::map<std::string, MaterialKind, std::less<>> forms;
		std::map<std::string, MaterialKind, std::less<>> editorIDs;
		std::map<std::string, MaterialKind, std::less<>> diffusePaths;
		std::map<std::string, MaterialKind, std::less<>> surfaceMaterials;
		std::array<std::array<bool, MaterialNames.size()>, MaterialNames.size()> blendPairs{};
		std::size_t blendPairCount = 0u;
		std::uint64_t revision = 0u;
	};

	/** Stage every allocation before a rule-file transaction publishes its policy. */
	[[nodiscard]] inline std::shared_ptr<const CompiledPolicy> CompilePolicy(const Policy& a_policy)
	{
		auto result = std::make_shared<CompiledPolicy>();
		const auto requireKind = [](std::string_view value) {
			const auto kind = ParseMaterialKind(value);
			if (!kind)
				throw std::invalid_argument("Unknown LAND material kind");
			return *kind;
		};
		for (const auto& assignment : a_policy.assignments) {
			const auto kind = requireKind(assignment.Kind);
			if (!assignment.Form.empty()) {
				auto form = CanonicalFormKey(assignment.Form);
				if (form.empty())
					throw std::invalid_argument("Invalid LAND plugin-local form identity");
				result->forms.insert_or_assign(std::move(form), kind);
			}
			if (!assignment.EditorID.empty())
				result->editorIDs.insert_or_assign(NormalizeIdentity(assignment.EditorID), kind);
			if (!assignment.Diffuse.empty())
				result->diffusePaths.insert_or_assign(NormalizeTexturePath(assignment.Diffuse), kind);
		}
		for (const auto& rule : a_policy.surfaceMaterials) {
			auto identity = CanonicalFormKey(rule.Material);
			if (identity.empty())
				identity = NormalizeIdentity(rule.Material);
			if (identity.empty())
				throw std::invalid_argument("Empty LAND surface material identity");
			result->surfaceMaterials.insert_or_assign(std::move(identity), requireKind(rule.Kind));
		}
		const auto matches = [](MaterialKind actual, MaterialKind pattern) {
			return actual == pattern ||
			       (pattern == MaterialKind::kSoft && GetMaterialClass(actual) == MaterialClass::kSoft) ||
			       (pattern == MaterialKind::kHard && GetMaterialClass(actual) == MaterialClass::kHard);
		};
		const auto expandPair = [&](MaterialKind source, MaterialKind receiver) {
			for (std::size_t first = 1u; first < MaterialNames.size(); ++first) {
				for (std::size_t second = 1u; second < MaterialNames.size(); ++second) {
					if (matches(static_cast<MaterialKind>(first), source) && matches(static_cast<MaterialKind>(second), receiver) && !result->blendPairs[first][second]) {
						result->blendPairs[first][second] = true;
						++result->blendPairCount;
					}
				}
			}
		};
		if (a_policy.blendPairsExplicit) {
			for (const auto& rule : a_policy.blendPairs)
				expandPair(requireKind(rule.Source), requireKind(rule.Receiver));
		} else {
			expandPair(MaterialKind::kSoft, MaterialKind::kHard);
			expandPair(MaterialKind::kSoft, MaterialKind::kSoft);
			expandPair(MaterialKind::kHard, MaterialKind::kHard);
		}
		static std::atomic_uint64_t nextRevision = 1u;
		result->revision = nextRevision.fetch_add(1u, std::memory_order_relaxed);
		return result;
	}

	template <class String>
	struct SurfaceIdentity
	{
		String form;
		String editorID;
		String name;
	};

	template <class String>
	struct TextureIdentity
	{
		String form;
		String editorID;
		String diffuse;
		std::array<SurfaceIdentity<String>, MaximumMaterialDepth> materials{};
		std::size_t materialCount = 0u;
		bool materialChainValid = true;
		bool present = false;
	};

	using CapturedTexture = TextureIdentity<std::string>;
	using TextureView = TextureIdentity<std::string_view>;

	/** Engine material names are heuristic fallbacks after all exact selectors. */
	[[nodiscard]] inline MaterialKind ClassifyName(std::string_view a_name) noexcept
	{
		const auto contains = [&](std::string_view token) { return a_name.find(token) != std::string_view::npos; };
		if (contains("snow"))
			return MaterialKind::kSnow;
		if (contains("ash"))
			return MaterialKind::kAsh;
		if (contains("dirt") || contains("soil") || contains("earth"))
			return MaterialKind::kDirt;
		if (contains("gravel"))
			return MaterialKind::kGravel;
		if (contains("mud"))
			return MaterialKind::kMud;
		if (contains("sandstone"))
			return MaterialKind::kStone;
		if (contains("sand"))
			return MaterialKind::kSand;
		if (contains("grass"))
			return MaterialKind::kGrass;
		if (contains("groundcover") || contains("ground cover") || contains("moss") || contains("lichen"))
			return MaterialKind::kGroundCover;
		if (contains("ice"))
			return MaterialKind::kIce;
		if (contains("boulder"))
			return MaterialKind::kBoulder;
		if (contains("stone") || contains("rock"))
			return MaterialKind::kStone;
		if (contains("wood") || contains("timber"))
			return MaterialKind::kWood;
		if (contains("glass"))
			return MaterialKind::kGlass;
		if (contains("metal") || contains("iron") || contains("steel") || contains("copper") || contains("bronze"))
			return MaterialKind::kMetal;
		if (contains("soft"))
			return MaterialKind::kSoft;
		if (contains("hard"))
			return MaterialKind::kHard;
		return MaterialKind::kUnknown;
	}

	template <class String>
	[[nodiscard]] MaterialKind ClassifyTexture(const TextureIdentity<String>& a_texture, const CompiledPolicy& a_policy)
	{
		if (!a_texture.present)
			return MaterialKind::kUnknown;
		if (const auto found = a_policy.forms.find(std::string_view(a_texture.form)); found != a_policy.forms.end())
			return found->second;
		if (const auto found = a_policy.editorIDs.find(std::string_view(a_texture.editorID)); found != a_policy.editorIDs.end())
			return found->second;
		if (const auto found = a_policy.diffusePaths.find(std::string_view(a_texture.diffuse)); found != a_policy.diffusePaths.end())
			return found->second;
		if (!a_texture.materialChainValid || a_texture.materialCount > MaximumMaterialDepth)
			return MaterialKind::kUnknown;
		for (std::size_t index = 0u; index < a_texture.materialCount; ++index) {
			const auto& material = a_texture.materials[index];
			for (const auto identity : { std::string_view(material.form), std::string_view(material.editorID), std::string_view(material.name) }) {
				if (const auto found = a_policy.surfaceMaterials.find(identity); found != a_policy.surfaceMaterials.end())
					return found->second;
			}
		}
		for (std::size_t index = 0u; index < a_texture.materialCount; ++index) {
			const auto& material = a_texture.materials[index];
			for (const auto name : { std::string_view(material.editorID), std::string_view(material.name) }) {
				if (const auto kind = ClassifyName(name); kind != MaterialKind::kUnknown)
					return kind;
			}
		}
		return MaterialKind::kUnknown;
	}

	/** The shader has six classes, so disallowed known pairs reject the whole quadrant. */
	template <class String>
	[[nodiscard]] std::uint32_t ClassifyQuadrant(const std::array<TextureIdentity<String>, LayerCount>& a_textures, const CompiledPolicy& a_policy)
	{
		std::array<MaterialKind, LayerCount> kinds{};
		std::uint32_t classes = 0u;
		for (std::size_t layer = 0u; layer < LayerCount; ++layer) {
			kinds[layer] = ClassifyTexture(a_textures[layer], a_policy);
			classes |= static_cast<std::uint32_t>(GetMaterialClass(kinds[layer])) << (ClassShift + ClassBits * layer);
		}
		for (std::size_t first = 0u; first < LayerCount; ++first) {
			const auto firstClass = GetMaterialClass(kinds[first]);
			if (firstClass == MaterialClass::kUnknown)
				continue;
			for (std::size_t second = first + 1u; second < LayerCount; ++second) {
				const auto secondClass = GetMaterialClass(kinds[second]);
				if (secondClass == MaterialClass::kUnknown)
					continue;
				auto source = static_cast<std::size_t>(kinds[first]);
				auto receiver = static_cast<std::size_t>(kinds[second]);
				if (firstClass == MaterialClass::kHard && secondClass == MaterialClass::kSoft)
					std::swap(source, receiver);
				if (!a_policy.blendPairs[source][receiver] &&
					(firstClass != secondClass || !a_policy.blendPairs[receiver][source]))
					return 0u;
			}
		}
		return classes & ClassMask;
	}
}
