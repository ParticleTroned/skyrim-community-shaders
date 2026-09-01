#pragma once

#include "VRSubmitInputFreshnessPolicy.h"

#include <openvr.h>

namespace VRSubmitInputFreshnessPolicy
{
	struct SubmitTextureIdentity
	{
		std::uintptr_t descriptor = 0;
		std::uintptr_t resource = 0;
		bool directX = false;
	};

	/** Captures descriptor and DirectX resource identities before nested submits. */
	[[nodiscard]] inline SubmitTextureIdentity CaptureSubmitTextureIdentity(
		const vr::Texture_t* a_texture) noexcept
	{
		return {
			.descriptor = reinterpret_cast<std::uintptr_t>(a_texture),
			.resource = a_texture && a_texture->eType == vr::TextureType_DirectX ?
			                reinterpret_cast<std::uintptr_t>(a_texture->handle) :
			                0,
			.directX = a_texture && a_texture->eType == vr::TextureType_DirectX,
		};
	}

	struct OuterPairBoundaryState
	{
		std::uint64_t token = 0;
		std::uint64_t compositorCycle = 0;
		std::uint32_t frame = 0;
		std::uint32_t thread = 0;
		std::uint32_t flags = 0;
		SubmitTextureIdentity source{};
		bool active = false;
	};

	/** Builds the nested observation from the same typed extraction as the outer hook. */
	[[nodiscard]] inline OuterBoundaryObservation ObserveNestedSubmit(
		const OuterPairBoundaryState& a_boundary,
		const vr::Texture_t* a_texture,
		std::uint64_t a_compositorCycle,
		std::uint32_t a_frame,
		std::uint32_t a_thread,
		vr::EVRSubmitFlags a_flags) noexcept
	{
		const auto nestedSource = CaptureSubmitTextureIdentity(a_texture);
		return {
			.expectedToken = a_boundary.active ? a_boundary.token : 0,
			.activeToken = a_boundary.token,
			.activeCompositorCycle = a_boundary.compositorCycle,
			.currentCompositorCycle = a_compositorCycle,
			.activeFrame = a_boundary.frame,
			.currentFrame = a_frame,
			.activeThread = a_boundary.thread,
			.currentThread = a_thread,
			.activeFlags = a_boundary.flags,
			.currentFlags = static_cast<std::uint32_t>(a_flags),
			.activeTextureIdentity = a_boundary.source.descriptor,
			.activeHandleIdentity = a_boundary.source.resource,
			.nestedTextureIdentity = nestedSource.descriptor,
			.nestedHandleIdentity = nestedSource.resource,
			.activeTextureIsDirectX = a_boundary.source.directX,
			.nestedTextureIsDirectX = nestedSource.directX,
		};
	}
}
