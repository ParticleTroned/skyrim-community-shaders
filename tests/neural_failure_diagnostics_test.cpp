#include "Features/Upscaling/NeuralRendering/CapacityFallback.h"
#include "Features/Upscaling/NeuralRendering/GuidedReconstructionPolicy.h"
#include "Features/Upscaling/NeuralRendering/MemoryRecoveryPolicy.h"
#include "Features/Upscaling/NeuralRendering/MemoryRetirementPolicy.h"
#include "Features/Upscaling/NeuralRendering/NetworkSelection.h"
#include "Features/Upscaling/NeuralRendering/RegionCapacity.h"
#include "Features/Upscaling/NeuralRendering/StallRecoveryPolicy.h"
#include "Features/Upscaling/NeuralRendering/SubmissionFenceSnapshot.h"
#include "Utils/GpuMemoryBudget.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <iostream>
#include <limits>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	std::vector<std::string> messages;
	std::uint32_t memoryQueries = 0, ownerUpdates = 0, debugFallbacks = 0;
	bool sampleThrows = false, logThrows = false;
	Util::GpuMemoryBudget::Snapshot memoryResult;
	std::uint64_t observedAt = 1000;
	std::uint64_t ObservedAtMs() { return observedAt; }
	void DebugFallback(const char*) { ++debugFallbacks; }
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	bool Has(const std::string& text)
	{
		return std::ranges::any_of(messages, [&](const auto& value) { return value.contains(text); });
	}
}

namespace logger
{
	template <class... Args>
	void error(std::format_string<Args...> pattern, Args&&... args)
	{
		if (logThrows)
			throw std::runtime_error("logging unavailable");
		messages.push_back(std::format(pattern, std::forward<Args>(args)...));
	}
	template <class... Args>
	void info(std::format_string<Args...> pattern, Args&&... args)
	{
		error(pattern, std::forward<Args>(args)...);
	}
	template <class... Args>
	void warn(std::format_string<Args...> pattern, Args&&... args)
	{
		error(pattern, std::forward<Args>(args)...);
	}
}

namespace Util
{
	GpuMemoryBudget& GpuMemoryBudget::Get()
	{
		static GpuMemoryBudget service;
		return service;
	}
	GpuMemoryBudget::Snapshot GpuMemoryBudget::Sample(ID3D11Device* source, std::uint64_t maxAgeMs, bool force)
	{
		Require(source && maxAgeMs == 0 && force, "capture must force a fresh sample on the existing device");
		++memoryQueries;
		if (sampleThrows)
			throw std::runtime_error("budget unavailable");
		return memoryResult;
	}
	void GpuMemoryBudget::SetOwnerWork(Owner owner, bool priorityWork, std::uint64_t demand)
	{
		Require(owner == Owner::NeuralRendering && !priorityWork && demand == 0, "failure owner policy changed");
		++ownerUpdates;
	}
}

#define GetTickCount64 ObservedAtMs
#define OutputDebugStringA DebugFallback
namespace NeuralRendering
{
#include "neural_failure_stage_under_test.h"
	const char* ToString(RendererStage value)
	{
		return value == RendererStage::CommandBegin ? "command_begin" : "failure";
	}
	const char* ToString(const char* value) { return value; }
	template <class T>
	void Increment(T& value)
	{
		++value;
	}

