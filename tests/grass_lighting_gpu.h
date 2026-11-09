struct GrassLightingFixture : GrassAlphaFixture
{
	ComPtr<ID3D11ShaderResourceView> shadowMask, normalTexture, terrainTexture, cloudTexture;
	ComPtr<ID3D11RasterizerState> rasterizer;

	GrassLightingFixture()
	{
		const auto texture = [&](const Pixel* values, UINT width, bool cube, const char* name) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = desc.MipLevels = desc.SampleDesc.Count = 1;
			desc.ArraySize = cube ? 6 : 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
			std::array<D3D11_SUBRESOURCE_DATA, 6> initial{};
			for (auto& face : initial)
				face = { values, width * UINT(sizeof(Pixel)), 0 };
			ComPtr<ID3D11Texture2D> resource;
			ComPtr<ID3D11ShaderResourceView> view;
			Check(device->CreateTexture2D(&desc, initial.data(), resource.GetAddressOf()));
			Util::SetResourceName(resource.Get(), "GrassLightingTest::%s", name);
			Check(device->CreateShaderResourceView(resource.Get(), nullptr, view.GetAddressOf()));
			Util::SetResourceName(view.Get(), "GrassLightingTest::%s SRV", name);
			return view;
		};
		const std::array<Pixel, 4> mask{ Pixel{ 1, 1, 1, 1 }, Pixel{ 1, 1, 1, 1 }, Pixel{ 1, 1, 1, 1 }, Pixel{ 1, 1, 1, 1 } };
		const Pixel normal{ .5f, .5f, 1, 1 }, terrain{ .5f, .5f, 0, 0 }, cloud{ 1, 0, 0, 0 };
		shadowMask = texture(mask.data(), 4, false, "ShadowMask");
		normalTexture = texture(&normal, 1, false, "Normal");
		terrainTexture = texture(&terrain, 1, false, "TerrainHeight");
		cloudTexture = texture(&cloud, 1, true, "CloudCube");
		D3D11_RASTERIZER_DESC desc{};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.DepthClipEnable = true;
		Check(device->CreateRasterizerState(&desc, rasterizer.GetAddressOf()));
		Util::SetResourceName(rasterizer.Get(), "GrassLightingTest::Rasterizer");
	}

	unsigned CheckLighting(bool vr)
	{
		constexpr const char* source = R"(
#include "RunGrass.hlsl"
PS_INPUT LightingVertex(uint vertex : SV_VertexID) {
    const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1) };
    uint texel = vertex / 6;
    float2 corner = corners[texel == 3 ? 5 - vertex % 6 : vertex % 6];
    PS_INPUT input = (PS_INPUT)0;
    input.HPosition = float4(-1 + (texel + corner.x) * 0.5, 1 - corner.y * 2, 0.5, 1);
    input.WorldPosition = float4(input.HPosition.xy, 10, 1);
    input.PreviousWorldPosition = input.WorldPosition;
    input.VertexNormal = float4(0.70710678, 0, 0.70710678, 0);
    input.Color = 1;
    input.TexCoord.xy = input.HPosition.xy * 0.5 + 0.5;
    input.VertexMult = texel == 0 ? 0 : (texel == 1 ? -1 : -2);
#ifdef VR
    input.ClipDistance = input.CullDistance = 1;
