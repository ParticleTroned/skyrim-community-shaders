#pragma once

#include "CSXAPI/CSshaderapi.h"

namespace CSX::Api
{
	void InitializeShaderService();
	const ShaderAPI::Interface001* GetShaderService001();
}
