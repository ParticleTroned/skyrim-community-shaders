struct GrassAlphaFixture
{
	using Pixel = std::array<float, 4>;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<ID3D11Texture2D> target, staging;
	ComPtr<ID3D11RenderTargetView> rtv;
	ComPtr<ID3D11SamplerState> sampler;
	std::array<ComPtr<ID3D11ShaderResourceView>, 2> textures;

	GrassAlphaFixture()
	{
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = 4;
		desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		Check(device->CreateTexture2D(&desc, nullptr, target.GetAddressOf()));
		Name(target.Get(), "GrassAlphaTest::Target");
		Check(device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf()));
		Name(rtv.Get(), "GrassAlphaTest::Target RTV");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
		Name(staging.Get(), "GrassAlphaTest::Readback");
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.CPUAccessFlags = 0;
		// The third texel fails only after fading; the fourth sits exactly on the threshold.
		const std::array<Pixel, 8> texels{ Pixel{ 0.2f, 0.3f, 0.4f, 0.25f }, Pixel{ 0.2f, 0.3f, 0.4f, 0.75f },
			Pixel{ 0.2f, 0.3f, 0.4f, 0.75f }, Pixel{ 0.2f, 0.3f, 0.4f, 0.5f },
			Pixel{ 0.5f, 0.5f, 1, 1 }, Pixel{ 0.5f, 0.5f, 1, 1 }, Pixel{ 0.5f, 0.5f, 1, 1 }, Pixel{ 0.5f, 0.5f, 1, 1 } };
		D3D11_SUBRESOURCE_DATA initial{ texels.data(), 4 * sizeof(Pixel), 0 };
		for (UINT atlas = 0; atlas < textures.size(); ++atlas) {
			desc.Height = atlas + 1;
			ComPtr<ID3D11Texture2D> texture;
			Check(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf()));
			Name(texture.Get(), "GrassAlphaTest::Texture");
			Check(device->CreateShaderResourceView(texture.Get(), nullptr, textures[atlas].GetAddressOf()));
			Name(textures[atlas].Get(), "GrassAlphaTest::Texture SRV");
		}
		D3D11_SAMPLER_DESC sample{};
		sample.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sample.MaxLOD = D3D11_FLOAT32_MAX;
		Check(device->CreateSamplerState(&sample, sampler.GetAddressOf()));
		Name(sampler.Get(), "GrassAlphaTest::Sampler");
	}

	std::array<Pixel, 4> DrawPixels()
	{
		constexpr float clear[4]{};
		context->ClearRenderTargetView(rtv.Get(), clear);
		context->Draw(24, 0);
		context->CopyResource(staging.Get(), target.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::array<Pixel, 4> pixels{};
		std::memcpy(pixels.data(), mapped.pData, sizeof(pixels));
		context->Unmap(staging.Get(), 0);
		return pixels;
	}

	void CheckBillboardVertexNormals(bool enhanced)
	{
		constexpr const char* source = R"(
#include "RunGrass.hlsl"
#ifdef VSHADER
VS_OUTPUT NormalVertex(uint vertex : SV_VertexID) {
    const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1) };
    uint texel = vertex / 6;
    VS_INPUT input = (VS_INPUT)0;
    input.Normal.xyz = float3(37, 128, 218) / 255.0;
    input.Color = float4(1, 1, 1, 0);
    input.InstanceData1.x = texel / 2;
    input.InstanceData2 = float4(1, 0, 0, 0);
    input.InstanceData3 = float4(0, 1, 0, 1);
    VS_OUTPUT output = main(input);
    float2 corner = corners[vertex % 6];
    output.HPosition = float4(-1 + (texel + corner.x) * 0.5, 1 - corner.y * 2, 0.5, 1);
    return output;
}
#else
float4 NormalPixel(PS_INPUT input) : SV_Target0 {
#ifdef GRASS_LIGHTING
    return float4(input.VertexNormal.xyz, 1);
#else
    return float4(input.AmbientColor.xyz, 1);
#endif
}
#endif
)";
		auto defines = GrassDefines(true, enhanced, false, false, false, false, true);
		auto vertexCode = CompileProgram(defines.data(), true, source, "NormalVertex");
		defines = GrassDefines(false, enhanced, false, false, false, false, true);
		auto pixelCode = CompileProgram(defines.data(), false, source, "NormalPixel");
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11PixelShader> pixel;
		Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
		Name(vertex.Get(), "GrassAlphaTest::NormalVS");
		Check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, pixel.GetAddressOf()));
		Name(pixel.Get(), "GrassAlphaTest::NormalPS");
		// Native half records encode identity and a quarter-turn about the up axis.
		constexpr std::array<uint16_t, 32> records{
			0, 0, 0, 0x3c00, 0x3c00, 0, 0, 0, 0, 0x3c00, 0, 0x3c00, 0, 0, 0, 0,
			0, 0, 0, 0x3c00, 0, 0xbc00, 0, 0, 0x3c00, 0, 0, 0x3c00, 0, 0, 0, 0
		};
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(records);
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		D3D11_SUBRESOURCE_DATA initial{ records.data(), 0, 0 };
		ComPtr<ID3D11Buffer> buffer;
		Check(device->CreateBuffer(&desc, &initial, buffer.GetAddressOf()));
		Name(buffer.Get(), "GrassAlphaTest::NormalInstances");
		D3D11_SHADER_RESOURCE_VIEW_DESC view{};
		view.Format = DXGI_FORMAT_R32_TYPELESS;
		view.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		view.BufferEx.NumElements = sizeof(records) / sizeof(uint32_t);
		view.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
		ComPtr<ID3D11ShaderResourceView> srv;
		Check(device->CreateShaderResourceView(buffer.Get(), &view, srv.GetAddressOf()));
		Name(srv.Get(), "GrassAlphaTest::NormalInstances SRV");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		D3D11ShaderTest::ConstantBuffer params(device.Get(), reflection.Get(), "GrassBatch");
		context->ClearState();
		context->VSSetShader(vertex.Get(), nullptr, 0);
		context->PSSetShader(pixel.Get(), nullptr, 0);
		context->VSSetShaderResources(3, 1, srv.GetAddressOf());
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
		D3D11_VIEWPORT viewport{ 0, 0, 4, 1, 0, 1 };
		context->RSSetViewports(1, &viewport);
		for (UINT flags : { 0u, 1u, 3u }) {
			params.SetVariable("GrassBatchEnabled", flags);
			context->UpdateSubresource(params.buffer.Get(), 0, nullptr, params.bytes.data(), 0, 0);
			context->VSSetConstantBuffers(params.slot, 1, params.buffer.GetAddressOf());
			const auto pixels = DrawPixels();
			for (UINT texel = 0; texel < pixels.size(); ++texel) {
				Pixel expected{ 37.0f / 255 * 2 - 1, 128.0f / 255 * 2 - 1, 218.0f / 255 * 2 - 1, 1 };
				if (flags && texel >= 2)
					expected = { -expected[1], expected[0], expected[2], 1 };
				if (!enhanced && flags != 3)
					expected = { 0, 0, 0, 1 };
				for (UINT channel = 0; channel < expected.size(); ++channel)
					Require(std::isfinite(pixels[texel][channel]) && std::abs(pixels[texel][channel] - expected[channel]) < 1e-5f,
						"Production grass VS lost authored normal rotation or changed the disabled path");
			}
		}
	}

	unsigned CheckVariant(bool optimized, bool enhanced, bool pbr, bool depth, bool alpha, bool billboard = false, bool authoredPBR = false)
	{
		constexpr const char* source = R"(
#include "RunGrass.hlsl"
PS_INPUT AlphaVertex(uint vertex : SV_VertexID) {
    const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1) };
    uint texel = vertex / 6;
    uint cornerIndex = vertex % 6;
