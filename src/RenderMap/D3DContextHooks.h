#pragma once

struct ID3D11DeviceContext;

namespace CSX::RenderMap
{
	void InstallD3DContextHooks(ID3D11DeviceContext* a_context);
}
