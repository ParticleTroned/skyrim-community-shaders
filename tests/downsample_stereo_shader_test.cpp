#define NOMINMAX
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
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
	constexpr UINT outputWidth = 8;

	struct Texture
	{
		UINT width;
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;
		std::vector<Pixel> pixels;

		Texture(ID3D11Device* device, UINT width, const char* name) : width(width), pixels(width)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			Check(device->CreateTexture2D(&desc, nullptr, &resource));
			Util::SetResourceName(resource.Get(), "DownsampleTest::%s", name);
			Check(device->CreateShaderResourceView(resource.Get(), nullptr, &srv));
			Util::SetResourceName(srv.Get(), "DownsampleTest::%s SRV", name);
			Check(device->CreateRenderTargetView(resource.Get(), nullptr, &rtv));
			Util::SetResourceName(rtv.Get(), "DownsampleTest::%s RTV", name);
		}

		void Upload(ID3D11DeviceContext* context) const
		{
			context->UpdateSubresource(resource.Get(), 0, nullptr, pixels.data(), width * sizeof(Pixel), 0);
		}

		float Sample(float uv) const
		{
			const float position = std::clamp(uv * width - .5f, 0.0f, float(width - 1));
			const auto left = UINT(position);
			return std::lerp(pixels[left][0], pixels[std::min(left + 1, width - 1)][0], position - left);
		}
	};

	struct Fixture
	{
		ID3D11DeviceContext* context;
		bool vr, dynamic;
		ComPtr<ID3D11PixelShader> shader;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11SamplerState> sampler;
		ComPtr<ID3D11Texture2D> readback;
		std::unique_ptr<ConstantBuffer> frame, geometry;
		Texture source, previous, motion, output;

		Fixture(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool dynamic, UINT width) :
			context(context), vr(vr), dynamic(dynamic), source(device, width, "Source"),
			previous(device, width + 2, "History"), motion(device, width + 4, "Motion"), output(device, outputWidth, "Output")
		{
			Util::CustomInclude includes{ "package/Shaders" };
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" } };
			if (vr)
				defines.push_back({ "VR", "1" });
			if (dynamic)
				defines.push_back({ "DYNAMIC_SOURCE", "1" });
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> code, errors;
			constexpr auto flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
			const auto compiled = D3DCompileFromFile(L"package/Shaders/ISDownsample.hlsl", defines.data(), &includes, "main", "ps_5_0", flags, 0, &code, &errors);
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiled);
			Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "DownsampleTest::PS");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			frame = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			geometry = std::make_unique<ConstantBuffer>(device, reflection.Get(), "PerGeometry");
			constexpr char triangle[] = R"(
struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
    Output o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
})";
			const auto compiledVertex = D3DCompile(triangle, sizeof(triangle), nullptr, nullptr, nullptr, "main", "vs_5_0", flags, 0, &code, &errors);
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiledVertex);
			Check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex));
			Util::SetResourceName(vertex.Get(), "DownsampleTest::VS");
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&samplerDesc, &sampler));
			Util::SetResourceName(sampler.Get(), "DownsampleTest::Sampler");
			D3D11_TEXTURE2D_DESC desc{};
			output.resource->GetDesc(&desc);
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, &readback));
			Util::SetResourceName(readback.Get(), "DownsampleTest::Readback");
		}

		void Verify(float scale, float shift, bool compensate, const std::array<float, 2>& offsets)
		{
			context->ClearState();
			const UINT activeSource = dynamic ? std::max(2u, UINT(source.width * scale)) : source.width;
			const UINT activeMotion = std::max(2u, UINT(motion.width * scale));
			for (UINT x = 0; x < source.width; ++x) {
				const float value = x >= activeSource ? 20.0f : x < activeSource / 2 ? 2.0f :
				                                                                       7.0f;
				source.pixels[x] = { value, value, value, value };
			}
			for (UINT x = 0; x < previous.width; ++x) {
				const float value = .5f * x + (x < previous.width / 2 ? 1 : 8);
				previous.pixels[x] = { value, value, value, value };
			}
			for (UINT x = 0; x < motion.width; ++x)
				motion.pixels[x] = { !vr ? shift : x >= activeMotion ? 10 :
											   x < activeMotion / 2  ? shift :
																	   -shift,
					0, 0, 0 };
			source.Upload(context);
			previous.Upload(context);
			motion.Upload(context);
			const float clampMax = scale - .001f;
			frame->SetVariable("FrameBuffer::DynamicResolutionParams1", Pixel{ scale, 1, scale, 1 });
			frame->SetVariable("FrameBuffer::DynamicResolutionParams2", Pixel{ 1 / scale, 1, clampMax, clampMax });
			frame->Bind(context, D3D11ShaderTest::Stage::Pixel);
			geometry->SetVariable("TexelSize", std::array{ 1.0f, 1.0f });
			geometry->SetVariable("SamplesCount", UINT(offsets.size()));
			geometry->SetVariable("CompensateJittering", UINT(compensate));
			std::array<Pixel, 16> taps{};
			for (size_t i = 0; i < offsets.size(); ++i)
				taps[i][0] = offsets[i];
			geometry->SetVariable("OffsetsAndWeights", taps);
			geometry->Bind(context, D3D11ShaderTest::Stage::Pixel);
			ID3D11ShaderResourceView* inputs[]{ source.srv.Get(), previous.srv.Get(), motion.srv.Get() };
			ID3D11SamplerState* samplers[]{ sampler.Get(), sampler.Get(), sampler.Get() };
			ID3D11RenderTargetView* target = output.rtv.Get();
			context->PSSetShaderResources(0, 3, inputs);
			context->PSSetSamplers(0, 3, samplers);
			context->OMSetRenderTargets(1, &target, nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(shader.Get(), nullptr, 0);
			const D3D11_VIEWPORT viewport{ 0, 0, float(outputWidth), 1, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->Draw(3, 0);
			context->ClearState();
			context->CopyResource(readback.Get(), output.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<Pixel, outputWidth> actual{};
			std::memcpy(actual.data(), mapped.pData, sizeof(actual));
			context->Unmap(readback.Get(), 0);
			for (UINT x = 0; x < outputWidth; ++x) {
				const float uv = (x + .5f) / outputWidth;
				const bool right = x >= outputWidth / 2;
				float expected = 0;
				if (vr) {
					expected = right ? 7.0f : 2.0f;
				} else {
					for (float offset : offsets) {
						const float sample = source.Sample(dynamic ? std::clamp((uv + offset) * scale, 0.0f, clampMax) : uv + offset);
						expected = dynamic ? expected + sample / offsets.size() : std::max(expected, sample);
					}
				}
				if (!dynamic && compensate) {
					float previousUV = uv + (vr ? (right ? -shift : shift) : motion.Sample(std::min(uv * scale, clampMax)));
					if (vr) {
						const UINT first = right ? previous.width / 2 : 0;
						const UINT end = right ? previous.width : previous.width / 2;
						previousUV = std::clamp(previousUV, (first + .5f) / previous.width, (end - .5f) / previous.width);
					}
					expected = .5f * (expected + previous.Sample(previousUV));
				}
				// D3D11 permits eight-bit subtexel filtering weights.
				for (float value : actual[x])
					if (!std::isfinite(value) || std::abs(value - expected) > .04f) {
						std::cerr << "vr=" << vr << " dynamic=" << dynamic << " width=" << source.width
								  << " scale=" << scale << " shift=" << shift << " compensate=" << compensate
								  << " offsets=" << offsets[0] << ',' << offsets[1] << " x=" << x
								  << " expected=" << expected << " actual=" << value << '\n';
						throw std::runtime_error("Downsample crossed eyes or changed flat filtering");
					}
			}
		}
	};
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, nullptr, &context));
		unsigned cases = 0;
		for (bool vr : { false, true })
			for (bool dynamic : { false, true })
				for (UINT width : { 2u, 9u, 16u }) {
					Fixture fixture(device.Get(), context.Get(), vr, dynamic, width);
					for (float scale : { 1.0f, .803f, .5f })
						for (float shift : { -1.0f, .2f, 1.0f })
							for (bool compensate : { false, true })
								for (const auto& offsets : { std::array{ 0.0f, 0.0f }, std::array{ -.2f, .2f }, std::array{ -2.0f, 2.0f } }) {
									fixture.Verify(scale, shift, compensate, offsets);
									++cases;
								}
				}
		std::cout << "PASS: " << cases << " production downsample cases (flat/VR, dynamic/static, bilinear temporal history)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
