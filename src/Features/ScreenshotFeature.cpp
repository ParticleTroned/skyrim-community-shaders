// Screenshot Feature
// Non-blocking screenshot tool. GPU copy runs on the
// render thread; encoding and disk I/O run on a dedicated worker thread so
// capture does not stall the frame.

#include "Features/ScreenshotFeature.h"
#include "Features/ScreenshotApi.h"
#include "Features/ScreenshotCapturePolicy.h"
#include "Features/ScreenshotNativeImage.h"
#include "Features/ScreenshotNeuralDiagnostics.h"
#include "Api/ServiceFoundation.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/ComIdentity.h"
#include "Utils/D3DContextProtection.h"
#include "Utils/RendererContextAccess.h"
#include "Utils/StringUtils.h"
#include "Utils/Finite.h"

#include <PCH.h>

#include "Features/HDRDisplay.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/SettingsPage.h"
#include "Utils/FileSystem.h"
#include "Utils/WinApi.h"

#define I18N_KEY_PREFIX "feature.screenshot."

#include <DirectXTex.h>
#pragma warning(push)
#pragma warning(disable: 4244)  // double->float conversion in third-party header
#include <sk_hdr_png.hpp>
#pragma warning(pop)

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <imgui.h>
#include <malloc.h>
#include <deque>
#include <optional>
#include <vector>
#include <utility>
#include <condition_variable>

