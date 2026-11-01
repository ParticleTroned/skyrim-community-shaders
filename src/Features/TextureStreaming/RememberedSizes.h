#pragma once

#include "Settings.h"
#include "TextureData.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace StreamingTextures
{
	inline std::string NormalizeTexturePath(std::string path)
	{
		std::ranges::transform(path, path.begin(), [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
		if (path.starts_with("data\\"))
			path.erase(0, 5);
		if (!path.starts_with("textures\\"))
			path = "textures\\" + path;
		return path;
	}

	/** Persist only a previously validated source contract, never pointers or GPU resources. */
	struct RememberedSize
	{
		D3D11_TEXTURE2D_DESC full{};
		std::uint32_t drop = 0, categories = 0;
		std::uint64_t headerHash = 0, fileBytes = 0;

		bool operator==(const RememberedSize& other) const
		{
			return full.Width == other.full.Width && full.Height == other.full.Height && full.MipLevels == other.full.MipLevels &&
			       full.Format == other.full.Format && drop == other.drop && categories == other.categories &&
			       headerHash == other.headerHash && fileBytes == other.fileBytes;
		}
		[[nodiscard]] bool Valid(std::string_view path) const
		{
			const auto required = PathCategories(path, categories);
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = full.Format;
			view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipLevels = full.MipLevels;
			return required && !(categories & ~(LandscapeStatics | AlphaTestedStatics | EmissiveStatics | Terrain | Parallax)) &&
			       ((*required & categories) == *required) && headerHash && fileBytes >= 128 && fileBytes <= TextureStreamingPolicy::MaximumPayloadBytes &&
			       Suitable(full, view) && LogicalBytes(full, 0) && drop <= TextureStreamingPolicy::MaximumDrop &&
			       drop < full.MipLevels && (std::min(full.Width, full.Height) >> drop) >= TextureStreamingPolicy::MinimumResidentEdge;
		}

		[[nodiscard]] std::uint32_t LoadDrop(const Options& options, const Categories& allowed, std::uint32_t maximumDrop, bool reductionRequested) const
		{
			if (!allowed.Allows(categories))
				return 0;
			const auto remembered = options.rememberedSizes && reductionRequested ? std::min(drop, maximumDrop) : 0u;
			return std::max(remembered, TextureStreamingPolicy::CappedDrop(full.Width, full.Height, full.MipLevels, options.maximumSize));
		}
	};
	inline std::uint64_t HeaderHash(std::span<const std::uint8_t> header)
	{
		std::uint64_t hash = 14695981039346656037ull;
		for (const auto value : header) {
			hash ^= value;
			hash *= 1099511628211ull;
		}
		return hash;
	}
	inline nlohmann::json SerializeRememberedSize(const RememberedSize& value)
	{
		return { value.full.Width, value.full.Height, value.full.MipLevels, static_cast<std::uint32_t>(value.full.Format),
			value.drop, value.categories, value.headerHash, value.fileBytes };
	}
	inline std::optional<RememberedSize> ParseRememberedSize(std::string_view path, const nlohmann::json& value)
	{
		if (!value.is_array() || value.size() != 8)
			return std::nullopt;
		for (std::size_t i = 0; i < value.size(); ++i)
			if (!value[i].is_number_integer() || (!value[i].is_number_unsigned() && value[i].get<std::int64_t>() < 0) ||
				(i < 6 && value[i].get<std::uint64_t>() > UINT32_MAX))
				return std::nullopt;
		RememberedSize result;
		result.full = { value[0].get<UINT>(), value[1].get<UINT>(), value[2].get<UINT>(), 1, static_cast<DXGI_FORMAT>(value[3].get<UINT>()),
			{ 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		result.drop = value[4].get<std::uint32_t>();
		result.categories = value[5].get<std::uint32_t>();
		result.headerHash = value[6].get<std::uint64_t>();
		result.fileBytes = value[7].get<std::uint64_t>();
		return result.Valid(path) ? std::optional(result) : std::nullopt;
	}
}
