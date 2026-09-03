#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace VRRenderScaleAuthorityPolicy
{
	using OwnerMask = std::uint64_t;
	using ServiceMask = std::uint32_t;
	using InconsistencyMask = std::uint32_t;

	enum class Owner : std::uint8_t
	{
		ControllerTransition,
		ControllerPresentation,
		PendingRequest,
		DeferredRequest,
		PhysicalRelatch,
		PendingPostLoadReset,
		DeferredPostLoadRecovery,
		ActivePostLoadRecovery,
		PreMutationNativeFallback,
		ProviderNeutralNativeRecovery,
		UnresolvedPhysicalMutation,
		PostMutationSerialization,
		VendorWorkGate,
		PostLoadCompositorHold,
		NativeRestore,
		NativeRestorePresentationGuard,
		IntermediateRetirement,
		EngineTargetRetirement,
		MemoryTrim,
		DLSSReset,
		FSRReset,
		DLSSLifecycle,
		FSRLifecycle,
		ResourceTrackingSync,
		FpsStabilizerSync,
		Count
	};

	enum class ServiceClass : std::uint8_t
	{
		QueuedRelatchWork,
		PreMutationFallbackWork,
		PostLoadRecoveryWork,
		PostMutationRecoveryWork,
		RetirementWork,
		TrimWork,
		PresentationHoldWork,
		ProviderLifecycleWork,
		Count
	};

	enum class Inconsistency : std::uint8_t
	{
		UnresolvedMutationWithoutSerialization,
		SerializationWithoutChain,
		ProviderNeutralRecoveryWithoutWorker,
		PreMutationFallbackWithoutAdmission,
		Count
	};

	[[nodiscard]] constexpr OwnerMask ToMask(Owner a_owner) noexcept
	{
		return a_owner == Owner::Count ?
		           0 :
		           OwnerMask{ 1 } << static_cast<std::uint8_t>(a_owner);
	}

	[[nodiscard]] constexpr ServiceMask ToMask(ServiceClass a_service) noexcept
	{
		return a_service == ServiceClass::Count ?
		           0 :
		           ServiceMask{ 1 } << static_cast<std::uint8_t>(a_service);
	}

	[[nodiscard]] constexpr InconsistencyMask ToMask(
		Inconsistency a_inconsistency) noexcept
	{
		return a_inconsistency == Inconsistency::Count ?
		           0 :
		           InconsistencyMask{ 1 } << static_cast<std::uint8_t>(a_inconsistency);
	}

	inline constexpr OwnerMask kAllOwners =
		(OwnerMask{ 1 } << static_cast<std::uint8_t>(Owner::Count)) - 1;
	inline constexpr ServiceMask kAllServiceClasses =
		(ServiceMask{ 1 } << static_cast<std::uint8_t>(ServiceClass::Count)) - 1;

	struct Facts
	{
		// Authoritative owner tuples.
		std::uint64_t controllerTransitionEpoch = 0;
		std::uint64_t controllerPresentationEpoch = 0;
		bool pendingRequestPresent = false;
		std::uint64_t pendingRequestID = 0;
		std::uint64_t pendingRequestEpoch = 0;
		bool deferredRequestPresent = false;
		std::uint64_t deferredRequestID = 0;
		std::uint64_t deferredRequestEpoch = 0;
		bool physicalRelatchQueued = false;
		bool physicalRelatchInProgress = false;
		std::uint64_t physicalRelatchEpoch = 0;
		bool postLoadResetPending = false;
		std::uint64_t pendingPostLoadResetEpoch = 0;
		std::uint64_t deferredPostLoadRecoveryEpoch = 0;
		bool postLoadRecoveryActive = false;
		std::uint64_t postLoadRecoveryEpoch = 0;
		std::uint64_t preMutationFallbackEpoch = 0;
		bool preMutationFallbackAdmissionActive = false;
		std::uint64_t providerNeutralRecoveryEpoch = 0;
		std::uint64_t unresolvedPhysicalMutationEpoch = 0;
		std::uint64_t postMutationSerializationEpoch = 0;
		std::uint64_t postMutationChainSerial = 0;
		std::uint32_t vendorWorkGateOwnerMask = 0;
		std::uint32_t vendorWorkGateOwnerEpoch = 0;
		bool compositorHoldActive = false;
		std::uint64_t compositorHoldEpoch = 0;
		bool nativeRestoreActive = false;
		std::uint64_t nativeRestoreOwnerEpoch = 0;
		std::uint64_t nativeRestorePresentationGuardEpoch = 0;
		bool intermediateRetirementPending = false;
		bool engineTargetRetirementPending = false;
		bool memoryTrimPending = false;
		std::uint64_t memoryTrimOwnerEpoch = 0;
		bool dlssResetPending = false;
		std::uint32_t dlssResetGeneration = 0;
		bool fsrResetPending = false;
		std::uint32_t fsrResetGeneration = 0;
		bool dlssLifecycleActive = false;
		bool fsrLifecycleActive = false;
		bool resourceTrackingSyncPending = false;
		std::uint32_t fpsStabilizerSyncFrame = 0;

		// Derived publications are exposed so tests can prove they have no authority.
		bool controllerStateMirrorBusy = false;
		bool deferredRequestHint = false;
		bool stableRuntimeProfileHint = false;
		bool dlssViewportPreparationPending = false;
		bool compositorCycleDrainPending = false;
		std::uint64_t compositorAwaitingSyncEpoch = 0;
	};

	struct OwnerServiceMapping
	{
		Owner owner = Owner::ControllerTransition;
		ServiceMask services = 0;
	};

	inline constexpr std::array<OwnerServiceMapping,
		static_cast<std::size_t>(Owner::Count)>
		kOwnerServiceMappings{
			OwnerServiceMapping{ Owner::ControllerTransition,
				ToMask(ServiceClass::QueuedRelatchWork) },
			OwnerServiceMapping{ Owner::ControllerPresentation,
				ToMask(ServiceClass::PresentationHoldWork) },
			OwnerServiceMapping{ Owner::PendingRequest,
				ToMask(ServiceClass::QueuedRelatchWork) },
			OwnerServiceMapping{ Owner::DeferredRequest,
				ToMask(ServiceClass::QueuedRelatchWork) },
			OwnerServiceMapping{ Owner::PhysicalRelatch,
				ToMask(ServiceClass::QueuedRelatchWork) },
			OwnerServiceMapping{ Owner::PendingPostLoadReset,
				ToMask(ServiceClass::PostLoadRecoveryWork) },
			OwnerServiceMapping{ Owner::DeferredPostLoadRecovery,
				ToMask(ServiceClass::PostLoadRecoveryWork) },
			OwnerServiceMapping{ Owner::ActivePostLoadRecovery,
				ToMask(ServiceClass::PostLoadRecoveryWork) },
			OwnerServiceMapping{ Owner::PreMutationNativeFallback,
				ToMask(ServiceClass::PreMutationFallbackWork) },
			OwnerServiceMapping{ Owner::ProviderNeutralNativeRecovery,
				ToMask(ServiceClass::PreMutationFallbackWork) |
					ToMask(ServiceClass::QueuedRelatchWork) },
			OwnerServiceMapping{ Owner::UnresolvedPhysicalMutation,
				ToMask(ServiceClass::PostMutationRecoveryWork) },
			OwnerServiceMapping{ Owner::PostMutationSerialization,
				ToMask(ServiceClass::PostMutationRecoveryWork) },
			OwnerServiceMapping{ Owner::VendorWorkGate,
				ToMask(ServiceClass::ProviderLifecycleWork) },
			OwnerServiceMapping{ Owner::PostLoadCompositorHold,
				ToMask(ServiceClass::PresentationHoldWork) },
			OwnerServiceMapping{ Owner::NativeRestore,
				ToMask(ServiceClass::QueuedRelatchWork) |
					ToMask(ServiceClass::RetirementWork) |
					ToMask(ServiceClass::ProviderLifecycleWork) },
			OwnerServiceMapping{ Owner::NativeRestorePresentationGuard,
				ToMask(ServiceClass::PresentationHoldWork) },
			OwnerServiceMapping{ Owner::IntermediateRetirement,
				ToMask(ServiceClass::RetirementWork) },
			OwnerServiceMapping{ Owner::EngineTargetRetirement,
				ToMask(ServiceClass::RetirementWork) },
			OwnerServiceMapping{ Owner::MemoryTrim,
				ToMask(ServiceClass::TrimWork) },
			OwnerServiceMapping{ Owner::DLSSReset,
				ToMask(ServiceClass::ProviderLifecycleWork) },
			OwnerServiceMapping{ Owner::FSRReset,
				ToMask(ServiceClass::ProviderLifecycleWork) },
			OwnerServiceMapping{ Owner::DLSSLifecycle,
				ToMask(ServiceClass::ProviderLifecycleWork) },
			OwnerServiceMapping{ Owner::FSRLifecycle,
				ToMask(ServiceClass::ProviderLifecycleWork) },
			OwnerServiceMapping{ Owner::ResourceTrackingSync,
				ToMask(ServiceClass::ProviderLifecycleWork) },
			OwnerServiceMapping{ Owner::FpsStabilizerSync,
				ToMask(ServiceClass::QueuedRelatchWork) },
		};

	[[nodiscard]] constexpr OwnerMask ResolveOwners(const Facts& a_facts) noexcept
	{
		OwnerMask owners = 0;
		const auto add = [&](Owner a_owner, bool a_active) {
			if (a_active)
				owners |= ToMask(a_owner);
		};

		add(Owner::ControllerTransition,
			a_facts.controllerTransitionEpoch != 0);
		add(Owner::ControllerPresentation,
			a_facts.controllerPresentationEpoch != 0);
		add(Owner::PendingRequest,
			a_facts.pendingRequestPresent && a_facts.pendingRequestID != 0 &&
				a_facts.pendingRequestEpoch != 0);
		add(Owner::DeferredRequest,
			a_facts.deferredRequestPresent && a_facts.deferredRequestID != 0 &&
				a_facts.deferredRequestEpoch != 0);
		add(Owner::PhysicalRelatch,
			(a_facts.physicalRelatchQueued ||
				a_facts.physicalRelatchInProgress) &&
				a_facts.physicalRelatchEpoch != 0);
		add(Owner::PendingPostLoadReset,
			a_facts.postLoadResetPending &&
				a_facts.pendingPostLoadResetEpoch != 0);
		add(Owner::DeferredPostLoadRecovery,
			a_facts.deferredPostLoadRecoveryEpoch != 0);
		add(Owner::ActivePostLoadRecovery,
			a_facts.postLoadRecoveryActive && a_facts.postLoadRecoveryEpoch != 0);
		add(Owner::PreMutationNativeFallback,
			a_facts.preMutationFallbackEpoch != 0);
		add(Owner::ProviderNeutralNativeRecovery,
			a_facts.providerNeutralRecoveryEpoch != 0 &&
				(a_facts.physicalRelatchQueued ||
					a_facts.physicalRelatchInProgress) &&
				a_facts.providerNeutralRecoveryEpoch ==
					a_facts.physicalRelatchEpoch);
		add(Owner::UnresolvedPhysicalMutation,
			a_facts.unresolvedPhysicalMutationEpoch != 0);
		add(Owner::PostMutationSerialization,
			a_facts.postMutationSerializationEpoch != 0);
		add(Owner::VendorWorkGate,
			a_facts.vendorWorkGateOwnerMask != 0);
		add(Owner::PostLoadCompositorHold,
			a_facts.compositorHoldActive && a_facts.compositorHoldEpoch != 0);
		add(Owner::NativeRestore,
			a_facts.nativeRestoreActive && a_facts.nativeRestoreOwnerEpoch != 0);
		add(Owner::NativeRestorePresentationGuard,
			a_facts.nativeRestorePresentationGuardEpoch != 0);
		add(Owner::IntermediateRetirement,
			a_facts.intermediateRetirementPending);
		add(Owner::EngineTargetRetirement,
			a_facts.engineTargetRetirementPending);
		add(Owner::MemoryTrim,
			a_facts.memoryTrimPending && a_facts.memoryTrimOwnerEpoch != 0);
		add(Owner::DLSSReset, a_facts.dlssResetPending);
		add(Owner::FSRReset, a_facts.fsrResetPending);
		add(Owner::DLSSLifecycle, a_facts.dlssLifecycleActive);
		add(Owner::FSRLifecycle, a_facts.fsrLifecycleActive);
		add(Owner::ResourceTrackingSync,
			a_facts.resourceTrackingSyncPending);
		add(Owner::FpsStabilizerSync,
			a_facts.fpsStabilizerSyncFrame != 0);
		return owners;
	}

	[[nodiscard]] constexpr ServiceMask ResolveServices(
		OwnerMask a_owners) noexcept
	{
		ServiceMask services = 0;
		for (const auto& mapping : kOwnerServiceMappings) {
			if ((a_owners & ToMask(mapping.owner)) != 0)
				services |= mapping.services;
		}
		return services;
	}

	[[nodiscard]] constexpr OwnerMask MappedOwners() noexcept
	{
		OwnerMask owners = 0;
		for (const auto& mapping : kOwnerServiceMappings) {
			if (mapping.services != 0)
				owners |= ToMask(mapping.owner);
		}
		return owners;
	}

	[[nodiscard]] constexpr bool AllOwnersHaveServiceMappings() noexcept
	{
		OwnerMask seen = 0;
		for (const auto& mapping : kOwnerServiceMappings) {
			const OwnerMask owner = ToMask(mapping.owner);
			if (owner == 0 || mapping.services == 0 ||
				(mapping.services & ~kAllServiceClasses) != 0 ||
				(seen & owner) != 0) {
				return false;
			}
			seen |= owner;
		}
		return seen == kAllOwners;
	}

	[[nodiscard]] constexpr InconsistencyMask ResolveInconsistencies(
		const Facts& a_facts) noexcept
	{
		InconsistencyMask inconsistencies = 0;
		if (a_facts.unresolvedPhysicalMutationEpoch != 0 &&
			a_facts.postMutationSerializationEpoch == 0) {
			inconsistencies |= ToMask(
				Inconsistency::UnresolvedMutationWithoutSerialization);
		}
		if (a_facts.postMutationSerializationEpoch != 0 &&
			a_facts.postMutationChainSerial == 0) {
			inconsistencies |= ToMask(
				Inconsistency::SerializationWithoutChain);
		}
		if (a_facts.providerNeutralRecoveryEpoch != 0 &&
			(!(a_facts.physicalRelatchQueued ||
				 a_facts.physicalRelatchInProgress) ||
				a_facts.providerNeutralRecoveryEpoch !=
					a_facts.physicalRelatchEpoch)) {
			inconsistencies |= ToMask(
				Inconsistency::ProviderNeutralRecoveryWithoutWorker);
		}
		if (a_facts.preMutationFallbackEpoch != 0 &&
			!a_facts.preMutationFallbackAdmissionActive) {
			inconsistencies |= ToMask(
				Inconsistency::PreMutationFallbackWithoutAdmission);
		}
		return inconsistencies;
	}

	struct Resolution
	{
		OwnerMask owners = 0;
		ServiceMask services = 0;
		OwnerMask unmappedOwners = 0;
		InconsistencyMask inconsistencies = 0;
	};

	[[nodiscard]] constexpr Resolution Resolve(const Facts& a_facts) noexcept
	{
		const OwnerMask owners = ResolveOwners(a_facts);
		return {
			.owners = owners,
			.services = ResolveServices(owners),
			.unmappedOwners = owners & ~MappedOwners(),
			.inconsistencies = ResolveInconsistencies(a_facts),
		};
	}

	[[nodiscard]] constexpr const char* GetOwnerName(Owner a_owner) noexcept
	{
		switch (a_owner) {
		case Owner::ControllerTransition:
			return "controller_transition";
		case Owner::ControllerPresentation:
			return "controller_presentation";
		case Owner::PendingRequest:
			return "pending_request";
		case Owner::DeferredRequest:
			return "deferred_request";
		case Owner::PhysicalRelatch:
			return "physical_relatch";
		case Owner::PendingPostLoadReset:
			return "pending_post_load_reset";
		case Owner::DeferredPostLoadRecovery:
			return "deferred_post_load_recovery";
		case Owner::ActivePostLoadRecovery:
			return "active_post_load_recovery";
		case Owner::PreMutationNativeFallback:
			return "pre_mutation_native_fallback";
		case Owner::ProviderNeutralNativeRecovery:
			return "provider_neutral_native_recovery";
		case Owner::UnresolvedPhysicalMutation:
			return "unresolved_physical_mutation";
		case Owner::PostMutationSerialization:
			return "post_mutation_serialization";
		case Owner::VendorWorkGate:
			return "vendor_work_gate";
		case Owner::PostLoadCompositorHold:
			return "post_load_compositor_hold";
		case Owner::NativeRestore:
			return "native_restore";
		case Owner::NativeRestorePresentationGuard:
			return "native_restore_presentation_guard";
		case Owner::IntermediateRetirement:
			return "intermediate_retirement";
		case Owner::EngineTargetRetirement:
			return "engine_target_retirement";
		case Owner::MemoryTrim:
			return "memory_trim";
		case Owner::DLSSReset:
			return "dlss_reset";
		case Owner::FSRReset:
			return "fsr_reset";
		case Owner::DLSSLifecycle:
			return "dlss_lifecycle";
		case Owner::FSRLifecycle:
			return "fsr_lifecycle";
		case Owner::ResourceTrackingSync:
			return "resource_tracking_sync";
		case Owner::FpsStabilizerSync:
			return "fps_stabilizer_sync";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] constexpr const char* GetServiceClassName(
		ServiceClass a_service) noexcept
	{
		switch (a_service) {
		case ServiceClass::QueuedRelatchWork:
			return "queued_relatch_work";
		case ServiceClass::PreMutationFallbackWork:
			return "pre_mutation_fallback_work";
		case ServiceClass::PostLoadRecoveryWork:
			return "post_load_recovery_work";
		case ServiceClass::PostMutationRecoveryWork:
			return "post_mutation_recovery_work";
		case ServiceClass::RetirementWork:
			return "retirement_work";
		case ServiceClass::TrimWork:
			return "trim_work";
		case ServiceClass::PresentationHoldWork:
			return "presentation_hold_work";
		case ServiceClass::ProviderLifecycleWork:
			return "provider_lifecycle_work";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] constexpr const char* GetInconsistencyName(
		Inconsistency a_inconsistency) noexcept
	{
		switch (a_inconsistency) {
		case Inconsistency::UnresolvedMutationWithoutSerialization:
			return "unresolved_mutation_without_serialization";
		case Inconsistency::SerializationWithoutChain:
			return "serialization_without_chain";
		case Inconsistency::ProviderNeutralRecoveryWithoutWorker:
			return "provider_neutral_recovery_without_worker";
		case Inconsistency::PreMutationFallbackWithoutAdmission:
			return "pre_mutation_fallback_without_admission";
		default:
			return "unknown";
		}
	}

	static_assert(AllOwnersHaveServiceMappings());
}
