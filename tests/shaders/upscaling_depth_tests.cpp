#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "../../src/Features/Upscaling/SceneDepthBindings.h"
#include "native_d3d11_test.h"

#include <catch2/catch_test_macros.hpp>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using D3D11ShaderTest::Name;
	using D3D11ShaderTest::ShaderIncludes;
	using Microsoft::WRL::ComPtr;
	using Pixel = std::array<float, 4>;
	constexpr UINT kWidth = 17, kHeight = 13, kOutputWidth = 20, kOutputHeight = 15;

	struct Texture
	{
		D3D11_TEXTURE2D_DESC desc{};
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		ComPtr<ID3D11RenderTargetView> rtv;
	};

	Texture MakeTexture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format,
		UINT flags, const D3D11_SUBRESOURCE_DATA* initial = nullptr)
	{
		Texture value;
		value.desc.Width = width;
		value.desc.Height = height;
		value.desc.MipLevels = value.desc.ArraySize = value.desc.SampleDesc.Count = 1;
		value.desc.Format = format;
		value.desc.BindFlags = flags;
		Check(device->CreateTexture2D(&value.desc, initial, value.resource.GetAddressOf()));
		Name(value.resource.Get(), "SEDepthEncodeTest::Texture");
		if (flags & D3D11_BIND_SHADER_RESOURCE) {
			Check(device->CreateShaderResourceView(value.resource.Get(), nullptr, value.srv.GetAddressOf()));
			Name(value.srv.Get(), "SEDepthEncodeTest::SRV");
		}
		if (flags & D3D11_BIND_UNORDERED_ACCESS) {
			Check(device->CreateUnorderedAccessView(value.resource.Get(), nullptr, value.uav.GetAddressOf()));
			Name(value.uav.Get(), "SEDepthEncodeTest::UAV");
		}
		if (flags & D3D11_BIND_RENDER_TARGET) {
			Check(device->CreateRenderTargetView(value.resource.Get(), nullptr, value.rtv.GetAddressOf()));
			Name(value.rtv.Get(), "SEDepthEncodeTest::RTV");
		}
		return value;
	}

	struct Inputs
	{
		std::array<float, kWidth * kHeight> depth{};
		std::array<std::array<Pixel, kWidth * kHeight>, 3> guides{};
		bool reversed;

		explicit Inputs(bool reverse) : reversed(reverse)
		{
			const auto set = [&](UINT x, UINT y, float standardDepth, float mx, float my) {
				const auto index = y * kWidth + x;
				depth[index] = reverse ? 1.0f - standardDepth : standardDepth;
				guides[0][index] = { float(x % 7) * .1f, float(y % 3) * .1f, 0, 0 };
				guides[1][index] = { 0, 0, .2f + float(x % 3) * .1f, 0 };
				guides[2][index] = { mx, my, 0, 0 };
			};
			for (UINT y = 0; y < kHeight; ++y)
				for (UINT x = 0; x < kWidth; ++x)
					set(x, y, .75f, 1, 0);
			set(1, 0, .5f, 3, 4);
			set(2, 0, .25f, 0, -5);  // Equal-length donors retain traversal order.
			set(1, 3, .125f, 2, 0);
			set(2, 3, .5f, 0, 4);  // The longest motion wins, not the nearest depth.
			set(7, 3, .5f, -5, 0);
			set(4, 4, .25f, 0, 5);  // Y-first traversal determines a tie.
			set(7, 8, .75f, 0, -5);
			set(5, 6, .5f, 3, 4);     // A donor tie retains the centre vector.
			set(9, 1, .75f, 80, 0);   // Equal depth cannot donate.
			set(9, 4, .875f, 90, 0);  // Farther depth cannot donate.
			set(0, 0, 0, 1, 0);
			set(kWidth - 1, kHeight - 1, 1, 1, 0);
		}

		std::array<float, 5> Reference(UINT x, UINT y, UINT width, UINT height, bool dlss, const Pixel& camera) const
		{
			const auto index = y * kWidth + x;
			const auto& original = guides[2][index];
			std::array<float, 2> motion{ original[0], original[1] };
			const float centerDepth = depth[index];
			if (dlss) {
				float longest = motion[0] * motion[0] + motion[1] * motion[1];
				for (int dy = -2; dy <= 2; ++dy)
					for (int dx = -2; dx <= 2; ++dx) {
						const int sx = int(x) + dx, sy = int(y) + dy;
						if (sx < 0 || sy < 0 || sx >= int(width) || sy >= int(height))
							continue;
						const auto donorDepth = depth[sy * kWidth + sx];
						if (!(reversed ? donorDepth > centerDepth : donorDepth < centerDepth))
							continue;
						const auto& donor = guides[2][sy * kWidth + sx];
						const float length = donor[0] * donor[0] + donor[1] * donor[1];
						if (length > longest) {
							longest = length;
							motion = { donor[0], donor[1] };
						}
					}
				const float distance = camera[3] / (reversed ? camera[1] + centerDepth * camera[2] : camera[0] - centerDepth * camera[2]);
				const float t = std::clamp(1.f - distance / 10240.f, 0.f, 1.f);
				const float blend = t * t * (3.f - 2.f * t);
				for (unsigned channel = 0; channel < 2; ++channel)
					motion[channel] += (original[channel] - motion[channel]) * blend;
			}
			return { guides[0][index][0] * .1f + guides[0][index][1], guides[1][index][2],
				motion[0], motion[1], reversed ? 1.0f - centerDepth : centerDepth };
		}
	};

	ComPtr<ID3DBlob> CompileProgram(const char* relativePath, const char* profile, const std::vector<D3D_SHADER_MACRO>& macros)
	{
		ShaderIncludes includes;
		const auto path = includes.root / relativePath;
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(path.c_str(), macros.data(), &includes, "main", profile,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, code.GetAddressOf(), errors.GetAddressOf());
		INFO("Shader: " << relativePath << " FXC: " << (errors ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
		REQUIRE(SUCCEEDED(result));
		return code;
	}

	ComPtr<ID3DBlob> Compile(const char* method, bool reverseFeature, bool depthOutput)
	{
		std::vector<D3D_SHADER_MACRO> macros{ { method, "1" }, { "COMPUTESHADER", "1" }, { "DX11", "1" }, { "WINPC", "1" } };
		if (reverseFeature)
			macros.push_back({ "REVERSE_Z", "1" });
		if (depthOutput)
			macros.push_back({ "DEPTH_OUTPUT", "1" });
		macros.push_back({ nullptr, nullptr });
		INFO("Encoder method=" << method << " reverseFeature=" << reverseFeature << " depthOutput=" << depthOutput);
		return CompileProgram("Upscaling/EncodeTexturesCS.hlsl", "cs_5_0", macros);
	}

	void Verify(ID3D11Device* device, ID3D11DeviceContext* context, const char* method, bool reverseFeature, bool reversed)
	{
		const bool dlss = std::string_view(method) == "DLSS";
		const Inputs fixture(reversed);
		constexpr Pixel farCamera{ 1, 1, 0, 10240 };
		REQUIRE(fixture.Reference(3, 2, kWidth, kHeight, true, farCamera)[2] == 3);
		REQUIRE(fixture.Reference(6, 5, kWidth, kHeight, true, farCamera)[2] == -5);
		REQUIRE(fixture.Reference(7, 8, kWidth, kHeight, true, farCamera)[3] == -5);
		const D3D11_SUBRESOURCE_DATA initial{ fixture.depth.data(), kWidth * sizeof(float), 0 };
		auto depth = MakeTexture(device, kWidth, kHeight, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, &initial);
		std::array<Texture, 3> guides;
		std::array<Texture, 4> outputs;
		std::array<ComPtr<ID3D11Texture2D>, 4> staging;
		for (unsigned i = 0; i < guides.size(); ++i) {
			const D3D11_SUBRESOURCE_DATA data{ fixture.guides[i].data(), kWidth * sizeof(Pixel), 0 };
			guides[i] = MakeTexture(device, kWidth, kHeight, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE, &data);
		}
		for (unsigned i = 0; i < outputs.size(); ++i) {
			outputs[i] = MakeTexture(device, kOutputWidth, kOutputHeight, i == 2 ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32_FLOAT,
				D3D11_BIND_UNORDERED_ACCESS);
			auto desc = outputs[i].desc;
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging[i].GetAddressOf()));
			Name(staging[i].Get(), "SEDepthEncodeTest::Readback");
		}
		auto code = Compile(method, reverseFeature, true);
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Name(shader.Get(), "SEDepthEncodeTest::Encoder");
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer constants(device, reflection.Get(), "UpscalingData");
		REQUIRE(constants.bytes.size() == 16);
		std::unique_ptr<ConstantBuffer> cameraConstants;
		if (dlss)
			cameraConstants = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::SharedData");
		std::unique_ptr<ConstantBuffer> frameConstants;
		if (reverseFeature) {
			frameConstants = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			std::array<float, 16> projection{};
			projection[10] = reversed ? -1.0f : 1.0f;
			projection[14] = 1.0f;
			frameConstants->SetVariable("FrameBuffer::CameraProj", projection);
		}

		constexpr std::array<std::array<UINT, 2>, 6> dimensions{ { { 1, 1 }, { 7, 5 }, { 8, 8 }, { 9, 7 }, { 13, 11 }, { kWidth, kHeight } } };
		constexpr std::array cameras{ Pixel{ 1, 1, 0, 0 }, Pixel{ 1, 1, 0, 5120 }, farCamera, Pixel{ 2, 1, 1, 8192 } };
		for (const auto& size : dimensions)
			for (unsigned camera = 0; camera < (dlss ? cameras.size() : 1u); ++camera) {
				INFO("method=" << method << " reversed=" << reversed << " size=" << size[0] << "x" << size[1] << " camera=" << camera);
				context->ClearState();
				constants.SetVariable("TrueSamplingDim", std::array{ float(size[0]), float(size[1]) });
				constants.Bind(context);
				if (frameConstants)
					frameConstants->Bind(context);
				if (cameraConstants) {
					cameraConstants->SetVariable("SharedData::CameraData", cameras[camera]);
					cameraConstants->Bind(context);
				}
				constexpr float sentinel[4]{ -1, -1, -1, -1 };
				for (auto& output : outputs)
					context->ClearUnorderedAccessViewFloat(output.uav.Get(), sentinel);
				ID3D11ShaderResourceView* srvs[]{ guides[0].srv.Get(), guides[1].srv.Get(), guides[2].srv.Get(), depth.srv.Get() };
				ID3D11UnorderedAccessView* uavs[]{ outputs[0].uav.Get(), outputs[1].uav.Get(), outputs[2].uav.Get(), outputs[3].uav.Get() };
				context->CSSetShaderResources(0, 4, srvs);
				context->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);
				context->CSSetShader(shader.Get(), nullptr, 0);
				context->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
				context->ClearState();
				for (unsigned output = 0; output < outputs.size(); ++output) {
					context->CopyResource(staging[output].Get(), outputs[output].resource.Get());
					D3D11_MAPPED_SUBRESOURCE mapped{};
					Check(context->Map(staging[output].Get(), 0, D3D11_MAP_READ, 0, &mapped));
					for (UINT y = 0; y < kOutputHeight; ++y) {
						const auto* values = reinterpret_cast<const float*>(static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch);
						for (UINT x = 0; x < kOutputWidth; ++x) {
							std::array<float, 5> expected{ -1, -1, -1, -1, -1 };
							if (x < size[0] && y < size[1])
								expected = fixture.Reference(x, y, size[0], size[1], dlss, cameras[camera]);
							const unsigned channels = output == 2 ? 2 : 1;
							for (unsigned channel = 0; channel < channels; ++channel) {
								const float value = values[x * channels + channel];
								const float reference = expected[(output == 3 ? 4 : output) + channel];
								CHECK(std::isfinite(value));
								CHECK(std::abs(value - reference) < 2e-4f);
							}
						}
					}
					context->Unmap(staging[output].Get(), 0);
				}
			}
	}

	void VerifyFrameGenerationDepth(ID3D11Device* device, ID3D11DeviceContext* context, bool reverseFeature, bool reversed)
	{
		context->ClearState();
		const Inputs fixture(reversed);
		const D3D11_SUBRESOURCE_DATA initial{ fixture.depth.data(), kWidth * sizeof(float), 0 };
		auto depth = MakeTexture(device, kWidth, kHeight, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, &initial);
		auto output = MakeTexture(device, kWidth, kHeight, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_RENDER_TARGET);
		std::vector<D3D_SHADER_MACRO> macros{ { "PSHADER", "1" }, { "DX11", "1" }, { "WINPC", "1" } };
		if (reverseFeature)
			macros.push_back({ "REVERSE_Z", "1" });
		macros.push_back({ nullptr, nullptr });
		auto pixelCode = CompileProgram("Upscaling/CopyDepthToSharedBufferPS.hlsl", "ps_5_0", macros);
		auto vertexCode = CompileProgram("Upscaling/UpscaleVS.hlsl", "vs_5_0", { { "VSHADER", "1" }, { nullptr, nullptr } });
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11VertexShader> vertex;
		Check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, pixel.GetAddressOf()));
		Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
		Name(pixel.Get(), "SEDepthEncodeTest::FGDepthCopy");
		Name(vertex.Get(), "SEDepthEncodeTest::FullscreenVertex");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		std::unique_ptr<ConstantBuffer> frame;
		if (reverseFeature) {
			frame = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			std::array<float, 16> projection{};
			projection[10] = reversed ? -1.f : 1.f;
			projection[14] = 1.f;
			frame->SetVariable("FrameBuffer::CameraProj", projection);
			context->UpdateSubresource(frame->buffer.Get(), 0, nullptr, frame->bytes.data(), 0, 0);
		}
		D3D11_RASTERIZER_DESC rasterDesc{};
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.CullMode = D3D11_CULL_NONE;
		rasterDesc.DepthClipEnable = TRUE;
		ComPtr<ID3D11RasterizerState> raster;
		Check(device->CreateRasterizerState(&rasterDesc, raster.GetAddressOf()));
		Name(raster.Get(), "SEDepthEncodeTest::FullscreenRasterizer");
		context->RSSetState(raster.Get());
		const D3D11_VIEWPORT viewport{ 0, 0, float(kWidth), float(kHeight), 0, 1 };
		context->RSSetViewports(1, &viewport);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertex.Get(), nullptr, 0);
		context->PSSetShader(pixel.Get(), nullptr, 0);
		auto* input = depth.srv.Get();
		auto* target = output.rtv.Get();
		context->PSSetShaderResources(0, 1, &input);
		context->OMSetRenderTargets(1, &target, nullptr);
		ComPtr<ID3D11DeviceContext1> context1;
		Check(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())));
		D3D11_BUFFER_DESC previousDesc{};
		previousDesc.ByteWidth = 65536;
		previousDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		ComPtr<ID3D11Buffer> previous;
		Check(device->CreateBuffer(&previousDesc, nullptr, previous.GetAddressOf()));
		Name(previous.Get(), "SEDepthEncodeTest::PriorFrameBuffer");
		D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
		Check(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)));
		const bool windowed = reverseFeature && options.ConstantBufferOffsetting;
		const UINT first = 16, count = 16;
		auto* prior = reverseFeature ? previous.Get() : nullptr;
		if (windowed)
			context1->PSSetConstantBuffers1(12, 1, &prior, &first, &count);
		else
			context->PSSetConstantBuffers(12, 1, &prior);
		{
			const UpscalingDepth::ScopedFrameBuffer binding(context, frame ? frame->buffer.Get() : nullptr, UpscalingDepth::Stage::Pixel);
			context->Draw(3, 0);
		}
		ComPtr<ID3D11Buffer> restored;
		UINT restoredFirst = 0, restoredCount = 0;
		context1->PSGetConstantBuffers1(12, 1, restored.GetAddressOf(), &restoredFirst, &restoredCount);
		CHECK(restored.Get() == prior);
		if (prior) {
			CHECK(restoredFirst == (windowed ? first : 0));
			CHECK(restoredCount == (windowed ? count : D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT));
		}
		context->ClearState();
		auto desc = output.desc;
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> staging;
		Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
		Name(staging.Get(), "SEDepthEncodeTest::FGDepthReadback");
		context->CopyResource(staging.Get(), output.resource.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		for (UINT y = 0; y < kHeight; ++y) {
			const auto* values = reinterpret_cast<const float*>(static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch);
			for (UINT x = 0; x < kWidth; ++x) {
				const auto raw = fixture.depth[y * kWidth + x];
				CHECK(values[x] == (reversed ? 1.f - raw : raw));
			}
		}
		context->Unmap(staging.Get(), 0);
	}
}

TEST_CASE("SE tiled encoder preserves motion donors, partial groups and standard SDK depth", "[shaders][fxc][se][upscaling]")
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	for (const bool reverseFeature : { false, true }) {
		for (const bool reversed : { false, true }) {
			if (reversed && !reverseFeature)
				continue;
			// The reverse permutation must also retain standard startup-menu depth.
			Verify(device.Get(), context.Get(), "DLSS", reverseFeature, reversed);
			Verify(device.Get(), context.Get(), "FSR", reverseFeature, reversed);
			VerifyFrameGenerationDepth(device.Get(), context.Get(), reverseFeature, reversed);
		}
		auto code = Compile("DLSS", reverseFeature, false);
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		REQUIRE(FAILED(reflection->GetResourceBindingDescByName("DepthOutput", &binding)));
	}
}
