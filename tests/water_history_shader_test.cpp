#define NOMINMAX
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	constexpr UINT width = 8;
	using Pixels = std::array<Pixel, width>;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource, staging;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;
		bool half;

		Texture(ID3D11Device* device, const char* name, bool half = false) : half(half)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = half ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			Check(device->CreateTexture2D(&desc, nullptr, &resource));
			Util::SetResourceName(resource.Get(), "WaterHistoryTest::%s", name);
			Check(device->CreateShaderResourceView(resource.Get(), nullptr, &srv));
			Util::SetResourceName(srv.Get(), "WaterHistoryTest::%s SRV", name);
			Check(device->CreateRenderTargetView(resource.Get(), nullptr, &rtv));
			Util::SetResourceName(rtv.Get(), "WaterHistoryTest::%s RTV", name);
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, &staging));
			Util::SetResourceName(staging.Get(), "WaterHistoryTest::%s Readback", name);
		}

		Pixels Read(ID3D11DeviceContext* context)
		{
			context->CopyResource(staging.Get(), resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			Pixels pixels{};
			if (half) {
				const auto* values = static_cast<const DirectX::PackedVector::HALF*>(mapped.pData);
				for (UINT x = 0; x < width; ++x)
					for (UINT c = 0; c < 4; ++c)
						pixels[x][c] = DirectX::PackedVector::XMConvertHalfToFloat(values[4 * x + c]);
			} else {
				std::memcpy(pixels.data(), mapped.pData, sizeof(pixels));
			}
			context->Unmap(staging.Get(), 0);
			return pixels;
		}
	};

	struct Fixture
	{
		ID3D11DeviceContext* context;
		bool vr, reversed;
		ComPtr<ID3D11PixelShader> shader;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11SamplerState> sampler;
		std::unique_ptr<ConstantBuffer> frame, geometry;
		Texture source, motion, depth, mask, color, historyA, historyB;

		Fixture(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool reversed, const wchar_t* path) :
			context(context), vr(vr), reversed(reversed),
			source(device, "Source"), motion(device, "Motion"), depth(device, "Depth"), mask(device, "Mask"),
			color(device, "Color"), historyA(device, "HistoryA", true), historyB(device, "HistoryB", true)
		{
			Util::CustomInclude includes{ "package/Shaders" };
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" } };
			if (vr)
				defines.push_back({ "VR", "1" });
			if (reversed)
				defines.push_back({ "REVERSE_Z", "1" });
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> code, errors;
			const auto flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
			const auto compiled = D3DCompileFromFile(path, defines.data(), &includes, "main", "ps_5_0", flags, 0, &code, &errors);
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiled);
			Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "WaterHistoryTest::PS");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			frame = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			frame->SetVariable("FrameBuffer::DynamicResolutionParams1", Pixel{ 1, 1, 1, 1 });
			frame->SetVariable("FrameBuffer::DynamicResolutionParams2", Pixel{ 1, 1, 1, 1 });
			geometry = std::make_unique<ConstantBuffer>(device, reflection.Get(), "PerGeometry");
			constexpr char triangle[] = R"(
struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
    Output o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
})";
			Check(D3DCompile(triangle, sizeof(triangle), nullptr, nullptr, nullptr, "main", "vs_5_0", flags, 0, &code, &errors));
			Check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex));
			Util::SetResourceName(vertex.Get(), "WaterHistoryTest::VS");
			D3D11_SAMPLER_DESC desc{};
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&desc, &sampler));
			Util::SetResourceName(sampler.Get(), "WaterHistoryTest::Sampler");
		}

		void Draw(Texture& previous, Texture& next, const Pixels& colors, float coverage, float shift, bool alternating, bool menu = false)
		{
			context->ClearState();
			Pixels masks{}, velocities{}, depths{};
			for (UINT x = 0; x < width; ++x) {
				masks[x][2] = alternating && x % 2 ? 0.0f : 1e-4f + coverage * 9e-4f;
				velocities[x][0] = shift / width * (vr ? 2.0f : 1.0f);
				depths[x][0] = reversed ? 0.0f : 1.0f;
			}
			const std::array<Texture*, 4> inputs{ &source, &motion, &depth, &mask };
			const std::array<const Pixels*, 4> data{ &colors, &velocities, &depths, &masks };
			for (UINT i = 0; i < inputs.size(); ++i)
				context->UpdateSubresource(inputs[i]->resource.Get(), 0, nullptr, data[i]->data(), sizeof(Pixels), 0);
			const float clear[4]{};
			context->ClearRenderTargetView(next.rtv.Get(), clear);
			context->ClearRenderTargetView(color.rtv.Get(), clear);
			frame->Bind(context, D3D11ShaderTest::Stage::Pixel);
			geometry->SetVariable("NearFar_Menu_DistanceFactor", Pixel{ 1, 1000, menu ? 1.0f : 0.0f, 1 });
			geometry->Bind(context, D3D11ShaderTest::Stage::Pixel);
			ID3D11ShaderResourceView* resources[]{ source.srv.Get(), previous.srv.Get(), motion.srv.Get(), depth.srv.Get(), mask.srv.Get() };
			ID3D11SamplerState* samplers[]{ sampler.Get(), sampler.Get(), sampler.Get(), sampler.Get(), sampler.Get() };
			ID3D11RenderTargetView* targets[]{ color.rtv.Get(), next.rtv.Get() };
			context->PSSetShaderResources(0, 5, resources);
			context->PSSetSamplers(0, 5, samplers);
			context->OMSetRenderTargets(2, targets, nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(shader.Get(), nullptr, 0);
			const D3D11_VIEWPORT viewport{ 0, 0, float(width), 1, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->Draw(3, 0);
			context->ClearState();
		}
	};

	unsigned Verify(Fixture& fixture)
	{
		Pixels colors{};
		for (UINT x = 0; x < width; ++x)
			colors[x] = x < width / 2 ? Pixel{ .25f, .5f, 2, 1 } : Pixel{ 1, .1f, 8, 1 };
		unsigned cases = 0;
		const float clear[4]{};
		for (float coverage : { 1.0f, .5f, .01f }) {
			fixture.context->ClearRenderTargetView(fixture.historyB.rtv.Get(), clear);
			fixture.Draw(fixture.historyB, fixture.historyA, colors, coverage, 0, true);
			const auto encoded = fixture.historyA.Read(fixture.context);
			for (UINT x = 0; x < width; ++x) {
				if (std::abs(encoded[x][3] - (x % 2 ? 0 : coverage)) >= .001f * coverage) {
					std::cerr << "vr=" << fixture.vr << " reversed=" << fixture.reversed << " x=" << x
							  << " coverage=" << coverage << " actual=" << encoded[x][3] << '\n';
					throw std::runtime_error("History coverage or transparent clear changed");
				}
				if (x % 2)
					Require(encoded[x] == Pixel{}, "Discarded pixels retained history");
			}
			for (float shift : { 0.0f, .25f, .5f, .75f, 1.0f }) {
				fixture.Draw(fixture.historyA, fixture.historyB, colors, coverage, shift, true);
				const auto actual = fixture.color.Read(fixture.context);
				for (UINT x = 0; x < width; x += 2)
					for (UINT c = 0; c < 3; ++c)
						Require(std::isfinite(actual[x][c]) && std::abs(actual[x][c] - colors[x][c]) < .005f * (1 + colors[x][c]),
							"Filtered partial history darkened a constant water color");
				++cases;
			}
		}
		Pixels highlights = colors;
		for (UINT x = 1; x < width; x += 2)
			for (UINT c = 0; c < 3; ++c)
				highlights[x][c] *= 4;
		fixture.context->ClearRenderTargetView(fixture.historyB.rtv.Get(), clear);
		fixture.Draw(fixture.historyB, fixture.historyA, highlights, 1, 0, false, true);
		fixture.Draw(fixture.historyA, fixture.historyB, highlights, 1, .5f, false, true);
		const auto filtered = fixture.color.Read(fixture.context);
		const float black = .18f * std::exp2(-444.0f / 1023.0f * 14.0f);
		for (UINT x = 0; x < width; x += 2)
			for (UINT c = 0; c < 3; ++c) {
				const float history = std::sqrt((highlights[x][c] + black) * (highlights[x + 1][c] + black)) - black;
				const float expected = .05f * highlights[x][c] + .95f * history;
				Require(std::abs(filtered[x][c] - expected) < .01f * (1 + expected), "Fully covered highlights lost logarithmic filtering");
			}
		++cases;
		for (float invalid : { 0.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
			const float value[]{ invalid, invalid, invalid, invalid };
			fixture.context->ClearRenderTargetView(fixture.historyA.rtv.Get(), value);
			fixture.Draw(fixture.historyA, fixture.historyB, colors, 1, .5f, false);
			const auto actual = fixture.color.Read(fixture.context);
			for (UINT x = 0; x < width; ++x)
				for (UINT c = 0; c < 3; ++c)
					Require(actual[x][c] == colors[x][c], "Invalid history changed the current frame");
			++cases;
		}
		for (float alpha : { 0.0f, 6e-8f, .01f, 1.0f }) {
			const float stale[]{ 65504, 65504, 65504, alpha };
			fixture.context->ClearRenderTargetView(fixture.historyA.rtv.Get(), stale);
			fixture.Draw(fixture.historyA, fixture.historyB, colors, 1, .5f, false);
			const auto actual = fixture.color.Read(fixture.context);
			for (UINT x = 0; x < width; ++x)
				for (UINT c = 0; c < 3; ++c) {
					Require(std::isfinite(actual[x][c]) && actual[x][c] >= 0 && actual[x][c] < 44,
						"Unnormalizing history overflowed its logarithmic range");
					if (alpha <= 6e-8f)
						Require(std::abs(actual[x][c] - colors[x][c]) < 1e-5f, "Negligible coverage influenced the current frame");
				}
			++cases;
		}
		for (float coverage : { 0.0f, .01f, .5f, 1.0f }) {
			Pixels bright;
			bright.fill(Pixel{ 100, 100, 100, 1 });
			fixture.context->ClearRenderTargetView(fixture.historyA.rtv.Get(), clear);
			fixture.Draw(fixture.historyA, fixture.historyB, bright, coverage, 0, false);
			const auto encoded = fixture.historyB.Read(fixture.context);
			for (const auto& value : encoded)
				Require(value[0] == value[3] && std::abs(value[3] - coverage) < .001f * std::max(coverage, .0001f), "HDR history escaped the encoded coverage bound");
			++cases;
		}
		return cases;
	}
}

int wmain(int argc, wchar_t** argv)
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, nullptr, &context));
		unsigned cases = 0;
		for (bool vr : { false, true })
			for (bool reversed : { false, true }) {
				Fixture fixture(device.Get(), context.Get(), vr, reversed, argc > 1 ? argv[1] : L"package/Shaders/ISWaterBlend.hlsl");
				cases += Verify(fixture);
			}
		std::cout << "PASS: " << cases << " production water-history cases (flat/VR, standard/reversed depth, FP16 bilinear history)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
