#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/ISLDiagnostics.h"
#	include "Features/LightLimitFix/ShadowCullingPolicy.h"
#	include <array>
#	include <atomic>
#	include <mutex>
#	include <nlohmann/json.hpp>
#	include <vector>

namespace CSX::Diagnostics::ISL
{
	using json = nlohmann::json;
	inline constexpr size_t ThreadCapacity = 32, UniqueCapacity = 2048;
	inline constexpr uint64_t UnknownFrame = ~uint64_t{};
	struct Row
	{
		int64_t enter{}, exit{};
		uint64_t generation{}, frame = UnknownFrame;
		uintptr_t caller{};
		uint32_t thread{};
		Metric metric{};
	};
	struct Totals
	{
		uint64_t count{}, timed{};
		int64_t elapsed{}, maximum{};
	};
	struct CallerCount
	{
		uintptr_t address{};
		uint64_t count{};
	};
	struct Invocation
	{
		uint64_t generation{};
		int64_t enter{};
		uint32_t thread{};
		bool active{}, unstable{};
	};
	struct Slot
	{
		std::atomic_flag busy{};
		std::atomic_uint32_t thread{};
		unsigned depth{}, invocationDepth{};
		std::atomic_uint64_t invocationSequence{}, invocationGeneration{};
		std::atomic<int64_t> invocationEnter{};
		std::atomic_bool invocationActive{};
		std::array<Totals, size_t(Metric::Count)> totals{};
		std::array<uintptr_t, UniqueCapacity> unique{};
		std::array<CallerCount, 16> callers{};
		uint64_t callerOverflow{};
		std::vector<Row> rows;
		uint64_t overflow{}, uniqueOverflow{}, checksum{}, candidateCount{}, overlap{}, uniqueCount{};
	};
	struct DiagnosticState
	{
		std::atomic<uint64_t> effective{ Policy{}.Pack() }, requested{ Policy{}.Pack() }, appliedFrame{}, appliedQpc{};
		std::atomic_bool sessionActive{}, admitting{}, contaminated{}, expired{};
		std::atomic<int64_t> expiresAt{};
		std::atomic_uint64_t threadLoss{}, contention{};
		std::array<Slot, ThreadCapacity> slots;
		std::array<Invocation, ThreadCapacity> entryInvocations{};
		uint64_t capture{}, captureGeneration{};
		int64_t start{}, stop{}, freeze{}, frequency{};
		bool frozen{}, acknowledged = true, suppressed{}, startupLoaded{};
		size_t contiguous{}, capacity{};
		std::vector<Row> frozenRows;
		json frozenHeader, captureCondition, startupReceipt = { { "active", "normal" }, { "requestedNextLaunch", nullptr } };
		json originalShadow, ownedShadow;
		bool originalEnabled{}, ownedEnabled{}, settingsOwned{}, restorationConflict{};
		bool capturedEnabled{}, capturedNR{}, capturedBatch{}, capturedDetection{};
		LightLimitFixDetail::ShadowCulling::Settings capturedShadow;
		std::mutex pruneMutex;
		PruneDelay prune;
		std::atomic_uint32_t pruneArm{};
		std::atomic_bool pruneActive{};
		std::atomic_uint64_t captureEpoch{};
	};
	DiagnosticState& Data();
	Policy CurrentPolicy() noexcept;
	json PolicyJson(Policy policy);
	json ConditionJson();
	json StartupStage(const json& args);
	json Export(const json& args);
	json Configure(const json& args);
	json RestorationJson();
	void StageDefaultPolicy();
	void TouchSession();
	const char* MetricName(Metric metric);
	Invocation InspectInvocation(const Slot& slot) noexcept;
}
#endif
