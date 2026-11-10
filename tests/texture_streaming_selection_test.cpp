#include "Features/TextureStreaming/Diagnostics.h"
#include "Features/TextureStreaming/Policy.h"
#include "Features/TextureStreaming/RestorationPriority.h"
#include "Features/TextureStreaming/WorkSchedule.h"
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Policy = TextureStreamingPolicy;
using Block = StreamingTextures::Diagnostics::Block;
std::uint64_t GetTickCount64() { return 10000; }
namespace StreamingTextures
{
	struct Origin
	{
		bool protectedConsumer = false;
		unsigned requiredCategories = 0;
	};
	bool ReadOrigin(void*, Origin&) { return false; }
	namespace LoadHooks
	{
		struct RememberedSize
		{
			int full;
			unsigned drop, categories;
			std::uint64_t hash, bytes;
		};
		void Remember(const std::string&, const RememberedSize&) {}
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
		std::uint64_t pendingBytes = 0, recoveryDemandBytes = 0;
		bool priorityWork = false, fresh = true;
		bool Fresh(std::uint64_t) const { return fresh; }
	};
	static bool StreamingFits(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, bool, bool, bool, std::uint64_t, std::uint64_t) { return true; }
};
enum class ReadKind
{
	Restore,
	ValidateSource
};
struct ReadRequest
{
	std::uint64_t serial = 0, epoch = 0;
	std::string path;
	int full = 0;
	unsigned drop = 0;
	ReadKind kind = ReadKind::Restore;
};
struct Engine
{
	void* texture = nullptr;
} engine;
struct Source
{
	Engine* rendererTexture = &engine;
} source;
constexpr std::size_t maximumReducedTextures = 4096;
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
		struct Record
		{
			Source* source = &::source;
			Engine* engine = &::engine;
			void* current = nullptr;
			bool previewProtected = false, loadLifetime = false, sourceValidated = false, protectedConsumer = false;
			std::uint64_t headerHash = 0, fileBytes = 0, stableSince = 0, retryAfter = 0, restorationWaitingSince = 0;
			unsigned rememberedDrop = 0, distanceDrop = 0, drop = 0, desired = 0, target = 0, demand = 0, visits = 0;
			double restorationUrgency = 0;
			struct
			{
				std::uint64_t verified = 0;
				std::vector<int> committed;
			} consumers;
			int full = 0;
			std::string path = "textures/test.dds";
			std::array<std::uint64_t, 4> bytes{ 1024, 256, 64, 16 };
		};
		struct Transaction
		{
			bool published = false;
			struct
			{
				ReadRequest request;
			} payload;
		};
		struct Reader
		{
			ReadRequest request;
			void Start(ReadRequest value) { request = std::move(value); }
		} reader;
		struct
		{
			unsigned reserveMiB = 1024;
		} options;
		bool reading = false, enabled = true;
		std::optional<Transaction> transaction;
		Memory::Snapshot memory;
		StreamingTextures::Diagnostics diagnostics;
		StreamingTextures::WorkSchedule workSchedule;
		std::unordered_map<std::uint64_t, Record> records;
		std::unordered_set<std::uint64_t> reducedRecords;
		std::vector<int> frontier;
		std::uint64_t epoch = 1, completeScan = 1, reducedCursor = 0, selectionCursor = 0, admissionDeferrals = 0;
		std::size_t learningRemaining = 0;
		std::string detail;
		unsigned Demand(Record& record)
		{
			++record.visits;
			return record.demand;
		}
		unsigned Target(const Record& record, unsigned) const { return record.target; }
		void Defer(Block block, const Record&) { diagnostics.Defer(block); }
		void Select(Deadline, Selection);
	};
};
#include "streaming_select_under_test.h"

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
int main()
{
	try {
		using State = TextureStreaming::State;
		using Selection = State::Selection;
		const auto unlimited = State::Deadline::max();
		State learning;
		learning.workSchedule.mode = StreamingTextures::InventoryWork::Learning;
		for (unsigned id = 1; id <= 160; ++id) learning.records.emplace(id, State::Record{});
		learning.learningRemaining = learning.records.size();
		learning.Select(State::Deadline::min(), Selection::Full);
		Require(learning.learningRemaining == 160, "Expired deadline consumed learning work");
		learning.Select(unlimited, Selection::Full);
		Require(learning.learningRemaining == 96, "Learning failed to bound a frame to 64 records");
		learning.reading = true;
		learning.Select(unlimited, Selection::Full);
		Require(learning.learningRemaining == 96, "Busy reader consumed learning work");
		learning.reading = false;
		learning.Select(unlimited, Selection::Full);
		learning.Select(unlimited, Selection::Full);
		learning.Select(unlimited, Selection::Full);
		Require(learning.learningRemaining == 0, "Learning never completed");
		for (const auto& [id, record] : learning.records) Require(record.visits == 1, "Learning skipped or repeatedly swept a record");
		State active;
		active.workSchedule.mode = StreamingTextures::InventoryWork::Active;
		for (unsigned id = 1; id <= 3; ++id) active.records.emplace(id, State::Record{});
		for (unsigned id : { 1u, 2u }) {
			active.reducedRecords.insert(id);
			active.records.at(id).drop = active.records.at(id).target = 1;
		}
		active.records.at(2).previewProtected = true;
		active.Select(unlimited, Selection::Preview);
		Require(active.records.at(1).visits == 0 && active.records.at(2).visits == 1 && active.records.at(3).visits == 0, "Preview service visited unprotected records");
		active.Select(unlimited, Selection::Reduced);
		Require(active.records.at(1).visits == 1 && active.records.at(2).visits == 2 && active.records.at(3).visits == 0, "Reduced service visited full inventory");
		active.Select(unlimited, Selection::Full);
		Require(active.records.at(1).visits == 1 && active.records.at(2).visits == 2 && active.records.at(3).visits == 1, "Full service revisited reduced textures");
		active.records.at(1).target = 0;
		active.records.at(2).target = active.records.at(2).desired = 2;
		active.records.at(3).target = active.records.at(3).desired = 1;
		active.Select(unlimited, Selection::Reduced);
		Require(active.reading && active.reader.request.serial == 1 && active.reader.request.kind == ReadKind::Restore, "Reduction beat a required restoration");
		active.Select(unlimited, Selection::Full);
		Require(active.reader.request.serial == 1 && active.records.at(3).visits == 1, "Full selection replaced a pending restoration");
		active.reading = false;
		active.records.at(3).sourceValidated = true;
		active.Select(unlimited, Selection::Full);
		Require(active.transaction && active.transaction->payload.request.serial == 3, "Full inventory failed to admit a validated reduction");
		std::cout << "PASS: production selection budgets, learning completion, disjoint lanes, preview protection and restoration priority\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
