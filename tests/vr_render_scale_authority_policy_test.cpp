#include "Features/Upscaling/VRRenderScaleAuthorityPolicy.h"

namespace
{
	using namespace VRRenderScaleAuthorityPolicy;

	constexpr Facts FactsForOwner(Owner a_owner)
	{
		Facts facts{};
		switch (a_owner) {
		case Owner::ControllerTransition:
			facts.controllerTransitionEpoch = 1;
			break;
		case Owner::ControllerPresentation:
			facts.controllerPresentationEpoch = 1;
			break;
		case Owner::PendingRequest:
			facts.pendingRequestPresent = true;
			facts.pendingRequestID = 1;
			facts.pendingRequestEpoch = 1;
			break;
		case Owner::DeferredRequest:
			facts.deferredRequestPresent = true;
			facts.deferredRequestID = 1;
			facts.deferredRequestEpoch = 1;
			break;
		case Owner::PhysicalRelatch:
			facts.physicalRelatchQueued = true;
			facts.physicalRelatchEpoch = 1;
			break;
		case Owner::PendingPostLoadReset:
			facts.postLoadResetPending = true;
			facts.pendingPostLoadResetEpoch = 1;
			break;
		case Owner::DeferredPostLoadRecovery:
			facts.deferredPostLoadRecoveryEpoch = 1;
			break;
		case Owner::ActivePostLoadRecovery:
			facts.postLoadRecoveryActive = true;
			facts.postLoadRecoveryEpoch = 1;
			break;
		case Owner::PreMutationNativeFallback:
			facts.preMutationFallbackEpoch = 1;
			facts.preMutationFallbackAdmissionActive = true;
			break;
		case Owner::ProviderNeutralNativeRecovery:
			facts.providerNeutralRecoveryEpoch = 1;
			facts.physicalRelatchQueued = true;
			facts.physicalRelatchEpoch = 1;
			break;
		case Owner::UnresolvedPhysicalMutation:
			facts.unresolvedPhysicalMutationEpoch = 1;
			break;
		case Owner::PostMutationSerialization:
			facts.postMutationSerializationEpoch = 1;
			facts.postMutationChainSerial = 1;
			break;
		case Owner::VendorWorkGate:
			facts.vendorWorkGateOwnerMask = 1;
			facts.vendorWorkGateOwnerEpoch = 1;
			break;
		case Owner::PostLoadCompositorHold:
			facts.compositorHoldActive = true;
			facts.compositorHoldEpoch = 1;
			break;
		case Owner::NativeRestore:
			facts.nativeRestoreActive = true;
			facts.nativeRestoreOwnerEpoch = 1;
			break;
		case Owner::NativeRestorePresentationGuard:
			facts.nativeRestorePresentationGuardEpoch = 1;
			break;
		case Owner::IntermediateRetirement:
			facts.intermediateRetirementPending = true;
			break;
		case Owner::EngineTargetRetirement:
			facts.engineTargetRetirementPending = true;
			break;
		case Owner::MemoryTrim:
			facts.memoryTrimPending = true;
			facts.memoryTrimOwnerEpoch = 1;
			break;
		case Owner::DLSSReset:
			facts.dlssResetPending = true;
			facts.dlssResetGeneration = 1;
			break;
		case Owner::FSRReset:
			facts.fsrResetPending = true;
			facts.fsrResetGeneration = 1;
			break;
		case Owner::DLSSLifecycle:
			facts.dlssLifecycleActive = true;
			break;
		case Owner::FSRLifecycle:
			facts.fsrLifecycleActive = true;
			break;
		case Owner::ResourceTrackingSync:
			facts.resourceTrackingSyncPending = true;
			break;
		case Owner::FpsStabilizerSync:
			facts.fpsStabilizerSyncFrame = 1;
			break;
		default:
			break;
		}
		return facts;
	}

	constexpr ServiceMask ServicesForOwner(Owner a_owner)
	{
		for (const auto& mapping : kOwnerServiceMappings) {
			if (mapping.owner == a_owner)
				return mapping.services;
		}
		return 0;
	}

	constexpr bool CoversEveryAuthoritativeOwner()
	{
		if (!AllOwnersHaveServiceMappings())
			return false;
		for (std::uint8_t index = 0;
			index < static_cast<std::uint8_t>(Owner::Count);
			++index) {
			const auto owner = static_cast<Owner>(index);
			const auto resolution = Resolve(FactsForOwner(owner));
			if ((resolution.owners & ToMask(owner)) == 0 ||
				ServicesForOwner(owner) == 0 ||
				(resolution.services & ServicesForOwner(owner)) == 0 ||
				resolution.unmappedOwners != 0) {
				return false;
			}
		}
		return true;
	}

	constexpr bool DerivedStateHasNoAuthority()
	{
		Facts facts{};
		facts.controllerStateMirrorBusy = true;
		facts.deferredRequestHint = true;
		facts.stableRuntimeProfileHint = true;
		facts.dlssViewportPreparationPending = true;
		facts.compositorCycleDrainPending = true;
		facts.compositorAwaitingSyncEpoch = 1;
		facts.physicalRelatchQueued = true;
		const auto resolution = Resolve(facts);
		return resolution.owners == 0 && resolution.services == 0;
	}

