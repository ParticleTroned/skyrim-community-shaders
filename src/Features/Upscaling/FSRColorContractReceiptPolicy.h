#pragma once

#include "FSRColorContractPolicy.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

namespace FSRColorContractReceiptPolicy
{
	template <class Snapshot>
	struct SetReceipt
	{
		bool accepted = false;
		std::uint64_t resultingRevision = 0;
		Snapshot status{};
	};

	template <class Snapshot, class Mutex, class ClearDispatch, class BuildSnapshot>
	[[nodiscard]] SetReceipt<Snapshot> ApplySet(
		Mutex& a_mutex,
		std::atomic<std::uint64_t>& a_requestState,
		std::uint64_t a_expectedRevision,
		bool a_highDynamicRangeInput,
		bool a_autoExposure,
		ClearDispatch&& a_clearDispatch,
		BuildSnapshot&& a_buildSnapshot)
	{
		const std::lock_guard lock(a_mutex);
		const auto update = FSRColorContractPolicy::PlanUpdate(
			a_requestState.load(std::memory_order_acquire),
			a_expectedRevision,
			a_highDynamicRangeInput,
			a_autoExposure);
		if (update.revisionMatched && update.changed) {
			std::forward<ClearDispatch>(a_clearDispatch)();
			a_requestState.store(update.desiredState, std::memory_order_release);
		}
		return {
			update.revisionMatched,
			update.resultingRevision,
			std::forward<BuildSnapshot>(a_buildSnapshot)(),
		};
	}
}
