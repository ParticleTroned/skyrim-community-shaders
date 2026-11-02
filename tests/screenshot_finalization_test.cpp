#include "BuildProvenance.h"
#include "Features/ScreenshotApi.h"
#include "Features/ScreenshotReferencePolicy.h"
#include "Utils/JsonCleanup.h"
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
#include <optional>
#include <stdexcept>
#include <utility>

namespace
{
	using json = nlohmann::json;
	thread_local bool failNextAllocation = false;
	thread_local bool failAllAllocations = false;
	thread_local std::array<std::string_view, 2> failEvents{};
	thread_local bool persistentEventFailure = false;
	thread_local unsigned eventFaults = 0;
	thread_local std::size_t allocationsUntilFailure = 0;
	thread_local bool countAllocations = false;
	thread_local std::size_t countedAllocations = 0;
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

	using StackCaptureRoutine = USHORT(WINAPI*)(DWORD, DWORD, PVOID*, PDWORD);

	void CaptureRejectedStack(RejectedAllocation& allocation, StackCaptureRoutine capture = &CaptureStackBackTrace) noexcept
	{
		if (capturingRejectedStack) {
			allocation.captureStatus = StackCaptureStatus::Reentrant;
			return;
		}
		capturingRejectedStack = true;
		__try {
			allocation.frameCount = capture(
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
		allocationsUntilFailure = 0;
		countAllocations = false;
	}
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

void* operator new(std::size_t size)
{
	if (countAllocations)
		++countedAllocations;
	if (allocationsUntilFailure && --allocationsUntilFailure == 0)
		failAllAllocations = true;
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

namespace
{
	constexpr DWORD kInducedCaptureException = 0xE0424346;
	unsigned inducedCaptureCalls = 0;

	USHORT WINAPI EmptyCapture(DWORD, DWORD, PVOID*, PDWORD)
	{
		++inducedCaptureCalls;
		return 0;
	}

	USHORT WINAPI InducedNativeCaptureException(DWORD, DWORD, PVOID*, PDWORD)
	{
		++inducedCaptureCalls;
		RaiseException(kInducedCaptureException, EXCEPTION_NONCONTINUABLE, 0, nullptr);
		return 0;
	}

	void RunCaptureControl(const char* name, StackCaptureRoutine capture, StackCaptureStatus expected, bool reentrant)
	{
		currentCase = name;
		RejectedAllocation sample{};
		inducedCaptureCalls = 0;
		capturingRejectedStack = reentrant;
		failAllAllocations = true;
		CaptureRejectedStack(sample, capture);
		const bool guardAfterCapture = capturingRejectedStack;
		const bool originalFaultStillArmed = failAllAllocations;
		const auto callsAfterCapture = inducedCaptureCalls;
		const auto rejectedBeforeThrow = rejectedAllocationCount;
		bool originalBadAllocObserved = false;
		void* unexpectedMemory = nullptr;
		try {
			unexpectedMemory = ::operator new(1);
		} catch (const std::bad_alloc&) {
			originalBadAllocObserved = true;
		}
		::operator delete(unexpectedMemory);
		capturingRejectedStack = false;
		ClearFaults();
		Require(sample.captureStatus == expected && sample.frameCount == 0,
			"induced capture control changed status or fabricated frames");
		Require(sample.captureException == (expected == StackCaptureStatus::NativeException ? kInducedCaptureException : 0),
			"induced capture exception identity changed");
		Require(callsAfterCapture == (reentrant ? 0u : 1u) && guardAfterCapture == reentrant,
			"capture refused incorrectly or failed to reset its owned guard");
		Require(originalFaultStillArmed && originalBadAllocObserved && rejectedAllocationCount > rejectedBeforeThrow,
			"capture control disarmed or bypassed the original bad_alloc");
		RejectedAllocation resetProbe{};
		CaptureRejectedStack(resetProbe, EmptyCapture);
		Require(resetProbe.captureStatus == StackCaptureStatus::Empty && resetProbe.frameCount == 0 &&
					!capturingRejectedStack && inducedCaptureCalls == callsAfterCapture + 1,
			"capture guard did not admit the next induced empty capture");
		std::fprintf(stderr, "CAPTURE-CONTROL %s PASS induced-status=%u subsequent-bad-alloc=1\n",
			name, static_cast<unsigned>(sample.captureStatus));
		std::fflush(stderr);
	}

	void RunCaptureControls()
	{
		RunCaptureControl("induced-reentrant", EmptyCapture, StackCaptureStatus::Reentrant, true);
		RunCaptureControl("induced-empty", EmptyCapture, StackCaptureStatus::Empty, false);
		RunCaptureControl("induced-native-exception", InducedNativeCaptureException, StackCaptureStatus::NativeException, false);
	}

	void RunJsonCleanupControl()
	{
		using json = nlohmann::json;
		json value = json::array({ nullptr, true, 7, "scalar", json::binary({ 1, 2, 3 }) });
		value.push_back(json::object());
		auto* nested = &value.back();
		for (unsigned depth = 0; depth < 256; ++depth) {
			(*nested)["child"] = json::array({ json::object() });
			nested = &(*nested)["child"].front();
		}
		(*nested)["leaf"] = "retained leaf";
		const auto before = rejectedAllocationCount;
		failAllAllocations = true;
		{
			const Util::JsonCleanupGuard cleanup(value);
		}
		Util::ClearJsonWithoutAllocation(value);
		const bool empty = value.empty();
		const bool faultStillArmed = failAllAllocations;
		const bool noAllocationAttempted = rejectedAllocationCount == before;
		ClearFaults();
		Require(empty && faultStillArmed && noAllocationAttempted,
			"JSON cleanup allocated, retained children or changed the original fault");
		std::fprintf(stderr, "JSON-CLEANUP persistent-fault mixed-depth256 PASS allocations=0\n");
	}

	void RunJsonCopyControl()
	{
		currentCase = "json-copy";
		json source = { { "mixed", json::array({ nullptr, true, 7, "scalar", json::binary({ 1, 2, 3 }) }) } };
		const Util::JsonCleanupGuard sourceCleanup(source);
		json destination;
		const Util::JsonCleanupGuard destinationCleanup(destination);
		countedAllocations = 0;
		countAllocations = true;
		Util::CopyJsonInto(destination, source);
		countAllocations = false;
		Require(destination == source, "guarded JSON copy changed a value or binary subtype");
		const auto allocations = countedAllocations;
		Require(allocations > 0 && allocations <= 4096, "guarded JSON copy sweep is missing or unbounded");
		for (std::size_t nth = 1; nth <= allocations; ++nth) {
			Util::CopyJsonInto(destination, "sentinel");
			allocationsUntilFailure = nth;
			bool caught = false;
			try {
				Util::CopyJsonInto(destination, source);
			} catch (const std::bad_alloc&) {
				caught = true;
			}
			const auto rejectedBeforeCheck = rejectedAllocationCount;
			Require(caught && failAllAllocations && allocationsUntilFailure == 0 && destination.is_string() &&
						destination.get_ref<const json::string_t&>() == "sentinel",
				"guarded partial JSON copy changed its destination or disarmed failure");
			Require(rejectedAllocationCount == rejectedBeforeCheck,
				"fault-armed JSON copy verification attempted an allocation");
			ClearFaults();
		}
		std::fprintf(stderr, "JSON-COPY persistent-nth=%zu PASS strong-destination=1\n", allocations);
	}

	void RunJsonContainerControl()
	{
		currentCase = "json-container";
		const json source = { { "nested", { { "value", "container allocation control" } } } };
		for (unsigned lane = 0; lane < 2; ++lane) {
			const auto populate = [&](json& destination) {
				if (lane == 0) {
					Util::PopulateJsonObject(destination, [&](json& object) {
						Util::CopyJsonInto(object["nested"], source);
					});
				} else {
					Util::AppendJsonCopy(destination, source);
				}
			};
			json complete;
			const Util::JsonCleanupGuard completeCleanup(complete);
			countedAllocations = 0;
			countAllocations = true;
			populate(complete);
			countAllocations = false;
			const auto allocations = countedAllocations;
			Require(allocations > 0 && allocations <= 4096, "container allocation sweep is missing or unbounded");
			for (std::size_t nth = 1; nth <= allocations; ++nth) {
				json destination;
				const Util::JsonCleanupGuard cleanup(destination);
				allocationsUntilFailure = nth;
				bool caught = false;
				try {
					populate(destination);
				} catch (const std::bad_alloc&) {
					caught = true;
				}
				Require(caught && failAllAllocations && allocationsUntilFailure == 0 && destination.is_null(),
					"container allocation failure corrupted its null destination");
				ClearFaults();
			}
			std::fprintf(stderr, "JSON-CONTAINER lane=%u persistent-nth=%zu PASS strong-destination=1\n", lane, allocations);
		}
	}
}

namespace logger
{
	template <class... Args>
	void error(Args&&...)
	{}
	template <class... Args>
	void warn(Args&&...)
	{}
}
namespace BuildProvenance
{
	// Inert environmental metadata; no issued artifact or worker success is inferred.
	nlohmann::json GetProducer()
	{
		nlohmann::json producer;
		const Util::JsonCleanupGuard cleanup(producer);
		producer["fixture"] = "finalization-input";
		return nlohmann::json(std::move(producer));
	}
}

// Only lifecycle setup is bound for a CPU fixture; all coordinator records and methods are production code.
ScreenshotApi::ScreenshotApi(FinalizationTestTag) :
	service({ "csx.screenshot", kContractMajor, kContractMinor, kSchemaRevision }),
	manifestWorkerState(std::make_shared<ManifestWorkerState>()),
	preparationWorkerState(std::make_shared<PreparationWorkerState>())
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
		std::optional<ScreenshotApi::ManifestResult> terminalInput;
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

	static void ConsumedManifestResult(bool persistent, bool success, bool inject = true)
	{
		Case test;
		test.api->TryFinalizeSequenceLocked(test.Sequence());
		test.api->manifestWorkerState->jobs.pop_front();
		// Controlled result delivery exercises the actual coordinator drain, not a worker/file claim.
		test.api->manifestWorkerState->results.push_back({ .requestId = "sequence", .generation = 1, .final = true, .success = success, .destination = test.Sequence().finalManifestPath, .error = "controlled worker failure" });
		failEvents = { inject ? (success ? "packaging.completed" : "packaging.failed") : std::string_view{}, {} };
		persistentEventFailure = persistent;
		eventFaults = 0;
		test.api->DrainWorkerResultsLocked();
		const bool faultStillArmed = !persistent || failAllAllocations;
		ClearFaults();
		Require(eventFaults == (inject ? 1u : 0u) && faultStillArmed && test.lease.expired() &&
					test.api->manifestWorkerState->outstanding == 0 && test.api->manifestWorkerState->results.empty() &&
					static_cast<bool>(test.Record().finalizationJournalFailure) == inject && test.api->CountPendingOperationsLocked() == 0,
			"consumed final result was lost after packaging publication failure");
		Require(test.Record().state == (success && !persistent ? "completed" : "failed"),
			"consumed final result produced an incorrect terminal outcome");
		if (persistent)
			Require(test.Record().finalizationFailure != nullptr, "persistent result failure lost its primary exception");
	}

	static void IndependentWorkerResults()
	{
		Case test;
		test.api->TryFinalizeSequenceLocked(test.Sequence());
		test.api->manifestWorkerState->jobs.pop_front();
		test.api->manifestWorkerState->results.push_back({ .requestId = "sequence", .generation = 1, .final = true, .success = false, .destination = test.Sequence().finalManifestPath, .error = "worker failure" });
		auto& parent = test.api->requests["preparing"];
		parent.requestId = "preparing";
		parent.kind = "sequence";
		parent.state = "preparing";
		auto& preparing = test.api->sequences["preparing"];
		preparing.requestId = "preparing";
		preparing.preparationPending = true;
		test.api->preparationWorkerState->preparationResults.push_back({ .requestId = "preparing", .error = "preparation failure" });
		test.api->preparationWorkerState->outstanding = 1;
		failEvents = { "sequence.preparation_failed", {} };
		eventFaults = 0;
		test.api->DrainWorkerResultsLocked();
		ClearFaults();
		Require(eventFaults == 1 && parent.state == "failed" && parent.finalizationFailure &&
					!preparing.preparationPending && test.Record().state == "failed" && test.lease.expired() &&
					test.api->manifestWorkerState->outstanding == 0 && test.api->preparationWorkerState->outstanding == 0,
			"a consumed preparation failure discarded another lane's final result");
	}

	static auto& AddChild(Case& test, const char* id)
	{
		auto& child = test.api->requests[id];
		child.requestId = id;
		child.commandId = id;
		child.parentRequestId = "sequence";
		child.kind = "sequence_frame";
		child.state = "acquiring";
		child.sequenceOrdinal = 1;
		test.Sequence().frameManifest = false;
		++test.Sequence().inFlight;
		test.Sequence().activeChildRequestId = id;
		return child;
	}

	static void SourceChildCompletion(std::string_view event, bool persistent)
	{
		Case test;
		auto& child = AddChild(test, "source-child");
		failEvents = { event, {} };
		persistentEventFailure = persistent;
		eventFaults = 0;
		test.api->OnSourceTerminal("source-child", "failed", "source_timeout");
		const bool faultStillArmed = !persistent || failAllAllocations;
		ClearFaults();
		Require(eventFaults == 1 && faultStillArmed && child.sequenceFinished && child.completionFailure &&
					test.Sequence().inFlight == 0 && test.Sequence().activeChildRequestId.empty() &&
					test.Sequence().failed == 1 && IsTerminal(test.Record().state) && test.lease.expired(),
			"source terminal reporting stranded parent accounting or storage");
		test.api->OnSourceTerminal("source-child", "failed", "source_timeout");
		Require(test.Sequence().failed == 1 && test.Sequence().inFlight == 0, "source callback accounted twice");
	}

	static void ArtifactChildCompletion(std::string_view event, bool persistent)
	{
		Case test;
		auto& child = AddChild(test, "artifact-child");
		const auto path = test.Sequence().directoryLease->Path() / "controlled-output.bin";
		const auto committed = CSX::ScreenshotStorage::CommittedFile::WriteAtomically(
			test.Sequence().directoryLease->Path() / "controlled-output.tmp", path, "abc", 3, false);
		json actual = { { "view", "fixture" }, { "width", 1u }, { "height", 1u }, { "format", "fixture" }, { "colourContract", "fixture" } };
		failEvents = { event, {} };
		persistentEventFailure = persistent;
		eventFaults = 0;
		test.api->OnArtifactTerminal("artifact-child", true, path, {}, std::move(actual), committed);
		const bool faultStillArmed = !persistent || failAllAllocations;
		ClearFaults();
		Require(eventFaults == 1 && faultStillArmed && child.sequenceFinished && child.completionFailure &&
					child.terminalArtifacts == 1 && child.successfulArtifacts == 1 &&
					test.Sequence().written == 1 && test.Sequence().inFlight == 0 &&
					IsTerminal(test.Record().state) && test.lease.expired(),
			"committed artifact callback lost terminal accounting after publication failure");
		test.api->OnArtifactTerminal("artifact-child", false, {}, "duplicate", nullptr);
		Require(test.Sequence().written == 1 && child.terminalArtifacts == 1, "artifact callback accounted twice");
	}

	static void QueuedChildCancellation()
	{
		Case test;
		auto& first = AddChild(test, "queued-one");
		auto& second = AddChild(test, "queued-two");
		test.api->manualDispatchQueue.push_back({ .requestId = "queued-one" });
		test.api->sequenceDispatchQueue.push_back({ .requestId = "queued-two" });
		failEvents = { "request.terminal", {} };
		persistentEventFailure = true;
		eventFaults = 0;
		test.api->CancelQueuedDispatchesLocked("cancelled", "fixture cancellation");
		const bool faultStillArmed = failAllAllocations;
		ClearFaults();
		Require(eventFaults == 1 && faultStillArmed && first.sequenceFinished && second.sequenceFinished &&
					test.Sequence().inFlight == 0 && test.lease.expired() && IsTerminal(test.Record().state) &&
					test.api->manualDispatchQueue.empty() && test.api->sequenceDispatchQueue.empty(),
			"cancellation reporting consumed remaining queued children without accounting");
	}

	static void MultipleArtifactCompletion()
	{
		Case test;
		auto& child = AddChild(test, "two-outputs");
		child.expectedArtifacts = 2;
		const auto directory = test.Sequence().directoryLease->Path();
		for (unsigned i = 0; i < 2; ++i) {
			const auto path = directory / (std::to_string(i) + ".bin");
			const auto committed = CSX::ScreenshotStorage::CommittedFile::WriteAtomically(
				directory / (std::to_string(i) + ".tmp"), path, "abc", 3, false);
			json actual = { { "view", "fixture" }, { "width", 1u }, { "height", 1u }, { "format", "fixture" }, { "colourContract", "fixture" } };
			if (i == 0)
				failEvents = { "artifact.written", {} };
			test.api->OnArtifactTerminal("two-outputs", true, path, {}, std::move(actual), committed);
			ClearFaults();
			Require(child.terminalArtifacts == i + 1 && child.successfulArtifacts == i + 1,
				"artifact reporting failure lost a consumed output");
			if (i == 0)
				Require(!IsTerminal(child.state) && !child.sequenceFinished && test.Sequence().inFlight == 1,
					"first output reporting failure prematurely consumed the second callback");
		}
		Require(child.sequenceFinished && child.completionFailure && child.state == "failed_partial" &&
					test.Sequence().inFlight == 0 && test.Sequence().written == 1 && test.lease.expired(),
			"multiple-output reporting failure did not reach a single terminal disposition");
	}

	static void NotificationRetention()
	{
		for (unsigned pressure = 0; pressure < 3; ++pressure) {
			Case test;
			test.Sequence().frameManifest = false;
			unsigned callbacks = 0;
			test.Record().referenceCompletion = [&](const json&) { ++callbacks; };
			test.api->requestOrder.push_back("sequence");
			test.api->TryFinalizeSequenceLocked(test.Sequence());
			if (pressure == 0)
				test.Record().acknowledged = true;
			else if (pressure == 1)
				test.Record().terminalAt -= ScreenshotApi::kRetention;
			else {
				for (std::size_t i = 0; i < ScreenshotApi::kMaximumRequests; ++i) {
					const auto id = "active-" + std::to_string(i);
					auto& active = test.api->requests[id];
					active.requestId = id;
					active.state = "acquiring";
					test.api->requestOrder.push_back(id);
				}
			}
			failNextAllocation = true;
			test.api->DrainReferenceNotifications();
			ClearFaults();
			test.api->TrimLocked();
			Require(test.api->requests.contains("sequence") && callbacks == 0 && test.Record().referenceCompletion,
				"retention evicted an acknowledged/expired/capacity-selected pending callback");
			test.api->DrainReferenceNotifications();
			test.api->DrainReferenceNotifications();
			Require(callbacks == 1 && !test.Record().referenceCompletion, "retained callback was lost or duplicated");
			test.api->TrimLocked();
			Require(!test.api->requests.contains("sequence"), "delivered callback prevented ordinary terminal eviction");
		}
	}

	static void PrepareConstruction(Case& test, unsigned lane, unsigned& callbacks)
	{
		test.Record().referenceCompletion = [&](const json&) { ++callbacks; };
		if (lane == 1)
			test.terminalInput.emplace(ScreenshotApi::ManifestResult{ .success = false, .error = "worker failure" });
		test.Sequence().packaging["nested"] = {
			{ "objects", json::array({ { { "first", "retained" } }, { { "last", "retained" } } }) }
		};
		if (lane >= 2) {
			test.Sequence().frameManifest = false;
			test.api->TryFinalizeSequenceLocked(test.Sequence());
			test.Record().finalizationFailure = std::make_exception_ptr(std::runtime_error("retained construction failure"));
		}
	}

	static void Construct(Case& test, unsigned lane)
	{
		if (lane == 0) {
			test.Sequence().finalizing = true;
			test.api->QueueSequenceManifestLocked(test.Sequence(), true);
		} else if (lane == 1) {
			test.api->FinalizeSequenceLocked(test.Sequence(), &*test.terminalInput);
		} else if (lane == 2) {
			auto receipt = test.api->MakeReceipt(test.Record());
			const Util::JsonCleanupGuard cleanup(receipt);
		} else {
			test.api->DrainReferenceNotifications();
		}
	}

	static void PersistentConstruction(unsigned lane)
	{
		// Warm lazy runtime state before measuring the exact operation's allocations.
		for (unsigned pass = 0; pass < 2; ++pass) {
			Case test;
			unsigned callbacks = 0;
			PrepareConstruction(test, lane, callbacks);
			countedAllocations = 0;
			countAllocations = pass == 1;
			Construct(test, lane);
			countAllocations = false;
		}
		const auto allocations = countedAllocations;
		Require(allocations > 0 && allocations <= 4096, "construction allocation sweep is missing or unbounded");
		for (std::size_t nth = 1; nth <= allocations; ++nth) {
			Case test;
			unsigned callbacks = 0;
			PrepareConstruction(test, lane, callbacks);
			allocationsUntilFailure = nth;
			bool caught = false;
			try {
				Construct(test, lane);
			} catch (const std::bad_alloc&) {
				caught = true;
			}
			Require(failAllAllocations && allocationsUntilFailure == 0,
				"nth allocation was not reached with persistent failure armed");
			Require(test.lease.expired() && test.api->CountPendingOperationsLocked() == 0 &&
						test.api->manifestWorkerState->jobs.empty() && test.api->manifestWorkerState->outstanding == 0,
				"persistent construction failure retained storage, a job or pending capacity");
			Require(test.Record().referenceCompletion && callbacks == 0,
				"persistent construction failure transferred or invoked the callback");
			if (lane < 2) {
				Require(!caught && test.Record().state == "failed" && test.Record().finalizationFailure &&
							test.Sequence().manifestGeneration == 0 && test.Sequence().finalManifestGeneration == 0 &&
							test.Record().eventIndex == 0,
					"construction unwind failed terminal commit or changed rejected identities");
			} else {
				Require(test.Record().state == "completed" && test.Record().eventIndex == 2,
					"receipt construction changed the committed terminal identities");
			}
			ClearFaults();
			const auto expectedEvent = lane < 2 ? 0u : 2u;
			Require(test.api->service.JournalStatus()["latestEventId"] == expectedEvent,
				"construction failure consumed a journal identity");
			test.api->DrainReferenceNotifications();
			test.api->DrainReferenceNotifications();
			Require(callbacks == 1 && !test.Record().referenceCompletion,
				"construction recovery lost or duplicated the retained callback");
		}
		std::fprintf(stderr, "CONSTRUCTION lane=%u persistent-nth=%zu PASS\n", lane, allocations);
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
		RunCase("consumed-final-failed-journal", [] { ConsumedManifestResult(false, false); });
		RunCase("consumed-final-positive", [] { ConsumedManifestResult(false, true, false); });
		RunCase("consumed-final-completed-journal", [] { ConsumedManifestResult(false, true); });
		RunCase("consumed-final-persistent", [] { ConsumedManifestResult(true, false); });
		RunCase("independent-worker-results", IndependentWorkerResults);
		RunCase("source-child-terminal-journal", [] { SourceChildCompletion("request.terminal", false); });
		RunCase("source-child-timeout-journal", [] { SourceChildCompletion("source.timeout", false); });
		RunCase("source-child-persistent", [] { SourceChildCompletion("request.terminal", true); });
		RunCase("artifact-child-terminal-journal", [] { ArtifactChildCompletion("request.terminal", false); });
		RunCase("artifact-child-written-journal", [] { ArtifactChildCompletion("artifact.written", false); });
		RunCase("artifact-child-persistent", [] { ArtifactChildCompletion("request.terminal", true); });
		RunCase("queued-child-cancellation", QueuedChildCancellation);
		RunCase("multiple-artifact-completion", MultipleArtifactCompletion);
		RunCase("notification-retention", NotificationRetention);
		RunCase("construction-queue", [] { PersistentConstruction(0); });
		RunCase("construction-terminal", [] { PersistentConstruction(1); });
		RunCase("construction-receipt", [] { PersistentConstruction(2); });
		RunCase("construction-notification", [] { PersistentConstruction(3); });
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
		RunCaptureControls();
		RunJsonCleanupControl();
		RunJsonCopyControl();
		RunJsonContainerControl();
		ScreenshotFinalizationTest::Run();
		std::cout << "ScreenshotFinalization PASS\n";
		return 0;
	} catch (const std::exception& error) {
		ClearFaults();
		std::cerr << error.what() << '\n';
		return 1;
	}
}
