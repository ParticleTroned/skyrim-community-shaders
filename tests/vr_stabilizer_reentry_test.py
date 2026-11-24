"""Exercise production Stabilizer queries during target recreation and recovery."""

from pathlib import Path
import re
import unittest

from grass_draw_lifecycle_test import compile_fixture
from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


def definition(source: str, signature: str) -> str:
    match = re.search(re.escape(signature) + r"[^;{]*\{", source)
    if match is None:
        raise ValueError(f"Missing definition: {signature}")
    return block(source[match.start():], signature)


def build_fixture(source: str) -> str:
    declarations = definition(source, "class ScopedVRLoadingSerialReadOwner") + ";\n"
    renderdoc = (ROOT / "src/Features/RenderDoc.h").read_text(encoding="utf-8")
    compatibility = ("namespace globals::features { struct RenderDoc {\n"
                     "bool enableRenderDocCapture = false, renderDocModule = false, upscalingCompatible = false;\n" +
                     definition(renderdoc, "bool ShouldBlockUpscaling()") + "\n" +
                     definition(renderdoc, "bool IsUpscalingCompatible()") +
                     "\n} renderDoc; }\n" +
                     definition(source, "bool IsRenderDocUpscalingBlocked("))
    queries = "\n".join(definition(source, signature) for signature in (
        "bool Upscaling::IsVRFpsStabilizerSyncActive()",
        "bool HasCompletedVRWorldFrameAfterLatestLoad(",
        "bool IsExplicitSettingsVRRenderScaleTransitionOrigin(",
        "bool IsVRRenderScaleRelatchSensitiveContextActive(",
        "bool ShouldDeferExplicitVRRenderScaleRelatch(",
        "VRVendorRelatchPolicy::StabilizerSyncOwner GetVRFpsStabilizerSyncOwnerLocked(",
        "std::optional<VRVendorRelatchPolicy::StabilizerSyncOwner> GetReadyVRFpsStabilizerSyncOwner(",
        "std::optional<VRVendorRelatchPolicy::StabilizerSyncOutcome> GetVRFpsStabilizerSyncOutcomeLocked(",
        "bool HasTerminalVRFpsStabilizerFallbackForCurrentLoad(",
        "bool HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(",
        "bool UsesVRRenderScaleStartupActivationProtection(",
        "bool CanActivateVRRenderScaleRuntime(",
        "bool Upscaling::IsPerfModeActive()",
        "bool CanStartVRRenderScaleRuntime(",
        "bool ShouldBlockVRRenderScaleBootLatchForPendingWork(",
        "bool Upscaling::TryGetPerfModeOpenVRRenderTargetSize("))
    preparation = definition(source, "struct VREngineTargetRecreateCheckpointContext") + ";\n"
    preparation += definition(source, "void CaptureVREngineTargetGenerationPreparation(")
    recovery = definition(source, "void Upscaling::PrepareVRRenderScalePostLoadRecovery(")
    recovery += definition(source, "bool Upscaling::TryResumeVRRenderScalePresentationLocked(")
    recovery += definition(source, "bool Upscaling::TryRetainFSRForCompletedSaveLoad(")
    callback = definition(source, "struct BSOpenVR_GetRenderTargetSize") + ";\n"
    return (PRELUDE + declarations + compatibility + FIXTURE + queries + preparation + callback +
            RECOVERY_DEPENDENCIES + recovery + CHECKS)


PRELUDE = r'''
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include "Features/Upscaling/VRVendorRelatchPolicy.h"
std::mutex g_vrLoadingTransitionSerialMutex;
bool injectedRenderDoc = false;
bool IsRenderDocDllLoaded(bool) { return injectedRenderDoc; }
'''

