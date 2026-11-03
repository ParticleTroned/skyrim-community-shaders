#include "RenderMap/D3DContextHooks.h"

#include "RenderMap/Runtime.h"

#include <d3d11.h>

#include <algorithm>
#include <array>

namespace CSX::RenderMap
{
#ifdef CSX_RENDER_MAP_CONTEXT_TESTING
	template <class Signature>
	using ContextOriginal = Signature*;
#else
	template <class Signature>
	using ContextOriginal = REL::Relocation<Signature>;
#endif
	namespace
	{
		// Skip known-stale metadata; publication rechecks after reentrant getters.
		bool CaptureCurrent(std::uint64_t a_generation) noexcept
		{
			return a_generation != 0 && GetRuntime().ActiveCaptureGeneration() == a_generation;
		}

		template <class Current>
		ResourceObservationInput DescribeResourceWhen(ID3D11Resource* a_resource, Current&& a_current) noexcept
		{
			ResourceObservationInput result;
			if (!a_resource || !a_current())
				return result;
			result.d3dObject = reinterpret_cast<std::uintptr_t>(a_resource);
			D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
			a_resource->GetType(&dimension);
			if (!a_current())
				return result;
			switch (dimension) {
			case D3D11_RESOURCE_DIMENSION_BUFFER:
				{
					D3D11_BUFFER_DESC desc{};
					reinterpret_cast<ID3D11Buffer*>(a_resource)->GetDesc(&desc);
					result.dimension = ResourceDimension::kBuffer;
					result.widthOrBytes = desc.ByteWidth;
					result.usage = desc.Usage;
					result.bindFlags = desc.BindFlags;
					result.cpuAccessFlags = desc.CPUAccessFlags;
					result.miscFlags = desc.MiscFlags;
					result.structureByteStride = desc.StructureByteStride;
					break;
				}
			case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
				{
					D3D11_TEXTURE1D_DESC desc{};
					reinterpret_cast<ID3D11Texture1D*>(a_resource)->GetDesc(&desc);
					result.dimension = ResourceDimension::kTexture1D;
					result.widthOrBytes = desc.Width;
					result.depthOrArraySize = desc.ArraySize;
					result.mipLevels = desc.MipLevels;
					result.format = desc.Format;
					result.usage = desc.Usage;
					result.bindFlags = desc.BindFlags;
					result.cpuAccessFlags = desc.CPUAccessFlags;
					result.miscFlags = desc.MiscFlags;
					break;
				}
			case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
				{
					D3D11_TEXTURE2D_DESC desc{};
					reinterpret_cast<ID3D11Texture2D*>(a_resource)->GetDesc(&desc);
					result.dimension = ResourceDimension::kTexture2D;
					result.widthOrBytes = desc.Width;
					result.height = desc.Height;
					result.depthOrArraySize = desc.ArraySize;
					result.mipLevels = desc.MipLevels;
					result.format = desc.Format;
					result.sampleCount = desc.SampleDesc.Count;
					result.sampleQuality = desc.SampleDesc.Quality;
					result.usage = desc.Usage;
					result.bindFlags = desc.BindFlags;
					result.cpuAccessFlags = desc.CPUAccessFlags;
					result.miscFlags = desc.MiscFlags;
					break;
				}
			case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
				{
					D3D11_TEXTURE3D_DESC desc{};
					reinterpret_cast<ID3D11Texture3D*>(a_resource)->GetDesc(&desc);
					result.dimension = ResourceDimension::kTexture3D;
					result.widthOrBytes = desc.Width;
					result.height = desc.Height;
					result.depthOrArraySize = desc.Depth;
					result.mipLevels = desc.MipLevels;
					result.format = desc.Format;
					result.usage = desc.Usage;
					result.bindFlags = desc.BindFlags;
					result.cpuAccessFlags = desc.CPUAccessFlags;
					result.miscFlags = desc.MiscFlags;
					break;
				}
			default:
				break;
			}
			return result;
		}
	}

	ResourceObservationInput DescribeResource(ID3D11Resource* a_resource) noexcept
	{
		return DescribeResourceWhen(a_resource, []() noexcept { return true; });
	}

	namespace
	{
		ResourceObservationInput DescribeCapturedResource(ID3D11Resource* a_resource, std::uint64_t a_generation) noexcept
		{
			return DescribeResourceWhen(a_resource, [a_generation]() noexcept { return CaptureCurrent(a_generation); });
		}

		void RegisterDeferredContextIfNeeded(ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration) noexcept
		{
			if (CaptureCurrent(a_expectedCaptureGeneration) && a_context && a_context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED) {
				if (CaptureCurrent(a_expectedCaptureGeneration))
					GetRuntime().ForCapture(a_expectedCaptureGeneration).RegisterDeferredContext(reinterpret_cast<std::uintptr_t>(a_context), a_context->GetContextFlags(), false);
			}
		}

