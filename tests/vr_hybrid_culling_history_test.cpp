#include "Features/VRHybridCullingHistory.h"
#include "Features/VRHybridCullingLifecycle.h"
#include "Features/VRHybridCullingSnapshot.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace
{
	using namespace VRHybridCullingHistory;

	constexpr Batch Submission()
	{
		return { 0x1000, 0x2000, 0x3000, 7, 120, 37, 1 };
	}

	constexpr VRHybridCullingSnapshot::Source DepthSource()
	{
		return {
			.view = 0x4000,
			.resourceGeneration = 11,
			.frame = 120,
			.width = 4034,
			.height = 2031,
			.textureFormat = 44,
			.viewFormat = 46,
			.sampleCount = 1,
			.phase = VRHybridCullingSnapshot::Phase::NativeDownscale,
		};
	}

	bool RejectsUnpublishedAndRecreatedDepthAtDispatch()
	{
		using namespace VRHybridCullingSnapshot;
		const auto source = DepthSource();
		if (!CanDispatch(source, source.resourceGeneration, true, source.frame, source.view))
			return false;
		// Invalidation retains the old publication and source until setup completes.
		if (CanDispatch(source, source.resourceGeneration, false, source.frame, source.view) ||
			CanDispatch(source, 0, false, source.frame, source.view) ||
			CanDispatch(source, source.resourceGeneration + 1, true, source.frame, source.view))
			return false;
		if (CanDispatch(source, source.resourceGeneration, true, source.frame + 1, source.view) ||
			CanDispatch(source, source.resourceGeneration, true, source.frame, source.view + 1))
			return false;
		auto invalid = source;
		invalid.resourceGeneration = 0;
		if (CanDispatch(invalid, 0, true, source.frame, source.view))
			return false;
		invalid = source;
		invalid.view = 0;
		if (CanDispatch(invalid, source.resourceGeneration, true, source.frame, 0))
			return false;
		for (const auto phase : { Phase::Unknown, Phase::NativeReadback }) {
			invalid = source;
			invalid.phase = phase;
			if (CanDispatch(invalid, source.resourceGeneration, true, source.frame, source.view))
				return false;
		}
		return true;
	}

	bool RejectsChangedDepthProvenanceAtReadback()
	{
		using namespace VRHybridCullingSnapshot;
		const auto producer = DepthSource();
		auto consumer = producer;
		consumer.phase = Phase::NativeReadback;
		++consumer.frame;
		if (!MatchesReadback(producer, consumer))
			return false;
		auto rebuilt = consumer;
		++rebuilt.resourceGeneration;
		if (MatchesReadback(producer, rebuilt))
			return false;
		rebuilt = consumer;
		++rebuilt.view;
		if (MatchesReadback(producer, rebuilt))
			return false;
		for (const auto member : { &Source::width, &Source::height, &Source::textureFormat, &Source::viewFormat, &Source::sampleCount }) {
			auto changed = consumer;
			++(changed.*member);
			if (MatchesReadback(producer, changed))
				return false;
		}
		for (const auto member : { &Source::nearDepth, &Source::farDepth }) {
			auto changed = consumer;
			changed.*member = std::nextafter(changed.*member, 10.0f);
			if (MatchesReadback(producer, changed))
				return false;
		}
		for (const auto phase : { Phase::Unknown, Phase::NativeDownscale }) {
			auto wrongPhase = consumer;
			wrongPhase.phase = phase;
			if (MatchesReadback(producer, wrongPhase))
				return false;
		}
		for (const auto frame : { producer.frame, producer.frame - 1, producer.frame + 2 }) {
			auto wrongFrame = consumer;
			wrongFrame.frame = frame;
			if (MatchesReadback(producer, wrongFrame))
				return false;
		}
		auto wrapProducer = producer;
		wrapProducer.frame = std::numeric_limits<std::uint32_t>::max();
		consumer.frame = 0;
		return MatchesReadback(wrapProducer, consumer);
	}

	bool RetriesPipelineOnlyAfterOwnerOrCacheChange()
	{
		using namespace VRHybridCullingSnapshot;
		struct Preparation
		{
			PipelineOwner owner;
			std::uint64_t publication;
			bool cacheCleared;
			bool creationSucceeds;
			bool pipelineAvailable;
		};
		const std::array preparations{
			Preparation{ { 0x1000, 0x2000 }, 1, false, false, false },
			Preparation{ { 0x1000, 0x2000 }, 1, false, true, false },
			Preparation{ { 0x1000, 0x2000 }, 2, false, true, false },
			Preparation{ { 0x1000, 0x2000 }, 2, true, true, true },
			Preparation{ { 0x1000, 0x2000 }, 3, false, false, true },
			Preparation{ { 0x1000, 0x3000 }, 4, false, false, false },
			Preparation{ { 0x1000, 0x3000 }, 5, false, true, false },
			Preparation{ { 0x4000, 0x5000 }, 6, false, true, true },
		};
		PipelineOwner retained{};
		auto preparedSource = DepthSource();
		bool available = false;
		unsigned attempts = 0;
		for (const auto& preparation : preparations) {
			if (ShouldRecreatePipeline(retained, preparation.owner, preparation.cacheCleared)) {
				retained = preparation.owner;
				available = preparation.creationSucceeds;
				++attempts;
			}
			preparedSource.resourceGeneration = preparation.publication;
			if (available != preparation.pipelineAvailable ||
				!MatchesPipelineOwner(retained, preparation.owner) ||
				!HasCurrentPublication(preparedSource, preparation.publication, true))
				return false;
		}
		for (const auto missing : { PipelineOwner{}, PipelineOwner{ 0x4000, 0 }, PipelineOwner{ 0, 0x5000 } }) {
			if (MatchesPipelineOwner(retained, missing) || ShouldRecreatePipeline(retained, missing, true))
				return false;
		}
		return attempts == 4;
	}

	bool CoversSubmissionIdentityAndLatency()
	{
		const auto producer = Submission();
		auto consumer = producer;
		++consumer.frame;
		if (!Matches(producer, consumer))
			return false;
		for (const auto member : { &Batch::culler, &Batch::transforms, &Batch::results }) {
			auto other = consumer;
			other.*member += 0x100;
			if (Matches(producer, other))
				return false;
		}
		for (const auto member : { &Batch::count, &Batch::selector }) {
			auto other = consumer;
			++(other.*member);
			if (Matches(producer, other))
				return false;
		}
		for (const auto frame : { producer.frame, producer.frame - 1, producer.frame + 2 }) {
			auto other = consumer;
			other.frame = frame;
			if (Matches(producer, other))
				return false;
		}
		++consumer.epoch;
		if (Matches(producer, consumer))
			return false;

		auto wrapProducer = producer;
		wrapProducer.frame = std::numeric_limits<std::uint32_t>::max();
		auto wrapConsumer = wrapProducer;
		wrapConsumer.frame = 0;
		return Matches(wrapProducer, wrapConsumer);
	}

	bool CoversMalformedSubmissions()
	{
		const auto rejects = [](Batch a_batch) {
			auto consumer = a_batch;
			++consumer.frame;
			return !Matches(a_batch, consumer);
		};
		for (const auto member : { &Batch::culler, &Batch::transforms, &Batch::results }) {
			auto missing = Submission();
			missing.*member = 0;
			if (!rejects(missing))
				return false;
		}
		for (const auto count : { 0u, VRDepthCullingTemporalPolicy::kMaximumObjects + 1u }) {
			auto invalid = Submission();
			invalid.count = count;
			if (!rejects(invalid))
				return false;
		}
		auto invalid = Submission();
		invalid.selector = 2;
		if (!rejects(invalid))
			return false;
		invalid = Submission();
		invalid.epoch = 0;
		if (!rejects(invalid))
			return false;
		auto full = Submission();
		full.count = VRDepthCullingTemporalPolicy::kMaximumObjects;
		full.selector = 0;
		return !rejects(full);
	}

	EyePose Camera(float a_yawDegrees = 0.0f, float a_axisScale = 1.0f)
	{
		EyePose pose{};
		const auto radians = a_yawDegrees * 0.017453292519943295f;
		pose.rotation[0][0] = std::cos(radians) * a_axisScale;
		pose.rotation[0][1] = -std::sin(radians) * a_axisScale;
		pose.rotation[1][0] = std::sin(radians) * a_axisScale;
		pose.rotation[1][1] = std::cos(radians) * a_axisScale;
		pose.rotation[2][2] = a_axisScale;
		pose.position[0] = 10.0f;
		pose.position[1] = -20.0f;
		pose.position[2] = 30.0f;
		pose.projection[0][0] = 1.2f;
		pose.projection[1][1] = 1.3f;
		pose.projection[2][0] = 0.04f;
		pose.projection[2][2] = 1.0001f;
		pose.projection[2][3] = 1.0f;
		pose.projection[3][2] = -0.1f;
		return pose;
	}

	bool CoversPhysicalCameraMotion()
	{
		const auto producer = Camera();
		if (!IsCoherent(producer, producer) || !IsCoherent(producer, Camera(0.02f)) ||
			IsCoherent(producer, Camera(0.15f)))
			return false;
		for (std::size_t axis = 0; axis < 3; ++axis) {
			auto moved = producer;
			moved.position[axis] += 0.09f;
			if (!IsCoherent(producer, moved))
				return false;
			moved.position[axis] += 0.02f;
			if (IsCoherent(producer, moved))
				return false;
		}
		auto diagonal = producer;
		diagonal.position[0] += 0.08f;
		diagonal.position[1] += 0.08f;
		if (IsCoherent(producer, diagonal))
			return false;

		// Admitted floating-point drift must not hide rotation larger than the reuse window.
		return !IsCoherent(Camera(0.0f, 1.0001f), Camera(0.15f, 1.0001f));
	}

	bool CoversProjectionIdentityAndNonfiniteInputs()
	{
		const auto producer = Camera();
		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				auto changed = producer;
				changed.projection[row][column] = std::nextafter(changed.projection[row][column], 10.0f);
				if (IsCoherent(producer, changed))
					return false;
			}
		}
		for (const auto value : { std::numeric_limits<float>::infinity(),
				 -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			for (unsigned field = 0; field < 3; ++field) {
				auto invalid = producer;
				if (field == 0)
					invalid.position[0] = value;
				else if (field == 1)
					invalid.rotation[0][0] = value;
				else
					invalid.projection[0][0] = value;
				if (IsCoherent(producer, invalid) || IsCoherent(invalid, producer) || IsCoherent(invalid, invalid))
					return false;
			}
		}
		auto distant = producer;
		distant.position[0] = std::numeric_limits<float>::max();
		return !IsCoherent(producer, distant);
	}

	bool CoversInvalidRigidTransforms()
	{
		const auto producer = Camera();
		std::array invalid{ Camera(0.0f, 2.0f), Camera(0.0f, 0.0f), Camera(), Camera() };
		invalid[2].rotation[0][1] = 0.01f;
		invalid[3].rotation[1][0] = 1.0f;
		invalid[3].rotation[1][1] = 0.0f;
		for (const auto& pose : invalid) {
			if (IsCoherent(producer, pose) || IsCoherent(pose, producer) || IsCoherent(pose, pose))
				return false;
		}
		return true;
	}

	bool CoversNativeHandednessAndBothEyes()
	{
		auto native = Camera();
		const float basis[3][3]{ { 0.998986f, 0.0f, 0.045011f },
			{ -0.045011f, 0.0f, 0.998986f }, { 0.0f, 1.0f, 0.0f } };
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned column = 0; column < 3; ++column)
				native.rotation[row][column] = basis[row][column];
		if (!IsCoherent(native, native))
			return false;
		auto oppositeHandedness = native;
		for (auto& component : oppositeHandedness.rotation[0])
			component = -component;
		if (IsCoherent(native, oppositeHandedness))
			return false;

		std::array producer{ native, native };
		producer[0].position[0] -= 3.0f;
		producer[1].position[0] += 3.0f;
		if (!IsStereoCoherent(producer, producer))
			return false;
		for (unsigned eye = 0; eye < producer.size(); ++eye) {
			auto consumer = producer;
			consumer[eye].position[0] += 1.0f;
			if (IsStereoCoherent(producer, consumer))
				return false;
			consumer = producer;
			consumer[eye].projection[2][0] += 0.01f;
			if (IsStereoCoherent(producer, consumer))
				return false;
		}
		return true;
	}

	enum class ProducerEvent
	{
		TryHybrid,
		Dispatch,
		Replay,
		Cancel,
		Native,
		CapturePose
	};

	bool CoversProducerRoutingAndRetirement()
	{
		using enum ProducerEvent;
		struct Case
		{
			bool suppressed;
			bool selected;
			bool dispatchSucceeds;
			bool usesHybrid;
			std::vector<ProducerEvent> expected;
		};
		const std::array cases{
			Case{ false, true, true, false, { Cancel, Native, CapturePose } },
			Case{ true, true, true, true, { TryHybrid, Dispatch } },
			Case{ true, true, false, false, { TryHybrid, Dispatch, Replay, Cancel, Native, CapturePose } },
			Case{ true, false, true, false, { TryHybrid, Replay, Cancel, Native, CapturePose } },
		};
		for (const auto& test : cases) {
			bool suppressed = test.suppressed;
			bool retiredBeforeCallbacks = true;
			std::vector<ProducerEvent> events;
			const auto observe = [&](ProducerEvent event) {
				retiredBeforeCallbacks = retiredBeforeCallbacks && !suppressed;
				events.push_back(event);
			};
			const auto run = [&]() {
				return VRHybridCullingLifecycle::RunProducer(suppressed, [&]() {
						observe(TryHybrid);
						if (!test.selected)
							return false;
						observe(Dispatch);
						return test.dispatchSucceeds; }, [&]() { observe(Replay); }, [&]() { observe(Cancel); }, [&]() { observe(Native); }, [&]() { observe(CapturePose); });
			};
			if (run() != test.usesHybrid || suppressed || !retiredBeforeCallbacks || events != test.expected)
				return false;
			events.clear();
			if (run() || suppressed || events != std::vector{ Cancel, Native, CapturePose })
				return false;
		}
		return true;
	}

	bool CoversProducerExceptionPropagation()
	{
		using enum ProducerEvent;
		struct CallbackFailure
		{};
		for (const auto failure : { TryHybrid, Replay }) {
			bool suppressed = true;
			bool caught = false;
			std::vector<ProducerEvent> events;
			const auto run = [&]() {
				return VRHybridCullingLifecycle::RunProducer(suppressed, [&]() {
						events.push_back(TryHybrid);
						if (failure == TryHybrid)
							throw CallbackFailure{};
						return false; }, [&]() {
						events.push_back(Replay);
						throw CallbackFailure{}; }, [&]() { events.push_back(Cancel); }, [&]() { events.push_back(Native); }, [&]() { events.push_back(CapturePose); });
			};
			try {
				run();
			} catch (const CallbackFailure&) {
				caught = true;
			}
			const auto expected = failure == TryHybrid ? std::vector{ TryHybrid } : std::vector{ TryHybrid, Replay };
			if (!caught || suppressed || events != expected)
				return false;
			events.clear();
			if (run() || events != std::vector{ Cancel, Native, CapturePose })
				return false;
		}
		return true;
	}
}

