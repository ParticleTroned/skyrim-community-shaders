#include "Features/Upscaling/NeuralRendering/CharacterActorPolicy.h"
#include "Features/Upscaling/NeuralRendering/FaceDetailPolicy.h"

#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace RE
{
	struct NiPoint3
	{
		float x = 0, y = 0, z = 0;
	};
}
namespace Util::Units
{
#include "face_detail_units_constants_under_test.h"
#include "face_detail_units_under_test.h"
}
namespace globals::game
{
	bool isVR = false;
	struct Matrix
	{
		Matrix Transpose() const { return {}; }
	};
	struct FrameBuffer
	{
		std::array<RE::NiPoint3, 2> positions{};
		const RE::NiPoint3& GetCameraPosAdjust(unsigned eye) const { return positions[eye]; }
		Matrix GetCameraViewProjUnjittered(unsigned) const { return {}; }
		Matrix GetCameraViewProj(unsigned) const { return {}; }
	} frameBufferCached;
}
namespace NeuralRendering
{
#include "face_detail_bound_under_test.h"
#include "face_detail_camera_under_test.h"
	struct CharacterRendering
	{
		struct State
		{
			struct Observation
			{
				RE::NiPoint3 center;
				float radius;
			};
			mutable std::mutex mutex_;
#include "face_detail_state_under_test.h"
			unsigned projections = 0;
			std::array<CharacterProjectionResult, 2> results{ CharacterProjectionResult::Visible, CharacterProjectionResult::Visible };
			CharacterProjectionResult ProjectGeometry(const Observation&, const RE::NiPoint3&,
				const globals::game::Matrix&, const globals::game::Matrix&, unsigned, unsigned, CharacterRect& rect)
			{
				const auto result = results[projections++ % (globals::game::isVR ? 2u : 1u)];
				rect = { 4096, 4096, 8192, 8192 };
				return result;
			}
		};
		std::unique_ptr<State> state_ = std::make_unique<State>();
		void ObserveFaceDetail(std::uint32_t frame, std::uint32_t actor, const CharacterActorBound& bound);
		FaceDetailRegions GetFaceDetailRegions(std::uint32_t frame, std::uint32_t eye, const UpscalingDLSS::ViewportCrop& crop) const;
	};
#include "face_detail_observation_under_test.h"
}

namespace
{
	using namespace NeuralRendering;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	CharacterActorBound Head(float distanceMeters)
	{
		const auto eye = GetProjectionAverageEyePosition();
		return { eye.x, eye.y, eye.z + distanceMeters / Util::Units::GAME_UNIT_TO_M, 10.0f };
	}
}