#ifdef GRASS_TEST_BILLBOARD
    if (texel >= 2) cornerIndex = 5 - cornerIndex;
#endif
    float2 corner = corners[cornerIndex];
    float4 position = float4(-1 + (texel + corner.x) * 0.5, 1 - corner.y * 2, 0.5, 1);
    PS_INPUT input = (PS_INPUT)0;
    input.HPosition = position;
    input.WorldPosition = float4(position.xy, 1, 1);
    input.PreviousWorldPosition = input.WorldPosition;
    input.ViewSpacePosition = input.WorldPosition.xyz;
#ifdef GRASS_TEST_BILLBOARD
    input.VertexMult = -2;
    // Vertical cards retain their authored upward tilt on either visible face.
    input.WorldPosition.xyz = float3(0, position.x, position.y);
    if (texel >= 2) input.WorldPosition.y = -input.WorldPosition.y;
#endif
#ifdef GRASS_LIGHTING
    input.VertexNormal = float4(0, 0, 1, 1);
#endif
#ifdef GRASS_TEST_BILLBOARD
    float3 authoredNormal = float3(37, 128, 218) / 255.0 * 2 - 1;
#ifdef GRASS_LIGHTING
    input.VertexNormal = float4(authoredNormal, 0);
#else
    input.AmbientColor.xyz = authoredNormal;
