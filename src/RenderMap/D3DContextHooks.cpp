#include "RenderMap/D3DContextHooks.h"

#include "RenderMap/Runtime.h"

#include <d3d11.h>

#include <algorithm>
#include <array>

namespace CSX::RenderMap
{
	ResourceObservationInput DescribeResource(ID3D11Resource* a_resource) noexcept
	{
		ResourceObservationInput result;
		if (!a_resource)
			return result;
		result.d3dObject = reinterpret_cast<std::uintptr_t>(a_resource);
		D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
		a_resource->GetType(&dimension);
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

	namespace
	{
		void RegisterDeferredContextIfNeeded(ID3D11DeviceContext* a_context) noexcept
		{
			if (a_context && a_context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED) {
				GetRuntime().RegisterDeferredContext(
					reinterpret_cast<std::uintptr_t>(a_context),
					a_context->GetContextFlags(),
					false);
			}
		}

		std::uint64_t ReadQpc() noexcept
		{
			LARGE_INTEGER value{};
			return ::QueryPerformanceCounter(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 0;
		}

		template <class T>
		ResourceViewInput DescribeViewBase(T* a_view, TargetViewKind a_kind)
		{
			ResourceViewInput result;
			result.view.kind = a_kind;
			result.view.d3dObject = reinterpret_cast<std::uintptr_t>(a_view);
			if (!a_view)
				return result;
			ID3D11Resource* resource = nullptr;
			a_view->GetResource(&resource);
			result.resource = DescribeResource(resource);
			if (resource)
				resource->Release();
			return result;
		}

		ResourceViewInput DescribeView(ID3D11RenderTargetView* a_view)
		{
			auto result = DescribeViewBase(a_view, TargetViewKind::kRenderTarget);
			if (!a_view)
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

		ResourceViewInput DescribeView(ID3D11DepthStencilView* a_view)
		{
			auto result = DescribeViewBase(a_view, TargetViewKind::kDepthTarget);
			if (!a_view)
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

		ResourceViewInput DescribeView(ID3D11ShaderResourceView* a_view)
		{
			auto result = DescribeViewBase(a_view, TargetViewKind::kShaderResource);
			if (!a_view)
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

		ResourceViewInput DescribeView(ID3D11UnorderedAccessView* a_view)
		{
			auto result = DescribeViewBase(a_view, TargetViewKind::kUnorderedAccess);
			if (!a_view)
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
			ID3D11DeviceContext* a_context,
			UINT a_renderTargetCount,
			ID3D11RenderTargetView* const* a_renderTargets,
			ID3D11DepthStencilView* a_depthTarget,
			bool a_keepTargets = false,
			TargetBindingSource a_source = TargetBindingSource::kObservedCall,
			std::uint64_t a_expectedCaptureGeneration = 0)
		{
			if (!GetRuntime().IsCapturing())
				return;
			std::array<ResourceViewInput, kMaximumRenderTargets> views{};
			if (!a_keepTargets && a_renderTargets) {
				const auto count = std::min<UINT>(a_renderTargetCount, static_cast<UINT>(views.size()));
				for (UINT index = 0; index < count; ++index)
					views[index] = DescribeView(a_renderTargets[index]);
			}
			const auto depth = DescribeView(a_depthTarget);
			GetRuntime().BindRenderTargetViews(
				reinterpret_cast<std::uintptr_t>(a_context),
				a_keepTargets ? 0u : a_renderTargetCount,
				views.data(),
				a_depthTarget ? &depth : nullptr,
				a_keepTargets,
				a_source,
				a_expectedCaptureGeneration);
		}

		void ObserveEffectiveRenderTargets(
			ID3D11DeviceContext* a_context,
			TargetBindingSource a_source,
			std::uint64_t a_expectedCaptureGeneration = 0)
		{
			if (!a_context || !GetRuntime().IsCapturing())
				return;

			std::array<ID3D11RenderTargetView*, kMaximumRenderTargets> targets{};
			ID3D11DepthStencilView* depth = nullptr;
			a_context->OMGetRenderTargets(
				static_cast<UINT>(targets.size()), targets.data(), &depth);
			std::array<ResourceViewInput, kMaximumRenderTargets> views{};
			std::uint32_t targetCount = 0;
			for (std::size_t index = 0; index < targets.size(); ++index) {
				views[index] = DescribeView(targets[index]);
				if (targets[index])
					targetCount = static_cast<std::uint32_t>(index + 1);
			}
			const auto depthView = DescribeView(depth);
			GetRuntime().BindRenderTargetViews(
				reinterpret_cast<std::uintptr_t>(a_context), targetCount, views.data(),
				depth ? &depthView : nullptr, false,
				a_source, a_expectedCaptureGeneration);
			for (auto* target : targets) {
				if (target)
					target->Release();
			}
			if (depth)
				depth->Release();
			if (a_source == TargetBindingSource::kPostCallQuery)
				GetRuntime().ClaimRenderTargetStateSeed(reinterpret_cast<std::uintptr_t>(a_context));
		}

		void ObserveEffectiveStateBeforeDraw(ID3D11DeviceContext* a_context);

		template <class... Args>
		void RecordDrawWithEffectiveState(
			ID3D11DeviceContext* a_context, DrawOperation a_operation, Args... a_arguments)
		{
			RegisterDeferredContextIfNeeded(a_context);
			if (a_context && a_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE)
				ObserveEffectiveStateBeforeDraw(a_context);
			GetRuntime().RecordDraw(
				reinterpret_cast<std::uintptr_t>(a_context), a_operation, a_arguments...);
		}

		void ObserveShaderResources(
			ID3D11DeviceContext* a_context,
			ResourceStage a_stage,
			UINT a_startSlot,
			UINT a_viewCount,
			ID3D11ShaderResourceView* const* a_views,
			ResourceBindingSource a_source = ResourceBindingSource::kRequestedCall,
			std::uint64_t a_expectedCaptureGeneration = 0)
		{
			if (!GetRuntime().IsCapturing())
				return;
			std::array<ResourceViewInput, kMaximumShaderResourceSlots> views{};
			const auto count = std::min<UINT>(a_viewCount, static_cast<UINT>(views.size()));
			for (UINT index = 0; index < count; ++index)
				views[index] = DescribeView(a_views ? a_views[index] : nullptr);
			GetRuntime().BindResourceViews(
				reinterpret_cast<std::uintptr_t>(a_context), ResourceBindingKind::kShaderResource,
				a_stage, a_startSlot, count, views.data(), false, a_source,
				a_expectedCaptureGeneration);
		}

		void ObserveEffectiveShaderResources(
			ID3D11DeviceContext* a_context,
			ResourceStage a_stage,
			UINT a_startSlot,
			UINT a_viewCount,
			ResourceBindingSource a_source,
			std::uint64_t a_expectedCaptureGeneration = 0)
		{
			if (!a_context || !GetRuntime().IsCapturing() ||
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
				a_context, a_stage, a_startSlot, count, views.data(), a_source,
				a_expectedCaptureGeneration);
			for (UINT index = 0; index < count; ++index) {
				if (views[index])
					views[index]->Release();
			}
		}

		void ObserveAllEffectiveShaderResources(
			ID3D11DeviceContext* a_context,
			ResourceBindingSource a_source,
			std::uint64_t a_expectedCaptureGeneration = 0)
		{
			for (const auto stage : std::array{
					 ResourceStage::kVertex, ResourceStage::kHull, ResourceStage::kDomain,
					 ResourceStage::kGeometry, ResourceStage::kPixel, ResourceStage::kCompute }) {
				ObserveEffectiveShaderResources(
					a_context, stage, 0, static_cast<UINT>(kMaximumShaderResourceSlots),
					a_source, a_expectedCaptureGeneration);
			}
		}

		void ObserveUnorderedAccessViews(
			ID3D11DeviceContext* a_context,
			ResourceStage a_stage,
			UINT a_startSlot,
			UINT a_viewCount,
			ID3D11UnorderedAccessView* const* a_views,
			bool a_keepViews = false)
		{
			if (!GetRuntime().IsCapturing())
				return;
			std::array<ResourceViewInput, kMaximumUnorderedAccessSlots> views{};
			if (!a_keepViews) {
				const auto count = std::min<UINT>(a_viewCount, static_cast<UINT>(views.size()));
				for (UINT index = 0; index < count; ++index)
					views[index] = DescribeView(a_views ? a_views[index] : nullptr);
			}
			GetRuntime().BindResourceViews(
				reinterpret_cast<std::uintptr_t>(a_context), ResourceBindingKind::kUnorderedAccess,
				a_stage, a_startSlot, a_viewCount, views.data(), a_keepViews);
		}

		void ObserveEffectiveUnorderedAccessViews(
			ID3D11DeviceContext* a_context,
			ResourceStage a_stage,
			UINT a_startSlot,
			UINT a_viewCount,
			ResourceBindingSource a_source,
			std::uint64_t a_expectedCaptureGeneration = 0)
		{
			if (!a_context || !GetRuntime().IsCapturing() ||
				a_startSlot >= kMaximumUnorderedAccessSlots) {
				return;
			}
			const auto count = std::min<UINT>(
				a_viewCount, static_cast<UINT>(kMaximumUnorderedAccessSlots - a_startSlot));
			std::array<ID3D11UnorderedAccessView*, kMaximumUnorderedAccessSlots> views{};
			if (a_stage == ResourceStage::kCompute) {
				a_context->CSGetUnorderedAccessViews(a_startSlot, count, views.data());
			} else if (a_stage == ResourceStage::kOutputMerger) {
				a_context->OMGetRenderTargetsAndUnorderedAccessViews(
					0, nullptr, nullptr, a_startSlot, count, views.data());
			} else {
				return;
			}
			std::array<ResourceViewInput, kMaximumUnorderedAccessSlots> described{};
			for (UINT index = 0; index < count; ++index)
				described[index] = DescribeView(views[index]);
			GetRuntime().BindResourceViews(
				reinterpret_cast<std::uintptr_t>(a_context), ResourceBindingKind::kUnorderedAccess,
				a_stage, a_startSlot, count, described.data(), false, a_source,
				a_expectedCaptureGeneration);
			for (UINT index = 0; index < count; ++index) {
				if (views[index])
					views[index]->Release();
			}
		}

		void ObserveEffectiveStateBeforeDraw(ID3D11DeviceContext* a_context)
		{
			if (!a_context)
				return;
			auto& runtime = GetRuntime();
			const auto context = reinterpret_cast<std::uintptr_t>(a_context);
			const auto targetGeneration = runtime.ClaimRenderTargetStateSeed(context);
			if (targetGeneration != 0) {
				ObserveEffectiveRenderTargets(
					a_context, TargetBindingSource::kCaptureStateSnapshot, targetGeneration);
			}
			const auto resourceGeneration = runtime.ClaimResourceViewStateSeed(context);
			if (resourceGeneration != 0) {
				ObserveAllEffectiveShaderResources(
					a_context, ResourceBindingSource::kCaptureStateSnapshot, resourceGeneration);
				ObserveEffectiveUnorderedAccessViews(
					a_context, ResourceStage::kCompute, 0,
					static_cast<UINT>(kMaximumUnorderedAccessSlots),
					ResourceBindingSource::kCaptureStateSnapshot, resourceGeneration);
				ObserveEffectiveUnorderedAccessViews(
					a_context, ResourceStage::kOutputMerger, 0,
					D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
					ResourceBindingSource::kCaptureStateSnapshot, resourceGeneration);
			}
		}

		void ObserveAllEffectiveResourceViews(
			ID3D11DeviceContext* a_context,
			ResourceBindingSource a_source,
			std::uint64_t a_expectedCaptureGeneration = 0)
		{
			ObserveAllEffectiveShaderResources(a_context, a_source, a_expectedCaptureGeneration);
			ObserveEffectiveUnorderedAccessViews(
				a_context, ResourceStage::kCompute, 0,
				static_cast<UINT>(kMaximumUnorderedAccessSlots),
				a_source, a_expectedCaptureGeneration);
			ObserveEffectiveUnorderedAccessViews(
				a_context, ResourceStage::kOutputMerger, 0,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
				a_source, a_expectedCaptureGeneration);
			if (a_source == ResourceBindingSource::kPostCallQuery)
				GetRuntime().ClaimResourceViewStateSeed(reinterpret_cast<std::uintptr_t>(a_context));
		}

		struct ID3D11DeviceContext_OMSetRenderTargets
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_renderTargetCount,
				ID3D11RenderTargetView* const* a_renderTargets, ID3D11DepthStencilView* a_depthTarget)
			{
				func(a_context, a_renderTargetCount, a_renderTargets, a_depthTarget);
				ObserveRenderTargets(a_context, a_renderTargetCount, a_renderTargets, a_depthTarget);
				ObserveEffectiveRenderTargets(a_context, TargetBindingSource::kPostCallQuery);
				ObserveAllEffectiveResourceViews(a_context, ResourceBindingSource::kPostCallQuery);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_renderTargetCount,
				ID3D11RenderTargetView* const* a_renderTargets, ID3D11DepthStencilView* a_depthTarget,
				UINT a_uavStartSlot, UINT a_uavCount, ID3D11UnorderedAccessView* const* a_uavs,
				const UINT* a_initialCounts)
			{
				func(a_context, a_renderTargetCount, a_renderTargets, a_depthTarget,
					a_uavStartSlot, a_uavCount, a_uavs, a_initialCounts);
				ObserveRenderTargets(
					a_context, a_renderTargetCount, a_renderTargets, a_depthTarget,
					a_renderTargetCount == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL);
				ObserveUnorderedAccessViews(
					a_context, ResourceStage::kOutputMerger, a_uavStartSlot, a_uavCount, a_uavs,
					a_uavCount == D3D11_KEEP_UNORDERED_ACCESS_VIEWS);
				ObserveEffectiveRenderTargets(a_context, TargetBindingSource::kPostCallQuery);
				ObserveAllEffectiveResourceViews(a_context, ResourceBindingSource::kPostCallQuery);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

#define CSX_RESOURCE_BIND_HOOK(Name, Stage)                                                         \
	struct Name                                                                                     \
	{                                                                                               \
		static void thunk(ID3D11DeviceContext* a_context, UINT a_startSlot, UINT a_viewCount,       \
			ID3D11ShaderResourceView* const* a_views)                                               \
		{                                                                                           \
			func(a_context, a_startSlot, a_viewCount, a_views);                                     \
			ObserveShaderResources(a_context, Stage, a_startSlot, a_viewCount, a_views);            \
			ObserveEffectiveShaderResources(                                                        \
				a_context, Stage, a_startSlot, a_viewCount, ResourceBindingSource::kPostCallQuery); \
		}                                                                                           \
		static inline REL::Relocation<decltype(thunk)> func;                                        \
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
				func(a_context, a_startSlot, a_viewCount, a_views, a_initialCounts);
				ObserveUnorderedAccessViews(
					a_context, ResourceStage::kCompute, a_startSlot, a_viewCount, a_views);
				ObserveAllEffectiveResourceViews(a_context, ResourceBindingSource::kPostCallQuery);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_PSSetShader
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_shader,
				ID3D11ClassInstance* const* a_classInstances, UINT a_classInstanceCount)
			{
				RegisterDeferredContextIfNeeded(a_context);
				func(a_context, a_shader, a_classInstances, a_classInstanceCount);
				GetRuntime().BindStage(reinterpret_cast<std::uintptr_t>(a_context), ShaderStage::kPixel,
					reinterpret_cast<std::uintptr_t>(a_shader));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_VSSetShader
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11VertexShader* a_shader,
				ID3D11ClassInstance* const* a_classInstances, UINT a_classInstanceCount)
			{
				RegisterDeferredContextIfNeeded(a_context);
				func(a_context, a_shader, a_classInstances, a_classInstanceCount);
				GetRuntime().BindStage(reinterpret_cast<std::uintptr_t>(a_context), ShaderStage::kVertex,
					reinterpret_cast<std::uintptr_t>(a_shader));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_CSSetShader
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11ComputeShader* a_shader,
				ID3D11ClassInstance* const* a_classInstances, UINT a_classInstanceCount)
			{
				RegisterDeferredContextIfNeeded(a_context);
				func(a_context, a_shader, a_classInstances, a_classInstanceCount);
				GetRuntime().BindStage(reinterpret_cast<std::uintptr_t>(a_context), ShaderStage::kCompute,
					reinterpret_cast<std::uintptr_t>(a_shader));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawIndexedInstanced
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_indexCountPerInstance,
				UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation,
				UINT a_startInstanceLocation)
			{
				RecordDrawWithEffectiveState(a_context,
					DrawOperation::kDrawIndexedInstanced, a_indexCountPerInstance, a_instanceCount,
					a_startIndexLocation, PackSignedAndUnsigned(a_baseVertexLocation, a_startInstanceLocation));
				func(a_context, a_indexCountPerInstance, a_instanceCount, a_startIndexLocation,
					a_baseVertexLocation, a_startInstanceLocation);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawIndexed
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_indexCount,
				UINT a_startIndexLocation, INT a_baseVertexLocation)
			{
				RecordDrawWithEffectiveState(a_context,
					DrawOperation::kDrawIndexed, a_indexCount, a_startIndexLocation,
					static_cast<std::uint32_t>(a_baseVertexLocation));
				func(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_Draw
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_vertexCount, UINT a_startVertexLocation)
			{
				RecordDrawWithEffectiveState(a_context,
					DrawOperation::kDraw, a_vertexCount, a_startVertexLocation);
				func(a_context, a_vertexCount, a_startVertexLocation);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawInstanced
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_vertexCountPerInstance,
				UINT a_instanceCount, UINT a_startVertexLocation, UINT a_startInstanceLocation)
			{
				RecordDrawWithEffectiveState(a_context,
					DrawOperation::kDrawInstanced, a_vertexCountPerInstance, a_instanceCount,
					a_startVertexLocation, a_startInstanceLocation);
				func(a_context, a_vertexCountPerInstance, a_instanceCount,
					a_startVertexLocation, a_startInstanceLocation);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawAuto
		{
			static void thunk(ID3D11DeviceContext* a_context)
			{
				RecordDrawWithEffectiveState(a_context, DrawOperation::kDrawAuto);
				func(a_context);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawIndexedInstancedIndirect
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_argumentBuffer,
				UINT a_alignedByteOffset)
			{
				RecordDrawWithEffectiveState(a_context,
					DrawOperation::kDrawIndexedInstancedIndirect,
					reinterpret_cast<std::uintptr_t>(a_argumentBuffer), a_alignedByteOffset);
				func(a_context, a_argumentBuffer, a_alignedByteOffset);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawInstancedIndirect
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_argumentBuffer,
				UINT a_alignedByteOffset)
			{
				RecordDrawWithEffectiveState(a_context,
					DrawOperation::kDrawInstancedIndirect,
					reinterpret_cast<std::uintptr_t>(a_argumentBuffer), a_alignedByteOffset);
				func(a_context, a_argumentBuffer, a_alignedByteOffset);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_Dispatch
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_threadGroupCountX,
				UINT a_threadGroupCountY, UINT a_threadGroupCountZ)
			{
				RegisterDeferredContextIfNeeded(a_context);
				GetRuntime().RecordDispatch(reinterpret_cast<std::uintptr_t>(a_context),
					DispatchOperation::kDispatch, a_threadGroupCountX, a_threadGroupCountY,
					a_threadGroupCountZ);
				func(a_context, a_threadGroupCountX, a_threadGroupCountY, a_threadGroupCountZ);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DispatchIndirect
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_argumentBuffer,
				UINT a_alignedByteOffset)
			{
				RegisterDeferredContextIfNeeded(a_context);
				GetRuntime().RecordDispatch(reinterpret_cast<std::uintptr_t>(a_context),
					DispatchOperation::kDispatchIndirect,
					reinterpret_cast<std::uintptr_t>(a_argumentBuffer), a_alignedByteOffset);
				func(a_context, a_argumentBuffer, a_alignedByteOffset);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ExecuteCommandList
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11CommandList* a_commandList,
				BOOL a_restoreContextState)
			{
				func(a_context, a_commandList, a_restoreContextState);
				GetRuntime().RecordExecuteCommandList(
					reinterpret_cast<std::uintptr_t>(a_context),
					reinterpret_cast<std::uintptr_t>(a_commandList),
					a_restoreContextState != FALSE);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_FinishCommandList
		{
			static HRESULT thunk(ID3D11DeviceContext* a_context,
				BOOL a_restoreDeferredContextState, ID3D11CommandList** a_commandList)
			{
				RegisterDeferredContextIfNeeded(a_context);
				const auto result = func(a_context, a_restoreDeferredContextState, a_commandList);
				GetRuntime().RecordFinishCommandList(
					reinterpret_cast<std::uintptr_t>(a_context),
					reinterpret_cast<std::uintptr_t>(SUCCEEDED(result) && a_commandList ? *a_commandList : nullptr),
					a_restoreDeferredContextState != FALSE,
					static_cast<std::int32_t>(result));
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_CopySubresourceRegion
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_destination,
				UINT a_destinationSubresource, UINT a_destinationX, UINT a_destinationY, UINT a_destinationZ,
				ID3D11Resource* a_source, UINT a_sourceSubresource, const D3D11_BOX* a_sourceBox)
			{
				func(a_context, a_destination, a_destinationSubresource, a_destinationX, a_destinationY,
					a_destinationZ, a_source, a_sourceSubresource, a_sourceBox);
				if (!GetRuntime().IsCapturing())
					return;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kCopySubresourceRegion,
					DescribeResource(a_source), DescribeResource(a_destination),
					a_sourceSubresource, a_destinationSubresource);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_Map
		{
			static HRESULT thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_resource,
				UINT a_subresource, D3D11_MAP a_mapType, UINT a_mapFlags,
				D3D11_MAPPED_SUBRESOURCE* a_mappedResource)
			{
				const auto generation = GetRuntime().ActiveCaptureGeneration();
				const auto started = generation != 0 ? ReadQpc() : 0;
				const auto result = func(
					a_context, a_resource, a_subresource, a_mapType, a_mapFlags, a_mappedResource);
				if (generation != 0) {
					const auto completed = ReadQpc();
					GetRuntime().RecordCpuMap(
						reinterpret_cast<std::uintptr_t>(a_context), DescribeResource(a_resource),
						a_subresource, static_cast<std::uint32_t>(a_mapType), a_mapFlags,
						static_cast<std::int32_t>(result),
						completed >= started ? completed - started : 0, completed,
						SUCCEEDED(result) && a_mappedResource ? a_mappedResource->RowPitch : 0,
						SUCCEEDED(result) && a_mappedResource ? a_mappedResource->DepthPitch : 0,
						generation);
				}
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_Unmap
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_resource,
				UINT a_subresource)
			{
				const auto generation = GetRuntime().ActiveCaptureGeneration();
				func(a_context, a_resource, a_subresource);
				if (generation != 0) {
					GetRuntime().RecordCpuUnmap(
						reinterpret_cast<std::uintptr_t>(a_context), DescribeResource(a_resource),
						a_subresource, ReadQpc(), generation);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_CopyResource
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_destination, ID3D11Resource* a_source)
			{
				func(a_context, a_destination, a_source);
				if (!GetRuntime().IsCapturing())
					return;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kCopyResource,
					DescribeResource(a_source), DescribeResource(a_destination));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ResolveSubresource
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_destination,
				UINT a_destinationSubresource, ID3D11Resource* a_source, UINT a_sourceSubresource, DXGI_FORMAT a_format)
			{
				func(a_context, a_destination, a_destinationSubresource, a_source, a_sourceSubresource, a_format);
				if (!GetRuntime().IsCapturing())
					return;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kResolveSubresource,
					DescribeResource(a_source), DescribeResource(a_destination),
					a_sourceSubresource, a_destinationSubresource);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_UpdateSubresource
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Resource* a_destination,
				UINT a_destinationSubresource, const D3D11_BOX* a_destinationBox,
				const void* a_sourceData, UINT a_sourceRowPitch, UINT a_sourceDepthPitch)
			{
				func(a_context, a_destination, a_destinationSubresource, a_destinationBox,
					a_sourceData, a_sourceRowPitch, a_sourceDepthPitch);
				if (!GetRuntime().IsCapturing())
					return;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kUpdateSubresource,
					{}, DescribeResource(a_destination), 0, a_destinationSubresource);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_CopyStructureCount
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_destination,
				UINT a_destinationAlignedByteOffset, ID3D11UnorderedAccessView* a_source)
			{
				func(a_context, a_destination, a_destinationAlignedByteOffset, a_source);
				if (!GetRuntime().IsCapturing())
					return;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kCopyStructureCount,
					DescribeView(a_source).resource, DescribeResource(a_destination));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ClearRenderTargetView
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11RenderTargetView* a_target,
				const FLOAT a_colour[4])
			{
				func(a_context, a_target, a_colour);
				if (!GetRuntime().IsCapturing())
					return;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kClearRenderTarget,
					{}, DescribeView(a_target).resource);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ClearUnorderedAccessViewUint
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11UnorderedAccessView* a_target,
				const UINT a_values[4])
			{
				func(a_context, a_target, a_values);
				if (!GetRuntime().IsCapturing())
					return;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kClearUnorderedAccess,
					{}, DescribeView(a_target).resource);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ClearUnorderedAccessViewFloat
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11UnorderedAccessView* a_target,
				const FLOAT a_values[4])
			{
				func(a_context, a_target, a_values);
				if (!GetRuntime().IsCapturing())
					return;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kClearUnorderedAccess,
					{}, DescribeView(a_target).resource);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_ClearDepthStencilView
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11DepthStencilView* a_target,
				UINT a_clearFlags, FLOAT a_depth, UINT8 a_stencil)
			{
				func(a_context, a_target, a_clearFlags, a_depth, a_stencil);
				if (!GetRuntime().IsCapturing())
					return;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kClearDepthStencil,
					{}, DescribeView(a_target).resource);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_GenerateMips
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_view)
			{
				func(a_context, a_view);
				if (!GetRuntime().IsCapturing())
					return;
				const auto resource = DescribeView(a_view).resource;
				GetRuntime().RecordResourceFlow(
					reinterpret_cast<std::uintptr_t>(a_context), ResourceFlowOperation::kGenerateMips,
					resource, resource);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

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
}
