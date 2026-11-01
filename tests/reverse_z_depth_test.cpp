#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "ShaderPackageIncludes.h"
#include "Utils/DepthConvention.h"
#include "Utils/NativeDepthShader.h"
#include "d3d11_shader_test.h"
#include "d3d_resource_naming.h"

#include <DirectXMath.h>
#include <array>
#include <cstring>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include <winrt/base.h>
#include <wrl/implements.h>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	struct ShaderIdentity final : Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ID3D11VertexShader>
	{
		explicit ShaderIdentity(std::atomic<unsigned>* a_destroyed) : destroyed(a_destroyed) {}
		~ShaderIdentity() { ++*destroyed; }
		void STDMETHODCALLTYPE GetDevice(ID3D11Device** device) override { *device = nullptr; }
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
		std::atomic<unsigned>* destroyed;
	};
	void TestShaderRegistry()
	{
		std::atomic<unsigned> destroyed{ 0 };
		{
			Depth::NativeVertexShaderRegistry registry;
			auto original = Microsoft::WRL::Make<ShaderIdentity>(&destroyed);
			auto* identity = original.Get();
			Require(registry.Classify(identity) == Depth::NativeVertexProgram::Other, "Unregistered shader was classified");
			registry.Register(identity, Depth::NativeVertexProgram::NearPlane);
			registry.Register(identity, Depth::NativeVertexProgram::NearPlane);
			original.Reset();
			Require(destroyed == 0 && registry.Classify(identity) == Depth::NativeVertexProgram::NearPlane,
				"Registry did not retain the original shader identity");
			std::atomic<bool> valid{ true };
			std::jthread reader([&](std::stop_token stop) {
				while (!stop.stop_requested())
					if (registry.Classify(identity) != Depth::NativeVertexProgram::NearPlane)
						valid.store(false, std::memory_order_relaxed);
			});
			for (unsigned generation = 0; generation < 128; ++generation) {
				auto shader = Microsoft::WRL::Make<ShaderIdentity>(&destroyed);
				const auto program = generation % 2 ? Depth::NativeVertexProgram::NearPlane : Depth::NativeVertexProgram::Occluder;
				registry.Register(shader.Get(), program);
				Require(registry.Classify(shader.Get()) == program, "New shader classification was not published before return");
			}
			reader.request_stop();
			reader.join();
			Require(valid && destroyed == 0, "Concurrent publication lost an older shader generation");
			Require(registry.Classify(nullptr) == Depth::NativeVertexProgram::Other, "Null shader was classified");
		}
		Require(destroyed == 129, "Registry did not release all retained shader generations");
	}
	struct Matrix
	{
		float m[4][4];
	};
	using namespace DirectX;
	Matrix Store(FXMMATRIX value)
	{
		Matrix result;
		XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&result), value);
		return result;
	}
	XMMATRIX Load(const Matrix& value) { return XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&value)); }
	void TestMatrices()
	{
		Require(Depth::IsValidRange(1, 100000), "Valid scene depth range rejected");
		for (auto range : { std::array{ 0.0, 10.0 }, std::array{ -1.0, 10.0 }, std::array{ 10.0, 10.0 },
				 std::array{ 20.0, 10.0 }, std::array{ 1.0, std::numeric_limits<double>::infinity() } })
			Require(!Depth::IsValidRange(range[0], range[1]), "Invalid camera depth range accepted");
		for (float eye : { -0.03f, 0.03f }) {
			const auto standard = XMMatrixPerspectiveOffCenterLH(-0.8f + eye, 1.2f + eye, -0.7f, 0.9f, 1.0f, 100000.0f);
			auto reversed = Store(standard);
			Depth::SetReverseProjection(reversed, 1, 100000);
			for (float z : { 1.0f, 2.0f, 100.0f, 10000.0f, 100000.0f }) {
				const auto position = XMVectorSet(eye, 0.2f, z, 1);
				const auto projected = XMVector4Transform(position, Load(reversed));
				const auto depth = XMVectorGetZ(projected) / XMVectorGetW(projected);
				const auto reconstructed = Depth::Linearize(depth, 1, 100000, Depth::Convention::Reversed);
				Require(std::abs(reconstructed - z) < (std::max)(1e-4, double(z) * 2e-6), "Reverse depth reconstruction lost precision");
			}
			const auto view = XMMatrixLookToLH(XMVectorSet(50, -13, 200, 1), XMVectorSet(0.3f, 0.2f, 1, 0), XMVectorSet(0, 1, 0, 0));
			auto combined = Store(XMMatrixMultiply(view, standard));
			Depth::SetReverseProjection(combined, 1, 100000);
			const auto rebuilt = Store(XMMatrixMultiply(view, Load(reversed)));
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					Require(std::abs(combined.m[r][c] - rebuilt.m[r][c]) < 1e-5f, "Translated camera depth disagrees with its projection");
			auto uploaded = Store(XMMatrixTranspose(standard));
			Depth::SetReverseUpload(uploaded, 1, 100000);
			const auto expected = Store(XMMatrixTranspose(Load(reversed)));
			Require(std::memcmp(&expected, &uploaded, sizeof(expected)) == 0, "GPU and engine matrix transformations differ");
			auto inverse = Store(XMMatrixInverse(nullptr, Load(uploaded)));
			const auto identity = XMMatrixMultiply(Load(uploaded), Load(inverse));
			const auto product = Store(identity);
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					Require(std::abs(product.m[r][c] - (r == c ? 1.0f : 0.0f)) < 1e-5f, "Native reversed depth uses an incompatible inverse");
			Depth::SetReverseProjection(reversed, 1, 100000);
			const auto original = expected;
			reversed = Store(XMMatrixTranspose(Load(reversed)));
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					Require(std::abs(original.m[r][c] - reversed.m[r][c]) < 1e-6f, "Direct reverse projection is not idempotent");
		}
	}
	void TestMaskIdentity()
	{
		for (int variant = 0; variant < 3; ++variant) {
			const char* shader = variant == 0 ? "float4 main(float3 p:POSITION):SV_Position{return float4(p.xy,0,1);}" :
			                     variant == 1 ? "float4 main(float3 p:POSITION):SV_Position{return float4(p.xy,1,1);}" :
			                                    "float4 main(float3 p:POSITION):SV_Position{return float4(p,1);}";
			Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
			Require(SUCCEEDED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors)), "Mask identity fixture did not compile");
			Require(Depth::IsFixedNearPlaneVertexProgram(code->GetBufferPointer(), code->GetBufferSize()) == (variant == 0), "Mask adapter matched the wrong vertex program");
		}
	}
	void TestSceneVertexShaders()
	{
		for (const auto* path : { L"package/Shaders/Effect.hlsl", L"package/Shaders/Lighting.hlsl" }) {
			for (bool vr : { false, true }) {
				for (bool reversed : { true, false }) {
					for (bool skinned : { false, true }) {
						for (bool motionVectors : { false, true }) {
							std::vector<D3D_SHADER_MACRO> defines{ { "VSHADER", nullptr }, { "NORMALS", nullptr } };
							if (vr)
								defines.push_back({ "VR", nullptr });
							if (reversed)
								defines.push_back({ "REVERSE_Z", nullptr });
							if (skinned)
								defines.push_back({ "SKINNED", nullptr });
							if (motionVectors)
								defines.push_back({ "MOTIONVECTORS_NORMALS", nullptr });
							defines.push_back({ nullptr, nullptr });
							PackageIncludes includes{ "package/Shaders" };
							winrt::com_ptr<ID3DBlob> code, errors;
							const auto result = D3DCompileFromFile(path, defines.data(), &includes, "main", "vs_5_0",
								D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
								0, code.put(), errors.put());
							if (errors)
								std::cerr << static_cast<const char*>(errors->GetBufferPointer());
							Require(SUCCEEDED(result), "Production scene vertex shader failed to compile");
							winrt::com_ptr<ID3D11ShaderReflection> reflection;
							Require(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.put()))), "Scene shader reflection failed");
							auto* frame = reflection->GetConstantBufferByName("VS_PerFrame");
							for (const auto& [name, offset] : { std::pair{ "Proj", vr ? 128u : 64u },
									 std::pair{ "ViewProj", vr ? 256u : 128u } }) {
								D3D11_SHADER_VARIABLE_DESC variable{};
								Require(SUCCEEDED(frame->GetVariableByName(name)->GetDesc(&variable)) && variable.StartOffset == offset,
									"Scene vertex frame constants changed their native offsets");
							}
							if (skinned) {
								D3D11_SHADER_VARIABLE_DESC pivot{};
								Require(SUCCEEDED(frame->GetVariableByName("BonesPivot")->GetDesc(&pivot)) && pivot.StartOffset == (vr ? 1280u : 640u),
									"Scene vertex skinning pivot changed its native offset");
							}
						}
					}
				}
			}
		}
	}
	void TestUnderwaterMaskInputs()
	{
		using D3D11ShaderTest::Check;
		for (bool vr : { false, true }) {
			for (bool reversed : { false, true }) {
				for (unsigned variant = 0; variant < (vr ? 3u : 1u); ++variant) {
					std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" } };
					if (vr)
						defines.push_back({ "VR", "1" });
					if (reversed)
						defines.push_back({ "REVERSE_Z", "1" });
					if (variant)
						defines.push_back({ "NO_HMD_STENCIL_MASK", "1" });
					if (variant == 1)
						defines.push_back({ "DYNAMIC_SCENE_DEPTH", "1" });
					if (variant == 2)
						defines.push_back({ "RAW_SCENE_DEPTH", "1" });
					defines.push_back({ nullptr, nullptr });
					PackageIncludes includes{ "package/Shaders", "features/Upscaling/Shaders" };
					winrt::com_ptr<ID3DBlob> code, errors;
					const auto result = D3DCompileFromFile(L"features/Upscaling/Shaders/Upscaling/UnderwaterMaskUpscalePS.hlsl",
						defines.data(), &includes, "main", "ps_5_0",
						D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
						0, code.put(), errors.put());
					if (errors)
						std::cerr << static_cast<const char*>(errors->GetBufferPointer());
					Check(result);
					winrt::com_ptr<ID3D11ShaderReflection> reflection;
					Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.put())));
					D3D11_SHADER_INPUT_BIND_DESC binding{};
					const bool readsPreviousMask = SUCCEEDED(reflection->GetResourceBindingDescByName("UnderwaterMask", &binding));
					Require(readsPreviousMask == (variant != 2), "Underwater mask shader inputs violate the copy-lifetime contract");
				}
			}
		}
	}
	void TestUpscaleDepthKernel()
	{
		using D3D11ShaderTest::Check;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = textureDesc.Height = 3;
		textureDesc.MipLevels = textureDesc.ArraySize = textureDesc.SampleDesc.Count = 1;
		textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		winrt::com_ptr<ID3D11Texture2D> texture;
		Check(device->CreateTexture2D(&textureDesc, nullptr, texture.put()));
		Util::SetResourceName(texture.get(), "ReverseZDepthTest::UpscaleDepthInput");
		winrt::com_ptr<ID3D11ShaderResourceView> input;
		Check(device->CreateShaderResourceView(texture.get(), nullptr, input.put()));
		Util::SetResourceName(input.get(), "ReverseZDepthTest::UpscaleDepthInput SRV");
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = sizeof(float);
		bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		winrt::com_ptr<ID3D11Buffer> output, staging;
		Check(device->CreateBuffer(&bufferDesc, nullptr, output.put()));
		Util::SetResourceName(output.get(), "ReverseZDepthTest::UpscaleDepthOutput");
		D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
		view.Format = DXGI_FORMAT_R32_FLOAT;
		view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		view.Buffer.NumElements = 1;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		Check(device->CreateUnorderedAccessView(output.get(), &view, uav.put()));
		Util::SetResourceName(uav.get(), "ReverseZDepthTest::UpscaleDepthOutput UAV");
		bufferDesc.BindFlags = 0;
		bufferDesc.Usage = D3D11_USAGE_STAGING;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateBuffer(&bufferDesc, nullptr, staging.put()));
		Util::SetResourceName(staging.get(), "ReverseZDepthTest::UpscaleDepthReadback");
		const char* source = R"(
