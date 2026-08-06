// Screenshot Feature
// Non-blocking screenshot tool for flat (SE/AE) and VR. GPU copy runs on the
// render thread; encoding and disk I/O run on a dedicated worker thread so
// capture does not stall the frame.

#include "Features/ScreenshotFeature.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"
#include "Utils/NormalizedCoordinates.h"
#include <DirectXTex.h>
#include <PCH.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <thread>
#include <utility>

namespace
{
	constexpr uint32_t kCaptureTimeoutPresents = 6;
	constexpr std::size_t kMaxOutstandingScreenshots = 2;
	constexpr auto kReadbackMapTimeout = std::chrono::milliseconds(500);
	constexpr auto kReadbackMapRetryDelay = std::chrono::milliseconds(1);

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

	// Game-root-relative paths must be absolute for CF_HDROP / Discord.
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
		return SUCCEEDED(DirectX::SaveToWICFile(
			*saveImage,
			DirectX::WIC_FLAGS_NONE,
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
		return ResolveToAbsoluteGamePath(std::filesystem::path(screenshotPath) / buf);
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
			{ .name = "Full Frame", .uv = { 0.0f, 0.0f, 1.0f, 1.0f } },
		});
	}
}

void ScreenshotFeature::LoadSettings(json& a_json)
{
	const bool captureEnabled = a_json.value("Enabled", true);
	if (a_json.contains("ScreenshotPath"))
		screenshotPath = a_json["ScreenshotPath"];
	if (a_json.contains("ApplyCropToScreenshot"))
		applyCropToScreenshot = a_json["ApplyCropToScreenshot"];
	if (a_json.contains("SdrUsePng"))
		sdrUsePng = a_json["SdrUsePng"];
	if (a_json.contains("CopyToClipboard"))
		copyToClipboard = a_json["CopyToClipboard"];
	if (a_json.contains("VRCaptureSource") && a_json["VRCaptureSource"].is_string()) {
		vrCaptureSource = a_json["VRCaptureSource"].get<std::string>() == "DesktopMirror" ?
		                      VRCaptureSource::DesktopMirror :
		                      VRCaptureSource::HMDSubmission;
	}

	subrect.LoadSettings(a_json);
	SetEnabled(captureEnabled);
}

void ScreenshotFeature::SaveSettings(json& a_json)
{
	a_json["Enabled"] = enabled.load(std::memory_order_acquire);
	a_json["ScreenshotPath"] = screenshotPath;
	a_json["ApplyCropToScreenshot"] = applyCropToScreenshot;
	a_json["SdrUsePng"] = sdrUsePng;
	a_json["CopyToClipboard"] = copyToClipboard;
	a_json["VRCaptureSource"] = vrCaptureSource == VRCaptureSource::DesktopMirror ?
	                                "DesktopMirror" :
	                                "HMDSubmission";
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
		int captureSource = vrCaptureSource == VRCaptureSource::DesktopMirror ? 1 : 0;
		ImGui::RadioButton("HMD submission (best quality)", &captureSource, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Desktop mirror", &captureSource, 1);
		vrCaptureSource = captureSource == 1 ?
		                      VRCaptureSource::DesktopMirror :
		                      VRCaptureSource::HMDSubmission;
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("HMD submission captures the final accepted left and right eye textures.");
			ImGui::TextUnformatted("Desktop mirror temporarily forces the improved Render Scale mirror for this capture only.");
		}
	}

	ImGui::BeginDisabled(!IsRuntimeEnabled());
	if (ImGui::Button("Take Screenshot Now")) {
		RequestCapture();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Checkbox("Apply crop", &applyCropToScreenshot);

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
		std::error_code ec;
		std::filesystem::create_directories(screenshotPath, ec);
		ShellExecuteA(nullptr, "open", screenshotPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text("Folder");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Relative paths resolve against the Skyrim install dir.");
		ImGui::Text("Absolute paths (e.g. D:\\Captures) save there directly.");
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
		.copyToClipboard = copyToClipboard
	};
}

void ScreenshotFeature::ArmDesktopMirrorOverride(ActiveCapture& a_capture)
{
	if (!globals::game::isVR || a_capture.desktopMirrorEpoch != 0) {
		return;
	}

	a_capture.desktopMirrorEpoch =
		globals::features::upscaling.BeginScreenshotDesktopMirrorQualityOverride();
}

