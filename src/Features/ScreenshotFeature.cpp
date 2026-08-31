// Screenshot Feature
// Non-blocking screenshot tool for flat (SE/AE) and VR. GPU copy runs on the
// render thread; encoding and disk I/O run on a dedicated worker thread so
// capture does not stall the frame.

#include "Features/ScreenshotFeature.h"
#include "Api/ScreenshotService.h"
#include "Features/ScreenshotApi.h"
#include "Features/ScreenshotApiPolicy.h"
#include "Features/VR.h"
#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"
#include "Utils/NormalizedCoordinates.h"
#include "Utils/WinApi.h"
#include <DirectXTex.h>
#include <PCH.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <limits>
#include <numeric>
#include <thread>
#include <tuple>
#include <utility>

namespace
{
	constexpr uint32_t kCaptureTimeoutPresents = 6;
	constexpr std::size_t kMaxOutstandingScreenshots = 2;
	constexpr auto kReadbackMapTimeout = std::chrono::milliseconds(500);

	const char* CaptureEyeName(ScreenshotFeature::CaptureEye a_eye)
	{
		switch (a_eye) {
		case ScreenshotFeature::CaptureEye::Right:
			return "Right";
		case ScreenshotFeature::CaptureEye::Both:
			return "Both";
		case ScreenshotFeature::CaptureEye::Left:
		default:
			return "Left";
		}
	}

	ScreenshotFeature::CaptureEye ParseCaptureEye(
		const json& a_json,
		std::string_view a_key,
		ScreenshotFeature::CaptureEye a_default)
	{
		if (!a_json.contains(a_key) || !a_json[a_key].is_string())
			return a_default;
		const auto value = a_json[a_key].get<std::string>();
		if (value == "Right")
			return ScreenshotFeature::CaptureEye::Right;
		if (value == "Both")
			return ScreenshotFeature::CaptureEye::Both;
		return ScreenshotFeature::CaptureEye::Left;
	}

	std::filesystem::path ResolveCapturePath(
		const std::filesystem::path& a_configuredPath,
		bool a_sequence)
	{
		if (a_configuredPath.is_absolute())
			return a_configuredPath;
		const auto root = a_sequence ? Util::GetVideosPath() : Util::GetPicturesPath();
		if (!root)
			throw std::runtime_error("Windows capture folder is unavailable");
		return *root / "Community Shaders" / a_configuredPath;
	}

	bool IsTerminalCaptureState(std::string_view a_state)
	{
		return a_state == "completed" || a_state == "completed_with_warnings" ||
		       a_state == "failed" || a_state == "failed_partial" ||
		       a_state == "rejected" || a_state == "cancelled" ||
		       a_state == "cancelled_partial" || a_state == "stopped" ||
		       a_state == "dropped";
	}

	json BuildCaptureDescriptor(
		const ScreenshotFeature& a_feature,
		ScreenshotFeature::CaptureEye a_eye,
		bool a_usePng,
		bool a_clipboard)
	{
		const bool vrRuntime = globals::game::isVR;
		const bool framed = a_feature.vrCaptureSource == ScreenshotFeature::VRCaptureSource::FramedEye ||
		                    a_feature.vrCaptureSource == ScreenshotFeature::VRCaptureSource::FramedStereo;
		json outputs = json::array();
		auto append = [&](std::string_view a_view, std::string_view a_suffix) {
			outputs.push_back({
				{ "view", a_view },
				{ "encoding", { { "format", a_usePng ? "png" : "bmp" }, { "colourContract", "sdr_srgb" } } },
				{ "nameSuffix", a_suffix },
			});
		};

		if (!vrRuntime || a_feature.vrCaptureSource == ScreenshotFeature::VRCaptureSource::DesktopMirror) {
			append("source_native", "desktop");
		} else if (framed) {
			if (a_eye == ScreenshotFeature::CaptureEye::Both)
				append("framed_combined", "combined");
			else if (a_eye == ScreenshotFeature::CaptureEye::Right)
				append("framed_right", "right");
			else
				append("framed_left", "left");
		} else if (a_eye == ScreenshotFeature::CaptureEye::Both) {
			append("left_eye", "left");
			append("right_eye", "right");
		} else if (a_eye == ScreenshotFeature::CaptureEye::Right) {
			append("right_eye", "right");
		} else {
			append("left_eye", "left");
		}

		return {
			{ "source", {
							{ "kind", vrRuntime && a_feature.vrCaptureSource != ScreenshotFeature::VRCaptureSource::DesktopMirror ? "hmd_submission" : "desktop_mirror" },
							{ "fallback", "reject" },
						} },
			{ "outputs", std::move(outputs) },
			{ "destination", { { "policy", "settings_default" }, { "overwrite", "never" } } },
			{ "clipboard", a_clipboard ? "file_reference" : "none" },
		};
	}
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

	const char* DescribeCaptureSource(ScreenshotFeature::VRCaptureSource a_source)
	{
		switch (a_source) {
		case ScreenshotFeature::VRCaptureSource::HMDSubmission:
			return "the accepted HMD submission";
		case ScreenshotFeature::VRCaptureSource::HMDEye:
			return "one accepted HMD eye";
		case ScreenshotFeature::VRCaptureSource::FramedEye:
			return "a framed HMD eye";
		case ScreenshotFeature::VRCaptureSource::FramedStereo:
			return "a combined framed HMD view";
		case ScreenshotFeature::VRCaptureSource::DesktopMirror:
		default:
			return "the desktop mirror";
		}
	}

