#pragma once

#include "CSXAPI/CSshadercompatibilityapi.h"

namespace CSX::Api
{
	void InitializeShaderCompatibilityService();
	const ShaderCompatibilityAPI::Interface001* GetShaderCompatibilityService001();
}
