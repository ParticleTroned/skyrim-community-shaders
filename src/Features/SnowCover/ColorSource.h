#pragma once

#include "Utils/StringUtils.h"
#include "Utils/TexturePath.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace SnowCoverColorSource
{
	struct Source
	{
		std::filesystem::path path;
		bool pbr = false;
	};

	/** Caches canonical snow landscape records once after game data is loaded. */
	void Discover();
	/** Resolves installed and seasonal snow colors, retaining standard paths for unavailable records. */
	std::array<Source, 2> Resolve();
	/** Detects effective texture-set changes since Resolve without scans or resource loads. */
	bool Changed();

	namespace Detail
	{
		inline constexpr std::array<std::string_view, 2> DefaultPaths{
			"textures/landscape/snow01.dds", "textures/landscape/snow02.dds"
		};

		struct Candidate
		{
			std::string editorID;
			std::string diffusePath;
			bool baseGame = false;
		};

		struct Selection
		{
			std::optional<size_t> index;
			unsigned rank = 0;
			bool ambiguous = false;
		};

		/** Exact canonical identities survive plugin texture remaps; equal candidates fail closed. */
		inline std::array<Selection, 2> SelectRecords(std::span<const Candidate> candidates)
		{
			std::array<Selection, 2> selected{};
			for (size_t i = 0; i < candidates.size(); ++i) {
				const auto& candidate = candidates[i];
				const auto id = Util::ToLowerAscii(candidate.editorID);
				const auto path = Util::NormalizeTexturePath(candidate.diffusePath);
				std::optional<size_t> identity;
				for (size_t slot = 0; slot < DefaultPaths.size(); ++slot) {
					const auto name = slot == 0 ? "snow01" : "snow02";
					if (id == name || id == std::string("landscape") + name)
						identity = slot;
				}
				for (size_t slot = 0; slot < DefaultPaths.size(); ++slot) {
					if (identity ? *identity != slot : path != Util::NormalizeTexturePath(std::string(DefaultPaths[slot])))
						continue;
					const unsigned rank = (identity ? 4u : 2u) + (candidate.baseGame ? 1u : 0u);
					auto& choice = selected[slot];
					if (rank > choice.rank)
						choice = { i, rank, false };
					else if (rank == choice.rank) {
						choice.index.reset();
						choice.ambiguous = true;
					}
				}
			}
			return selected;
		}

		/** Resolve the effective source before classifying its sampling space. */
		template <class TextureSet, class SeasonalSwap, class DiffusePath, class IsPbr>
		Source ResolveTextureSet(TextureSet* textureSet, Source fallback, SeasonalSwap&& seasonalSwap, DiffusePath&& diffusePath, IsPbr&& isPbr)
		{
			auto* effective = seasonalSwap(textureSet);
			const char* path = effective ? diffusePath(effective) : nullptr;
			if (!path || !*path)
				return fallback;
			Util::JoinRelativePath({}, std::filesystem::path(path));
			return { std::filesystem::path(Util::NormalizeTexturePath(path)), isPbr(effective) };
		}
	}
}
