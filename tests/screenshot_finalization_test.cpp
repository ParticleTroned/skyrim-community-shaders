#include "BuildProvenance.h"
#include "Features/ScreenshotApi.h"
#include "Features/ScreenshotReferencePolicy.h"
#include "Utils/StringUtils.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <intrin.h>
#include <iostream>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	thread_local bool failNextAllocation = false;
	thread_local bool failAllAllocations = false;
	thread_local std::array<std::string_view, 2> failEvents{};
	thread_local bool persistentEventFailure = false;
	thread_local unsigned eventFaults = 0;
	enum class StackCaptureStatus : unsigned
	{
		NotAttempted,
		Captured,
		Empty,
		Reentrant,
		NativeException
	};
	struct RejectedAllocation
	{
		void* caller = nullptr;
		std::size_t bytes = 0;
		std::array<void*, 16> frames{};
		USHORT frameCount = 0;
		StackCaptureStatus captureStatus = StackCaptureStatus::NotAttempted;
		DWORD captureException = 0;
	};
	thread_local std::array<RejectedAllocation, 16> rejectedAllocations{};
	thread_local std::size_t rejectedAllocationCount = 0;
	thread_local bool capturingRejectedStack = false;
	void* executableAddressMarker = nullptr;
	const char* currentCase = "startup";

	void CaptureRejectedStack(RejectedAllocation& allocation) noexcept
	{
		if (capturingRejectedStack) {
			allocation.captureStatus = StackCaptureStatus::Reentrant;
			return;
		}
		capturingRejectedStack = true;
		__try {
			allocation.frameCount = CaptureStackBackTrace(
				0, static_cast<DWORD>(allocation.frames.size()), allocation.frames.data(), nullptr);
			allocation.captureStatus = allocation.frameCount ? StackCaptureStatus::Captured : StackCaptureStatus::Empty;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			allocation.captureException = GetExceptionCode();
			allocation.captureStatus = StackCaptureStatus::NativeException;
		}
		capturingRejectedStack = false;
	}

	void EventAllocation(std::string_view type)
	{
		for (auto& selected : failEvents) {
			if (!selected.empty() && selected == type) {
				selected = {};
				++eventFaults;
				failNextAllocation = true;
				failAllAllocations = persistentEventFailure;
				return;
			}
		}
	}
	void ClearFaults() noexcept
	{
		failNextAllocation = failAllAllocations = persistentEventFailure = false;
		failEvents = {};
	}
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

