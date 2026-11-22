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
	Require(ComposeFramedStereo({ floatEye.GetImage(0, 0, 0), floatEye.GetImage(0, 0, 0) }, projections, transforms, {}, vr::Eye_Left, vr::ColorSpace_Linear, { 3840, 2160 }, false, floatCombined), "float combined framing failed");
	floatCombined.Release();
	for (const auto size : { CSX::ScreenshotPolicy::FramedSize{ 2560, 1440 }, CSX::ScreenshotPolicy::FramedSize{ 3840, 2160 }, CSX::ScreenshotPolicy::FramedSize{ 4000, 2250 } }) {
		for (const auto dominant : { vr::Eye_Left, vr::Eye_Right }) {
			DirectX::ScratchImage framed;
			Require(ComposeFramedStereo(images, projections, transforms, {}, dominant, vr::ColorSpace_Linear, size, false, framed), "combined framing failed");
			const auto* output = framed.GetImage(0, 0, 0);
			Require(output && output->width == size.width && output->height == size.height, "combined dimensions incorrect");
			const auto* center = reinterpret_cast<const DirectX::XMFLOAT4*>(output->pixels + (output->height / 2) * output->rowPitch) + output->width / 2;
			Require(std::abs(center->x - 64.0f / 255.0f) < 1e-6f && std::abs(center->y - 128.0f / 255.0f) < 1e-6f, "combined framing changed flat colour");
		}
	}
	DirectX::Image densityEye{};
	densityEye.height = 4000;
	densityEye.width = 8000;
	const float diagonal = std::sqrt(0.5f);
	const vr::HmdMatrix34_t rolled{ { { diagonal, -diagonal, 0, 0 }, { diagonal, diagonal, 0, 0 }, { 0, 0, 1, 0 } } };
	const auto rolledDensity = GetCentralHeadTangentPixelDensity(densityEye, projections[0], rolled, 0, 0);
	Require(rolledDensity && std::abs(*rolledDensity - 4000.0) < 0.001, "rolled eye density missed the most detailed direction");
	const float cosine = std::sqrt(0.75f);
	for (const float sine : { -0.5f, 0.5f }) {
		const vr::HmdMatrix34_t canted{ { { cosine, 0, sine, 0 }, { 0, 1, 0, 0 }, { -sine, 0, cosine, 0 } } };
		for (const size_t width : { 4000u, 8000u }) {
			densityEye.width = width;
			const auto density = GetCentralHeadTangentPixelDensity(densityEye, projections[0], canted, -sine / cosine, 0);
			const double expected = std::max(width * 0.5 * cosine * cosine, 2000.0 * cosine);
			Require(density && std::abs(*density - expected) < 0.001, "canted eye density ignored the perspective divide");
		}
	}
	DirectX::Image invalidEye = *images[0];
	invalidEye.format = DXGI_FORMAT_UNKNOWN;
	DirectX::ScratchImage failedComposite;
	Require(!ComposeFramedStereo({ &invalidEye, &invalidEye }, projections, transforms, {}, vr::Eye_Left, vr::ColorSpace_Linear, { 16, 9 }, false, failedComposite), "invalid source format accepted");
	Require(failedComposite.GetPixelsSize() == 0, "failed composition retained its output allocation before fallback");
	for (const bool native : { false, true }) {
		for (const size_t invalidSize : { size_t{ 0 }, size_t{ 16385 }, std::numeric_limits<size_t>::max() }) {
			for (size_t eyeIndex = 0; eyeIndex < images.size(); ++eyeIndex) {
				for (const bool invalidWidth : { false, true }) {
					invalidEye = *images[eyeIndex];
					(invalidWidth ? invalidEye.width : invalidEye.height) = invalidSize;
					auto invalidImages = images;
					invalidImages[eyeIndex] = &invalidEye;
					Require(!ComposeFramedStereo(invalidImages, projections, transforms, {}, vr::Eye_Left, vr::ColorSpace_Linear, { 16, 9 }, native, failedComposite), "invalid eye dimensions reached composition");
					Require(failedComposite.GetPixelsSize() == 0, "invalid dimensions allocated a composite");
				}
			}
		}
	}

	DirectX::ScratchImage detailedEye;
	Require(SUCCEEDED(detailedEye.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 4000, 4000, 1, 1)), "detail source allocation failed");
	const auto* detailSource = detailedEye.GetImage(0, 0, 0);
	for (size_t y = 0; y < detailSource->height; ++y) {
		auto* row = reinterpret_cast<uint32_t*>(detailSource->pixels + y * detailSource->rowPitch);
		for (size_t x = 0; x < detailSource->width; ++x)
			row[x] = x % 2 ? 0xffffffffu : 0xff000000u;
	}
	const std::array detailImages{ detailSource, detailSource };
	const std::array<std::array<float, 4>, 2> wideProjections{ { { -0.8f, 1.2f, -1, 1 }, { -1.2f, 0.8f, -1, 1 } } };
	for (const auto dominant : { vr::Eye_Left, vr::Eye_Right }) {
		double priorContrast = 0;
		for (const bool sourceDensity : { false, true }) {
			DirectX::ScratchImage combined;
			Require(ComposeFramedStereo(detailImages, wideProjections, transforms, {}, dominant, vr::ColorSpace_Linear, { 4000, 2250 }, sourceDensity, combined), "wide combined framing failed");
			const auto* output = combined.GetImage(0, 0, 0);
			Require(output && output->width == (sourceDensity ? 4800u : 4000u) && output->height == (sourceDensity ? 2700u : 2250u), "combined density dimensions incorrect");
			const auto* row = reinterpret_cast<const DirectX::XMFLOAT4*>(output->pixels + (output->height / 2) * output->rowPitch);
			double contrast = 0;
			for (size_t x = output->width / 2 - 64; x < output->width / 2 + 64; ++x)
				contrast += std::abs(row[x + 1].x - row[x].x);
			contrast /= 128;
			if (sourceDensity)
				Require(contrast > 0.99 && contrast > priorContrast * 1.5, "native combined framing lost central source detail");
			else
				priorContrast = contrast;
		}
	}
	std::cout << "4K/native framing, canted density and combined detail retention passed\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
