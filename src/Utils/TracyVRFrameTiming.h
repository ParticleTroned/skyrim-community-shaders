#pragma once

#include "Utils/OpenVRFrameTiming.h"

#include <Tracy/Tracy.hpp>

#include <cmath>
#include <cstdint>

namespace Util::TracyVRFrameTiming
{
	/** @brief Records distinct OpenVR frame timings as Tracy plots, without inventing elapsed zones. */
	inline void Record(std::uint32_t a_gameFrame)
	{
#if defined(TRACY_ENABLE) && defined(ENABLE_SKYRIM_VR)
		static constexpr std::uint32_t kRetryFrames = 120;
		static constexpr double kMaxTimingMs = 1000.0;
		static std::uint32_t lastFrameIndex = 0;
		static double lastSystemTime = 0.0;
		static std::uint32_t nextRetryFrame = 0;
		static bool disabled = false;

		if (!TracyIsConnected) {
			lastFrameIndex = 0;
			lastSystemTime = 0.0;
			return;
		}
		if (disabled || a_gameFrame == 0 || a_gameFrame < nextRetryFrame)
			return;

		bool faulted = false;
		auto* compositor = OpenVRFrameTiming::TryResolveCompositor(&faulted);
		if (faulted) {
			disabled = true;
			return;
		}
		if (!compositor) {
			nextRetryFrame = a_gameFrame + kRetryFrames;
			return;
		}

		vr::Compositor_FrameTiming timing{};
		timing.m_nSize = static_cast<std::uint32_t>(sizeof(timing));
		if (!OpenVRFrameTiming::TryGetFrameTiming(compositor, &timing, &faulted)) {
			disabled = faulted;
			if (!faulted)
				nextRetryFrame = a_gameFrame + kRetryFrames;
			return;
		}
		if (timing.m_nFrameIndex == 0 || timing.m_nFrameIndex == lastFrameIndex)
			return;

		const auto validMs = [](double a_value) { return std::isfinite(a_value) && a_value > 0.0 && a_value <= kMaxTimingMs; };
		const double poseToSubmitMs = timing.m_flNewFrameReadyMs - timing.m_flWaitGetPosesCalledMs;
		if (validMs(poseToSubmitMs))
			TracyPlot("VR::PoseToSubmitMs", poseToSubmitMs);
		if (validMs(timing.m_flPreSubmitGpuMs))
			TracyPlot("VR::AppPreSubmitGpuMs", timing.m_flPreSubmitGpuMs);
		if (validMs(timing.m_flPostSubmitGpuMs))
			TracyPlot("VR::AppPostSubmitGpuMs", timing.m_flPostSubmitGpuMs);
		if (validMs(timing.m_flTotalRenderGpuMs))
			TracyPlot("VR::TotalRenderGpuMs", timing.m_flTotalRenderGpuMs);
		if (validMs(timing.m_flCompositorRenderGpuMs))
			TracyPlot("VR::CompositorRenderGpuMs", timing.m_flCompositorRenderGpuMs);
		if (validMs(timing.m_flCompositorRenderCpuMs))
			TracyPlot("VR::CompositorRenderCpuMs", timing.m_flCompositorRenderCpuMs);
		if (validMs(timing.m_flClientFrameIntervalMs))
			TracyPlot("VR::ClientFrameIntervalMs", timing.m_flClientFrameIntervalMs);

		TracyPlot("VR::FramePresents", static_cast<std::int64_t>(timing.m_nNumFramePresents));
		TracyPlot("VR::DroppedFrames", static_cast<std::int64_t>(timing.m_nNumDroppedFrames));
		TracyPlot("VR::CompositorFrameIndex", static_cast<std::int64_t>(timing.m_nFrameIndex));
		if (lastFrameIndex != 0 && timing.m_nFrameIndex > lastFrameIndex) {
			const auto advance = timing.m_nFrameIndex - lastFrameIndex;
			TracyPlot("VR::FrameIndexAdvance", static_cast<std::int64_t>(advance));
			const double intervalMs = (timing.m_flSystemTimeInSeconds - lastSystemTime) * 1000.0;
			if (validMs(intervalMs)) {
				TracyPlot("VR::ObservedTimingGapMs", intervalMs);
				if (advance == 1)
					TracyPlot("VR::CompositorFrameIntervalMs", intervalMs);
			}
		}
		lastFrameIndex = timing.m_nFrameIndex;
		lastSystemTime = timing.m_flSystemTimeInSeconds;
#else
		(void)a_gameFrame;
#endif
	}
}
