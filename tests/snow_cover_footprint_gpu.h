void CheckFootprintGate(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, bool pbr, const char* technique, bool melt = false)
{
	std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" } };
	if (vr)
		defines.push_back({ "VR", "1" });
	if (pbr)
		defines.push_back({ "TRUE_PBR", "1" });
	if (melt)
		defines.push_back({ "WETTERNESS", "1" });
	if (*technique)
		defines.push_back({ technique, "1" });
	if (std::strcmp(technique, "LODLANDSCAPE") == 0 || std::strcmp(technique, "LODLANDNOISE") == 0)
		defines.push_back({ "MODELSPACENORMALS", "1" });
	std::array<ComPtr<ID3D11PixelShader>, 2> shaders;
	std::array<ComPtr<ID3D11ShaderReflection>, 2> reflections;
	for (size_t i = 0; i < shaders.size(); ++i) {
		if (i == 1)
			defines.push_back({ "SNOW_SCALE_REFERENCE", "1" });
		auto code = CompilePixel(defines, "FootprintGate", L"tests/snow_cover_color.hlsl", "features/Wetterness/Shaders");
		Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shaders[i]));
		Util::SetResourceName(shaders[i].Get(), "SnowFootprintTest::PS %zu", i);
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflections[i])));
	}
	// Different mip colours expose derivative changes hidden by a constant texture.
	std::array<std::vector<Color>, 4> levels;
	std::array<D3D11_SUBRESOURCE_DATA, 4> data{};
	for (UINT mip = 0; mip < levels.size(); ++mip) {
		const UINT size = 8u >> mip;
		levels[mip].resize(size * size, Color{ .1f + .2f * float(mip), .3f, .5f, 1 });
		data[mip] = { levels[mip].data(), size * UINT(sizeof(Color)), 0 };
	}
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = desc.Height = 8;
	desc.MipLevels = UINT(levels.size());
	desc.ArraySize = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	ComPtr<ID3D11Texture2D> texture;
	Check(device->CreateTexture2D(&desc, data.data(), &texture));
	Util::SetResourceName(texture.Get(), "SnowFootprintTest::Mips");
	ComPtr<ID3D11ShaderResourceView> albedo;
	Check(device->CreateShaderResourceView(texture.Get(), nullptr, &albedo));
	Util::SetResourceName(albedo.Get(), "SnowFootprintTest::Mips SRV");
	const auto normal = Texture(device, { .5f, .5f, 1, 1 });
	const auto rmaos = Texture(device, { .5f, 0, 1, .04f });
	const auto map = Texture(device, { .5f, .5f, .5f, .5f });
	std::array views{ albedo.Get(), normal.Get(), rmaos.Get(), albedo.Get(), normal.Get(), rmaos.Get(), map.Get() };
	ComPtr<ID3D11SamplerState> sampler;
	D3D11_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	Check(device->CreateSamplerState(&samplerDesc, &sampler));
	Util::SetResourceName(sampler.Get(), "SnowFootprintTest::Sampler");
	RenderTarget<4, 4> target(device);
	for (uint32_t scenario = 0; scenario < 6; ++scenario) {
		std::array<std::array<Color, 16>, 2> results{};
		for (size_t i = 0; i < shaders.size(); ++i) {
			ConstantBuffer features(device, reflections[i].Get(), "SharedData::FeatureData");
			ConstantBuffer samples(device, reflections[i].Get(), "Samples");
			features.SetMember("SharedData::snowCoverSettings", "BlendSmoothness", 1.0f);
			features.SetMember("SharedData::snowCoverSettings", "minAngle", .5f);
			features.SetMember("SharedData::snowCoverSettings", "maxAngle", 1.0f);
			features.SetMember("SharedData::snowCoverSettings", "ObjectFadeStart", 100.0f);
			features.SetMember("SharedData::snowCoverSettings", "ObjectFadeEnd", 200.0f);
			features.SetMember("SharedData::snowCoverSettings", "ObjectFadeAmount", 1.0f);
			features.SetMember("SharedData::snowCoverSettings", "UVScale", 11.0f);
			features.SetMember("SharedData::snowCoverSettings", "MainTint", Color{ 1, 1, 1, 1 });
			features.SetMember("SharedData::snowCoverSettings", "AltTint", Color{ 1, 1, 1, 1 });
			if (melt) {
				features.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", -.5f);
				features.SetMember("SharedData::snowMeltSettings", "enabled", 1u);
				features.SetMember("SharedData::snowMeltSettings", "count", 2u);
				features.SetMember("SharedData::snowMeltSettings", "maximumState", Color{ 0, 500, 0, 0 });
				std::array<Color, 32> states{}, ages{};
				states[0] = { 0, 500, 0, .2f };
				states[1] = { 0, 500, -.5f, 0 };
				ages[0] = { .2f, .2f, .2f, .2f };
				features.SetMember("SharedData::snowMeltSettings", "states", states);
				features.SetMember("SharedData::snowMeltSettings", "lightDryingAges", ages);
				features.SetMember("SharedData::snowMeltSettings", "heavyDryingAges", ages);
			}
			samples.SetVariable("FootprintScenario", scenario);
			target.Bind(context, vertex, shaders[i].Get());
			features.Bind(context, D3D11ShaderTest::Stage::Pixel);
			samples.Bind(context, D3D11ShaderTest::Stage::Pixel);
			context->PSSetShaderResources(112, UINT(views.size()), views.data());
			std::array samplers{ sampler.Get(), sampler.Get() };
			context->PSSetSamplers(0, UINT(samplers.size()), samplers.data());
			context->Draw(3, 0);
			context->ClearState();
			results[i] = target.Read(context);
		}
		if (melt && scenario == 3 && !(results[0][0][0] > 0 && results[0][0][1] > 0))
			throw std::runtime_error("Snow footprint test did not exercise active historical melt");
		for (size_t pixel = 0; pixel < 16; ++pixel)
			for (size_t channel = 0; channel < 4; ++channel)
				if (!std::isfinite(results[0][pixel][channel]) || !std::isfinite(results[1][pixel][channel]) || std::abs(results[0][pixel][channel] - results[1][pixel][channel]) > 2e-5f)
					throw std::runtime_error("Snow footprint rejection changed mixed-quad output or derivatives: VR=" + std::to_string(vr) + " PBR=" + std::to_string(pbr) + " melt=" + std::to_string(melt) + " technique=" + technique + " scenario=" + std::to_string(scenario) + " pixel=" + std::to_string(pixel) + " channel=" + std::to_string(channel) + " actual=" + std::to_string(results[0][pixel][channel]) + " expected=" + std::to_string(results[1][pixel][channel]));
	}
}
