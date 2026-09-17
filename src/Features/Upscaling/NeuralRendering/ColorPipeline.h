#pragma once

#include "ColorMeasurementBatch.h"
#include "ColorPolicy.h"
#include "ComputeSubrect.h"
#include "ExposureCapture.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <mutex>
#include <string>
#include <wrl/client.h>

namespace NeuralRendering::Color
{
	inline constexpr std::size_t kMeasurementValues = 24;
	struct Observation
	{
		std::uint32_t frame = 0, sourceWorldFrame = 0, slot = 0, insertion = 0;
		std::uint64_t generation = 0, revision = 0;
		std::uint64_t measurementOrder = 0;  // Monotonic CPU submission order, not a frame timestamp.
		std::uint64_t measurementBatchId = 0;
		std::uint32_t expectedMeasurementSlotMask = 0;
		ComputeSubrect rect{};
		std::uint32_t sourceFormat = 0, outputFormat = 0;
		Mode mode = Mode::LegacyRaw;
		Profile profile{};
		bool bypass = false, atomicStereo = false, processed = false;
		bool modelEditShown = true;
		float lightingPreservation = 1.0f;
		ExposureBindingState exposureState = ExposureBindingState::NotRequested;
		ExposureEvidence exposure{};
		std::uint64_t preparationCpuMicroseconds = 0, reconstructionCpuMicroseconds = 0, retainedBytes = 0;
		std::string failure;
	};
	struct Measurement
	{
		Observation source{};
		// First 16 entries retain API-v1 meanings. Last 8 expose codec validity
		// and the exact exposure snapshot used by this measured transaction.
		std::array<float, kMeasurementValues> data{};
	};
	struct Status
	{
		std::array<Observation, 8> slots{};
		std::array<Measurement, 8> measurements{};
		std::array<MeasurementBatch<Measurement>, 4> measurementBatches{};
		std::uint64_t evictedIncompleteBatches = 0;
		std::uint64_t prepared = 0, reconstructed = 0, failed = 0, bypassed = 0, samples = 0, dropped = 0;
	};
	class Registry
	{
	public:
		static Registry& Instance();
		Configuration Snapshot() const;
		Status GetStatus() const;
		/** Retain only CPU measurement evidence for an accepted screenshot. */
		MeasurementBatchHistory<Measurement>::Lease PinMeasurementBatch(const MeasurementBatchKey&);
		bool CaptureEvidenceEnabled() const noexcept { return captureEvidenceEnabled_.load(std::memory_order_acquire); }
		bool Configure(const Settings&, const Experiments&, std::uint64_t expectedRevision = 0);
		void Record(const Observation&) noexcept;
		void Record(const Measurement&) noexcept;
		void DropMeasurement() noexcept;

	private:
		mutable std::mutex mutex_;
		Configuration configuration_{};
		std::atomic_bool captureEvidenceEnabled_{ false };
		Status status_{};
		MeasurementBatchHistory<Measurement> measurementBatches_{};
	};
	struct Texture
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> resource;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
		void Abandon() noexcept;
	};
	struct Readback
	{
		Microsoft::WRL::ComPtr<ID3D11Buffer> gpu, staging;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
		Microsoft::WRL::ComPtr<ID3D11Query> ready;
		Observation source{};
		bool pending = false;
		void Abandon() noexcept;
	};
	struct Work
	{
		Texture baseline, result;
		ExposureBinding exposure;
		std::array<Readback, 3> readbacks{};
		std::uint32_t capacityWidth = 0, capacityHeight = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		bool readbackAttempted = false, prepared = false;
		Configuration configuration{};
		Observation observation{};
		void Abandon() noexcept;
	};
	class Pipeline
	{
	public:
		/** Drain pending readbacks even when their physical region is no longer selected. */
		void Poll(ID3D11DeviceContext*, Work&);
		/** Monotonic identity survives resource resets; zero means exhausted diagnostics. */
		std::uint64_t BeginMeasurementBatch() noexcept;
		bool Ensure(ID3D11Device*, Work&, const ComputeSubrect&, DXGI_FORMAT, bool diagnostics);
		bool Prepare(ID3D11DeviceContext*, Work&, ID3D11Resource* original,
			ID3D11Resource* prepared, ID3D11UnorderedAccessView* preparedUAV,
			const Configuration&, Observation);
		bool Reconstruct(ID3D11DeviceContext*, Work&, ID3D11Resource* neural,
			ID3D11ShaderResourceView* neuralSRV, ID3D11ShaderResourceView* preparedSRV, const Configuration&);
		void Commit(ID3D11DeviceContext*, const Work&, ID3D11Resource* destination);
		void Reset() noexcept;
		void Abandon() noexcept;

	private:
		bool EnsureShaders(ID3D11Device*, bool diagnostics);
		void Measure(ID3D11DeviceContext*, Work&, ID3D11ShaderResourceView* neural, ID3D11ShaderResourceView* prepared);
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> prepare_, reconstruct_, measure_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
		bool compileFailed_ = false, measureCompileAttempted_ = false;
		std::uint64_t measurementOrder_ = 0;  // Intentionally survives Reset, like Registry status.
		std::uint64_t measurementBatchOrder_ = 0;
	};
}