FIXTURE = r'''
using namespace std::chrono_literals;
struct State {
    uint32_t frameCount = 200, lastCompletedWorldRenderFrame = 199, lastWorldRenderFrame = 199;
    bool pendingPostLoadRuntimeReset = false, safeMode = false;
    static constexpr uint32_t kSaveLoadSafeModeGraceFrames = 30;
    std::atomic_uint32_t saveLoadSafeModeEndFrame{0};
    bool IsEngineSaveLoadActivityActive() const { return false; }
    bool IsEngineSaveLoadKnownIdle() const { return true; }
    bool IsSaveLoadSafeModeActive() const { return safeMode; }
    void ExtendSaveLoadSafeMode(uint32_t frame, uint32_t grace) { saveLoadSafeModeEndFrame = frame + grace; }
} fixtureState;
namespace globals {
State* state = &fixtureState;
namespace game { bool isVR = true; void* ui = nullptr; }
}
namespace logger {
template<class... T> void debug(const char*, T...) {}
template<class... T> void info(const char*, T...) {}
}
namespace VRFpsStabilizer { bool loaded = true; bool IsLoaded() { return loaded; } }
namespace RE { struct BSOpenVR {}; }
namespace REL {
template<class F> struct Relocation {
    F* target = nullptr;
    template<class... Args> auto operator()(Args... args) { return target(args...); }
};
}
std::mutex g_vrLoadingMenuGateReconciliationMutex;
std::atomic_uint64_t g_vrLoadingTransitionSerial{1};
std::atomic_bool g_vrLoadingTransitionSerialOpen{false};
std::atomic_uint32_t g_vrLoadingTransitionCloseFrame{100};
std::atomic_uint32_t g_vrLoadingTransitionSourceCellFormID{1};
std::atomic_uint32_t g_vrLoadingTransitionDestinationCellFormID{1};
std::atomic_uint32_t g_vrLoadingTransitionDestinationObservationWorldFrame{150};
uint64_t g_vrLoadingMenuEdgeGeneration = 1;
constexpr uint32_t kVRFpsStabilizerIdentitySyncFallbackFrames = 30;
thread_local bool g_vrNativeRenderTargetRecreateOverrideActive = false;
bool physicalMenuOpen = false, replaceOwnerDuringSample = false, throwDuringPreparation = false;
bool throwDuringProviderCheck = false;
bool enableSyncDuringSnapshot = false;
bool raceMenuOpen = false, raceMenuTail = false, otherMenu = false, doorHandoff = false;
std::function<void()> duringProviderCheck;
uint32_t playerCell = 1;
struct VRStabilizerSyncCompletion {
    VRVendorRelatchPolicy::StabilizerSyncOwner owner;
    VRVendorRelatchPolicy::StabilizerSyncOutcome outcome;
};
std::optional<VRStabilizerSyncCompletion> g_vrStabilizerSyncCompletion;
bool IsLoadingMenuContextActive() { return physicalMenuOpen; }
bool IsLoadingMenuPhysicallyOpen() { return physicalMenuOpen; }
bool IsMainOrLoadingMenuContextActive() { return physicalMenuOpen; }
bool IsVRLoadingPresentationContextActive(const State*) { return physicalMenuOpen; }
bool IsMainMenuContextActive() { return false; }
bool IsNonLoadingVRMenuPresentationContextActive() { return otherMenu; }
bool IsVRMenuPresentationContextActive() { return otherMenu; }
bool IsRaceSexMenuContextActive(void*) { return raceMenuOpen; }
bool IsVRRaceSexMenuEventContextActive(const State*) { return raceMenuTail; }
bool IsSaveLoadMutationContextActive(const State*) { return fixtureState.pendingPostLoadRuntimeReset; }
bool HasCompletedVRWorldFrameAfterLatestLoad(const State*);
uint32_t GetVRPlayerCellFormID() {
    if (replaceOwnerDuringSample) {
        const std::scoped_lock lock(g_vrLoadingTransitionSerialMutex);
        ++g_vrLoadingMenuEdgeGeneration;
        g_vrLoadingTransitionSerial = 2;
        g_vrLoadingTransitionCloseFrame = 180;
    }
    return playerCell;
}
enum class DynamicResolutionOverride { ForceDisabled };
bool dynamicResolutionDisabled = false;
bool SetDynamicResolutionOverrideForUpscaling(DynamicResolutionOverride) {
    dynamicResolutionDisabled = true;
    return true;
}
struct Upscaling {
    enum class UpscaleMethod { kNONE, kDLSS, kFSR };
    enum class VRRenderScaleMemoryTrimReason { None, PostLoad };
    enum class VRRenderScaleTransitionState { Active, Stabilizing };
    enum class VRUpscalingTransitionOrigin { CSMenu, VRAPI, PostLoadSync, RecoveryRelatch };
    enum class VRPostLoadCompositorHoldState { Idle, Armed, Holding };
    enum class VRPostLoadCompositorHoldRoute { None, InGameLoad };
    struct VRRenderScaleCommonTargetOffer {};
    struct VRRenderScaleProfileSnapshot {
        uint64_t transitionEpoch = 4;
        UpscaleMethod method = UpscaleMethod::kDLSS;
        uint32_t qualityMode = 3;
        bool fsr4RuntimeEnabled = false;
    };
    struct Recovery {
        bool active = true;
        uint64_t recoveryEpoch = 3, transitionEpoch = 4, loadingSerial = 1;
        bool cleanupArmed = false, trimArmed = false, trimCompleted = false;
        bool trimSucceeded = false, cleanupDrained = false;
        uint32_t admissionWaitStartFrame = 0;
        uint32_t startFrame = 100;
        bool engineTargetCreateEntered = false;
    };
    struct MemoryTrim {
        bool pending = false, lastSucceeded = true;
        VRRenderScaleMemoryTrimReason reason = VRRenderScaleMemoryTrimReason::None;
        uint64_t ownerEpoch = 0;
    };
    struct {
        Recovery postLoadRecovery;
        VRRenderScaleProfileSnapshot stable, applied;
        MemoryTrim memoryTrim;
        uint32_t revision = 0;
        VRRenderScaleTransitionState state = VRRenderScaleTransitionState::Active;
    } vrRenderScaleTransitionController;
    std::recursive_mutex perfModeRenderTargetRecreateQueueMutex;
    std::mutex vrRenderScaleTransitionControllerMutex;
    std::atomic_uint32_t vrRenderScaleMemoryReliefEndFrame{0}, vrRenderScaleMemoryReliefCleanEyeMask{0};
    std::atomic_bool vrRenderScaleMemoryReliefLogged{false};
    unsigned providerChecks = 0, cleanupCalls = 0;
    bool preservedIntermediates = false, providerReady = true;
    bool resumeArmed = false, protectionReset = false, deferLifecycle = false;
    std::atomic_uint64_t vrRenderScalePostMutationSerializationEpoch{4};
    std::atomic_uint64_t vrRenderScaleUnresolvedPhysicalMutationEpoch{0};
    std::atomic_bool perfModeRenderTargetRecreateInProgress{false};
    bool TryResumeVRRenderScalePresentationLocked(uint64_t);
    auto GetVRRenderScaleTransitionSnapshot() const {
        if (enableSyncDuringSnapshot) {
            const std::scoped_lock lock(vrFpsStabilizerSessionConfigMutex);
            vrFpsStabilizerSessionConfig.upscalingSwitchingEnabled = true;
        }
        return vrRenderScaleTransitionController;
    }
    bool IsSubmitStageDeviceLost() const { return false; }
    template<class Controller, class Boot>
    bool IsVRRenderScalePhysicalVendorContractExact(const Controller&, const Boot&, UpscaleMethod) const { return true; }
    bool IsVRRenderScalePhysicalContractConverged(UpscaleMethod, uint32_t) const { return true; }
    bool ShouldDeferVRVendorLifecycleMutation() const { return deferLifecycle; }
    void SetVRRenderScaleTransitionState(VRRenderScaleTransitionState state, const char*) {
        vrRenderScaleTransitionController.state = state;
    }
    void ArmSubmitStageVendorResumeCooldown(uint32_t) { resumeArmed = true; }
    void ResetVRPostLoadPresentationProtectionLocked() { protectionReset = true; }
    void PrepareVRRenderScalePostLoadRecovery(uint64_t);
    bool HasTruthfulStableVRVendorResources(const VRRenderScaleProfileSnapshot&, bool = true);
    bool IsVRRenderScaleMemoryReliefActive() const { return false; }
    bool CanAdmitVRIntermediateRetirement(uint64_t) const { return true; }
    void ApplyVRRenderScaleMemoryReliefTransitionCleanup(const char*, bool preserve) {
        ++cleanupCalls;
        preservedIntermediates = preserve;
    }
    void ServiceVRIntermediateTextureCleanup(bool) {}
    bool ArmVRRenderScaleMemoryTrim(uint64_t epoch, VRRenderScaleMemoryTrimReason reason) {
        vrRenderScaleTransitionController.memoryTrim = {false, true, reason, epoch};
        return true;
    }
    bool HasPendingVRRenderScaleMemoryTrim() const { return false; }
    void ServiceVRRenderScaleMemoryTrim(const char*) {}
    void SampleVRRenderScaleMemory(bool, const char*) {}
    bool HasPendingVRIntermediateTextureCleanup() const { return false; }
    struct Boot { bool valid = true; uint32_t renderEyeWidth = 500, renderEyeHeight = 600, qualityMode = 3, generation = 1; };
    struct Settings { bool fsr4RuntimeEnable = false; } settings;
    struct PerfMode {
        Boot boot;
        const Boot& GetBootSnapshot() const { return boot; }
        bool IsActive(const Settings&, UpscaleMethod) const { return true; }
        bool HasRestartRequiredChange() const { return false; }
        bool TryGetOpenVRRenderTargetSize(const Settings&, UpscaleMethod, uint32_t& width, uint32_t& height, bool, uint32_t) {
            width = boot.renderEyeWidth;
            height = boot.renderEyeHeight;
            return true;
        }
    } perfMode;
    bool loaded = true, latched = true;
    struct Config {
        bool fileReadable = true, upscalingSwitchingEnabled = true, hasProfiles = true;
        bool HasAnyUpscalingProfile() const { return hasProfiles; }
    };
    mutable Config vrFpsStabilizerSessionConfig;
    mutable std::mutex vrFpsStabilizerSessionConfigMutex;
    void InitializeVRFpsStabilizerSessionConfig() const {}
    bool IsVRFpsStabilizerSyncActive() const;
    bool IsPerfModeActive() const;
    UpscaleMethod configuredMethod = UpscaleMethod::kDLSS;
    std::atomic_uint32_t submitStageVendorResumeFrame{0};
    std::atomic<VRPostLoadCompositorHoldState> vrPostLoadCompositorHoldState{VRPostLoadCompositorHoldState::Holding};
    std::atomic<VRPostLoadCompositorHoldRoute> vrPostLoadCompositorHoldRoute{VRPostLoadCompositorHoldRoute::InGameLoad};
    std::atomic_uint64_t vrPostLoadCompositorHoldLoadingSerial{1};
    std::atomic_bool vrPostLoadCompositorHoldLoadingMenuClosed{true};
    bool TryRetainFSRForCompletedSaveLoad(UpscaleMethod, uint64_t);
    bool IsRenderScaleModeRequested() const { return true; }
    bool GetVRRenderScaleModeRequested() const { return true; }
    uint32_t GetEffectiveUpscalingQualityMode() const { return 3; }
    bool CanDispatchExistingVRVendorEvaluation(UpscaleMethod) const { return true; }
    void RequestHistoryReset() {}
    std::atomic_uint32_t pendingVRFpsStabilizerSyncFrame{0};
    std::atomic_bool pendingPerfModeRenderTargetRecreate{false};
    std::atomic_bool postLoadRuntimeResetPending{false};
    std::atomic_bool perfModeAllowBootLatchCreate{false};
    std::atomic_uint32_t vrRenderScaleNextContractGeneration{1};
    std::atomic_uint64_t vrNativeRestorePresentationGuardEpoch{0};
    bool IsVRRenderScaleModeLatched() const { return latched; }
    bool HasPendingVRUpscalingTransition() const { return false; }
    bool IsVRNativeRestorePresentationGuardActive() const { return false; }
    bool IsOpenCompositeUpscalingBlocked() const { return false; }
    bool IsVRStartupNativeFallbackRestartRequired() const { return false; }
    UpscaleMethod GetConfiguredUpscaleMethodForTransition() const { return configuredMethod; }
    UpscaleMethod GetUpscaleMethod() const { return configuredMethod; }
    void RecordTrueHMDRenderTargetSize(uint32_t, uint32_t) {}
    bool ConsumePerfModeBootLatchCreate() { return perfModeAllowBootLatchCreate.exchange(false); }
    bool TryGetPerfModeOpenVRRenderTargetSize(uint32_t&, uint32_t&, bool);
};
bool DeferVRPerfModeBootLatchForPendingDLSS(const Upscaling&) { return false; }
bool IsCurrentVRFpsStabilizerDoorHandoff(const Upscaling&, uint64_t) { return doorHandoff; }
bool IsVRRenderScaleAppliedVendorContractExact(const auto&, const auto&, Upscaling::UpscaleMethod) { return true; }
std::atomic_uint64_t g_vrRetainedPostLoadPresentationSerial{0};
std::atomic_uint32_t g_vrRetainedPostLoadPresentationCloseFrame{0};
struct {} g_vrDeferredPostLoadFSRReset;
namespace globals::features { Upscaling upscaling; }
struct VREngineTargetGenerationSnapshot {};
struct VREngineTargetGenerationDelta {};
struct VRRenderScaleCommonTargetReclaimResult {};
VREngineTargetGenerationSnapshot CaptureVREngineTargetGeneration(Upscaling&) {
    if (throwDuringPreparation) throw std::runtime_error("snapshot failed");
    return {};
}
void NeutralizeReleasedDeferredTargetSnapshots(VREngineTargetGenerationSnapshot&, Upscaling&) {}
VREngineTargetGenerationDelta PrepareVREngineTargetGenerationDelta(const VREngineTargetGenerationSnapshot&) { return {}; }
constexpr uint32_t kVRRenderScaleMemoryReliefFrames = 60;
bool HasPendingVRVendorRuntimeReset(const Upscaling&, Upscaling::UpscaleMethod) { return false; }
'''