	constexpr bool ClearingDerivedStateCannotHideAnOwner()
	{
		Facts facts = FactsForOwner(Owner::PendingRequest);
		facts.controllerStateMirrorBusy = true;
		facts.deferredRequestHint = true;
		facts.stableRuntimeProfileHint = true;
		facts.dlssViewportPreparationPending = true;
		facts.compositorCycleDrainPending = true;
		facts.compositorAwaitingSyncEpoch = 1;
		const auto before = Resolve(facts);
		facts.controllerStateMirrorBusy = false;
		facts.deferredRequestHint = false;
		facts.stableRuntimeProfileHint = false;
		facts.dlssViewportPreparationPending = false;
		facts.compositorCycleDrainPending = false;
		facts.compositorAwaitingSyncEpoch = 0;
		const auto after = Resolve(facts);
		return before.owners == after.owners &&
		       before.services == after.services &&
		       (after.owners & ToMask(Owner::PendingRequest)) != 0;
	}

	constexpr bool OwnerTransferRemainsServiceable()
	{
		Facts source = FactsForOwner(Owner::PendingRequest);
		Facts overlap = source;
		overlap.physicalRelatchQueued = true;
		overlap.physicalRelatchEpoch = source.pendingRequestEpoch;
		Facts destination{};
		destination.physicalRelatchInProgress = true;
		destination.physicalRelatchEpoch = source.pendingRequestEpoch;
		const auto queuedRelatch = ToMask(ServiceClass::QueuedRelatchWork);
		return (Resolve(source).services & queuedRelatch) != 0 &&
		       (Resolve(overlap).services & queuedRelatch) != 0 &&
		       (Resolve(destination).services & queuedRelatch) != 0;
	}

	constexpr bool UnresolvedMutationNeverLooksIdle()
	{
		Facts facts{};
		facts.unresolvedPhysicalMutationEpoch = 7;
		const auto resolution = Resolve(facts);
		return (resolution.owners &
				   ToMask(Owner::UnresolvedPhysicalMutation)) != 0 &&
		       (resolution.services &
				   ToMask(ServiceClass::PostMutationRecoveryWork)) != 0 &&
		       (resolution.inconsistencies &
				   ToMask(Inconsistency::UnresolvedMutationWithoutSerialization)) != 0;
	}

	constexpr bool StaleQueueFlagDoesNotCreateWork()
	{
		Facts facts{};
		facts.physicalRelatchQueued = true;
		facts.postLoadResetPending = true;
		const auto resolution = Resolve(facts);
		return (resolution.owners & ToMask(Owner::PhysicalRelatch)) == 0 &&
		       (resolution.owners & ToMask(Owner::PendingPostLoadReset)) == 0 &&
		       (resolution.services &
				   ToMask(ServiceClass::QueuedRelatchWork)) == 0;
	}

	constexpr bool RecoveryTransferRemainsServiceable()
	{
		Facts pending = FactsForOwner(Owner::PendingPostLoadReset);
		Facts overlap = pending;
		overlap.postLoadRecoveryActive = true;
		overlap.postLoadRecoveryEpoch = pending.pendingPostLoadResetEpoch;
		Facts deferred{};
		deferred.deferredPostLoadRecoveryEpoch =
			pending.pendingPostLoadResetEpoch;
		const auto recovery =
			ToMask(ServiceClass::PostLoadRecoveryWork);
		return (Resolve(pending).services & recovery) != 0 &&
		       (Resolve(overlap).services & recovery) != 0 &&
		       (Resolve(deferred).services & recovery) != 0;
	}

	constexpr bool MutationTransferRemainsServiceable()
	{
		Facts creator = FactsForOwner(Owner::PhysicalRelatch);
		creator.physicalRelatchQueued = false;
		creator.physicalRelatchInProgress = true;
		Facts boundary = creator;
		boundary.unresolvedPhysicalMutationEpoch = creator.physicalRelatchEpoch;
		boundary.postMutationSerializationEpoch = creator.physicalRelatchEpoch;
		boundary.postMutationChainSerial = 1;
		Facts published{};
		published.postMutationSerializationEpoch = creator.physicalRelatchEpoch;
		published.postMutationChainSerial = 1;
		const auto relatch = ToMask(ServiceClass::QueuedRelatchWork);
		const auto recovery =
			ToMask(ServiceClass::PostMutationRecoveryWork);
		return (Resolve(creator).services & relatch) != 0 &&
		       (Resolve(boundary).services & recovery) != 0 &&
		       (Resolve(published).services & recovery) != 0;
	}

	constexpr bool OrphanedCompoundOwnerIsVisible()
	{
		Facts facts{};
		facts.providerNeutralRecoveryEpoch = 9;
		const auto resolution = Resolve(facts);
		return (resolution.owners &
				   ToMask(Owner::ProviderNeutralNativeRecovery)) == 0 &&
		       (resolution.inconsistencies &
				   ToMask(
					   Inconsistency::ProviderNeutralRecoveryWithoutWorker)) != 0;
	}

	static_assert(CoversEveryAuthoritativeOwner());
	static_assert(DerivedStateHasNoAuthority());
	static_assert(ClearingDerivedStateCannotHideAnOwner());
	static_assert(OwnerTransferRemainsServiceable());
	static_assert(UnresolvedMutationNeverLooksIdle());
	static_assert(StaleQueueFlagDoesNotCreateWork());
	static_assert(RecoveryTransferRemainsServiceable());
	static_assert(MutationTransferRemainsServiceable());
	static_assert(OrphanedCompoundOwnerIsVisible());
}

int main() {}
