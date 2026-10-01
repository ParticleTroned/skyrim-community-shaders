#pragma once

#include "VRRenderScaleFrameBoundaryPolicy.h"

#include <array>
#include <cstdint>
#include <openvr.h>

struct ID3D11Texture2D;

namespace ImGuiVRHelperHost
{
	using PairIdentity = VRRenderScaleFrameBoundaryPolicy::PairIdentity;

	/// Negotiate once after all SKSE listeners exist; a missing/older helper leaves rendering unchanged.
	void Initialize();
	/// Update content demand on the render thread without polling modules or installing helper hooks.
	void Tick();
	[[nodiscard]] bool IsAvailable() noexcept;
	[[nodiscard]] bool HasContent() noexcept;
	/// True only while this boundary owns a complete current source capture.
	[[nodiscard]] bool CanComposePair() noexcept;
	[[nodiscard]] bool HasWorldScene() noexcept;
	/// Activate/deactivate ownership only outside a pair; close the pair before render-target relatching.
	void BeginPair(const PairIdentity& a_pair, ID3D11Texture2D* a_nativeSource);
	void EndPair() noexcept;

	/// Compose both current eyes to private scratch surfaces before exposing either; failure preserves originals.
	[[nodiscard]] bool ComposePair(const std::array<vr::Texture_t, 2>& a_sources,
		const std::array<vr::VRTextureBounds_t, 2>& a_bounds, bool a_reconstructed);
	/// Return a prepared eye only for the exact admitted source, bounds and resource generation.
	[[nodiscard]] ID3D11Texture2D* GetPreparedEye(std::uint32_t a_eye,
		const vr::Texture_t& a_source, const vr::VRTextureBounds_t* a_bounds) noexcept;
	[[nodiscard]] bool IsPairAttempted() noexcept;
	/// Once an undecorated eye is submitted, later eyes cannot start composition for that pair.
	void NoteSubmission() noexcept;
	void SkipPair() noexcept;
}
