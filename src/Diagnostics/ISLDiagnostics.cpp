#include "Diagnostics/ISLDiagnosticsInternal.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include "CSEditor/EditorWindow.h"
#	include "Diagnostics/ReflexMeasurementInternal.h"
#	include "Features/InverseSquareLighting.h"
#	include "Features/LightLimitFix/ShadowCulling.h"
#	include "Features/Upscaling.h"
#	include "Features/VR.h"
#	include "Globals.h"
#	include "State.h"
#	include "Utils/RuntimeToggle.h"
#	include <bit>
#	include <filesystem>
#	include <fstream>

namespace CSX::Diagnostics::ISL
{
	namespace RM = ReflexMeasurement;
	namespace
	{
		thread_local Slot* localSlot{};
		thread_local uint64_t localFrame = UnknownFrame;
		thread_local volatile uint64_t discardSink{};
		constexpr auto StartupFile = "Data/SKSE/Plugins/CommunityShaders/ISLDiagnostics.once.json";
	}
	DiagnosticState& Data()
	{
		static auto* state = new DiagnosticState;
		return *state;
	}
	Policy CurrentPolicy() noexcept { return Policy::Unpack(Data().effective.load(std::memory_order_acquire)); }
	const char* MetricName(Metric metric)
	{
		static constexpr const char* names[] = {
#	define CS_ISL_NAME(name) #name,
			CS_ISL_METRICS(CS_ISL_NAME)
#	undef CS_ISL_NAME
		};
		return names[size_t(metric)];
	}
	bool AcquisitionActive() noexcept { return RM::State().acquiring.load(std::memory_order_acquire); }
	void MarkContaminated() noexcept { Data().contaminated.store(true, std::memory_order_release); }
	Scope::Scope(Metric selected, bool broad, uintptr_t address) noexcept : policy(CurrentPolicy()), metric(selected), caller(address), frame(localFrame)
	{
		auto& d = Data();
		const auto epoch = d.captureEpoch.load(std::memory_order_acquire);
		if (policy.observation == Observation::Off || (!d.sessionActive.load(std::memory_order_acquire) && !d.admitting.load(std::memory_order_acquire)))
			return;
		if (!localSlot) {
			const auto thread = GetCurrentThreadId();
			for (auto& candidate : d.slots) {
				uint32_t empty = 0;
				if (candidate.thread.load(std::memory_order_relaxed) == thread || candidate.thread.compare_exchange_strong(empty, thread)) {
					localSlot = &candidate;
					break;
				}
			}
			if (!localSlot) {
				d.threadLoss.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
		auto* candidate = localSlot;
		if (selected == Metric::LuminanceCalls || selected == Metric::ProcessLight || selected == Metric::ParticleQuery) {
			invocationSlot = candidate;
			if (!candidate->invocationDepth++) {
				candidate->invocationSequence.fetch_add(1, std::memory_order_acq_rel);
				candidate->invocationGeneration.store(policy.generation, std::memory_order_relaxed);
				candidate->invocationEnter.store(policy.observation == Observation::Timed ? RM::Clock() : 0, std::memory_order_relaxed);
				candidate->invocationActive.store(true, std::memory_order_relaxed);
				candidate->invocationSequence.fetch_add(1, std::memory_order_release);
			}
		}
		if (!d.admitting.load(std::memory_order_acquire))
			return;
		if (!candidate->depth) {
			if (candidate->busy.test_and_set(std::memory_order_acquire)) {
				d.contention.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			if (!d.admitting.load(std::memory_order_acquire) || epoch != d.captureEpoch.load(std::memory_order_acquire)) {
				candidate->busy.clear(std::memory_order_release);
				return;
			}
		}
		slot = candidate;
		++slot->depth;
		auto& total = slot->totals[size_t(metric)];
		++total.count;
		if (caller && policy.observation == Observation::Timed && (total.count & 63) == 1) {
			bool found = false;
			for (auto& entry : slot->callers) {
				if (!entry.address || entry.address == caller) {
					entry.address = caller;
					++entry.count;
					found = true;
					break;
				}
			}
			if (!found)
				++slot->callerOverflow;
		}
		if (policy.generation != d.captureGeneration)
			++slot->overlap;
		if (policy.observation == Observation::Timed && (broad || (total.count & 63) == 1))
			enter = RM::Clock();
	}
	Scope::~Scope() { Finish(); }
	void Scope::Finish() noexcept
	{
		const auto retire = SKSE::stl::scope_exit([&] {
			if (invocationSlot && !--invocationSlot->invocationDepth) {
				invocationSlot->invocationSequence.fetch_add(1, std::memory_order_acq_rel);
				invocationSlot->invocationActive.store(false, std::memory_order_relaxed);
				invocationSlot->invocationSequence.fetch_add(1, std::memory_order_release);
			}
			invocationSlot = nullptr;
		});
		if (!slot)
			return;
		if (enter) {
			const auto exit = RM::Clock();
			auto& total = slot->totals[size_t(metric)];
			++total.timed;
			total.elapsed += std::max<int64_t>(0, exit - enter);
			total.maximum = std::max(total.maximum, exit - enter);
			if (slot->rows.size() < Data().capacity)
				slot->rows.push_back({ enter, exit, policy.generation, frame, caller, slot->thread.load(std::memory_order_relaxed), metric });
			else
				++slot->overflow;
		}
		if (!--slot->depth)
			slot->busy.clear(std::memory_order_release);
		slot = nullptr;
	}
	void Scope::Count(Metric counter, uint64_t value) noexcept
	{
		if (slot)
			slot->totals[size_t(counter)].count += value;
	}
	void Scope::Candidate(float value) noexcept
	{
		const auto bits = std::bit_cast<uint32_t>(value);
		discardSink = (discardSink * 1099511628211ull) ^ bits;
		if (slot) {
			slot->checksum = (slot->checksum * 1099511628211ull) ^ bits;
			++slot->candidateCount;
		}
	}
	void Scope::Unique(const void* identity) noexcept
	{
		if (!slot || !identity)
			return;
		const auto key = reinterpret_cast<uintptr_t>(identity);
		const auto index = ((key >> 4) ^ (key >> 13)) % UniqueCapacity;
		for (size_t n = 0; n < 16; ++n) {
			auto& entry = slot->unique[(index + n) % UniqueCapacity];
			if (entry == key)
				return;
			if (!entry) {
				entry = key;
				++slot->uniqueCount;
				return;
			}
		}
		++slot->uniqueOverflow;
	}
	void RecordCount(Metric metric, uint64_t count) noexcept
	{
		if (count) {
			Scope scope(metric);
			scope.Count(metric, count - 1);
		}
	}
	void ClearFrame() noexcept { localFrame = UnknownFrame; }
	bool SuppressRadiusWrites() noexcept { return Data().suppressed; }
	void InitializeStartup()
	{
		auto& d = Data();
		if (d.startupLoaded)
			return;
		d.startupLoaded = true;
		try {
			if (!std::filesystem::exists(StartupFile))
				return;
			if (std::filesystem::file_size(StartupFile) > 4096)
				throw std::runtime_error("startup request exceeds 4096 bytes");
			std::ifstream input(StartupFile);
			const auto request = json::parse(input);
			input.close();
			if (!std::filesystem::remove(StartupFile))
				throw std::runtime_error("could not consume one-launch request");
			if (request.at("schema") != 1 || request.at("expectedBuildId").get<std::string>() != BuildProvenance::GetBuildId() || request.at("engineRadiusWrites") != "suppress_from_startup")
				throw std::invalid_argument("startup request schema, build or policy mismatch");
			d.suppressed = true;
			d.startupReceipt = { { "active", "suppress_from_startup" }, { "requestedNextLaunch", nullptr }, { "consumed", true }, { "restartRequiredToRestore", true } };
		} catch (const std::exception& error) {
			d.suppressed = false;
			std::error_code ignored;
			std::filesystem::remove(StartupFile, ignored);
			d.startupReceipt = { { "active", "normal" }, { "requestedNextLaunch", nullptr }, { "error", error.what() }, { "consumeError", ignored ? json(ignored.message()) : json(nullptr) } };
			logger::warn("[ISL diagnostics] Invalid startup override: {}", error.what());
		}
	}
	json StartupStage(const json& args)
	{
		const auto mode = args.at("engineRadiusWrites").get<std::string>();
		if (mode != "normal" && mode != "suppress_from_startup")
			throw std::invalid_argument("invalid startup policy");
		if (mode == "normal") {
			std::filesystem::remove(StartupFile);
			Data().startupReceipt["requestedNextLaunch"] = "normal";
		} else {
			if (std::filesystem::exists(StartupFile))
				throw std::logic_error("an unconsumed startup request already exists; cancel it first");
			const std::filesystem::path target(StartupFile), temporary = target.string() + ".tmp";
			std::filesystem::create_directories(target.parent_path());
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			output << json{ { "schema", 1 }, { "expectedBuildId", BuildProvenance::GetBuildId() }, { "engineRadiusWrites", mode } }.dump();
			output.flush();
			if (!output)
				throw std::runtime_error("startup request write failed");
			output.close();
			std::filesystem::rename(temporary, target);
			Data().startupReceipt["requestedNextLaunch"] = mode;
		}
		return { { "restartRequired", true }, { "startup", Data().startupReceipt } };
	}
	void TouchSession()
	{
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		Data().expiresAt = RM::Clock() + frequency.QuadPart * 120;
	}
	void StageDefaultPolicy()
	{
		auto policy = Policy{};
		policy.generation = Policy::Unpack(Data().requested.load()).generation + 1;
		Data().requested.store(policy.Pack(), std::memory_order_release);
	}
	void PrepareSession()
	{
		auto& d = Data();
		if (d.sessionActive)
			throw std::logic_error("previous ISL session requires restoration");
		if (CurrentPolicy().Pack() != d.requested.load())
			throw std::logic_error("previous diagnostic policy has not settled");
		d.originalShadow = globals::features::lightLimitFix.settings.ShadowCulling;
		d.ownedShadow = d.originalShadow;
		d.originalEnabled = d.ownedEnabled = globals::features::inverseSquareLighting.settings.Enabled;
		d.settingsOwned = false;
		d.restorationConflict = false;
		d.expired = false;
		d.sessionActive = true;
		TouchSession();
	}
	void RestoreLive()
	{
		auto& d = Data();
		if ((d.requested.load() & 0xffff) != (Policy{}.Pack() & 0xffff))
			StageDefaultPolicy();
		d.sessionActive = false;
		d.pruneArm = 0;
		d.pruneActive = false;
		{
			std::scoped_lock lock(d.pruneMutex);
			d.prune.Cancel();
		}
		if (d.settingsOwned) {
			auto& llf = globals::features::lightLimitFix;
			auto& isl = globals::features::inverseSquareLighting;
			if (json(llf.settings.ShadowCulling) == d.ownedShadow)
				llf.settings.ShadowCulling = d.originalShadow.get<LightLimitFixDetail::ShadowCulling::Settings>();
			else
				d.restorationConflict = true;
			if (isl.settings.Enabled == d.ownedEnabled)
				isl.SetPerformanceCostMeasurementEnabled(d.originalEnabled);
			else
				d.restorationConflict = true;
			d.settingsOwned = false;
		}
	}
	void ExpireSession()
	{
		if (Data().sessionActive.exchange(false)) {
			Data().expired = true;
			RestoreLive();
		}
	}
	void Prepass(uint64_t frame)
	{
		localFrame = frame;
		auto& d = Data();
		if (AcquisitionActive()) {
			if (globals::features::inverseSquareLighting.settings.Enabled != d.capturedEnabled ||
				globals::features::upscaling.settings.neuralRenderingEnabled != d.capturedNR ||
				globals::features::vr.settings.EnableVRShadowBatch != d.capturedBatch ||
				globals::features::lightLimitFix.settings.EnableParticleLightsDetection != d.capturedDetection ||
				globals::features::lightLimitFix.settings.ShadowCulling != d.capturedShadow)
				MarkContaminated();
			if (EditorWindow::GetSingleton()->lightEditor.IsEnabled() || EditorWindow::GetSingleton()->lightEditor.HasDeferredWork())
				MarkContaminated();
		} else if (d.sessionActive && RM::Clock() > d.expiresAt.load()) {
			ExpireSession();
		}
		if (d.expired && !AcquisitionActive() && RM::State().leaseActive)
			RM::RequestExpiredRestoration();
		if (Util::IsRuntimeToggleBlocked(globals::state)) {
			if (AcquisitionActive())
				MarkContaminated();
			return;
		}
		const auto requested = d.requested.load(std::memory_order_acquire);
		if (requested != d.effective.load(std::memory_order_relaxed)) {
			d.appliedFrame = frame;
			d.appliedQpc = RM::Clock();
			d.effective.store(requested, std::memory_order_release);
		}
	}
	bool DeferPrune(uint32_t frame)
	{
		auto& d = Data();
		if (!d.pruneActive.load(std::memory_order_relaxed))
			return false;
		std::unique_lock lock(d.pruneMutex, std::try_to_lock);
		if (!lock.owns_lock()) {
			MarkContaminated();
			return false;
		}
		const bool defer = d.prune.Defer(frame, RM::Clock());
		d.pruneActive.store(d.prune.active, std::memory_order_relaxed);
		return defer;
	}
	uint64_t Outstanding() noexcept
	{
		uint64_t count = 0;
		for (auto& slot : Data().slots) count += slot.busy.test(std::memory_order_acquire);
		return count;
	}
	void PrepareCapture(uint64_t capture, size_t capacity)
	{
		auto& d = Data();
		if (!d.acknowledged || Outstanding())
			throw std::logic_error("ISL evidence not acknowledged or still has outstanding invocations");
		const auto boundedCapacity = std::min<size_t>(capacity, 65536);
		const auto policy = CurrentPolicy();
		auto condition = ConditionJson();
		std::array<Slot*, ThreadCapacity> locked{};
		const auto release = SKSE::stl::scope_exit([&] { for (auto* slot : locked) if (slot) slot->busy.clear(std::memory_order_release); });
		for (size_t i = 0; i < d.slots.size(); ++i) {
			if (d.slots[i].busy.test_and_set(std::memory_order_acquire))
				throw std::logic_error("ISL invocation still draining; retry after completion");
			locked[i] = &d.slots[i];
		}
		// Allocate before committing ownership so allocation failure remains retryable.
		if (policy.observation == Observation::Timed) {
			for (auto& slot : d.slots) slot.rows.reserve(boundedCapacity);
			d.frozenRows.reserve(boundedCapacity * ThreadCapacity);
		}
		d.captureEpoch.fetch_add(1, std::memory_order_acq_rel);
		d.capture = capture;
		d.capacity = boundedCapacity;
		d.frozen = false;
		d.acknowledged = false;
		d.contiguous = 0;
		d.start = RM::State().journal.start;
		d.stop = d.freeze = 0;
		d.threadLoss = d.contention = 0;
		d.contaminated = false;
		d.captureGeneration = policy.generation;
		d.captureCondition = std::move(condition);
		d.entryInvocations = {};
		d.capturedEnabled = globals::features::inverseSquareLighting.settings.Enabled;
		d.capturedNR = globals::features::upscaling.settings.neuralRenderingEnabled;
		d.capturedBatch = globals::features::vr.settings.EnableVRShadowBatch;
		d.capturedDetection = globals::features::lightLimitFix.settings.EnableParticleLightsDetection;
		d.capturedShadow = globals::features::lightLimitFix.settings.ShadowCulling;
		for (auto& slot : d.slots) {
			slot.totals = {};
			slot.unique = {};
			slot.callers = {};
			slot.callerOverflow = 0;
			slot.rows.clear();
			slot.overflow = slot.uniqueOverflow = slot.checksum = slot.candidateCount = slot.overlap = slot.uniqueCount = 0;
		}
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		d.frequency = frequency.QuadPart;
		d.frozenRows.clear();
	}
	Invocation InspectInvocation(const Slot& slot) noexcept
	{
		Invocation result;
		result.thread = slot.thread.load(std::memory_order_relaxed);
		for (unsigned attempt = 0; attempt < 3; ++attempt) {
			const auto before = slot.invocationSequence.load(std::memory_order_acquire);
			if (before & 1)
				continue;
			result.generation = slot.invocationGeneration.load(std::memory_order_relaxed);
			result.enter = slot.invocationEnter.load(std::memory_order_relaxed);
			result.active = slot.invocationActive.load(std::memory_order_relaxed);
			if (before == slot.invocationSequence.load(std::memory_order_acquire))
				return result;
		}
		result.unstable = true;
		return result;
	}
	void OpenCapture(int64_t now)
	{
		auto& d = Data();
		d.start = now;
		for (size_t i = 0; i < d.slots.size(); ++i) d.entryInvocations[i] = InspectInvocation(d.slots[i]);
		{
			std::scoped_lock lock(d.pruneMutex);
			if (const auto frames = d.pruneArm.exchange(0)) {
				d.prune.Arm(frames);
				d.prune.Activate(now, d.frequency);
				d.pruneActive = true;
			}
		}
		d.admitting.store(true, std::memory_order_release);
	}
	void CloseCapture() { Data().admitting.store(false, std::memory_order_release); }
	bool ExportAcknowledged() noexcept { return Data().acknowledged; }
}
#endif
