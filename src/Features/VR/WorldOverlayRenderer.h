#pragma once

#include "VRAPI/CSworldoverlayapi.h"
#include <d3d11.h>
#include <nlohmann/json_fwd.hpp>
#include <openvr.h>

namespace CSX::WorldOverlays
{
	void InitializeService();
	void InstallDevBench();
	/// Called only at the existing engine framebuffer upload and completed-depth boundaries.
	void CaptureCamera() noexcept;
	void CompleteScene() noexcept;
	void CaptureDepth(ID3D11Texture2D* texture, ID3D11ShaderResourceView* view) noexcept;
	void InvalidateDepth() noexcept;
	void ResetWorld() noexcept;
	void ReleaseResources() noexcept;
	void BeginCycle(std::uint64_t cycle) noexcept;
	/// Freezes atlas pages and ordered metadata for the complete native stereo call.
	void BeginPair(std::uint64_t cycle) noexcept;
	void EndPair() noexcept;
	/// Records downstream acceptance only for an eye that actually contains this pair.
	void AcceptEye(std::uint32_t eye) noexcept;
	bool EyeDrawn(std::uint32_t eye) noexcept;
	void RecordCopy(bool capture) noexcept;
	bool HasContent() noexcept;
	/// Matches the accepted vendor output to the retained completed-world producer.
	bool MatchesVendor(std::uint32_t frame, std::uint64_t cycle, std::uint32_t generation, std::uint32_t eye, std::uint32_t width, std::uint32_t height) noexcept;
	bool CanDraw(std::uint32_t eye) noexcept;
	bool Draw(std::uint32_t eye, ID3D11RenderTargetView* target,
		const D3D11_TEXTURE2D_DESC& desc, const vr::VRTextureBounds_t* bounds, bool reconstructed) noexcept;
	nlohmann::json Diagnostics();
	bool SetSynthetic(bool enabled, const WorldOverlayAPI::Quad001& quad);
}
