#include "VRDepthCullingTemporal.h"

#include "VRDepthCullingTemporalPolicy.h"
#include "VRHybridCulling.h"
#include "VRHybridCullingLifecycle.h"

#include "RE/N/NiCamera.h"
#include "RE/N/NiPoint3.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "GpuPass.h"
#	include "State.h"

#	include <array>
#	include <chrono>
#endif

namespace VRDepthCullingTemporal
{
	namespace
	{
		using namespace VRDepthCullingTemporalPolicy;
		inline constexpr std::size_t kObjectCountOffset = 0xB0;
		inline constexpr std::size_t kTransformsOffset = 0xB8;
		inline constexpr std::size_t kResultSelectorOffset = 0xC0;
		inline constexpr std::size_t kResultsOffset = 0xD0;

		struct ProducerPose
		{
			RE::NiMatrix3 rotation{};
			RE::NiPoint3 translation{};
			bool valid = false;
		};

		struct MotionDelta
		{
			double rotationCosine = -1.0;
			float translationSquared = 0.0f;
		};

		// Both hooks execute in order on the render-depth thread.
		ProducerPose g_producerPose{};
		std::atomic_bool g_installed{ false };
		std::atomic_bool g_hybridInstalled{ false };
		std::atomic_bool g_cullingEnabled{ false };
		std::atomic_uint64_t g_cullingEpoch{ 1 };
		std::atomic_uint64_t g_producerPoseEpoch{ 0 };
		std::atomic<Mode> g_mode{ Mode::Balanced };
		std::atomic_uint64_t g_policyEpoch{ 2 };
		RE::BSSceneGraph* g_downscaleScene = nullptr;
		bool g_suppressedDownscale = false;
		bool g_replayNativeDownscale = false;
		bool g_insideOwnedDownscale = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		VRDepthCullingTelemetryPolicy::WriterGate g_telemetryGate;
		VRDepthCullingTelemetry::TimingCounters g_nativeReadbackTiming, g_outerDownscaleTiming, g_replayDownscaleTiming, g_nativeProducerTiming;
		VRNativeVisibilityTelemetry::Counters g_nativeVisibility;
		std::atomic_uint64_t g_measurementWindowId{ 0 }, g_measurementStartEpoch{ 0 };
		std::atomic_uint32_t g_measurementStartFrame{ 0 };
		std::atomic_bool g_measurementResetting{ false };
		std::atomic_uint64_t g_envelopeMisses{ 0 };
		std::atomic_uint64_t g_recoveryAttempts{ 0 };
		std::atomic_uint64_t g_objectsInspected{ 0 };
		std::atomic_uint64_t g_invalidTransforms{ 0 };
		std::atomic_uint64_t g_invalidMotionEnvelopes{ 0 };
		std::atomic_uint64_t g_frustumTests{ 0 };
		std::atomic_uint64_t g_totalEligible{ 0 };
		std::atomic_uint64_t g_totalPromoted{ 0 };
		std::atomic_uint64_t g_totalDurationNanoseconds{ 0 };
		std::atomic_uint64_t g_maximumDurationNanoseconds{ 0 };
		std::array<std::atomic_uint64_t, Status::DurationBinCount> g_durationHistogram{};
		std::atomic_uint32_t g_lastObjectCount{ 0 };
		std::atomic_uint32_t g_lastEligibleCount{ 0 };
		std::atomic_uint32_t g_lastPromotedCount{ 0 };

		void ClearLastRecoveryStatus()
		{
			g_lastObjectCount.store(0, std::memory_order_relaxed);
			g_lastEligibleCount.store(0, std::memory_order_relaxed);
			g_lastPromotedCount.store(0, std::memory_order_relaxed);
		}

		struct RecoveryTelemetryScope
		{
			RecoveryTelemetryScope() : writer(g_telemetryGate)
			{
				if (!writer)
					return;
				active = true;
				started = std::chrono::steady_clock::now();
			}
			RecoveryTelemetryScope(const RecoveryTelemetryScope&) = delete;
			RecoveryTelemetryScope& operator=(const RecoveryTelemetryScope&) = delete;

