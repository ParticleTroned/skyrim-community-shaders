#pragma once

#include "RegionCapacity.h"
#include "SubmissionFenceSnapshot.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "ComputeSubrect.h"

#	include <array>
#	include <cstddef>
#	include <cstdint>
#	include <optional>
#	include <vector>

namespace NeuralRendering
{
	struct LifetimeRegion
	{
		std::uint32_t slot = 0, previousFrame = UINT32_MAX, previousSourceFrame = UINT32_MAX;
		std::uint64_t resourceSerial = 0, previousResourceSerial = 0;
		std::uint64_t regionIdentity = 0, previousRegionIdentity = 0;
		std::array<std::uint64_t, 5> resources11{}, resources12{};
		std::array<std::uint32_t, 2> colorSize{}, guideSize{}, outputSize{};
		ComputeSubrect context{}, previousContext{};
		bool previousHistoryValid = false, resourcesRebuilt = false, effectiveReset = false;
		bool createAttempted = false, createSucceeded = false, evaluateAttempted = false, evaluateSucceeded = false;
	};

	enum class LifetimeOperation : std::uint32_t
	{
		Batch,
		BackendRetirement,
		SlotRetirement
	};

	/** The frame fields identify the last renderer request for retirement-only records. */
	struct LifetimeRecord
	{
		std::uint64_t sequence = 0, backendSerial = 0, sourceTransactionId = 0, generation = 0;
		LifetimeOperation operation = LifetimeOperation::Batch;
		std::uint32_t requestFrame = UINT32_MAX, sourceFrame = UINT32_MAX;
		std::optional<std::uint32_t> mode;
		std::uint32_t insertion = 0, stage = 0, slotMask = 0, regionCount = 0;
		std::int32_t result = 0;
		bool succeeded = false, failureObserved = false;
		LifetimeFenceSnapshot before{}, after{};
		std::array<LifetimeRegion, kMaximumRegionEvaluations> regions{};
	};

	struct LifetimeSnapshot
	{
		std::vector<LifetimeRecord> records;
		std::optional<LifetimeRecord> firstFailure;
		std::uint64_t totalRecords = 0, overwrittenRecords = 0, diagnosticFailures = 0;
	};

	/** Renderer-owned fixed storage; a failure freezes the diagnostic window across resets. */
	class LifetimeDiagnostics
	{
	public:
		static constexpr std::size_t kCapacity = 64;
		[[nodiscard]] bool Frozen() const noexcept { return firstFailure_.has_value(); }
		void Record(LifetimeRecord record) noexcept
		{
			if (Frozen())
				return;
			record.sequence = ++totalRecords_;
			records_[(totalRecords_ - 1u) % kCapacity] = record;
			if (record.failureObserved)
				firstFailure_ = record;
		}
		void DiagnosticFailure() noexcept { ++diagnosticFailures_; }
		[[nodiscard]] LifetimeSnapshot Snapshot() const
		{
			LifetimeSnapshot result;
			result.totalRecords = totalRecords_;
			result.overwrittenRecords = totalRecords_ > kCapacity ? totalRecords_ - kCapacity : 0;
			result.diagnosticFailures = diagnosticFailures_;
			result.firstFailure = firstFailure_;
			for (auto sequence = result.overwrittenRecords + 1u; sequence <= totalRecords_; ++sequence)
				result.records.push_back(records_[(sequence - 1u) % kCapacity]);
			return result;
		}

	private:
		std::array<LifetimeRecord, kCapacity> records_{};
		std::optional<LifetimeRecord> firstFailure_;
		std::uint64_t totalRecords_ = 0, diagnosticFailures_ = 0;
	};
}

#endif
