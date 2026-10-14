#include "Features/TextureStreaming/DemandDiagnostics.h"
#include "Features/TextureStreaming/Diagnostics.h"
#include "Features/TextureStreaming/GeometryDemand.h"
#include "Features/TextureStreaming/RestorationCache.h"
#include "Features/TextureStreaming/Settings.h"
#include "Features/TextureStreaming/StatusAccounting.h"
#include "Utils/GpuMemoryBudget.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	std::mutex* statusMutex = nullptr;
	bool probeLocks = true;
	unsigned formattingCalls = 0, externalCalls = 0;
	std::function<void()> afterFormattingProbe;
	void Probe(bool external = false)
	{
		if (external)
			++externalCalls;
		else
			++formattingCalls;
		if (probeLocks) {
			bool unlocked = false;
			std::thread worker([&] {
				unlocked = statusMutex->try_lock();
				if (unlocked)
					statusMutex->unlock();
			});
			worker.join();
			Require(unlocked, "Status formatting or external snapshot lookup held the state mutex");
		}
		if (!external && afterFormattingProbe) {
			auto callback = std::move(afterFormattingProbe);
			afterFormattingProbe = {};
			callback();
		}
	}
	std::uint64_t StatusTestTickCount() { return 10000; }
	struct Memory
	{
		using Snapshot = Util::GpuMemoryBudget::Snapshot;
		static Memory& Get()
		{
			static Memory memory;
			return memory;
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		Util::GpuMemoryBudget::StreamingPressureSimulation GetStreamingPressure() const
		{
			Probe(true);
			Util::GpuMemoryBudget::StreamingPressureSimulation result;
			result.headroomMiB = 512;
			result.budgetBytes = 4096;
			result.startedAtMs = 9000;
			result.expiresAtMs = 11000;
			result.generation = 123;
			return result;
		}
#endif
	};
	namespace Policy = TextureStreamingPolicy;
	using Diagnostics = StreamingTextures::Diagnostics;
	const char* streamingConflictWarning = "conflict";
	std::atomic_bool originHookReady = true;
#include "streaming_status_diagnostics_under_test.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "streaming_status_terrain_under_test.h"
#endif
#include "streaming_status_snapshot_under_test.h"
	struct TextureStreaming
	{
		struct State : StatusSnapshot
		{
			using StatusSnapshot = ::StatusSnapshot;
			mutable std::mutex mutex;
			struct Records
			{
				inline static unsigned traversals = 0;
				bool empty() const { return false; }
				unsigned begin() const
				{
					++traversals;
					throw std::runtime_error("GetStatus traversed texture records");
				}
				unsigned end() const { return begin(); }
			} records;
			std::vector<int> frontier;
			std::unordered_set<int> visited;
			std::optional<int> transaction;
			struct Reader
			{
				StreamingTextures::RestorationCache::Snapshot CacheStatus() const
				{
					Probe(true);
					StreamingTextures::RestorationCache::Snapshot result;
					result.limitBytes = 4096;
					result.bytes = 1024;
					result.entries = 4;
					result.generation = 456;
#ifdef DEVBENCH_BRIDGE_ENABLED
					result.hits = 3;
					result.misses = 2;
					result.evictions = 1;
					result.storeFailures = 0;
#endif
					return result;
				}
			} reader;
			[[nodiscard]] StatusSnapshot CopyStatusSnapshot() const;
			bool ReductionRequested() const noexcept;
		};
		std::unique_ptr<State> state = std::make_unique<State>();
		std::string failedLoadedMessage = "unavailable";
		nlohmann::json GetStatus() const;
	};
#define GetTickCount64 StatusTestTickCount
#include "streaming_status_copy_under_test.h"
#include "streaming_status_reduction_under_test.h"
#undef GetTickCount64

	nlohmann::json StatusTestDiagnosticStatus(const Diagnostics& value, const Diagnostics& initial = {})
	{
		Probe();
		return DiagnosticStatus(value, initial);
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	template <std::size_t Count>
	nlohmann::json StatusTestDiagnosticSampleStatus(const StreamingTextures::DiagnosticSamples<Count>& value,
		const std::array<std::string_view, Count>& names)
	{
		Probe();
		return DiagnosticSampleStatus(value, names);
	}
	nlohmann::json StatusTestMaterialDiagnosticStatus(const StreamingTextures::MaterialDiagnostics& value)
	{
		Probe();
		return MaterialDiagnosticStatus(value);
	}
	nlohmann::json StatusTestTextureDiagnosticStatus(const StreamingTextures::TextureDiagnostics& value)
	{
		Probe();
		return TextureDiagnosticStatus(value);
	}
	nlohmann::json StatusTestDemandDiagnosticStatus(const StreamingTextures::DemandDiagnostics& value)
	{
		Probe();
		return DemandDiagnosticStatus(value);
	}
#endif
}

namespace StreamingTextures
{
	nlohmann::json StatusTestSerializeModes(const Modes& value)
	{
		Probe();
		return SerializeModes(value);
	}
	nlohmann::json StatusTestSerializeOptions(const Options& value)
	{
		Probe();
		return SerializeOptions(value);
	}
	nlohmann::json StatusTestSerializeCategories(const Categories& value)
	{
		Probe();
		return SerializeCategories(value);
	}
	namespace LoadHooks
	{
		nlohmann::json Status()
		{
			Probe(true);
			return { { "generation", 789 }, { "entries", 2 } };
		}
	}
}

namespace
{
#define GetTickCount64 StatusTestTickCount
#define SerializeModes StatusTestSerializeModes
#define SerializeOptions StatusTestSerializeOptions
#define SerializeCategories StatusTestSerializeCategories
#define DiagnosticStatus StatusTestDiagnosticStatus
#define DiagnosticSampleStatus StatusTestDiagnosticSampleStatus
#define MaterialDiagnosticStatus StatusTestMaterialDiagnosticStatus
#define TextureDiagnosticStatus StatusTestTextureDiagnosticStatus
#define DemandDiagnosticStatus StatusTestDemandDiagnosticStatus
#include "streaming_status_under_test.h"
#undef GetTickCount64
#undef SerializeModes
#undef SerializeOptions
#undef SerializeCategories
#undef DiagnosticStatus
#undef DiagnosticSampleStatus
#undef MaterialDiagnosticStatus
#undef TextureDiagnosticStatus
#undef DemandDiagnosticStatus

	void Populate(TextureStreaming::State& state, std::uint64_t version)
	{
		state.enabled = true;
		state.requestedEnabled = true;
		state.modes.continuous = true;
		state.options.maximumSize = 2048;
		state.maximumDrop = 3;
		state.pressure.conserving = true;
		state.inventoryStatus.records = version;
		state.inventoryStatus.fullLogicalBytes = version * 1000;
		state.inventoryStatus.residentLogicalBytes = version * 250;
		state.inventoryStatus.protectedRecords = version - 1;
		state.inventoryStatus.reducedTextures = version;
		state.inventoryStatus.desiredShrinks = version;
		state.inventoryStatus.desiredRestores = version - 1;
		state.shrinks = version + 40;
		state.restores = version + 50;
		state.failures = version + 60;
		state.cancelled = version + 70;
		state.admissionDeferrals = version + 80;
		state.diagnostics.candidateRegistrations = version;
		state.demand.eyeCount = 2;
		state.demand.frame = static_cast<std::uint32_t>(version);
		state.demand.sampledAtMs = 9900;
		state.demand.mipBias = 1.25;
		state.inventoryGeneration = version + 10;
		state.inventoryRetirementPending = true;
		state.inventoryRetirementRemaining = version;
		state.scan = version + 30;
		state.completeScan = version + 29;
		state.frontier = { 1, 2 };
		state.visited = { 1, 2, 3 };
		state.pressureEpisode.active = true;
		state.pressureEpisode.startedAtMs = 8000;
		state.pressureEpisode.initialShrinks = 40;
		state.pressureEpisode.initialRestores = 50;
		state.pressureEpisode.initial.candidateRegistrations = 0;
		state.transaction = 1;
		state.paused = true;
		state.detail = "detail-" + std::to_string(version);
		state.memory.local.Budget = version * 10000;
		state.memory.local.CurrentUsage = version * 5000;
		state.memory.pendingBytes = version * 100;
		state.memory.recoveryDemandBytes = version * 50;
		state.memory.sampledAtMs = 9900;
		state.memory.generation = version + 20;
		state.memory.sequence = version + 100;
		state.memory.result = S_OK;
		state.epoch = version;
		state.deviceGeneration = version + 20;
		state.lastFrame = static_cast<std::uint32_t>(version);
		state.hasServiceAttemptFrame = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
		state.inventoryStatus.terrainRecords = version;
		state.inventoryStatus.terrainReducedTextures = version;
		state.inventoryStatus.terrainDesiredShrinks = version;
		state.gpuCopyShrinks = version;
		state.cachedRestores = version + 1;
		state.ddsRestores = version + 2;
		state.sourceHeaderReads = version + 3;
		state.sourcePayloadReads = version + 4;
		state.sourceBytesRead = version + 5;
		state.terrainInventoryDiagnostics.candidateRegistrations = version;
		state.terrainShrinks = version;
		state.terrainRestores = version + 1;
		state.terrainRetiredReductions = version + 2;
		state.terrainRetiredLogicalBytes = version * 50;
		state.memory.physicalBudgetBytes = version * 20000;
		state.memory.pressureSimulated = true;
		const auto path = "texture-" + std::to_string(version);
		state.terrainExclusions.observed[0] = true;
		state.terrainExclusions.texturePaths[0] = path;
		state.terrainExclusions.geometryNames[0] = "geometry-" + std::to_string(version);
		state.terrainReduction.path.fill(0);
		std::copy(path.begin(), path.end(), state.terrainReduction.path.begin());
		state.terrainReduction.serial = version;
#endif
	}
	void CheckVersion(const nlohmann::json& status, std::uint64_t version)
	{
		Require(status.at("snapshot").at("configurationEpoch") == version &&
					status.at("snapshot").at("inventoryGeneration") == version + 10 &&
					status.at("snapshot").at("deviceGeneration") == version + 20 &&
					status.at("snapshot").at("lastServiceAttemptFrame") == version &&
					status.at("snapshot").at("capturedAtMs") == 10000,
			"Status snapshot mixed lifecycle identities");
		Require(status.at("records") == version && status.at("protectedRecords") == version - 1 &&
					status.at("reducedTextures") == version && status.at("fullLogicalBytes") == version * 1000 &&
					status.at("residentLogicalBytes") == version * 250 && status.at("logicalBytesRemoved") == version * 750 &&
					status.at("physicalBytesReclaimed").is_null(),
			"Status inventory totals changed during formatting");
		Require(status.at("inventory").at("desiredShrinks") == version && status.at("inventory").at("desiredRestores") == version - 1 &&
					status.at("inventory").at("retirementRecords") == version && status.at("inventory").at("scan") == version + 30 &&
					status.at("inventory").at("completeScan") == version + 29 && status.at("inventory").at("frontierNodes") == 2 &&
					status.at("inventory").at("visitedNodes") == 3,
			"Status inventory and scan counters mixed snapshots");
		Require(status.at("memory").at("valid") == true && status.at("memory").at("budgetBytes") == version * 10000 &&
					status.at("memory").at("usageBytes") == version * 5000 && status.at("memory").at("pendingBytes") == version * 100 &&
					status.at("memory").at("deviceGeneration") == version + 20 && status.at("memory").at("sampleSequence") == version + 100,
			"Status memory fields mixed generations");
		Require(status.at("detail") == "detail-" + std::to_string(version) && status.at("shrinks") == version + 40 &&
					status.at("restores") == version + 50 && status.at("failures") == version + 60 && status.at("cancelled") == version + 70 &&
					status.at("admissionDeferrals") == version + 80 && status.at("diagnostics").at("candidateRegistrations") == version &&
					status.at("pressureEpisode").at("committedShrinks") == version &&
					status.at("pressureEpisode").at("committedRestores") == version,
			"Status copied counters or owned strings changed after capture");
		Require(status.at("rememberedSizes").at("generation") == 789 && status.at("distanceReductionRequested") == true &&
					status.at("reductionRequested") == true && status.at("paused") == true && status.at("transactionActive") == true,
			"Status controls or external snapshot fields changed");
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(status.at("terrainDiagnostics").at("records") == version && status.at("terrainDiagnostics").at("reducedTextures") == version &&
					status.at("terrainDiagnostics").at("desiredShrinks") == version &&
					status.at("terrainDiagnostics").at("inventory").at("candidateRegistrations") == version &&
					status.at("terrainDiagnostics").at("exclusionSamples").at("missingTexture").at("texturePath") == "texture-" + std::to_string(version) &&
					status.at("terrainDiagnostics").at("lastReduction").at("texturePath") == "texture-" + std::to_string(version),
			"Developer terrain values or owned diagnostic samples changed after capture");
		Require(status.at("transfers").at("gpuCopyShrinks") == version && status.at("transfers").at("cachedRestores") == version + 1 &&
					status.at("transfers").at("ddsRestores") == version + 2 && status.at("memory").at("physicalBudgetBytes") == version * 20000 &&
					status.at("restorationCache").at("generation") == 456 && status.at("pressureSimulation").at("deviceGeneration") == 123,
			"Developer transfers or independent external snapshot fields changed");
#else
		Require(!status.contains("terrainDiagnostics") && !status.contains("transfers") && !status.contains("restorationCache") &&
					!status.contains("pressureSimulation") && !status.at("memory").contains("physicalBudgetBytes"),
			"Developer status escaped production build isolation");
#endif
	}
	void CheckUnlockedOwnedSnapshot()
	{
		TextureStreaming streaming;
		statusMutex = &streaming.state->mutex;
		Populate(*streaming.state, 7);
		afterFormattingProbe = [&] {
			std::scoped_lock lock(streaming.state->mutex);
			Populate(*streaming.state, 8);
			streaming.failedLoadedMessage = "replacement message";
		};
		const auto status = streaming.GetStatus();
		CheckVersion(status, 7);
		Require(status.at("unavailableReason") == "unavailable", "Unavailable reason did not own its captured bytes");
		CheckVersion(streaming.GetStatus(), 8);
		Require(formattingCalls >= 6 && externalCalls >= 2 && TextureStreaming::State::Records::traversals == 0,
			"Status skipped its formatting probes or traversed the texture inventory");
	}
	void CheckEmptyAndInactiveStatus()
	{
		TextureStreaming streaming;
		statusMutex = &streaming.state->mutex;
		streaming.state->lastFrame = UINT32_MAX;
		const auto initial = streaming.GetStatus();
		Require(initial.at("records") == 0 && initial.at("fullLogicalBytes") == 0 && initial.at("logicalBytesRemoved") == 0 &&
					initial.at("snapshot").at("lastServiceAttemptFrame").is_null() && initial.at("pressureEpisode").at("observations").is_null() &&
					initial.at("pressureEpisode").at("committedShrinks").is_null() && initial.at("physicalBytesReclaimed").is_null(),
			"Empty initial status changed zero or null field semantics");
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(initial.at("terrainDiagnostics").at("lastReduction").is_null(), "Initial terrain status exposed an uncommitted reduction");
#endif
		Populate(*streaming.state, UINT32_MAX);
		CheckVersion(streaming.GetStatus(), UINT32_MAX);
		Populate(*streaming.state, 1);
		CheckVersion(streaming.GetStatus(), 1);
		streaming.state->externalStreamingConflict = true;
		Require(streaming.GetStatus().at("unavailableReason") == "conflict", "Conflict warning changed in snapshot publication");
	}
	void CheckConcurrentSnapshotIdentity()
	{
		TextureStreaming streaming;
		statusMutex = &streaming.state->mutex;
		Populate(*streaming.state, 1);
		probeLocks = false;
		std::atomic_bool stop = false;
		std::thread writer([&] {
			std::uint64_t version = 1;
			while (!stop.load()) {
				std::scoped_lock lock(streaming.state->mutex);
				Populate(*streaming.state, version);
				version = version == 31 ? 1 : version + 1;
			}
		});
		try {
			for (unsigned count = 0; count < 64; ++count) {
				const auto status = streaming.GetStatus();
				const auto version = status.at("snapshot").at("configurationEpoch").get<std::uint64_t>();
				CheckVersion(status, version);
			}
		} catch (...) {
			stop.store(true);
			writer.join();
			probeLocks = true;
			throw;
		}
		stop.store(true);
		writer.join();
		probeLocks = true;
	}
}

int main()
{
	try {
		CheckUnlockedOwnedSnapshot();
		CheckEmptyAndInactiveStatus();
		CheckConcurrentSnapshotIdentity();
		std::cout << "Texture streaming owned status snapshots and unlocked formatting passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