			~RecoveryTelemetryScope()
			{
				if (!active)
					return;
				const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started)
						.count());
				g_envelopeMisses.fetch_add(coherenceMisses, std::memory_order_relaxed);
				g_recoveryAttempts.fetch_add(recoveryAttempts, std::memory_order_relaxed);
				g_objectsInspected.fetch_add(objectsInspected, std::memory_order_relaxed);
				g_invalidTransforms.fetch_add(invalidTransforms, std::memory_order_relaxed);
				g_invalidMotionEnvelopes.fetch_add(invalidMotionEnvelopes, std::memory_order_relaxed);
				g_frustumTests.fetch_add(frustumTests, std::memory_order_relaxed);
				g_totalEligible.fetch_add(eligible, std::memory_order_relaxed);
				g_totalPromoted.fetch_add(promoted, std::memory_order_relaxed);
				g_totalDurationNanoseconds.fetch_add(elapsed, std::memory_order_relaxed);
				VRDepthCullingTelemetryPolicy::UpdateMaximum(g_maximumDurationNanoseconds, elapsed);
				g_durationHistogram[VRDepthCullingTelemetryPolicy::DurationBin(elapsed)].fetch_add(1, std::memory_order_relaxed);
			}

			VRDepthCullingTelemetryPolicy::WriterScope writer;
			bool active = false;
			std::chrono::steady_clock::time_point started{};
			std::uint64_t coherenceMisses = 1;
			std::uint64_t recoveryAttempts = 0;
			std::uint64_t objectsInspected = 0;
			std::uint64_t invalidTransforms = 0;
			std::uint64_t invalidMotionEnvelopes = 0;
			std::uint64_t frustumTests = 0;
			std::uint64_t eligible = 0;
			std::uint64_t promoted = 0;
		};
