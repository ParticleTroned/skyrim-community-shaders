#include "ExposureCapture.h"
#include "ColorPipeline.h"
#include "ComputeStateGuard.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "RE/B/BSImagespaceShader.h"
#include "RE/I/ImageSpaceManager.h"
#include "State.h"
#include "Utils/D3D.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <utility>

namespace NeuralRendering::Color
{
	using Microsoft::WRL::ComPtr;
	namespace
	{
		std::array<RE::BSShader*, 2> owners{};
		thread_local RE::BSShader* activeHDRProducer = nullptr;
		using SnapshotPixels = std::array<std::array<float, 4>, kExposureSnapshotPixels>;

		SnapshotPixels InvalidSnapshot()
		{
			SnapshotPixels pixels{};
			pixels[0] = { 0, 0, 1, 0 };
			return pixels;
		}

		bool ResourceOnDevice(ID3D11Resource* resource, ID3D11Device* device)
		{
			if (!resource || !device)
				return false;
			ComPtr<ID3D11Device> actual;
			resource->GetDevice(&actual);
			return actual.Get() == device;
		}
		bool CreateValueTexture(ID3D11Device* device, ExposureBinding& value, bool uav, ComPtr<ID3D11UnorderedAccessView>& view)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = kExposureSnapshotPixels;
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0u);
			const auto invalid = InvalidSnapshot();
			D3D11_SUBRESOURCE_DATA initial{ invalid.data(), static_cast<UINT>(sizeof(invalid)), 0 };
			ComPtr<ID3D11Texture2D> texture;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11UnorderedAccessView> output;
			if (FAILED(device->CreateTexture2D(&desc, &initial, &texture)) ||
				FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &srv)) ||
				(uav && FAILED(device->CreateUnorderedAccessView(texture.Get(), nullptr, &output))))
				return false;
			Util::SetResourceName(texture.Get(), "NeuralColor::ExposureSnapshot");
			Util::SetResourceName(srv.Get(), "NeuralColor::ExposureSnapshot SRV");
			if (output)
				Util::SetResourceName(output.Get(), "NeuralColor::ExposureSnapshot UAV");
			value.resource = std::move(texture);
			value.srv = std::move(srv);
			view = std::move(output);
			return true;
		}

	}

	struct ExposureCapture::State
	{
		struct Entry
		{
			ExposureBinding value;
			ComPtr<ID3D11UnorderedAccessView> uav;
			ComPtr<ID3D11Texture2D> staging;
			ComPtr<ID3D11Buffer> gammaStaging;
			ComPtr<ID3D11Query> ready;
			ExposureEvidence pendingEvidence;
			UINT gammaOffset = 0, gammaBytes = 0;
			bool pending = false, pendingGamma = false;
		};
		struct Latch
		{
			ExposureBinding value;
			ExposureLatchPolicy policy{};
		};
		std::atomic_bool requested{ false };
		std::atomic_uint64_t epoch{ 1 };
		std::atomic_uint64_t producerScopes{ 0 };
		std::atomic_uint32_t lastProducerFrame{ 0 };
		mutable std::mutex mutex;
		ExposureCaptureStatus status;
		std::array<Entry, 8> entries{};
		std::array<Latch, 4> latches{};
		ExposureEvidenceHistory retained;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11ComputeShader> shader;
		bool compileAttempted = false;
		std::uint64_t sequence = 0;
		ExposureShaderSelection shaderSelection{};
		ComPtr<ID3D11PixelShader> selectedPixelShader;

		void Retain(const ExposureEvidence& evidence, const char* reason)
		{
			if (Registry::Instance().CaptureEvidenceEnabled())
				retained.Record(evidence, reason);
		}

		void Complete(const ExposureEvidence& evidence, const char* reason)
		{
			(void)retained.Complete(evidence, reason);
		}

		void RetirePending(const char* reason)
		{
			for (const auto& e : entries)
				if (e.pending)
					Complete(e.pendingEvidence, reason);
		}

		void Poll()
		{
			if (!context)
				return;
			for (auto& e : entries) {
				if (!e.pending)
					continue;
				const auto readyResult = context->GetData(e.ready.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
				if (readyResult == S_FALSE)
					continue;
				if (FAILED(readyResult)) {
					e.pending = false;
					Complete(e.pendingEvidence, "readback_query_failed");
					++status.dropped;
					continue;
				}
				D3D11_MAPPED_SUBRESOURCE mapped{};
				const auto hr = context->Map(e.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
				if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
					continue;
				e.pending = false;
				if (FAILED(hr)) {
					Complete(e.pendingEvidence, "readback_map_failed");
					++status.dropped;
					continue;
				}
				auto sample = e.pendingEvidence;
				std::memcpy(sample.values.data(), mapped.pData, sizeof(sample.values));
				std::memcpy(sample.texels.data(), static_cast<const char*>(mapped.pData) + sizeof(sample.values), sizeof(sample.texels));
				context->Unmap(e.staging.Get(), 0);
				sample.readbackComplete = true;
				if (e.pendingGamma && e.gammaStaging && SUCCEEDED(context->Map(e.gammaStaging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
					std::memcpy(&sample.frameGammaExponent, static_cast<const char*>(mapped.pData) + e.gammaOffset, sizeof(float));
					context->Unmap(e.gammaStaging.Get(), 0);
					sample.gammaKnown = Finite(sample.frameGammaExponent) && sample.frameGammaExponent > 0;
				}
				Complete(sample, sample.gammaKnown ? "readback_complete" : "readback_complete_gamma_unavailable");
				status.samples[sample.stamp.sequence % status.samples.size()] = std::move(sample);
			}
		}

		void Capture(ID3D11DeviceContext* c, RE::BSShader* producer, std::optional<ExposureDrawKind> kind)
		{
			if (!requested.load(std::memory_order_acquire))
				return;
			auto* state = globals::state;
			if (!c || c != globals::d3d::context || !state || c->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return;
			std::scoped_lock lock(mutex);
			if (std::find(owners.begin(), owners.end(), producer) == owners.end())
				return;
			if (kind) {
				++status.drawCounts[static_cast<std::size_t>(*kind)];
			} else {
				++status.graphicsStateFlushes;
				status.lastGraphicsStateFlushFrame = state->frameCount;
			}
			const auto reject = [&](const char* why) { ++status.rejected; status.lastReason = why; };
			ComPtr<ID3D11Device> d;
			c->GetDevice(&d);
			if (device && (device.Get() != d.Get() || context.Get() != c)) {
				reject("capture context changed; use NR reset before rebinding");
				return;
			}
			device = d;
			context = c;
			Poll();
			auto [average, ps] = ReadExposureDrawBindings(c);
			status.lastBinding = {};
			status.lastBinding.frame = state->frameCount;
			const auto* expected = globals::game::currentPixelShader ? *globals::game::currentPixelShader : nullptr;
			status.lastBinding.shaderIdentity = reinterpret_cast<std::uintptr_t>(ps.Get());
			// A scoped effect cannot inherit another effect's cached engine selection.
			const bool ownedSelection = expected && std::find(producer->pixelShaders.begin(), producer->pixelShaders.end(), expected) != producer->pixelShaders.end();
			status.lastBinding.expectedShaderIdentity = ownedSelection ? reinterpret_cast<std::uintptr_t>(expected->shader) : 0;
			if (const auto selected = shaderSelection.Match(reinterpret_cast<std::uintptr_t>(c),
					reinterpret_cast<std::uintptr_t>(producer), reinterpret_cast<std::uintptr_t>(expected),
					state->frameCount, epoch.load(std::memory_order_acquire)))
				status.lastBinding.expectedShaderIdentity = selected;
			status.lastBinding.viewIdentity = reinterpret_cast<std::uintptr_t>(average.Get());
			if (const auto* why = ExposureDrawRejection(status.lastBinding.expectedShaderIdentity,
					status.lastBinding.shaderIdentity, status.lastBinding.viewIdentity)) {
				reject(why);
				return;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			average->GetDesc(&view);
			status.lastBinding.shaderIdentity = reinterpret_cast<std::uintptr_t>(ps.Get());
			status.lastBinding.viewFormat = static_cast<std::uint32_t>(view.Format);
			status.lastBinding.viewDimension = static_cast<std::uint32_t>(view.ViewDimension);
			ComPtr<ID3D11Resource> source;
			average->GetResource(&source);
			ComPtr<ID3D11Texture2D> texture;
			if (view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || FAILED(source.As(&texture))) {
				reject("AvgTex is not a Texture2D view");
				return;
			}
			D3D11_TEXTURE2D_DESC td{};
			texture->GetDesc(&td);
			const auto mip = view.Texture2D.MostDetailedMip;
			status.lastBinding.sourceIdentity = reinterpret_cast<std::uintptr_t>(texture.Get());
			status.lastBinding.width = td.Width;
			status.lastBinding.height = td.Height;
			status.lastBinding.mip = mip;
			status.lastBinding.mipLevels = td.MipLevels;
			status.lastBinding.arraySize = td.ArraySize;
			status.lastBinding.samples = td.SampleDesc.Count;
			status.lastBinding.sourceFormat = static_cast<std::uint32_t>(td.Format);
			if (mip >= 32u || mip >= td.MipLevels || td.SampleDesc.Count != 1 || td.ArraySize != 1) {
				reject("AvgTex mip, array or sample layout is unsupported");
				return;
			}
			status.lastBinding.viewWidth = std::max(1u, td.Width >> mip);
			status.lastBinding.viewHeight = std::max(1u, td.Height >> mip);
			status.lastBinding.visibleMips = std::min(view.Texture2D.MipLevels, td.MipLevels - mip);
			if (!SupportedExposureView(status.lastBinding.viewWidth, status.lastBinding.viewHeight,
					status.lastBinding.visibleMips)) {
				reject("AvgTex requires one visible mip of a 1x1 or 2x2 adaptation view");
				return;
			}
			ComPtr<ID3D11SamplerState> sampler;
			c->PSGetSamplers(2, 1, &sampler);
			D3D11_SAMPLER_DESC samplerDesc{};
			if (sampler)
				sampler->GetDesc(&samplerDesc);
			status.lastBinding.samplerIdentity = reinterpret_cast<std::uintptr_t>(sampler.Get());
			status.lastBinding.samplerFilter = static_cast<std::uint32_t>(samplerDesc.Filter);
			status.lastBinding.samplerAddressU = static_cast<std::uint32_t>(samplerDesc.AddressU);
			status.lastBinding.samplerAddressV = static_cast<std::uint32_t>(samplerDesc.AddressV);
			if (!sampler || !SupportedExposureSampler(samplerDesc)) {
				reject("AvgSampler requires a non-comparison filter and non-border addressing");
				return;
			}
			switch (view.Format) {
			case DXGI_FORMAT_R11G11B10_FLOAT:
			case DXGI_FORMAT_R16G16_FLOAT:
			case DXGI_FORMAT_R32G32_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
				break;
			default:
				reject("AvgTex must expose at least two floating-point channels");
				return;
			}
			const auto frame = static_cast<std::uint32_t>(state->frameCount);
			const auto currentEpoch = epoch.load(std::memory_order_acquire);
			for (auto& old : entries) {
				const auto& stamp = old.value.evidence.stamp;
				if (!stamp.sequence || stamp.frame != frame || stamp.epoch != currentEpoch)
					continue;
				if (old.value.evidence.sourceIdentity != reinterpret_cast<std::uintptr_t>(texture.Get()) ||
					old.value.evidence.sourceViewIdentity != reinterpret_cast<std::uintptr_t>(average.Get()) ||
					old.value.evidence.samplerIdentity != reinterpret_cast<std::uintptr_t>(sampler.Get()) ||
					old.value.evidence.shaderIdentity != reinterpret_cast<std::uintptr_t>(ps.Get()) ||
					old.value.evidence.sourceViewFormat != static_cast<std::uint32_t>(view.Format)) {
					old.value.evidence.stamp.ambiguous = true;
					if (ExposureEvidenceHistory::SameKey(old.pendingEvidence.stamp, stamp))
						old.pendingEvidence.stamp.ambiguous = true;
					retained.MarkAmbiguous(stamp);
					for (auto& sample : status.samples)
						if (ExposureEvidenceHistory::SameKey(sample.stamp, stamp))
							sample.stamp.ambiguous = true;
					reject("different AvgTex view, sampler or shader in one frame; automatic binding rejected");
				}
				return;  // Keep the first immutable exposure for this source frame.
			}
			if (!shader && !compileAttempted) {
				compileAttempted = true;
				shader.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
					L"Data/Shaders/Upscaling/NeuralRendering/ColorExposureCS.hlsl", {}, "cs_5_0", "main")));
			}
			if (!shader) {
				reject("ColorExposureCS unavailable; check shader deployment");
				return;
			}
			auto& e = entries[(sequence + 1u) % entries.size()];
			if (!e.value.resource && !CreateValueTexture(d.Get(), e.value, true, e.uav)) {
				reject("exposure snapshot allocation failed");
				return;
			}
			ExposureEvidence evidence;
			evidence.stamp = { frame, currentEpoch, ++sequence, false };
			evidence.sourceFormat = static_cast<std::uint32_t>(td.Format);
			evidence.sourceViewFormat = static_cast<std::uint32_t>(view.Format);
			evidence.sourceIdentity = reinterpret_cast<std::uintptr_t>(texture.Get());
			evidence.sourceViewIdentity = reinterpret_cast<std::uintptr_t>(average.Get());
			evidence.samplerIdentity = reinterpret_cast<std::uintptr_t>(sampler.Get());
			evidence.sourceWidth = status.lastBinding.viewWidth;
			evidence.sourceHeight = status.lastBinding.viewHeight;
			evidence.sourceMip = mip;
			evidence.shaderIdentity = reinterpret_cast<std::uintptr_t>(ps.Get());
			evidence.producer = kind ? "ISHDR BLEND effect scope / AvgTex t2 / D3D11 draw entry" :
			                           "ISHDR BLEND effect scope / AvgTex t2 / finalized engine graphics bindings";
			ComPtr<ID3D11RenderTargetView> target;
			c->OMGetRenderTargets(1, &target, nullptr);
			if (target) {
				D3D11_RENDER_TARGET_VIEW_DESC out{};
				target->GetDesc(&out);
				evidence.outputViewFormat = static_cast<std::uint32_t>(out.Format);
			}
			ComputeStateGuard<1> guard(c);
			auto* s = average.Get();
			auto* u = e.uav.Get();
			c->CSSetShaderResources(0, 1, &s);
			c->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
			c->CSSetShader(shader.Get(), nullptr, 0);
			{
				CS_PROFILE_SCOPE("Upscaling::NRColorExposureCapture");
				c->Dispatch(1, 1, 1);
			}
			guard.Unbind();
			e.value.evidence = evidence;
			e.value.state = ExposureBindingState::SnapshotQueued;
			Retain(evidence, "readback_pending");
			++status.captures;
			status.lastReason.clear();
			if (e.pending) {
				Complete(evidence, "readback_slot_busy");
				++status.dropped;
				return;
			}
			if (!e.staging) {
				D3D11_TEXTURE2D_DESC stage{};
				e.value.resource->GetDesc(&stage);
				stage.Usage = D3D11_USAGE_STAGING;
				stage.BindFlags = 0;
				stage.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(d->CreateTexture2D(&stage, nullptr, &e.staging))) {
					Complete(evidence, "readback_allocation_failed");
					++status.dropped;
					return;
				}
				Util::SetResourceName(e.staging.Get(), "NeuralColor::ExposureReadback");
			}
			if (!e.ready) {
				D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 };
				if (FAILED(d->CreateQuery(&query, &e.ready))) {
					Complete(evidence, "readback_query_allocation_failed");
					++status.dropped;
					return;
				}
				Util::SetResourceName(e.ready.Get(), "NeuralColor::ExposureReady");
			}
			e.pendingGamma = false;
			ComPtr<ID3D11Buffer> frameCB;
			c->PSGetConstantBuffers(12, 1, &frameCB);
			if (frameCB) {
				D3D11_BUFFER_DESC cb{};
				frameCB->GetDesc(&cb);
				e.gammaOffset = (globals::game::isVR ? 84u : 42u) * 16u;
				if (cb.ByteWidth >= e.gammaOffset + sizeof(float) && cb.ByteWidth <= D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u) {
					if (!e.gammaStaging || e.gammaBytes != cb.ByteWidth) {
						e.gammaStaging.Reset();
						D3D11_BUFFER_DESC stage{};
						stage.ByteWidth = cb.ByteWidth;
						stage.Usage = D3D11_USAGE_STAGING;
						stage.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
						if (SUCCEEDED(d->CreateBuffer(&stage, nullptr, &e.gammaStaging))) {
							e.gammaBytes = cb.ByteWidth;
							Util::SetResourceName(e.gammaStaging.Get(), "NeuralColor::GammaReadback");
						}
					}
					if (e.gammaStaging) {
						c->CopyResource(e.gammaStaging.Get(), frameCB.Get());
						e.pendingGamma = true;
					}
				}
			}
			c->CopyResource(e.staging.Get(), e.value.resource.Get());
			c->End(e.ready.Get());
			e.pendingEvidence = std::move(evidence);
			e.pending = true;
		}
	};

	ExposureCapture::ExposureCapture() : state_(new State) {}
	void ExposureCapture::ObservePixelShaderSelection(ID3D11DeviceContext* context, RE::BSShader* producer,
		const void* engineSelection, ID3D11PixelShader* selected) noexcept
	{
		if (!state_->requested.load(std::memory_order_acquire) || !globals::state ||
			context != globals::d3d::context || !producer ||
			std::find(owners.begin(), owners.end(), producer) == owners.end())
			return;
		try {
			std::scoped_lock lock(state_->mutex);
			state_->selectedPixelShader = selected;
			state_->shaderSelection = { reinterpret_cast<std::uintptr_t>(context),
				reinterpret_cast<std::uintptr_t>(producer), reinterpret_cast<std::uintptr_t>(engineSelection),
				reinterpret_cast<std::uintptr_t>(selected), globals::state->frameCount,
				state_->epoch.load(std::memory_order_acquire) };
		} catch (...) {
			std::scoped_lock lock(state_->mutex);
			state_->shaderSelection = {};
			++state_->status.rejected;
			state_->status.lastReason = "HDR shader selection observation failed";
		}
	}

	RE::BSShader* ExposureCapture::EnterProducer(RE::BSShader* producer) noexcept
	{
		auto* previous = std::exchange(activeHDRProducer, producer);
		if (producer && globals::state && state_->requested.load(std::memory_order_acquire)) {
			state_->producerScopes.fetch_add(1, std::memory_order_relaxed);
			state_->lastProducerFrame.store(globals::state->frameCount, std::memory_order_relaxed);
		}
		return previous;
	}
	void ExposureCapture::LeaveProducer(RE::BSShader* previous) noexcept
	{
		activeHDRProducer = previous;
	}
	void ExposureCapture::ObserveDraw(ID3D11DeviceContext* context, ExposureDrawKind kind) noexcept
	{
		if (kind < ExposureDrawKind::Count)
			Observe(context, kind);
	}
	void ExposureCapture::ObserveGraphicsStateFlush(ID3D11DeviceContext* context, bool isCompute) noexcept
	{
		if (!isCompute)
			Observe(context, std::nullopt);
	}
	void ExposureCapture::Observe(ID3D11DeviceContext* context, std::optional<ExposureDrawKind> kind) noexcept
	{
		auto* shader = activeHDRProducer;
		if (!state_->requested.load(std::memory_order_acquire) || !shader ||
			std::find(owners.begin(), owners.end(), shader) == owners.end())
			return;
		try {
			state_->Capture(context, shader, kind);
		} catch (const std::exception& e) {
			std::scoped_lock lock(state_->mutex);
			++state_->status.rejected;
			state_->status.lastReason = e.what();
		} catch (...) {
			std::scoped_lock lock(state_->mutex);
			++state_->status.rejected;
			state_->status.lastReason = "HDR draw observation failed";
		}
	}

	ExposureCapture& ExposureCapture::Instance()
	{
		// Draw callbacks run until engine teardown. Explicit Reset/Abandon own
		// resources; the small controller itself has process lifetime.
		static auto* instance = new ExposureCapture;
		return *instance;
	}
	void ExposureCapture::Request(bool enabled) noexcept
	{
		const auto previousValue = state_->requested.exchange(enabled, std::memory_order_acq_rel);
		if (enabled && !previousValue)
			state_->epoch.fetch_add(1, std::memory_order_acq_rel);
	}
	void ExposureCapture::RefreshProducers() noexcept
	{
		if (!state_->requested.load(std::memory_order_acquire))
			return;
		try {
			auto* manager = RE::ImageSpaceManager::GetSingleton();
			if (!manager)
				return;
			std::scoped_lock lock(state_->mutex);
			const std::array effects{ RE::ImageSpaceManager::ISHDRTonemapBlendCinematic, RE::ImageSpaceManager::ISHDRTonemapBlendCinematicFade };
			owners = {};
			state_->status.producersRegistered = 0;
			for (std::size_t i = 0; i < effects.size(); ++i) {
				const auto index = RE::ImageSpaceManager::GetCurrentIndex(effects[i]);
				if (index >= manager->effects.size())
					continue;
				auto* effect = manager->effects[static_cast<std::uint16_t>(index)];
				if (!effect)
					continue;
				auto* shader = static_cast<RE::BSImagespaceShader*>(effect);
				if (shader->shaderType.get() != RE::BSShader::Type::ImageSpace)
					continue;
				owners[i] = shader;
				++state_->status.producersRegistered;
			}
		} catch (...) {
			std::scoped_lock lock(state_->mutex);
			state_->status.lastReason = "HDR producer registration failed";
		}
	}
	ExposureCaptureStatus ExposureCapture::GetStatus() const
	{
		std::scoped_lock lock(state_->mutex);
		auto status = state_->status;
		status.requested = state_->requested.load(std::memory_order_acquire);
		status.epoch = state_->epoch.load(std::memory_order_acquire);
		status.producerScopes = state_->producerScopes.load(std::memory_order_relaxed);
		status.lastProducerFrame = state_->lastProducerFrame.load(std::memory_order_relaxed);
		return status;
	}
	ExposureEvidenceLookup ExposureCapture::GetEvidence(const ExposureStamp& key) const
	{
		std::scoped_lock lock(state_->mutex);
		return state_->retained.Get(key);
	}
	ExposureEvidenceLookup ExposureCapture::GetSourceFrameEvidence(std::uint32_t frame, std::uint64_t epoch) const
	{
		std::scoped_lock lock(state_->mutex);
		if (!epoch)
			epoch = state_->epoch.load(std::memory_order_acquire);
		return state_->retained.GetSourceFrame(frame, epoch);
	}
	bool ExposureCapture::Bind(ID3D11DeviceContext* c, ExposureBinding& output, const ExposureTransaction& key)
	{
		output.evidence = {};
		output.state = ExposureBindingState::ResourceFailure;
		if (!c || c->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE || key.route >= 2 || key.insertion >= 2 ||
			key.sourceWorldFrame == std::numeric_limits<std::uint32_t>::max())
			return false;
		std::scoped_lock lock(state_->mutex);
		ComPtr<ID3D11Device> device;
		c->GetDevice(&device);
		if (!device)
			return false;
		const bool contextMatches = state_->context.Get() == c && state_->device.Get() == device.Get();
		// Never reuse a successful latch after switching contexts/devices. Returning
		// false lets the outer transaction fall back before either eye is committed.
		if ((state_->context && !contextMatches) ||
			(output.resource && !ResourceOnDevice(output.resource.Get(), device.Get()))) {
			output.state = ExposureBindingState::ContextMismatch;
			return false;
		}
		ComPtr<ID3D11UnorderedAccessView> unused;
		if (!output.resource && !CreateValueTexture(device.Get(), output, false, unused))
			return false;
		output.state = ExposureBindingState::WaitingForHDRPass;
		const auto invalidate = [&]() {
			const auto invalid = InvalidSnapshot();
			c->UpdateSubresource(output.resource.Get(), 0, nullptr, invalid.data(), static_cast<UINT>(sizeof(invalid)), 0);
		};
		if (contextMatches)
			state_->Poll();
		auto& latch = state_->latches[key.route * 2u + key.insertion];
		const auto epoch = state_->epoch.load(std::memory_order_acquire);
		// Once chosen, exposure availability is frozen for the whole stereo/ROI
		// transaction, even if an API request toggles capture midway through it.
		const auto decision = latch.policy.Begin(key, reinterpret_cast<std::uintptr_t>(c),
			reinterpret_cast<std::uintptr_t>(device.Get()));
		if (decision == ExposureLatchDecision::Reject) {
			output.state = ExposureBindingState::ContextMismatch;
			return false;
		}
		if (decision == ExposureLatchDecision::NewTransaction) {
			latch.value.evidence = {};
			latch.value.state = ExposureBindingState::StaleOrAmbiguous;
			const auto match = std::find_if(state_->entries.begin(), state_->entries.end(), [&](const State::Entry& e) {
				return MatchesExposure(e.value.evidence.stamp, key, epoch);
			});
			if (!contextMatches) {
				latch.value.state = state_->context ? ExposureBindingState::ContextMismatch : ExposureBindingState::WaitingForHDRPass;
			} else if (match != state_->entries.end()) {
				if (!latch.value.resource && !CreateValueTexture(device.Get(), latch.value, false, unused)) {
					latch.policy.Clear();
					return false;
				}
				if (!ResourceOnDevice(latch.value.resource.Get(), device.Get()) ||
					!ResourceOnDevice(match->value.resource.Get(), device.Get())) {
					latch.policy.Clear();
					output.state = ExposureBindingState::ContextMismatch;
					return false;
				}
				c->CopyResource(latch.value.resource.Get(), match->value.resource.Get());
				latch.value.evidence = match->value.evidence;
				latch.value.state = ExposureBindingState::SnapshotQueued;
			}
		}
		output.state = latch.value.state;
		output.evidence = latch.value.evidence;
		if (output.state == ExposureBindingState::SnapshotQueued) {
			if (!ResourceOnDevice(latch.value.resource.Get(), device.Get())) {
				output.evidence = {};
				output.state = ExposureBindingState::ContextMismatch;
				return false;
			}
			c->CopyResource(output.resource.Get(), latch.value.resource.Get());
		} else
			invalidate();
		return true;
	}
	void ExposureCapture::Reset() noexcept
	{
		std::scoped_lock lock(state_->mutex);
		state_->RetirePending("resources_retired_before_readback");
		state_->entries = {};
		state_->latches = {};
		state_->shaderSelection = {};
		state_->selectedPixelShader.Reset();
		state_->shader.Reset();
		state_->device.Reset();
		state_->context.Reset();
		state_->compileAttempted = false;
		state_->epoch.fetch_add(1, std::memory_order_acq_rel);
	}
	void ExposureCapture::Abandon() noexcept
	{
		std::scoped_lock lock(state_->mutex);
		state_->RetirePending("device_abandoned_before_readback");
		state_->shaderSelection = {};
		(void)state_->selectedPixelShader.Detach();
		for (auto& e : state_->entries) {
			e.value.Abandon();
			(void)e.uav.Detach();
			(void)e.staging.Detach();
			(void)e.gammaStaging.Detach();
			(void)e.ready.Detach();
			e.pending = false;
		}
		for (auto& l : state_->latches) {
			l.value.Abandon();
			l.policy.Clear();
		}
		(void)state_->shader.Detach();
		(void)state_->device.Detach();
		(void)state_->context.Detach();
		state_->epoch.fetch_add(1, std::memory_order_acq_rel);
	}
	const char* ExposureBindingName(ExposureBindingState value) noexcept
	{
		switch (value) {
		case ExposureBindingState::NotRequested:
			return "not_requested";
		case ExposureBindingState::WaitingForHDRPass:
			return "waiting_for_hdr_pass";
		case ExposureBindingState::StaleOrAmbiguous:
			return "no_matching_unambiguous_source_frame";
		case ExposureBindingState::ContextMismatch:
			return "context_mismatch";
		case ExposureBindingState::SnapshotQueued:
			return "gpu_snapshot_queued";
		default:
			return "resource_failure";
		}
	}
}