	const char* ActualSourceKind(ScreenshotFeature::VRCaptureSource a_source)
	{
		return a_source == ScreenshotFeature::VRCaptureSource::DesktopMirror ?
		           "desktop_mirror" :
		           "hmd_submission";
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

	// User capture paths live outside the game tree by default.
	std::filesystem::path ResolveToAbsoluteGamePath(const std::filesystem::path& path)
	{
		return ResolveCapturePath(path, false);
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

		const auto absolutePath = ResolveToAbsoluteGamePath(path);
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
		const auto temporaryPath = outputPath.parent_path() /
		                           (outputPath.stem().wstring() +
									   std::format(L".writing-{}-{}", GetCurrentProcessId(), GetTickCount64()) +
									   outputPath.extension().wstring());
		std::error_code ec;
		std::filesystem::remove(temporaryPath, ec);
		if (FAILED(DirectX::SaveToWICFile(*saveImage, wicFlags, codec, temporaryPath.c_str())))
			return false;
		std::filesystem::rename(temporaryPath, outputPath, ec);
		if (ec) {
			std::filesystem::remove(temporaryPath, ec);
			return false;
		}
		return true;
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
		return ResolveToAbsoluteGamePath(std::filesystem::path(screenshotPath) / buf);
	}

	std::filesystem::path MakeCollisionSafePath(std::filesystem::path path)
	{
		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return path;
		const auto parent = path.parent_path();
		const auto stem = path.stem().wstring();
		const auto extension = path.extension().wstring();
		for (uint32_t suffix = 1; suffix != 100000; ++suffix) {
			auto candidate = parent / std::filesystem::path(std::format(L"{}_{:03}{}", stem, suffix, extension));
			if (!std::filesystem::exists(candidate, ec))
				return candidate;
		}
		throw std::runtime_error("unable to allocate a collision-free screenshot path");
	}

}

ScreenshotFeature::ScreenshotFeature() :
	screenshotWorkerState(std::make_shared<ScreenshotWorkerState>())
{
	// Start only after every member used by the watchdog has begun lifetime.
	sourceDeadlineWatchdog = std::jthread(
		[this](std::stop_token token) { SourceDeadlineLoop(token); });
}

ScreenshotFeature::~ScreenshotFeature()
{
	sourceDeadlineWatchdog.request_stop();
	sourceDeadlineCondition.notify_all();
	if (sourceDeadlineWatchdog.joinable())
		sourceDeadlineWatchdog.join();
	if (screenshotApi)
		screenshotApi->BeginShutdown("screenshot feature shutdown");
	std::string cancelledRequestId;
	{
		std::lock_guard lock(captureStateMutex);
		if (activeCapture.pending)
			cancelledRequestId = activeCapture.options.requestId;
		ClearActiveCapture(activeCapture);
		capturePending.store(false, std::memory_order_release);
	}
	if (!cancelledRequestId.empty() && screenshotApi)
		screenshotApi->OnSourceTerminal(cancelledRequestId, "cancelled", "shutdown");
	StopWorkerThread();
	if (screenshotApi && !screenshotApi->DrainForShutdown(std::chrono::seconds(2)))
		logger::error("Screenshot manifest work did not drain within the shutdown bound.");
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
	if (a_json.contains("FrameCaptureUsePng"))
		frameCaptureUsePng = a_json["FrameCaptureUsePng"];
	if (a_json.contains("CopyToClipboard"))
		copyToClipboard = a_json["CopyToClipboard"];
	screenshotEye = ParseCaptureEye(a_json, "ScreenshotEye", screenshotEye);
	frameCaptureEye = ParseCaptureEye(a_json, "FrameCaptureEye", frameCaptureEye);
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

	// Schema 2 groups sequence defaults while retaining one-way compatibility
	// with the flat experimental keys used by earlier automation builds.
	const json* sequence = nullptr;
	if (a_json.contains("Sequence") && a_json["Sequence"].is_object())
		sequence = &a_json["Sequence"];
	if (sequence) {
		sequenceDefaults.frameCount = std::clamp(sequence->value("FrameCount", sequenceDefaults.frameCount), 1u, 10000u);
		if (sequence->contains("Schedule") && (*sequence)["Schedule"].is_object())
			sequenceDefaults.intervalFrames = std::max(1u, (*sequence)["Schedule"].value("IntervalFrames", sequenceDefaults.intervalFrames));
		if (sequence->contains("Outputs") && (*sequence)["Outputs"].is_object())
			sequenceDefaults.saveSeparateEyes = (*sequence)["Outputs"].value("SeparateEyes", sequenceDefaults.saveSeparateEyes);
		if (sequence->contains("Packaging") && (*sequence)["Packaging"].is_object()) {
			const auto& packaging = (*sequence)["Packaging"];
			if (packaging.contains("PreviewVideo") && packaging["PreviewVideo"].is_object()) {
				const auto& preview = packaging["PreviewVideo"];
				sequenceDefaults.writePreviewVideo = preview.value("Requested", sequenceDefaults.writePreviewVideo);
				sequenceDefaults.previewFramesPerSecond = std::clamp(preview.value("FramesPerSecond", sequenceDefaults.previewFramesPerSecond), 1u, 240u);
			}
		}
	} else {
		sequenceDefaults.frameCount = std::clamp(a_json.value("SequenceFrameCount", sequenceDefaults.frameCount), 1u, 10000u);
		sequenceDefaults.intervalFrames = std::max(1u, a_json.value("SequenceFrameInterval", sequenceDefaults.intervalFrames));
		sequenceDefaults.previewFramesPerSecond = std::clamp(a_json.value("SequencePreviewFramesPerSecond", sequenceDefaults.previewFramesPerSecond), 1u, 240u);
		sequenceDefaults.saveSeparateEyes = a_json.value("SequenceSaveSeparateEyes", sequenceDefaults.saveSeparateEyes);
		sequenceDefaults.writePreviewVideo = a_json.value("SequenceWritePreviewVideo", sequenceDefaults.writePreviewVideo);
	}

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
	a_json["FrameCaptureUsePng"] = frameCaptureUsePng;
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
	case VRCaptureSource::HMDSubmission:
	case VRCaptureSource::HMDEye:
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
	a_json["SettingsSchemaVersion"] = 2;
	a_json["Sequence"] = {
		{ "FrameCount", sequenceDefaults.frameCount },
		{ "Schedule", { { "Basis", "game_frames" }, { "IntervalFrames", sequenceDefaults.intervalFrames } } },
		{ "Backpressure", { { "Policy", "skip" }, { "MaximumConsecutiveSkips", 10 } } },
		{ "FailurePolicy", "continue" },
		{ "Outputs", { { "SeparateEyes", sequenceDefaults.saveSeparateEyes } } },
		{ "Packaging", { { "PreviewVideo", { { "Requested", sequenceDefaults.writePreviewVideo }, { "FramesPerSecond", sequenceDefaults.previewFramesPerSecond } } } } },
	};
	// Remove migrated experimental spellings so preset layers have one
	// authoritative representation.
	a_json.erase("SequenceFrameCount");
	a_json.erase("SequenceFrameInterval");
	a_json.erase("SequencePreviewFramesPerSecond");
	a_json.erase("SequenceSaveSeparateEyes");
	a_json.erase("SequenceWritePreviewVideo");
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
			ImGui::TextUnformatted("Captures the selected final accepted eye submission, or both eyes as separate files.");
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
			screenshotEye = static_cast<CaptureEye>(eye);
			vrFramedView = eye == 2 ?
			                   VRFramedView::Combined :
			                   (eye == 1 ? VRFramedView::Right : VRFramedView::Left);
			if (IsFramedCapture(vrCaptureSource)) {
				vrCaptureSource = eye == 2 ? VRCaptureSource::FramedStereo : VRCaptureSource::FramedEye;
			}
			if (IsFramedCapture(vrCaptureSource) && eye == 2) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted("Combined keeps the dominant eye through the shared view and fills its outer edge from the other eye.");
				}
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
		RequestUiCapture();
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
			const auto resolved = ResolveCapturePath(screenshotPath, false);
			std::error_code ec;
			std::filesystem::create_directories(resolved, ec);
			ShellExecuteW(nullptr, L"open", resolved.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		} catch (const std::exception& e) {
			logger::error("Could not open screenshot directory: {}", e.what());
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
	ImGui::TextWrapped("Sequence capture uses Screenshot API v1. Video composition and audio are intentionally outside CSX.");
	int sequenceFrames = static_cast<int>(sequenceDefaults.frameCount);
	if (ImGui::SliderInt("Frames", &sequenceFrames, 1, 10000))
		sequenceDefaults.frameCount = static_cast<uint32_t>(sequenceFrames);
	int sequenceInterval = static_cast<int>(sequenceDefaults.intervalFrames);
	if (ImGui::SliderInt("Interval (game frames)", &sequenceInterval, 1, 60))
		sequenceDefaults.intervalFrames = static_cast<uint32_t>(sequenceInterval);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Start at 12 on a busy mod list, then reduce until the manifest first reports pressure.");
	}

