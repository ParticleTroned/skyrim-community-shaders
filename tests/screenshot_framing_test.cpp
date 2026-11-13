#define NOMINMAX
#include "Features/ScreenshotApiPolicy.h"
#include <DirectXTex.h>
#include <openvr.h>

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace logger
{
	template <class... T>
	void trace(T&&...)
	{}
}

namespace
{
#include "screenshot_framing_under_test.h"

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
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
	Require(SUCCEEDED(com.result), "COM initialization failed");
	DirectX::ScratchImage source;
	Require(SUCCEEDED(source.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 4000, 2400, 1, 1)), "source allocation failed");
	const auto* eye = source.GetImage(0, 0, 0);
	for (size_t y = 0; y < eye->height; ++y) {
		for (size_t x = 0; x < eye->width; ++x) {
			auto* pixel = eye->pixels + y * eye->rowPitch + x * 4;
			pixel[0] = static_cast<uint8_t>(x);
			pixel[1] = static_cast<uint8_t>(y);
			pixel[2] = 127;
			pixel[3] = 255;
		}
	}
	for (const bool native : { false, true }) {
		const auto size = CSX::ScreenshotPolicy::ResolveFramedSize(eye->width, eye->height, native);
		Require(size.has_value(), "valid source dimensions rejected");
		DirectX::ScratchImage crop, framed;
		Require(CenterCropAndResize(*eye, size->width, size->height, vr::ColorSpace_Gamma, crop, framed), "eye framing failed");
		Require(crop.GetPixelsSize() == 0, "framing retained an unnecessary full-size crop");
		const auto* output = framed.GetImage(0, 0, 0);
		Require(output && output->width == (native ? 4000u : 3840u) && output->height == (native ? 2250u : 2160u), "framed dimensions incorrect");
		if (native) {
			for (size_t y = 0; y < output->height; ++y)
				Require(std::memcmp(output->pixels + y * output->rowPitch, eye->pixels + (y + 75) * eye->rowPitch, output->width * 4) == 0, "native crop resampled source pixels");
		}
	}
	DirectX::ScratchImage crop, invalid;
	Require(!CenterCropAndResize(*eye, 16385, 2160, vr::ColorSpace_Gamma, crop, invalid), "oversized output accepted");
	source.Release();
	DirectX::ScratchImage floatEye;
	Require(SUCCEEDED(floatEye.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, 64, 64, 1, 1)), "float source allocation failed");
	std::fill_n(reinterpret_cast<DirectX::XMFLOAT4*>(floatEye.GetPixels()), 64 * 64, DirectX::XMFLOAT4{ 0.5f, 0.5f, 0.5f, 0.75f });
	for (const auto colourSpace : { vr::ColorSpace_Linear, vr::ColorSpace_Auto, vr::ColorSpace_Gamma }) {
		DirectX::ScratchImage linear;
		Require(ConvertCaptureToLinearFloat(*floatEye.GetImage(0, 0, 0), colourSpace, linear), "float eye conversion failed");
		const auto* pixel = reinterpret_cast<const DirectX::XMFLOAT4*>(linear.GetPixels());
		const auto expected = colourSpace == vr::ColorSpace_Gamma ? 0.21404114f : 0.5f;
		Require(std::abs(pixel->x - expected) < 1e-6f && pixel->w == 0.75f, "float eye colour or alpha changed");
	}

	DirectX::ScratchImage stereoEye;
	Require(SUCCEEDED(stereoEye.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 1)), "stereo source allocation failed");
	auto* pixels = reinterpret_cast<uint32_t*>(stereoEye.GetPixels());
	std::fill_n(pixels, 64 * 64, 0xffc08040u);
	const std::array images{ stereoEye.GetImage(0, 0, 0), stereoEye.GetImage(0, 0, 0) };
	const std::array<std::array<float, 4>, 2> projections{ { { -1, 1, -1, 1 }, { -1, 1, -1, 1 } } };
	const vr::HmdMatrix34_t identity{ { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
	const std::array transforms{ identity, identity };
	DirectX::ScratchImage floatCombined;
	Require(ComposeFramedStereo({ floatEye.GetImage(0, 0, 0), floatEye.GetImage(0, 0, 0) }, projections, transforms, {}, vr::Eye_Left, vr::ColorSpace_Linear, { 3840, 2160 }, floatCombined), "float combined framing failed");
	floatCombined.Release();
	for (const auto size : { CSX::ScreenshotPolicy::FramedSize{ 2560, 1440 }, CSX::ScreenshotPolicy::FramedSize{ 3840, 2160 }, CSX::ScreenshotPolicy::FramedSize{ 4000, 2250 } }) {
		for (const auto dominant : { vr::Eye_Left, vr::Eye_Right }) {
			DirectX::ScratchImage framed;
			Require(ComposeFramedStereo(images, projections, transforms, {}, dominant, vr::ColorSpace_Linear, size, framed), "combined framing failed");
			const auto* output = framed.GetImage(0, 0, 0);
			Require(output && output->width == size.width && output->height == size.height, "combined dimensions incorrect");
			const auto* center = reinterpret_cast<const DirectX::XMFLOAT4*>(output->pixels + (output->height / 2) * output->rowPitch) + output->width / 2;
			Require(std::abs(center->x - 64.0f / 255.0f) < 1e-6f && std::abs(center->y - 128.0f / 255.0f) < 1e-6f, "combined framing changed flat colour");
		}
	}
	std::cout << "4K/native eye and combined framing passed; native crop pixels preserved\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
