#pragma once

#include "Utils/ContentHash.h"

#include <cstdint>
#include <d3d11.h>
#include <d3dcommon.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <winrt/base.h>

namespace SIE
{
	class ShaderCache;
}

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
		std::optional<ContentHash::Hash128> globalCompileDigest;
		std::optional<uint64_t> compilationGeneration;
		std::optional<uint64_t> sourceGeneration;
		bool requireStableSource = false;
		bool* staleSource = nullptr;
	};

	struct ShaderPreparation;
	struct ShaderCompileTiming;

	/** Owner-thread snapshot shared by compiler macros and cache identity. */
	struct StandaloneCompileState
	{
		ContentHash::Hash128 digest;
		uint64_t compilationGeneration = 0;
		uint64_t sourceGeneration = 0;
		bool developerMode = false;
		bool isVR = false;
		bool partialPrecision = false;
		bool avoidFlowControl = false;
		bool skipValidation = false;
		std::vector<std::pair<std::string, std::string>> defines;
		uint64_t shaderDefinesGeneration = 0;
	};

	/** Owns every compiler input and the device through queued execution. */
	struct OwnedStandaloneShaderRequest
	{
		struct Macro
		{
			std::string name;
			std::optional<std::string> definition;
		};
		std::filesystem::path source;
		std::vector<Macro> macros;
		std::string profile;
		std::string entryPoint;
		std::string resourceName;
		uint32_t flags = 0;
		StandaloneCompileState state;
		winrt::com_ptr<ID3D11Device> device;
		bool requireStableSource = false;
	};

	/** Queues exact standalone preparation; only explicit owner boundaries adopt it. */
	std::shared_ptr<ShaderPreparation> RequestShaderPreparation(
		const wchar_t* a_path, const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_target, const char* a_entry, const char* a_name);
	/** Checks pending publication epochs and retained device against the current renderer. */
	bool IsShaderPreparationCurrent(const ShaderPreparation& a_preparation, bool a_resident = false);
	/** Executes a captured request without reading render-owned configuration or context. */
	winrt::com_ptr<ID3D11DeviceChild> CompilePreparedShader(
		const OwnedStandaloneShaderRequest& a_request, SIE::ShaderCache& a_cache, ShaderCompileTiming* a_timing = nullptr, bool* a_staleSource = nullptr);

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
