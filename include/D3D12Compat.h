#pragma once

#include <d3d12.h>

// Avoid compiling d3dx12 feature-support helper class on SDKs that don't
// expose newer D3D12 options structs/enums (e.g. OPTIONS14/15).
#ifndef D3DX12_NO_CHECK_FEATURE_SUPPORT_CLASS
#	define D3DX12_NO_CHECK_FEATURE_SUPPORT_CLASS
#endif

// Compatibility shim for SDK/header combinations where directx/d3dx12.h expects
// newer D3D12 types that are not present in d3d12.h.
#if defined(_MSC_VER)
__if_not_exists(D3D12_DEPTH_STENCILOP_DESC1) {
	struct D3D12_DEPTH_STENCILOP_DESC1
	{
		D3D12_STENCIL_OP StencilFailOp;
		D3D12_STENCIL_OP StencilDepthFailOp;
		D3D12_STENCIL_OP StencilPassOp;
		D3D12_COMPARISON_FUNC StencilFunc;
		UINT8 StencilReadMask;
		UINT8 StencilWriteMask;
	};
}
__if_not_exists(D3D12_DEPTH_STENCIL_DESC2) {
	struct D3D12_DEPTH_STENCIL_DESC2
	{
		BOOL DepthEnable;
		D3D12_DEPTH_WRITE_MASK DepthWriteMask;
		D3D12_COMPARISON_FUNC DepthFunc;
		BOOL StencilEnable;
		D3D12_DEPTH_STENCILOP_DESC1 FrontFace;
		D3D12_DEPTH_STENCILOP_DESC1 BackFace;
		BOOL DepthBoundsTestEnable;
	};
}
#else
#	if defined(D3D12_SDK_VERSION) && (D3D12_SDK_VERSION <= 602)
struct D3D12_DEPTH_STENCILOP_DESC1
{
	D3D12_STENCIL_OP StencilFailOp;
	D3D12_STENCIL_OP StencilDepthFailOp;
	D3D12_STENCIL_OP StencilPassOp;
	D3D12_COMPARISON_FUNC StencilFunc;
	UINT8 StencilReadMask;
	UINT8 StencilWriteMask;
};

struct D3D12_DEPTH_STENCIL_DESC2
{
	BOOL DepthEnable;
	D3D12_DEPTH_WRITE_MASK DepthWriteMask;
	D3D12_COMPARISON_FUNC DepthFunc;
	BOOL StencilEnable;
	D3D12_DEPTH_STENCILOP_DESC1 FrontFace;
	D3D12_DEPTH_STENCILOP_DESC1 BackFace;
	BOOL DepthBoundsTestEnable;
};
#	endif
#endif

#ifndef D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2
// 10.0.22621.0 leaves subobject value 23 unused (AS starts at 24),
// so use 23 to avoid duplicate switch cases with DEPTH_STENCIL1 (21).
#	define D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2 static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(23)
#endif
