#pragma once
#include <array>
#include <d3d11.h>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace CSX::RenderMap::ContextHookFixture
{
	inline void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	inline unsigned getterEntries = 0;
	inline void (*getterTurnover)() = nullptr;
	inline void TouchGetter()
	{
		++getterEntries;
		if (auto callback = std::exchange(getterTurnover, nullptr))
			callback();
	}
	template <class Interface>
	struct DeviceChild : Interface
	{
		ULONG references = 1;
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** output) override
		{
			if (output)
				*output = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			Require(references > 1, "fixture COM reference was released without matching acquisition");
			return --references;
		}
		void STDMETHODCALLTYPE GetDevice(ID3D11Device** ppDevice) override
		{
			if (ppDevice)
				*ppDevice = nullptr;
		}
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override
		{
			(void)guid;
			(void)pDataSize;
			(void)pData;
			throw std::logic_error("unexpected COM method GetPrivateData");
		}
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override
		{
			(void)guid;
			(void)DataSize;
			(void)pData;
			throw std::logic_error("unexpected COM method SetPrivateData");
		}
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override
		{
			(void)guid;
			(void)pData;
			throw std::logic_error("unexpected COM method SetPrivateDataInterface");
		}
	};
	struct Buffer : DeviceChild<ID3D11Buffer>
	{
		unsigned descriptions = 0;
		void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION* pResourceDimension) override
		{
			TouchGetter();
			*pResourceDimension = D3D11_RESOURCE_DIMENSION_BUFFER;
		}
		void STDMETHODCALLTYPE SetEvictionPriority(UINT EvictionPriority) override
		{
			(void)EvictionPriority;
		}
		UINT STDMETHODCALLTYPE GetEvictionPriority(void) override
		{
			return 0;
		}
		void STDMETHODCALLTYPE GetDesc(D3D11_BUFFER_DESC* pDesc) override
		{
			TouchGetter();
			++descriptions;
			*pDesc = {};
			pDesc->ByteWidth = 64;
			pDesc->BindFlags = D3D11_BIND_SHADER_RESOURCE;
			pDesc->StructureByteStride = 4;
		}
	};
	struct View : DeviceChild<ID3D11ShaderResourceView>
	{
		Buffer* resource;
		explicit View(Buffer& input) : resource(&input) {}
		void STDMETHODCALLTYPE GetResource(ID3D11Resource** output) override
		{
			TouchGetter();
			*output = resource;
			resource->AddRef();
		}
		void STDMETHODCALLTYPE GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc) override
		{
			*pDesc = {};
			pDesc->Format = DXGI_FORMAT_R32_FLOAT;
			pDesc->ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			pDesc->Buffer.NumElements = 16;
		}
	};
	struct CommandList : DeviceChild<ID3D11CommandList>
	{
		UINT STDMETHODCALLTYPE GetContextFlags() override { return 0; }
	};
	struct Context : DeviceChild<ID3D11DeviceContext>
	{
		D3D11_DEVICE_CONTEXT_TYPE type = D3D11_DEVICE_CONTEXT_IMMEDIATE;
		std::array<ID3D11ShaderResourceView*, 6> resources{};
		unsigned queries = 0;
		ID3D11PixelShader* queriedPixel = nullptr;
		ID3D11ComputeShader* queriedCompute = nullptr;
		void ReadResources(unsigned stage, UINT start, UINT count, ID3D11ShaderResourceView** output)
		{
			TouchGetter();
			++queries;
			for (UINT index = 0; index < count; ++index) {
				output[index] = start + index == 7 ? resources[stage] : nullptr;
				if (output[index])
					output[index]->AddRef();
			}
		}
		void STDMETHODCALLTYPE VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method VSSetConstantBuffers");
		}
		void STDMETHODCALLTYPE PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override
		{
			(void)StartSlot;
			(void)NumViews;
			(void)ppShaderResourceViews;
			throw std::logic_error("unexpected COM method PSSetShaderResources");
		}
		void STDMETHODCALLTYPE PSSetShader(ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override
		{
			(void)pPixelShader;
			(void)ppClassInstances;
			(void)NumClassInstances;
			throw std::logic_error("unexpected COM method PSSetShader");
		}
		void STDMETHODCALLTYPE PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method PSSetSamplers");
		}
		void STDMETHODCALLTYPE VSSetShader(ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override
		{
			(void)pVertexShader;
			(void)ppClassInstances;
			(void)NumClassInstances;
			throw std::logic_error("unexpected COM method VSSetShader");
		}
		void STDMETHODCALLTYPE DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) override
		{
			(void)IndexCount;
			(void)StartIndexLocation;
			(void)BaseVertexLocation;
			throw std::logic_error("unexpected COM method DrawIndexed");
		}
		void STDMETHODCALLTYPE Draw(UINT VertexCount, UINT StartVertexLocation) override
		{
			(void)VertexCount;
			(void)StartVertexLocation;
			throw std::logic_error("unexpected COM method Draw");
		}
		HRESULT STDMETHODCALLTYPE Map(ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource) override
		{
			(void)pResource;
			(void)Subresource;
			(void)MapType;
			(void)MapFlags;
			(void)pMappedResource;
			throw std::logic_error("unexpected COM method Map");
		}
		void STDMETHODCALLTYPE Unmap(ID3D11Resource* pResource, UINT Subresource) override
		{
			(void)pResource;
			(void)Subresource;
			throw std::logic_error("unexpected COM method Unmap");
		}
		void STDMETHODCALLTYPE PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method PSSetConstantBuffers");
		}
		void STDMETHODCALLTYPE IASetInputLayout(ID3D11InputLayout* pInputLayout) override
		{
			(void)pInputLayout;
			throw std::logic_error("unexpected COM method IASetInputLayout");
		}
		void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppVertexBuffers;
			(void)pStrides;
			(void)pOffsets;
			throw std::logic_error("unexpected COM method IASetVertexBuffers");
		}
		void STDMETHODCALLTYPE IASetIndexBuffer(ID3D11Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) override
		{
			(void)pIndexBuffer;
			(void)Format;
			(void)Offset;
			throw std::logic_error("unexpected COM method IASetIndexBuffer");
		}
		void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) override
		{
			(void)IndexCountPerInstance;
			(void)InstanceCount;
			(void)StartIndexLocation;
			(void)BaseVertexLocation;
			(void)StartInstanceLocation;
			throw std::logic_error("unexpected COM method DrawIndexedInstanced");
		}
		void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) override
		{
			(void)VertexCountPerInstance;
			(void)InstanceCount;
			(void)StartVertexLocation;
			(void)StartInstanceLocation;
			throw std::logic_error("unexpected COM method DrawInstanced");
		}
		void STDMETHODCALLTYPE GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method GSSetConstantBuffers");
		}
		void STDMETHODCALLTYPE GSSetShader(ID3D11GeometryShader* pShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override
		{
			(void)pShader;
			(void)ppClassInstances;
			(void)NumClassInstances;
			throw std::logic_error("unexpected COM method GSSetShader");
		}
		void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology) override
		{
			(void)Topology;
			throw std::logic_error("unexpected COM method IASetPrimitiveTopology");
		}
		void STDMETHODCALLTYPE VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override
		{
			(void)StartSlot;
			(void)NumViews;
			(void)ppShaderResourceViews;
			throw std::logic_error("unexpected COM method VSSetShaderResources");
		}
		void STDMETHODCALLTYPE VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method VSSetSamplers");
		}
		void STDMETHODCALLTYPE Begin(ID3D11Asynchronous* pAsync) override
		{
			(void)pAsync;
			throw std::logic_error("unexpected COM method Begin");
		}
		void STDMETHODCALLTYPE End(ID3D11Asynchronous* pAsync) override
		{
			(void)pAsync;
			throw std::logic_error("unexpected COM method End");
		}
		HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous* pAsync, void* pData, UINT DataSize, UINT GetDataFlags) override
		{
			(void)pAsync;
			(void)pData;
			(void)DataSize;
			(void)GetDataFlags;
			throw std::logic_error("unexpected COM method GetData");
		}
		void STDMETHODCALLTYPE SetPredication(ID3D11Predicate* pPredicate, BOOL PredicateValue) override
		{
			(void)pPredicate;
			(void)PredicateValue;
			throw std::logic_error("unexpected COM method SetPredication");
		}
		void STDMETHODCALLTYPE GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override
		{
			(void)StartSlot;
			(void)NumViews;
			(void)ppShaderResourceViews;
			throw std::logic_error("unexpected COM method GSSetShaderResources");
		}
		void STDMETHODCALLTYPE GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method GSSetSamplers");
		}
		void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView) override
		{
			(void)NumViews;
			(void)ppRenderTargetViews;
			(void)pDepthStencilView;
			throw std::logic_error("unexpected COM method OMSetRenderTargets");
		}
		void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts) override
		{
			(void)NumRTVs;
			(void)ppRenderTargetViews;
			(void)pDepthStencilView;
			(void)UAVStartSlot;
			(void)NumUAVs;
			(void)ppUnorderedAccessViews;
			(void)pUAVInitialCounts;
			throw std::logic_error("unexpected COM method OMSetRenderTargetsAndUnorderedAccessViews");
		}
		void STDMETHODCALLTYPE OMSetBlendState(ID3D11BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) override
		{
			(void)pBlendState;
			(void)BlendFactor;
			(void)SampleMask;
			throw std::logic_error("unexpected COM method OMSetBlendState");
		}
		void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DepthStencilState* pDepthStencilState, UINT StencilRef) override
		{
			(void)pDepthStencilState;
			(void)StencilRef;
			throw std::logic_error("unexpected COM method OMSetDepthStencilState");
		}
		void STDMETHODCALLTYPE SOSetTargets(UINT NumBuffers, ID3D11Buffer* const* ppSOTargets, const UINT* pOffsets) override
		{
			(void)NumBuffers;
			(void)ppSOTargets;
			(void)pOffsets;
			throw std::logic_error("unexpected COM method SOSetTargets");
		}
		void STDMETHODCALLTYPE DrawAuto(void) override
		{
			throw std::logic_error("unexpected COM method DrawAuto");
		}
		void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override
		{
			(void)pBufferForArgs;
			(void)AlignedByteOffsetForArgs;
			throw std::logic_error("unexpected COM method DrawIndexedInstancedIndirect");
		}
		void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override
		{
			(void)pBufferForArgs;
			(void)AlignedByteOffsetForArgs;
			throw std::logic_error("unexpected COM method DrawInstancedIndirect");
		}
		void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override
		{
			(void)ThreadGroupCountX;
			(void)ThreadGroupCountY;
			(void)ThreadGroupCountZ;
			throw std::logic_error("unexpected COM method Dispatch");
		}
		void STDMETHODCALLTYPE DispatchIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override
		{
			(void)pBufferForArgs;
			(void)AlignedByteOffsetForArgs;
			throw std::logic_error("unexpected COM method DispatchIndirect");
		}
		void STDMETHODCALLTYPE RSSetState(ID3D11RasterizerState* pRasterizerState) override
		{
			(void)pRasterizerState;
			throw std::logic_error("unexpected COM method RSSetState");
		}
		void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) override
		{
			(void)NumViewports;
			(void)pViewports;
			throw std::logic_error("unexpected COM method RSSetViewports");
		}
		void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects) override
		{
			(void)NumRects;
			(void)pRects;
			throw std::logic_error("unexpected COM method RSSetScissorRects");
		}
		void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource* pSrcResource, UINT SrcSubresource, const D3D11_BOX* pSrcBox) override
		{
			(void)pDstResource;
			(void)DstSubresource;
			(void)DstX;
			(void)DstY;
			(void)DstZ;
			(void)pSrcResource;
			(void)SrcSubresource;
			(void)pSrcBox;
			throw std::logic_error("unexpected COM method CopySubresourceRegion");
		}
		void STDMETHODCALLTYPE CopyResource(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource) override
		{
			(void)pDstResource;
			(void)pSrcResource;
			throw std::logic_error("unexpected COM method CopyResource");
		}
		void STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override
		{
			(void)pDstResource;
			(void)DstSubresource;
			(void)pDstBox;
			(void)pSrcData;
			(void)SrcRowPitch;
			(void)SrcDepthPitch;
			throw std::logic_error("unexpected COM method UpdateSubresource");
		}
		void STDMETHODCALLTYPE CopyStructureCount(ID3D11Buffer* pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView* pSrcView) override
		{
			(void)pDstBuffer;
			(void)DstAlignedByteOffset;
			(void)pSrcView;
			throw std::logic_error("unexpected COM method CopyStructureCount");
		}
		void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]) override
		{
			(void)pRenderTargetView;
			(void)ColorRGBA;
			throw std::logic_error("unexpected COM method ClearRenderTargetView");
		}
		void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* pUnorderedAccessView, const UINT Values[4]) override
		{
			(void)pUnorderedAccessView;
			(void)Values;
			throw std::logic_error("unexpected COM method ClearUnorderedAccessViewUint");
		}
		void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* pUnorderedAccessView, const FLOAT Values[4]) override
		{
			(void)pUnorderedAccessView;
			(void)Values;
			throw std::logic_error("unexpected COM method ClearUnorderedAccessViewFloat");
		}
		void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) override
		{
			(void)pDepthStencilView;
			(void)ClearFlags;
			(void)Depth;
			(void)Stencil;
			throw std::logic_error("unexpected COM method ClearDepthStencilView");
		}
		void STDMETHODCALLTYPE GenerateMips(ID3D11ShaderResourceView* pShaderResourceView) override
		{
			(void)pShaderResourceView;
			throw std::logic_error("unexpected COM method GenerateMips");
		}
		void STDMETHODCALLTYPE SetResourceMinLOD(ID3D11Resource* pResource, FLOAT MinLOD) override
		{
			(void)pResource;
			(void)MinLOD;
			throw std::logic_error("unexpected COM method SetResourceMinLOD");
		}
		FLOAT STDMETHODCALLTYPE GetResourceMinLOD(ID3D11Resource* pResource) override
		{
			(void)pResource;
			throw std::logic_error("unexpected COM method GetResourceMinLOD");
		}
		void STDMETHODCALLTYPE ResolveSubresource(ID3D11Resource* pDstResource, UINT DstSubresource, ID3D11Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) override
		{
			(void)pDstResource;
			(void)DstSubresource;
			(void)pSrcResource;
			(void)SrcSubresource;
			(void)Format;
			throw std::logic_error("unexpected COM method ResolveSubresource");
		}
		void STDMETHODCALLTYPE ExecuteCommandList(ID3D11CommandList* pCommandList, BOOL RestoreContextState) override
		{
			(void)pCommandList;
			(void)RestoreContextState;
			throw std::logic_error("unexpected COM method ExecuteCommandList");
		}
		void STDMETHODCALLTYPE HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override
		{
			(void)StartSlot;
			(void)NumViews;
			(void)ppShaderResourceViews;
			throw std::logic_error("unexpected COM method HSSetShaderResources");
		}
		void STDMETHODCALLTYPE HSSetShader(ID3D11HullShader* pHullShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override
		{
			(void)pHullShader;
			(void)ppClassInstances;
			(void)NumClassInstances;
			throw std::logic_error("unexpected COM method HSSetShader");
		}
		void STDMETHODCALLTYPE HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method HSSetSamplers");
		}
		void STDMETHODCALLTYPE HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method HSSetConstantBuffers");
		}
		void STDMETHODCALLTYPE DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override
		{
			(void)StartSlot;
			(void)NumViews;
			(void)ppShaderResourceViews;
			throw std::logic_error("unexpected COM method DSSetShaderResources");
		}
		void STDMETHODCALLTYPE DSSetShader(ID3D11DomainShader* pDomainShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override
		{
			(void)pDomainShader;
			(void)ppClassInstances;
			(void)NumClassInstances;
			throw std::logic_error("unexpected COM method DSSetShader");
		}
		void STDMETHODCALLTYPE DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method DSSetSamplers");
		}
		void STDMETHODCALLTYPE DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method DSSetConstantBuffers");
		}
		void STDMETHODCALLTYPE CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override
		{
			(void)StartSlot;
			(void)NumViews;
			(void)ppShaderResourceViews;
			throw std::logic_error("unexpected COM method CSSetShaderResources");
		}
		void STDMETHODCALLTYPE CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts) override
		{
			(void)StartSlot;
			(void)NumUAVs;
			(void)ppUnorderedAccessViews;
			(void)pUAVInitialCounts;
			throw std::logic_error("unexpected COM method CSSetUnorderedAccessViews");
		}
		void STDMETHODCALLTYPE CSSetShader(ID3D11ComputeShader* pComputeShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override
		{
			(void)pComputeShader;
			(void)ppClassInstances;
			(void)NumClassInstances;
			throw std::logic_error("unexpected COM method CSSetShader");
		}
		void STDMETHODCALLTYPE CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method CSSetSamplers");
		}
		void STDMETHODCALLTYPE CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method CSSetConstantBuffers");
		}
		void STDMETHODCALLTYPE VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method VSGetConstantBuffers");
		}
		void STDMETHODCALLTYPE PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override
		{
			ReadResources(4, StartSlot, NumViews, ppShaderResourceViews);
		}
		void STDMETHODCALLTYPE PSGetShader(ID3D11PixelShader** ppPixelShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override
		{
			TouchGetter();
			(void)ppClassInstances;
			if (pNumClassInstances)
				*pNumClassInstances = 0;
			*ppPixelShader = queriedPixel;
			if (*ppPixelShader)
				(*ppPixelShader)->AddRef();
		}
		void STDMETHODCALLTYPE PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method PSGetSamplers");
		}
		void STDMETHODCALLTYPE VSGetShader(ID3D11VertexShader** ppVertexShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override
		{
			TouchGetter();
			(void)ppClassInstances;
			if (pNumClassInstances)
				*pNumClassInstances = 0;
			*ppVertexShader = nullptr;
		}
		void STDMETHODCALLTYPE PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method PSGetConstantBuffers");
		}
		void STDMETHODCALLTYPE IAGetInputLayout(ID3D11InputLayout** ppInputLayout) override
		{
			(void)ppInputLayout;
			throw std::logic_error("unexpected COM method IAGetInputLayout");
		}
		void STDMETHODCALLTYPE IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppVertexBuffers, UINT* pStrides, UINT* pOffsets) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppVertexBuffers;
			(void)pStrides;
			(void)pOffsets;
			throw std::logic_error("unexpected COM method IAGetVertexBuffers");
		}
		void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D11Buffer** pIndexBuffer, DXGI_FORMAT* Format, UINT* Offset) override
		{
			(void)pIndexBuffer;
			(void)Format;
			(void)Offset;
			throw std::logic_error("unexpected COM method IAGetIndexBuffer");
		}
		void STDMETHODCALLTYPE GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method GSGetConstantBuffers");
		}
		void STDMETHODCALLTYPE GSGetShader(ID3D11GeometryShader** ppGeometryShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override
		{
			TouchGetter();
			(void)ppClassInstances;
			if (pNumClassInstances)
				*pNumClassInstances = 0;
			*ppGeometryShader = nullptr;
		}
		void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* pTopology) override
		{
			(void)pTopology;
			throw std::logic_error("unexpected COM method IAGetPrimitiveTopology");
		}
		void STDMETHODCALLTYPE VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override
		{
			ReadResources(0, StartSlot, NumViews, ppShaderResourceViews);
		}
		void STDMETHODCALLTYPE VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method VSGetSamplers");
		}
		void STDMETHODCALLTYPE GetPredication(ID3D11Predicate** ppPredicate, BOOL* pPredicateValue) override
		{
			(void)ppPredicate;
			(void)pPredicateValue;
			throw std::logic_error("unexpected COM method GetPredication");
		}
		void STDMETHODCALLTYPE GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override
		{
			ReadResources(3, StartSlot, NumViews, ppShaderResourceViews);
		}
		void STDMETHODCALLTYPE GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method GSGetSamplers");
		}
		void STDMETHODCALLTYPE OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView** ppRenderTargetViews, ID3D11DepthStencilView** ppDepthStencilView) override
		{
			TouchGetter();
			for (UINT index = 0; index < NumViews; ++index) ppRenderTargetViews[index] = nullptr;
			if (ppDepthStencilView)
				*ppDepthStencilView = nullptr;
		}
		void STDMETHODCALLTYPE OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView** ppRenderTargetViews, ID3D11DepthStencilView** ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUnorderedAccessViews) override
		{
			TouchGetter();
			(void)UAVStartSlot;
			for (UINT index = 0; index < NumRTVs; ++index) ppRenderTargetViews[index] = nullptr;
			if (ppDepthStencilView)
				*ppDepthStencilView = nullptr;
			for (UINT index = 0; index < NumUAVs; ++index) ppUnorderedAccessViews[index] = nullptr;
		}
		void STDMETHODCALLTYPE OMGetBlendState(ID3D11BlendState** ppBlendState, FLOAT BlendFactor[4], UINT* pSampleMask) override
		{
			(void)ppBlendState;
			(void)BlendFactor;
			(void)pSampleMask;
			throw std::logic_error("unexpected COM method OMGetBlendState");
		}
		void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D11DepthStencilState** ppDepthStencilState, UINT* pStencilRef) override
		{
			(void)ppDepthStencilState;
			(void)pStencilRef;
			throw std::logic_error("unexpected COM method OMGetDepthStencilState");
		}
		void STDMETHODCALLTYPE SOGetTargets(UINT NumBuffers, ID3D11Buffer** ppSOTargets) override
		{
			(void)NumBuffers;
			(void)ppSOTargets;
			throw std::logic_error("unexpected COM method SOGetTargets");
		}
		void STDMETHODCALLTYPE RSGetState(ID3D11RasterizerState** ppRasterizerState) override
		{
			TouchGetter();
			*ppRasterizerState = nullptr;
		}
		void STDMETHODCALLTYPE RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports) override
		{
			TouchGetter();
			if (*pNumViewports && pViewports)
				*pViewports = { 0, 0, 64, 32, 0, 1 };
			*pNumViewports = 1;
		}
		void STDMETHODCALLTYPE RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects) override
		{
			TouchGetter();
			if (*pNumRects && pRects)
				*pRects = { 0, 0, 64, 32 };
			*pNumRects = 1;
		}
		void STDMETHODCALLTYPE HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override
		{
			ReadResources(1, StartSlot, NumViews, ppShaderResourceViews);
		}
		void STDMETHODCALLTYPE HSGetShader(ID3D11HullShader** ppHullShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override
		{
			TouchGetter();
			(void)ppClassInstances;
			if (pNumClassInstances)
				*pNumClassInstances = 0;
			*ppHullShader = nullptr;
		}
		void STDMETHODCALLTYPE HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method HSGetSamplers");
		}
		void STDMETHODCALLTYPE HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method HSGetConstantBuffers");
		}
		void STDMETHODCALLTYPE DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override
		{
			ReadResources(2, StartSlot, NumViews, ppShaderResourceViews);
		}
		void STDMETHODCALLTYPE DSGetShader(ID3D11DomainShader** ppDomainShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override
		{
			TouchGetter();
			(void)ppClassInstances;
			if (pNumClassInstances)
				*pNumClassInstances = 0;
			*ppDomainShader = nullptr;
		}
		void STDMETHODCALLTYPE DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method DSGetSamplers");
		}
		void STDMETHODCALLTYPE DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method DSGetConstantBuffers");
		}
		void STDMETHODCALLTYPE CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override
		{
			ReadResources(5, StartSlot, NumViews, ppShaderResourceViews);
		}
		void STDMETHODCALLTYPE CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUnorderedAccessViews) override
		{
			TouchGetter();
			(void)StartSlot;
			for (UINT index = 0; index < NumUAVs; ++index) ppUnorderedAccessViews[index] = nullptr;
		}
		void STDMETHODCALLTYPE CSGetShader(ID3D11ComputeShader** ppComputeShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override
		{
			TouchGetter();
			(void)ppClassInstances;
			if (pNumClassInstances)
				*pNumClassInstances = 0;
			*ppComputeShader = queriedCompute;
			if (*ppComputeShader)
				(*ppComputeShader)->AddRef();
		}
		void STDMETHODCALLTYPE CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override
		{
			(void)StartSlot;
			(void)NumSamplers;
			(void)ppSamplers;
			throw std::logic_error("unexpected COM method CSGetSamplers");
		}
		void STDMETHODCALLTYPE CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override
		{
			(void)StartSlot;
			(void)NumBuffers;
			(void)ppConstantBuffers;
			throw std::logic_error("unexpected COM method CSGetConstantBuffers");
		}
		void STDMETHODCALLTYPE ClearState(void) override
		{
			throw std::logic_error("unexpected COM method ClearState");
		}
		void STDMETHODCALLTYPE Flush(void) override
		{
			throw std::logic_error("unexpected COM method Flush");
		}
		D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType(void) override
		{
			TouchGetter();
			return type;
		}
		UINT STDMETHODCALLTYPE GetContextFlags(void) override
		{
			return 0;
		}
		HRESULT STDMETHODCALLTYPE FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList** ppCommandList) override
		{
			(void)RestoreDeferredContextState;
			(void)ppCommandList;
			throw std::logic_error("unexpected COM method FinishCommandList");
		}
	};
}
