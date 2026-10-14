#include "Features/TextureStreaming/Categories.h"
#include "Features/TextureStreaming/ConsumerInventory.h"
#include "Features/TextureStreaming/Diagnostics.h"
#include "Features/TextureStreaming/Maintenance.h"
#include "Features/TextureStreaming/Settings.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	namespace Policy = TextureStreamingPolicy;
	using Block = StreamingTextures::Diagnostics::Block;
	using MaintenancePhase = StreamingTextures::MaintenancePhase;
	struct FakeClock
	{
		using time_point = std::chrono::steady_clock::time_point;
		inline static time_point current{};
		inline static std::chrono::microseconds releaseCost{};
		static time_point now() { return current; }
		static void Advance(std::chrono::microseconds amount) { current += amount; }
	};
	using MaintenanceBudget = StreamingTextures::MaintenanceBudget<FakeClock>;
	std::uint64_t GetTickCount64() { return 10000; }
	unsigned referenceReleases = 0, geometryDereferences = 0, ownerDepth = 0;
	bool requireOwner = false;
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	struct Geometry
	{
		~Geometry()
		{
			++referenceReleases;
			FakeClock::Advance(FakeClock::releaseCost);
			if (requireOwner && !ownerDepth)
				std::terminate();
		}
	};
	namespace PreviewState
	{
		inline std::vector<std::shared_ptr<Geometry>> roots;
		inline bool active = false, complete = true;
		inline unsigned snapshots = 0;
	}
	struct Engine
	{
		void* texture = nullptr;
		void* resourceView = nullptr;
		void* UAV = nullptr;
	};
	struct Source
	{
		Engine* rendererTexture = nullptr;
	};
	struct GlobalState
	{
		unsigned frameCount = 0;
		std::atomic_uint worldLoadTransitionStartFrame = 0;
		bool loading = false;
		bool IsSaveLoadSafeModeActive() const { return loading; }
	};
	namespace globals
	{
		namespace d3d
		{
			void* device = nullptr;
			void* context = nullptr;
		}
		namespace game
		{
			void* renderer = nullptr;
			void* shadowState = nullptr;
		}
		namespace features
		{
			struct Upscaling
			{
				bool transition = false;
				bool IsTextureStreamingTransitionActive() const { return transition; }
			} upscaling;
		}
		GlobalState* state = nullptr;
	}
	namespace logger
	{
		template <class... Args>
		void info(const char*, Args&&...)
		{}
	}
	namespace Util
	{
		int GetRendererContextLock(void*, void*) { return 1; }
		struct RendererOwnership
		{
			explicit RendererOwnership(int) { ++ownerDepth; }
			~RendererOwnership() { --ownerDepth; }
			explicit operator bool() const { return true; }
		};
	}
	struct Memory
	{
		struct Snapshot
		{
			struct Local
			{
				std::uint64_t Budget = 1024 * Policy::MiB, CurrentUsage = 0;
			} local;
			std::uint64_t generation = 1, sampledAtMs = 10000, pendingBytes = 0;
			bool priorityWork = false;
			bool Fresh(std::uint64_t) const { return true; }
		};
		static Memory& Get()
		{
			static Memory value;
			return value;
		}
		inline static std::uint64_t sampleGeneration = 1;
		inline static unsigned samples = 0;
		Snapshot SampleStreaming(void*)
		{
			++samples;
			Snapshot snapshot;
			snapshot.generation = sampleGeneration;
			return snapshot;
		}
		static std::uint64_t Add(std::uint64_t a, std::uint64_t b) { return a + b; }
	};
	std::atomic_bool originHookReady = true;
	struct TextureStreaming
	{
		struct State
		{
			struct Consumer
			{
				std::shared_ptr<Geometry> geometry;
			};
			struct Record
			{
				std::shared_ptr<Source> source;
				Engine* engine = nullptr;
				void* current = nullptr;
				void* currentView = nullptr;
				StreamingTextures::ConsumerInventory<Consumer> consumers;
				struct Full
				{
					unsigned Width = 4096, Height = 4096, MipLevels = 13;
				} full;
				unsigned drop = 0, distanceDrop = 0;
				double restorationUrgency = 0;
				bool protectedConsumer = false, previewProtected = false;
			};
			struct Node
			{
				std::shared_ptr<Geometry> object;
			};
			struct Transaction
			{
				struct Payload
				{
					struct Request
					{
						std::uint64_t serial = 0, epoch = 0;
						unsigned drop = 0;
					} request;
				} payload;
				bool published = false;
			};
			struct Reader
			{
				unsigned clears = 0;
				void ClearCache(bool = false) { ++clears; }
			} reader;
			std::uint64_t inventoryGeneration = 1, inventoryRetirementRemaining = 0, inventoryRetirementCursor = 0;
			bool inventoryRetirementPending = false;
			std::uint64_t epoch = 1, scan = 1, completeScan = 1, nextScanMs = 0, deviceGeneration = 1;
			unsigned worldStart = 0, lastFrame = UINT32_MAX;
			bool enabled = true, reductionRequested = true, paused = false, reading = false, hasPendingLoad = false;
			std::unordered_map<std::uint64_t, Record> records;
			std::unordered_set<std::uint64_t> reducedRecords;
			std::uint64_t pruneCursor = 0;
			std::vector<Node> frontier;
			std::unordered_set<Geometry*> visited;
			std::optional<Transaction> transaction;
			Memory::Snapshot memory;
			Policy::Pressure pressure;
			StreamingTextures::Options options;
			StreamingTextures::Categories categories;
			StreamingTextures::Diagnostics diagnostics;
			unsigned maximumDrop = 3, demand = 0;
			bool previewActive = false, firstPressureReductionPending = false;
			std::string detail;
			unsigned scans = 0, services = 0, selections = 0, cancellations = 0;
			unsigned pressureUpdates = 0, previewSuspensions = 0, approvedTransactions = 0, acceptedCandidates = 0;
			bool lastPreviewOnly = false;
			std::vector<std::shared_ptr<Geometry>> previewRoots, previewFrontier;
			std::unordered_set<Geometry*> previewVisited;
			std::uint64_t nextPreviewScanMs = 0;
			StreamingTextures::MaintenanceSchedule maintenanceSchedule;
			std::array<std::chrono::microseconds, StreamingTextures::MaintenanceSchedule::phaseCount> phaseCosts{};
			std::vector<MaintenancePhase> phases;
			const MaintenanceBudget* phaseBudget = nullptr;
			void EnterPhase(MaintenancePhase phase, const MaintenanceBudget& budget)
			{
				Require(budget.Available(), "An optional maintenance phase entered after the shared deadline");
				Require(!phaseBudget || phaseBudget == &budget, "Maintenance phases received different budgets within a frame");
				phaseBudget = &budget;
				phases.push_back(phase);
				FakeClock::Advance(phaseCosts[static_cast<std::size_t>(phase)]);
			}
			void InvalidateInventory();
			void MarkInventoryRetired(Record&);
			void RetireInventory(const MaintenanceBudget& budget);
			void Prune(const MaintenanceBudget& budget);
			std::uint32_t Demand(Record&);
			void Tick();
			void Defer(Block reason, const Record&) { diagnostics.Defer(reason); }
			bool ReductionRequested() const { return enabled && reductionRequested; }
#include "streaming_target_policy_under_test.h"
			void ValidateTransaction();
			void EvaluateCandidate(Record&, std::uint32_t);
			void AdoptLoads(const MaintenanceBudget& budget) { EnterPhase(MaintenancePhase::AdoptLoads, budget); }
			void RefreshPreviews();
			bool PreviewProtectionPending() const;
			void SuspendPreviewReductions(const char*)
			{
				++previewSuspensions;
				enabled = false;
			}
			void ScanPreviews(const MaintenanceBudget& budget) { EnterPhase(MaintenancePhase::PreviewScan, budget); }
			void Scan(const MaintenanceBudget& budget);
			void Select(const MaintenanceBudget& budget, bool previewOnly = false)
			{
				EnterPhase(MaintenancePhase::Select, budget);
				lastPreviewOnly = previewOnly;
				++selections;
			}
			void UpdatePressureDiagnostics(std::uint64_t) { ++pressureUpdates; }
			void CancelTransaction()
			{
				if (transaction && !transaction->published) {
					++cancellations;
					transaction.reset();
				}
			}
			void ServiceTransaction()
			{
				++services;
				if (transaction && transaction->payload.request.epoch != epoch)
					CancelTransaction();
			}
		};
	};
}

