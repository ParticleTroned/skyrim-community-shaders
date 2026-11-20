#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <array>
#include <bit>
#include <cmath>
#include <d3dcompiler.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using Microsoft::WRL::ComPtr;
	using UV = std::array<float, 2>;
	struct Cache
	{
		UV uv;
		UINT seed;
		float area;
	};
	static_assert(sizeof(Cache) == 16);
	using Result = std::array<Cache, 2>;
	constexpr UINT interlayer = 1, projectedGlint = 2;

	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Buffer> result, staging;
		ComPtr<ID3D11UnorderedAccessView> view;

		Fixture()
		{
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
				nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(Result);
			desc.StructureByteStride = sizeof(Cache);
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			Check(device->CreateBuffer(&desc, nullptr, &result));
			Util::SetResourceName(result.Get(), "GlintParallaxTest::Result");
			Check(device->CreateUnorderedAccessView(result.Get(), nullptr, &view));
			Util::SetResourceName(view.Get(), "GlintParallaxTest::Result UAV");
			desc.BindFlags = desc.MiscFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateBuffer(&desc, nullptr, &staging));
			Util::SetResourceName(staging.Get(), "GlintParallaxTest::Readback");
		}

		void CheckCache(ConstantBuffer& constants, const char* variant, unsigned caseIndex)
		{
			constants.Bind(context.Get());
			const UINT sentinel[4]{ std::bit_cast<UINT>(std::numeric_limits<float>::quiet_NaN()), 0, 0, 0 };
			context->ClearUnorderedAccessViewUint(view.Get(), sentinel);
			auto* uav = view.Get();
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->Dispatch(1, 1, 1);
			uav = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->CopyResource(staging.Get(), result.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			const Result values = *static_cast<const Result*>(mapped.pData);
			context->Unmap(staging.Get(), 0);
			const auto& actual = values[0];
			const auto& expected = values[1];
			bool match = actual.seed == expected.seed && std::isfinite(actual.area) &&
			             std::isfinite(expected.area) && std::abs(actual.area - expected.area) < 1e-8f;
			for (size_t axis = 0; axis < 2; ++axis)
				match &= std::isfinite(actual.uv[axis]) && std::isfinite(expected.uv[axis]) &&
				         std::abs(actual.uv[axis] - expected.uv[axis]) < 1e-5f;
			if (!match)
				throw std::runtime_error(std::string("Glint cache placement/footprint mismatch: ") + variant +
										 ", case " + std::to_string(caseIndex) + ", actual UV " + std::to_string(actual.uv[0]) + ", " +
										 std::to_string(actual.uv[1]) + ", expected UV " + std::to_string(expected.uv[0]) + ", " + std::to_string(expected.uv[1]));
		}
	};
}

int main()
{
	try {
		Fixture fixture;
		unsigned cases = 0;
		for (const char* variant : { "mesh", "projected", "LANDSCAPE", "LODLANDSCAPE", "no-parallax" }) {
			const std::string name = variant;
			const bool terrain = name == "LANDSCAPE" || name == "LODLANDSCAPE";
			const bool parallax = name != "no-parallax" && name != "LODLANDSCAPE";
			std::vector<D3D_SHADER_MACRO> defines;
			if (parallax)
				defines.push_back({ "EMAT", "1" });
			if (terrain)
				defines.push_back({ variant, "1" });
			if (name == "projected")
				defines.push_back({ "PROJECTED_UV", "1" });
			defines.push_back({ nullptr, nullptr });
			Util::CustomInclude includes("package/Shaders");
			ComPtr<ID3DBlob> code, errors;
			const auto hr = D3DCompileFromFile(GLINT_PARALLAX_TEST_SHADER, defines.data(), &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, &code, &errors);
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(hr);
			ComPtr<ID3D11ComputeShader> shader;
			Check(fixture.device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "GlintParallaxTest::%s", variant);
			fixture.context->CSSetShader(shader.Get(), nullptr, 0);
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			ConstantBuffer constants(fixture.device.Get(), reflection.Get(), "TestInput");
			for (const UV original : { UV{ 0.48f, 0.51f }, UV{ 0.52f, 0.49f } })
				for (float scale : { 1.0f, -2.0f })
					for (UINT flags : { 0u, interlayer, projectedGlint, interlayer | projectedGlint })
						for (float coverage : { 0.0f, 0.25f, 0.5f, 1.0f }) {
							const UV hit = parallax ? UV{ 0.5f, 0.5f } : original;
							constants.SetVariable("uv", hit);
							constants.SetVariable("uvOriginal", original);
							constants.SetVariable("uvOriginal_ddx", UV{ 0.004f * scale, 0.001f * scale });
							constants.SetVariable("uvOriginal_ddy", UV{ 0, 0.007f * scale });
							constants.SetVariable("HasInterlayer", UINT((flags & interlayer) != 0));
							constants.SetVariable("HasProjectedGlint", UINT((flags & projectedGlint) != 0));
							constants.SetVariable("projectedMaterialWeight", coverage);
							constants.SetVariable("ExpectedUV", !terrain && (flags & interlayer) ? original : hit);
							fixture.CheckCache(constants, variant, cases);
							++cases;
						}
			if (parallax) {
				constants.SetVariable("HasInterlayer", 0u);
				constants.SetVariable("ExpectedUV", UV{ 0.52f, 0.49f });
				constants.SetVariable("uv", UV{ 0.52f, 0.49f });
				fixture.CheckCache(constants, variant, cases);
				++cases;
				const float nan = std::numeric_limits<float>::quiet_NaN();
				const float infinity = std::numeric_limits<float>::infinity();
				for (const UV invalid : { UV{ nan, 0.5f }, UV{ 0.5f, nan }, UV{ infinity, 0.5f }, UV{ 0.5f, -infinity } }) {
					constants.SetVariable("uv", invalid);
					fixture.CheckCache(constants, variant, cases);
					++cases;
				}
			}
		}
		std::cout << cases << " glint placement, interlayer, footprint and invalid-hit cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
