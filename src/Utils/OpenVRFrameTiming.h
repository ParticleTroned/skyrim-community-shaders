#pragma once

#include "RE/B/BSOpenVR.h"

#include <cstdint>

#ifdef ENABLE_SKYRIM_VR
namespace Util::OpenVRFrameTiming
{
	/** @brief Resolves Skyrim VR's compositor without propagating a stale runtime pointer fault. */
	inline vr::IVRCompositor* TryResolveCompositor(bool* a_faulted)
	{
		if (a_faulted)
			*a_faulted = false;

		vr::IVRCompositor* compositor = nullptr;
		__try {
			auto* openvr = RE::BSOpenVR::GetSingleton();
			compositor = openvr ? RE::BSOpenVR::GetIVRCompositor() : nullptr;
			if (!compositor && openvr)
				compositor = openvr->vrContext.vrCompositor;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			if (a_faulted)
				*a_faulted = true;
			compositor = nullptr;
		}
		return compositor;
	}

	/** @brief Reads a compositor frame without trusting a pointer after an access fault. */
	inline bool TryGetFrameTiming(vr::IVRCompositor* a_compositor, vr::Compositor_FrameTiming* a_timing, bool* a_faulted)
	{
		if (a_faulted)
			*a_faulted = false;
		if (!a_compositor || !a_timing)
			return false;

		bool result = false;
		__try {
			result = a_compositor->GetFrameTiming(a_timing, 0);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			if (a_faulted)
				*a_faulted = true;
		}
		return result;
	}

#	ifdef TRACY_ENABLE
	/** @brief Copies a bounded, oldest-first compositor timing history. */
	inline std::uint32_t TryGetFrameTimings(
		vr::IVRCompositor* a_compositor,
		vr::Compositor_FrameTiming* a_timings,
		std::uint32_t a_capacity,
		bool* a_faulted)
	{
		if (a_faulted)
			*a_faulted = false;
		if (!a_compositor || !a_timings || a_capacity == 0)
			return 0;

		a_timings[0].m_nSize = static_cast<std::uint32_t>(sizeof(*a_timings));
		std::uint32_t count = 0;
		__try {
			count = a_compositor->GetFrameTimings(a_timings, a_capacity);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			if (a_faulted)
				*a_faulted = true;
		}
		return count <= a_capacity ? count : 0;
	}
#	endif
}
#endif
