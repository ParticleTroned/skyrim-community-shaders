#include "Features/TextureStreaming/Policy.h"
#include "Features/TextureStreaming/WorkSchedule.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Policy = TextureStreamingPolicy;
using StreamingTextures::InventoryWork;
std::uint64_t clockMs = 1000;
std::uint64_t GetTickCount64() { return clockMs; }
bool pendingLoad = false;
namespace StreamingTextures::LoadHooks
{
	bool HasPending() { return pendingLoad; }
}
namespace logger
{
	template <class... Args>
	void info(const char*, Args&&...)
	{}
}
namespace Util
{
	bool rendererOwned = true;
	int GetRendererContextLock(void*, void*) { return 0; }
	struct RendererOwnership
	{
		explicit RendererOwnership(int) {}
		explicit operator bool() const { return rendererOwned; }
	};
}
namespace globals
{
	struct GameState
	{
		std::uint32_t frameCount = 1;
		std::atomic_uint32_t worldLoadTransitionStartFrame{ 0 };
		bool loading = false;
		bool IsSaveLoadSafeModeActive() const { return loading; }
	} gameState;
	auto* state = &gameState;
	namespace d3d
	{
		void* device = &gameState;
		void* context = &gameState;
	}
	namespace game
	{
		void* renderer = &gameState;
		void* shadowState = &gameState;
	}
	namespace features
	{
		struct Upscaling
		{
			bool transition = false;
			bool IsTextureStreamingTransitionActive() const { return transition; }
		} upscaling;
	}
}
struct Memory
{
	struct Snapshot
	{
		struct
		{
			std::uint64_t Budget = 8192 * Policy::MiB, CurrentUsage = 1024 * Policy::MiB;
		} local;
		std::uint64_t sampledAtMs = 0, generation = 1, pendingBytes = 0;
		bool priorityWork = false;
		bool Fresh(std::uint64_t now) const { return now >= sampledAtMs && now - sampledAtMs <= 500; }
	};
	Snapshot sample;
	static Memory& Get()
	{
		static Memory value;
		return value;
	}
	Snapshot SampleStreaming(void*)
	{
		sample.sampledAtMs = clockMs;
		return sample;
	}
	static std::uint64_t Add(std::uint64_t a, std::uint64_t b) { return a + b; }
};
enum class Block
{
	WorldLoading,
	Transition,
	OriginUnavailable
};
bool originHookReady = true;
struct TextureStreaming
{
	struct State
	{
		using Deadline = std::chrono::steady_clock::time_point;
		enum class Selection
		{
			Reduced,
			Full,
			Preview
		};
		bool enabled = true, reading = false, paused = false, previewActive = false, firstPressureReductionPending = false;
		struct
		{
			bool continuous = false, onPressure = true;
		} modes;
		struct
		{
			unsigned reserveMiB = 1024, maximumSize = 0;
			bool rememberedSizes = true;
		} options;
		struct Record
		{
			bool previewProtected = false;
		};
		std::map<int, Record> records;
		std::vector<int> reducedRecords, frontier;
		struct Transaction
		{
			struct
			{
				struct
				{
					int serial = 0;
				} request;
			} payload;
		};
		std::optional<Transaction> transaction;
		struct Reader
		{
			void ClearCache() {}
		} reader;
		struct Diagnostics
		{
			void Defer(Block) {}
		} diagnostics;
		std::uint32_t lastFrame = UINT32_MAX, worldStart = 0;
		std::uint64_t epoch = 1, deviceGeneration = 0;
		std::size_t learningRemaining = 0;
		StreamingTextures::WorkSchedule workSchedule;
		Policy::Pressure pressure;
		Memory::Snapshot memory;
		std::string detail;
		unsigned adopted = 0, previews = 0, serviced = 0, cancelled = 0, invalidated = 0, scans = 0, fullSelections = 0, reducedSelections = 0, previewSelections = 0, pruned = 0;
		bool ReductionRequested() const { return enabled && (modes.continuous || (modes.onPressure && pressure.conserving)); }
		InventoryWork InventoryMode() const;
		void AdoptLoads()
		{
			++adopted;
			if (pendingLoad) {
				reducedRecords.push_back(1);
				pendingLoad = false;
			}
		}
		void ScanPreviews() { ++previews; }
		void ServiceTransaction() { ++serviced; }
		void CancelTransaction()
		{
			++cancelled;
			transaction.reset();
		}
		void UpdatePressureDiagnostics(std::uint64_t) {}
		void InvalidateInventory(std::uint64_t now)
		{
			++invalidated;
			frontier.clear();
			learningRemaining = 0;
			workSchedule.Reset(now);
		}
		void Scan(Deadline)
		{
			++scans;
			workSchedule.Begin(clockMs);
			workSchedule.Complete(clockMs);
			if (workSchedule.mode == InventoryWork::Learning)
				learningRemaining = records.size();
		}
		void Select(Deadline, Selection selection)
		{
			if (selection == Selection::Full) {
				++fullSelections;
				learningRemaining = 0;
			} else if (selection == Selection::Reduced)
				++reducedSelections;
			else
				++previewSelections;
		}
		void Prune(Deadline) { ++pruned; }
		void Tick();
	};
};
#include "streaming_tick_under_test.h"

