#include "ColorPipeline.h"
#include "ComputeStateGuard.h"
#include "GpuPass.h"
#include "Utils/D3D.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

namespace NeuralRendering::Color
{
	using Microsoft::WRL::ComPtr;
	namespace
	{
		struct Constants
		{
			std::uint32_t x, y, width, height;
			std::uint32_t mode, domain, transform, flags;
			float exposure, detail, appearance, maximumStops;
			float lightingPreservation;
			float padding[3]{};
		};
		static_assert(sizeof(Constants) == 64);
		static_assert(offsetof(Constants, lightingPreservation) == 48);
		static_assert(offsetof(Constants, exposure) == 32);

		Storage OutputStorage(DXGI_FORMAT format)
		{
			switch (format) {
			case DXGI_FORMAT_R11G11B10_FLOAT:
				return Storage::R11G11B10;
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
				return Storage::Float16;
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			case DXGI_FORMAT_R16G16B16A16_UNORM:
				return Storage::UNorm;
			default:
				return Storage::Float32;  // Ensure admits only the explicit formats below.
			}
		}
		Constants MakeConstants(const Work& work)
		{
			const auto& config = work.configuration;
			const auto settings = ResolveReconstructionSettings(config.settings);
			const auto& roi = work.observation.rect;
			const auto& profile = work.observation.profile;
			const std::uint32_t flags = (config.experiments.transportBypass ? 1u : 0u) |
			                            (!config.experiments.applyModelEdit ? 2u : 0u) |
			                            (profile.exposureSource != ExposureSource::Manual ? 4u : 0u) |
			                            (NeedsExposureCapture(config) ? 8u : 0u) | static_cast<std::uint32_t>(OutputStorage(work.format));
			return { roi.baseX, roi.baseY, roi.width, roi.height,
				static_cast<std::uint32_t>(settings.mode), static_cast<std::uint32_t>(profile.domain),
				static_cast<std::uint32_t>(profile.transform), flags, profile.exposureMultiplier,
				settings.detailStrength, settings.appearanceMix, settings.maximumDetailStops,
				settings.lightingPreservation, {} };
		}
		bool CreateTexture(ID3D11Device* device, Texture& texture,
			std::uint32_t width, std::uint32_t height, DXGI_FORMAT format, bool output)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (output ? D3D11_BIND_UNORDERED_ACCESS : 0u);
			const bool created = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture.resource)) &&
			                     SUCCEEDED(device->CreateShaderResourceView(texture.resource.Get(), nullptr, &texture.srv)) &&
			                     (!output || SUCCEEDED(device->CreateUnorderedAccessView(texture.resource.Get(), nullptr, &texture.uav)));
			const char* name = output ? "NeuralRendering::ColourReconstruction" : "NeuralRendering::ColourBaseline";
			if (texture.resource)
				Util::SetResourceName(texture.resource.Get(), "%s", name);
			if (texture.srv)
				Util::SetResourceName(texture.srv.Get(), "%s SRV", name);
			if (texture.uav)
				Util::SetResourceName(texture.uav.Get(), "%s UAV", name);
			return created;
		}
		bool CreateReadback(ID3D11Device* device, Readback& readback)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = static_cast<UINT>(kMeasurementValues * sizeof(float));
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = 4 * sizeof(float);
			D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_UNKNOWN;
			view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			view.Buffer.NumElements = static_cast<UINT>(kMeasurementValues / 4);
			if (FAILED(device->CreateBuffer(&desc, nullptr, &readback.gpu)) ||
				FAILED(device->CreateUnorderedAccessView(readback.gpu.Get(), &view, &readback.uav)))
				return false;
			Util::SetResourceName(readback.gpu.Get(), "NeuralRendering::ColourMeasurement");
			Util::SetResourceName(readback.uav.Get(), "NeuralRendering::ColourMeasurement UAV");
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.MiscFlags = 0;
			desc.StructureByteStride = 0;
			D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 };
			const bool created = SUCCEEDED(device->CreateBuffer(&desc, nullptr, &readback.staging)) &&
			                     SUCCEEDED(device->CreateQuery(&query, &readback.ready));
			if (readback.staging)
				Util::SetResourceName(readback.staging.Get(), "NeuralRendering::ColourMeasurementReadback");
			if (readback.ready)
				Util::SetResourceName(readback.ready.Get(), "NeuralRendering::ColourMeasurementReady");
			return created;
		}
		std::uint64_t Elapsed(std::chrono::steady_clock::time_point start)
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - start)
					.count());
		}
	}

	Registry& Registry::Instance()
	{
		static Registry instance;
		return instance;
	}
	Configuration Registry::Snapshot() const
	{
		std::scoped_lock lock(mutex_);
		return configuration_;
	}
	Status Registry::GetStatus() const
	{
		std::scoped_lock lock(mutex_);
		auto status = status_;
		status.measurementBatches = measurementBatches_.Latest();
		status.evictedIncompleteBatches = measurementBatches_.EvictedIncomplete();
		return status;
	}
	bool Registry::Configure(const Settings& settings, const Experiments& experiments, std::uint64_t expectedRevision)
	{
		if (!Valid(settings) || !Valid(experiments))
			return false;
		std::scoped_lock lock(mutex_);
		if (expectedRevision != 0 && expectedRevision != configuration_.revision)
			return false;
		if (settings == configuration_.settings && experiments == configuration_.experiments)
			return true;
		if (configuration_.revision == std::numeric_limits<std::uint64_t>::max())
			return false;
		const bool beginCapture = experiments.captureFrameEvidence && !configuration_.experiments.captureFrameEvidence;
		const auto captureEpoch = captureEpoch_.load(std::memory_order_relaxed);
		if (beginCapture && captureEpoch == std::numeric_limits<std::uint64_t>::max())
			return false;
		auto next = configuration_;
		next.settings = settings;
		next.experiments = experiments;
		++next.revision;
		for (std::uint32_t i = 0; i < 2; ++i) {
			if (!ChangesInput(configuration_, next, i))
				continue;
			if (next.inputEpoch[i] == std::numeric_limits<std::uint64_t>::max())
				return false;
			++next.inputEpoch[i];
		}
		auto& capture = ExposureCapture::Instance();
		configuration_ = next;
		if (beginCapture)
			captureEpoch_.store(captureEpoch + 1, std::memory_order_release);
		captureEvidenceEnabled_.store(next.experiments.captureFrameEvidence, std::memory_order_release);
		capture.Request(NeedsExposureCapture(next));  // Atomic request only, no engine/GPU work.
		return true;
	}
	MeasurementBatchHistory<Measurement>::Lease Registry::PinMeasurementBatch(const MeasurementBatchKey& key)
	{
		std::scoped_lock lock(mutex_);
		return measurementBatches_.Pin(key);
	}
	void Registry::Record(const Observation& observation) noexcept
	{
		try {
			std::scoped_lock lock(mutex_);
			if (observation.slot >= status_.slots.size())
				return;
			status_.slots[observation.slot] = observation;
			if (!observation.failure.empty())
				++status_.failed;
			else if (observation.processed) {
				++status_.reconstructed;
				if (observation.bypass)
					++status_.bypassed;
			} else
				++status_.prepared;
		} catch (...) { /* Optional diagnostics never alter rendering. */
		}
	}
	void Registry::Record(const Measurement& measurement) noexcept
	{
		try {
			std::scoped_lock lock(mutex_);
			if (measurement.source.slot >= status_.measurements.size())
				return;
			const auto& o = measurement.source;
			const MeasurementBatchKey key{ o.measurementBatchId, o.revision, o.generation,
				o.frame, o.sourceWorldFrame, o.insertion, o.expectedMeasurementSlotMask, o.atomicStereo };
			if (!o.processed || !o.failure.empty() || !measurementBatches_.Record(key, o.slot, measurement)) {
				++status_.dropped;
				return;
			}
			auto& previous = status_.measurements[o.slot];
			if (previous.source.measurementOrder <= o.measurementOrder)
				previous = measurement;
			++status_.samples;
		} catch (...) { /* Optional diagnostics never alter rendering. */
		}
	}
	void Registry::DropMeasurement() noexcept
	{
		try {
			std::scoped_lock lock(mutex_);
			++status_.dropped;
		} catch (...) {}
	}
	void Texture::Abandon() noexcept
	{
		(void)resource.Detach();
		(void)srv.Detach();
		(void)uav.Detach();
	}
	void Readback::Abandon() noexcept
	{
		(void)gpu.Detach();
		(void)staging.Detach();
		(void)uav.Detach();
		(void)ready.Detach();
		pending = false;
	}
	void Work::Abandon() noexcept
	{
		baseline.Abandon();
		result.Abandon();
		exposure.Abandon();
		prepared = false;
		for (auto& readback : readbacks) readback.Abandon();
	}
	bool Pipeline::EnsureShaders(ID3D11Device* device, bool diagnostics)
	{
		if (compileFailed_)
			return false;
		if (!prepare_)
			prepare_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/ColorPrepareCS.hlsl", {}, "cs_5_0", "main")));
		if (!reconstruct_)
			reconstruct_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/ColorReconstructCS.hlsl", {}, "cs_5_0", "main")));
		compileFailed_ = !prepare_ || !reconstruct_;
		if (compileFailed_)
			return false;
		if (!constants_) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(Constants);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &constants_)))
				return false;
			Util::SetResourceName(constants_.Get(), "NeuralRendering::ColourConstants");
		}
		if (diagnostics && !measure_ && !measureCompileAttempted_) {
			measureCompileAttempted_ = true;
			try {
				measure_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
					L"Data/Shaders/Upscaling/NeuralRendering/ColorMeasureCS.hlsl", {}, "cs_5_0", "main")));
			} catch (...) {
				measure_.Reset();
			}
		}
		return true;
	}
	bool Pipeline::Ensure(ID3D11Device* device, Work& work, const ComputeSubrect& roi, DXGI_FORMAT format, bool diagnostics)
	{
		if (!device || !roi.IsValid() || roi.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
			roi.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || !EnsureShaders(device, diagnostics))
			return false;
		switch (format) {
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			break;
		default:
			return false;
		}
		UINT support = 0;
		if (FAILED(device->CheckFormatSupport(format, &support)) || (support & D3D11_FORMAT_SUPPORT_SHADER_LOAD) == 0 ||
			(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) == 0)
			return false;
		if (!work.baseline.resource || work.capacityWidth < roi.width || work.capacityHeight < roi.height || work.format != format) {
			Texture baseline, result;
			const auto width = (roi.width + 63u) & ~63u, height = (roi.height + 63u) & ~63u;
			if (!CreateTexture(device, baseline, width, height, format, false) || !CreateTexture(device, result, width, height, format, true))
				return false;
			work.baseline = std::move(baseline);
			work.result = std::move(result);
			work.capacityWidth = width;
			work.capacityHeight = height;
			work.format = format;
			work.prepared = false;
		}
		if (diagnostics && measure_ && !work.readbackAttempted) {
			work.readbackAttempted = true;
			for (auto& readback : work.readbacks) {
				if (readback.ready)
					continue;
				Readback replacement;
				if (CreateReadback(device, replacement))
					readback = std::move(replacement);
			}
		}
		return true;
	}
	void Pipeline::Poll(ID3D11DeviceContext* context, Work& work)
	{
		for (auto& readback : work.readbacks) {
			if (!readback.pending)
				continue;
			const auto ready = context->GetData(readback.ready.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (ready == S_FALSE)
				continue;
			if (FAILED(ready)) {
				readback.pending = false;
				Registry::Instance().DropMeasurement();
				continue;
			}
			Measurement measurement{};
			measurement.source = readback.source;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto result = context->Map(readback.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
			if (result == DXGI_ERROR_WAS_STILL_DRAWING)
				continue;
			readback.pending = false;
			if (FAILED(result)) {
				Registry::Instance().DropMeasurement();
				continue;
			}
			std::memcpy(measurement.data.data(), mapped.pData, sizeof(measurement.data));
			context->Unmap(readback.staging.Get(), 0);
			Registry::Instance().Record(measurement);
		}
	}
	std::uint64_t Pipeline::BeginMeasurementBatch() noexcept
	{
		return measurementBatchOrder_ == std::numeric_limits<std::uint64_t>::max() ? 0 : ++measurementBatchOrder_;
	}
	bool Pipeline::Prepare(ID3D11DeviceContext* context, Work& work, ID3D11Resource* original,
		ID3D11Resource* prepared, ID3D11UnorderedAccessView* preparedUAV, const Configuration& config, Observation observation)
	{
		work.prepared = false;
		if (!context || !original || !prepared || !preparedUAV || !work.baseline.resource || !constants_ ||
			measurementOrder_ == std::numeric_limits<std::uint64_t>::max())
			return false;
		const auto start = std::chrono::steady_clock::now();
		work.observation = std::move(observation);
		work.configuration = config;
		auto& o = work.observation;
		o.measurementOrder = ++measurementOrder_;
		o.profile = EffectiveProfile(config, o.insertion);
		o.mode = config.EffectiveMode();
		o.revision = config.revision;
		o.bypass = config.experiments.transportBypass;
		o.modelEditShown = config.experiments.applyModelEdit;
		o.lightingPreservation = ResolveReconstructionSettings(work.configuration.settings).lightingPreservation;
		o.processed = false;
		std::uint64_t pixelBytes = 4;
		if (work.format == DXGI_FORMAT_R16G16B16A16_FLOAT || work.format == DXGI_FORMAT_R16G16B16A16_UNORM)
			pixelBytes = 8;
		if (work.format == DXGI_FORMAT_R32G32B32A32_FLOAT)
			pixelBytes = 16;
		o.retainedBytes = 2 * pixelBytes * work.capacityWidth * work.capacityHeight;
		CS_GPU_DETAIL_PASS("Upscaling::NRColorPreparation", o.preparationPass);
		ComputeStateGuard<5> guard(context);
		if (NeedsExposureCapture(config)) {
			const ExposureTransaction key{ o.frame, o.sourceWorldFrame, o.insertion, (o.slot % 4u) / 2u, o.generation,
				o.profile.exposureSource == ExposureSource::Manual ? ExposureSource::CapturedHDR : o.profile.exposureSource };
			if (!ExposureCapture::Instance().Bind(context, work.exposure, key))
				return false;
			o.exposureState = work.exposure.state;
			o.exposure = work.exposure.evidence;
		} else {
			o.exposureState = ExposureBindingState::NotRequested;
			o.exposure = {};
		}
		if (work.exposure.resource)
			o.retainedBytes += kExposureSnapshotPixels * 4 * sizeof(float);
		auto* exposure = NeedsExposureCapture(config) ? work.exposure.srv.Get() : nullptr;
		context->CSSetShaderResources(3, 1, &exposure);
		const auto& roi = o.rect;
		D3D11_BOX box{ roi.baseX, roi.baseY, 0, roi.baseX + roi.width, roi.baseY + roi.height, 1 };
		context->CopySubresourceRegion(work.baseline.resource.Get(), 0, 0, 0, 0, original, 0, &box);
		o.copiedLogicalBytes = roi.Area() * pixelBytes;
		if (o.profile.transform == Transform::Identity) {
			context->CopySubresourceRegion(prepared, 0, roi.baseX, roi.baseY, 0, original, 0, &box);
			o.copiedLogicalBytes += roi.Area() * pixelBytes;
		} else {
			const auto constants = MakeConstants(work);
			context->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
			auto* cb = constants_.Get();
			auto* source = work.baseline.srv.Get();
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShaderResources(0, 1, &source);
			context->CSSetUnorderedAccessViews(0, 1, &preparedUAV, nullptr);
			context->CSSetShader(prepare_.Get(), nullptr, 0);
			CS_GPU_PASS("Upscaling::NRColorPrepare");
			context->Dispatch((roi.width + 7u) / 8u, (roi.height + 7u) / 8u, 1);
		}
		work.prepared = true;
		o.preparationCpuMicroseconds = Elapsed(start);
		Registry::Instance().Record(o);
		return true;
	}
	bool Pipeline::Reconstruct(ID3D11DeviceContext* context, Work& work, ID3D11Resource* neural,
		ID3D11ShaderResourceView* neuralSRV, ID3D11ShaderResourceView* preparedSRV, const Configuration& requested)
	{
		if (!context || !neural || !neuralSRV || !preparedSRV || !work.result.uav || !reconstruct_ || !constants_ ||
			!work.prepared || requested.revision != work.configuration.revision)
			return false;
		const auto& config = work.configuration;  // The same immutable settings as Prepare.
		CS_GPU_DETAIL_PASS("Upscaling::NRColorReconstruction", work.observation.reconstructionPass);
		const auto start = std::chrono::steady_clock::now();
		ComputeStateGuard<5> guard(context);
		const auto& roi = work.observation.rect;
		const auto constants = MakeConstants(work);
		context->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
		auto* cb = constants_.Get();
		context->CSSetConstantBuffers(0, 1, &cb);
		auto* exposure = NeedsExposureCapture(config) ? work.exposure.srv.Get() : nullptr;
		if (config.EffectiveMode() == Mode::LegacyRaw && !config.experiments.transportBypass && config.experiments.applyModelEdit) {
			D3D11_BOX box{ roi.baseX, roi.baseY, 0, roi.baseX + roi.width, roi.baseY + roi.height, 1 };
			context->CopySubresourceRegion(work.result.resource.Get(), 0, 0, 0, 0, neural, 0, &box);
			const std::uint64_t pixelBytes = work.format == DXGI_FORMAT_R32G32B32A32_FLOAT                                                  ? 16u :
			                                 work.format == DXGI_FORMAT_R16G16B16A16_FLOAT || work.format == DXGI_FORMAT_R16G16B16A16_UNORM ? 8u :
			                                                                                                                                  4u;
			work.observation.copiedLogicalBytes += roi.Area() * pixelBytes;
		} else {
			ID3D11ShaderResourceView* sources[]{ work.baseline.srv.Get(), neuralSRV, preparedSRV, exposure };
			auto* destination = work.result.uav.Get();
			context->CSSetShaderResources(0, 4, sources);
			context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
			context->CSSetShader(reconstruct_.Get(), nullptr, 0);
			CS_GPU_PASS("Upscaling::NRColorReconstruct");
			context->Dispatch((roi.width + 7u) / 8u, (roi.height + 7u) / 8u, 1);
		}
		guard.Unbind();
		work.observation.processed = true;
		work.observation.reconstructionCpuMicroseconds = Elapsed(start);
		Registry::Instance().Record(work.observation);
		if (config.experiments.diagnostics)
			Measure(context, work, neuralSRV, preparedSRV);
		return true;
	}
	void Pipeline::Measure(ID3D11DeviceContext* context, Work& work,
		ID3D11ShaderResourceView* neural, ID3D11ShaderResourceView* prepared)
	{
		auto available = std::find_if(work.readbacks.begin(), work.readbacks.end(), [](const Readback& item) { return item.ready && !item.pending; });
		if (!measure_ || available == work.readbacks.end()) {
			Registry::Instance().DropMeasurement();
			return;
		}
		auto& readback = *available;
		auto* exposure = work.observation.exposureState != ExposureBindingState::NotRequested ? work.exposure.srv.Get() : nullptr;
		ID3D11ShaderResourceView* sources[]{ work.baseline.srv.Get(), neural, work.result.srv.Get(), exposure, prepared };
		auto* output = readback.uav.Get();
		context->CSSetShaderResources(0, 5, sources);
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		context->CSSetShader(measure_.Get(), nullptr, 0);
		context->Dispatch(1, 1, 1);
		ID3D11UnorderedAccessView* empty = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &empty, nullptr);
		context->CopyResource(readback.staging.Get(), readback.gpu.Get());
		context->End(readback.ready.Get());
		readback.source = work.observation;
		readback.pending = true;
	}
	void Pipeline::Commit(ID3D11DeviceContext* context, const Work& work, ID3D11Resource* destination)
	{
		ComputeStateGuard<5> guard(context);
		const auto& roi = work.observation.rect;
		D3D11_BOX box{ 0, 0, 0, roi.width, roi.height, 1 };
		context->CopySubresourceRegion(destination, 0, roi.baseX, roi.baseY, 0, work.result.resource.Get(), 0, &box);
	}
	void Pipeline::Reset() noexcept
	{
		prepare_.Reset();
		reconstruct_.Reset();
		measure_.Reset();
		constants_.Reset();
		compileFailed_ = false;
		measureCompileAttempted_ = false;
		ExposureCapture::Instance().Reset();
	}
	void Pipeline::Abandon() noexcept
	{
		(void)prepare_.Detach();
		(void)reconstruct_.Detach();
		(void)measure_.Detach();
		(void)constants_.Detach();
		ExposureCapture::Instance().Abandon();
	}
}
