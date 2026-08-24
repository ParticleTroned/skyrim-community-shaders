// Screenshot Feature
// Non-blocking screenshot tool for flat (SE/AE) and VR. GPU copy runs on the
// render thread; encoding and disk I/O run on a dedicated worker thread so
// capture does not stall the frame.

#include "Features/ScreenshotFeature.h"
#include "Features/VR.h"
#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"
#include "Utils/NormalizedCoordinates.h"
#include "Utils/WinApi.h"
#include "VRAPI/CSpluginapi.h"
#include <DirectXTex.h>
#include <PCH.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <limits>
#include <numeric>
#include <thread>
#include <utility>

namespace
{
	constexpr uint32_t kCaptureTimeoutPresents = 6;
	constexpr std::size_t kMaxOutstandingScreenshots = 2;
	constexpr auto kReadbackMapTimeout = std::chrono::milliseconds(500);
	constexpr auto kReadbackMapRetryDelay = std::chrono::milliseconds(1);
	constexpr uint32_t kFramedEyeOutputWidth = 2560;
	constexpr uint32_t kFramedEyeOutputHeight = 1440;
	constexpr float kStereoFeatherFraction = 0.08f;
	constexpr float kFrameCoverageSafetyScale = 0.995f;
	constexpr std::size_t kVisibilityMaskGuardPixels = 2;
	constexpr uint32_t kMaxHiddenAreaTriangles = 4096;
	constexpr std::size_t kProjectionLeft = 0;
	constexpr std::size_t kProjectionRight = 1;
	constexpr std::size_t kProjectionBottom = 2;
	constexpr std::size_t kProjectionTop = 3;

	bool IsFramedCapture(ScreenshotFeature::VRCaptureSource a_source)
	{
		return a_source == ScreenshotFeature::VRCaptureSource::FramedEye ||
		       a_source == ScreenshotFeature::VRCaptureSource::FramedStereo;
	}

	bool IsSubmittedEyeCapture(ScreenshotFeature::VRCaptureSource a_source)
	{
		return a_source == ScreenshotFeature::VRCaptureSource::HMDSubmission ||
		       a_source == ScreenshotFeature::VRCaptureSource::HMDEye ||
		       IsFramedCapture(a_source);
	}

	bool IsValidCaptureEye(CSPluginAPI::CaptureEye001 a_eye)
	{
		return a_eye == CSPluginAPI::CaptureEye001::kLeft ||
		       a_eye == CSPluginAPI::CaptureEye001::kRight ||
		       a_eye == CSPluginAPI::CaptureEye001::kBoth;
	}

	const char* CaptureEyeName(CSPluginAPI::CaptureEye001 a_eye)
	{
		switch (a_eye) {
		case CSPluginAPI::CaptureEye001::kRight:
			return "Right";
		case CSPluginAPI::CaptureEye001::kBoth:
			return "Both";
		case CSPluginAPI::CaptureEye001::kLeft:
		default:
			return "Left";
		}
	}

	CSPluginAPI::CaptureEye001 ParseCaptureEye(const json& a_json, std::string_view a_key, CSPluginAPI::CaptureEye001 a_default)
	{
		if (!a_json.contains(a_key) || !a_json[a_key].is_string()) {
			return a_default;
		}
		const auto value = a_json[a_key].get<std::string>();
		if (value == "Right") {
			return CSPluginAPI::CaptureEye001::kRight;
		}
		if (value == "Both") {
			return CSPluginAPI::CaptureEye001::kBoth;
		}
		return CSPluginAPI::CaptureEye001::kLeft;
	}

	const char* DescribeCaptureSource(ScreenshotFeature::VRCaptureSource a_source)
	{
		switch (a_source) {
		case ScreenshotFeature::VRCaptureSource::HMDSubmission:
			return "the accepted HMD submission";
		case ScreenshotFeature::VRCaptureSource::FramedEye:
			return "a framed HMD eye";
		case ScreenshotFeature::VRCaptureSource::FramedStereo:
			return "a combined framed HMD view";
		case ScreenshotFeature::VRCaptureSource::HMDEye:
			return "one accepted HMD eye";
		case ScreenshotFeature::VRCaptureSource::DesktopMirror:
		default:
			return "the desktop mirror";
		}
	}

