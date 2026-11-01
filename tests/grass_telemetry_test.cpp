#include "Features/GrassOptimizations/GrassTelemetry.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void CheckAdmission()
	{
		GrassTelemetry::Channel channel;
		Require(!channel.Read(), "An unpublished snapshot must remain unavailable");
		Require(!channel.BeginSample(1000), "Idle telemetry must not sample");
		for (unsigned i = 0; i < 100; ++i)
			channel.Request();
		Require(channel.BeginSample(1000), "Coalesced requests must admit one sample");
		Require(!channel.Requested(), "Admission consumes the requests");
		channel.Request();
		Require(!channel.BeginSample(1999) && channel.Requested(), "Rate limiting must retain the pending request");
		Require(channel.BeginSample(2000), "A pending request must be admitted after one second");
		Require(!channel.BeginSample(3000), "Requests must not become continuous recording");
		GrassTelemetry::Snapshot snapshot;
		snapshot.frame = 7;
		snapshot.work.dispatches = 0;
		channel.Publish(snapshot);
		snapshot.frame = 9;
		const auto retained = channel.Read();
		Require(retained && retained->frame == 7 && retained->work.dispatches == 0, "Publication must own a value copy and preserve zero work");
	}

	void CheckConcurrentReaders()
	{
		GrassTelemetry::Channel channel;
		std::atomic_bool finished = false, inconsistent = false;
		channel.Publish(GrassTelemetry::Snapshot{});
		std::atomic_uint64_t reads = 0;
		std::vector<std::thread> readers;
		for (unsigned i = 0; i < 4; ++i)
			readers.emplace_back([&] {
				while (!finished.load()) {
					channel.Request();
					if (const auto value = channel.Read()) {
						++reads;
						if (value->residentSources != value->frame * 3 || value->cullingBufferBytes != value->frame * 512 ||
							value->work.candidateEyeInstances != value->frame * 2)
							inconsistent.store(true);
					}
				}
			});
		while (reads.load() == 0)
			std::this_thread::yield();
		for (uint64_t frame = 1; frame <= 20000; ++frame) {
			GrassTelemetry::Snapshot value;
			value.frame = frame;
			value.residentSources = frame * 3;
			value.cullingBufferBytes = frame * 512;
			value.work.candidateEyeInstances = frame * 2;
			channel.Publish(value);
		}
		finished.store(true);
		for (auto& reader : readers)
			reader.join();
		Require(reads.load() > 0 && !inconsistent.load(), "Concurrent DevBench readers must receive consistent render-thread snapshots");
	}
}

int main()
{
	try {
		CheckAdmission();
		CheckConcurrentReaders();
		std::cout << "Grass telemetry admission and concurrent publication passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
