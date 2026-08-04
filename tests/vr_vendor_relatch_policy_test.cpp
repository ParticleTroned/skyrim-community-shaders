#include "Features/Upscaling/VRVendorRelatchPolicy.h"

#include <cassert>

int main()
{
	using namespace VRVendorRelatchPolicy;

	// Native/RS-off retains the selected FSR method as a setting, but its
	// physical resource contract has no submit-stage vendor backend.
	assert(!RequiresVendorRuntime(false, true));
	assert(!RequiresFSRCompatibility(false, true));
	assert(!NeedsDeferredFSRReset(false, true, false, false));

	// An active FSR contract still requires creation, compatibility proof, and
	// a deferred rebuild when neither preserved nor recreated resources exist.
	assert(RequiresVendorRuntime(true, true));
	assert(RequiresFSRCompatibility(true, true));
	assert(NeedsDeferredFSRReset(true, true, false, false));
	assert(!NeedsDeferredFSRReset(true, true, true, false));
	assert(!NeedsDeferredFSRReset(true, true, false, true));

	// Active non-vendor methods do not acquire a vendor runtime.
	assert(!RequiresVendorRuntime(true, false));

	// The relatch transaction exclusively owns resource teardown and creation.
	// Ordinary frame/resource checks must not enter the vendor backend while a
	// load-start/post-load recovery is waiting to queue that relatch, while
	// mutation is queued, or while synchronous target recreation is active.
	assert(ShouldDeferOrdinaryVendorWork(true, true, false, false));
	assert(ShouldDeferOrdinaryVendorWork(true, false, true, false));
	assert(ShouldDeferOrdinaryVendorWork(true, false, false, true));
	assert(ShouldDeferOrdinaryVendorWork(true, true, true, true));
	assert(!ShouldDeferOrdinaryVendorWork(true, false, false, false));
	assert(!ShouldDeferOrdinaryVendorWork(false, true, true, true));

	// A missing or delayed SKSE game-entry message may not be the sole release
	// authority. Renderer convergence is a safe fallback only after every menu,
	// load, recovery, relatch, and profile owner has cleared.
	GameEntryConvergence convergence{};
	assert(!CanReleaseGameEntryVendorGate(convergence));
	convergence.hasGateOwner = true;
	convergence.completedWorldFrame = true;
	assert(CanReleaseGameEntryVendorGate(convergence));

	convergence.mainMenuActive = true;
	assert(!CanReleaseGameEntryVendorGate(convergence));
	convergence.mainMenuActive = false;
	convergence.loadingPresentationActive = true;
	assert(!CanReleaseGameEntryVendorGate(convergence));
	convergence.loadingPresentationActive = false;
	convergence.raceSexPresentationActive = true;
	assert(!CanReleaseGameEntryVendorGate(convergence));
	convergence.raceSexPresentationActive = false;
	convergence.saveLoadProtectionActive = true;
	assert(!CanReleaseGameEntryVendorGate(convergence));
	convergence.saveLoadProtectionActive = false;
	convergence.recoveryPending = true;
	assert(!CanReleaseGameEntryVendorGate(convergence));
	convergence.recoveryPending = false;
	convergence.relatchPending = true;
	assert(!CanReleaseGameEntryVendorGate(convergence));
	convergence.relatchPending = false;
	convergence.profileTransitionPending = true;
	assert(!CanReleaseGameEntryVendorGate(convergence));
	convergence.profileTransitionPending = false;
	convergence.completedWorldFrame = false;
	assert(!CanReleaseGameEntryVendorGate(convergence));
}