	int sequenceFormat = frameCaptureUsePng ? 1 : 0;
	ImGui::RadioButton("BMP (fast)##SequenceFormat", &sequenceFormat, 0);
	ImGui::SameLine();
	ImGui::RadioButton("PNG (compact)##SequenceFormat", &sequenceFormat, 1);
	frameCaptureUsePng = sequenceFormat != 0;
	if (globals::game::isVR) {
		int eye = static_cast<int>(frameCaptureEye);
		ImGui::TextUnformatted("Capture eye:");
		ImGui::SameLine();
		ImGui::RadioButton("Left##SequenceEye", &eye, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Right##SequenceEye", &eye, 1);
		ImGui::SameLine();
		ImGui::RadioButton("Both##SequenceEye", &eye, 2);
		frameCaptureEye = static_cast<CaptureEye>(eye);
	}

	char sequencePath[260];
	strncpy_s(sequencePath, sizeof(sequencePath), frameCapturePath.c_str(), _TRUNCATE);
	ImGui::PushItemWidth(-FLT_MIN - 120.0f);
	if (ImGui::InputText("##FrameCaptureFolder", sequencePath, sizeof(sequencePath)))
		frameCapturePath = sequencePath;
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::BeginDisabled(frameCapturePath.empty());
	if (ImGui::Button("Open##FrameCaptureFolder")) {
		try {
			const auto resolved = ResolveCapturePath(frameCapturePath, true);
			std::error_code ec;
			std::filesystem::create_directories(resolved, ec);
			ShellExecuteW(nullptr, L"open", resolved.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		} catch (const std::exception& e) {
			logger::error("Could not open frame-capture directory: {}", e.what());
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text("Folder");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Relative paths resolve beneath Videos\\Community Shaders.");
		ImGui::Text("Absolute paths save there directly.");
	}

	static std::atomic_uint64_t sequenceCommand{ 1 };
	const auto now = std::chrono::steady_clock::now();
	if (!uiSequenceRequestId.empty() && now >= nextUiSequencePoll) {
		nextUiSequencePoll = now + std::chrono::milliseconds(500);
		const auto response = CSX::Api::DispatchScreenshotServiceRequest({
			{ "contractMajor", 1 },
			{ "action", "request_get" },
			{ "clientId", "csx.menu.sequence" },
			{ "commandId", std::format("status:{}:{}", GetTickCount64(), sequenceCommand.fetch_add(1, std::memory_order_relaxed)) },
			{ "requestId", uiSequenceRequestId },
		});
		if (response.value("ok", false) && response.contains("result")) {
			const auto state = response["result"].value("state", std::string{});
			if (IsTerminalCaptureState(state))
				uiSequenceRequestId.clear();
		}
	}

	ImGui::BeginDisabled(!IsRuntimeEnabled());
	if (uiSequenceRequestId.empty()) {
		if (ImGui::Button("Start Frame Capture")) {
			auto capture = BuildCaptureDescriptor(*this, frameCaptureEye, frameCaptureUsePng, false);
			const auto response = CSX::Api::DispatchScreenshotServiceRequest({
				{ "contractMajor", 1 },
				{ "action", "sequence_start" },
				{ "clientId", "csx.menu.sequence" },
				{ "commandId", std::format("start:{}:{}", GetTickCount64(), sequenceCommand.fetch_add(1, std::memory_order_relaxed)) },
				{ "sequence", {
								  { "frameCount", sequenceDefaults.frameCount },
								  { "useSettings", false },
								  { "schedule", { { "basis", "game_frames" }, { "intervalFrames", sequenceDefaults.intervalFrames } } },
								  { "backpressure", { { "policy", "skip" }, { "maximumConsecutiveSkips", 5 } } },
								  { "failurePolicy", "continue" },
								  { "capture", std::move(capture) },
								  { "packaging", { { "frameManifest", true }, { "previewVideo", { { "requested", false } } } } },
							  } },
			});
			if (response.value("ok", false) && response.contains("result"))
				uiSequenceRequestId = response["result"].value("requestId", std::string{});
		}
	} else if (ImGui::Button("Stop Frame Capture")) {
		const auto response = CSX::Api::DispatchScreenshotServiceRequest({
			{ "contractMajor", 1 },
			{ "action", "sequence_stop" },
			{ "clientId", "csx.menu.sequence" },
			{ "commandId", std::format("stop:{}:{}", GetTickCount64(), sequenceCommand.fetch_add(1, std::memory_order_relaxed)) },
			{ "requestId", uiSequenceRequestId },
		});
		if (response.value("ok", false) && response.contains("result") &&
			IsTerminalCaptureState(response["result"].value("state", std::string{})))
			uiSequenceRequestId.clear();
	}
	ImGui::EndDisabled();

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

ScreenshotFeature::CaptureOptions ScreenshotFeature::SnapshotCaptureOptions() const
{
	return {
		.screenshotPath = screenshotPath,
		.cropUV = subrect.GetUV(),
		.applyCrop = applyCropToScreenshot,
		.saveAsPng = sdrUsePng,
		.copyToClipboard = copyToClipboard,
		.framedEye = vrCaptureSource == VRCaptureSource::FramedStereo ?
		                 vrFramedDominantEye :
		                 (vrFramedView == VRFramedView::Right ? vr::Eye_Right : vr::Eye_Left)
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
	if (!a_capture.options.requestId.empty() && screenshotApi)
		screenshotApi->OnSourceFallback(a_capture.options.requestId, a_reason, "desktop_mirror");
	a_capture.source = VRCaptureSource::DesktopMirror;
	a_capture.compositorCycleToken = 0;
	a_capture.eyeMask = 0;
	a_capture.eyes = {};
	a_capture.presentsWaited = 0;
}

void ScreenshotFeature::RequestUiCapture()
{
	(void)RequestApiCapture("ui");
}

void ScreenshotFeature::EnsureScreenshotApi()
{
	std::lock_guard lock(screenshotWorkerState->mutex);
	if (!screenshotWorkerState->api)
		screenshotWorkerState->api = std::make_shared<ScreenshotApi>();
	screenshotApi = screenshotWorkerState->api;
}

nlohmann::json ScreenshotFeature::HandleApiRequest(const nlohmann::json& a_request)
{
	EnsureScreenshotApi();
	return screenshotApi->HandleRequest(*this, a_request);
}

nlohmann::json ScreenshotFeature::RequestApiCapture(std::string_view a_origin)
{
	static std::atomic_uint64_t commandSequence{ 1 };
	return CSX::Api::DispatchScreenshotServiceRequest({
		{ "contractMajor", 1 },
		{ "action", "capture" },
		{ "clientId", std::format("csx.control:{}", a_origin) },
		{ "commandId", std::format("{}:{}", GetTickCount64(), commandSequence.fetch_add(1, std::memory_order_relaxed)) },
		{ "useSettings", false },
		{ "capture", BuildCaptureDescriptor(*this, screenshotEye, sdrUsePng, copyToClipboard) },
	});
}

bool ScreenshotFeature::TryStartApiCapture(
	std::string a_requestId,
	const nlohmann::json& a_effectiveDescriptor,
	std::string a_parentRequestId,
	uint32_t a_sequenceOrdinal)
{
	if (!IsRuntimeEnabled())
		return false;

	auto options = SnapshotCaptureOptions();
	options.requestId = std::move(a_requestId);
	options.parentRequestId = std::move(a_parentRequestId);
	options.sequenceOrdinal = a_sequenceOrdinal;

	const auto source = a_effectiveDescriptor.value("source", nlohmann::json::object());
	const auto sourceKind = source.value("kind", std::string("desktop_mirror"));
	const auto fallback = source.value("fallback", std::string("reject"));
	options.allowDesktopFallback = fallback == "desktop_mirror";
	const auto outputs = a_effectiveDescriptor.value("outputs", nlohmann::json::array());
	if (outputs.empty())
		return false;
	options.applyCrop = false;
	const auto destination = a_effectiveDescriptor.value("destination", nlohmann::json::object());
	const auto destinationPolicy = destination.value("policy", std::string("settings_default"));
	if (destination.contains("resolvedDirectory") && destination["resolvedDirectory"].is_string())
		options.screenshotPath = destination["resolvedDirectory"].get<std::string>();
	else if (destinationPolicy != "settings_default")
		options.screenshotPath = destination.value("directory", options.screenshotPath);
	std::string baseName;
	if (destination.contains("baseName") && destination["baseName"].is_string())
		baseName = destination["baseName"].get<std::string>();
	if (baseName.empty()) {
		baseName = BuildScreenshotPath(options.screenshotPath, true).stem().string();
		std::string shortRequestId;
		for (char c : options.requestId) {
			if (c == '-')
				continue;
			shortRequestId.push_back(c);
			if (shortRequestId.size() == 8)
				break;
		}
		baseName += '_' + shortRequestId;
	}
	const bool clipboard = a_effectiveDescriptor.value("clipboard", std::string("none")) == "file_reference";
	bool requiresBothEyes = outputs.size() > 1;
	bool requiresStereoGeometry = false;
	for (const auto& output : outputs) {
		OutputPlan plan;
		const auto view = output.value("view", std::string("source_native"));
		if (view == "left_eye")
			plan.view = OutputView::LeftEye;
		else if (view == "right_eye")
			plan.view = OutputView::RightEye;
		else if (view == "side_by_side")
			plan.view = OutputView::SideBySide;
		else if (view == "framed_left")
			plan.view = OutputView::FramedLeft;
		else if (view == "framed_right")
			plan.view = OutputView::FramedRight;
		else if (view == "framed_combined")
			plan.view = OutputView::FramedCombined;
		else
			plan.view = OutputView::SourceNative;
		requiresBothEyes = requiresBothEyes || plan.view == OutputView::SourceNative ||
		                   plan.view == OutputView::SideBySide || plan.view == OutputView::FramedCombined;
		requiresStereoGeometry = requiresStereoGeometry || plan.view == OutputView::FramedCombined;
		if (output.contains("crop") && output["crop"].is_object()) {
			const auto& crop = output["crop"];
			plan.cropUV = { crop.value("x", 0.0f), crop.value("y", 0.0f), crop.value("width", 1.0f), crop.value("height", 1.0f) };
			plan.applyCrop = true;
		}
		plan.width = output.value("width", 0u);
		plan.height = output.value("height", 0u);
		plan.saveAsPng = output.value("encoding", nlohmann::json::object()).value("format", std::string("png")) == "png";
		plan.copyToClipboard = clipboard;
		plan.dominantEye = output.value("dominantEye", std::string("left")) == "right" ? vr::Eye_Right : vr::Eye_Left;
		plan.outputPath = ResolveToAbsoluteGamePath(std::filesystem::u8path(options.screenshotPath)) /
		                  (baseName + '_' + output.value("nameSuffix", view) + (plan.saveAsPng ? ".png" : ".bmp"));
		options.outputs.push_back(std::move(plan));
	}

	VRCaptureSource requestedSource = VRCaptureSource::DesktopMirror;
	if (sourceKind == "hmd_submission") {
		if (requiresBothEyes) {
			requestedSource = VRCaptureSource::HMDSubmission;
		} else {
			const auto onlyView = options.outputs.front().view;
			const bool right = onlyView == OutputView::RightEye || onlyView == OutputView::FramedRight;
			options.framedEye = right ? vr::Eye_Right : vr::Eye_Left;
			requestedSource = onlyView == OutputView::FramedLeft || onlyView == OutputView::FramedRight ?
			                      VRCaptureSource::FramedEye :
			                      VRCaptureSource::HMDEye;
		}
	}
	if (requiresStereoGeometry && !SnapshotStereoGeometry(options)) {
		logger::warn("Combined-eye projection data is unavailable; framed-combined output will use its dominant eye.");
		EnsureScreenshotApi();
		screenshotApi->OnSourceFallback(options.requestId, "combined-eye projection data unavailable; dominant eye used");
	}

	std::lock_guard lock(captureStateMutex);
	if (!IsRuntimeEnabled() || activeCapture.pending)
		return false;
	if (!TryReserveScreenshotSlot()) {
		logger::warn("Screenshot encoder is busy; rejecting API capture {}.", options.requestId);
		return false;
	}
	activeCapture.pending = true;
	activeCapture.ownsQueueSlot = true;
	activeCapture.options = std::move(options);
	activeCapture.source = requestedSource;
	activeCapture.sourceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

	if (globals::game::isVR && globals::state && globals::state->isLoadingMenuOpen) {
		if (activeCapture.source == VRCaptureSource::HMDSubmission && fallback == "desktop_mirror") {
			activeCapture.source = VRCaptureSource::DesktopMirror;
			EnsureScreenshotApi();
			screenshotApi->OnSourceFallback(
				activeCapture.options.requestId,
				"loading screen does not provide a coherent HMD submission",
				"desktop_mirror");
		} else if (IsSubmittedEyeCapture(activeCapture.source)) {
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			return false;
		}
	}

	capturePending.store(true, std::memory_order_release);
	sourceDeadlineCondition.notify_all();
	EnsureScreenshotApi();
	screenshotApi->OnSourceWaiting(
		activeCapture.options.requestId,
		ActualSourceKind(activeCapture.source));
	logger::debug("Screenshot request {} is waiting for {}", activeCapture.options.requestId, DescribeCaptureSource(activeCapture.source));
	return true;
}

bool ScreenshotFeature::CancelApiCapture(std::string_view a_requestId)
{
	std::lock_guard lock(captureStateMutex);
	if (!activeCapture.pending || activeCapture.options.requestId != a_requestId)
		return false;
	ClearActiveCapture(activeCapture);
	capturePending.store(false, std::memory_order_release);
	return true;
}

void ScreenshotFeature::SetEnabled(bool a_enabled)
{
	bool wasEnabled = false;
	bool cancelledPendingCapture = false;
	std::string cancelledRequestId;
	{
		std::lock_guard lock(captureStateMutex);
		wasEnabled = enabled.exchange(a_enabled, std::memory_order_acq_rel);
		if (!a_enabled) {
			// Close the Submit fast path before releasing partial textures and its
			// reserved encoder slot. Completed or queued encoder work remains committed.
			capturePending.store(false, std::memory_order_release);
			cancelledPendingCapture = activeCapture.pending;
			cancelledRequestId = activeCapture.options.requestId;
			ClearActiveCapture(activeCapture);
		}
	}

	if (wasEnabled != a_enabled) {
		logger::debug("Community Shaders screenshot capture {}", a_enabled ? "enabled" : "disabled");
	}
	if (cancelledPendingCapture) {
		logger::debug("Cancelled the pending screenshot capture after the feature was disabled");
		EnsureScreenshotApi();
		screenshotApi->OnSourceTerminal(cancelledRequestId, "cancelled", "feature_disabled");
	}
	if (!a_enabled) {
		EnsureScreenshotApi();
		screenshotApi->OnFeatureDisabled("feature_disabled");
	}
}

void ScreenshotFeature::SourceDeadlineLoop(std::stop_token a_stopToken)
{
	while (!a_stopToken.stop_requested()) {
		{
			std::unique_lock lock(sourceDeadlineMutex);
			sourceDeadlineCondition.wait_for(lock, a_stopToken, std::chrono::milliseconds(100), [] { return false; });
		}
		if (a_stopToken.stop_requested())
			break;
		std::string expiredRequestId;
		{
			std::lock_guard lock(captureStateMutex);
			if (activeCapture.pending &&
				activeCapture.sourceDeadline != std::chrono::steady_clock::time_point{} &&
				std::chrono::steady_clock::now() >= activeCapture.sourceDeadline) {
				expiredRequestId = activeCapture.options.requestId;
				ClearActiveCapture(activeCapture);
				capturePending.store(false, std::memory_order_release);
			}
		}
		if (!expiredRequestId.empty()) {
			std::shared_ptr<ScreenshotApi> api;
			{
				std::lock_guard lock(screenshotWorkerState->mutex);
				api = screenshotWorkerState->api;
			}
			if (api)
				api->OnSourceTerminal(expiredRequestId, "failed", "source_timeout");
		}
	}
}

bool ScreenshotFeature::HasPendingDesktopMirrorCapture() const
{
	if (!HasPendingCapture())
		return false;

	std::lock_guard lock(captureStateMutex);
	return activeCapture.pending &&
	       activeCapture.source == VRCaptureSource::DesktopMirror;
}

std::size_t ScreenshotFeature::GetOutstandingArtifactCount() const
{
	std::lock_guard lock(screenshotWorkerState->mutex);
	return screenshotWorkerState->outstandingCount;
}

std::string ScreenshotFeature::GetActiveCaptureRequestId() const
{
	std::lock_guard lock(captureStateMutex);
	return activeCapture.pending ? activeCapture.options.requestId : std::string{};
}

bool ScreenshotFeature::TryReserveScreenshotSlot()
{
	std::lock_guard queueLock(screenshotWorkerState->mutex);
	if (!screenshotWorkerState->accepting ||
		screenshotWorkerState->outstandingCount >= kMaxOutstandingScreenshots) {
		return false;
	}
	++screenshotWorkerState->outstandingCount;
	return true;
}

void ScreenshotFeature::ReleaseScreenshotSlot()
{
	ReleaseScreenshotSlot(screenshotWorkerState);
}

void ScreenshotFeature::ReleaseScreenshotSlot(const std::shared_ptr<ScreenshotWorkerState>& a_state)
{
	std::lock_guard queueLock(a_state->mutex);
	if (a_state->outstandingCount == 0) {
		logger::error("Screenshot queue-slot accounting underflow was prevented.");
		return;
	}
	--a_state->outstandingCount;
	a_state->condition.notify_all();
}

bool ScreenshotFeature::EnsureReadbackContextProtection(ID3D11DeviceContext* a_context)
{
	winrt::com_ptr<REX::W32::ID3D11Multithread> multithread;
	if (!a_context || FAILED(a_context->QueryInterface(multithread.put()))) {
		return false;
	}

	std::lock_guard queueLock(screenshotWorkerState->mutex);
	const auto existing = std::find_if(
		screenshotWorkerState->readbackProtections.begin(),
		screenshotWorkerState->readbackProtections.end(),
		[a_context](const ReadbackContextProtection& protection) {
			return protection.context.get() == a_context;
		});
	if (existing != screenshotWorkerState->readbackProtections.end()) {
		multithread->SetMultithreadProtected(TRUE);
		return true;
	}

	try {
		ReadbackContextProtection protection;
		protection.context.copy_from(a_context);
		screenshotWorkerState->readbackProtections.push_back(std::move(protection));
	} catch (const std::exception& e) {
		logger::error("Failed to track screenshot readback protection: {}", e.what());
		return false;
	} catch (...) {
		logger::error("Failed to track screenshot readback protection.");
		return false;
	}

	const BOOL wasProtected = multithread->SetMultithreadProtected(TRUE);
	screenshotWorkerState->readbackProtections.back().restoreToUnprotected = wasProtected == FALSE;
	screenshotWorkerState->restoreReadbackProtection = true;
	return true;
}

void ScreenshotFeature::RestoreReadbackContextProtectionIfIdle()
{
	RestoreReadbackContextProtectionIfIdle(screenshotWorkerState);
}

void ScreenshotFeature::RestoreReadbackContextProtectionIfIdle(const std::shared_ptr<ScreenshotWorkerState>& a_state)
{
	std::lock_guard queueLock(a_state->mutex);
	if (!a_state->restoreReadbackProtection || a_state->outstandingCount != 0) {
		return;
	}

	for (const auto& protection : a_state->readbackProtections) {
		if (!protection.restoreToUnprotected || !protection.context) {
			continue;
		}
		winrt::com_ptr<REX::W32::ID3D11Multithread> multithread;
		if (SUCCEEDED(protection.context->QueryInterface(multithread.put()))) {
			multithread->SetMultithreadProtected(FALSE);
		}
	}
	a_state->readbackProtections.clear();
	a_state->restoreReadbackProtection = false;
}

bool ScreenshotFeature::QueueScreenshot(PendingScreenshot&& screenshot)
{
	if (!screenshot.ownsQueueSlot) {
		logger::error("Screenshot was queued without a reserved encoder slot.");
		return false;
	}

	std::lock_guard lifecycleLock(screenshotWorkerLifecycleMutex);

	{
		std::lock_guard queueLock(screenshotWorkerState->mutex);
		if (!screenshotWorkerState->accepting) {
			if (screenshotWorkerState->outstandingCount > 0)
				--screenshotWorkerState->outstandingCount;
			return false;
		}
	}

	if (CSX::ScreenshotPolicy::CanStartWorker(true, screenshotWorker.joinable())) {
		try {
			screenshotWorker = std::thread(&ScreenshotFeature::ScreenshotWorkerLoop, screenshotWorkerState);
		} catch (const std::exception& e) {
			logger::error("Failed to start screenshot worker: {}", e.what());
			screenshot = {};
			ReleaseScreenshotSlot();
			return false;
		} catch (...) {
			logger::error("Failed to start screenshot worker.");
			screenshot = {};
			ReleaseScreenshotSlot();
			return false;
		}
	}

	const auto queuedRequestId = screenshot.requestId;
	const auto queuedPath = screenshot.outputPath;
	{
		std::lock_guard queueLock(screenshotWorkerState->mutex);
		try {
			screenshotWorkerState->queue.push(std::move(screenshot));
		} catch (const std::exception& e) {
			logger::error("Failed to enqueue screenshot: {}", e.what());
			screenshot = {};
			if (screenshotWorkerState->outstandingCount > 0) {
				--screenshotWorkerState->outstandingCount;
			}
			return false;
		} catch (...) {
			logger::error("Failed to enqueue screenshot.");
			screenshot = {};
			if (screenshotWorkerState->outstandingCount > 0) {
				--screenshotWorkerState->outstandingCount;
			}
			return false;
		}
	}
	screenshotWorkerState->condition.notify_one();
	if (!queuedRequestId.empty()) {
		EnsureScreenshotApi();
		screenshotApi->OnArtifactQueued(queuedRequestId, queuedPath);
	}
	return true;
}

void ScreenshotFeature::StopWorkerThread()
{
	std::lock_guard lifecycleLock(screenshotWorkerLifecycleMutex);
	{
		std::lock_guard queueLock(screenshotWorkerState->mutex);
		screenshotWorkerState->notifyAllowed.store(false, std::memory_order_release);
		screenshotWorkerState->accepting = false;
		screenshotWorkerState->stopRequested = true;
	}
	screenshotWorkerState->condition.notify_all();

	if (!screenshotWorker.joinable())
		return;
	bool exited = false;
	{
		std::unique_lock queueLock(screenshotWorkerState->mutex);
		exited = screenshotWorkerState->condition.wait_for(queueLock, std::chrono::seconds(5), [this] {
			return screenshotWorkerState->exited;
		});
	}
	if (exited) {
		screenshotWorker.join();
	} else {
		logger::error("Screenshot encoder worker did not stop within five seconds; preserving its isolated work state until it exits.");
		screenshotWorker.detach();
	}
}

void ScreenshotFeature::ScreenshotWorkerLoop(std::shared_ptr<ScreenshotWorkerState> a_state)
{
	const auto screenshotApi = a_state->api;
	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool uninitializeCom = SUCCEEDED(comResult);
	if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
		logger::warn("Screenshot worker COM initialization failed: 0x{:08X}", static_cast<uint32_t>(comResult));
	}
	auto reportFailure = [a_state](std::string_view message) {
		logger::error("{}", message);
		if (a_state->notifyAllowed.load(std::memory_order_acquire))
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
	};

	while (true) {
		bool ownsQueueSlot = false;
		const SKSE::stl::scope_exit finishScreenshot([a_state, &ownsQueueSlot]() noexcept {
			if (ownsQueueSlot) {
				ReleaseScreenshotSlot(a_state);
			}
		});
		PendingScreenshot screenshot;
		{
			std::unique_lock queueLock(a_state->mutex);
			a_state->condition.wait(queueLock, [&] {
				return !a_state->queue.empty() || a_state->stopRequested;
			});

			if (a_state->stopRequested && a_state->queue.empty()) {
				break;
			}

			screenshot = std::move(a_state->queue.front());
			a_state->queue.pop();
		}
		ownsQueueSlot = screenshot.ownsQueueSlot;
		uint32_t reportedArtifacts = 0;
		const SKSE::stl::scope_exit reportUnclassifiedFailure([screenshotApi, &screenshot, &reportedArtifacts]() noexcept {
			const auto expected = std::max<std::size_t>(1, screenshot.outputs.size());
			while (reportedArtifacts < expected && !screenshot.requestId.empty() && screenshotApi) {
				const auto& path = screenshot.outputs.empty() ?
				                       screenshot.outputPath :
				                       screenshot.outputs[reportedArtifacts].outputPath;
				screenshotApi->OnArtifactTerminal(screenshot.requestId, false, path, "capture worker did not commit an artifact");
				++reportedArtifacts;
			}
		});
		if (!screenshot.requestId.empty() && screenshotApi)
			screenshotApi->OnArtifactEncoding(screenshot.requestId);

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
			for (uint32_t index = 1; index < screenshot.planeCount; ++index) {
				if (planeImages[index]->format != planeImages[0]->format ||
					screenshot.planes[index].colorSpace != screenshot.planes[0].colorSpace ||
					screenshot.planes[index].tonemapSceneHdr != screenshot.planes[0].tonemapSceneHdr) {
					planeFailure = true;
					break;
				}
			}
			if (planeFailure) {
				reportFailure("Screenshot planes used incompatible image contracts.");
				continue;
			}

			if (!screenshot.outputs.empty()) {
				auto outputViewName = [](OutputView view) -> std::string_view {
					switch (view) {
					case OutputView::LeftEye:
						return "left_eye";
					case OutputView::RightEye:
						return "right_eye";
					case OutputView::SideBySide:
						return "side_by_side";
					case OutputView::FramedLeft:
						return "framed_left";
					case OutputView::FramedRight:
						return "framed_right";
					case OutputView::FramedCombined:
						return "framed_combined";
					case OutputView::SourceNative:
					default:
						return "source_native";
					}
				};
				auto getPlane = [&](std::size_t a_eye) -> std::pair<DirectX::ScratchImage*, const DirectX::Image*> {
					const std::size_t index = screenshot.planeCount > 1 ? std::min(a_eye, static_cast<std::size_t>(screenshot.planeCount - 1)) : 0u;
					const auto& plane = screenshot.planes[index];
					auto* scratch = plane.flipHorizontal || plane.flipVertical ? &orientedPlanes[index] : &mappedPlanes[index];
					return { scratch, planeImages[index] };
				};
				auto composeSideBySide = [&](DirectX::ScratchImage& a_composed) -> const DirectX::Image* {
					if (screenshot.planeCount == 1)
						return planeImages[0];
					const auto format = planeImages[0]->format;
					const uint32_t slotWidth = std::max(static_cast<uint32_t>(planeImages[0]->width), static_cast<uint32_t>(planeImages[1]->width));
					const uint32_t height = std::max(static_cast<uint32_t>(planeImages[0]->height), static_cast<uint32_t>(planeImages[1]->height));
					if (planeImages[1]->format != format || FAILED(a_composed.Initialize2D(format, slotWidth * 2u, height, 1, 1)))
						return nullptr;
					auto* destination = a_composed.GetImage(0, 0, 0);
					if (!destination)
						return nullptr;
					std::memset(a_composed.GetPixels(), 0, a_composed.GetPixelsSize());
					for (std::size_t eye = 0; eye < 2; ++eye) {
						const DirectX::Rect sourceRect(0, 0, planeImages[eye]->width, planeImages[eye]->height);
						if (FAILED(DirectX::CopyRectangle(*planeImages[eye], sourceRect, *destination, DirectX::TEX_FILTER_DEFAULT, eye * slotWidth, 0)))
							return nullptr;
					}
					return destination;
				};

				for (auto& output : screenshot.outputs) {
					try {
						const auto requestedView = outputViewName(output.view);
						if (screenshot.desktopSource && output.view != OutputView::SourceNative)
							throw std::runtime_error("requested HMD view is unavailable after desktop fallback");
						DirectX::ScratchImage sideBySide;
						DirectX::ScratchImage stereoComposite;
						DirectX::ScratchImage* sourceScratch = nullptr;
						const DirectX::Image* sourceImage = nullptr;
						vr::EColorSpace colourSpace = screenshot.planes[0].colorSpace;
						bool tonemapSceneHdr = screenshot.planes[0].tonemapSceneHdr;
						bool needsFraming = false;
						bool dominantEyeFallback = false;
						switch (output.view) {
						case OutputView::LeftEye:
						case OutputView::FramedLeft:
							std::tie(sourceScratch, sourceImage) = getPlane(0);
							needsFraming = output.view == OutputView::FramedLeft;
							break;
						case OutputView::RightEye:
						case OutputView::FramedRight:
							std::tie(sourceScratch, sourceImage) = getPlane(1);
							needsFraming = output.view == OutputView::FramedRight;
							break;
						case OutputView::FramedCombined:
							if (screenshot.planeCount == 2 && screenshot.stereoProjectionValid &&
								ComposeFramedStereo(
									planeImages,
									screenshot.eyeProjectionTangents,
									screenshot.eyeToHeadTransforms,
									screenshot.hiddenAreaMeshes,
									output.dominantEye,
									colourSpace,
									stereoComposite)) {
								sourceScratch = &stereoComposite;
								sourceImage = stereoComposite.GetImage(0, 0, 0);
								colourSpace = vr::ColorSpace_Linear;
								tonemapSceneHdr = false;
							} else {
								std::tie(sourceScratch, sourceImage) = getPlane(output.dominantEye == vr::Eye_Right ? 1u : 0u);
								needsFraming = true;
								dominantEyeFallback = true;
								if (screenshotApi)
									screenshotApi->OnSourceFallback(screenshot.requestId, "framed-combined composition failed; dominant eye used");
							}
							break;
						case OutputView::SourceNative:
						case OutputView::SideBySide:
						default:
							sourceImage = composeSideBySide(sideBySide);
							sourceScratch = screenshot.planeCount == 1 ? getPlane(0).first : &sideBySide;
							break;
						}
						if (!sourceScratch || !sourceImage)
							throw std::runtime_error("failed to compose requested screenshot view");
						const auto actualView = CSX::ScreenshotPolicy::ResolveActualOutputView(
							requestedView,
							screenshot.desktopSource,
							dominantEyeFallback,
							output.dominantEye == vr::Eye_Right);
						if (actualView.empty())
							throw std::runtime_error("requested output view is incompatible with the resolved source");

						DirectX::ScratchImage cropped;
						DirectX::ScratchImage* imageToSave = sourceScratch;
						if (output.applyCrop) {
							const auto crop = Util::Subrect::ResolvePixelRegion(output.cropUV, static_cast<uint32_t>(sourceImage->width), static_cast<uint32_t>(sourceImage->height));
							if (FAILED(cropped.Initialize2D(sourceImage->format, crop.w, crop.h, 1, 1)))
								throw std::runtime_error("failed to allocate output crop");
							auto* croppedImage = cropped.GetImage(0, 0, 0);
							const DirectX::Rect cropRect(crop.x, crop.y, crop.w, crop.h);
							if (!croppedImage || FAILED(DirectX::CopyRectangle(*sourceImage, cropRect, *croppedImage, DirectX::TEX_FILTER_DEFAULT, 0, 0)))
								throw std::runtime_error("failed to crop requested screenshot view");
							imageToSave = &cropped;
							sourceImage = croppedImage;
						}

						DirectX::ScratchImage framingCrop;
						DirectX::ScratchImage framed;
						if (needsFraming) {
							if (!CenterCropAndResize(
									*sourceImage,
									output.width ? output.width : kFramedEyeOutputWidth,
									output.height ? output.height : kFramedEyeOutputHeight,
									colourSpace,
									framingCrop,
									framed))
								throw std::runtime_error("failed to frame requested screenshot view");
							imageToSave = &framed;
						}

						Util::FileHelpers::EnsureDirectoryExists(output.outputPath.parent_path());
						output.outputPath = MakeCollisionSafePath(std::move(output.outputPath));
						if (!SaveSdrScreenshot(*imageToSave, output.outputPath, output.saveAsPng, colourSpace, tonemapSceneHdr))
							throw std::runtime_error("failed to save requested screenshot output");
						CopySavedPathToClipboard(output.copyToClipboard, output.outputPath);
						logger::info("Saved screenshot output to {}", output.outputPath.string());
						if (screenshotApi) {
							const auto* savedImage = imageToSave->GetImage(0, 0, 0);
							screenshotApi->OnArtifactTerminal(
								screenshot.requestId,
								true,
								output.outputPath,
								{},
								{
									{ "view", actualView },
									{ "width", savedImage ? savedImage->width : 0 },
									{ "height", savedImage ? savedImage->height : 0 },
									{ "format", output.saveAsPng ? "png" : "bmp" },
									{ "colourContract", "sdr_srgb" },
								});
						}
						++reportedArtifacts;
					} catch (const std::exception& e) {
						reportFailure(e.what());
						if (screenshotApi)
							screenshotApi->OnArtifactTerminal(screenshot.requestId, false, output.outputPath, e.what());
						++reportedArtifacts;
					}
				}
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
			screenshot.outputPath = MakeCollisionSafePath(std::move(screenshot.outputPath));
			const bool saveOk = SaveSdrScreenshot(
				*imageToSave,
				screenshot.outputPath,
				screenshot.saveAsPng,
				combinedColorSpace,
				combinedTonemapSceneHdr);

			if (!saveOk) {
				reportFailure("Failed to save screenshot.");
				if (!screenshot.requestId.empty() && screenshotApi) {
					screenshotApi->OnArtifactTerminal(screenshot.requestId, false, screenshot.outputPath, "failed to save screenshot");
					++reportedArtifacts;
				}
			} else {
				CopySavedPathToClipboard(screenshot.copyToClipboard, screenshot.outputPath);
				logger::info("Saved screenshot to {}", screenshot.outputPath.string());
				if (!screenshot.requestId.empty() && screenshotApi) {
					const auto* savedImage = imageToSave->GetImage(0, 0, 0);
					screenshotApi->OnArtifactTerminal(
						screenshot.requestId,
						true,
						screenshot.outputPath,
						{},
						{
							{ "view", "source_native" },
							{ "width", savedImage ? savedImage->width : 0 },
							{ "height", savedImage ? savedImage->height : 0 },
							{ "format", screenshot.saveAsPng ? "png" : "bmp" },
							{ "colourContract", "sdr_srgb" },
						});
					++reportedArtifacts;
				}
				if (a_state->notifyAllowed.load(std::memory_order_acquire)) {
					ShowInGameNotification(std::format("Screenshot saved: {}",
						screenshot.outputPath.filename().string()));
				}
			}
		} catch (const std::exception& e) {
			logger::error("Screenshot worker failed with an exception: {}", e.what());
			if (a_state->notifyAllowed.load(std::memory_order_acquire))
				ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
		} catch (...) {
			reportFailure("Screenshot worker failed with an unknown exception.");
		}
	}
	RestoreReadbackContextProtectionIfIdle(a_state);
	if (uninitializeCom)
		CoUninitialize();
	{
		std::lock_guard lock(a_state->mutex);
		a_state->exited = true;
	}
	a_state->condition.notify_all();
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
		screenshot.desktopSource = true;
		screenshot.cropUV = a_options.cropUV;
		screenshot.applyCrop = false;
		screenshot.saveAsPng = a_options.saveAsPng;
		screenshot.copyToClipboard = a_options.copyToClipboard;
		screenshot.ownsQueueSlot = true;
		screenshot.requestId = a_options.requestId;
		screenshot.parentRequestId = a_options.parentRequestId;
		screenshot.sequenceOrdinal = a_options.sequenceOrdinal;
		screenshot.outputs = a_options.outputs;
		screenshot.outputPath = !screenshot.outputs.empty() ? screenshot.outputs.front().outputPath :
		                                                      (a_options.explicitOutputPath.empty() ?
																	  BuildScreenshotPath(a_options.screenshotPath, screenshot.saveAsPng) :
																	  a_options.explicitOutputPath);
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
			if (framedEyeCapture && activeCapture.options.outputs.empty()) {
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
		completedScreenshot.requestId = activeCapture.options.requestId;
		completedScreenshot.parentRequestId = activeCapture.options.parentRequestId;
		completedScreenshot.sequenceOrdinal = activeCapture.options.sequenceOrdinal;
		completedScreenshot.outputs = std::move(activeCapture.options.outputs);
		if (!completedScreenshot.outputs.empty()) {
			completedScreenshot.eyeProjectionTangents = activeCapture.options.eyeProjectionTangents;
			completedScreenshot.eyeToHeadTransforms = activeCapture.options.eyeToHeadTransforms;
			completedScreenshot.hiddenAreaMeshes = std::move(activeCapture.options.hiddenAreaMeshes);
			completedScreenshot.stereoProjectionValid = activeCapture.options.stereoProjectionValid;
		}
		try {
			completedScreenshot.outputPath = !completedScreenshot.outputs.empty() ?
			                                     completedScreenshot.outputs.front().outputPath :
			                                     (activeCapture.options.explicitOutputPath.empty() ?
														 BuildScreenshotPath(
															 activeCapture.options.screenshotPath,
															 completedScreenshot.saveAsPng) :
														 activeCapture.options.explicitOutputPath);
		} catch (const std::exception& e) {
			logger::error("Failed to prepare the VR screenshot output path: {}", e.what());
			const auto failedRequestId = activeCapture.options.requestId;
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			if (screenshotApi)
				screenshotApi->OnSourceTerminal(failedRequestId, "failed", "unsafe_output_path");
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
			return;
		} catch (...) {
			logger::error("Failed to prepare the VR screenshot output path.");
			const auto failedRequestId = activeCapture.options.requestId;
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			if (screenshotApi)
				screenshotApi->OnSourceTerminal(failedRequestId, "failed", "unsafe_output_path");
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
			return;
		}
		completedScreenshot.ownsQueueSlot = std::exchange(activeCapture.ownsQueueSlot, false);
		completedSource = activeCapture.source;
		ClearActiveCapture(activeCapture);
		capturePending.store(false, std::memory_order_release);
		completed = true;
	}

	if (completed) {
		const auto completedRequestId = completedScreenshot.requestId;
		switch (completedSource) {
		case VRCaptureSource::FramedEye:
			logger::debug("Capturing one accepted OpenVR eye at 2560 x 1440");
			break;
		case VRCaptureSource::HMDEye:
			logger::debug("Capturing one accepted OpenVR eye at source resolution");
			break;
		case VRCaptureSource::FramedStereo:
			logger::debug("Capturing a combined accepted OpenVR eye pair at 2560 x 1440");
			break;
		case VRCaptureSource::HMDSubmission:
		default:
			logger::debug("Capturing the accepted OpenVR HMD eye pair");
			break;
		}
		if (!QueueScreenshot(std::move(completedScreenshot))) {
			if (!completedRequestId.empty() && screenshotApi)
				screenshotApi->OnSourceTerminal(completedRequestId, "failed", "artifact_queue_failed");
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
		}
	}
}

void ScreenshotFeature::OnBeforePresent(IDXGISwapChain* a_swapChain)
{
	RestoreReadbackContextProtectionIfIdle();
	EnsureScreenshotApi();
	screenshotApi->Tick(*this, globals::state ? globals::state->frameCount : 0u);
	if (!HasPendingCapture()) {
		return;
	}

	std::lock_guard lock(captureStateMutex);
	if (!IsRuntimeEnabled() || !activeCapture.pending) {
		return;
	}

	++activeCapture.presentsWaited;
	if (activeCapture.source == VRCaptureSource::HMDSubmission) {
		if (activeCapture.presentsWaited >= kCaptureTimeoutPresents) {
			if (activeCapture.options.allowDesktopFallback) {
				FallBackToDesktopCapture(activeCapture, "no coherent accepted eye pair arrived before the timeout");
			} else {
				const auto failedRequestId = activeCapture.options.requestId;
				ClearActiveCapture(activeCapture);
				capturePending.store(false, std::memory_order_release);
				if (!failedRequestId.empty())
					screenshotApi->OnSourceTerminal(failedRequestId, "failed", "source_timeout");
			}
		}
		return;
	}
	if (IsFramedCapture(activeCapture.source)) {
		if (activeCapture.presentsWaited >= kCaptureTimeoutPresents) {
			logger::warn("Framed-view screenshot capture timed out before the required eye submission arrived.");
			const auto failedRequestId = activeCapture.options.requestId;
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			if (!failedRequestId.empty())
				screenshotApi->OnSourceTerminal(failedRequestId, "failed", "source_timeout");
			ShowInGameNotification("Framed-view screenshot failed - missing eye submission");
		}
		return;
	}

	const auto requestId = activeCapture.options.requestId;
	const bool queued = QueueDesktopCapture(
		a_swapChain,
		activeCapture.options,
		std::exchange(activeCapture.ownsQueueSlot, false));
	ClearActiveCapture(activeCapture);
	capturePending.store(false, std::memory_order_release);
	if (!queued) {
		if (!requestId.empty())
			screenshotApi->OnSourceTerminal(requestId, "failed", "desktop_stage_failed");
		ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
	}
}

void ScreenshotFeature::DrawPostCaptureIndicator()
{
	EnsureScreenshotApi();
	const bool recording = screenshotApi->IsSequenceRecording();
	if (globals::game::isVR) {
		globals::features::vr.SubmitCaptureIndicator(recording);
		return;
	}
	if (!recording)
		return;

	ImGuiContext* context = ImGui::GetCurrentContext();
	if (!context)
		return;
	ImGuiIO& io = ImGui::GetIO();
	if (!io.Fonts || !io.Fonts->IsBuilt() || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
		return;

	ImDrawList drawList(ImGui::GetDrawListSharedData());
	drawList._OwnerName = "CSXCaptureIndicator";
	drawList._ResetForNewFrame();
	drawList.PushTextureID(io.Fonts->TexID);
	drawList.PushClipRectFullScreen();
	const float radius = std::clamp(io.DisplaySize.y * 0.009f, 6.0f, 12.0f);
	const ImVec2 centre(io.DisplaySize.x * 0.375f, io.DisplaySize.y * 0.5f);
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
