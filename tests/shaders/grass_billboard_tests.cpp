#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "native_d3d11_test.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::Name;
	using Microsoft::WRL::ComPtr;

	void Require(bool value, const char* reason)
	{
		if (!value)
			throw std::runtime_error(reason);
	}
	ComPtr<ID3DBlob> CompileProgram(const D3D_SHADER_MACRO* defines, bool vertex, const char* source = nullptr, const char* entry = "VerifyLegacyColor")
	{
		D3D11ShaderTest::ShaderIncludes includes;
		ComPtr<ID3DBlob> bytecode, errors;
		constexpr UINT options = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		const auto result = source ?
		                        D3DCompile(source, std::strlen(source), "grass-color-regression", defines, &includes, entry, vertex ? "vs_5_0" : "ps_5_0", options, 0, bytecode.GetAddressOf(), errors.GetAddressOf()) :
		                        D3DCompileFromFile((includes.root / "RunGrass.hlsl").c_str(), defines, &includes, "main", vertex ? "vs_5_0" : "ps_5_0", options, 0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return bytecode;
	}

	std::vector<D3D_SHADER_MACRO> GrassDefines(bool vertex, bool enhanced, bool pbr, bool depth, bool alpha, bool features, bool optimized, bool category = false)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { vertex ? "VSHADER" : "PSHADER", "1" } };
		if (optimized)
			defines.push_back({ "GRASS_OPTIMIZATIONS", "1" });
		if (enhanced)
			defines.push_back({ "GRASS_LIGHTING", "1" });
		if (pbr)
			defines.push_back({ "PBR_GRASS", "1" });
		if (depth)
			defines.push_back({ "RENDER_DEPTH", "1" });
		if (alpha)
			defines.push_back({ "DO_ALPHA_TEST", "1" });
		if (category)
			defines.push_back({ "CHARACTER_CATEGORY", "1" });
		if (features) {
			for (const char* feature : { "LIGHT_LIMIT_FIX", "ISL", "SKYLIGHTING", "IBL", "DYNAMIC_CUBEMAPS",
					 "SCREEN_SPACE_SHADOWS", "TERRAIN_SHADOWS", "CLOUD_SHADOWS", "WATER_EFFECTS", "GRASS_COLLISION" })
				defines.push_back({ feature, "1" });
		}
		defines.push_back({ nullptr, nullptr });
		return defines;
	}

#include "grass_billboard_fixture.h"
}

TEST_CASE("SE far grass preserves authored normals and lights both card faces", "[shaders][warp][grass][se]")
{
	GrassAlphaFixture fixture;
	for (bool enhanced : { false, true })
		fixture.CheckBillboardVertexNormals(enhanced);
	for (unsigned mode = 0; mode < 3; ++mode)
		for (bool depth : { false, true })
			for (bool authoredPBR : { false, true })
				fixture.CheckVariant(true, mode != 0, mode == 2, depth, true, true, authoredPBR);
}