int main()
{
	const std::array cases{
		std::pair{ "depth publication retirement before dispatch", RejectsUnpublishedAndRecreatedDepthAtDispatch },
		std::pair{ "depth provenance across delayed readback", RejectsChangedDepthProvenanceAtReadback },
		std::pair{ "pipeline owner replacement and latched failure retry", RetriesPipelineOnlyAfterOwnerOrCacheChange },
		std::pair{ "submission identity and one-frame latency", CoversSubmissionIdentityAndLatency },
		std::pair{ "malformed native submissions", CoversMalformedSubmissions },
		std::pair{ "physical camera motion", CoversPhysicalCameraMotion },
		std::pair{ "projection identity and finite inputs", CoversProjectionIdentityAndNonfiniteInputs },
		std::pair{ "proper rigid transforms", CoversInvalidRigidTransforms },
		std::pair{ "native handedness and both-eye validity", CoversNativeHandednessAndBothEyes },
		std::pair{ "producer routing and suppression retirement", CoversProducerRoutingAndRetirement },
		std::pair{ "producer exceptions stop fallback and retire suppression", CoversProducerExceptionPropagation },
	};
	for (const auto& [name, test] : cases) {
		if (!test()) {
			std::fprintf(stderr, "Failed: %s\n", name);
			return 1;
		}
	}
	std::puts("Hybrid visibility history and lifecycle tests passed");
	return 0;
}
