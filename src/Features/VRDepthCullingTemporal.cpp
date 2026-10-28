#include "VRDepthCullingTemporal.h"

#include "State.h"
#include "Utils/D3D.h"
#include "VRDepthCullingSubmissionPolicy.h"
#include "VRDepthCullingTemporalPolicy.h"

#include "RE/N/NiCamera.h"
#include "RE/N/NiPoint3.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <chrono>
#endif

namespace VRDepthCullingTemporal
{
	namespace
	{
		using namespace VRDepthCullingTemporalPolicy;
		namespace SubmissionPolicy = VRDepthCullingSubmissionPolicy;
		using SubmissionPolicy::Reason;
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

		struct DepthEvidence
		{
			std::array<SubmissionPolicy::Eye, 2> eyes{};
			std::array<SubmissionPolicy::Frustum, 2> readbackFrusta{};
			SubmissionPolicy::Scene scene{};
			std::uint64_t resourceGeneration = 0;
			std::uint32_t frame = 0;
			std::uint32_t sourceWidth = 0, sourceHeight = 0, activeWidth = 0, activeHeight = 0;
			std::uintptr_t sourceView = 0, reducedTarget = 0, camera = 0;
			SubmissionPolicy::DepthConvention convention = SubmissionPolicy::DepthConvention::Standard;
			Reason reason = Reason::MissingDepth;
		};

		struct DepthResourceCache
		{
			winrt::com_ptr<ID3D11ShaderResourceView> source;
			winrt::com_ptr<ID3D11Texture2D> reduced;
			std::uint64_t generation = 0;
			D3D11_TEXTURE2D_DESC sourceDesc{}, reducedDesc{};
			bool valid = false;
		};

		struct MotionDelta
		{
			double rotationCosine = -1.0;
			float translationSquared = 0.0f;
		};

		// These hooks execute in order on the render-depth thread.
		ProducerPose g_producerPose{};
		DepthEvidence g_preparedDepth{}, g_producerDepth{};
		DepthResourceCache g_depthResources{};
		SubmissionPolicy::Handoff g_handoff{};
		SubmissionPolicy::ReadbackProjection g_pendingReadbackProjection{};
		bool g_depthPrepared = false;
		std::atomic_bool g_installed{ false };
		std::atomic_bool g_cullingEnabled{ false };
		std::atomic_uint64_t g_cullingEpoch{ 1 };
		std::atomic_uint64_t g_producerPoseEpoch{ 0 };
		std::atomic<Mode> g_mode{ Mode::Balanced };
		std::atomic_uint64_t g_policyEpoch{ 2 };
#ifdef DEVBENCH_BRIDGE_ENABLED
		VRDepthCullingTelemetryPolicy::WriterGate g_telemetryGate;
		std::atomic_uint64_t g_envelopeMisses{ 0 };
		std::atomic_uint64_t g_recoveryAttempts{ 0 };
		std::atomic_uint64_t g_objectsInspected{ 0 };
		std::atomic_uint64_t g_invalidTransforms{ 0 };
		std::atomic_uint64_t g_invalidMotionEnvelopes{ 0 };
		std::atomic_uint64_t g_frustumTests{ 0 };
		std::atomic_uint64_t g_totalEligible{ 0 };
		std::atomic_uint64_t g_totalPromoted{ 0 };
		std::atomic_uint64_t g_motionExpansions{ 0 }, g_budgetSaturated{ 0 }, g_eligibleUnpromoted{ 0 };
		std::atomic_uint64_t g_directEligible{ 0 }, g_expandedEligible{ 0 }, g_transitionPromoted{ 0 };
		std::array<std::atomic_uint64_t, static_cast<std::size_t>(Reason::Count)> g_submissionReasons{};
		std::atomic_uint32_t g_lastProducerAge{ 0 };
		std::atomic_bool g_lastProducerAgeKnown{ false };
		std::atomic<Reason> g_lastSubmissionReason{ Reason::MissingProducer };
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

