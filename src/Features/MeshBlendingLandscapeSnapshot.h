#pragma once

#include "MeshBlendingLandscapePolicy.h"

#include <bit>
#include <cstring>

namespace CSX::MeshBlendingLandscapePolicy
{
	inline constexpr std::uint32_t SnapshotMagic = 0x4D424C53u;
	inline constexpr std::uint32_t PublicationMagic = 0x4D424C50u;
	inline constexpr std::uint32_t SnapshotVersion = 1u;
	inline constexpr std::size_t SnapshotHeaderWords = 32u;
	inline constexpr std::size_t PublicationWords = 8u;
	inline constexpr std::size_t MaximumSnapshotBytes = 64u * 1024u;
	inline constexpr std::size_t MaximumQuadrantChildren = 32u;

	enum class MaterialLayout : std::uint32_t
	{
		kVanilla = 1u,
		kPBR = 2u
	};

	struct DrawIdentity
	{
		std::uint64_t root = 0u;
		std::uint64_t geometry = 0u;
		std::uint64_t shaderProperty = 0u;
		std::uint64_t material = 0u;
		MaterialLayout layout = MaterialLayout::kVanilla;
		std::uint32_t textureCount = 0u;
		std::uint32_t childSlots = 0u;
		std::uint32_t childIndex = 0u;
		std::array<std::uint64_t, LayerCount> textures{};
		bool operator==(const DrawIdentity&) const = default;
	};

	struct SnapshotHeader
	{
		std::uint64_t session = 0u;
		std::uint64_t publication = 0u;
		DrawIdentity draw;
	};

	inline void WriteWord(std::span<std::int32_t> a_words, std::size_t a_index, std::uint32_t a_value) noexcept
	{
		a_words[a_index] = std::bit_cast<std::int32_t>(a_value);
	}

	inline void WriteWide(std::span<std::int32_t> a_words, std::size_t a_index, std::uint64_t a_value) noexcept
	{
		WriteWord(a_words, a_index, static_cast<std::uint32_t>(a_value));
		WriteWord(a_words, a_index + 1u, static_cast<std::uint32_t>(a_value >> 32u));
	}

	[[nodiscard]] inline std::uint32_t ReadWord(std::span<const std::int32_t> a_words, std::size_t a_index) noexcept
	{
		return std::bit_cast<std::uint32_t>(a_words[a_index]);
	}

	[[nodiscard]] inline std::uint64_t ReadWide(std::span<const std::int32_t> a_words, std::size_t a_index) noexcept
	{
		return ReadWord(a_words, a_index) | (static_cast<std::uint64_t>(ReadWord(a_words, a_index + 1u)) << 32u);
	}

	/** A root token revokes a geometry snapshot even when a replacement layout is rejected. */
	[[nodiscard]] inline std::vector<std::int32_t> EncodePublication(const SnapshotHeader& a_header)
	{
		std::vector<std::int32_t> words(PublicationWords);
		WriteWord(words, 0u, PublicationMagic);
		WriteWord(words, 1u, SnapshotVersion);
		WriteWide(words, 2u, a_header.session);
		WriteWide(words, 4u, a_header.publication);
		WriteWide(words, 6u, a_header.draw.root);
		return words;
	}

