#include "ColorPipeline.h"
#include "Utils/D3D.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstddef>
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
			std::uint32_t mode, domain, transform, bypass;
			float exposure, detail, appearance, maximumStops;
		};
		static_assert(sizeof(Constants) == 48);
		static_assert(offsetof(Constants, exposure) == 32);

		Constants MakeConstants(const Work& work, const Configuration& config)
		{
			const auto& roi = work.observation.rect;
			const auto profile = EffectiveProfile(config, work.observation.insertion);
			return { roi.baseX, roi.baseY, roi.width, roi.height,
				static_cast<std::uint32_t>(config.settings.mode),
				static_cast<std::uint32_t>(profile.domain),
				static_cast<std::uint32_t>(profile.transform),
				config.experiments.transportBypass ? 1u : 0u,
				profile.exposureMultiplier, config.settings.detailStrength,
				config.settings.appearanceMix, config.settings.maximumDetailStops };
		}

		class StateGuard
		{
		public:
			explicit StateGuard(ID3D11DeviceContext* context) : context_(context)
			{
				context_->CSGetShader(&shader_, classes_.data(), &classCount_);
				context_->CSGetShaderResources(0, 3, srvs_.data());
				context_->CSGetUnorderedAccessViews(0, 1, &uav_);
				context_->CSGetConstantBuffers(0, 1, &cb_);
				context_->GetPredication(&predicate_, &predicateValue_);
				context_->SetPredication(nullptr, FALSE);
				Unbind();
			}
			StateGuard(const StateGuard&) = delete;
			StateGuard& operator=(const StateGuard&) = delete;
			~StateGuard()
			{
				Unbind();
				context_->CSSetShader(shader_, classes_.data(), classCount_);
				context_->CSSetConstantBuffers(0, 1, &cb_);
				context_->CSSetShaderResources(0, 3, srvs_.data());
				context_->CSSetUnorderedAccessViews(0, 1, &uav_, nullptr);
				context_->SetPredication(predicate_, predicateValue_);
				if (shader_) shader_->Release();
				for (UINT i = 0; i < classCount_; ++i) if (classes_[i]) classes_[i]->Release();
				for (auto* view : srvs_) if (view) view->Release();
				if (uav_) uav_->Release();
				if (cb_) cb_->Release();
				if (predicate_) predicate_->Release();
			}
			void Unbind() const
			{
				ID3D11ShaderResourceView* emptySRVs[3]{};
				ID3D11UnorderedAccessView* emptyUAV = nullptr;
				context_->CSSetShaderResources(0, 3, emptySRVs);
				context_->CSSetUnorderedAccessViews(0, 1, &emptyUAV, nullptr);
			}
		private:
			ID3D11DeviceContext* context_;
			ID3D11ComputeShader* shader_ = nullptr;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classes_{};
			UINT classCount_ = D3D11_SHADER_MAX_INTERFACES;
			std::array<ID3D11ShaderResourceView*, 3> srvs_{};
			ID3D11UnorderedAccessView* uav_ = nullptr;
			ID3D11Buffer* cb_ = nullptr;
			ID3D11Predicate* predicate_ = nullptr;
			BOOL predicateValue_ = FALSE;
		};

		bool CreateTexture(ID3D11Device* device, Texture& texture,
			std::uint32_t width, std::uint32_t height, DXGI_FORMAT format, bool output)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width; desc.Height = height;
			desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = format;
			desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (output ? D3D11_BIND_UNORDERED_ACCESS : 0u);
			return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture.resource)) &&
			       SUCCEEDED(device->CreateShaderResourceView(texture.resource.Get(), nullptr, &texture.srv)) &&
			       (!output || SUCCEEDED(device->CreateUnorderedAccessView(texture.resource.Get(), nullptr, &texture.uav)));
		}

		bool CreateReadback(ID3D11Device* device, Readback& readback)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = 16 * sizeof(float);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = 4 * sizeof(float);
			D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_UNKNOWN;
			view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			view.Buffer.NumElements = 4;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &readback.gpu)) ||
				FAILED(device->CreateUnorderedAccessView(readback.gpu.Get(), &view, &readback.uav)))
				return false;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.MiscFlags = 0; desc.StructureByteStride = 0;
			D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 };
			return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &readback.staging)) &&
			       SUCCEEDED(device->CreateQuery(&query, &readback.ready));
		}

		std::uint64_t Elapsed(std::chrono::steady_clock::time_point start)
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - start).count());
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
		return status_;
	}
	bool Registry::Configure(const Settings& settings, const Experiments& experiments, std::uint64_t expectedRevision)
	{
		if (!Valid(settings) || !Valid(experiments))
			return false;
		std::scoped_lock lock(mutex_);
		if (expectedRevision != 0 && expectedRevision != configuration_.revision) return false;
		if (settings == configuration_.settings && experiments == configuration_.experiments)
			return true;
		auto next = configuration_;
		next.settings = settings;
		next.experiments = experiments;
		++next.revision;
		for (std::uint32_t i = 0; i < 2; ++i)
			if (ChangesInput(configuration_, next, i)) ++next.inputEpoch[i];
		configuration_ = next;
		return true;
	}
	void Registry::Record(const Observation& observation) noexcept
	{
		try {
			std::scoped_lock lock(mutex_);
			if (observation.slot >= status_.slots.size()) return;
			status_.slots[observation.slot] = observation;
			if (!observation.failure.empty()) ++status_.failed;
			else if (observation.processed) {
				++status_.reconstructed;
				if (observation.bypass) ++status_.bypassed;
			} else ++status_.prepared;
		} catch (...) {
			// Optional diagnostics must not change renderer success or fallback.
		}
	}
	void Registry::Record(const Measurement& measurement) noexcept
	{
		try {
			std::scoped_lock lock(mutex_);
			if (measurement.source.slot >= status_.measurements.size()) return;
			status_.measurements[measurement.source.slot] = measurement;
			++status_.samples;
		} catch (...) {
			// Optional diagnostics must not change renderer success or fallback.
		}
	}
	void Registry::DropMeasurement() noexcept
	{
		try {
			std::scoped_lock lock(mutex_);
			++status_.dropped;
		} catch (...) {
			// Optional diagnostics must not change renderer success or fallback.
		}
	}

	void Texture::Abandon() noexcept
	{
		(void)resource.Detach(); (void)srv.Detach(); (void)uav.Detach();
	}
	void Readback::Abandon() noexcept
	{
		(void)gpu.Detach(); (void)staging.Detach(); (void)uav.Detach(); (void)ready.Detach();
		pending = false;
	}
	void Work::Abandon() noexcept
	{
		baseline.Abandon(); result.Abandon();
		for (auto& readback : readbacks) readback.Abandon();
	}

	bool Pipeline::EnsureShaders(ID3D11Device* device, bool diagnostics)
	{
		if (compileFailed_) return false;
		if (!prepare_) {
			prepare_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/ColorPrepareCS.hlsl", {}, "cs_5_0", "main")));
		}
		if (!reconstruct_) {
			reconstruct_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/ColorReconstructCS.hlsl", {}, "cs_5_0", "main")));
		}
		compileFailed_ = !prepare_ || !reconstruct_;
		if (compileFailed_) return false;
		if (!constants_) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(Constants); desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &constants_))) return false;
		}
		if (diagnostics && !measure_ && !measureCompileAttempted_) {
			measureCompileAttempted_ = true;
			try {
				measure_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
					L"Data/Shaders/Upscaling/NeuralRendering/ColorMeasureCS.hlsl", {}, "cs_5_0", "main")));
			} catch (...) {
				measure_.Reset();
			}
			// Measurement failure must not disable the colour path.
		}
		return true;
	}

	bool Pipeline::Ensure(ID3D11Device* device, Work& work, const ComputeSubrect& roi,
		DXGI_FORMAT format, bool diagnostics)
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
		if (FAILED(device->CheckFormatSupport(format, &support)) ||
			(support & D3D11_FORMAT_SUPPORT_SHADER_LOAD) == 0 ||
			(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) == 0)
			return false;
		if (!work.baseline.resource || work.capacityWidth < roi.width || work.capacityHeight < roi.height || work.format != format) {
			Texture baseline, result;
			const auto width = (roi.width + 63u) & ~63u;
			const auto height = (roi.height + 63u) & ~63u;
			if (!CreateTexture(device, baseline, width, height, format, false) ||
				!CreateTexture(device, result, width, height, format, true)) return false;
			work.baseline = std::move(baseline); work.result = std::move(result);
			work.capacityWidth = width; work.capacityHeight = height; work.format = format;
		}
		if (diagnostics && measure_ && !work.readbackAttempted) {
			work.readbackAttempted = true;
			for (auto& readback : work.readbacks) {
				if (readback.ready) continue;
				Readback replacement;
				if (CreateReadback(device, replacement)) readback = std::move(replacement);
			}
		}
		return true;
	}

	void Pipeline::Poll(ID3D11DeviceContext* context, Work& work)
	{
		for (auto& readback : work.readbacks) {
			if (!readback.pending) continue;
			const auto ready = context->GetData(readback.ready.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (ready == S_FALSE) continue;
			if (FAILED(ready)) { readback.pending = false; Registry::Instance().DropMeasurement(); continue; }
			Measurement measurement{};
			measurement.source = readback.source;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto result = context->Map(readback.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
			if (result == DXGI_ERROR_WAS_STILL_DRAWING) continue;
			readback.pending = false;
			if (FAILED(result)) { Registry::Instance().DropMeasurement(); continue; }
			std::memcpy(measurement.data.data(), mapped.pData, sizeof(measurement.data));
			context->Unmap(readback.staging.Get(), 0);
			Registry::Instance().Record(measurement);
		}
	}

	bool Pipeline::Prepare(ID3D11DeviceContext* context, Work& work, ID3D11Resource* original,
		ID3D11Resource* prepared, ID3D11UnorderedAccessView* preparedUAV,
		const Configuration& config, Observation observation)
	{
		if (!context || !original || !prepared || !preparedUAV || !work.baseline.resource || !constants_)
			return false;
		Poll(context, work);
		const auto start = std::chrono::steady_clock::now();
		work.observation = std::move(observation);
		work.observation.profile = EffectiveProfile(config, work.observation.insertion);
		work.observation.mode = config.settings.mode;
		work.observation.revision = config.revision;
		work.observation.bypass = config.experiments.transportBypass;
		work.observation.processed = false;
		// All supported formats are 32/64/128 bits per pixel. Report exact native
		// storage for the common formats rather than pretending full-eye allocation.
		std::uint64_t pixelBytes = 4;
		if (work.format == DXGI_FORMAT_R16G16B16A16_FLOAT || work.format == DXGI_FORMAT_R16G16B16A16_UNORM) pixelBytes = 8;
		if (work.format == DXGI_FORMAT_R32G32B32A32_FLOAT) pixelBytes = 16;
		work.observation.retainedBytes = 2 * pixelBytes * work.capacityWidth * work.capacityHeight;
		StateGuard guard(context);
		const auto& roi = work.observation.rect;
		D3D11_BOX box{ roi.baseX, roi.baseY, 0, roi.baseX + roi.width, roi.baseY + roi.height, 1 };
		context->CopySubresourceRegion(work.baseline.resource.Get(), 0, 0, 0, 0, original, 0, &box);
		if (work.observation.profile.transform == Transform::Identity) {
			context->CopySubresourceRegion(prepared, 0, roi.baseX, roi.baseY, 0, original, 0, &box);
		} else {
			const auto constants = MakeConstants(work, config);
			context->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
			auto* cb = constants_.Get(); auto* source = work.baseline.srv.Get();
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShaderResources(0, 1, &source);
			context->CSSetUnorderedAccessViews(0, 1, &preparedUAV, nullptr);
			context->CSSetShader(prepare_.Get(), nullptr, 0);
			CS_PROFILE_SCOPE("Upscaling::NRColorPrepare");
			context->Dispatch((roi.width + 7u) / 8u, (roi.height + 7u) / 8u, 1);
		}
		work.observation.preparationCpuMicroseconds = Elapsed(start);
		Registry::Instance().Record(work.observation);
		return true;
	}

	bool Pipeline::Reconstruct(ID3D11DeviceContext* context, Work& work, ID3D11Resource* neural,
		ID3D11ShaderResourceView* neuralSRV, ID3D11ShaderResourceView* preparedSRV, const Configuration& config)
	{
		if (!context || !neural || !neuralSRV || !preparedSRV || !work.result.uav || !reconstruct_ || !constants_)
			return false;
		const auto start = std::chrono::steady_clock::now();
		StateGuard guard(context);
		const auto& roi = work.observation.rect;
		const auto constants = MakeConstants(work, config);
		context->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
		auto* cb = constants_.Get();
		context->CSSetConstantBuffers(0, 1, &cb);
		if (config.settings.mode == Mode::LegacyRaw && !config.experiments.transportBypass) {
			D3D11_BOX box{ roi.baseX, roi.baseY, 0, roi.baseX + roi.width, roi.baseY + roi.height, 1 };
			context->CopySubresourceRegion(work.result.resource.Get(), 0, 0, 0, 0, neural, 0, &box);
		} else {
			ID3D11ShaderResourceView* sources[]{ work.baseline.srv.Get(), neuralSRV, preparedSRV };
			auto* destination = work.result.uav.Get();
			context->CSSetShaderResources(0, 3, sources);
			context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
			context->CSSetShader(reconstruct_.Get(), nullptr, 0);
			CS_PROFILE_SCOPE("Upscaling::NRColorReconstruct");
			context->Dispatch((roi.width + 7u) / 8u, (roi.height + 7u) / 8u, 1);
		}
		guard.Unbind();
		work.observation.processed = true;
		work.observation.reconstructionCpuMicroseconds = Elapsed(start);
		Registry::Instance().Record(work.observation);
		if (config.experiments.diagnostics) Measure(context, work, neuralSRV);
		return true;
	}

	void Pipeline::Measure(ID3D11DeviceContext* context, Work& work, ID3D11ShaderResourceView* neural)
	{
		auto available = std::find_if(work.readbacks.begin(), work.readbacks.end(), [](const Readback& item) {
			return item.ready && !item.pending;
		});
		if (!measure_ || available == work.readbacks.end()) { Registry::Instance().DropMeasurement(); return; }
		auto& readback = *available;
		ID3D11ShaderResourceView* sources[]{ work.baseline.srv.Get(), neural, work.result.srv.Get() };
		auto* output = readback.uav.Get();
		context->CSSetShaderResources(0, 3, sources);
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
		StateGuard guard(context);
		const auto& roi = work.observation.rect;
		D3D11_BOX box{ 0, 0, 0, roi.width, roi.height, 1 };
		context->CopySubresourceRegion(destination, 0, roi.baseX, roi.baseY, 0, work.result.resource.Get(), 0, &box);
	}
	void Pipeline::Reset() noexcept
	{
		prepare_.Reset(); reconstruct_.Reset(); measure_.Reset(); constants_.Reset(); compileFailed_ = false; measureCompileAttempted_ = false;
	}
	void Pipeline::Abandon() noexcept
	{
		(void)prepare_.Detach(); (void)reconstruct_.Detach(); (void)measure_.Detach(); (void)constants_.Detach();
	}
}
