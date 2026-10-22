#pragma once

#include "Features/VR/OpenVRSubmitLeasePolicy.h"
#include "RenderMap/Runtime.h"

namespace CSX::RenderMap
{
	/** Record accepted native eye evidence only for the retained current publication. */
	inline void RecordRetainedEyeSubmission(
		Runtime& a_runtime,
		const OpenVRSubmitLeasePolicy::PublicationLease& a_lease,
		std::uint64_t a_currentGeneration,
		std::uintptr_t a_currentDevice,
		const ResourceObservationInput& a_resource,
		Eye a_eye,
		std::uint8_t a_eyeMask,
		float a_uMin,
		float a_vMin,
		float a_uMax,
		float a_vMax,
		std::uint32_t a_submitFlags,
		std::uint64_t a_compositorCycle) noexcept
	{
		if (!OpenVRSubmitLeasePolicy::CanPublish(a_lease, a_currentGeneration, a_currentDevice))
			return;
		a_runtime.RecordEyeSubmission(a_resource, a_eye, a_eyeMask,
			a_uMin, a_vMin, a_uMax, a_vMax, a_submitFlags, a_compositorCycle, a_lease.generation);
	}
}
