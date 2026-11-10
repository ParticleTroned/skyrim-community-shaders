#include "Utils/TextureAlpha.h"

#include <cstring>
#include <iostream>
#include <stdexcept>

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
{
	try {
		DirectX::ScratchImage image;
		Require(!Util::HasOnlyZeroAlpha(image), "Empty images must not be culled");
		Require(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 3)), "Image allocation failed");
		std::memset(image.GetPixels(), 0, image.GetPixelsSize());
		Require(Util::HasOnlyZeroAlpha(image), "Transparent mip chain rejected");
		image.GetImage(1, 0, 0)->pixels[3] = 1;
		Require(!Util::HasOnlyZeroAlpha(image), "Nonzero alpha in a lower mip must preserve rendering");
		image.GetImage(1, 0, 0)->pixels[3] = 0;
		DirectX::ScratchImage compressed;
		Require(SUCCEEDED(DirectX::Compress(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
					DXGI_FORMAT_BC3_UNORM, DirectX::TEX_COMPRESS_DEFAULT, 0.5f, compressed)),
			"Compression failed");
		Require(Util::HasOnlyZeroAlpha(compressed), "Transparent compressed texture rejected");
		Require(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8_UNORM, 1, 1, 1, 1)), "Opaque allocation failed");
		std::memset(image.GetPixels(), 0, image.GetPixelsSize());
		Require(!Util::HasOnlyZeroAlpha(image), "Textures without alpha must preserve rendering");
		Require(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1, 1, 1)), "Float allocation failed");
		std::memset(image.GetPixels(), 0, image.GetPixelsSize());
		reinterpret_cast<float*>(image.GetPixels())[3] = 1e-30f;
		Require(!Util::HasOnlyZeroAlpha(image), "Small nonzero alpha must not be quantized away");
		Require(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 2048, 1, 1, 1)), "Bound test allocation failed");
		std::memset(image.GetPixels(), 0, image.GetPixelsSize());
		Require(!Util::HasOnlyZeroAlpha(image), "Oversized textures must preserve rendering");
		std::cout << "Texture alpha verification passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
