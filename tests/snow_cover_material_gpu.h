void CheckGrassFireReuse(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, unsigned mode)
{
	std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" }, { "WETTERNESS", "1" } };
	if (vr)
		defines.push_back({ "VR", "1" });
	if (mode != 0)
		defines.push_back({ "GRASS_LIGHTING", "1" });
	if (mode == 2)
		defines.push_back({ "PBR_GRASS", "1" });
	auto bytecode = CompilePixel(defines, "FireReuse", L"tests/snow_cover_grass_material.hlsl");
	ComPtr<ID3D11PixelShader> shader;
	Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "SnowFireReuseTest::PS");
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	ConstantBuffer features(device, reflection.Get(), "SharedData::FeatureData");
	ConstantBuffer samples(device, reflection.Get(), "Samples");
	ConstantBuffer permutation(device, reflection.Get(), "Permutation::PerShader");
	permutation.SetVariable("Permutation::ExtraShaderDescriptor", 1u);
	features.SetMember("SharedData::snowCoverSettings", "MainTint", Color{ 1, 1, 1, 1 });
	features.SetMember("SharedData::snowCoverSettings", "BlendSmoothness", 1.0f);
	features.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", 0.5f);
	features.SetMember("SharedData::snowCoverSettings", "FireMeltRadiusScale", 1.0f);
	features.SetMember("SharedData::snowCoverSettings", "FireMeltStrength", 0.8f);
	features.SetMember("SharedData::snowCoverSettings", "EnableExpensiveFoliage", 1u);
	features.SetMember("SharedData::linearLightingSettings", "colorGamma", 2.2f);
	features.SetMember("SharedData::linearLightingSettings", "vanillaDiffuseColorMult", 1.0f);
	std::array<Color, 8> fires{};
	for (unsigned i = 0; i < fires.size(); ++i)
		fires[i] = { float(i), 0, 0, 10 };
	features.SetMember("SharedData::snowCoverSettings", "FireMeltSpheres", fires);
	const auto albedo = Texture(device, { 0.9f, 0.8f, 0.7f, 1 });
	ComPtr<ID3D11SamplerState> sampler;
	D3D11_SAMPLER_DESC desc{};
	desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.MaxLOD = D3D11_FLOAT32_MAX;
	Check(device->CreateSamplerState(&desc, sampler.GetAddressOf()));
	Util::SetResourceName(sampler.Get(), "SnowFireReuseTest::Sampler");
	Target actual(device), expected(device);
	for (uint32_t fireCount : { 0u, 1u, 8u }) {
		features.SetMember("SharedData::snowCoverSettings", "FireMeltCount", fireCount);
		for (unsigned scenario = 0; scenario < 10; ++scenario) {
			WetternessSnowMelt::PerFrame history;
			history.enabled = scenario == 2 ? 0u : 1u;
			history.count = scenario == 3 ? 1u : 2u;
			history.samples[0].state = { 0, 500, 1, 0.2f };
			history.samples[1].state = { 0, 500, 0.5f, 0 };
			if (scenario == 7)
				history.samples[0].state[2] = history.samples[1].state[2] = -2;
			history.samples[0].lightDryingAge.fill(0.2f);
			history.samples[0].heavyDryingAge.fill(0.2f);
			features.SetVariable("SharedData::snowMeltSettings", WetternessSnowMelt::ShaderData(history));
			features.SetMember("SharedData::snowCoverSettings", "EnableSnowCover", scenario == 1 ? 0u : 1u);
			features.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", scenario == 8 ? 1.0f : 0.5f);
			features.SetMember("SharedData::wetternessSettings", "GrassWetDarkeningStrength", scenario == 5 ? 0.0f : 1.0f);
			features.SetMember("SharedData::wetternessSettings", "GrassWetnessPhase", scenario == 6 ? 1.0f : 0.0f);
			samples.SetVariable("CoatingCoverage", scenario == 4 || scenario == 9 ? 0.0f : 0.7f);
			samples.SetVariable("FireFootprintScale", scenario == 9 ? 0.0f : 1.0f);
			actual.Bind(context, vertex, shader.Get());
			features.Bind(context, D3D11ShaderTest::Stage::Pixel);
			samples.Bind(context, D3D11ShaderTest::Stage::Pixel);
			permutation.Bind(context, D3D11ShaderTest::Stage::Pixel);
			context->PSSetShaderResources(112, 1, albedo.GetAddressOf());
			context->PSSetSamplers(0, 1, sampler.GetAddressOf());
			ID3D11RenderTargetView* targets[]{ actual.view.Get(), expected.view.Get() };
			context->OMSetRenderTargets(2, targets, nullptr);
			context->Draw(3, 0);
			context->ClearState();
			const auto values = actual.Read(context), reference = expected.Read(context);
			if (scenario == 0 && !(values[2][0] < 0.08f && values[0][3] > 0))
				throw std::runtime_error("Grass fire test missed active darkening or coating");
			if ((scenario == 1 || scenario == 9) && !(values[0][0] == 0.08f && values[0][3] == 0))
				throw std::runtime_error("Inactive grass footprint changed color or coating");
			if ((scenario == 7 || (scenario == 8 && fireCount == 0)) && !(values[2][0] == 0.08f && values[0][3] > 0))
				throw std::runtime_error("Grass fire test missed a lazy history exit followed by coating");
			for (size_t route = 0; route < values.size(); ++route)
				for (size_t channel = 0; channel < 4; ++channel)
					if (!std::isfinite(values[route][channel]) || !std::isfinite(reference[route][channel]) || std::abs(values[route][channel] - reference[route][channel]) > 1e-6f)
						throw std::runtime_error("Grass current-fire reuse changed coating or melt darkening");
		}
	}
}

