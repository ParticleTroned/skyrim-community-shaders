#pragma once

#include "VRDepthCullingTemporalPolicy.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace VRDepthCullingSubmissionPolicy
{
	/** Native depth remains standard Z; coverage belongs to the native producer. */
	enum class DepthConvention : std::uint8_t
	{
		Standard
	};
	enum class Reason : std::uint8_t
	{
		None,
		MissingProducer,
		InvalidBatch,
		BatchMismatch,
		EpochMismatch,
		MissingDepth,
		InvalidView,
		ProjectionChanged,
		LayoutChanged,
		SceneChanged,
		CameraCut,
		CompatibleResourceChange,
		Count
	};

	constexpr const char* ReasonName(Reason a_reason)
	{
		switch (a_reason) {
		case Reason::None:
			return "none";
		case Reason::MissingProducer:
			return "missing_or_skipped_producer";
		case Reason::InvalidBatch:
			return "invalid_batch";
		case Reason::BatchMismatch:
			return "submission_mismatch";
		case Reason::EpochMismatch:
			return "policy_epoch_mismatch";
		case Reason::MissingDepth:
			return "missing_or_invalid_depth";
		case Reason::InvalidView:
			return "invalid_view";
		case Reason::ProjectionChanged:
			return "projection_changed";
		case Reason::LayoutChanged:
			return "layout_changed";
		case Reason::SceneChanged:
			return "scene_changed";
		case Reason::CameraCut:
			return "camera_cut";
		case Reason::CompatibleResourceChange:
			return "compatible_resource_change";
		default:
			return "unknown";
		}
	}

	struct Batch
	{
		std::uintptr_t culler = 0, transforms = 0, results = 0;
		std::uint32_t count = 0, selector = 0;
		bool operator==(const Batch&) const = default;
	};

	constexpr bool IsValid(const Batch& a_batch)
	{
		return a_batch.culler && a_batch.transforms && a_batch.results &&
		       a_batch.count > 0 && a_batch.count <= VRDepthCullingTemporalPolicy::kMaximumObjects && a_batch.selector <= 1;
	}

	/** Registration storage remains immutable until the readback caller resets it. */
	struct Submission
	{
		Batch batch{};
		std::uint64_t sequence = 0, cullingEpoch = 0, policyEpoch = 0;
		std::uint32_t frame = 0;
		bool produced = false, frameKnown = false;
	};

	/** Render-thread owner of the registration lifetime; empty reads also retire history. */
	class Handoff
	{
	public:
		struct Consumed
		{
			Submission stamp;
			std::uint64_t sequence;
		};
		/** Discard skipped or replaced production without extending its lifetime. */
		void Invalidate() { stamp = {}; }
		/** Attach fresh producer evidence to the current native registration interval. */
		void Publish(Submission a_stamp)
		{
			a_stamp.sequence = sequence;
			stamp = a_stamp;
		}
		/** Retire the stamp before native readback, whether or not recovery runs. */
		[[nodiscard]] Consumed Consume()
		{
			return { std::exchange(stamp, {}), sequence++ };
		}

	private:
		Submission stamp{};
		std::uint64_t sequence = 1;
	};

	/** Sequence advances at every readback, including disabled/empty intervals. */
	constexpr Reason Match(const Submission& a_stamp, const Batch& a_batch, std::uint64_t a_sequence,
		std::uint64_t a_cullingEpoch, std::uint64_t a_policyEpoch)
	{
		if (!IsValid(a_batch))
			return Reason::InvalidBatch;
		if (!a_stamp.produced)
			return Reason::MissingProducer;
		if (a_stamp.sequence != a_sequence || a_stamp.batch != a_batch)
			return Reason::BatchMismatch;
		if (a_stamp.cullingEpoch != a_cullingEpoch || a_stamp.policyEpoch != a_policyEpoch || (a_policyEpoch & 1))
			return Reason::EpochMismatch;
		return Reason::None;
	}

	/** Valid discontinuities use the same ranked recovery budget as ordinary motion. */
	constexpr bool CanRecover(Reason a_reason)
	{
		return a_reason == Reason::None || a_reason == Reason::CompatibleResourceChange ||
		       a_reason == Reason::ProjectionChanged || a_reason == Reason::SceneChanged || a_reason == Reason::CameraCut;
	}

	/** Apply at most the shared budget to ranked candidates in this consumed registration. */
	inline std::uint32_t ApplyRecovery(Reason a_reason, const Handoff::Consumed& a_consumed, const Batch& a_batch,
		std::uint64_t a_cullingEpoch, std::uint64_t a_policyEpoch, std::span<std::uint32_t> a_results,
		const VRDepthCullingTemporalPolicy::CandidateSet<VRDepthCullingTemporalPolicy::kBalancedPromotionBudget>& a_candidates)
	{
		if (!CanRecover(a_reason) ||
			Match(a_consumed.stamp, a_batch, a_consumed.sequence, a_cullingEpoch, a_policyEpoch) != Reason::None ||
			a_results.size() != a_batch.count || reinterpret_cast<std::uintptr_t>(a_results.data()) != a_batch.results)
			return 0;
		for (std::size_t index = 0; index < a_candidates.Size(); ++index)
			if (a_candidates[index].index >= a_results.size())
				return 0;
		std::uint32_t promoted = 0;
		for (std::size_t index = 0; index < a_candidates.Size(); ++index) {
			auto& result = a_results[a_candidates[index].index];
			if (result == 0) {
				result = 1;
				++promoted;
			}
		}
		return promoted;
	}

	/** Modular age is diagnostic until runtime pause/transition timing is qualified. */
	constexpr std::uint32_t Age(std::uint32_t a_producer, std::uint32_t a_consumer)
	{
		return a_consumer - a_producer;
	}

	/** Native stereo rasterization permits fractional eye widths, including odd reduced targets. */
	inline bool IsSupportedRasterExtent(double a_width, double a_height)
	{
		return std::isfinite(a_width) && std::isfinite(a_height) && a_width >= 2 && a_height >= 1;
	}

	/** The source stores whole eyes; dynamic viewports scale those eyes without integer rounding. */
	inline bool IsSupportedStereoLayout(std::uint32_t a_sourceWidth, std::uint32_t a_sourceHeight,
		float a_activeWidth, float a_activeHeight)
	{
		return a_sourceWidth >= 2 && a_sourceWidth % 2 == 0 && a_sourceHeight > 0 &&
		       IsSupportedRasterExtent(a_activeWidth, a_activeHeight) &&
		       a_activeWidth <= a_sourceWidth && a_activeHeight <= a_sourceHeight;
	}

	/** A camera can retain its pose across unrelated cells or worldspaces. */
	struct Scene
	{
		std::uintptr_t interiorCell = 0, worldSpace = 0;
		bool operator==(const Scene&) const = default;
	};

	constexpr bool IsValid(const Scene& a_scene)
	{
		return (a_scene.interiorCell != 0) != (a_scene.worldSpace != 0);
	}

	struct Eye
	{
		std::array<float, 16> view{}, projection{}, viewProjection{};
		std::array<float, 3> position{};
		std::array<float, 4> viewport{};
		std::array<float, 2> depthRange{};
	};

	template <std::size_t Size>
	bool AllFinite(const std::array<float, Size>& a_values)
	{
		for (float value : a_values)
			if (!std::isfinite(value))
				return false;
		return true;
	}

	inline bool IsValid(const Eye& a_eye)
	{
		return AllFinite(a_eye.view) && AllFinite(a_eye.projection) && AllFinite(a_eye.viewProjection) && AllFinite(a_eye.position) &&
		       a_eye.viewport == std::array<float, 4>{ 0, 1, 1, 0 } &&
		       a_eye.depthRange == std::array<float, 2>{ 0, 1 } &&
		       a_eye.projection[0] != 0 && a_eye.projection[5] != 0;
	}

	/** Readback-phase camera frusta detect changes before the view cache refreshes. */
	struct Frustum
	{
		std::array<float, 6> planes{};
		bool orthographic = false;
	};

	inline bool IsValid(const Frustum& a_frustum)
	{
		const auto& planes = a_frustum.planes;
		return AllFinite(planes) && planes[0] < planes[1] && planes[3] < planes[2] &&
		       planes[4] >= 0 && (a_frustum.orthographic || planes[4] > 0) && planes[4] < planes[5];
	}

	/** Native depth rendering temporarily overwrites the live left-eye frustum. */
	struct ReadbackProjection
	{
		std::array<Frustum, 2> frusta{};
		std::uintptr_t camera = 0;
		std::uint64_t cullingEpoch = 0, policyEpoch = 0;
	};

	/** Bind the next producer to the preceding readback's normal camera phase. */
	inline bool Matches(const ReadbackProjection& a_projection, std::uintptr_t a_camera,
		std::uint64_t a_cullingEpoch, std::uint64_t a_policyEpoch)
	{
		return a_camera && a_projection.camera == a_camera &&
		       a_projection.cullingEpoch == a_cullingEpoch && a_projection.policyEpoch == a_policyEpoch &&
		       (a_policyEpoch & 1u) == 0 && IsValid(a_projection.frusta[0]) && IsValid(a_projection.frusta[1]);
	}

	template <std::size_t Size>
	bool SameProjectionValues(const std::array<float, Size>& a_previous, const std::array<float, Size>& a_current)
	{
		for (std::size_t index = 0; index < Size; ++index) {
			if (!std::isfinite(a_previous[index]) || !std::isfinite(a_current[index]) ||
				std::abs(a_previous[index] - a_current[index]) > 1e-5f)
				return false;
		}
		return true;
	}

	inline bool SameProjection(const Eye& a_previous, const Eye& a_current)
	{
		return SameProjectionValues(a_previous.projection, a_current.projection);
	}

	inline bool SameProjection(const Frustum& a_previous, const Frustum& a_current)
	{
		return IsValid(a_previous) && IsValid(a_current) &&
		       a_previous.orthographic == a_current.orthographic && SameProjectionValues(a_previous.planes, a_current.planes);
	}

	/** Large cuts are separate from normal headset motion and bounded recovery. */
	inline bool IsCameraCut(double a_rotationCosine, float a_translationSquared)
	{
		constexpr double minimumRotationCosine = 0.7071067811865476;  // 45 degrees
		constexpr float maximumTranslationSquared = 1024.0f * 1024.0f;
		return std::isfinite(a_rotationCosine) && std::isfinite(a_translationSquared) &&
		       (a_rotationCosine < minimumRotationCosine || a_translationSquared > maximumTranslationSquared);
	}
}
