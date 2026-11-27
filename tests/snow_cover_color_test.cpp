#define NOMINMAX
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Color = std::array<float, 4>;
	using Results = std::array<Color, 3>;
	constexpr Color MainTint{ 0.8f, 0.95f, 0.7f, 1.0f };
	constexpr Color AlternateTint{ 0.9f, 0.75f, 1.0f, 1.0f };
	constexpr float MainAO = 0.7f;
	constexpr float AlternateAO = 0.4f;

	ComPtr<ID3DBlob> CompilePixel(std::vector<D3D_SHADER_MACRO> defines, const char* entry, const wchar_t* file = L"tests/snow_cover_color.hlsl", const char* extraRoot = "features/Grass Lighting/Shaders", UINT flags = 0)
	{
		PackageIncludes includes("package/Shaders", "features/Snow Cover/Shaders", extraRoot);
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> bytecode, errors;
		const auto compiled = D3DCompileFromFile(file, defines.data(), &includes,
			entry, "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | flags,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(compiled);
		return bytecode;
	}

	struct Target
	{
		ComPtr<ID3D11Texture2D> texture, staging;
		ComPtr<ID3D11RenderTargetView> view;

		explicit Target(ID3D11Device* device)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 3;
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			Check(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()));
			Util::SetResourceName(texture.Get(), "SnowColorTest::Target");
			Check(device->CreateRenderTargetView(texture.Get(), nullptr, view.GetAddressOf()));
			Util::SetResourceName(view.Get(), "SnowColorTest::Target RTV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "SnowColorTest::Readback");
		}

		void Bind(ID3D11DeviceContext* context, ID3D11VertexShader* vertex, ID3D11PixelShader* pixel)
		{
			context->ClearState();
			context->OMSetRenderTargets(1, view.GetAddressOf(), nullptr);
			const D3D11_VIEWPORT viewport{ 0, 0, 3, 1, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex, nullptr, 0);
			context->PSSetShader(pixel, nullptr, 0);
		}

		Results Read(ID3D11DeviceContext* context)
		{
			context->CopyResource(staging.Get(), texture.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			Results result;
			std::memcpy(result.data(), mapped.pData, sizeof(result));
			context->Unmap(staging.Get(), 0);
			return result;
		}
	};

	ComPtr<ID3D11ShaderResourceView> Texture(ID3D11Device* device, const Color& color)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data{ color.data(), sizeof(color), 0 };
		ComPtr<ID3D11Texture2D> texture;
		Check(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()));
		Util::SetResourceName(texture.Get(), "SnowColorTest::Material");
		ComPtr<ID3D11ShaderResourceView> view;
		Check(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));
		Util::SetResourceName(view.Get(), "SnowColorTest::Material SRV");
		return view;
	}

	size_t CheckColors(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, bool pbr)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (pbr)
			defines.push_back({ "TRUE_PBR", "1" });
		auto bytecode = CompilePixel(std::move(defines), "main");
		ComPtr<ID3D11PixelShader> shader;
		Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SnowColorTest::PS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer features(device, reflection.Get(), "SharedData::FeatureData");
		ConstantBuffer samples(device, reflection.Get(), "Samples");
		ConstantBuffer permutation(device, reflection.Get(), "Permutation::PerShader");
		permutation.SetVariable("Permutation::ExtraShaderDescriptor", 1u);
		D3D11_SHADER_TYPE_DESC mask{};
		Check(features.reflection->GetVariableByName("SharedData::snowCoverSettings")->GetType()->GetMemberTypeByName("LandscapeSnowColorMask")->GetDesc(&mask));
		if (mask.Offset != 200)
			throw std::runtime_error("Snow color mask does not occupy the existing world settings padding");
		features.SetMember("SharedData::snowCoverSettings", "MainTint", MainTint);
		features.SetMember("SharedData::snowCoverSettings", "AltTint", AlternateTint);
		features.SetMember("SharedData::snowCoverSettings", "BlendSmoothness", 1.0f);
		features.SetMember("SharedData::snowCoverSettings", "maxAngle", 1.0f);
		samples.SetVariable("MainAO", MainAO);
		samples.SetVariable("AlternateAO", AlternateAO);
		const auto normal = Texture(device, { 0.5f, 0.5f, 1.0f, 1.0f });
		const auto mainMaterial = Texture(device, { 0.8f, 0.0f, MainAO, 0.02f });
		const auto alternateMaterial = Texture(device, { 0.8f, 0.0f, AlternateAO, 0.02f });
		ComPtr<ID3D11SamplerState> sampler;
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		Check(device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf()));
		Util::SetResourceName(sampler.Get(), "SnowColorTest::Sampler");
		Target actual(device), expected(device);
		size_t count = 0;
		for (bool linearLighting : { false, true }) {
			features.SetMember("SharedData::linearLightingSettings", "enableLinearLighting", static_cast<uint32_t>(linearLighting));
			for (const auto& controls : { std::array{ 2.2f, 1.0f }, std::array{ 1.8f, 0.8f }, std::array{ 2.4f, 1.2f } }) {
				features.SetMember("SharedData::linearLightingSettings", "colorGamma", controls[0]);
				features.SetMember("SharedData::linearLightingSettings", "vanillaDiffuseColorMult", controls[1]);
				for (uint32_t sourceMask : { 0u, 1u, 2u, 3u, 5u, 7u, 10u, 11u, 15u }) {
					features.SetMember("SharedData::snowCoverSettings", "LandscapeSnowColorMask", sourceMask);
					for (const auto& color : { Color{ 0.0f, 0.0005f, 0.02f, 1 }, Color{ 0.18f, 0.42f, 0.72f, 1 }, Color{ 0.8f, 0.95f, 1.0f, 1 } }) {
						const Color alternateColor{ color[2], color[0], color[1], 1 };
						samples.SetVariable("MainSample", std::array{ color[0], color[1], color[2] });
						samples.SetVariable("AlternateSample", std::array{ alternateColor[0], alternateColor[1], alternateColor[2] });
						const auto mainTexture = Texture(device, color);
						const auto alternateTexture = Texture(device, alternateColor);
						for (bool alternate : { false, true }) {
							context->ClearState();
							samples.SetVariable("AlternateSurface", static_cast<uint32_t>(alternate));
							features.SetMember("SharedData::snowCoverSettings", "peakMainAngle", alternate ? -10.0f : 1.0f);
							features.SetMember("SharedData::snowCoverSettings", "peakAltAngle", alternate ? 1.0f : -10.0f);
							features.Bind(context, D3D11ShaderTest::Stage::Pixel);
							samples.Bind(context, D3D11ShaderTest::Stage::Pixel);
							permutation.Bind(context, D3D11ShaderTest::Stage::Pixel);
							ID3D11ShaderResourceView* textures[]{ mainTexture.Get(), normal.Get(), mainMaterial.Get(), alternateTexture.Get(), normal.Get(), alternateMaterial.Get() };
							context->PSSetShaderResources(112, 6, textures);
							ID3D11SamplerState* samplers[]{ sampler.Get(), sampler.Get() };
							context->PSSetSamplers(0, 2, samplers);
							ID3D11RenderTargetView* targets[]{ actual.view.Get(), expected.view.Get() };
							context->OMSetRenderTargets(2, targets, nullptr);
							const D3D11_VIEWPORT viewport{ 0, 0, 3, 1, 0, 1 };
							context->RSSetViewports(1, &viewport);
							context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
							context->VSSetShader(vertex, nullptr, 0);
							context->PSSetShader(shader.Get(), nullptr, 0);
							context->Draw(3, 0);
							context->ClearState();
							const auto values = actual.Read(context);
							const auto native = expected.Read(context);
							for (size_t route = 0; route < values.size(); ++route) {
								if (values[route][3] != (linearLighting ? 2.0f : 1.0f) || native[route][3] != 1.0f)
									throw std::runtime_error("Snow color route did not render with the requested Linear Lighting state");
								for (size_t channel = 0; channel < 3; ++channel) {
									if (!std::isfinite(values[route][channel]) || !std::isfinite(native[route][channel]) || std::abs(values[route][channel] - native[route][channel]) > 2e-5f)
										throw std::runtime_error("Snow color differs from native source shading: VR=" + std::to_string(vr) + " PBR=" + std::to_string(pbr) + " LL=" + std::to_string(linearLighting) + " mask=" + std::to_string(sourceMask) + " alternate=" + std::to_string(alternate) + " route=" + std::to_string(route));
								}
								++count;
							}
						}
					}
				}
			}
		}
		return count;
	}

	size_t CheckProjectedColors(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, bool pbr)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "PROJECTED_UV", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (pbr)
			defines.push_back({ "TRUE_PBR", "1" });
		for (bool snowCover : { false, true })
			for (bool deferred : { false, true }) {
				auto productionDefines = defines;
				if (snowCover)
					productionDefines.push_back({ "SNOW_COVER", "1" });
				if (deferred)
					productionDefines.push_back({ "DEFERRED", "1" });
				CompilePixel(std::move(productionDefines), "main", L"package/Shaders/Lighting.hlsl");
			}
		defines.push_back({ "SNOW_COVER", "1" });
		auto bytecode = CompilePixel(std::move(defines), "ProjectedColor");
		ComPtr<ID3D11PixelShader> shader;
		Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SnowColorTest::Projected PS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer features(device, reflection.Get(), "SharedData::FeatureData");
		ConstantBuffer samples(device, reflection.Get(), "Samples");
		ConstantBuffer permutation(device, reflection.Get(), "Permutation::PerShader");
		features.SetMember("SharedData::snowCoverSettings", "LandscapeSnowColorMask", pbr ? 5u : 1u);
		// Both views share the same bytes; hardware sRGB sampling is the independent PBR reference.
		constexpr std::array<std::array<uint8_t, 4>, 4> texels{ { { 0, 1, 10, 255 }, { 11, 32, 64, 255 }, { 128, 192, 255, 255 }, { 204, 242, 250, 255 } } };
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = static_cast<UINT>(texels.size());
		desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data{ texels.data(), sizeof(texels), 0 };
		ComPtr<ID3D11Texture2D> texture;
		Check(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()));
		Util::SetResourceName(texture.Get(), "SnowColorTest::Projected Material");
		std::array<ComPtr<ID3D11ShaderResourceView>, 2> views;
		D3D11_SHADER_RESOURCE_VIEW_DESC view{};
		view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		view.Texture2D.MipLevels = 1;
		for (size_t i = 0; i < views.size(); ++i) {
			view.Format = i == 1 && pbr ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
			Check(device->CreateShaderResourceView(texture.Get(), &view, views[i].GetAddressOf()));
			Util::SetResourceName(views[i].Get(), "SnowColorTest::Projected Material %zu SRV", i);
		}
		Target output(device);
		size_t count = 0;
		for (uint32_t mode = 0; mode < 4; ++mode) {
			const bool linearLighting = (mode & 1) != 0;
			features.SetMember("SharedData::linearLightingSettings", "enableLinearLighting", static_cast<uint32_t>(linearLighting));
			features.SetMember("SharedData::linearLightingSettings", "enableAdaptiveBrightnessColorAdjustments", (mode >> 1));
			for (const auto& controls : { std::array{ 1.8f, 0.8f }, std::array{ 2.2f, 1.0f }, std::array{ 2.4f, 1.2f } }) {
				features.SetMember("SharedData::linearLightingSettings", "colorGamma", controls[0]);
				features.SetMember("SharedData::linearLightingSettings", "vanillaDiffuseColorMult", controls[1]);
				for (uint32_t scene : { 0u, 1u, 2u }) {
					permutation.SetVariable("Permutation::ExtraShaderDescriptor", scene);
					const bool active = linearLighting && scene != 0;
					for (uint32_t texel = 0; texel < texels.size(); ++texel) {
						samples.SetVariable("ProjectedTexel", texel);
						output.Bind(context, vertex, shader.Get());
						context->PSSetShaderResources(3, 1, views[0].GetAddressOf());
						context->PSSetShaderResources(112, 1, views[1].GetAddressOf());
						features.Bind(context, D3D11ShaderTest::Stage::Pixel);
						samples.Bind(context, D3D11ShaderTest::Stage::Pixel);
						permutation.Bind(context, D3D11ShaderTest::Stage::Pixel);
						context->Draw(3, 0);
						context->ClearState();
						const auto colors = output.Read(context);
						const auto& native = colors[2];
						for (size_t route = 0; route < colors.size(); ++route) {
							for (size_t channel = 0; channel < 3; ++channel)
								if (!std::isfinite(colors[route][channel]) || !std::isfinite(native[channel]) || std::abs(colors[route][channel] - native[channel]) > 2e-5f || colors[route][3] != (active ? 2.0f : 1.0f))
									throw std::runtime_error("Projected/native snow color mismatch: VR=" + std::to_string(vr) + " PBR=" + std::to_string(pbr) + " mode=" + std::to_string(mode) + " gamma=" + std::to_string(controls[0]) + " scene=" + std::to_string(scene) + " texel=" + std::to_string(texel) + " route=" + std::to_string(route));
							++count;
						}
					}
				}
			}
		}
		return count;
	}

	size_t CheckObjectFade(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, bool pbr, const char* technique, bool fades)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (pbr)
			defines.push_back({ "TRUE_PBR", "1" });
		if (*technique)
			defines.push_back({ technique, "1" });
		if (std::strcmp(technique, "LODLANDSCAPE") == 0 || std::strcmp(technique, "LODLANDNOISE") == 0)
			defines.push_back({ "MODELSPACENORMALS", "1" });
		auto bytecode = CompilePixel(std::move(defines), "ObjectFadeCoverage");
		ComPtr<ID3D11PixelShader> shader;
		Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SnowColorTest::ObjectFadePS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer features(device, reflection.Get(), "SharedData::FeatureData");
		features.SetMember("SharedData::snowCoverSettings", "BlendSmoothness", 1.0f);
		features.SetMember("SharedData::snowCoverSettings", "maxAngle", 1.0f);
		features.SetMember("SharedData::snowCoverSettings", "ObjectFadeStart", 100.0f);
		features.SetMember("SharedData::snowCoverSettings", "ObjectFadeEnd", 300.0f);
		Target target(device);
		target.Bind(context, vertex, shader.Get());
		size_t count = 0;
		for (float amount : { 0.0f, 0.5f, 1.0f }) {
			features.SetMember("SharedData::snowCoverSettings", "ObjectFadeAmount", amount);
			features.Bind(context, D3D11ShaderTest::Stage::Pixel);
			context->Draw(3, 0);
			const auto values = target.Read(context);
			// Pixels lie before, halfway through and beyond the configured fade interval.
			const std::array expected{ 1.0f, fades ? 1.0f - amount * 0.5f : 1.0f, fades ? 1.0f - amount : 1.0f };
			for (size_t i = 0; i < values.size(); ++i) {
				for (size_t channel = 0; channel < 3; ++channel) {
					if (!std::isfinite(values[i][channel]) || std::abs(values[i][channel] - expected[i]) > 1e-6f || values[i][3] != 1)
						throw std::runtime_error("Snow surface/melt coverage has incorrect distance fade: VR=" + std::to_string(vr) + " PBR=" + std::to_string(pbr) + " technique=" + technique + " amount=" + std::to_string(amount));
				}
				++count;
			}
		}
		context->ClearState();
		return count;
	}

	void CheckMaterialSelection(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, bool pbr, UINT flags)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (pbr)
			defines.push_back({ "TRUE_PBR", "1" });
		auto code = CompilePixel(std::move(defines), "MaterialSelection", L"tests/snow_cover_color.hlsl", "features/Grass Lighting/Shaders", flags);
		ComPtr<ID3D11PixelShader> shader;
		Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
		Util::SetResourceName(shader.Get(), "SnowColorTest::Selection PS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		ConstantBuffer features(device, reflection.Get(), "SharedData::FeatureData");
		ConstantBuffer samples(device, reflection.Get(), "Samples");
		features.SetMember("SharedData::snowCoverSettings", "BlendSmoothness", 1.0f);
		features.SetMember("SharedData::snowCoverSettings", "SnowingDensity", 1000.0f);
		Target actual(device), expected(device);
		std::mt19937 random(73119);
		std::uniform_real_distribution<float> unit(0, 1);
		for (unsigned i = 0; i < 640; ++i) {
			const bool boundary = i >= 512;
			const float mainPeak = boundary || i % 4 == 0 ? .3f : unit(random);
			const float altPeak = boundary || i % 4 == 0 ? .6f : unit(random);
			float normal = i % 4 == 0 ? .5f * (mainPeak + altPeak + (i & 4u ? .05f : -.05f)) : 2 * unit(random) - 1;
			float accumulation = i % 4 == 0 ? 0.0f : float(int(i % 5) - 2);
			float minAngle = unit(random);
			std::array position{ 200000 * unit(random) - 100000, 200000 * unit(random) - 100000, 20000 * unit(random) - 10000 };
			if (boundary) {
				// Probe both padded branch edges with extremal noise and negative weather.
				const float bound = .05f + 4 * std::numeric_limits<float>::epsilon();
				accumulation = i & 16u ? -2.0f : 0.0f;
				minAngle = .2f;
				normal = .5f * (mainPeak + altPeak + (accumulation < 0 ? -minAngle : 0) + (i & 8u ? bound : -bound));
				const int offset = int(i % 8) - 4;
				for (int step = 0; step < std::abs(offset); ++step)
					normal = std::nextafter(normal, offset < 0 ? -1.0f : 1.0f);
				position = { 0, 0, ((i & 32u ? 1.5707963268f : -1.5707963268f) - .025f) / .01f };
			}
			samples.SetVariable("SelectionNormals", std::array{ std::nextafter(normal, -std::numeric_limits<float>::infinity()), normal, std::nextafter(normal, std::numeric_limits<float>::infinity()) });
			samples.SetVariable("SelectionPosition", position);
			features.SetMember("SharedData::snowCoverSettings", "peakMainAngle", mainPeak);
			features.SetMember("SharedData::snowCoverSettings", "peakAltAngle", altPeak);
			features.SetMember("SharedData::snowCoverSettings", "minAngle", minAngle);
			features.SetMember("SharedData::snowCoverSettings", "TimeSnowing", accumulation);
			actual.Bind(context, vertex, shader.Get());
			std::array targets{ actual.view.Get(), expected.view.Get() };
			context->OMSetRenderTargets(UINT(targets.size()), targets.data(), nullptr);
			features.Bind(context, D3D11ShaderTest::Stage::Pixel);
			samples.Bind(context, D3D11ShaderTest::Stage::Pixel);
			context->Draw(3, 0);
			context->ClearState();
			if (actual.Read(context) != expected.Read(context))
				throw std::runtime_error("Snow material bounds changed the original strict selection");
		}
	}

	void CheckWeatherRange(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, bool reference)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (reference)
			defines.push_back({ "CSX_PERF_REFERENCE_FLAGS", "CSX_PERF_SNOW_WEATHER_RANGE" });
		auto bytecode = CompilePixel(std::move(defines), "WeatherRange");
		ComPtr<ID3D11PixelShader> shader;
		Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SnowColorTest::WeatherRangePS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer features(device, reflection.Get(), "SharedData::FeatureData");
		features.SetMember("SharedData::snowCoverSettings", "WeatherFadeEnd", 1000.0f);
		Target target(device);
		target.Bind(context, vertex, shader.Get());
		Results weather{};
		for (float fadeStart : { 200.0f, std::nextafter(1000.0f, 0.0f), 1000.0f, std::nextafter(1000.0f, 2000.0f), 60000.0f }) {
			features.SetMember("SharedData::snowCoverSettings", "WeatherFadeStart", fadeStart);
			for (float accumulation : { 1.0f, 0.0f, -1.0f }) {
				features.SetMember("SharedData::snowCoverSettings", "TimeSnowing", accumulation);
				features.Bind(context, D3D11ShaderTest::Stage::Pixel);
				context->Draw(3, 0);
				const auto values = target.Read(context);
				if (accumulation > 0)
					weather = values;
				for (size_t i = 0; i < values.size(); ++i) {
					if ((fadeStart == 200 && !(weather[i][0] > 0 && weather[i][0] < 1)) || values[i][3] != 1)
						throw std::runtime_error("Snow weather-distance test missed the rendered fade");
					const float expected = accumulation == 0 && !reference ? 0.0f : weather[i][0];
					if (!std::isfinite(values[i][0]) || values[i][0] != values[i][1] || std::abs(values[i][0] - expected) > 1e-6f)
						throw std::runtime_error("Snow weather-distance optimization changed zero or negative accumulation behavior");
				}
			}
		}
		context->ClearState();
	}
}

