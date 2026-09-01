#include "Api/UpscalingService.h"

#include "Api/MainThreadDispatchPolicy.h"
#include "Api/RuntimeThreadAffinity.h"
#include "Api/ServiceRegistry.h"
#include "Api/UpscalingContract.h"
#include "Api/UpscalingServicePolicy.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "State.h"
#include "VRAPI/CSserviceapi.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <deque>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace
{
	using namespace CSX;
	using namespace CSX::UpscalingAPI;
	using Clock = std::chrono::steady_clock;

	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr std::size_t kMaximumCommands = 1024;
	constexpr std::size_t kMaximumEvents = 4096;
	constexpr auto kCommandRetention = std::chrono::hours(1);
	constexpr std::uint32_t kMaximumEventPageSize = 500;

	std::uint64_t Bit(std::uint32_t a_value)
	{
		return 1ull << a_value;
	}

	Method ToAPI(Upscaling::UpscaleMethod a_method)
	{
		switch (a_method) {
		case Upscaling::UpscaleMethod::kTAA:
			return Method::kTAA;
		case Upscaling::UpscaleMethod::kFSR:
			return Method::kFSR;
		case Upscaling::UpscaleMethod::kDLSS:
			return Method::kDLSS;
		case Upscaling::UpscaleMethod::kNONE:
		default:
			return Method::kNone;
		}
	}

	Upscaling::UpscaleMethod FromAPI(Method a_method)
	{
		switch (a_method) {
		case Method::kTAA:
			return Upscaling::UpscaleMethod::kTAA;
		case Method::kFSR:
			return Upscaling::UpscaleMethod::kFSR;
		case Method::kDLSS:
			return Upscaling::UpscaleMethod::kDLSS;
		case Method::kNone:
		default:
			return Upscaling::UpscaleMethod::kNONE;
		}
	}

	Profile001 MakeProfile(
		Upscaling::UpscaleMethod a_method,
		std::uint32_t a_qualityMode,
		bool a_renderScaleMode,
		std::uint32_t a_dlssProfile,
		bool a_fsr4Runtime)
	{
		Profile001 profile;
		profile.method = ToAPI(a_method);
		profile.qualityMode = static_cast<QualityMode>(
			std::min(a_qualityMode, Upscaling::kQualityModeMaxIndex));
		profile.renderScaleMode = a_renderScaleMode ? 1u : 0u;
		profile.dlssProfile = static_cast<DLSSProfile>(
			Upscaling::ClampDLSSPresetUInt(a_dlssProfile));
		profile.fsrRuntime = a_fsr4Runtime ? FSRRuntime::kFSR4 : FSRRuntime::kFSR3;
		return profile;
	}

	Profile001 MakeProfile(const Upscaling::VRRenderScaleProfileSnapshot& a_profile)
	{
		return MakeProfile(
			a_profile.method,
			a_profile.qualityMode,
			a_profile.renderScaleModeEnabled,
			a_profile.dlssPreset,
			a_profile.fsr4RuntimeEnabled);
	}

	bool ProfilesEqual(const Profile001& a_left, const Profile001& a_right)
	{
		return a_left.method == a_right.method &&
		       a_left.qualityMode == a_right.qualityMode &&
		       a_left.renderScaleMode == a_right.renderScaleMode &&
		       a_left.dlssProfile == a_right.dlssProfile &&
		       a_left.fsrRuntime == a_right.fsrRuntime;
	}

	TransitionState ToAPI(Upscaling::VRRenderScaleTransitionState a_state)
	{
		switch (a_state) {
		case Upscaling::VRRenderScaleTransitionState::Requested:
			return TransitionState::kRequested;
		case Upscaling::VRRenderScaleTransitionState::WaitingForSafePoint:
			return TransitionState::kWaitingForSafePoint;
		case Upscaling::VRRenderScaleTransitionState::Preparing:
			return TransitionState::kPreparing;
		case Upscaling::VRRenderScaleTransitionState::Applying:
			return TransitionState::kApplying;
		case Upscaling::VRRenderScaleTransitionState::Stabilizing:
			return TransitionState::kStabilizing;
		case Upscaling::VRRenderScaleTransitionState::Active:
			return TransitionState::kActive;
		case Upscaling::VRRenderScaleTransitionState::Idle:
		default:
			return TransitionState::kIdle;
		}
	}

	RenderScaleStatus ToAPI(Upscaling::VRRenderScaleStatus a_status)
	{
		return static_cast<RenderScaleStatus>(static_cast<std::uint32_t>(a_status));
	}

	OperationState ToOperationState(Upscaling::VRRenderScaleTransitionState a_state)
	{
		switch (a_state) {
		case Upscaling::VRRenderScaleTransitionState::Requested:
			return OperationState::kQueued;
		case Upscaling::VRRenderScaleTransitionState::WaitingForSafePoint:
			return OperationState::kWaitingForSafePoint;
		case Upscaling::VRRenderScaleTransitionState::Preparing:
			return OperationState::kPreparing;
		case Upscaling::VRRenderScaleTransitionState::Applying:
			return OperationState::kApplying;
		case Upscaling::VRRenderScaleTransitionState::Stabilizing:
		case Upscaling::VRRenderScaleTransitionState::Active:
			return OperationState::kStabilizing;
		case Upscaling::VRRenderScaleTransitionState::Idle:
		default:
			return OperationState::kQueued;
		}
	}

	std::uint64_t TranslateBlockReasons(std::uint32_t a_reasons)
	{
		std::uint64_t conditions = kConditionNone;
		if ((a_reasons & Upscaling::kVRUpscalingApplyBlockRaceSexMenu) != 0)
			conditions |= kConditionRaceSexMenu;
		if ((a_reasons & Upscaling::kVRUpscalingApplyBlockRaceSexStartupTail) != 0)
			conditions |= kConditionRaceSexStartupTail;
		if ((a_reasons & Upscaling::kVRUpscalingApplyBlockLoadingMenu) != 0)
			conditions |= kConditionLoadingTransition;
		if ((a_reasons & Upscaling::kVRUpscalingApplyBlockRelatchPending) != 0)
			conditions |= kConditionRelatchPending;
		if ((a_reasons & Upscaling::kVRUpscalingApplyBlockTransitionPending) != 0)
			conditions |= kConditionTransitionPending;
		if ((a_reasons & Upscaling::kVRUpscalingApplyBlockOpenComposite) != 0)
			conditions |= kConditionOpenCompositeUpscaling;
		if ((a_reasons &
				Upscaling::kVRUpscalingApplyBlockStartupNativeFallback) != 0) {
			conditions |= kConditionRestartRequired;
		}
		return conditions;
	}

	bool SnapshotsEqual(const Snapshot001& a_left, const Snapshot001& a_right)
	{
		return a_left.capabilityRevision == a_right.capabilityRevision &&
		       a_left.profilePresence == a_right.profilePresence &&
		       a_left.flags == a_right.flags &&
		       a_left.observedConditions == a_right.observedConditions &&
		       a_left.transitionState == a_right.transitionState &&
		       a_left.renderScaleStatus == a_right.renderScaleStatus &&
		       a_left.activeOperationId == a_right.activeOperationId &&
		       ProfilesEqual(a_left.configured, a_right.configured) &&
		       ProfilesEqual(a_left.requested, a_right.requested) &&
		       ProfilesEqual(a_left.applying, a_right.applying) &&
		       ProfilesEqual(a_left.effective, a_right.effective) &&
		       ProfilesEqual(a_left.stable, a_right.stable) &&
		       ProfilesEqual(a_left.persisted, a_right.persisted) &&
		       a_left.displayEyeWidth == a_right.displayEyeWidth &&
		       a_left.displayEyeHeight == a_right.displayEyeHeight &&
		       a_left.renderEyeWidth == a_right.renderEyeWidth &&
		       a_left.renderEyeHeight == a_right.renderEyeHeight;
	}

	bool CapabilitiesEqual(const Capabilities001& a_left, const Capabilities001& a_right)
	{
		if (a_left.runtime != a_right.runtime ||
			a_left.capabilities != a_right.capabilities ||
			a_left.supportedMethodMask != a_right.supportedMethodMask ||
			a_left.availableMethodMask != a_right.availableMethodMask ||
			a_left.pendingMethodMask != a_right.pendingMethodMask ||
			a_left.supportedQualityModeMask != a_right.supportedQualityModeMask ||
			a_left.supportedDLSSProfileMask != a_right.supportedDLSSProfileMask ||
			a_left.supportedFSRRuntimeMask != a_right.supportedFSRRuntimeMask) {
			return false;
		}
		for (std::size_t i = 0; i < 4; ++i) {
			if (a_left.methodUnavailableConditions[i] != a_right.methodUnavailableConditions[i])
				return false;
		}
		for (std::size_t i = 0; i < 2; ++i) {
			if (a_left.fsrRuntimeUnavailableConditions[i] != a_right.fsrRuntimeUnavailableConditions[i])
				return false;
		}
		return true;
	}

	struct CommandSignature
	{
		std::string clientId;
		std::string commandId;
		std::string reason;
		std::uint64_t expectedStateRevision = AnyStateRevision;
		RequestPurpose purpose = RequestPurpose::kDirect;
		PersistencePolicy persistence = PersistencePolicy::kRuntimeOnly;
		Profile001 target{};

		bool SameRequest(const CommandSignature& a_other) const
		{
			return clientId == a_other.clientId && commandId == a_other.commandId &&
			       reason == a_other.reason && expectedStateRevision == a_other.expectedStateRevision &&
			       purpose == a_other.purpose && persistence == a_other.persistence &&
			       ProfilesEqual(target, a_other.target);
		}
	};

	struct StoredCommand
	{
		CommandSignature signature;
		Status status = Status::kBusy;
		ApplyResult001 result{};
		Clock::time_point createdAt = Clock::now();
		bool ready = false;
	};

	struct LiveOperation
	{
		OperationSnapshot001 snapshot{};
		std::string commandKey;
		CommandSignature command;
		std::uint64_t rendererRequestId = 0;
		std::uint64_t rendererTransitionEpoch = 0;
	};

	class UpscalingServiceBundle
	{
	public:
		const Interface001* GetInterface() const noexcept
		{
			return &serviceInterface;
		}

		Status GetCapabilities(Capabilities001& a_output)
		{
			if (const auto refreshStatus = RefreshFromAnyThread(); refreshStatus != Status::kSuccess)
				return refreshStatus;
			std::lock_guard lock(mutex);
			a_output = capabilities;
			return Status::kSuccess;
		}

		Status GetSnapshot(Snapshot001& a_output)
		{
			if (const auto refreshStatus = RefreshFromAnyThread(); refreshStatus != Status::kSuccess)
				return refreshStatus;
			std::lock_guard lock(mutex);
			a_output = snapshot;
			return Status::kSuccess;
		}

		Status Preflight(const PreflightRequest001& a_request, PreflightResult001& a_output)
		{
			const auto validation = CSX::Api::ValidateUpscalingPreflightRequest(a_request);
			if (validation != Status::kSuccess)
				return validation;
			auto dispatch = RunOnMainThread<PreflightEvaluation>([this, request = a_request] {
				RefreshLive();
				return EvaluatePreflight(request);
			});
			if (!dispatch.value)
				return dispatch.admitted ? Status::kBusy : Status::kServiceUnavailable;
			a_output = dispatch.value->result;
			return dispatch.value->status;
		}

		Status Apply(const ApplyRequest001& a_request, ApplyResult001& a_output)
		{
			const auto validation = CSX::Api::ValidateUpscalingApplyRequest(a_request);
			if (validation != Status::kSuccess)
				return validation;

			CommandSignature signature{
				.clientId = a_request.clientId,
				.commandId = a_request.commandId,
				.reason = a_request.reason ? a_request.reason : "",
				.expectedStateRevision = a_request.expectedStateRevision,
				.purpose = a_request.purpose,
				.persistence = a_request.persistence,
				.target = a_request.target,
			};
			const std::string key = signature.clientId + '\n' + signature.commandId;
			{
				std::lock_guard lock(mutex);
				TrimLocked();
				if (const auto found = commands.find(key); found != commands.end()) {
					if (!found->second.signature.SameRequest(signature)) {
						a_output = ApplyResult001{};
						a_output.status = Status::kIdempotencyConflict;
						a_output.disposition = ApplyDisposition::kRejected;
						a_output.normalizedTarget = a_request.target;
						return Status::kIdempotencyConflict;
					}
					a_output = found->second.result;
					a_output.idempotentReplay = 1;
					return found->second.ready ? found->second.status : Status::kBusy;
				}
				if (!CSX::Api::HasUpscalingServiceCapacity(
						commands.size(), operations.size(), pendingOperationReservations, kMaximumCommands)) {
					a_output = ApplyResult001{};
					a_output.status = Status::kBusy;
					a_output.disposition = ApplyDisposition::kRejected;
					a_output.normalizedTarget = a_request.target;
					return Status::kBusy;
				}
				StoredCommand reservation;
				reservation.signature = signature;
				reservation.result.status = Status::kBusy;
				reservation.result.disposition = ApplyDisposition::kRejected;
				const auto [inserted, didInsert] = commands.emplace(key, std::move(reservation));
				assert(didInsert);
				try {
					commandOrder.push_back(key);
				} catch (...) {
					commands.erase(inserted);
					throw;
				}
				++pendingOperationReservations;
			}
			std::uint64_t operationId = 0;
			bool commandTerminal = false;
			bool ownsOperationReservation = true;
			const SKSE::stl::scope_exit releaseOperationReservation([&]() noexcept {
				if (!ownsOperationReservation)
					return;
				std::lock_guard lock(mutex);
				assert(pendingOperationReservations != 0);
				--pendingOperationReservations;
			});
			const SKSE::stl::scope_exit rollbackUnreadyCommand([&]() noexcept {
				if (!commandTerminal)
					DiscardUnreadyAdmission(key, operationId);
			});
			const auto completeCommand = [&](Status a_status, const ApplyResult001& a_result) {
				CompleteCommand(key, a_status, a_result);
				commandTerminal = true;
			};

			PreflightRequest001 preflightRequest;
			preflightRequest.expectedStateRevision = signature.expectedStateRevision;
			preflightRequest.purpose = signature.purpose;
			preflightRequest.persistence = signature.persistence;
			preflightRequest.target = signature.target;
			PreflightResult001 preflight;
			const auto preflightStatus = Preflight(preflightRequest, preflight);

			ApplyResult001 receipt;
			receipt.status = preflightStatus;
			receipt.admissionRoute = preflight.admissionRoute;
			receipt.admittedStateRevision = preflight.evaluatedStateRevision;
			receipt.resultingStateRevision = preflight.evaluatedStateRevision;
			receipt.observedConditions = preflight.observedConditions;
			receipt.blockingConditions = preflight.blockingConditions;
			receipt.retryable = preflight.retryable;
			receipt.requiresRestart = preflight.requiresRestart;
			receipt.willPersist = preflight.willPersist;
			receipt.normalizedTarget = preflight.normalizedTarget;

			if (preflightStatus != Status::kSuccess ||
				preflight.decision == PreflightDecision::kBlocked ||
				preflight.decision == PreflightDecision::kUnsupported) {
				receipt.disposition = ApplyDisposition::kRejected;
				if (preflightStatus == Status::kSuccess)
					receipt.status = preflight.decision == PreflightDecision::kUnsupported ?
					                     Status::kUnsupportedProfile :
					                     Status::kBlocked;
				completeCommand(receipt.status, receipt);
				a_output = receipt;
				return receipt.status;
			}

			if (preflight.decision == PreflightDecision::kNoChange) {
				receipt.status = Status::kSuccess;
				receipt.disposition = ApplyDisposition::kNoChange;
				{
					std::lock_guard lock(mutex);
					latestAdmittedTarget = signature.target;
				}
				if (RefreshFromAnyThread() == Status::kSuccess) {
					std::lock_guard lock(mutex);
					receipt.resultingStateRevision = snapshot.stateRevision;
				}
				completeCommand(receipt.status, receipt);
				a_output = receipt;
				return Status::kSuccess;
			}

			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				receipt.status = Status::kServiceUnavailable;
				receipt.disposition = ApplyDisposition::kRejected;
				completeCommand(receipt.status, receipt);
				a_output = receipt;
				return receipt.status;
			}

			{
				std::lock_guard lock(mutex);
				operationId = AllocateOperationIdLocked();
				LiveOperation operation;
				operation.commandKey = key;
				operation.command = signature;
				operation.snapshot.operationId = operationId;
				operation.snapshot.state = OperationState::kQueued;
				operation.snapshot.result = Status::kSuccess;
				operation.snapshot.flags = signature.persistence == PersistencePolicy::kPersistWhenStable ?
				                               kOperationPersistenceRequested :
				                               0;
				operation.snapshot.acceptedStateRevision = preflight.evaluatedStateRevision;
				operation.snapshot.latestStateRevision = preflight.evaluatedStateRevision;
				operation.snapshot.target = signature.target;
				operations.emplace(operationId, std::move(operation));
				assert(pendingOperationReservations != 0);
				--pendingOperationReservations;
				ownsOperationReservation = false;
				AppendEventLocked(operationId, EventType::kAccepted, OperationState::kQueued, Status::kSuccess);
				AppendEventLocked(operationId, EventType::kQueued, OperationState::kQueued, Status::kSuccess);
			}

			receipt.status = Status::kSuccess;
			receipt.disposition = ApplyDisposition::kQueued;
			receipt.operationId = operationId;
			try {
				tasks->AddTask([this, operationId] {
					CSX::Api::EnterRuntimeMainThreadTask();
					ExecuteOperation(operationId);
				});
			} catch (...) {
				receipt.status = Status::kServiceUnavailable;
				receipt.disposition = ApplyDisposition::kRejected;
				receipt.operationId = 0;
				FailOperation(operationId, receipt.status, kConditionNone);
				completeCommand(receipt.status, receipt);
				a_output = receipt;
				return receipt.status;
			}
			completeCommand(Status::kSuccess, receipt);
			a_output = receipt;
			return Status::kSuccess;
		}

		Status GetOperation(std::uint64_t a_operationId, OperationSnapshot001& a_output)
		{
			if (a_operationId == 0)
				return Status::kInvalidArgument;
			if (const auto refreshStatus = RefreshFromAnyThread(); refreshStatus != Status::kSuccess)
				return refreshStatus;
			std::lock_guard lock(mutex);
			const auto found = operations.find(a_operationId);
			if (found == operations.end())
				return Status::kOperationNotFound;
			a_output = found->second.snapshot;
			return Status::kSuccess;
		}

		Status ReadEvents(
			const EventQuery001& a_query,
			Event001* a_events,
			std::uint32_t a_capacity,
			EventPage001& a_page)
		{
			if (a_query.limit == 0 || (a_capacity != 0 && !a_events))
				return Status::kInvalidArgument;
			if (const auto refreshStatus = RefreshFromAnyThread(); refreshStatus != Status::kSuccess)
				return refreshStatus;

			std::lock_guard lock(mutex);
			a_page = EventPage001{};
			const std::uint32_t limit = std::min({ a_query.limit, a_capacity, kMaximumEventPageSize });
			a_page.oldestRetainedEventId = events.empty() ? nextEventId : events.front().eventId;
			a_page.latestEventId = nextEventId - 1;
			a_page.cursorExpired = a_query.afterEventId != 0 &&
			                       a_query.afterEventId + 1 < a_page.oldestRetainedEventId;
			for (const auto& event : events) {
				if (event.eventId <= a_query.afterEventId ||
					(a_query.operationId != 0 && event.operationId != a_query.operationId)) {
					continue;
				}
				if (a_page.returnedEventCount == limit) {
					a_page.moreAvailable = 1;
					break;
				}
				if (a_events[a_page.returnedEventCount].structSize < sizeof(Event001))
					return Status::kStructureTooSmall;
				a_events[a_page.returnedEventCount] = event;
				a_page.nextEventId = event.eventId;
				++a_page.returnedEventCount;
			}
			if (a_page.returnedEventCount == 0)
				a_page.nextEventId = a_query.afterEventId;
			return Status::kSuccess;
		}

	private:
		struct PreflightEvaluation
		{
			Status status = Status::kInternalError;
			PreflightResult001 result{};
		};

		std::mutex mutex;
		Capabilities001 capabilities{};
		Snapshot001 snapshot{};
		std::uint64_t stateRevision = 0;
		std::uint64_t capabilityRevision = 0;
		std::uint64_t nextOperationId = 1;
		std::uint64_t nextEventId = 1;
		std::size_t pendingOperationReservations = 0;
		std::unordered_map<std::string, StoredCommand> commands;
		std::deque<std::string> commandOrder;
		std::unordered_map<std::uint64_t, LiveOperation> operations;
		std::deque<Event001> events;
		std::optional<Profile001> latestAdmittedTarget;

		static Status GetCapabilitiesThunk(const void* a_context, Capabilities001* a_output) noexcept
		{
			if (!a_context || !a_output)
				return Status::kInvalidArgument;
			if (a_output->structSize < sizeof(Capabilities001))
				return Status::kStructureTooSmall;
			try {
				return const_cast<UpscalingServiceBundle*>(static_cast<const UpscalingServiceBundle*>(a_context))->GetCapabilities(*a_output);
			} catch (...) {
				return Status::kInternalError;
			}
		}

		static Status GetSnapshotThunk(const void* a_context, Snapshot001* a_output) noexcept
		{
			if (!a_context || !a_output)
				return Status::kInvalidArgument;
			if (a_output->structSize < sizeof(Snapshot001))
				return Status::kStructureTooSmall;
			try {
				return const_cast<UpscalingServiceBundle*>(static_cast<const UpscalingServiceBundle*>(a_context))->GetSnapshot(*a_output);
			} catch (...) {
				return Status::kInternalError;
			}
		}

		static Status PreflightThunk(const void* a_context, const PreflightRequest001* a_request, PreflightResult001* a_output) noexcept
		{
			if (!a_context || !a_request || !a_output)
				return Status::kInvalidArgument;
			if (a_output->structSize < sizeof(PreflightResult001))
				return Status::kStructureTooSmall;
			try {
				return const_cast<UpscalingServiceBundle*>(static_cast<const UpscalingServiceBundle*>(a_context))->Preflight(*a_request, *a_output);
			} catch (...) {
				return Status::kInternalError;
			}
		}

		static Status ApplyThunk(const void* a_context, const ApplyRequest001* a_request, ApplyResult001* a_output) noexcept
		{
			if (!a_context || !a_request || !a_output)
				return Status::kInvalidArgument;
			if (a_output->structSize < sizeof(ApplyResult001))
				return Status::kStructureTooSmall;
			try {
				return const_cast<UpscalingServiceBundle*>(static_cast<const UpscalingServiceBundle*>(a_context))->Apply(*a_request, *a_output);
			} catch (...) {
				return Status::kInternalError;
			}
		}

		static Status GetOperationThunk(const void* a_context, std::uint64_t a_operationId, OperationSnapshot001* a_output) noexcept
		{
			if (!a_context || !a_output)
				return Status::kInvalidArgument;
			if (a_output->structSize < sizeof(OperationSnapshot001))
				return Status::kStructureTooSmall;
			try {
				return const_cast<UpscalingServiceBundle*>(static_cast<const UpscalingServiceBundle*>(a_context))->GetOperation(a_operationId, *a_output);
			} catch (...) {
				return Status::kInternalError;
			}
		}

		static Status ReadEventsThunk(const void* a_context, const EventQuery001* a_query, Event001* a_events, std::uint32_t a_capacity, EventPage001* a_page) noexcept
		{
			if (!a_context || !a_query || !a_page)
				return Status::kInvalidArgument;
			if (a_query->structSize < sizeof(EventQuery001) || a_page->structSize < sizeof(EventPage001))
				return Status::kStructureTooSmall;
			try {
				return const_cast<UpscalingServiceBundle*>(static_cast<const UpscalingServiceBundle*>(a_context))->ReadEvents(*a_query, a_events, a_capacity, *a_page);
			} catch (...) {
				return Status::kInternalError;
			}
		}

		inline static Interface001 serviceInterface{
			.structSize = sizeof(Interface001),
			.abiMajor = ServiceMajor,
			.abiMinor = ServiceMinor,
			.context = nullptr,
			.GetCapabilities = GetCapabilitiesThunk,
			.GetSnapshot = GetSnapshotThunk,
			.PreflightProfile = PreflightThunk,
			.ApplyProfile = ApplyThunk,
			.GetOperation = GetOperationThunk,
			.ReadEvents = ReadEventsThunk,
		};

		template <class T>
		struct MainThreadDispatchResult
		{
			std::optional<T> value;
			bool admitted = false;
		};

		template <class T, class F>
		MainThreadDispatchResult<T> RunOnMainThread(F&& a_run)
		{
			if (CSX::Api::IsRuntimeMainThread())
				return { a_run(), false };
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks)
				return {};
			auto promise = std::make_shared<std::promise<T>>();
			auto claim = std::make_shared<CSX::Api::MainThreadDispatchClaim>();
			auto future = promise->get_future();
			tasks->AddTask([promise, claim, run = std::forward<F>(a_run)]() mutable {
				CSX::Api::EnterRuntimeMainThreadTask();
				if (!claim->TryClaim())
					return;
				try {
					promise->set_value(run());
				} catch (...) {
					try {
						promise->set_exception(std::current_exception());
					} catch (...) {
					}
				}
				claim->Complete();
			});
			if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
				if (claim->TryCancel())
					return {};
				return { std::nullopt, true };
			}
			try {
				return { future.get(), false };
			} catch (...) {
				return {};
			}
		}

		Status RefreshFromAnyThread()
		{
			auto dispatch = RunOnMainThread<bool>([this] {
				RefreshLive();
				return true;
			});
			if (dispatch.value)
				return Status::kSuccess;
			return dispatch.admitted ? Status::kBusy : Status::kServiceUnavailable;
		}

		Capabilities001 BuildCapabilities() const
		{
			Capabilities001 output;
			output.runtime = globals::game::isVR ? RuntimeKind::kSkyrimVR :
			                 REL::Module::IsAE() ? RuntimeKind::kSkyrimAE :
			                                       RuntimeKind::kSkyrimSE;
			output.capabilities =
				kCapabilityInspection |
				kCapabilityAtomicProfileMutation |
				kCapabilityAsynchronousTransitions |
				kCapabilityOptimisticConcurrency |
				kCapabilityIdempotentCommands |
				kCapabilityEventJournal |
				kCapabilityFSRRuntimeSelection;
			if (globals::game::isVR)
				output.capabilities |= kCapabilityVRRenderScaleMode;

			output.supportedMethodMask =
				Bit(static_cast<std::uint32_t>(Method::kNone)) |
				Bit(static_cast<std::uint32_t>(Method::kTAA)) |
				Bit(static_cast<std::uint32_t>(Method::kFSR)) |
				Bit(static_cast<std::uint32_t>(Method::kDLSS));
			output.availableMethodMask =
				Bit(static_cast<std::uint32_t>(Method::kNone)) |
				Bit(static_cast<std::uint32_t>(Method::kTAA)) |
				Bit(static_cast<std::uint32_t>(Method::kFSR));
			if (!Upscaling::streamline.featureCheckComplete) {
				output.pendingMethodMask |= Bit(static_cast<std::uint32_t>(Method::kDLSS));
				output.methodUnavailableConditions[static_cast<std::uint32_t>(Method::kDLSS)] =
					kConditionProviderCheckPending;
			} else if (Upscaling::streamline.featureDLSS) {
				output.availableMethodMask |= Bit(static_cast<std::uint32_t>(Method::kDLSS));
			} else {
				output.methodUnavailableConditions[static_cast<std::uint32_t>(Method::kDLSS)] =
					kConditionProviderUnavailable;
			}

			for (std::uint32_t value = 0; value <= Upscaling::kQualityModeMaxIndex; ++value) {
				output.supportedQualityModeMask |= Bit(value);
				output.qualityResolutionScales[value] = Upscaling::GetQualityModeResolutionScale(value);
			}
			for (std::uint32_t value = 0; value <= Upscaling::kDLSSPresetMaxIndex; ++value)
				output.supportedDLSSProfileMask |= Bit(value);
			output.supportedFSRRuntimeMask =
				Bit(static_cast<std::uint32_t>(FSRRuntime::kFSR3)) |
				Bit(static_cast<std::uint32_t>(FSRRuntime::kFSR4));
			if (!globals::features::upscaling.fidelityFX.IsRuntimeFsr4Available()) {
				output.fsrRuntimeUnavailableConditions[static_cast<std::uint32_t>(FSRRuntime::kFSR4)] =
					globals::features::upscaling.fidelityFX.HasRuntimeUpscalerSupportCheckResult() ?
						kConditionProviderUnavailable :
						kConditionProviderCheckPending;
			}
			output.maximumEventPageSize = kMaximumEventPageSize;
			output.eventRetentionCapacity = static_cast<std::uint32_t>(kMaximumEvents);
			output.commandRetentionMilliseconds =
				std::chrono::duration_cast<std::chrono::milliseconds>(kCommandRetention).count();
			return output;
		}

		Snapshot001 BuildSnapshot(
			const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
			const std::optional<Profile001>& a_latestAdmittedTarget) const
		{
			auto& upscaling = globals::features::upscaling;
			Snapshot001 output;
			output.profilePresence = kProfileConfigured | kProfileEffective;
			output.configured = MakeProfile(
				upscaling.GetConfiguredUpscaleMethodForTransition(),
				upscaling.settings.qualityMode,
				upscaling.settings.renderScaleMode != 0,
				upscaling.settings.dlssPreset,
				upscaling.settings.fsr4RuntimeEnable);
			output.effective = MakeProfile(
				upscaling.GetRuntimeUpscaleMethod(),
				upscaling.GetRuntimeQualityMode(),
				upscaling.IsVRRenderScaleModeActive(),
				upscaling.GetRuntimeDLSSPreset(),
				upscaling.GetRuntimeFSR4Enabled());
			if (a_controller.requested.valid) {
				output.profilePresence |= kProfileRequested;
				output.requested = MakeProfile(a_controller.requested);
			}
			if (a_controller.applying.valid) {
				output.profilePresence |= kProfileApplying;
				output.applying = MakeProfile(a_controller.applying);
			}
			if (a_controller.stable.valid) {
				output.profilePresence |= kProfileStable;
				output.stable = MakeProfile(a_controller.stable);
				output.displayEyeWidth = a_controller.stable.displayEyeWidth;
				output.displayEyeHeight = a_controller.stable.displayEyeHeight;
				output.renderEyeWidth = a_controller.stable.renderEyeWidth;
				output.renderEyeHeight = a_controller.stable.renderEyeHeight;
			} else {
				output.displayEyeWidth = upscaling.perfMode.trueHMDEyeWidth;
				output.displayEyeHeight = upscaling.perfMode.trueHMDEyeHeight;
				const auto plan = upscaling.GetRuntimeResolutionPlan();
				output.renderEyeWidth = static_cast<std::uint32_t>(plan.engineRenderSize.x);
				output.renderEyeHeight = static_cast<std::uint32_t>(plan.engineRenderSize.y);
			}
			const bool controllerSettled =
				a_controller.state == Upscaling::VRRenderScaleTransitionState::Idle ||
				a_controller.state == Upscaling::VRRenderScaleTransitionState::Active;
			if (a_latestAdmittedTarget &&
				controllerSettled &&
				ProfilesEqual(output.effective, *a_latestAdmittedTarget) &&
				((output.profilePresence & kProfileStable) == 0 ||
					ProfilesEqual(output.stable, *a_latestAdmittedTarget))) {
				output.profilePresence |= kProfileRequested;
				output.requested = *a_latestAdmittedTarget;
			}
			output.transitionState = ToAPI(a_controller.state);
			output.renderScaleStatus = ToAPI(upscaling.GetVRRenderScaleModeStatus());
			output.observedConditions = TranslateBlockReasons(upscaling.GetVRUpscalingApplyBlockReasonsForAPI());
			if (Upscaling::streamline.featureCheckComplete)
				output.flags |= kSnapshotProviderCheckComplete;
			if (a_controller.state != Upscaling::VRRenderScaleTransitionState::Idle &&
				a_controller.state != Upscaling::VRRenderScaleTransitionState::Active)
				output.flags |= kSnapshotTransitionActive;
			if (upscaling.perfMode.HasRestartRequiredChange())
				output.flags |= kSnapshotRestartRequired;
			if (upscaling.IsRenderScaleModeRequested())
				output.flags |= kSnapshotRenderScaleRequested;
			if (upscaling.IsVRRenderScaleModeLatched())
				output.flags |= kSnapshotRenderScaleLatched;
			if (upscaling.IsVRRenderScaleModeActive())
				output.flags |= kSnapshotRenderScaleActive;
			return output;
		}

		void RefreshLive()
		{
			auto& upscaling = globals::features::upscaling;
			const auto controller = upscaling.GetVRRenderScaleTransitionSnapshot();
			std::optional<Profile001> admittedTarget;
			{
				std::lock_guard lock(mutex);
				admittedTarget = latestAdmittedTarget;
			}
			auto nextCapabilities = BuildCapabilities();
			auto nextSnapshot = BuildSnapshot(controller, admittedTarget);

			std::lock_guard lock(mutex);
			if (capabilityRevision == 0 || !CapabilitiesEqual(capabilities, nextCapabilities))
				++capabilityRevision;
			nextCapabilities.revision = capabilityRevision;
			nextSnapshot.capabilityRevision = capabilityRevision;
			UpdateOperationsLocked(controller, nextSnapshot);
			for (const auto& [operationId, operation] : operations) {
				if (!CSX::Api::IsTerminalUpscalingOperation(operation.snapshot.state) &&
					operation.rendererRequestId != 0) {
					nextSnapshot.activeOperationId = std::max(nextSnapshot.activeOperationId, operationId);
				}
			}
			if (stateRevision == 0 || !SnapshotsEqual(snapshot, nextSnapshot))
				++stateRevision;
			nextSnapshot.stateRevision = stateRevision;
			capabilities = nextCapabilities;
			snapshot = nextSnapshot;
			for (auto& [operationId, operation] : operations)
				operation.snapshot.latestStateRevision = stateRevision;
			TrimLocked();
		}

		PreflightEvaluation EvaluatePreflight(const PreflightRequest001& a_request)
		{
			PreflightEvaluation evaluation;
			Snapshot001 currentSnapshot;
			Capabilities001 currentCapabilities;
			{
				std::lock_guard lock(mutex);
				currentSnapshot = snapshot;
				currentCapabilities = capabilities;
			}
			auto& result = evaluation.result;
			result.evaluatedStateRevision = currentSnapshot.stateRevision;
			result.normalizedTarget = a_request.target;
			result.predictedDisplayEyeWidth = currentSnapshot.displayEyeWidth;
			result.predictedDisplayEyeHeight = currentSnapshot.displayEyeHeight;
			const float scale = Upscaling::GetQualityModeResolutionScale(
				static_cast<std::uint32_t>(a_request.target.qualityMode));
			result.predictedRenderEyeWidth = a_request.target.renderScaleMode ?
			                                     Upscaling::ScaleVRRenderDimension(currentSnapshot.displayEyeWidth, scale) :
			                                     currentSnapshot.displayEyeWidth;
			result.predictedRenderEyeHeight = a_request.target.renderScaleMode ?
			                                      Upscaling::ScaleVRRenderDimension(currentSnapshot.displayEyeHeight, scale) :
			                                      currentSnapshot.displayEyeHeight;

			if (a_request.expectedStateRevision != AnyStateRevision &&
				a_request.expectedStateRevision != currentSnapshot.stateRevision) {
				evaluation.status = Status::kStateConflict;
				result.decision = PreflightDecision::kBlocked;
				result.retryable = 1;
				return evaluation;
			}
			if (!globals::game::isVR && a_request.target.renderScaleMode) {
				evaluation.status = Status::kUnsupportedRuntime;
				result.decision = PreflightDecision::kUnsupported;
				return evaluation;
			}

			std::uint64_t observed = currentSnapshot.observedConditions;
			const auto methodIndex = static_cast<std::uint32_t>(a_request.target.method);
			const auto methodBit = Bit(methodIndex);
			if ((currentCapabilities.pendingMethodMask & methodBit) != 0)
				observed |= kConditionProviderCheckPending;
			else if ((currentCapabilities.availableMethodMask & methodBit) == 0)
				observed |= kConditionProviderUnavailable;
			if (a_request.target.method == Method::kFSR &&
				a_request.target.fsrRuntime == FSRRuntime::kFSR4) {
				observed |= currentCapabilities.fsrRuntimeUnavailableConditions[static_cast<std::uint32_t>(FSRRuntime::kFSR4)];
			}
			const auto admission = CSX::Api::ResolveUpscalingAdmission(
				observed,
				a_request.purpose,
				a_request.persistence,
				false);
			result.observedConditions = admission.observedConditions;
			result.blockingConditions = admission.blockingConditions;
			result.admissionRoute = admission.route;
			result.willPersist = 0;
			result.requiresRestart = (currentSnapshot.flags & kSnapshotRestartRequired) != 0;
			if (admission.blockingConditions != 0) {
				result.decision =
					(admission.blockingConditions & (kConditionProviderCheckPending | kConditionProviderUnavailable)) != 0 ?
						PreflightDecision::kUnsupported :
						PreflightDecision::kBlocked;
				result.retryable =
					(admission.blockingConditions & (kConditionOpenCompositeUpscaling | kConditionProviderUnavailable | kConditionPersistenceUnavailable)) == 0;
				evaluation.status = Status::kSuccess;
				return evaluation;
			}

			const bool effectiveMatches = ProfilesEqual(currentSnapshot.effective, a_request.target);
			const bool stableMatches =
				(currentSnapshot.profilePresence & kProfileStable) == 0 ||
				ProfilesEqual(currentSnapshot.stable, a_request.target);
			const bool transitionActive = (currentSnapshot.flags & kSnapshotTransitionActive) != 0;
			if (CSX::Api::IsUpscalingRuntimeNoChange(
					transitionActive,
					effectiveMatches,
					stableMatches)) {
				result.decision = PreflightDecision::kNoChange;
				result.admissionRoute = AdmissionRoute::kDirect;
			} else if (globals::game::isVR) {
				result.decision = PreflightDecision::kQueue;
				if (result.admissionRoute == AdmissionRoute::kDirect)
					result.admissionRoute = AdmissionRoute::kDeferredSafePoint;
			} else {
				result.decision = PreflightDecision::kApplySynchronously;
				result.admissionRoute = AdmissionRoute::kDirect;
			}
			result.retryable = 0;
			evaluation.status = Status::kSuccess;
			return evaluation;
		}

		void ExecuteOperation(std::uint64_t a_operationId)
		{
			LiveOperation operation;
			{
				std::lock_guard lock(mutex);
				const auto found = operations.find(a_operationId);
				if (found == operations.end())
					return;
				operation = found->second;
			}

			RefreshLive();
			Snapshot001 current;
			{
				std::lock_guard lock(mutex);
				current = snapshot;
			}
			if (operation.command.expectedStateRevision != AnyStateRevision &&
				operation.command.expectedStateRevision != current.stateRevision) {
				FailOperation(a_operationId, Status::kStateConflict, current.observedConditions);
				return;
			}

			auto& upscaling = globals::features::upscaling;
			const std::uint32_t rawReasons = upscaling.GetVRUpscalingApplyBlockReasonsForAPI();
			std::uint64_t admissionSerial = 0;
			if (operation.command.purpose == RequestPurpose::kEnvironmentProfileTransition)
				admissionSerial = upscaling.CanBufferVRFpsStabilizerAPITransitionProfile(rawReasons);
			if (rawReasons != 0 && admissionSerial == 0) {
				FailOperation(a_operationId, Status::kBlocked, TranslateBlockReasons(rawReasons));
				return;
			}

			const auto method = FromAPI(operation.command.target.method);
			const auto quality = static_cast<std::uint32_t>(operation.command.target.qualityMode);
			const auto dlss = static_cast<std::uint32_t>(operation.command.target.dlssProfile);
			if (globals::game::isVR &&
				operation.command.purpose == RequestPurpose::kEnvironmentProfileTransition &&
				!upscaling.IsVRFpsStabilizerAPITransitionProfileAllowed(
					method,
					operation.command.target.renderScaleMode != 0,
					quality,
					dlss,
					admissionSerial)) {
				upscaling.ClearVRFpsStabilizerAPITransitionProfileAdmission(admissionSerial);
				FailOperation(a_operationId, Status::kBlocked, TranslateBlockReasons(rawReasons));
				return;
			}

			const auto applied = upscaling.ApplyCSMenuUpscalingTransition(
				method,
				operation.command.target.renderScaleMode != 0,
				quality,
				dlss,
				operation.command.reason.c_str(),
				Upscaling::VRUpscalingTransitionOrigin::VRAPI,
				admissionSerial,
				operation.command.target.fsrRuntime == FSRRuntime::kFSR4);
			upscaling.ClearVRFpsStabilizerAPITransitionProfileAdmission(admissionSerial);

			{
				std::lock_guard lock(mutex);
				auto found = operations.find(a_operationId);
				if (found == operations.end())
					return;
				auto& live = found->second;
				switch (applied.disposition) {
				case Upscaling::UpscalingTransitionApplyDisposition::Rejected:
					live.snapshot.state = OperationState::kFailed;
					if (applied.rejection == Upscaling::UpscalingTransitionApplyRejection::OpenComposite) {
						live.snapshot.result = Status::kBlocked;
						live.snapshot.observedConditions |= kConditionOpenCompositeUpscaling;
						live.snapshot.blockingConditions |= kConditionOpenCompositeUpscaling;
					} else if (applied.rejection ==
							   Upscaling::UpscalingTransitionApplyRejection::StartupNativeFallback) {
						live.snapshot.result = Status::kBlocked;
						live.snapshot.observedConditions |= kConditionRestartRequired;
						live.snapshot.blockingConditions |= kConditionRestartRequired;
					} else {
						live.snapshot.result = Status::kBusy;
						live.snapshot.observedConditions |= kConditionTransitionPending;
						live.snapshot.blockingConditions |= kConditionTransitionPending;
					}
					AppendEventLocked(a_operationId, EventType::kFailed, live.snapshot.state, live.snapshot.result);
					break;
				case Upscaling::UpscalingTransitionApplyDisposition::NoChange:
				case Upscaling::UpscalingTransitionApplyDisposition::AppliedSynchronously:
					latestAdmittedTarget = operation.command.target;
					live.snapshot.state = OperationState::kCompleted;
					live.snapshot.result = Status::kSuccess;
					live.snapshot.flags |= kOperationPhysicalStateStable;
					live.snapshot.effective = operation.command.target;
					AppendEventLocked(a_operationId, EventType::kCompleted, live.snapshot.state, Status::kSuccess);
					break;
				case Upscaling::UpscalingTransitionApplyDisposition::Queued:
				case Upscaling::UpscalingTransitionApplyDisposition::Deferred:
				case Upscaling::UpscalingTransitionApplyDisposition::Coalesced:
					live.rendererRequestId = applied.requestID;
					live.rendererTransitionEpoch = applied.transitionEpoch;
					live.snapshot.state = applied.disposition == Upscaling::UpscalingTransitionApplyDisposition::Deferred ?
					                          OperationState::kWaitingForSafePoint :
					                          OperationState::kQueued;
					AppendEventLocked(a_operationId, EventType::kStateChanged, live.snapshot.state, Status::kSuccess);
					break;
				}
			}
			RefreshLive();
		}

		void UpdateOperationsLocked(
			const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
			const Snapshot001& a_liveSnapshot)
		{
			for (auto& [operationId, operation] : operations) {
				if (CSX::Api::IsTerminalUpscalingOperation(operation.snapshot.state) || operation.rendererRequestId == 0)
					continue;
				OperationState next = operation.snapshot.state;
				bool matched = false;
				if (a_controller.stable.valid && a_controller.stable.requestID == operation.rendererRequestId) {
					next = OperationState::kCompleted;
					matched = true;
					latestAdmittedTarget = operation.command.target;
					operation.snapshot.flags |= kOperationPhysicalStateStable;
					operation.snapshot.effective = MakeProfile(a_controller.stable);
				} else if (a_controller.applying.valid && a_controller.applying.requestID == operation.rendererRequestId) {
					next = ToOperationState(a_controller.state);
					matched = true;
				} else if (a_controller.requested.valid && a_controller.requested.requestID == operation.rendererRequestId) {
					next = ToOperationState(a_controller.state);
					matched = true;
				} else if (a_controller.applied.valid && a_controller.applied.requestID == operation.rendererRequestId) {
					next = OperationState::kStabilizing;
					matched = true;
					operation.snapshot.effective = MakeProfile(a_controller.applied);
				}
				if (!matched && !globals::features::upscaling.IsLatestVRRenderScaleRequest(operation.rendererRequestId))
					next = OperationState::kSuperseded;

				operation.snapshot.observedConditions = a_liveSnapshot.observedConditions;
				if (next == operation.snapshot.state)
					continue;
				operation.snapshot.state = next;
				if (next == OperationState::kSuperseded) {
					operation.snapshot.result = Status::kBusy;
					AppendEventLocked(operationId, EventType::kSuperseded, next, operation.snapshot.result);
				} else if (next == OperationState::kCompleted) {
					operation.snapshot.result = Status::kSuccess;
					AppendEventLocked(operationId, EventType::kCompleted, next, Status::kSuccess);
				} else {
					AppendEventLocked(operationId, EventType::kStateChanged, next, Status::kSuccess);
				}
			}
		}

		void FailOperation(std::uint64_t a_operationId, Status a_status, std::uint64_t a_conditions)
		{
			std::lock_guard lock(mutex);
			const auto found = operations.find(a_operationId);
			if (found == operations.end())
				return;
			found->second.snapshot.state = OperationState::kFailed;
			found->second.snapshot.result = a_status;
			found->second.snapshot.observedConditions = a_conditions;
			found->second.snapshot.blockingConditions = a_conditions;
			AppendEventLocked(a_operationId, EventType::kFailed, OperationState::kFailed, a_status);
		}

		std::uint64_t AllocateOperationIdLocked()
		{
			const std::uint64_t value = nextOperationId++;
			if (nextOperationId == 0)
				nextOperationId = 1;
			return value == 0 ? nextOperationId++ : value;
		}

		void AppendEventLocked(std::uint64_t a_operationId, EventType a_type, OperationState a_state, Status a_result)
		{
			Event001 event;
			event.eventId = nextEventId++;
			event.operationId = a_operationId;
			event.type = a_type;
			event.operationState = a_state;
			event.result = a_result;
			event.stateRevision = stateRevision;
			if (const auto found = operations.find(a_operationId); found != operations.end()) {
				event.eventIndex = ++found->second.snapshot.eventIndex;
				event.observedConditions = found->second.snapshot.observedConditions;
			}
			events.push_back(event);
			while (events.size() > kMaximumEvents)
				events.pop_front();
		}

		void CompleteCommand(const std::string& a_key, Status a_status, const ApplyResult001& a_result)
		{
			std::lock_guard lock(mutex);
			const auto found = commands.find(a_key);
			if (found == commands.end())
				return;
			found->second.status = a_status;
			found->second.result = a_result;
			found->second.ready = true;
			TrimLocked();
		}

		void DiscardUnreadyAdmission(
			const std::string& a_key,
			std::uint64_t a_operationId) noexcept
		{
			try {
				std::lock_guard lock(mutex);
				const auto command = commands.find(a_key);
				if (command == commands.end() || command->second.ready)
					return;

				commands.erase(command);
				if (const auto ordered = std::ranges::find(commandOrder, a_key);
					ordered != commandOrder.end()) {
					commandOrder.erase(ordered);
				}
				if (a_operationId != 0) {
					operations.erase(a_operationId);
					std::erase_if(events, [a_operationId](const Event001& a_event) {
						return a_event.operationId == a_operationId;
					});
				}
			} catch (...) {
				OutputDebugStringA(
					"[UpscalingAPI] Exceptional command admission rollback failed; service capacity may be degraded.\n");
			}
		}

		void TrimLocked()
		{
			const auto cutoff = Clock::now() - kCommandRetention;
			while (!commandOrder.empty()) {
				const auto found = commands.find(commandOrder.front());
				if (found == commands.end()) {
					commandOrder.pop_front();
					continue;
				}
				if (!found->second.ready)
					break;
				if (commands.size() <= kMaximumCommands && found->second.createdAt >= cutoff)
					break;
				commands.erase(found);
				commandOrder.pop_front();
			}
			for (auto operation = operations.begin(); operation != operations.end();) {
				if (CSX::Api::IsTerminalUpscalingOperation(operation->second.snapshot.state) &&
					!commands.contains(operation->second.commandKey)) {
					operation = operations.erase(operation);
				} else {
					++operation;
				}
			}
		}
	};

	UpscalingServiceBundle& GetUpscalingServiceBundle()
	{
		static UpscalingServiceBundle bundle;
		return bundle;
	}
}

namespace CSX::Api
{
	void InitializeUpscalingService()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			auto& bundle = GetUpscalingServiceBundle();
			auto* serviceInterface = const_cast<UpscalingAPI::Interface001*>(bundle.GetInterface());
			serviceInterface->context = &bundle;
			const std::uint64_t registryCapabilities =
				ServiceAPI::kCapabilityInspection |
				ServiceAPI::kCapabilityRuntimeMutation |
				ServiceAPI::kCapabilityAsynchronousOperations |
				ServiceAPI::kCapabilityEventStream |
				ServiceAPI::kCapabilityTransactions;
			const auto status = GetProcessServiceRegistry().Register({
				UpscalingAPI::ServiceName,
				UpscalingAPI::ServiceMajor,
				UpscalingAPI::ServiceMinor,
				UpscalingAPI::SchemaRevision,
				registryCapabilities,
				serviceInterface,
			});
			if (status != ServiceAPI::Status::kSuccess)
				logger::error("Failed to register CSX upscaling service ({})", static_cast<std::uint32_t>(status));
			else
				logger::info("Registered CSX upscaling service ABI {}.{}", UpscalingAPI::ServiceMajor, UpscalingAPI::ServiceMinor);
		});
	}
}