size_t CheckFoliageMaterial(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, bool animated, bool grass = false, bool wetness = false)
{
	std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" }, { "DO_ALPHA_TEST", "1" } };
	if (!grass)
		defines.push_back({ "TRUE_PBR", "1" });
	if (vr)
		defines.push_back({ "VR", "1" });
	if (animated)
		defines.push_back({ "TREE_ANIM", "1" });
	if (wetness)
		defines.push_back({ "WETTERNESS", "1" });
	if (grass) {
		defines.push_back({ "GRASS_LIGHTING", "1" });
		defines.push_back({ "PBR_GRASS", "1" });
	}
	if (grass)
		CompilePixel(defines, "ProductionGrassMain", L"tests/snow_cover_grass_material.hlsl");
	auto bytecode = CompilePixel(std::move(defines), "FoliageMaterial", grass ? L"tests/snow_cover_grass_material.hlsl" : L"tests/snow_cover_color.hlsl",
		wetness ? "features/Wetterness/Shaders" : "features/Grass Lighting/Shaders");
	ComPtr<ID3D11PixelShader> shader;
	Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "SnowMaterialTest::PS");
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	ConstantBuffer features(device, reflection.Get(), "SharedData::FeatureData");
	ConstantBuffer samples(device, reflection.Get(), "Samples");
	ConstantBuffer permutation(device, reflection.Get(), "Permutation::PerShader");
	ConstantBuffer material(device, reflection.Get(), "PerMaterial");
	features.SetMember("SharedData::snowCoverSettings", "BlendSmoothness", 1.0f);
	features.SetMember("SharedData::snowCoverSettings", "EnableSnowCover", 1u);
	features.SetMember("SharedData::snowCoverSettings", "maxAngle", 1.0f);
	features.SetMember("SharedData::snowCoverSettings", "peakMainAngle", 1.0f);
	features.SetMember("SharedData::snowCoverSettings", "peakAltAngle", -10.0f);
	features.SetMember("SharedData::snowCoverSettings", "Glint", Color{ 4, 2, 0.1f, 1 });
	features.SetMember("SharedData::linearLightingSettings", "colorGamma", 2.2f);
	features.SetMember("SharedData::linearLightingSettings", "vanillaDiffuseColorMult", 1.0f);
	features.SetMember("SharedData::foliageLightingSettings", "FoliageAmbientAmount", 0.3f);
	if (wetness) {
		features.SetMember("SharedData::wetternessSettings", "WetDarkeningStrength", 1.0f);
		features.SetMember("SharedData::wetternessSettings", "ShorePersistentDarkeningStrength", 1.0f);
	}
	if (grass) {
		features.SetMember("SharedData::snowCoverSettings", "FoliageHeightOffset", -2048.0f);
		features.SetMember("SharedData::snowCoverSettings", "SnowHeightOffset", 100.0f);
		features.SetMember("SharedData::snowCoverSettings", "WeatherFadeStart", 1000.0f);
		features.SetMember("SharedData::snowCoverSettings", "WeatherFadeEnd", 2000.0f);
		features.SetMember("SharedData::snowCoverSettings", "FireMeltCount", 1u);
		features.SetMember("SharedData::snowCoverSettings", "FireMeltRadiusScale", 1.0f);
		std::array<Color, 8> fires{};
		fires[0] = { 0, 0, 100, 10 };
		features.SetMember("SharedData::snowCoverSettings", "FireMeltSpheres", fires);
	}
	const auto albedo = Texture(device, { 0.9f, 0.8f, 0.7f, 1 });
	const auto normal = Texture(device, { 0.5f, 0.5f, 1, 1 });
	const auto rmaos = Texture(device, { 0.8f, 0, 1, 0.04f });
	const auto transparent = Texture(device, { 0.08f, 0.3f, 0.04f, 0 });
	ComPtr<ID3D11SamplerState> sampler;
	D3D11_SAMPLER_DESC desc{};
	desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.MaxLOD = D3D11_FLOAT32_MAX;
	Check(device->CreateSamplerState(&desc, sampler.GetAddressOf()));
	Util::SetResourceName(sampler.Get(), "SnowMaterialTest::Sampler");
	Target actual(device), expected(device);
	size_t count = 0;
	for (uint32_t flags : { 16u, 528u, 32u }) {
		material.SetVariable("PBRFlags", flags);
		for (bool plant : { false, true }) {
			permutation.SetVariable("Permutation::ExtraShaderDescriptor", (grass && !plant) ? 0u : 1u | (plant ? (1u << 29) : 0u));
			samples.SetVariable("PlantSurface", static_cast<uint32_t>(plant));
			for (float projected : { 0.0f, 0.4f, 1.0f }) {
				samples.SetVariable("ProjectedCoverage", projected);
				features.SetMember("SharedData::snowCoverSettings", "FireMeltStrength", grass ? projected : 0.0f);
				for (float coverage : { 0.0f, 0.005f, 0.25f, 1.0f }) {
					samples.SetVariable("CoatingCoverage", coverage);
					for (float alpha : { 0.0f, 0.5f, 1.0f }) {
						features.SetMember("SharedData::snowCoverSettings", "MainTint", Color{ 0.6f, 0.8f, 1, alpha });
						for (unsigned mode : { 0u, 1u, 2u }) {
							if (!grass && mode == 2)
								continue;
							const bool enabled = mode != 0;
							features.SetMember("SharedData::snowCoverSettings", "EnableSnowCover", mode == 2 ? 0u : 1u);
							features.SetMember("SharedData::snowCoverSettings", "EnableGlints", static_cast<uint32_t>(enabled));
							features.SetMember("SharedData::foliageLightingSettings", "EnableFoliageScattering", static_cast<uint32_t>(enabled));
							features.SetMember("SharedData::foliageLightingSettings", "EnableFoliageAmbientBoost", static_cast<uint32_t>(enabled));
							features.SetMember("SharedData::foliageLightingSettings", "EnableGrassScattering", static_cast<uint32_t>(enabled));
							features.SetMember("SharedData::snowCoverSettings", "EnableExpensiveFoliage", static_cast<uint32_t>(enabled));
							features.SetMember("SharedData::snowCoverSettings", "AffectGrassTint", static_cast<uint32_t>(enabled));
							features.SetMember("SharedData::snowCoverSettings", "TimeSnowing", grass && enabled ? 0.35f : 0.0f);
							context->ClearState();
							features.Bind(context, D3D11ShaderTest::Stage::Pixel);
							samples.Bind(context, D3D11ShaderTest::Stage::Pixel);
							permutation.Bind(context, D3D11ShaderTest::Stage::Pixel);
							material.Bind(context, D3D11ShaderTest::Stage::Pixel);
							ID3D11ShaderResourceView* textures[]{ albedo.Get(), normal.Get(), rmaos.Get(), albedo.Get(), normal.Get(), rmaos.Get() };
							context->PSSetShaderResources(112, 6, textures);
							if (grass)
								context->PSSetShaderResources(0, 1, transparent.GetAddressOf());
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
							const auto values = actual.Read(context), reference = expected.Read(context);
							for (size_t route = 0; route < values.size(); ++route)
								for (size_t channel = 0; channel < 4; ++channel)
									if (!std::isfinite(values[route][channel]) || !std::isfinite(reference[route][channel]) || std::abs(values[route][channel] - reference[route][channel]) > 2e-5f)
										throw std::runtime_error("Foliage snow material/lighting mismatch: tree=" + std::to_string(animated) + " plant=" + std::to_string(plant) + " coverage=" + std::to_string(coverage) + " route=" + std::to_string(route));
							++count;
						}
					}
				}
			}
		}
	}
	return count;
}
