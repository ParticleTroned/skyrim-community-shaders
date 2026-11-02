#pragma once

#include "CharacterSettings.h"
#include "MonoPolicy.h"

#include <array>
#include <cstdint>
#include <d3d11.h>
#include <string>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "MonoExperiments.h"
#	include <nlohmann/json.hpp>
#endif

namespace NeuralRendering
{
	struct MonoFrame
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		ID3D11Texture2D* color = nullptr;
		std::uint32_t frame = 0, width = 0, height = 0;
		bool linearColor = false, reset = false;
		std::array<float, 4> depthLinearization{};
		std::array<float, 2> guideJitter{};
		std::array<float, 2> nativeJitter{};
	};
	struct MonoSnapshot
	{
		std::string status = "inactive";
		std::string detail;
		std::string compressionStatus = "disabled", prewarmStatus = "disabled";
		std::uint64_t stallRecoveries = 0;
		std::uint32_t stallAttempts = 0;
		bool recoveryPending = false;
		std::uint64_t evaluations = 0, presented = 0, historyResets = 0, prewarms = 0;
		std::uint32_t modelWidth = 0, modelHeight = 0, effectivePercent = 100;
		std::uint32_t packedRegions = 0;
		std::uint64_t packedCoverageRejected = 0;
		std::uint32_t evaluationFrame = UINT32_MAX, presentationFrame = UINT32_MAX;
		std::uint32_t sourceWidth = 0, sourceHeight = 0, exposureFrame = UINT32_MAX;
		std::uint64_t exposureEpoch = 0, exposureSequence = 0;
		bool packedCoverageProven = false;
		bool quarantined = false;
	};

	/** Owns only a single-view inference path; the caller retains its SE upscaler and presentation graph. */
	class MonoRenderer
	{
	public:
		static MonoRenderer& Instance();
		/** A stable whole-actor decision is reused by every material draw in this frame. */
		ActorAdmission ObserveActor(const ActorObservation&, const MonoSettings&);
		/** Publish source-frame categories with the depth against which they were authored. */
		void SetSceneCategories(ID3D11ShaderResourceView*, ID3D11ShaderResourceView*, std::uint32_t frame,
			std::uint32_t width, std::uint32_t height);
		/** Snapshot guides before SE depth reconstruction or post-processing changes their contents. */
		bool CaptureGuides(ID3D11Device*, ID3D11DeviceContext*, ID3D11ShaderResourceView* depth,
			ID3D11ShaderResourceView* motion, std::uint32_t frame, std::uint32_t width, std::uint32_t height);
		/** Prepare the exact single-view creation layout at an authoritative main/loading menu boundary. */
		void Prewarm(const MonoFrame&, std::uint32_t guideWidth, std::uint32_t guideHeight, const MonoSettings&);
		/** Return an owned result on success; failure never changes the source scene. */
		ID3D11Texture2D* Apply(const MonoFrame&, const MonoSettings&);
		/** Copy the completed mono result into any supported scene RTV before the UI is drawn. */
		bool Commit(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D* destination);
		void AcknowledgePresentation();
		void ReportDlssMemoryPressure() noexcept;
		/** Service pending retirements while paused/disabled without running inference. */
		void Service(bool requested, bool configured = true, bool prewarmEnabled = true);
		/** Shutdown may be retried on following render boundaries until both API tails complete. */
		bool Retire(bool shutdown = false);
		/** Invalidate source-frame evidence after world replacement while retaining safely owned native allocations. */
		void ResetWorldHistory();
		void ResetShaderCache();
		MonoSnapshot Snapshot() const;
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Session-only input and reconstruction controls, applied at a safe render boundary. */
		bool SetExperiments(const MonoExperiments&);
		MonoExperiments Experiments() const;
		/** Whether observed opaque NPC face bounds are needed by the active experiment. */
		bool WantsFaceDetail() const noexcept;
		void ObserveFaceDetail(std::uint32_t frame, std::uint32_t actor, const CharacterRect&,
			std::uint32_t width, std::uint32_t height);
		/** CPU snapshot of real native calls; frame-specific copies can outlive the bounded live ring. */
		nlohmann::json Diagnostics(std::uint32_t sourceFrame = UINT32_MAX) const;
#endif

	private:
		MonoRenderer();
		~MonoRenderer();
		struct State;
		State* state_;
	};
}
