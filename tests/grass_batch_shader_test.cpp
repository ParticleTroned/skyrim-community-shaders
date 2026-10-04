#include "Features/GrassDrawBatch.h"
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <memory>

namespace
{
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	struct Includes : ID3DInclude
	{
		std::vector<std::pair<std::filesystem::path, std::unique_ptr<Util::CustomInclude>>> roots;
		Includes()
		{
			roots.emplace_back("package/Shaders", std::make_unique<Util::CustomInclude>("package/Shaders"));
			for (const auto& feature : std::filesystem::directory_iterator("features"))
				if (std::filesystem::is_directory(feature.path() / "Shaders"))
					roots.emplace_back(feature.path() / "Shaders", std::make_unique<Util::CustomInclude>(feature.path() / "Shaders"));
		}
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			for (auto& [path, root] : roots)
				if (std::filesystem::is_regular_file(path / name) && SUCCEEDED(root->Open(type, name, parent, data, size)))
					return S_OK;
			return E_FAIL;
		}
		HRESULT Close(LPCVOID data) override { return roots.front().second->Close(data); }
	};

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	ComPtr<ID3DBlob> Compile(const wchar_t* path, const char* target, std::vector<D3D_SHADER_MACRO> defines = {})
	{
		Includes includes;
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> bytecode, errors;
		const auto result = D3DCompileFromFile(path, defines.data(), &includes, "main", target,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return bytecode;
	}

	void ValidateGrassVariants()
	{
		for (bool vr : { false, true })
			for (bool enhanced : { false, true })
				for (bool depth : { false, true })
					for (bool collision : { false, true }) {
						std::vector<D3D_SHADER_MACRO> defines{ { "VSHADER", "1" }, { "GRASS_OPTIMIZATIONS", "1" } };
						if (vr)
							defines.push_back({ "VR", "1" });
						if (enhanced)
							defines.push_back({ "GRASS_LIGHTING", "1" });
						if (depth)
							defines.push_back({ "RENDER_DEPTH", "1" });
						if (collision)
							defines.push_back({ "GRASS_COLLISION", "1" });
						auto bytecode = Compile(L"package/Shaders/RunGrass.hlsl", "vs_5_0", defines);
						ComPtr<ID3D11ShaderReflection> reflection;
						Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
						D3D11_SHADER_INPUT_BIND_DESC binding{};
						Check(reflection->GetResourceBindingDescByName("GrassBatch", &binding));
						Require(binding.BindPoint == 9, "Batch constants changed register");
						Check(reflection->GetResourceBindingDescByName("GrassInstanceFades", &binding));
						Require(binding.BindPoint == 2, "Batch fade changed register");
						D3D11_SHADER_VARIABLE_DESC world{}, previous{};
						auto geometry = reflection->GetConstantBufferByName("PerGeometry");
						Check(geometry->GetVariableByName("World")->GetDesc(&world));
						Check(geometry->GetVariableByName("PreviousWorld")->GetDesc(&previous));
						Require(world.StartOffset == (vr ? 256u : 128u) && previous.StartOffset == (vr ? 384u : 192u), "Native geometry layout changed");
					}
	}

	void RunFadeShader()
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		auto bytecode = Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassBatchFadeCS.hlsl", "cs_5_0");
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "GrassBatchTest::FadeCS");
		const std::array<GrassDrawBatch::Range, 3> ranges{ GrassDrawBatch::Range{ 3, 0 }, { 132, 7 }, { 134, 959 } };
		std::array<float, 960> nativeFades{};
		nativeFades[0] = 0.125f;
		nativeFades[7] = 0.5f;
		nativeFades[959] = 0.875f;
		const std::array<UINT, 4> params{ 134, 3, 0, 0 };
		const auto makeBuffer = [&](const void* data, UINT size, UINT bind, UINT stride, const char* name) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = size;
			desc.BindFlags = bind;
			desc.StructureByteStride = stride;
			desc.MiscFlags = stride ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : 0;
			D3D11_SUBRESOURCE_DATA initial{ data, 0, 0 };
			ComPtr<ID3D11Buffer> result;
			Check(device->CreateBuffer(&desc, data ? &initial : nullptr, result.GetAddressOf()));
			Util::SetResourceName(result.Get(), "%s", name);
			return result;
		};
		auto paramsBuffer = makeBuffer(params.data(), sizeof(params), D3D11_BIND_CONSTANT_BUFFER, 0, "GrassBatchTest::Params");
		auto fades = makeBuffer(nativeFades.data(), sizeof(nativeFades), D3D11_BIND_CONSTANT_BUFFER, 0, "GrassBatchTest::NativeFade");
		auto table = makeBuffer(ranges.data(), sizeof(ranges), D3D11_BIND_SHADER_RESOURCE, sizeof(ranges[0]), "GrassBatchTest::Ranges");
		std::array<float, 192> initial;
		initial.fill(-1);
		auto output = makeBuffer(initial.data(), sizeof(initial), D3D11_BIND_UNORDERED_ACCESS, sizeof(float), "GrassBatchTest::Output");
		ComPtr<ID3D11ShaderResourceView> srv;
		Check(device->CreateShaderResourceView(table.Get(), nullptr, srv.GetAddressOf()));
		Util::SetResourceName(srv.Get(), "GrassBatchTest::Ranges SRV");
		ComPtr<ID3D11UnorderedAccessView> uav;
		Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
		Util::SetResourceName(uav.Get(), "GrassBatchTest::Output UAV");
		ID3D11Buffer* buffers[]{ paramsBuffer.Get(), fades.Get() };
		auto rawSrv = srv.Get();
		auto rawUav = uav.Get();
		context->CSSetConstantBuffers(0, 2, buffers);
		context->CSSetShaderResources(0, 1, &rawSrv);
		context->CSSetUnorderedAccessViews(0, 1, &rawUav, nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(3, 1, 1);
		rawUav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &rawUav, nullptr);
		D3D11_BUFFER_DESC desc{};
		output->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.BindFlags = 0;
		ComPtr<ID3D11Buffer> staging;
		Check(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()));
		Util::SetResourceName(staging.Get(), "GrassBatchTest::Readback");
		context->CopyResource(staging.Get(), output.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		const auto values = static_cast<const float*>(mapped.pData);
		bool correct = true;
		for (UINT i = 0; i < initial.size(); ++i)
			correct = correct && values[i] == (i < 3 ? 0.125f : i < 132 ? 0.5f :
															i < 134     ? 0.875f :
																		  -1.0f);
		context->Unmap(staging.Get(), 0);
		Require(correct, "Fade transfer changed a native fade or wrote past the instance count");
	}
}

int main()
{
	try {
		ValidateGrassVariants();
		RunFadeShader();
		std::cout << "16 native grass vertex variants and sparse-group WARP fade dispatch passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