#endif
#endif
    input.TexCoord.xy = float2((texel + 0.5) / 4, 0.75);
    input.Color = float4(1, 1, 1, texel == 2 ? 0.5 : 1);
#ifdef RENDER_DEPTH
    input.Depth = float2(0.5, 1);
#endif
    return input;
}
)";
		auto defines = GrassDefines(false, enhanced, pbr, depth, alpha, false, optimized);
		if (billboard) {
			defines.back() = { "GRASS_TEST_BILLBOARD", "1" };
			// Keep the zero-initialized fixture outside scene-specific colour grading.
			defines.push_back({ "LL_COLOR_ADJUSTMENTS_USE_EXTRA_FLAGS", "1" });
			defines.push_back({ nullptr, nullptr });
		}
		ComPtr<ID3DBlob> code, vertexCode;
		try {
			code = CompileProgram(defines.data(), false);
			vertexCode = CompileProgram(defines.data(), true, source, "AlphaVertex");
		} catch (...) {
			std::cerr << "Alpha permutation: GO=" << optimized << " enhanced=" << enhanced
					  << " PBR=" << pbr << " depth=" << depth << " alpha=" << alpha << '\n';
			throw;
		}
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, pixel.GetAddressOf()));
		Name(pixel.Get(), "GrassAlphaTest::PS");
		Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
		Name(vertex.Get(), "GrassAlphaTest::VS");
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		context->ClearState();
		ComPtr<ID3D11RasterizerState> rasterizer;
		if (billboard) {
			D3D11_RASTERIZER_DESC raster{};
			raster.FillMode = D3D11_FILL_SOLID;
			raster.CullMode = D3D11_CULL_NONE;
			raster.DepthClipEnable = TRUE;
			Check(device->CreateRasterizerState(&raster, rasterizer.GetAddressOf()));
			Name(rasterizer.Get(), "GrassAlphaTest::TwoSided");
			context->RSSetState(rasterizer.Get());
		}
		std::vector<std::unique_ptr<D3D11ShaderTest::ConstantBuffer>> constants;
		D3D11_SHADER_DESC shader{};
		Check(reflection->GetDesc(&shader));
		D3D11ShaderTest::ConstantBuffer* features = nullptr;
		D3D11ShaderTest::ConstantBuffer* alphaReference = nullptr;
		for (UINT i = 0; i < shader.ConstantBuffers; ++i) {
			D3D11_SHADER_BUFFER_DESC buffer{};
			Check(reflection->GetConstantBufferByIndex(i)->GetDesc(&buffer));
			auto& value = constants.emplace_back(std::make_unique<D3D11ShaderTest::ConstantBuffer>(device.Get(), reflection.Get(), buffer.Name));
			if (std::strcmp(buffer.Name, "AlphaTestRefCB") == 0)
				alphaReference = value.get();
			if (std::strcmp(buffer.Name, "SharedData::FeatureData") == 0)
				features = value.get();
			if (billboard && std::strcmp(buffer.Name, "SharedData::SharedData") == 0) {
				value->SetVariable("SharedData::DirLightDirection", std::array<float, 4>{ 0, 0, 1, 0 });
				value->SetVariable("SharedData::DirLightColor", std::array<float, 4>{ 1, 1, 1, 1 });
			}
			if (authoredPBR && std::strcmp(buffer.Name, "Permutation::PerShader") == 0)
				value->SetVariable("Permutation::ExtraShaderDescriptor", (1u << 9) | (1u << 10));
			value->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
		}
		Require(!(depth || alpha) || alphaReference, "Alpha-tested grass must bind the engine threshold");
		context->VSSetShader(vertex.Get(), nullptr, 0);
		context->PSSetShader(pixel.Get(), nullptr, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		std::array<ID3D11RenderTargetView*, 4> outputs{};
		outputs[depth || billboard ? 0 : 3] = rtv.Get();
		context->OMSetRenderTargets(static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
		D3D11_VIEWPORT viewport{ 0, 0, 4, 1, 0, 1 };
		context->RSSetViewports(1, &viewport);
		ID3D11SamplerState* sample = sampler.Get();
		context->PSSetSamplers(0, 1, &sample);
		unsigned cases = 0;
		for (UINT path = 0; path < (enhanced ? 3u : 1u); ++path) {
			if (features) {
				features->SetMember("SharedData::grassLightingSettings", "Enabled", path != 0 ? 1u : 0u);
				features->SetMember("SharedData::grassLightingSettings", "ComplexGrassThreshold", 0.1f);
				if (billboard) {
					features->SetMember("SharedData::grassLightingSettings", "BasicGrassBrightness", 1.0f);
				}
				// Ordinary materials retain legacy shading even when True PBR is enabled.
				features->SetMember("SharedData::truePBRSettings", "Enabled", pbr ? 1u : 0u);
				features->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
			}
			ID3D11ShaderResourceView* texture = textures[authoredPBR || path == 2 ? 1 : 0].Get();
			context->PSSetShaderResources(0, 1, &texture);
			for (float threshold : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
				if (alphaReference) {
					alphaReference->SetVariable("AlphaTestRefRS", threshold);
					alphaReference->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
				}
				const auto pixels = DrawPixels();
				if (billboard && !depth && threshold == 0) {
					for (UINT texel = 0; texel < pixels.size(); ++texel)
						for (UINT channel = 0; channel < 3; ++channel)
							if (!std::isfinite(pixels[texel][channel]) || pixels[texel][channel] <= 0 ||
								std::abs(pixels[texel][channel] - pixels[0][channel]) >= 1e-5f)
								throw std::runtime_error("Far billboard lighting mismatch: enhanced=" + std::to_string(enhanced) + " PBR=" + std::to_string(pbr) +
														 " path=" + std::to_string(path) + " texel=" + std::to_string(texel) +
														 " value=" + std::to_string(pixels[texel][channel]) + " expected=" + std::to_string(pixels[0][channel]));
					Require(features != nullptr, "Far billboard must expose grass lighting settings");
					for (bool overrideMaterial : { false, true }) {
						features->SetMember("SharedData::grassLightingSettings", "BasicGrassBrightness", 2.0f);
						features->SetMember("SharedData::grassLightingSettings", "OverrideComplexGrassSettings", UINT(overrideMaterial));
						features->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
						const auto adjusted = DrawPixels();
						const float multiplier = path != 0 && (overrideMaterial || (!authoredPBR && path != 2)) ? 2.0f : 1.0f;
						for (UINT texel = 0; texel < pixels.size(); ++texel)
							for (UINT channel = 0; channel < 3; ++channel)
								Require(std::isfinite(adjusted[texel][channel]) &&
											std::abs(adjusted[texel][channel] - pixels[texel][channel] * multiplier) < 1e-5f,
									"Far billboard did not respect the material brightness override");
					}
					features->SetMember("SharedData::grassLightingSettings", "BasicGrassBrightness", 1.0f);
					features->SetMember("SharedData::grassLightingSettings", "OverrideComplexGrassSettings", 0u);
					features->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
				}
				const std::array<float, 4> fadedAlpha = authoredPBR ? std::array<float, 4>{ 1, 1, 0.5f, 1 } : std::array<float, 4>{ 0.25f, 0.75f, 0.375f, 0.5f };
				for (UINT texel = 0; texel < pixels.size(); ++texel) {
					const bool covered = !(depth || alpha) || fadedAlpha[texel] >= threshold;
					const float expectedAlpha = covered ? (depth ? fadedAlpha[texel] : 1.0f) : 0.0f;
					if (pixels[texel][3] != expectedAlpha)
						throw std::runtime_error("Grass alpha coverage mismatch: GO=" + std::to_string(optimized) + " enhanced=" + std::to_string(enhanced) +
												 " PBR=" + std::to_string(pbr) + " depth=" + std::to_string(depth) +
												 " alpha=" + std::to_string(alpha) + " path=" + std::to_string(path) +
												 " threshold=" + std::to_string(threshold) + " texel=" + std::to_string(texel));
				}
				++cases;
			}
		}
		return cases;
	}
};