namespace
{
#include "snow_cover_material_gpu.h"
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		constexpr char vertexSource[] = "float4 main(uint id : SV_VertexID) : SV_Position { return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1); }";
		ComPtr<ID3DBlob> bytecode;
		Check(D3DCompile(vertexSource, sizeof(vertexSource), nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, bytecode.GetAddressOf(), nullptr));
		ComPtr<ID3D11VertexShader> vertex;
		Check(device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
		Util::SetResourceName(vertex.Get(), "SnowColorTest::VS");
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				for (UINT flags : { 0u, UINT(D3DCOMPILE_PARTIAL_PRECISION), UINT(D3DCOMPILE_AVOID_FLOW_CONTROL), UINT(D3DCOMPILE_PARTIAL_PRECISION | D3DCOMPILE_AVOID_FLOW_CONTROL) })
					CheckMaterialSelection(device.Get(), context.Get(), vertex.Get(), vr, pbr, flags);
		std::cout << "Snow material selection: 30,720 random and padded-boundary cases passed on WARP\n";
		size_t projectedCount = 0;
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				projectedCount += CheckProjectedColors(device.Get(), context.Get(), vertex.Get(), vr, pbr);
		std::cout << "Projected snow: " << projectedCount << " color comparisons passed on WARP\n";
		size_t count = 0;
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				count += CheckColors(device.Get(), context.Get(), vertex.Get(), vr, pbr);
		for (bool vr : { false, true })
			for (bool reference : { false, true })
				CheckWeatherRange(device.Get(), context.Get(), vertex.Get(), vr, reference);
		constexpr std::array<std::pair<const char*, bool>, 7> fadePermutations{ { { "", true }, { "LODOBJECTS", true }, { "LODOBJECTSHD", true },
			{ "LANDSCAPE", false }, { "LODLANDSCAPE", false }, { "LODLANDNOISE", false }, { "WORLD_MAP", false } } };
		size_t fadeCount = 0;
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				for (const auto& [technique, fades] : fadePermutations)
					fadeCount += CheckObjectFade(device.Get(), context.Get(), vertex.Get(), vr, pbr, technique, fades);
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				for (bool lodBlending : { false, true })
					for (const auto* technique : { "", "LODOBJECTS", "LODOBJECTSHD" }) {
						std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" }, { "DO_ALPHA_TEST", "1" }, { "DEFERRED", "1" } };
						if (vr)
							defines.push_back({ "VR", "1" });
						if (pbr)
							defines.push_back({ "TRUE_PBR", "1" });
						if (lodBlending)
							defines.push_back({ "LOD_BLENDING", "1" });
						if (*technique)
							defines.push_back({ technique, "1" });
						CompilePixel(std::move(defines), "ProductionLightingMain");
					}
		std::cout << "Snow color: " << count << " production surface/foliage samples match native shading on WARP (SE/AE and VR, legacy/PBR sources and targets, LL controls, main/alternate fallback, retained tints/AO)\n";
		std::cout << "Snow weather distance: positive, zero and negative accumulation passed on WARP (SE/AE and VR, optimized/reference)\n";
		std::cout << "Snow object fade: " << fadeCount << " distance/amount/permutation cases passed on WARP (SE/AE and VR, legacy/PBR, objects/LOD, terrain/map exemptions)\n";
		for (bool vr : { false, true })
			for (bool animated : { false, true })
				for (bool snowCover : { false, true }) {
					std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "TRUE_PBR", "1" }, { "DO_ALPHA_TEST", "1" }, { "DEFERRED", "1" } };
					if (vr)
						defines.push_back({ "VR", "1" });
					if (animated)
						defines.push_back({ "TREE_ANIM", "1" });
					if (snowCover)
						defines.push_back({ "SNOW_COVER", "1" });
					CompilePixel(std::move(defines), "main", L"package/Shaders/Lighting.hlsl");
				}
		size_t materialCount = 0;
		for (bool vr : { false, true })
			for (bool animated : { false, true })
				materialCount += CheckFoliageMaterial(device.Get(), context.Get(), vertex.Get(), vr, animated);
		for (bool vr : { false, true })
			materialCount += CheckFoliageMaterial(device.Get(), context.Get(), vertex.Get(), vr, false, true);
		for (bool vr : { false, true })
			materialCount += CheckFoliageMaterial(device.Get(), context.Get(), vertex.Get(), vr, true, false, true);
		std::cout << "Foliage snow: " << materialCount << " material and final-lighting cases passed on WARP\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