namespace
{
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
		bool uiExcluded = false;
	};

	HRESULT PopulateScratchImageFromStagingTexture(
		ID3D11DeviceContext* context, ID3D11Texture2D* stagingTexture, ID3D11Query* completed,
		DXGI_FORMAT format, uint32_t width, uint32_t height, DirectX::ScratchImage& image)
	{
		if (image.GetImageCount() == 0) {
			const auto result = image.Initialize2D(format, width, height, 1, 1);
			if (FAILED(result)) return result;
		}
		auto* rendererLock = Util::GetRendererContextLock(globals::game::renderer, context);
		if (!rendererLock) return DXGI_ERROR_DEVICE_REMOVED;
		{
			const Util::RendererOwnership owner(rendererLock);
			if (!owner) return DXGI_ERROR_WAS_STILL_DRAWING;
			const auto result = context->GetData(completed, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (result == S_FALSE) return DXGI_ERROR_WAS_STILL_DRAWING;
			if (FAILED(result)) return result;
		}
		return Util::TryReadbackWithRendererOwnership(context, stagingTexture, rendererLock, [&](const D3D11_MAPPED_SUBRESOURCE& mapped) {
			const auto* target = image.GetImage(0, 0, 0);
			if (!mapped.pData || !target || mapped.RowPitch < target->rowPitch ||
				(mapped.DepthPitch && std::uint64_t(mapped.DepthPitch) < std::uint64_t(mapped.RowPitch) * height)) return E_FAIL;
			for (size_t row = 0; row < height; ++row)
				std::memcpy(image.GetPixels() + row * target->rowPitch,
					static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch, target->rowPitch);
			return S_OK;
		});
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

	// Tonemaps a linear RGB ScratchImage in-place: Reinhard c/(1+c), then gamma-2.2.
	void TonemapHdrToSrgb(DirectX::ScratchImage& image)
	{
		using namespace DirectX;
		DirectX::ScratchImage tonemapped;
		const HRESULT hr = TransformImage(
			image.GetImages(),
			image.GetImageCount(),
			image.GetMetadata(),
			[](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t /*y*/) {
				const XMVECTOR one = XMVectorSplatOne();
				const XMVECTOR invGamma = XMVectorReplicate(1.0f / 2.2f);
				for (size_t i = 0; i < width; ++i) {
					XMVECTOR c = XMVectorMax(inPixels[i], XMVectorZero());
					const XMVECTOR rgb = XMVectorDivide(c, XMVectorAdd(c, one));
					const XMVECTOR gammaCorrected = XMVectorPow(rgb, invGamma);
					outPixels[i] = XMVectorSelect(gammaCorrected, c, g_XMSelect1110);
				}
			},
			tonemapped);
		if (SUCCEEDED(hr)) {
			image = std::move(tonemapped);
		}
	}

	const DirectX::Image* PrepareBmpImage(DirectX::ScratchImage& sourceImage, DirectX::ScratchImage& convertedImage)
	{
		if (sourceImage.GetMetadata().format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
			TonemapHdrToSrgb(sourceImage);
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

	// Game-root-relative paths (e.g. "Screenshots") must be absolute for CF_HDROP / Discord.
	std::filesystem::path ResolveToAbsoluteGamePath(const std::filesystem::path& path)
	{
		if (path.is_absolute()) {
			return path;
		}
		wchar_t buffer[MAX_PATH]{};
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH) {
			return std::filesystem::path(buffer).parent_path() / path;
		}
		std::error_code ec;
		return std::filesystem::absolute(path, ec);
	}

	bool CopyFilePathToClipboardHDrop(const std::wstring& absolutePath)
	{
		if (absolutePath.empty()) {
			return false;
		}

		const size_t pathChars = absolutePath.size();
		const size_t bytes = sizeof(DROPFILES) + (pathChars + 2) * sizeof(wchar_t);
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
		if (!hMem) {
			return false;
		}

		auto* drop = static_cast<DROPFILES*>(GlobalLock(hMem));
		if (!drop) {
			GlobalFree(hMem);
			return false;
		}

		drop->pFiles = sizeof(DROPFILES);
		drop->fWide = TRUE;

		auto* files = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
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

	void RunOnMainThread(std::function<void()> fn)
	{
		if (auto* taskInterface = SKSE::GetTaskInterface()) {
			taskInterface->AddTask(std::move(fn));
		} else {
			fn();
		}
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

	// Returns the texture that was presented to the display (post-ApplyHDR).
	ID3D11Texture2D* ResolveDisplayedBackBuffer(winrt::com_ptr<ID3D11Texture2D>& holder)
	{
		auto& upscaling = globals::features::upscaling;
		if (upscaling.d3d12SwapChainActive &&
			upscaling.dx12SwapChain.swapChainBufferWrapped &&
			upscaling.dx12SwapChain.swapChainBufferWrapped->resource11) {
			holder.copy_from(upscaling.dx12SwapChain.swapChainBufferWrapped->resource11.get());
			return holder.get();
		}

		if (!globals::d3d::swapChain) {
			return nullptr;
		}

		winrt::com_ptr<ID3D11Texture2D> backBuffer;
		if (FAILED(globals::d3d::swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void()))) {
			return nullptr;
		}
		holder = std::move(backBuffer);
		return holder.get();
	}

	bool IsFlatHdrScreenshotCapture()
	{
		return globals::features::hdrDisplay.loaded &&
		       globals::features::hdrDisplay.settings.enableHDR;
	}

	// Picks the capture source:
	//   HDR + CS menu open -> clean HDR composite (no UI, no menu blur).
	//   HDR enabled        -> swap-chain back buffer after ApplyHDR (PQ HDR10 / PQ float).
	//   otherwise          -> kFRAMEBUFFER (tonemapped UNORM).
	// forCapture: post-blur screenshot uses the snapshot; pre-blur preview uses hdrTexture.
	CaptureSource SelectCaptureSource(winrt::com_ptr<ID3D11Texture2D>& holder, bool forCapture)
	{
		CaptureSource src;
		auto* renderer = globals::game::renderer;
		if (!renderer) {
			return src;
		}

		if (IsFlatHdrScreenshotCapture()) {
			// Recompose from the clean scene with no UI buffer.
			auto& hdr = globals::features::hdrDisplay;
			auto* menu = Menu::GetSingleton();
			if (menu && menu->IsEnabled && hdr.outputTexture && hdr.outputTexture->srv) {
				ID3D11ShaderResourceView* sceneSRV =
					(forCapture && hdr.IsCleanSceneCaptureFresh()) ? hdr.cleanSceneCapture->srv.get() :
																	 (hdr.hdrTexture ? hdr.hdrTexture->srv.get() : nullptr);
				if (sceneSRV) {
					if (ID3D11Texture2D* clean = hdr.ComposeCleanCapture(sceneSRV, /*sdrPreview=*/!forCapture)) {
						src.texture = clean;
						src.srv = hdr.outputTexture->srv.get();
						src.needsPreviewCache = false;
						src.description = "HDR clean composite (no UI, no menu blur)";
						src.uiExcluded = true;
						return src;
					}
				}
			}

			src.texture = ResolveDisplayedBackBuffer(holder);
			src.needsPreviewCache = true;
			src.description = "Swap chain back buffer (HDR display composite)";
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

	// Forces BlendEnable=FALSE and opaque alpha for the preview Image draw.
	// Paired with ImDrawCallback_ResetRenderState queued by Subrect::DrawEditor.
	void OpaquePreviewBlendCallback(const ImDrawList*, const ImDrawCmd*)
	{
		auto* menu = Menu::GetSingleton();
		const bool writeAlpha = IsFlatHdrScreenshotCapture() && menu && menu->IsEnabled;

		static winrt::com_ptr<ID3D11BlendState> rgbBlend;
		static winrt::com_ptr<ID3D11BlendState> rgbaBlend;
		auto& blend = writeAlpha ? rgbaBlend : rgbBlend;
		if (!blend) {
			D3D11_BLEND_DESC desc{};
			desc.RenderTarget[0].BlendEnable = FALSE;
			desc.RenderTarget[0].RenderTargetWriteMask =
				D3D11_COLOR_WRITE_ENABLE_RED |
				D3D11_COLOR_WRITE_ENABLE_GREEN |
				D3D11_COLOR_WRITE_ENABLE_BLUE |
				(writeAlpha ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0);
			if (SUCCEEDED(globals::d3d::device->CreateBlendState(&desc, blend.put())))
				Util::SetResourceName(blend.get(), writeAlpha ? "Screenshot::OpaqueRGBA" : "Screenshot::OpaqueRGB");
		}
		if (blend) {
			globals::d3d::context->OMSetBlendState(blend.get(), nullptr, 0xFFFFFFFF);
		}
	}


	struct HdrFormatInfo
	{
		DXGI_FORMAT dxgi;
		sk_hdr_png::format png;
		size_t bytesPerPixel;
	};

	constexpr HdrFormatInfo kHdrFormats[] = {
		{ DXGI_FORMAT_R10G10B10A2_UNORM, sk_hdr_png::format::r10g10b10a2_unorm, 4 },
		{ DXGI_FORMAT_R16G16B16A16_FLOAT, sk_hdr_png::format::r16g16b16a16_pq, 8 },
	};

	const HdrFormatInfo* LookupHdrFormat(DXGI_FORMAT format)
	{
		for (const auto& info : kHdrFormats) {
			if (info.dxgi == format) {
				return &info;
			}
		}
		return nullptr;
	}

	bool IsHdrCaptureFormat(DXGI_FORMAT format)
	{
		return LookupHdrFormat(format) != nullptr;
	}

	// sk_hdr_png requires 16-byte aligned pixel memory.
	bool CopyToAlignedPixelBuffer(
		const DirectX::Image& image,
		size_t bytesPerPixel,
		void*& outAligned,
		size_t& outByteSize)
	{
		if (bytesPerPixel == 0) {
			return false;
		}

		const size_t tightRowBytes = static_cast<size_t>(image.width) * bytesPerPixel;
		outByteSize = tightRowBytes * image.height;

		outAligned = _aligned_malloc(outByteSize, 16);
		if (!outAligned) {
			return false;
		}

		auto* dest = static_cast<uint8_t*>(outAligned);
		const auto* src = image.pixels;
		for (size_t row = 0; row < image.height; ++row) {
			memcpy(dest + row * tightRowBytes, src + row * image.rowPitch, tightRowBytes);
		}
		return true;
	}

	bool SaveHdrPng(
		const DirectX::ScratchImage& image,
		const std::filesystem::path& outputPath,
		int quantizationBits,
		DXGI_FORMAT format)
	{
		const DirectX::Image* firstImage = image.GetImage(0, 0, 0);
		const HdrFormatInfo* hdrInfo = firstImage ? LookupHdrFormat(format) : nullptr;
		if (!firstImage || !hdrInfo || firstImage->format != format) {
			return false;
		}

		void* alignedPixels = nullptr;
		size_t byteSize = 0;
		if (!CopyToAlignedPixelBuffer(*firstImage, hdrInfo->bytesPerPixel, alignedPixels, byteSize)) {
			return false;
		}

		const bool saved = sk_hdr_png::write_image_to_disk(
			outputPath.wstring().c_str(),
			static_cast<unsigned int>(firstImage->width),
			static_cast<unsigned int>(firstImage->height),
			alignedPixels,
			quantizationBits,
			hdrInfo->png,
			false);

		_aligned_free(alignedPixels);
		return saved;
	}

	CSX::ScreenshotStorage::CommittedArtifact SaveScreenshotAtomically(
		DirectX::ScratchImage& image, const std::filesystem::path& outputPath,
		DXGI_FORMAT format, int hdrPngBitDepth, bool hdr, bool png, bool strictNative)
	{
		std::filesystem::create_directories(outputPath.parent_path());
		const auto temporary = outputPath.parent_path() /
			(outputPath.stem().wstring() + L".writing-" + std::filesystem::path(CSX::Api::ServiceFoundation::NewId()).wstring() + outputPath.extension().wstring());
		if (hdr)
			return CSX::ScreenshotStorage::CommittedFile::EncodeAtomically(temporary, outputPath,
				[&](const auto& path) { return SaveHdrPng(image, path, hdrPngBitDepth, format); });
		const auto linear = DirectX::MakeLinear(image.GetMetadata().format);
		if (strictNative || linear == DXGI_FORMAT_R8G8B8A8_UNORM || linear == DXGI_FORMAT_B8G8R8A8_UNORM)
			CSX::Screenshot::PreserveNativeSdrBytes(image);
		StripAlphaForBmp(image);
		DirectX::ScratchImage converted;
		const auto* output = PrepareBmpImage(image, converted);
		if (!output) throw std::runtime_error("SDR conversion failed");
		DirectX::Blob encoded;
		if (FAILED(DirectX::SaveToWICMemory(*output, png ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_NONE,
				DirectX::GetWICCodec(png ? DirectX::WIC_CODEC_PNG : DirectX::WIC_CODEC_BMP), encoded)))
			throw std::runtime_error("SDR encoding failed");
		return CSX::ScreenshotStorage::CommittedFile::WriteAtomically(temporary, outputPath,
			encoded.GetBufferPointer(), encoded.GetBufferSize(), false);
	}

}

struct ScreenshotFeature::CaptureState
{
	struct Request
	{
		std::string requestId, parentRequestId;
		uint32_t ordinal = 0;
		json descriptor;
		std::chrono::steady_clock::time_point expiresAt;
	};
	struct Output
	{
		winrt::com_ptr<ID3D11Texture2D> staging;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0, height = 0;
		std::filesystem::path path;
		bool hdr = false, png = true, clipboard = false, strictNative = false;
		uint32_t hdrBits = 11;
		json actual;
	};
	struct Job
	{
		std::string requestId;
		std::vector<Output> outputs;
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::com_ptr<ID3D11Query> completed;
		uint64_t bytes = 0;
		bool notify = false;
	};
	std::mutex mutex;
	std::condition_variable condition;
	std::shared_ptr<Request> active;
	std::deque<Job> jobs;
	std::size_t outstanding = 0;
	uint64_t stagingBytes = 0;
	bool stopRequested = false, exited = false, deferEncoding = false;
};

ScreenshotFeature::ScreenshotFeature() : captureState(std::make_shared<CaptureState>()), screenshotApi(std::make_shared<ScreenshotApi>())
{
	screenshotWorker = std::thread(&ScreenshotFeature::ScreenshotWorkerLoop, captureState, screenshotApi);
	deadlineWorker = std::jthread(&ScreenshotFeature::DeadlineWorkerLoop, captureState, screenshotApi);
}

ScreenshotFeature::~ScreenshotFeature()
{
	deadlineWorker.request_stop();
	captureState->condition.notify_all();
	if (deadlineWorker.joinable()) deadlineWorker.join();
	screenshotApi->BeginShutdown("plugin_shutdown");
	std::string cancelled;
	{
		std::lock_guard lock(captureState->mutex);
		captureState->stopRequested = true;
		captureState->deferEncoding = false;
		if (captureState->active) {
			cancelled = captureState->active->requestId;
			captureState->active.reset();
			--captureState->outstanding;
		}
	}
	if (!cancelled.empty()) screenshotApi->OnSourceTerminal(cancelled, "cancelled", "plugin_shutdown");
	captureState->condition.notify_all();
	bool exited;
	{
		std::unique_lock lock(captureState->mutex);
		exited = captureState->condition.wait_for(lock, std::chrono::seconds(2), [&] { return captureState->exited; });
	}
	if (screenshotWorker.joinable()) {
		if (exited) screenshotWorker.join();
		else { logger::warn("Screenshot encoder is still draining; retaining its isolated state until completion."); screenshotWorker.detach(); }
	}
	(void)screenshotApi->DrainForShutdown(std::chrono::seconds(2));
}

void ScreenshotFeature::PostPostLoad() {}

void ScreenshotFeature::LoadSettings(json& settings)
{
	if (!settings.is_object()) return;
	const auto text = [&](const char* key, std::string& target) {
		if (auto it = settings.find(key); it != settings.end() && it->is_string() && it->get_ref<const std::string&>().size() <= 32767)
			target = it->get<std::string>();
	};
	const auto boolean = [&](const char* key, bool& target) {
		if (auto it = settings.find(key); it != settings.end() && it->is_boolean()) target = it->get<bool>();
	};
	const auto integer = [&](const char* key, uint32_t& target, uint32_t low, uint32_t high) {
		if (auto it = settings.find(key); it != settings.end() && it->is_number_integer()) {
			if (it->is_number_unsigned()) target = static_cast<uint32_t>(std::clamp(it->get<uint64_t>(), uint64_t(low), uint64_t(high)));
			else target = static_cast<uint32_t>(std::clamp(it->get<int64_t>(), int64_t(low), int64_t(high)));
		}
	};
	text("ScreenshotPath", screenshotPath); text("FrameCapturePath", frameCapturePath);
	boolean("ApplyCropToScreenshot", applyCropToScreenshot); boolean("SdrUsePng", sdrUsePng);
	boolean("FrameCaptureUsePng", frameCaptureUsePng); boolean("CopyToClipboard", copyToClipboard);
	integer("HdrPngBitDepth", hdrPngBitDepth, 7, 16);
	integer("SequenceFrameCount", sequenceDefaults.frameCount, 1, 10000);
	integer("SequenceIntervalFrames", sequenceDefaults.intervalFrames, 1, 1000000);
	if (auto it = settings.find("Enabled"); it != settings.end() && it->is_boolean()) SetEnabled(it->get<bool>());
	subrect.LoadSettings(settings);
}

void ScreenshotFeature::SaveSettings(json& settings)
{
	settings["Enabled"] = enabled.load();
	settings["ScreenshotPath"] = screenshotPath; settings["FrameCapturePath"] = frameCapturePath;
	settings["ApplyCropToScreenshot"] = applyCropToScreenshot; settings["HdrPngBitDepth"] = hdrPngBitDepth;
	settings["SdrUsePng"] = sdrUsePng; settings["FrameCaptureUsePng"] = frameCaptureUsePng;
	settings["CopyToClipboard"] = copyToClipboard;
	settings["SequenceFrameCount"] = sequenceDefaults.frameCount;
	settings["SequenceIntervalFrames"] = sequenceDefaults.intervalFrames;
	subrect.SaveSettings(settings);
}

void ScreenshotFeature::DrawSettingsEnabledControl()
{
	bool active = enabled.load();
	if (Util::Widgets::Checkbox(T(TKEY("enabled"), "Enable screenshot capture"), &active))
		SetEnabled(active);
}

namespace
{
	MenuUI::ActionFeedback CaptureRequestFeedback(const json& response, const char* success)
	{
		const auto errorText = [](const json& error) {
			const auto* fallback = T(TKEY("request_failed"), "Capture service could not complete this request.");
			if (error.is_object())
				return error.value("message", std::string(fallback));
			return error.is_string() ? error.get<std::string>() : std::string(fallback);
		};
		if (!response.value("ok", false) || !response.contains("result"))
			return { errorText(response.value("error", json{})), true };
		const auto& result = response.at("result");
		if (!result.value("valid", true))
			return { T(TKEY("invalid_folder"), "The capture folder is invalid. The previous folder was retained."), true };
		const auto state = result.value("state", std::string{});
		if (state == "failed" || state == "failed_partial" || state == "rejected" || state == "dropped")
			return { errorText(result.value("error", json{})), true };
		if (state == "completed_with_warnings")
			return { T(TKEY("sequence_warnings"), "Sequence completed with warnings. Review its manifest for skipped or failed frames.") };
		return { success };
	}
}

void ScreenshotFeature::DrawSettings()
{
	static MenuUI::ActionFeedback folderFeedback;
	static MenuUI::ActionFeedback captureFeedback;
	PollUiSequence(captureFeedback);
	MenuUI::SettingsPage page("Screenshot", {
												{ "source", T(TKEY("page.source"), "Source"), T(TKEY("page.source_help"), "Capture the displayed SE frame."), T(TKEY("page.source_summary"), "Desktop capture and display colour"), true, true, T(TKEY("page.capture_output"), "Choose capture and output") },
												{ "output", T(TKEY("page.output"), "Output"), T(TKEY("page.output_help"), "Choose the file format and destination."), T(TKEY("page.output_summary"), "Format and destination") },
												{ "framing", T(TKEY("page.framing"), "Framing"), T(TKEY("page.framing_help"), "Choose the part of the picture to keep."), T(TKEY("page.framing_summary"), "Crop and hotkey"), true, true, T(TKEY("page.framing_sequence"), "Refine framing and sequences") },
												{ "sequence", T(TKEY("page.sequence"), "Sequence"), T(TKEY("page.sequence_help"), "Record images using the current capture choices."), T(TKEY("page.sequence_summary"), "Timed image sequences") },
											});
	if (page.Is("source"))
		DrawCaptureSource(captureFeedback);
	if (page.Is("output"))
		DrawCaptureOutput(folderFeedback);
	if (page.Is("framing"))
		DrawCaptureFraming();
	if (page.Is("sequence"))
		DrawCaptureSequence(captureFeedback, folderFeedback);
}

void ScreenshotFeature::DrawCaptureSource(MenuUI::ActionFeedback& feedback)
{
	MenuUI::DetailText(T(TKEY("async_note"), "Capture and save run asynchronously without stalling the game."));
	MenuUI::DetailNote(T(TKEY("desktop_source"), "Captures an accepted game frame from the SE desktop presentation path. Generated frames are not separate capture sources."));
	if (IsFlatHdrScreenshotCapture())
		MenuUI::DetailNote(T(TKEY("hdr_note"), "HDR enabled: saves the displayed frame as PNG with HDR10 metadata (48 bpp RGB, cICP/cLLi). Use an HDR-aware viewer such as Windows Photos (HDR on) or Special K SKIF."));
	else
		MenuUI::DetailNote(T(TKEY("sdr_note"), "Enable HDR Display to capture HDR PNG screenshots with HDR10 metadata. SDR captures use the lossless format selected below."));
	if (!IsRuntimeEnabled())
		MenuUI::DetailNote(T(TKEY("disabled_note"), "Screenshot capture is disabled. Output and crop settings can still be edited."));
	{
		const auto guard = Util::DisableGuard(!IsRuntimeEnabled());
		if (ImGui::Button(T(TKEY("take_screenshot"), "Take Screenshot Now")))
			feedback = CaptureRequestFeedback(RequestApiCapture("csx_menu"), T(TKEY("capture_requested"), "Screenshot requested."));
	}
	Util::Widgets::Checkbox(T(TKEY("apply_crop"), "Apply crop"), &applyCropToScreenshot);
	feedback.Draw();
	ImGui::Text("%s: %zu", T(TKEY("outstanding"), "Capture jobs"), GetOutstandingCaptureJobCount());
}

void ScreenshotFeature::DrawCaptureOutput(MenuUI::ActionFeedback& feedback)
{
	Util::Widgets::Checkbox(T(TKEY("clipboard"), "Copy saved file to clipboard"), &copyToClipboard);
	Util::AddTooltip(T(TKEY("clipboard_tooltip"), "Places the saved screenshot on the clipboard as a file."));
	if (IsFlatHdrScreenshotCapture()) {
		MenuUI::DetailText(T(TKEY("hdr_format"), "HDR captures use PNG with HDR10 metadata."));
		int bits = static_cast<int>(hdrPngBitDepth);
		if (Util::Widgets::SliderInt(T(TKEY("hdr_bit_depth"), "HDR PNG bit depth"), &bits, 7, 16, "%d-bit", ImGuiSliderFlags_AlwaysClamp))
			hdrPngBitDepth = static_cast<unsigned int>(bits);
		Util::AddTooltip(T(TKEY("hdr_bit_depth_tooltip"), "Quantization for the 48 bpp RGB PNG payload. 11-bit is a good default; higher values increase file size with diminishing returns."));
	} else {
		int format = sdrUsePng ? 1 : 0;
		const char* formats[]{ T(TKEY("bmp"), "BMP (lossless)"), T(TKEY("png"), "PNG (lossless)") };
		if (MenuUI::ChoiceSetting(T(TKEY("file_format"), "File format"), &format, formats, 2))
			sdrUsePng = format == 1;
	}
	DrawCaptureFolder(false, feedback);
	feedback.Draw();
}

void ScreenshotFeature::DrawCaptureFolder(bool sequence, MenuUI::ActionFeedback& feedback)
{
	ImGui::PushID(sequence ? "SequenceFolder" : "ScreenshotFolder");
	const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
	auto& configured = sequence ? frameCapturePath : screenshotPath;
	std::string draft = configured;
	const auto makeRequest = [&](const char* action, const std::string& directory) {
		const json destination{ { "Destination", { { "Directory", directory } } } };
		return json{ { "contractMajor", 1 }, { "clientId", "csx_menu" },
			{ "commandId", CSX::Api::ServiceFoundation::NewId() }, { "action", action }, { "scope", "runtime_session" },
			{ "patch", sequence ? json{ { "Sequence", destination } } : destination } };
	};
	if (Util::Widgets::InputText(T(TKEY("folder"), "Folder"), &draft))
		feedback = CaptureRequestFeedback(HandleApiRequest(makeRequest("settings_apply", draft)), "");
	Util::AddTooltip(T(TKEY("folder_tooltip"), "Relative paths resolve against the Skyrim install dir.\nAbsolute paths (e.g. D:\\Captures) save there directly."));
	{
		const auto guard = Util::DisableGuard(configured.empty());
		if (ImGui::Button(T(TKEY("open"), "Open"))) {
			feedback = CaptureRequestFeedback(HandleApiRequest(makeRequest("settings_validate", configured)), "");
			if (!feedback.error) {
				try {
					const auto path = std::filesystem::weakly_canonical(ResolveToAbsoluteGamePath(std::filesystem::path(std::u8string(configured.begin(), configured.end()))));
					std::filesystem::create_directories(path);
					std::string error;
					if (Util::OpenInShell(path, error))
						feedback = { T(TKEY("folder_opened"), "Capture folder opened.") };
					else
						feedback = { std::move(error), true };
				} catch (const std::exception& error) {
					feedback = { error.what(), true };
				}
			}
		}
	}
}

void ScreenshotFeature::PollUiSequence(MenuUI::ActionFeedback& feedback)
{
	const auto now = std::chrono::steady_clock::now();
	if (uiSequenceRequestId.empty() || now < nextUiSequencePoll)
		return;
	nextUiSequencePoll = now + std::chrono::milliseconds(250);
	const auto result = HandleApiRequest({ { "contractMajor", 1 }, { "clientId", "csx_menu" },
		{ "commandId", CSX::Api::ServiceFoundation::NewId() }, { "action", "request_get" }, { "requestId", uiSequenceRequestId } });
	if (result.value("ok", false) && result.contains("result")) {
		const auto& receipt = result.at("result");
		if (!receipt.value("terminalUtc", json{}).is_null()) {
			const auto state = receipt.value("state", std::string{});
			const bool cancelled = state == "cancelled" || state == "cancelled_partial" || state == "stopped";
			feedback = CaptureRequestFeedback(result, cancelled ? T(TKEY("sequence_stopped"), "Sequence stopped.") : T(TKEY("sequence_complete"), "Sequence complete."));
			uiSequenceRequestId.clear();
		}
	} else {
		feedback = CaptureRequestFeedback(result, "");
		if (result.value("error", json::object()).value("code", std::string{}) == "request_not_found")
			uiSequenceRequestId.clear();
	}
}

void ScreenshotFeature::DrawCaptureSequence(MenuUI::ActionFeedback& captureFeedback, MenuUI::ActionFeedback& folderFeedback)
{
	MenuUI::DetailText(T(TKEY("sequence_help"), "Save a timed sequence of lossless images. Video composition and audio are not included."));
	int count = static_cast<int>(sequenceDefaults.frameCount);
	int interval = static_cast<int>(sequenceDefaults.intervalFrames);
	if (Util::Widgets::SliderInt(T(TKEY("sequence_count"), "Frame count"), &count, 1, 10000, "%d", ImGuiSliderFlags_AlwaysClamp))
		sequenceDefaults.frameCount = static_cast<uint32_t>(count);
	if (Util::Widgets::SliderInt(T(TKEY("sequence_interval"), "Interval in game frames"), &interval, 1, 1000000, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
		sequenceDefaults.intervalFrames = static_cast<uint32_t>(interval);
	if (IsFlatHdrScreenshotCapture()) {
		MenuUI::DetailNote(T(TKEY("sequence_hdr"), "Timed HDR sequences use HDR PNG with the bit depth chosen in Output. HDR reference and burst capture are unavailable."));
	} else {
		int format = frameCaptureUsePng ? 1 : 0;
		const char* formats[]{ T(TKEY("bmp"), "BMP (lossless)"), T(TKEY("png"), "PNG (lossless)") };
		if (MenuUI::ChoiceSetting(T(TKEY("sequence_format"), "Sequence format"), &format, formats, 2))
			frameCaptureUsePng = format == 1;
	}
	DrawCaptureFolder(true, folderFeedback);
	folderFeedback.Draw();
	{
		const auto guard = Util::DisableGuard(!IsRuntimeEnabled() && uiSequenceRequestId.empty());
		if (uiSequenceRequestId.empty()) {
			if (ImGui::Button(T(TKEY("sequence_start"), "Start sequence"))) {
				const auto response = HandleApiRequest({ { "contractMajor", 1 }, { "clientId", "csx_menu" },
					{ "commandId", CSX::Api::ServiceFoundation::NewId() }, { "action", "sequence_start" },
					{ "sequence", { { "useSettings", true }, { "frameCount", sequenceDefaults.frameCount },
									  { "schedule", { { "basis", "game_frames" }, { "intervalFrames", sequenceDefaults.intervalFrames } } } } } });
				captureFeedback = CaptureRequestFeedback(response, T(TKEY("sequence_started"), "Sequence started."));
				if (!captureFeedback.error) {
					uiSequenceRequestId = response.at("result").value("requestId", std::string{});
					if (uiSequenceRequestId.empty())
						captureFeedback = { T(TKEY("sequence_no_id"), "The capture service returned no sequence identifier."), true };
				}
			}
		} else if (ImGui::Button(T(TKEY("sequence_stop"), "Stop sequence"))) {
			const auto response = HandleApiRequest({ { "contractMajor", 1 }, { "clientId", "csx_menu" },
				{ "commandId", CSX::Api::ServiceFoundation::NewId() }, { "action", "sequence_stop" }, { "requestId", uiSequenceRequestId } });
			captureFeedback = CaptureRequestFeedback(response, T(TKEY("sequence_stopping"), "Sequence is stopping."));
			nextUiSequencePoll = {};
		}
	}
	captureFeedback.Draw();
	ImGui::Text("%s: %zu", T(TKEY("outstanding"), "Capture jobs"), GetOutstandingCaptureJobCount());
}

void ScreenshotFeature::DrawCaptureFraming()
{
	auto* menu = Menu::GetSingleton();
	Util::InputComboWidget(T(TKEY("hotkey"), "Hotkey"), menu->GetSettings().ScreenshotKey, menu->settingScreenshotKey, "Change##ScreenshotFeature");
	if (HotkeyCollidesWithVanilla())
		Util::Text::WrappedWarning("%s", T(TKEY("hotkey_collision"), "This hotkey collides with vanilla PrintScreen; both saves will fire. Set bAllowScreenShot=0 in Skyrim.ini to suppress vanilla, or pick a different hotkey above."));
	Util::Widgets::Checkbox(T(TKEY("apply_crop"), "Apply crop"), &applyCropToScreenshot);
	MenuUI::SectionHeading(T(TKEY("crop"), "Crop"));
	// Preview reflects what Capture() would save.
	winrt::com_ptr<ID3D11Texture2D> previewTextureKeepAlive;
	const auto src = SelectCaptureSource(previewTextureKeepAlive, /*forCapture=*/false);

	ID3D11ShaderResourceView* previewView = src.srv;
	if (src.texture && (src.needsPreviewCache || !previewView)) {
		EnsurePreviewCache(src.texture);
		if (previewCacheSRV && previewCacheTexture) {
			globals::d3d::context->CopySubresourceRegion(
				previewCacheTexture.get(), 0, 0, 0, 0, src.texture, 0, nullptr);
			previewView = previewCacheSRV.get();
		}
	}

	subrect.DrawEditor(previewView, src.texture, 1.0f, 0.0f, OpaquePreviewBlendCallback);
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
	Util::SetResourceName(previewCacheTexture.get(), "Screenshot::Preview");
	if (FAILED(globals::d3d::device->CreateShaderResourceView(
			previewCacheTexture.get(), nullptr, previewCacheSRV.put()))) {
		previewCacheSRV = nullptr;
		previewCacheTexture = nullptr;
	} else Util::SetResourceName(previewCacheSRV.get(), "Screenshot::Preview SRV");
}

void ScreenshotFeature::Reset()
{
	if (auto* menu = Menu::GetSingleton(); !menu || !menu->IsEnabled) {
		previewCacheSRV = nullptr; previewCacheTexture = nullptr;
	}
}

void ScreenshotFeature::Capture() { RequestUiCapture(); }
void ScreenshotFeature::RequestUiCapture() { captureRequested.store(true, std::memory_order_release); }
json ScreenshotFeature::HandleApiRequest(const json& request) { return screenshotApi->HandleRequest(*this, request); }
json ScreenshotFeature::RequestApiCapture(std::string_view origin)
{
	return HandleApiRequest({ { "contractMajor", 1 }, { "action", "capture" }, { "clientId", origin },
		{ "commandId", CSX::Api::ServiceFoundation::NewId() }, { "useSettings", true } });
}
#ifdef DEVBENCH_BRIDGE_ENABLED
json ScreenshotFeature::HandleReferenceCapture(const json& request, std::function<void(const json&)> completion)
{
	return screenshotApi->HandleReferenceRequest(*this, request, std::move(completion));
}
void ScreenshotFeature::SetBurstDeferral(bool defer)
{
	{ std::lock_guard lock(captureState->mutex); captureState->deferEncoding = defer; }
	captureState->condition.notify_all();
}
#endif

void ScreenshotFeature::SetEnabled(bool value)
{
	enabled.store(value, std::memory_order_release);
	if (value) return;
	captureRequested.store(false, std::memory_order_release);
	const auto active = GetActiveCaptureRequestId();
	if (!active.empty() && CancelApiCapture(active)) screenshotApi->OnSourceTerminal(active, "cancelled", "feature_disabled");
	screenshotApi->OnFeatureDisabled("feature_disabled");
}
std::size_t ScreenshotFeature::GetOutstandingCaptureJobCount() const
{
	std::lock_guard lock(captureState->mutex); return captureState->outstanding;
}
std::string ScreenshotFeature::GetActiveCaptureRequestId() const
{
	std::lock_guard lock(captureState->mutex); return captureState->active ? captureState->active->requestId : std::string{};
}

json ScreenshotFeature::BuildCaptureDescriptor(bool usePng, bool clipboard) const
{
	const bool hdr = IsFlatHdrScreenshotCapture();
	json output = { { "view", "source_native" },
		{ "encoding", { { "format", hdr || usePng ? "png" : "bmp" }, { "colourContract", hdr ? "hdr10_pq" : "sdr_srgb" },
			{ "bitDepth", hdr ? std::clamp(hdrPngBitDepth, 7u, 16u) : 8u } } } };
	if (applyCropToScreenshot) {
		const auto crop = subrect.GetUV();
		output["crop"] = { { "x", crop.x }, { "y", crop.y }, { "width", crop.w }, { "height", crop.h } };
	}
	return { { "source", { { "kind", "desktop_mirror" }, { "fallback", "reject" } } },
		{ "outputs", json::array({ output }) }, { "clipboard", clipboard ? "file_reference" : "none" } };
}

ScreenshotFeature::CaptureStartResult ScreenshotFeature::TryStartApiCapture(const std::string& requestId,
	const json& descriptor, const std::string& parentRequestId, uint32_t ordinal)
{
	if (!IsRuntimeEnabled()) return CaptureStartResult::FeatureDisabled;
	if (!descriptor.is_object() || descriptor.at("source").at("kind") != "desktop_mirror" ||
		!descriptor.at("outputs").is_array() || descriptor.at("outputs").empty() ||
		descriptor.at("outputs").size() > CSX::ScreenshotPolicy::MaximumOutputsPerFrame)
		return CaptureStartResult::InvalidDescriptor;
	bool burst = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
	burst = descriptor.contains("burst");
#endif
	{
		std::lock_guard lock(captureState->mutex);
		if (captureState->stopRequested) return CaptureStartResult::FeatureDisabled;
		if (captureState->active) return CaptureStartResult::SourceBusy;
		if (captureState->outstanding >= (burst ? 240u : CSX::Screenshot::NormalJobCapacity) ||
			(!burst && captureState->deferEncoding)) return CaptureStartResult::EncoderBackpressure;
		captureState->active = std::make_shared<CaptureState::Request>(CaptureState::Request{
			requestId, parentRequestId, ordinal, descriptor, std::chrono::steady_clock::now() + std::chrono::seconds(10) });
		++captureState->outstanding;
	}
	screenshotApi->OnSourceWaiting(requestId, "desktop_mirror");
	return CaptureStartResult::Started;
}

bool ScreenshotFeature::CancelApiCapture(std::string_view requestId)
{
	std::lock_guard lock(captureState->mutex);
	if (!captureState->active || captureState->active->requestId != requestId) return false;
	captureState->active.reset(); --captureState->outstanding;
	return true;
}

void ScreenshotFeature::ProcessCaptureRequest()
{
	if (captureRequested.exchange(false, std::memory_order_acq_rel)) {
		const auto response = RequestApiCapture("csx_hotkey");
		if (!response.value("ok", false)) logger::warn("Screenshot request rejected: {}", response.dump());
	}
	screenshotApi->Tick(*this, globals::state ? globals::state->frameCount : 0u);
	AcquirePendingCapture();
}

void ScreenshotFeature::AcquirePendingCapture()
{
	std::shared_ptr<CaptureState::Request> request;
	{ std::lock_guard lock(captureState->mutex); request = captureState->active; }
	if (!request) return;
	uint64_t acquiredBytes = 0;
	const auto fail = [&](std::string_view reason) {
		bool owns = false;
		{
			std::lock_guard lock(captureState->mutex);
			if (captureState->active == request) { captureState->active.reset(); --captureState->outstanding; owns = true; }
			else if (acquiredBytes) {
				captureState->stagingBytes -= acquiredBytes; --captureState->outstanding;
				acquiredBytes = 0; owns = true;
			}
		}
		if (owns) screenshotApi->OnSourceTerminal(request->requestId, "failed", reason);
	};
	auto* device = globals::d3d::device;
	auto* context = globals::d3d::context;
	if (!device || !context || !globals::state) return;
	const Util::RendererOwnership owner(Util::GetRendererContextLock(globals::game::renderer, context));
	if (!owner) return;
	try {
		winrt::com_ptr<ID3D11Texture2D> retained;
		const auto source = SelectCaptureSource(retained, true);
		if (!source.texture) return;
		winrt::com_ptr<ID3D11Device> sourceDevice, contextDevice;
		source.texture->GetDevice(sourceDevice.put()); context->GetDevice(contextDevice.put());
		if (!Util::SameIdentity(sourceDevice.get(), device) || !Util::SameIdentity(contextDevice.get(), device)) {
			fail("capture_source_device_changed"); return;
		}
		D3D11_TEXTURE2D_DESC description{}; source.texture->GetDesc(&description);
		if (!description.Width || !description.Height || description.Width > CSX::Screenshot::MaximumDimension ||
			description.Height > CSX::Screenshot::MaximumDimension || description.SampleDesc.Count != 1 ||
			description.ArraySize != 1 || description.MipLevels != 1 || DirectX::IsCompressed(description.Format)) {
			fail("unsupported_source_layout"); return;
		}
		const bool hdr = IsFlatHdrScreenshotCapture();
		winrt::com_ptr<IUnknown> deviceIdentity;
		if (FAILED(device->QueryInterface(IID_IUnknown, deviceIdentity.put_void()))) { fail("capture_device_identity_unavailable"); return; }
		if (hdr && !IsHdrCaptureFormat(description.Format)) { fail("unsupported_hdr_source"); return; }
		const auto& descriptor = request->descriptor;
		bool strictNative = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		ScreenshotBurst::Plan burst;
		if (descriptor.contains("burst")) burst = ScreenshotBurst::Parse(descriptor.at("burst"), 1, 1);
		strictNative = descriptor.contains("referenceOutputPath") || bool(burst);
#endif
		const auto linearFormat = DirectX::MakeLinear(description.Format);
		if (strictNative && (hdr || (linearFormat != DXGI_FORMAT_R8G8B8A8_UNORM && linearFormat != DXGI_FORMAT_B8G8R8A8_UNORM))) {
			fail("native_sdr8_source_required"); return;
		}
		const auto& destination = descriptor.at("destination");
		const auto directory = std::filesystem::u8path(destination.value("resolvedDirectory", destination.value("directory", std::string{})));
		if (!directory.is_absolute()) { fail("invalid_capture_destination"); return; }
		const auto base = destination.contains("baseName") && destination.at("baseName").is_string() ?
			destination.at("baseName").get<std::string>() : "CS_" + request->requestId;
		CaptureState::Job job;
		job.requestId = request->requestId; job.context.copy_from(context); job.notify = request->parentRequestId.empty();
		const uint32_t frame = globals::state->frameCount;
		for (const auto& output : descriptor.at("outputs")) {
			const auto& encoding = output.at("encoding");
			if ((encoding.at("colourContract") == "hdr10_pq") != hdr) { fail("colour_contract_source_mismatch"); return; }
			CaptureState::Output item;
			item.format = description.Format; item.hdr = hdr; item.png = encoding.at("format") == "png";
			item.strictNative = strictNative; item.hdrBits = encoding.value("bitDepth", 11u);
			item.clipboard = descriptor.value("clipboard", "none") == "file_reference";
			CSX::Screenshot::PixelRect crop{ 0, 0, description.Width, description.Height };
			if (output.contains("crop") && output.at("crop").is_object()) {
				const auto& value = output.at("crop");
				crop = CSX::Screenshot::ResolveCrop(description.Width, description.Height, value.at("x").get<float>(), value.at("y").get<float>(),
					value.at("width").get<float>(), value.at("height").get<float>());
			}
			item.width = crop.width; item.height = crop.height;
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (burst) { item.width = burst.width; item.height = burst.height; }
#endif
			const auto bits = DirectX::BitsPerPixel(description.Format);
			if (!bits || bits % 8 != 0) { fail("unsupported_source_format"); return; }
			job.bytes += uint64_t(item.width) * item.height * (bits / 8);
			if (!CSX::Screenshot::CanReserve(0, job.bytes)) { fail("capture_payload_limit"); return; }
			item.path = directory / (base + "_" + output.at("nameSuffix").get<std::string>() + (item.png ? ".png" : ".bmp"));
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (descriptor.contains("referenceOutputPath")) item.path = std::filesystem::u8path(descriptor.at("referenceOutputPath").get<std::string>());
#endif
			item.actual = { { "view", "source_native" }, { "width", item.width }, { "height", item.height },
				{ "format", item.png ? "png" : "bmp" }, { "colourContract", hdr ? "hdr10_pq" : "sdr_srgb" },
				{ "nativePixels", strictNative }, { "uiExcluded", source.uiExcluded }, { "engineFrame", frame },
				{ "generatedFrame", false }, { "sourceFormat", static_cast<uint32_t>(description.Format) },
				{ "crop", { { "x", crop.x }, { "y", crop.y }, { "width", crop.width }, { "height", crop.height } } } };
			if (hdr) item.actual["hdrPngBitDepth"] = item.hdrBits;
			D3D11_TEXTURE2D_DESC staging = description;
			staging.Width = item.width; staging.Height = item.height;
			staging.Usage = D3D11_USAGE_STAGING; staging.BindFlags = 0;
			staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ; staging.MiscFlags = 0;
			{
				std::lock_guard lock(captureState->mutex);
				if (captureState->active != request) return;
				if (!CSX::Screenshot::CanReserve(captureState->stagingBytes, job.bytes)) throw std::runtime_error("capture_staging_budget_exhausted");
			}
			if (FAILED(device->CreateTexture2D(&staging, nullptr, item.staging.put()))) throw std::runtime_error("capture_staging_allocation_failed");
			Util::SetResourceName(item.staging.get(), "Screenshot::Readback");
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (burst) {
				item.actual["burstRegions"] = json::array();
				for (const auto& region : burst.Copies(description.Width, description.Height, false, false)) {
					const D3D11_BOX box{ region.x, region.y, 0, region.x + region.width, region.y + region.height, 1 };
					context->CopySubresourceRegion(item.staging.get(), 0, 0, region.destinationY, 0, source.texture, 0, &box);
					item.actual["burstRegions"].push_back({ { "x", region.x }, { "y", region.y }, { "width", region.width }, { "height", region.height }, { "destinationY", region.destinationY } });
				}
			} else
#endif
			{
				const D3D11_BOX box{ crop.x, crop.y, 0, crop.x + crop.width, crop.y + crop.height, 1 };
				context->CopySubresourceRegion(item.staging.get(), 0, 0, 0, 0, source.texture, 0, &box);
			}
			job.outputs.push_back(std::move(item));
		}
		const D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 };
		if (FAILED(device->CreateQuery(&query, job.completed.put()))) throw std::runtime_error("capture_completion_query_failed");
		Util::SetResourceName(job.completed.get(), "Screenshot::CopyComplete");
		context->End(job.completed.get());
		const auto evidence = CSX::ScreenshotPolicy::BuildMonoNeuralEvidence(frame);
		{
			std::lock_guard lock(captureState->mutex);
			if (captureState->active != request) return;
			captureState->active.reset();
			captureState->stagingBytes += job.bytes;
			acquiredBytes = job.bytes;
		}
		json acquisition = { { "engineFrame", frame }, { "source", "desktop_mirror" }, { "description", source.description },
			{ "generatedFrame", false }, { "uiExcluded", source.uiExcluded }, { "nrEvidence", evidence },
			{ "planes", json::array({ { { "sourceWidth", description.Width }, { "sourceHeight", description.Height },
				{ "format", static_cast<uint32_t>(description.Format) }, { "samples", description.SampleDesc.Count },
				{ "deviceIdentity", reinterpret_cast<std::uintptr_t>(deviceIdentity.get()) } } }) } };
		screenshotApi->OnSourceAcquired(request->requestId, std::move(acquisition));
		for (const auto& output : job.outputs) screenshotApi->OnArtifactQueued(request->requestId, output.path);
		{
			std::lock_guard lock(captureState->mutex);
			captureState->jobs.push_back(std::move(job));
			acquiredBytes = 0;
		}
		captureState->condition.notify_all();
	} catch (const std::exception& error) { fail(error.what()); }
}

void ScreenshotFeature::ScreenshotWorkerLoop(std::shared_ptr<CaptureState> state, std::shared_ptr<ScreenshotApi> api)
{
	const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	for (;;) {
		CaptureState::Job job;
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] { return state->stopRequested || (!state->deferEncoding && !state->jobs.empty()); });
			if (state->jobs.empty()) { if (state->stopRequested) break; continue; }
			job = std::move(state->jobs.front()); state->jobs.pop_front();
		}
		api->OnArtifactEncoding(job.requestId);
		bool allSaved = true;
		for (auto& output : job.outputs) {
			try {
				DirectX::ScratchImage image;
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				HRESULT result;
				for (;;) {
					{
						std::lock_guard lock(state->mutex);
						if (state->stopRequested) throw std::runtime_error("capture_shutdown_before_readback");
					}
					result = PopulateScratchImageFromStagingTexture(job.context.get(), output.staging.get(), job.completed.get(),
						output.format, output.width, output.height, image);
					if (result != DXGI_ERROR_WAS_STILL_DRAWING) break;
					if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("capture_readback_timeout");
					std::unique_lock lock(state->mutex);
					state->condition.wait_for(lock, std::chrono::milliseconds(5), [&] { return state->stopRequested; });
				}
				if (FAILED(result)) throw std::runtime_error(std::format("capture_readback_failed:{:#x}", static_cast<uint32_t>(result)));
				const auto committed = SaveScreenshotAtomically(image, output.path, output.format, output.hdrBits, output.hdr, output.png, output.strictNative);
				api->OnArtifactTerminal(job.requestId, true, output.path, {}, output.actual, committed);
				CopySavedPathToClipboard(output.clipboard, output.path);
				logger::info("Saved screenshot to {}", Util::PathToUtf8(output.path));
			} catch (const std::exception& error) {
				allSaved = false;
				logger::error("Screenshot artifact failed: {}", error.what());
				api->OnArtifactTerminal(job.requestId, false, output.path, error.what(), output.actual);
			}
		}
		bool notify;
		{
			std::lock_guard lock(state->mutex);
			--state->outstanding; state->stagingBytes -= job.bytes;
			notify = job.notify && !state->stopRequested;
		}
		if (notify) ShowInGameNotification(allSaved ? T(TKEY("saved"), "Screenshot saved") : T(TKEY("failed"), "Screenshot failed; see CommunityShaders.log"));
		api->ServiceDeadlines();
	}
	if (SUCCEEDED(com)) CoUninitialize();
	{ std::lock_guard lock(state->mutex); state->exited = true; }
	state->condition.notify_all();
}

void ScreenshotFeature::DeadlineWorkerLoop(std::stop_token stop, std::shared_ptr<CaptureState> state, std::shared_ptr<ScreenshotApi> api)
{
	while (!stop.stop_requested()) {
		std::string expired;
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait_for(lock, std::chrono::milliseconds(50), [&] { return stop.stop_requested(); });
			if (stop.stop_requested()) break;
			if (state->active && std::chrono::steady_clock::now() >= state->active->expiresAt) {
				expired = state->active->requestId; state->active.reset(); --state->outstanding;
			}
		}
		if (!expired.empty()) api->OnSourceTerminal(expired, "failed", "source_timeout");
		api->ServiceDeadlines();
#ifdef DEVBENCH_BRIDGE_ENABLED
		const bool defer = api->ShouldDeferEncoding();
		{ std::lock_guard lock(state->mutex); state->deferEncoding = defer; }
		state->condition.notify_all();
#endif
	}
}

void ScreenshotFeature::ShowInGameNotification(std::string message)
{
	RunOnMainThread([message = std::move(message)] { RE::SendHUDMessage::ShowHUDMessage(message.c_str(), nullptr, true); });
}
#undef I18N_KEY_PREFIX
