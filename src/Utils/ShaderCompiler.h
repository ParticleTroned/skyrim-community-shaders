#pragma once

#include "ContentHash.h"
#include "OwnedShaderMacros.h"

#include <cstdint>
#include <d3d11.h>
#include <d3dcommon.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <winrt/base.h>

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
		bool cacheOnly = false;
		std::optional<ContentHash::Hash128> compileStateDigest;
		bool forceSource = false;
	};

	/** Immutable settings and ABI identity captured by the compiler's owner. */
	struct ShaderCompilerSnapshot
	{
		bool developerMode = false, isVR = false, reverseZ = false;
		uint32_t flags = 0;
		OwnedShaderMacros globalDefines;
		ContentHash::Hash128 digest;
		uint64_t graphicsRevision = 0, inputGeneration = 0;
		std::function<bool()> isCancelled;
	};
	ShaderCompilerSnapshot CaptureShaderCompilerSnapshot();

	/** Retains a deferred creation stage and a single rejected-cache source retry. */
	struct ShaderBytecodeContinuation
	{
		winrt::com_ptr<ID3DBlob> blob;
		bool fromSource = false, retrySource = false;
	};

	/** @brief Optional QPC accumulator separating compiler and device creation cost. */
	struct ShaderCompileTiming
	{
		uint64_t bytecodeCompilationQpcTicks = 0;
		uint64_t d3dObjectCreationQpcTicks = 0;
		uint64_t sourceCompiles = 0;
		bool deferred = false;
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

	/** Compile on a caller-retained device, including from a background worker. */
	ID3D11DeviceChild* CompileShaderOnDevice(
		ID3D11Device* device,
		const wchar_t* FilePath,
		const std::vector<std::pair<const char*, const char*>>& Defines,
		const char* ProgramType,
		const char* Program,
		HRESULT& a_result,
		ShaderCompileTiming* a_timing = nullptr,
		bool a_optionalSpecialization = false,
		const ShaderCompilerSnapshot* a_snapshot = nullptr,
		ShaderBytecodeContinuation* a_continuation = nullptr);

	/** Permit standalone retries without retaining failures from pre-reset attempts. */
	void ClearShaderCompileFailures();
}
