#pragma once

#include <cstdint>

namespace VRHybridCulling
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	struct StageTiming
	{
		std::uint64_t samples = 0;
		std::uint64_t totalNanoseconds = 0;
		std::uint64_t maximumNanoseconds = 0;
	};

	struct Status
	{
		const char* state = "idle";
		const char* effectiveBackend = "pending";
		const char* fallbackReason = "none";
		const char* historyRejectionReason = "none";
		std::uint64_t submittedBatches = 0;
		std::uint64_t acceptedBatches = 0;
		std::uint64_t invalidatedBatches = 0;
		std::uint64_t fallbackBatches = 0;
		std::uint64_t promotedObjects = 0;
		std::uint64_t unreadableBatches = 0;
		std::uint32_t lastObjectCount = 0;
		StageTiming prepare, dispatch, readback;
	};
#endif

	/** Prepare stereo resources before suppressing the native depth downsample. */
	[[nodiscard]] bool Prepare(std::uint64_t a_epoch);
	/** Write native-indexed visibility and enqueue its existing staging copy. */
	[[nodiscard]] bool Dispatch(void* a_culler, std::uint64_t a_epoch);
	/** Validate the exact submitted batch after native readback, before collection resets it. */
	[[nodiscard]] bool CompleteReadback(void* a_culler, std::uint64_t a_epoch, bool a_selected);
	/** Clear render-thread preparation when the native producer must be used. */
	void CancelPreparation(bool a_hybridSelected, std::uint64_t a_epoch);
	/** Request pipeline recreation on the next render-thread preparation. */
	void ClearShaderCache();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Read current backend state and gated measurements without owning the render context. */
	[[nodiscard]] Status GetStatus(std::uint64_t a_epoch, bool a_selected, bool a_enabled, bool a_installed);
	/** Reset measurements while the shared depth-culling telemetry gate is exclusively held. */
	void ResetTelemetryUnderLock() noexcept;
#endif
}
