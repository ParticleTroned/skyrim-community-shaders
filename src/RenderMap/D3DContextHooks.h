#pragma once

#include "RenderMap/Collector.h"

struct ID3D11DeviceContext;
struct ID3D11Resource;

namespace CSX::RenderMap
{
	ResourceObservationInput DescribeResource(ID3D11Resource* a_resource) noexcept;
	void InstallD3DContextHooks(ID3D11DeviceContext* a_context);
}
