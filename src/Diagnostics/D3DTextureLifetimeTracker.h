#pragma once

#include <d3d11.h>
#include <nlohmann/json.hpp>

#include <cstdint>

namespace RE
{
	class BGSHeadPart;
	class BSFaceGenNiNode;
	class NiSourceTexture;
	class TESNPC;
}

namespace Diagnostics::D3DTextureLifetimeTracker
{
	// Capture is intentionally opt-in. The device hook always forwards, but it
	// attaches no sentinel and allocates nothing unless a DevBench capture is
	// active.
	bool Start();
	bool Stop();
	bool Reset();
	bool Checkpoint();
	bool IsActive();

	void OnTextureCreated(
		ID3D11Texture2D* a_texture,
		const D3D11_TEXTURE2D_DESC& a_desc,
		std::uintptr_t a_caller) noexcept;

	// Records the native FaceGen objects that own a generated tint texture.
	// This is called only by the validated Skyrim VR diagnostic call-site hook.
	void OnFaceGenTintAssigned(
		void* a_tintTextureSlot,
		RE::NiSourceTexture* a_texture,
		RE::BSFaceGenNiNode* a_node,
		RE::BGSHeadPart* a_headPart,
		RE::TESNPC* a_npc) noexcept;

	nlohmann::json BuildStatus();
}