RECOVERY_DEPENDENCIES = r'''
bool Upscaling::HasTruthfulStableVRVendorResources(const VRRenderScaleProfileSnapshot&, bool) {
    ++providerChecks;
    // Provider texture validation queries presentation eligibility on the same thread.
    const bool presentationActive = IsPerfModeActive();
    if (duringProviderCheck) duringProviderCheck();
    if (throwDuringProviderCheck) throw std::runtime_error("provider validation failed");
    return providerReady && presentationActive;
}
'''

CHECKS = r'''
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Reset() {
    physicalMenuOpen = replaceOwnerDuringSample = throwDuringPreparation = false;
    throwDuringProviderCheck = false;
    enableSyncDuringSnapshot = false;
    raceMenuOpen = raceMenuTail = otherMenu = doorHandoff = false;
    injectedRenderDoc = false;
    globals::features::renderDoc = {};
    globals::game::isVR = VRFpsStabilizer::loaded = true;
    duringProviderCheck = {};
    playerCell = 1;
    fixtureState.frameCount = 200;
    fixtureState.lastCompletedWorldRenderFrame = fixtureState.lastWorldRenderFrame = 199;
    fixtureState.pendingPostLoadRuntimeReset = fixtureState.safeMode = false;
    fixtureState.saveLoadSafeModeEndFrame = 0;
    globals::state = &fixtureState;
    g_vrLoadingTransitionSerial = 1;
    g_vrLoadingTransitionCloseFrame = 100;
    g_vrLoadingTransitionSerialOpen = false;
    g_vrLoadingMenuEdgeGeneration = 1;
    g_vrLoadingTransitionSourceCellFormID = 1;
    g_vrLoadingTransitionDestinationCellFormID = 1;
    g_vrLoadingTransitionDestinationObservationWorldFrame = 150;
    g_vrStabilizerSyncCompletion = VRStabilizerSyncCompletion{
        {{1,100},1}, VRVendorRelatchPolicy::StabilizerSyncOutcome::Resolved};
    auto& u = globals::features::upscaling;
    u.latched = u.loaded = true;
    u.vrFpsStabilizerSessionConfig = {};
    u.configuredMethod = Upscaling::UpscaleMethod::kDLSS;
    u.perfModeAllowBootLatchCreate = false;
    u.pendingPerfModeRenderTargetRecreate = u.postLoadRuntimeResetPending = false;
    u.submitStageVendorResumeFrame = 0;
    u.vrPostLoadCompositorHoldLoadingSerial = 1;
    g_vrRetainedPostLoadPresentationSerial = 0;
    g_vrRetainedPostLoadPresentationCloseFrame = 0;
    u.pendingVRFpsStabilizerSyncFrame = 0;
    u.vrRenderScaleTransitionController = {};
    u.providerChecks = u.cleanupCalls = 0;
    u.preservedIntermediates = false;
    u.providerReady = true;
    u.resumeArmed = u.protectionReset = u.deferLifecycle = false;
    u.vrRenderScalePostMutationSerializationEpoch = 4;
    u.vrRenderScaleUnresolvedPhysicalMutationEpoch = 0;
    u.perfModeRenderTargetRecreateInProgress = false;
    dynamicResolutionDisabled = false;
}
void RequireReleased() {
    auto query = ScopedVRLoadingSerialReadOwner::LockForQuery();
    Require(query.owns_lock(), "A stale recreation owner bypassed normal locking");
    query.unlock();
    std::unique_lock gate(g_vrLoadingMenuGateReconciliationMutex, std::try_to_lock);
    Require(gate.owns_lock(), "Recreation retained the loading gate");
}
void NativeRenderSizeCallback(bool reduced = true) {
    BSOpenVR_GetRenderTargetSize::func.target = [](RE::BSOpenVR*, uint32_t* width, uint32_t* height) {
        *width = 1000; *height = 1200;
    };
    uint32_t width = 0, height = 0;
    BSOpenVR_GetRenderTargetSize::thunk(nullptr, &width, &height);
    Require(width == (reduced ? 500u : 1000u) && height == (reduced ? 600u : 1200u) &&
            dynamicResolutionDisabled == reduced,
        "Reentrant render-size callback violated render-scale admission");
}
void CheckRecreationCallbacks() {
    for (bool sync : {false, true}) {
        Reset();
        auto& u = globals::features::upscaling;
        u.vrFpsStabilizerSessionConfig.upscalingSwitchingEnabled = sync;
        VREngineTargetGenerationSnapshot snapshot;
        VREngineTargetGenerationDelta delta;
        VREngineTargetRecreateCheckpointContext context{
            .upscaling=&u, .snapshot=&snapshot, .delta=&delta, .transitionEpoch=4, .recoveryEpoch=3};
        CaptureVREngineTargetGenerationPreparation(&context);
        Require(context.captured && context.loadingSerialLock.owns_lock() && context.loadingGateLock.owns_lock(),
            "Recreation did not retain load ownership");
        NativeRenderSizeCallback();
        Require(!HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(u), "Resolved sync was lost during setup");
        Require(!HasTerminalVRFpsStabilizerFallbackForCurrentLoad(), "Resolved sync became terminal");
        Require(context.loadingSerialLock.owns_lock(), "Readiness query released the recreation lock");
        context.ReleaseLoadingBoundaryOwnership();
        context.ReleaseLoadingBoundaryOwnership();
        RequireReleased();
    }
}
void CheckReadinessOwnership() {
    Reset();
    auto& u = globals::features::upscaling;
    VREngineTargetGenerationSnapshot snapshot;
    VREngineTargetGenerationDelta delta;
    VREngineTargetRecreateCheckpointContext context{.upscaling=&u, .snapshot=&snapshot, .delta=&delta};
    CaptureVREngineTargetGenerationPreparation(&context);
    g_vrStabilizerSyncCompletion.reset();
    Require(HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(u), "Borrowed ownership skipped unresolved sync");
    u.pendingVRFpsStabilizerSyncFrame = 100;
    Require(HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(u), "Borrowed ownership skipped pending sync");
    u.pendingVRFpsStabilizerSyncFrame = 0;
    g_vrStabilizerSyncCompletion = VRStabilizerSyncCompletion{
        {{1,100},1}, VRVendorRelatchPolicy::StabilizerSyncOutcome::Refused};
    Require(HasTerminalVRFpsStabilizerFallbackForCurrentLoad(), "Refused sync lost its fallback");
    playerCell = 2;
    Require(!GetReadyVRFpsStabilizerSyncOwner(&fixtureState), "First destination sample became ready");
    Require(g_vrLoadingTransitionDestinationCellFormID == 2 &&
        g_vrLoadingTransitionDestinationObservationWorldFrame == 199, "Destination observation was not published");
    fixtureState.lastCompletedWorldRenderFrame = 200;
    Require(GetReadyVRFpsStabilizerSyncOwner(&fixtureState).has_value(), "Fresh destination frame was not admitted");
    context.ReleaseLoadingBoundaryOwnership();
    RequireReleased();

    Reset();
    replaceOwnerDuringSample = true;
    Require(!GetReadyVRFpsStabilizerSyncOwner(&fixtureState), "A superseded load was accepted");
    Require(g_vrLoadingTransitionDestinationCellFormID == 1, "Stale sampling replaced destination ownership");
    Reset();
    g_vrLoadingTransitionSerialOpen = true;
    Require(!GetReadyVRFpsStabilizerSyncOwner(&fixtureState), "An open load was accepted");
    Require(!HasTerminalVRFpsStabilizerFallbackForCurrentLoad(), "An open load inherited old fallback");
    Reset();
    physicalMenuOpen = true;
    Require(!GetReadyVRFpsStabilizerSyncOwner(&fixtureState), "An open physical menu was accepted");
    Require(!GetReadyVRFpsStabilizerSyncOwner(nullptr), "Missing state was accepted");
    RequireReleased();
}
void CheckExceptionCleanup() {
    constexpr const char* expectedFailures[] = {
        "LoadingMenu ownership changed before VR render-target recreation",
        "snapshot failed",
        "native creator failed",
        "Post-load recovery ownership changed before VR render-target recreation"
    };
    for (int failure = 0; failure != 4; ++failure) {
        Reset();
        bool threw = false;
        try {
            auto& u = globals::features::upscaling;
            VREngineTargetGenerationSnapshot snapshot;
            VREngineTargetGenerationDelta delta;
            VREngineTargetRecreateCheckpointContext context{.upscaling=&u, .snapshot=&snapshot, .delta=&delta};
            context.recoveryEpoch = failure == 3 ? 9 : 0;
            physicalMenuOpen = failure == 0;
            throwDuringPreparation = failure == 1;
            CaptureVREngineTargetGenerationPreparation(&context);
            NativeRenderSizeCallback();
            throw std::runtime_error("native creator failed");
        } catch (const std::runtime_error& error) {
            Require(std::string_view(error.what()) == expectedFailures[failure],
                "The expected preparation or native failure was not exercised");
            threw = true;
        }
        Require(threw, "The failure fixture did not throw");
        RequireReleased();
    }
}
void CheckBorrowedOwnerBounds() {
    Reset();
    {
        std::mutex unrelated;
        std::unique_lock owner(unrelated);
        ScopedVRLoadingSerialReadOwner scope(owner);
        auto query = ScopedVRLoadingSerialReadOwner::LockForQuery();
        Require(query.owns_lock(), "An unrelated mutex bypassed serial locking");
    }
    {
        std::unique_lock owner(g_vrLoadingTransitionSerialMutex, std::defer_lock);
        ScopedVRLoadingSerialReadOwner scope(owner);
        auto query = ScopedVRLoadingSerialReadOwner::LockForQuery();
        Require(query.owns_lock(), "An unowned lock bypassed serial locking");
    }
    {
        std::unique_lock owner(g_vrLoadingTransitionSerialMutex);
        ScopedVRLoadingSerialReadOwner outer(owner);
        {
            ScopedVRLoadingSerialReadOwner inner(owner);
            auto query = ScopedVRLoadingSerialReadOwner::LockForQuery();
            Require(!query.owns_lock(), "A nested query reacquired the recreation lock");
        }
        auto query = ScopedVRLoadingSerialReadOwner::LockForQuery();
        Require(!query.owns_lock() && owner.owns_lock(), "Nested cleanup lost the enclosing owner");
    }
    RequireReleased();
}
void CheckThreadIsolation() {
    Reset();
    auto& u = globals::features::upscaling;
    VREngineTargetGenerationSnapshot snapshot;
    VREngineTargetGenerationDelta delta;
    VREngineTargetRecreateCheckpointContext context{.upscaling=&u, .snapshot=&snapshot, .delta=&delta};
    CaptureVREngineTargetGenerationPreparation(&context);
    std::promise<void> started;
    auto startedFuture = started.get_future();
    auto other = std::async(std::launch::async, [&] {
        started.set_value();
        return HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(u);
    });
    startedFuture.wait();
    const bool blocked = other.wait_for(50ms) == std::future_status::timeout;
    context.ReleaseLoadingBoundaryOwnership();
    const bool unresolved = other.get();
    Require(blocked && !unresolved, "Another thread bypassed recreation ownership");
    RequireReleased();
}
void RequireRecoveryReleased() {
    RequireReleased();
    auto& u = globals::features::upscaling;
    auto other = std::async(std::launch::async, [&] {
        std::unique_lock queue(u.perfModeRenderTargetRecreateQueueMutex, std::try_to_lock);
        std::unique_lock controller(u.vrRenderScaleTransitionControllerMutex, std::try_to_lock);
        return queue.owns_lock() && controller.owns_lock();
    });
    Require(other.get(), "Recovery retained queue or controller ownership");
}
void CheckPostLoadRecovery() {
    for (bool sync : {false, true}) for (bool ready : {false, true}) {
        Reset();
        auto& u = globals::features::upscaling;
        u.vrFpsStabilizerSessionConfig.upscalingSwitchingEnabled = sync;
        u.providerReady = ready;
        u.PrepareVRRenderScalePostLoadRecovery(3);
        const auto& recovery = u.vrRenderScaleTransitionController.postLoadRecovery;
        Require(u.providerChecks == 1 && u.cleanupCalls == 1 && u.preservedIntermediates == ready,
            "Recovery changed provider retention or failed its reentrant readiness query");
        Require(recovery.cleanupDrained && recovery.trimSucceeded && recovery.admissionWaitStartFrame == 200,
            "Recovery failed to publish cleanup and trim completion");
        u.PrepareVRRenderScalePostLoadRecovery(3);
        Require(u.providerChecks == 2 && u.cleanupCalls == 1,
            "Repeated recovery must revalidate without repeating destructive cleanup");
        RequireRecoveryReleased();
    }
    for (int stale = 0; stale != 5; ++stale) {
        Reset();
        auto& u = globals::features::upscaling;
        if (stale == 1) physicalMenuOpen = true;
        if (stale == 2) g_vrLoadingTransitionSerialOpen = true;
        if (stale == 3) u.vrRenderScaleTransitionController.postLoadRecovery.recoveryEpoch = 9;
        if (stale == 4) u.vrRenderScaleTransitionController.postLoadRecovery.loadingSerial = 2;
        u.PrepareVRRenderScalePostLoadRecovery(stale == 0 ? 0 : 3);
        Require(u.providerChecks == 0 && u.cleanupCalls == 0 &&
                u.vrRenderScaleTransitionController.revision == 0,
            "Recovery admitted a missing, open or superseded loading owner");
        RequireRecoveryReleased();
    }
    Reset();
    auto& u = globals::features::upscaling;
    throwDuringProviderCheck = true;
    bool threw = false;
    try { u.PrepareVRRenderScalePostLoadRecovery(3); }
    catch (const std::runtime_error& error) {
        Require(std::string_view(error.what()) == "provider validation failed",
            "Recovery threw before reaching provider validation");
        threw = true;
    }
    Require(threw && u.cleanupCalls == 0, "Failed validation entered destructive cleanup");
    RequireRecoveryReleased();
}
void CheckPresentationResume() {
    for (bool sync : {false, true}) for (bool ready : {false, true}) {
        Reset();
        auto& u = globals::features::upscaling;
        u.vrFpsStabilizerSessionConfig.upscalingSwitchingEnabled = sync;
        u.providerReady = ready;
        fixtureState.lastCompletedWorldRenderFrame = fixtureState.lastWorldRenderFrame = 200;
        Require(u.TryResumeVRRenderScalePresentationLocked(4) == ready,
            "Presentation resume lost provider readiness during reentry");
        Require(u.providerChecks == 1 && u.resumeArmed == ready && u.protectionReset == ready,
            "Resume must retain presentation protection until resources qualify");
        RequireRecoveryReleased();
    }
    for (int gate = 0; gate != 6; ++gate) {
        Reset();
        auto& u = globals::features::upscaling;
        fixtureState.lastCompletedWorldRenderFrame = fixtureState.lastWorldRenderFrame = 200;
        if (gate == 0) g_vrLoadingTransitionSerialOpen = true;
        if (gate == 1) u.vrRenderScaleUnresolvedPhysicalMutationEpoch = 4;
        if (gate == 2) u.vrRenderScalePostMutationSerializationEpoch = 5;
        if (gate == 3) fixtureState.lastCompletedWorldRenderFrame = 199;
        if (gate == 4) fixtureState.safeMode = true;
        if (gate == 5) u.deferLifecycle = true;
        Require(!u.TryResumeVRRenderScalePresentationLocked(4) && !u.resumeArmed && !u.protectionReset,
            "Presentation resumed across a loading, mutation, frame or lifecycle gate");
        if (gate == 4) Require(fixtureState.saveLoadSafeModeEndFrame == 230,
            "Resume discarded the safe-mode grace period");
        RequireRecoveryReleased();
    }
    Reset();
    auto& u = globals::features::upscaling;
    fixtureState.lastCompletedWorldRenderFrame = fixtureState.lastWorldRenderFrame = 200;
    throwDuringProviderCheck = true;
    bool threw = false;
    try { u.TryResumeVRRenderScalePresentationLocked(4); }
    catch (const std::runtime_error& error) {
        Require(std::string_view(error.what()) == "provider validation failed",
            "Resume threw before reaching provider validation");
        threw = true;
    }
    Require(threw && !u.resumeArmed && !u.protectionReset, "Failed validation released presentation protection");
    RequireRecoveryReleased();
}
void CheckRecoveryThreadIsolation(bool resume) {
    Reset();
    auto& u = globals::features::upscaling;
    std::future<bool> other;
    bool blocked = false;
    duringProviderCheck = [&] {
        std::promise<void> started;
        auto startedFuture = started.get_future();
        other = std::async(std::launch::async, [&] {
            started.set_value();
            return HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(u);
        });
        startedFuture.wait();
        blocked = other.wait_for(50ms) == std::future_status::timeout;
    };
    if (resume) {
        fixtureState.lastCompletedWorldRenderFrame = fixtureState.lastWorldRenderFrame = 200;
        Require(u.TryResumeVRRenderScalePresentationLocked(4), "Isolated resume was rejected");
    } else {
        u.PrepareVRRenderScalePostLoadRecovery(3);
    }
    Require(blocked && !other.get(), "Another thread bypassed recovery load ownership");
    RequireRecoveryReleased();
}
void CheckRenderDocRecovery() {
    for (unsigned flags = 0; flags != 16; ++flags) for (unsigned route = 0; route != 3; ++route) {
        Reset();
        auto& u = globals::features::upscaling;
        auto& renderDoc = globals::features::renderDoc;
        renderDoc.enableRenderDocCapture = (flags & 1) != 0;
        renderDoc.renderDocModule = (flags & 2) != 0;
        injectedRenderDoc = (flags & 4) != 0;
        renderDoc.upscalingCompatible = (flags & 8) != 0;
        const bool allowed = (flags & 7) == 0 || renderDoc.upscalingCompatible;
        Require(u.IsVRFpsStabilizerSyncActive() == allowed,
            "Stabilizer ignored RenderDoc compatibility");
        if (route == 0) {
            VREngineTargetGenerationSnapshot snapshot;
            VREngineTargetGenerationDelta delta;
            VREngineTargetRecreateCheckpointContext context{
                .upscaling=&u, .snapshot=&snapshot, .delta=&delta, .transitionEpoch=4, .recoveryEpoch=3};
            CaptureVREngineTargetGenerationPreparation(&context);
            NativeRenderSizeCallback(allowed);
            context.ReleaseLoadingBoundaryOwnership();
        } else if (route == 1) {
            u.PrepareVRRenderScalePostLoadRecovery(3);
            Require(u.preservedIntermediates == allowed, "Recovery bypassed RenderDoc protection");
        } else {
            fixtureState.lastCompletedWorldRenderFrame = fixtureState.lastWorldRenderFrame = 200;
            Require(u.TryResumeVRRenderScalePresentationLocked(4) == allowed && u.protectionReset == allowed,
                "Presentation resumed through incompatible RenderDoc");
        }
        RequireRecoveryReleased();
    }
    for (unsigned blocker = 0; blocker != 4; ++blocker) {
        Reset();
        auto& u = globals::features::upscaling;
        if (blocker == 0) globals::game::isVR = false;
        if (blocker == 1) VRFpsStabilizer::loaded = false;
        if (blocker == 2) u.vrFpsStabilizerSessionConfig.fileReadable = false;
        if (blocker == 3) u.vrFpsStabilizerSessionConfig.hasProfiles = false;
        Require(!u.IsVRFpsStabilizerSyncActive(), "Unavailable Stabilizer became active");
    }
}
void CheckNewGameAndRaceMenuAdmission() {
    Reset();
    auto& u = globals::features::upscaling;
    g_vrLoadingTransitionSerial = g_vrLoadingMenuEdgeGeneration = 0;
    g_vrLoadingTransitionCloseFrame = g_vrLoadingTransitionSourceCellFormID = 0;
    g_vrStabilizerSyncCompletion.reset();
    fixtureState.lastCompletedWorldRenderFrame = UINT32_MAX;
    u.latched = false;
    {
        std::unique_lock serial(g_vrLoadingTransitionSerialMutex);
        const ScopedVRLoadingSerialReadOwner owner(serial);
        Require(UsesVRRenderScaleStartupActivationProtection(u, &fixtureState, true) &&
                !CanActivateVRRenderScaleRuntime(u), "New game activated before a completed world frame");
        fixtureState.lastCompletedWorldRenderFrame = 199;
        Require(HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(u) &&
                UsesVRRenderScaleStartupActivationProtection(u, &fixtureState, true) &&
                !CanActivateVRRenderScaleRuntime(u), "New game bypassed its pending Stabilizer profile");
        g_vrStabilizerSyncCompletion = VRStabilizerSyncCompletion{
            {{0,0},0}, VRVendorRelatchPolicy::StabilizerSyncOutcome::Resolved};
        Require(!UsesVRRenderScaleStartupActivationProtection(u, &fixtureState, true) &&
                CanActivateVRRenderScaleRuntime(u), "Resolved new-game startup stayed blocked");
        for (bool handoff : {false, true}) for (bool tail : {false, true}) {
            doorHandoff = handoff;
            raceMenuOpen = !tail;
            raceMenuTail = tail;
            for (auto origin : {Upscaling::VRUpscalingTransitionOrigin::CSMenu, Upscaling::VRUpscalingTransitionOrigin::VRAPI})
                Require(ShouldDeferExplicitVRRenderScaleRelatch(u, origin, &fixtureState, 4),
                    "Settings or Stabilizer handoff bypassed RaceMenu/close-tail ownership");
        }
        raceMenuOpen = raceMenuTail = false;
        Require(!ShouldDeferExplicitVRRenderScaleRelatch(u, Upscaling::VRUpscalingTransitionOrigin::VRAPI, &fixtureState, 4),
            "Closed RaceMenu kept a resolved handoff blocked");
        Require(serial.owns_lock(), "Startup checks released borrowed ownership");
    }
    RequireRecoveryReleased();
}
void CheckFSRRetentionConfigReload() {
    for (bool reload : {false, true}) {
        Reset();
        auto& u = globals::features::upscaling;
        u.vrFpsStabilizerSessionConfig.upscalingSwitchingEnabled = false;
        u.configuredMethod = Upscaling::UpscaleMethod::kFSR;
        u.vrRenderScaleTransitionController.applied.method = u.configuredMethod;
        u.vrRenderScaleTransitionController.stable.method = u.configuredMethod;
        u.vrRenderScalePostMutationSerializationEpoch = 0;
        // A config reload can publish after inactive-sync admission without owning the serial mutex.
        enableSyncDuringSnapshot = reload;
        Require(u.TryRetainFSRForCompletedSaveLoad(Upscaling::UpscaleMethod::kFSR, 3) &&
                g_vrRetainedPostLoadPresentationSerial == 1 && g_vrRetainedPostLoadPresentationCloseFrame == 100,
            "FSR retention failed when Stabilizer was enabled after admission");
        Require(u.providerChecks == 1, "FSR retention skipped resource validation");
        RequireRecoveryReleased();
    }
}
int main() {
    try {
        CheckRecreationCallbacks();
        CheckReadinessOwnership();
        CheckExceptionCleanup();
        CheckBorrowedOwnerBounds();
        CheckThreadIsolation();
        CheckPostLoadRecovery();
        CheckPresentationResume();
        CheckRecoveryThreadIsolation(false);
        CheckRecoveryThreadIsolation(true);
        CheckRenderDocRecovery();
        CheckNewGameAndRaceMenuAdmission();
        CheckFSRRetentionConfigReload();
        std::cout << "Recreation and recovery reentry, readiness ownership, exception cleanup, owner bounds and thread isolation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
'''


class VRStabilizerReentryTests(unittest.TestCase):
    def test_recreation_and_post_load_recovery_queries(self):
        source = (ROOT / "src/Features/Upscaling.cpp").read_text(encoding="utf-8")
        for devbench in (False, True):
            with self.subTest(devbench=devbench):
                result = compile_fixture(build_fixture(source), devbench=devbench)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
