#pragma once

#include <SimpleMath.h>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace CSX::ViewRefresh
{
	using DirectX::SimpleMath::Matrix;
	using DirectX::SimpleMath::Vector3;

	inline constexpr double MaximumSceneAgeSeconds = 0.035;
	inline constexpr float MaximumHeadMovementMeters = 0.05f;
	inline constexpr float MinimumRotationCosine = 0.99026807f;  // Eight degrees.

	/** A reused image never becomes the source of another image. */
	struct SceneIdentity
	{
		uint32_t frame = 0;
		uint64_t cycle = 0;
		uint64_t generation = 0;
		uint64_t settingsRevision = 0;
		uint32_t thread = 0;
		bool complete = false;
		uintptr_t device = 0;
	};

	/** Settings may change after preparation; frame, resource and thread ownership may not. */
	inline bool SameFrame(const SceneIdentity& source, const SceneIdentity& target)
	{
		return source.cycle != 0 && source.generation != 0 && source.thread != 0 && source.device != 0 &&
		       source.frame == target.frame && source.cycle == target.cycle &&
		       source.generation == target.generation && source.thread == target.thread && source.device == target.device;
	}

	/** A source is reusable only after one successful submission of each eye. */
	struct StereoReceipt
	{
		uint64_t cycle = 0;
		uint8_t observed = 0;
		uint8_t accepted = 0;
		bool malformed = false;

		void Record(uint32_t eye, uint64_t submittedCycle, bool success)
		{
			if (eye > 1 || submittedCycle != cycle || cycle == 0) {
				malformed = true;
				return;
			}
			const auto bit = static_cast<uint8_t>(1u << eye);
			malformed |= (observed & bit) != 0;
			observed |= bit;
			if (success)
				accepted |= bit;
		}

		bool Complete(uint64_t currentCycle) const
		{
			return !malformed && cycle != 0 && cycle == currentCycle && observed == 3 && accepted == 3;
		}
	};

	/** Admit exactly the next display cycle, after both original eyes succeeded. */
	inline bool CanReuse(const SceneIdentity& source, const SceneIdentity& target, double age)
	{
		return source.complete && source.cycle != 0 && source.generation != 0 &&
		       source.frame != UINT32_MAX && target.frame == source.frame + 1 &&
		       source.cycle != UINT64_MAX && target.cycle == source.cycle + 1 &&
		       source.generation == target.generation &&
		       source.device != 0 && source.device == target.device && source.thread != 0 &&
		       source.settingsRevision == target.settingsRevision && source.thread == target.thread &&
		       std::isfinite(age) && age >= 0 && age <= MaximumSceneAgeSeconds;
	}

	/** Invalid player transforms must not pass a stationary-player comparison. */
	inline bool StationaryPlayer(const Vector3& sourcePosition, const Vector3& sourceAngles,
		const Vector3& position, const Vector3& angles)
	{
		for (const auto& value : { sourcePosition, sourceAngles, position, angles })
			if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
				return false;
		constexpr float maximumPositionDeltaSquared = 0.01f;
		constexpr float maximumAngleDeltaSquared = 0.000001f;
		return (position - sourcePosition).LengthSquared() <= maximumPositionDeltaSquared &&
		       (angles - sourceAngles).LengthSquared() <= maximumAngleDeltaSquared;
	}

	inline bool Finite(const Matrix& value)
	{
		for (const auto& row : value.m)
			for (float element : row)
				if (!std::isfinite(element))
					return false;
		return true;
	}

	/** Reject tracking loss, recentering and nonrigid transforms before inversion. */
	inline bool RigidPose(const Matrix& pose)
	{
		if (!Finite(pose) || std::abs(pose._14) > 0.0001f || std::abs(pose._24) > 0.0001f ||
			std::abs(pose._34) > 0.0001f || std::abs(pose._44 - 1.0f) > 0.0001f)
			return false;
		const Vector3 axes[] = { { pose._11, pose._12, pose._13 }, { pose._21, pose._22, pose._23 }, { pose._31, pose._32, pose._33 } };
		for (const auto& axis : axes)
			if (std::abs(axis.LengthSquared() - 1.0f) > 0.002f)
				return false;
		return std::abs(axes[0].Dot(axes[1])) < 0.002f && std::abs(axes[0].Dot(axes[2])) < 0.002f &&
		       std::abs(axes[1].Dot(axes[2])) < 0.002f && std::abs(pose.Determinant() - 1.0f) < 0.003f;
	}

	inline bool BoundedMotion(const Matrix& source, const Matrix& target)
	{
		if (!RigidPose(source) || !RigidPose(target))
			return false;
		const auto delta = source * target.Invert();
		const float cosine = (delta._11 + delta._22 + delta._33 - 1.0f) * 0.5f;
		return cosine >= MinimumRotationCosine &&
		       Vector3(delta._41, delta._42, delta._43).LengthSquared() <=
		           MaximumHeadMovementMeters * MaximumHeadMovementMeters;
	}

	struct EyeTransform
	{
		Matrix sourceClipToView;
		Matrix sourceViewToTargetView;
		Matrix targetViewToClip;
		Matrix targetClipToSourceSkyClip;
	};

	/** Convert OpenVR meters and handedness locally, retaining each eye's pivot. */
	inline bool BuildEyeTransform(const Matrix& projection, const Matrix& eyeToHead,
		const Matrix& renderedHead, const Matrix& displayedHead, float unitsPerMeter, EyeTransform& output)
	{
		if (!BoundedMotion(renderedHead, displayedHead) || !RigidPose(eyeToHead) ||
			!Finite(projection) || !std::isfinite(projection.Determinant()) || std::abs(projection.Determinant()) < 1e-8f ||
			!std::isfinite(unitsPerMeter) || unitsPerMeter < 1 || unitsPerMeter > 1000 ||
			std::abs(std::abs(projection._34) - 1.0f) > 0.001f || std::abs(projection._44) > 0.001f)
			return false;
		auto eyeDelta = eyeToHead * renderedHead * (eyeToHead * displayedHead).Invert();
		eyeDelta._41 *= unitsPerMeter;
		eyeDelta._42 *= unitsPerMeter;
		eyeDelta._43 *= unitsPerMeter;
		const auto handedness = Matrix::CreateScale(1, 1, projection._34 > 0 ? -1.0f : 1.0f);
		eyeDelta = handedness * eyeDelta * handedness;
		auto inverseRotation = eyeDelta.Invert();
		inverseRotation._41 = inverseRotation._42 = inverseRotation._43 = 0;
		output = { projection.Invert(), eyeDelta, projection, projection.Invert() * inverseRotation * projection };
		return Finite(output.sourceClipToView) && Finite(output.targetClipToSourceSkyClip);
	}
}
