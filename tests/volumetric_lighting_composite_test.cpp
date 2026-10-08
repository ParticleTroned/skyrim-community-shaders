#define NOMINMAX
#include "Features/VolumetricLightingTuning.h"
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <d3d11.h>
#include <d3d11shader.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using uint = uint32_t;
	struct alignas(16) float4
	{
		float x, y, z, w;
	};
#include "shared_data_under_test.h"
#include "volumetric_lighting_runtime_fixture.h"

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	using D3D11ShaderTest::Check;
	using Buffer = D3D11ShaderTest::ConstantBuffer;

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;

		Texture(ID3D11Device* device, const char* name, UINT flags, const float4* pixels = nullptr)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 2;
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = flags;
			if (!flags) {
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			}
			D3D11_SUBRESOURCE_DATA initial{ pixels, 2 * sizeof(float4), 0 };
			Check(device->CreateTexture2D(&desc, pixels ? &initial : nullptr, resource.GetAddressOf()));
			Util::SetResourceName(resource.Get(), "VolumetricTest::%s", name);
			if (flags & D3D11_BIND_SHADER_RESOURCE) {
				Check(device->CreateShaderResourceView(resource.Get(), nullptr, srv.GetAddressOf()));
				Util::SetResourceName(srv.Get(), "VolumetricTest::%s SRV", name);
			}
			if (flags & D3D11_BIND_RENDER_TARGET) {
				Check(device->CreateRenderTargetView(resource.Get(), nullptr, rtv.GetAddressOf()));
				Util::SetResourceName(rtv.Get(), "VolumetricTest::%s RTV", name);
			}
		}
	};

	struct Fixture
	{
		ID3D11DeviceContext* context;
		bool volumetric, lensFlare;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11SamplerState> sampler;
		std::unique_ptr<Buffer> shared, geometry, frame, feature;
		Texture output, staging, shafts, flare;
		static constexpr float4 shaftPixels[2]{ { 0.5f, 0, 0, 0 }, { 0.5f, 0, 0, 0 } };
		static constexpr float4 flarePixels[2]{ { 0.1f, 0.2f, 0.3f, 0 }, { 0.1f, 0.2f, 0.3f, 0 } };

		Fixture(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool vl, bool lf) :
			context(context), volumetric(vl), lensFlare(lf),
			output(device, "Output", D3D11_BIND_RENDER_TARGET), staging(device, "Readback", 0),
			shafts(device, "Shafts", D3D11_BIND_SHADER_RESOURCE, shaftPixels),
			flare(device, "Flare", D3D11_BIND_SHADER_RESOURCE, flarePixels)
		{
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" } };
			if (vr)
				defines.push_back({ "VR", "1" });
			if (vl)
				defines.push_back({ "VOLUMETRIC_LIGHTING", "1" });
			if (lf)
				defines.push_back({ "LENS_FLARE", "1" });
			defines.push_back({ nullptr, nullptr });
			Util::CustomInclude includes("package/Shaders");
			ComPtr<ID3DBlob> bytecode, errors;
			const auto compiled = D3DCompileFromFile(L"package/Shaders/ISCompositeLensFlareVolumetricLighting.hlsl",
				defines.data(), &includes, "main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, bytecode.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiled);
			Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, pixel.GetAddressOf()));
			Util::SetResourceName(pixel.Get(), "VolumetricTest::CompositePS");
			constexpr char vs[] = "struct V { float4 p:SV_POSITION; float2 uv:TEXCOORD0; }; V main(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }";
			ComPtr<ID3DBlob> vertexCode;
			Check(D3DCompile(vs, sizeof(vs) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertexCode.GetAddressOf(), nullptr));
			Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
			Util::SetResourceName(vertex.Get(), "VolumetricTest::FullscreenVS");
			D3D11_SAMPLER_DESC desc{};
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&desc, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "VolumetricTest::Sampler");
			feature = std::make_unique<Buffer>(device, reflection.Get(), "SharedData::FeatureData");
			if (vl) {
				shared = std::make_unique<Buffer>(device, reflection.Get(), "SharedData::SharedData");
				Require(shared->bytes.size() == sizeof(SharedDataCB), "SharedDataCB size mismatch");
				Require(shared->Offset("SharedData::VolumetricLightingSaturation") == offsetof(SharedDataCB, VolumetricLightingSaturation), "Saturation offset mismatch");
				Require(shared->Offset("SharedData::VolumetricLightingOpacity") == offsetof(SharedDataCB, VolumetricLightingOpacity), "Opacity offset mismatch");
				Require(shared->Offset("SharedData::VolumetricLightingCustomColor") == offsetof(SharedDataCB, VolumetricLightingCustomColor), "Custom color offset mismatch");
				geometry = std::make_unique<Buffer>(device, reflection.Get(), "PerGeometry");
				frame = std::make_unique<Buffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
				frame->SetVariable("FrameBuffer::DynamicResolutionParams1", float4{ 1, 1, 1, 1 });
				frame->SetVariable("FrameBuffer::DynamicResolutionParams2", float4{ 1, 1, 1, 1 });
			}
		}

		float4 Draw(float4 authored, float saturation = 1, float4 custom = {}, float opacity = 1, bool linear = false)
		{
			SharedDataCB data{};
			data.VolumetricLightingSaturation = saturation;
			data.VolumetricLightingOpacity = opacity;
			data.VolumetricLightingCustomColor = custom;
			return Draw(authored, data, linear);
		}

		float4 Draw(float4 authored, const SharedDataCB& data, bool linear = false)
		{
			context->ClearState();
			feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint(linear));
			feature->SetMember("SharedData::linearLightingSettings", "vlGamma", 1.0f);
			feature->Bind(context, D3D11ShaderTest::Stage::Pixel);
			if (volumetric) {
				std::memcpy(shared->bytes.data(), &data, sizeof(data));
				shared->Bind(context, D3D11ShaderTest::Stage::Pixel);
				geometry->SetVariable("VolumetricLightingColor", authored);
				geometry->Bind(context, D3D11ShaderTest::Stage::Pixel);
				frame->Bind(context, D3D11ShaderTest::Stage::Pixel);
			}
			ID3D11ShaderResourceView* sources[]{ shafts.srv.Get(), flare.srv.Get() };
			context->PSSetShaderResources(0, 2, sources);
			ID3D11SamplerState* samplers[]{ sampler.Get(), sampler.Get() };
			context->PSSetSamplers(0, 2, samplers);
			ID3D11RenderTargetView* target = output.rtv.Get();
			context->OMSetRenderTargets(1, &target, nullptr);
			D3D11_VIEWPORT viewport{ 0, 0, 2, 1, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			context->Draw(3, 0);
			context->ClearState();
			context->CopyResource(staging.resource.Get(), output.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.resource.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<float4, 2> result;
			std::memcpy(result.data(), mapped.pData, sizeof(result));
			context->Unmap(staging.resource.Get(), 0);
			Require(std::abs(result[0].x - result[1].x) < 1e-5f && std::abs(result[0].y - result[1].y) < 1e-5f && std::abs(result[0].z - result[1].z) < 1e-5f, "Stereo eyes differ");
			return result[0];
		}
	};

	void Expect(float4 actual, float4 expected, const char* message)
	{
		if (!std::isfinite(actual.x) || !std::isfinite(actual.y) || !std::isfinite(actual.z) ||
			std::abs(actual.x - expected.x) >= 2e-4f || std::abs(actual.y - expected.y) >= 2e-4f || std::abs(actual.z - expected.z) >= 2e-4f) {
			std::cerr << message << ": actual " << actual.x << ',' << actual.y << ',' << actual.z
					  << " expected " << expected.x << ',' << expected.y << ',' << expected.z << '\n';
			throw std::runtime_error(message);
		}
	}

	void Verify(Fixture& fixture)
	{
		const float4 authored{ 2.0f, 0.6f, 0.2f, 0 };
		const float4 lens = fixture.lensFlare ? Fixture::flarePixels[0] : float4{};
		const auto addLens = [&](float4 color) { return float4{ color.x + lens.x, color.y + lens.y, color.z + lens.z, 0 }; };
		if (!fixture.volumetric) {
			Expect(fixture.Draw(authored, 0, { 0, 1, 0, 1 }, 0), lens, "Godray tuning affected lens flare");
			return;
		}
		Expect(fixture.Draw(authored), addLens({ 1, 0.3f, 0.1f, 0 }), "Neutral settings changed weather color");
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 0 }), addLens({ 1, 0.3f, 0.1f, 0 }), "Zero contribution changed weather color");
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 1 }), addLens({ 0, 0.5f, 0, 0 }), "Custom color did not reach final composite");
		Expect(fixture.Draw({ 65504, 65504, 65504, 0 }, 1, { 0.001f, 0.002f, 0.003f, 1 }),
			addLens({ 0.0005f, 0.001f, 0.0015f, 0 }), "Full custom replacement lost dark channels against HDR weather");
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 0.5f }), addLens({ 0.5f, 0.4f, 0.05f, 0 }), "Partial contribution failed");
		const float grey = (2.0f * 0.2125f + 0.6f * 0.7154f + 0.2f * 0.0721f) * 0.5f;
		Expect(fixture.Draw(authored, 0), addLens({ grey, grey, grey, 0 }), "Saturation zero did not desaturate shafts");
		for (float saturation : { 2.0f, 4.0f }) {
			const auto result = fixture.Draw(authored, saturation);
			const float red = result.x - lens.x, green = result.y - lens.y, blue = result.z - lens.z;
			Require(red > 1.0f && green < 0.3f && blue < 0.1f, "HDR saturation remained neutral");
			Require(std::abs(red * 0.2125f + green * 0.7154f + blue * 0.0721f - grey) < 2e-4f, "Saturation changed brightness");
		}
		for (const auto hdr : { float4{ 60000, 30000, 10000, 0 }, float4{ 65504, 65504, 65504, 0 } }) {
			const auto result = fixture.Draw(hdr, 4);
			const float red = result.x - lens.x, green = result.y - lens.y, blue = result.z - lens.z;
			const float expectedLuminance = (hdr.x * 0.2125f + hdr.y * 0.7154f + hdr.z * 0.0721f) * 0.5f;
			Require(std::abs(red * 0.2125f + green * 0.7154f + blue * 0.0721f - expectedLuminance) < expectedLuminance * 1e-5f, "HDR range limiting changed saturation brightness");
			Require(red <= 32752 && green <= 32752 && blue <= 32752, "Saturation exceeded half-float color range");
		}
		Expect(fixture.Draw(authored, 0, { 0, 1, 0, 1 }), addLens({ 0, 0.5f, 0, 0 }), "Saturation altered full custom replacement");
		Expect(fixture.Draw(authored, 4, { 0, 1, 0, 1 }, 0), lens, "Zero opacity retained shafts or removed flare");
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 1 }, 2), addLens({ 0, 0.75f, 0, 0 }), "Opacity curve changed");
		const float nan = std::numeric_limits<float>::quiet_NaN();
		Expect(fixture.Draw(authored, nan, { 0, 1, 0, nan }), addLens({ 1, 0.3f, 0.1f, 0 }), "Invalid tuning did not fall back to neutral");
		Expect(fixture.Draw(authored, 1, {}, nan), addLens({ 1, 0.3f, 0.1f, 0 }), "Invalid opacity did not fall back to neutral");
		Expect(fixture.Draw({ nan, -1, 0.2f, 0 }), addLens({ 0, 0, 0.1f, 0 }), "Neutral tuning retained invalid authored color");
		Expect(fixture.Draw({}, 4), lens, "Black authored color became nonfinite");
		const float4 linearLens = fixture.lensFlare ? float4{ std::pow(0.1f, 1.6f), std::pow(0.2f, 1.6f), std::pow(0.3f, 1.6f), 0 } : float4{};
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 1 }, 1, true), { linearLens.x, linearLens.y + 0.5f, linearLens.z, 0 }, "Linear lighting lost custom color or changed flare conversion");

		auto& feature = Runtime::globals::features::volumetricLighting;
		auto& state = Runtime::globals::replacementState;
		feature = {};
		state = {};
		Runtime::globals::state = &state;
		Runtime::LocationContext::interior = false;
		Runtime::LocationContext::sun = false;
		const auto expectRuntime = [&](float4 expected, const char* message, bool inWorld = true) {
			Expect(fixture.Draw(authored, Runtime::Upload(inWorld)), addLens(expected), message);
		};
		const float4 neutral{ 1, 0.3f, 0.1f, 0 };
		feature.settings.ExteriorGodrays.Saturation = 0;
		expectRuntime({ grey, grey, grey, 0 }, "Exterior saturation did not reach composite");
		feature.settings.ExteriorGodrays = { .Opacity = 2, .CustomColorContribution = 1, .CustomColorRed = 0, .CustomColorBlue = 0 };
		expectRuntime({ 0, 0.75f, 0, 0 }, "Exterior color and opacity did not reach composite");
		feature.settings.InteriorGodrays = { .CustomColorContribution = 1, .CustomColorGreen = 0, .CustomColorBlue = 0 };
		Runtime::LocationContext::interior = true;
		expectRuntime(neutral, "Sunless interior retained exterior tuning");
		Runtime::LocationContext::sun = true;
		expectRuntime({ 0.5f, 0, 0, 0 }, "Sunlit interior did not select its own profile");
		feature.settings.InteriorEnabled = false;
		expectRuntime(neutral, "Disabled interior retained tuning");
		Runtime::LocationContext::interior = false;
		feature.settings.ExteriorEnabled = false;
		expectRuntime(neutral, "Disabled exterior retained tuning");
		feature.settings.ExteriorEnabled = true;
		feature.loaded = false;
		expectRuntime(neutral, "Unloaded feature retained tuning");
		feature.loaded = true;
		state.enablePShaders = false;
		expectRuntime(neutral, "Disabled pixel replacement retained tuning");
		state.enablePShaders = true;
		state.enabledClasses[0] = false;
		expectRuntime(neutral, "Disabled ImageSpace class retained tuning");
		state.enabledClasses[0] = true;
		Runtime::globals::state = nullptr;
		expectRuntime(neutral, "Missing renderer state retained tuning");
		Runtime::globals::state = &state;
		expectRuntime(neutral, "Non-world pass retained tuning", false);
		feature.settings.ExteriorGodrays.Opacity = nan;
		feature.settings.ExteriorGodrays.CustomColorGreen = nan;
		expectRuntime({ 0, 0.5f, 0, 0 }, "Profile sanitization did not reach composite");
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		for (bool vr : { false, true }) {
			for (const auto mode : { std::array{ true, false }, std::array{ false, true }, std::array{ true, true } }) {
				Fixture fixture(device.Get(), context.Get(), vr, mode[0], mode[1]);
				Verify(fixture);
				std::cout << (vr ? "VR" : "SE/AE") << " volumetric=" << mode[0] << " flare=" << mode[1] << " passed\n";
			}
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
