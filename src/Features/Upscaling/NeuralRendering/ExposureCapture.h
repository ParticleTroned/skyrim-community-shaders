#pragma once
#include "ExposurePolicy.h"
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <optional>
#include <string>
#include <wrl/client.h>

namespace RE
{
	class BSShader;
}

namespace NeuralRendering::Color
{
	struct ExposureDrawBindings
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> average;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
	};
	/** Read the finalized HDR inputs without depending on a D3D11 draw entry point. */
	[[nodiscard]] inline ExposureDrawBindings ReadExposureDrawBindings(ID3D11DeviceContext* context) noexcept
	{
		ExposureDrawBindings bindings;
		if (context) {
			context->PSGetShaderResources(2, 1, &bindings.average);
			context->PSGetShader(&bindings.shader, nullptr, nullptr);
		}
		return bindings;
	}
	enum class ExposureDrawKind : std::uint32_t
	{
		Indexed,
		Direct,
		IndexedInstanced,
		Instanced,
		Auto,
		IndexedIndirect,
		Indirect,
		Count
	};
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

	/** Uniform texels remain scalar only under ordinary, non-border sampling. */
	[[nodiscard]] inline bool SupportedExposureSampler(const D3D11_SAMPLER_DESC& sampler)
	{
		const auto address = [](D3D11_TEXTURE_ADDRESS_MODE mode) {
			return mode == D3D11_TEXTURE_ADDRESS_CLAMP || mode == D3D11_TEXTURE_ADDRESS_WRAP ||
			       mode == D3D11_TEXTURE_ADDRESS_MIRROR || mode == D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
		};
		if (!address(sampler.AddressU) || !address(sampler.AddressV))
			return false;
		switch (sampler.Filter) {
		case D3D11_FILTER_MIN_MAG_MIP_POINT:
		case D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR:
		case D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT:
		case D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR:
		case D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT:
		case D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
		case D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:
		case D3D11_FILTER_MIN_MAG_MIP_LINEAR:
		case D3D11_FILTER_ANISOTROPIC:
			return true;
		default:
			return false;
		}
	}

	struct ExposureEvidence
	{
		ExposureStamp stamp{};
		std::uint32_t sourceFormat = 0, sourceViewFormat = 0, outputViewFormat = 0;
		std::uint32_t sourceWidth = 0, sourceHeight = 0, sourceMip = 0;
		// Process-local diagnostic identities, never accepted as addresses by the API.
		std::uint64_t sourceIdentity = 0, shaderIdentity = 0;
		std::uint64_t sourceViewIdentity = 0, samplerIdentity = 0;
		std::array<float, 4> values{};
		std::array<std::array<float, 4>, kExposureTexelCount> texels{};
		float frameGammaExponent = 0;
		bool gammaKnown = false, readbackComplete = false;
		const char* producer = "";  // Static producer label; no per-frame string allocation.
	};
	struct ExposureBindingObservation
	{
		std::uint32_t frame = 0, width = 0, height = 0, mip = 0, mipLevels = 0;
		std::uint32_t arraySize = 0, samples = 0, sourceFormat = 0, viewFormat = 0, viewDimension = 0;
		std::uint32_t viewWidth = 0, viewHeight = 0, visibleMips = 0;
		std::uint32_t samplerFilter = 0, samplerAddressU = 0, samplerAddressV = 0;
		std::uint64_t samplerIdentity = 0;
		std::uint64_t sourceIdentity = 0, shaderIdentity = 0;
		std::uint64_t expectedShaderIdentity = 0, viewIdentity = 0;
	};
	struct ExposureCaptureStatus
	{
		bool requested = false;
		std::uint32_t producersRegistered = 0;
		ExposureBindingObservation lastBinding{};
		std::uint64_t epoch = 0, captures = 0, rejected = 0, dropped = 0;
		std::uint64_t producerScopes = 0;
		std::uint32_t lastProducerFrame = 0;
		std::uint64_t graphicsStateFlushes = 0;
		std::uint32_t lastGraphicsStateFlushFrame = 0;
		std::array<std::uint64_t, static_cast<std::size_t>(ExposureDrawKind::Count)> drawCounts{};
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
		/// Record the exact pixel shader selected by the engine/replacement binding hook.
		void ObservePixelShaderSelection(ID3D11DeviceContext*, RE::BSShader*,
			const void* a_engineSelection, ID3D11PixelShader*) noexcept;
		/// Observe live bindings at the immediate context's HDR draw boundary.
		void ObserveDraw(ID3D11DeviceContext*, ExposureDrawKind) noexcept;
		/// Observe finalized graphics bindings inside the exact HDR producer scope.
		void ObserveGraphicsStateFlush(ID3D11DeviceContext*, bool a_isCompute) noexcept;
		/// Establish the exact engine effect owner for nested draw callbacks.
		RE::BSShader* EnterProducer(RE::BSShader*) noexcept;
		void LeaveProducer(RE::BSShader*) noexcept;
		ExposureCaptureStatus GetStatus() const;
		bool Bind(ID3D11DeviceContext*, ExposureBinding&, const ExposureTransaction&);
		void Reset() noexcept;    // Called after Renderer's existing idle/retirement boundary.
		void Abandon() noexcept;  // Device-loss/unfenced path: do not release ownership.
	private:
		ExposureCapture();
		void Observe(ID3D11DeviceContext*, std::optional<ExposureDrawKind>) noexcept;
		struct State;
		State* state_;
	};
	/** Restore the enclosing producer even when an effect exits exceptionally. */
	class ExposureProducerScope
	{
	public:
		explicit ExposureProducerScope(RE::BSShader* producer) noexcept :
			previous_(ExposureCapture::Instance().EnterProducer(producer)) {}
		~ExposureProducerScope() { ExposureCapture::Instance().LeaveProducer(previous_); }
		ExposureProducerScope(const ExposureProducerScope&) = delete;
		ExposureProducerScope& operator=(const ExposureProducerScope&) = delete;

	private:
		RE::BSShader* previous_;
	};
}
