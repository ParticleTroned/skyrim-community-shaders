#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "native_d3d11_test.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::Name;
	using D3D11ShaderTest::ShaderIncludes;
	using Microsoft::WRL::ComPtr;
	struct DepthCase
	{
		bool featureEnabled;
		bool reversed;
	};
	constexpr std::array depthCases{ DepthCase{ false, false }, DepthCase{ true, false }, DepthCase{ true, true } };

	std::array<float, 16> CameraProjection(bool reversed)
	{
		std::array<float, 16> matrix{};
		matrix[0] = matrix[5] = matrix[14] = 1.0f;
		matrix[10] = reversed ? -0.001f : 1.001f;
		matrix[11] = reversed ? 1.001f : -1.001f;
		return matrix;
	}

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void TestSceneVertexShaders()
	{
		for (const auto* path : { L"Effect.hlsl", L"Lighting.hlsl" }) {
			{
				for (bool reversed : { true, false }) {
					for (bool skinned : { false, true }) {
						for (bool motionVectors : { false, true }) {
							std::vector<D3D_SHADER_MACRO> defines{ { "VSHADER", nullptr }, { "NORMALS", nullptr } };

							if (reversed)
								defines.push_back({ "REVERSE_Z", nullptr });
							if (skinned)
								defines.push_back({ "SKINNED", nullptr });
							if (motionVectors)
								defines.push_back({ "MOTIONVECTORS_NORMALS", nullptr });
							defines.push_back({ nullptr, nullptr });
							ShaderIncludes includes;
							ComPtr<ID3DBlob> code, errors;
							const auto result = D3DCompileFromFile((includes.root / path).c_str(), defines.data(), &includes, "main", "vs_5_0",
								D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
								0, code.GetAddressOf(), errors.GetAddressOf());
							if (errors)
								std::cerr << static_cast<const char*>(errors->GetBufferPointer());
							Require(SUCCEEDED(result), "Production scene vertex shader failed to compile");
							ComPtr<ID3D11ShaderReflection> reflection;
							Require(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf()))), "Scene shader reflection failed");
							auto* frame = reflection->GetConstantBufferByName("VS_PerFrame");
							for (const auto& [name, offset] : { std::pair{ "Proj", 64u },
									 std::pair{ "ViewProj", 128u } }) {
								D3D11_SHADER_VARIABLE_DESC variable{};
								Require(SUCCEEDED(frame->GetVariableByName(name)->GetDesc(&variable)) && variable.StartOffset == offset,
									"Scene vertex frame constants changed their native offsets");
							}
							if (skinned) {
								D3D11_SHADER_VARIABLE_DESC pivot{};
								Require(SUCCEEDED(frame->GetVariableByName("BonesPivot")->GetDesc(&pivot)) && pivot.StartOffset == 640u,
									"Scene vertex skinning pivot changed its native offset");
							}
						}
					}
				}
			}
		}
	}

	void TestUpscaleDepthKernel()
	{
		using D3D11ShaderTest::Check;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = textureDesc.Height = 3;
		textureDesc.MipLevels = textureDesc.ArraySize = textureDesc.SampleDesc.Count = 1;
		textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		ComPtr<ID3D11Texture2D> texture;
		Check(device->CreateTexture2D(&textureDesc, nullptr, texture.GetAddressOf()));
		Name(texture.Get(), "ReverseZDepthTest::UpscaleDepthInput");
		ComPtr<ID3D11ShaderResourceView> input;
		Check(device->CreateShaderResourceView(texture.Get(), nullptr, input.GetAddressOf()));
		Name(input.Get(), "ReverseZDepthTest::UpscaleDepthInput SRV");
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = sizeof(float);
		bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		ComPtr<ID3D11Buffer> output, staging;
		Check(device->CreateBuffer(&bufferDesc, nullptr, output.GetAddressOf()));
		Name(output.Get(), "ReverseZDepthTest::UpscaleDepthOutput");
		D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
		view.Format = DXGI_FORMAT_R32_FLOAT;
		view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		view.Buffer.NumElements = 1;
		ComPtr<ID3D11UnorderedAccessView> uav;
		Check(device->CreateUnorderedAccessView(output.Get(), &view, uav.GetAddressOf()));
		Name(uav.Get(), "ReverseZDepthTest::UpscaleDepthOutput UAV");
		bufferDesc.BindFlags = 0;
		bufferDesc.Usage = D3D11_USAGE_STAGING;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateBuffer(&bufferDesc, nullptr, staging.GetAddressOf()));
		Name(staging.Get(), "ReverseZDepthTest::UpscaleDepthReadback");
		const char* source = R"(