		std::uint64_t ReadQpc() noexcept
		{
			LARGE_INTEGER value{};
			return ::QueryPerformanceCounter(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 0;
		}

		template <class T>
		ResourceViewInput DescribeViewBase(T* a_view, TargetViewKind a_kind, std::uint64_t a_generation)
		{
			ResourceViewInput result;
			result.view.kind = a_kind;
			result.view.d3dObject = reinterpret_cast<std::uintptr_t>(a_view);
			if (!a_view || !CaptureCurrent(a_generation))
				return result;
			ID3D11Resource* resource = nullptr;
			a_view->GetResource(&resource);
			result.resource = DescribeCapturedResource(resource, a_generation);
			if (resource)
				resource->Release();
			return result;
		}

		ResourceViewInput DescribeView(ID3D11RenderTargetView* a_view, std::uint64_t a_generation)
		{
			auto result = DescribeViewBase(a_view, TargetViewKind::kRenderTarget, a_generation);
			if (!a_view || !CaptureCurrent(a_generation))
				return result;
			D3D11_RENDER_TARGET_VIEW_DESC desc{};
			a_view->GetDesc(&desc);
			result.view.format = desc.Format;
			result.view.dimension = desc.ViewDimension;
			switch (desc.ViewDimension) {
			case D3D11_RTV_DIMENSION_TEXTURE1D:
				result.view.mipSlice = desc.Texture1D.MipSlice;
				break;
			case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
				result.view.mipSlice = desc.Texture1DArray.MipSlice;
				result.view.firstArraySlice = desc.Texture1DArray.FirstArraySlice;
				result.view.arraySize = desc.Texture1DArray.ArraySize;
				break;
			case D3D11_RTV_DIMENSION_TEXTURE2D:
				result.view.mipSlice = desc.Texture2D.MipSlice;
				break;
			case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
				result.view.mipSlice = desc.Texture2DArray.MipSlice;
				result.view.firstArraySlice = desc.Texture2DArray.FirstArraySlice;
				result.view.arraySize = desc.Texture2DArray.ArraySize;
				break;
			case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
				result.view.firstArraySlice = desc.Texture2DMSArray.FirstArraySlice;
				result.view.arraySize = desc.Texture2DMSArray.ArraySize;
				break;
			case D3D11_RTV_DIMENSION_TEXTURE3D:
				result.view.mipSlice = desc.Texture3D.MipSlice;
				result.view.firstArraySlice = desc.Texture3D.FirstWSlice;
				result.view.arraySize = desc.Texture3D.WSize;
				break;
			case D3D11_RTV_DIMENSION_BUFFER:
				result.view.firstElement = desc.Buffer.FirstElement;
				result.view.elementCount = desc.Buffer.NumElements;
				break;
			default:
				break;
			}
			return result;
		}

		ResourceViewInput DescribeView(ID3D11DepthStencilView* a_view, std::uint64_t a_generation)
		{
			auto result = DescribeViewBase(a_view, TargetViewKind::kDepthTarget, a_generation);
			if (!a_view || !CaptureCurrent(a_generation))
				return result;
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			a_view->GetDesc(&desc);
			result.view.format = desc.Format;
			result.view.dimension = desc.ViewDimension;
			result.view.flags = desc.Flags;
			switch (desc.ViewDimension) {
			case D3D11_DSV_DIMENSION_TEXTURE1D:
				result.view.mipSlice = desc.Texture1D.MipSlice;
				break;
			case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
				result.view.mipSlice = desc.Texture1DArray.MipSlice;
				result.view.firstArraySlice = desc.Texture1DArray.FirstArraySlice;
				result.view.arraySize = desc.Texture1DArray.ArraySize;
				break;
			case D3D11_DSV_DIMENSION_TEXTURE2D:
				result.view.mipSlice = desc.Texture2D.MipSlice;
				break;
			case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
				result.view.mipSlice = desc.Texture2DArray.MipSlice;
				result.view.firstArraySlice = desc.Texture2DArray.FirstArraySlice;
				result.view.arraySize = desc.Texture2DArray.ArraySize;
				break;
			case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
				result.view.firstArraySlice = desc.Texture2DMSArray.FirstArraySlice;
				result.view.arraySize = desc.Texture2DMSArray.ArraySize;
				break;
			default:
				break;
			}
			return result;
		}

		ResourceViewInput DescribeView(ID3D11ShaderResourceView* a_view, std::uint64_t a_generation)
		{
			auto result = DescribeViewBase(a_view, TargetViewKind::kShaderResource, a_generation);
			if (!a_view || !CaptureCurrent(a_generation))
				return result;
			D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
			a_view->GetDesc(&desc);
			result.view.format = desc.Format;
			result.view.dimension = desc.ViewDimension;
			switch (desc.ViewDimension) {
			case D3D11_SRV_DIMENSION_BUFFER:
				result.view.firstElement = desc.Buffer.FirstElement;
				result.view.elementCount = desc.Buffer.NumElements;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE1D:
				result.view.mipSlice = desc.Texture1D.MostDetailedMip;
				result.view.arraySize = desc.Texture1D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
				result.view.mipSlice = desc.Texture1DArray.MostDetailedMip;
				result.view.arraySize = desc.Texture1DArray.MipLevels;
				result.view.firstArraySlice = desc.Texture1DArray.FirstArraySlice;
				result.view.elementCount = desc.Texture1DArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2D:
				result.view.mipSlice = desc.Texture2D.MostDetailedMip;
				result.view.arraySize = desc.Texture2D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
				result.view.mipSlice = desc.Texture2DArray.MostDetailedMip;
				result.view.arraySize = desc.Texture2DArray.MipLevels;
				result.view.firstArraySlice = desc.Texture2DArray.FirstArraySlice;
				result.view.elementCount = desc.Texture2DArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
				result.view.firstArraySlice = desc.Texture2DMSArray.FirstArraySlice;
				result.view.arraySize = desc.Texture2DMSArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE3D:
				result.view.mipSlice = desc.Texture3D.MostDetailedMip;
				result.view.arraySize = desc.Texture3D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBE:
				result.view.mipSlice = desc.TextureCube.MostDetailedMip;
				result.view.arraySize = desc.TextureCube.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
				result.view.mipSlice = desc.TextureCubeArray.MostDetailedMip;
				result.view.arraySize = desc.TextureCubeArray.MipLevels;
				result.view.firstArraySlice = desc.TextureCubeArray.First2DArrayFace;
				result.view.elementCount = desc.TextureCubeArray.NumCubes * 6u;
				break;
			case D3D11_SRV_DIMENSION_BUFFEREX:
				result.view.firstElement = desc.BufferEx.FirstElement;
				result.view.elementCount = desc.BufferEx.NumElements;
				result.view.flags = desc.BufferEx.Flags;
				break;
			default:
				break;
			}
			return result;
		}

		ResourceViewInput DescribeView(ID3D11UnorderedAccessView* a_view, std::uint64_t a_generation)
		{
			auto result = DescribeViewBase(a_view, TargetViewKind::kUnorderedAccess, a_generation);
			if (!a_view || !CaptureCurrent(a_generation))
				return result;
			D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
			a_view->GetDesc(&desc);
			result.view.format = desc.Format;
			result.view.dimension = desc.ViewDimension;
			switch (desc.ViewDimension) {
			case D3D11_UAV_DIMENSION_BUFFER:
				result.view.firstElement = desc.Buffer.FirstElement;
				result.view.elementCount = desc.Buffer.NumElements;
				result.view.flags = desc.Buffer.Flags;
				break;
			case D3D11_UAV_DIMENSION_TEXTURE1D:
				result.view.mipSlice = desc.Texture1D.MipSlice;
				break;
			case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
				result.view.mipSlice = desc.Texture1DArray.MipSlice;
				result.view.firstArraySlice = desc.Texture1DArray.FirstArraySlice;
				result.view.arraySize = desc.Texture1DArray.ArraySize;
				break;
			case D3D11_UAV_DIMENSION_TEXTURE2D:
				result.view.mipSlice = desc.Texture2D.MipSlice;
				break;
			case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
				result.view.mipSlice = desc.Texture2DArray.MipSlice;
				result.view.firstArraySlice = desc.Texture2DArray.FirstArraySlice;
				result.view.arraySize = desc.Texture2DArray.ArraySize;
				break;
			case D3D11_UAV_DIMENSION_TEXTURE3D:
				result.view.mipSlice = desc.Texture3D.MipSlice;
				result.view.firstArraySlice = desc.Texture3D.FirstWSlice;
				result.view.arraySize = desc.Texture3D.WSize;
				break;
			default:
				break;
			}
			return result;
		}
		std::uint64_t PackSignedAndUnsigned(std::int32_t a_signed, std::uint32_t a_unsigned) noexcept
		{
			return static_cast<std::uint32_t>(a_signed) |
			       (static_cast<std::uint64_t>(a_unsigned) << 32u);
		}

		void ObserveRenderTargets(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration,
			UINT a_renderTargetCount,
			ID3D11RenderTargetView* const* a_renderTargets,
			ID3D11DepthStencilView* a_depthTarget,
			bool a_keepTargets = false,
			TargetBindingSource a_source = TargetBindingSource::kObservedCall)
		{
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			std::array<ResourceViewInput, kMaximumRenderTargets> views{};
			if (!a_keepTargets && a_renderTargets) {
				const auto count = std::min<UINT>(a_renderTargetCount, static_cast<UINT>(views.size()));
				for (UINT index = 0; index < count; ++index)
					views[index] = DescribeView(a_renderTargets[index], a_expectedCaptureGeneration);
			}
			const auto depth = DescribeChangedDepthTarget(a_depthTarget, a_keepTargets,
				[a_expectedCaptureGeneration](ID3D11DepthStencilView* a_view) { return DescribeView(a_view, a_expectedCaptureGeneration); });
			GetRuntime().ForCapture(a_expectedCaptureGeneration).BindRenderTargetViews(reinterpret_cast<std::uintptr_t>(a_context), a_keepTargets ? 0u : a_renderTargetCount, views.data(), a_depthTarget ? &depth : nullptr, a_keepTargets, a_source, a_expectedCaptureGeneration);
		}

		void ObserveEffectiveRenderTargets(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration,
			TargetBindingSource a_source)
		{
			if (!a_context || !CaptureCurrent(a_expectedCaptureGeneration))
				return;

			std::array<ID3D11RenderTargetView*, kMaximumRenderTargets> targets{};
			ID3D11DepthStencilView* depth = nullptr;
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			a_context->OMGetRenderTargets(
				static_cast<UINT>(targets.size()), targets.data(), &depth);
			std::array<ResourceViewInput, kMaximumRenderTargets> views{};
			std::uint32_t targetCount = 0;
			for (std::size_t index = 0; index < targets.size(); ++index) {
				views[index] = DescribeView(targets[index], a_expectedCaptureGeneration);
				if (targets[index])
					targetCount = static_cast<std::uint32_t>(index + 1);
			}
			const auto depthView = DescribeView(depth, a_expectedCaptureGeneration);
			GetRuntime().ForCapture(a_expectedCaptureGeneration).BindRenderTargetViews(reinterpret_cast<std::uintptr_t>(a_context), targetCount, views.data(), depth ? &depthView : nullptr, false, a_source, a_expectedCaptureGeneration);
			for (auto* target : targets) {
				if (target)
					target->Release();
			}
			if (depth)
				depth->Release();
			if (a_source == TargetBindingSource::kPostCallQuery)
				GetRuntime().ForCapture(a_expectedCaptureGeneration).ClaimRenderTargetStateSeed(reinterpret_cast<std::uintptr_t>(a_context));
		}

		void ObserveEffectiveStateBeforeDraw(ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration);
		void ObserveEffectiveStateBeforeDispatch(ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration);
		void ObservePostProcessingState(ID3D11DeviceContext* a_context, bool a_compute, bool a_after,
			std::uint64_t a_expectedCaptureGeneration);
		void BeginPostProcessingOperation(ID3D11DeviceContext* a_context, bool a_compute, std::uint32_t a_operation,
			std::uint64_t a_expectedCaptureGeneration, bool a_bootstrap = false);

		template <class... Args>
		void RecordDrawWithEffectiveState(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration, DrawOperation a_operation, Args... a_arguments)
		{
			RegisterDeferredContextIfNeeded(a_context, a_expectedCaptureGeneration);
			if (CaptureCurrent(a_expectedCaptureGeneration) && a_context && a_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE)
				ObserveEffectiveStateBeforeDraw(a_context, a_expectedCaptureGeneration);
			BeginPostProcessingOperation(a_context, false, static_cast<std::uint32_t>(a_operation), a_expectedCaptureGeneration);
			ObservePostProcessingState(a_context, false, false, a_expectedCaptureGeneration);
			GetRuntime().ForCapture(a_expectedCaptureGeneration).RecordDraw(reinterpret_cast<std::uintptr_t>(a_context), a_operation, a_arguments...);
		}

		template <class... Args>
		void RecordDispatchWithEffectiveState(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration, DispatchOperation a_operation, Args... a_arguments)
		{
			RegisterDeferredContextIfNeeded(a_context, a_expectedCaptureGeneration);
			if (CaptureCurrent(a_expectedCaptureGeneration) && a_context && a_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE)
				ObserveEffectiveStateBeforeDispatch(a_context, a_expectedCaptureGeneration);
			BeginPostProcessingOperation(a_context, true, static_cast<std::uint32_t>(a_operation), a_expectedCaptureGeneration);
			ObservePostProcessingState(a_context, true, false, a_expectedCaptureGeneration);
			GetRuntime().ForCapture(a_expectedCaptureGeneration).RecordDispatch(reinterpret_cast<std::uintptr_t>(a_context), a_operation, a_arguments...);
		}

		void ObserveShaderResources(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration,
			ResourceStage a_stage,
			UINT a_startSlot,
			UINT a_viewCount,
			ID3D11ShaderResourceView* const* a_views,
			ResourceBindingSource a_source = ResourceBindingSource::kRequestedCall)
		{
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			std::array<ResourceViewInput, kMaximumShaderResourceSlots> views{};
			const auto count = std::min<UINT>(a_viewCount, static_cast<UINT>(views.size()));
			for (UINT index = 0; index < count; ++index)
				views[index] = DescribeView(a_views ? a_views[index] : nullptr, a_expectedCaptureGeneration);
			GetRuntime().ForCapture(a_expectedCaptureGeneration).BindResourceViews(reinterpret_cast<std::uintptr_t>(a_context), ResourceBindingKind::kShaderResource, a_stage, a_startSlot, count, views.data(), false, a_source, a_expectedCaptureGeneration);
		}

		void ObserveEffectiveShaderResources(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration,
			ResourceStage a_stage,
			UINT a_startSlot,
			UINT a_viewCount,
			ResourceBindingSource a_source)
		{
			if (!a_context || !CaptureCurrent(a_expectedCaptureGeneration) ||
				a_startSlot >= kMaximumShaderResourceSlots) {
				return;
			}
			const auto count = std::min<UINT>(
				a_viewCount, static_cast<UINT>(kMaximumShaderResourceSlots - a_startSlot));
			std::array<ID3D11ShaderResourceView*, kMaximumShaderResourceSlots> views{};
			switch (a_stage) {
			case ResourceStage::kVertex:
				a_context->VSGetShaderResources(a_startSlot, count, views.data());
				break;
			case ResourceStage::kHull:
				a_context->HSGetShaderResources(a_startSlot, count, views.data());
				break;
			case ResourceStage::kDomain:
				a_context->DSGetShaderResources(a_startSlot, count, views.data());
				break;
			case ResourceStage::kGeometry:
				a_context->GSGetShaderResources(a_startSlot, count, views.data());
				break;
			case ResourceStage::kPixel:
				a_context->PSGetShaderResources(a_startSlot, count, views.data());
				break;
			case ResourceStage::kCompute:
				a_context->CSGetShaderResources(a_startSlot, count, views.data());
				break;
			default:
				return;
			}
			ObserveShaderResources(
				a_context, a_expectedCaptureGeneration, a_stage, a_startSlot, count, views.data(), a_source);
			for (UINT index = 0; index < count; ++index) {
				if (views[index])
					views[index]->Release();
			}
		}

		void ObserveAllEffectiveShaderResources(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration,
			ResourceBindingSource a_source)
		{
			for (const auto stage : std::array{
					 ResourceStage::kVertex, ResourceStage::kHull, ResourceStage::kDomain,
					 ResourceStage::kGeometry, ResourceStage::kPixel, ResourceStage::kCompute }) {
				ObserveEffectiveShaderResources(
					a_context, a_expectedCaptureGeneration, stage, 0, static_cast<UINT>(kMaximumShaderResourceSlots),
					a_source);
			}
		}

		void ObserveUnorderedAccessViews(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration,
			ResourceStage a_stage,
			UINT a_startSlot,
			UINT a_viewCount,
			ID3D11UnorderedAccessView* const* a_views,
			bool a_keepViews = false)
		{
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			std::array<ResourceViewInput, kMaximumUnorderedAccessSlots> views{};
			if (!a_keepViews) {
				const auto count = std::min<UINT>(a_viewCount, static_cast<UINT>(views.size()));
				for (UINT index = 0; index < count; ++index)
					views[index] = DescribeView(a_views ? a_views[index] : nullptr, a_expectedCaptureGeneration);
			}
			GetRuntime().ForCapture(a_expectedCaptureGeneration).BindResourceViews(reinterpret_cast<std::uintptr_t>(a_context), ResourceBindingKind::kUnorderedAccess, a_stage, a_startSlot, a_viewCount, views.data(), a_keepViews);
		}

		void ObserveEffectiveUnorderedAccessViews(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration,
			ResourceStage a_stage,
			UINT a_startSlot,
			UINT a_viewCount,
			ResourceBindingSource a_source)
		{
			if (!a_context || !CaptureCurrent(a_expectedCaptureGeneration) ||
				a_startSlot >= kMaximumUnorderedAccessSlots) {
				return;
			}
			const auto count = std::min<UINT>(
				a_viewCount, static_cast<UINT>(kMaximumUnorderedAccessSlots - a_startSlot));
			std::array<ID3D11UnorderedAccessView*, kMaximumUnorderedAccessSlots> views{};
			if (a_stage == ResourceStage::kCompute) {
				if (!CaptureCurrent(a_expectedCaptureGeneration))
					return;
				a_context->CSGetUnorderedAccessViews(a_startSlot, count, views.data());
			} else if (a_stage == ResourceStage::kOutputMerger) {
				if (!CaptureCurrent(a_expectedCaptureGeneration))
					return;
				a_context->OMGetRenderTargetsAndUnorderedAccessViews(
					0, nullptr, nullptr, a_startSlot, count, views.data());
			} else {
				return;
			}
			std::array<ResourceViewInput, kMaximumUnorderedAccessSlots> described{};
			for (UINT index = 0; index < count; ++index)
				described[index] = DescribeView(views[index], a_expectedCaptureGeneration);
			GetRuntime().ForCapture(a_expectedCaptureGeneration).BindResourceViews(reinterpret_cast<std::uintptr_t>(a_context), ResourceBindingKind::kUnorderedAccess, a_stage, a_startSlot, count, described.data(), false, a_source, a_expectedCaptureGeneration);
			for (UINT index = 0; index < count; ++index) {
				if (views[index])
					views[index]->Release();
			}
		}

		void ObserveEffectiveStateBeforeDraw(ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration)
		{
			if (!a_context)
				return;
			auto& runtime = GetRuntime();
			const auto context = reinterpret_cast<std::uintptr_t>(a_context);
			const auto targetGeneration = runtime.ForCapture(a_expectedCaptureGeneration).ClaimRenderTargetStateSeed(context);
			if (targetGeneration != 0) {
				ObserveEffectiveRenderTargets(
					a_context, targetGeneration, TargetBindingSource::kCaptureStateSnapshot);
			}
			const auto resourceGeneration = runtime.ForCapture(a_expectedCaptureGeneration).ClaimResourceViewStateSeed(context);
			if (resourceGeneration != 0) {
				ObserveAllEffectiveShaderResources(
					a_context, resourceGeneration, ResourceBindingSource::kCaptureStateSnapshot);
				ObserveEffectiveUnorderedAccessViews(
					a_context, resourceGeneration, ResourceStage::kCompute, 0,
					static_cast<UINT>(kMaximumUnorderedAccessSlots),
					ResourceBindingSource::kCaptureStateSnapshot);
				ObserveEffectiveUnorderedAccessViews(
					a_context, resourceGeneration, ResourceStage::kOutputMerger, 0,
					D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
					ResourceBindingSource::kCaptureStateSnapshot);
			}
		}

		void ObserveAllEffectiveResourceViews(
			ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration,
			ResourceBindingSource a_source)
		{
			ObserveAllEffectiveShaderResources(a_context, a_expectedCaptureGeneration, a_source);
			ObserveEffectiveUnorderedAccessViews(
				a_context, a_expectedCaptureGeneration, ResourceStage::kCompute, 0,
				static_cast<UINT>(kMaximumUnorderedAccessSlots),
				a_source);
			ObserveEffectiveUnorderedAccessViews(
				a_context, a_expectedCaptureGeneration, ResourceStage::kOutputMerger, 0,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
				a_source);
			if (a_source == ResourceBindingSource::kPostCallQuery)
				GetRuntime().ForCapture(a_expectedCaptureGeneration).ClaimResourceViewStateSeed(reinterpret_cast<std::uintptr_t>(a_context));
		}

		void ObserveEffectiveStateBeforeDispatch(ID3D11DeviceContext* a_context, std::uint64_t a_expectedCaptureGeneration)
		{
			if (!a_context)
				return;
			auto& runtime = GetRuntime();
			const auto generation = runtime.ForCapture(a_expectedCaptureGeneration).ClaimResourceViewStateSeed(reinterpret_cast<std::uintptr_t>(a_context));
			if (generation != 0) {
				ObserveAllEffectiveResourceViews(
					a_context, generation, ResourceBindingSource::kCaptureStateSnapshot);
			}
		}

		void BeginPostProcessingOperation(ID3D11DeviceContext* a_context, bool a_compute, std::uint32_t a_operation,
			std::uint64_t a_expectedCaptureGeneration, bool a_bootstrap)
		{
			if (!CaptureCurrent(a_expectedCaptureGeneration) || !a_context || !GetRuntime().IsInsidePostProcessing() ||
				a_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return;
			winrt::com_ptr<ID3D11VertexShader> vs;
			winrt::com_ptr<ID3D11HullShader> hs;
			winrt::com_ptr<ID3D11DomainShader> ds;
			winrt::com_ptr<ID3D11GeometryShader> gs;
			winrt::com_ptr<ID3D11PixelShader> ps;
			winrt::com_ptr<ID3D11ComputeShader> cs;
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			a_context->VSGetShader(vs.put(), nullptr, nullptr);
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			a_context->HSGetShader(hs.put(), nullptr, nullptr);
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			a_context->DSGetShader(ds.put(), nullptr, nullptr);
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			a_context->GSGetShader(gs.put(), nullptr, nullptr);
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			a_context->PSGetShader(ps.put(), nullptr, nullptr);
			if (!CaptureCurrent(a_expectedCaptureGeneration))
				return;
			a_context->CSGetShader(cs.put(), nullptr, nullptr);
			const std::array<IUnknown*, 6> shaders{ vs.get(), hs.get(), ds.get(), gs.get(), ps.get(), cs.get() };
			std::array<std::uintptr_t, 6> pointers{};
			for (std::size_t index = 0; index < shaders.size(); ++index)
				pointers[index] = reinterpret_cast<std::uintptr_t>(shaders[index]);
			if (a_bootstrap)
				GetRuntime().RecordPostProcessingBootstrap(pointers, a_expectedCaptureGeneration);
			else
				GetRuntime().ForCapture(a_expectedCaptureGeneration).BeginTransferOperation(reinterpret_cast<std::uintptr_t>(a_context), a_compute, a_operation, pointers);
		}

		void ObservePostProcessingState(ID3D11DeviceContext* a_context, bool a_compute, bool a_after,
			std::uint64_t a_expectedCaptureGeneration)
		{
			auto& runtime = GetRuntime();
			if (!CaptureCurrent(a_expectedCaptureGeneration) || !a_context || !runtime.IsInsidePostProcessing() ||
				a_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return;
			const auto context = reinterpret_cast<std::uintptr_t>(a_context);
			if (!a_after) {
				std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
				std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
				UINT viewportCount = static_cast<UINT>(viewports.size());
				UINT scissorCount = static_cast<UINT>(scissors.size());
				if (!CaptureCurrent(a_expectedCaptureGeneration))
					return;
				a_context->RSGetViewports(&viewportCount, viewports.data());
				if (!CaptureCurrent(a_expectedCaptureGeneration))
					return;
				a_context->RSGetScissorRects(&scissorCount, scissors.data());
				winrt::com_ptr<ID3D11RasterizerState> rasterizer;
				if (!CaptureCurrent(a_expectedCaptureGeneration))
					return;
				a_context->RSGetState(rasterizer.put());
				D3D11_RASTERIZER_DESC desc{};
				if (rasterizer && CaptureCurrent(a_expectedCaptureGeneration)) {
					rasterizer->GetDesc(&desc);
				}
				const auto count = std::max(1u, std::max(viewportCount, scissorCount));
				for (UINT index = 0; index < std::min<UINT>(count, static_cast<UINT>(viewports.size())); ++index) {
					const auto& vp = viewports[index];
					const auto& sc = scissors[index];
					runtime.ForCapture(a_expectedCaptureGeneration).RecordRasterState(context, index, { vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height, vp.MinDepth, vp.MaxDepth }, { sc.left, sc.top, sc.right, sc.bottom }, viewportCount, scissorCount, desc.ScissorEnable != FALSE);
				}
				for (const auto stage : std::array{ ResourceStage::kVertex, ResourceStage::kHull,
						 ResourceStage::kDomain, ResourceStage::kGeometry, ResourceStage::kPixel, ResourceStage::kCompute }) {
					if ((stage == ResourceStage::kCompute) != a_compute)
						continue;
					std::array<ID3D11ShaderResourceView*, kMaximumShaderResourceSlots> views{};
					const auto slots = static_cast<UINT>(views.size());
					if (!CaptureCurrent(a_expectedCaptureGeneration))
						return;
					switch (stage) {
					case ResourceStage::kVertex:
						a_context->VSGetShaderResources(0, slots, views.data());
						break;
					case ResourceStage::kHull:
						a_context->HSGetShaderResources(0, slots, views.data());
						break;
					case ResourceStage::kDomain:
						a_context->DSGetShaderResources(0, slots, views.data());
						break;
					case ResourceStage::kGeometry:
						a_context->GSGetShaderResources(0, slots, views.data());
						break;
					case ResourceStage::kPixel:
						a_context->PSGetShaderResources(0, slots, views.data());
						break;
					case ResourceStage::kCompute:
						a_context->CSGetShaderResources(0, slots, views.data());
						break;
					default:
						break;
					}
					std::array<winrt::com_ptr<ID3D11ShaderResourceView>, kMaximumShaderResourceSlots> retained;
					for (UINT slot = 0; slot < slots; ++slot)
						retained[slot].attach(views[slot]);
					for (UINT slot = 0; slot < slots; ++slot) {
						if (!views[slot])
							continue;
						runtime.ForCapture(a_expectedCaptureGeneration).RecordTransferResourceAccess(context, DescribeView(views[slot], a_expectedCaptureGeneration), stage, slot, false);
					}
				}
				return;
			}
			if (!a_compute) {
				std::array<ID3D11RenderTargetView*, kMaximumRenderTargets> targets{};
				if (!CaptureCurrent(a_expectedCaptureGeneration))
					return;
				a_context->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), nullptr);
				std::array<winrt::com_ptr<ID3D11RenderTargetView>, kMaximumRenderTargets> retained;
				for (UINT slot = 0; slot < targets.size(); ++slot)
					retained[slot].attach(targets[slot]);
				for (UINT slot = 0; slot < targets.size(); ++slot) {
					if (!targets[slot])
						continue;
					runtime.ForCapture(a_expectedCaptureGeneration).RecordTransferResourceAccess(context, DescribeView(targets[slot], a_expectedCaptureGeneration), ResourceStage::kOutputMerger, slot, true);
				}
			}
			std::array<ID3D11UnorderedAccessView*, kMaximumUnorderedAccessSlots> uavs{};
			if (a_compute) {
				if (!CaptureCurrent(a_expectedCaptureGeneration))
					return;
				a_context->CSGetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data());
			} else {
				if (!CaptureCurrent(a_expectedCaptureGeneration))
					return;
				a_context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
					D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, uavs.data());
			}
			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMaximumUnorderedAccessSlots> retained;
			for (UINT slot = 0; slot < uavs.size(); ++slot)
				retained[slot].attach(uavs[slot]);
			for (UINT slot = 0; slot < uavs.size(); ++slot) {
				if (!uavs[slot])
					continue;
				runtime.ForCapture(a_expectedCaptureGeneration).RecordTransferResourceAccess(context, DescribeView(uavs[slot], a_expectedCaptureGeneration), a_compute ? ResourceStage::kCompute : ResourceStage::kOutputMerger, slot, true);
			}
		}

