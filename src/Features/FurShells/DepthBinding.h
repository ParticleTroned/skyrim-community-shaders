#pragma once

#include "Utils/ComIdentity.h"
#include "Utils/ManualDeviceContextState.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

namespace FurRendering
{
	/** Finds the engine-owned writable counterpart without changing its registered depth domain. */
	template <class Target, class AsView>
	ID3D11DepthStencilView* FindWritableDepthView(ID3D11DepthStencilView* source, std::span<Target> targets, AsView asView)
	{
		if (!source)
			return nullptr;
		for (const auto& target : targets)
			for (size_t i = 0; i < std::size(target.readOnlyViews); ++i)
				if (asView(target.readOnlyViews[i]) == source)
					if (auto* writable = asView(target.views[i]))
						return writable;
		return nullptr;
	}

	/** Retains native depth-view pairs on the render thread until targets are recreated. */
	class DepthViewCache
	{
	public:
		static constexpr size_t MaximumViews = 16;
		DepthViewCache() = default;
		DepthViewCache(const DepthViewCache&) = delete;
		DepthViewCache& operator=(const DepthViewCache&) = delete;
		struct View
		{
			winrt::com_ptr<ID3D11DepthStencilView> source, writable;
			Microsoft::WRL::ComPtr<IUnknown> resource;
			HRESULT status = E_FAIL;
		};

		/** Caches the native lookup; unknown read-only views retain their original bindings. */
		template <class ResolveView>
		const View* Resolve(ID3D11DepthStencilView* source, ResolveView resolveView)
		{
			const View* cached = last && last->source.get() == source ? last : nullptr;
			if (!cached)
				for (size_t i = 0; i < count; ++i)
					if (views[i].source.get() == source) {
						cached = &views[i];
						break;
					}
			if (cached && SUCCEEDED(cached->status))
				return last = cached;
			if (!source)
				return nullptr;

			winrt::com_ptr<ID3D11DepthStencilView> retained;
			retained.copy_from(source);
			// Native pairs can become available after a rejected lookup without recreating the source.
			const auto index = cached ? static_cast<size_t>(cached - views.data()) : (count < views.size() ? count++ : replacement);
			if (!cached && count == views.size())
				replacement = (index + 1) % views.size();
			auto& view = views[index];
			view = {};
			last = &view;
			view.source = std::move(retained);
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			source->GetDesc(&desc);
			if (!(desc.Flags & D3D11_DSV_READ_ONLY_DEPTH)) {
				view.writable.copy_from(source);
				view.status = S_OK;
				return &view;
			}

			winrt::com_ptr<ID3D11Resource> resource;
			source->GetResource(resource.put());
			view.resource = Util::GetComIdentity(resource.get());
			if (!view.resource) {
				view.status = E_NOINTERFACE;
				return &view;
			}
			view.writable.copy_from(resolveView(source));
			if (!view.writable)
				return &view;
			D3D11_DEPTH_STENCIL_VIEW_DESC writableDesc{};
			view.writable->GetDesc(&writableDesc);
			winrt::com_ptr<ID3D11Resource> writableResource;
			view.writable->GetResource(writableResource.put());
			view.status = !(writableDesc.Flags & D3D11_DSV_READ_ONLY_DEPTH) && SameSubresource(desc, writableDesc) &&
			                      view.resource == Util::GetComIdentity(writableResource.get()) ?
			                  S_OK :
			                  E_INVALIDARG;
			return &view;
		}

		/** Drops retained identities when the renderer recreates its depth targets. */
		void Reset()
		{
			for (size_t i = 0; i < count; ++i)
				views[i] = {};
			count = 0;
			replacement = 0;
			last = nullptr;
		}

	private:
		static bool SameSubresource(const D3D11_DEPTH_STENCIL_VIEW_DESC& left, const D3D11_DEPTH_STENCIL_VIEW_DESC& right)
		{
			if (left.Format != right.Format || left.ViewDimension != right.ViewDimension)
				return false;
			switch (left.ViewDimension) {
			case D3D11_DSV_DIMENSION_TEXTURE1D:
				return left.Texture1D.MipSlice == right.Texture1D.MipSlice;
			case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
				return left.Texture1DArray.MipSlice == right.Texture1DArray.MipSlice &&
				       left.Texture1DArray.FirstArraySlice == right.Texture1DArray.FirstArraySlice && left.Texture1DArray.ArraySize == right.Texture1DArray.ArraySize;
			case D3D11_DSV_DIMENSION_TEXTURE2D:
				return left.Texture2D.MipSlice == right.Texture2D.MipSlice;
			case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
				return left.Texture2DArray.MipSlice == right.Texture2DArray.MipSlice &&
				       left.Texture2DArray.FirstArraySlice == right.Texture2DArray.FirstArraySlice && left.Texture2DArray.ArraySize == right.Texture2DArray.ArraySize;
			case D3D11_DSV_DIMENSION_TEXTURE2DMS:
				return true;
			case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
				return left.Texture2DMSArray.FirstArraySlice == right.Texture2DMSArray.FirstArraySlice && left.Texture2DMSArray.ArraySize == right.Texture2DMSArray.ArraySize;
			default:
				return false;
			}
		}
		std::array<View, MaximumViews> views{};
		size_t count = 0;
		size_t replacement = 0;
		const View* last = nullptr;
	};

