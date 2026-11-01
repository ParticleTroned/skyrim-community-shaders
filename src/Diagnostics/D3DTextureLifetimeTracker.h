#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include <d3d11.h>
#	include <nlohmann/json.hpp>

#	include <cstdint>

namespace RE
{
	class NiSourceTexture;
}

namespace Diagnostics::D3DTextureLifetimeTracker
{
	/** Start an opt-in capture; no textures are retained by the tracker. */
	bool Start();
	/** Freeze counters only when the optional expected capture still owns them. */
	bool Stop(std::uint64_t a_expectedSessionID = 0);
	/** Clear a stopped capture; an active capture cannot be reset. */
	bool Reset(std::uint64_t a_expectedSessionID = 0);
	/** Advance the allocation cohort of the expected active capture. */
	bool Checkpoint(std::uint64_t a_expectedSessionID = 0);
	/** Return whether newly created D3D11 textures are being tracked. */
	bool IsActive();

	/** Attach a private COM sentinel after successful native texture creation. */
	void OnTextureCreated(
		ID3D11Texture2D* a_texture,
		const D3D11_TEXTURE2D_DESC& a_desc,
		std::uintptr_t a_caller) noexcept;

	/** Copy creation-origin metadata while the native caller owns the source texture. */
	void OnSourceTextureCreated(RE::NiSourceTexture* a_texture) noexcept;

	/** Snapshot global counters and bounded native ownership with explicit coverage. */
	nlohmann::json BuildStatus();
}

#endif
