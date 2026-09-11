#include "Utils/ShaderInclude.h"

#include <wrl/client.h>

#include <iostream>
#include <vector>

int main()
{
	const std::filesystem::path shaderRoot = "features/Upscaling/Shaders";
	Util::CustomInclude include(shaderRoot);
	unsigned failures = 0;
	unsigned compiled = 0;
	for (const auto* shader : { "RCAS/RCAS.hlsl", "LumaSharpen/LumaSharpen.hlsl" }) {
		for (const bool adaptive : { false, true }) {
			for (const bool vr : { false, true }) {
				for (const bool hdr : { false, true }) {
					std::vector<D3D_SHADER_MACRO> defines{ { "COMPUTESHADER", "1" }, { "WINPC", "1" }, { "DX11", "1" } };
					if (adaptive)
						defines.push_back({ "MOTION_ADAPTIVE", "1" });
					if (vr)
						defines.push_back({ "VR", "1" });
					if (hdr)
						defines.push_back({ "HDR_OUTPUT", "1" });
					defines.push_back({ nullptr, nullptr });
					const auto path = shaderRoot / "Upscaling" / shader;
					Microsoft::WRL::ComPtr<ID3DBlob> bytecode, errors;
					const HRESULT result = D3DCompileFromFile(path.c_str(), defines.data(), &include, "main", "cs_5_0",
						D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
						0, bytecode.GetAddressOf(), errors.GetAddressOf());
					if (FAILED(result) || !bytecode) {
						++failures;
						std::cerr << shader << " adaptive=" << adaptive << " VR=" << vr << " HDR=" << hdr << '\n';
						if (errors)
							std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
					} else {
						++compiled;
					}
				}
			}
		}
	}
	std::cout << compiled << " runtime shader permutations compiled; " << failures << " failed.\n";
	return failures == 0 ? 0 : 1;
}
