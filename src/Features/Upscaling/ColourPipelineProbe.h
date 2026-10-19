#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "ColourPipelineProbePolicy.h"
#	include "FSRDispatchInputTelemetry.h"
#	include <d3d11.h>
#	include <nlohmann/json_fwd.hpp>

#	include <cstdint>
#	include <string>

namespace CSX::Diagnostics::ColourPipelineProbe
{
	struct DispatchMetadata
	{
		std::uint64_t colourContractRevision = 0;
		std::uint32_t frame = 0;
		std::uint64_t dispatchSerial = 0;
		std::uint64_t contextGeneration = 0;
		std::uint32_t contextIndex = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t displayWidth = 0;
		std::uint32_t displayHeight = 0;
		bool requestedHighDynamicRangeInput = false;
		bool requestedAutoExposure = false;
		bool effectiveHighDynamicRangeInput = false;
		bool effectiveAutoExposure = false;
		bool exposureResourceBound = false;
		float preExposure = 1.0f;
		float configuredSharpness = 0.0f;
		float effectiveSharpness = 0.0f;
		bool sharpeningEnabled = false;
		std::uint64_t dispatchQpc = 0;
		FSRDispatchInputTelemetry::Snapshot submittedInputs{};
		std::string path;
	};

	/** Arm one bounded VR capture; return its immutable generation receipt. */
	bool Arm(
		const std::string& a_captureId,
		std::uint64_t a_expectedColourContractRevision,
		const nlohmann::json& a_metadata,
		std::uint64_t& a_generation,
		std::string& a_error);
	/** Release a matching terminal capture; active captures cannot be reset. */
	bool Reset(const std::string& a_captureId, std::uint64_t a_expectedGeneration,
		std::uint64_t& a_generation, std::string& a_error);
	/** Test whether an opted-in capture still needs render-thread observations. */
	bool WantsVendorCapture() noexcept;
	/** Service nonblocking readback on the capture's immediate context. */
	void ServiceReadbacks(ID3D11DeviceContext* a_context, std::uint32_t a_cpuFrame) noexcept;
	/** Queue one bounded vendor texture region with coherent dispatch metadata. */
	void CaptureVendorStage(
		Stage a_stage,
		std::uint32_t a_eye,
		ID3D11Texture2D* a_texture,
		ID3D11ShaderResourceView* a_srv,
		ID3D11RenderTargetView* a_rtv,
		ID3D11UnorderedAccessView* a_uav,
		std::uint32_t a_activeWidth,
		std::uint32_t a_activeHeight,
		const DispatchMetadata& a_dispatch,
		const char* a_symbol,
		const char* a_callsite) noexcept;
	/** Queue main-source or VR destination eyes with their actual target identity. */
	void CaptureImageSpaceStage(
		Stage a_stage,
		ID3D11Texture2D* a_texture,
		ID3D11ShaderResourceView* a_srv,
		ID3D11RenderTargetView* a_rtv,
		ID3D11UnorderedAccessView* a_uav,
		std::uint32_t a_engineTarget,
		bool a_matchesMainTarget,
		const char* a_symbol,
		const char* a_callsite) noexcept;
	/** Record one actual queued copy, matching the retained FSR source. */
	void RecordFsrOutputCopyBack(
		ID3D11Resource* a_combinedDestination,
		ID3D11Texture2D* a_source,
		std::uint32_t a_eye,
		std::uint32_t a_destinationX,
		std::uint32_t a_width,
		std::uint32_t a_height,
		const char* a_symbol,
		const char* a_callsite) noexcept;
	/** Return current capture identity/state, enforcing its wall deadline. */
	nlohmann::json BuildStatus();
	/** Read one correlated stage/eye page, rejecting a replaced capture. */
	nlohmann::json BuildCapture(const std::string& a_captureId,
		std::uint64_t a_generation, Stage a_stage, std::uint32_t a_eye);
}

#endif
