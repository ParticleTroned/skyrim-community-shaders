#pragma once

struct ID3D11DeviceContext;

namespace CSX::Api
{
	/** Install indexed submission observers while retaining originals for each context. */
	bool InstallAcceptedDrawD3DHooks(ID3D11DeviceContext* context);
}