	// Capture source for the current runtime. SRV is non-owning - the texture's
	// lifetime is owned by the slot or a caller-held com_ptr.
	struct CaptureSource
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		// kFRAMEBUFFER's SRV aliases the swap-chain backbuffer, which ImGui's DX11
		// backend can't sample directly. When true, the preview path copies through
		// the SRV-readable cache instead.
		bool needsPreviewCache = false;
		const char* description = "(none)";
	};

	bool PopulateScratchImageFromStagingTexture(
		ID3D11DeviceContext* context,
		ID3D11Texture2D* stagingTexture,
		DXGI_FORMAT format,
		uint32_t width,
		uint32_t height,
		DirectX::ScratchImage& image)
	{
		if (!context || !stagingTexture || width == 0 || height == 0) {
			return false;
		}

		const HRESULT initHr = image.Initialize2D(format, width, height, 1, 1);
		if (FAILED(initHr)) {
			return false;
		}

		const auto* destImage = image.GetImage(0, 0, 0);
		auto* destPixels = image.GetPixels();
		if (!destImage || !destPixels) {
			return false;
		}
		std::memset(destPixels, 0, image.GetPixelsSize());

		D3D11_MAPPED_SUBRESOURCE mapped{};
		HRESULT mapResult = E_FAIL;
		const auto mapDeadline = std::chrono::steady_clock::now() + kReadbackMapTimeout;
		do {
			mapResult = context->Map(
				stagingTexture,
				0,
				D3D11_MAP_READ,
				D3D11_MAP_FLAG_DO_NOT_WAIT,
				&mapped);
			if (mapResult != DXGI_ERROR_WAS_STILL_DRAWING) {
				break;
			}
			std::this_thread::sleep_for(kReadbackMapRetryDelay);
		} while (std::chrono::steady_clock::now() < mapDeadline);

		if (FAILED(mapResult)) {
			return false;
		}

		const auto unmap = [&]() { context->Unmap(stagingTexture, 0); };
		if (!mapped.pData || mapped.RowPitch == 0) {
			unmap();
			return false;
		}

		// Driver-mapped region can be smaller than height * mapped.RowPitch
		// (alignment quirks, partial mappings). Cap by mapped.DepthPitch and
		// clamp each row's copy to whichever of source/dest pitches is smaller -
		// stepping past either side hits unmapped memory and the worker crashes
		// inside rep movsb (see crash 2026-05-19).
		const size_t bytesPerRow = std::min<size_t>(destImage->rowPitch, mapped.RowPitch);
		const size_t mappedDepth = mapped.DepthPitch != 0 ? mapped.DepthPitch :
		                                                    mapped.RowPitch * destImage->height;
		const size_t maxRowsBySize = mapped.RowPitch > 0 ? (mappedDepth / mapped.RowPitch) : 0;
		const size_t rowsToCopy = std::min<size_t>(destImage->height, maxRowsBySize);

		const auto* srcPixels = static_cast<const uint8_t*>(mapped.pData);

		for (size_t row = 0; row < rowsToCopy; ++row) {
			memcpy(
				destPixels + row * destImage->rowPitch,
				srcPixels + row * mapped.RowPitch,
				bytesPerRow);
		}

		unmap();
		return true;
	}

	void StripAlphaForBmp(DirectX::ScratchImage& image)
	{
		const DirectX::Image* firstImage = image.GetImage(0, 0, 0);
		if (!firstImage || firstImage->pixels == nullptr) {
			return;
		}

		const DXGI_FORMAT format = firstImage->format;
		if (format != DXGI_FORMAT_R8G8B8A8_UNORM &&
			format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
			format != DXGI_FORMAT_B8G8R8A8_UNORM &&
			format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
			return;
		}

		auto* pixels = image.GetPixels();
		const size_t rowPitch = firstImage->rowPitch;
		for (size_t y = 0; y < firstImage->height; ++y) {
			uint8_t* row = pixels + y * rowPitch;
			for (size_t x = 0; x < firstImage->width; ++x) {
				row[x * 4 + 3] = 0xFF;
			}
		}
	}

	bool IsEightBitPerComponentFormat(DXGI_FORMAT a_format)
	{
		switch (a_format) {
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	bool IsLinearCapture(vr::EColorSpace a_colorSpace, DXGI_FORMAT a_format)
	{
		if (a_colorSpace == vr::ColorSpace_Linear) {
			return true;
		}
		if (a_colorSpace == vr::ColorSpace_Gamma) {
			return false;
		}

		// OpenVR Auto treats 8-bit-per-component sources as gamma and all
		// other formats as linear.
		return !IsEightBitPerComponentFormat(a_format);
	}

	bool CenterCropAndResize(
		const DirectX::Image& a_source,
		uint32_t a_targetWidth,
		uint32_t a_targetHeight,
		vr::EColorSpace a_colorSpace,
		DirectX::ScratchImage& a_croppedImage,
		DirectX::ScratchImage& a_resizedImage)
	{
		if (!a_source.pixels || a_source.width == 0 || a_source.height == 0 ||
			a_targetWidth == 0 || a_targetHeight == 0) {
			return false;
		}

		const size_t aspectDivisor = std::gcd<size_t>(a_targetWidth, a_targetHeight);
		const size_t aspectWidth = a_targetWidth / aspectDivisor;
		const size_t aspectHeight = a_targetHeight / aspectDivisor;
		const size_t aspectScale = std::min(
			a_source.width / aspectWidth,
			a_source.height / aspectHeight);
		if (aspectScale == 0) {
			return false;
		}

		const size_t cropWidth = aspectScale * aspectWidth;
		const size_t cropHeight = aspectScale * aspectHeight;
		const size_t cropX = (a_source.width - cropWidth) / 2;
		const size_t cropY = (a_source.height - cropHeight) / 2;
		if (FAILED(a_croppedImage.Initialize2D(a_source.format, cropWidth, cropHeight, 1, 1))) {
			return false;
		}

		const auto* cropped = a_croppedImage.GetImage(0, 0, 0);
		const DirectX::Rect cropRect(cropX, cropY, cropWidth, cropHeight);
		if (!cropped || FAILED(DirectX::CopyRectangle(
							a_source,
							cropRect,
							*cropped,
							DirectX::TEX_FILTER_DEFAULT,
							0,
							0))) {
			return false;
		}

		DirectX::Image resizeSource = *cropped;
		uint32_t filterFlags = DirectX::TEX_FILTER_CUBIC | DirectX::TEX_FILTER_SEPARATE_ALPHA;
		if (a_colorSpace == vr::ColorSpace_Linear) {
			// Prevent an _SRGB resource format from overriding OpenVR's explicit
			// linear declaration inside DirectXTex's automatic filter flags.
			resizeSource.format = DirectX::MakeLinear(resizeSource.format);
		} else if (!IsLinearCapture(a_colorSpace, resizeSource.format)) {
			filterFlags |= DirectX::TEX_FILTER_SRGB;
		}
		return SUCCEEDED(DirectX::Resize(
			resizeSource,
			a_targetWidth,
			a_targetHeight,
			static_cast<DirectX::TEX_FILTER_FLAGS>(filterFlags),
			a_resizedImage));
	}

	struct HeadTangentBounds
	{
		float left = std::numeric_limits<float>::max();
		float right = std::numeric_limits<float>::lowest();
		float bottom = std::numeric_limits<float>::max();
		float top = std::numeric_limits<float>::lowest();
	};

	bool IsValidProjectionTangents(const std::array<float, 4>& a_tangents)
	{
		return std::ranges::all_of(a_tangents, [](float a_value) { return std::isfinite(a_value); }) &&
		       a_tangents[kProjectionRight] > a_tangents[kProjectionLeft] &&
		       a_tangents[kProjectionTop] > a_tangents[kProjectionBottom];
	}

	bool IsValidEyeRotation(const vr::HmdMatrix34_t& a_eyeToHead)
	{
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t column = 0; column < 3; ++column) {
				if (!std::isfinite(a_eyeToHead.m[row][column])) {
					return false;
				}
			}
		}
		return true;
	}

	void TransformEyeDirectionToHead(
		const vr::HmdMatrix34_t& a_eyeToHead,
		float a_eyeX,
		float a_eyeY,
		float a_eyeZ,
		float& a_headX,
		float& a_headY,
		float& a_headZ)
	{
		a_headX = a_eyeToHead.m[0][0] * a_eyeX + a_eyeToHead.m[0][1] * a_eyeY + a_eyeToHead.m[0][2] * a_eyeZ;
		a_headY = a_eyeToHead.m[1][0] * a_eyeX + a_eyeToHead.m[1][1] * a_eyeY + a_eyeToHead.m[1][2] * a_eyeZ;
		a_headZ = a_eyeToHead.m[2][0] * a_eyeX + a_eyeToHead.m[2][1] * a_eyeY + a_eyeToHead.m[2][2] * a_eyeZ;
	}

	void TransformHeadDirectionToEye(
		const vr::HmdMatrix34_t& a_eyeToHead,
		float a_headX,
		float a_headY,
		float a_headZ,
		float& a_eyeX,
		float& a_eyeY,
		float& a_eyeZ)
	{
		// Eye-to-head is rigid, so the inverse rotation is its transpose.
		a_eyeX = a_eyeToHead.m[0][0] * a_headX + a_eyeToHead.m[1][0] * a_headY + a_eyeToHead.m[2][0] * a_headZ;
		a_eyeY = a_eyeToHead.m[0][1] * a_headX + a_eyeToHead.m[1][1] * a_headY + a_eyeToHead.m[2][1] * a_headZ;
		a_eyeZ = a_eyeToHead.m[0][2] * a_headX + a_eyeToHead.m[1][2] * a_headY + a_eyeToHead.m[2][2] * a_headZ;
	}

	bool ComputeHeadTangentBounds(
		const std::array<float, 4>& a_tangents,
		const vr::HmdMatrix34_t& a_eyeToHead,
		HeadTangentBounds& a_bounds)
	{
		if (!IsValidProjectionTangents(a_tangents) || !IsValidEyeRotation(a_eyeToHead)) {
			return false;
		}

		constexpr float kMinForward = 1.0e-4f;
		const std::array horizontal{ a_tangents[kProjectionLeft], a_tangents[kProjectionRight] };
		const std::array vertical{ a_tangents[kProjectionBottom], a_tangents[kProjectionTop] };
		for (float eyeY : vertical) {
			for (float eyeX : horizontal) {
				float headX = 0.0f;
				float headY = 0.0f;
				float headZ = 0.0f;
				TransformEyeDirectionToHead(a_eyeToHead, eyeX, eyeY, -1.0f, headX, headY, headZ);
				const float forward = -headZ;
				if (!std::isfinite(forward) || forward <= kMinForward) {
					return false;
				}
				const float tangentX = headX / forward;
				const float tangentY = headY / forward;
				if (!std::isfinite(tangentX) || !std::isfinite(tangentY)) {
					return false;
				}
				a_bounds.left = std::min(a_bounds.left, tangentX);
				a_bounds.right = std::max(a_bounds.right, tangentX);
				a_bounds.bottom = std::min(a_bounds.bottom, tangentY);
				a_bounds.top = std::max(a_bounds.top, tangentY);
			}
		}

		return a_bounds.right > a_bounds.left && a_bounds.top > a_bounds.bottom;
	}

	bool MapHeadTangentToEyeUV(
		float a_headTangentX,
		float a_headTangentY,
		const std::array<float, 4>& a_tangents,
		const vr::HmdMatrix34_t& a_eyeToHead,
		float& a_u,
		float& a_v)
	{
		float eyeX = 0.0f;
		float eyeY = 0.0f;
		float eyeZ = 0.0f;
		TransformHeadDirectionToEye(
			a_eyeToHead,
			a_headTangentX,
			a_headTangentY,
			-1.0f,
			eyeX,
			eyeY,
			eyeZ);
		const float forward = -eyeZ;
		if (!std::isfinite(forward) || forward <= 1.0e-4f) {
			return false;
		}

		const float tangentX = eyeX / forward;
		const float tangentY = eyeY / forward;
		a_u = (tangentX - a_tangents[kProjectionLeft]) /
		      (a_tangents[kProjectionRight] - a_tangents[kProjectionLeft]);
		a_v = (a_tangents[kProjectionTop] - tangentY) /
		      (a_tangents[kProjectionTop] - a_tangents[kProjectionBottom]);
		constexpr float kEdgeTolerance = 1.0e-4f;
		if (!std::isfinite(a_u) || !std::isfinite(a_v) ||
			a_u < -kEdgeTolerance || a_u > 1.0f + kEdgeTolerance ||
			a_v < -kEdgeTolerance || a_v > 1.0f + kEdgeTolerance) {
			return false;
		}
		a_u = std::clamp(a_u, 0.0f, 1.0f);
		a_v = std::clamp(a_v, 0.0f, 1.0f);
		return true;
	}

	float TriangleEdge(
		const vr::HmdVector2_t& a_start,
		const vr::HmdVector2_t& a_end,
		float a_u,
		float a_v)
	{
		return (a_u - a_start.v[0]) * (a_end.v[1] - a_start.v[1]) -
		       (a_v - a_start.v[1]) * (a_end.v[0] - a_start.v[0]);
	}

	bool IsPointInTriangle(
		float a_u,
		float a_v,
		const vr::HmdVector2_t& a_first,
		const vr::HmdVector2_t& a_second,
		const vr::HmdVector2_t& a_third)
	{
		constexpr float kTriangleEpsilon = 1.0e-7f;
		const float area = TriangleEdge(a_first, a_second, a_third.v[0], a_third.v[1]);
		if (!std::isfinite(area) || std::abs(area) <= kTriangleEpsilon) {
			return false;
		}

		const std::array edges{
			TriangleEdge(a_first, a_second, a_u, a_v),
			TriangleEdge(a_second, a_third, a_u, a_v),
			TriangleEdge(a_third, a_first, a_u, a_v)
		};
		bool hasNegative = false;
		bool hasPositive = false;
		for (float edge : edges) {
			hasNegative |= edge < -kTriangleEpsilon;
			hasPositive |= edge > kTriangleEpsilon;
		}
		return !(hasNegative && hasPositive);
	}

	bool IsEyeSampleVisible(
		const std::vector<uint8_t>& a_visibilityMask,
		std::size_t a_width,
		std::size_t a_height,
		float a_u,
		float a_v);

	bool IsHeadTangentCovered(
		float a_headTangentX,
		float a_headTangentY,
		const std::array<std::array<float, 4>, 2>& a_projectionTangents,
		const std::array<vr::HmdMatrix34_t, 2>& a_eyeToHeadTransforms,
		const std::array<std::vector<uint8_t>, 2>& a_visibilityMasks,
		const std::array<const DirectX::Image*, 2>& a_eyeImages)
	{
		for (std::size_t eyeIndex = 0; eyeIndex < a_projectionTangents.size(); ++eyeIndex) {
			float sourceU = 0.0f;
			float sourceV = 0.0f;
			if (MapHeadTangentToEyeUV(
					a_headTangentX,
					a_headTangentY,
					a_projectionTangents[eyeIndex],
					a_eyeToHeadTransforms[eyeIndex],
					sourceU,
					sourceV) &&
				a_eyeImages[eyeIndex] &&
				IsEyeSampleVisible(
					a_visibilityMasks[eyeIndex],
					a_eyeImages[eyeIndex]->width,
					a_eyeImages[eyeIndex]->height,
					sourceU,
					sourceV)) {
				return true;
			}
		}
		return false;
	}

	bool IsFrameCoverageSampled(
		float a_centerX,
		float a_centerY,
		float a_frameWidth,
		float a_frameHeight,
		const std::array<std::array<float, 4>, 2>& a_projectionTangents,
		const std::array<vr::HmdMatrix34_t, 2>& a_eyeToHeadTransforms,
		const std::array<std::vector<uint8_t>, 2>& a_visibilityMasks,
		const std::array<const DirectX::Image*, 2>& a_eyeImages)
	{
		// A modest interior grid catches gaps introduced by reducing rotated eye
		// frusta to axis-aligned bounds. The per-pixel compositor still validates
		// coverage, so this is only a cheap way to fit the frame before allocation.
		constexpr int kCoverageSamples = 64;
		for (int y = 0; y <= kCoverageSamples; ++y) {
			const float normalizedY = static_cast<float>(y) / static_cast<float>(kCoverageSamples) - 0.5f;
			const float tangentY = a_centerY - normalizedY * a_frameHeight;
			for (int x = 0; x <= kCoverageSamples; ++x) {
				const float normalizedX = static_cast<float>(x) / static_cast<float>(kCoverageSamples) - 0.5f;
				const float tangentX = a_centerX + normalizedX * a_frameWidth;
				if (!IsHeadTangentCovered(
						tangentX,
						tangentY,
						a_projectionTangents,
						a_eyeToHeadTransforms,
						a_visibilityMasks,
						a_eyeImages)) {
					return false;
				}
			}
		}
		return true;
	}

	bool IsFrameCoverageComplete(
		float a_centerX,
		float a_centerY,
		float a_frameWidth,
		float a_frameHeight,
		const std::array<std::array<float, 4>, 2>& a_projectionTangents,
		const std::array<vr::HmdMatrix34_t, 2>& a_eyeToHeadTransforms,
		const std::array<std::vector<uint8_t>, 2>& a_visibilityMasks,
		const std::array<const DirectX::Image*, 2>& a_eyeImages)
	{
		const float frameLeft = a_centerX - a_frameWidth * 0.5f;
		const float frameTop = a_centerY + a_frameHeight * 0.5f;
		for (std::size_t y = 0; y < kFramedEyeOutputHeight; ++y) {
			const float tangentY = frameTop -
			                       (static_cast<float>(y) + 0.5f) /
			                           static_cast<float>(kFramedEyeOutputHeight) * a_frameHeight;
			for (std::size_t x = 0; x < kFramedEyeOutputWidth; ++x) {
				const float tangentX = frameLeft +
				                       (static_cast<float>(x) + 0.5f) /
				                           static_cast<float>(kFramedEyeOutputWidth) * a_frameWidth;
				if (!IsHeadTangentCovered(
						tangentX,
						tangentY,
						a_projectionTangents,
						a_eyeToHeadTransforms,
						a_visibilityMasks,
						a_eyeImages)) {
					return false;
				}
			}
		}
		return true;
	}

	DirectX::XMFLOAT4 LerpColor(
		const DirectX::XMFLOAT4& a_from,
		const DirectX::XMFLOAT4& a_to,
		float a_weight)
	{
		return {
			a_from.x + (a_to.x - a_from.x) * a_weight,
			a_from.y + (a_to.y - a_from.y) * a_weight,
			a_from.z + (a_to.z - a_from.z) * a_weight,
			a_from.w + (a_to.w - a_from.w) * a_weight
		};
	}

	bool SampleLinearFloatImage(
		const DirectX::Image& a_image,
		float a_u,
		float a_v,
		DirectX::XMFLOAT4& a_color)
	{
		if (!a_image.pixels || a_image.format != DXGI_FORMAT_R32G32B32A32_FLOAT ||
			a_image.width == 0 || a_image.height == 0 ||
			a_image.rowPitch < a_image.width * sizeof(DirectX::XMFLOAT4)) {
			return false;
		}

		const float sourceX = std::clamp(
			a_u * static_cast<float>(a_image.width) - 0.5f,
			0.0f,
			static_cast<float>(a_image.width - 1));
		const float sourceY = std::clamp(
			a_v * static_cast<float>(a_image.height) - 0.5f,
			0.0f,
			static_cast<float>(a_image.height - 1));
		const std::size_t x0 = static_cast<std::size_t>(std::floor(sourceX));
		const std::size_t y0 = static_cast<std::size_t>(std::floor(sourceY));
		const std::size_t x1 = std::min(x0 + 1, a_image.width - 1);
		const std::size_t y1 = std::min(y0 + 1, a_image.height - 1);
		const float xWeight = sourceX - static_cast<float>(x0);
		const float yWeight = sourceY - static_cast<float>(y0);

		const auto* row0 = reinterpret_cast<const DirectX::XMFLOAT4*>(a_image.pixels + y0 * a_image.rowPitch);
		const auto* row1 = reinterpret_cast<const DirectX::XMFLOAT4*>(a_image.pixels + y1 * a_image.rowPitch);
		const auto top = LerpColor(row0[x0], row0[x1], xWeight);
		const auto bottom = LerpColor(row1[x0], row1[x1], xWeight);
		a_color = LerpColor(top, bottom, yWeight);
		return true;
	}

	bool BuildEyeVisibilityMask(
		const std::vector<vr::HmdVector2_t>& a_hiddenAreaMesh,
		std::size_t a_width,
		std::size_t a_height,
		std::vector<uint8_t>& a_visibilityMask)
	{
		a_visibilityMask.clear();
		if (a_hiddenAreaMesh.empty()) {
			return true;
		}
		if (a_width == 0 || a_height == 0 ||
			a_width > std::numeric_limits<std::size_t>::max() / a_height) {
			return false;
		}

		a_visibilityMask.assign(a_width * a_height, 1);
		for (std::size_t vertex = 0; vertex + 2 < a_hiddenAreaMesh.size(); vertex += 3) {
			const auto& first = a_hiddenAreaMesh[vertex];
			const auto& second = a_hiddenAreaMesh[vertex + 1];
			const auto& third = a_hiddenAreaMesh[vertex + 2];
			const float minU = std::min({ first.v[0], second.v[0], third.v[0] });
			const float maxU = std::max({ first.v[0], second.v[0], third.v[0] });
			const float minV = std::min({ first.v[1], second.v[1], third.v[1] });
			const float maxV = std::max({ first.v[1], second.v[1], third.v[1] });
			if (!std::isfinite(minU) || !std::isfinite(maxU) ||
				!std::isfinite(minV) || !std::isfinite(maxV)) {
				return false;
			}
			if (maxU < 0.0f || minU > 1.0f || maxV < 0.0f || minV > 1.0f) {
				continue;
			}
			const float clippedMinU = std::clamp(minU, 0.0f, 1.0f);
			const float clippedMaxU = std::clamp(maxU, 0.0f, 1.0f);
			const float clippedMinV = std::clamp(minV, 0.0f, 1.0f);
			const float clippedMaxV = std::clamp(maxV, 0.0f, 1.0f);

			const auto lastX = static_cast<int64_t>(a_width - 1);
			const auto lastY = static_cast<int64_t>(a_height - 1);
			const int64_t firstX = std::clamp(
				static_cast<int64_t>(std::floor(static_cast<double>(clippedMinU) * a_width)) - 1,
				int64_t{ 0 },
				lastX);
			const int64_t finalX = std::clamp(
				static_cast<int64_t>(std::ceil(static_cast<double>(clippedMaxU) * a_width)) + 1,
				int64_t{ 0 },
				lastX);
			const int64_t firstY = std::clamp(
				static_cast<int64_t>(std::floor(static_cast<double>(clippedMinV) * a_height)) - 1,
				int64_t{ 0 },
				lastY);
			const int64_t finalY = std::clamp(
				static_cast<int64_t>(std::ceil(static_cast<double>(clippedMaxV) * a_height)) + 1,
				int64_t{ 0 },
				lastY);
			for (int64_t y = firstY; y <= finalY; ++y) {
				const float sampleV = (static_cast<float>(y) + 0.5f) / static_cast<float>(a_height);
				for (int64_t x = firstX; x <= finalX; ++x) {
					const float sampleU = (static_cast<float>(x) + 0.5f) / static_cast<float>(a_width);
					if (IsPointInTriangle(sampleU, sampleV, first, second, third)) {
						a_visibilityMask[static_cast<std::size_t>(y) * a_width + static_cast<std::size_t>(x)] = 0;
					}
				}
			}
		}
		return true;
	}

	void RefineEyeVisibilityMaskFromSubmittedImage(
		const DirectX::Image& a_image,
		std::vector<uint8_t>& a_visibilityMask)
	{
		if (!a_image.pixels || a_image.width == 0 || a_image.height == 0 ||
			a_image.width > std::numeric_limits<std::size_t>::max() / a_image.height) {
			return;
		}
		if (!DirectX::HasAlpha(a_image.format)) {
			logger::trace(
				"Screenshot submitted-mask refinement skipped: format {} has no alpha channel.",
				static_cast<uint32_t>(a_image.format));
			return;
		}

		const std::size_t pixelCount = a_image.width * a_image.height;
		if ((!a_visibilityMask.empty() && a_visibilityMask.size() != pixelCount) ||
			pixelCount > std::numeric_limits<uint32_t>::max()) {
			return;
		}

		std::vector<uint8_t> clearCandidates(pixelCount, 0);
		std::size_t centerSampleCount = 0;
		std::size_t nonzeroCenterAlphaCount = 0;
		const std::size_t centerLeft = a_image.width / 4;
		const std::size_t centerRight = a_image.width - centerLeft;
		const std::size_t centerTop = a_image.height / 4;
		const std::size_t centerBottom = a_image.height - centerTop;
		constexpr float kClearAlphaEpsilon = 1.0e-4f;
		std::vector<uint32_t> pending;
		auto enqueueCandidate = [&](std::size_t a_index) {
			if (clearCandidates[a_index] == 1) {
				clearCandidates[a_index] = 2;
				pending.push_back(static_cast<uint32_t>(a_index));
			}
		};

		const HRESULT evaluationResult = DirectX::EvaluateImage(
			a_image,
			[&](const DirectX::XMVECTOR* a_pixels, std::size_t a_width, std::size_t a_y) {
				if (!a_pixels || a_y >= a_image.height) {
					return;
				}
				const std::size_t rowWidth = std::min(a_width, a_image.width);
				for (std::size_t x = 0; x < rowWidth; ++x) {
					DirectX::XMFLOAT4 color;
					DirectX::XMStoreFloat4(&color, a_pixels[x]);
					const bool finiteAlpha = std::isfinite(color.w);
					const std::size_t index = a_y * a_image.width + x;
					if (finiteAlpha && std::abs(color.w) <= kClearAlphaEpsilon) {
						clearCandidates[index] = 1;
						if (x == 0 || x + 1 == a_image.width ||
							a_y == 0 || a_y + 1 == a_image.height) {
							enqueueCandidate(index);
						}
					}

					if (finiteAlpha && x >= centerLeft && x < centerRight &&
						a_y >= centerTop && a_y < centerBottom &&
						(a_visibilityMask.empty() || a_visibilityMask[index] != 0)) {
						++centerSampleCount;
						if (std::abs(color.w) > kClearAlphaEpsilon) {
							++nonzeroCenterAlphaCount;
						}
					}
				}
			});

		// Some render targets have an alpha channel in their DXGI format without
		// carrying meaningful alpha. Only trust zero alpha as the submitted
		// hidden-area clear when the visible center establishes a nonzero contract.
		constexpr std::size_t kMinimumCenterSamples = 64;
		if (FAILED(evaluationResult) || centerSampleCount < kMinimumCenterSamples ||
			nonzeroCenterAlphaCount * 100 < centerSampleCount * 98) {
			logger::trace(
				"Screenshot submitted-mask refinement skipped: alpha contract was not reliable "
				"(format {}, center nonzero {}/{}, evaluation {:#x}).",
				static_cast<uint32_t>(a_image.format),
				nonzeroCenterAlphaCount,
				centerSampleCount,
				static_cast<uint32_t>(evaluationResult));
			return;
		}

		if (pending.empty()) {
			logger::trace("Screenshot submitted-mask refinement found no edge-connected clear pixels.");
			return;
		}
		if (a_visibilityMask.empty()) {
			a_visibilityMask.assign(pixelCount, 1);
		}

		for (std::size_t next = 0; next < pending.size(); ++next) {
			const std::size_t index = pending[next];
			a_visibilityMask[index] = 0;
			const std::size_t y = index / a_image.width;
			const std::size_t x = index - y * a_image.width;
			const std::size_t firstY = y == 0 ? 0 : y - 1;
			const std::size_t finalY = std::min(y + 1, a_image.height - 1);
			const std::size_t firstX = x == 0 ? 0 : x - 1;
			const std::size_t finalX = std::min(x + 1, a_image.width - 1);
			for (std::size_t adjacentY = firstY; adjacentY <= finalY; ++adjacentY) {
				for (std::size_t adjacentX = firstX; adjacentX <= finalX; ++adjacentX) {
					enqueueCandidate(adjacentY * a_image.width + adjacentX);
				}
			}
		}
		logger::trace(
			"Screenshot submitted-mask refinement classified {} edge-connected clear pixels.",
			pending.size());
	}

	void AddEyeVisibilitySamplingGuard(
		std::vector<uint8_t>& a_visibilityMask,
		std::size_t a_width,
		std::size_t a_height)
	{
		if (a_visibilityMask.empty() || a_width == 0 || a_height == 0 ||
			a_width > std::numeric_limits<std::size_t>::max() / a_height ||
			a_visibilityMask.size() != a_width * a_height ||
			std::find(a_visibilityMask.begin(), a_visibilityMask.end(), uint8_t{ 0 }) == a_visibilityMask.end()) {
			return;
		}

		std::vector<uint8_t> horizontal(a_visibilityMask.size(), 1);
		const std::size_t horizontalRadius = std::min(kVisibilityMaskGuardPixels, a_width - 1);
		for (std::size_t y = 0; y < a_height; ++y) {
			const std::size_t rowStart = y * a_width;
			std::size_t hiddenCount = 0;
			for (std::size_t x = 0; x <= horizontalRadius; ++x) {
				hiddenCount += a_visibilityMask[rowStart + x] == 0 ? 1u : 0u;
			}
			for (std::size_t x = 0; x < a_width; ++x) {
				horizontal[rowStart + x] = hiddenCount == 0 ? 1 : 0;
				if (x >= horizontalRadius) {
					hiddenCount -= a_visibilityMask[rowStart + x - horizontalRadius] == 0 ? 1u : 0u;
				}
				if (x + horizontalRadius + 1 < a_width) {
					hiddenCount += a_visibilityMask[rowStart + x + horizontalRadius + 1] == 0 ? 1u : 0u;
				}
			}
		}

		const std::size_t verticalRadius = std::min(kVisibilityMaskGuardPixels, a_height - 1);
		for (std::size_t x = 0; x < a_width; ++x) {
			std::size_t hiddenCount = 0;
			for (std::size_t y = 0; y <= verticalRadius; ++y) {
				hiddenCount += horizontal[y * a_width + x] == 0 ? 1u : 0u;
			}
			for (std::size_t y = 0; y < a_height; ++y) {
				a_visibilityMask[y * a_width + x] = hiddenCount == 0 ? 1 : 0;
				if (y >= verticalRadius) {
					hiddenCount -= horizontal[(y - verticalRadius) * a_width + x] == 0 ? 1u : 0u;
				}
				if (y + verticalRadius + 1 < a_height) {
					hiddenCount += horizontal[(y + verticalRadius + 1) * a_width + x] == 0 ? 1u : 0u;
				}
			}
		}
	}

	bool BuildDominantPeripheralBoundary(
		const std::vector<uint8_t>& a_visibilityMask,
		std::size_t a_width,
		std::size_t a_height,
		bool a_dominantIsRight,
		std::vector<float>& a_boundary)
	{
		if (a_width == 0 || a_height == 0 ||
			a_width > std::numeric_limits<std::size_t>::max() / a_height ||
			(!a_visibilityMask.empty() && a_visibilityMask.size() != a_width * a_height)) {
			return false;
		}

		a_boundary.assign(
			a_height,
			a_dominantIsRight ? -0.5f : static_cast<float>(a_width) - 0.5f);
		if (a_visibilityMask.empty()) {
			return true;
		}

		for (std::size_t y = 0; y < a_height; ++y) {
			const std::size_t rowStart = y * a_width;
			if (a_dominantIsRight) {
				a_boundary[y] = static_cast<float>(a_width);
				for (std::size_t x = 0; x < a_width; ++x) {
					if (a_visibilityMask[rowStart + x] != 0) {
						a_boundary[y] = static_cast<float>(x) - 0.5f;
						break;
					}
				}
			} else {
				a_boundary[y] = -1.0f;
				for (std::size_t x = a_width; x > 0; --x) {
					if (a_visibilityMask[rowStart + x - 1] != 0) {
						a_boundary[y] = static_cast<float>(x) - 0.5f;
						break;
					}
				}
			}
		}
		return true;
	}

	float ComputeDominantFeatherWeight(
		const std::vector<float>& a_boundary,
		std::size_t a_width,
		std::size_t a_height,
		bool a_dominantIsRight,
		float a_u,
		float a_v)
	{
		if (a_boundary.size() != a_height || a_width == 0 || a_height == 0) {
			return 0.0f;
		}

		const float sourceX = std::clamp(
			a_u * static_cast<float>(a_width) - 0.5f,
			0.0f,
			static_cast<float>(a_width - 1));
		const float sourceY = std::clamp(
			a_v * static_cast<float>(a_height) - 0.5f,
			0.0f,
			static_cast<float>(a_height - 1));
		const std::size_t y0 = static_cast<std::size_t>(std::floor(sourceY));
		const std::size_t y1 = std::min(y0 + 1, a_height - 1);
		const float boundary = a_dominantIsRight ?
		                           std::max(a_boundary[y0], a_boundary[y1]) :
		                           std::min(a_boundary[y0], a_boundary[y1]);
		const float distanceFromPeripheralEdge = a_dominantIsRight ?
		                                             sourceX - boundary :
		                                             boundary - sourceX;
		float weight = std::clamp(
			distanceFromPeripheralEdge /
				(kStereoFeatherFraction * static_cast<float>(a_width)),
			0.0f,
			1.0f);
		return weight * weight * (3.0f - 2.0f * weight);
	}

	bool IsEyeSampleVisible(
		const std::vector<uint8_t>& a_visibilityMask,
		std::size_t a_width,
		std::size_t a_height,
		float a_u,
		float a_v)
	{
		if (a_visibilityMask.empty()) {
			return true;
		}
		if (a_width == 0 || a_height == 0 ||
			a_width > std::numeric_limits<std::size_t>::max() / a_height ||
			a_visibilityMask.size() != a_width * a_height) {
			return false;
		}

		const float sourceX = std::clamp(
			a_u * static_cast<float>(a_width) - 0.5f,
			0.0f,
			static_cast<float>(a_width - 1));
		const float sourceY = std::clamp(
			a_v * static_cast<float>(a_height) - 0.5f,
			0.0f,
			static_cast<float>(a_height - 1));
		const std::size_t x0 = static_cast<std::size_t>(std::floor(sourceX));
		const std::size_t y0 = static_cast<std::size_t>(std::floor(sourceY));
		const std::size_t x1 = std::min(x0 + 1, a_width - 1);
		const std::size_t y1 = std::min(y0 + 1, a_height - 1);
		const float xWeight = sourceX - static_cast<float>(x0);
		const float yWeight = sourceY - static_cast<float>(y0);
		if (a_visibilityMask[y0 * a_width + x0] == 0 ||
			(xWeight > 0.0f && a_visibilityMask[y0 * a_width + x1] == 0) ||
			(yWeight > 0.0f && a_visibilityMask[y1 * a_width + x0] == 0) ||
			(xWeight > 0.0f && yWeight > 0.0f && a_visibilityMask[y1 * a_width + x1] == 0)) {
			return false;
		}
		return true;
	}

	bool ConvertCaptureToLinearFloat(
		const DirectX::Image& a_source,
		vr::EColorSpace a_colorSpace,
		DirectX::ScratchImage& a_output)
	{
		DirectX::Image conversionSource = a_source;
		uint32_t convertFlags = DirectX::TEX_FILTER_DEFAULT;
		if (a_colorSpace == vr::ColorSpace_Linear) {
			// OpenVR's explicit colorspace is authoritative. DirectXTex otherwise
			// decodes an _SRGB format automatically, even with no SRGB_IN flag.
			conversionSource.format = DirectX::MakeLinear(conversionSource.format);
		} else if (!IsLinearCapture(a_colorSpace, conversionSource.format)) {
			convertFlags |= DirectX::TEX_FILTER_SRGB_IN;
		}

		return SUCCEEDED(DirectX::Convert(
			conversionSource,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			static_cast<DirectX::TEX_FILTER_FLAGS>(convertFlags),
			0.0f,
			a_output));
	}

	bool ComposeFramedStereo(
		const std::array<const DirectX::Image*, 2>& a_eyeImages,
		const std::array<std::array<float, 4>, 2>& a_projectionTangents,
		const std::array<vr::HmdMatrix34_t, 2>& a_eyeToHeadTransforms,
		const std::array<std::vector<vr::HmdVector2_t>, 2>& a_hiddenAreaMeshes,
		vr::EVREye a_dominantEye,
		vr::EColorSpace a_colorSpace,
		DirectX::ScratchImage& a_output)
	{
		if (!a_eyeImages[0] || !a_eyeImages[1]) {
			return false;
		}

		std::array<HeadTangentBounds, 2> headBounds{};
		for (std::size_t eyeIndex = 0; eyeIndex < headBounds.size(); ++eyeIndex) {
			if (!ComputeHeadTangentBounds(
					a_projectionTangents[eyeIndex],
					a_eyeToHeadTransforms[eyeIndex],
					headBounds[eyeIndex])) {
				return false;
			}
		}
		std::array<std::vector<uint8_t>, 2> eyeVisibilityMasks;
		for (std::size_t eyeIndex = 0; eyeIndex < eyeVisibilityMasks.size(); ++eyeIndex) {
			if (!BuildEyeVisibilityMask(
					a_hiddenAreaMeshes[eyeIndex],
					a_eyeImages[eyeIndex]->width,
					a_eyeImages[eyeIndex]->height,
					eyeVisibilityMasks[eyeIndex])) {
				return false;
			}
			RefineEyeVisibilityMaskFromSubmittedImage(
				*a_eyeImages[eyeIndex],
				eyeVisibilityMasks[eyeIndex]);
			AddEyeVisibilitySamplingGuard(
				eyeVisibilityMasks[eyeIndex],
				a_eyeImages[eyeIndex]->width,
				a_eyeImages[eyeIndex]->height);
		}

		const float unionLeft = std::min(headBounds[0].left, headBounds[1].left);
		const float unionRight = std::max(headBounds[0].right, headBounds[1].right);
		const float unionBottom = std::min(headBounds[0].bottom, headBounds[1].bottom);
		const float unionTop = std::max(headBounds[0].top, headBounds[1].top);
		if (!(unionRight > unionLeft) || !(unionTop > unionBottom)) {
			return false;
		}

		const std::size_t dominantIndex = a_dominantEye == vr::Eye_Right ? 1u : 0u;
		float dominantHeadX = 0.0f;
		float dominantHeadY = 0.0f;
		float dominantHeadZ = 0.0f;
		TransformEyeDirectionToHead(
			a_eyeToHeadTransforms[dominantIndex],
			0.0f,
			0.0f,
			-1.0f,
			dominantHeadX,
			dominantHeadY,
			dominantHeadZ);
		const float dominantForward = -dominantHeadZ;
		if (!std::isfinite(dominantForward) || dominantForward <= 1.0e-4f) {
			return false;
		}
		const float desiredCenterX = dominantHeadX / dominantForward;
		const float desiredCenterY = dominantHeadY / dominantForward;

		constexpr float targetAspect = static_cast<float>(kFramedEyeOutputWidth) /
		                               static_cast<float>(kFramedEyeOutputHeight);
		const float availableHalfWidth = std::min(
			desiredCenterX - unionLeft,
			unionRight - desiredCenterX);
		const float availableHalfHeight = std::min(
			desiredCenterY - unionBottom,
			unionTop - desiredCenterY);
		if (!(availableHalfWidth > 0.0f) || !(availableHalfHeight > 0.0f) ||
			!IsHeadTangentCovered(
				desiredCenterX,
				desiredCenterY,
				a_projectionTangents,
				a_eyeToHeadTransforms,
				eyeVisibilityMasks,
				a_eyeImages)) {
			return false;
		}

		// Keep the selected eye's optical axis at the exact center. Maximizing
		// first and clamping afterward would silently recenter wide frames on the
		// union midpoint, defeating the dominant-eye setting.
		float frameHalfWidth = std::min(availableHalfWidth, availableHalfHeight * targetAspect);
		float frameHalfHeight = frameHalfWidth / targetAspect;
		float frameWidth = frameHalfWidth * 2.0f;
		float frameHeight = frameHalfHeight * 2.0f;

		if (!IsFrameCoverageSampled(
				desiredCenterX,
				desiredCenterY,
				frameWidth,
				frameHeight,
				a_projectionTangents,
				a_eyeToHeadTransforms,
				eyeVisibilityMasks,
				a_eyeImages)) {
			// Rotated frusta can leave a notch inside their axis-aligned union.
			// Find the largest sampled-safe frame without moving the chosen center.
			float coveredScale = 0.0f;
			float uncoveredScale = 1.0f;
			for (int iteration = 0; iteration < 18; ++iteration) {
				const float candidateScale = (coveredScale + uncoveredScale) * 0.5f;
				if (IsFrameCoverageSampled(
						desiredCenterX,
						desiredCenterY,
						frameWidth * candidateScale,
						frameHeight * candidateScale,
						a_projectionTangents,
						a_eyeToHeadTransforms,
						eyeVisibilityMasks,
						a_eyeImages)) {
					coveredScale = candidateScale;
				} else {
					uncoveredScale = candidateScale;
				}
			}
			if (coveredScale <= 1.0e-3f) {
				return false;
			}
			frameWidth *= coveredScale;
			frameHeight *= coveredScale;
			frameHalfWidth = frameWidth * 0.5f;
			frameHalfHeight = frameHeight * 0.5f;
		}
		if (!eyeVisibilityMasks[0].empty() || !eyeVisibilityMasks[1].empty()) {
			frameWidth *= kFrameCoverageSafetyScale;
			frameHeight *= kFrameCoverageSafetyScale;
			frameHalfWidth = frameWidth * 0.5f;
			frameHalfHeight = frameHeight * 0.5f;
		}
		if (!IsFrameCoverageComplete(
				desiredCenterX,
				desiredCenterY,
				frameWidth,
				frameHeight,
				a_projectionTangents,
				a_eyeToHeadTransforms,
				eyeVisibilityMasks,
				a_eyeImages)) {
			// The coarse fit cannot see a narrow mask notch. Fit the actual output
			// pixel grid so one uncovered pixel cannot force a raw-eye fallback.
			float coveredScale = 0.0f;
			float uncoveredScale = 1.0f;
			for (int iteration = 0; iteration < 8; ++iteration) {
				const float candidateScale = (coveredScale + uncoveredScale) * 0.5f;
				if (IsFrameCoverageComplete(
						desiredCenterX,
						desiredCenterY,
						frameWidth * candidateScale,
						frameHeight * candidateScale,
						a_projectionTangents,
						a_eyeToHeadTransforms,
						eyeVisibilityMasks,
						a_eyeImages)) {
					coveredScale = candidateScale;
				} else {
					uncoveredScale = candidateScale;
				}
			}
			if (coveredScale <= 1.0e-3f) {
				return false;
			}
			frameWidth *= coveredScale * kFrameCoverageSafetyScale;
			frameHeight *= coveredScale * kFrameCoverageSafetyScale;
			frameHalfWidth = frameWidth * 0.5f;
			frameHalfHeight = frameHeight * 0.5f;
			if (!IsFrameCoverageComplete(
					desiredCenterX,
					desiredCenterY,
					frameWidth,
					frameHeight,
					a_projectionTangents,
					a_eyeToHeadTransforms,
					eyeVisibilityMasks,
					a_eyeImages)) {
				return false;
			}
		}

		const float frameLeft = desiredCenterX - frameHalfWidth;
		const float frameTop = desiredCenterY + frameHalfHeight;

		if (FAILED(a_output.Initialize2D(
				DXGI_FORMAT_R32G32B32A32_FLOAT,
				kFramedEyeOutputWidth,
				kFramedEyeOutputHeight,
				1,
				1))) {
			return false;
		}
		auto* outputImage = a_output.GetImage(0, 0, 0);
		if (!outputImage || !outputImage->pixels ||
			outputImage->rowPitch < outputImage->width * sizeof(DirectX::XMFLOAT4)) {
			return false;
		}

		std::vector<uint8_t> dominantWeights(outputImage->width * outputImage->height, 0);
		std::vector<float> dominantPeripheralBoundary;
		if (!BuildDominantPeripheralBoundary(
				eyeVisibilityMasks[dominantIndex],
				a_eyeImages[dominantIndex]->width,
				a_eyeImages[dominantIndex]->height,
				dominantIndex == 1u,
				dominantPeripheralBoundary)) {
			return false;
		}
		DirectX::ScratchImage linearEye;
		if (!ConvertCaptureToLinearFloat(*a_eyeImages[dominantIndex], a_colorSpace, linearEye)) {
			return false;
		}
		const auto* linearEyeImage = linearEye.GetImage(0, 0, 0);
		if (!linearEyeImage) {
			return false;
		}

		// Store the dominant eye first and retain a compact feather weight per
		// output pixel. Processing eyes sequentially avoids holding two full-size
		// RGBA32F conversions at once.
		for (std::size_t y = 0; y < outputImage->height; ++y) {
			auto* outputRow = reinterpret_cast<DirectX::XMFLOAT4*>(outputImage->pixels + y * outputImage->rowPitch);
			const float tangentY = frameTop -
			                       (static_cast<float>(y) + 0.5f) /
			                           static_cast<float>(outputImage->height) * frameHeight;
			for (std::size_t x = 0; x < outputImage->width; ++x) {
				const float tangentX = frameLeft +
				                       (static_cast<float>(x) + 0.5f) /
				                           static_cast<float>(outputImage->width) * frameWidth;
				float sourceU = 0.0f;
				float sourceV = 0.0f;
				DirectX::XMFLOAT4 dominantColor{};
				if (MapHeadTangentToEyeUV(
						tangentX,
						tangentY,
						a_projectionTangents[dominantIndex],
						a_eyeToHeadTransforms[dominantIndex],
						sourceU,
						sourceV) &&
					IsEyeSampleVisible(
						eyeVisibilityMasks[dominantIndex],
						linearEyeImage->width,
						linearEyeImage->height,
						sourceU,
						sourceV) &&
					SampleLinearFloatImage(*linearEyeImage, sourceU, sourceV, dominantColor)) {
					const float dominantWeight = ComputeDominantFeatherWeight(
						dominantPeripheralBoundary,
						linearEyeImage->width,
						linearEyeImage->height,
						dominantIndex == 1u,
						sourceU,
						sourceV);
					outputRow[x] = dominantColor;
					dominantWeights[y * outputImage->width + x] = static_cast<uint8_t>(
						std::lround(dominantWeight * 254.0f) + 1);
				} else {
					outputRow[x] = { 0.0f, 0.0f, 0.0f, 1.0f };
				}
			}
		}

		linearEye.Release();
		const std::size_t otherIndex = 1u - dominantIndex;
		if (!ConvertCaptureToLinearFloat(*a_eyeImages[otherIndex], a_colorSpace, linearEye)) {
			return false;
		}
		linearEyeImage = linearEye.GetImage(0, 0, 0);
		if (!linearEyeImage) {
			return false;
		}

		for (std::size_t y = 0; y < outputImage->height; ++y) {
			auto* outputRow = reinterpret_cast<DirectX::XMFLOAT4*>(outputImage->pixels + y * outputImage->rowPitch);
			const float tangentY = frameTop -
			                       (static_cast<float>(y) + 0.5f) /
			                           static_cast<float>(outputImage->height) * frameHeight;
			for (std::size_t x = 0; x < outputImage->width; ++x) {
				const uint8_t dominantWeightByte = dominantWeights[y * outputImage->width + x];
				if (dominantWeightByte == 255) {
					continue;
				}

				const float tangentX = frameLeft +
				                       (static_cast<float>(x) + 0.5f) /
				                           static_cast<float>(outputImage->width) * frameWidth;
				float sourceU = 0.0f;
				float sourceV = 0.0f;
				DirectX::XMFLOAT4 otherColor{};
				const bool hasOtherColor = MapHeadTangentToEyeUV(
											   tangentX,
											   tangentY,
											   a_projectionTangents[otherIndex],
											   a_eyeToHeadTransforms[otherIndex],
											   sourceU,
											   sourceV) &&
				                           IsEyeSampleVisible(
											   eyeVisibilityMasks[otherIndex],
											   linearEyeImage->width,
											   linearEyeImage->height,
											   sourceU,
											   sourceV) &&
				                           SampleLinearFloatImage(
											   *linearEyeImage,
											   sourceU,
											   sourceV,
											   otherColor);
				if (!hasOtherColor) {
					if (dominantWeightByte == 0) {
						// Never emit a synthetic black hole. Let the caller fall back to
						// a conventional dominant-eye frame for unusual headset geometry.
						return false;
					}
					continue;
				}

				if (dominantWeightByte == 0) {
					outputRow[x] = otherColor;
				} else {
					const float dominantWeight = static_cast<float>(dominantWeightByte - 1) / 254.0f;
					outputRow[x] = LerpColor(otherColor, outputRow[x], dominantWeight);
				}
			}
		}

		return true;
	}

	float LinearToSrgb(float a_value)
	{
		const float linear = std::max(a_value, 0.0f);
		return linear <= 0.0031308f ?
		           linear * 12.92f :
		           1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
	}

	// Converts a linear display image to the same piecewise sRGB transfer used
	// by DXGI. Reinhard is opt-in only for the legacy desktop FP16 scene source;
	// OpenVR ColorSpace_Linear does not itself imply scene-referred HDR.
	bool EncodeLinearToSrgb(DirectX::ScratchImage& image, bool a_tonemapSceneHdr)
	{
		using namespace DirectX;
		DirectX::ScratchImage encoded;
		const HRESULT hr = TransformImage(
			image.GetImages(),
			image.GetImageCount(),
			image.GetMetadata(),
			[a_tonemapSceneHdr](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t /*y*/) {
				for (size_t i = 0; i < width; ++i) {
					XMFLOAT4 value{};
					XMStoreFloat4(&value, inPixels[i]);
					float rgb[3] = { value.x, value.y, value.z };
					for (float& channel : rgb) {
						channel = std::max(channel, 0.0f);
						if (a_tonemapSceneHdr) {
							channel /= 1.0f + channel;
						}
						channel = LinearToSrgb(channel);
					}
					outPixels[i] = XMVectorSet(rgb[0], rgb[1], rgb[2], value.w);
				}
			},
			encoded);
		if (FAILED(hr)) {
			return false;
		}
		image = std::move(encoded);
		return true;
	}

	const DirectX::Image* PrepareSdrImage(
		DirectX::ScratchImage& sourceImage,
		DirectX::ScratchImage& convertedImage,
		vr::EColorSpace a_colorSpace,
		bool a_tonemapSceneHdr)
	{
		const DXGI_FORMAT sourceFormat = sourceImage.GetMetadata().format;
		if (IsLinearCapture(a_colorSpace, sourceFormat)) {
			if (!EncodeLinearToSrgb(
					sourceImage,
					a_tonemapSceneHdr && sourceFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)) {
				return nullptr;
			}
		}

		if (SUCCEEDED(DirectX::Convert(
				sourceImage.GetImages(),
				sourceImage.GetImageCount(),
				sourceImage.GetMetadata(),
				DXGI_FORMAT_B8G8R8X8_UNORM,
				DirectX::TEX_FILTER_DEFAULT,
				0.0f,
				convertedImage))) {
			return convertedImage.GetImage(0, 0, 0);
		}

		return sourceImage.GetImage(0, 0, 0);
	}

	std::filesystem::path ResolveKnownCapturePath(
		REFKNOWNFOLDERID a_folderId,
		const std::filesystem::path& a_configuredPath)
	{
		if (a_configuredPath.is_absolute()) {
			return a_configuredPath;
		}

		const auto knownFolder = Util::GetWindowsKnownFolderPath(a_folderId);
		if (!knownFolder) {
			throw std::runtime_error("Windows capture folder is unavailable");
		}
		return *knownFolder / "Community Shaders" / a_configuredPath;
	}

	bool CopyFilePathToClipboardHDrop(const std::wstring& absolutePath)
	{
		if (absolutePath.empty()) {
			return false;
		}

		struct ClipboardDropFiles
		{
			DWORD pFiles = 0;
			POINT pt{};
			BOOL fNC = FALSE;
			BOOL fWide = TRUE;
		};

		const size_t pathChars = absolutePath.size();
		const size_t bytes = sizeof(ClipboardDropFiles) + (pathChars + 2) * sizeof(wchar_t);
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
		if (!hMem) {
			return false;
		}

		auto* drop = static_cast<ClipboardDropFiles*>(GlobalLock(hMem));
		if (!drop) {
			GlobalFree(hMem);
			return false;
		}

		drop->pFiles = sizeof(ClipboardDropFiles);

		auto* files = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(ClipboardDropFiles));
		memcpy(files, absolutePath.c_str(), (pathChars + 1) * sizeof(wchar_t));

		GlobalUnlock(hMem);

		for (int attempt = 0; attempt < 8; ++attempt) {
			if (attempt > 0) {
				Sleep(1 << (attempt - 1));
			}
			if (!OpenClipboard(nullptr)) {
				continue;
			}

			EmptyClipboard();
			const bool placed = SetClipboardData(CF_HDROP, hMem) != nullptr;
			CloseClipboard();
			if (placed) {
				return true;
			}
		}

		GlobalFree(hMem);
		return false;
	}

	void CopySavedPathToClipboard(bool enabled, const std::filesystem::path& path)
	{
		if (!enabled || path.empty()) {
			return;
		}

		const auto absolutePath = path.is_absolute() ? path : std::filesystem::absolute(path);
		std::error_code ec;
		if (!std::filesystem::exists(absolutePath, ec)) {
			logger::warn("Screenshot not found for clipboard: {}", absolutePath.string());
			return;
		}
		if (std::filesystem::file_size(absolutePath, ec) == 0) {
			logger::warn("Screenshot file is empty, skipping clipboard: {}", absolutePath.string());
			return;
		}

		if (!CopyFilePathToClipboardHDrop(absolutePath.wstring())) {
			logger::warn("Screenshot saved but clipboard copy failed.");
		}
	}

	bool SaveSdrScreenshot(
		DirectX::ScratchImage& image,
		const std::filesystem::path& outputPath,
		bool saveAsPng,
		vr::EColorSpace colorSpace,
		bool tonemapSceneHdr)
	{
		StripAlphaForBmp(image);
		DirectX::ScratchImage convertedImage;
		const DirectX::Image* saveImage = PrepareSdrImage(
			image,
			convertedImage,
			colorSpace,
			tonemapSceneHdr);
		if (!saveImage) {
			return false;
		}

		const GUID& codec = saveAsPng ?
		                        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG) :
		                        DirectX::GetWICCodec(DirectX::WIC_CODEC_BMP);
		const auto wicFlags = saveAsPng ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_NONE;
		return SUCCEEDED(DirectX::SaveToWICFile(
			*saveImage,
			wicFlags,
			codec,
			outputPath.c_str()));
	}

	// Resolves the slot's underlying texture, falling back to QueryInterface on
	// SRV/RTV when slot.texture is null (kFRAMEBUFFER on flat aliases the swap-
	// chain backbuffer that way). `holder` keeps the QI refcount alive across
	// the caller's use of the returned pointer.
	ID3D11Texture2D* ResolveSlotTexture(
		const RE::BSGraphics::RenderTargetData& slot,
		winrt::com_ptr<ID3D11Texture2D>& holder)
	{
		if (slot.texture) {
			return slot.texture;
		}
		auto resolveFromView = [&](ID3D11View* view) -> ID3D11Texture2D* {
			if (!view) {
				return nullptr;
			}
			winrt::com_ptr<ID3D11Resource> resource;
			view->GetResource(resource.put());
			if (!resource) {
				return nullptr;
			}
			if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), holder.put_void()))) {
				return nullptr;
			}
			return holder.get();
		};
		if (auto* tex = resolveFromView(slot.SRV)) {
			return tex;
		}
		return resolveFromView(slot.RTV);
	}

	// Picks the capture source for this branch:
	//   VR        -> kFRAMEBUFFER (SBS).
	//   flat      -> kFRAMEBUFFER (usually already tonemapped UNORM).
	// Dedicated HDR capture is intentionally omitted in 3.15-VR; if a
	// future source is FP16, the save path still tonemaps before SDR encoding.
	CaptureSource SelectCaptureSource(winrt::com_ptr<ID3D11Texture2D>& holder)
	{
		CaptureSource src;
		auto* renderer = globals::game::renderer;
		if (!renderer) {
			return src;
		}

		if (globals::game::isVR) {
			auto& slot = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
			src.texture = ResolveSlotTexture(slot, holder);
			src.srv = slot.SRV;
			src.description = "VR SBS framebuffer";
			return src;
		}

		auto& slot = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
		src.texture = ResolveSlotTexture(slot, holder);
		src.srv = slot.SRV;
		src.needsPreviewCache = true;
		src.description = "kFRAMEBUFFER";
		return src;
	}

	// True when our hotkey is the single PrintScreen key vanilla binds. Anything
	// else (different key, chord, modifier) means the user wants both ours and
	// vanilla independently.
	bool HotkeyCollidesWithVanilla()
	{
		const auto& combo = Menu::GetSingleton()->GetSettings().ScreenshotKey;
		return combo.size() == 1 &&
		       combo[0].GetDevice() == InputDeviceType::Keyboard &&
		       combo[0].GetKey() == VK_SNAPSHOT;
	}

	std::filesystem::path BuildScreenshotPath(const std::string& screenshotPath, bool usePng)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		char buf[80];
		const char* extension = usePng ? ".png" : ".bmp";
		snprintf(buf, sizeof(buf), "CS_%04d-%02d-%02d_%02d-%02d-%02d_%03d%s",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			st.wMilliseconds,
			extension);
		return ResolveKnownCapturePath(FOLDERID_Pictures, screenshotPath) / buf;
	}

	std::string TimestampUtc()
	{
		const auto now = std::chrono::system_clock::now();
		const auto value = std::chrono::system_clock::to_time_t(now);
		std::tm utc{};
		gmtime_s(&utc, &value);
		char timestamp[32]{};
		if (std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
			return {};
		}
		return timestamp;
	}

	std::string BuildSequenceDirectoryName(uint64_t a_sessionId)
	{
		SYSTEMTIME st{};
		GetLocalTime(&st);
		return std::format(
			"CS_sequence_{:04}-{:02}-{:02}_{:02}-{:02}-{:02}_{:016X}",
			st.wYear,
			st.wMonth,
			st.wDay,
			st.wHour,
			st.wMinute,
			st.wSecond,
			a_sessionId);
	}

}

