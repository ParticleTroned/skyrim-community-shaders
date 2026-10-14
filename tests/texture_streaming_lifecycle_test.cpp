#include "Features/TextureStreaming/Categories.h"
#include "Features/TextureStreaming/ConsumerInventory.h"
#include "Features/TextureStreaming/Diagnostics.h"
#include "Features/TextureStreaming/Settings.h"
#include <algorithm>
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
			if (requireOwner && !ownerDepth)
				std::terminate();
		}
	};
	struct Engine
	{
		void* texture = nullptr;
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
		Snapshot SampleStreaming(void*)
		{
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
			bool enabled = true, paused = false, reading = false, hasPendingLoad = false;
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
			void InvalidateInventory();
			void MarkInventoryRetired(Record&);
			void RetireInventory();
			void Prune();
			std::uint32_t Demand(Record&);
			void Tick();
			void Defer(Block reason, const Record&) { diagnostics.Defer(reason); }
			bool ReductionRequested() const { return enabled; }
			void AdoptLoads() {}
			void ScanPreviews() {}
			void Scan();
			void Select(bool = false) { ++selections; }
			void UpdatePressureDiagnostics(std::uint64_t) {}
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
#include "streaming_prune_under_test.h"
#include "streaming_retirement_under_test.h"
#include "streaming_scan_gate_under_test.h"
#include "streaming_tick_under_test.h"
	using State = TextureStreaming::State;
	struct Fixture
	{
		State state;
		Engine engine{ this };
		GlobalState global;
		Fixture()
		{
			globals::state = &global;
			globals::d3d::device = globals::d3d::context = globals::game::renderer = globals::game::shadowState = this;
			globals::features::upscaling.transition = false;
			Memory::sampleGeneration = 1;
		}
		~Fixture()
		{
			requireOwner = false;
			globals::state = nullptr;
		}
		State::Record& Add(std::uint64_t id, unsigned committed = 1, unsigned scanning = 1, unsigned drop = 0)
		{
			auto& record = state.records[id];
			record.source = std::make_shared<Source>(Source{ &engine });
			record.engine = &engine;
			record.current = engine.texture;
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
				state.RetireInventory();
			}
			requireOwner = false;
			Require(referenceReleases - before <= maximumInventoryRetirementSteps, "A retirement pass exceeded its reference-release budget");
		}
		void Tick(bool ordinaryReset = true)
		{
			++global.frameCount;
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
		f.state.Prune();
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
		f.state.Prune();
		f.state.records.at(1).source = std::move(source);
		Require(f.state.records.size() == 2 && referenceReleases == before, "Prune moved bulk stale reference destruction into record erasure");
		for (unsigned i = 0; f.state.inventoryRetirementPending && i < 100; ++i) {
			f.Retire();
			f.state.Prune();
		}
		f.state.Prune();
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
}

int main()
{
	try {
		CheckImmediateInvalidation();
		CheckRepeatedInvalidationAndMutation();
		CheckPrunePreservesOwnership();
		CheckDeviceResetDuringRetirement();
		CheckTickLifecycle();
		std::cout << "Texture streaming generation and bounded retirement lifecycle passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
