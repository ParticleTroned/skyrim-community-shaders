#pragma once

#include <cstdint>

/** @brief Registers the render-scale iteration tool with the external devbench host. */
namespace VRRenderScaleDevBenchBridge
{
#ifdef DEVBENCH_BRIDGE_ENABLED
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
