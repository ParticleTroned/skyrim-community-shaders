#include "Features/Upscaling/NeuralRendering/CharacterMaskReadback.h"
#include "Features/Upscaling/NeuralRendering/CharacterRendering.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>

struct float4
{
	float x{}, y{}, z{}, w{};
	bool operator==(const float4&) const = default;
};
struct float4x4
{
	unsigned identity{};
	float4x4 Transpose() const { return *this; }
	bool operator==(const float4x4&) const = default;
};
using Matrix = float4x4;
namespace RE
{
	struct NiPoint3
	{
		float x{}, y{}, z{};
		bool operator==(const NiPoint3&) const = default;
	};
}
namespace globals::game
{
	struct Camera
	{
		unsigned epoch = 100;
		float4x4 GetCameraViewProjUnjittered(unsigned eye) const { return { epoch + eye }; }
		float4x4 GetCameraViewProj(unsigned eye) const { return { epoch + 2 + eye }; }
		Matrix GetCameraProjInverse(unsigned eye) const { return { epoch + 4 + eye }; }
	} frameBufferCached;
}
namespace Util
{
	float4 cameraData{ 1, 2, 3, 4 };
	float4 GetCameraData() { return cameraData; }
}
std::array<RE::NiPoint3, 2> positions{ RE::NiPoint3{ 1, 2, 3 }, RE::NiPoint3{ 5, 6, 7 } };
RE::NiPoint3 GetProjectionEyePosition(unsigned eye) { return positions[eye]; }
using namespace NeuralRendering;

struct Fixture
{
	struct Observation
	{
		unsigned identity;
	};
	struct ActorAdmission
	{
		unsigned frame, identity;
	};
#include "neural_source_types_under_test.h"
	SourceGeometry capturedGeometry_;
	std::uint32_t unboundedCategoryMask_ = 2;
	std::vector<Observation> observations_{ { 7 }, { 8 } };
	std::unordered_map<std::uint32_t, ActorAdmission> actorAdmissions_{
		{ 20, { 10, 2 } }, { 10, { 10, 1 } }, { 30, { 9, 3 } }
	};
	unsigned invalidations = 0;
	void InvalidateProjectionCache() { ++invalidations; }
#include "neural_source_admission_under_test.h"
#include "neural_source_capture_under_test.h"
	struct EarlyMaskReadback
	{
		unsigned captureSerial{}, frame{}, width{}, height{}, eyeCount{}, categories{};
	};
	std::array<EarlyMaskReadback, 3> earlyMaskReadbacks_{};
	unsigned earlyMaskCaptureSerial_ = 7, capturedFrame_ = 10;
	unsigned capturedEyeWidth_ = 100, capturedHeight_ = 80, capturedEyeCount_ = 2;
#include "neural_source_support_under_test.h"
};
static void Require(bool value)
{
	if (!value)
		throw std::runtime_error("Source transaction changed or admitted stale identity");
}
int main()
{
	struct RingEntry
	{
		bool pending = true;
		bool complete = false;
	};
	std::array<RingEntry, 3> ring;
	unsigned probes = 0;
	const auto completed = [&](auto& entry) { ++probes; return entry.complete; };
	for (unsigned repeat = 0; repeat < 100; ++repeat) {
		probes = 0;
		Require(!FindReusableCharacterMaskReadback(std::span<RingEntry>(ring), completed) && probes == 3);
		Require(std::ranges::all_of(ring, [](auto& entry) { return entry.pending; }));
	}
	ring[2].complete = true;
	Require(FindReusableCharacterMaskReadback(std::span<RingEntry>(ring), completed) == &ring[2]);
	Require(ring[0].pending && ring[1].pending && !ring[2].pending);
	probes = 0;
	Require(FindReusableCharacterMaskReadback(std::span<RingEntry>(ring), completed) == &ring[2] && probes == 2);
	Fixture f;
	f.CaptureSourceGeometry(10, 2);
	Require(f.capturedGeometry_.frame == 10 && f.invalidations == 1);
	Require(f.FindCapturedAdmission(10)->identity == 1 && !f.FindCapturedAdmission(30));
	Require(f.capturedGeometry_.averageEye == RE::NiPoint3{ 3, 4, 5 });
	const auto first = f.capturedGeometry_;
	// Simulate the producer advancing between the two source consumers.
	f.observations_ = { { 90 } };
	f.actorAdmissions_.at(10) = { 11, 99 };
	f.unboundedCategoryMask_ = 14;
	globals::game::frameBufferCached.epoch = 900;
	Util::cameraData = { 9, 8, 7, 6 };
	positions[0] = { 100, 200, 300 };
	Require(f.capturedGeometry_.observations.size() == 2 && f.capturedGeometry_.observations[0].identity == 7);
	Require(f.FindCapturedAdmission(10)->identity == 1 && f.capturedGeometry_.unboundedCategoryMask == 2);
	for (unsigned eye = 0; eye < 2; ++eye) {
		Require(f.capturedGeometry_.eyes[eye].position == first.eyes[eye].position);
		Require(f.capturedGeometry_.eyes[eye].unjittered == first.eyes[eye].unjittered);
		Require(f.capturedGeometry_.eyes[eye].raster == first.eyes[eye].raster);
		Require(f.capturedGeometry_.eyes[eye].inverse == first.eyes[eye].inverse);
	}
	Require(f.capturedGeometry_.depthLinearization == first.depthLinearization);
	f.CaptureSourceGeometry(11, 1);
	Require(f.capturedGeometry_.frame == 11 && f.capturedGeometry_.observations[0].identity == 90);
	Require(f.FindCapturedAdmission(10)->identity == 99 && !f.FindCapturedAdmission(20));
	Require(f.capturedGeometry_.averageEye == positions[0] && f.capturedGeometry_.eyes[0].inverse.identity == 904);
	Require(f.capturedGeometry_.depthLinearization == Util::cameraData);

	CharacterMaskPrepareArgs args{};
	args.sourceWorldFrame = 10;
	f.earlyMaskReadbacks_ = { Fixture::EarlyMaskReadback{ 6, 10, 100, 80, 2, 14 },
		Fixture::EarlyMaskReadback{ 7, 9, 100, 80, 2, 14 }, Fixture::EarlyMaskReadback{ 7, 10, 100, 80, 2, 14 } };
	for (unsigned eye = 0; eye < 2; ++eye) {
		args.eyeIndex = eye;
		Require(f.FindCurrentSupport(args) == &f.earlyMaskReadbacks_[2]);
	}
	for (unsigned mismatch = 0; mismatch < 7; ++mismatch) {
		auto& r = f.earlyMaskReadbacks_[2];
		r = { 7, 10, 100, 80, 2, 14 };
		if (mismatch == 0)
			r.captureSerial++;
		if (mismatch == 1)
			r.frame++;
		if (mismatch == 2)
			r.width++;
		if (mismatch == 3)
			r.height++;
		if (mismatch == 4)
			r.eyeCount--;
		if (mismatch == 5)
			r.categories = 2;
		if (mismatch == 6)
			args.eyeIndex = 2;
		Require(!f.FindCurrentSupport(args));
	}
}
