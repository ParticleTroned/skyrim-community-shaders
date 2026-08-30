#include "Api/EditorService.h"

#include "Api/RuntimeThreadAffinity.h"
#include "Api/ServiceFoundation.h"
#include "Api/ServiceRegistry.h"
#include "BuildProvenance.h"
#include "CSEditor/EditorWindow.h"
#include "Features/CSEditor.h"
#include "Globals.h"
#include "Menu.h"
#include "State.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace
{
	using CSX::EditorAPI::MutationReceipt001;
	using CSX::EditorAPI::MutationRequest001;
	using CSX::EditorAPI::Preflight001;
	using CSX::EditorAPI::Snapshot001;
	using CSX::EditorAPI::Status;
	constexpr auto kPreflightLifetime = std::chrono::seconds(30);

	struct OwnedMutation
	{
		CSX::EditorAPI::MutationAction action = CSX::EditorAPI::MutationAction::kOpen;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = CSX::EditorAPI::kMutationNone;
		bool operator==(const OwnedMutation&) const = default;
	};

	struct PendingPreflight
	{
		OwnedMutation mutation;
		std::chrono::steady_clock::time_point expiresAt;
	};

	struct ResponseStrings
	{
		std::string unavailableReason;
		std::string token;
		std::string reasonCode;
		std::string message;
	};

	struct StateSignature
	{
		bool available = false;
		bool dataAvailable = false;
		bool canOpen = false;
		bool resourcesInitialized = false;
		bool editorOpen = false;
		bool menuSessionOpen = false;
		bool mainMenuOpen = false;
		bool loadingMenuOpen = false;
		bool persistentMutationBlocked = false;
		bool saveLoadSafeModeActive = false;
		bool weatherLocked = false;
		bool timePaused = false;
		bool undoAvailable = false;
		bool lightEditorSelected = false;
		bool lightEditorEnabled = false;
		bool lightPickerActive = false;
		bool lightDeferredWorkPending = false;
		bool lightViewportVisible = false;
		CSX::EditorAPI::PreviewMode previewMode = CSX::EditorAPI::PreviewMode::kNone;
		bool operator==(const StateSignature&) const = default;
	};

	CSX::EditorAPI::PreviewMode ConvertPreviewMode(EditorWindow::PreviewMode a_mode)
	{
		using Source = EditorWindow::PreviewMode;
		switch (a_mode) {
		case Source::FreeCamera:
			return CSX::EditorAPI::PreviewMode::kFreeCamera;
		case Source::FreeCameraLocked:
			return CSX::EditorAPI::PreviewMode::kFreeCameraLocked;
		case Source::PlayMode:
			return CSX::EditorAPI::PreviewMode::kPlayMode;
		default:
			return CSX::EditorAPI::PreviewMode::kNone;
		}
	}

	class EditorService
	{
	public:
		Status GetSnapshot(Snapshot001& a_output, std::uint64_t a_capabilities)
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			const auto signature = CaptureState();
			UpdateRevision(signature);
			strings.unavailableReason = UnavailableReason(signature);
			a_output = {
				.structSize = sizeof(Snapshot001),
				.available = signature.available ? 1u : 0u,
				.dataAvailable = signature.dataAvailable ? 1u : 0u,
				.canOpen = signature.canOpen ? 1u : 0u,
				.resourcesInitialized = signature.resourcesInitialized ? 1u : 0u,
				.editorOpen = signature.editorOpen ? 1u : 0u,
				.menuSessionOpen = signature.menuSessionOpen ? 1u : 0u,
				.mainMenuOpen = signature.mainMenuOpen ? 1u : 0u,
				.loadingMenuOpen = signature.loadingMenuOpen ? 1u : 0u,
				.persistentMutationBlocked = signature.persistentMutationBlocked ? 1u : 0u,
				.saveLoadSafeModeActive = signature.saveLoadSafeModeActive ? 1u : 0u,
				.weatherLocked = signature.weatherLocked ? 1u : 0u,
				.timePaused = signature.timePaused ? 1u : 0u,
				.undoAvailable = signature.undoAvailable ? 1u : 0u,
				.previewMode = signature.previewMode,
				.stateRevision = revision,
				.capabilities = a_capabilities,
				.unavailableReason = strings.unavailableReason.c_str(),
				.buildId = BuildProvenance::GetBuildId().data(),
			};
			return Status::kSuccess;
		}

		Status Preflight(
			const MutationRequest001& a_request,
			Preflight001& a_output,
			std::uint64_t a_capabilities)
		{
			if (!IsOwnerThread())
				return FillPreflight(a_output, Status::kWrongThread, false, "wrong_thread", "editor API calls are main-thread-affine");
			PrunePreflights();
			const auto signature = CaptureState();
			UpdateRevision(signature);
			const auto mutation = Own(a_request);
			if (mutation.expectedStateRevision != revision)
				return FillPreflight(a_output, Status::kRevisionConflict, false, "revision_conflict", "state changed; request a fresh snapshot");
			if (!CSX::EditorAPI::SupportsMutationAction(a_capabilities, mutation.action))
				return FillPreflight(a_output, Status::kInvalidArgument, false, "invalid_action", "mutation action is unknown");

			const bool disruptive = IsDisruptive(mutation.action, signature);
			const auto requiredFlags = disruptive ? CSX::EditorAPI::kMutationAllowDisruptive : CSX::EditorAPI::kMutationNone;
			if (!CanApply(mutation.action, signature))
				return FillPreflight(a_output, Status::kBlocked, false, "blocked", BlockReason(mutation.action, signature));
			if ((mutation.flags & requiredFlags) != requiredFlags) {
				strings.message = "mutation requires explicit allowDisruptive acknowledgement";
				strings.reasonCode = "acknowledgement_required";
				a_output = { .structSize = sizeof(Preflight001), .status = Status::kBlocked, .allowed = 0,
					.disruptive = disruptive ? 1u : 0u, .stateRevision = revision, .requiredFlags = requiredFlags,
					.reasonCode = strings.reasonCode.c_str(), .message = strings.message.c_str() };
				return Status::kBlocked;
			}

			strings.token = CSX::Api::ServiceFoundation::NewId();
			pending.insert_or_assign(strings.token, PendingPreflight{ mutation, std::chrono::steady_clock::now() + kPreflightLifetime });
			strings.reasonCode = "allowed";
			strings.message = "mutation may execute with the returned token";
			a_output = { .structSize = sizeof(Preflight001), .status = Status::kSuccess, .allowed = 1,
				.disruptive = disruptive ? 1u : 0u, .stateRevision = revision, .requiredFlags = requiredFlags,
				.token = strings.token.c_str(), .reasonCode = strings.reasonCode.c_str(), .message = strings.message.c_str() };
			return Status::kSuccess;
		}

		Status Execute(
			const MutationRequest001& a_request,
			MutationReceipt001& a_output,
			std::uint64_t a_capabilities)
		{
			if (!IsOwnerThread())
				return FillReceipt(a_output, Status::kWrongThread, "editor API calls are main-thread-affine");
			PrunePreflights();
			const auto before = CaptureState();
			UpdateRevision(before);
			const auto previousRevision = revision;
			const auto mutation = Own(a_request);
			if (!CSX::EditorAPI::SupportsMutationAction(a_capabilities, mutation.action))
				return FillReceipt(a_output, Status::kInvalidArgument, "mutation action is unknown");
			if (!a_request.preflightToken || !*a_request.preflightToken)
				return FillReceipt(a_output, Status::kPreflightRequired, "preflightToken is required");
			const std::string token = a_request.preflightToken;
			const auto found = pending.find(token);
			if (found == pending.end())
				return FillReceipt(a_output, Status::kPreflightExpired, "preflight token is missing or expired");
			const auto expected = found->second.mutation;
			pending.erase(found);
			if (expected != mutation)
				return FillReceipt(a_output, Status::kPreflightMismatch, "mutation does not match the preflight request");
			if (mutation.expectedStateRevision != revision)
				return FillReceipt(a_output, Status::kRevisionConflict, "state changed after preflight");
			if (!CanApply(mutation.action, before))
				return FillReceipt(a_output, Status::kBlocked, BlockReason(mutation.action, before));

			auto* editor = EditorWindow::GetSingleton();
			switch (mutation.action) {
			case CSX::EditorAPI::MutationAction::kOpen:
				CSEditor::OpenEditorWindow();
				editor->UpdateOpenState();
				break;
			case CSX::EditorAPI::MutationAction::kClose:
				editor->open = false;
				editor->UpdateOpenState();
				break;
			case CSX::EditorAPI::MutationAction::kToggle:
				CSEditor::ToggleEditorWindow();
				editor->UpdateOpenState();
				break;
			case CSX::EditorAPI::MutationAction::kResetLayout:
				editor->resetLayout = true;
				break;
			case CSX::EditorAPI::MutationAction::kExitPreview:
				editor->ExitPreviewMode();
				break;
			case CSX::EditorAPI::MutationAction::kOpenLightEditor:
				CSEditor::OpenEditorWindow();
				editor->ActivateLightEditor();
				editor->UpdateOpenState();
				break;
			case CSX::EditorAPI::MutationAction::kBeginLightPick:
				editor->BeginLightPick();
				break;
			case CSX::EditorAPI::MutationAction::kCancelLightPick:
				editor->CancelLightPick();
				break;
			default:
				return FillReceipt(a_output, Status::kInvalidArgument, "mutation action is unknown");
			}

			const auto after = CaptureState();
			const bool changed = !(before == after) || mutation.action == CSX::EditorAPI::MutationAction::kResetLayout;
			if (changed)
				++revision;
			lastState = after;
			strings.message = changed ? "editor mutation applied" : "editor already matched requested state";
			a_output = { .structSize = sizeof(MutationReceipt001), .status = Status::kSuccess, .applied = 1,
				.changed = changed ? 1u : 0u, .previousStateRevision = previousRevision, .stateRevision = revision,
				.message = strings.message.c_str() };
			return Status::kSuccess;
		}

	private:
		std::uint64_t revision = 1;
		std::optional<StateSignature> lastState;
		std::unordered_map<std::string, PendingPreflight> pending;
		ResponseStrings strings;

		bool IsOwnerThread() const { return CSX::Api::IsRuntimeMainThread(); }
		static OwnedMutation Own(const MutationRequest001& a_request) { return { a_request.action, a_request.expectedStateRevision, a_request.flags }; }

		StateSignature CaptureState() const
		{
			auto* feature = CSEditor::GetSingleton();
			auto* editor = EditorWindow::GetSingleton();
			auto* menu = Menu::GetSingleton();
			auto* state = globals::state;
			return {
				.available = feature && feature->loaded && CSEditor::IsDataAvailable(),
				.dataAvailable = CSEditor::IsDataAvailable(),
				.canOpen = CSEditor::CanOpenEditorNow(),
				.resourcesInitialized = CSEditor::AreResourcesInitialized(),
				.editorOpen = editor && editor->open,
				.menuSessionOpen = menu && menu->IsMenuSessionOpen(),
				.mainMenuOpen = state && state->isMainMenuOpen,
				.loadingMenuOpen = state && state->isLoadingMenuOpen,
				.persistentMutationBlocked = state && state->IsPersistentMutationBlocked(),
				.saveLoadSafeModeActive = state && state->IsSaveLoadSafeModeActive(),
				.weatherLocked = editor && editor->IsWeatherLocked(),
				.timePaused = editor && editor->IsTimePaused(),
				.undoAvailable = editor && editor->CanUndo(),
				.lightEditorSelected = editor && editor->IsLightEditorSelected(),
				.lightEditorEnabled = editor && editor->IsLightEditorEnabled(),
				.lightPickerActive = editor && editor->IsLightPickerActive(),
				.lightDeferredWorkPending = editor && editor->HasLightEditorDeferredWork(),
				.lightViewportVisible = editor && editor->IsEditorViewportVisible(),
				.previewMode = editor ? ConvertPreviewMode(editor->GetPreviewMode()) : CSX::EditorAPI::PreviewMode::kNone,
			};
		}

		void UpdateRevision(const StateSignature& a_state)
		{
			if (lastState && *lastState != a_state)
				++revision;
			lastState = a_state;
		}

		static std::string UnavailableReason(const StateSignature& a_state)
		{
			if (a_state.available)
				return {};
			if (!a_state.dataAvailable)
				return "editor data is not loaded";
			return "CS Editor feature is not loaded";
		}

		static bool IsDisruptive(CSX::EditorAPI::MutationAction a_action, const StateSignature& a_state)
		{
			if (a_action == CSX::EditorAPI::MutationAction::kOpenLightEditor ||
				a_action == CSX::EditorAPI::MutationAction::kBeginLightPick) {
				return true;
			}
			return a_state.previewMode != CSX::EditorAPI::PreviewMode::kNone &&
			       (a_action == CSX::EditorAPI::MutationAction::kClose || a_action == CSX::EditorAPI::MutationAction::kToggle ||
				       a_action == CSX::EditorAPI::MutationAction::kExitPreview);
		}

		static bool CanApply(CSX::EditorAPI::MutationAction a_action, const StateSignature& a_state)
		{
			switch (a_action) {
			case CSX::EditorAPI::MutationAction::kOpen:
				return a_state.editorOpen || a_state.canOpen;
			case CSX::EditorAPI::MutationAction::kClose:
				return a_state.editorOpen;
			case CSX::EditorAPI::MutationAction::kToggle:
				return a_state.editorOpen || a_state.canOpen;
			case CSX::EditorAPI::MutationAction::kResetLayout:
				return a_state.editorOpen;
			case CSX::EditorAPI::MutationAction::kExitPreview:
				return a_state.previewMode != CSX::EditorAPI::PreviewMode::kNone;
			case CSX::EditorAPI::MutationAction::kOpenLightEditor:
				return a_state.available && a_state.canOpen &&
				       !a_state.persistentMutationBlocked &&
				       !a_state.saveLoadSafeModeActive &&
				       a_state.previewMode == CSX::EditorAPI::PreviewMode::kNone;
			case CSX::EditorAPI::MutationAction::kBeginLightPick:
				return a_state.available && a_state.canOpen && a_state.editorOpen &&
				       a_state.lightEditorSelected &&
				       a_state.lightEditorEnabled &&
				       a_state.lightViewportVisible &&
				       !a_state.lightPickerActive &&
				       !a_state.lightDeferredWorkPending &&
				       !a_state.persistentMutationBlocked &&
				       !a_state.saveLoadSafeModeActive &&
				       a_state.previewMode == CSX::EditorAPI::PreviewMode::kNone;
			case CSX::EditorAPI::MutationAction::kCancelLightPick:
				return a_state.lightPickerActive;
			default:
				return false;
			}
		}

		static std::string BlockReason(CSX::EditorAPI::MutationAction a_action, const StateSignature& a_state)
		{
			if (a_action == CSX::EditorAPI::MutationAction::kOpen || a_action == CSX::EditorAPI::MutationAction::kToggle) {
				if (!a_state.available)
					return UnavailableReason(a_state);
				if (a_state.loadingMenuOpen)
					return "editor cannot open during a loading screen";
				if (a_state.mainMenuOpen)
					return "enter the game world before opening the editor";
				return "player world context is unavailable";
			}
			if (a_action == CSX::EditorAPI::MutationAction::kClose)
				return "editor is already closed";
			if (a_action == CSX::EditorAPI::MutationAction::kResetLayout)
				return "editor must be open before resetting its layout";
			if (a_action == CSX::EditorAPI::MutationAction::kExitPreview)
				return "editor is not in preview mode";
			if (a_action == CSX::EditorAPI::MutationAction::kOpenLightEditor) {
				if (!a_state.available)
					return UnavailableReason(a_state);
				if (a_state.persistentMutationBlocked || a_state.saveLoadSafeModeActive)
					return "Light Editor cannot open while persistent mutations are blocked";
				if (a_state.previewMode != CSX::EditorAPI::PreviewMode::kNone)
					return "exit editor preview mode before opening Light Editor";
				if (a_state.loadingMenuOpen)
					return "Light Editor cannot open during a loading screen";
				if (a_state.mainMenuOpen)
					return "enter the game world before opening Light Editor";
				return "player world context is unavailable";
			}
			if (a_action == CSX::EditorAPI::MutationAction::kBeginLightPick) {
				if (!a_state.available)
					return UnavailableReason(a_state);
				if (a_state.loadingMenuOpen)
					return "Light Editor picking is unavailable during a loading screen";
				if (a_state.mainMenuOpen || !a_state.canOpen)
					return "enter the game world before beginning a Light Editor pick";
				if (!a_state.editorOpen)
					return "open Light Editor before beginning a pick";
				if (!a_state.lightEditorSelected)
					return "select Light Editor before beginning a pick";
				if (!a_state.lightEditorEnabled)
					return "enable Light Editor before beginning a pick";
				if (!a_state.lightViewportVisible)
					return "wait for a rendered editor Viewport before beginning a pick";
				if (a_state.lightPickerActive)
					return "Light Editor picker is already active";
				if (a_state.lightDeferredWorkPending)
					return "wait for Light Editor reference work to finish before beginning a pick";
				if (a_state.persistentMutationBlocked || a_state.saveLoadSafeModeActive)
					return "Light Editor picking is blocked during save/load mutation guards";
				if (a_state.previewMode != CSX::EditorAPI::PreviewMode::kNone)
					return "exit editor preview mode before beginning a pick";
				return "Light Editor picker is unavailable";
			}
			if (a_action == CSX::EditorAPI::MutationAction::kCancelLightPick)
				return "Light Editor picker is not active";
			return "mutation action is unsupported";
		}

		Status FillPreflight(Preflight001& a_output, Status a_status, bool a_allowed, std::string a_reason, std::string a_message)
		{
			strings.reasonCode = std::move(a_reason);
			strings.message = std::move(a_message);
			a_output = { .structSize = sizeof(Preflight001), .status = a_status, .allowed = a_allowed ? 1u : 0u,
				.stateRevision = revision, .reasonCode = strings.reasonCode.c_str(), .message = strings.message.c_str() };
			return a_status;
		}

		Status FillReceipt(MutationReceipt001& a_output, Status a_status, std::string a_message)
		{
			strings.message = std::move(a_message);
			a_output = { .structSize = sizeof(MutationReceipt001), .status = a_status, .stateRevision = revision,
				.message = strings.message.c_str() };
			return a_status;
		}

		void PrunePreflights()
		{
			const auto now = std::chrono::steady_clock::now();
			for (auto it = pending.begin(); it != pending.end();) {
				if (it->second.expiresAt <= now)
					it = pending.erase(it);
				else
					++it;
			}
		}
	};

	EditorService& GetEditorService()
	{
		static EditorService service;
		return service;
	}

	struct EditorServiceContext
	{
		EditorService* service = nullptr;
		std::uint64_t capabilities = 0;
	};

	EditorServiceContext& GetLegacyEditorServiceContext()
	{
		static EditorServiceContext context{ &GetEditorService(), CSX::EditorAPI::LegacyServiceCapabilities };
		return context;
	}

	EditorServiceContext& GetCurrentEditorServiceContext()
	{
		static EditorServiceContext context{ &GetEditorService(), CSX::EditorAPI::ServiceCapabilities };
		return context;
	}

	const EditorServiceContext* ContextFrom(const void* a_context)
	{
		return static_cast<const EditorServiceContext*>(a_context);
	}

	Status GetSnapshotFn(const void* a_context, Snapshot001* a_output)
	{
		auto* context = ContextFrom(a_context);
		if (!context || !context->service || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(Snapshot001))
			return Status::kStructureTooSmall;
		try {
			return context->service->GetSnapshot(*a_output, context->capabilities);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	Status PreflightFn(const void* a_context, const MutationRequest001* a_request, Preflight001* a_output)
	{
		auto* context = ContextFrom(a_context);
		if (!context || !context->service || !a_request || !a_output)
			return Status::kInvalidArgument;
		if (a_request->structSize < sizeof(MutationRequest001) || a_output->structSize < sizeof(Preflight001))
			return Status::kStructureTooSmall;
		try {
			return context->service->Preflight(*a_request, *a_output, context->capabilities);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	Status ExecuteFn(const void* a_context, const MutationRequest001* a_request, MutationReceipt001* a_output)
	{
		auto* context = ContextFrom(a_context);
		if (!context || !context->service || !a_request || !a_output)
			return Status::kInvalidArgument;
		if (a_request->structSize < sizeof(MutationRequest001) || a_output->structSize < sizeof(MutationReceipt001))
			return Status::kStructureTooSmall;
		try {
			return context->service->Execute(*a_request, *a_output, context->capabilities);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	const CSX::EditorAPI::Interface001* GetLegacyEditorService001()
	{
		static const CSX::EditorAPI::Interface001 service{
			.structSize = sizeof(CSX::EditorAPI::Interface001),
			.major = CSX::EditorAPI::ServiceMajor,
			.minor = CSX::EditorAPI::MinimumServiceMinor,
			.schemaRevision = CSX::EditorAPI::LegacySchemaRevision,
			.capabilities = CSX::EditorAPI::LegacyServiceCapabilities,
			.context = &GetLegacyEditorServiceContext(),
			.GetSnapshot = &GetSnapshotFn,
			.Preflight = &PreflightFn,
			.Execute = &ExecuteFn,
		};
		return &service;
	}
}

namespace CSX::Api
{
	const EditorAPI::Interface001* GetEditorService001()
	{
		static const EditorAPI::Interface001 service{
			.structSize = sizeof(EditorAPI::Interface001), .major = EditorAPI::ServiceMajor,
			.minor = EditorAPI::ServiceMinor, .schemaRevision = EditorAPI::SchemaRevision,
			.capabilities = EditorAPI::ServiceCapabilities,
			.context = &GetCurrentEditorServiceContext(),
			.GetSnapshot = &GetSnapshotFn, .Preflight = &PreflightFn, .Execute = &ExecuteFn,
		};
		return &service;
	}

	void InitializeEditorService()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			constexpr auto registryCapabilities =
				ServiceAPI::kCapabilityInspection |
				ServiceAPI::kCapabilityRuntimeMutation |
				ServiceAPI::kCapabilityTransactions;
			auto& registry = GetProcessServiceRegistry();
			const auto legacyStatus = registry.Register({ EditorAPI::ServiceName, EditorAPI::ServiceMajor,
				EditorAPI::MinimumServiceMinor, EditorAPI::LegacySchemaRevision, registryCapabilities, GetLegacyEditorService001() });
			const auto currentStatus = registry.Register({ EditorAPI::ServiceName, EditorAPI::ServiceMajor,
				EditorAPI::ServiceMinor, EditorAPI::SchemaRevision, registryCapabilities, GetEditorService001() });
			const auto registrationAccepted = [](ServiceAPI::Status a_status) {
				return a_status == ServiceAPI::Status::kSuccess ||
				       a_status == ServiceAPI::Status::kAlreadyRegistered;
			};
			if (!registrationAccepted(legacyStatus) || !registrationAccepted(currentStatus)) {
				logger::error("Failed to register CSX editor service ABI {}.{} ({}) / {}.{} ({})",
					EditorAPI::ServiceMajor,
					EditorAPI::MinimumServiceMinor,
					static_cast<std::uint32_t>(legacyStatus),
					EditorAPI::ServiceMajor,
					EditorAPI::ServiceMinor,
					static_cast<std::uint32_t>(currentStatus));
			} else {
				logger::info("Registered CSX editor service ABI {}.{}-{}.{}",
					EditorAPI::ServiceMajor,
					EditorAPI::MinimumServiceMinor,
					EditorAPI::ServiceMajor,
					EditorAPI::ServiceMinor);
			}
		});
	}
}