		struct ID3D11DeviceContext_OMSetRenderTargets
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_renderTargetCount,
				ID3D11RenderTargetView* const* a_renderTargets, ID3D11DepthStencilView* a_depthTarget)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_renderTargetCount, a_renderTargets, a_depthTarget);
				ObserveRenderTargets(a_context, call.Generation(), a_renderTargetCount, a_renderTargets, a_depthTarget);
				ObserveEffectiveRenderTargets(a_context, call.Generation(), TargetBindingSource::kPostCallQuery);
				ObserveAllEffectiveResourceViews(a_context, call.Generation(), ResourceBindingSource::kPostCallQuery);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_renderTargetCount,
				ID3D11RenderTargetView* const* a_renderTargets, ID3D11DepthStencilView* a_depthTarget,
				UINT a_uavStartSlot, UINT a_uavCount, ID3D11UnorderedAccessView* const* a_uavs,
				const UINT* a_initialCounts)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_renderTargetCount, a_renderTargets, a_depthTarget,
					a_uavStartSlot, a_uavCount, a_uavs, a_initialCounts);
				ObserveRenderTargets(
					a_context, call.Generation(), a_renderTargetCount, a_renderTargets, a_depthTarget,
					a_renderTargetCount == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL);
				ObserveUnorderedAccessViews(
					a_context, call.Generation(), ResourceStage::kOutputMerger, a_uavStartSlot, a_uavCount, a_uavs,
					a_uavCount == D3D11_KEEP_UNORDERED_ACCESS_VIEWS);
				ObserveEffectiveRenderTargets(a_context, call.Generation(), TargetBindingSource::kPostCallQuery);
				ObserveAllEffectiveResourceViews(a_context, call.Generation(), ResourceBindingSource::kPostCallQuery);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

