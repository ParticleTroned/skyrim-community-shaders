#include "../Wetterness.h"
#include "Utils/D3D.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <vector>

namespace
{
	constexpr uint32_t kPuddleMaskTextureSize = 256u;

	struct PuddleMaskTexel
	{
		std::uint8_t macro;
		std::uint8_t detail;
	};
	static_assert(sizeof(PuddleMaskTexel) == 2);

	uint32_t HashPuddleMaskCell(uint32_t x, uint32_t y, uint32_t seed)
	{
		uint32_t value = x * 0x8DA6B343u ^ y * 0xD8163841u ^ seed;
		value ^= value >> 16u;
		value *= 0x7FEB352Du;
		value ^= value >> 15u;
		value *= 0x846CA68Bu;
		value ^= value >> 16u;
		return value;
	}

	float PuddleMaskCellValue(uint32_t x, uint32_t y, uint32_t seed)
	{
		return static_cast<float>(HashPuddleMaskCell(x, y, seed) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
	}

	float PeriodicValueNoise(float x, float y, uint32_t period, uint32_t seed)
	{
		const float floorX = std::floor(x);
		const float floorY = std::floor(y);
		const auto wrapCell = [period](std::int32_t cell) {
			const std::int32_t signedPeriod = static_cast<std::int32_t>(period);
			const std::int32_t wrapped = cell % signedPeriod;
			return static_cast<uint32_t>(wrapped < 0 ? wrapped + signedPeriod : wrapped);
		};
		const uint32_t x0 = wrapCell(static_cast<std::int32_t>(floorX));
		const uint32_t y0 = wrapCell(static_cast<std::int32_t>(floorY));
		const uint32_t x1 = (x0 + 1u) % period;
		const uint32_t y1 = (y0 + 1u) % period;
		float blendX = x - floorX;
		float blendY = y - floorY;
		blendX = blendX * blendX * (3.0f - 2.0f * blendX);
		blendY = blendY * blendY * (3.0f - 2.0f * blendY);

		const float row0 = std::lerp(PuddleMaskCellValue(x0, y0, seed), PuddleMaskCellValue(x1, y0, seed), blendX);
		const float row1 = std::lerp(PuddleMaskCellValue(x0, y1, seed), PuddleMaskCellValue(x1, y1, seed), blendX);
		return std::lerp(row0, row1, blendY);
	}

	std::vector<PuddleMaskTexel> BuildPuddleMaskTextureData()
	{
		std::vector<PuddleMaskTexel> pixels(kPuddleMaskTextureSize * kPuddleMaskTextureSize);
		for (uint32_t y = 0; y < kPuddleMaskTextureSize; ++y) {
			for (uint32_t x = 0; x < kPuddleMaskTextureSize; ++x) {
				const float u = static_cast<float>(x) / static_cast<float>(kPuddleMaskTextureSize);
				const float v = static_cast<float>(y) / static_cast<float>(kPuddleMaskTextureSize);
				const float warpX = PeriodicValueNoise(u * 3.0f, v * 3.0f, 3u, 0xA511E9B3u) - 0.5f;
				const float warpY = PeriodicValueNoise(u * 3.0f, v * 3.0f, 3u, 0x63D83595u) - 0.5f;
				const float warpedU = u + warpX * 0.12f;
				const float warpedV = v + warpY * 0.12f;
				const float macro =
					PeriodicValueNoise(warpedU * 4.0f, warpedV * 4.0f, 4u, 0x9E3779B9u) * 0.70f +
					PeriodicValueNoise(warpedU * 8.0f, warpedV * 8.0f, 8u, 0xBB67AE85u) * 0.30f;
				const float detail =
					PeriodicValueNoise(u * 9.0f, v * 9.0f, 9u, 0x3C6EF372u) * 0.65f +
					PeriodicValueNoise(u * 18.0f, v * 18.0f, 18u, 0xA54FF53Au) * 0.35f;
				auto& pixel = pixels[y * kPuddleMaskTextureSize + x];
				pixel.macro = static_cast<std::uint8_t>(std::clamp(macro, 0.0f, 1.0f) * 255.0f + 0.5f);
				pixel.detail = static_cast<std::uint8_t>(std::clamp(detail, 0.0f, 1.0f) * 255.0f + 0.5f);
			}
		}
		return pixels;
	}
}

void Wetterness::SetupResources()
{
	puddleMaskSrv = nullptr;
	auto device = globals::d3d::device;
	if (!device) {
		logger::warn("Wetterness puddle mask has no D3D device; using Simple puddles.");
		return;
	}

	try {
		const auto pixels = BuildPuddleMaskTextureData();
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = kPuddleMaskTextureSize;
		textureDesc.Height = kPuddleMaskTextureSize;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initialData{};
		initialData.pSysMem = pixels.data();
		initialData.SysMemPitch = kPuddleMaskTextureSize * sizeof(PuddleMaskTexel);

		winrt::com_ptr<ID3D11Texture2D> texture;
		DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, &initialData, texture.put()));
		Util::SetResourceName(texture.get(), "Wetterness::PuddleMask");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		DX::ThrowIfFailed(device->CreateShaderResourceView(texture.get(), &srvDesc, puddleMaskSrv.put()));
		Util::SetResourceName(puddleMaskSrv.get(), "Wetterness::PuddleMask SRV");
	} catch (const winrt::hresult_error& error) {
		puddleMaskSrv = nullptr;
		logger::warn("Wetterness puddle mask creation failed (HRESULT {}); using Simple puddles.", error.code().value);
	} catch (const std::exception& error) {
		puddleMaskSrv = nullptr;
		logger::warn("Wetterness puddle mask creation failed ({}); using Simple puddles.", error.what());
	}
}
