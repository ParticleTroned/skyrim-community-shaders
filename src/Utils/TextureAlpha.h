#pragma once

#include <DirectXTex.h>

namespace Util
{
	/** @brief Verifies zero alpha in every mip of a bounded 2D image; unknown formats fail closed. */
	inline bool HasOnlyZeroAlpha(const DirectX::ScratchImage& a_image)
	{
		const auto& metadata = a_image.GetMetadata();
		if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 ||
			metadata.IsCubemap() || !metadata.width || !metadata.height || !metadata.mipLevels ||
			metadata.width > 1024 || metadata.height > 1024 || a_image.GetImageCount() != metadata.mipLevels)
			return false;
		DirectX::ScratchImage decompressed;
		const auto* image = &a_image;
		if (DirectX::IsCompressed(metadata.format)) {
			if (FAILED(DirectX::Decompress(a_image.GetImages(), a_image.GetImageCount(), metadata,
					DXGI_FORMAT_R32G32B32A32_FLOAT, decompressed)))
				return false;
			image = &decompressed;
		}
		bool transparent = true;
		const auto result = DirectX::EvaluateImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
			[&](const DirectX::XMVECTOR* pixels, size_t width, size_t) {
				for (size_t i = 0; i < width && transparent; ++i)
					transparent = DirectX::XMVectorGetW(pixels[i]) == 0.0f;
			});
		return SUCCEEDED(result) && transparent;
	}
}
