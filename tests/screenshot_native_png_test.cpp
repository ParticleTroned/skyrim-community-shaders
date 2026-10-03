#include "Features/ScreenshotNativeImage.h"

#include <cstdint>
#include <iostream>

namespace
{
	void Require(bool condition)
	{
		if (!condition)
			throw std::runtime_error("native screenshot pixel round trip failed");
	}
	struct ComScope
	{
		HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		~ComScope()
		{
			if (SUCCEEDED(result))
				CoUninitialize();
		}
	};
}

int main()
try {
	ComScope com;
	Require(SUCCEEDED(com.result));
	for (auto format : { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			 DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB }) {
		DirectX::ScratchImage native;
		Require(SUCCEEDED(native.Initialize2D(format, 256, 2, 1, 1)));
		const bool bgra = DirectX::MakeLinear(format) == DXGI_FORMAT_B8G8R8A8_UNORM;
		for (size_t row = 0; row < 2; ++row) {
			for (size_t x = 0; x < 256; ++x) {
				auto* pixel = native.GetPixels() + row * native.GetImage(0, 0, 0)->rowPitch + x * 4;
				pixel[bgra ? 2 : 0] = static_cast<uint8_t>(x);
				pixel[1] = static_cast<uint8_t>(255 - x);
				pixel[bgra ? 0 : 2] = static_cast<uint8_t>(row ? x / 2 : 127);
				pixel[3] = 255;
			}
		}
		CSX::Screenshot::PreserveNativeSdrBytes(native);
		DirectX::ScratchImage converted;
		Require(SUCCEEDED(DirectX::Convert(native.GetImages(), native.GetImageCount(), native.GetMetadata(),
			DXGI_FORMAT_B8G8R8X8_UNORM, DirectX::TEX_FILTER_DEFAULT, 0, converted)));
		for (bool png : { false, true }) {
			DirectX::Blob encoded;
			Require(SUCCEEDED(DirectX::SaveToWICMemory(*converted.GetImage(0, 0, 0),
				png ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_NONE,
				DirectX::GetWICCodec(png ? DirectX::WIC_CODEC_PNG : DirectX::WIC_CODEC_BMP), encoded)));
			DirectX::ScratchImage decoded;
			Require(SUCCEEDED(DirectX::LoadFromWICMemory(encoded.GetBufferPointer(), encoded.GetBufferSize(),
				DirectX::WIC_FLAGS_FORCE_RGB, nullptr, decoded)));
			const auto* image = decoded.GetImage(0, 0, 0);
			Require(image && image->width == 256 && image->height == 2);
			Require(DirectX::MakeLinear(image->format) == DXGI_FORMAT_R8G8B8A8_UNORM);
			for (size_t row = 0; row < 2; ++row) {
				for (size_t x = 0; x < 256; ++x) {
					const auto* pixel = image->pixels + row * image->rowPitch + x * 4;
					Require(pixel[0] == x && pixel[1] == 255 - x && pixel[2] == (row ? x / 2 : 127) && pixel[3] == 255);
				}
			}
		}
	}
	DirectX::ScratchImage hdr;
	Require(SUCCEEDED(hdr.Initialize2D(DXGI_FORMAT_R16G16B16A16_FLOAT, 1, 1, 1, 1)));
	bool rejected = false;
	try {
		CSX::Screenshot::PreserveNativeSdrBytes(hdr);
	} catch (const std::invalid_argument&) {
		rejected = true;
	}
	Require(rejected);
	std::cout << "RGBA/BGRA UNORM and sRGB: all 4096 PNG/BMP pixels preserved\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
