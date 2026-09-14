#pragma once
#include "ExposurePolicy.h"
#include <array>
#include <cstdint>
#include <string>
#include <d3d11.h>
#include <wrl/client.h>

namespace NeuralRendering::Color
{
	enum class ExposureBindingState : std::uint32_t
	{
		NotRequested, WaitingForHDRPass, StaleOrAmbiguous, ContextMismatch,
		SnapshotQueued, ResourceFailure
	};
	const char* ExposureBindingName(ExposureBindingState) noexcept;

	struct ExposureEvidence
	{
		ExposureStamp stamp{};
		std::uint32_t sourceFormat = 0, sourceViewFormat = 0, outputViewFormat = 0;
		// Process-local diagnostic identities, never accepted as addresses by the API.
		std::uint64_t sourceIdentity = 0, shaderIdentity = 0;
		std::array<float, 4> values{};
		float frameGammaExponent = 0;
		bool gammaKnown = false, readbackComplete = false;
		const char* producer = "";  // Static producer label; no per-frame string allocation.
	};
	struct ExposureCaptureStatus
	{
		bool requested = false;
		std::uint32_t hooksInstalled = 0;
		std::uint64_t epoch = 0, captures = 0, rejected = 0, dropped = 0;
		std::string lastReason;
		std::array<ExposureEvidence, 8> samples{};
	};
	struct ExposureBinding
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> resource;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		ExposureEvidence evidence{};
		ExposureBindingState state = ExposureBindingState::NotRequested;
		void Abandon() noexcept { (void)resource.Detach(); (void)srv.Detach(); }
	};

	// Only Request/GetStatus are called from UI/DevBench threads. Installation,
	// capture, binding and resource retirement belong to the render thread.
	class ExposureCapture
	{
	public:
		static ExposureCapture& Instance();
		void Request(bool) noexcept;
		void InstallHooks() noexcept;
		ExposureCaptureStatus GetStatus() const;
		bool Bind(ID3D11DeviceContext*, ExposureBinding&, const ExposureTransaction&);
		void Reset() noexcept;    // Called after Renderer's existing idle/retirement boundary.
		void Abandon() noexcept;  // Device-loss/unfenced path: do not release ownership.
	private:
		ExposureCapture();
		struct State;
		State* state_;
	};
}
