#pragma once

#include "Features/VR/OpenVRSubmitLeasePolicy.h"
#include "RenderMap/Runtime.h"

#include <optional>
#include <shared_mutex>

namespace CSX::RenderMap
{
	/** Publication and CPU-frame evidence sampled while recreation is excluded. */
	struct RetainedEyePublication
	{
		std::uint64_t generation = 0;
		std::uintptr_t device = 0;
		std::uint64_t cpuFrame = kUnknownFrame;
		ResourceObservationInput resource;
	};

	/** Keep publication validation and recording in one shared claim/use interval. */
	template <class ObservePublication>
	inline void RecordRetainedEyeSubmission(
		Runtime& a_runtime,
		std::shared_mutex& a_recreationMutex,
		const OpenVRSubmitLeasePolicy::PublicationLease& a_lease,
		std::uint64_t a_captureGeneration,
		ObservePublication&& a_observePublication,
		Eye a_eye,
		std::uint8_t a_eyeMask,
		float a_uMin,
		float a_vMin,
		float a_uMax,
		float a_vMax,
		std::uint32_t a_submitFlags,
		std::uint64_t a_compositorCycle) noexcept
	{
		const std::shared_lock publicationLock(a_recreationMutex);
		if (a_captureGeneration == 0 || a_runtime.ActiveCaptureGeneration() != a_captureGeneration)
			return;
		const auto publication = a_observePublication();
		if (!publication || !OpenVRSubmitLeasePolicy::CanPublish(a_lease, publication->generation, publication->device))
			return;
		a_runtime.RecordEyeSubmission(publication->resource, a_eye, a_eyeMask,
			a_uMin, a_vMin, a_uMax, a_vMax, a_submitFlags, a_compositorCycle,
			a_lease.generation, a_captureGeneration, publication->cpuFrame);
	}
}
