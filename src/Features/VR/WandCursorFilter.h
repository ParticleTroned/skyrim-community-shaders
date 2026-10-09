#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace WandCursorFilter
{
	struct Position
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	/** @brief Smooth menu coordinates in display-height units, independently of frame rate. */
	class Filter
	{
	public:
		void Reset() { *this = {}; }

		/** @brief Preserve the displayed target on a press; reacquisition starts at the new sample. */
		Position Update(Position a_input, double a_sampleSeconds, bool a_holdPosition = false)
		{
			if (!std::isfinite(a_input.x) || !std::isfinite(a_input.y) || !std::isfinite(a_sampleSeconds)) {
				Reset();
				return a_input;
			}
			const double elapsed = a_sampleSeconds - sampleSeconds;
			if (!valid || elapsed < 0.0 || elapsed > kMaxSampleGapSeconds) {
				output = previousInput = a_input;
				velocity = {};
				sampleSeconds = a_sampleSeconds;
				valid = true;
				return output;
			}
			if (elapsed <= 0.0)
				return output;
			sampleSeconds = a_sampleSeconds;
			const float dt = static_cast<float>(elapsed);
			const Position rawVelocity{ (a_input.x - previousInput.x) / dt, (a_input.y - previousInput.y) / dt };
			previousInput = a_input;
			const float velocityAlpha = Alpha(kVelocityCutoffHz, dt);
			velocity.x += velocityAlpha * (rawVelocity.x - velocity.x);
			velocity.y += velocityAlpha * (rawVelocity.y - velocity.y);
			if (a_holdPosition)
				return output;

			const float speed = std::hypot(velocity.x, velocity.y);
			const float cutoff = std::min(kMaxCutoffHz, kRestCutoffHz + kSpeedCoefficient * speed);
			const float alpha = Alpha(cutoff, dt);
			output.x += alpha * (a_input.x - output.x);
			output.y += alpha * (a_input.y - output.y);
			return output;
		}

	private:
		static float Alpha(float a_cutoffHz, float a_dt)
		{
			return -std::expm1(-2.0f * std::numbers::pi_v<float> * a_cutoffHz * a_dt);
		}

		static constexpr float kRestCutoffHz = 4.0f;
		static constexpr float kMaxCutoffHz = 30.0f;
		static constexpr float kVelocityCutoffHz = 5.0f;
		static constexpr float kSpeedCoefficient = 10.0f;
		static constexpr double kMaxSampleGapSeconds = 0.25;
		Position output{};
		Position previousInput{};
		Position velocity{};
		double sampleSeconds = 0.0;
		bool valid = false;
	};
}
