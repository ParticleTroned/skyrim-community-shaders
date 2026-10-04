#pragma once

#include <utility>

namespace VRHybridCullingLifecycle
{
	/** Retire suppression once; native testing requires replaying any suppressed depth draw first. */
	template <class TryHybrid, class ReplayNative, class Cancel, class NativeProducer, class CapturePose>
	bool RunProducer(bool& a_suppressed, TryHybrid&& a_tryHybrid, ReplayNative&& a_replayNative,
		Cancel&& a_cancel, NativeProducer&& a_nativeProducer, CapturePose&& a_capturePose)
	{
		if (std::exchange(a_suppressed, false)) {
			if (std::forward<TryHybrid>(a_tryHybrid)())
				return true;
			std::forward<ReplayNative>(a_replayNative)();
		}
		std::forward<Cancel>(a_cancel)();
		std::forward<NativeProducer>(a_nativeProducer)();
		std::forward<CapturePose>(a_capturePose)();
		return false;
	}
}