	struct RendererApplyOutcome
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		bool reconstructionHistoryReset = false;
#endif
	};
	struct Runtime
	{
		static constexpr auto kFeatureSlotCount = kPhysicalFeatureSlotCount;
		CapacityFailure failure = CapacityFailure::Unsafe;
		static Runtime& Instance()
		{
			static Runtime instance;
			return instance;
		}
		CapacityFailure CreationCapacityFailure() const { return failure; }
		const char* Status() const { return "unsafe"; }
		const char* FailureStage() const { return "failure"; }
	};

	namespace Color
	{
		struct Observation
		{
			std::uint32_t frame = 0, sourceWorldFrame = 0, slot = 0, revision = 0, insertion = 0, mode = 0, rect = 0;
			std::uint64_t generation = 0;
			std::string failure;
			bool processed = false;
		};
		struct Registry
		{
			static Registry& Instance()
			{
				static Registry registry;
				return registry;
			}
			void Record(const Observation&) {}
		};
	}

	struct DeviceFixture
	{
		HRESULT reason = S_OK;
		std::uint32_t queries = 0;
		HRESULT GetDeviceRemovedReason()
		{
			++queries;
			return reason;
		}
	};
	struct DevicePointer
	{
		DeviceFixture* value = nullptr;
		explicit operator bool() const { return value != nullptr; }
		DeviceFixture* operator->() const { return value; }
		ID3D11Device* Get() const { return reinterpret_cast<ID3D11Device*>(value); }
	};
	struct InteropFixture
	{
		bool IsParallelDisabled() const { return true; }
		DeviceFixture device;
		bool present = true, captureThrows = false;
		std::uint32_t captures = 0;
		LifetimeFenceSnapshot fences{ .issued = 109, .completed = 101, .contexts = { 103, 105, 107 }, .initialized = true, .completedKnown = true };
		LifetimeFenceSnapshot GetLifetimeSnapshot()
		{
			++captures;
			if (captureThrows)
				throw std::runtime_error("snapshot unavailable");
			return fences;
		}
		std::string operation = "BeginD3D12 backpressure";
		std::string LastOperation() const { return operation; }
		DeviceFixture* Device() { return present ? &device : nullptr; }
		void AbandonUnsafe()
		{
			present = false;
			fences = {};
		}
	};
	struct Renderer
	{
		struct State
		{
#include "neural_failure_resource_key_under_test.h"
			struct Slot
			{
				ResourceKey resourceKey{};
				bool resourcesValid = false, historyValid = false;
				std::uint32_t lastSuccessfulFrame = 900, lastSuccessfulSourceWorldFrame = 899;
				struct
				{
					Color::Observation observation;
				} colorWork;
			};
			struct Snapshot
			{
				struct Counters
				{
					std::uint64_t failures = 0, successes = 0, validationFailures = 0, deviceRemovals = 0, quarantines = 0, resetFailures = 0, resetAttempts = 0;
					std::array<std::uint64_t, Runtime::kFeatureSlotCount> slotFailures{};
					std::array<std::uint64_t, static_cast<std::size_t>(RendererStage::Count)> failuresByStage{};
				} counters;
				std::uint32_t requestedModelResolutionPercent = 100, effectiveModelResolutionPercent = 70;
				RendererStage failureStage = RendererStage::None;
				std::uint32_t failureFeatureSlot = 0, frameId = 0, sourceWorldFrame = 0, insertionPoint = 0, ngxResult = 0;
				std::uint64_t generation = 0, successes = 0, failures = 0;
				HRESULT lastResult = S_OK;
				bool failureLatched = false, quarantined = false, outputCommitted = false;
				std::string detail, status, runtimeFailureStage;
				StallRecoveryPolicy stallRecovery;
				MemoryRecoveryPolicy memoryRecovery;
			} snapshot_;
			struct Configuration
			{
				std::uint32_t revision = 0;
				bool Enabled() const { return false; }
				std::uint32_t EffectiveMode() const { return 0; }
			} colorConfiguration_;
#ifdef DEVBENCH_BRIDGE_ENABLED
			struct Lifetime
			{
				bool failureObserved = false;
				std::uint32_t stage = 0;
				HRESULT result = S_OK;
				LifetimeFenceSnapshot after;
			}* activeLifetime_ = nullptr;
			struct
			{
				void DiagnosticFailure() {}
			} lifetimeDiagnostics_;
#endif
			DeviceFixture device11_;
			DevicePointer device_{ &device11_ };
			InteropFixture interop_;
			std::array<Slot, Runtime::kFeatureSlotCount> slots_{};
			MemoryRecoveryPolicy memoryRecovery_;
			MemoryRetirementPolicy memoryRetirement_;
			std::atomic<bool> backendMaintenancePending_ = false;
			bool failureLatched_ = false, quarantined_ = false, abandoned = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
			GuidedReconstructionExperiment reconstructionExperiment_;
#endif
			unsigned capturesFinalized = 0;
			void FinishCapture(const RendererApplyOutcome&) noexcept { ++capturesFinalized; }
#include "neural_reconstruction_finish_under_test.h"
			HRESULT removal = S_OK;
			RendererStage activeStage_ = RendererStage::None;
			void SetActiveFeatureSlotLocked(std::uint32_t) {}
			HRESULT GetDeviceRemovalReasonLocked(HRESULT) const { return removal; }
			void RefreshRuntimeTelemetryLocked() {}
			void AbandonRuntimeOwnershipNoexcept()
			{
				abandoned = true;
				device_.value = nullptr;
				interop_.AbandonUnsafe();
			}
			void AbandonSlotsLocked()
			{
				for (auto& slot : slots_) slot.resourcesValid = false;
			}
			std::uint32_t resets = 0;
			bool drainSucceeds = true;
			bool ResetLocked(bool resetShader, bool destruction)
			{
				Require(!resetShader && !destruction, "automatic recovery changes shaders or destruction state");
				++resets;
				if (!drainSucceeds)
					return FailLocked(RendererStage::ResetWait, HRESULT_FROM_WIN32(ERROR_TIMEOUT), "unsafe drain", Runtime::kFeatureSlotCount, true, true, false);
				failureLatched_ = false;
				snapshot_.failureLatched = false;
				snapshot_.stallRecovery.Cancel();
				return true;
			}
			bool ServiceStallRecoveryLocked(bool);
			void LogFailureDiagnosticsLocked(RendererStage, HRESULT, std::uint32_t) noexcept;
			bool FailLocked(RendererStage, HRESULT, std::string, std::uint32_t, bool, bool = false, bool = true);
			void QuarantineAfterUnexpectedFailureLocked(RendererStage, std::uint32_t, bool, std::uint64_t) noexcept;
			State()
			{
				auto& slot = slots_[1];
				slot.resourcesValid = slot.historyValid = true;
				slot.resourceKey = { .colorWidth = 723, .colorHeight = 779, .guideWidth = 723, .guideHeight = 779, .outputWidth = 723, .outputHeight = 779 };
			}
		};
	};