namespace StreamingTextures
{
	namespace Preview
	{
		struct Snapshot
		{
			bool complete;
			std::vector<std::shared_ptr<Geometry>> roots;
		};
		Snapshot Roots()
		{
			++PreviewState::snapshots;
			return { PreviewState::complete, PreviewState::roots };
		}
		bool Active() { return PreviewState::active; }
	}
	struct Origin
	{
		bool protectedConsumer = false;
		unsigned requiredCategories = 0;
	};
	bool ReadOrigin(void*, Origin&) { return true; }
	const void* StaticMaterial(Geometry*, std::uint32_t*)
	{
		++geometryDereferences;
		return reinterpret_cast<void*>(1);
	}
	bool IsStreamingSlot(const void*, Source*) { return true; }
	double RequiredEdge(Geometry*, unsigned)
	{
		++geometryDereferences;
		return 128;
	}
	namespace LoadHooks
	{
		bool HasPending() { return false; }
	}
}

namespace
{
#include "streaming_lifecycle_demand_under_test.h"
#include "streaming_preview_state_under_test.h"
#include "streaming_prune_under_test.h"
#include "streaming_retirement_under_test.h"
#include "streaming_scan_gate_under_test.h"
#include "streaming_tick_under_test.h"
	void TextureStreaming::State::ValidateTransaction()
	{
		if (!transaction)
			return;
		auto& work = *transaction;
#include "streaming_transaction_validation_under_test.h"
		++approvedTransactions;
	}
	void TextureStreaming::State::EvaluateCandidate(Record& record, std::uint32_t target)
	{
#include "streaming_selection_guard_under_test.h"
		++acceptedCandidates;
	}