#endif

		bool IsBalancedRecoveryActive(std::uint64_t a_cullingEpoch, std::uint64_t a_policyEpoch)
		{
			return (a_policyEpoch & 1u) == 0 &&
			       g_cullingEnabled.load(std::memory_order_acquire) &&
			       g_mode.load(std::memory_order_acquire) != Mode::Legacy &&
			       g_cullingEpoch.load(std::memory_order_acquire) == a_cullingEpoch &&
			       g_policyEpoch.load(std::memory_order_acquire) == a_policyEpoch;
		}

		template <class T>
		T ReadCullerField(const std::byte* a_culler, std::size_t a_offset)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			T value{};
			std::memcpy(&value, a_culler + a_offset, sizeof(value));
			return value;
		}

		bool TryCalculateMotionDelta(const RE::NiTransform& a_currentView, MotionDelta& a_delta)
		{
			if (!g_producerPose.valid)
				return false;

			double relativeRotationTrace = 0.0;
			for (std::size_t row = 0; row < 3; ++row) {
				for (std::size_t column = 0; column < 3; ++column) {
					relativeRotationTrace += static_cast<double>(g_producerPose.rotation.entry[row][column]) *
					                         static_cast<double>(a_currentView.rotate.entry[row][column]);
				}
			}
			a_delta.rotationCosine = std::clamp((relativeRotationTrace - 1.0) * 0.5, -1.0, 1.0);
			const auto translationDelta = a_currentView.translate - g_producerPose.translation;
			a_delta.translationSquared = translationDelta.SqrLength();
			return std::isfinite(a_delta.rotationCosine) && std::isfinite(a_delta.translationSquared) &&
			       a_delta.translationSquared >= 0.0f;
		}

		void CaptureProducerPose()
		{
			if (!g_cullingEnabled.load(std::memory_order_acquire) ||
				g_mode.load(std::memory_order_acquire) == Mode::Legacy) {
				return;
			}
			const auto cullingEpoch = g_cullingEpoch.load(std::memory_order_acquire);

			const auto* camera = RE::Main::WorldRootCamera();
			if (!camera) {
				g_producerPose.valid = false;
				g_producerPoseEpoch.store(0, std::memory_order_release);
				return;
			}

			g_producerPose.rotation = camera->world.rotate;
			g_producerPose.translation = camera->world.translate;
			g_producerPose.valid = true;
			if (g_cullingEnabled.load(std::memory_order_acquire) &&
				g_cullingEpoch.load(std::memory_order_acquire) == cullingEpoch) {
				g_producerPoseEpoch.store(cullingEpoch, std::memory_order_release);
			}
		}

		void RecoverHighRiskObjects(void* a_culler)
		{
			if (!g_cullingEnabled.load(std::memory_order_acquire) ||
				g_mode.load(std::memory_order_acquire) == Mode::Legacy) {
				return;
			}
			const auto cullingEpoch = g_cullingEpoch.load(std::memory_order_acquire);
			const auto policyEpoch = g_policyEpoch.load(std::memory_order_acquire);
			if ((policyEpoch & 1u) != 0)
				return;
			if (g_producerPoseEpoch.load(std::memory_order_acquire) != cullingEpoch)
				return;

			auto* camera = RE::Main::WorldRootCamera();
			if (!a_culler || !camera)
				return;

			MotionDelta motion{};
			if (!TryCalculateMotionDelta(camera->world, motion))
				return;
			const bool viewCoherent = IsViewCoherent(
				true,
				motion.rotationCosine,
				motion.translationSquared);
			if (viewCoherent)
				return;
#ifdef DEVBENCH_BRIDGE_ENABLED
			RecoveryTelemetryScope telemetry;
			if (telemetry.active)
				ClearLastRecoveryStatus();
#endif
			MotionEnvelope envelope{};
			if (!TryBuildMotionEnvelope(motion.rotationCosine, motion.translationSquared, envelope)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (telemetry.active)
					++telemetry.invalidMotionEnvelopes;
#endif
				return;
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (telemetry.active)
				telemetry.recoveryAttempts = 1;
#endif

			auto* bytes = static_cast<std::byte*>(a_culler);
			const auto objectCount = ReadCullerField<std::uint32_t>(bytes, kObjectCountOffset);
			const auto bufferIndex = ReadCullerField<std::uint32_t>(bytes, kResultSelectorOffset);
			if (objectCount == 0 || objectCount > kMaximumObjects || bufferIndex > 1)
				return;

			auto* transforms = ReadCullerField<OBBTransform*>(bytes, kTransformsOffset);
			auto* results = ReadCullerField<std::uint32_t*>(bytes, kResultsOffset + bufferIndex * sizeof(void*));
			if (!transforms || !results)
				return;

			CandidateSet<kBalancedPromotionBudget> candidates;
#ifdef DEVBENCH_BRIDGE_ENABLED
			std::uint32_t eligibleCount = 0;
#endif
			for (std::uint32_t index = 0; index < objectCount; ++index) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (telemetry.active)
					++telemetry.objectsInspected;
#endif
				if (results[index] != 0)
					continue;

				BoundingSphere sphere{};
				if (!TryBuildBoundingSphere(transforms[index], sphere)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					if (telemetry.active)
						++telemetry.invalidTransforms;
#endif
					continue;
				}
				const RE::NiPoint3 center{ sphere.center[0], sphere.center[1], sphere.center[2] };
				const auto positionDelta = center - camera->world.translate;
				const float distanceSquared = positionDelta.SqrLength();
				float motionExpansion = 0.0f;
				if (!TryCalculateMotionExpansion(
						distanceSquared,
						envelope,
						motionExpansion)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					if (telemetry.active)
						++telemetry.invalidMotionEnvelopes;
#endif
					continue;
				}
				const float expandedRadius = sphere.radius + motionExpansion;
				if (!std::isfinite(expandedRadius) || expandedRadius < sphere.radius) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					if (telemetry.active)
						++telemetry.invalidMotionEnvelopes;
#endif
					continue;
				}

#ifdef DEVBENCH_BRIDGE_ENABLED
				if (telemetry.active)
					++telemetry.frustumTests;
#endif
				const bool directlyVisible = camera->PointInFrustum(center, sphere.radius);
				if (!directlyVisible) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					if (telemetry.active)
						++telemetry.frustumTests;
#endif
					if (!camera->PointInFrustum(center, expandedRadius))
						continue;
				}

#ifdef DEVBENCH_BRIDGE_ENABLED
				++eligibleCount;
#endif
				candidates.Add({ index, CalculateRiskScore(sphere.radius, distanceSquared), directlyVisible });
			}