void* operator new(std::size_t size)
{
	if (failAllAllocations || std::exchange(failNextAllocation, false)) {
		// Fixed storage preserves failure sites without allocating during unwinding.
		auto& allocation = rejectedAllocations[rejectedAllocationCount++ % rejectedAllocations.size()];
		allocation = { _ReturnAddress(), size };
		CaptureRejectedStack(allocation);
		throw std::bad_alloc{};
	}
	if (auto memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace logger
{
	template <class... Args>
	void error(Args&&...)
	{}
}
namespace BuildProvenance
{
	// Inert environmental metadata; no issued artifact or worker success is inferred.
	nlohmann::json GetProducer() { return { { "fixture", "finalization-input" } }; }
}

// Only lifecycle setup is bound for a CPU fixture; all coordinator records and methods are production code.
ScreenshotApi::ScreenshotApi(FinalizationTestTag) :
	service({ "csx.screenshot", kContractMajor, kContractMinor, kSchemaRevision }),
	manifestWorkerState(std::make_shared<ManifestWorkerState>())
{}
ScreenshotApi::~ScreenshotApi() = default;

#include "screenshot_finalization_under_test.h"

struct ScreenshotFinalizationTest
{
	using json = nlohmann::json;
	struct Case
	{
		std::filesystem::path root;
		std::unique_ptr<ScreenshotApi> api;
		std::weak_ptr<CSX::ScreenshotStorage::DirectoryLease> lease;
		Case() :
			root(std::filesystem::temp_directory_path() / ("csx-finalization-" + CSX::Api::ServiceFoundation::NewId())),
			api(new ScreenshotApi(ScreenshotApi::FinalizationTestTag{}))
		{
			Require(std::filesystem::create_directory(root), "fixture root was not exclusively created");
			auto owned = CSX::ScreenshotStorage::DirectoryLease::CreateExclusive(root, "finalization-sequence");
			lease = owned;
			auto& record = api->requests["sequence"];
			record.requestId = "sequence";
			record.commandId = "finalization-command";
			record.kind = "sequence";
			record.state = "recording";
			auto& sequence = api->sequences["sequence"];
			sequence.requestId = "sequence";
			sequence.frameCount = 1;
			sequence.nextOrdinal = 2;
			sequence.directoryLease = std::move(owned);
			sequence.finalManifestPath = sequence.directoryLease->Path() / "manifest.json";
			sequence.partialManifestPath = sequence.directoryLease->Path() / "partial.json";
			sequence.packaging = { { "previewVideo", { { "state", "disabled" } } } };
		}
		~Case()
		{
			ClearFaults();
			api.reset();
			std::error_code error;
			std::filesystem::remove_all(root, error);
			if (error) {
				std::fprintf(stderr, "fixture cleanup failed with error %d\n", error.value());
				std::terminate();
			}
		}
		auto& Record() { return api->requests.at("sequence"); }
		auto& Sequence() { return api->sequences.at("sequence"); }
		void FailedWithoutJob(std::uint64_t events)
		{
			Require(Record().state == "failed" && Record().finalizationFailure &&
						Sequence().finalizing && lease.expired() && api->CountPendingOperationsLocked() == 0,
				"failure left a nonterminal sequence, lease or pending slot");
			Require(api->manifestWorkerState->jobs.empty() && api->manifestWorkerState->outstanding == 0 &&
						Sequence().manifestGeneration == 0 && Sequence().finalManifestGeneration == 0,
				"rejected admission left worker-visible state or generation");
			Require(Record().eventIndex == events && api->service.JournalStatus()["latestEventId"] == events,
				"failed event consumed an identity");
			const auto receipt = api->MakeReceipt(Record());
			Require(receipt["error"]["code"] == "sequence_finalization_failed", "original failure is absent from receipt");
		}
	};

	static void RejectedFinalization(std::string_view first, std::string_view second, bool stopping, bool persistent)
	{
		Case test;
		test.api->manifestWorkerState->stopRequested = stopping;
		failEvents = { first, second };
		persistentEventFailure = persistent;
		eventFaults = 0;
		test.api->TryFinalizeSequenceLocked(test.Sequence());
		ClearFaults();
		const std::uint64_t expected = first == "sequence.finalizing" ? 0 :
		                                                                (first == "packaging.queued" && second.empty() && !persistent ? 2 : 1);
		test.FailedWithoutJob(expected);
		Require(eventFaults == (second.empty() ? 1u : 2u), "fault did not reach its actual journal allocation boundary");
		if (second == "packaging.failed" || stopping || persistent)
			Require(test.Record().finalizationJournalFailure && test.Sequence().manifestReportingFailure,
				"secondary reporting exception was discarded");
		test.api->TryFinalizeSequenceLocked(test.Sequence());
		test.FailedWithoutJob(expected);
	}

	static void TerminalPublication()
	{
		Case test;
		test.api->TryFinalizeSequenceLocked(test.Sequence());
		Require(test.api->manifestWorkerState->jobs.size() == 1 && test.api->manifestWorkerState->outstanding == 1 &&
					test.Sequence().manifestGeneration == 1 && test.Sequence().finalManifestGeneration == 1 &&
					test.Record().eventIndex == 2 && !test.lease.expired(),
			"positive admission transaction changed");
		// Controlled worker-result input; no manifest file is written or certified here.
		test.api->manifestWorkerState->jobs.pop_front();
		--test.api->manifestWorkerState->outstanding;
		ScreenshotApi::ManifestResult result{ .requestId = "sequence", .generation = 1, .final = true, .success = false, .error = "worker failure" };
		failEvents = { "request.terminal", {} };
		eventFaults = 0;
		test.api->FinalizeSequenceLocked(test.Sequence(), &result);
		ClearFaults();
		Require(eventFaults == 1 && test.Record().state == "failed" && test.Record().finalizationFailure &&
					test.lease.expired() && test.api->CountPendingOperationsLocked() == 0,
			"terminal journal failure retained storage or pending capacity");
		Require(test.Record().eventIndex == 2 && test.api->service.JournalStatus()["latestEventId"] == 2 &&
					test.Sequence().manifestGeneration == 1 && test.Sequence().finalManifestGeneration == 1,
			"terminal event failure rewrote admitted identities");
	}

	static void TerminalMetadataAllocation()
	{
		Case test;
		test.Sequence().frameManifest = false;
		// Bypass only the earlier admission step to fail the real terminal metadata allocation.
		failNextAllocation = true;
		test.api->FinalizeSequenceLocked(test.Sequence(), nullptr);
		ClearFaults();
		Require(test.Record().state == "failed" && test.Record().finalizationFailure && test.lease.expired() &&
					test.api->CountPendingOperationsLocked() == 0,
			"terminal JSON failure prevented mandatory cleanup");
	}

	static void NotificationAndSuccess()
	{
		Case test;
		test.Sequence().frameManifest = false;
		unsigned callbacks = 0;
		test.Record().referenceCompletion = [&](const json& receipt) {
			Require(receipt["requestId"] == "finalization-command", "native receipt identity changed");
			++callbacks;
		};
		test.api->TryFinalizeSequenceLocked(test.Sequence());
		Require(test.Record().state == "completed" && test.lease.expired() && test.Record().eventIndex == 2 &&
					test.api->CountPendingOperationsLocked() == 0,
			"ordinary non-manifest terminal transition changed");
		failNextAllocation = true;
		test.api->DrainReferenceNotifications();
		ClearFaults();
		Require(callbacks == 0 && test.Record().referenceCompletion && test.Record().referenceNotificationFailure,
			"failed receipt allocation lost or called the original callback");
		test.api->DrainReferenceNotifications();
		Require(callbacks == 1 && !test.Record().referenceCompletion, "recovered receipt did not dispatch exactly once");
		test.api->DrainReferenceNotifications();
		Require(callbacks == 1, "completed callback was replayed");
	}

	static void ParentAndPartialBoundaries()
	{
		for (bool absent : { false, true }) {
			Case test;
			if (absent)
				test.api->requests.clear();
			else
				test.Record().state = "cancelled";
			test.api->TryFinalizeSequenceLocked(test.Sequence());
			Require(test.lease.expired() && test.api->manifestWorkerState->jobs.empty() &&
						test.api->CountPendingOperationsLocked() == 0,
				"retired parent retained storage or admitted work");
		}
		Case test;
		test.api->manifestWorkerState->stopRequested = true;
		failEvents = { "packaging.failed", {} };
		test.api->QueueSequenceManifestLocked(test.Sequence(), false);
		ClearFaults();
		Require(test.Record().state == "recording" && !test.Sequence().finalizing && !test.lease.expired() &&
					test.Sequence().manifestAdmissionFailure && test.Sequence().manifestReportingFailure,
			"partial failure lost its exception or retired an active sequence");
		test.api->TryFinalizeSequenceLocked(test.Sequence());
		Require(test.Record().state == "failed" && test.lease.expired() && test.api->CountPendingOperationsLocked() == 0,
			"partial reporting failure prevented subsequent finalization");
	}

	static void CallbackOrder()
	{
		Case test;
		test.Sequence().frameManifest = false;
		auto& second = test.api->requests["second"];
		second.requestId = "second";
		second.commandId = "second-command";
		second.kind = "sequence";
		second.state = "recording";
		auto& sequence = test.api->sequences["second"];
		sequence.requestId = "second";
		sequence.frameCount = 1;
		sequence.nextOrdinal = 2;
		sequence.frameManifest = false;
		sequence.packaging = { { "previewVideo", { { "state", "disabled" } } } };
		unsigned order = 0;
		test.Record().referenceCompletion = [&](const json&) { Require(++order == 1, "first callback reordered"); };
		second.referenceCompletion = [&](const json&) { Require(++order == 2, "second callback reordered"); };
		test.api->TryFinalizeSequenceLocked(test.Sequence());
		test.api->TryFinalizeSequenceLocked(sequence);
		failNextAllocation = true;
		test.api->DrainReferenceNotifications();
		ClearFaults();
		Require(order == 0 && test.Record().referenceCompletion && second.referenceCompletion,
			"a later callback overtook a failed receipt");
		test.api->DrainReferenceNotifications();
		Require(order == 2, "terminal callbacks were lost");
	}

	static void RunCase(const char* name, void (*test)())
	{
		currentCase = name;
		rejectedAllocationCount = 0;
		std::fprintf(stderr, "START %s\n", name);
		std::fflush(stderr);
		test();
		std::fprintf(stderr, "PASS %s\n", name);
	}

	static void Run()
	{
		CSX::Api::ServiceFoundation::SetEventAllocationTestHook(EventAllocation);
		RunCase("finalizing-journal", [] { RejectedFinalization("sequence.finalizing", {}, false, false); });
		RunCase("queued-journal", [] { RejectedFinalization("packaging.queued", {}, false, false); });
		RunCase("failed-journal", [] { RejectedFinalization("packaging.failed", {}, true, false); });
		RunCase("queued-and-failed-journal", [] { RejectedFinalization("packaging.queued", "packaging.failed", false, false); });
		RunCase("persistent-allocation", [] { RejectedFinalization("packaging.queued", {}, false, true); });
		RunCase("terminal-publication", TerminalPublication);
		RunCase("terminal-metadata", TerminalMetadataAllocation);
		RunCase("notification-and-success", NotificationAndSuccess);
		RunCase("parent-and-partial", ParentAndPartialBoundaries);
		RunCase("callback-order", CallbackOrder);
		CSX::Api::ServiceFoundation::SetEventAllocationTestHook(nullptr);
	}
};

int main()
{
	executableAddressMarker = reinterpret_cast<void*>(&main);
	std::set_terminate([] {
		ClearFaults();
		std::fprintf(stderr, "TERMINATE %s\n", currentCase);
		std::fprintf(stderr, "ADDRESS main=%p rejected=%zu\n", executableAddressMarker, rejectedAllocationCount);
		const auto first = rejectedAllocationCount > rejectedAllocations.size() ? rejectedAllocationCount - rejectedAllocations.size() : 0;
		for (auto index = first; index < rejectedAllocationCount; ++index) {
			const auto& allocation = rejectedAllocations[index % rejectedAllocations.size()];
			std::fprintf(stderr, "REJECTED %zu caller=%p bytes=%zu\n", index, allocation.caller, allocation.bytes);
			std::fprintf(stderr, "CAPTURE %zu status=%u frames=%u exception=%08lX\n", index,
				static_cast<unsigned>(allocation.captureStatus), static_cast<unsigned>(allocation.frameCount), allocation.captureException);
			for (USHORT frame = 0; frame < allocation.frameCount; ++frame)
				std::fprintf(stderr, "FRAME rejected=%zu index=%u address=%p\n", index,
					static_cast<unsigned>(frame), allocation.frames[frame]);
		}
		if (const auto failure = std::current_exception()) {
			try {
				std::rethrow_exception(failure);
			} catch (const std::exception& error) {
				std::fprintf(stderr, "%s\n", error.what());
			} catch (...) {
				std::fputs("non-standard active exception\n", stderr);
			}
		}
		std::fflush(stderr);
		std::abort();
	});
	try {
		ScreenshotFinalizationTest::Run();
		std::cout << "ScreenshotFinalization PASS\n";
		return 0;
	} catch (const std::exception& error) {
		ClearFaults();
		std::cerr << error.what() << '\n';
		return 1;
	}
}