	using State = TextureStreaming::State;
	struct Fixture
	{
		State state;
		Engine engine{ this, this };
		GlobalState global;
		Fixture()
		{
			globals::state = &global;
			globals::d3d::device = globals::d3d::context = globals::game::renderer = globals::game::shadowState = this;
			globals::features::upscaling.transition = false;
			Memory::sampleGeneration = 1;
			Memory::samples = 0;
			PreviewState::roots.clear();
			PreviewState::active = false;
			PreviewState::complete = true;
			PreviewState::snapshots = 0;
			FakeClock::current = {};
			FakeClock::releaseCost = {};
		}
		~Fixture()
		{
			requireOwner = false;
			FakeClock::releaseCost = {};
			PreviewState::roots.clear();
			globals::state = nullptr;
		}
		State::Record& Add(std::uint64_t id, unsigned committed = 1, unsigned scanning = 1, unsigned drop = 0)
		{
			auto& record = state.records[id];
			record.source = std::make_shared<Source>(Source{ &engine });
			record.engine = &engine;
			record.current = engine.texture;
			record.currentView = engine.resourceView;
			record.drop = drop;
			Require(record.consumers.Observe(state.scan, 0, state.inventoryGeneration), "Fixture inventory did not initialize");
			for (unsigned i = 0; i < committed; ++i)
				record.consumers.committed.push_back({ std::make_shared<Geometry>() });
			for (unsigned i = 0; i < scanning; ++i)
				record.consumers.scanning.push_back({ std::make_shared<Geometry>() });
			record.consumers.verified = state.completeScan;
			if (drop)
				state.reducedRecords.insert(id);
			return record;
		}
		void Retire()
		{
			const auto before = referenceReleases;
			requireOwner = true;
			{
				Util::RendererOwnership owner(1);
				state.RetireInventory(MaintenanceBudget{});
			}
			requireOwner = false;
			Require(referenceReleases - before <= maximumInventoryRetirementSteps, "A retirement pass exceeded its reference-release budget");
		}
		void Tick(bool ordinaryReset = true)
		{
			++global.frameCount;
			state.phaseBudget = nullptr;
			state.phases.clear();
			const auto before = referenceReleases;
			requireOwner = true;
			state.Tick();
			requireOwner = false;
			Require(!ordinaryReset || referenceReleases - before <= maximumInventoryRetirementSteps, "Tick exceeded its inventory retirement budget");
		}
	};
	void CheckImmediateInvalidation()
	{
		Fixture f;
		auto& record = f.Add(1, 256, 256, 1);
		for (unsigned i = 2; i <= 16; ++i)
			f.Add(i, 256, 256, 1);
		f.state.frontier.push_back({ std::make_shared<Geometry>() });
		f.state.visited.insert(f.state.frontier.back().object.get());
		Require(f.state.Demand(record) > 0, "Current complete inventory did not authorize distance reduction");
		const auto before = referenceReleases;
		const auto generation = f.state.inventoryGeneration;
		f.state.InvalidateInventory();
		Require(referenceReleases == before && f.state.inventoryGeneration != generation && f.state.inventoryRetirementPending && f.state.inventoryRetirementRemaining == f.state.records.size() && !f.state.completeScan,
			"Invalidation released references or failed to revoke the completed inventory immediately");
		f.state.completeScan = record.consumers.verified;
		geometryDereferences = 0;
		Require(f.state.Demand(record) == 0 && geometryDereferences == 0,
			"Stale inventory authorized reduction or dereferenced a geometry");
		f.state.completeScan = 0;
		f.Retire();
		Require(f.state.inventoryRetirementPending, "A large inventory bypassed incremental retirement");
		for (unsigned i = 0; f.state.inventoryRetirementPending && i < 100; ++i)
			f.Retire();
		Require(!f.state.inventoryRetirementPending && !f.state.inventoryRetirementRemaining && record.consumers.Empty() && record.consumers.Current(f.state.inventoryGeneration) && f.state.frontier.empty() && f.state.visited.empty(),
			"Retirement failed to finish or activate the empty inventory");
		Require(record.consumers.Observe(3, 0, f.state.inventoryGeneration), "Retired inventory refused a fresh scan");
		record.consumers.scanning.push_back({ std::make_shared<Geometry>() });
		f.state.completeScan = 3;
		Require(f.state.Demand(record) > 0, "A fresh completed inventory remained blocked after retirement");
	}
	void CheckRepeatedInvalidationAndMutation()
	{
		Fixture f;
		for (unsigned i = 1; i <= 300; ++i)
			f.Add(i, 16, 1, 1);
		f.state.InvalidateInventory();
		f.Retire();
		const auto cursor = f.state.inventoryRetirementCursor;
		const auto before = referenceReleases;
		f.state.InvalidateInventory();
		Require(referenceReleases == before && f.state.inventoryRetirementRemaining == f.state.records.size() && f.state.inventoryRetirementCursor == cursor,
			"Repeated invalidation eagerly destroyed consumers or restarted its physical cursor");
		f.state.records.rehash(4096);
		Require(f.state.inventoryRetirementPending, "Mutation fixture did not retain a partial retirement");
		auto cursorEntry = f.state.records.find(f.state.inventoryRetirementCursor);
		Require(cursorEntry != f.state.records.end(), "Partial retirement lost its physical cursor");
		{
			Util::RendererOwnership owner(1);
			while (cursorEntry->second.consumers.RetireOne()) {}
		}
		const auto removed = cursorEntry->first;
		cursorEntry->second.drop = 0;
		f.state.pruneCursor = removed;
		const auto remaining = f.state.inventoryRetirementRemaining;
		f.state.Prune(MaintenanceBudget{});
		Require(!f.state.records.contains(removed) && f.state.inventoryRetirementRemaining < remaining,
			"Prune erased its retirement cursor without acknowledging the empty stale record");
		auto& adopted = f.Add(301, 0, 0, 1);
		adopted.consumers.seen = adopted.consumers.verified = 0;
		Require(f.state.inventoryRetirementRemaining < f.state.records.size(), "A newly adopted empty record enlarged stale retirement work");
		for (unsigned i = 0; f.state.inventoryRetirementPending && i < 100; ++i) {
			f.Retire();
			f.state.records.rehash(i % 2 ? 4096 : 1024);
			Require(f.state.inventoryRetirementRemaining <= f.state.records.size(), "Retirement count exceeded retained records after rehash");
		}
		Require(!f.state.inventoryRetirementPending && !f.state.inventoryRetirementRemaining, "Rehash stranded a stale inventory");
		for (const auto& [id, record] : f.state.records)
			Require(record.consumers.Current(f.state.inventoryGeneration) && record.consumers.Empty(), "Retirement reported completion with stale records");
	}
	void CheckPrunePreservesOwnership()
	{
		Fixture f;
		f.Add(1, 256, 256);
		f.Add(2, 256, 256, 1);
		f.state.InvalidateInventory();
		f.state.enabled = false;
		const auto before = referenceReleases;
		auto source = std::move(f.state.records.at(1).source);
		f.state.Prune(MaintenanceBudget{});
		f.state.records.at(1).source = std::move(source);
		Require(f.state.records.size() == 2 && referenceReleases == before, "Prune moved bulk stale reference destruction into record erasure");
		for (unsigned i = 0; f.state.inventoryRetirementPending && i < 100; ++i) {
			f.Retire();
			f.state.Prune(MaintenanceBudget{});
		}
		f.state.Prune(MaintenanceBudget{});
		Require(f.state.records.size() == 1 && f.state.records.contains(2) && f.state.reducedRecords.contains(2),
			"Invalidation lost the reduced texture's restoration ownership or retained stale full-size records");
	}
	void CheckDeviceResetDuringRetirement()
	{
		Fixture f;
		for (unsigned i = 1; i <= 16; ++i)
			f.Add(i, 256, 256, 1);
		f.state.InvalidateInventory();
		f.Retire();
		Require(f.state.inventoryRetirementPending && f.state.inventoryRetirementRemaining, "Device-reset fixture did not preserve pending retirement");
		f.state.transaction.emplace();
		f.state.transaction->payload.request = { 1, f.state.epoch };
		Memory::sampleGeneration = 2;
		f.Tick(false);
		Require(f.state.deviceGeneration == 2 && f.state.epoch == 2 && f.state.records.empty() && f.state.reducedRecords.empty() &&
					!f.state.transaction && f.state.reader.clears == 1 && !f.state.inventoryRetirementPending && !f.state.inventoryRetirementRemaining && f.state.scans == 1,
			"Device reset retained stale records, work identity, or a stuck inventory retirement count");
	}
	void CheckTickLifecycle()
	{
		for (bool loading : { false, true }) {
			Fixture f;
			for (unsigned i = 1; i <= 16; ++i)
				f.Add(i, 256, 256, 1);
			f.global.loading = loading;
			f.global.worldLoadTransitionStartFrame = 4;
			f.state.transaction.emplace();
			f.state.transaction->payload.request = { 1, f.state.epoch };
			f.Tick();
			Require(f.state.epoch == 2 && f.state.inventoryGeneration == 2 && f.state.inventoryRetirementPending && !f.state.transaction && f.state.cancellations == 1 && f.state.services == 1 && !f.state.scans,
				"Transition did not invalidate/cancel work before service or scanned stale ownership");
			f.state.enabled = false;
			for (unsigned i = 0; f.state.inventoryRetirementPending && i < 100; ++i)
				f.Tick();
			Require(!f.state.inventoryRetirementPending && !f.state.inventoryRetirementRemaining && f.state.records.contains(1),
				"Disabled or paused Tick stopped retirement or lost restoration ownership");
		}
		Fixture empty;
		empty.state.enabled = false;
		empty.state.frontier.push_back({ std::make_shared<Geometry>() });
		empty.state.InvalidateInventory();
		empty.Tick();
		Require(!empty.state.inventoryRetirementPending && empty.state.frontier.empty(), "Disabled empty Tick skipped pending native reference retirement");
	}
	void CheckSharedDeadlineAndSchedule()
	{
		FakeClock::current = {};
		MaintenanceBudget budget;
		Require(budget.Available(), "A new maintenance budget started exhausted");
		FakeClock::Advance(std::chrono::microseconds(399));
		Require(budget.Available(), "Maintenance stopped before the shared allowance elapsed");
		FakeClock::Advance(std::chrono::microseconds(1));
		Require(!budget.Available() && !budget.Available(), "Checking an exhausted budget renewed its deadline");
		MaintenanceBudget nextFrame;
		Require(nextFrame.Available() && !budget.Available(), "A new frame did not get an independent allowance");
		StreamingTextures::MaintenanceSchedule schedule;
		std::array<bool, StreamingTextures::MaintenanceSchedule::phaseCount> firstSeen{};
		for (std::size_t frame = 0; frame < firstSeen.size(); ++frame) {
			const auto first = schedule.StartFrame();
			const auto phase = StreamingTextures::MaintenanceSchedule::Phase(first, 0);
			const auto index = static_cast<std::size_t>(phase);
			Require(index < firstSeen.size() && !firstSeen[index], "Maintenance fairness repeated a first phase before completing its cycle");
			firstSeen[index] = true;
			std::array<bool, StreamingTextures::MaintenanceSchedule::phaseCount> phasesSeen{};
			for (std::size_t offset = 0; offset < phasesSeen.size(); ++offset) {
				const auto current = static_cast<std::size_t>(StreamingTextures::MaintenanceSchedule::Phase(first, offset));
				Require(current < phasesSeen.size() && !phasesSeen[current], "A maintenance frame repeated or omitted a phase");
				phasesSeen[current] = true;
			}
		}
		Require(schedule.StartFrame() == 0, "Maintenance fairness did not wrap its first phase");
	}
	void CheckTickSharedDeadline()
	{
		Fixture f;
		f.Add(1, 1, 0, 1);
		f.state.phaseCosts[static_cast<std::size_t>(MaintenancePhase::AdoptLoads)] = std::chrono::microseconds(250);
		f.state.phaseCosts[static_cast<std::size_t>(MaintenancePhase::PreviewScan)] = std::chrono::microseconds(200);
		f.Tick();
		Require(f.state.phases == std::vector<MaintenancePhase>{ MaintenancePhase::AdoptLoads, MaintenancePhase::PreviewScan } &&
					f.state.services == 1 && !f.state.scans && !f.state.selections && PreviewState::snapshots == 1 && f.state.pressureUpdates == 1 && Memory::samples == 1,
			"Optional phases renewed the shared deadline or exhausted maintenance suppressed mandatory work");
		const auto services = f.state.services, refreshes = PreviewState::snapshots, samples = Memory::samples;
		const auto phases = f.state.phases;
		f.state.Tick();
		Require(f.state.services == services && PreviewState::snapshots == refreshes && Memory::samples == samples && f.state.phases == phases,
			"A repeated same-frame Tick spent another allowance or repeated mandatory work");
	}
	void CheckTickMaintenanceFairness()
	{
		Fixture f;
		f.Add(1, 1, 0, 1);
		for (auto& cost : f.state.phaseCosts)
			cost = std::chrono::microseconds(400);
		std::array<bool, StreamingTextures::MaintenanceSchedule::phaseCount> serviced{};
		for (std::size_t frame = 0; frame < serviced.size(); ++frame) {
			f.Tick();
			Require(f.state.services == frame + 1 && PreviewState::snapshots == frame + 1 && f.state.pressureUpdates == frame + 1,
				"Fair scheduling delayed mandatory frame work");
			for (const auto phase : f.state.phases)
				serviced[static_cast<std::size_t>(phase)] = true;
		}
		for (const auto phase : { MaintenancePhase::AdoptLoads, MaintenancePhase::PreviewScan, MaintenancePhase::WorldScan, MaintenancePhase::Select })
			Require(serviced[static_cast<std::size_t>(phase)], "An expensive earlier phase starved later maintenance across the fairness cycle");
	}
	void CheckLifecycleBeforeExpiredMaintenance()
	{
		for (bool blocked : { false, true }) {
			Fixture f;
			f.Add(1, 16, 16, 1);
			f.global.loading = blocked;
			f.global.worldLoadTransitionStartFrame = 4;
			f.state.transaction.emplace();
			f.state.transaction->payload.request = { 1, f.state.epoch };
			f.state.phaseCosts[static_cast<std::size_t>(MaintenancePhase::AdoptLoads)] = std::chrono::microseconds(400);
			const auto before = referenceReleases;
			f.Tick();
			Require(f.state.epoch == 2 && f.state.inventoryGeneration == 2 && f.state.inventoryRetirementPending && !f.state.completeScan &&
						!f.state.transaction && f.state.cancellations == 1 && f.state.services == 1 && referenceReleases == before,
				"Exhausted maintenance delayed lifecycle invalidation or mandatory cancellation and service");
			f.state.enabled = false;
			for (unsigned frame = 0; f.state.inventoryRetirementPending && frame < 24; ++frame)
				f.Tick();
			Require(!f.state.inventoryRetirementPending && f.state.records.contains(1) && f.state.reducedRecords.contains(1),
				"Fair scheduling starved retirement while loading or disabled, or discarded restoration ownership");
		}
	}
	void CheckRetirementUsesSharedDeadline()
	{
		Fixture f;
		f.Add(1, 64, 64, 1);
		f.state.InvalidateInventory();
		FakeClock::releaseCost = std::chrono::microseconds(100);
		const auto before = referenceReleases;
		const MaintenanceBudget budget;
		{
			Util::RendererOwnership owner(1);
			f.state.RetireInventory(budget);
			f.state.Prune(budget);
		}
		Require(referenceReleases - before == 4 && f.state.inventoryRetirementPending && f.state.records.contains(1),
			"Retirement ignored the shared deadline or pruning renewed an exhausted allowance");
		FakeClock::releaseCost = {};
	}
	void CheckPreviewProtectionBoundaries()
	{
		Fixture f;
		Require(!f.state.PreviewProtectionPending(), "Inactive previews blocked safe reduction");
		f.state.previewActive = true;
		Require(f.state.PreviewProtectionPending(), "An active preview without roots allowed reduction");
		f.state.previewRoots.push_back(std::make_shared<Geometry>());
		f.state.nextPreviewScanMs = GetTickCount64() + 1;
		Require(!f.state.PreviewProtectionPending(), "Fresh completed preview protection blocked reduction");
		f.state.previewFrontier.push_back(f.state.previewRoots.front());
		Require(f.state.PreviewProtectionPending(), "Partial preview traversal allowed reduction");
		f.state.previewFrontier.clear();
		f.state.nextPreviewScanMs = GetTickCount64();
		Require(f.state.PreviewProtectionPending(), "Due preview traversal allowed reduction");
		f.state.nextPreviewScanMs = GetTickCount64() - 1;
		Require(f.state.PreviewProtectionPending(), "Overdue preview traversal allowed reduction");
		f.state.previewActive = false;
		Require(!f.state.PreviewProtectionPending(), "Inactive preview retained a traversal block");
	}
	void CheckMandatoryPreviewRefresh()
	{
		Fixture f;
		f.Add(1, 1, 0, 1);
		auto previous = std::make_shared<Geometry>();
		f.state.previewRoots.push_back(previous);
		f.state.previewFrontier.push_back(previous);
		f.state.previewVisited.insert(previous.get());
		f.state.nextPreviewScanMs = GetTickCount64() + 250;
		PreviewState::roots.push_back(std::make_shared<Geometry>());
		f.state.phaseCosts[static_cast<std::size_t>(MaintenancePhase::AdoptLoads)] = std::chrono::microseconds(400);
		f.Tick();
		Require(PreviewState::snapshots == 1 && f.state.previewRoots == PreviewState::roots && f.state.previewActive &&
					f.state.previewFrontier.empty() && f.state.previewVisited.empty() && !f.state.nextPreviewScanMs && f.state.PreviewProtectionPending() &&
					f.state.phases == std::vector<MaintenancePhase>{ MaintenancePhase::AdoptLoads } && f.state.services == 1,
			"Exhausted maintenance hid a changed preview or retained a completed protection certificate");
		PreviewState::complete = false;
		f.Tick();
		Require(f.state.previewSuspensions == 1 && !f.state.enabled && f.state.services == 2,
			"Incomplete preview roots failed to suspend reductions before mandatory service");
	}

