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
#include <bit>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>
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
		for (const auto& [shader, nearPlane] : {
				 std::pair{ "float4 main(float3 p:POSITION):SV_Position{return float4(p.xy,0,1);}", true },
				 std::pair{ "float4 main(float3 p:POSITION, float2 unused:TEXCOORD0):SV_Position{return float4(p.xy,0,1);}", true },
				 std::pair{ "float4 main(float3 p:POSITION):SV_Position{return float4(p.xy,1,1);}", false },
				 std::pair{ "float4 main(float3 p:POSITION):SV_Position{return float4(p,1);}", false },
				 std::pair{ "cbuffer C {float4x4 m;} float4 main(float3 p:POSITION):SV_Position{return mul(m,float4(p,1));}", false } }) {
			Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
			Require(SUCCEEDED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors)), "Mask identity fixture did not compile");
			Require(Depth::IsFixedNearPlaneVertexProgram(code->GetBufferPointer(), code->GetBufferSize()) == nearPlane, "Mask adapter matched the wrong vertex program");
			Microsoft::WRL::ComPtr<ID3DBlob> stripped;
			Require(SUCCEEDED(D3DStripShader(code->GetBufferPointer(), code->GetBufferSize(), D3DCOMPILER_STRIP_REFLECTION_DATA, &stripped)),
				"Mask identity fixture could not strip reflection data");
			Require(Depth::IsFixedNearPlaneVertexProgram(stripped->GetBufferPointer(), stripped->GetBufferSize()) == nearPlane,
				"Mask adapter requires optional reflection data");
		}
	}
	std::string WaterFunction(const std::string& source, const char* signature)
	{
		const auto start = source.find(signature);
		Require(start != std::string::npos, "Production Water helper missing");
		auto end = source.find('{', start);
		unsigned depth = 1;
		while (++end < source.size() && depth) {
			if (source[end] == '{')
				++depth;
			if (source[end] == '}')
				--depth;
		}
		Require(depth == 0, "Production Water helper is incomplete");
		return source.substr(start, end - start);
	}
	void TestWaterProjection(bool shadowDepth = false)
	{
		using D3D11ShaderTest::Check;
		std::ifstream waterFile("package/Shaders/Water.hlsl");
		const std::string water{ std::istreambuf_iterator<char>(waterFile), {} };
		std::ifstream shadowFile("package/Shaders/Common/ShadowSampling.hlsli");
		const std::string shadow{ std::istreambuf_iterator<char>(shadowFile), {} };
		const auto source = std::string("cbuffer TestInputs : register(b0) { float4 Samples[8]; float4 CameraDataWater; };\n") +
		                    (shadowDepth ? std::string("#include \"Common/FrameBuffer.hlsli\"\nnamespace ShadowSampling {\n") +
											   WaterFunction(shadow, "float GetShadowDepth(") + "\n}\n" :
										   std::string("#include \"Common/ReverseZ.hlsli\"\n#ifdef VR\n") +
											   WaterFunction(water, "float CalculateDepthMultFromUV(") +
											   "\n#endif\n" +
											   WaterFunction(water, "float ResolveScreenDepthWater(")) +
		                    R"(
RWBuffer<float> Output : register(u0);
[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
    float4 sample = Samples[id.x];
#ifdef TEST_SHADOW_DEPTH
    Output[id.x] = ShadowSampling::GetShadowDepth(sample.xyz, (uint)sample.w);
#elif defined(VR)
    Output[id.x] = CalculateDepthMultFromUV(sample.xy, ResolveScreenDepthWater(sample.z), (uint)sample.w);
#else
    Output[id.x] = ResolveScreenDepthWater(sample.z);
#endif
})";
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		for (bool vr : { false, true })
			for (const auto& [reversed, reverseProjection] : { std::pair{ false, false }, std::pair{ true, true }, std::pair{ true, false } }) {
				D3D_SHADER_MACRO defines[]{ { vr ? "VR" : "FLAT", nullptr }, { reversed ? "REVERSE_Z" : "STANDARD_Z", nullptr },
					{ shadowDepth ? "TEST_SHADOW_DEPTH" : "TEST_WATER_PROJECTION", nullptr }, { nullptr, nullptr } };
				PackageIncludes includes{ "package/Shaders" };
				winrt::com_ptr<ID3DBlob> code, errors;
				const auto result = D3DCompile(source.data(), source.size(), "WaterProjection", defines, &includes,
					"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put());
				if (errors)
					std::cerr << static_cast<const char*>(errors->GetBufferPointer());
				Check(result);
				winrt::com_ptr<ID3D11ShaderReflection> reflection;
				Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.put())));
				std::optional<D3D11ShaderTest::ConstantBuffer> frame;
				if (vr || shadowDepth)
					frame.emplace(device.get(), reflection.get(), "FrameBuffer::PerFrame");
				D3D11ShaderTest::ConstantBuffer inputs(device.get(), reflection.get(), "TestInputs");
				const auto setCameraMatrix = [&](const char* name, const std::array<Matrix, 2>& matrices) {
					if (vr)
						frame->SetVariable(name, matrices);
					else
						frame->SetVariable(name, matrices[0]);
				};
				std::array<Matrix, 2> projections, inverses;
				std::array<Matrix, 2> views, viewProjections;
				std::array<XMFLOAT4, 8> samples;
				std::array<float, 8> expected;
				const float nearPlane = shadowDepth ? 5.0f : 1.0f, farPlane = shadowDepth ? 50000.0f : 100000.0f;
				for (unsigned eye = 0; eye < 2; ++eye) {
					const auto cameraEye = vr ? eye : 0;
					const float offset = cameraEye ? 0.03f : -0.03f;
					const auto standard = XMMatrixPerspectiveOffCenterLH(-0.8f + offset, 1.2f + offset, -0.7f, 0.9f, nearPlane, farPlane);
					auto native = Store(standard);
					if (reverseProjection)
						Depth::SetReverseProjection(native, nearPlane, farPlane);
					projections[eye] = Store(XMMatrixTranspose(Load(native)));
					inverses[eye] = Store(XMMatrixTranspose(XMMatrixInverse(nullptr, Load(native))));
					const float yaw = 0.3f * cameraEye, pitch = 0.4f * cameraEye;
					const auto cameraView = XMMatrixLookToLH(XMVectorSet(5.0f * cameraEye, -3.0f * cameraEye, 2.0f * cameraEye, 1),
						XMVectorSet(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch), 0), XMVectorSet(0, 1, 0, 0));
					views[eye] = Store(XMMatrixTranspose(cameraView));
					viewProjections[eye] = Store(XMMatrixTranspose(XMMatrixMultiply(cameraView, Load(native))));
					for (unsigned point = 0; point < 4; ++point) {
						const float distance = shadowDepth ? std::array{ 0.0f, 10.0f, 10000.0f, 200000.0f }[point] :
						                                     std::array{ 1.0f, 10.0f, 1000.0f, 10000.0f }[point];
						if (shadowDepth) {
							samples[eye * 4 + point] = { 0.3f * distance, 0.2f, distance, float(cameraEye) };
							expected[eye * 4 + point] = XMVectorGetZ(XMVector3TransformCoord(XMLoadFloat4(&samples[eye * 4 + point]), cameraView));
							continue;
						}
						const auto clip = XMVector4Transform(XMVectorSet(offset, 0.2f, distance, 1), standard);
						const float standardDepth = XMVectorGetZ(clip) / XMVectorGetW(clip);
						const float x = 0.25f + 0.1f * point, y = 0.35f + 0.05f * eye;
						samples[eye * 4 + point] = { x, y, reversed ? 1.0f - standardDepth : standardDepth, float(cameraEye) };
						if (!vr) {
							expected[eye * 4 + point] = distance;
							continue;
						}
						const auto adjusted = XMVectorSet(2 * x - 1, 2 * y - 1, standardDepth * 1.01f - 0.01f, 1);
						const auto view = XMVector4Transform(adjusted, XMMatrixInverse(nullptr, standard));
						expected[eye * 4 + point] = XMVectorGetX(XMVector3Length(XMVectorScale(view, 1.0f / XMVectorGetW(view))));
					}
				}
				if (reversed && frame)
					setCameraMatrix("FrameBuffer::CameraProj", projections);
				if (shadowDepth) {
					setCameraMatrix("FrameBuffer::CameraView", views);
					setCameraMatrix("FrameBuffer::CameraViewProj", viewProjections);
				} else if (vr)
					setCameraMatrix("FrameBuffer::CameraProjInverse", inverses);
				else
					inputs.SetVariable("CameraDataWater", XMFLOAT4{ farPlane, nearPlane, farPlane - nearPlane, farPlane * nearPlane });
				inputs.SetVariable("Samples", samples);
				if (frame)
					frame->Bind(context.get());
				inputs.Bind(context.get());
				winrt::com_ptr<ID3D11ComputeShader> shader;
				Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.put()));
				Util::SetResourceName(shader.get(), "ReverseZDepthTest::WaterProjection");
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = sizeof(expected);
				desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
				winrt::com_ptr<ID3D11Buffer> output, staging;
				Check(device->CreateBuffer(&desc, nullptr, output.put()));
				Util::SetResourceName(output.get(), "ReverseZDepthTest::WaterOutput");
				D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
				view.Format = DXGI_FORMAT_R32_FLOAT;
				view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
				view.Buffer.NumElements = static_cast<UINT>(expected.size());
				winrt::com_ptr<ID3D11UnorderedAccessView> uav;
				Check(device->CreateUnorderedAccessView(output.get(), &view, uav.put()));
				Util::SetResourceName(uav.get(), "ReverseZDepthTest::WaterOutput UAV");
				desc.BindFlags = 0;
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Check(device->CreateBuffer(&desc, nullptr, staging.put()));
				Util::SetResourceName(staging.get(), "ReverseZDepthTest::WaterReadback");
				auto* rawView = uav.get();
				context->CSSetUnorderedAccessViews(0, 1, &rawView, nullptr);
				context->CSSetShader(shader.get(), nullptr, 0);
				context->Dispatch(1, 1, 1);
				context->ClearState();
				context->CopyResource(staging.get(), output.get());
				D3D11_MAPPED_SUBRESOURCE mapped{};
				Check(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
				std::array<float, 8> actual;
				std::memcpy(actual.data(), mapped.pData, sizeof(actual));
				context->Unmap(staging.get(), 0);
				for (size_t i = 0; i < actual.size(); ++i)
					Require(std::isfinite(actual[i]) && std::abs(actual[i] - expected[i]) <= std::max(1e-4f, std::abs(expected[i]) * (shadowDepth ? 1e-5f : 0.005f)),
						shadowDepth ? "Shadow cascade distance does not match the receiver's view-space depth" :
									  "Water adjusted depth is incompatible with its eye inverse");
			}
	}
	void TestWaterShaders()
	{
		unsigned count = 0;
		for (bool vr : { false, true })
			for (bool reversed : { false, true })
				for (bool unified : { false, true })
					for (bool horizon : { false, true })
						for (const auto& [technique, alphaDepth, flowmap] : {
								 std::tuple{ "SIMPLE", false, false }, std::tuple{ "STENCIL", false, false },
								 std::tuple{ "LOD", false, false }, std::tuple{ "UNDERWATER", false, false },
								 std::tuple{ "SPECULAR", false, false }, std::tuple{ "SPECULAR", true, false },
								 std::tuple{ "SPECULAR", false, true } })
							for (const char* stage : { "VSHADER", "PSHADER" }) {
								std::vector<D3D_SHADER_MACRO> defines{ { stage, nullptr }, { "WATER", nullptr }, { "FOG", nullptr },
									{ technique, nullptr }, { "NUM_SPECULAR_LIGHTS", "0" }, { "VC", nullptr } };
								if (std::strcmp(technique, "SPECULAR") == 0 || std::strcmp(technique, "UNDERWATER") == 0) {
									defines.push_back({ "DEPTH", nullptr });
									defines.push_back({ "REFRACTIONS", nullptr });
									defines.push_back({ "NORMAL_TEXCOORD", nullptr });
								}
								if (alphaDepth)
									defines.push_back({ "VERTEX_ALPHA_DEPTH", nullptr });
								if (flowmap)
									defines.push_back({ "FLOWMAP", nullptr });
								if (vr)
									defines.push_back({ "VR", nullptr });
								if (reversed)
									defines.push_back({ "REVERSE_Z", nullptr });
								if (unified)
									defines.push_back({ "UNIFIED_WATER", nullptr });
								if (horizon)
									defines.push_back({ "HORIZON_FIX", nullptr });
								defines.push_back({ nullptr, nullptr });
								PackageIncludes includes{ "package/Shaders" };
								winrt::com_ptr<ID3DBlob> code, errors;
								const auto result = D3DCompileFromFile(L"package/Shaders/Water.hlsl", defines.data(), &includes, "main",
									std::strcmp(stage, "VSHADER") == 0 ? "vs_5_0" : "ps_5_0",
									D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put());
								if (FAILED(result) && errors)
									std::cerr << static_cast<const char*>(errors->GetBufferPointer());
								Require(SUCCEEDED(result), "Production Water shader failed to compile");
								++count;
							}
		std::cout << "Water: " << count << " flat/VR, depth, readiness and Horizon shader variants compiled\n";
	}
	void TestWaterDepthValidity()
	{
		using D3D11ShaderTest::Check;
		std::ifstream waterFile("package/Shaders/Water.hlsl");
		const std::string water{ std::istreambuf_iterator<char>(waterFile), {} };
		const auto source = std::string("#include \"Common/ReverseZ.hlsli\"\n#include \"Common/VR.hlsli\"\n") +
		                    WaterFunction(water, "namespace HorizonFix") +
		                    "\ncbuffer TestInputs : register(b0) { float4 Samples[32]; float4 DepthBounds; float4 WaterSurface; float4 ReflectPlane[2]; };\n" +
		                    WaterFunction(water, "bool IsUnifiedWaterRawDepthValid(") +
		                    WaterFunction(water, "bool TryGetUnifiedWaterScenePosition(") +
		                    "\n#ifdef VR\n" + WaterFunction(water, "bool TryGetUnifiedWaterVRColumn(") + "\n#endif\n" + R"(
RWBuffer<uint> Output : register(u0);
[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
    float rawDepth = Samples[id.x].x;
    uint eye = (uint)Samples[id.x].y;
    float2 uv = Samples[id.x].zw;
    float3 position;
    uint flags = (uint)IsUnifiedWaterRawDepthValid(rawDepth);
    flags |= (uint)TryGetUnifiedWaterScenePosition(uv, rawDepth, eye, position) << 1;
    Output[3 * id.x + 1] = asuint(position.z);
    Output[3 * id.x + 2] = 0;
#ifdef VR
    float3 surface = position + WaterSurface.xyz;
    float column;
    flags |= (uint)TryGetUnifiedWaterVRColumn(uv, rawDepth, DepthBounds.x, surface, eye, column) << 2;
    Output[3 * id.x + 2] = asuint(column);
    flags |= (uint)TryGetUnifiedWaterVRColumn(uv, rawDepth, DepthBounds.y, surface, eye, column) << 3;
    flags |= (uint)TryGetUnifiedWaterVRColumn(uv, rawDepth, DepthBounds.z, surface, eye, column) << 5;
#endif
    flags |= (uint)TryGetUnifiedWaterScenePosition(DepthBounds.zw, rawDepth, eye, position) << 4;
    Output[3 * id.x] = flags;
})";
		const float horizonMargin = 64.0f / 16777216.0f;
		// Expected bits select standard/reversed depth, then their Horizon Fix variants.
		const std::array<std::pair<float, unsigned>, 32> cases{
			std::pair{ 0.0f, 0u }, { -0.0f, 0u }, { 1.0f, 0u }, { -1e-10f, 0u },
			{ std::nextafter(1.0f, 2.0f), 0u }, { std::numeric_limits<float>::infinity(), 0u },
			{ -std::numeric_limits<float>::infinity(), 0u }, { std::numeric_limits<float>::quiet_NaN(), 0u },
			{ 1e-10f, 2u }, { std::numeric_limits<float>::min(), 2u }, { 1e-6f, 2u }, { 5e-6f, 10u },
			{ 1e-5f, 10u }, { std::nextafter(1e-5f, 1.0f), 15u }, { 2e-5f, 15u }, { 0.5f, 15u },
			{ std::nextafter(1.0f, 0.0f), 1u }, { 1.0f - 1e-6f, 1u }, { 1.0f - 5e-6f, 5u }, { 1.0f - 2e-5f, 15u },
			{ horizonMargin, 2u }, { std::nextafter(horizonMargin, 1.0f), 10u },
			{ 1.0f - horizonMargin, 1u }, { std::nextafter(1.0f - horizonMargin, 0.0f), 5u },
			{ 8.0f / 16777216.0f, 2u }, { 1.0f - 8.0f / 16777216.0f, 1u },
			{ -1.0f, 0u }, { 2.0f, 0u }, { std::nextafter(1e-5f, 0.0f), 10u },
			{ 1.0f - 1e-5f, 15u }, { std::nextafter(1.0f - 1e-5f, 1.0f), 5u }, { 1e-20f, 2u }
		};
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		unsigned checked = 0;
		for (bool vr : { false, true })
			for (bool reversed : { false, true })
				for (bool horizon : { false, true }) {
					D3D_SHADER_MACRO defines[]{ { vr ? "VR" : "FLAT", "1" }, { reversed ? "REVERSE_Z" : "STANDARD_Z", "1" },
						{ horizon ? "HORIZON_FIX" : "NO_HORIZON_FIX", "1" }, { nullptr, nullptr } };
					PackageIncludes includes{ "package/Shaders" };
					winrt::com_ptr<ID3DBlob> code, errors;
					const auto compiled = D3DCompile(source.data(), source.size(), "WaterDepthValidity", defines, &includes, "main", "cs_5_0",
						D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put());
					if (errors)
						std::cerr << static_cast<const char*>(errors->GetBufferPointer());
					Check(compiled);
					winrt::com_ptr<ID3D11ShaderReflection> reflection;
					Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.put())));
					D3D11ShaderTest::ConstantBuffer frame(device.get(), reflection.get(), "FrameBuffer::PerFrame");
					D3D11ShaderTest::ConstantBuffer inputs(device.get(), reflection.get(), "TestInputs");
					inputs.SetVariable("DepthBounds", XMFLOAT4{ reversed ? 1.0f : 0.0f, reversed ? 0.0f : 1.0f, std::numeric_limits<float>::quiet_NaN(), 0.5f });
					if (vr) {
						inputs.SetVariable("WaterSurface", XMFLOAT4{ 0, 0, 7.5f, 0 });
						inputs.SetVariable("ReflectPlane", std::array{ XMFLOAT4{ 0, 0, 2, 0 }, XMFLOAT4{ 0, 0, -3, 0 } });
					}
					winrt::com_ptr<ID3D11ComputeShader> shader;
					Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.put()));
					Util::SetResourceName(shader.get(), "ReverseZDepthTest::WaterValidity");
					D3D11_BUFFER_DESC desc{};
					desc.ByteWidth = static_cast<UINT>(cases.size() * 3 * sizeof(uint32_t));
					desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
					winrt::com_ptr<ID3D11Buffer> output, staging;
					Check(device->CreateBuffer(&desc, nullptr, output.put()));
					Util::SetResourceName(output.get(), "ReverseZDepthTest::WaterValidityOutput");
					D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
					view.Format = DXGI_FORMAT_R32_UINT;
					view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
					view.Buffer.NumElements = static_cast<UINT>(cases.size() * 3);
					winrt::com_ptr<ID3D11UnorderedAccessView> uav;
					Check(device->CreateUnorderedAccessView(output.get(), &view, uav.put()));
					Util::SetResourceName(uav.get(), "ReverseZDepthTest::WaterValidityOutput UAV");
					desc.BindFlags = 0;
					desc.Usage = D3D11_USAGE_STAGING;
					desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					Check(device->CreateBuffer(&desc, nullptr, staging.put()));
					Util::SetResourceName(staging.get(), "ReverseZDepthTest::WaterValidityReadback");
					for (unsigned matrixCase = 0; matrixCase < 3; ++matrixCase) {
						std::array<Matrix, 2> inverses;
						for (unsigned eye = 0; eye < 2; ++eye) {
							auto projection = Store(XMMatrixPerspectiveOffCenterLH(-0.8f + 0.03f * eye, 1.2f + 0.03f * eye, -0.7f, 0.9f, 1.0f, 100000.0f));
							if (reversed)
								Depth::SetReverseProjection(projection, 1, 100000);
							inverses[eye] = Store(XMMatrixTranspose(XMMatrixInverse(nullptr, Load(projection))));
							if (matrixCase == 1)
								inverses[eye].m[3][2] = inverses[eye].m[3][3] = 0.0f;
							if (matrixCase == 2)
								inverses[eye].m[2][3] = std::numeric_limits<float>::quiet_NaN();
						}
						if (vr)
							frame.SetVariable("FrameBuffer::CameraViewProjInverse", inverses);
						else
							frame.SetVariable("FrameBuffer::CameraViewProjInverse", inverses[0]);
						for (unsigned eye = 0; eye < (vr ? 2u : 1u); ++eye) {
							std::array<XMFLOAT4, cases.size()> samples{};
							for (size_t i = 0; i < cases.size(); ++i) samples[i] = { cases[i].first, float(eye), vr ? 0.25f + 0.5f * eye : 0.5f, 0.5f };
							inputs.SetVariable("Samples", samples);
							inputs.Bind(context.get());
							frame.Bind(context.get());
							auto* outputView = uav.get();
							context->CSSetUnorderedAccessViews(0, 1, &outputView, nullptr);
							context->CSSetShader(shader.get(), nullptr, 0);
							context->Dispatch(static_cast<UINT>(cases.size()), 1, 1);
							context->ClearState();
							context->CopyResource(staging.get(), output.get());
							D3D11_MAPPED_SUBRESOURCE mapped{};
							Check(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
							std::array<uint32_t, cases.size() * 3> actual;
							std::memcpy(actual.data(), mapped.pData, sizeof(actual));
							context->Unmap(staging.get(), 0);
							for (size_t i = 0; i < cases.size(); ++i) {
								const bool valid = (cases[i].second & (1u << (unsigned(reversed) + 2u * unsigned(horizon)))) != 0;
								const uint32_t expected = valid ? (matrixCase == 0 ? (vr ? 7u : 3u) : 1u) : 0u;
								if (actual[i * 3] != expected)
									throw std::runtime_error("Water depth validity case " + std::to_string(i) + " failed (VR=" + std::to_string(vr) +
															 ", reversed=" + std::to_string(reversed) + ", horizon=" + std::to_string(horizon) + ", matrix=" + std::to_string(matrixCase) + ")");
								if (valid && matrixCase == 0) {
									const float position = std::bit_cast<float>(actual[i * 3 + 1]);
									const double distance = Depth::Linearize(cases[i].first, 1, 100000, reversed ? Depth::Convention::Reversed : Depth::Convention::Standard);
									Require(std::isfinite(position) && std::abs(position - distance) <= distance * 0.005, "Admitted water depth failed perspective reconstruction");
								}
								if (vr) {
									const float column = std::bit_cast<float>(actual[i * 3 + 2]);
									const float expectedColumn = valid && matrixCase == 0 ? 7.5f : -1.0f;
									Require(std::isfinite(column) && std::abs(column - expectedColumn) <= 0.01f, "Water depth did not retain its shallow column or failure sentinel");
								}
								++checked;
							}
						}
					}
				}
		std::cout << "Water depth validity: " << checked << " endpoint, reconstruction and foreground cases passed\n";
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
	void TestReflectionHits()
	{
		using D3D11ShaderTest::Check;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		const char* source = R"(
#include "ISReflectionsRayTracing.hlsl"
RWBuffer<float4> Output : register(u0);
[numthreads(2, 1, 1)] void TestReflection(uint3 id : SV_DispatchThreadID) {
    uint eye = 0;
#ifdef VR
    eye = id.x;
#endif
    float3 start = float3(0.5, 0.5, 0.99);
    float3 direction = float3(0, 0.01, 0.02);
#ifdef REVERSE_Z
    start.z = 1 - start.z;
    direction.z = -direction.z;
#endif
    Output[id.x] = GetReflectionColor(direction, start, eye, uint2(8, 8)
#ifdef VR
        , 64, 6, 1
#endif
    );
})";
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = textureDesc.Height = 8;
		textureDesc.MipLevels = textureDesc.ArraySize = textureDesc.SampleDesc.Count = 1;
		textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		winrt::com_ptr<ID3D11Texture2D> depth;
		winrt::com_ptr<ID3D11ShaderResourceView> depthView;
		Check(device->CreateTexture2D(&textureDesc, nullptr, depth.put()));
		Util::SetResourceName(depth.get(), "ReverseZDepthTest::ReflectionDepth");
		Check(device->CreateShaderResourceView(depth.get(), nullptr, depthView.put()));
		Util::SetResourceName(depthView.get(), "ReverseZDepthTest::ReflectionDepth SRV");
		std::array<XMFLOAT4, 64> colours;
		colours.fill({ 0.25f, 0.5f, 0.75f, 0 });
		textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		D3D11_SUBRESOURCE_DATA colourData{ colours.data(), 8 * sizeof(XMFLOAT4), 0 };
		winrt::com_ptr<ID3D11Texture2D> colour;
		winrt::com_ptr<ID3D11ShaderResourceView> colourView;
		Check(device->CreateTexture2D(&textureDesc, &colourData, colour.put()));
		Util::SetResourceName(colour.get(), "ReverseZDepthTest::ReflectionColour");
		Check(device->CreateShaderResourceView(colour.get(), nullptr, colourView.put()));
		Util::SetResourceName(colourView.get(), "ReverseZDepthTest::ReflectionColour SRV");
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		winrt::com_ptr<ID3D11SamplerState> sampler;
		Check(device->CreateSamplerState(&samplerDesc, sampler.put()));
		Util::SetResourceName(sampler.get(), "ReverseZDepthTest::ReflectionSampler");
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = 2 * sizeof(XMFLOAT4);
		bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		winrt::com_ptr<ID3D11Buffer> output, staging;
		Check(device->CreateBuffer(&bufferDesc, nullptr, output.put()));
		Util::SetResourceName(output.get(), "ReverseZDepthTest::ReflectionOutput");
		D3D11_UNORDERED_ACCESS_VIEW_DESC outputDesc{};
		outputDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		outputDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		outputDesc.Buffer.NumElements = 2;
		winrt::com_ptr<ID3D11UnorderedAccessView> outputView;
		Check(device->CreateUnorderedAccessView(output.get(), &outputDesc, outputView.put()));
		Util::SetResourceName(outputView.get(), "ReverseZDepthTest::ReflectionOutput UAV");
		bufferDesc.BindFlags = 0;
		bufferDesc.Usage = D3D11_USAGE_STAGING;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateBuffer(&bufferDesc, nullptr, staging.put()));
		Util::SetResourceName(staging.get(), "ReverseZDepthTest::ReflectionReadback");
		for (bool vr : { false, true }) {
			for (bool reversed : { false, true }) {
				for (bool unifiedWater : { false, true }) {
					D3D_SHADER_MACRO defines[]{ { "PSHADER", "1" }, { "ENABLESSR", "1" }, { unifiedWater ? "UNIFIED_WATER" : "NATIVE_WATER", "1" },
						{ vr ? "VR" : "FLAT", "1" }, { reversed ? "REVERSE_Z" : "STANDARD_Z", "1" }, { nullptr, nullptr } };
					PackageIncludes includes{ "package/Shaders" };
					winrt::com_ptr<ID3DBlob> code, errors;
					winrt::com_ptr<ID3DBlob> pixelCode;
					const auto pixelCompiled = D3DCompileFromFile(L"package/Shaders/ISReflectionsRayTracing.hlsl", defines, &includes,
						"main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
						0, pixelCode.put(), errors.put());
					if (errors)
						std::cerr << static_cast<const char*>(errors->GetBufferPointer());
					Check(pixelCompiled);
					const auto compiled = D3DCompile(source, std::strlen(source), "ReflectionHits", defines, &includes,
						"TestReflection", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
						0, code.put(), errors.put());
					if (errors)
						std::cerr << static_cast<const char*>(errors->GetBufferPointer());
					Check(compiled);
					winrt::com_ptr<ID3D11ComputeShader> shader;
					Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.put()));
					Util::SetResourceName(shader.get(), "ReverseZDepthTest::ReflectionHits");
					winrt::com_ptr<ID3D11ShaderReflection> reflection;
					Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.put())));
					D3D11ShaderTest::ConstantBuffer frame(device.get(), reflection.get(), "FrameBuffer::PerFrame");
					D3D11ShaderTest::ConstantBuffer geometry(device.get(), reflection.get(), "PerGeometry");
					auto projection = Store(XMMatrixPerspectiveFovLH(1.5f, 1, 1, 100000));
					if (reversed)
						Depth::SetReverseProjection(projection, 1, 100000);
					const auto uploadedProjection = Store(XMMatrixTranspose(Load(projection)));
					const auto inverse = Store(XMMatrixTranspose(XMMatrixInverse(nullptr, Load(projection))));
					const std::array<float, 4> dimensions{ 1, 1, 1, 1 };
					const auto setMatrices = [&](const char* name, const Matrix& value) {
						if (vr)
							frame.SetVariable(name, std::array{ value, value });
						else
							frame.SetVariable(name, value);
					};
					setMatrices("FrameBuffer::CameraProj", uploadedProjection);
					setMatrices("FrameBuffer::CameraProjInverse", inverse);
					setMatrices("FrameBuffer::CameraViewProjInverse", inverse);
					setMatrices("FrameBuffer::CameraViewProj", uploadedProjection);
					setMatrices("FrameBuffer::CameraViewProjUnjittered", uploadedProjection);
					setMatrices("FrameBuffer::CameraPreviousViewProjUnjittered", uploadedProjection);
					frame.SetVariable("FrameBuffer::DynamicResolutionParams1", dimensions);
					frame.SetVariable("FrameBuffer::DynamicResolutionParams2", dimensions);
					geometry.SetVariable("DefaultNormal", std::array{ 0.0f, 1.0f, 0.0f });
					const auto order = reversed ? Depth::Convention::Reversed : Depth::Convention::Standard;
					const float ordinaryDepth = reversed ? 0.001f : 0.999f;
					const float nativeCutoff = reversed ? 1.0f - 0.9999f : 0.9999f;
					const float infinity = std::numeric_limits<float>::infinity();
					const float nan = std::numeric_limits<float>::quiet_NaN();
					struct Sample
					{
						float depth, radius;
						bool hit;
					};
					const std::array samples{
						Sample{ ordinaryDepth, 1, true },
						Sample{ reversed ? 1e-10f : std::nextafter(1.0f, 0.0f), 1, unifiedWater },
						Sample{ Depth::Near(order), 1, true },
						Sample{ Depth::Far(order), 1, false },
						Sample{ -0.01f, 1, false }, Sample{ 1.01f, 1, false },
						Sample{ infinity, 1, false }, Sample{ nan, 1, false },
						Sample{ ordinaryDepth, 0, false }, Sample{ ordinaryDepth, -1, false },
						Sample{ ordinaryDepth, infinity, false }, Sample{ ordinaryDepth, nan, false },
						Sample{ nativeCutoff, 1, true },
						Sample{ std::nextafter(nativeCutoff, Depth::Far(order)), 1, unifiedWater }
					};
					for (const auto& sample : samples) {
						geometry.SetVariable("SSRParams", std::array{ 1.0f, sample.radius, 0.0f, 1.0f });
						std::array<float, 64> depths;
						depths.fill(sample.depth);
						context->UpdateSubresource(depth.get(), 0, nullptr, depths.data(), 8 * sizeof(float), 0);
						frame.Bind(context.get());
						geometry.Bind(context.get());
						ID3D11ShaderResourceView* views[]{ colourView.get(), colourView.get(), depthView.get(), colourView.get() };
						ID3D11SamplerState* samplers[]{ sampler.get(), sampler.get(), sampler.get(), sampler.get() };
						auto* destination = outputView.get();
						context->CSSetShaderResources(0, 4, views);
						context->CSSetSamplers(0, 4, samplers);
						context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
						context->CSSetShader(shader.get(), nullptr, 0);
						context->Dispatch(1, 1, 1);
						context->ClearState();
						context->CopyResource(staging.get(), output.get());
						D3D11_MAPPED_SUBRESOURCE mapped{};
						Check(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
						std::array<XMFLOAT4, 2> results;
						std::memcpy(results.data(), mapped.pData, sizeof(results));
						context->Unmap(staging.get(), 0);
						for (const auto& actual : results) {
							Require(std::isfinite(actual.x) && std::isfinite(actual.y) && std::isfinite(actual.z) &&
										std::isfinite(actual.w) && actual.w >= 0 && actual.w <= 1,
								"Reflection returned a nonfinite colour or invalid confidence");
							Require(sample.hit ? actual.w > 0 : actual.w == 0,
								sample.hit ? "Reflection rejected valid geometry" : "Reflection accepted invalid depth or trace radius");
							if (sample.hit)
								Require(actual.x == 0.25f && actual.y == 0.5f && actual.z == 0.75f,
									"Reflection sampled the wrong source colour");
						}
					}
				}
			}
		}
		std::cout << "SSR: 224 ray checks across depth orders, both eyes and water modes passed\n";
	}
	void TestDepthHelper(const std::string& source, uint32_t standardExpected, uint32_t reversedExpected)
	{
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
					D3D11_SDK_VERSION, device.put(), nullptr, context.put())),
			"Depth-order WARP device failed");
		for (bool reversed : { false, true }) {
			D3D_SHADER_MACRO defines[]{ { reversed ? "REVERSE_Z" : "STANDARD_Z", "1" }, { nullptr, nullptr } };
			winrt::com_ptr<ID3DBlob> code, errors;
			const auto compiled = D3DCompile(source.data(), source.size(), nullptr, defines, nullptr, "main", "cs_5_0",
				D3DCOMPILE_IEEE_STRICTNESS, 0, code.put(), errors.put());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Require(SUCCEEDED(compiled), "Depth helper shader failed to compile");
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
			if (result != (reversed ? reversedExpected : standardExpected))
				throw std::runtime_error("Depth helper returned " + std::to_string(result) + (reversed ? " for reverse depth" : " for standard depth"));
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
		TestDepthHelper(source, 4, 12);
	}
	void TestUnderwaterGeometryDepth()
	{
		std::ifstream orderFile("package/Shaders/Common/DepthOrder.hlsli");
		std::string source{ std::istreambuf_iterator<char>(orderFile), {} };
		std::ifstream reverseFile("package/Shaders/Common/ReverseZ.hlsli");
		const std::string reverse{ std::istreambuf_iterator<char>(reverseFile), {} };
		std::ifstream maskFile("features/Upscaling/Shaders/Upscaling/UnderwaterMaskUpscalePS.hlsl");
		const std::string mask{ std::istreambuf_iterator<char>(maskFile), {} };
		source += "\nnamespace FrameBuffer {\n" + WaterFunction(reverse, "float FarPlaneDepth(") + "\n}\n" +
		          WaterFunction(mask, "bool IsUnderwaterGeometryDepth(") + R"(
RWBuffer<uint> Output : register(u0);
[numthreads(1, 1, 1)] void main() {
    uint valid = (uint)IsUnderwaterGeometryDepth(0) |
        ((uint)IsUnderwaterGeometryDepth(1) << 1) |
        ((uint)IsUnderwaterGeometryDepth(0.5) << 2) |
        ((uint)IsUnderwaterGeometryDepth(1e-10) << 3) |
        ((uint)IsUnderwaterGeometryDepth(1e-6) << 4) |
        ((uint)IsUnderwaterGeometryDepth(2e-6) << 5);
#ifdef REVERSE_Z
    valid |= (uint)(!IsUnderwaterGeometryDepth(asfloat(0x7fc00000)) &&
        !IsUnderwaterGeometryDepth(asfloat(0x7f800000)) &&
        !IsUnderwaterGeometryDepth(-0.01) && !IsUnderwaterGeometryDepth(1.01)) << 6;
#else
    valid |= 64;
#endif
    Output[0] = valid;
})";
		TestDepthHelper(source, 125, 126);
	}
}

int main()
{
	try {
		TestMatrices();
		TestWaterProjection();
		TestWaterProjection(true);
		TestWaterDepthValidity();
		TestWaterShaders();
		TestShaderRegistry();
		TestMaskIdentity();
		TestSceneVertexShaders();
		TestInteriorDepth();
		TestReflectionHits();
		TestUnderwaterGeometryDepth();
		TestUnderwaterMaskInputs();
		TestUpscaleDepthKernel();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
