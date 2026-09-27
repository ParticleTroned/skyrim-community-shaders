#define NOMINMAX
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct ShaderIncludes : ID3DInclude
	{
		Util::CustomInclude package{ "package/Shaders" };
		Util::CustomInclude feature{ "features/Upscaling/Shaders" };
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			return std::string_view(name).starts_with("Upscaling/") ?
			           feature.Open(type, name, parent, data, size) :
			           package.Open(type, name, parent, data, size);
		}
		HRESULT Close(LPCVOID data) override { return package.Close(data); }
	};

	ComPtr<ID3DBlob> Compile(const wchar_t* path, bool vr, bool allowCooperativeCacheWarnings = false)
	{
		ShaderIncludes includes;
		const D3D_SHADER_MACRO defines[] = { { "VR", "1" }, { nullptr, nullptr } };
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(path, vr ? defines : defines + 1, &includes, "main", "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | (allowCooperativeCacheWarnings ? 0 : D3DCOMPILE_WARNINGS_ARE_ERRORS),
			0, code.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		if (errors && allowCooperativeCacheWarnings) {
			// Periphery cache reads follow cooperative writes and a group barrier.
			std::istringstream diagnostics(static_cast<const char*>(errors->GetBufferPointer()));
			for (std::string line; std::getline(diagnostics, line);) {
				if (line.empty() || line == "\r")
					continue;
				const bool knownCacheWarning =
					line.find("warning X4000: use of potentially uninitialized variable (LoadCachedDepthClamped)") != std::string::npos ||
					line.find("warning X4000: use of potentially uninitialized variable (LoadCachedCurrentColorClamped)") != std::string::npos;
				Require(knownCacheWarning, "Unexpected periphery shader diagnostic");
			}
		}
		return code;
	}

	void CheckProductionLayouts(ID3D11Device* device, bool vr)
	{
		struct Layout
		{
			const wchar_t* path;
			const char* buffer;
			const char* field;
			UINT size, offset;
		};
		for (const auto& layout : {
				 Layout{ L"features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl", "FoveatedCenterBlendCB", "BlendFalloff", 64, 60 },
				 Layout{ L"features/Upscaling/Shaders/Upscaling/FoveatedSpatialCompositeCS.hlsl", "FoveatedSpatialCompositeCB", "Tuning", 96, 80 },
				 Layout{ L"features/Upscaling/Shaders/Upscaling/PeripheryTAACS.hlsl", "PeripheryTAACB", "BlendTuning", 336, 320 } }) {
			auto code = Compile(layout.path, vr, std::string_view(layout.buffer) == "PeripheryTAACB");
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			ConstantBuffer buffer(device, reflection.Get(), layout.buffer);
			Require(buffer.bytes.size() == layout.size && buffer.Offset(layout.field) == layout.offset, "Production blend buffer layout mismatch");
		}
	}

	void CheckWeights(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		auto code = Compile(L"tests/foveated_blend_curve.hlsl", vr);
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "FoveatedBlendTest::CS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer constants(device, reflection.Get(), "Samples");
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = 256;
		desc.Height = 128;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		ComPtr<ID3D11Texture2D> output, staging;
		ComPtr<ID3D11UnorderedAccessView> uav;
		Check(device->CreateTexture2D(&desc, nullptr, output.GetAddressOf()));
		Util::SetResourceName(output.Get(), "FoveatedBlendTest::Output");
		Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
		Util::SetResourceName(uav.Get(), "FoveatedBlendTest::Output UAV");
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
		Util::SetResourceName(staging.Get(), "FoveatedBlendTest::Readback");
		for (const auto& geometry : { Pixel{ 0.3f, 0.05f, 1.0f, 0 }, Pixel{ 0.6f, 0.1f, 2.0f, 0 }, Pixel{ 0.25f, 0.0f, 1.5f, 0 } }) {
			context->ClearState();
			constants.SetVariable("Geometry", geometry);
			constants.Bind(context);
			ID3D11UnorderedAccessView* target = uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(32, 16, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<Pixel> pixels(desc.Width * desc.Height);
			for (UINT y = 0; y < desc.Height; ++y)
				std::memcpy(pixels.data() + y * desc.Width, static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch, desc.Width * sizeof(Pixel));
			context->Unmap(staging.Get(), 0);
			unsigned inside = 0, outside = 0, curved = 0;
			for (UINT y = 0; y < desc.Height; ++y) {
				for (UINT x = 0; x < desc.Width; ++x) {
					const auto& p = pixels[y * desc.Width + x];
					Require(p[0] == p[1], "Neutral curve must equal legacy arithmetic exactly");
					for (float weight : p)
						Require(std::isfinite(weight) && weight >= 0 && weight <= 1, "Blend weight out of bounds");
					if (p[0] == 0 || p[0] == 1) {
						Require(p[2] == p[0] && p[3] == p[0], "Curve must preserve zero/full-weight ownership");
						inside += p[0] == 1;
						outside += p[0] == 0;
					} else {
						Require(p[2] + 2e-5f >= p[0] && p[3] <= p[0] + 2e-5f, "Falloff direction reversed");
						curved += p[2] > p[0] && p[3] < p[0];
					}
					if (x < 128) {
						const auto& mirror = pixels[y * desc.Width + (255 - x)];
						for (size_t i = 0; i < p.size(); ++i)
							Require(std::abs(p[i] - mirror[i]) < 2e-4f, "Mirrored eye offsets must give matching blends");
					}
				}
			}
			Require(inside > 0 && outside > 0, "Fixture must cover both ownership regions");
			if (geometry[1] > 0)
				Require(curved > 0, "Fixture must exercise a changed blend band");
		}
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		for (bool vr : { false, true }) {
			CheckProductionLayouts(device.Get(), vr);
			CheckWeights(device.Get(), context.Get(), vr);
		}
		std::cout << "PASS: neutral/bounds/stereo FOV curve samples and production buffer layouts\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