ScreenshotFeature::~ScreenshotFeature()
{
	{
		std::lock_guard lock(captureStateMutex);
		ClearActiveCapture(activeCapture);
		capturePending.store(false, std::memory_order_release);
	}
	StopWorkerThread();
	RestoreReadbackContextProtectionIfIdle();
}

bool ScreenshotFeature::IsInMenu() const
{
	return true;
}

void ScreenshotFeature::DrawSettingsHeaderControls()
{
	bool runtimeEnabled = enabled.load(std::memory_order_acquire);
	if (ImGui::Checkbox("Enable Community Shaders Screenshots", &runtimeEnabled)) {
		SetEnabled(runtimeEnabled);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Controls the Community Shaders screenshot hotkey and manual capture button.");
		ImGui::Text("Vanilla Skyrim screenshots are unaffected.");
	}
}

void ScreenshotFeature::PostPostLoad()
{
	// Seed VR-specific presets here rather than in LoadSettings: Feature::Load
	// only dispatches to LoadSettings when the JSON already has a settings
	// block, so a fresh install would skip a seed placed there. Left first so
	// it's the initial selection (matches vanilla Skyrim VR's left-eye save).
	if (REL::Module::IsVR()) {
		subrect.SeedDefaultPresets({
			{ .name = "Left Eye", .uv = { 0.0f, 0.0f, 0.5f, 1.0f } },
			{ .name = "Right Eye", .uv = { 0.5f, 0.0f, 0.5f, 1.0f } },
			{ .name = "Both Eyes (Side-by-Side)", .uv = { 0.0f, 0.0f, 1.0f, 1.0f } },
		});
	}
}

