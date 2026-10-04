#pragma once

#include "LifetimeDiagnostics.h"
#include "NativeEvaluationLayout.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct NVSDK_NGX_Parameter;

namespace NeuralRendering
{
	/** Opt-in native kernel backend; unsupported epochs retain the original provider path. */
	class ExperimentalKernelBatch
	{
	public:
		enum class Mode
		{
			Original,
			LayerControl,
			ClonedN2,
			SharedN2
		};
		struct Region
		{
			unsigned eye = 0, region = 0, slot = 0;
			NativeEvaluationLayout layout{};
			bool reset = false, controlMask = false;
		};
		struct Frame
		{
			std::uint64_t sequence = 0;
			unsigned style = 0;
			bool useAutoMask = true, uiCorrection = false;
			bool inspectUnqualifiedPipeline = false;
			float singleSubrectScale = 1.0f;
			std::array<Region, 4> regions{};
		};
		struct Status
		{
			bool initialized = false, recording = false, failed = false, epochStale = false;
			bool warmup = true, retirementProven = true;
			bool canFallback = false;
			Mode mode = Mode::Original;
			std::uint64_t frames = 0, warmupFrames = 0, batchedFrames = 0;
			std::uint64_t logicalLaunches = 0, physicalLaunches = 0, privateLaunches = 0;
			std::size_t pendingFrames = 0;
			bool inspection = false, graphMatchesQualified = false;
			std::array<std::size_t, 4> graphLaunches{};
			std::array<std::string, 4> graphIdentities{};
			std::array<std::string, 4> graphFamilyIdentities{};
			std::array<std::array<std::size_t, 2>, 2> regionPairs{};
			std::string reason;
		};

		ExperimentalKernelBatch();
		~ExperimentalKernelBatch();
		ExperimentalKernelBatch(const ExperimentalKernelBatch&) = delete;
		ExperimentalKernelBatch& operator=(const ExperimentalKernelBatch&) = delete;

		/** Attach only after native initialization and before any feature creation in this epoch. */
		bool Initialize(ID3D12Device* device, const std::filesystem::path& provider,
			const std::filesystem::path& manifest, Mode mode) noexcept;
		/** Reject unsupported geometry before beginning capture; outside capture hooks forward unchanged. */
		bool BeginFrame(ID3D12GraphicsCommandList* commandList, const LifetimeFenceSnapshot& before,
			const Frame& frame) noexcept;
		ID3D12GraphicsCommandList* BeginEvaluation(unsigned eye, unsigned region) noexcept;
		/** Validates the actual parameters immediately before native feature creation. */
		bool ValidateFeatureCreation(const NVSDK_NGX_Parameter* parameters) noexcept;
		/** Commits the pending allocation proof only after native creation returned a valid handle. */
		bool CommitFeatureCreation() noexcept;
		bool EndEvaluation() noexcept;
		/** Complete recording before EndD3D12; failure requires aborting the unsubmitted real list. */
		bool FinishFrame() noexcept;
		/** Call immediately after successful EndD3D12; verifies the exact changed context fence. */
		bool Submitted(const LifetimeFenceSnapshot& after) noexcept;
		/** Call only after AbortD3D12 has proven that this frame was never submitted. */
		void Aborted() noexcept;
		/** An unproven idle or restoration retains all native/frame owners until process exit. */
		bool Shutdown(bool gpuIdle) noexcept;
		[[nodiscard]] Status GetStatus() const;
		[[nodiscard]] static std::string_view ModeName(Mode mode) noexcept;
		[[nodiscard]] static std::string_view AdmissionViolation(const Frame& frame) noexcept;

	private:
		struct State;
		std::unique_ptr<State> state_;
	};
}
