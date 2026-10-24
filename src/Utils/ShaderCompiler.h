#pragma once

#include <cstdint>
#include <d3d11.h>
#include <d3dcommon.h>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

namespace Util
{
	/** Captured compiler inputs borrowed only for one synchronous standalone request. */
	struct StandaloneShaderRequest
	{
		std::filesystem::path source;
		std::span<const D3D_SHADER_MACRO> macros;
		const char* profile = nullptr;
		const char* entryPoint = "main";
		uint32_t flags = 0;
		bool developerMode = false;
	};

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
	/** Report compiler/object creation errors so optional paths can preserve allocation failures. */
	ID3D11DeviceChild* CompileShader(
		const wchar_t* FilePath,
		const std::vector<std::pair<const char*, const char*>>& Defines,
		const char* ProgramType,
		const char* Program,
		HRESULT& a_result,
		ShaderCompileTiming* a_timing = nullptr);

	/** Permit standalone retries without retaining failures from pre-reset attempts. */
	void ClearShaderCompileFailures();
}