#define CSX_RESOURCE_BIND_HOOK(Name, Stage)                                                                            \
	struct Name                                                                                                        \
	{                                                                                                                  \
		static void thunk(ID3D11DeviceContext* a_context, UINT a_startSlot, UINT a_viewCount,                          \
			ID3D11ShaderResourceView* const* a_views)                                                                  \
		{                                                                                                              \
			const auto call = GetRuntime().BeginContextCall();                                                         \
			call.InvokeNative(func, a_context, a_startSlot, a_viewCount, a_views);                                     \
			ObserveShaderResources(a_context, call.Generation(), Stage, a_startSlot, a_viewCount, a_views);            \
			ObserveEffectiveShaderResources(                                                                           \
				a_context, call.Generation(), Stage, a_startSlot, a_viewCount, ResourceBindingSource::kPostCallQuery); \
		}                                                                                                              \
		static inline ContextOriginal<decltype(thunk)> func;                                                           \
	}

		CSX_RESOURCE_BIND_HOOK(ID3D11DeviceContext_VSSetShaderResources, ResourceStage::kVertex);
		CSX_RESOURCE_BIND_HOOK(ID3D11DeviceContext_HSSetShaderResources, ResourceStage::kHull);
		CSX_RESOURCE_BIND_HOOK(ID3D11DeviceContext_DSSetShaderResources, ResourceStage::kDomain);
		CSX_RESOURCE_BIND_HOOK(ID3D11DeviceContext_GSSetShaderResources, ResourceStage::kGeometry);
		CSX_RESOURCE_BIND_HOOK(ID3D11DeviceContext_PSSetShaderResources, ResourceStage::kPixel);
		CSX_RESOURCE_BIND_HOOK(ID3D11DeviceContext_CSSetShaderResources, ResourceStage::kCompute);