	void SetPreviewCondition(State& state, bool pending)
	{
		state.previewActive = true;
		state.previewRoots.push_back(std::make_shared<Geometry>());
		state.nextPreviewScanMs = GetTickCount64() + (pending ? 0 : 1);
	}
	void CheckActualTransactionPreviewValidation()
	{
		for (bool pending : { false, true }) {
			Fixture f;
			auto& record = f.Add(1, 1, 0, 0);
			SetPreviewCondition(f.state, pending);
			Require(f.state.Demand(record) >= 1, "Transaction fixture did not have admissible fresh demand");
			const auto checks = f.state.diagnostics.demandChecks;
			f.state.transaction.emplace();
			f.state.transaction->payload.request = { 1, f.state.epoch, 1 };
			f.state.ValidateTransaction();
			Require(pending ? (!f.state.transaction && f.state.cancellations == 1 && !f.state.approvedTransactions && f.state.diagnostics.demandChecks == checks) :
							  (f.state.transaction && !f.state.cancellations && f.state.approvedTransactions == 1 && f.state.diagnostics.demandChecks == checks + 1),
				"Actual transaction validation admitted worsening preview detail or rejected complete protection");
		}
		{
			Fixture f;
			f.Add(1, 1, 0, 3);
			SetPreviewCondition(f.state, true);
			f.state.transaction.emplace();
			f.state.transaction->payload.request = { 1, f.state.epoch, 1 };
			f.state.ValidateTransaction();
			Require(f.state.transaction && f.state.approvedTransactions == 1 && !f.state.cancellations && f.state.diagnostics.demandChecks == 1,
				"Pending preview protection blocked a safe restoration or bypassed its fresh demand validation");
		}
		for (bool pending : { false, true }) {
			Fixture f;
			f.Add(1, 1, 0, 0);
			SetPreviewCondition(f.state, pending);
			f.state.reductionRequested = false;
			f.state.options.maximumSize = 1024;
			f.state.transaction.emplace();
			f.state.transaction->payload.request = { 1, f.state.epoch, 2 };
			f.state.ValidateTransaction();
			Require(pending ? (!f.state.transaction && f.state.cancellations == 1 && !f.state.approvedTransactions) :
							  (f.state.transaction && !f.state.cancellations && f.state.approvedTransactions == 1),
				"Maximum-size target bypassed pending preview protection or its completed-policy path");
		}
	}
	void CheckActualTransactionIdentityValidation()
	{
		for (unsigned mismatch = 0; mismatch < 6; ++mismatch) {
			Fixture f;
			auto& record = f.Add(1, 1, 0, 0);
			f.state.transaction.emplace();
			auto& request = f.state.transaction->payload.request;
			request = { 1, f.state.epoch, 1 };
			switch (mismatch) {
			case 0:
				++request.epoch;
				break;
			case 1:
				++request.serial;
				break;
			case 2:
				record.source->rendererTexture = nullptr;
				break;
			case 3:
				f.engine.texture = nullptr;
				break;
			case 4:
				f.engine.resourceView = nullptr;
				break;
			case 5:
				f.engine.UAV = &f;
				break;
			}
			f.state.ValidateTransaction();
			Require(!f.state.transaction && f.state.cancellations == 1 && !f.state.approvedTransactions && !f.state.diagnostics.demandChecks,
				"Actual transaction validation admitted obsolete identity or dereferenced demand before rejecting it");
		}
	}
	void CheckActualSelectionPreviewGuard()
	{
		for (bool pending : { false, true })
			for (bool restore : { false, true }) {
				Fixture f;
				auto& record = f.Add(1, 1, 0, restore ? 3 : 0);
				SetPreviewCondition(f.state, pending);
				f.state.EvaluateCandidate(record, 1);
				Require(f.state.acceptedCandidates == ((!pending || restore) ? 1u : 0u) &&
							f.state.diagnostics.blocked[static_cast<std::size_t>(Block::InventoryIncomplete)] == ((pending && !restore) ? 1u : 0u),
					"Actual selection preview guard admitted a reduction or blocked restoration");
			}
	}

