#include "Features/Upscaling/NeuralRendering/ExecutionEvidence.h"
#include "neural_execution_wait_scope.h"

#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
}

int main()
{
	using namespace NeuralRendering;
	static_assert(noexcept(std::declval<const ExecutionEvidence&>().Snapshot()));
	static_assert(std::is_nothrow_copy_constructible_v<ExecutionContext>);
	static_assert(std::is_nothrow_copy_assignable_v<ExecutionContext>);
	try {
		std::set<std::uint32_t> queries;
		for (std::uint32_t context = 0; context < 3; ++context) {
			Require(queries.insert(context * kExecutionTimestampQueriesPerContext).second, "batch begin overlaps");
			Require(queries.insert(context * kExecutionTimestampQueriesPerContext + 1u).second, "batch end overlaps");
			for (std::uint32_t region = 0; region < kMaximumExecutionRegions; ++region) {
				const auto query = ExecutionEvaluationQuery(context, region);
				Require(queries.insert(query).second && queries.insert(query + 1u).second, "evaluation timestamps overlap");
			}
		}
		Require(queries.size() == 30 && *queries.rbegin() == 29, "query heap bounds differ from used ranges");
		Require(ResolveExecutionGpuTiming(100, 1100, 1000000).microseconds == 1000, "timestamp units");
		Require(ResolveExecutionGpuTiming(100, 100, 1000000).microseconds == 0, "measured zero lost");
		Require(!ResolveExecutionGpuTiming(101, 100, 1000000).microseconds, "reversed timestamps became a number");
		Require(ResolveExecutionGpuTiming(0, 1, 0).state == ExecutionTimingState::Failed, "zero frequency accepted");
		Require(!ResolveExecutionGpuTiming(0, UINT64_MAX, 1).microseconds, "overflow wrapped a timestamp");

		ExecutionDescriptor first{};
		first.submissionId = NextExecutionSubmissionId();
		first.frame = 40;
		first.sourceWorldFrame = 39;
		first.generation = 7;
		first.context.renderingMode = RenderingMode::ReducedResolution;
		first.context.captureEpoch = 1;
		first.context.sourceTransactionId = NextExecutionSubmissionId();
		first.regionCount = 2;
		first.logicalEyeCount = 1;
		first.plannedPhysicalSlotMask = (1u << 0) | (1u << 4);
		first.regions[0].physicalSlot = 0;
		first.regions[1].physicalSlot = 4;
		auto pending = std::make_shared<ExecutionEvidence>(first);
		pending->Update([](auto& outcome) {
			outcome.batchGpu.state = ExecutionTimingState::Pending;
			outcome.regions[0].evaluationGpu.state = ExecutionTimingState::Pending;
			outcome.regions[1].evaluationGpu.state = ExecutionTimingState::Pending;
			outcome.attemptedPhysicalSlotMask = 1u;
			outcome.succeededPhysicalSlotMask = 1u;
		});
		const auto pinned = pending;
		auto next = first;
		next.submissionId = NextExecutionSubmissionId();
		next.context.captureEpoch = 2;
		next.generation = 8;
		pending = std::make_shared<ExecutionEvidence>(next);
		Require(pinned->Descriptor().submissionId != pending->Descriptor().submissionId, "backend/capture restart reused identity");
		Require(pinned->Descriptor().context.captureEpoch == 1, "pinned descriptor was overwritten");
		Require(pinned->Snapshot().attemptedPhysicalSlotMask != first.plannedPhysicalSlotMask, "planned regions became attempted work");
		Require(!pending->Snapshot().batchGpu.microseconds, "new transaction inherited old sample");
		std::thread completion([pinned] {
			pinned->Update([](auto& result) {
				result.batchGpu = ResolveExecutionGpuTiming(10, 2010, 1000000);
				result.regions[0].evaluationGpu = ResolveExecutionGpuTiming(10, 1010, 1000000);
			});
		});
		for (int i = 0; i < 32; ++i)
			Require(!pending->Snapshot().batchGpu.microseconds, "delayed timing joined current frame by latest identity");
		completion.join();
		// Collection resolves closed pairs, then fails any pair whose end was not recorded.
		pinned->FailPendingTimings();
		const auto completed = pinned->Snapshot();
		Require(completed.batchGpu.microseconds == 2000 && completed.regions[0].evaluationGpu.microseconds == 1000, "complete timing was erased");
		Require(completed.regions[1].evaluationGpu.state == ExecutionTimingState::Failed && !completed.regions[1].evaluationGpu.microseconds, "aborted region retained pending/zero timing");
		Require(completed.committedPhysicalSlotMask == 0, "inference success implied output publication");
		Require(!completed.cpuWaitMicroseconds && completed.cpuWaitCalls == 0, "unobserved CPU waits became measured zero");
		pinned->RecordCpuWait({ "BeginD3D12 backpressure", 37, 0, 0, 1000 });
		pinned->RecordCpuWait({ "WaitForIdle D3D12 fence", 250000, 258, 0, 250 });
		pinned->RecordCpuWait({ "WaitForIdle D3D11 acknowledgement", 2, UINT32_MAX, 5, 250 });
		const auto waits = pinned->Snapshot();
		Require(waits.cpuWaitMicroseconds == 250039 && waits.cpuWaitCalls == 3 && waits.cpuWaitSampleCount == 3,
			"actual CPU wait samples and totals disagree");
		Require(std::string_view(waits.cpuWaitSamples[0].operation) == "BeginD3D12 backpressure" &&
					waits.cpuWaitSamples[1].result == 258 && waits.cpuWaitSamples[1].timeoutMilliseconds == 250 &&
					waits.cpuWaitSamples[2].result == UINT32_MAX && waits.cpuWaitSamples[2].error == 5,
			"wait causes, timeout or failure result lost");
		for (int i = 0; i < 15; ++i)
			pinned->RecordCpuWait({ "WaitForIdle D3D12 fence", 1, 0, 0, 250 });
		const auto boundedWaits = pinned->Snapshot();
		Require(boundedWaits.cpuWaitCalls == 18 && boundedWaits.cpuWaitSampleCount == 16 && boundedWaits.cpuWaitSamplesDropped == 2 &&
					boundedWaits.cpuWaitMicroseconds == 250054,
			"bounded wait detail lost aggregate or hid dropped samples");
		auto rebuilding = std::make_shared<ExecutionEvidence>(next);
		std::shared_ptr<ExecutionEvidence> activeWait;
		{
			ExecutionWaitScope retirement(activeWait, rebuilding);
			Require(activeWait == rebuilding && rebuilding->Snapshot().cpuWaitMicroseconds == 0,
				"observed idle scope must initialize measured zero for the exact transaction");
			activeWait->RecordCpuWait({ "WaitForIdle D3D12 fence", 7, 0, 0, 250 });
		}
		Require(!activeWait, "retirement left the execution bound to unrelated waits");
		{
			ExecutionWaitScope shutdown(activeWait, rebuilding);
			Require(rebuilding->Snapshot().cpuWaitMicroseconds == 7, "shutdown erased the retirement wait");
			activeWait->RecordCpuWait({ "WaitForIdle D3D11 acknowledgement", 11, 0, 0, 250 });
		}
		{
			ExecutionWaitScope commandBegin(activeWait, rebuilding);
			Require(rebuilding->Snapshot().cpuWaitMicroseconds == 18 && rebuilding->Snapshot().cpuWaitCalls == 2,
				"nonblocking command begin erased or duplicated backend teardown waits");
		}
		{
			ExecutionWaitScope uncaptured(activeWait, {});
			Require(!activeWait, "uncaptured operation inherited the preceding execution");
		}
		Require(!activeWait && !pending->Snapshot().cpuWaitMicroseconds,
			"wait scope attributed work to another transaction");
		Require(!ExecutionContext{}.renderingMode && !ExecutionContext{}.captureEpoch, "missing caller facts inferred");
		pending->Update([](auto&) { throw std::runtime_error("evidence failure"); });
		Require(pending->Snapshot().evidenceFailed, "instrumentation exception disappeared");
		std::cout << "Execution evidence: correlation, timestamp layout, unavailable/failed states and retention passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
