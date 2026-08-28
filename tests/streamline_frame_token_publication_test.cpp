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
}

int main()
{
	return TestConcurrentPublication() && TestFailureAndReset() ? 0 : 1;
}