int main()
{
	try {
		const auto crop = UpscalingDLSS::ViewportCrop::Identity(1920, 1080, 1920, 1080);
		Require(kFaceDetailMaximumDistanceMeters == 1.0f && kFaceDetailDistanceFadeMeters == 0.25f,
			"Face protection must use the documented one-metre cutoff and quarter-metre fade");
		for (bool vr : { false, true }) {
			globals::game::isVR = vr;
			globals::game::frameBufferCached.positions = { RE::NiPoint3{ -2, 0, 0 }, RE::NiPoint3{ 2, 0, 0 } };
			CharacterRendering rendering;
			std::uint32_t frame = 0;
			for (float distance : { 1.0f, 1.001f, 2.0f, 100.0f }) {
				rendering.ObserveFaceDetail(++frame, 1, Head(distance));
				Require(rendering.state_->projections == 0 && !rendering.GetFaceDetailRegions(frame, 0, crop).current,
					"Distant heads must not project or activate protection");
			}
			for (float distance : { 0.5f, 0.75f, 0.875f, 0.99f }) {
				const auto before = rendering.state_->projections;
				rendering.ObserveFaceDetail(++frame, 1, Head(distance));
				const auto left = rendering.GetFaceDetailRegions(frame, 0, crop);
				Require(left.current && left.count == 1 && rendering.state_->projections == before + (vr ? 2u : 1u),
					"Nearby heads must project once per eye");
				const float expected = distance <= 0.75f ? 1.0f : (1.0f - distance) * 4.0f;
				Require(std::abs(left.proximityWeights[0] - expected) < 1e-5f, "Incorrect proximity fade");
				if (vr) {
					const auto right = rendering.GetFaceDetailRegions(frame, 1, crop);
					Require(right.current && right.proximityWeights[0] == left.proximityWeights[0],
						"Both eyes must share the same proximity weight");
				}
				rendering.ObserveFaceDetail(frame, 1, Head(distance));
				Require(rendering.state_->projections == before + (vr ? 2u : 1u), "Duplicate face draw repeated projection");
			}
			for (unsigned crossing = 0; crossing < 8; ++crossing) {
				const bool inside = crossing % 2 == 0;
				const auto before = rendering.state_->projections;
				rendering.ObserveFaceDetail(++frame, 1, Head(inside ? 0.99f : 1.01f));
				Require(rendering.GetFaceDetailRegions(frame, 0, crop).current == inside &&
							rendering.state_->projections == before + (inside ? (vr ? 2u : 1u) : 0u),
					"Crossing the distance boundary retained stale protection or projected a distant head");
			}
			const auto projected = rendering.state_->projections;
			rendering.ObserveFaceDetail(++frame, 1, Head(1.001f));
			Require(rendering.state_->projections == projected && !rendering.GetFaceDetailRegions(frame, 0, crop).current,
				"Moving away retained close-head protection");
			auto invalid = Head(0.5f);
			invalid.centerZ = std::numeric_limits<float>::quiet_NaN();
			rendering.ObserveFaceDetail(++frame, 1, invalid);
			globals::game::frameBufferCached.positions[0].x = std::numeric_limits<float>::infinity();
			rendering.ObserveFaceDetail(frame, 2, { 0, 0, 10, 10 });
			Require(rendering.state_->projections == projected, "Invalid coordinates reached head projection");
			globals::game::frameBufferCached.positions = {};
			rendering.state_->results = { CharacterProjectionResult::Uncertain, CharacterProjectionResult::Visible };
			rendering.ObserveFaceDetail(++frame, 1, Head(0.5f));
			Require(!rendering.GetFaceDetailRegions(frame, 0, crop).current, "Uncertain heads enabled protection");
			if (vr) {
				rendering.state_->results = { CharacterProjectionResult::Visible, CharacterProjectionResult::Uncertain };
				rendering.ObserveFaceDetail(++frame, 1, Head(0.5f));
				Require(!rendering.GetFaceDetailRegions(frame, 0, crop).current &&
							!rendering.GetFaceDetailRegions(frame, 1, crop).current,
					"One uncertain eye allowed asymmetric protection");
			}
			rendering.state_->results.fill(CharacterProjectionResult::Offscreen);
			rendering.ObserveFaceDetail(++frame, 1, Head(0.5f));
			const auto offscreen = rendering.GetFaceDetailRegions(frame, 0, crop);
			Require(offscreen.current && offscreen.count == 1 && offscreen.bounds[0] == std::array<float, 4>{},
				"Offscreen heads acquired visible protection bounds");
			// Rejected distant heads cannot consume slots needed by a close face.
			rendering.state_->results.fill(CharacterProjectionResult::Visible);
			++frame;
			for (unsigned actor = 1; actor <= kFaceDetailMaximumActors; ++actor)
				rendering.ObserveFaceDetail(frame, actor, Head(2.0f));
			rendering.ObserveFaceDetail(frame, kFaceDetailMaximumActors + 1, Head(0.5f));
			Require(rendering.GetFaceDetailRegions(frame, 0, crop).count == 1, "Distant crowd excluded a close face");
			globals::game::frameBufferCached.positions = { RE::NiPoint3{ 99998, -200000, 300000 }, RE::NiPoint3{ 100002, -200000, 300000 } };
			for (float distance : { 0.5f, 0.875f, 1.01f }) {
				rendering.ObserveFaceDetail(++frame, 1, Head(distance));
				const auto translated = rendering.GetFaceDetailRegions(frame, 0, crop);
				Require(translated.current == (distance < 1.0f), "World translation changed the proximity decision");
				if (translated.current)
					Require(std::abs(translated.proximityWeights[0] - (distance < 0.75f ? 1.0f : 0.5f)) < 0.002f,
						"World translation changed the proximity fade");
			}
		}
		std::cout << "PASS: face proximity, fade, stereo, stale observations and early projection rejection\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
