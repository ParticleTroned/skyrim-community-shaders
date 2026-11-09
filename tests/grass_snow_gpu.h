struct GrassSnowFixture : GrassAlphaFixture
{
	static constexpr Pixel baseColor{ 0.2f, 0.3f, 0.4f, 1 };
	static constexpr Pixel snowColor{ 0.25f, 0.36f, 0.49f, 1 };
	ComPtr<ID3D11ShaderResourceView> baseTexture, snowTexture;

	GrassSnowFixture()
	{
		const auto texture = [&](const Pixel& color, const char* name, ComPtr<ID3D11ShaderResourceView>& view) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA initial{ color.data(), sizeof(Pixel), 0 };
			ComPtr<ID3D11Texture2D> resource;
			Check(device->CreateTexture2D(&desc, &initial, resource.GetAddressOf()));
			Util::SetResourceName(resource.Get(), "GrassSnowTest::%s", name);
			Check(device->CreateShaderResourceView(resource.Get(), nullptr, view.GetAddressOf()));
			Util::SetResourceName(view.Get(), "GrassSnowTest::%s SRV", name);
		};
		texture(baseColor, "Base", baseTexture);
		texture(snowColor, "Snow", snowTexture);
	}

	unsigned CheckColor(bool vr, bool billboard)
	{
		constexpr const char* source = R"(
#include "RunGrass.hlsl"
PS_INPUT SnowVertex(uint vertex : SV_VertexID) {
    const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1) };
    uint texel = vertex / 6;
    float2 corner = corners[vertex % 6];
    PS_INPUT input = (PS_INPUT)0;
    input.HPosition = float4(-1 + (texel + corner.x) * 0.5, 1 - corner.y * 2, 0.5, 1);
    input.WorldPosition = float4(input.HPosition.xy, 10, 1);
    input.PreviousWorldPosition = input.WorldPosition;
    input.VertexNormal = float4(0, 0, 1, 1);
    input.Color = 1;
    input.TexCoord.xy = float2(0.5, 0.75);
#ifdef GRASS_SNOW_BILLBOARD
    input.VertexMult = -2;
#endif
#ifdef VR
    input.ClipDistance = input.CullDistance = 1;
#endif
    return input;
}
float4 SnowPixel(PS_INPUT input, bool frontFace : SV_IsFrontFace) : SV_Target0 {
    return RenderBasicGrass(input, frontFace).Albedo;
}
)";
		auto defines = GrassDefines(false, vr, true, true, false, false, false, true);
		defines.back() = { "SNOW_COVER", "1" };
		if (billboard)
			defines.push_back({ "GRASS_SNOW_BILLBOARD", "1" });
		defines.push_back({ nullptr, nullptr });
		auto pixelCode = CompileProgram(defines.data(), false, source, "SnowPixel");
		auto vertexCode = CompileProgram(defines.data(), true, source, "SnowVertex");
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, pixel.GetAddressOf()));
		Util::SetResourceName(pixel.Get(), "GrassSnowTest::PS");
		Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
		Util::SetResourceName(vertex.Get(), "GrassSnowTest::VS");
		Check(D3DReflect(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		context->ClearState();
		std::vector<std::unique_ptr<D3D11ShaderTest::ConstantBuffer>> constants;
		D3D11ShaderTest::ConstantBuffer* features = nullptr;
		D3D11ShaderTest::ConstantBuffer* permutation = nullptr;
		D3D11_SHADER_DESC shader{};
		Check(reflection->GetDesc(&shader));
		for (UINT i = 0; i < shader.ConstantBuffers; ++i) {
			D3D11_SHADER_BUFFER_DESC buffer{};
			Check(reflection->GetConstantBufferByIndex(i)->GetDesc(&buffer));
			auto& value = constants.emplace_back(std::make_unique<D3D11ShaderTest::ConstantBuffer>(device.Get(), reflection.Get(), buffer.Name));
			if (std::strcmp(buffer.Name, "SharedData::FeatureData") == 0)
				features = value.get();
			if (std::strcmp(buffer.Name, "Permutation::PerShader") == 0)
				permutation = value.get();
			value->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
		}
		Require(features && permutation, "Grass snow must retain its material and feature controls");
		features->SetMember("SharedData::snowCoverSettings", "EnableSnowCover", 1u);
		features->SetMember("SharedData::snowCoverSettings", "BlendSmoothness", 1.0f);
		features->SetMember("SharedData::snowCoverSettings", "MainTint", Pixel{ 1, 1, 1, 1 });
		features->SetMember("SharedData::snowCoverSettings", "minAngle", -1.0f);
		features->SetMember("SharedData::snowCoverSettings", "maxAngle", 0.0f);
		features->SetMember("SharedData::grassLightingSettings", "BasicGrassBrightness", 1.0f);
		features->SetMember("SharedData::grassLightingSettings", "ComplexGrassThreshold", 0.01f);
		features->SetMember("SharedData::linearLightingSettings", "colorGamma", 2.2f);
		features->SetMember("SharedData::linearLightingSettings", "vanillaDiffuseColorMult", 0.75f);
		context->VSSetShader(vertex.Get(), nullptr, 0);
		context->PSSetShader(pixel.Get(), nullptr, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
		const D3D11_VIEWPORT viewport{ 0, 0, 4, 1, 0, 1 };
		context->RSSetViewports(1, &viewport);
		context->PSSetSamplers(0, 1, sampler.GetAddressOf());
		context->PSSetShaderResources(0, 1, baseTexture.GetAddressOf());
		context->PSSetShaderResources(112, 1, snowTexture.GetAddressOf());
		unsigned cases = 0;
		for (bool pbr : { false, true })
			for (bool linear : { false, true }) {
				permutation->SetVariable("Permutation::ExtraShaderDescriptor", 1u | (pbr ? 1u << 9 : 0u));
				permutation->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
				features->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", UINT(linear));
				features->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
				const auto pixels = DrawPixels();
				for (const auto& actual : pixels)
					for (UINT channel = 0; channel < 3; ++channel) {
						const auto srgb = [](float color) { return std::pow(color, 1.0f / 2.2f); };
						float base = baseColor[channel], snow = srgb(snowColor[channel]);
						if (billboard) {
							base = pbr ? (linear ? base : srgb(base)) : (linear ? std::pow(base, 2.2f) * .75f : base);
							if (linear)
								snow = snowColor[channel] * (pbr ? 1.0f : .75f);
						}
						snow *= linear ? 1.0f : .65f;
						const float expected = base * .45f + snow * .55f;
						Require(std::isfinite(actual[channel]) && std::abs(actual[channel] - expected) < 1e-5f,
							"Snow changed grass material color space or the ordinary basic path");
					}
				++cases;
			}
		return cases;
	}
};

unsigned CheckGrassSnowColor()
{
	GrassSnowFixture fixture;
	unsigned cases = 0;
	for (bool vr : { false, true })
		for (bool billboard : { false, true })
			cases += fixture.CheckColor(vr, billboard);
	return cases;
}
