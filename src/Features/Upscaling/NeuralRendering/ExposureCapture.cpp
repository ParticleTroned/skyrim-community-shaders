#include "ExposureCapture.h"
#include "Globals.h"
#include "Features/Upscaling.h"
#include "State.h"
#include "Utils/D3D.h"
#include "RE/B/BSImagespaceShader.h"
#include "RE/I/ImageSpaceManager.h"
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
		// Install on the two concrete HDR effect instances' PRIMARY BSShader
		// vtables. Slot 3 is BSShader::RestoreTechnique(uint32_t), not the
		// secondary ImageSpaceEffect::Render vtable. Existing hooks are chained.
		using Restore = void (*)(RE::BSShader*, std::uint32_t);
		std::array<Restore, 2> previous{};
		std::array<std::uintptr_t, 2> tables{};
		std::array<RE::BSShader*, 2> owners{};
		void CaptureHDR(RE::BSShader*) noexcept;
		template <std::size_t I> void RestoreTechnique(RE::BSShader* shader, std::uint32_t technique)
		{
			CaptureHDR(shader);  // Before restoration can clear the live PS bindings.
			previous[I](shader, technique);
		}

		bool SameDevice(ID3D11DeviceContext* context, ID3D11Device* device)
		{
			ComPtr<ID3D11Device> actual;
			context->GetDevice(&actual);
			return actual.Get() == device;
		}
		bool CreateValueTexture(ID3D11Device* device, ExposureBinding& value, bool uav, ComPtr<ID3D11UnorderedAccessView>& view)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0u);
			const std::array<float, 4> invalid{ 0, 0, 1, 0 };
			D3D11_SUBRESOURCE_DATA initial{ invalid.data(), static_cast<UINT>(sizeof(invalid)), 0 };
			ComPtr<ID3D11Texture2D> texture;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11UnorderedAccessView> output;
			if (FAILED(device->CreateTexture2D(&desc, &initial, &texture)) ||
				FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &srv)) ||
				(uav && FAILED(device->CreateUnorderedAccessView(texture.Get(), nullptr, &output)))) return false;
			value.resource = std::move(texture); value.srv = std::move(srv); view = std::move(output);
			return true;
		}
		// Capture changes CS t0/u0/shader only. Preserve class instances and
		// predication too; PS bindings are observed, never modified.
		struct Guard
		{
			ID3D11DeviceContext* context;
			ComPtr<ID3D11ComputeShader> shader;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11UnorderedAccessView> uav;
			ComPtr<ID3D11Predicate> predicate;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classes{};
			UINT classCount = D3D11_SHADER_MAX_INTERFACES;
			BOOL predicateValue = FALSE;
			explicit Guard(ID3D11DeviceContext* c) : context(c)
			{
				c->CSGetShader(&shader, classes.data(), &classCount);
				c->CSGetShaderResources(0, 1, &srv);
				c->CSGetUnorderedAccessViews(0, 1, &uav);
				c->GetPredication(&predicate, &predicateValue);
				c->SetPredication(nullptr, FALSE);
				Clear();
			}
			void Clear()
			{
				ID3D11ShaderResourceView* s = nullptr;
				ID3D11UnorderedAccessView* u = nullptr;
				context->CSSetShaderResources(0, 1, &s);
				context->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
			}
			~Guard()
			{
				Clear();
				context->CSSetShader(shader.Get(), classes.data(), classCount);
				auto* s = srv.Get(); auto* u = uav.Get();
				context->CSSetShaderResources(0, 1, &s);
				context->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
				context->SetPredication(predicate.Get(), predicateValue);
				for (UINT i = 0; i < classCount; ++i) if (classes[i]) classes[i]->Release();
			}
		};
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
			ExposureTransaction key{};
			std::uint64_t captureEpoch = 0;
			bool occupied = false;
		};
		std::atomic_bool requested{ false };
		std::atomic_uint64_t epoch{ 1 };
		mutable std::mutex mutex;
		ExposureCaptureStatus status;
		std::array<Entry, 8> entries{};
		std::array<Latch, 4> latches{};
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11ComputeShader> shader;
		bool compileAttempted = false;
		std::uint64_t sequence = 0;

		void Poll()
		{
			if (!context) return;
			for (auto& e : entries) {
				if (!e.pending) continue;
				const auto readyResult = context->GetData(e.ready.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
				if (readyResult == S_FALSE) continue;
				if (FAILED(readyResult)) { e.pending = false; ++status.dropped; continue; }
				D3D11_MAPPED_SUBRESOURCE mapped{};
				const auto hr = context->Map(e.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
				if (hr == DXGI_ERROR_WAS_STILL_DRAWING) continue;
				e.pending = false;
				if (FAILED(hr)) { ++status.dropped; continue; }
				auto sample = e.pendingEvidence;
				std::memcpy(sample.values.data(), mapped.pData, sizeof(sample.values));
				context->Unmap(e.staging.Get(), 0);
				sample.readbackComplete = true;
				if (e.pendingGamma && e.gammaStaging && SUCCEEDED(context->Map(e.gammaStaging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
					std::memcpy(&sample.frameGammaExponent, static_cast<const char*>(mapped.pData) + e.gammaOffset, sizeof(float));
					context->Unmap(e.gammaStaging.Get(), 0);
					sample.gammaKnown = Finite(sample.frameGammaExponent) && sample.frameGammaExponent > 0;
				}
				status.samples[sample.stamp.sequence % status.samples.size()] = std::move(sample);
			}
		}

		void Capture(RE::BSShader* producer)
		{
			if (!requested.load(std::memory_order_acquire)) return;
			if (!globals::features::upscaling.settings.neuralRenderingEnabled) return;
			auto* c = globals::d3d::context;
			auto* state = globals::state;
			if (!c || !state || c->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
			std::scoped_lock lock(mutex);
			if (std::find(owners.begin(), owners.end(), producer) == owners.end()) return;
			const auto reject = [&](const char* why) { ++status.rejected; status.lastReason = why; };
			ComPtr<ID3D11Device> d; c->GetDevice(&d);
			if (device && (device.Get() != d.Get() || context.Get() != c)) {
				reject("capture context changed; use NR reset before rebinding"); return;
			}
			device = d; context = c;
			Poll();
			ComPtr<ID3D11ShaderResourceView> average;
			ComPtr<ID3D11PixelShader> ps;
			c->PSGetShaderResources(2, 1, &average);
			c->PSGetShader(&ps, nullptr, nullptr);
			if (!average || !ps) { reject("HDR restore boundary has no live PS/AvgTex t2; no exposure assumed"); return; }
			D3D11_SHADER_RESOURCE_VIEW_DESC view{}; average->GetDesc(&view);
			ComPtr<ID3D11Resource> source; average->GetResource(&source);
			ComPtr<ID3D11Texture2D> texture;
			if (view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || FAILED(source.As(&texture))) {
				reject("AvgTex is not a Texture2D view"); return;
			}
			D3D11_TEXTURE2D_DESC td{}; texture->GetDesc(&td);
			const auto mip = view.Texture2D.MostDetailedMip;
			if (mip >= td.MipLevels || td.SampleDesc.Count != 1 || td.ArraySize != 1 ||
				std::max(1u, td.Width >> mip) != 1u || std::max(1u, td.Height >> mip) != 1u) {
				reject("AvgTex is not a scalar 1x1 adaptation view; spatial exposure not guessed"); return;
			}
			switch (view.Format) {
			case DXGI_FORMAT_R11G11B10_FLOAT:
			case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32B32A32_FLOAT: break;
			default: reject("AvgTex must expose at least two floating-point channels"); return;
			}
			const auto frame = static_cast<std::uint32_t>(state->frameCount);
			const auto currentEpoch = epoch.load(std::memory_order_acquire);
			for (auto& old : entries) {
				const auto& stamp = old.value.evidence.stamp;
				if (!stamp.sequence || stamp.frame != frame || stamp.epoch != currentEpoch) continue;
				if (old.value.evidence.sourceIdentity != reinterpret_cast<std::uintptr_t>(texture.Get()) ||
					old.value.evidence.sourceViewFormat != static_cast<std::uint32_t>(view.Format)) {
					old.value.evidence.stamp.ambiguous = true;
					reject("different AvgTex resources in one frame; automatic binding rejected");
				}
				return; // Keep the first immutable exposure for this source frame.
			}
			if (!shader && !compileAttempted) {
				compileAttempted = true;
				shader.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
					L"Data/Shaders/Upscaling/NeuralRendering/ColorExposureCS.hlsl", {}, "cs_5_0", "main")));
			}
			if (!shader) { reject("ColorExposureCS unavailable; check shader deployment"); return; }
			auto& e = entries[(sequence + 1u) % entries.size()];
			if (!e.value.resource && !CreateValueTexture(d.Get(), e.value, true, e.uav)) {
				reject("exposure snapshot allocation failed"); return;
			}
			ExposureEvidence evidence;
			evidence.stamp = { frame, currentEpoch, ++sequence, false };
			evidence.sourceFormat = static_cast<std::uint32_t>(td.Format);
			evidence.sourceViewFormat = static_cast<std::uint32_t>(view.Format);
			evidence.sourceIdentity = reinterpret_cast<std::uintptr_t>(texture.Get());
			evidence.shaderIdentity = reinterpret_cast<std::uintptr_t>(ps.Get());
			evidence.producer = "ISHDR BLEND / AvgTex t2 / BSShader RestoreTechnique entry";
			ComPtr<ID3D11RenderTargetView> target;
			c->OMGetRenderTargets(1, &target, nullptr);
			if (target) { D3D11_RENDER_TARGET_VIEW_DESC out{}; target->GetDesc(&out); evidence.outputViewFormat = static_cast<std::uint32_t>(out.Format); }
			Guard guard(c);
			auto* s = average.Get(); auto* u = e.uav.Get();
			c->CSSetShaderResources(0, 1, &s); c->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
			c->CSSetShader(shader.Get(), nullptr, 0);
			{ CS_PROFILE_SCOPE("Upscaling::NRColorExposureCapture"); c->Dispatch(1, 1, 1); }
			guard.Clear();
			e.value.evidence = evidence;
			e.value.state = ExposureBindingState::SnapshotQueued;
			++status.captures; status.lastReason.clear();
			if (e.pending) { ++status.dropped; return; }
			if (!e.staging) {
				D3D11_TEXTURE2D_DESC stage{}; e.value.resource->GetDesc(&stage);
				stage.Usage = D3D11_USAGE_STAGING; stage.BindFlags = 0; stage.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(d->CreateTexture2D(&stage, nullptr, &e.staging))) { ++status.dropped; return; }
			}
			if (!e.ready) { D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 }; if (FAILED(d->CreateQuery(&query, &e.ready))) { ++status.dropped; return; } }
			e.pendingGamma = false;
			ComPtr<ID3D11Buffer> frameCB; c->PSGetConstantBuffers(12, 1, &frameCB);
			if (frameCB) {
				D3D11_BUFFER_DESC cb{}; frameCB->GetDesc(&cb);
				e.gammaOffset = (globals::game::isVR ? 84u : 42u) * 16u;
				if (cb.ByteWidth >= e.gammaOffset + sizeof(float) && cb.ByteWidth <= D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u) {
					if (!e.gammaStaging || e.gammaBytes != cb.ByteWidth) {
						e.gammaStaging.Reset();
						D3D11_BUFFER_DESC stage{}; stage.ByteWidth = cb.ByteWidth;
						stage.Usage = D3D11_USAGE_STAGING; stage.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
						if (SUCCEEDED(d->CreateBuffer(&stage, nullptr, &e.gammaStaging))) e.gammaBytes = cb.ByteWidth;
					}
					if (e.gammaStaging) { c->CopyResource(e.gammaStaging.Get(), frameCB.Get()); e.pendingGamma = true; }
				}
			}
			c->CopyResource(e.staging.Get(), e.value.resource.Get());
			c->End(e.ready.Get()); e.pendingEvidence = std::move(evidence); e.pending = true;
		}
	};

	namespace
	{
		// Set once by construction; no initialization recursion through a hook.
		void (*captureCallback)(RE::BSShader*) noexcept = nullptr;
		void CaptureHDR(RE::BSShader* shader) noexcept { if (captureCallback) captureCallback(shader); }
	}
	ExposureCapture::ExposureCapture() : state_(new State)
	{
		captureCallback = [](RE::BSShader* shader) noexcept {
			try { ExposureCapture::Instance().state_->Capture(shader); } catch (...) { /* Never prevent original RestoreTechnique. */ }
		};
	}
	ExposureCapture& ExposureCapture::Instance()
	{
		// Hooks may be called until engine teardown. Explicit Reset/Abandon own
		// resources; the small controller itself has process lifetime.
		static auto* instance = new ExposureCapture;
		return *instance;
	}
	void ExposureCapture::Request(bool enabled) noexcept
	{
		const auto previousValue = state_->requested.exchange(enabled, std::memory_order_acq_rel);
		if (enabled && !previousValue) state_->epoch.fetch_add(1, std::memory_order_acq_rel);
	}
	void ExposureCapture::InstallHooks() noexcept
	{
		if (!state_->requested.load(std::memory_order_acquire)) return;
		try {
			auto* manager = RE::ImageSpaceManager::GetSingleton();
			if (!manager) return;
			std::scoped_lock lock(state_->mutex);
			const std::array effects{ RE::ImageSpaceManager::ISHDRTonemapBlendCinematic, RE::ImageSpaceManager::ISHDRTonemapBlendCinematicFade };
			const std::array<Restore, 2> thunks{ &RestoreTechnique<0>, &RestoreTechnique<1> };
			for (std::size_t i = 0; i < effects.size(); ++i) {
				const auto index = RE::ImageSpaceManager::GetCurrentIndex(effects[i]);
				if (index >= manager->effects.capacity()) continue;
				auto* effect = manager->effects[static_cast<std::uint16_t>(index)];
				if (!effect) continue;
				auto* shader = static_cast<RE::BSImagespaceShader*>(effect);
				if (shader->shaderType.get() != RE::BSShader::Type::ImageSpace) continue;
				owners[i] = shader;
				const auto table = *reinterpret_cast<std::uintptr_t*>(shader);
				if (std::find(tables.begin(), tables.end(), table) != tables.end()) continue;
				auto free = std::find(tables.begin(), tables.end(), std::uintptr_t{ 0 });
				if (free == tables.end()) { state_->status.lastReason = "HDR vtable changed after hook installation; restart required"; continue; }
				const auto slot = static_cast<std::size_t>(free - tables.begin());
				REL::Relocation<std::uintptr_t> vtable{ table };
				const auto prior = reinterpret_cast<Restore>(*reinterpret_cast<std::uintptr_t*>(table + 3 * sizeof(std::uintptr_t)));
				if (!prior || std::find(thunks.begin(), thunks.end(), prior) != thunks.end()) {
					state_->status.lastReason = "HDR restore hook already present or invalid; not chaining recursively"; continue;
				}
				previous[slot] = prior;
				(void)vtable.write_vfunc(3, thunks[slot]);
				*free = table; ++state_->status.hooksInstalled;
			}
		} catch (...) { /* Observation is optional; do not disrupt renderer startup. */ }
	}
	ExposureCaptureStatus ExposureCapture::GetStatus() const
	{
		std::scoped_lock lock(state_->mutex);
		auto status = state_->status;
		status.requested = state_->requested.load(std::memory_order_acquire);
		status.epoch = state_->epoch.load(std::memory_order_acquire);
		return status;
	}
	bool ExposureCapture::Bind(ID3D11DeviceContext* c, ExposureBinding& output, const ExposureTransaction& key)
	{
		if (!c || key.route >= 2 || key.insertion >= 2) return false;
		std::scoped_lock lock(state_->mutex);
		ComPtr<ID3D11Device> device; c->GetDevice(&device);
		ComPtr<ID3D11UnorderedAccessView> unused;
		if (!output.resource && !CreateValueTexture(device.Get(), output, false, unused)) return false;
		output.evidence = {}; output.state = ExposureBindingState::WaitingForHDRPass;
		const auto invalidate = [&]() {
			const std::array<float, 4> invalid{ 0, 0, 1, 0 };
			c->UpdateSubresource(output.resource.Get(), 0, nullptr, invalid.data(), static_cast<UINT>(sizeof(invalid)), 0);
		};
		const bool contextMatches = state_->context.Get() == c && SameDevice(c, state_->device.Get());
		if (contextMatches) state_->Poll();
		auto& latch = state_->latches[key.route * 2u + key.insertion];
		const auto epoch = state_->epoch.load(std::memory_order_acquire);
		// Once chosen, exposure availability is frozen for the whole stereo/ROI
		// transaction, even if an API request toggles capture midway through it.
		if (!latch.occupied || latch.key != key) {
			latch.key = key; latch.captureEpoch = epoch; latch.occupied = true;
			latch.value.evidence = {}; latch.value.state = ExposureBindingState::StaleOrAmbiguous;
			const auto match = std::find_if(state_->entries.begin(), state_->entries.end(), [&](const State::Entry& e) {
				return MatchesExposure(e.value.evidence.stamp, key.sourceWorldFrame, epoch);
			});
			if (!contextMatches) {
				latch.value.state = state_->context ? ExposureBindingState::ContextMismatch : ExposureBindingState::WaitingForHDRPass;
			} else if (match != state_->entries.end()) {
				if (!latch.value.resource && !CreateValueTexture(device.Get(), latch.value, false, unused)) {
					latch.occupied = false; return false;
				}
				c->CopyResource(latch.value.resource.Get(), match->value.resource.Get());
				latch.value.evidence = match->value.evidence;
				latch.value.state = ExposureBindingState::SnapshotQueued;
			}
		}
		output.state = latch.value.state; output.evidence = latch.value.evidence;
		if (output.state == ExposureBindingState::SnapshotQueued) c->CopyResource(output.resource.Get(), latch.value.resource.Get());
		else invalidate();
		return true;
	}
	void ExposureCapture::Reset() noexcept
	{
		std::scoped_lock lock(state_->mutex);
		state_->entries = {}; state_->latches = {};
		state_->shader.Reset(); state_->device.Reset(); state_->context.Reset(); state_->compileAttempted = false;
		state_->epoch.fetch_add(1, std::memory_order_acq_rel);
	}
	void ExposureCapture::Abandon() noexcept
	{
		std::scoped_lock lock(state_->mutex);
		for (auto& e : state_->entries) {
			e.value.Abandon(); (void)e.uav.Detach(); (void)e.staging.Detach();
			(void)e.gammaStaging.Detach(); (void)e.ready.Detach(); e.pending = false;
		}
		for (auto& l : state_->latches) { l.value.Abandon(); l.occupied = false; }
		(void)state_->shader.Detach(); (void)state_->device.Detach(); (void)state_->context.Detach();
		state_->epoch.fetch_add(1, std::memory_order_acq_rel);
	}
	const char* ExposureBindingName(ExposureBindingState value) noexcept
	{
		switch (value) {
		case ExposureBindingState::NotRequested: return "not_requested";
		case ExposureBindingState::WaitingForHDRPass: return "waiting_for_hdr_pass";
		case ExposureBindingState::StaleOrAmbiguous: return "no_matching_unambiguous_source_frame";
		case ExposureBindingState::ContextMismatch: return "context_mismatch";
		case ExposureBindingState::SnapshotQueued: return "gpu_snapshot_queued";
		default: return "resource_failure";
		}
	}
}