void ScreenshotFeature::LoadSettings(json& a_json)
{
	const bool captureEnabled = a_json.value("Enabled", true);
	if (a_json.contains("ScreenshotPath"))
		screenshotPath = a_json["ScreenshotPath"];
	if (a_json.contains("FrameCapturePath"))
		frameCapturePath = a_json["FrameCapturePath"];
	if (a_json.contains("ApplyCropToScreenshot"))
		applyCropToScreenshot = a_json["ApplyCropToScreenshot"];
	if (a_json.contains("SdrUsePng"))
		sdrUsePng = a_json["SdrUsePng"];
	if (a_json.contains("CopyToClipboard"))
		copyToClipboard = a_json["CopyToClipboard"];
	vr::EVREye legacyFramedEye = vr::Eye_Left;
	if (a_json.contains("VRCaptureSource") && a_json["VRCaptureSource"].is_string()) {
		const auto captureSource = a_json["VRCaptureSource"].get<std::string>();
		if (captureSource == "DesktopMirror") {
			vrCaptureSource = VRCaptureSource::DesktopMirror;
		} else if (captureSource == "FramedStereo") {
			vrCaptureSource = VRCaptureSource::FramedStereo;
		} else if (captureSource == "FramedEye") {
			vrCaptureSource = VRCaptureSource::FramedEye;
		} else {
			vrCaptureSource = VRCaptureSource::HMDSubmission;
		}
	}
	if (a_json.contains("VRFramedEye") && a_json["VRFramedEye"].is_string()) {
		legacyFramedEye = a_json["VRFramedEye"].get<std::string>() == "Right" ?
		                      vr::Eye_Right :
		                      vr::Eye_Left;
	}
	if (a_json.contains("VRFramedView") && a_json["VRFramedView"].is_string()) {
		const auto framedView = a_json["VRFramedView"].get<std::string>();
		if (framedView == "Combined") {
			vrFramedView = VRFramedView::Combined;
		} else if (framedView == "Right") {
			vrFramedView = VRFramedView::Right;
		} else {
			vrFramedView = VRFramedView::Left;
		}
	} else if (vrCaptureSource == VRCaptureSource::FramedStereo) {
		vrFramedView = VRFramedView::Combined;
	} else {
		vrFramedView = legacyFramedEye == vr::Eye_Right ? VRFramedView::Right : VRFramedView::Left;
	}
	if (a_json.contains("VRFramedDominantEye") && a_json["VRFramedDominantEye"].is_string()) {
		vrFramedDominantEye = a_json["VRFramedDominantEye"].get<std::string>() == "Right" ?
		                          vr::Eye_Right :
		                          vr::Eye_Left;
	} else {
		vrFramedDominantEye = legacyFramedEye;
	}
	if (IsFramedCapture(vrCaptureSource)) {
		vrCaptureSource = vrFramedView == VRFramedView::Combined ?
		                      VRCaptureSource::FramedStereo :
		                      VRCaptureSource::FramedEye;
	}
	screenshotEye = ParseCaptureEye(a_json, "ScreenshotEye", screenshotEye);
	frameCaptureEye = ParseCaptureEye(a_json, "FrameCaptureEye", frameCaptureEye);

	subrect.LoadSettings(a_json);
	SetEnabled(captureEnabled);
}

void ScreenshotFeature::SaveSettings(json& a_json)
{
	a_json["Enabled"] = enabled.load(std::memory_order_acquire);
	a_json["ScreenshotPath"] = screenshotPath;
	a_json["FrameCapturePath"] = frameCapturePath;
	a_json["ApplyCropToScreenshot"] = applyCropToScreenshot;
	a_json["SdrUsePng"] = sdrUsePng;
	a_json["CopyToClipboard"] = copyToClipboard;
	a_json["ScreenshotEye"] = CaptureEyeName(screenshotEye);
	a_json["FrameCaptureEye"] = CaptureEyeName(frameCaptureEye);
	switch (vrCaptureSource) {
	case VRCaptureSource::DesktopMirror:
		a_json["VRCaptureSource"] = "DesktopMirror";
		break;
	case VRCaptureSource::FramedEye:
		a_json["VRCaptureSource"] = "FramedEye";
		break;
	case VRCaptureSource::FramedStereo:
		a_json["VRCaptureSource"] = "FramedStereo";
		break;
	case VRCaptureSource::HMDEye:
		a_json["VRCaptureSource"] = "HMDSubmission";
		break;
	case VRCaptureSource::HMDSubmission:
	default:
		a_json["VRCaptureSource"] = "HMDSubmission";
		break;
	}
	switch (vrFramedView) {
	case VRFramedView::Combined:
		a_json["VRFramedView"] = "Combined";
		break;
	case VRFramedView::Right:
		a_json["VRFramedView"] = "Right";
		break;
	case VRFramedView::Left:
	default:
		a_json["VRFramedView"] = "Left";
		break;
	}
	a_json["VRFramedDominantEye"] = vrFramedDominantEye == vr::Eye_Right ? "Right" : "Left";
	const auto legacyFramedEye = vrFramedView == VRFramedView::Combined ?
	                                 vrFramedDominantEye :
	                                 (vrFramedView == VRFramedView::Right ? vr::Eye_Right : vr::Eye_Left);
	a_json["VRFramedEye"] = legacyFramedEye == vr::Eye_Right ? "Right" : "Left";
	subrect.SaveSettings(a_json);
}

