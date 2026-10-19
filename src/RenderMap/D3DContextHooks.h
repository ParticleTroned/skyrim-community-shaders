#pragma once

#include "RenderMap/Runtime.h"

struct ID3D11DeviceContext;
struct ID3D11Resource;

namespace CSX::RenderMap
{
	/** Describe a depth view only when D3D11 consumes the target arguments. */
	template <class View, class Describer>
	ResourceViewInput DescribeChangedDepthTarget(View* a_view, bool a_keepTargets, Describer a_describe)
	{
		return a_keepTargets ? ResourceViewInput{} : a_describe(a_view);
	}

	ResourceObservationInput DescribeResource(ID3D11Resource* a_resource) noexcept;
	void InstallD3DContextHooks(ID3D11DeviceContext* a_context);
	/** Query current immediate state at an activated boundary; never replay calls. */
	bool CapturePostProcessingBootstrap(ID3D11DeviceContext* a_context) noexcept;
}
