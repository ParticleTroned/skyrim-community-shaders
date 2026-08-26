#include "VRDepthCullingTemporal.h"

#include "VRDepthCullingTemporalPolicy.h"

#include "RE/N/NiCamera.h"
#include "RE/N/NiPoint3.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

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
		std::atomic_bool g_cullingEnabled{ false };
		std::atomic_uint64_t g_cullingEpoch{ 1 };
		std::atomic_uint64_t g_producerPoseEpoch{ 0 };
		std::atomic<Mode> g_mode{ Mode::Balanced };
		std::atomic_uint64_t g_policyEpoch{ 2 };
		std::atomic_uint64_t g_envelopeMisses{ 0 };
		std::atomic_uint64_t g_totalPromoted{ 0 };
		std::atomic_uint32_t g_lastObjectCount{ 0 };
		std::atomic_uint32_t g_lastEligibleCount{ 0 };
		std::atomic_uint32_t g_lastPromotedCount{ 0 };

		void ClearLastRecoveryStatus()
		{
			g_lastObjectCount.store(0, std::memory_order_relaxed);
			g_lastEligibleCount.store(0, std::memory_order_relaxed);
			g_lastPromotedCount.store(0, std::memory_order_relaxed);
		}

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

			// Keep the pose warm so switching from Performance to Balanced is valid immediately.
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
				g_mode.load(std::memory_order_acquire) != Mode::Balanced) {
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
			MotionEnvelope envelope{};
			if (!TryBuildMotionEnvelope(motion.rotationCosine, motion.translationSquared, envelope))
				return;

			g_envelopeMisses.fetch_add(1, std::memory_order_relaxed);
			ClearLastRecoveryStatus();

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
			std::uint32_t eligibleCount = 0;
			for (std::uint32_t index = 0; index < objectCount; ++index) {
				if (results[index] != 0)
					continue;

				BoundingSphere sphere{};
				if (!TryBuildBoundingSphere(transforms[index], sphere))
					continue;
				const RE::NiPoint3 center{ sphere.center[0], sphere.center[1], sphere.center[2] };
				const auto positionDelta = center - camera->world.translate;
				const float distanceSquared = positionDelta.SqrLength();
				float motionExpansion = 0.0f;
				if (!TryCalculateMotionExpansion(
						distanceSquared,
						envelope,
						motionExpansion)) {
					continue;
				}
				const float expandedRadius = sphere.radius + motionExpansion;
				if (!std::isfinite(expandedRadius) || expandedRadius < sphere.radius)
					continue;

				const bool directlyVisible = camera->PointInFrustum(center, sphere.radius);
				if (!directlyVisible && !camera->PointInFrustum(center, expandedRadius))
					continue;

				++eligibleCount;
				candidates.Add({ index, CalculateRiskScore(sphere.radius, distanceSquared), directlyVisible });
			}

			if (!IsBalancedRecoveryActive(cullingEpoch, policyEpoch)) {
				return;
			}

			for (std::size_t index = 0; index < candidates.Size(); ++index)
				results[candidates[index].index] = 1;

			const auto promotedCount = static_cast<std::uint32_t>(candidates.Size());
			g_lastObjectCount.store(objectCount, std::memory_order_relaxed);
			g_lastEligibleCount.store(eligibleCount, std::memory_order_relaxed);
			g_lastPromotedCount.store(promotedCount, std::memory_order_relaxed);
			g_totalPromoted.fetch_add(promotedCount, std::memory_order_relaxed);
			if (!IsBalancedRecoveryActive(cullingEpoch, policyEpoch))
				ClearLastRecoveryStatus();
		}

		struct DepthCullingReadback
		{
			static void thunk(void* a_culler)
			{
				func(a_culler);
				RecoverHighRiskObjects(a_culler);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DepthCullingRender
		{
			static void thunk(RE::BSImagespaceShader* a_shader, std::uint32_t a_param)
			{
				func(a_shader, a_param);
				CaptureProducerPose();
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
		if (!IsCallInstruction(readbackCallsite) || !IsCallInstruction(renderCallsite)) {
			logger::error("VR: temporal depth-culling recovery not installed because a hook callsite was not recognized");
			return;
		}

		stl::write_thunk_call<DepthCullingReadback>(readbackCallsite);
		// Frame annotations may already wrap this call; preserve that current target in the thunk chain.
		stl::write_thunk_call<DepthCullingRender>(renderCallsite);
		g_installed.store(true, std::memory_order_release);
		logger::info(
			"VR: installed temporal depth-culling recovery (default balanced budget {})",
			kBalancedPromotionBudget);
	}

	void SetMode(Mode a_mode)
	{
		a_mode = SelectMode(
			a_mode == Mode::Performance,
			a_mode == Mode::Legacy);
		const auto current = g_mode.load(std::memory_order_acquire);
		if (current == a_mode)
			return;
		// The main-thread writer leaves an odd epoch while publishing a new policy.
		g_policyEpoch.fetch_add(1, std::memory_order_acq_rel);
		const auto previous = g_mode.exchange(a_mode, std::memory_order_acq_rel);
		if (a_mode != Mode::Balanced)
			ClearLastRecoveryStatus();
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
		if (!a_enabled)
			ClearLastRecoveryStatus();
	}

	Mode GetMode()
	{
		return g_mode.load(std::memory_order_acquire);
	}

	Status GetStatus()
	{
		const auto mode = GetMode();
		const bool cullingEnabled = g_cullingEnabled.load(std::memory_order_acquire);
		const bool recoveryActive = cullingEnabled && mode == Mode::Balanced;
		return {
			.installed = g_installed.load(std::memory_order_acquire),
			.cullingEnabled = cullingEnabled,
			.mode = mode,
			.envelopeMisses = g_envelopeMisses.load(std::memory_order_relaxed),
			.totalPromoted = g_totalPromoted.load(std::memory_order_relaxed),
			.lastObjectCount = recoveryActive ? g_lastObjectCount.load(std::memory_order_relaxed) : 0,
			.lastEligibleCount = recoveryActive ? g_lastEligibleCount.load(std::memory_order_relaxed) : 0,
			.lastPromotedCount = recoveryActive ? g_lastPromotedCount.load(std::memory_order_relaxed) : 0,
		};
	}
}
