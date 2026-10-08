#pragma once

#include <cstdint>
#include <d3d11.h>
#include <utility>
#include <vector>

namespace Util
{
	/** @brief Optional QPC accumulator separating compiler and device creation cost. */
	struct ShaderCompileTiming
	{
		uint64_t bytecodeCompilationQpcTicks = 0;
		uint64_t d3dObjectCreationQpcTicks = 0;
	};

	/** Compile a packaged shader and create its object on the renderer's D3D11 device. */
	ID3D11DeviceChild* CompileShader(
		const wchar_t* FilePath,
		const std::vector<std::pair<const char*, const char*>>& Defines,
		const char* ProgramType,
		const char* Program = "main",
		ShaderCompileTiming* a_timing = nullptr);
}
