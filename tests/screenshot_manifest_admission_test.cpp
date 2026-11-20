#include "Features/ScreenshotManifestAdmission.h"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <semaphore>
#include <thread>

namespace
{
	struct AllocationGate
	{
		std::binary_semaphore entered{ 0 };
		std::binary_semaphore resume{ 0 };
	};
	thread_local bool failNextNew = false;
	thread_local AllocationGate* blockNextNew = nullptr;
	thread_local bool failQueueAllocation = false;
}

void* operator new(std::size_t a_size)
{
	if (std::exchange(failNextNew, false))
		throw std::bad_alloc{};
	if (const auto gate = std::exchange(blockNextNew, nullptr)) {
		gate->entered.release();
		gate->resume.acquire();
	}
	if (auto memory = std::malloc(a_size ? a_size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete(void* a_memory) noexcept { std::free(a_memory); }
void operator delete(void* a_memory, std::size_t) noexcept { std::free(a_memory); }

namespace
{
	using CSX::Api::ServiceFoundation;
	using CSX::Screenshot::AdmitManifestJob;
	using CSX::Screenshot::ManifestJob;
	using json = nlohmann::json;

	// Separate deque allocation from journal allocation without replacing either.
	template <class T>
	struct QueueAllocator
	{
		using value_type = T;
		QueueAllocator() = default;
		template <class U>
		QueueAllocator(const QueueAllocator<U>&) noexcept
		{}
		T* allocate(std::size_t a_count)
		{
			if (failQueueAllocation || a_count > std::numeric_limits<std::size_t>::max() / sizeof(T))
				throw std::bad_alloc{};
			if (auto memory = std::malloc(a_count * sizeof(T)))
				return static_cast<T*>(memory);
			throw std::bad_alloc{};
		}
		void deallocate(T* a_memory, std::size_t) noexcept { std::free(a_memory); }
		template <class U>
		bool operator==(const QueueAllocator<U>&) const noexcept
		{
			return true;
		}
	};
	struct WorkerState
	{
		std::mutex mutex;
		std::deque<ManifestJob, QueueAllocator<ManifestJob>> jobs;
		std::size_t outstanding = 0;
		bool stopRequested = false;
	};
	struct Sequence
	{
		std::uint64_t manifestGeneration = 7;
		std::uint64_t finalManifestGeneration = 3;
	};
	struct Record
	{
		std::string requestId = "manifest-request";
		std::uint64_t eventIndex = 0;
	};
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}
	ManifestJob Job(const Sequence& a_sequence, bool a_final)
	{
		return { .requestId = "manifest-request", .generation = a_sequence.manifestGeneration + 1, .final = a_final, .destination = "manifest.json" };
	}
	json Payload(const ManifestJob& a_job)
	{
		return { { "generation", a_job.generation }, { "final", a_job.final }, { "path", "manifest.json" } };
	}

	void CheckRefusedAdmission()
	{
		for (const bool final : { false, true }) {
			for (const bool stopping : { false, true }) {
				WorkerState state;
				Sequence sequence;
				Record record;
				ServiceFoundation service({ "manifest-test" });
				state.stopRequested = stopping;
				state.outstanding = stopping ? 0 : (final ? 256 : 192);
				auto job = Job(sequence, final);
				auto payload = Payload(job);
				bool refused = false;
				try {
					AdmitManifestJob(state, sequence, std::move(job), service, record, std::move(payload));
				} catch (const std::runtime_error&) {
					refused = true;
				}
				Require(refused && state.jobs.empty() && sequence.manifestGeneration == 7 &&
							sequence.finalManifestGeneration == 3 && record.eventIndex == 0 &&
							service.PollEvents(0, 10)["events"].empty(),
					"refused admission published generation or event");
			}
		}
	}

	void CheckAllocationRollback()
	{
		WorkerState state;
		Sequence sequence;
		Record record;
		ServiceFoundation service({ "manifest-test" });
		for (const bool final : { false, true }) {
			auto job = Job(sequence, final);
			auto payload = Payload(job);
			failNextNew = true;
			bool failed = false;
			try {
				AdmitManifestJob(state, sequence, std::move(job), service, record, std::move(payload));
			} catch (const std::bad_alloc&) {
				failed = true;
			}
			failNextNew = false;
			Require(failed && state.jobs.empty() && state.outstanding == 0 &&
						sequence.manifestGeneration == 7 && sequence.finalManifestGeneration == 3 && record.eventIndex == 0 &&
						service.JournalStatus()["latestEventId"] == 0,
				"journal allocation failure left admitted work or identity");
		}
		auto job = Job(sequence, true);
		auto payload = Payload(job);
		AdmitManifestJob(state, sequence, std::move(job), service, record, std::move(payload));
		const auto events = service.PollEvents(0, 10)["events"];
		Require(state.jobs.size() == 1 && state.outstanding == 1 && sequence.manifestGeneration == 8 &&
					sequence.finalManifestGeneration == 8 && record.eventIndex == 1 && events.size() == 1 &&
					events[0]["eventId"] == 1 && events[0]["payload"]["generation"] == 8,
			"successful successor did not commit exactly one coherent job/event");

		bool insertionFailed = false;
		for (std::size_t count = 0; count < 64 && !insertionFailed; ++count) {
			const auto beforeSize = state.jobs.size();
			const auto beforeGeneration = sequence.manifestGeneration;
			const auto beforeIndex = record.eventIndex;
			job = Job(sequence, false);
			payload = Payload(job);
			failQueueAllocation = true;
			try {
				AdmitManifestJob(state, sequence, std::move(job), service, record, std::move(payload));
			} catch (const std::bad_alloc&) {
				insertionFailed = true;
			}
			failQueueAllocation = false;
			if (insertionFailed)
				Require(state.jobs.size() == beforeSize && state.outstanding == beforeSize &&
							sequence.manifestGeneration == beforeGeneration && record.eventIndex == beforeIndex &&
							service.JournalStatus()["latestEventId"] == beforeIndex,
					"deque insertion failure published queued work or identity");
		}
		Require(insertionFailed, "fixture did not reach a real deque allocation boundary");
	}

	void CheckConcurrentObserver()
	{
		WorkerState state;
		Sequence sequence;
		Record record;
		ServiceFoundation service({ "manifest-test" });
		AllocationGate gate;
		std::exception_ptr failure;
		auto job = Job(sequence, true);
		auto payload = Payload(job);
		std::thread admission([&] {
			try {
				blockNextNew = &gate;
				AdmitManifestJob(state, sequence, std::move(job), service, record, std::move(payload));
			} catch (...) {
				failure = std::current_exception();
			}
			blockNextNew = nullptr;
		});
		const bool entered = gate.entered.try_acquire_for(std::chrono::seconds(2));
		bool workerCouldConsume = false;
		bool journalEmpty = false;
		if (entered) {
			workerCouldConsume = state.mutex.try_lock();
			if (workerCouldConsume)
				state.mutex.unlock();
			journalEmpty = service.PollEvents(0, 10)["events"].empty();
		}
		gate.resume.release();
		admission.join();
		if (failure)
			std::rethrow_exception(failure);
		Require(entered && !workerCouldConsume && journalEmpty,
			"worker or observer crossed a manifest publication in progress");
		Require(state.jobs.size() == 1 && state.outstanding == 1 && sequence.finalManifestGeneration == 8 &&
					service.PollEvents(0, 10)["events"].size() == 1,
			"concurrent publication did not complete coherently");
	}
}

int main()
try {
	CheckRefusedAdmission();
	CheckAllocationRollback();
	CheckConcurrentObserver();
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