void ScreenshotFeature::ReleaseDesktopMirrorOverride(ActiveCapture& a_capture)
{
	if (a_capture.desktopMirrorEpoch == 0) {
		return;
	}

	globals::features::upscaling.EndScreenshotDesktopMirrorQualityOverride(
		a_capture.desktopMirrorEpoch);
	a_capture.desktopMirrorEpoch = 0;
}

void ScreenshotFeature::ClearActiveCapture(ActiveCapture& a_capture)
{
	const bool ownsQueueSlot = std::exchange(a_capture.ownsQueueSlot, false);
	ReleaseDesktopMirrorOverride(a_capture);
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
	ArmDesktopMirrorOverride(a_capture);
}

void ScreenshotFeature::RequestCapture()
{
	if (!IsRuntimeEnabled()) {
		return;
	}

	auto options = SnapshotCaptureOptions();

	std::lock_guard lock(captureStateMutex);
	if (!IsRuntimeEnabled()) {
		return;
	}
	ClearActiveCapture(activeCapture);
	if (!TryReserveScreenshotSlot()) {
		capturePending.store(false, std::memory_order_release);
		logger::warn("Screenshot encoder is busy; rejecting the newest capture request.");
		ShowInGameNotification("Screenshot busy - try again shortly");
		return;
	}
	activeCapture.pending = true;
	activeCapture.ownsQueueSlot = true;
	activeCapture.options = std::move(options);
	activeCapture.source = globals::game::isVR ?
	                           vrCaptureSource :
	                           VRCaptureSource::DesktopMirror;

	if (globals::game::isVR &&
		activeCapture.source == VRCaptureSource::HMDSubmission &&
		globals::state && globals::state->isLoadingMenuOpen) {
		activeCapture.source = VRCaptureSource::DesktopMirror;
	}

	if (activeCapture.source == VRCaptureSource::DesktopMirror) {
		ArmDesktopMirrorOverride(activeCapture);
	}
	capturePending.store(true, std::memory_order_release);

	logger::debug(
		"Screenshot requested from {}",
		activeCapture.source == VRCaptureSource::HMDSubmission ?
			"the accepted HMD submission" :
			"the desktop mirror");
}

