#pragma once

struct ID3D11DeviceChild;

namespace Util
{
	/** @brief Sets the shared RenderDoc/PIX debug name for a D3D resource. */
	void SetResourceName(ID3D11DeviceChild* Resource, const char* Format, ...);
}
