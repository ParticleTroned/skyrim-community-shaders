#define NOMINMAX
#include "Features/Skin/TransmissionProfile.h"
#include "ShaderPackageIncludes.h"
#include "Utils/Finite.h"
#include "d3d11_shader_test.h"

#include <DirectXMath.h>
#include <array>
#include <cmath>
#include <iostream>
#include <string>

using float3 = DirectX::XMFLOAT3;
using float4 = DirectX::XMFLOAT4;

struct Skin
{
#include "skin_transmission_data.h"

	bool loaded = true, perGeometryCB = true, detailTexture = true;
	std::string resourceError;
	bool IsWetnessEnabled() const { return true; }
};
#include "skin_transmission_packing.h"

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using Microsoft::WRL::ComPtr;
	using Pixel = std::array<float, 4>;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	bool Close(float actual, float expected)
	{
		// GPU denormal flushing and exp approximation need an absolute floor.
		return Util::IsFinite(actual) && Util::IsFinite(expected) && std::abs(actual - expected) <= std::max(2e-37f, std::abs(expected) * 2e-5f);
	}

	struct Fixture
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11Texture2D> output, staging;
		ComPtr<ID3D11UnorderedAccessView> view;

		Fixture(ID3D11Device* device, bool vr, UINT flags)
		{
			auto includes = PackageIncludes::AllFeatures(".");
			std::vector<D3D_SHADER_MACRO> active{ { "PSHADER", "1" }, { "SNOW_COVER", "1" }, { "SKIN", "1" }, { "CS_SKIN", "1" } };
			if (vr)
				active.push_back({ "VR", "1" });
			active.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> code, errors;
			const auto result = D3DCompileFromFile(L"tests/skin_transmission.hlsl", active.data(), &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | flags, 0, &code, &errors);
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(result);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "SkinTransmissionTest::CS");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			D3D11_SHADER_VARIABLE_DESC skin{}, snow{};
			Check(reflection->GetVariableByName("SharedData::skinData")->GetDesc(&skin));
			Check(reflection->GetVariableByName("SharedData::snowCoverSettings")->GetDesc(&snow));
			Require(skin.Size == 128 && snow.StartOffset == skin.StartOffset + skin.Size, "Skin changed the downstream feature layout");
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 4;
			desc.Height = 3;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture2D(&desc, nullptr, &output));
			Util::SetResourceName(output.Get(), "SkinTransmissionTest::Output");
			Check(device->CreateUnorderedAccessView(output.Get(), nullptr, &view));
			Util::SetResourceName(view.Get(), "SkinTransmissionTest::Output UAV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, &staging));
			Util::SetResourceName(staging.Get(), "SkinTransmissionTest::Readback");
		}

		std::array<Pixel, 12> Draw(ID3D11DeviceContext* context, ConstantBuffer& feature, ConstantBuffer& geometry)
		{
			feature.Bind(context);
			geometry.Bind(context);
			auto* target = view.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<Pixel, 12> pixels{};
			for (unsigned row = 0; row < 3; ++row)
				std::memcpy(pixels.data() + row * 4, static_cast<const std::byte*>(mapped.pData) + row * mapped.RowPitch, 4 * sizeof(Pixel));
			context->Unmap(staging.Get(), 0);
			return pixels;
		}
	};

	void Run(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, UINT flags)
	{
		Fixture fixture(device, vr, flags);
		ConstantBuffer feature(device, fixture.reflection.Get(), "SharedData::FeatureData");
		ConstantBuffer geometry(device, fixture.reflection.Get(), "Skin::SkinPerGeometry");
		Skin skin;
		// Reverse the sweep too so cache refresh cannot depend on monotonic edits.
		for (unsigned step = 0; step <= 200; ++step) {
			skin.settings.sssWidth = std::max(.001f, float(step <= 100 ? step : 200 - step) / 100);
			const auto packed = skin.GetCommonBufferData();
			const auto repeated = skin.GetCommonBufferData();
			Require(std::memcmp(&packed, &repeated, sizeof(packed)) == 0, "Unchanged settings changed the packed profile");
			feature.SetVariable("SharedData::skinData", packed);
			for (float map : { 0.0f, 1.0f }) {
				geometry.SetVariable("Skin::skinMaterialFlags", Pixel{ 1 - map, 0, 0, map });
				const auto pixels = fixture.Draw(context, feature, geometry);
				const Pixel cpu{ packed.noMapTransmissionProfile.x, packed.noMapTransmissionProfile.y, packed.noMapTransmissionProfile.z, 1 };
				for (unsigned channel = 0; channel < 3; ++channel) {
					Require(Close(cpu[channel], pixels[4][channel]), "CPU unit-thickness profile diverged from mapped HLSL");
					for (unsigned pixel = 0; pixel < 4; ++pixel)
						Require(Close(pixels[pixel][channel], pixels[(map > 0 ? 8 : 4) + pixel][channel]), "Thickness resource selection changed transmission");
				}
				if (map > 0)
					Require(pixels[0][0] > .99f, "Mapped zero thickness incorrectly used the no-map constant");
			}
			if (skin.settings.sssWidth >= .5f)
				Require(packed.noMapTransmissionProfile.x > 0, "Supported thick profiles must not be replaced with zero");
		}
		for (unsigned gate = 0; gate < 6; ++gate) {
			skin = {};
			skin.settings.sssWidth = .5f;
			Require(skin.GetCommonBufferData().noMapTransmissionProfile.x > 0, "Active transmission did not seed the cache");
			skin.settings.sssWidth = 1;
			if (gate == 0)
				skin.settings.EnableSkin = false;
			if (gate == 1)
				skin.settings.UseSSS = false;
			if (gate == 2)
				skin.settings.Translucency = 0;
			if (gate == 3)
				skin.loaded = false;
			if (gate == 4)
				skin.resourceError = "unavailable";
			if (gate == 5)
				skin.perGeometryCB = false;
			const auto packed = skin.GetCommonBufferData();
			Require(packed.noMapTransmissionProfile.x == 0 && packed.noMapTransmissionProfile.y == 0 && packed.noMapTransmissionProfile.z == 0, "Disabled transmission packed a cached profile");
			feature.SetVariable("SharedData::skinData", packed);
			geometry.SetVariable("Skin::skinMaterialFlags", Pixel{ 0, 0, 0, 0 });
			const auto pixels = fixture.Draw(context, feature, geometry);
			for (unsigned channel = 0; channel < 3; ++channel)
				Require(pixels[0][channel] == 0, "Disabled transmission reached the shader output");
			skin = {};
			skin.settings.sssWidth = 1;
			feature.SetVariable("SharedData::skinData", skin.GetCommonBufferData());
			const auto reenabled = fixture.Draw(context, feature, geometry);
			for (unsigned channel = 0; channel < 3; ++channel)
				Require(Close(reenabled[0][channel], reenabled[4][channel]), "Reenabled transmission retained a stale profile");
		}
		skin = {};
		skin.settings.sssWidth = 1;
		skin.settings.SSSAmount = 0;
		const auto packed = skin.GetCommonBufferData();
		Require(packed.noMapTransmissionProfile.x > 0, "Diffusion strength suppressed direct transmission");
		feature.SetVariable("SharedData::skinData", packed);
		geometry.SetVariable("Skin::skinMaterialFlags", Pixel{ 1, 0, 0, -1 });
		Require(fixture.Draw(context, feature, geometry)[0][0] == 0, "Material-disable sentinel lost its effect");
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
		for (bool vr : { false, true })
			for (UINT flags : { 0u, UINT(D3DCOMPILE_PARTIAL_PRECISION), UINT(D3DCOMPILE_AVOID_FLOW_CONTROL), UINT(D3DCOMPILE_PARTIAL_PRECISION | D3DCOMPILE_AVOID_FLOW_CONTROL) })
				Run(device.Get(), context.Get(), vr, flags);
		std::cout << "Skin packing, cache refresh, map selection and WARP profile parity passed (SE/AE, VR, four release flag policies)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
