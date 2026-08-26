#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace VRDepthCullingTemporalPolicy
{
	enum class Mode
	{
		Balanced,
		Performance
	};

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

	constexpr bool IsViewCoherent(
		bool a_poseValid,
		double a_rotationCosine,
		float a_translationSquared)
	{
		return a_poseValid &&
		       a_rotationCosine >= kMinimumCoherentRotationCosine &&
		       a_translationSquared <= kMaximumCoherentTranslationSquared;
	}

	constexpr bool ShouldEvaluateEnvelope(Mode a_mode, bool a_viewCoherent)
	{
		return a_mode == Mode::Balanced && !a_viewCoherent;
	}

	inline bool TryBuildBoundingSphere(const OBBTransform& a_transform, BoundingSphere& a_sphere)
	{
		float radiusSquared = 0.0f;
		for (std::size_t column = 0; column < 3; ++column) {
			float axisLengthSquared = 0.0f;
			for (std::size_t row = 0; row < 3; ++row) {
				const float component = a_transform.entry[row][column];
				if (!std::isfinite(component))
					return false;
				axisLengthSquared += component * component;
			}
			radiusSquared += axisLengthSquared;
		}

		for (std::size_t row = 0; row < 3; ++row) {
			a_sphere.center[row] = a_transform.entry[row][3];
			if (!std::isfinite(a_sphere.center[row]))
				return false;
		}

		if (!std::isfinite(radiusSquared) || radiusSquared < 0.0f)
			return false;
		a_sphere.radius = std::sqrt(radiusSquared);
		return std::isfinite(a_sphere.radius);
	}

	inline float CalculateMotionExpansion(
		float a_distanceSquared,
		double a_rotationCosine,
		float a_translationSquared)
	{
		if (!std::isfinite(a_distanceSquared) || a_distanceSquared < 0.0f ||
			!std::isfinite(a_rotationCosine) || !std::isfinite(a_translationSquared) ||
			a_translationSquared < 0.0f) {
			return 0.0f;
		}

		const float distance = std::sqrt(a_distanceSquared);
		const float translation = std::sqrt(a_translationSquared);
		const double clampedCosine = std::clamp(a_rotationCosine, -1.0, 1.0);
		const double halfChord = std::sqrt(std::max(0.0, (1.0 - clampedCosine) * 0.5));
		const float expansion = translation + static_cast<float>(2.0 * distance * halfChord);
		return std::isfinite(expansion) ? expansion : 0.0f;
	}

	inline float CalculateRiskScore(float a_expandedRadius, float a_distanceSquared)
	{
		if (!std::isfinite(a_expandedRadius) || a_expandedRadius < 0.0f ||
			!std::isfinite(a_distanceSquared) || a_distanceSquared < 0.0f) {
			return 0.0f;
		}
		return a_expandedRadius * a_expandedRadius / std::max(a_distanceSquared, 1.0f);
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
				return;
			}

			std::size_t lowestPriority = 0;
			for (std::size_t index = 1; index < size_; ++index) {
				if (IsHigherPriority(items_[lowestPriority], items_[index]))
					lowestPriority = index;
			}
			if (IsHigherPriority(a_candidate, items_[lowestPriority]))
				items_[lowestPriority] = a_candidate;
		}

		[[nodiscard]] std::size_t Size() const { return size_; }
		[[nodiscard]] const Candidate& operator[](std::size_t a_index) const { return items_[a_index]; }

	private:
		std::array<Candidate, Capacity> items_{};
		std::size_t size_ = 0;
	};
}
