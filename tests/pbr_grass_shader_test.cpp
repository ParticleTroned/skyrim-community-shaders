#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <memory>

namespace
{
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	struct Includes : ID3DInclude
	{
		std::vector<std::unique_ptr<Util::CustomInclude>> roots;
		Includes()
		{
			roots.emplace_back(std::make_unique<Util::CustomInclude>("package/Shaders"));
			for (const auto& feature : std::filesystem::directory_iterator("features"))
				if (std::filesystem::is_directory(feature.path() / "Shaders"))
					roots.emplace_back(std::make_unique<Util::CustomInclude>(feature.path() / "Shaders"));
		}
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			for (size_t index = 0; index < roots.size(); ++index)
				if (SUCCEEDED(roots[index]->Open(type, name, parent, data, size)))
					return S_OK;
			return E_FAIL;
		}
		HRESULT Close(LPCVOID data) override { return roots.front()->Close(data); }
	};

	void Require(bool value, const char* reason)
	{
		if (!value)
			throw std::runtime_error(reason);
	}

	ComPtr<ID3D11ShaderReflection> Compile(bool vertex, bool vr, bool enhanced, bool pbr, bool depth, bool alpha, bool features)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { vertex ? "VSHADER" : "PSHADER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (enhanced)
			defines.push_back({ "GRASS_LIGHTING", "1" });
		if (pbr)
			defines.push_back({ "PBR_GRASS", "1" });
		if (depth)
			defines.push_back({ "RENDER_DEPTH", "1" });
		if (alpha)
			defines.push_back({ "DO_ALPHA_TEST", "1" });
		if (features) {
			for (const char* feature : { "LIGHT_LIMIT_FIX", "ISL", "SKYLIGHTING", "IBL", "DYNAMIC_CUBEMAPS",
					 "SCREEN_SPACE_SHADOWS", "TERRAIN_SHADOWS", "CLOUD_SHADOWS", "WATER_EFFECTS", "GRASS_COLLISION" })
				defines.push_back({ feature, "1" });
		}
		defines.push_back({ nullptr, nullptr });
		Includes includes;
		ComPtr<ID3DBlob> bytecode, errors;
		const auto result = D3DCompileFromFile(L"package/Shaders/RunGrass.hlsl", defines.data(), &includes, "main",
			vertex ? "vs_5_0" : "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		return reflection;
	}

	void CheckBinding(ID3D11ShaderReflection* shader, const char* name, UINT slot)
	{
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		Check(shader->GetResourceBindingDescByName(name, &binding));
		Require(binding.BindPoint == slot && binding.BindCount == 1, "Grass resource binding changed");
	}

	void CheckMaterial(ID3D11ShaderReflection* shader)
	{
		CheckBinding(shader, "PerMaterial", 1);
		auto* material = shader->GetConstantBufferByName("PerMaterial");
		D3D11_SHADER_BUFFER_DESC buffer{};
		Check(material->GetDesc(&buffer));
		Require(buffer.Size == 32, "Grass material constant size changed");
		const std::array names{ "PBRFlags", "PBRParams1", "PBRParams2" };
		const std::array<UINT, 3> offsets{ 0, 4, 16 };
		for (size_t index = 0; index < names.size(); ++index) {
			D3D11_SHADER_VARIABLE_DESC variable{};
			Check(material->GetVariableByName(names[index])->GetDesc(&variable));
			Require(variable.StartOffset == offsets[index], "Grass material constant offset changed");
		}
		for (const auto& [name, slot] : std::array<std::pair<const char*, UINT>, 6>{ { { "TexNormalSampler", 2 }, { "TexRMAOSSampler", 3 }, { "TexSubsurfaceSampler", 4 },
				 { "SampNormalSampler", 2 }, { "SampRMAOSSampler", 3 }, { "SampSubsurfaceSampler", 4 } } })
			CheckBinding(shader, name, slot);
	}

	void CheckGeometry(ID3D11ShaderReflection* shader, bool vr)
	{
		CheckBinding(shader, "PerGeometry", 2);
		auto* geometry = shader->GetConstantBufferByName("PerGeometry");
		D3D11_SHADER_VARIABLE_DESC world{};
		Check(geometry->GetVariableByName("World")->GetDesc(&world));
		Require(world.StartOffset == (vr ? 256u : 128u), "Grass World geometry offset changed");
		D3D11_SHADER_VARIABLE_DESC previous{};
		Check(geometry->GetVariableByName("PreviousWorld")->GetDesc(&previous));
		Require(previous.StartOffset == (vr ? 384u : 192u), "Grass previous-world offset changed");
	}

	void CheckOutputs(ID3D11ShaderReflection* shader, bool pbr, bool depth)
	{
		D3D11_SHADER_DESC descriptor{};
		Check(shader->GetDesc(&descriptor));
		bool reflectance = false;
		for (UINT index = 0; index < descriptor.OutputParameters; ++index) {
			D3D11_SIGNATURE_PARAMETER_DESC output{};
			Check(shader->GetOutputParameterDesc(index, &output));
			if (output.SystemValueType == D3D_NAME_TARGET && output.SemanticIndex == 5)
				reflectance = true;
		}
		Require(reflectance == (pbr && !depth), "Grass reflectance output contract changed");
		if (depth) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			Require(FAILED(shader->GetResourceBindingDescByName("PerMaterial", &binding)), "Depth grass must not bind PBR material constants");
		}
	}
}

int main()
{
	try {
		unsigned compiled = 0;
		for (bool vr : { false, true }) {
			for (bool enhanced : { false, true }) {
				for (bool pbr : { false, true }) {
					if (pbr && !enhanced)
						continue;
					for (bool vertex : { false, true }) {
						for (bool depth : { false, true }) {
							auto shader = Compile(vertex, vr, enhanced, pbr, depth, true, false);
							++compiled;
							if (vertex)
								CheckGeometry(shader.Get(), vr);
							else
								CheckOutputs(shader.Get(), pbr, depth);
							if (pbr && !vertex && !depth)
								CheckMaterial(shader.Get());
						}
					}
				}
			}
			for (bool vertex : { false, true }) {
				for (bool alpha : { false, true }) {
					auto shader = Compile(vertex, vr, true, true, false, alpha, true);
					++compiled;
					if (vertex)
						CheckGeometry(shader.Get(), vr);
					else
						CheckMaterial(shader.Get());
				}
			}
		}
		std::cout << compiled << " strict grass permutations and material/geometry reflection passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
