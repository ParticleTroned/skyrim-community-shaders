"""Exercise production Stabilizer queries during native target recreation."""

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
    queries = "\n".join(definition(source, signature) for signature in (
        "VRVendorRelatchPolicy::StabilizerSyncOwner GetVRFpsStabilizerSyncOwnerLocked(",
        "std::optional<VRVendorRelatchPolicy::StabilizerSyncOwner> GetReadyVRFpsStabilizerSyncOwner(",
        "std::optional<VRVendorRelatchPolicy::StabilizerSyncOutcome> GetVRFpsStabilizerSyncOutcomeLocked(",
        "bool HasTerminalVRFpsStabilizerFallbackForCurrentLoad(",
        "bool HasUnresolvedVRFpsStabilizerSyncForCurrentLoad(",
        "bool CanActivateVRRenderScaleRuntime(",
        "bool CanStartVRRenderScaleRuntime(",
        "bool ShouldBlockVRRenderScaleBootLatchForPendingWork(",
        "bool Upscaling::TryGetPerfModeOpenVRRenderTargetSize("))
    preparation = definition(source, "struct VREngineTargetRecreateCheckpointContext") + ";\n"
    preparation += definition(source, "void CaptureVREngineTargetGenerationPreparation(")
    callback = definition(source, "struct BSOpenVR_GetRenderTargetSize") + ";\n"
    return PRELUDE + declarations + FIXTURE + queries + preparation + callback + CHECKS


PRELUDE = r'''
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
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
'''

FIXTURE = r'''
using namespace std::chrono_literals;
struct State {
    uint32_t frameCount = 200, lastCompletedWorldRenderFrame = 199;
    bool pendingPostLoadRuntimeReset = false;
} fixtureState;
namespace globals {
State* state = &fixtureState;
namespace game { bool isVR = true; }
}
namespace logger { template<class... T> void debug(const char*, T...) {} }
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
uint32_t playerCell = 1;
struct VRStabilizerSyncCompletion {
    VRVendorRelatchPolicy::StabilizerSyncOwner owner;
    VRVendorRelatchPolicy::StabilizerSyncOutcome outcome;
};
std::optional<VRStabilizerSyncCompletion> g_vrStabilizerSyncCompletion;
bool IsLoadingMenuContextActive() { return physicalMenuOpen; }
bool IsMainOrLoadingMenuContextActive() { return physicalMenuOpen; }
bool IsVRLoadingPresentationContextActive(const State*) { return physicalMenuOpen; }
bool HasCompletedVRWorldFrameAfterLatestLoad(const State* s) {
    return s && s->lastCompletedWorldRenderFrame != UINT32_MAX &&
           s->lastCompletedWorldRenderFrame > g_vrLoadingTransitionCloseFrame.load();
}
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
    enum class VRRenderScaleMemoryTrimReason { None };
    struct VRRenderScaleCommonTargetOffer {};
    struct Recovery {
        bool active = true;
        uint64_t recoveryEpoch = 3, transitionEpoch = 4, loadingSerial = 1;
    };
    struct { Recovery postLoadRecovery; } vrRenderScaleTransitionController;
    std::mutex vrRenderScaleTransitionControllerMutex;
    struct Boot { bool valid = true; uint32_t renderEyeWidth = 500, renderEyeHeight = 600; };
    struct PerfMode {
        Boot boot;
        const Boot& GetBootSnapshot() const { return boot; }
        bool TryGetOpenVRRenderTargetSize(int, UpscaleMethod, uint32_t& width, uint32_t& height, bool, uint32_t) {
            width = boot.renderEyeWidth;
            height = boot.renderEyeHeight;
            return true;
        }
    } perfMode;
    bool loaded = true, sync = true, latched = true;
    int settings = 0;
    std::atomic_uint32_t pendingVRFpsStabilizerSyncFrame{0};
    std::atomic_bool pendingPerfModeRenderTargetRecreate{false};
    std::atomic_bool postLoadRuntimeResetPending{false};
    std::atomic_bool perfModeAllowBootLatchCreate{false};
    std::atomic_uint32_t vrRenderScaleNextContractGeneration{1};
    std::atomic_uint64_t vrNativeRestorePresentationGuardEpoch{0};
    bool IsVRFpsStabilizerSyncActive() const { return sync; }
    bool IsVRRenderScaleModeLatched() const { return latched; }
    bool HasPendingVRUpscalingTransition() const { return false; }
    bool IsVRNativeRestorePresentationGuardActive() const { return false; }
    bool IsOpenCompositeUpscalingBlocked() const { return false; }
    bool IsRenderDocUpscalingBlocked() const { return false; }
    bool IsVRStartupNativeFallbackRestartRequired() const { return false; }
    UpscaleMethod GetConfiguredUpscaleMethodForTransition() const { return UpscaleMethod::kDLSS; }
    void RecordTrueHMDRenderTargetSize(uint32_t, uint32_t) {}
    bool ConsumePerfModeBootLatchCreate() { return perfModeAllowBootLatchCreate.exchange(false); }
    bool TryGetPerfModeOpenVRRenderTargetSize(uint32_t&, uint32_t&, bool);
};
bool DeferVRPerfModeBootLatchForPendingDLSS(const Upscaling&) { return false; }
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
'''

CHECKS = r'''
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Reset() {
    physicalMenuOpen = replaceOwnerDuringSample = throwDuringPreparation = false;
    playerCell = 1;
    fixtureState = {};
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
    u.sync = u.latched = true;
    u.pendingVRFpsStabilizerSyncFrame = 0;
    dynamicResolutionDisabled = false;
}
void RequireReleased() {
    auto query = ScopedVRLoadingSerialReadOwner::LockForQuery();
    Require(query.owns_lock(), "A stale recreation owner bypassed normal locking");
    query.unlock();
    std::unique_lock gate(g_vrLoadingMenuGateReconciliationMutex, std::try_to_lock);
    Require(gate.owns_lock(), "Recreation retained the loading gate");
}
void NativeRenderSizeCallback() {
    BSOpenVR_GetRenderTargetSize::func.target = [](RE::BSOpenVR*, uint32_t* width, uint32_t* height) {
        *width = 1000; *height = 1200;
    };
    uint32_t width = 0, height = 0;
    BSOpenVR_GetRenderTargetSize::thunk(nullptr, &width, &height);
    Require(width == 500 && height == 600 && dynamicResolutionDisabled,
        "Reentrant render-size callback lost the admitted reduced dimensions");
}
void CheckRecreationCallbacks() {
    for (bool sync : {false, true}) {
        Reset();
        auto& u = globals::features::upscaling;
        u.sync = sync;
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
int main() {
    try {
        CheckRecreationCallbacks();
        CheckReadinessOwnership();
        CheckExceptionCleanup();
        CheckBorrowedOwnerBounds();
        CheckThreadIsolation();
        std::cout << "Recreation callbacks, readiness ownership, exception cleanup, owner bounds and thread isolation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
'''


class VRStabilizerReentryTests(unittest.TestCase):
    def test_native_recreation_queries(self):
        source = (ROOT / "src/Features/Upscaling.cpp").read_text(encoding="utf-8")
        for devbench in (False, True):
            with self.subTest(devbench=devbench):
                result = compile_fixture(build_fixture(source), devbench=devbench)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