	/** Fixed-size validation does not decode or allocate copied texture identities. */
	[[nodiscard]] inline bool ReadSnapshotHeader(std::span<const std::int32_t> a_words, SnapshotHeader& a_header) noexcept
	{
		if (a_words.size() < SnapshotHeaderWords || a_words.size() > MaximumSnapshotBytes / sizeof(std::int32_t) ||
			ReadWord(a_words, 0u) != SnapshotMagic || ReadWord(a_words, 1u) != SnapshotVersion ||
			ReadWord(a_words, 2u) != a_words.size() || ReadWord(a_words, 3u) != 0u)
			return false;
		a_header.session = ReadWide(a_words, 4u);
		a_header.publication = ReadWide(a_words, 6u);
		auto& draw = a_header.draw;
		draw.root = ReadWide(a_words, 8u);
		draw.geometry = ReadWide(a_words, 10u);
		draw.shaderProperty = ReadWide(a_words, 12u);
		draw.material = ReadWide(a_words, 14u);
		draw.layout = static_cast<MaterialLayout>(ReadWord(a_words, 16u));
		draw.textureCount = ReadWord(a_words, 17u);
		draw.childSlots = ReadWord(a_words, 18u);
		draw.childIndex = ReadWord(a_words, 19u);
		for (std::size_t layer = 0u; layer < LayerCount; ++layer)
			draw.textures[layer] = ReadWide(a_words, 20u + 2u * layer);
		return a_header.session != 0u && a_header.publication != 0u &&
		       draw.root != 0u && draw.geometry != 0u && draw.shaderProperty != 0u && draw.material != 0u &&
		       (draw.layout == MaterialLayout::kVanilla || draw.layout == MaterialLayout::kPBR) &&
		       draw.textureCount > 0u && draw.textureCount <= LayerCount &&
		       draw.childSlots > 0u && draw.childSlots <= MaximumQuadrantChildren && draw.childIndex < draw.childSlots;
	}

	/** Both current owners must agree; cloned or serialized snapshots cannot publish themselves. */
	[[nodiscard]] inline bool MatchesPublication(
		std::span<const std::int32_t> a_publication,
		const SnapshotHeader& a_header,
		const DrawIdentity& a_current,
		std::uint64_t a_session) noexcept
	{
		return a_publication.size() == PublicationWords &&
		       ReadWord(a_publication, 0u) == PublicationMagic && ReadWord(a_publication, 1u) == SnapshotVersion &&
		       a_header.session == a_session && ReadWide(a_publication, 2u) == a_session &&
		       ReadWide(a_publication, 4u) == a_header.publication &&
		       ReadWide(a_publication, 6u) == a_current.root && a_header.draw == a_current;
	}

	/** Copy bounded identities into the engine-owned integer array without retaining engine objects. */
	[[nodiscard]] inline std::optional<std::vector<std::int32_t>> EncodeSnapshot(
		const SnapshotHeader& a_header,
		const std::array<CapturedTexture, LayerCount>& a_textures)
	{
		std::vector<std::int32_t> words(SnapshotHeaderWords);
		WriteWord(words, 0u, SnapshotMagic);
		WriteWord(words, 1u, SnapshotVersion);
		WriteWide(words, 4u, a_header.session);
		WriteWide(words, 6u, a_header.publication);
		const auto& draw = a_header.draw;
		WriteWide(words, 8u, draw.root);
		WriteWide(words, 10u, draw.geometry);
		WriteWide(words, 12u, draw.shaderProperty);
		WriteWide(words, 14u, draw.material);
		WriteWord(words, 16u, static_cast<std::uint32_t>(draw.layout));
		WriteWord(words, 17u, draw.textureCount);
		WriteWord(words, 18u, draw.childSlots);
		WriteWord(words, 19u, draw.childIndex);
		for (std::size_t layer = 0u; layer < LayerCount; ++layer)
			WriteWide(words, 20u + 2u * layer, draw.textures[layer]);
		const auto appendString = [&](std::string_view value) {
			const auto added = 1u + (value.size() + sizeof(std::int32_t) - 1u) / sizeof(std::int32_t);
			if (value.size() > MaximumIdentityLength || words.size() + added > MaximumSnapshotBytes / sizeof(std::int32_t))
				return false;
			const auto start = words.size();
			words.resize(start + added);
			WriteWord(words, start, static_cast<std::uint32_t>(value.size()));
			if (!value.empty())
				std::memcpy(words.data() + start + 1u, value.data(), value.size());
			return true;
		};
		for (const auto& texture : a_textures) {
			if (texture.materialCount > MaximumMaterialDepth || words.size() + 3u > MaximumSnapshotBytes / sizeof(std::int32_t))
				return std::nullopt;
			words.push_back(texture.present ? 1 : 0);
			words.push_back(texture.materialChainValid ? 1 : 0);
			words.push_back(static_cast<std::int32_t>(texture.materialCount));
			if (!appendString(texture.form) || !appendString(texture.editorID) || !appendString(texture.diffuse))
				return std::nullopt;
			for (std::size_t index = 0u; index < texture.materialCount; ++index) {
				const auto& material = texture.materials[index];
				if (!appendString(material.form) || !appendString(material.editorID) || !appendString(material.name))
					return std::nullopt;
			}
		}
		WriteWord(words, 2u, static_cast<std::uint32_t>(words.size()));
		SnapshotHeader checked;
		return ReadSnapshotHeader(words, checked) ? std::optional(std::move(words)) : std::nullopt;
	}