void ScreenshotFeature::SetEnabled(bool a_enabled)
{
	bool wasEnabled = false;
	bool cancelledPendingCapture = false;
	{
		std::lock_guard lock(captureStateMutex);
		wasEnabled = enabled.exchange(a_enabled, std::memory_order_acq_rel);
		if (!a_enabled) {
			// Close the Submit fast path before releasing partial textures and its
			// reserved encoder slot. Completed or queued encoder work remains committed.
			capturePending.store(false, std::memory_order_release);
			cancelledPendingCapture = activeCapture.pending;
			ClearActiveCapture(activeCapture);
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
	auto reportFailure = [](std::string_view message) {
		logger::error("{}", message);
		ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
	};

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

			const DXGI_FORMAT combinedFormat = planeImages[0]->format;
			const vr::EColorSpace combinedColorSpace = screenshot.planes[0].colorSpace;
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
			const uint32_t combinedWidth = planeSlotWidth * screenshot.planeCount;
			if (planeFailure || combinedWidth == 0 || combinedHeight == 0) {
				reportFailure("Screenshot planes used incompatible image contracts.");
				continue;
			}

			DirectX::ScratchImage combinedImage;
			DirectX::ScratchImage* assembledImage = nullptr;
			const DirectX::Image* assembled = nullptr;
			if (screenshot.planeCount == 1) {
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
				CopySavedPathToClipboard(screenshot.copyToClipboard, screenshot.outputPath);
				logger::info("Saved screenshot to {}", screenshot.outputPath.string());
				ShowInGameNotification(std::format("Screenshot saved: {}",
					screenshot.outputPath.filename().string()));
			}
		} catch (const std::exception& e) {
			logger::error("Screenshot worker failed with an exception: {}", e.what());
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
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
	ID3D11Texture2D* a_mirrorTexture,
	vr::EColorSpace a_mirrorColorSpace,
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
		const char* sourceDescription = "refreshed Render Scale desktop mirror";
		vr::EColorSpace sourceColorSpace = a_mirrorColorSpace;
		bool tonemapSceneHdr = false;
		if (a_mirrorTexture) {
			sourceTexture.copy_from(a_mirrorTexture);
		} else if (a_swapChain) {
			sourceDescription = "DXGI desktop backbuffer";
			sourceColorSpace = vr::ColorSpace_Auto;
			tonemapSceneHdr = true;
			(void)a_swapChain->GetBuffer(
				0,
				__uuidof(ID3D11Texture2D),
				sourceTexture.put_void());
		}

		winrt::com_ptr<ID3D11Texture2D> slotTextureKeepAlive;
		if (!sourceTexture) {
			const auto source = SelectCaptureSource(slotTextureKeepAlive);
			if (source.texture) {
				sourceTexture.copy_from(source.texture);
				sourceDescription = source.description;
				sourceColorSpace = vr::ColorSpace_Auto;
				tonemapSceneHdr = true;
			}
		}
		if (!sourceTexture) {
			logger::error("Failed to acquire the desktop screenshot source.");
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
		screenshot.outputPath = BuildScreenshotPath(a_options.screenshotPath, screenshot.saveAsPng);
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
	{
		std::lock_guard lock(captureStateMutex);
		if (!IsRuntimeEnabled() ||
			!activeCapture.pending ||
			activeCapture.source != VRCaptureSource::HMDSubmission) {
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

		activeCapture.eyes[eyeIndex] = std::move(plane);
		activeCapture.eyeMask |= static_cast<uint8_t>(1u << eyeIndex);
		if (activeCapture.eyeMask != 0x3u) {
			return;
		}

		if (activeCapture.eyes[0].format != activeCapture.eyes[1].format ||
			activeCapture.eyes[0].colorSpace != activeCapture.eyes[1].colorSpace ||
			activeCapture.eyes[0].tonemapSceneHdr != activeCapture.eyes[1].tonemapSceneHdr) {
			logger::warn("Accepted HMD screenshot eyes used incompatible image contracts; waiting for a coherent pair.");
			activeCapture.eyeMask = 0;
			activeCapture.eyes = {};
			return;
		}

		completedScreenshot.planes = std::move(activeCapture.eyes);
		completedScreenshot.planeCount = 2;
		completedScreenshot.cropUV = activeCapture.options.cropUV;
		completedScreenshot.applyCrop = activeCapture.options.applyCrop;
		completedScreenshot.saveAsPng = activeCapture.options.saveAsPng;
		completedScreenshot.copyToClipboard = activeCapture.options.copyToClipboard;
		try {
			completedScreenshot.outputPath = BuildScreenshotPath(
				activeCapture.options.screenshotPath,
				completedScreenshot.saveAsPng);
		} catch (const std::exception& e) {
			logger::error("Failed to prepare the HMD screenshot output path: {}", e.what());
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
			return;
		} catch (...) {
			logger::error("Failed to prepare the HMD screenshot output path.");
			ClearActiveCapture(activeCapture);
			capturePending.store(false, std::memory_order_release);
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
			return;
		}
		completedScreenshot.ownsQueueSlot = std::exchange(activeCapture.ownsQueueSlot, false);
		ClearActiveCapture(activeCapture);
		capturePending.store(false, std::memory_order_release);
		completed = true;
	}

	if (completed) {
		logger::debug("Capturing the accepted OpenVR HMD eye pair");
		if (!QueueScreenshot(std::move(completedScreenshot))) {
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
		}
	}
}

void ScreenshotFeature::OnBeforePresent(IDXGISwapChain* a_swapChain)
{
	RestoreReadbackContextProtectionIfIdle();
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
			FallBackToDesktopCapture(activeCapture, "no coherent accepted eye pair arrived before the timeout");
		}
		return;
	}

	winrt::com_ptr<ID3D11Texture2D> refreshedMirrorTexture;
	vr::EColorSpace refreshedMirrorColorSpace = vr::ColorSpace_Auto;
	const bool renderScaleActive = globals::game::isVR &&
	                               globals::features::upscaling.IsVRRenderScaleModeActive();
	bool mirrorReady = !renderScaleActive;
	if (renderScaleActive) {
		mirrorReady = globals::features::upscaling.TryAcquireScreenshotDesktopMirror(
			activeCapture.desktopMirrorEpoch,
			globals::features::upscaling.GetActiveVRRenderScaleContractGeneration(),
			refreshedMirrorTexture,
			refreshedMirrorColorSpace);
	}

	if (!mirrorReady && activeCapture.presentsWaited < kCaptureTimeoutPresents) {
		return;
	}
	if (!mirrorReady) {
		logger::warn("Desktop screenshot mirror refresh timed out; capturing the current backbuffer.");
	}

	const bool queued = QueueDesktopCapture(
		a_swapChain,
		refreshedMirrorTexture.get(),
		refreshedMirrorColorSpace,
		activeCapture.options,
		std::exchange(activeCapture.ownsQueueSlot, false));
	ClearActiveCapture(activeCapture);
	capturePending.store(false, std::memory_order_release);
	if (!queued) {
		ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
	}
}