	void CheckLoadingPreviewSelection()
	{
		Fixture f;
		auto& record = f.Add(1, 1, 0, 1);
		record.previewProtected = true;
		f.global.loading = true;
		PreviewState::active = true;
		f.Tick();
		Require(f.state.selections == 1 && f.state.lastPreviewOnly && f.state.services == 1,
			"Loading stopped preview restoration selection or allowed ordinary selection");
		globals::features::upscaling.transition = true;
		const auto selections = f.state.selections;
		f.Tick();
		Require(f.state.selections == selections && f.state.services == 2,
			"Render transitions admitted preview allocation or suppressed mandatory transaction service");
	}

}

int main()
{
	try {
		CheckImmediateInvalidation();
		CheckRepeatedInvalidationAndMutation();
		CheckPrunePreservesOwnership();
		CheckDeviceResetDuringRetirement();
		CheckTickLifecycle();
		CheckSharedDeadlineAndSchedule();
		CheckTickSharedDeadline();
		CheckTickMaintenanceFairness();
		CheckLifecycleBeforeExpiredMaintenance();
		CheckRetirementUsesSharedDeadline();
		CheckPreviewProtectionBoundaries();
		CheckMandatoryPreviewRefresh();
		CheckActualTransactionPreviewValidation();
		CheckActualTransactionIdentityValidation();
		CheckActualSelectionPreviewGuard();
		CheckLoadingPreviewSelection();
		std::cout << "Texture streaming generation, shared maintenance and bounded retirement lifecycle passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