	/** Views borrow the locked metadata array and are consumed before that lock is released. */
	[[nodiscard]] inline bool DecodeTextureViews(
		std::span<const std::int32_t> a_words,
		std::array<TextureView, LayerCount>& a_textures) noexcept
	{
		SnapshotHeader header;
		if (!ReadSnapshotHeader(a_words, header))
			return false;
		std::size_t cursor = SnapshotHeaderWords;
		const auto readString = [&](std::string_view& value) {
			if (cursor >= a_words.size())
				return false;
			const auto bytes = ReadWord(a_words, cursor++);
			const auto count = (bytes + sizeof(std::int32_t) - 1u) / sizeof(std::int32_t);
			if (bytes > MaximumIdentityLength || count > a_words.size() - cursor)
				return false;
			value = { reinterpret_cast<const char*>(a_words.data() + cursor), bytes };
			cursor += count;
			return true;
		};
		for (auto& texture : a_textures) {
			if (a_words.size() - cursor < 3u || ReadWord(a_words, cursor) > 1u || ReadWord(a_words, cursor + 1u) > 1u)
				return false;
			texture.present = ReadWord(a_words, cursor++) != 0u;
			texture.materialChainValid = ReadWord(a_words, cursor++) != 0u;
			texture.materialCount = ReadWord(a_words, cursor++);
			if (texture.materialCount > MaximumMaterialDepth || !readString(texture.form) || !readString(texture.editorID) || !readString(texture.diffuse))
				return false;
			for (std::size_t index = 0u; index < texture.materialCount; ++index) {
				auto& material = texture.materials[index];
				if (!readString(material.form) || !readString(material.editorID) || !readString(material.name))
					return false;
			}
		}
		return cursor == a_words.size();
	}

	/** Thread-owned acceleration only: an eviction never removes captured identity data. */
	template <std::size_t SetCount = 128u, std::size_t Ways = 4u>
	class ClassificationCache
	{
		static_assert(std::has_single_bit(SetCount) && Ways != 0u);
		struct Entry
		{
			std::uint64_t session = 0u;
			std::uint64_t publication = 0u;
			std::uint64_t revision = 0u;
			std::uint64_t used = 0u;
			std::uint32_t classes = 0u;
		};

	public:
		/** Recompute bounded copied identities on any policy change or cache eviction. */
		bool Resolve(std::span<const std::int32_t> a_words, const SnapshotHeader& a_header,
			const CompiledPolicy& a_policy, std::uint32_t& a_classes)
		{
			auto hash = a_header.publication ^ a_header.session;
			hash = (hash ^ (hash >> 30u)) * 0xBF58476D1CE4E5B9ull;
			hash = (hash ^ (hash >> 27u)) * 0x94D049BB133111EBull;
			hash ^= hash >> 31u;
			const auto start = (hash & (SetCount - 1u)) * Ways;
			auto* replacement = &entries[start];
			for (std::size_t way = 0u; way < Ways; ++way) {
				auto& entry = entries[start + way];
				if (entry.session == a_header.session && entry.publication == a_header.publication && entry.revision == a_policy.revision) {
					entry.used = ++clock;
					a_classes = entry.classes;
					return true;
				}
				if (entry.used < replacement->used)
					replacement = &entry;
			}
			std::array<TextureView, LayerCount> textures{};
			if (!DecodeTextureViews(a_words, textures))
				return false;
			a_classes = ClassifyQuadrant(textures, a_policy);
			*replacement = { a_header.session, a_header.publication, a_policy.revision, ++clock, a_classes };
			return true;
		}

	private:
		std::array<Entry, SetCount * Ways> entries{};
		std::uint64_t clock = 0u;
	};
}
