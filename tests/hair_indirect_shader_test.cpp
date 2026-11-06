#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	struct Geometry
	{
		Pixel tangent, normal, view;
	};
	constexpr std::array geometries{
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 } },
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 0, -1, 0 } },
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 1e-5f, 1, 1e-5f } },
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 0.4f, 0.3f, 1 } },
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 0, 1, -0.15f } },
		Geometry{ { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, 1 } },
		Geometry{ { 1, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },
		Geometry{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 } },
		Geometry{ { 0, 0, 1 }, { 1, 0, 0 }, { 0.12f, 0, 1 } },
		Geometry{ { 1, 1, 0 }, { 0, 0, 1 }, { 1, 1, 0 } },
		Geometry{ { 1, 1, 0 }, { 0, 0, 1 }, { 0.3f, -0.2f, 1 } },
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 0, 1, -0.0011f } },
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 0, 1, -0.0009f } },
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 0.0009f, 1, 0 } },
		Geometry{ { 1, 1, 0 }, { 0, 0, 1 }, { 1, 1, 0.0001f } },
		Geometry{ { 1, 1, 0 }, { 0, 0, 1 }, { 1, 1, -0.0001f } },
		Geometry{ { 1, 2, 3 }, { -2, 1, 0 }, { 1, 2, 3 } },
		Geometry{ { 1, 2, 3 }, { -2, 1, 0 }, { 1.0001f, 1.99995f, 3 } },
		Geometry{ { 0, 1, 0 }, { 0, 0, 1 }, { 0, 1, -1e-7f } },
		Geometry{ { 1, 2, 3 }, { -2, 1, 0 }, { -1, -2, -3 } }
	};
	using Samples = std::array<Pixel, geometries.size()>;
	constexpr UINT rows = 7;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct Includes : ID3DInclude
	{
		Util::CustomInclude package{ "package/Shaders" };
		Util::CustomInclude hair{ "features/Hair Specular/Shaders" };
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			return std::string_view(name).starts_with("Hair/") ?
			           hair.Open(type, name, parent, data, size) :
			           package.Open(type, name, parent, data, size);
		}
		HRESULT Close(LPCVOID data) override { return package.Close(data); }
	};

	struct Fixture
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11Texture2D> output, staging, shiftTexture;
		ComPtr<ID3D11UnorderedAccessView> uav;
		ComPtr<ID3D11ShaderResourceView> shiftSrv;
		ComPtr<ID3D11SamplerState> sampler;
		std::unique_ptr<ConstantBuffer> settings, inputs;
		UINT shiftSlot;

		Fixture(ID3D11Device* device, bool vr)
		{
			Includes includes;
			const auto sampleCount = std::to_string(geometries.size());
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "HAIR", "1" }, { "CS_HAIR", "1" }, { "SAMPLE_COUNT", sampleCount.c_str() } };
			if (vr)
				defines.push_back({ "VR", "1" });
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> code, errors;
			const auto result = D3DCompileFromFile(HAIR_INDIRECT_TEST_SHADER, defines.data(), &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, code.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(result);
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			D3D11_SHADER_INPUT_BIND_DESC shiftBinding{};
			Check(reflection->GetResourceBindingDescByName("Hair::TexTangentShift", &shiftBinding));
			shiftSlot = shiftBinding.BindPoint;
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "HairIndirectTest::CS");
			settings = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
			settings->SetMember("SharedData::hairSpecularSettings", "SpecularIndirectMult", 1.03f);
			settings->SetMember("SharedData::hairSpecularSettings", "DiffuseIndirectMult", 1.16f);
			inputs = std::make_unique<ConstantBuffer>(device, reflection.Get(), "Samples");
			inputs->SetVariable("Geometries", geometries);
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = static_cast<UINT>(geometries.size());
			desc.Height = rows;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture2D(&desc, nullptr, output.GetAddressOf()));
			Util::SetResourceName(output.Get(), "HairIndirectTest::Output");
			Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
			Util::SetResourceName(uav.Get(), "HairIndirectTest::Output UAV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "HairIndirectTest::Readback");
			desc.Width = desc.Height = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.CPUAccessFlags = 0;
			Check(device->CreateTexture2D(&desc, nullptr, shiftTexture.GetAddressOf()));
			Util::SetResourceName(shiftTexture.Get(), "HairIndirectTest::TangentShift");
			Check(device->CreateShaderResourceView(shiftTexture.Get(), nullptr, shiftSrv.GetAddressOf()));
			Util::SetResourceName(shiftSrv.Get(), "HairIndirectTest::TangentShift SRV");
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "HairIndirectTest::Sampler");
		}

		void Run(ID3D11DeviceContext* context, uint32_t enabled, uint32_t mode, bool shifted, float shift)
		{
			context->ClearState();
			settings->SetMember("SharedData::hairSpecularSettings", "Enabled", enabled);
			settings->SetMember("SharedData::hairSpecularSettings", "HairMode", mode);
			settings->SetMember("SharedData::hairSpecularSettings", "EnableTangentShift", uint32_t(shifted));
			settings->Bind(context);
			inputs->Bind(context);
			context->UpdateSubresource(shiftTexture.Get(), 0, nullptr, &shift, sizeof(shift), 0);
			ID3D11ShaderResourceView* srv = shiftSrv.Get();
			ID3D11SamplerState* sampling = sampler.Get();
			ID3D11UnorderedAccessView* target = uav.Get();
			context->CSSetShaderResources(shiftSlot, 1, &srv);
			context->CSSetSamplers(0, 1, &sampling);
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<Samples, rows> pixels;
			for (UINT y = 0; y < rows; ++y)
				std::memcpy(pixels[y].data(), static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch, sizeof(Samples));
			context->Unmap(staging.Get(), 0);
			for (const auto& row : pixels)
				for (const auto& pixel : row)
					for (float component : pixel)
						Require(std::isfinite(component), "Hair indirect result must remain finite at frontal and axial views");
			for (size_t x = 0; x < geometries.size(); ++x) {
				for (UINT y : { 0u, 2u })
					for (size_t channel = 0; channel < 3; ++channel)
						Require(std::abs(pixels[y][x][channel] - pixels[y + 1][x][channel]) <= 2e-5f * std::max(1.0f, std::abs(pixels[y + 1][x][channel])),
							"Production indirect lobes differ from the strand-tangent reference");
				for (UINT y : { 4u, 6u }) {
					const auto& normal = pixels[y][x];
					Require(std::abs(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2] - 1) < 2e-5f,
						"Hair ambient and shifted view normals must have unit length");
				}
				Require(std::abs(pixels[4][x][3]) < 2e-5f, "Projected hair normal must be perpendicular to the shifted strand");
				Require(std::abs(pixels[6][x][3]) < 2e-3f, "Non-axial hair normals must lie in the strand/view plane");
				const auto& view = geometries[x].view;
				Require(pixels[4][x][0] * view[0] + pixels[4][x][1] * view[1] + pixels[4][x][2] * view[2] >= -2e-5f,
					"Projected hair normal must face the view outside the axial limit");
				Require(pixels[5][x][3] < 2e-5f, "Hair lobe selection must retain the ambient/wetness context");
			}
		}
	};
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		unsigned cases = 0;
		for (bool vr : { false, true }) {
			Fixture fixture(device.Get(), vr);
			for (uint32_t enabled : { 0u, 1u })
				for (uint32_t mode : { 0u, 1u })
					for (float shift : { -1.0f, 0.0f, 0.35f, 0.5f, 0.62f, 1.0f }) {
						fixture.Run(context.Get(), enabled, mode, shift >= 0, std::max(0.0f, shift));
						cases += static_cast<unsigned>(geometries.size());
					}
		}
		std::cout << "PASS: " << cases << " hair indirect cases (SE/AE/VR, both models, disabled paths, frontal/axial views, tangent shift)\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
