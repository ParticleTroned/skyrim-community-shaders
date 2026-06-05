#include "test_common.h"

#include <catch2/catch_test_macros.hpp>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	using Microsoft::WRL::ComPtr;

	struct ShaderMacro
	{
		const char* name;
		const char* definition = "1";
	};

	std::string ToUtf8(const std::filesystem::path& path)
	{
		const auto value = path.u8string();
		return std::string(reinterpret_cast<const char*>(value.data()), value.size());
	}

	std::string FormatDefines(const std::vector<ShaderMacro>& defines)
	{
		std::ostringstream stream;
		for (const auto& define : defines) {
			if (stream.tellp() > 0) {
				stream << ", ";
			}

			stream << define.name << "=" << define.definition;
		}

		return stream.str();
	}

	std::string BlobToString(ID3DBlob* blob)
	{
		if (!blob || !blob->GetBufferPointer() || blob->GetBufferSize() == 0) {
			return {};
		}

		return std::string(
			static_cast<const char*>(blob->GetBufferPointer()),
			static_cast<size_t>(blob->GetBufferSize()));
	}

	void CompilePermutation(
		const wchar_t* shaderFile,
		const char* target,
		const std::vector<ShaderMacro>& defines)
	{
		const auto shaderPath = ShaderTest::GetExecutableDirectory() / "Shaders" / shaderFile;
		REQUIRE(std::filesystem::exists(shaderPath));

		std::vector<D3D_SHADER_MACRO> macros;
		macros.reserve(defines.size() + 1);
		for (const auto& define : defines) {
			macros.push_back({ define.name, define.definition });
		}
		macros.push_back({ nullptr, nullptr });

		ComPtr<ID3DBlob> bytecode;
		ComPtr<ID3DBlob> errors;

		const HRESULT result = D3DCompileFromFile(
			shaderPath.c_str(),
			macros.data(),
			D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"main",
			target,
			D3DCOMPILE_ENABLE_STRICTNESS,
			0,
			bytecode.GetAddressOf(),
			errors.GetAddressOf());

		INFO("Shader: " << ToUtf8(shaderPath));
		INFO("Target: " << target);
		INFO("Defines: " << FormatDefines(defines));
		INFO("Compiler output: " << BlobToString(errors.Get()));
		REQUIRE(SUCCEEDED(result));
	}
}

TEST_CASE("Production shader permutations compile with feature includes", "[shaders][fxc][permutations]")
{
	SECTION("Particle pixel shader with Terrain Shadows")
	{
		CompilePermutation(
			L"Particle.hlsl",
			"ps_5_0",
			{
				{ "PSHADER" },
				{ "TERRAIN_SHADOWS" },
			});
	}

	SECTION("Particle pixel shader with Terrain Shadows and Cloud Shadows")
	{
		CompilePermutation(
			L"Particle.hlsl",
			"ps_5_0",
			{
				{ "PSHADER" },
				{ "TERRAIN_SHADOWS" },
				{ "CLOUD_SHADOWS" },
			});
	}
}
