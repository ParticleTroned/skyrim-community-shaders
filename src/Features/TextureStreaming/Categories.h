#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace StreamingTextures
{
	enum Category : std::uint32_t
	{
		LandscapeStatics = 1u << 0,
		AlphaTestedStatics = 1u << 1,
		EmissiveStatics = 1u << 2,
		Terrain = 1u << 3,
		Parallax = 1u << 4
	};

	/** Optional coverage never overrides DDS, geometry or special-consumer protection. */
	struct Categories
	{
		bool landscapeStatics = true;
		bool alphaTestedStatics = true;
		bool emissiveStatics = true;
		bool terrain = true;
		bool parallax = true;
		bool operator==(const Categories&) const = default;

		[[nodiscard]] bool Allows(std::uint32_t required) const noexcept
		{
			const std::uint32_t enabled = (landscapeStatics ? LandscapeStatics : 0u) |
			                              (alphaTestedStatics ? AlphaTestedStatics : 0u) | (emissiveStatics ? EmissiveStatics : 0u) | (terrain ? Terrain : 0u) | (parallax ? Parallax : 0u);
			return (required & ~enabled) == 0;
		}
	};

	/** Classify a normalized resource path; LOD atlases and special-purpose textures remain protected. */
	inline std::optional<std::uint32_t> PathCategories(std::string_view path, std::uint32_t materialCategories = 0)
	{
		if (path.size() > 260 || !path.starts_with("textures\\") || !path.ends_with(".dds") ||
			path.find("..") != std::string_view::npos || path.find(':') != std::string_view::npos)
			return std::nullopt;
		for (const auto* excluded : { "actors\\", "terrain\\", "effects\\", "interface\\", "cubemaps\\",
				 "water\\", "armor\\", "clothes\\", "clothing\\", "fonts\\", "book", "\\maps\\", "mapmarker", "\\lod\\", "\\dyndolod\\", "\\sky\\", "_lod", ".objects", ".treelod" })
			if (path.find(excluded) != std::string_view::npos)
				return std::nullopt;
		return !(materialCategories & Terrain) && path.find("landscape\\") != std::string_view::npos ? LandscapeStatics : 0u;
	}
}
