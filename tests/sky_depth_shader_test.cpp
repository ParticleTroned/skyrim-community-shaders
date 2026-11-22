#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Matrix = std::array<float, 16>;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct Technique
	{
		std::vector<const char*> defines;
		float units;
		bool textured;
	};

	ComPtr<ID3DBlob> Compile(const Technique& technique, bool vr, bool reverse, const char* stage,
		std::span<const char* const> features = {})
	{
		std::vector<D3D_SHADER_MACRO> defines{ { std::strcmp(stage, "ps_5_0") == 0 ? "PSHADER" : "VSHADER", "1" } };
		for (const auto* define : technique.defines)
			defines.push_back({ define, "1" });
		for (const auto* feature : features)
			defines.push_back({ feature, "1" });
		if (vr)
			defines.push_back({ "VR", "1" });
		if (reverse)
			defines.push_back({ "REVERSE_Z", "1" });
		defines.push_back({ nullptr, nullptr });
		auto includes = PackageIncludes::AllFeatures(".");
		const char* source = R"(
#define main SkyMain
#include "Sky.hlsl"
#undef main
cbuffer TestInput : register(b0) { uint Eye; };
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(1, 1, 1)] void main() {
    VS_INPUT input = (VS_INPUT)0;
    input.Position = float4(0, 0, 1, 1);
#if defined(VR)
    input.InstanceID = Eye;
#endif
    Result[0] = SkyMain(input).Position;
}
)";
		ComPtr<ID3DBlob> code, errors;
		const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		const HRESULT result = std::strcmp(stage, "cs_5_0") == 0 ?
		                           D3DCompile(source, std::strlen(source), "sky-depth-test", defines.data(), &includes, "main", stage, flags, 0, &code, &errors) :
		                           D3DCompileFromFile(L"package/Shaders/Sky.hlsl", defines.data(), &includes, "main", stage, flags, 0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	unsigned Run(ID3D11Device* device, ID3D11DeviceContext* context, const Technique& technique, bool vr, bool reverse)
	{
		for (const auto& features : { std::vector<const char*>{}, { "CLOUD_SHADOWS" },
				 { "CLOUD_SHADOWS", "DEFERRED" }, { "CLOUD_SHADOWS", "DEFERRED", "CHARACTER_CATEGORY" } }) {
			Compile(technique, vr, reverse, "vs_5_0", features);
			Compile(technique, vr, reverse, "ps_5_0", features);
		}
		auto code = Compile(technique, vr, reverse, "cs_5_0");
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
		Util::SetResourceName(shader.Get(), "SkyDepthTest::CS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		auto optionalBuffer = [&](const char* name) -> std::unique_ptr<ConstantBuffer> {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDescByName(name, &binding)))
				return {};
			return std::make_unique<ConstantBuffer>(device, reflection.Get(), name);
		};
		ConstantBuffer geometry(device, reflection.Get(), "PerGeometry");
		auto frame = optionalBuffer("FrameBuffer::PerFrame");
		auto permutation = optionalBuffer("Permutation::PerShader");
		auto input = optionalBuffer("TestInput");
		auto stereo = optionalBuffer("VRValues");
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = desc.StructureByteStride = 4 * sizeof(float);
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		ComPtr<ID3D11Buffer> output, readback;
		Check(device->CreateBuffer(&desc, nullptr, &output));
		Util::SetResourceName(output.Get(), "SkyDepthTest::Output");
		ComPtr<ID3D11UnorderedAccessView> uav;
		Check(device->CreateUnorderedAccessView(output.Get(), nullptr, &uav));
		Util::SetResourceName(uav.Get(), "SkyDepthTest::Output UAV");
		desc.BindFlags = desc.MiscFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateBuffer(&desc, nullptr, &readback));
		Util::SetResourceName(readback.Get(), "SkyDepthTest::Readback");
		unsigned checked = 0;
		for (uint32_t reversedEyes : { 0u, 1u, 2u, 3u })
			for (bool moon : { false, true })
				for (float clipW : { 0.5f, 1.0f, 64.0f })
					for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye) {
						context->ClearState();
						std::array<Matrix, 2> world{}, projection{};
						for (uint32_t i = 0; i < 2; ++i) {
							world[i][0] = world[i][5] = world[i][10] = 1;
							world[i][15] = clipW;
							projection[i][10] = (reversedEyes & (1u << i)) ? -1.0f : 1.0f;
							projection[i][14] = 1;
						}
						if (vr)
							geometry.SetVariable("WorldViewProj", world);
						else
							geometry.SetVariable("WorldViewProj", world[0]);
						geometry.Bind(context);
						if (frame) {
							if (vr)
								frame->SetVariable("FrameBuffer::CameraProj", projection);
							else
								frame->SetVariable("FrameBuffer::CameraProj", projection[0]);
							frame->Bind(context);
						}
						if (permutation) {
							permutation->SetVariable("Permutation::ExtraShaderDescriptor", 0xFFFFFFFFu);
							permutation->SetVariable("Permutation::ExtraFeatureDescriptor", moon ? 1u << 11 : 0u);
							permutation->Bind(context);
						}
						if (input) {
							input->SetVariable("Eye", eye);
							input->Bind(context);
						}
						if (stereo) {
							stereo->SetVariable("StereoEnabled", 1.0f);
							stereo->Bind(context);
						}
						auto* view = uav.Get();
						context->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
						context->CSSetShader(shader.Get(), nullptr, 0);
						context->Dispatch(1, 1, 1);
						context->ClearState();
						context->CopyResource(readback.Get(), output.Get());
						D3D11_MAPPED_SUBRESOURCE mapped{};
						Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
						std::array<float, 4> position;
						std::memcpy(position.data(), mapped.pData, sizeof(position));
						context->Unmap(readback.Get(), 0);
						const float units = technique.units != 0 ? technique.units : (technique.textured && moon ? 9.0f : 0.0f);
						const float expected = reverse && (reversedEyes & (1u << eye)) ? units / 16777216.0f : 1.0f;
						Require(position[3] == clipW && position[2] / position[3] == expected,
							"Production sky depth changed standard projection, layer order, eye selection or flag ownership");
						++checked;
					}
		return checked;
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, nullptr, &context));
		const std::array techniques{
			Technique{ { "TEX", "MOONMASK" }, 3, true }, Technique{ { "TEX", "CLOUDS" }, 9, true },
			Technique{ { "TEX", "CLOUDS", "TEXLERP" }, 9, true }, Technique{ { "TEX" }, 0, true },
			Technique{ { "TEX", "CLOUDS", "TEXFADE" }, 9, true }, Technique{ { "TEX", "DITHER" }, 0, true },
			Technique{ { "DITHER" }, 0, false }, Technique{ { "HORIZFADE" }, 0, false }, Technique{ { "OCCLUSION" }, 0, false }
		};
		unsigned checked = 0;
		for (bool vr : { false, true })
			for (bool reverse : { false, true })
				for (const auto& technique : techniques)
					checked += Run(device.Get(), context.Get(), technique, vr, reverse);
		std::cout << checked << " production sky depth cases and 288 VS/PS permutations passed on WARP (SE/AE and VR)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
