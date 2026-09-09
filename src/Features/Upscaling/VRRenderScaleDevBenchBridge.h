#pragma once

#include <cstdint>

namespace VRSubmitInputFreshnessPolicy
{
	enum class OuterBoundaryRejection : std::uint8_t;
	enum class ProducerRejection : std::uint8_t;
}

/** @brief Registers the render-scale iteration tool with the external devbench host. */
namespace VRRenderScaleDevBenchBridge
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	enum class SubmitFreshnessWork : std::uint8_t
	{
		FallbackPreparedHits,
		FallbackOutputHits,
		GuideEncodeEyes,
		ColorCopyEyes,
		InputSanitizationEyes,
		VendorEyeAttempts,
		VendorEyeRetries,
		Count
	};

	/** Records a pair-boundary admission outcome in fixed process-lifetime counters. */
	void RecordSubmitBoundaryRejection(
		VRSubmitInputFreshnessPolicy::OuterBoundaryRejection a_reason) noexcept;

	/** Records a producer admission outcome separately for each vendor method. */
	void RecordSubmitInputRejection(
		VRSubmitInputFreshnessPolicy::ProducerRejection a_reason,
		std::uint32_t a_method) noexcept;

	/** Counts actual submit work without allocating or logging on the render thread. */
	void RecordSubmitFreshnessWork(
		SubmitFreshnessWork a_work,
		std::uint32_t a_method,
		std::uint64_t a_amount = 1) noexcept;

	enum class PresentationAuditSelection : std::uint8_t
	{
		Observed,
		BlackKeepalive,
		Quarantine
	};

	enum class PhysicalMutationBoundarySource : std::uint8_t
	{
		EngineTargetCreator,
		ProviderInvalidation,
		ProviderActivation
	};

	struct PresentationAuditObservation
	{
		bool valid = false;
		std::uint32_t eyeIndex = 0;
		std::uint32_t frame = 0;
		std::uint64_t compositorCycleToken = 0;
		std::uint64_t transitionEpoch = 0;
		std::uint32_t contractGeneration = 0;
		std::uint32_t method = 0;
		std::uint32_t backend = 0;
		std::uint32_t vendorDispatchFrame = 0;
		std::uint64_t vendorDispatchSerial = 0;
		bool vendorRuntimeFallback = false;
		std::uint32_t path = 0;
		std::uintptr_t deviceIdentity = 0;
		std::uint64_t resourceRevision = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t displayWidth = 0;
		std::uint32_t displayHeight = 0;
		bool loadingOrMenuContext = false;
		bool transitionCooldown = false;
		bool submitted = false;
		PresentationAuditSelection selection = PresentationAuditSelection::Observed;
	};

	/** Records one authoritative compositor decision for the active DevBench owner. */
	void RecordPresentationAuditObservation(
		const PresentationAuditObservation& a_observation) noexcept;

	/** Retains the first owner-bound destructive boundary before polling can miss it. */
	void RecordPhysicalMutationBoundary(
		std::uint64_t a_transitionEpoch,
		PhysicalMutationBoundarySource a_source,
		std::uint32_t a_providerMethod = 0) noexcept;
#endif

	/**
	 * @brief Installs the optional MCP/REST bridge after SKSE data loading.
	 *
	 * This is idempotent and becomes a no-op when the bridge was disabled at
	 * build time or the external devbench host is not installed.
	 */
	void Install();

	/** @brief Returns whether this binary contains devbench API support. */
	bool IsBuilt();

	/** @brief Returns whether the render-scale tool registered with a live host. */
	bool IsRegistered();
}