void Require(bool value, const char* reason)
{
	if (!value)
		throw std::runtime_error(reason);
}
void Frame(TextureStreaming::State& state, std::uint64_t advance = 16)
{
	clockMs += advance;
	++globals::state->frameCount;
	state.Tick();
}
int main()
{
	try {
		using State = TextureStreaming::State;
		State state;
		state.records.emplace(1, State::Record{});
		Frame(state);
		for (unsigned i = 0; i < 300; ++i) Frame(state);
		Require(state.scans == 0 && state.fullSelections == 0 && state.reducedSelections == 0 && state.pruned == 0, "Idle pressure-only streaming scanned or swept records");
		Require(state.adopted == 301 && state.serviced == 301, "Idle gate blocked lifecycle or completion service");
		Frame(state, 1000);
		Require(state.scans == 1 && state.fullSelections == 1, "Sparse remembered-size learning did not complete");
		const auto full = state.fullSelections;
		for (unsigned i = 0; i < 100; ++i) Frame(state);
		Require(state.scans == 1 && state.fullSelections == full, "Idle demand work continued between learning passes");
		state.options.rememberedSizes = false;
		Frame(state, 6000);
		const auto scans = state.scans;
		for (unsigned i = 0; i < 100; ++i) Frame(state, 100);
		Require(state.scans == scans, "Disabled learning still scanned an idle world");
		const auto invalidated = state.invalidated;
		Memory::Get().sample.local.CurrentUsage = 8000 * Policy::MiB;
		Frame(state);
		Require(state.pressure.conserving && state.scans == scans + 1 && state.invalidated > invalidated, "Pressure did not wake a fresh inventory");
		state.reducedRecords.push_back(1);
		Memory::Get().sample.local.CurrentUsage = 1024 * Policy::MiB;
		for (unsigned i = 0; i < 55; ++i) Frame(state, 100);
		Require(!state.pressure.conserving && state.workSchedule.mode == InventoryWork::Active, "Pressure recovery stranded reduced resources");
		state.enabled = false;
		const auto reducedSelections = state.reducedSelections;
		Frame(state);
		Require(state.reducedSelections > reducedSelections && state.workSchedule.mode == InventoryWork::Idle, "Disabling streaming suppressed restoration");
		state.enabled = true;
		state.reducedRecords.clear();
		state.options.maximumSize = 1024;
		Frame(state);
		Require(state.workSchedule.mode == InventoryWork::Active, "A size cap was ignored without pressure");
		state.options.maximumSize = 0;
		pendingLoad = true;
		Frame(state);
		Require(!pendingLoad && !state.reducedRecords.empty() && state.workSchedule.mode == InventoryWork::Active, "A reduced load did not wake demand service");
		globals::state->loading = true;
		state.previewActive = true;
		const auto previewSelections = state.previewSelections;
		Frame(state);
		Require(state.previewSelections > previewSelections, "Loading blocked preview restoration");
		globals::features::upscaling.transition = true;
		state.transaction.emplace();
		Frame(state);
		Require(!state.transaction && state.paused, "Render transition retained an unfinished transaction");
		globals::state->loading = false;
		globals::features::upscaling.transition = false;
		Memory::Get().sample.generation++;
		Frame(state);
		Require(state.records.empty() && state.reducedRecords.empty(), "Device change retained old resource inventory");
		const auto adopted = state.adopted;
		state.Tick();
		Require(state.adopted == adopted, "Maintenance ran twice in one application frame");
		state.enabled = false;
		state.frontier.push_back(1);
		state.workSchedule.mode = InventoryWork::Active;
		Util::rendererOwned = false;
		Frame(state);
		Require(!state.frontier.empty(), "Inventory references were released without renderer ownership");
		Util::rendererOwned = true;
		Frame(state);
		Require(state.frontier.empty() && state.workSchedule.mode == InventoryWork::Idle, "Disabling an empty inventory retained queued scene references");
		const auto disabledAdopted = state.adopted;
		Frame(state);
		Require(state.adopted == disabledAdopted, "Fully disabled streaming failed to take the idle gate");

		StreamingTextures::WorkSchedule cadence;
		Require(cadence.Update(InventoryWork::Active, 10) && cadence.Due(10), "Active work did not start immediately");
		cadence.Begin(10);
		cadence.Complete(2010);
		Require(!cadence.Due(4009) && cadence.Due(4010), "Large inventories did not rest for their duration");
		cadence.Update(InventoryWork::Learning, 4010);
		Require(!cadence.Due(9009) && cadence.Due(9010), "Learning cadence was not five seconds");
		Require(cadence.Due(1), "Clock rollback permanently stalled inventory");
		cadence.Update(InventoryWork::Active, 10000);
		cadence.Begin(10000);
		cadence.Complete(30000);
		Require(cadence.lastDurationMs == 20000 && !cadence.Due(34999) && cadence.Due(35000), "Long scan diagnostics or capped rest duration changed");
		cadence.Update(InventoryWork::Idle, 40000);
		Require(!cadence.NeedsDemand(InventoryWork::Idle, 40001, 0, false), "Idle world requested camera work");
		Require(cadence.NeedsDemand(InventoryWork::Active, 40001, 0, false), "Pressure/cap/reduced/pending wakeup skipped fresh demand");
		cadence.Update(InventoryWork::Learning, 40000);
		Require(!cadence.NeedsDemand(InventoryWork::Learning, 44749, 0, false) && !cadence.NeedsDemand(InventoryWork::Learning, 44750, 0, false) &&
					cadence.NeedsDemand(InventoryWork::Learning, 45000, 0, false),
			"An initial inventory prefetched demand without a reusable checkpoint");
		cadence.Begin(45000);
		Require(cadence.NeedsDemand(InventoryWork::Learning, 45001, 0, false), "In-progress inventory lost fresh demand");
		cadence.Complete(45002);
		Require(cadence.NeedsDemand(InventoryWork::Learning, 45003, 1, false) && !cadence.NeedsDemand(InventoryWork::Learning, 45003, 0, false), "Learning completion did not gate camera work");

		StreamingTextures::DemandContext demand;
		demand.eyeCount = 2;
		demand.frame = 100;
		demand.sampledAtMs = 50000;
		demand.mipBias = -1;
		for (auto& eye : demand.eyes)
			eye = { 1000, 1200, 1500, 1800, 1, 1, 0 };
		int root = 0, replacementRoot = 0;
		StreamingTextures::WorkSchedule learning;
		learning.Update(InventoryWork::Learning, 45000);
		Require(!learning.DeferUnchangedLearning(50000, 100, demand, 1, 1, &root, false), "The first learning inventory was skipped");
		learning.Begin(50000);
		learning.Complete(50100);
		const auto learned = learning;
		Require(!learning.NeedsDemand(InventoryWork::Learning, 54849, 0, false) &&
					learning.NeedsDemand(InventoryWork::Learning, 54850, 0, false),
			"Reusable learning lost its bounded prefetch window");
		for (std::uint64_t now = 54850; now < 55100; ++now)
			Require(!learning.NeedsDemand(InventoryWork::Learning, now, 0, true),
				"A loading or reduction catalog prefetched demand that could not be reused");
		Require(learning.NeedsDemand(InventoryWork::Learning, 55100, 0, true) &&
					learning.NeedsDemand(InventoryWork::Learning, 54850, 1, true) &&
					learning.NeedsDemand(InventoryWork::Active, 54850, 0, true),
			"Catalog guards suppressed due inventory, learning selection or pressure demand");
		StreamingTextures::DemandContext captured;
		if (learning.NeedsDemand(InventoryWork::Learning, 55099, 0, false)) {
			captured = demand;
			captured.sampledAtMs = 55099;
		}
		Require(learning.DeferUnchangedLearning(55101, 100, captured, 1, 1, &root, false),
			"Crossing the learning deadline between world update and Present defeated inventory reuse");
		learning = learned;
		auto catalogRefresh = learned;
		demand.sampledAtMs = 55100;
		Require(!catalogRefresh.DeferUnchangedLearning(55100, 100, demand, 1, 1, &root, true) && catalogRefresh.Due(55100),
			"A loading catalog or remembered reduction extended the normal inventory interval");
		catalogRefresh.Begin(55100);
		catalogRefresh.Complete(55200);
		demand.sampledAtMs = 60200;
		Require(!catalogRefresh.DeferUnchangedLearning(60200, 100, demand, 1, 1, &root, false),
			"Removing remembered reductions reused a checkpoint from before normal inventories");
		for (std::uint64_t now = 55100; now < 80000; now += 5000) {
			demand.sampledAtMs = now;
			Require(learning.DeferUnchangedLearning(now, 100, demand, 1, 1, &root, false), "Unchanged learning traversed the scene again");
		}
		Require(learning.nextScanMs == 80000, "Unknown consumer changes escaped the bounded refresh");
		Require(!learning.NeedsDemand(InventoryWork::Learning, 79750, 0, false) &&
					learning.NeedsDemand(InventoryWork::Learning, 80000, 0, false),
			"Mandatory learning refresh prefetched demand that could not be reused");
		demand.sampledAtMs = 80000;
		Require(!learning.DeferUnchangedLearning(80000, 100, demand, 1, 1, &root, false), "Full learning refresh was suppressed indefinitely");

		demand.sampledAtMs = 55100;
		const auto mustRefresh = [&](StreamingTextures::DemandContext changed, std::uint64_t epoch, std::uint64_t resources, const void* world,
									 std::uint32_t frame = 100, std::uint64_t now = 55100) {
			auto candidate = learned;
			Require(!candidate.DeferUnchangedLearning(now, frame, changed, epoch, resources, world, false), "Changed or invalid learning inputs reused an inventory");
		};
		mustRefresh(demand, 2, 1, &root);
		mustRefresh(demand, 1, 2, &root);
		mustRefresh(demand, 1, 1, &replacementRoot);
		mustRefresh(demand, 1, 1, nullptr);
		mustRefresh(demand, 1, 1, &root, 102);
		mustRefresh(demand, 1, 1, &root, 100, 55351);
		mustRefresh(demand, 1, 1, &root, 100, 49999);
		for (unsigned eye = 0; eye < 2; ++eye) {
			auto changed = demand;
			changed.positions[eye][0] = static_cast<float>(StreamingTextures::WorkSchedule::LearningMovement);
			mustRefresh(changed, 1, 1, &root);
			changed = demand;
			changed.positions[eye][1] = std::numeric_limits<float>::quiet_NaN();
			mustRefresh(changed, 1, 1, &root);
			changed = demand;
			changed.eyes[eye].projectionX = 1.1;
			mustRefresh(changed, 1, 1, &root);
			changed = demand;
			changed.eyes[eye].renderWidth = 900;
			mustRefresh(changed, 1, 1, &root);
			changed = demand;
			changed.eyes[eye].outputHeight = 0;
			mustRefresh(changed, 1, 1, &root);
		}
		for (unsigned eyes : { 0u, 1u, 3u }) {
			auto changed = demand;
			changed.eyeCount = eyes;
			mustRefresh(changed, 1, 1, &root);
		}
		for (double bias : { 0.0, 17.0, std::numeric_limits<double>::infinity() }) {
			auto changed = demand;
			changed.mipBias = bias;
			mustRefresh(changed, 1, 1, &root);
		}
		learning = learned;
		demand.positions[0][0] = 1;
		Require(learning.DeferUnchangedLearning(55100, 100, demand, 1, 1, &root, false), "Small HMD movement defeated idle learning reuse");
		demand.positions[0][0] = static_cast<float>(StreamingTextures::WorkSchedule::LearningMovement);
		demand.sampledAtMs = 60100;
		Require(!learning.DeferUnchangedLearning(60100, 100, demand, 1, 1, &root, false), "Small steps moved the baseline and hid cumulative motion");
		learning = learned;
		learning.Reset(55100);
		demand.positions[0][0] = 0;
		Require(!learning.DeferUnchangedLearning(60100, 100, demand, 1, 1, &root, false), "Inventory invalidation retained learning reuse");
		learning = learned;
		learning.Update(InventoryWork::Active, 55100);
		Require(!learning.DeferUnchangedLearning(60100, 100, demand, 1, 1, &root, false) && learning.Due(55100), "Learning reuse blocked pressure/cap/reduced/pending work");
		learning = learned;
		learning.Begin(55100);
		Require(!learning.DeferUnchangedLearning(60100, 100, demand, 1, 1, &root, false), "Learning reuse interrupted an unfinished inventory");
		learning.Complete(60100);
		demand.sampledAtMs = 65100;
		Require(!learning.DeferUnchangedLearning(65100, 100, demand, 1, 2, &root, false), "An origin change during scanning was hidden by completion");
		learning.Reset(65100);
		demand.eyes[1].projectionY = 0;
		Require(!learning.DeferUnchangedLearning(65100, 100, demand, 1, 1, &root, false), "Invalid projection suppressed the first scan");
		learning.Begin(65100);
		learning.Complete(65100);
		demand.sampledAtMs = 70100;
		Require(!learning.DeferUnchangedLearning(70100, 100, demand, 1, 1, &root, false), "Repeated invalid projections authorized learning reuse");
		std::cout << "PASS: production Tick idle/learning/pressure/cap/restore/preview/transition/device/frame gates and scan cadence\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
