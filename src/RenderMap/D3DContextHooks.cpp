#include "RenderMap/D3DContextHooks.h"

#include "RenderMap/Runtime.h"

#include <d3d11.h>

#include <algorithm>
#include <array>

namespace CSX::RenderMap
{
	namespace
	{
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
			bool a_keepTargets = false)
		{
			std::array<std::uintptr_t, kMaximumRenderTargets> pointers{};
			if (!a_keepTargets && a_renderTargets) {
				const auto count = std::min<UINT>(a_renderTargetCount, static_cast<UINT>(pointers.size()));
				for (UINT index = 0; index < count; ++index)
					pointers[index] = reinterpret_cast<std::uintptr_t>(a_renderTargets[index]);
			}
			GetRuntime().BindRenderTargets(
				reinterpret_cast<std::uintptr_t>(a_context),
				a_keepTargets ? 0u : a_renderTargetCount,
				pointers.data(),
				reinterpret_cast<std::uintptr_t>(a_depthTarget),
				a_keepTargets);
		}

		struct ID3D11DeviceContext_OMSetRenderTargets
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_renderTargetCount,
				ID3D11RenderTargetView* const* a_renderTargets, ID3D11DepthStencilView* a_depthTarget)
			{
				func(a_context, a_renderTargetCount, a_renderTargets, a_depthTarget);
				ObserveRenderTargets(a_context, a_renderTargetCount, a_renderTargets, a_depthTarget);
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
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_PSSetShader
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_shader,
				ID3D11ClassInstance* const* a_classInstances, UINT a_classInstanceCount)
			{
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
				GetRuntime().RecordDraw(reinterpret_cast<std::uintptr_t>(a_context),
					DrawOperation::kDrawIndexedInstanced, a_indexCountPerInstance, a_instanceCount,
					a_startIndexLocation, PackSignedAndUnsigned(a_baseVertexLocation, a_startInstanceLocation));
				func(a_context, a_indexCountPerInstance, a_instanceCount, a_startIndexLocation,
					a_baseVertexLocation, a_startInstanceLocation);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawInstanced
		{
			static void thunk(ID3D11DeviceContext* a_context, UINT a_vertexCountPerInstance,
				UINT a_instanceCount, UINT a_startVertexLocation, UINT a_startInstanceLocation)
			{
				GetRuntime().RecordDraw(reinterpret_cast<std::uintptr_t>(a_context),
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
				GetRuntime().RecordDraw(reinterpret_cast<std::uintptr_t>(a_context), DrawOperation::kDrawAuto);
				func(a_context);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_DrawIndexedInstancedIndirect
		{
			static void thunk(ID3D11DeviceContext* a_context, ID3D11Buffer* a_argumentBuffer,
				UINT a_alignedByteOffset)
			{
				GetRuntime().RecordDraw(reinterpret_cast<std::uintptr_t>(a_context),
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
				GetRuntime().RecordDraw(reinterpret_cast<std::uintptr_t>(a_context),
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
				GetRuntime().RecordDispatch(reinterpret_cast<std::uintptr_t>(a_context),
					DispatchOperation::kDispatchIndirect,
					reinterpret_cast<std::uintptr_t>(a_argumentBuffer), a_alignedByteOffset);
				func(a_context, a_argumentBuffer, a_alignedByteOffset);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void InstallD3DContextHooks(ID3D11DeviceContext* a_context)
	{
		if (!a_context)
			return;
		GetRuntime().SetImmediateContext(reinterpret_cast<std::uintptr_t>(a_context));
		stl::detour_vfunc<9, ID3D11DeviceContext_PSSetShader>(a_context);
		stl::detour_vfunc<11, ID3D11DeviceContext_VSSetShader>(a_context);
		stl::detour_vfunc<20, ID3D11DeviceContext_DrawIndexedInstanced>(a_context);
		stl::detour_vfunc<21, ID3D11DeviceContext_DrawInstanced>(a_context);
		stl::detour_vfunc<33, ID3D11DeviceContext_OMSetRenderTargets>(a_context);
		stl::detour_vfunc<34, ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews>(a_context);
		stl::detour_vfunc<38, ID3D11DeviceContext_DrawAuto>(a_context);
		stl::detour_vfunc<39, ID3D11DeviceContext_DrawIndexedInstancedIndirect>(a_context);
		stl::detour_vfunc<40, ID3D11DeviceContext_DrawInstancedIndirect>(a_context);
		stl::detour_vfunc<41, ID3D11DeviceContext_Dispatch>(a_context);
		stl::detour_vfunc<42, ID3D11DeviceContext_DispatchIndirect>(a_context);
		stl::detour_vfunc<69, ID3D11DeviceContext_CSSetShader>(a_context);
	}
}
