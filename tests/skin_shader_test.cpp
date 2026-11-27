#define NOMINMAX
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using Microsoft::WRL::ComPtr;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	void Binding(ID3D11ShaderReflection* reflection, const char* name, UINT slot)
	{
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		if (FAILED(reflection->GetResourceBindingDescByName(name, &binding)))
			throw std::runtime_error(std::string("Missing skin shader binding: ") + name);
		Require(binding.BindPoint == slot, "Skin resource slot disagrees with its CPU binding");
	}
}

int main()
{
	try {
		unsigned count = 0;
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				for (bool deferred : { false, true })
					for (unsigned lightingFeatures : { 0u, 1u, 2u, 3u })
						for (unsigned geometryFlags : { 0u, 1u, 2u, 3u })
							for (const char* material : { "FACEGEN", "FACEGEN_RGB_TINT" })
								for (const char* wetness : { "", "WETTERNESS", "WETNESS_EFFECTS" }) {
									if (lightingFeatures != 3 && (geometryFlags != 0 || std::string_view(material) != "FACEGEN" || std::string_view(wetness) != "WETNESS_EFFECTS"))
										continue;
									auto includes = PackageIncludes::AllFeatures(".");
									std::vector<D3D_SHADER_MACRO> defines{
										{ "PSHADER", "1" }, { "CS_SKIN", "1" }, { material, "1" }, { "SPECULAR", "1" },
										{ "SSS", "1" }, { "ADAPTIVE_BALANCE", "1" }, { "DYNAMIC_CUBEMAPS", "1" },
										{ "DETAIL_MAPS", "1" }, { "SNOW_COVER", "1" }
									};
									if (lightingFeatures & 1u)
										defines.push_back({ "IBL", "1" });
									if (lightingFeatures & 2u)
										defines.push_back({ "LIGHT_LIMIT_FIX", "1" });
									if (vr)
										defines.push_back({ "VR", "1" });
									if (pbr)
										defines.push_back({ "TRUE_PBR", "1" });
									if (deferred)
										defines.push_back({ "DEFERRED", "1" });
									if (geometryFlags & 1u)
										defines.push_back({ "MODELSPACENORMALS", "1" });
									if (geometryFlags & 2u)
										defines.push_back({ "SKINNED", "1" });
									if (*wetness)
										defines.push_back({ wetness, "1" });
									defines.push_back({ nullptr, nullptr });
									ComPtr<ID3DBlob> code, errors;
									const auto result = D3DCompileFromFile(L"package/Shaders/Lighting.hlsl", defines.data(), &includes, "main", "ps_5_0",
										D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
									if (FAILED(result)) {
										for (const auto& define : defines)
											if (define.Name)
												std::cerr << define.Name << ' ';
										std::cerr << '\n';
									}
									if (errors)
										std::cerr << static_cast<const char*>(errors->GetBufferPointer());
									Check(result);
									ComPtr<ID3D11ShaderReflection> reflection;
									Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
									if (!pbr) {
										auto* skin = reflection->GetVariableByName("SharedData::skinData");
										D3D11_SHADER_VARIABLE_DESC shared{};
										Require(SUCCEEDED(skin->GetDesc(&shared)), "Missing shared skin data");
										Require(shared.Size == 128, "Skin shared CPU/GPU layout mismatch");
										D3D11_SHADER_TYPE_DESC response{};
										Require(SUCCEEDED(skin->GetType()->GetMemberTypeByName("wetResponse")->GetDesc(&response)), "Missing wetness response");
										Require(response.Offset == 112, "Skin wetness response offset mismatch");
										D3D11_SHADER_TYPE_DESC profile{};
										Require(SUCCEEDED(skin->GetType()->GetMemberTypeByName("noMapTransmissionProfile")->GetDesc(&profile)), "Missing transmission profile");
										Require(profile.Offset == 116 && profile.Columns == 3, "Skin transmission profile offset mismatch");
										Binding(reflection.Get(), "Skin::SkinPerGeometry", 7);
										Binding(reflection.Get(), "Skin::TexSkinDetailNormal", 72);
										Binding(reflection.Get(), "Skin::DetailSampler", 7);
										Binding(reflection.Get(), "Skin::TexSkinExtra", 75);
										Binding(reflection.Get(), "Skin::TexSkinThickness", 99);
										if (*wetness)
											Binding(reflection.Get(), "Skin::TexSkinWetness", 74);
										D3D11_SHADER_BUFFER_DESC buffer{};
										Check(reflection->GetConstantBufferByName("Skin::SkinPerGeometry")->GetDesc(&buffer));
										Require(buffer.Size == 32, "Skin per-geometry CPU/GPU layout mismatch");
									}
									++count;
								}
		std::cout << count << " SE/AE and VR skin, PBR, SSS and wetness permutations compiled and reflected\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