#endif
    return input;
}
)";
		auto defines = GrassDefines(false, vr, true, true, false, false, false, true);
		defines.back() = { "TERRAIN_SHADOWS", "1" };
		defines.push_back({ "CLOUD_SHADOWS", "1" });
		defines.push_back({ nullptr, nullptr });
		auto pixelCode = CompileProgram(defines.data(), false);
		auto vertexCode = CompileProgram(defines.data(), true, source, "LightingVertex");
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, pixel.GetAddressOf()));
		Util::SetResourceName(pixel.Get(), "GrassLightingTest::PS");
		Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
		Util::SetResourceName(vertex.Get(), "GrassLightingTest::VS");
		Check(D3DReflect(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		context->ClearState();
		std::vector<std::unique_ptr<D3D11ShaderTest::ConstantBuffer>> constants;
		D3D11ShaderTest::ConstantBuffer *features = nullptr, *shared = nullptr, *permutation = nullptr;
		D3D11_SHADER_DESC shader{};
		Check(reflection->GetDesc(&shader));
		for (UINT i = 0; i < shader.ConstantBuffers; ++i) {
			D3D11_SHADER_BUFFER_DESC buffer{};
			Check(reflection->GetConstantBufferByIndex(i)->GetDesc(&buffer));
			auto& value = constants.emplace_back(std::make_unique<D3D11ShaderTest::ConstantBuffer>(device.Get(), reflection.Get(), buffer.Name));
			if (std::strcmp(buffer.Name, "SharedData::FeatureData") == 0)
				features = value.get();
			if (std::strcmp(buffer.Name, "SharedData::SharedData") == 0)
				shared = value.get();
			if (std::strcmp(buffer.Name, "Permutation::PerShader") == 0)
				permutation = value.get();
			if (std::strcmp(buffer.Name, "PerMaterial") == 0)
				value->SetVariable("PBRParams1", std::array<float, 3>{ .5f, .04f, 0 });
			if (std::strcmp(buffer.Name, "VRValues") == 0)
				value->SetVariable("StereoEnabled", 1.0f);
			if (std::strcmp(buffer.Name, "PerFrame") == 0)
				value->SetVariable("VPOSOffset", Pixel{ .25f, 1, 0, 0 });
			if (std::strcmp(buffer.Name, "FrameBuffer::PerFrame") == 0)
				value->SetVariable("FrameBuffer::DynamicResolutionParams2", Pixel{ 1, 1, 1, 1 });
			value->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
		}
		Require(features && shared && permutation, "Grass lighting must retain its light, material and feature controls");
		features->SetMember("SharedData::adaptiveBalanceSettings", "appearance", AdaptiveBalanceTest::NeutralAppearance());
		features->SetMember("SharedData::adaptiveBalanceSettings", "directionalLightMult", 1.0f);
		features->SetMember("SharedData::adaptiveBalanceSettings", "ambientMult", 1.0f);
		for (const auto* name : { "colorGamma", "lightGamma", "ambientGamma", "dirLightMult", "ambientMult", "vanillaDiffuseColorMult" })
			features->SetMember("SharedData::linearLightingSettings", name, 1.0f);
		features->SetMember("SharedData::linearLightingSettings", "isDirLightLinear", 1u);
		features->SetMember("SharedData::grassLightingSettings", "BasicGrassBrightness", 1.0f);
		features->SetMember("SharedData::grassLightingSettings", "ComplexGrassThreshold", .01f);
		features->SetMember("SharedData::truePBRSettings", "Enabled", 1u);
		features->SetMember("SharedData::terraOccSettings", "Offset", std::array<float, 2>{ .5f, .5f });
		features->SetMember("SharedData::terraOccSettings", "ZBlur", 2.0f);
		shared->SetVariable("SharedData::HasDirectionalShadows", 1u);
		for (const auto* name : { "SharedData::AmbientSHR", "SharedData::AmbientSHG", "SharedData::AmbientSHB" })
			shared->SetVariable(name, Pixel{ .1f, 0, 0, 0 });
		context->VSSetShader(vertex.Get(), nullptr, 0);
		context->PSSetShader(pixel.Get(), nullptr, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
		context->RSSetState(rasterizer.Get());
		const D3D11_VIEWPORT viewport{ 0, 0, 4, 1, 0, 1 };
		context->RSSetViewports(1, &viewport);
		context->PSSetSamplers(0, 1, sampler.GetAddressOf());
		context->PSSetSamplers(2, 1, sampler.GetAddressOf());
		context->PSSetShaderResources(0, 1, textures[0].GetAddressOf());
		context->PSSetShaderResources(1, 1, shadowMask.GetAddressOf());
		context->PSSetShaderResources(2, 1, normalTexture.GetAddressOf());
		context->PSSetShaderResources(25, 1, cloudTexture.GetAddressOf());
		context->PSSetShaderResources(60, 1, terrainTexture.GetAddressOf());
		return CheckScenarios(vr, *features, *shared, *permutation);
	}

	unsigned CheckScenarios(bool vr, D3D11ShaderTest::ConstantBuffer& features,
		D3D11ShaderTest::ConstantBuffer& shared, D3D11ShaderTest::ConstantBuffer& permutation)
	{
		struct Sun
		{
			Pixel direction, color;
		};
		constexpr std::array suns{
			Sun{ { .99498744f, 0, .1f, 0 }, { .8f, .4f, .2f, 0 } },
			Sun{ { 0, 0, 1, 0 }, { .8f, .8f, .8f, 0 } },
			Sun{ { 0, 0, 1, 0 }, { 0, 0, 0, 0 } }
		};
		struct Shadow
		{
			bool terrain, cloud;
			float terrainVisibility, cloudVisibility;
		};
		constexpr std::array shadows{
			Shadow{ false, false, 0, 0 },
			Shadow{ true, false, 0, 0 }, Shadow{ true, false, .5f, 0 }, Shadow{ true, false, 1, 0 },
			Shadow{ false, true, 0, 0 }, Shadow{ false, true, 0, .5f }, Shadow{ false, true, 0, 1 },
			Shadow{ true, true, .5f, .5f }, Shadow{ true, true, 1, 1 }
		};
		const auto setShadow = [&](const Shadow& shadow) {
			features.SetMember("SharedData::terraOccSettings", "EnableTerrainShadow", UINT(shadow.terrain));
			features.SetMember("SharedData::cloudShadowsSettings", "Enabled", UINT(shadow.cloud));
			features.SetMember("SharedData::cloudShadowsSettings", "Opacity", 1 - shadow.cloudVisibility);
			// With world z=10, bias=1024 and blur=2, this height produces the requested visibility.
			const float height = 1036 - 4 * shadow.terrainVisibility;
			features.SetMember("SharedData::terraOccSettings", "ZRange", std::array<float, 2>{ height, height });
			features.Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
		};
		unsigned cases = 0;
		for (bool enhanced : { false, true })
			for (bool pbr : { false, true })
				for (bool linear : { false, true }) {
					features.SetMember("SharedData::grassLightingSettings", "Enabled", UINT(enhanced));
					features.SetMember("SharedData::linearLightingSettings", "enableLinearLighting", UINT(linear));
					permutation.SetVariable("Permutation::ExtraShaderDescriptor", 1u | (1u << 5) | (pbr ? (1u << 9) | (1u << 10) : 0u));
					permutation.Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
					shared.SetVariable("SharedData::DirLightDirection", suns[2].direction);
					shared.SetVariable("SharedData::DirLightColor", suns[2].color);
					shared.Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
					setShadow(shadows[0]);
					const auto ambient = DrawPixels();
					for (const auto& texel : ambient)
						for (UINT channel = 0; channel < 3; ++channel)
							Require(std::isfinite(texel[channel]) && texel[channel] > 1e-4f, "Grass ambient control must remain visible without direct light");
					for (UINT sun = 0; sun < suns.size(); ++sun) {
						shared.SetVariable("SharedData::DirLightDirection", suns[sun].direction);
						shared.SetVariable("SharedData::DirLightColor", suns[sun].color);
						shared.Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
						setShadow(shadows[0]);
						const auto unshadowed = DrawPixels();
						for (UINT texel = 0; texel < unshadowed.size(); ++texel)
							for (UINT channel = 0; channel < 3; ++channel)
								Require(std::isfinite(unshadowed[texel][channel]) &&
											(sun == 2 ? std::abs(unshadowed[texel][channel] - ambient[texel][channel]) < 1e-6f : unshadowed[texel][channel] > ambient[texel][channel] + 1e-4f),
									"Grass direct-light control must contribute only when the sun is lit");
						for (const auto& shadow : shadows) {
							setShadow(shadow);
							const auto pixels = DrawPixels();
							const float visibility = (shadow.terrain ? shadow.terrainVisibility : 1) * (shadow.cloud ? shadow.cloudVisibility : 1);
							for (UINT texel = 0; texel < pixels.size(); ++texel)
								for (UINT channel = 0; channel < 3; ++channel) {
									const float expected = ambient[texel][channel] + (unshadowed[texel][channel] - ambient[texel][channel]) * visibility;
									if (!std::isfinite(pixels[texel][channel]) || std::abs(pixels[texel][channel] - expected) > 1e-5f)
										throw std::runtime_error("Grass world shadow mismatch: VR=" + std::to_string(vr) +
																 " enhanced=" + std::to_string(enhanced) + " PBR=" + std::to_string(pbr) + " linear=" + std::to_string(linear) +
																 " terrain=" + std::to_string(shadow.terrain) + ":" + std::to_string(shadow.terrainVisibility) +
																 " cloud=" + std::to_string(shadow.cloud) + ":" + std::to_string(shadow.cloudVisibility) +
																 " sun=" + std::to_string(sun) + " texel=" + std::to_string(texel) + " visibility=" + std::to_string(visibility) +
																 " expected=" + std::to_string(expected) + " actual=" + std::to_string(pixels[texel][channel]));
								}
							for (UINT channel = 0; channel < 3; ++channel)
								Require(std::abs(pixels[2][channel] - pixels[3][channel]) < 1e-5f, "Billboard lighting must match on both faces");
							++cases;
						}
					}
				}
		return cases;
	}
};

unsigned CheckGrassLODLighting()
{
	GrassLightingFixture fixture;
	unsigned cases = 0;
	for (bool vr : { false, true })
		cases += fixture.CheckLighting(vr);
	return cases;
}
