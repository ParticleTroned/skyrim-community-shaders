#pragma once

#include "Utils/ShaderCompiler.h"

// The standalone harness substitutes shader loading and profiler entry points only.
#define CS_GPU_PASS(...)
#define CS_GPU_DETAIL_PASS(...)
namespace Util
{
	void SetResourceName(ID3D11DeviceChild*, const char*, ...);
}