#ifdef DEVBENCH_BRIDGE_ENABLED
			// Candidate discovery is measured even if a policy change cancels promotion.
			if (telemetry.active)
				telemetry.eligible = eligibleCount;
#endif
			if (!IsBalancedRecoveryActive(cullingEpoch, policyEpoch)) {
				return;
			}

			for (std::size_t index = 0; index < candidates.Size(); ++index)
				results[candidates[index].index] = 1;

#ifdef DEVBENCH_BRIDGE_ENABLED
			const auto promotedCount = static_cast<std::uint32_t>(candidates.Size());
			if (telemetry.active) {
				telemetry.promoted = promotedCount;
				g_lastObjectCount.store(objectCount, std::memory_order_relaxed);
				g_lastEligibleCount.store(eligibleCount, std::memory_order_relaxed);
				g_lastPromotedCount.store(promotedCount, std::memory_order_relaxed);
			}
			if (!IsBalancedRecoveryActive(cullingEpoch, policyEpoch))
				ClearLastRecoveryStatus();
#endif
		}

		struct DepthCullingReadback
		{
			static void thunk(void* a_culler)
			{
				{
#ifdef DEVBENCH_BRIDGE_ENABLED
					const VRDepthCullingTelemetry::Scope telemetry(g_nativeReadbackTiming, g_telemetryGate);
#endif
					func(a_culler);
				}
				const bool hybridSelected = g_cullingEnabled.load(std::memory_order_acquire) &&
				                            g_mode.load(std::memory_order_acquire) == Mode::Hybrid;
				if (!VRHybridCulling::CompleteReadback(a_culler, g_cullingEpoch.load(std::memory_order_acquire), hybridSelected)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					const VRNativeVisibilityTelemetry::Scope visibility(g_nativeVisibility, g_telemetryGate, [a_culler] {
						VRNativeVisibilityTelemetry::Batch batch;
						if (a_culler) {
							auto* bytes = static_cast<std::byte*>(a_culler);
							batch.count = ReadCullerField<std::uint32_t>(bytes, kObjectCountOffset);
							batch.selector = ReadCullerField<std::uint32_t>(bytes, kResultSelectorOffset);
							if (batch.count > 0 && batch.count <= kMaximumObjects && batch.selector <= 1)
								batch.results = ReadCullerField<const std::uint32_t*>(bytes, kResultsOffset + batch.selector * sizeof(void*));
						}
						return batch;
					});
#endif
					RecoverHighRiskObjects(a_culler);
#ifdef DEVBENCH_BRIDGE_ENABLED
					VRHybridCulling::CompleteMatchedRecovery(a_culler, g_cullingEpoch.load(std::memory_order_acquire));
#endif
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DepthCullingDownscale
		{
			static void thunk(RE::BSSceneGraph* a_scene)
			{
				g_downscaleScene = a_scene;
				g_suppressedDownscale = false;
				g_insideOwnedDownscale = true;
				const SKSE::stl::scope_exit restore([]() noexcept { g_insideOwnedDownscale = false; });
#ifdef DEVBENCH_BRIDGE_ENABLED
				const VRDepthCullingTelemetry::Scope telemetry(g_outerDownscaleTiming, g_telemetryGate);
#endif
				func(a_scene);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DepthCullingDownscaleDraw
		{
			static void thunk(RE::ImageSpaceManager* a_manager, std::uint32_t a_effect,
				RE::RENDER_TARGET a_source, RE::RENDER_TARGET a_destination, RE::ImageSpaceEffectParam* a_parameters, bool a_depthSource)
			{
				if (g_insideOwnedDownscale && !g_replayNativeDownscale && a_effect == 100 && static_cast<std::uint32_t>(a_source) == 7 && a_depthSource &&
					g_cullingEnabled.load(std::memory_order_acquire) && g_mode.load(std::memory_order_acquire) == Mode::Hybrid &&
					(g_policyEpoch.load(std::memory_order_acquire) & 1u) == 0 &&
					VRHybridCulling::Prepare(g_cullingEpoch.load(std::memory_order_acquire))) {
					g_suppressedDownscale = true;
					return;
				}
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (g_insideOwnedDownscale && !g_replayNativeDownscale && a_effect == 100 && static_cast<std::uint32_t>(a_source) == 7 && a_depthSource &&
					g_cullingEnabled.load(std::memory_order_acquire) && (g_policyEpoch.load(std::memory_order_acquire) & 1u) == 0 &&
					VRHybridCulling::IsMatchedDiagnosticsActive())
					(void)VRHybridCulling::Prepare(g_cullingEpoch.load(std::memory_order_acquire));
				const VRDepthCullingTelemetryPolicy::WriterScope telemetry(g_telemetryGate);
				if (telemetry) {
					CS_GPU_PASS_SELECT(g_replayNativeDownscale, "VRDepthCulling::ReplayDownscale", "VRDepthCulling::NativeDownscale");
					func(a_manager, a_effect, a_source, a_destination, a_parameters, a_depthSource);
					return;
				}
#endif
				func(a_manager, a_effect, a_source, a_destination, a_parameters, a_depthSource);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DepthCullingRender
		{
			static void thunk(RE::BSImagespaceShader* a_shader, std::uint32_t a_param)
			{
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (!g_suppressedDownscale && g_cullingEnabled.load(std::memory_order_acquire) && VRHybridCulling::IsMatchedDiagnosticsActive()) {
					VRHybridCulling::DispatchMatched(a_shader, g_cullingEpoch.load(std::memory_order_acquire));
					{
						const VRDepthCullingTelemetry::Scope telemetry(g_nativeProducerTiming, g_telemetryGate);
						CS_GPU_PASS("VRDepthCulling::NativeProducer");
						func(a_shader, a_param);
					}
					VRHybridCulling::FinalizeMatchedSubmission(a_shader, g_cullingEpoch.load(std::memory_order_acquire));
					CaptureProducerPose();
					return;
				}
#endif
				VRHybridCullingLifecycle::RunProducer(g_suppressedDownscale, [&] { return g_cullingEnabled.load(std::memory_order_acquire) && g_mode.load(std::memory_order_acquire) == Mode::Hybrid &&
					                                                                      VRHybridCulling::Dispatch(a_shader, g_cullingEpoch.load(std::memory_order_acquire)); }, [] {
						g_replayNativeDownscale = true;
						const SKSE::stl::scope_exit restore([]() noexcept { g_replayNativeDownscale = false; });
#ifdef DEVBENCH_BRIDGE_ENABLED
						const VRDepthCullingTelemetry::Scope telemetry(g_replayDownscaleTiming, g_telemetryGate);
#endif
						DepthCullingDownscale::func(g_downscaleScene); }, [] { VRHybridCulling::CancelPreparation(g_mode.load(std::memory_order_acquire) == Mode::Hybrid &&
																																																																																					g_cullingEnabled.load(std::memory_order_acquire),
																																																																												 g_cullingEpoch.load(std::memory_order_acquire)); }, [&] {
#ifdef DEVBENCH_BRIDGE_ENABLED
						const VRDepthCullingTelemetry::Scope telemetry(g_nativeProducerTiming, g_telemetryGate);
						if (telemetry) {
							CS_GPU_PASS("VRDepthCulling::NativeProducer");
							func(a_shader, a_param);
							return;
						}
#endif
						func(a_shader, a_param); }, [] { CaptureProducerPose(); });
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		bool IsCallInstruction(std::uintptr_t a_address)
		{
			return *reinterpret_cast<const std::uint8_t*>(a_address) == 0xE8;
		}
	}

	void Install()
	{
		if (g_installed.load(std::memory_order_acquire))
			return;
		if (!REL::Module::IsVR())
			return;
		if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
			logger::error(
				"VR: temporal depth-culling recovery not installed for unsupported runtime {}",
				REL::Module::get().version().string());
			return;
		}

		const auto readbackCallsite = REL::Offset(0x132208B).address();
		const auto renderCallsite =
			REL::RelocationID(100421, 107139).address() + REL::Relocate(0x3B1, 0);
		const auto downscaleCallsite = REL::RelocationID(100421, 107139).address() + REL::Relocate(0x37F, 0);
		const auto downscaleDrawCallsite = REL::Offset(0x1322EC2).address();
		if (!IsCallInstruction(readbackCallsite) || !IsCallInstruction(renderCallsite)) {
			logger::error("VR: temporal depth-culling recovery not installed because a hook callsite was not recognized");
			return;
		}

		stl::write_thunk_call<DepthCullingReadback>(readbackCallsite);
		// Frame annotations may already wrap this call; preserve that current target in the thunk chain.
		stl::write_thunk_call<DepthCullingRender>(renderCallsite);
		if (IsCallInstruction(downscaleCallsite) && IsCallInstruction(downscaleDrawCallsite)) {
			stl::write_thunk_call<DepthCullingDownscale>(downscaleCallsite);
			stl::write_thunk_call<DepthCullingDownscaleDraw>(downscaleDrawCallsite);
			g_hybridInstalled.store(true, std::memory_order_release);
		} else {
			logger::warn("VR: Hybrid Hi-Z hooks unavailable; retaining native testing with Advanced recovery");
		}
		g_installed.store(true, std::memory_order_release);
		logger::info(
			"VR: installed temporal depth-culling recovery (default balanced budget {})",
			kBalancedPromotionBudget);
	}

	void SetMode(Mode a_mode)
	{
		a_mode = NormalizeMode(a_mode);
		const auto current = g_mode.load(std::memory_order_acquire);
		if (current == a_mode)
			return;
		// The main-thread writer leaves an odd epoch while publishing a new policy.
		g_policyEpoch.fetch_add(1, std::memory_order_acq_rel);
		g_mode.store(a_mode, std::memory_order_release);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_mode != Mode::Balanced)
			ClearLastRecoveryStatus();
#endif
		// A different backend or policy cannot inherit an earlier producer's validity.
		g_cullingEpoch.fetch_add(1, std::memory_order_acq_rel);
		g_producerPoseEpoch.store(0, std::memory_order_release);
		g_policyEpoch.fetch_add(1, std::memory_order_release);
		logger::info("VR: depth-culling method changed from {} to {}", GetModeName(current), GetModeName(a_mode));
	}

	void SetCullingEnabled(bool a_enabled)
	{
		const bool wasEnabled = g_cullingEnabled.load(std::memory_order_acquire);
		if (wasEnabled == a_enabled)
			return;

		g_cullingEpoch.fetch_add(1, std::memory_order_acq_rel);
		g_cullingEnabled.store(a_enabled, std::memory_order_release);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (!a_enabled)
			ClearLastRecoveryStatus();
#endif
	}

	Mode GetMode()
	{
		return g_mode.load(std::memory_order_acquire);
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	Status GetStatus()
	{
		const bool frozen = g_telemetryGate.IsFrozen();
		const auto policyEpoch = g_policyEpoch.load(std::memory_order_acquire);
		const auto mode = GetMode();
		const bool cullingEnabled = g_cullingEnabled.load(std::memory_order_acquire);
		const bool recoveryActive = cullingEnabled && mode != Mode::Legacy;
		const bool resetting = g_measurementResetting.load(std::memory_order_acquire);
		const auto windowId = g_measurementWindowId.load(std::memory_order_acquire);
		const auto startEpoch = g_measurementStartEpoch.load(std::memory_order_relaxed);
		const auto startFrame = g_measurementStartFrame.load(std::memory_order_relaxed);
		const auto epoch = g_cullingEpoch.load(std::memory_order_acquire);
		Status result{
			.installed = g_installed.load(std::memory_order_acquire),
			.hybridInstalled = g_hybridInstalled.load(std::memory_order_acquire),
			.cullingEnabled = cullingEnabled,
			.telemetryEnabled = g_telemetryGate.IsEnabled(),
			.telemetryFrozen = frozen,
			.mode = mode,
			.cullingEpoch = epoch,
			.measurementWindowId = windowId,
			.measurementStartEpoch = startEpoch,
			.measurementStartFrame = startFrame,
			.nativeReadback = g_nativeReadbackTiming.Read(),
			.outerDownscale = g_outerDownscaleTiming.Read(),
			.replayDownscale = g_replayDownscaleTiming.Read(),
			.nativeProducer = g_nativeProducerTiming.Read(),
			.nativeVisibility = g_nativeVisibility.Read(),
			.envelopeMisses = g_envelopeMisses.load(std::memory_order_relaxed),
			.recoveryAttempts = g_recoveryAttempts.load(std::memory_order_relaxed),
			.objectsInspected = g_objectsInspected.load(std::memory_order_relaxed),
			.invalidTransforms = g_invalidTransforms.load(std::memory_order_relaxed),
			.invalidMotionEnvelopes = g_invalidMotionEnvelopes.load(std::memory_order_relaxed),
			.frustumTests = g_frustumTests.load(std::memory_order_relaxed),
			.totalEligible = g_totalEligible.load(std::memory_order_relaxed),
			.totalPromoted = g_totalPromoted.load(std::memory_order_relaxed),
			.totalDurationNanoseconds = g_totalDurationNanoseconds.load(std::memory_order_relaxed),
			.maximumDurationNanoseconds = g_maximumDurationNanoseconds.load(std::memory_order_relaxed),
			.durationHistogram = [&] {
				std::array<std::uint64_t, Status::DurationBinCount> bins{};
				for (std::size_t index = 0; index < bins.size(); ++index)
					bins[index] = g_durationHistogram[index].load(std::memory_order_relaxed);
				return bins;
			}(),
			.lastObjectCount = recoveryActive ? g_lastObjectCount.load(std::memory_order_relaxed) : 0,
			.lastEligibleCount = recoveryActive ? g_lastEligibleCount.load(std::memory_order_relaxed) : 0,
			.lastPromotedCount = recoveryActive ? g_lastPromotedCount.load(std::memory_order_relaxed) : 0,
		};
		result.measurementWindowCurrent = windowId != 0 && startEpoch == epoch && !resetting &&
		                                  !g_measurementResetting.load(std::memory_order_acquire) &&
		                                  windowId == g_measurementWindowId.load(std::memory_order_acquire) &&
		                                  epoch == g_cullingEpoch.load(std::memory_order_acquire) &&
		                                  (policyEpoch & 1u) == 0 && policyEpoch == g_policyEpoch.load(std::memory_order_acquire);
		return result;
	}

	VRDepthCullingTelemetryPolicy::WriterGate& GetTelemetryGate() noexcept
	{
		return g_telemetryGate;
	}

	void SetTelemetryEnabled(bool a_enabled)
	{
		g_telemetryGate.SetEnabled(a_enabled);
	}

	bool TryResetStatus()
	{
		return VRDepthCullingTelemetryPolicy::TryReset(g_telemetryGate, []() noexcept {
			g_measurementResetting.store(true, std::memory_order_release);
			g_nativeReadbackTiming.Reset();
			g_outerDownscaleTiming.Reset();
			g_replayDownscaleTiming.Reset();
			g_nativeProducerTiming.Reset();
			g_nativeVisibility.Reset();
			g_envelopeMisses.store(0, std::memory_order_relaxed);
			g_recoveryAttempts.store(0, std::memory_order_relaxed);
			g_objectsInspected.store(0, std::memory_order_relaxed);
			g_invalidTransforms.store(0, std::memory_order_relaxed);
			g_invalidMotionEnvelopes.store(0, std::memory_order_relaxed);
			g_frustumTests.store(0, std::memory_order_relaxed);
			g_totalEligible.store(0, std::memory_order_relaxed);
			g_totalPromoted.store(0, std::memory_order_relaxed);
			g_totalDurationNanoseconds.store(0, std::memory_order_relaxed);
			g_maximumDurationNanoseconds.store(0, std::memory_order_relaxed);
			for (auto& bin : g_durationHistogram)
				bin.store(0, std::memory_order_relaxed);
			ClearLastRecoveryStatus();
			VRHybridCulling::ResetTelemetryUnderLock();
			g_measurementStartEpoch.store(g_cullingEpoch.load(std::memory_order_acquire), std::memory_order_relaxed);
			g_measurementStartFrame.store(globals::state ? globals::state->frameCount : 0, std::memory_order_relaxed);
			g_measurementWindowId.fetch_add(1, std::memory_order_release);
			g_measurementResetting.store(false, std::memory_order_release);
		});
	}
#endif
}
