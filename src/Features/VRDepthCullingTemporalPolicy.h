#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace VRDepthCullingTemporalPolicy
{
	inline constexpr double kMinimumCoherentRotationCosine = 0.9999996192282494;  // cos(0.05 degrees)
	inline constexpr float kMaximumCoherentTranslationSquared = 0.01f;            // 0.1 world units squared
	inline constexpr std::uint32_t kMaximumObjects = 0x1000u;
	inline constexpr std::size_t kBalancedPromotionBudget = 64;

	struct OBBTransform
	{
		float entry[4][4]{};
	};

	static_assert(sizeof(OBBTransform) == 64);

	struct BoundingSphere
	{
		std::array<float, 3> center{};
		float radius = 0.0f;
	};

	struct Candidate
	{
		std::uint32_t index = 0;
		float score = 0.0f;
		bool directlyVisible = false;
	};

	struct MotionEnvelope
	{
		double translation = 0.0;
		double rotationChord = 0.0;
	};

	constexpr bool IsViewCoherent(
		bool a_poseValid,
		double a_rotationCosine,
		float a_translationSquared)
	{
		return a_poseValid &&
		       std::isfinite(a_rotationCosine) &&
		       std::isfinite(a_translationSquared) &&
		       a_rotationCosine >= kMinimumCoherentRotationCosine &&
		       a_translationSquared <= kMaximumCoherentTranslationSquared;
	}

	inline bool TryBuildBoundingSphere(const OBBTransform& a_transform, BoundingSphere& a_sphere)
	{
		std::array<std::array<double, 3>, 3> axes{};
		for (std::size_t column = 0; column < 3; ++column) {
			for (std::size_t row = 0; row < 3; ++row) {
				const float component = a_transform.entry[row][column];
				if (!std::isfinite(component))
					return false;
				axes[column][row] = component;
			}
		}

		for (std::size_t row = 0; row < 3; ++row) {
			a_sphere.center[row] = a_transform.entry[row][3];
			if (!std::isfinite(a_sphere.center[row]))
				return false;
		}

		double radiusSquared = 0.0;
		for (std::uint32_t vertexPair = 0; vertexPair < 4; ++vertexPair) {
			std::array<double, 3> offset = axes[0];
			for (std::size_t axis = 1; axis < 3; ++axis) {
				const double sign = (vertexPair & (1u << (axis - 1))) != 0 ? 1.0 : -1.0;
				for (std::size_t component = 0; component < 3; ++component)
					offset[component] += axes[axis][component] * sign;
			}
			const double vertexRadiusSquared =
				offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
			if (!std::isfinite(vertexRadiusSquared))
				return false;
			radiusSquared = std::max(radiusSquared, vertexRadiusSquared);
		}

		const double radius = std::sqrt(radiusSquared);
		if (!std::isfinite(radius) || radius > std::numeric_limits<float>::max())
			return false;
		a_sphere.radius = static_cast<float>(radius);
		return true;
	}

	inline bool TryBuildMotionEnvelope(
		double a_rotationCosine,
		float a_translationSquared,
		MotionEnvelope& a_envelope)
	{
		a_envelope = {};
		if (!std::isfinite(a_rotationCosine) || !std::isfinite(a_translationSquared) ||
			a_translationSquared < 0.0f) {
			return false;
		}

		const double clampedCosine = std::clamp(a_rotationCosine, -1.0, 1.0);
		const double halfChord = std::sqrt(std::max(0.0, (1.0 - clampedCosine) * 0.5));
		a_envelope.translation = std::sqrt(static_cast<double>(a_translationSquared));
		a_envelope.rotationChord = 2.0 * halfChord;
		return std::isfinite(a_envelope.translation) && std::isfinite(a_envelope.rotationChord);
	}

	inline bool TryCalculateMotionExpansion(
		float a_distanceSquared,
		const MotionEnvelope& a_envelope,
		float& a_expansion)
	{
		a_expansion = 0.0f;
		if (!std::isfinite(a_distanceSquared) || a_distanceSquared < 0.0f ||
			!std::isfinite(a_envelope.translation) || a_envelope.translation < 0.0 ||
			!std::isfinite(a_envelope.rotationChord) || a_envelope.rotationChord < 0.0) {
			return false;
		}

		const double distance = std::sqrt(static_cast<double>(a_distanceSquared));
		const double expansion = a_envelope.translation +
		                         (distance + a_envelope.translation) * a_envelope.rotationChord;
		if (!std::isfinite(expansion) || expansion > std::numeric_limits<float>::max())
			return false;
		a_expansion = static_cast<float>(expansion);
		return true;
	}

	inline float CalculateRiskScore(float a_radius, float a_distanceSquared)
	{
		if (!std::isfinite(a_radius) || a_radius < 0.0f ||
			!std::isfinite(a_distanceSquared) || a_distanceSquared < 0.0f) {
			return 0.0f;
		}
		const double score = static_cast<double>(a_radius) * a_radius /
		                     std::max(static_cast<double>(a_distanceSquared), 1.0);
		return static_cast<float>(std::min(score, static_cast<double>(std::numeric_limits<float>::max())));
	}

	constexpr bool IsHigherPriority(const Candidate& a_left, const Candidate& a_right)
	{
		if (a_left.directlyVisible != a_right.directlyVisible)
			return a_left.directlyVisible;
		if (a_left.score != a_right.score)
			return a_left.score > a_right.score;
		return a_left.index < a_right.index;
	}

	template <std::size_t Capacity>
	class CandidateSet
	{
	public:
		static_assert(Capacity > 0);

		void Add(const Candidate& a_candidate)
		{
			if (!std::isfinite(a_candidate.score))
				return;
			if (size_ < Capacity) {
				items_[size_++] = a_candidate;
				std::push_heap(items_.begin(), items_.begin() + size_, IsHigherPriority);
				return;
			}

			if (!IsHigherPriority(a_candidate, items_.front()))
				return;
			std::pop_heap(items_.begin(), items_.begin() + size_, IsHigherPriority);
			items_[size_ - 1] = a_candidate;
			std::push_heap(items_.begin(), items_.begin() + size_, IsHigherPriority);
		}

		[[nodiscard]] std::size_t Size() const { return size_; }
		[[nodiscard]] const Candidate& operator[](std::size_t a_index) const { return items_[a_index]; }

	private:
		std::array<Candidate, Capacity> items_{};
		std::size_t size_ = 0;
	};
}