#include "neural_failure_diagnostics_under_test.h"
#include "neural_failure_quarantine_under_test.h"
#include "neural_stall_service_under_test.h"
}
#undef GetTickCount64
#undef OutputDebugStringA

int main()
{
	using namespace NeuralRendering;
	try {
		spdlog::set_level(spdlog::level::err);
		memoryResult.result = S_OK;
		memoryResult.sampledAtMs = 1000;
		memoryResult.sequence = 27;
		memoryResult.local.Budget = 16000000000;
		memoryResult.local.CurrentUsage = 9000000000;
		memoryResult.pendingBytes = 4096;
		memoryResult.recoveryDemandBytes = 8192;
		Renderer::State validation;
		validation.FailLocked(RendererStage::Validation, E_INVALIDARG, "invalid", 1, false);
		validation.FailLocked(RendererStage::ResourceCreation, E_OUTOFMEMORY, "pressure", 1, true);
		Runtime::Instance().failure = CapacityFailure::Pressure;
		validation.FailLocked(RendererStage::FeatureEvaluate, E_FAIL, "capacity", 1, true);
		Require(memoryQueries == 0 && validation.interop_.captures == 0 && !validation.failureLatched_, "nonterminal failures gained diagnostics or changed recovery");
		Runtime::Instance().failure = CapacityFailure::Unsafe;

		Renderer::State timeout;
		const auto timeoutResult = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
		timeout.FailLocked(RendererStage::CommandBegin, timeoutResult, "timeout", 1, true);
		Require(timeout.failureLatched_ && !timeout.quarantined_ && !timeout.abandoned, "timeout handling changed");
		Require(memoryQueries == 1 && timeout.interop_.captures == 1, "terminal snapshot was not captured exactly once");
		Require(timeout.device11_.queries == 1 && timeout.interop_.device.queries == 1, "both device reasons must be queried independently");
		Require(Has("hr=0x800705B4") && Has("contexts=[103,105,107]") && Has("completed=101, completed_known=true"), "failure or fence evidence missing");
		Require(Has("resident_slot=1: color=723x779") && !Has("resident_slot=0:"), "cached resident dimensions were misreported");
		Require(Has("requested_model_percent=100, effective_model_percent=70") && Has("fresh=true") && Has("usage_bytes=9000000000, budget_bytes=16000000000"), "memory or model state missing");
		timeout.FailLocked(RendererStage::FailureLatched, E_FAIL, "bypass", 1, true);
		Require(memoryQueries == 1 && timeout.interop_.captures == 1, "latched failure repeats diagnostic work");

		messages.clear();
		Renderer::State removed;
		removed.removal = removed.device11_.reason = DXGI_ERROR_DEVICE_REMOVED;
		removed.interop_.device.reason = DXGI_ERROR_DEVICE_HUNG;
		removed.interop_.fences.completed = UINT64_MAX;
		removed.interop_.fences.completedKnown = false;
		removed.interop_.fences.deviceRemoved = true;
		memoryResult.result = DXGI_ERROR_DEVICE_REMOVED;
		memoryResult.local = {};
		removed.FailLocked(RendererStage::CommandBegin, timeoutResult, "removed", 1, true);
		Require(removed.abandoned && removed.quarantined_ && removed.snapshot_.lastResult == DXGI_ERROR_DEVICE_REMOVED, "device-loss policy changed");
		Require(Has("hr=0x800705B4") && Has("d3d11_removal_hr=0x887A0005") && Has("d3d12_removal_hr=0x887A0006"), "raw failure or independent device reasons missing");
		Require(Has("completed_known=false, device_removed=true") && Has("fresh=false") && Has("issued=109"), "removed-device evidence was overwritten or reported as healthy");

		messages.clear();
		Renderer::State forced;
		forced.FailLocked(RendererStage::ResetWait, timeoutResult, "unsafe drain", 1, false, true, false);
		Require(forced.abandoned && Has("issued=109") && forced.snapshot_.counters.failures == 0, "forced teardown snapshot changed failure accounting or ran after detach");

		messages.clear();
		Renderer::State absent;
		absent.device_.value = nullptr;
		absent.interop_.present = false;
		const auto queriesBeforeAbsent = memoryQueries;
		absent.FailLocked(RendererStage::InteropInitialization, E_FAIL, "unavailable", 1, true);
		Require(memoryQueries == queriesBeforeAbsent && Has("query_attempted=false") && Has("d3d11_present=false, d3d11_removal_hr=0x80004003"), "missing devices reset shared memory state or were reported as healthy");

		messages.clear();
		Renderer::State disabled;
		spdlog::set_level(spdlog::level::off);
		disabled.FailLocked(RendererStage::CommandBegin, timeoutResult, "disabled", 1, true);
		Require(disabled.interop_.captures == 0 && memoryQueries == queriesBeforeAbsent, "disabled logging still gathers diagnostics");
		spdlog::set_level(spdlog::level::err);

		messages.clear();
		Renderer::State budgetException;
		sampleThrows = true;
		budgetException.FailLocked(RendererStage::CommandBegin, timeoutResult, "query throws", 1, true);
		Require(budgetException.failureLatched_ && Has("query_threw=true, hr=0x80004005, fresh=false") && Has("contexts=[103,105,107]") && Has("color=723x779"), "query exception changed failure handling or lost other evidence");
		sampleThrows = false;

		messages.clear();
		Renderer::State captureException;
		captureException.interop_.captureThrows = true;
		captureException.FailLocked(RendererStage::CommandBegin, timeoutResult, "capture throws", 1, true);
		Require(captureException.failureLatched_ && Has("Diagnostic snapshot failed; renderer failure handling continues"), "diagnostic exception escapes or is silent");

		messages.clear();
		Renderer::State unexpected;
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(unexpected.reconstructionExperiment_.Configure(GuidedReconstructionOverride::GuidedExperiment, 4, 1), "Comparison setup failed");
		(void)unexpected.reconstructionExperiment_.Resolve(1, 1);
#endif
		unexpected.QuarantineAfterUnexpectedFailureLocked(RendererStage::FeatureEvaluate, 1, true, 0);
		Require(unexpected.quarantined_ && unexpected.abandoned && Has("issued=109") && Has("color=723x779"), "exception quarantine failed to preserve pre-detach evidence");
		RendererApplyOutcome unexpectedOutcome;
		unexpected.FinishApplyLocked(unexpectedOutcome);
		Require(unexpected.capturesFinalized == 1, "Terminal outcome lost capture finalization");
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(unexpected.reconstructionExperiment_.Status().mode == GuidedReconstructionOverride::Auto && unexpectedOutcome.reconstructionHistoryReset &&
					!unexpected.reconstructionExperiment_.Status().historyResetPending,
			"Exceptional failure retained its lease or deferred history reset");
		RendererApplyOutcome nextOutcome;
		unexpected.FinishApplyLocked(nextOutcome);
		Require(!nextOutcome.reconstructionHistoryReset, "Exceptional failure repeated its history reset");
		for (bool failed : { false, true }) {
			Renderer::State terminal;
			Require(terminal.reconstructionExperiment_.Configure(GuidedReconstructionOverride::Basic, 4, 1), "Comparison setup failed");
			(void)terminal.reconstructionExperiment_.Resolve(1, 1);
			terminal.failureLatched_ = failed;
			if (!failed)
				terminal.reconstructionExperiment_.Cancel();
			RendererApplyOutcome outcome;
			terminal.FinishApplyLocked(outcome);
			Require(outcome.reconstructionHistoryReset && terminal.reconstructionExperiment_.Status().mode == GuidedReconstructionOverride::Auto,
				"Failure or optional fallback deferred its history reset");
		}
#endif
		const auto queriesBeforeRepeat = memoryQueries;
		unexpected.QuarantineAfterUnexpectedFailureLocked(RendererStage::FeatureEvaluate, 1, true, 0);
		Require(memoryQueries == queriesBeforeRepeat, "exception quarantine repeats diagnostics");

		Renderer::State loggerException;
		logThrows = true;
		loggerException.QuarantineAfterUnexpectedFailureLocked(RendererStage::FeatureEvaluate, 1, true, 0);
		Require(loggerException.quarantined_ && loggerException.abandoned && debugFallbacks == 1, "logging exception changed quarantine or lost fallback reporting");
		logThrows = false;
		Renderer::State recovering;
		observedAt = 1000;
		recovering.FailLocked(RendererStage::CommandBegin, timeoutResult, "stall", 1, true);
		Require(recovering.snapshot_.stallRecovery.pending && recovering.backendMaintenancePending_.load(), "eligible stall did not schedule existing maintenance");
		const auto initialCaptures = recovering.interop_.captures;
		observedAt = 1200;
		Require(!recovering.ServiceStallRecoveryLocked(true) && recovering.interop_.captures == initialCaptures, "failure polling is not throttled");
		observedAt = 11000;
		Require(!recovering.ServiceStallRecoveryLocked(true) && recovering.resets == 0, "pending GPU work was drained or retried");
		recovering.interop_.fences.completed = recovering.interop_.fences.issued;
		observedAt = 11500;
		Require(recovering.ServiceStallRecoveryLocked(true) && recovering.resets == 1 && !recovering.failureLatched_, "completed work did not safely recover");
		Require(recovering.snapshot_.stallRecovery.attempts == 1 && !recovering.backendMaintenancePending_.load(), "safe reset lost attempt history or left maintenance active");
		recovering.FailLocked(RendererStage::CommandBegin, timeoutResult, "second stall", 1, true);
		Require(recovering.snapshot_.stallRecovery.CooldownMs() == 20000, "repeated stalls lost the extended cooldown");
		observedAt = 21500;
		Require(!recovering.ServiceStallRecoveryLocked(true) && recovering.resets == 1, "second stall bypassed cooldown");
		observedAt = 31500;
		Require(recovering.ServiceStallRecoveryLocked(true) && recovering.snapshot_.stallRecovery.attempts == 2, "second recovery failed");
		recovering.FailLocked(RendererStage::CommandBegin, timeoutResult, "third stall", 1, true);
		observedAt = 61500;
		Require(recovering.ServiceStallRecoveryLocked(true) && recovering.snapshot_.stallRecovery.attempts == 3, "third recovery failed");
		recovering.FailLocked(RendererStage::CommandBegin, timeoutResult, "fourth stall", 1, true);
		Require(!recovering.snapshot_.stallRecovery.pending && !recovering.backendMaintenancePending_.load() && recovering.failureLatched_, "automatic attempts were not bounded across safe teardown");

		Renderer::State unsafe;
		unsafe.FailLocked(RendererStage::CommandBegin, timeoutResult, "stall", 1, true);
		unsafe.interop_.fences.completed = unsafe.interop_.fences.issued;
		unsafe.drainSucceeds = false;
		observedAt += 10000;
		Require(!unsafe.ServiceStallRecoveryLocked(true) && unsafe.quarantined_ && unsafe.abandoned && !unsafe.snapshot_.stallRecovery.pending, "unsafe drain did not retain quarantine");

		Renderer::State lateRemoval;
		lateRemoval.FailLocked(RendererStage::CommandBegin, timeoutResult, "stall", 1, true);
		lateRemoval.removal = lateRemoval.device11_.reason = DXGI_ERROR_DEVICE_REMOVED;
		lateRemoval.interop_.device.reason = DXGI_ERROR_DEVICE_HUNG;
		observedAt += 500;
		messages.clear();
		Require(!lateRemoval.ServiceStallRecoveryLocked(true) && lateRemoval.quarantined_ && lateRemoval.resets == 0, "late device removal attempted recovery");
		Require(Has("d3d11_removal_hr=0x887A0005") && Has("d3d12_removal_hr=0x887A0006"), "quarantine escalation lost both fresh device reasons");

		Renderer::State neverCompletes;
		neverCompletes.FailLocked(RendererStage::CommandBegin, timeoutResult, "stall", 1, true);
		observedAt += StallRecoveryPolicy::kMaximumWaitMs;
		Require(!neverCompletes.ServiceStallRecoveryLocked(true) && neverCompletes.quarantined_ && neverCompletes.resets == 0, "permanent stall did not fail closed within the polling window");

		Renderer::State cancelled;
		cancelled.FailLocked(RendererStage::CommandBegin, timeoutResult, "stall", 1, true);
		const auto capturesBeforeCancel = cancelled.interop_.captures;
		Require(!cancelled.ServiceStallRecoveryLocked(false) && !cancelled.snapshot_.stallRecovery.pending && cancelled.interop_.captures == capturesBeforeCancel, "disabled NR still polls or recovers");
		Renderer::State unrelatedTimeout;
		unrelatedTimeout.interop_.operation = "ID3D11DeviceContext4::Signal";
		unrelatedTimeout.FailLocked(RendererStage::CommandBegin, timeoutResult, "other timeout", 1, true);
		Require(!unrelatedTimeout.snapshot_.stallRecovery.pending, "a timeout outside bounded backpressure became recoverable");
		std::cout << "Failure diagnostics and guarded recovery preserve completion, cooldown, bounded attempts and quarantine\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
