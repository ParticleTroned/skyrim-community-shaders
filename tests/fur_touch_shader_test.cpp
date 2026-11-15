#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Vector = std::array<float, 4>;
	using Result = std::array<Vector, 4>;

	void Run(bool reference)
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		PackageIncludes includes("package/Shaders", "features/Fur Shells/Shaders");
		const D3D_SHADER_MACRO defines[]{ { "CSX_PERF_REFERENCE_FLAGS", reference ? "4" : "0" }, { nullptr, nullptr } };
		ComPtr<ID3DBlob> code, errors;
		const auto compiled = D3DCompileFromFile(L"tests/shaders/FurTouchTestCS.hlsl", defines, &includes, "main", "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, code.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(compiled);
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "FurTouchTest::Compute");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer fur(device.Get(), reflection.Get(), "FurShells::FurShellData");
		ConstantBuffer cases(device.Get(), reflection.Get(), "Cases");
		std::vector<Vector> positions;
		for (int z = -2; z <= 2; ++z)
			for (int y = -8; y <= 8; ++y)
				for (int x = -8; x <= 8; ++x)
					positions.push_back({ x * .5f, y * .5f, z * .5f, 0 });
		for (float reach : { 1.3f, 4.0f })
			for (float distance : { std::nextafter(reach, 0.0f), reach, std::nextafter(reach, std::numeric_limits<float>::infinity()),
					 reach * 1.0000005f, reach * 1.000002f })
				for (const auto direction : { std::array{ 1.0f, 0.0f, 0.0f }, std::array{ 0.6f, 0.8f, 0.0f }, std::array{ -0.8f, 0.0f, 0.6f } })
					positions.push_back({ direction[0] * distance, direction[1] * distance, -1 + direction[2] * distance, 0 });
		for (float distance : { 10000.0f, 1e20f, std::numeric_limits<float>::max() })
			positions.push_back({ distance, 0, -1, 0 });
		const auto makeBuffer = [&](UINT bytes, UINT stride, UINT bind, const void* initial, const char* name) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = bytes;
			desc.StructureByteStride = stride;
			desc.BindFlags = bind;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			D3D11_SUBRESOURCE_DATA data{ initial, 0, 0 };
			ComPtr<ID3D11Buffer> buffer;
			Check(device->CreateBuffer(&desc, initial ? &data : nullptr, buffer.GetAddressOf()));
			Util::SetResourceName(buffer.Get(), "%s", name);
			return buffer;
		};
		auto input = makeBuffer(UINT(positions.size() * sizeof(Vector)), sizeof(Vector), D3D11_BIND_SHADER_RESOURCE, positions.data(), "FurTouchTest::Positions");
		auto output = makeBuffer(UINT(positions.size() * sizeof(Result)), sizeof(Result), D3D11_BIND_UNORDERED_ACCESS, nullptr, "FurTouchTest::Results");
		ComPtr<ID3D11ShaderResourceView> inputView;
		ComPtr<ID3D11UnorderedAccessView> outputView;
		Check(device->CreateShaderResourceView(input.Get(), nullptr, inputView.GetAddressOf()));
		Check(device->CreateUnorderedAccessView(output.Get(), nullptr, outputView.GetAddressOf()));
		Util::SetResourceName(inputView.Get(), "FurTouchTest::Positions SRV");
		Util::SetResourceName(outputView.Get(), "FurTouchTest::Results UAV");
		D3D11_BUFFER_DESC stagingDesc{};
		output->GetDesc(&stagingDesc);
		stagingDesc.BindFlags = stagingDesc.MiscFlags = stagingDesc.StructureByteStride = 0;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Buffer> staging;
		Check(device->CreateBuffer(&stagingDesc, nullptr, staging.GetAddressOf()));
		Util::SetResourceName(staging.Get(), "FurTouchTest::Readback");
		std::array<Vector, 12> pointsA{}, pointsB{}, movements{};
		for (size_t i = 0; i < pointsA.size(); ++i) {
			pointsA[i] = { float(i % 3), float(i / 3), -1, i == 0 ? 1.0f : .25f * float(i % 4) };
			pointsB[i] = { pointsA[i][0], pointsA[i][1], i % 3 ? 1.0f : -1.0f, float(i % 2) };
			movements[i] = { .1f, -.1f, .05f, float(i + 1) / 12 };
		}
		cases.SetVariable("PositionCount", UINT(positions.size()));
		context->CSSetShader(shader.Get(), nullptr, 0);
		auto* srv = inputView.Get();
		context->CSSetShaderResources(0, 1, &srv);
		for (const auto translation : { std::array{ 0.0f, 0.0f, 0.0f }, std::array{ 100000.0f, -200000.0f, 8000.0f },
				 std::array{ 0.0039f, -0.0039f, 0.0078f } }) {
			auto translatedA = pointsA, translatedB = pointsB;
			for (size_t i = 0; i < pointsA.size(); ++i)
				for (size_t axis = 0; axis < translation.size(); ++axis) {
					translatedA[i][axis] += translation[axis];
					translatedB[i][axis] += translation[axis];
				}
			for (const char* state : { "FurShells::Current", "FurShells::Previous" }) {
				fur.SetMember(state, "TouchA", translatedA);
				fur.SetMember(state, "TouchB", translatedB);
				fur.SetMember(state, "TouchMove", movements);
				fur.SetMember(state, "TouchStrength", 1.0f);
			}
			cases.SetVariable("PositionOffset", translation);
			for (unsigned previous : { 0u, 1u })
				for (float length : { 0.0f, .1f, 1.0f, 12.0f })
					for (float shell : { 0.0f, .3f, 1.0f })
						for (float count : { 0.0f, 1.0f, 12.0f }) {
							fur.SetMember("FurShells::Current", "TouchCount", count);
							fur.SetMember("FurShells::Previous", "TouchCount", count);
							fur.Bind(context.Get());
							cases.SetVariable("PreviousState", previous);
							cases.SetVariable("FurLength", length);
							cases.SetVariable("Shell", shell);
							cases.Bind(context.Get());
							auto* uav = outputView.Get();
							context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
							context->Dispatch((UINT(positions.size()) + 63) / 64, 1, 1);
							uav = nullptr;
							context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
							context->CopyResource(staging.Get(), output.Get());
							D3D11_MAPPED_SUBRESOURCE mapped{};
							Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
							bool matches = true;
							const auto* results = static_cast<const Result*>(mapped.pData);
							for (size_t i = 0; i < positions.size(); ++i)
								for (size_t field = 0; field < 2; ++field)
									for (size_t axis = 0; axis < 4; ++axis)
										matches &= std::isfinite(results[i][field][axis]) && std::abs(results[i][field][axis] - results[i][field + 2][axis]) <= 2e-6f;
							context->Unmap(staging.Get(), 0);
							if (!matches)
								throw std::runtime_error("Conservative fur capsule rejection changed contact displacement or debug coverage");
						}
		}
	}
}

int main()
{
	try {
		Run(false);
		Run(true);
		std::cout << "Fur touch shader matches unfiltered contact evaluation\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
