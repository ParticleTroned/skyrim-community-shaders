#pragma once

#include "VRDepthCullingTemporalPolicy.h"

namespace VRHybridCullingHistory
{
	struct Batch
	{
		std::uintptr_t culler = 0;
		std::uintptr_t transforms = 0;
		std::uintptr_t results = 0;
		std::uint64_t epoch = 0;
		std::uint32_t frame = 0;
		std::uint32_t count = 0;
		std::uint32_t selector = 0;
	};

	/** Native indices identify only the submission retained until the next readback. */
	constexpr bool Matches(const Batch& a_producer, const Batch& a_consumer)
	{
		return a_producer.culler != 0 && a_producer.transforms != 0 && a_producer.results != 0 &&
		       a_producer.count > 0 && a_producer.count <= VRDepthCullingTemporalPolicy::kMaximumObjects &&
		       a_producer.selector <= 1 && a_producer.epoch != 0 &&
		       a_producer.culler == a_consumer.culler && a_producer.transforms == a_consumer.transforms &&
		       a_producer.results == a_consumer.results && a_producer.count == a_consumer.count &&
		       a_producer.selector == a_consumer.selector && a_producer.epoch == a_consumer.epoch &&
		       a_consumer.frame - a_producer.frame == 1;
	}

	struct EyePose
	{
		float rotation[3][3]{};
		float position[3]{};
		float projection[4][4]{};
	};

	/** Native view bases may change handedness, but must remain orthonormal. */
	inline bool IsRotation(const float (&a_matrix)[3][3])
	{
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t other = row; other < 3; ++other) {
				double dot = 0.0;
				for (std::size_t column = 0; column < 3; ++column)
					dot += static_cast<double>(a_matrix[row][column]) * a_matrix[other][column];
				if (!std::isfinite(dot) || std::abs(dot - (row == other ? 1.0 : 0.0)) > 0.001)
					return false;
			}
		}
		const double determinant =
			a_matrix[0][0] * (a_matrix[1][1] * a_matrix[2][2] - a_matrix[1][2] * a_matrix[2][1]) -
			a_matrix[0][1] * (a_matrix[1][0] * a_matrix[2][2] - a_matrix[1][2] * a_matrix[2][0]) +
			a_matrix[0][2] * (a_matrix[1][0] * a_matrix[2][1] - a_matrix[1][1] * a_matrix[2][0]);
		return std::isfinite(determinant) && std::abs(std::abs(determinant) - 1.0) <= 0.001;
	}

	/** Bound reuse to the existing small-motion window in each eye, without a promotion quota. */
	inline bool IsCoherent(const EyePose& a_producer, const EyePose& a_consumer)
	{
		if (!IsRotation(a_producer.rotation) || !IsRotation(a_consumer.rotation))
			return false;
		double basisDifferenceSquared = 0.0;
		float translationSquared = 0.0f;
		for (std::size_t row = 0; row < 3; ++row) {
			double producerNorm = 0.0;
			double consumerNorm = 0.0;
			for (std::size_t column = 0; column < 3; ++column) {
				producerNorm += static_cast<double>(a_producer.rotation[row][column]) * a_producer.rotation[row][column];
				consumerNorm += static_cast<double>(a_consumer.rotation[row][column]) * a_consumer.rotation[row][column];
			}
			producerNorm = std::sqrt(producerNorm);
			consumerNorm = std::sqrt(consumerNorm);
			for (std::size_t column = 0; column < 3; ++column) {
				const double delta = a_producer.rotation[row][column] / producerNorm - a_consumer.rotation[row][column] / consumerNorm;
				basisDifferenceSquared += delta * delta;
			}
			const float delta = a_producer.position[row] - a_consumer.position[row];
			translationSquared += delta * delta;
		}
		for (std::size_t row = 0; row < 4; ++row)
			for (std::size_t column = 0; column < 4; ++column)
				if (!std::isfinite(a_producer.projection[row][column]) ||
					a_producer.projection[row][column] != a_consumer.projection[row][column])
					return false;
		return VRDepthCullingTemporalPolicy::IsViewCoherent(true, 1.0 - basisDifferenceSquared * 0.25, translationSquared);
	}

	/** Either eye can invalidate a stereo occlusion batch. */
	inline bool IsStereoCoherent(const std::array<EyePose, 2>& a_producer, const std::array<EyePose, 2>& a_consumer)
	{
		return IsCoherent(a_producer[0], a_consumer[0]) && IsCoherent(a_producer[1], a_consumer[1]);
	}
}
