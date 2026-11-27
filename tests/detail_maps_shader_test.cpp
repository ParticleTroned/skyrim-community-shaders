#define NOMINMAX
#include "Features/DetailMapsPolicy.h"
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <iostream>
#include <winrt/base.h>

struct DetailMaps
{
#include "detail_maps_bindings_decl_under_test.h"
};
constexpr auto PerDrawSlot = DetailMapsPolicy::ConstantSlot;
constexpr auto TextureSlot = DetailMapsPolicy::TextureSlot;
constexpr auto SamplerSlot = DetailMapsPolicy::SamplerSlot;
#include "detail_maps_bindings_under_test.h"

namespace
{
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	struct Includes : ID3DInclude
	{
		std::vector<std::unique_ptr<PackageIncludes>> roots;
		Includes()
		{
			roots.push_back(std::make_unique<PackageIncludes>("package/Shaders"));
			for (const auto& feature : std::filesystem::directory_iterator("features"))
				if (std::filesystem::is_directory(feature.path() / "Shaders"))
					roots.push_back(std::make_unique<PackageIncludes>(feature.path() / "Shaders"));
		}
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			for (const auto& root : roots)
				if (SUCCEEDED(root->Open(type, name, parent, data, size)))
					return S_OK;
			return E_FAIL;
		}
		HRESULT Close(LPCVOID data) override { return roots.front()->Close(data); }
	};

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	ComPtr<ID3DBlob> Compile(const char* source, const char* profile, const std::vector<D3D_SHADER_MACRO>& defines)
	{
		Includes includes;
		ComPtr<ID3DBlob> code, errors;
		const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		const HRESULT result = source ?
		                           D3DCompile(source, std::strlen(source), "detail-maps-test", defines.data(), &includes, "main", profile, flags, 0, &code, &errors) :
		                           D3DCompileFromFile(L"package/Shaders/Lighting.hlsl", defines.data(), &includes, "main", profile, flags, 0, &code, &errors);
		if (FAILED(result)) {
			for (const auto& define : defines)
				if (define.Name)
					std::cerr << define.Name << ' ';
			std::cerr << '\n';
		}
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	void CheckVariants()
	{
		unsigned count = 0;
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				for (bool deferred : { false, true })
					for (bool extended : { false, true })
						for (const char* material : { "", "SKINNED", "ENVMAP", "PARALLAX", "MULTI_LAYER_PARALLAX", "TREE_ANIM", "PROJECTED_UV", "SPARKLE" }) {
							if (pbr && (std::string_view(material) == "ENVMAP" || std::string_view(material) == "PARALLAX" ||
										   std::string_view(material) == "MULTI_LAYER_PARALLAX" || std::string_view(material) == "SPARKLE"))
								continue;
							std::vector<D3D_SHADER_MACRO> defines{
								{ "PSHADER", "1" }, { "DETAIL_MAPS", "1" }, { "ADAPTIVE_BALANCE", "1" }, { "DO_ALPHA_TEST", "1" },
								{ "VOLUMETRIC_SHADOWS", "1" }
							};
							if (vr)
								defines.push_back({ "VR", "1" });
							if (pbr)
								defines.push_back({ "TRUE_PBR", "1" });
							if (extended)
								for (const char* feature : { "EXTENDED_MATERIALS", "WETTERNESS", "SNOW_COVER", "TERRAIN_VARIATION" })
									defines.push_back({ feature, "1" });
							if (extended && !pbr && std::string_view(material) == "ENVMAP")
								defines.push_back({ "DYNAMIC_CUBEMAPS", "1" });
							if (deferred) {
								defines.push_back({ "DEFERRED", "1" });
								defines.push_back({ "CHARACTER_CATEGORY", "1" });
							}
							if (*material)
								defines.push_back({ material, "1" });
							if (std::string_view(material) == "SPARKLE")
								defines.push_back({ "PROJECTED_UV", "1" });
							defines.push_back({ nullptr, nullptr });
							const auto code = Compile(nullptr, "ps_5_0", defines);
							ComPtr<ID3D11ShaderReflection> reflection;
							Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
							D3D11_SHADER_INPUT_BIND_DESC binding{};
							Check(reflection->GetResourceBindingDescByName("DetailMaps::DetailMapsPerDraw", &binding));
							Require(binding.BindPoint == DetailMapsPolicy::ConstantSlot, "Detail buffer overlaps an existing binding");
							Check(reflection->GetResourceBindingDescByName("DetailMaps::TexDetail", &binding));
							Require(binding.BindPoint == DetailMapsPolicy::TextureSlot, "Detail texture binding changed");
							Check(reflection->GetResourceBindingDescByName("DetailMaps::DetailSampler", &binding));
							Require(binding.BindPoint == DetailMapsPolicy::SamplerSlot, "Detail sampler binding changed");
							D3D11_SHADER_BUFFER_DESC buffer{};
							Check(reflection->GetConstantBufferByName("DetailMaps::DetailMapsPerDraw")->GetDesc(&buffer));
							Require(buffer.Size == 32, "CPU/GPU detail constant layouts disagree");
							++count;
						}
		std::cout << count << " SE/AE and VR lighting variants compiled and reflected\n";
	}

	void CheckPixels()
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, nullptr, &context));
		const auto vsCode = Compile(R"(
float4 main(uint id : SV_VertexID) : SV_Position
{
	float2 uv = float2((id << 1) & 2, id & 2);
	return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
})",
			"vs_5_0", { { nullptr, nullptr } });
		const auto psCode = Compile(R"(
namespace SharedData { static const float MipBias = 0; }
#include "DetailMaps/DetailMaps.hlsli"
float4 main(float4 position : SV_Position) : SV_Target
{
	DetailMaps::Detail detail = DetailMaps::GetDetail(position.xy * 0.25, 600);
	float3 normal = DetailMaps::ApplyNormal(float3(0, 0, 1), detail);
	return float4(detail.Albedo, normal.xy, detail.Roughness);
})",
			"ps_5_0", { { "PSHADER", "1" }, { nullptr, nullptr } });
		ComPtr<ID3D11VertexShader> vs;
		ComPtr<ID3D11PixelShader> ps;
		Check(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs));
		Check(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps));
		Util::SetResourceName(vs.Get(), "DetailMapsTest::VS");
		Util::SetResourceName(ps.Get(), "DetailMapsTest::PS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(psCode->GetBufferPointer(), psCode->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		D3D11ShaderTest::ConstantBuffer constants(device.Get(), reflection.Get(), "DetailMaps::DetailMapsPerDraw");

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		ComPtr<ID3D11Texture2D> output, readback, texture;
		Check(device->CreateTexture2D(&desc, nullptr, &output));
		Util::SetResourceName(output.Get(), "DetailMapsTest::Output");
		ComPtr<ID3D11RenderTargetView> target;
		Check(device->CreateRenderTargetView(output.Get(), nullptr, &target));
		Util::SetResourceName(target.Get(), "DetailMapsTest::Output RTV");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateTexture2D(&desc, nullptr, &readback));
		Util::SetResourceName(readback.Get(), "DetailMapsTest::Readback");
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Check(device->CreateTexture2D(&desc, nullptr, &texture));
		Util::SetResourceName(texture.Get(), "DetailMapsTest::Texture");
		ComPtr<ID3D11ShaderResourceView> view;
		Check(device->CreateShaderResourceView(texture.Get(), nullptr, &view));
		Util::SetResourceName(view.Get(), "DetailMapsTest::Texture SRV");
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		ComPtr<ID3D11SamplerState> sampler;
		Check(device->CreateSamplerState(&samplerDesc, &sampler));
		Util::SetResourceName(sampler.Get(), "DetailMapsTest::Sampler");
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vs.Get(), nullptr, 0);
		context->PSSetShader(ps.Get(), nullptr, 0);
		const D3D11_VIEWPORT viewport{ 0, 0, 1, 1, 0, 1 };
		context->RSSetViewports(1, &viewport);
		context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
		context->PSSetShaderResources(DetailMapsPolicy::TextureSlot, 1, view.GetAddressOf());
		context->PSSetSamplers(DetailMapsPolicy::SamplerSlot, 1, sampler.GetAddressOf());
		constants.Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
		{
			DetailMaps::PassState saved(context.Get());
			ID3D11Buffer* emptyBuffer = nullptr;
			ID3D11ShaderResourceView* emptyView = nullptr;
			ID3D11SamplerState* emptySampler = nullptr;
			context->PSSetShader(nullptr, nullptr, 0);
			context->PSSetConstantBuffers(PerDrawSlot, 1, &emptyBuffer);
			context->PSSetShaderResources(TextureSlot, 1, &emptyView);
			context->PSSetSamplers(SamplerSlot, 1, &emptySampler);
		}
		ComPtr<ID3D11PixelShader> restoredShader;
		ComPtr<ID3D11Buffer> restoredBuffer;
		ComPtr<ID3D11ShaderResourceView> restoredView;
		ComPtr<ID3D11SamplerState> restoredSampler;
		context->PSGetShader(&restoredShader, nullptr, nullptr);
		context->PSGetConstantBuffers(PerDrawSlot, 1, &restoredBuffer);
		context->PSGetShaderResources(TextureSlot, 1, &restoredView);
		context->PSGetSamplers(SamplerSlot, 1, &restoredSampler);
		Require(restoredShader.Get() == ps.Get() && restoredBuffer.Get() == constants.buffer.Get() &&
					restoredView.Get() == view.Get() && restoredSampler.Get() == sampler.Get(),
			"Detail draw did not restore original D3D11 bindings");
		std::cout << "D3D11 binding restoration passed\n";
		for (unsigned condition = 0; condition < 8; ++condition) {
			const std::array texel = condition == 0 ? std::array{ .5f, .5f, .5f, .5f } : std::array{ condition == 5 ? .1f : .75f, .6f, .7f, .8f };
			context->UpdateSubresource(texture.Get(), 0, nullptr, texel.data(), sizeof(texel), 0);
			const std::array<float, 8> values{ 4, condition == 4 || condition == 7 ? 0.0f : condition == 5 ? 4.0f :
																											 1.0f,
				condition == 7 ? 0.0f : 1.0f, condition == 7 ? 0.0f : 1.0f,
				condition == 2 ? 500.0f : condition == 6 ? 1500.0f :
														   1200.0f,
				1.0f / 800.0f, condition == 3 ? 1.0f : 0.0f, 0 };
			std::memcpy(constants.bytes.data(), values.data(), sizeof(values));
			constants.Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
			context->Draw(3, 0);
			context->CopyResource(readback.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<float, 4> actual{};
			std::memcpy(actual.data(), mapped.pData, sizeof(actual));
			context->Unmap(readback.Get(), 0);
			const float fade = condition == 2 ? 0.0f : condition == 6 ? 1.0f :
			                                                            .75f;
			const float x = (texel[3] * 2 - 1) * fade * values[2], y = (texel[1] * 2 - 1) * fade * values[2];
			const float norm = std::sqrt(x * x + y * y + 1);
			const std::array expected{ std::max(0.0f, 1 + (texel[0] * 2 - 1) * values[1] * fade),
				(condition == 3 ? y : x) / norm, (condition == 3 ? x : y) / norm, (texel[2] - .5f) * 2 * fade * values[3] };
			for (size_t i = 0; i < actual.size(); ++i) {
				if (!std::isfinite(actual[i]) || std::abs(actual[i] - expected[i]) >= .0001f)
					std::cerr << "Condition " << condition << ", component " << i << ": expected " << expected[i] << ", received " << actual[i] << '\n';
				Require(std::isfinite(actual[i]) && std::abs(actual[i] - expected[i]) < .0001f, "Detail pixel/fade/transpose mismatch");
			}
		}
		std::cout << "8 WARP detail pixel cases passed\n";
	}
}

int main()
{
	try {
		CheckPixels();
		CheckVariants();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