	/** Owns native writable depth and aliased inputs for a prepass with stencil writes disabled. */
	class ScopedDepthWrite
	{
	public:
		template <class ResolveView>
		ScopedDepthWrite(ID3D11DeviceContext* a_context, DepthViewCache& cache, ResolveView resolveView) : context(a_context)
		{
			context->OMGetRenderTargets(0, nullptr, previous.put());
			const auto* view = cache.Resolve(previous.get(), resolveView);
			status = view ? view->status : E_FAIL;
			if (FAILED(status) || view->writable.get() == previous.get())
				return;

			bindings.emplace();
			context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, bindings->targets.Put(), nullptr);
			for (UINT i = 0; i < bindings->targets.values.size(); ++i)
				if (bindings->targets.values[i])
					bindings->targetCount = i + 1;
			for (size_t stage = 0; stage < accesses.size(); ++stage) {
				auto& saved = bindings->stages[stage];
				(context->*accesses[stage].get)(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, saved.resources.Put());
				for (size_t slot = 0; slot < saved.resources.values.size(); ++slot) {
					auto* input = saved.resources.values[slot];
					if (!input)
						continue;
					winrt::com_ptr<ID3D11Resource> resource;
					input->GetResource(resource.put());
					const auto identity = Util::GetComIdentity(resource.get());
					if (!identity) {
						status = E_NOINTERFACE;
						bindings.reset();
						return;
					}
					if (identity == view->resource)
						saved.aliases.set(slot);
				}
			}
			for (size_t stage = 0; stage < accesses.size(); ++stage) {
				const auto& saved = bindings->stages[stage];
				if (saved.aliases.none())
					continue;
				auto next = saved.resources.values;
				for (size_t slot = 0; slot < next.size(); ++slot)
					if (saved.aliases[slot])
						next[slot] = nullptr;
				(context->*accesses[stage].set)(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, next.data());
			}
			Bind(view->writable.get());
		}

		~ScopedDepthWrite()
		{
			if (!bindings)
				return;
			Bind(previous.get());
			for (size_t stage = 0; stage < accesses.size(); ++stage)
				if (bindings->stages[stage].aliases.any())
					(context->*accesses[stage].set)(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, bindings->stages[stage].resources.Data());
		}
		ScopedDepthWrite(const ScopedDepthWrite&) = delete;
		ScopedDepthWrite& operator=(const ScopedDepthWrite&) = delete;
		/** Failure leaves the original output binding unchanged. */
		[[nodiscard]] HRESULT Status() const { return status; }

	private:
		struct Access
		{
			decltype(&ID3D11DeviceContext::VSGetShaderResources) get;
			decltype(&ID3D11DeviceContext::VSSetShaderResources) set;
		};
		static constexpr std::array<Access, 6> accesses{ {
			{ &ID3D11DeviceContext::VSGetShaderResources, &ID3D11DeviceContext::VSSetShaderResources },
			{ &ID3D11DeviceContext::HSGetShaderResources, &ID3D11DeviceContext::HSSetShaderResources },
			{ &ID3D11DeviceContext::DSGetShaderResources, &ID3D11DeviceContext::DSSetShaderResources },
			{ &ID3D11DeviceContext::GSGetShaderResources, &ID3D11DeviceContext::GSSetShaderResources },
			{ &ID3D11DeviceContext::PSGetShaderResources, &ID3D11DeviceContext::PSSetShaderResources },
			{ &ID3D11DeviceContext::CSGetShaderResources, &ID3D11DeviceContext::CSSetShaderResources },
		} };
		struct Stage
		{
			Util::detail::StateReferences<ID3D11ShaderResourceView, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources;
			std::bitset<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> aliases;
		};
		struct Bindings
		{
			Util::detail::StateReferences<ID3D11RenderTargetView, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
			UINT targetCount = 0;
			std::array<Stage, accesses.size()> stages;
		};
		void Bind(ID3D11DepthStencilView* depth)
		{
			// Keeping pixel UAVs also preserves their hidden append/consume counters.
			context->OMSetRenderTargetsAndUnorderedAccessViews(bindings->targetCount, bindings->targets.Data(), depth,
				0, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, nullptr, nullptr);
		}

		ID3D11DeviceContext* context;
		winrt::com_ptr<ID3D11DepthStencilView> previous;
		std::optional<Bindings> bindings;
		HRESULT status = E_FAIL;
	};
}