#undef CSX_RESOURCE_BIND_HOOK

		struct ID3D11DeviceContext_CSSetUnorderedAccessViews
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_startSlot, UINT a_viewCount,
				ID3D11UnorderedAccessView* const* a_views, const UINT* a_initialCounts)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_startSlot, a_viewCount, a_views, a_initialCounts);
				ObserveUnorderedAccessViews(
					a_context, call.Generation(), ResourceStage::kCompute, a_startSlot, a_viewCount, a_views);
				ObserveAllEffectiveResourceViews(a_context, call.Generation(), ResourceBindingSource::kPostCallQuery);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_PSSetShader
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_shader,
				ID3D11ClassInstance* const* a_classInstances, UINT a_classInstanceCount)
			{
				const auto call = GetRuntime().BeginContextCall();
				RegisterDeferredContextIfNeeded(a_context, call.Generation());
				call.InvokeNative(func, a_context, a_shader, a_classInstances, a_classInstanceCount);
				GetRuntime().ForCapture(call.Generation()).BindStage(reinterpret_cast<std::uintptr_t>(a_context), ShaderStage::kPixel, reinterpret_cast<std::uintptr_t>(a_shader));
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_VSSetShader
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11VertexShader* a_shader,
				ID3D11ClassInstance* const* a_classInstances, UINT a_classInstanceCount)
			{
				const auto call = GetRuntime().BeginContextCall();
				RegisterDeferredContextIfNeeded(a_context, call.Generation());
				call.InvokeNative(func, a_context, a_shader, a_classInstances, a_classInstanceCount);
				GetRuntime().ForCapture(call.Generation()).BindStage(reinterpret_cast<std::uintptr_t>(a_context), ShaderStage::kVertex, reinterpret_cast<std::uintptr_t>(a_shader));
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_CSSetShader
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11ComputeShader* a_shader,
				ID3D11ClassInstance* const* a_classInstances, UINT a_classInstanceCount)
			{
				const auto call = GetRuntime().BeginContextCall();
				RegisterDeferredContextIfNeeded(a_context, call.Generation());
				call.InvokeNative(func, a_context, a_shader, a_classInstances, a_classInstanceCount);
				GetRuntime().ForCapture(call.Generation()).BindStage(reinterpret_cast<std::uintptr_t>(a_context), ShaderStage::kCompute, reinterpret_cast<std::uintptr_t>(a_shader));
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawIndexedInstanced
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_indexCountPerInstance,
				UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation,
				UINT a_startInstanceLocation)
			{
				const auto call = GetRuntime().BeginContextCall();
				RecordDrawWithEffectiveState(a_context, call.Generation(),
					DrawOperation::kDrawIndexedInstanced, a_indexCountPerInstance, a_instanceCount,
					a_startIndexLocation, PackSignedAndUnsigned(a_baseVertexLocation, a_startInstanceLocation));
				call.InvokeNative(func, a_context, a_indexCountPerInstance, a_instanceCount, a_startIndexLocation,
					a_baseVertexLocation, a_startInstanceLocation);
				ObservePostProcessingState(a_context, false, true, call.Generation());
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawIndexed
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_indexCount,
				UINT a_startIndexLocation, INT a_baseVertexLocation)
			{
				const auto call = GetRuntime().BeginContextCall();
				RecordDrawWithEffectiveState(a_context, call.Generation(),
					DrawOperation::kDrawIndexed, a_indexCount, a_startIndexLocation,
					static_cast<std::uint32_t>(a_baseVertexLocation));
				call.InvokeNative(func, a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				ObservePostProcessingState(a_context, false, true, call.Generation());
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_Draw
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_vertexCount, UINT a_startVertexLocation)
			{
				const auto call = GetRuntime().BeginContextCall();
				RecordDrawWithEffectiveState(a_context, call.Generation(),
					DrawOperation::kDraw, a_vertexCount, a_startVertexLocation);
				call.InvokeNative(func, a_context, a_vertexCount, a_startVertexLocation);
				ObservePostProcessingState(a_context, false, true, call.Generation());
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawInstanced
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_vertexCountPerInstance,
				UINT a_instanceCount, UINT a_startVertexLocation, UINT a_startInstanceLocation)
			{
				const auto call = GetRuntime().BeginContextCall();
				RecordDrawWithEffectiveState(a_context, call.Generation(),
					DrawOperation::kDrawInstanced, a_vertexCountPerInstance, a_instanceCount,
					a_startVertexLocation, a_startInstanceLocation);
				call.InvokeNative(func, a_context, a_vertexCountPerInstance, a_instanceCount,
					a_startVertexLocation, a_startInstanceLocation);
				ObservePostProcessingState(a_context, false, true, call.Generation());
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawAuto
		{
			static void thunk(ID3D11DeviceContext* a_context)
			{
				const auto call = GetRuntime().BeginContextCall();
				RecordDrawWithEffectiveState(a_context, call.Generation(), DrawOperation::kDrawAuto);
				call.InvokeNative(func, a_context);
				ObservePostProcessingState(a_context, false, true, call.Generation());
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawIndexedInstancedIndirect
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_argumentBuffer,
				UINT a_alignedByteOffset)
			{
				const auto call = GetRuntime().BeginContextCall();
				RecordDrawWithEffectiveState(a_context, call.Generation(),
					DrawOperation::kDrawIndexedInstancedIndirect,
					reinterpret_cast<std::uintptr_t>(a_argumentBuffer), a_alignedByteOffset);
				call.InvokeNative(func, a_context, a_argumentBuffer, a_alignedByteOffset);
				ObservePostProcessingState(a_context, false, true, call.Generation());
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawInstancedIndirect
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_argumentBuffer,
				UINT a_alignedByteOffset)
			{
				const auto call = GetRuntime().BeginContextCall();
				RecordDrawWithEffectiveState(a_context, call.Generation(),
					DrawOperation::kDrawInstancedIndirect,
					reinterpret_cast<std::uintptr_t>(a_argumentBuffer), a_alignedByteOffset);
				call.InvokeNative(func, a_context, a_argumentBuffer, a_alignedByteOffset);
				ObservePostProcessingState(a_context, false, true, call.Generation());
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_Dispatch
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_threadGroupCountX,
				UINT a_threadGroupCountY, UINT a_threadGroupCountZ)
			{
				const auto call = GetRuntime().BeginContextCall();
				RecordDispatchWithEffectiveState(a_context, call.Generation(),
					DispatchOperation::kDispatch, a_threadGroupCountX, a_threadGroupCountY,
					a_threadGroupCountZ);
				call.InvokeNative(func, a_context, a_threadGroupCountX, a_threadGroupCountY, a_threadGroupCountZ);
				ObservePostProcessingState(a_context, true, true, call.Generation());
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DispatchIndirect
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_argumentBuffer,
				UINT a_alignedByteOffset)
			{
				const auto call = GetRuntime().BeginContextCall();
				RecordDispatchWithEffectiveState(a_context, call.Generation(),
					DispatchOperation::kDispatchIndirect,
					reinterpret_cast<std::uintptr_t>(a_argumentBuffer), a_alignedByteOffset);
				call.InvokeNative(func, a_context, a_argumentBuffer, a_alignedByteOffset);
				ObservePostProcessingState(a_context, true, true, call.Generation());
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ExecuteCommandList
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11CommandList* a_commandList,
				BOOL a_restoreContextState)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_commandList, a_restoreContextState);
				GetRuntime().ForCapture(call.Generation()).RecordExecuteCommandList(reinterpret_cast<std::uintptr_t>(a_context), reinterpret_cast<std::uintptr_t>(a_commandList), a_restoreContextState != FALSE);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_FinishCommandList
		{
			static HRESULT thunk(ID3D11DeviceContext* a_context,
				BOOL a_restoreDeferredContextState, ID3D11CommandList** a_commandList)
			{
				const auto call = GetRuntime().BeginContextCall();
				RegisterDeferredContextIfNeeded(a_context, call.Generation());
				const auto result = call.InvokeNative(func, a_context, a_restoreDeferredContextState, a_commandList);
				GetRuntime().ForCapture(call.Generation()).RecordFinishCommandList(reinterpret_cast<std::uintptr_t>(a_context), reinterpret_cast<std::uintptr_t>(SUCCEEDED(result) && a_commandList ? *a_commandList : nullptr), a_restoreDeferredContextState != FALSE, static_cast<std::int32_t>(result));
				return result;
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_CopySubresourceRegion
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_destination,
				UINT a_destinationSubresource, UINT a_destinationX, UINT a_destinationY, UINT a_destinationZ,
				ID3D11Resource* a_source, UINT a_sourceSubresource, const D3D11_BOX* a_sourceBox)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_destination, a_destinationSubresource, a_destinationX, a_destinationY,
					a_destinationZ, a_source, a_sourceSubresource, a_sourceBox);
				if (!CaptureCurrent(call.Generation()))
					return;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kCopySubresourceRegion, DescribeCapturedResource(a_source, call.Generation()), DescribeCapturedResource(a_destination, call.Generation()), a_sourceSubresource, a_destinationSubresource);
				if (!CaptureCurrent(call.Generation()))
					return;
				const D3D11_BOX box = a_sourceBox ? *a_sourceBox : D3D11_BOX{};
				GetRuntime().ForCapture(call.Generation()).RecordTransferCopyRegion(reinterpret_cast<std::uintptr_t>(a_context), a_sourceSubresource, a_destinationSubresource, { a_destinationX, a_destinationY, a_destinationZ }, { box.left, box.top, box.front, box.right, box.bottom, box.back }, a_sourceBox != nullptr);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_Map
		{
			static HRESULT thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_resource,
				UINT a_subresource, D3D11_MAP a_mapType, UINT a_mapFlags,
				D3D11_MAPPED_SUBRESOURCE* a_mappedResource)
			{
				const auto call = GetRuntime().BeginContextCall();
				const auto generation = call.Generation();
				const auto started = generation != 0 ? ReadQpc() : 0;
				const auto result = call.InvokeNative(func,
					a_context, a_resource, a_subresource, a_mapType, a_mapFlags, a_mappedResource);
				if (CaptureCurrent(generation)) {
					const auto completed = ReadQpc();
					GetRuntime().ForCapture(call.Generation()).RecordCpuMap(reinterpret_cast<std::uintptr_t>(a_context), DescribeCapturedResource(a_resource, call.Generation()), a_subresource, static_cast<std::uint32_t>(a_mapType), a_mapFlags, static_cast<std::int32_t>(result), completed >= started ? completed - started : 0, completed, SUCCEEDED(result) && a_mappedResource ? a_mappedResource->RowPitch : 0, SUCCEEDED(result) && a_mappedResource ? a_mappedResource->DepthPitch : 0, generation);
				}
				return result;
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_Unmap
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_resource,
				UINT a_subresource)
			{
				const auto call = GetRuntime().BeginContextCall();
				const auto generation = call.Generation();
				call.InvokeNative(func, a_context, a_resource, a_subresource);
				if (CaptureCurrent(generation)) {
					GetRuntime().ForCapture(call.Generation()).RecordCpuUnmap(reinterpret_cast<std::uintptr_t>(a_context), DescribeCapturedResource(a_resource, call.Generation()), a_subresource, ReadQpc(), generation);
				}
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_CopyResource
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_destination, ID3D11Resource* a_source)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_destination, a_source);
				if (!CaptureCurrent(call.Generation()))
					return;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kCopyResource, DescribeCapturedResource(a_source, call.Generation()), DescribeCapturedResource(a_destination, call.Generation()));
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ResolveSubresource
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_destination,
				UINT a_destinationSubresource, ID3D11Resource* a_source, UINT a_sourceSubresource, DXGI_FORMAT a_format)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_destination, a_destinationSubresource, a_source, a_sourceSubresource, a_format);
				if (!CaptureCurrent(call.Generation()))
					return;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kResolveSubresource, DescribeCapturedResource(a_source, call.Generation()), DescribeCapturedResource(a_destination, call.Generation()), a_sourceSubresource, a_destinationSubresource);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_UpdateSubresource
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_destination,
				UINT a_destinationSubresource, const D3D11_BOX* a_destinationBox,
				const void* a_sourceData, UINT a_sourceRowPitch, UINT a_sourceDepthPitch)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_destination, a_destinationSubresource, a_destinationBox,
					a_sourceData, a_sourceRowPitch, a_sourceDepthPitch);
				if (!CaptureCurrent(call.Generation()))
					return;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kUpdateSubresource, ResourceObservationInput{}, DescribeCapturedResource(a_destination, call.Generation()), 0, a_destinationSubresource);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_CopyStructureCount
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_destination,
				UINT a_destinationAlignedByteOffset, ID3D11UnorderedAccessView* a_source)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_destination, a_destinationAlignedByteOffset, a_source);
				if (!CaptureCurrent(call.Generation()))
					return;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kCopyStructureCount, DescribeView(a_source, call.Generation()).resource, DescribeCapturedResource(a_destination, call.Generation()));
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ClearRenderTargetView
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11RenderTargetView* a_target,
				const FLOAT a_colour[4])
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_target, a_colour);
				if (!CaptureCurrent(call.Generation()))
					return;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kClearRenderTarget, ResourceObservationInput{}, DescribeView(a_target, call.Generation()).resource);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ClearUnorderedAccessViewUint
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11UnorderedAccessView* a_target,
				const UINT a_values[4])
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_target, a_values);
				if (!CaptureCurrent(call.Generation()))
					return;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kClearUnorderedAccess, ResourceObservationInput{}, DescribeView(a_target, call.Generation()).resource);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ClearUnorderedAccessViewFloat
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11UnorderedAccessView* a_target,
				const FLOAT a_values[4])
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_target, a_values);
				if (!CaptureCurrent(call.Generation()))
					return;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kClearUnorderedAccess, ResourceObservationInput{}, DescribeView(a_target, call.Generation()).resource);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ClearDepthStencilView
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11DepthStencilView* a_target,
				UINT a_clearFlags, FLOAT a_depth, UINT8 a_stencil)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_target, a_clearFlags, a_depth, a_stencil);
				if (!CaptureCurrent(call.Generation()))
					return;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kClearDepthStencil, ResourceObservationInput{}, DescribeView(a_target, call.Generation()).resource);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_GenerateMips
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_view)
			{
				const auto call = GetRuntime().BeginContextCall();
				call.InvokeNative(func, a_context, a_view);
				if (!CaptureCurrent(call.Generation()))
					return;
				const auto resource = DescribeView(a_view, call.Generation()).resource;
				GetRuntime().ForCapture(call.Generation()).RecordResourceFlow(reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kGenerateMips, resource, resource);
			}
			static inline ContextOriginal<decltype(thunk)> func;
		};
	}

	bool CapturePostProcessingBootstrap(ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context || a_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
			!GetRuntime().IsInsidePostProcessing() || !GetRuntime().IsCapturing())
			return false;
		try {
			const auto generation = GetRuntime().PostProcessingCaptureGeneration();
			if (generation == 0)
				return false;
			ObserveEffectiveStateBeforeDraw(a_context, generation);
			BeginPostProcessingOperation(a_context, false, 0, generation, true);
			ObservePostProcessingState(a_context, false, false, generation);
			ObservePostProcessingState(a_context, true, false, generation);
			return GetRuntime().IsInsidePostProcessing();
		} catch (...) {
			return false;
		}
	}