void ScreenshotFeature::DrawSettings()
{
	ImGui::TextWrapped("Capture and save run asynchronously without stalling the game.");
	ImGui::TextWrapped(
		"VR HMD captures use the exact accepted OpenVR eye submissions before compositor distortion. "
		"SDR and VR captures use the selected lossless format. Desktop FP16 scene sources are tonemapped "
		"(Reinhard) before SDR save; HDR PNG metadata is intentionally not included in this branch.");
	if (!IsRuntimeEnabled()) {
		ImGui::TextDisabled("Community Shaders screenshot capture is off. Output and crop settings can still be edited.");
	}

	if (globals::game::isVR) {
		ImGui::SeparatorText("VR Capture Source");
		int captureSource = IsFramedCapture(vrCaptureSource) ?
		                        1 :
		                        (vrCaptureSource == VRCaptureSource::DesktopMirror ? 2 : 0);
		ImGui::RadioButton(
			"HMD submission",
			&captureSource,
			0);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Captures the selected final accepted eye submission, or both eyes side-by-side.");
		}
		ImGui::SameLine();
		ImGui::RadioButton(
			"Framed view (2560 x 1440)",
			&captureSource,
			1);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Saves a left-eye, right-eye, or combined 16:9 view at 2560 x 1440.");
		}
		ImGui::SameLine();
		ImGui::RadioButton(
			"Desktop mirror",
			&captureSource,
			2);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Captures Skyrim's current desktop backbuffer without substituting HMD eye textures.");
		}
		if (captureSource == 0) {
			vrCaptureSource = VRCaptureSource::HMDSubmission;
		} else if (captureSource == 2) {
			vrCaptureSource = VRCaptureSource::DesktopMirror;
		} else if (!IsFramedCapture(vrCaptureSource)) {
			vrCaptureSource = vrFramedView == VRFramedView::Combined ?
			                      VRCaptureSource::FramedStereo :
			                      VRCaptureSource::FramedEye;
		}

		if (vrCaptureSource != VRCaptureSource::DesktopMirror) {
			int eye = static_cast<int>(screenshotEye);
			ImGui::TextUnformatted("Screenshot eye:");
			ImGui::SameLine();
			ImGui::RadioButton("Left##ScreenshotEye", &eye, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Right##ScreenshotEye", &eye, 1);
			ImGui::SameLine();
			ImGui::RadioButton("Both##ScreenshotEye", &eye, 2);
			screenshotEye = static_cast<CSPluginAPI::CaptureEye001>(eye);
			vrFramedView = eye == 2 ? VRFramedView::Combined : (eye == 1 ? VRFramedView::Right : VRFramedView::Left);
			if (IsFramedCapture(vrCaptureSource)) {
				vrCaptureSource = eye == 2 ? VRCaptureSource::FramedStereo : VRCaptureSource::FramedEye;
			}
			if (eye == 2 && IsFramedCapture(vrCaptureSource)) {
				int dominantEye = vrFramedDominantEye == vr::Eye_Right ? 1 : 0;
				ImGui::TextUnformatted("Dominant eye:");
				ImGui::SameLine();
				ImGui::RadioButton("Left##DominantFramedEye", &dominantEye, 0);
				ImGui::SameLine();
				ImGui::RadioButton("Right##DominantFramedEye", &dominantEye, 1);
				vrFramedDominantEye = dominantEye == 1 ? vr::Eye_Right : vr::Eye_Left;
			}
		}
	}

	ImGui::BeginDisabled(!IsRuntimeEnabled());
	if (ImGui::Button("Take Screenshot Now")) {
		(void)CSPluginAPI::g_captureInterface001.RequestScreenshot(screenshotEye);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	const bool usesFixedEyeFraming = globals::game::isVR && IsFramedCapture(vrCaptureSource);
	if (usesFixedEyeFraming) {
		bool fixedCropDisabled = false;
		ImGui::BeginDisabled();
		ImGui::Checkbox("Apply crop", &fixedCropDisabled);
		ImGui::EndDisabled();
	} else {
		ImGui::Checkbox("Apply crop", &applyCropToScreenshot);
	}

	ImGui::SeparatorText("Output");

	ImGui::Checkbox("Copy saved file to clipboard", &copyToClipboard);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Places the saved screenshot on the clipboard as a file.");
		ImGui::Text("Paste in Explorer or attach in chat apps.");
	}

	int sdrFormat = sdrUsePng ? 1 : 0;
	ImGui::RadioButton("BMP (lossless)", &sdrFormat, 0);
	ImGui::SameLine();
	ImGui::RadioButton("PNG (lossless)", &sdrFormat, 1);
	sdrUsePng = sdrFormat != 0;

	char buf[260];
	strncpy_s(buf, sizeof(buf), screenshotPath.c_str(), _TRUNCATE);
	ImGui::PushItemWidth(-FLT_MIN - 120.0f);  // leave room for Open button + label
	if (ImGui::InputText("##ScreenshotFolder", buf, sizeof(buf))) {
		screenshotPath = buf;
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	const bool canOpen = !screenshotPath.empty();
	ImGui::BeginDisabled(!canOpen);
	if (ImGui::Button("Open")) {
		try {
			const auto resolvedPath = ResolveScreenshotDirectory();
			std::error_code ec;
			std::filesystem::create_directories(resolvedPath, ec);
			ShellExecuteW(nullptr, L"open", resolvedPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		} catch (const std::exception& e) {
			logger::error("Could not open screenshot folder: {}", e.what());
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text("Folder");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Relative paths resolve beneath Pictures\\Community Shaders.");
		ImGui::Text("Absolute paths (e.g. D:\\Captures) save there directly.");
	}

	ImGui::SeparatorText("Lossless Frame Sequence");
	if (globals::game::isVR) {
		int eye = static_cast<int>(frameCaptureEye);
		ImGui::TextUnformatted("Eye:");
		ImGui::SameLine();
		ImGui::RadioButton("Left##FrameCaptureEye", &eye, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Right##FrameCaptureEye", &eye, 1);
		ImGui::SameLine();
		ImGui::RadioButton("Both##FrameCaptureEye", &eye, 2);
		frameCaptureEye = static_cast<CSPluginAPI::CaptureEye001>(eye);
	}
	CSPluginAPI::CaptureStatus001 captureStatus;
	(void)CSPluginAPI::g_captureInterface001.GetCaptureStatus(&captureStatus);
	ImGui::BeginDisabled(!IsRuntimeEnabled() || captureStatus.state == CSPluginAPI::CaptureState001::kFlushing);
	if (captureStatus.state == CSPluginAPI::CaptureState001::kCapturing) {
		if (ImGui::Button("Stop Frame Capture")) {
			(void)CSPluginAPI::g_captureInterface001.StopFrameSequence();
		}
	} else if (ImGui::Button("Start Frame Capture")) {
		(void)CSPluginAPI::g_captureInterface001.StartFrameSequence(frameCaptureEye);
	}
	ImGui::EndDisabled();
	if (captureStatus.state == CSPluginAPI::CaptureState001::kFlushing) {
		ImGui::SameLine();
		ImGui::TextDisabled("Flushing lossless frames...");
	}

	char framePathBuffer[260];
	strncpy_s(framePathBuffer, sizeof(framePathBuffer), frameCapturePath.c_str(), _TRUNCATE);
	ImGui::PushItemWidth(-FLT_MIN - 120.0f);
	if (ImGui::InputText("##FrameCaptureFolder", framePathBuffer, sizeof(framePathBuffer))) {
		frameCapturePath = framePathBuffer;
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	if (ImGui::Button("Open##FrameCaptureFolder")) {
		try {
			const auto resolvedPath = ResolveFrameCaptureDirectory();
			std::error_code ec;
			std::filesystem::create_directories(resolvedPath, ec);
			ShellExecuteW(nullptr, L"open", resolvedPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		} catch (const std::exception& e) {
			logger::error("Could not open frame capture folder: {}", e.what());
		}
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("Folder");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Relative paths resolve beneath Videos\\Community Shaders.");
		ImGui::Text("CSX saves lossless frames and a sequence manifest; companion mods own video encoding.");
	}

	auto& menuSettings = Menu::GetSingleton()->GetSettings();
	Util::InputComboWidget(
		"Hotkey",
		menuSettings.ScreenshotKey,
		Menu::GetSingleton()->settingScreenshotKey,
		"Change##ScreenshotFeature");

	if (IsRuntimeEnabled() && HotkeyCollidesWithVanilla()) {
		Util::Text::WrappedWarning(
			"This hotkey collides with vanilla PrintScreen; both saves will fire. "
			"Set bAllowScreenShot=0 in Skyrim.ini to suppress vanilla, or pick a different hotkey above.");
	}

	if (usesFixedEyeFraming) {
		ImGui::SeparatorText("Framing");
		if (vrCaptureSource == VRCaptureSource::FramedStereo) {
			ImGui::TextWrapped(
				"Combined aligns both submitted eyes in head-projection space. The dominant eye owns the shared view; "
				"the other eye fills the outer periphery through a narrow feathered join. Without scene depth, nearby "
				"objects can show a seam or duplication.");
		} else {
			ImGui::TextWrapped(
				"The selected submitted eye is center-cropped to 16:9 and resized to 2560 x 1440 without stretching.");
		}
		ImGui::TextWrapped(
			"The ordinary crop preset is not applied. A live eye submission is required, so framed views are "
			"unavailable during loading screens.");
		return;
	}

	ImGui::SeparatorText("Crop");

	// The desktop framebuffer remains available for interactive SBS crop setup.
	// HMD capture replaces its content with the accepted eye pair before applying
	// the same normalized crop.
	if (globals::game::isVR && vrCaptureSource == VRCaptureSource::HMDSubmission) {
		ImGui::TextDisabled("Crop preview uses the desktop SBS layout; saved pixels come from the HMD submission.");
	}
	winrt::com_ptr<ID3D11Texture2D> previewTextureKeepAlive;
	const auto src = SelectCaptureSource(previewTextureKeepAlive);

	ID3D11ShaderResourceView* previewView = src.srv;
	if (src.texture && (src.needsPreviewCache || !previewView)) {
		EnsurePreviewCache(src.texture);
		if (previewCacheSRV && previewCacheTexture) {
			globals::d3d::context->CopySubresourceRegion(
				previewCacheTexture.get(), 0, 0, 0, 0, src.texture, 0, nullptr);
			previewView = previewCacheSRV.get();
		}
	}

	subrect.DrawEditor(
		previewView,
		src.texture,
		1.0f,
		0.0f,
		Util::Subrect::OpaquePreviewBlendCallback);
}

void ScreenshotFeature::EnsurePreviewCache(ID3D11Texture2D* sourceTexture)
{
	if (!sourceTexture) {
		return;
	}
	D3D11_TEXTURE2D_DESC srcDesc{};
	sourceTexture->GetDesc(&srcDesc);

	// Reuse the cache when the source dimensions/format haven't changed.
	if (previewCacheTexture) {
		D3D11_TEXTURE2D_DESC cacheDesc{};
		previewCacheTexture->GetDesc(&cacheDesc);
		if (cacheDesc.Width == srcDesc.Width &&
			cacheDesc.Height == srcDesc.Height &&
			cacheDesc.Format == srcDesc.Format) {
			return;
		}
		previewCacheSRV = nullptr;
		previewCacheTexture = nullptr;
	}

	// SRV-readable copy. Match source format for CopySubresourceRegion compatibility.
	D3D11_TEXTURE2D_DESC cacheDesc = srcDesc;
	cacheDesc.MipLevels = 1;
	cacheDesc.ArraySize = 1;
	cacheDesc.SampleDesc.Count = 1;
	cacheDesc.SampleDesc.Quality = 0;
	cacheDesc.Usage = D3D11_USAGE_DEFAULT;
	cacheDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	cacheDesc.CPUAccessFlags = 0;
	cacheDesc.MiscFlags = 0;

	if (FAILED(globals::d3d::device->CreateTexture2D(&cacheDesc, nullptr, previewCacheTexture.put()))) {
		previewCacheTexture = nullptr;
		return;
	}
	Util::SetResourceName(previewCacheTexture.get(), "Screenshot::PreviewCache");
	if (FAILED(globals::d3d::device->CreateShaderResourceView(
			previewCacheTexture.get(), nullptr, previewCacheSRV.put()))) {
		previewCacheSRV = nullptr;
		previewCacheTexture = nullptr;
		return;
	}
	Util::SetResourceName(previewCacheSRV.get(), "Screenshot::PreviewCache SRV");
}

ScreenshotFeature::CaptureOptions ScreenshotFeature::SnapshotCaptureOptions(CSPluginAPI::CaptureEye001 a_eye) const
{
	return {
		.screenshotPath = screenshotPath,
		.cropUV = subrect.GetUV(),
		.applyCrop = applyCropToScreenshot,
		.saveAsPng = sdrUsePng,
		.copyToClipboard = copyToClipboard,
		.framedEye = a_eye == CSPluginAPI::CaptureEye001::kRight ?
		                 vr::Eye_Right :
		                 (a_eye == CSPluginAPI::CaptureEye001::kBoth ? vrFramedDominantEye : vr::Eye_Left),
		.eye = a_eye
	};
}

bool ScreenshotFeature::SnapshotStereoGeometry(CaptureOptions& a_options) const
{
	a_options.stereoProjectionValid = false;
	a_options.hiddenAreaMeshes = {};
	auto* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		return false;
	}

	for (std::size_t eyeIndex = 0; eyeIndex < a_options.eyeProjectionTangents.size(); ++eyeIndex) {
		const auto eye = eyeIndex == 1 ? vr::Eye_Right : vr::Eye_Left;
		float left = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
		float top = 0.0f;
		// OpenVR's third/fourth parameter names are historically reversed;
		// their returned values are the bottom and top tangents respectively.
		openvr->vrSystem->GetProjectionRaw(eye, &left, &right, &bottom, &top);
		a_options.eyeProjectionTangents[eyeIndex] = { left, right, bottom, top };
		a_options.eyeToHeadTransforms[eyeIndex] = openvr->vrSystem->GetEyeToHeadTransform(eye);
		if (!IsValidProjectionTangents(a_options.eyeProjectionTangents[eyeIndex]) ||
			!IsValidEyeRotation(a_options.eyeToHeadTransforms[eyeIndex])) {
			return false;
		}

		const auto hiddenAreaMesh = openvr->vrSystem->GetHiddenAreaMesh(
			eye,
			vr::k_eHiddenAreaMesh_Standard);
		if (hiddenAreaMesh.unTriangleCount > kMaxHiddenAreaTriangles ||
			(hiddenAreaMesh.unTriangleCount != 0 && !hiddenAreaMesh.pVertexData)) {
			return false;
		}
		const std::size_t hiddenVertexCount = static_cast<std::size_t>(hiddenAreaMesh.unTriangleCount) * 3;
		if (hiddenVertexCount != 0) {
			a_options.hiddenAreaMeshes[eyeIndex].assign(
				hiddenAreaMesh.pVertexData,
				hiddenAreaMesh.pVertexData + hiddenVertexCount);
			for (const auto& vertex : a_options.hiddenAreaMeshes[eyeIndex]) {
				if (!std::isfinite(vertex.v[0]) || !std::isfinite(vertex.v[1])) {
					return false;
				}
			}
		}
	}

	a_options.stereoProjectionValid = true;
	return true;
}

void ScreenshotFeature::ClearActiveCapture(ActiveCapture& a_capture)
{
	const bool ownsQueueSlot = std::exchange(a_capture.ownsQueueSlot, false);
	a_capture = {};
	if (ownsQueueSlot) {
		ReleaseScreenshotSlot();
	}
}

void ScreenshotFeature::FallBackToDesktopCapture(ActiveCapture& a_capture, std::string_view a_reason)
{
	logger::warn("HMD screenshot capture is falling back to the desktop mirror: {}", a_reason);
	a_capture.source = VRCaptureSource::DesktopMirror;
	a_capture.compositorCycleToken = 0;
	a_capture.eyeMask = 0;
	a_capture.eyes = {};
	a_capture.presentsWaited = 0;
}

void ScreenshotFeature::RequestCapture()
{
	(void)RequestScreenshot(screenshotEye);
}

CSPluginAPI::CaptureResult001 ScreenshotFeature::RequestScreenshot(CSPluginAPI::CaptureEye001 a_eye)
{
	if (!IsValidCaptureEye(a_eye)) {
		return CSPluginAPI::CaptureResult001::kInvalidArgument;
	}
	return RequestCaptureInternal(SnapshotCaptureOptions(a_eye), true);
}

CSPluginAPI::CaptureResult001 ScreenshotFeature::RequestCaptureInternal(CaptureOptions a_options, bool a_notifyBusy)
{
	using Result = CSPluginAPI::CaptureResult001;
	if (!IsRuntimeEnabled()) {
		return Result::kFeatureDisabled;
	}

	auto requestedSource = globals::game::isVR ?
	                           vrCaptureSource :
	                           VRCaptureSource::DesktopMirror;
	if (globals::game::isVR && a_options.sequenceSessionId != 0) {
		// Frame sequences preserve submitted eye planes. Desktop/framed composition
		// remains a still-image presentation choice.
		requestedSource = VRCaptureSource::HMDSubmission;
	}
	if (globals::game::isVR) {
		if (requestedSource == VRCaptureSource::HMDSubmission &&
			a_options.eye != CSPluginAPI::CaptureEye001::kBoth) {
			requestedSource = VRCaptureSource::HMDEye;
		}
		if (IsFramedCapture(requestedSource)) {
			requestedSource = a_options.eye == CSPluginAPI::CaptureEye001::kBoth ?
			                      VRCaptureSource::FramedStereo :
			                      VRCaptureSource::FramedEye;
		}
	}
	if (requestedSource == VRCaptureSource::FramedStereo && !SnapshotStereoGeometry(a_options)) {
		logger::warn("Combined-eye projection data is unavailable; this screenshot will use the dominant eye only.");
		requestedSource = VRCaptureSource::FramedEye;
	}

	std::lock_guard lock(captureStateMutex);
	if (!IsRuntimeEnabled()) {
		return Result::kFeatureDisabled;
	}
	if (activeCapture.pending) {
		if (a_notifyBusy) {
			ShowInGameNotification("Screenshot busy - try again shortly");
		}
		return Result::kBusy;
	}
	if (!TryReserveScreenshotSlot()) {
		logger::debug("Capture writer is busy; rejecting the newest capture request.");
		if (a_notifyBusy) {
			ShowInGameNotification("Screenshot busy - try again shortly");
		}
		return Result::kBusy;
	}
	activeCapture.pending = true;
	activeCapture.ownsQueueSlot = true;
	activeCapture.options = std::move(a_options);
	activeCapture.source = requestedSource;

	if (globals::game::isVR && globals::state && globals::state->isLoadingMenuOpen) {
		if (activeCapture.source == VRCaptureSource::HMDSubmission ||
			activeCapture.source == VRCaptureSource::HMDEye) {
			if (activeCapture.options.sequenceSessionId != 0) {
				ClearActiveCapture(activeCapture);
				capturePending.store(false, std::memory_order_release);
				return Result::kOutputUnavailable;
			}
			activeCapture.source = VRCaptureSource::DesktopMirror;
		} else if (IsFramedCapture(activeCapture.source)) {
			logger::warn("Framed-view screenshot capture is unavailable during a loading screen.");
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			if (a_notifyBusy) {
				ShowInGameNotification("Framed-view screenshot unavailable during loading");
			}
			return Result::kOutputUnavailable;
		}
	}

	capturePending.store(true, std::memory_order_release);

	logger::debug(
		"Screenshot requested from {}",
		DescribeCaptureSource(activeCapture.source));
	return Result::kSuccess;
}

CSPluginAPI::CaptureResult001 ScreenshotFeature::StartFrameSequence(CSPluginAPI::CaptureEye001 a_eye)
{
	using Result = CSPluginAPI::CaptureResult001;
	if (!IsValidCaptureEye(a_eye)) {
		return Result::kInvalidArgument;
	}
	if (!IsRuntimeEnabled()) {
		return Result::kFeatureDisabled;
	}
	{
		std::lock_guard lock(frameSequenceMutex);
		if (frameSequence.state == CSPluginAPI::CaptureState001::kCapturing ||
			frameSequence.state == CSPluginAPI::CaptureState001::kFlushing) {
			return Result::kBusy;
		}
	}

	std::filesystem::path directory;
	try {
		directory = ResolveFrameCaptureDirectory();
	} catch (const std::exception& e) {
		logger::error("Frame capture output is unavailable: {}", e.what());
		return Result::kOutputUnavailable;
	}

	static std::atomic_uint64_t nextSessionId{ 1 };
	const uint64_t clockPart = static_cast<uint64_t>(
		std::chrono::steady_clock::now().time_since_epoch().count());
	const uint64_t sessionId = clockPart ^ nextSessionId.fetch_add(1, std::memory_order_relaxed);
	directory /= BuildSequenceDirectoryName(sessionId);

	std::error_code ec;
	std::filesystem::create_directories(directory, ec);
	if (!ec && globals::game::isVR) {
		if (a_eye == CSPluginAPI::CaptureEye001::kLeft || a_eye == CSPluginAPI::CaptureEye001::kBoth) {
			std::filesystem::create_directories(directory / "left", ec);
		}
		if (!ec && (a_eye == CSPluginAPI::CaptureEye001::kRight || a_eye == CSPluginAPI::CaptureEye001::kBoth)) {
			std::filesystem::create_directories(directory / "right", ec);
		}
	} else if (!ec) {
		std::filesystem::create_directories(directory / "frames", ec);
	}
	if (ec) {
		logger::error("Could not create frame capture directory {}: {}", directory.string(), ec.message());
		return Result::kOutputUnavailable;
	}

	FrameSequence snapshot;
	{
		std::lock_guard lock(frameSequenceMutex);
		// Recheck after filesystem setup in case another caller started a sequence.
		if (frameSequence.state == CSPluginAPI::CaptureState001::kCapturing ||
			frameSequence.state == CSPluginAPI::CaptureState001::kFlushing) {
			std::error_code removeError;
			std::filesystem::remove_all(directory, removeError);
			return Result::kBusy;
		}
		frameSequence = {};
		frameSequence.state = CSPluginAPI::CaptureState001::kCapturing;
		frameSequence.eye = a_eye;
		frameSequence.sessionId = sessionId;
		frameSequence.directory = directory;
		frameSequence.startedAt = std::chrono::steady_clock::now();
		frameSequence.startedUtc = TimestampUtc();
		snapshot = frameSequence;
	}

	if (!WriteSequenceManifest(snapshot, false)) {
		std::lock_guard lock(frameSequenceMutex);
		frameSequence.state = CSPluginAPI::CaptureState001::kFailed;
		return Result::kOutputUnavailable;
	}
	logger::info("Started lossless frame sequence {} in {}", sessionId, directory.string());
	return Result::kSuccess;
}

CSPluginAPI::CaptureResult001 ScreenshotFeature::StopFrameSequence()
{
	using Result = CSPluginAPI::CaptureResult001;
	{
		std::lock_guard lock(frameSequenceMutex);
		if (frameSequence.state != CSPluginAPI::CaptureState001::kCapturing) {
			return Result::kNoActiveSequence;
		}
		frameSequence.stopRequested = true;
		frameSequence.state = CSPluginAPI::CaptureState001::kFlushing;
	}
	FinalizeSequenceIfReady();
	return Result::kSuccess;
}

CSPluginAPI::CaptureResult001 ScreenshotFeature::GetCaptureStatus(CSPluginAPI::CaptureStatus001* a_status) const
{
	using Result = CSPluginAPI::CaptureResult001;
	if (!a_status || a_status->structSize < sizeof(CSPluginAPI::CaptureStatus001)) {
		return Result::kInvalidArgument;
	}
	std::lock_guard lock(frameSequenceMutex);
	a_status->state = frameSequence.state;
	a_status->eye = frameSequence.eye;
	a_status->sessionId = frameSequence.sessionId;
	a_status->framesScheduled = frameSequence.framesScheduled;
	a_status->framesWritten = frameSequence.framesWritten;
	a_status->framesDropped = frameSequence.framesDropped;
	return Result::kSuccess;
}

CSPluginAPI::CaptureResult001 ScreenshotFeature::CopySequencePath(
	uint64_t a_sessionId,
	char* a_buffer,
	uint32_t a_bufferBytes,
	uint32_t* a_requiredBytes) const
{
	using Result = CSPluginAPI::CaptureResult001;
	if (!a_requiredBytes) {
		return Result::kInvalidArgument;
	}
	std::string path;
	{
		std::lock_guard lock(frameSequenceMutex);
		if (a_sessionId == 0 || a_sessionId != frameSequence.sessionId || frameSequence.directory.empty()) {
			return Result::kOutputUnavailable;
		}
		const auto utf8 = frameSequence.directory.generic_u8string();
		path.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
	}
	if (path.size() >= std::numeric_limits<uint32_t>::max()) {
		return Result::kInternalError;
	}
	*a_requiredBytes = static_cast<uint32_t>(path.size() + 1);
	if (!a_buffer) {
		return Result::kSuccess;
	}
	if (a_bufferBytes < *a_requiredBytes) {
		return Result::kBufferTooSmall;
	}
	memcpy(a_buffer, path.c_str(), *a_requiredBytes);
	return Result::kSuccess;
}

bool ScreenshotFeature::IsFrameSequenceCapturing() const
{
	std::lock_guard lock(frameSequenceMutex);
	return frameSequence.state == CSPluginAPI::CaptureState001::kCapturing;
}

std::filesystem::path ScreenshotFeature::ResolveScreenshotDirectory() const
{
	return ResolveKnownCapturePath(FOLDERID_Pictures, screenshotPath);
}

std::filesystem::path ScreenshotFeature::ResolveFrameCaptureDirectory() const
{
	return ResolveKnownCapturePath(FOLDERID_Videos, frameCapturePath);
}

void ScreenshotFeature::ScheduleNextSequenceFrame()
{
	if (HasPendingCapture()) {
		return;
	}

	CaptureOptions options;
	uint64_t sessionId = 0;
	uint64_t frameIndex = 0;
	{
		std::lock_guard lock(frameSequenceMutex);
		if (frameSequence.state != CSPluginAPI::CaptureState001::kCapturing || frameSequence.stopRequested) {
			return;
		}

		sessionId = frameSequence.sessionId;
		frameIndex = frameSequence.nextFrameIndex++;
		options = SnapshotCaptureOptions(frameSequence.eye);
		options.applyCrop = false;
		options.copyToClipboard = false;
		options.sequenceSessionId = sessionId;
		options.sequenceFrameIndex = frameIndex;
		options.sequenceTimestampUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - frameSequence.startedAt).count());

		const char* extension = options.saveAsPng ? ".png" : ".bmp";
		const auto fileName = std::format("frame_{:09}{}", frameIndex, extension);
		if (!globals::game::isVR) {
			options.sequenceOutputPaths[0] = frameSequence.directory / "frames" / fileName;
			options.sequenceOutputCount = 1;
		} else if (frameSequence.eye == CSPluginAPI::CaptureEye001::kBoth) {
			options.sequenceOutputPaths[0] = frameSequence.directory / "left" / fileName;
			options.sequenceOutputPaths[1] = frameSequence.directory / "right" / fileName;
			options.sequenceOutputCount = 2;
		} else {
			const char* eyeDirectory = frameSequence.eye == CSPluginAPI::CaptureEye001::kRight ? "right" : "left";
			options.sequenceOutputPaths[0] = frameSequence.directory / eyeDirectory / fileName;
			options.sequenceOutputCount = 1;
		}

		++frameSequence.framesScheduled;
		++frameSequence.inFlight;
	}

	const auto result = RequestCaptureInternal(std::move(options), false);
	if (result != CSPluginAPI::CaptureResult001::kSuccess) {
		std::array<std::filesystem::path, 2> emptyPaths{};
		CompleteSequenceFrame(
			sessionId,
			frameIndex,
			0,
			emptyPaths,
			0,
			false,
			result == CSPluginAPI::CaptureResult001::kBusy ? "writer_backpressure" : "capture_unavailable");
	}
}

void ScreenshotFeature::CompleteSequenceFrame(
	uint64_t a_sessionId,
	uint64_t a_frameIndex,
	uint64_t a_timestampUs,
	const std::array<std::filesystem::path, 2>& a_paths,
	uint32_t a_pathCount,
	bool a_written,
	std::string a_error)
{
	try {
		FrameSequence partialSnapshot;
		bool writePartial = false;
		{
			std::lock_guard lock(frameSequenceMutex);
			if (a_sessionId == 0 || frameSequence.sessionId != a_sessionId) {
				return;
			}
			if (frameSequence.inFlight > 0) {
				--frameSequence.inFlight;
			}

			SequenceFrameRecord record;
			record.index = a_frameIndex;
			record.timestampUs = a_timestampUs;
			record.paths = a_paths;
			record.pathCount = std::min<uint32_t>(a_pathCount, static_cast<uint32_t>(record.paths.size()));
			record.written = a_written;
			record.error = std::move(a_error);
			frameSequence.frames.push_back(std::move(record));
			if (a_written) {
				++frameSequence.framesWritten;
			} else {
				++frameSequence.framesDropped;
			}
			writePartial = frameSequence.frames.size() % 60 == 0;
			if (writePartial) {
				partialSnapshot = frameSequence;
			}
		}

		if (writePartial) {
			(void)WriteSequenceManifest(partialSnapshot, false);
		}
		FinalizeSequenceIfReady();
	} catch (const std::exception& e) {
		logger::error("Frame sequence completion failed: {}", e.what());
		std::lock_guard lock(frameSequenceMutex);
		if (frameSequence.sessionId == a_sessionId) {
			frameSequence.state = CSPluginAPI::CaptureState001::kFailed;
		}
	} catch (...) {
		logger::error("Frame sequence completion failed with an unknown exception.");
		std::lock_guard lock(frameSequenceMutex);
		if (frameSequence.sessionId == a_sessionId) {
			frameSequence.state = CSPluginAPI::CaptureState001::kFailed;
		}
	}
}

void ScreenshotFeature::FinalizeSequenceIfReady()
{
	FrameSequence snapshot;
	{
		std::lock_guard lock(frameSequenceMutex);
		if (frameSequence.state != CSPluginAPI::CaptureState001::kFlushing || frameSequence.inFlight != 0) {
			return;
		}
		frameSequence.state = CSPluginAPI::CaptureState001::kComplete;
		snapshot = frameSequence;
	}

	if (!WriteSequenceManifest(snapshot, true)) {
		std::lock_guard lock(frameSequenceMutex);
		if (frameSequence.sessionId == snapshot.sessionId) {
			frameSequence.state = CSPluginAPI::CaptureState001::kFailed;
		}
		return;
	}
	logger::info(
		"Completed lossless frame sequence {} (written={}, dropped={})",
		snapshot.sessionId,
		snapshot.framesWritten,
		snapshot.framesDropped);
}

bool ScreenshotFeature::WriteSequenceManifest(const FrameSequence& a_sequence, bool a_final) const
{
	try {
		json frames = json::array();
		for (const auto& frame : a_sequence.frames) {
			json paths = json::array();
			for (uint32_t index = 0; index < frame.pathCount; ++index) {
				paths.push_back(frame.paths[index].lexically_relative(a_sequence.directory).generic_string());
			}
			frames.push_back({
				{ "index", frame.index },
				{ "timestampUs", frame.timestampUs },
				{ "written", frame.written },
				{ "paths", std::move(paths) },
				{ "error", frame.error.empty() ? json(nullptr) : json(frame.error) },
			});
		}

		const json manifest = {
			{ "schema", "csx.frame-sequence/1" },
			{ "sessionId", a_sequence.sessionId },
			{ "state", a_final ? "complete" : "capturing" },
			{ "eye", CaptureEyeName(a_sequence.eye) },
			{ "audio", false },
			{ "startedUtc", a_sequence.startedUtc },
			{ "updatedUtc", TimestampUtc() },
			{ "counts", {
				{ "scheduled", a_sequence.framesScheduled },
				{ "written", a_sequence.framesWritten },
				{ "dropped", a_sequence.framesDropped },
			} },
			{ "frames", std::move(frames) },
		};

		const auto target = a_sequence.directory / (a_final ? "sequence.json" : "sequence.json.partial");
		const auto temporary = a_sequence.directory / "sequence.json.tmp";
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			if (!stream) {
				return false;
			}
			stream << manifest.dump(2) << '\n';
			if (!stream) {
				return false;
			}
		}
		if (!MoveFileExW(
				temporary.c_str(),
				target.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			logger::error("Could not commit frame sequence manifest {} ({})", target.string(), GetLastError());
			return false;
		}
		if (a_final) {
			std::error_code ec;
			std::filesystem::remove(a_sequence.directory / "sequence.json.partial", ec);
		}
		return true;
	} catch (const std::exception& e) {
		logger::error("Could not write frame sequence manifest: {}", e.what());
		return false;
	} catch (...) {
		logger::error("Could not write frame sequence manifest.");
		return false;
	}
}

void ScreenshotFeature::SetEnabled(bool a_enabled)
{
	bool wasEnabled = false;
	bool cancelledPendingCapture = false;
	uint64_t cancelledSequenceSessionId = 0;
	uint64_t cancelledSequenceFrameIndex = 0;
	uint64_t cancelledSequenceTimestampUs = 0;
	std::array<std::filesystem::path, 2> cancelledSequencePaths{};
	uint32_t cancelledSequencePathCount = 0;
	{
		std::lock_guard lock(captureStateMutex);
		wasEnabled = enabled.exchange(a_enabled, std::memory_order_acq_rel);
		if (!a_enabled) {
			// Close the Submit fast path before releasing partial textures and its
			// reserved encoder slot. Completed or queued encoder work remains committed.
			capturePending.store(false, std::memory_order_release);
			cancelledPendingCapture = activeCapture.pending;
			cancelledSequenceSessionId = activeCapture.options.sequenceSessionId;
			cancelledSequenceFrameIndex = activeCapture.options.sequenceFrameIndex;
			cancelledSequenceTimestampUs = activeCapture.options.sequenceTimestampUs;
			cancelledSequencePaths = activeCapture.options.sequenceOutputPaths;
			cancelledSequencePathCount = activeCapture.options.sequenceOutputCount;
			ClearActiveCapture(activeCapture);
		}
	}
	if (!a_enabled) {
		{
			std::lock_guard lock(frameSequenceMutex);
			if (frameSequence.state == CSPluginAPI::CaptureState001::kCapturing) {
				frameSequence.stopRequested = true;
				frameSequence.state = CSPluginAPI::CaptureState001::kFlushing;
			}
		}
		if (cancelledSequenceSessionId != 0) {
			CompleteSequenceFrame(
				cancelledSequenceSessionId,
				cancelledSequenceFrameIndex,
				cancelledSequenceTimestampUs,
				cancelledSequencePaths,
				cancelledSequencePathCount,
				false,
				"feature_disabled");
		} else {
			FinalizeSequenceIfReady();
		}
	}

	if (wasEnabled != a_enabled) {
		logger::debug("Community Shaders screenshot capture {}", a_enabled ? "enabled" : "disabled");
	}
	if (cancelledPendingCapture) {
		logger::debug("Cancelled the pending screenshot capture after the feature was disabled");
	}
}

bool ScreenshotFeature::TryReserveScreenshotSlot()
{
	std::lock_guard queueLock(screenshotQueueMutex);
	if (outstandingScreenshotCount >= kMaxOutstandingScreenshots) {
		return false;
	}
	++outstandingScreenshotCount;
	return true;
}

void ScreenshotFeature::ReleaseScreenshotSlot()
{
	std::lock_guard queueLock(screenshotQueueMutex);
	if (outstandingScreenshotCount == 0) {
		logger::error("Screenshot queue-slot accounting underflow was prevented.");
		return;
	}
	--outstandingScreenshotCount;
}

bool ScreenshotFeature::EnsureReadbackContextProtection(ID3D11DeviceContext* a_context)
{
	winrt::com_ptr<REX::W32::ID3D11Multithread> multithread;
	if (!a_context || FAILED(a_context->QueryInterface(multithread.put()))) {
		return false;
	}

	std::lock_guard queueLock(screenshotQueueMutex);
	const auto existing = std::find_if(
		readbackContextProtections.begin(),
		readbackContextProtections.end(),
		[a_context](const ReadbackContextProtection& protection) {
			return protection.context.get() == a_context;
		});
	if (existing != readbackContextProtections.end()) {
		multithread->SetMultithreadProtected(TRUE);
		return true;
	}

	try {
		ReadbackContextProtection protection;
		protection.context.copy_from(a_context);
		readbackContextProtections.push_back(std::move(protection));
	} catch (const std::exception& e) {
		logger::error("Failed to track screenshot readback protection: {}", e.what());
		return false;
	} catch (...) {
		logger::error("Failed to track screenshot readback protection.");
		return false;
	}

	const BOOL wasProtected = multithread->SetMultithreadProtected(TRUE);
	readbackContextProtections.back().restoreToUnprotected = wasProtected == FALSE;
	readbackProtectionCleanupPending.store(true, std::memory_order_release);
	return true;
}

void ScreenshotFeature::RestoreReadbackContextProtectionIfIdle()
{
	if (!readbackProtectionCleanupPending.load(std::memory_order_acquire)) {
		return;
	}

	std::lock_guard queueLock(screenshotQueueMutex);
	if (outstandingScreenshotCount != 0) {
		return;
	}

	for (const auto& protection : readbackContextProtections) {
		if (!protection.restoreToUnprotected || !protection.context) {
			continue;
		}
		winrt::com_ptr<REX::W32::ID3D11Multithread> multithread;
		if (SUCCEEDED(protection.context->QueryInterface(multithread.put()))) {
			multithread->SetMultithreadProtected(FALSE);
		}
	}
	readbackContextProtections.clear();
	readbackProtectionCleanupPending.store(false, std::memory_order_release);
}

bool ScreenshotFeature::QueueScreenshot(PendingScreenshot&& screenshot)
{
	if (!screenshot.ownsQueueSlot) {
		logger::error("Screenshot was queued without a reserved encoder slot.");
		return false;
	}

	std::lock_guard lifecycleLock(screenshotWorkerLifecycleMutex);

	if (!screenshotWorker.joinable()) {
		{
			std::lock_guard queueLock(screenshotQueueMutex);
			screenshotWorkerRunning = true;
		}
		try {
			screenshotWorker = std::thread(&ScreenshotFeature::ScreenshotWorkerLoop, this);
		} catch (const std::exception& e) {
			{
				std::lock_guard queueLock(screenshotQueueMutex);
				screenshotWorkerRunning = false;
			}
			logger::error("Failed to start screenshot worker: {}", e.what());
			screenshot = {};
			ReleaseScreenshotSlot();
			return false;
		} catch (...) {
			{
				std::lock_guard queueLock(screenshotQueueMutex);
				screenshotWorkerRunning = false;
			}
			logger::error("Failed to start screenshot worker.");
			screenshot = {};
			ReleaseScreenshotSlot();
			return false;
		}
	}

	{
		std::lock_guard queueLock(screenshotQueueMutex);
		try {
			screenshotQueue.push(std::move(screenshot));
		} catch (const std::exception& e) {
			logger::error("Failed to enqueue screenshot: {}", e.what());
			screenshot = {};
			if (outstandingScreenshotCount > 0) {
				--outstandingScreenshotCount;
			}
			return false;
		} catch (...) {
			logger::error("Failed to enqueue screenshot.");
			screenshot = {};
			if (outstandingScreenshotCount > 0) {
				--outstandingScreenshotCount;
			}
			return false;
		}
	}
	screenshotQueueCV.notify_one();
	return true;
}

void ScreenshotFeature::StopWorkerThread()
{
	std::lock_guard lifecycleLock(screenshotWorkerLifecycleMutex);
	{
		std::lock_guard queueLock(screenshotQueueMutex);
		screenshotWorkerRunning = false;
	}
	screenshotQueueCV.notify_all();

	if (screenshotWorker.joinable()) {
		screenshotWorker.join();
	}
}

void ScreenshotFeature::ScreenshotWorkerLoop()
{
	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool uninitializeCom = SUCCEEDED(comResult);
	if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
		logger::warn("Screenshot worker COM initialization failed: 0x{:08X}", static_cast<uint32_t>(comResult));
	}
	while (true) {
		bool ownsQueueSlot = false;
		const SKSE::stl::scope_exit finishScreenshot([this, &ownsQueueSlot]() noexcept {
			if (ownsQueueSlot) {
				ReleaseScreenshotSlot();
			}
		});
		PendingScreenshot screenshot;
		{
			std::unique_lock queueLock(screenshotQueueMutex);
			screenshotQueueCV.wait(queueLock, [this] {
				return !screenshotQueue.empty() || !screenshotWorkerRunning;
			});

			if (!screenshotWorkerRunning && screenshotQueue.empty()) {
				break;
			}

			screenshot = std::move(screenshotQueue.front());
			screenshotQueue.pop();
		}
		ownsQueueSlot = screenshot.ownsQueueSlot;
		bool operationSucceeded = false;
		std::string operationError = "frame_processing_failed";
		const SKSE::stl::scope_exit reportTerminal([this, &screenshot, &operationSucceeded, &operationError]() noexcept {
			if (screenshot.sequenceSessionId != 0) {
				CompleteSequenceFrame(
					screenshot.sequenceSessionId,
					screenshot.sequenceFrameIndex,
					screenshot.sequenceTimestampUs,
					screenshot.sequenceOutputPaths,
					screenshot.sequenceOutputCount,
					operationSucceeded,
					operationSucceeded ? std::string{} : operationError);
			} else if (!operationSucceeded) {
				ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
			}
		});
		auto reportFailure = [&operationError](std::string_view message) {
			operationError = message;
			logger::error("{}", message);
		};

		try {
			if (screenshot.planeCount == 0 || screenshot.planeCount > screenshot.planes.size()) {
				reportFailure("Screenshot contained no valid image planes.");
				continue;
			}

			std::array<DirectX::ScratchImage, 2> mappedPlanes;
			std::array<DirectX::ScratchImage, 2> orientedPlanes;
			std::array<const DirectX::Image*, 2> planeImages{};
			bool planeFailure = false;
			for (uint32_t index = 0; index < screenshot.planeCount; ++index) {
				const auto& plane = screenshot.planes[index];
				if (!PopulateScratchImageFromStagingTexture(
						plane.immediateContext.get(),
						plane.stagingTexture.get(),
						plane.format,
						plane.width,
						plane.height,
						mappedPlanes[index])) {
					planeFailure = true;
					break;
				}

				const DirectX::Image* image = mappedPlanes[index].GetImage(0, 0, 0);
				if (!image) {
					planeFailure = true;
					break;
				}

				uint32_t flipFlags = DirectX::TEX_FR_ROTATE0;
				if (plane.flipHorizontal) {
					flipFlags |= DirectX::TEX_FR_FLIP_HORIZONTAL;
				}
				if (plane.flipVertical) {
					flipFlags |= DirectX::TEX_FR_FLIP_VERTICAL;
				}
				if (flipFlags != DirectX::TEX_FR_ROTATE0) {
					if (FAILED(DirectX::FlipRotate(
							*image,
							static_cast<DirectX::TEX_FR_FLAGS>(flipFlags),
							orientedPlanes[index]))) {
						planeFailure = true;
						break;
					}
					image = orientedPlanes[index].GetImage(0, 0, 0);
				}
				if (!image) {
					planeFailure = true;
					break;
				}
				planeImages[index] = image;
			}

			if (planeFailure) {
				reportFailure("Failed to map or orient screenshot image planes.");
				continue;
			}

			if (screenshot.sequenceOutputCount != 0) {
				if (screenshot.sequenceOutputCount > screenshot.planeCount ||
					screenshot.sequenceOutputCount > screenshot.sequenceOutputPaths.size()) {
					reportFailure("Frame sequence output count did not match the captured eye planes.");
					continue;
				}
				bool allSaved = true;
				for (uint32_t index = 0; index < screenshot.sequenceOutputCount; ++index) {
					auto& image = screenshot.planes[index].flipHorizontal || screenshot.planes[index].flipVertical ?
					                  orientedPlanes[index] :
					                  mappedPlanes[index];
					const auto& path = screenshot.sequenceOutputPaths[index];
					Util::FileHelpers::EnsureDirectoryExists(path.parent_path());
					if (!SaveSdrScreenshot(
							image,
							path,
							screenshot.saveAsPng,
							screenshot.planes[index].colorSpace,
							screenshot.planes[index].tonemapSceneHdr)) {
						allSaved = false;
						operationError = std::format("Failed to save frame sequence eye {}.", index);
						logger::error("{}", operationError);
						break;
					}
				}
				if (!allSaved) {
					continue;
				}
				operationSucceeded = true;
				logger::debug(
					"Saved lossless frame {} for sequence {}",
					screenshot.sequenceFrameIndex,
					screenshot.sequenceSessionId);
				continue;
			}

			DXGI_FORMAT combinedFormat = planeImages[0]->format;
			vr::EColorSpace combinedColorSpace = screenshot.planes[0].colorSpace;
			const bool combinedTonemapSceneHdr = screenshot.planes[0].tonemapSceneHdr;
			uint32_t planeSlotWidth = 0;
			uint32_t combinedHeight = 0;
			for (uint32_t index = 0; index < screenshot.planeCount; ++index) {
				if (!planeImages[index] ||
					planeImages[index]->format != combinedFormat ||
					screenshot.planes[index].colorSpace != combinedColorSpace ||
					screenshot.planes[index].tonemapSceneHdr != combinedTonemapSceneHdr) {
					planeFailure = true;
					break;
				}
				planeSlotWidth = std::max(planeSlotWidth, static_cast<uint32_t>(planeImages[index]->width));
				combinedHeight = std::max(combinedHeight, static_cast<uint32_t>(planeImages[index]->height));
			}
			uint32_t combinedWidth = planeSlotWidth * screenshot.planeCount;
			if (planeFailure || combinedWidth == 0 || combinedHeight == 0) {
				reportFailure("Screenshot planes used incompatible image contracts.");
				continue;
			}

			DirectX::ScratchImage combinedImage;
			DirectX::ScratchImage stereoCompositeImage;
			DirectX::ScratchImage* assembledImage = nullptr;
			const DirectX::Image* assembled = nullptr;
			if (screenshot.combineFramedEyes) {
				const bool composed = screenshot.planeCount == 2 &&
				                      screenshot.stereoProjectionValid &&
				                      ComposeFramedStereo(
										  planeImages,
										  screenshot.eyeProjectionTangents,
										  screenshot.eyeToHeadTransforms,
										  screenshot.hiddenAreaMeshes,
										  screenshot.dominantEye,
										  combinedColorSpace,
										  stereoCompositeImage);
				if (composed) {
					assembledImage = &stereoCompositeImage;
					assembled = stereoCompositeImage.GetImage(0, 0, 0);
					combinedFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
					combinedColorSpace = vr::ColorSpace_Linear;
					combinedWidth = kFramedEyeOutputWidth;
					combinedHeight = kFramedEyeOutputHeight;
					screenshot.aspectFillWidth = 0;
					screenshot.aspectFillHeight = 0;
				} else {
					const std::size_t dominantIndex = screenshot.planeCount == 2 && screenshot.dominantEye == vr::Eye_Right ? 1u : 0u;
					const auto& plane = screenshot.planes[dominantIndex];
					assembledImage = plane.flipHorizontal || plane.flipVertical ?
					                     &orientedPlanes[dominantIndex] :
					                     &mappedPlanes[dominantIndex];
					assembled = planeImages[dominantIndex];
					combinedWidth = static_cast<uint32_t>(assembled->width);
					combinedHeight = static_cast<uint32_t>(assembled->height);
					logger::warn("Combined-eye screenshot composition failed; using the dominant eye only.");
				}
			} else if (screenshot.planeCount == 1) {
				const auto& plane = screenshot.planes[0];
				assembledImage = plane.flipHorizontal || plane.flipVertical ?
				                     &orientedPlanes[0] :
				                     &mappedPlanes[0];
				assembled = planeImages[0];
			} else {
				if (FAILED(combinedImage.Initialize2D(combinedFormat, combinedWidth, combinedHeight, 1, 1))) {
					reportFailure("Failed to allocate the combined screenshot image.");
					continue;
				}
				assembledImage = &combinedImage;
				assembled = combinedImage.GetImage(0, 0, 0);
				if (!assembled) {
					reportFailure("Failed to access the combined screenshot image.");
					continue;
				}
				std::memset(combinedImage.GetPixels(), 0, combinedImage.GetPixelsSize());

				// Equal-width slots keep the normalized Left/Right presets aligned
				// even if OpenVR accepts asymmetric eye dimensions.
				for (uint32_t index = 0; index < screenshot.planeCount; ++index) {
					const auto* image = planeImages[index];
					const DirectX::Rect sourceRect(0, 0, image->width, image->height);
					const size_t destinationX = static_cast<size_t>(index) * planeSlotWidth;
					if (FAILED(DirectX::CopyRectangle(
							*image,
							sourceRect,
							*assembled,
							DirectX::TEX_FILTER_DEFAULT,
							destinationX,
							0))) {
						planeFailure = true;
						break;
					}
				}
				if (planeFailure) {
					reportFailure("Failed to compose submitted screenshot eyes.");
					continue;
				}
			}
			if (!assembledImage || !assembled) {
				reportFailure("Failed to access the assembled screenshot image.");
				continue;
			}

			DirectX::ScratchImage croppedImage;
			DirectX::ScratchImage* imageToSave = assembledImage;
			if (screenshot.applyCrop) {
				const auto crop = Util::Subrect::ResolvePixelRegion(
					screenshot.cropUV,
					combinedWidth,
					combinedHeight);
				if (crop.x != 0 || crop.y != 0 || crop.w != combinedWidth || crop.h != combinedHeight) {
					if (FAILED(croppedImage.Initialize2D(combinedFormat, crop.w, crop.h, 1, 1))) {
						reportFailure("Failed to allocate the cropped screenshot image.");
						continue;
					}
					const auto* cropped = croppedImage.GetImage(0, 0, 0);
					const DirectX::Rect cropRect(crop.x, crop.y, crop.w, crop.h);
					if (!cropped || FAILED(DirectX::CopyRectangle(
										*assembled,
										cropRect,
										*cropped,
										DirectX::TEX_FILTER_DEFAULT,
										0,
										0))) {
						reportFailure("Failed to crop the screenshot image.");
						continue;
					}
					imageToSave = &croppedImage;
				}
			}

			DirectX::ScratchImage framingCropImage;
			DirectX::ScratchImage framedImage;
			if (screenshot.aspectFillWidth != 0 || screenshot.aspectFillHeight != 0) {
				const auto* framingSource = imageToSave->GetImage(0, 0, 0);
				if (screenshot.aspectFillWidth == 0 || screenshot.aspectFillHeight == 0 ||
					!framingSource ||
					!CenterCropAndResize(
						*framingSource,
						screenshot.aspectFillWidth,
						screenshot.aspectFillHeight,
						combinedColorSpace,
						framingCropImage,
						framedImage)) {
					reportFailure("Failed to frame the screenshot at the requested output size.");
					continue;
				}
				imageToSave = &framedImage;
			}

			Util::FileHelpers::EnsureDirectoryExists(screenshot.outputPath.parent_path());
			const bool saveOk = SaveSdrScreenshot(
				*imageToSave,
				screenshot.outputPath,
				screenshot.saveAsPng,
				combinedColorSpace,
				combinedTonemapSceneHdr);

			if (!saveOk) {
				reportFailure("Failed to save screenshot.");
			} else {
				operationSucceeded = true;
				CopySavedPathToClipboard(screenshot.copyToClipboard, screenshot.outputPath);
				logger::info("Saved screenshot to {}", screenshot.outputPath.string());
				ShowInGameNotification(std::format("Screenshot saved: {}",
					screenshot.outputPath.filename().string()));
			}
		} catch (const std::exception& e) {
			logger::error("Screenshot worker failed with an exception: {}", e.what());
			operationError = e.what();
		} catch (...) {
			reportFailure("Screenshot worker failed with an unknown exception.");
		}
	}
	if (uninitializeCom) {
		CoUninitialize();
	}
}

void ScreenshotFeature::ShowInGameNotification(std::string message)
{
	// ShowHUDMessage must run on the game's main thread; marshall via SKSE's
	// task interface. Third arg dedupes spam-clicks - one toast at a time.
	if (auto* taskInterface = SKSE::GetTaskInterface()) {
		taskInterface->AddTask([msg = std::move(message)]() {
			RE::SendHUDMessage::ShowHUDMessage(msg.c_str(), nullptr, true);
		});
	}
}

bool ScreenshotFeature::StageTexturePlane(
	ID3D11Texture2D* a_sourceTexture,
	const vr::VRTextureBounds_t* a_bounds,
	uint32_t a_eyeIndex,
	vr::EColorSpace a_colorSpace,
	bool a_tonemapSceneHdr,
	StagedPlane& a_plane)
{
	a_plane = {};
	if (!a_sourceTexture) {
		return false;
	}

	winrt::com_ptr<ID3D11Device> sourceDevice;
	a_sourceTexture->GetDevice(sourceDevice.put());
	winrt::com_ptr<ID3D11DeviceContext> sourceContext;
	if (sourceDevice) {
		sourceDevice->GetImmediateContext(sourceContext.put());
	}
	if (!sourceDevice || !sourceContext) {
		return false;
	}
	if (!EnsureReadbackContextProtection(sourceContext.get())) {
		logger::error("Screenshot readback requires ID3D11Multithread protection.");
		return false;
	}

	D3D11_TEXTURE2D_DESC sourceDesc{};
	a_sourceTexture->GetDesc(&sourceDesc);
	if (sourceDesc.Width == 0 || sourceDesc.Height == 0 ||
		sourceDesc.ArraySize == 0 || sourceDesc.MipLevels == 0) {
		return false;
	}

	float uMin = 0.0f;
	float vMin = 0.0f;
	float uMax = 1.0f;
	float vMax = 1.0f;
	if (a_bounds) {
		if (!std::isfinite(a_bounds->uMin) || !std::isfinite(a_bounds->uMax) ||
			!std::isfinite(a_bounds->vMin) || !std::isfinite(a_bounds->vMax)) {
			return false;
		}
		uMin = a_bounds->uMin;
		vMin = a_bounds->vMin;
		uMax = a_bounds->uMax;
		vMax = a_bounds->vMax;
	}

	a_plane.flipHorizontal = uMin > uMax;
	a_plane.flipVertical = vMin > vMax;
	const float leftUV = std::clamp(std::min(uMin, uMax), 0.0f, 1.0f);
	const float rightUV = std::clamp(std::max(uMin, uMax), 0.0f, 1.0f);
	const float topUV = std::clamp(std::min(vMin, vMax), 0.0f, 1.0f);
	const float bottomUV = std::clamp(std::max(vMin, vMax), 0.0f, 1.0f);
	if (rightUV <= leftUV || bottomUV <= topUV) {
		return false;
	}

	const uint32_t sourceLeft = std::min(
		sourceDesc.Width - 1,
		Util::NormalizedCoordinates::ResolvePixelBoundary(leftUV, sourceDesc.Width));
	const uint32_t sourceTop = std::min(
		sourceDesc.Height - 1,
		Util::NormalizedCoordinates::ResolvePixelBoundary(topUV, sourceDesc.Height));
	const uint32_t sourceRight = std::clamp(
		Util::NormalizedCoordinates::ResolvePixelBoundary(rightUV, sourceDesc.Width),
		sourceLeft + 1,
		sourceDesc.Width);
	const uint32_t sourceBottom = std::clamp(
		Util::NormalizedCoordinates::ResolvePixelBoundary(bottomUV, sourceDesc.Height),
		sourceTop + 1,
		sourceDesc.Height);
	const uint32_t copyWidth = sourceRight - sourceLeft;
	const uint32_t copyHeight = sourceBottom - sourceTop;

	const uint32_t arraySlice = std::min<uint32_t>(a_eyeIndex, sourceDesc.ArraySize - 1);
	UINT sourceSubresource = D3D11CalcSubresource(0, arraySlice, sourceDesc.MipLevels);
	ID3D11Texture2D* copySource = a_sourceTexture;
	winrt::com_ptr<ID3D11Texture2D> resolvedTexture;
	if (sourceDesc.SampleDesc.Count > 1) {
		D3D11_TEXTURE2D_DESC resolveDesc = sourceDesc;
		resolveDesc.MipLevels = 1;
		resolveDesc.ArraySize = 1;
		resolveDesc.SampleDesc.Count = 1;
		resolveDesc.SampleDesc.Quality = 0;
		resolveDesc.Usage = D3D11_USAGE_DEFAULT;
		resolveDesc.BindFlags = 0;
		resolveDesc.CPUAccessFlags = 0;
		resolveDesc.MiscFlags = 0;
		if (FAILED(sourceDevice->CreateTexture2D(&resolveDesc, nullptr, resolvedTexture.put()))) {
			return false;
		}
		Util::SetResourceName(resolvedTexture.get(), "Screenshot::ResolvePlane%u", a_eyeIndex);
		sourceContext->ResolveSubresource(
			resolvedTexture.get(),
			0,
			a_sourceTexture,
			sourceSubresource,
			sourceDesc.Format);
		copySource = resolvedTexture.get();
		sourceSubresource = 0;
	}

	D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
	stagingDesc.Width = copyWidth;
	stagingDesc.Height = copyHeight;
	stagingDesc.MipLevels = 1;
	stagingDesc.ArraySize = 1;
	stagingDesc.SampleDesc.Count = 1;
	stagingDesc.SampleDesc.Quality = 0;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;
	if (FAILED(sourceDevice->CreateTexture2D(&stagingDesc, nullptr, a_plane.stagingTexture.put()))) {
		return false;
	}
	Util::SetResourceName(a_plane.stagingTexture.get(), "Screenshot::StagingPlane%u", a_eyeIndex);

	D3D11_BOX sourceRegion{ sourceLeft, sourceTop, 0, sourceRight, sourceBottom, 1 };
	sourceContext->CopySubresourceRegion(
		a_plane.stagingTexture.get(),
		0,
		0,
		0,
		0,
		copySource,
		sourceSubresource,
		&sourceRegion);

	a_plane.format = sourceDesc.Format;
	a_plane.width = copyWidth;
	a_plane.height = copyHeight;
	a_plane.immediateContext = std::move(sourceContext);
	a_plane.colorSpace = a_colorSpace;
	a_plane.tonemapSceneHdr = a_tonemapSceneHdr;
	return true;
}

bool ScreenshotFeature::QueueDesktopCapture(
	IDXGISwapChain* a_swapChain,
	const CaptureOptions& a_options,
	bool a_ownsQueueSlot)
{
	if (!a_ownsQueueSlot) {
		logger::error("Desktop screenshot capture did not own an encoder slot.");
		return false;
	}
	bool ownsQueueSlot = true;
	const auto releaseQueueSlot = [this, &ownsQueueSlot]() {
		if (ownsQueueSlot) {
			ReleaseScreenshotSlot();
			ownsQueueSlot = false;
		}
	};
	const SKSE::stl::scope_exit releaseQueueSlotOnExit([&releaseQueueSlot]() noexcept {
		releaseQueueSlot();
	});
	try {
		winrt::com_ptr<ID3D11Texture2D> sourceTexture;
		const char* sourceDescription = "DXGI desktop backbuffer";
		constexpr vr::EColorSpace sourceColorSpace = vr::ColorSpace_Auto;
		constexpr bool tonemapSceneHdr = true;
		if (a_swapChain) {
			(void)a_swapChain->GetBuffer(
				0,
				__uuidof(ID3D11Texture2D),
				sourceTexture.put_void());
		}

		winrt::com_ptr<ID3D11Texture2D> slotTextureKeepAlive;
		if (!sourceTexture && !globals::game::isVR) {
			const auto source = SelectCaptureSource(slotTextureKeepAlive);
			if (source.texture) {
				sourceTexture.copy_from(source.texture);
				sourceDescription = source.description;
			}
		}
		if (!sourceTexture) {
			logger::error("Failed to acquire the DXGI desktop backbuffer for screenshot capture.");
			return false;
		}

		vr::VRTextureBounds_t cropBounds{};
		const vr::VRTextureBounds_t* stageBounds = nullptr;
		if (a_options.applyCrop) {
			cropBounds = {
				a_options.cropUV.x,
				a_options.cropUV.y,
				a_options.cropUV.x + a_options.cropUV.w,
				a_options.cropUV.y + a_options.cropUV.h
			};
			stageBounds = &cropBounds;
		}

		PendingScreenshot screenshot;
		if (!StageTexturePlane(
				sourceTexture.get(),
				stageBounds,
				0,
				sourceColorSpace,
				tonemapSceneHdr,
				screenshot.planes[0])) {
			logger::error("Failed to stage the desktop screenshot source ({}).", sourceDescription);
			return false;
		}

		screenshot.planeCount = 1;
		screenshot.cropUV = a_options.cropUV;
		screenshot.applyCrop = false;
		screenshot.saveAsPng = a_options.saveAsPng;
		screenshot.copyToClipboard = a_options.copyToClipboard;
		screenshot.ownsQueueSlot = true;
		screenshot.sequenceSessionId = a_options.sequenceSessionId;
		screenshot.sequenceFrameIndex = a_options.sequenceFrameIndex;
		screenshot.sequenceTimestampUs = a_options.sequenceTimestampUs;
		screenshot.sequenceOutputPaths = a_options.sequenceOutputPaths;
		screenshot.sequenceOutputCount = a_options.sequenceOutputCount;
		screenshot.outputPath = screenshot.sequenceOutputCount != 0 ?
		                        screenshot.sequenceOutputPaths[0] :
		                        BuildScreenshotPath(a_options.screenshotPath, screenshot.saveAsPng);
		logger::debug("Capturing from {}", sourceDescription);
		ownsQueueSlot = false;
		return QueueScreenshot(std::move(screenshot));
	} catch (const std::exception& e) {
		logger::error("Desktop screenshot staging failed with an exception: {}", e.what());
		return false;
	} catch (...) {
		logger::error("Desktop screenshot staging failed with an unknown exception.");
		return false;
	}
}

void ScreenshotFeature::ObserveAcceptedVRSubmit(
	uint64_t a_compositorCycleToken,
	vr::EVREye a_eye,
	ID3D11Texture2D* a_texture,
	const vr::VRTextureBounds_t* a_bounds,
	vr::EColorSpace a_colorSpace)
{
	if (!HasPendingCapture() ||
		!globals::game::isVR ||
		!a_texture ||
		(a_eye != vr::Eye_Left && a_eye != vr::Eye_Right) ||
		(globals::state && globals::state->isLoadingMenuOpen)) {
		return;
	}

	PendingScreenshot completedScreenshot;
	bool completed = false;
	VRCaptureSource completedSource = VRCaptureSource::HMDSubmission;
	{
		std::lock_guard lock(captureStateMutex);
		if (!IsRuntimeEnabled() ||
			!activeCapture.pending ||
			!IsSubmittedEyeCapture(activeCapture.source)) {
			return;
		}
		const bool singleEyeCapture = activeCapture.source == VRCaptureSource::FramedEye ||
		                              activeCapture.source == VRCaptureSource::HMDEye;
		const bool framedEyeCapture = activeCapture.source == VRCaptureSource::FramedEye;
		const bool framedStereoCapture = activeCapture.source == VRCaptureSource::FramedStereo;
		const vr::EVREye requestedEye = activeCapture.options.framedEye == vr::Eye_Right ?
		                                    vr::Eye_Right :
		                                    vr::Eye_Left;
		if (singleEyeCapture && a_eye != requestedEye) {
			return;
		}

		if (activeCapture.compositorCycleToken != a_compositorCycleToken) {
			activeCapture.compositorCycleToken = a_compositorCycleToken;
			activeCapture.eyeMask = 0;
			activeCapture.eyes = {};
		}

		const uint32_t eyeIndex = a_eye == vr::Eye_Right ? 1u : 0u;
		StagedPlane plane;
		if (!StageTexturePlane(
				a_texture,
				a_bounds,
				eyeIndex,
				a_colorSpace,
				false,
				plane)) {
			return;
		}

		if (singleEyeCapture) {
			completedScreenshot.planes[0] = std::move(plane);
			completedScreenshot.planeCount = 1;
			completedScreenshot.applyCrop = false;
			if (framedEyeCapture && activeCapture.options.sequenceSessionId == 0) {
				completedScreenshot.aspectFillWidth = kFramedEyeOutputWidth;
				completedScreenshot.aspectFillHeight = kFramedEyeOutputHeight;
			}
		} else {
			activeCapture.eyes[eyeIndex] = std::move(plane);
			activeCapture.eyeMask |= static_cast<uint8_t>(1u << eyeIndex);
			if (activeCapture.eyeMask != 0x3u) {
				return;
			}

			if (activeCapture.eyes[0].format != activeCapture.eyes[1].format ||
				activeCapture.eyes[0].colorSpace != activeCapture.eyes[1].colorSpace ||
				activeCapture.eyes[0].tonemapSceneHdr != activeCapture.eyes[1].tonemapSceneHdr) {
				logger::warn("Accepted VR screenshot eyes used incompatible image contracts; waiting for a coherent pair.");
				activeCapture.eyeMask = 0;
				activeCapture.eyes = {};
				return;
			}

			completedScreenshot.planes = std::move(activeCapture.eyes);
			completedScreenshot.planeCount = 2;
			if (framedStereoCapture) {
				completedScreenshot.applyCrop = false;
				completedScreenshot.aspectFillWidth = kFramedEyeOutputWidth;
				completedScreenshot.aspectFillHeight = kFramedEyeOutputHeight;
				completedScreenshot.combineFramedEyes = true;
				completedScreenshot.dominantEye = requestedEye;
				completedScreenshot.eyeProjectionTangents = activeCapture.options.eyeProjectionTangents;
				completedScreenshot.eyeToHeadTransforms = activeCapture.options.eyeToHeadTransforms;
				completedScreenshot.hiddenAreaMeshes = std::move(activeCapture.options.hiddenAreaMeshes);
				completedScreenshot.stereoProjectionValid = activeCapture.options.stereoProjectionValid;
			} else {
				completedScreenshot.cropUV = activeCapture.options.cropUV;
				completedScreenshot.applyCrop = activeCapture.options.applyCrop;
			}
		}

		completedScreenshot.saveAsPng = activeCapture.options.saveAsPng;
		completedScreenshot.copyToClipboard = activeCapture.options.copyToClipboard;
		completedScreenshot.sequenceSessionId = activeCapture.options.sequenceSessionId;
		completedScreenshot.sequenceFrameIndex = activeCapture.options.sequenceFrameIndex;
		completedScreenshot.sequenceTimestampUs = activeCapture.options.sequenceTimestampUs;
		completedScreenshot.sequenceOutputPaths = activeCapture.options.sequenceOutputPaths;
		completedScreenshot.sequenceOutputCount = activeCapture.options.sequenceOutputCount;
		try {
			completedScreenshot.outputPath = completedScreenshot.sequenceOutputCount != 0 ?
			                                     completedScreenshot.sequenceOutputPaths[0] :
			                                     BuildScreenshotPath(
										 activeCapture.options.screenshotPath,
										 completedScreenshot.saveAsPng);
		} catch (const std::exception& e) {
			logger::error("Failed to prepare the VR screenshot output path: {}", e.what());
			const auto sequenceSessionId = activeCapture.options.sequenceSessionId;
			const auto sequenceFrameIndex = activeCapture.options.sequenceFrameIndex;
			const auto sequenceTimestampUs = activeCapture.options.sequenceTimestampUs;
			const auto sequencePaths = activeCapture.options.sequenceOutputPaths;
			const auto sequencePathCount = activeCapture.options.sequenceOutputCount;
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			if (sequenceSessionId != 0) {
				CompleteSequenceFrame(sequenceSessionId, sequenceFrameIndex, sequenceTimestampUs, sequencePaths, sequencePathCount, false, "output_path_failed");
			} else {
				ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
			}
			return;
		} catch (...) {
			logger::error("Failed to prepare the VR screenshot output path.");
			const auto sequenceSessionId = activeCapture.options.sequenceSessionId;
			const auto sequenceFrameIndex = activeCapture.options.sequenceFrameIndex;
			const auto sequenceTimestampUs = activeCapture.options.sequenceTimestampUs;
			const auto sequencePaths = activeCapture.options.sequenceOutputPaths;
			const auto sequencePathCount = activeCapture.options.sequenceOutputCount;
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			if (sequenceSessionId != 0) {
				CompleteSequenceFrame(sequenceSessionId, sequenceFrameIndex, sequenceTimestampUs, sequencePaths, sequencePathCount, false, "output_path_failed");
			} else {
				ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
			}
			return;
		}
		completedScreenshot.ownsQueueSlot = std::exchange(activeCapture.ownsQueueSlot, false);
		completedSource = activeCapture.source;
		ClearActiveCapture(activeCapture);
		capturePending.store(false, std::memory_order_release);
		completed = true;
	}

	if (completed) {
		switch (completedSource) {
		case VRCaptureSource::FramedEye:
			logger::debug("Capturing one accepted OpenVR eye at 2560 x 1440");
			break;
		case VRCaptureSource::FramedStereo:
			logger::debug("Capturing a combined accepted OpenVR eye pair at 2560 x 1440");
			break;
		case VRCaptureSource::HMDEye:
			logger::debug("Capturing one accepted OpenVR eye at source resolution");
			break;
		case VRCaptureSource::HMDSubmission:
		default:
			logger::debug("Capturing the accepted OpenVR HMD eye pair");
			break;
		}
		const auto sequenceSessionId = completedScreenshot.sequenceSessionId;
		const auto sequenceFrameIndex = completedScreenshot.sequenceFrameIndex;
		const auto sequenceTimestampUs = completedScreenshot.sequenceTimestampUs;
		const auto sequencePaths = completedScreenshot.sequenceOutputPaths;
		const auto sequencePathCount = completedScreenshot.sequenceOutputCount;
		if (!QueueScreenshot(std::move(completedScreenshot))) {
			if (sequenceSessionId != 0) {
				CompleteSequenceFrame(
					sequenceSessionId,
					sequenceFrameIndex,
					sequenceTimestampUs,
					sequencePaths,
					sequencePathCount,
					false,
					"artifact_queue_failed");
			}
			if (sequenceSessionId == 0) {
				ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
			}
		}
	}
}

void ScreenshotFeature::OnBeforePresent(IDXGISwapChain* a_swapChain)
{
	RestoreReadbackContextProtectionIfIdle();
	ScheduleNextSequenceFrame();
	if (!HasPendingCapture()) {
		return;
	}

	std::lock_guard lock(captureStateMutex);
	if (!IsRuntimeEnabled() || !activeCapture.pending) {
		return;
	}

	++activeCapture.presentsWaited;
	if (activeCapture.source == VRCaptureSource::HMDSubmission ||
		activeCapture.source == VRCaptureSource::HMDEye) {
		if (activeCapture.presentsWaited >= kCaptureTimeoutPresents) {
			if (activeCapture.options.sequenceSessionId != 0) {
				const auto options = activeCapture.options;
				ClearActiveCapture(activeCapture);
				capturePending.store(false, std::memory_order_release);
				CompleteSequenceFrame(
					options.sequenceSessionId,
					options.sequenceFrameIndex,
					options.sequenceTimestampUs,
					options.sequenceOutputPaths,
					options.sequenceOutputCount,
					false,
					"source_timeout");
			} else {
				FallBackToDesktopCapture(activeCapture, "no coherent accepted eye pair arrived before the timeout");
			}
		}
		return;
	}
	if (IsFramedCapture(activeCapture.source)) {
		if (activeCapture.presentsWaited >= kCaptureTimeoutPresents) {
			logger::warn("Framed-view screenshot capture timed out before the required eye submission arrived.");
			const auto options = activeCapture.options;
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			if (options.sequenceSessionId != 0) {
				CompleteSequenceFrame(
					options.sequenceSessionId,
					options.sequenceFrameIndex,
					options.sequenceTimestampUs,
					options.sequenceOutputPaths,
					options.sequenceOutputCount,
					false,
					"source_timeout");
			} else {
				ShowInGameNotification("Framed-view screenshot failed - missing eye submission");
			}
		}
		return;
	}

	const auto options = activeCapture.options;
	const bool queued = QueueDesktopCapture(
		a_swapChain,
		options,
		std::exchange(activeCapture.ownsQueueSlot, false));
	ClearActiveCapture(activeCapture);
	capturePending.store(false, std::memory_order_release);
	if (!queued) {
		if (options.sequenceSessionId != 0) {
			CompleteSequenceFrame(
				options.sequenceSessionId,
				options.sequenceFrameIndex,
				options.sequenceTimestampUs,
				options.sequenceOutputPaths,
				options.sequenceOutputCount,
				false,
				"desktop_stage_failed");
		} else {
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
		}
	}
}

void ScreenshotFeature::DrawPostCaptureIndicator()
{
	const bool recording = IsFrameSequenceCapturing();
	if (globals::game::isVR) {
		globals::features::vr.SubmitCaptureIndicator(recording);
		// The OpenVR overlay is the sole VR indicator. Rendering the desktop ImGui
		// fallback as well makes a second dot part of Skyrim's avatar-relative
		// presentation, so it does not remain fixed to the player's gaze.
		return;
	}
	if (!recording) {
		return;
	}

	ImGuiContext* context = ImGui::GetCurrentContext();
	if (!context) {
		return;
	}
	ImGuiIO& io = ImGui::GetIO();
	if (!io.Fonts || !io.Fonts->IsBuilt() || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
		return;
	}

	// This is deliberately a second, tiny draw submission. OnBeforePresent has
	// already queued the lossless texture copy, so the indicator is visible to
	// the player but cannot become part of the captured frame.
	ImDrawList drawList(ImGui::GetDrawListSharedData());
	drawList._OwnerName = "CSXCaptureIndicator";
	drawList._ResetForNewFrame();
	drawList.PushTextureID(io.Fonts->TexID);
	drawList.PushClipRectFullScreen();
	const float radius = std::clamp(io.DisplaySize.y * 0.009f, 6.0f, 12.0f);
	const ImVec2 centre(io.DisplaySize.x - radius * 2.5f, radius * 2.5f);
	drawList.AddCircleFilled(centre, radius, IM_COL32(235, 38, 38, 255), 24);
	drawList.PopClipRect();
	drawList.PopTextureID();
	drawList._PopUnusedDrawCmd();

	ImDrawData drawData{};
	drawData.Valid = true;
	drawData.CmdLists.push_back(&drawList);
	drawData.CmdListsCount = 1;
	drawData.TotalIdxCount = drawList.IdxBuffer.Size;
	drawData.TotalVtxCount = drawList.VtxBuffer.Size;
	drawData.DisplayPos = ImVec2(0.0f, 0.0f);
	drawData.DisplaySize = io.DisplaySize;
	drawData.FramebufferScale = io.DisplayFramebufferScale;
	ImGui_ImplDX11_RenderDrawData(&drawData);
}
