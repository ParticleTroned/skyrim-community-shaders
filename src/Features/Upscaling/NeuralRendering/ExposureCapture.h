#pragma once
#include "ExposurePolicy.h"
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <string>
#include <wrl/client.h>

namespace RE
{
	class BSShader;
}

namespace NeuralRendering::Color
{
	enum class ExposureBindingState : std::uint32_t
	{
		NotRequested,
		WaitingForHDRPass,
		StaleOrAmbiguous,
		ContextMismatch,
		SnapshotQueued,
		ResourceFailure
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
	struct ExposureBindingObservation
	{
		std::uint32_t frame = 0, width = 0, height = 0, mip = 0, mipLevels = 0;
		std::uint32_t arraySize = 0, samples = 0, sourceFormat = 0, viewFormat = 0, viewDimension = 0;
		std::uint64_t sourceIdentity = 0, shaderIdentity = 0;
		std::uint64_t expectedShaderIdentity = 0, viewIdentity = 0;
	};
	struct ExposureCaptureStatus
	{
		bool requested = false;
		std::uint32_t producersRegistered = 0;
		ExposureBindingObservation lastBinding{};
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
		void Abandon() noexcept
		{
			(void)resource.Detach();
			(void)srv.Detach();
		}
	};

	// Only Request/GetStatus are called from UI/DevBench threads. Installation,
	// capture, binding and resource retirement belong to the render thread.
	class ExposureCapture
	{
	public:
		static ExposureCapture& Instance();
		void Request(bool) noexcept;
		/// Resolve the exact HDR shader instances on the render thread.
		void RefreshProducers() noexcept;
		/// Observe live bindings at the immediate context's HDR draw boundary.
		void ObserveDraw(ID3D11DeviceContext*, RE::BSShader*) noexcept;
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
