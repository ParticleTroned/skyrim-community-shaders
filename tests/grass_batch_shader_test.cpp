#include "Utils/DepthPyramidPolicy.h"
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"
#include <bit>
#include <cmath>

#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>

namespace
{
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	struct Includes : ID3DInclude
	{
		std::vector<std::pair<std::filesystem::path, std::unique_ptr<Util::CustomInclude>>> roots;
		Includes()
		{
			roots.emplace_back("package/Shaders", std::make_unique<Util::CustomInclude>("package/Shaders"));
			for (const auto& feature : std::filesystem::directory_iterator("features"))
				if (std::filesystem::is_directory(feature.path() / "Shaders"))
					roots.emplace_back(feature.path() / "Shaders", std::make_unique<Util::CustomInclude>(feature.path() / "Shaders"));
		}
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			for (auto& [path, root] : roots)
				if (std::filesystem::is_regular_file(path / name) && SUCCEEDED(root->Open(type, name, parent, data, size)))
					return S_OK;
			return E_FAIL;
		}
		HRESULT Close(LPCVOID data) override { return roots.front().second->Close(data); }
	};

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	ComPtr<ID3DBlob> Compile(const wchar_t* path, const char* target, std::vector<D3D_SHADER_MACRO> defines = {})
	{
		Includes includes;
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> bytecode, errors;
		const auto result = D3DCompileFromFile(path, defines.data(), &includes, "main", target,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return bytecode;
	}

	void ValidateGrassVariants()
	{
		for (bool vr : { false, true })
			for (bool enhanced : { false, true })
				for (bool depth : { false, true })
					for (bool collision : { false, true }) {
						std::vector<D3D_SHADER_MACRO> defines{ { "VSHADER", "1" }, { "GRASS_OPTIMIZATIONS", "1" } };
						if (vr)
							defines.push_back({ "VR", "1" });
						if (enhanced)
							defines.push_back({ "GRASS_LIGHTING", "1" });
						if (depth)
							defines.push_back({ "RENDER_DEPTH", "1" });
						if (collision)
							defines.push_back({ "GRASS_COLLISION", "1" });
						auto bytecode = Compile(L"package/Shaders/RunGrass.hlsl", "vs_5_0", defines);
						ComPtr<ID3D11ShaderReflection> reflection;
						Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
						D3D11_SHADER_INPUT_BIND_DESC binding{};
						Check(reflection->GetResourceBindingDescByName("GrassBatch", &binding));
						Require(binding.BindPoint == 9, "Batch constants changed register");
						if (collision) {
							D3D11_SHADER_VARIABLE_DESC collisionDistance{};
							Check(reflection->GetConstantBufferByName("GrassBatch")->GetVariableByName("GrassBatchCollisionDistance")->GetDesc(&collisionDistance));
							Require(collisionDistance.StartOffset == 12 && collisionDistance.Size == sizeof(float), "Batch collision distance layout changed");
						}
						Check(reflection->GetResourceBindingDescByName("GrassInstanceExtras", &binding));
						Require(binding.BindPoint == 2, "Batch fade changed register");
						D3D11_SHADER_VARIABLE_DESC world{}, previous{};
						auto geometry = reflection->GetConstantBufferByName("PerGeometry");
						Check(geometry->GetVariableByName("World")->GetDesc(&world));
						Check(geometry->GetVariableByName("PreviousWorld")->GetDesc(&previous));
						Require(world.StartOffset == (vr ? 256u : 128u) && previous.StartOffset == (vr ? 384u : 192u), "Native geometry layout changed");
					}
	}

#include "grass_culling_gpu.h"
#include "grass_spd_gpu.h"

}

int main()
{
	try {
		ValidateGrassVariants();
		for (bool vr : { false, true })
			for (bool enhanced : { false, true })
				for (bool depth : { false, true }) {
					std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "GRASS_OPTIMIZATIONS", "1" } };
					if (vr)
						defines.push_back({ "VR", "1" });
					if (enhanced)
						defines.push_back({ "GRASS_LIGHTING", "1" });
					if (depth)
						defines.push_back({ "RENDER_DEPTH", "1" });
					Compile(L"package/Shaders/RunGrass.hlsl", "ps_5_0", defines);
				}
		RunCullingShader(false);
		RunCullingShader(true);
		RunDepthReduction(false);
		RunDepthReduction(true);
		RunSPDReduction(64, 64, 1);
		RunSPDReduction(128, 128, 2);
		RunSPDReduction(1024, 128, 2);
		RunSPDReduction(2048, 512, 1);
		RunSPDReduction(4096, 128, 2);
		RunSPDReduction(128, 8192, 1);
		Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassDepthCS.hlsl", "cs_5_0");
		Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassInstanceSignatureVS.hlsl", "vs_5_0");
		for (bool vr : { false, true }) {
			std::vector<D3D_SHADER_MACRO> defines;
			if (vr)
				defines.push_back({ "VR", "1" });
			auto code = Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassCullingCS.hlsl", "cs_5_0", defines);
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			Require(FAILED(reflection->GetResourceBindingDescByName("Counters", &binding)), "Production shader retained diagnostic UAV");
			defines.push_back({ "GRASS_DIAGNOSTICS", "1" });
			code = Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassCullingCS.hlsl", "cs_5_0", defines);
			reflection.Reset();
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			Check(reflection->GetResourceBindingDescByName("Counters", &binding));
			Require(binding.BindPoint == 3, "Diagnostic counter UAV changed slot");
		}
		std::cout << "16 grass vertex and 8 pixel variants, production diagnostic isolation, flat/VR visibility, sparse offsets, shared frusta, native wind, collision bounds, and conservative SPD mip chains passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