#include "Upscaling/DepthRefractionUpscalePS.hlsl"
RWBuffer<float> Output : register(u0);
[numthreads(1, 1, 1)] void TestDepthKernel() {
    Output[0] = SampleNearestDepth3x3(float2(0.5, 0.5));
})";
		for (bool vr : { false, true }) {
			for (bool reversed : { false, true }) {
				std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" } };
				if (vr)
					defines.push_back({ "VR", "1" });
				if (reversed)
					defines.push_back({ "REVERSE_Z", "1" });
				defines.push_back({ nullptr, nullptr });
				PackageIncludes includes{ "package/Shaders", "features/Upscaling/Shaders" };
				winrt::com_ptr<ID3DBlob> code, errors, pixelCode;
				const auto compile = [&](const char* entry, const char* target, ID3DBlob** bytecode) {
					const auto result = D3DCompile(source, std::strlen(source), nullptr, defines.data(), &includes,
						entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
						0, bytecode, errors.put());
					if (errors)
						std::cerr << static_cast<const char*>(errors->GetBufferPointer());
					Check(result);
				};
				compile("main", "ps_5_0", pixelCode.put());
				compile("TestDepthKernel", "cs_5_0", code.put());
				winrt::com_ptr<ID3D11ComputeShader> shader;
				Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.put()));
				Util::SetResourceName(shader.get(), "ReverseZDepthTest::UpscaleDepthKernel");
				winrt::com_ptr<ID3D11ShaderReflection> reflection;
				Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.put())));
				D3D11ShaderTest::ConstantBuffer shared{ device.get(), reflection.get(), "SharedData::SharedData" };
				shared.SetVariable("SharedData::BufferDim", std::array{ 3.0f, 3.0f, 1.0f / 3.0f, 1.0f / 3.0f });
				for (unsigned nearest = 0; nearest < 9; ++nearest) {
					std::array<float, 9> depths;
					depths.fill(reversed ? 0.1f : 0.9f);
					const float expected = reversed ? 0.9f : 0.1f;
					depths[nearest] = expected;
					context->UpdateSubresource(texture.get(), 0, nullptr, depths.data(), 3 * sizeof(float), 0);
					shared.Bind(context.get());
					auto* sourceView = input.get();
					auto* outputView = uav.get();
					context->CSSetShaderResources(1, 1, &sourceView);
					context->CSSetUnorderedAccessViews(0, 1, &outputView, nullptr);
					context->CSSetShader(shader.get(), nullptr, 0);
					context->Dispatch(1, 1, 1);
					context->ClearState();
					context->CopyResource(staging.get(), output.get());
					D3D11_MAPPED_SUBRESOURCE mapped{};
					Check(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
					const float actual = *static_cast<const float*>(mapped.pData);
					context->Unmap(staging.get(), 0);
					Require(actual == expected, "Wide depth-upscale kernel selected the wrong surface");
				}
			}
		}
	}
	void TestInteriorDepth()
	{
		std::ifstream input("package/Shaders/Common/DepthOrder.hlsli");
		Require(input.good(), "Production depth-order shader unavailable");
		std::string source{ std::istreambuf_iterator<char>(input), {} };
		source += R"(
RWBuffer<uint> Output : register(u0);
[numthreads(1, 1, 1)] void main() {
    Output[0] = (uint)DepthOrder::IsInteriorDepth(0, 1e-5) |
        ((uint)DepthOrder::IsInteriorDepth(1, 1e-5) << 1) |
        ((uint)DepthOrder::IsInteriorDepth(0.5, 1e-5) << 2) |
        ((uint)DepthOrder::IsInteriorDepth(1e-10, 1e-5) << 3) |
        ((uint)DepthOrder::IsInteriorDepth(asfloat(0x7fc00000), 1e-5) << 4);
})";
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
					D3D11_SDK_VERSION, device.put(), nullptr, context.put())),
			"Depth-order WARP device failed");
		for (bool reversed : { false, true }) {
			D3D_SHADER_MACRO defines[]{ { reversed ? "REVERSE_Z" : "STANDARD_Z", "1" }, { nullptr, nullptr } };
			winrt::com_ptr<ID3DBlob> code, errors;
			Require(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, defines, nullptr, "main", "cs_5_0", 0, 0, code.put(), errors.put())), "Depth-order shader failed to compile");
			winrt::com_ptr<ID3D11ComputeShader> shader;
			Require(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.put())), "Depth-order shader creation failed");
			Util::SetResourceName(shader.get(), "ReverseZDepthTest::InteriorDepth");
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(uint32_t);
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			winrt::com_ptr<ID3D11Buffer> output, staging;
			Require(SUCCEEDED(device->CreateBuffer(&desc, nullptr, output.put())), "Depth-order output creation failed");
			Util::SetResourceName(output.get(), "ReverseZDepthTest::Output");
			D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_R32_UINT;
			view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			view.Buffer.NumElements = 1;
			winrt::com_ptr<ID3D11UnorderedAccessView> uav;
			Require(SUCCEEDED(device->CreateUnorderedAccessView(output.get(), &view, uav.put())), "Depth-order UAV creation failed");
			Util::SetResourceName(uav.get(), "ReverseZDepthTest::Output UAV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Require(SUCCEEDED(device->CreateBuffer(&desc, nullptr, staging.put())), "Depth-order readback creation failed");
			Util::SetResourceName(staging.get(), "ReverseZDepthTest::Readback");
			auto* outputView = uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &outputView, nullptr);
			context->CSSetShader(shader.get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.get(), output.get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Require(SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)), "Depth-order readback failed");
			const auto result = *static_cast<const uint32_t*>(mapped.pData);
			context->Unmap(staging.get(), 0);
			Require(result == (reversed ? 12u : 4u), "Depth validation lost reversed far precision or accepted an endpoint");
		}
	}
}

int main()
{
	try {
		TestMatrices();
		TestShaderRegistry();
		TestMaskIdentity();
		TestSceneVertexShaders();
		TestInteriorDepth();
		TestUnderwaterMaskInputs();
		TestUpscaleDepthKernel();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
