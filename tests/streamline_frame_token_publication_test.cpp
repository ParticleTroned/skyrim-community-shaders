#include "Features/Upscaling/NvidiaPipelinePolicy.h"
#include "Features/Upscaling/StreamlineFrameTokenPublication.h"

#include <atomic>
#include <future>
#include <optional>
#include <thread>

namespace
{
	struct Token
	{
		std::uint32_t value = 0;
	};

	using Coordinator =
		StreamlineFrameTokenPublication::Coordinator<Token*>;

	bool TestConcurrentPublication()
	{
		Coordinator coordinator;
		Token previous{ 1 };
		Token current{ 2 };
		Token unexpected{ 99 };
		std::atomic_uint32_t acquisitions = 0;

		auto firstFrame = coordinator.Resolve(
			1,
			[&](std::uint32_t) -> std::optional<Token*> {
				++acquisitions;
				return &previous;
			});
		if (!firstFrame || firstFrame->token != &previous || !firstFrame->acquired)
			return false;

		std::promise<void> acquisitionEntered;
		std::promise<void> allowPublication;
		auto allowPublicationFuture = allowPublication.get_future();
		std::optional<Coordinator::Snapshot> firstCaller;
		std::optional<Coordinator::Snapshot> secondCaller;

		std::thread first([&]() {
			firstCaller = coordinator.Resolve(
				2,
				[&](std::uint32_t) -> std::optional<Token*> {
					++acquisitions;
					acquisitionEntered.set_value();
					allowPublicationFuture.wait();
					return &current;
				});
		});

		acquisitionEntered.get_future().wait();
		std::thread second([&]() {
			secondCaller = coordinator.Resolve(
				2,
				[&](std::uint32_t) -> std::optional<Token*> {
					++acquisitions;
					return &unexpected;
				});
		});

		allowPublication.set_value();
		first.join();
		second.join();

		return acquisitions == 2 &&
		       firstCaller && firstCaller->frame == 2 &&
		       firstCaller->token == &current && firstCaller->acquired &&
		       secondCaller && secondCaller->frame == 2 &&
		       secondCaller->token == &current && !secondCaller->acquired;
	}

	bool TestFailureAndReset()
	{
		Coordinator coordinator;
		Token current{ 7 };
		std::atomic_uint32_t acquisitions = 0;

		auto failed = coordinator.Resolve(
			7,
			[&](std::uint32_t) -> std::optional<Token*> {
				++acquisitions;
				return std::nullopt;
			});
		if (failed)
			return false;

		auto recovered = coordinator.Resolve(
			7,
			[&](std::uint32_t) -> std::optional<Token*> {
				++acquisitions;
				return &current;
			});
		if (!recovered || recovered->token != &current || !recovered->acquired)
			return false;

		coordinator.Reset();
		auto reacquired = coordinator.Resolve(
			7,
			[&](std::uint32_t) -> std::optional<Token*> {
				++acquisitions;
				return &current;
			});
		return reacquired && reacquired->acquired && acquisitions == 3;
	}

	bool TestStaleFrameCannotReplacePublication()
	{
		Coordinator coordinator;
		Token frameN{ 10 };
		Token frameNPlusOne{ 11 };
		Token duplicateFrameN{ 99 };
		std::atomic_uint32_t acquisitions = 0;

		auto first = coordinator.Resolve(10, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &frameN;
		});
		auto next = coordinator.Resolve(11, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &frameNPlusOne;
		});
		auto stale = coordinator.Resolve(10, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &duplicateFrameN;
		});

		return first && next && !stale && acquisitions == 2;
	}

	bool TestFrameCounterWrapRemainsMonotonic()
	{
		Coordinator coordinator;
		Token beforeWrap{ UINT32_MAX };
		Token afterWrap{ 0 };
		std::atomic_uint32_t acquisitions = 0;

		auto first = coordinator.Resolve(UINT32_MAX, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &beforeWrap;
		});
		auto wrapped = coordinator.Resolve(0, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &afterWrap;
		});

		return first && wrapped && wrapped->token == &afterWrap && acquisitions == 2;
	}

	bool TestOptionalPipelinePolicies()
	{
		using namespace CSX::NvidiaPipelinePolicy;

		InteropFenceSequence fences;
		const auto firstProducer = fences.Next();
		const auto firstConsumer = fences.Next();
		if (!firstProducer || *firstProducer != 1u ||
			!firstConsumer || *firstConsumer != 2u)
			return false;
		fences.Reset();
		if (fences.Next() != 1u)
			return false;

		if (ResolveBackendBufferCount(0u, 1u) != 2u ||
			ResolveBackendBufferCount(1u, 1u) != 2u ||
			ResolveBackendBufferCount(2u, 1u))
			return false;

		if (!CanContinueBasePresentation(true, false) ||
			!CanContinueBasePresentation(false, true) ||
			CanContinueBasePresentation(false, false))
			return false;

		if (MustRetainModuleAfterShutdown(false, false) ||
			MustRetainModuleAfterShutdown(true, true) ||
			!MustRetainModuleAfterShutdown(true, false))
			return false;

		const auto dlssOnly = ResolveRuntimeAvailability(true, true, false, false);
		const auto reflexOnly = ResolveRuntimeAvailability(true, false, true, false);
		const auto noCore = ResolveRuntimeAvailability(false, true, true, true);
		return dlssOnly.core && dlssOnly.dlss && !dlssOnly.reflex && !dlssOnly.pcl &&
		       reflexOnly.core && !reflexOnly.dlss && reflexOnly.reflex && !reflexOnly.pcl &&
		       !noCore.core && !noCore.dlss && !noCore.reflex && !noCore.pcl;
	}
}

int main()
{
	return TestConcurrentPublication() &&
	               TestFailureAndReset() &&
	               TestStaleFrameCannotReplacePublication() &&
	               TestFrameCounterWrapRemainsMonotonic() &&
	               TestOptionalPipelinePolicies() ?
	           0 :
	           1;
}
