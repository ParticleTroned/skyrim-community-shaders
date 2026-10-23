#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/ISLDiagnosticsState.h"
#	include <cstddef>
#	include <cstdint>

namespace CSX::Diagnostics::ISL
{
	// clang-format off
#define CS_ISL_METRICS(X) \
    X(LuminanceCalls) \
    X(LuminanceEligible) \
    X(LuminanceNative) \
    X(LuminanceSelf) \
    X(LuminanceDisabled) \
    X(LuminanceFallback) \
    X(LuminanceCustom) \
    X(LuminanceCandidateNonfinite) \
    X(LuminanceNonfinite) \
    X(LuminanceZero) \
    X(LuminancePositive) \
    X(LuminanceNegative) \
    X(ProcessLight) \
    X(SceneLight) \
    X(StrictLight) \
    X(Uninitialized) \
    X(EligibleInverseSquare) \
    X(EligibleShadow) \
    X(Conversion) \
    X(ComputedRadius) \
    X(RadiusWrite) \
    X(RadiusChanged) \
    X(RadiusSuppressed) \
    X(RadiusRestore) \
    X(NonfiniteRadius) \
    X(CreatePointLight) \
    X(MetadataInit) \
    X(MetadataReinit) \
    X(EditorCall) \
    X(EditorOverride) \
    X(EditorHover) \
    X(EditorDisableCheck) \
    X(SceneSnapshot) \
    X(SceneSnapshotHit) \
    X(SceneSnapshotBuild) \
    X(SceneQueueWait) \
    X(SceneQueueHeld) \
    X(SceneActiveLights) \
    X(ScenePendingLights) \
    X(OwnerIndex) \
    X(RetainedShadowRender) \
    X(ParticleQuery) \
    X(ParticleScanSize) \
    X(ParticleHits) \
    X(ParticleSharedWait) \
    X(ParticleSharedHeld) \
    X(ParticleExclusiveWait) \
    X(ParticleExclusiveHeld) \
    X(ParticleEmitters) \
    X(ParticleBillboards) \
    X(ParticleAccepted) \
    X(ParticleNativeWrapper) \
    X(SceneLightsCPU) \
    X(ParticleLightsCPU) \
    X(LightUploadCPU) \
    X(LightMap) \
    X(UploadBytes) \
    X(UploadLights) \
    X(LightCullCPU) \
    X(ShadowUpdate) \
    X(ShadowQueueWait) \
    X(ShadowQueueHeld) \
    X(ShadowCameras) \
    X(ShadowSkip) \
    X(ShadowInactive) \
    X(ShadowMissingSnapshot) \
    X(ShadowStaleSnapshot) \
    X(ShadowCameraMismatch) \
    X(ShadowSourceChanged) \
    X(ShadowPositionChanged) \
    X(ShadowRadiusChanged) \
    X(ShadowClassify) \
    X(ShadowKept) \
    X(ShadowCulled) \
    X(BatchRegister) \
    X(BatchDuplicate) \
    X(BatchReject) \
    X(BatchAllocate) \
    X(BatchRecordGrowth) \
    X(BatchBucketAllocate) \
    X(BatchEntriesGrowth) \
    X(BatchReclaimedRecords) \
    X(BatchReclaim) \
    X(BatchMutexWait) \
    X(BatchMutexHeld) \
    X(BatchNativeRead) \
    X(BatchRender) \
    X(PruneSweep) \
    X(PruneEntries) \
    X(PruneErased) \
    X(PruneWait) \
    X(PruneDeferred) \
    X(EngineUpdate) \
    X(PoseCycle) \
    X(PoseNativeWait) \
    X(PoseMutexWait) \
    X(PoseMutexHeld) \
    X(CycleMutexWait) \
    X(CycleMutexHeld) \
    X(PoseNotify) \
    X(SubmitMutexWait) \
    X(SubmitMutexHeld) \
    X(VRSubmit) \
    X(Present)
	// clang-format on
	enum class Metric : uint16_t
	{
#	define CS_ISL_ENUM(name) name,
		CS_ISL_METRICS(CS_ISL_ENUM)
#	undef CS_ISL_ENUM
			Count
	};
	struct Slot;
	/// Holds one immutable policy snapshot and nonblocking per-thread observation admission.
	class Scope
	{
	public:
		explicit Scope(Metric metric, bool broad = false, uintptr_t caller = 0) noexcept;
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
		void Count(Metric metric, uint64_t value = 1) noexcept;
		void Candidate(float value) noexcept;
		void Unique(const void* identity) noexcept;
		void Finish() noexcept;
		const Policy policy;

	private:
		Slot* slot{};
		Slot* invocationSlot{};
		Metric metric{};
		int64_t enter{};
		uintptr_t caller{};
		uint64_t frame{};
	};
	/// Startup requests are consumed once before light initialization, never from presets.
	void InitializeStartup();
	void Install();
	bool SuppressRadiusWrites() noexcept;
	/// Applies one packed policy at prepass; this does not imply worker quiescence.
	void Prepass(uint64_t frame);
	void ClearFrame() noexcept;
	void RecordCount(Metric metric, uint64_t count) noexcept;
	bool DeferPrune(uint32_t frame);
	/// The same collector owns sidecar admission, drain, freeze and acknowledgement.
	void PrepareSession();
	void PrepareCapture(uint64_t capture, size_t capacity);
	void OpenCapture(int64_t now);
	void CloseCapture();
	uint64_t Outstanding() noexcept;
	void FreezeCapture(int64_t now);
	bool ExportAcknowledged() noexcept;
	void RestoreLive();
	void ExpireSession();
	/// Control-plane exclusion only; it does not alter runtime persistent-state locks.
	bool AcquisitionActive() noexcept;
	void MarkContaminated() noexcept;
}
#	define CS_ISL_SCOPE(name, metric) CSX::Diagnostics::ISL::Scope name(CSX::Diagnostics::ISL::Metric::metric)
#	define CS_ISL_BROAD(name, metric) CSX::Diagnostics::ISL::Scope name(CSX::Diagnostics::ISL::Metric::metric, true)
#	define CS_ISL_COUNT(metric, value) CSX::Diagnostics::ISL::RecordCount(CSX::Diagnostics::ISL::Metric::metric, (value))
#	define CS_ISL_END(name) name.Finish()
#else
#	define CS_ISL_SCOPE(name, metric) ((void)0)
#	define CS_ISL_BROAD(name, metric) ((void)0)
#	define CS_ISL_COUNT(metric, value) ((void)0)
#	define CS_ISL_END(name) ((void)0)
#endif
