#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <d3d11.h>
#include <optional>
#include <span>
#include <vector>

namespace Util
{
	/** @brief CPU R8 height map with bilinear sampling and conservative maximum queries. */
	class HeightMap
	{
		struct Level
		{
			uint32_t width;
			uint32_t height;
			std::vector<uint8_t> texels;
		};
		std::vector<Level> levels;
		std::optional<float> constant;

	public:
		/** @brief Reports whether a CPU altitude map is available. */
		bool IsLoaded() const { return constant.has_value() || !levels.empty(); }

		/** @brief Matches a single-pixel float fallback exactly, without R8 quantization. */
		void SetConstant(float value)
		{
			levels.clear();
			constant = std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
		}

		/** @brief Excludes edges where the material sampler's address mode affects the result. */
		bool ContainsSample(float u, float v) const
		{
			if (!std::isfinite(u) || !std::isfinite(v))
				return false;
			if (constant)
				return true;
			return !levels.empty() && u >= 0.5f / levels.front().width && u <= 1.0f - 0.5f / levels.front().width &&
			       v >= 0.5f / levels.front().height && v <= 1.0f - 0.5f / levels.front().height;
		}

		/** @brief Copies a validated R8 map and builds its maximum hierarchy once. */
		bool Load(uint32_t width, uint32_t height, std::span<const uint8_t> pixels, size_t rowPitch)
		{
			levels.clear();
			constant.reset();
			if (!width || !height || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || rowPitch < width || height > pixels.size() / rowPitch)
				return false;
			Level base{ width, height, std::vector<uint8_t>(static_cast<size_t>(width) * height) };
			for (uint32_t y = 0; y < height; ++y)
				std::copy_n(pixels.data() + y * rowPitch, width, base.texels.data() + static_cast<size_t>(y) * width);
			levels.push_back(std::move(base));
			while (levels.back().width > 1 || levels.back().height > 1) {
				const auto& source = levels.back();
				Level next{ (source.width + 1) / 2, (source.height + 1) / 2, {} };
				next.texels.resize(static_cast<size_t>(next.width) * next.height);
				for (uint32_t y = 0; y < source.height; ++y)
					for (uint32_t x = 0; x < source.width; ++x) {
						auto& value = next.texels[static_cast<size_t>(y / 2) * next.width + x / 2];
						value = std::max(value, source.texels[static_cast<size_t>(y) * source.width + x]);
					}
				levels.push_back(std::move(next));
			}
			return true;
		}

		/** @brief Returns an upper bound including the bilinear footprint; unknown regions cannot be excluded. */
		std::optional<float> Maximum(float u0, float v0, float u1, float v1) const
		{
			if (!std::isfinite(u0) || !std::isfinite(v0) || !std::isfinite(u1) || !std::isfinite(v1))
				return std::nullopt;
			if (constant || levels.empty())
				return constant;
			const auto& base = levels.front();
			const float x0f = std::floor(std::min(u0, u1) * base.width - 0.5f);
			const float x1f = std::floor(std::max(u0, u1) * base.width - 0.5f) + 1.0f;
			const float y0f = std::floor(std::min(v0, v1) * base.height - 0.5f);
			const float y1f = std::floor(std::max(v0, v1) * base.height - 0.5f) + 1.0f;
			if (!(x0f >= 0 && y0f >= 0 && x1f < base.width && y1f < base.height))
				return std::nullopt;
			const auto x0 = static_cast<uint32_t>(x0f), x1 = static_cast<uint32_t>(x1f);
			const auto y0 = static_cast<uint32_t>(y0f), y1 = static_cast<uint32_t>(y1f);
			size_t level = 0;
			while (level + 1 < levels.size() && ((x1 >> level) - (x0 >> level) > 1 || (y1 >> level) - (y0 >> level) > 1))
				++level;
			const auto& map = levels[level];
			uint8_t value = 0;
			for (uint32_t y = y0 >> level; y <= (y1 >> level); ++y)
				for (uint32_t x = x0 >> level; x <= (x1 >> level); ++x)
					value = std::max(value, map.texels[static_cast<size_t>(y) * map.width + x]);
			return value / 255.0f;
		}

		/** @brief Samples mip zero with bilinear filtering inside the map and clamped edges. */
		float Sample(float u, float v) const
		{
			if (!std::isfinite(u) || !std::isfinite(v))
				return 0.5f;
			if (constant || levels.empty())
				return constant.value_or(0.5f);
			const auto& map = levels.front();
			const float x = std::clamp(u * map.width - 0.5f, 0.0f, static_cast<float>(map.width - 1));
			const float y = std::clamp(v * map.height - 0.5f, 0.0f, static_cast<float>(map.height - 1));
			const auto x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
			const auto x1 = std::min(x0 + 1, map.width - 1), y1 = std::min(y0 + 1, map.height - 1);
			const auto top = std::lerp(static_cast<float>(map.texels[static_cast<size_t>(y0) * map.width + x0]), static_cast<float>(map.texels[static_cast<size_t>(y0) * map.width + x1]), x - x0);
			const auto bottom = std::lerp(static_cast<float>(map.texels[static_cast<size_t>(y1) * map.width + x0]), static_cast<float>(map.texels[static_cast<size_t>(y1) * map.width + x1]), x - x0);
			return std::lerp(top, bottom, y - y0) / 255.0f;
		}
	};
}