#ifndef CSX_RENDER_MAP_CONTEXT_TESTING
	void InstallD3DContextHooks(ID3D11DeviceContext* a_context)
	{
		if (!a_context)
			return;
		GetRuntime().SetImmediateContext(reinterpret_cast<std::uintptr_t>(a_context));
		stl::detour_vfunc<8, ID3D11DeviceContext_PSSetShaderResources>(a_context);
		stl::detour_vfunc<9, ID3D11DeviceContext_PSSetShader>(a_context);
		stl::detour_vfunc<11, ID3D11DeviceContext_VSSetShader>(a_context);
		stl::detour_vfunc<12, ID3D11DeviceContext_DrawIndexed>(a_context);
		stl::detour_vfunc<13, ID3D11DeviceContext_Draw>(a_context);
		stl::detour_vfunc<14, ID3D11DeviceContext_Map>(a_context);
		stl::detour_vfunc<15, ID3D11DeviceContext_Unmap>(a_context);
		stl::detour_vfunc<20, ID3D11DeviceContext_DrawIndexedInstanced>(a_context);
		stl::detour_vfunc<21, ID3D11DeviceContext_DrawInstanced>(a_context);
		stl::detour_vfunc<25, ID3D11DeviceContext_VSSetShaderResources>(a_context);
		stl::detour_vfunc<31, ID3D11DeviceContext_GSSetShaderResources>(a_context);
		stl::detour_vfunc<33, ID3D11DeviceContext_OMSetRenderTargets>(a_context);
		stl::detour_vfunc<34, ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews>(a_context);
		stl::detour_vfunc<38, ID3D11DeviceContext_DrawAuto>(a_context);
		stl::detour_vfunc<39, ID3D11DeviceContext_DrawIndexedInstancedIndirect>(a_context);
		stl::detour_vfunc<40, ID3D11DeviceContext_DrawInstancedIndirect>(a_context);
		stl::detour_vfunc<41, ID3D11DeviceContext_Dispatch>(a_context);
		stl::detour_vfunc<42, ID3D11DeviceContext_DispatchIndirect>(a_context);
		stl::detour_vfunc<46, ID3D11DeviceContext_CopySubresourceRegion>(a_context);
		stl::detour_vfunc<47, ID3D11DeviceContext_CopyResource>(a_context);
		stl::detour_vfunc<48, ID3D11DeviceContext_UpdateSubresource>(a_context);
		stl::detour_vfunc<49, ID3D11DeviceContext_CopyStructureCount>(a_context);
		stl::detour_vfunc<50, ID3D11DeviceContext_ClearRenderTargetView>(a_context);
		stl::detour_vfunc<51, ID3D11DeviceContext_ClearUnorderedAccessViewUint>(a_context);
		stl::detour_vfunc<52, ID3D11DeviceContext_ClearUnorderedAccessViewFloat>(a_context);
		stl::detour_vfunc<53, ID3D11DeviceContext_ClearDepthStencilView>(a_context);
		stl::detour_vfunc<54, ID3D11DeviceContext_GenerateMips>(a_context);
		stl::detour_vfunc<57, ID3D11DeviceContext_ResolveSubresource>(a_context);
		stl::detour_vfunc<58, ID3D11DeviceContext_ExecuteCommandList>(a_context);
		stl::detour_vfunc<59, ID3D11DeviceContext_HSSetShaderResources>(a_context);
		stl::detour_vfunc<63, ID3D11DeviceContext_DSSetShaderResources>(a_context);
		stl::detour_vfunc<67, ID3D11DeviceContext_CSSetShaderResources>(a_context);
		stl::detour_vfunc<68, ID3D11DeviceContext_CSSetUnorderedAccessViews>(a_context);
		stl::detour_vfunc<69, ID3D11DeviceContext_CSSetShader>(a_context);
		stl::detour_vfunc<114, ID3D11DeviceContext_FinishCommandList>(a_context);
	}
#endif

}

#ifdef CSX_RENDER_MAP_CONTEXT_TESTING
#	include "render_map_context_hook_test.h"
#	include "render_map_context_acquisition_test.h"
#endif