#include "Upscaling/DepthRefractionUpscalePS.hlsl"
RWBuffer<float> Output : register(u0);
[numthreads(1, 1, 1)] void TestDepthKernel() {
    Output[0] = SampleNearestDepth3x3(float2(0.5, 0.5));
})";
		{
			for (const auto [featureEnabled, reversed] : depthCases) {
				std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" } };
				if (featureEnabled)
					defines.push_back({ "REVERSE_Z", "1" });
				defines.push_back({ nullptr, nullptr });
				ShaderIncludes includes;
				ComPtr<ID3DBlob> code, errors, pixelCode;
				const auto compile = [&](const char* entry, const char* target, ID3DBlob** bytecode) {
					const auto result = D3DCompile(source, std::strlen(source), nullptr, defines.data(), &includes,
						entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
						0, bytecode, errors.GetAddressOf());
					if (errors)
						std::cerr << static_cast<const char*>(errors->GetBufferPointer());
					Check(result);
				};
				compile("main", "ps_5_0", pixelCode.GetAddressOf());
				compile("TestDepthKernel", "cs_5_0", code.GetAddressOf());
				ComPtr<ID3D11ComputeShader> shader;
				Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
				Name(shader.Get(), "ReverseZDepthTest::UpscaleDepthKernel");
				ComPtr<ID3D11ShaderReflection> reflection;
				Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
				D3D11ShaderTest::ConstantBuffer shared{ device.Get(), reflection.Get(), "SharedData::SharedData" };
				shared.SetVariable("SharedData::BufferDim", std::array{ 3.0f, 3.0f, 1.0f / 3.0f, 1.0f / 3.0f });
				std::unique_ptr<D3D11ShaderTest::ConstantBuffer> frame;
				if (featureEnabled) {
					frame = std::make_unique<D3D11ShaderTest::ConstantBuffer>(device.Get(), reflection.Get(), "FrameBuffer::PerFrame");
					frame->SetVariable("FrameBuffer::CameraProj", CameraProjection(reversed));
				}
				for (unsigned nearest = 0; nearest < 9; ++nearest) {
					std::array<float, 9> depths;
					depths.fill(reversed ? 0.1f : 0.9f);
					const float expected = reversed ? 0.9f : 0.1f;
					depths[nearest] = expected;
					context->UpdateSubresource(texture.Get(), 0, nullptr, depths.data(), 3 * sizeof(float), 0);
					shared.Bind(context.Get());
					if (frame)
						frame->Bind(context.Get());
					auto* sourceView = input.Get();
					auto* outputView = uav.Get();
					context->CSSetShaderResources(1, 1, &sourceView);
					context->CSSetUnorderedAccessViews(0, 1, &outputView, nullptr);
					context->CSSetShader(shader.Get(), nullptr, 0);
					context->Dispatch(1, 1, 1);
					context->ClearState();
					context->CopyResource(staging.Get(), output.Get());
					D3D11_MAPPED_SUBRESOURCE mapped{};
					Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
					const float actual = *static_cast<const float*>(mapped.pData);
					context->Unmap(staging.Get(), 0);
					Require(actual == expected, "Wide depth-upscale kernel selected the wrong surface");
				}
			}
		}
	}

	void TestInteriorDepth()
	{
		const std::string header = "#include \"Common/DepthOrder.hlsli\"\n";
		std::string source = header;
		source += R"(
RWBuffer<uint> Output : register(u0);
[numthreads(1, 1, 1)] void main() {
    Output[0] = (uint)DepthOrder::IsInteriorDepth(0, 1e-5) |
        ((uint)DepthOrder::IsInteriorDepth(1, 1e-5) << 1) |
        ((uint)DepthOrder::IsInteriorDepth(0.5, 1e-5) << 2) |
        ((uint)DepthOrder::IsInteriorDepth(1e-10, 1e-5) << 3) |
        ((uint)DepthOrder::IsInteriorDepth(asfloat(0x7fc00000), 1e-5) << 4);
})";
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
					D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf())),
			"Depth-order WARP device failed");
		for (const auto [featureEnabled, reversed] : depthCases) {
			D3D_SHADER_MACRO defines[]{ { featureEnabled ? "REVERSE_Z" : "STANDARD_Z", "1" }, { nullptr, nullptr } };
			ComPtr<ID3DBlob> code, errors;
			ShaderIncludes includes;
			Require(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, defines, &includes, "main", "cs_5_0", 0, 0, code.GetAddressOf(), errors.GetAddressOf())), "Depth-order shader failed to compile");
			ComPtr<ID3D11ComputeShader> shader;
			Require(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf())), "Depth-order shader creation failed");
			Name(shader.Get(), "ReverseZDepthTest::InteriorDepth");
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			std::unique_ptr<D3D11ShaderTest::ConstantBuffer> frame;
			if (featureEnabled) {
				frame = std::make_unique<D3D11ShaderTest::ConstantBuffer>(device.Get(), reflection.Get(), "FrameBuffer::PerFrame");
				frame->SetVariable("FrameBuffer::CameraProj", CameraProjection(reversed));
				frame->Bind(context.Get());
			}
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(uint32_t);
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			ComPtr<ID3D11Buffer> output, staging;
			Require(SUCCEEDED(device->CreateBuffer(&desc, nullptr, output.GetAddressOf())), "Depth-order output creation failed");
			Name(output.Get(), "ReverseZDepthTest::Output");
			D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_R32_UINT;
			view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			view.Buffer.NumElements = 1;
			ComPtr<ID3D11UnorderedAccessView> uav;
			Require(SUCCEEDED(device->CreateUnorderedAccessView(output.Get(), &view, uav.GetAddressOf())), "Depth-order UAV creation failed");
			Name(uav.Get(), "ReverseZDepthTest::Output UAV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Require(SUCCEEDED(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf())), "Depth-order readback creation failed");
			Name(staging.Get(), "ReverseZDepthTest::Readback");
			auto* outputView = uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &outputView, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "Depth-order readback failed");
			const auto result = *static_cast<const uint32_t*>(mapped.pData);
			context->Unmap(staging.Get(), 0);
			Require(result == (reversed ? 12u : 4u), "Depth validation lost reversed far precision or accepted an endpoint");
		}
	}
}

TEST_CASE("SE scene projection constants retain native ABI", "[shaders][fxc][reverse-z][se]")
{
	TestSceneVertexShaders();
}

TEST_CASE("SE reverse depth selects the nearest surface and preserves far precision", "[shaders][warp][reverse-z][se]")
{
	TestInteriorDepth();
	TestUpscaleDepthKernel();
}
