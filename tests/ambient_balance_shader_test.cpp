#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	using Pixels = std::array<Pixel, 10>;

	struct Includes : ID3DInclude
	{
		std::array<Util::CustomInclude, 4> roots{
			Util::CustomInclude{ "package/Shaders" }, Util::CustomInclude{ "features/IBL/Shaders" },
			Util::CustomInclude{ "features/Dynamic Cubemaps/Shaders" }, Util::CustomInclude{ "features/Skylighting/Shaders" }
		};
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			for (auto& root : roots)
				if (SUCCEEDED(root.Open(type, name, parent, data, size)))
					return S_OK;
			return E_FAIL;
		}
		HRESULT Close(LPCVOID data) override { return roots.front().Close(data); }
	};

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		Texture(ID3D11Device* device, const char* name, UINT width, UINT flags, const Pixel* values = nullptr, bool cube = false)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = desc.MipLevels = desc.SampleDesc.Count = 1;
			desc.ArraySize = cube ? 6 : 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = flags;
			desc.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
			if (!flags) {
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			}
			std::array<D3D11_SUBRESOURCE_DATA, 6> initial{};
			for (auto& entry : initial)
				entry = { values, width * static_cast<UINT>(sizeof(Pixel)), 0 };
			Check(device->CreateTexture2D(&desc, values ? initial.data() : nullptr, resource.GetAddressOf()));
			Util::SetResourceName(resource.Get(), "AmbientTest::%s", name);
			if (flags & D3D11_BIND_SHADER_RESOURCE) {
				Check(device->CreateShaderResourceView(resource.Get(), nullptr, srv.GetAddressOf()));
				Util::SetResourceName(srv.Get(), "AmbientTest::%s SRV", name);
			}
			if (flags & D3D11_BIND_UNORDERED_ACCESS) {
				Check(device->CreateUnorderedAccessView(resource.Get(), nullptr, uav.GetAddressOf()));
				Util::SetResourceName(uav.Get(), "AmbientTest::%s UAV", name);
			}
		}
	};

	struct Fixture
	{
		ID3D11DeviceContext* context;
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11SamplerState> sampler;
		std::unique_ptr<ConstantBuffer> feature, shared, permutation;
		Texture output, staging, envSH, skySH, envCube, fullCube;
		static constexpr std::array<Pixel, 3> envCoefficients{ Pixel{ 0.4f, 0.01f, 0.02f, 0.03f }, Pixel{ 0.3f, 0.01f, 0.02f, 0.03f }, Pixel{ 0.2f, 0.01f, 0.02f, 0.03f } };
		static constexpr std::array<Pixel, 3> skyCoefficients{ Pixel{ 0.2f, 0.01f, 0.02f, 0.03f }, Pixel{ 0.4f, 0.01f, 0.02f, 0.03f }, Pixel{ 0.6f, 0.01f, 0.02f, 0.03f } };
		static constexpr Pixel envValue{ 0.1f, 0.2f, 0.3f, 1 };
		static constexpr Pixel fullValue{ 0.4f, 0.5f, 0.6f, 1 };

		Fixture(ID3D11Device* device, ID3D11DeviceContext* context, bool vr) :
			context(context),
			output(device, "Output", static_cast<UINT>(Pixels{}.size()), D3D11_BIND_UNORDERED_ACCESS), staging(device, "Readback", static_cast<UINT>(Pixels{}.size()), 0),
			envSH(device, "EnvironmentSH", 3, D3D11_BIND_SHADER_RESOURCE, envCoefficients.data()),
			skySH(device, "SkySH", 3, D3D11_BIND_SHADER_RESOURCE, skyCoefficients.data()),
			envCube(device, "Environment", 1, D3D11_BIND_SHADER_RESOURCE, &envValue, true),
			fullCube(device, "FullEnvironment", 1, D3D11_BIND_SHADER_RESOURCE, &fullValue, true)
		{
			Includes includes;
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "LIGHTING", "1" }, { "SKYLIGHTING", "1" }, { "IBL", "1" } };
			if (vr)
				defines.push_back({ "VR", "1" });
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> bytecode, errors;
			const auto compiled = D3DCompileFromFile(L"tests/ambient_balance.hlsl", defines.data(), &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, bytecode.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiled);
			Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "AmbientTest::CS");
			feature = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
			auto* balance = feature->reflection->GetVariableByName("SharedData::adaptiveBalanceSettings");
			D3D11_SHADER_VARIABLE_DESC balanceDesc{};
			D3D11_SHADER_TYPE_DESC ambientDesc{};
			Check(balance->GetDesc(&balanceDesc));
			Check(balance->GetType()->GetMemberTypeByName("ambientMult")->GetDesc(&ambientDesc));
			if (balanceDesc.Size != 48 || ambientDesc.Offset != 36)
				throw std::runtime_error("Adaptive Balance shader layout differs from the CPU buffer");
			shared = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::SharedData");
			permutation = std::make_unique<ConstantBuffer>(device, reflection.Get(), "Permutation::PerShader");
			D3D11_SAMPLER_DESC desc{};
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&desc, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "AmbientTest::Sampler");
			for (const auto* name : { "ambientGamma", "glowmapGamma" })
				feature->SetMember("SharedData::linearLightingSettings", name, 1.8f);
			feature->SetMember("SharedData::linearLightingSettings", "ambientMult", 0.65f);
			feature->SetMember("SharedData::linearLightingSettings", "glowmapMult", 0.7f);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "directionalLightMult", 1.0f);
			feature->SetMember("SharedData::iblSettings", "EnvIBLScale", 0.75f);
			feature->SetMember("SharedData::iblSettings", "SkyIBLScale", 1.5f);
			feature->SetMember("SharedData::iblSettings", "EnvIBLSaturation", 1.0f);
			feature->SetMember("SharedData::iblSettings", "SkyIBLSaturation", 1.0f);
			feature->SetMember("SharedData::iblSettings", "FogAmount", 0.4f);
			shared->SetVariable("SharedData::AmbientSHR", Pixel{ 0.3f, 0, 0, 0 });
			shared->SetVariable("SharedData::AmbientSHG", Pixel{ 0.4f, 0, 0, 0 });
			shared->SetVariable("SharedData::AmbientSHB", Pixel{ 0.5f, 0, 0, 0 });
		}

		Pixels Draw(float ambient)
		{
			context->ClearState();
			feature->SetMember("SharedData::adaptiveBalanceSettings", "ambientMult", ambient);
			feature->Bind(context);
			shared->Bind(context);
			permutation->Bind(context);
			ID3D11ShaderResourceView* spherical[]{ envSH.srv.Get(), skySH.srv.Get() };
			context->CSSetShaderResources(76, 2, spherical);
			ID3D11ShaderResourceView* cubes[]{ fullCube.srv.Get(), envCube.srv.Get() };
			context->CSSetShaderResources(30, 2, cubes);
			ID3D11SamplerState* sample = sampler.Get();
			context->CSSetSamplers(0, 1, &sample);
			ID3D11UnorderedAccessView* target = output.uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.resource.Get(), output.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.resource.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			Pixels pixels;
			std::memcpy(pixels.data(), mapped.pData, sizeof(pixels));
			context->Unmap(staging.resource.Get(), 0);
			return pixels;
		}
	};

	unsigned Verify(Fixture& fixture)
	{
		unsigned cases = 0;
		for (uint32_t scene : { 0u, 1u, 2u }) {
			fixture.permutation->SetVariable("Permutation::ExtraShaderDescriptor", scene);
			for (bool linear : { false, true }) {
				fixture.feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint32_t(linear));
				for (uint32_t interior : { 0u, 1u }) {
					fixture.shared->SetVariable("SharedData::InInterior", interior);
					for (uint32_t mode : { 0u, 1u, 2u, 3u }) {
						fixture.feature->SetMember("SharedData::iblSettings", "DALCMode", mode);
						for (float amount : { 0.0f, 0.5f, 1.0f }) {
							fixture.feature->SetMember("SharedData::iblSettings", "DALCAmount", amount);
							const auto baseline = fixture.Draw(1.0f);
							if (!interior) {
								for (size_t output = 0; output < baseline.size(); ++output)
									for (size_t channel = 0; channel < 3; ++channel)
										if (!(baseline[output][channel] > 0.0f))
											throw std::runtime_error("Ambient fixture must exercise nonzero lighting at output " + std::to_string(output));
							}
							for (float ambient : { 0.0f, 0.5f, 2.0f, 5.0f }) {
								const auto actual = fixture.Draw(ambient);
								for (size_t output = 0; output < actual.size(); ++output) {
									float scale = scene != 0 && (output < 4 || output >= 8) ? ambient : 1.0f;
									if ((output == 3 || output >= 8) && !(linear && scene != 0))
										scale = std::pow(scale, 1.6f);
									for (size_t channel = 0; channel < 3; ++channel) {
										const float fog = output == 9 ? (0.2f + 0.1f * static_cast<float>(channel)) * 0.6f : 0.0f;
										const float expected = (baseline[output][channel] - fog) * scale + fog;
										if (!std::isfinite(actual[output][channel]) ||
											std::abs(actual[output][channel] - expected) > 2e-4f * (1.0f + std::abs(expected))) {
											std::cerr << "scene=" << scene << " linear=" << linear << " interior=" << interior
													  << " mode=" << mode << " amount=" << amount << " ambient=" << ambient
													  << " output=" << output << " channel=" << channel << " actual="
													  << actual[output][channel] << " expected=" << expected << '\n';
											throw std::runtime_error("Ambient response differs from single application");
										}
									}
								}
								++cases;
							}
						}
					}
				}
			}
		}
		return cases;
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		unsigned cases = 0;
		for (bool vr : { false, true }) {
			Fixture fixture(device.Get(), context.Get(), vr);
			for (uint32_t ibl : { 0u, 1u }) {
				fixture.feature->SetMember("SharedData::iblSettings", "EnableIBL", ibl);
				cases += Verify(fixture);
			}
		}
		std::cout << cases << " D3D11 ambient cases passed (SE/AE and VR).\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