		void UpdateMaximum(std::atomic_uint64_t& a_target, std::uint64_t a_value)
		{
			auto current = a_target.load(std::memory_order_relaxed);
			while (current < a_value &&
				   !a_target.compare_exchange_weak(
					   current, a_value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
		}

		struct RecoveryTelemetryScope
		{
			RecoveryTelemetryScope()
			{
				if (!g_telemetryGate.TryEnter())
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
				g_motionExpansions.fetch_add(motionExpansions, std::memory_order_relaxed);
				g_budgetSaturated.fetch_add(eligible > kBalancedPromotionBudget, std::memory_order_relaxed);
				g_eligibleUnpromoted.fetch_add(eligible - promoted, std::memory_order_relaxed);
				g_directEligible.fetch_add(directEligible, std::memory_order_relaxed);
				g_expandedEligible.fetch_add(eligible - directEligible, std::memory_order_relaxed);
				g_totalDurationNanoseconds.fetch_add(elapsed, std::memory_order_relaxed);
				UpdateMaximum(g_maximumDurationNanoseconds, elapsed);
				g_durationHistogram[VRDepthCullingTelemetryPolicy::DurationBin(elapsed)].fetch_add(1, std::memory_order_relaxed);
				g_telemetryGate.Leave();
			}

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
			std::uint64_t motionExpansions = 0;
			std::uint64_t directEligible = 0;
		};

		void RecordSubmission(Reason a_reason, const SubmissionPolicy::Submission& a_stamp, std::uint32_t a_promoted = 0)
		{
			if (!g_telemetryGate.TryEnter())
				return;
			const bool ageKnown = a_stamp.produced && a_stamp.frameKnown && globals::state;
			const auto age = ageKnown ? SubmissionPolicy::Age(a_stamp.frame, globals::state->frameCount) : 0;
			g_submissionReasons[static_cast<std::size_t>(a_reason)].fetch_add(1, std::memory_order_relaxed);
			g_lastSubmissionReason.store(a_reason, std::memory_order_relaxed);
			g_lastProducerAge.store(age, std::memory_order_relaxed);
			g_lastProducerAgeKnown.store(ageKnown, std::memory_order_relaxed);
			g_transitionPromoted.fetch_add(a_promoted, std::memory_order_relaxed);
			g_telemetryGate.Leave();
		}
#endif

		bool IsBalancedRecoveryActive(std::uint64_t a_cullingEpoch, std::uint64_t a_policyEpoch)
		{
			return (a_policyEpoch & 1u) == 0 &&
			       g_cullingEnabled.load(std::memory_order_acquire) &&
			       g_mode.load(std::memory_order_acquire) == Mode::Balanced &&
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

		SubmissionPolicy::Batch ReadBatch(void* a_culler)
		{
			if (!a_culler)
				return {};
			const auto* bytes = static_cast<const std::byte*>(a_culler);
			const auto selector = ReadCullerField<std::uint32_t>(bytes, kResultSelectorOffset);
			return {
				.culler = reinterpret_cast<std::uintptr_t>(a_culler),
				.transforms = ReadCullerField<std::uintptr_t>(bytes, kTransformsOffset),
				.results = selector <= 1 ? ReadCullerField<std::uintptr_t>(bytes, kResultsOffset + selector * sizeof(void*)) : 0,
				.count = ReadCullerField<std::uint32_t>(bytes, kObjectCountOffset),
				.selector = selector
			};
		}

		bool CaptureEyes(RE::NiCamera* a_camera, DepthEvidence& a_evidence)
		{
			if (!a_camera || !globals::game::graphicsState)
				return false;
			using FindCamera = const std::byte* (*)(RE::BSGraphics::State*, RE::NiCamera*, bool);
			static REL::Relocation<FindCamera> findCamera{ REL::Offset(0xDD1AD0) };
			const auto* cache = findCamera(globals::game::graphicsState, a_camera, false);
			// VR's cache has BSTArray members; the ALL-runtime convenience layout does not.
			if (!cache || ReadCullerField<std::uint32_t>(cache, 0x6C) != 2 ||
				ReadCullerField<std::uint32_t>(cache, 0x18) != 2 || ReadCullerField<std::uint32_t>(cache, 0x30) != 2)
				return false;
			const auto* views = ReadCullerField<const RE::BSGraphics::ViewData*>(cache, 0x08);
			const auto* positions = ReadCullerField<const RE::NiPoint3*>(cache, 0x20);
			if (!views || !positions)
				return false;
			for (std::size_t eye = 0; eye < a_evidence.eyes.size(); ++eye) {
				auto& output = a_evidence.eyes[eye];
				std::memcpy(output.view.data(), &views[eye].viewMat, sizeof(output.view));
				std::memcpy(output.projection.data(), &views[eye].projMatrixUnjittered, sizeof(output.projection));
				std::memcpy(output.viewProjection.data(), &views[eye].viewProjMat, sizeof(output.viewProjection));
				std::memcpy(output.position.data(), &positions[eye], sizeof(output.position));
				std::memcpy(output.viewport.data(), views[eye].viewPort, sizeof(output.viewport));
				std::memcpy(output.depthRange.data(), &views[eye].viewDepthRange, sizeof(output.depthRange));
				if (!SubmissionPolicy::IsValid(output))
					return false;
			}
			return true;
		}

		SubmissionPolicy::ReadbackProjection CaptureReadbackProjection(std::uint64_t a_cullingEpoch, std::uint64_t a_policyEpoch)
		{
			auto* camera = RE::Main::WorldRootCamera();
			if (!camera || !camera->GetVRRuntimeData().viewFrustumArray)
				return {};
			SubmissionPolicy::ReadbackProjection projection{ {}, reinterpret_cast<std::uintptr_t>(camera), a_cullingEpoch, a_policyEpoch };
			const auto* frusta = camera->GetVRRuntimeData().viewFrustumArray;
			for (std::size_t eye = 0; eye < projection.frusta.size(); ++eye) {
				const auto& native = frusta[eye];
				projection.frusta[eye] = { { native.fLeft, native.fRight, native.fTop, native.fBottom, native.fNear, native.fFar }, native.bOrtho };
			}
			return projection;
		}

		void CaptureDepthEvidence(DepthEvidence& a_evidence)
		{
			a_evidence = {};
			if (!g_cullingEnabled.load(std::memory_order_acquire) || g_mode.load(std::memory_order_acquire) != Mode::Balanced)
				return;
			auto* renderer = globals::game::renderer;
			auto* graphics = globals::game::graphicsState;
			if (!renderer || !graphics || !globals::state)
				return;
			const auto generation = globals::state->GetRenderTargetResourcePublicationGeneration();
			if (!generation || generation != globals::state->GetCompletedRenderTargetResourcePublicationGeneration())
				return;
			auto& targets = renderer->GetDepthStencilData().depthStencils;
			auto* source = targets[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;
			auto* reduced = targets[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_DOWNSAMPLE].texture;
			auto& cache = g_depthResources;
			if (cache.generation != generation || cache.source.get() != source || cache.reduced.get() != reduced) {
				cache = {};
				cache.generation = generation;
				cache.source.copy_from(source);
				cache.reduced.copy_from(reduced);
				D3D11_SHADER_RESOURCE_VIEW_DESC view{};
				if (source && reduced && Util::GetTexture2DDesc(source, cache.sourceDesc)) {
					source->GetDesc(&view);
					reduced->GetDesc(&cache.reducedDesc);
					cache.valid = cache.sourceDesc.Width > 1 && cache.sourceDesc.Height > 0 && cache.sourceDesc.Width % 2 == 0 &&
					              cache.sourceDesc.ArraySize == 1 && cache.sourceDesc.SampleDesc.Count == 1 &&
					              view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && view.Texture2D.MostDetailedMip == 0 &&
					              (view.Format == DXGI_FORMAT_R32_FLOAT || view.Format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS) &&
					              cache.reducedDesc.Width > 1 && cache.reducedDesc.Width % 2 == 0 && cache.reducedDesc.Height > 0 &&
					              cache.reducedDesc.ArraySize == 1 && cache.reducedDesc.SampleDesc.Count == 1;
				}
			}
			if (!cache.valid)
				return;
			auto& evidence = a_evidence;
			evidence.frame = globals::state->frameCount;
			evidence.resourceGeneration = generation;
			evidence.sourceView = reinterpret_cast<std::uintptr_t>(source);
			evidence.reducedTarget = reinterpret_cast<std::uintptr_t>(reduced);
			evidence.sourceWidth = cache.sourceDesc.Width;
			evidence.sourceHeight = cache.sourceDesc.Height;
			const auto& dynamic = graphics->GetRuntimeData();
			const float widthRatio = dynamic.dynamicResolutionLock ? 1 : dynamic.dynamicResolutionWidthRatio;
			const float heightRatio = dynamic.dynamicResolutionLock ? 1 : dynamic.dynamicResolutionHeightRatio;
			if (!std::isfinite(widthRatio) || !std::isfinite(heightRatio) || widthRatio <= 0 || widthRatio > 1 || heightRatio <= 0 || heightRatio > 1) {
				evidence.reason = Reason::LayoutChanged;
				return;
			}
			evidence.activeWidth = static_cast<std::uint32_t>(evidence.sourceWidth * widthRatio);
			evidence.activeHeight = static_cast<std::uint32_t>(evidence.sourceHeight * heightRatio);
			if (!SubmissionPolicy::IsSupportedStereoLayout(evidence.sourceWidth, evidence.sourceHeight, evidence.activeWidth, evidence.activeHeight)) {
				evidence.reason = Reason::LayoutChanged;
				return;
			}
			const auto* tes = globals::game::tes;
			if (tes) {
				evidence.scene.interiorCell = reinterpret_cast<std::uintptr_t>(tes->interiorCell);
				evidence.scene.worldSpace = tes->interiorCell ? 0 : reinterpret_cast<std::uintptr_t>(tes->GetRuntimeData2().worldSpace);
			}
			if (!SubmissionPolicy::IsValid(evidence.scene)) {
				evidence.reason = Reason::InvalidView;
				return;
			}
			auto* camera = RE::Main::WorldRootCamera();
			evidence.camera = reinterpret_cast<std::uintptr_t>(camera);
			evidence.reason = CaptureEyes(camera, evidence) ? Reason::None : Reason::InvalidView;
		}

		bool IsDepthPreparationCurrent()
		{
			if (!globals::state || !globals::game::renderer || g_preparedDepth.frame != globals::state->frameCount ||
				g_preparedDepth.resourceGeneration != globals::state->GetRenderTargetResourcePublicationGeneration() ||
				g_preparedDepth.resourceGeneration != globals::state->GetCompletedRenderTargetResourcePublicationGeneration())
				return false;
			const auto& targets = globals::game::renderer->GetDepthStencilData().depthStencils;
			return g_preparedDepth.sourceView == reinterpret_cast<std::uintptr_t>(targets[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV) &&
			       g_preparedDepth.reducedTarget == reinterpret_cast<std::uintptr_t>(targets[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_DOWNSAMPLE].texture);
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
			if (!std::isfinite(relativeRotationTrace))
				return false;
			a_delta.rotationCosine = std::clamp((relativeRotationTrace - 1.0) * 0.5, -1.0, 1.0);
			const auto translationDelta = a_currentView.translate - g_producerPose.translation;
			a_delta.translationSquared = translationDelta.SqrLength();
			return std::isfinite(a_delta.rotationCosine) && std::isfinite(a_delta.translationSquared) &&
			       a_delta.translationSquared >= 0.0f;
		}

		void CaptureProducerPose()
		{
			g_producerPose.valid = false;
			if (!g_cullingEnabled.load(std::memory_order_acquire) ||
				g_mode.load(std::memory_order_acquire) == Mode::Legacy) {
				return;
			}
			const auto cullingEpoch = g_cullingEpoch.load(std::memory_order_acquire);

			const auto* camera = RE::Main::WorldRootCamera();
			if (camera) {
				g_producerPose.rotation = camera->world.rotate;
				g_producerPose.translation = camera->world.translate;
				g_producerPose.valid = true;
			}
			if (g_cullingEnabled.load(std::memory_order_acquire) &&
				g_cullingEpoch.load(std::memory_order_acquire) == cullingEpoch) {
				g_producerPoseEpoch.store(cullingEpoch, std::memory_order_release);
			}
		}

		void RecoverHighRiskObjects(const SubmissionPolicy::Batch& a_batch, const SubmissionPolicy::Handoff::Consumed& a_consumed,
			const SubmissionPolicy::ReadbackProjection& a_currentProjection)
		{
			if (!g_cullingEnabled.load(std::memory_order_acquire) ||
				g_mode.load(std::memory_order_acquire) != Mode::Balanced) {
				return;
			}
			const auto cullingEpoch = g_cullingEpoch.load(std::memory_order_acquire);
			const auto policyEpoch = g_policyEpoch.load(std::memory_order_acquire);
			if ((policyEpoch & 1u) != 0)
				return;
			auto* camera = RE::Main::WorldRootCamera();
			const auto& stamp = a_consumed.stamp;
			auto reason = SubmissionPolicy::Match(stamp, a_batch, a_consumed.sequence, cullingEpoch, policyEpoch);
			if (reason != Reason::None) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				RecordSubmission(reason, stamp);
#endif
				return;
			}
			if (g_producerPoseEpoch.load(std::memory_order_acquire) != cullingEpoch) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				RecordSubmission(Reason::EpochMismatch, stamp);
#endif
				return;
			}

			MotionDelta motion{};
			reason = g_producerDepth.reason;
			if (reason == Reason::None && (!camera || g_producerDepth.camera != reinterpret_cast<std::uintptr_t>(camera) ||
											  !TryCalculateMotionDelta(camera->world, motion)))
				reason = Reason::InvalidView;
			DepthEvidence currentDepth{};
			if (reason == Reason::None) {
				// Revalidate replaced resources; compatible packed stereo sizes need not match.
				CaptureDepthEvidence(currentDepth);
				reason = currentDepth.reason;
			}
			if (reason == Reason::None && g_producerDepth.scene != currentDepth.scene)
				reason = Reason::SceneChanged;
			if (reason == Reason::None && !SubmissionPolicy::Matches(a_currentProjection, currentDepth.camera, cullingEpoch, policyEpoch))
				reason = Reason::InvalidView;
			if (reason == Reason::None) {
				for (std::size_t eye = 0; eye < currentDepth.eyes.size(); ++eye) {
					if (!SubmissionPolicy::SameProjection(g_producerDepth.eyes[eye], currentDepth.eyes[eye]) ||
						!SubmissionPolicy::SameProjection(g_producerDepth.readbackFrusta[eye], a_currentProjection.frusta[eye]))
						reason = Reason::ProjectionChanged;
				}
			}
			if (reason == Reason::None && SubmissionPolicy::IsCameraCut(motion.rotationCosine, motion.translationSquared))
				reason = Reason::CameraCut;
			if (reason != Reason::None) {
				// Only this consumed, matched registration owns these slots. The caller resets it next.
				if (!IsBalancedRecoveryActive(cullingEpoch, policyEpoch))
					return;
				auto* results = reinterpret_cast<std::uint32_t*>(a_batch.results);
				[[maybe_unused]] const auto promoted = SubmissionPolicy::RecoverDiscontinuity(a_consumed, a_batch, cullingEpoch, policyEpoch,
					{ results, a_batch.count });
#ifdef DEVBENCH_BRIDGE_ENABLED
				RecordSubmission(reason, stamp, promoted);
#endif
				return;
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			RecordSubmission(g_producerDepth.resourceGeneration != currentDepth.resourceGeneration ||
									 g_producerDepth.sourceView != currentDepth.sourceView || g_producerDepth.reducedTarget != currentDepth.reducedTarget ?
								 Reason::CompatibleResourceChange :
								 Reason::None,
				stamp);
#endif
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

			const auto objectCount = a_batch.count;
			const auto* transforms = reinterpret_cast<const OBBTransform*>(a_batch.transforms);
			auto* results = reinterpret_cast<std::uint32_t*>(a_batch.results);

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
				const auto relevance = ClassifySphere(sphere.radius, distanceSquared, envelope, [&](float radius) {
#ifdef DEVBENCH_BRIDGE_ENABLED
						if (telemetry.active) ++telemetry.frustumTests;
#endif
						return camera->PointInFrustum(center, radius); }, [&] {
#ifdef DEVBENCH_BRIDGE_ENABLED
					if (telemetry.active)
						++telemetry.motionExpansions;
#endif
				});
				if (relevance == Relevance::Invalid) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					if (telemetry.active)
						++telemetry.invalidMotionEnvelopes;
#endif
					continue;
				}
				if (relevance == Relevance::Excluded)
					continue;
				const bool directlyVisible = relevance == Relevance::Direct;

#ifdef DEVBENCH_BRIDGE_ENABLED
				++eligibleCount;
				if (telemetry.active)
					telemetry.directEligible += directlyVisible;
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
				g_pendingReadbackProjection = {};
				const auto consumed = g_handoff.Consume();
				g_depthPrepared = false;
				if (!g_cullingEnabled.load(std::memory_order_acquire) || g_mode.load(std::memory_order_acquire) != Mode::Balanced) {
					func(a_culler);
					return;
				}
				const auto cullingEpoch = g_cullingEpoch.load(std::memory_order_acquire);
				const auto policyEpoch = g_policyEpoch.load(std::memory_order_acquire);
				const auto projection = CaptureReadbackProjection(cullingEpoch, policyEpoch);
				const auto batch = ReadBatch(a_culler);
				func(a_culler);
				if (ReadBatch(a_culler) == batch)
					RecoverHighRiskObjects(batch, consumed, projection);
#ifdef DEVBENCH_BRIDGE_ENABLED
				else
					RecordSubmission(Reason::BatchMismatch, consumed.stamp);
#endif
				if (IsBalancedRecoveryActive(cullingEpoch, policyEpoch))
					g_pendingReadbackProjection = projection;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DepthCullingRender
		{
			static void thunk(RE::BSImagespaceShader* a_shader, std::uint32_t a_param)
			{
				g_handoff.Invalidate();
				if (!g_cullingEnabled.load(std::memory_order_acquire) || g_mode.load(std::memory_order_acquire) != Mode::Balanced) {
					func(a_shader, a_param);
					return;
				}
				const auto batch = ReadBatch(a_shader);
				const auto cullingEpoch = g_cullingEpoch.load(std::memory_order_acquire);
				const auto policyEpoch = g_policyEpoch.load(std::memory_order_acquire);
				const bool prepared = std::exchange(g_depthPrepared, false);
				const bool depthCurrent = IsDepthPreparationCurrent();
				const bool uploaded = a_shader && ReadCullerField<bool>(reinterpret_cast<const std::byte*>(a_shader), 0xC9);
				func(a_shader, a_param);
				if (prepared && uploaded && SubmissionPolicy::IsValid(batch) && ReadBatch(a_shader) == batch &&
					IsBalancedRecoveryActive(cullingEpoch, policyEpoch) &&
					g_producerPoseEpoch.load(std::memory_order_acquire) == cullingEpoch) {
					g_producerDepth = g_preparedDepth;
					if (g_producerDepth.reason == Reason::None && (!depthCurrent || !IsDepthPreparationCurrent()))
						g_producerDepth.reason = Reason::MissingDepth;
					g_handoff.Publish({ batch, 0, cullingEpoch, policyEpoch,
						globals::state ? globals::state->frameCount : 0, true, globals::state != nullptr });
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DepthCullingDownscale
		{
			static void thunk(RE::BSSceneGraph* a_scene)
			{
				const auto projection = std::exchange(g_pendingReadbackProjection, {});
				g_depthPrepared = false;
				g_handoff.Invalidate();
				func(a_scene);
				if (!g_cullingEnabled.load(std::memory_order_acquire) || g_mode.load(std::memory_order_acquire) != Mode::Balanced)
					return;
				CaptureDepthEvidence(g_preparedDepth);
				// Compare live frusta at readback on both sides; depth rendering uses temporary camera bounds.
				if (g_preparedDepth.reason == Reason::None) {
					if (SubmissionPolicy::Matches(projection, g_preparedDepth.camera,
							g_cullingEpoch.load(std::memory_order_acquire), g_policyEpoch.load(std::memory_order_acquire)))
						g_preparedDepth.readbackFrusta = projection.frusta;
					else
						g_preparedDepth.reason = Reason::InvalidView;
				}
				CaptureProducerPose();
				static REL::Relocation<const bool*> depthReady{ REL::Offset(0x1ED4180) };
				// Native submission ownership survives a missing pose; recovery handles invalid view evidence.
				g_depthPrepared = *depthReady;
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
		if (!IsCallInstruction(readbackCallsite) || !IsCallInstruction(renderCallsite) || !IsCallInstruction(downscaleCallsite)) {
			logger::error("VR: temporal depth-culling recovery not installed because a hook callsite was not recognized");
			return;
		}

		stl::write_thunk_call<DepthCullingReadback>(readbackCallsite);
		// Frame annotations may already wrap this call; preserve that current target in the thunk chain.
		stl::write_thunk_call<DepthCullingRender>(renderCallsite);
		stl::write_thunk_call<DepthCullingDownscale>(downscaleCallsite);
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
		const auto previous = g_mode.exchange(a_mode, std::memory_order_acq_rel);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_mode != Mode::Balanced)
			ClearLastRecoveryStatus();
#endif
		if (previous == Mode::Legacy || a_mode == Mode::Legacy) {
			// Legacy does not keep a producer pose warm. Require a new pose whenever
			// crossing that boundary so Balanced cannot consume an arbitrarily old one.
			g_cullingEpoch.fetch_add(1, std::memory_order_acq_rel);
			g_producerPoseEpoch.store(0, std::memory_order_release);
		}
		g_policyEpoch.fetch_add(1, std::memory_order_release);
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
		const auto mode = GetMode();
		const bool cullingEnabled = g_cullingEnabled.load(std::memory_order_acquire);
		const bool recoveryActive = cullingEnabled && mode == Mode::Balanced;
		return {
			.installed = g_installed.load(std::memory_order_acquire),
			.cullingEnabled = cullingEnabled,
			.telemetryEnabled = g_telemetryGate.IsEnabled(),
			.mode = mode,
			.envelopeMisses = g_envelopeMisses.load(std::memory_order_relaxed),
			.recoveryAttempts = g_recoveryAttempts.load(std::memory_order_relaxed),
			.objectsInspected = g_objectsInspected.load(std::memory_order_relaxed),
			.invalidTransforms = g_invalidTransforms.load(std::memory_order_relaxed),
			.invalidMotionEnvelopes = g_invalidMotionEnvelopes.load(std::memory_order_relaxed),
			.frustumTests = g_frustumTests.load(std::memory_order_relaxed),
			.totalEligible = g_totalEligible.load(std::memory_order_relaxed),
			.totalPromoted = g_totalPromoted.load(std::memory_order_relaxed),
			.motionExpansions = g_motionExpansions.load(std::memory_order_relaxed),
			.budgetSaturated = g_budgetSaturated.load(std::memory_order_relaxed),
			.eligibleUnpromoted = g_eligibleUnpromoted.load(std::memory_order_relaxed),
			.directEligible = g_directEligible.load(std::memory_order_relaxed),
			.expandedEligible = g_expandedEligible.load(std::memory_order_relaxed),
			.transitionPromoted = g_transitionPromoted.load(std::memory_order_relaxed),
			.lastProducerAge = g_lastProducerAge.load(std::memory_order_relaxed),
			.lastProducerAgeKnown = g_lastProducerAgeKnown.load(std::memory_order_relaxed),
			.lastSubmissionReason = g_lastSubmissionReason.load(std::memory_order_relaxed),
			.submissionReasons = [] {
				std::array<std::uint64_t, static_cast<std::size_t>(Reason::Count)> reasons{};
				for (std::size_t index = 0; index < reasons.size(); ++index)
					reasons[index] = g_submissionReasons[index].load(std::memory_order_relaxed);
				return reasons; }(),
			.totalDurationNanoseconds = g_totalDurationNanoseconds.load(std::memory_order_relaxed),
			.maximumDurationNanoseconds = g_maximumDurationNanoseconds.load(std::memory_order_relaxed),
			.durationHistogram = [&] {
				std::array<std::uint64_t, Status::DurationBinCount> bins{};
				for (std::size_t index = 0; index < bins.size(); ++index)
					bins[index] = g_durationHistogram[index].load(std::memory_order_relaxed);
				return bins; }(),
			.lastObjectCount = recoveryActive ? g_lastObjectCount.load(std::memory_order_relaxed) : 0,
			.lastEligibleCount = recoveryActive ? g_lastEligibleCount.load(std::memory_order_relaxed) : 0,
			.lastPromotedCount = recoveryActive ? g_lastPromotedCount.load(std::memory_order_relaxed) : 0,
		};
	}

	void SetTelemetryEnabled(bool a_enabled)
	{
		g_telemetryGate.SetEnabled(a_enabled);
	}

	bool TryResetStatus()
	{
		if (!g_telemetryGate.TryLockForReset())
			return false;
		g_envelopeMisses.store(0, std::memory_order_relaxed);
		g_recoveryAttempts.store(0, std::memory_order_relaxed);
		g_objectsInspected.store(0, std::memory_order_relaxed);
		g_invalidTransforms.store(0, std::memory_order_relaxed);
		g_invalidMotionEnvelopes.store(0, std::memory_order_relaxed);
		g_frustumTests.store(0, std::memory_order_relaxed);
		g_totalEligible.store(0, std::memory_order_relaxed);
		g_totalPromoted.store(0, std::memory_order_relaxed);
		g_motionExpansions.store(0, std::memory_order_relaxed);
		g_budgetSaturated.store(0, std::memory_order_relaxed);
		g_eligibleUnpromoted.store(0, std::memory_order_relaxed);
		g_directEligible.store(0, std::memory_order_relaxed);
		g_expandedEligible.store(0, std::memory_order_relaxed);
		g_transitionPromoted.store(0, std::memory_order_relaxed);
		g_lastProducerAge.store(0, std::memory_order_relaxed);
		g_lastProducerAgeKnown.store(false, std::memory_order_relaxed);
		g_lastSubmissionReason.store(Reason::MissingProducer, std::memory_order_relaxed);
		for (auto& reason : g_submissionReasons) reason.store(0, std::memory_order_relaxed);
		g_totalDurationNanoseconds.store(0, std::memory_order_relaxed);
		g_maximumDurationNanoseconds.store(0, std::memory_order_relaxed);
		for (auto& bin : g_durationHistogram)
			bin.store(0, std::memory_order_relaxed);
		ClearLastRecoveryStatus();
		g_telemetryGate.UnlockAfterReset();
		return true;
	}
#endif
}
