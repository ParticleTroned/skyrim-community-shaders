#define NOMINMAX
#include "Diagnostics/PerformanceCostExperiments.h"
#include "Features/Wetterness/SnowMeltHistory.h"
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <string>

namespace
{
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Results = std::array<std::array<float, 4>, 8>;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void CheckGrass(bool vr, bool pbr, bool alpha, bool grassLighting)
	{
		PackageIncludes includes("package/Shaders", "features/Wetterness/Shaders", "features/Snow Cover/Shaders", "features/Grass Lighting/Shaders");
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" } };
		if (grassLighting)
			defines.push_back({ "GRASS_LIGHTING", "1" });
		if (vr)
			defines.push_back({ "VR", "1" });
		if (pbr)
			defines.push_back({ "PBR_GRASS", "1" });
		if (alpha)
			defines.push_back({ "DO_ALPHA_TEST", "1" });
		D3D11_SHADER_DESC baseline{};
		for (bool melt : { false, true }) {
			if (melt)
				defines.push_back({ "WETTERNESS", "1" });
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> bytecode, errors;
			const auto result = D3DCompileFromFile(L"package/Shaders/RunGrass.hlsl", defines.data(), &includes,
				"main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, bytecode.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(result);
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			D3D11_SHADER_DESC descriptor{};
			Check(reflection->GetDesc(&descriptor));
			if (melt) {
				Require(descriptor.TempArrayCount <= baseline.TempArrayCount, "Snowmelt introduced a grass temporary array");
				std::cout << "Grass VR=" << vr << " PBR=" << pbr << " alpha=" << alpha << " GrassLighting=" << grassLighting
						  << ": arrays " << descriptor.TempArrayCount << "/" << baseline.TempArrayCount << std::endl;
			} else
				baseline = descriptor;
			defines.pop_back();
		}
	}

	void CheckMelt(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool pbr, uint32_t referenceFlags, bool sharedFire)
	{
		PackageIncludes includes("package/Shaders", "features/Wetterness/Shaders", "features/Snow Cover/Shaders", "tests");
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "WETTERNESS", "1" }, { "SNOW_COVER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (pbr)
			defines.push_back({ "TRUE_PBR", "1" });
		const auto referenceFlagsText = std::to_string(referenceFlags);
		defines.push_back({ "CSX_PERF_REFERENCE_FLAGS", referenceFlagsText.c_str() });
		if (sharedFire)
			defines.push_back({ "SNOW_MELT_SHARED_FIRE", "1" });
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> bytecode, errors;
		const auto result = D3DCompileFromFile(L"tests/wetterness_snow_melt.hlsl", defines.data(), &includes,
			"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SnowMeltTest::CS");
		ComPtr<ID3DBlob> assembly;
		Check(D3DDisassemble(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), 0, nullptr, assembly.GetAddressOf()));
		const std::string code(static_cast<const char*>(assembly->GetBufferPointer()), assembly->GetBufferSize());
		Require(code.find("dcl_indexableTemp") == std::string::npos, "Melt shader materialized a local history array");
		defines.insert(defines.end() - 1, { "SNOW_MELT_REFERENCE", "1" });
		ComPtr<ID3DBlob> referenceBytecode;
		errors.Reset();
		const auto referenceResult = D3DCompileFromFile(L"tests/wetterness_snow_melt.hlsl", defines.data(), &includes,
			"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, referenceBytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(referenceResult);
		ComPtr<ID3D11ComputeShader> referenceShader;
		Check(device->CreateComputeShader(referenceBytecode->GetBufferPointer(), referenceBytecode->GetBufferSize(), nullptr, referenceShader.GetAddressOf()));
		Util::SetResourceName(referenceShader.Get(), "SnowMeltTest::Reference CS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ComPtr<ID3D11ShaderReflection> referenceReflection;
		Check(D3DReflect(referenceBytecode->GetBufferPointer(), referenceBytecode->GetBufferSize(), IID_PPV_ARGS(referenceReflection.GetAddressOf())));
		D3D11_SHADER_DESC candidateDesc{}, referenceDesc{};
		Check(reflection->GetDesc(&candidateDesc));
		Check(referenceReflection->GetDesc(&referenceDesc));
		std::cout << "Snowmelt VR=" << vr << " PBR=" << pbr << " flags=" << referenceFlags
				  << " sharedFire=" << sharedFire
				  << ": instructions " << candidateDesc.InstructionCount << "/" << referenceDesc.InstructionCount
				  << ", temps " << candidateDesc.TempRegisterCount << "/" << referenceDesc.TempRegisterCount
				  << ", arrays " << candidateDesc.TempArrayCount << "/" << referenceDesc.TempArrayCount << std::endl;
		D3D11ShaderTest::ConstantBuffer constants(device, reflection.Get(), "SharedData::FeatureData");
		D3D11_SHADER_VARIABLE_DESC historyDesc{};
		Check(constants.reflection->GetVariableByName("SharedData::snowMeltSettings")->GetDesc(&historyDesc));
		Require(historyDesc.Size == sizeof(WetternessSnowMelt::ShaderData), "Snowmelt CPU/shader packing differs");
		constants.SetMember("SharedData::snowCoverSettings", "MainTint", std::array<float, 4>{ 1, 1, 1, 1 });
		constants.SetMember("SharedData::snowCoverSettings", "BlendSmoothness", 1.0f);
		constants.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", 0.0f);
		constants.SetMember("SharedData::snowCoverSettings", "FireMeltRadiusScale", 1.0f);

		ComPtr<ID3D11Buffer> output, staging;
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(Results);
		desc.StructureByteStride = sizeof(Results::value_type);
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		Check(device->CreateBuffer(&desc, nullptr, output.GetAddressOf()));
		Util::SetResourceName(output.Get(), "SnowMeltTest::Output");
		ComPtr<ID3D11UnorderedAccessView> uav;
		Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
		Util::SetResourceName(uav.Get(), "SnowMeltTest::Output UAV");
		desc.BindFlags = desc.MiscFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()));
		Util::SetResourceName(staging.Get(), "SnowMeltTest::Readback");
		const auto dispatch = [&](ID3D11ComputeShader* selected) {
			context->ClearState();
			constants.Bind(context);
			auto* target = uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(selected, nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			Results values;
			std::memcpy(values.data(), mapped.pData, sizeof(values));
			context->Unmap(staging.Get(), 0);
			for (const auto& value : values)
				Require(std::isfinite(value[0]) && std::isfinite(value[1]) && value[2] == 0, "Melt shader produced invalid or unbounded drying");
			return values;
		};
		const auto run = [&](const WetternessSnowMelt::PerFrame& data) {
			constants.SetVariable("SharedData::snowMeltSettings", WetternessSnowMelt::ShaderData(data));
			const auto actual = dispatch(shader.Get());
			const auto expected = dispatch(referenceShader.Get());
			for (size_t i = 0; i < actual.size(); ++i)
				for (size_t channel = 0; channel < actual[i].size(); ++channel)
					Require(std::abs(actual[i][channel] - expected[i][channel]) < 1e-6f, "Snowmelt preparation changed integration");
			return actual;
		};
		const auto setDryingAges = [](WetternessSnowMelt::PerFrame& data) {
			for (auto& sample : data.samples)
				for (size_t material = 0; material < 4; ++material) {
					sample.lightDryingAge[material] = sample.state[3] / data.lightDryingHours[material];
					sample.heavyDryingAge[material] = sample.state[3] / data.heavyDryingHours[material];
				}
		};

		WetternessSnowMelt::PerFrame data;
		data.enabled = 1;
		data.count = 4;
		data.lightDryingHours = { 3, 3, 3, 6 };
		data.heavyDryingHours = { 6, 9, 12, 15 };
		data.samples[0].state = { 0, 500, 1, 12 };
		data.samples[1].state = { 0, 500, 0.8f, 10 };
		data.samples[2].state = { 0, 500, 0, 1 };
		data.samples[3].state = { 0, 500, 0, 0 };
		setDryingAges(data);
		auto values = run(data);
		Require(values[0][0] == 0 && values[0][1] == 0, "Never-snowy ground became wet");
		Require(values[1][0] == 0 && values[1][1] == 0, "Early-cleared light snow stayed wet until heavy snow cleared");
		Require(values[2][0] > 0.1f && values[2][1] > 0.1f, "Heavy late-melting snow lost its local water");
		Require(values[4][0] == 0 && values[4][1] == 0, "Sheltered snow-free ground became wet");
		Require(values[6][0] < values[7][0] && values[7][0] < values[2][0], "Snowmelt ignored Wetterness material drying order");
		for (auto& sample : data.samples)
			sample.state[3] += 24;
		setDryingAges(data);
		values = run(data);
		for (const auto& value : values)
			Require(value[0] == 0 && value[1] == 0, "Old meltwater did not become completely dry");
		data.lightDryingHours = data.heavyDryingHours = { 24, 24, 24, 24 };
		values = run(data);
		for (const auto& value : values)
			Require(value[0] == 0 && value[1] == 0, "Colder arrival weather restored already-dried meltwater");

		WetternessSnowMelt::PerFrame skipped;
		skipped.enabled = 1;
		skipped.count = 2;
		skipped.lightDryingHours = skipped.heavyDryingHours = { 1, 1, 1, 1 };
		skipped.samples[0].state = { 0, 500, 1, 10 };
		skipped.samples[1].state = { 0, 500, 0, 0 };
		setDryingAges(skipped);
		const auto afterWait = run(skipped);
		Require(std::abs(afterWait[2][0] - 0.05f) < 0.001f && std::abs(afterWait[2][1] - 0.05f) < 0.001f,
			"Long wait discarded the water released during its final drying hour");
		skipped.count = 11;
		for (uint32_t i = 0; i < skipped.count; ++i)
			skipped.samples[i].state = { 0, 500, 1.0f - static_cast<float>(i) / 10.0f, 10.0f - static_cast<float>(i) };
		setDryingAges(skipped);
		const auto stepped = run(skipped);
		Require(std::abs(afterWait[2][0] - stepped[2][0]) < 0.001f && std::abs(afterWait[2][1] - stepped[2][1]) < 0.001f,
			"Waiting and ordinary game-time progression disagree on late meltwater");
		constants.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", 1.0f);
		values = run(skipped);
		Require(values[2][0] == 0 && values[2][1] == 0, "Complete snow coverage exposed historical meltwater");
		constants.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", 0.5f);
		values = run(skipped);
		Require(std::abs(values[2][0] - stepped[2][0] * 0.5f) < 0.001f && std::abs(values[2][1] - stepped[2][1] * 0.5f) < 0.001f,
			"Partial snow coverage discarded or failed to attenuate historical meltwater");

		data.count = 2;
		data.samples[0].state = { 0, 500, 1, 2 };
		data.samples[1].state = { 0, 500, 1, 0 };
		data.samples[1].fireCount = 1;
		data.samples[1].fireStrength = 1;
		data.samples[1].fires[0] = { 0, 0, 0, 10 };
		setDryingAges(data);
		constants.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", 1.0f);
		constants.SetMember("SharedData::snowCoverSettings", "FireMeltCount", 1u);
		constants.SetMember("SharedData::snowCoverSettings", "FireMeltStrength", 1.0f);
		constants.SetMember("SharedData::snowCoverSettings", "FireMeltSpheres", data.samples[1].fires);
		values = run(data);
		Require(values[2][0] > 0.1f && values[5][0] == 0, "Fire melt escaped the actual cleared snow footprint: center=" + std::to_string(values[2][0]) + ", outside=" + std::to_string(values[5][0]));

		WetternessSnowMelt::History history;
		WetternessSnowMelt::Sample sample;
		sample.state = { 0, 500, 1, 0 };
		history.Observe(sample, 0, {}, data.lightDryingHours, data.heavyDryingHours);
		sample.state[2] = 0.9f;
		sample.fireCount = 1;
		sample.fireStrength = 1;
		sample.fires[0] = { 0, 0, 0, 10 };
		history.Observe(sample, 0.25, {}, data.lightDryingHours, data.heavyDryingHours);
		sample.state[2] = 0.8f;
		history.Observe(sample, 0.5, {}, data.lightDryingHours, data.heavyDryingHours);
		sample.state[2] = 0;
		sample.fires[0][0] = 100;
		history.Observe(sample, 0.75, {}, data.lightDryingHours, data.heavyDryingHours);
		auto cachedFires = history.GetData();
		Require(cachedFires.unchangedFireMask == 4u, "Fire reuse did not distinguish unchanged and moving footprints");
		constants.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", 0.0f);
		constants.SetMember("SharedData::snowCoverSettings", "FireMeltSpheres", cachedFires.samples[cachedFires.count - 1].fires);
		const auto reused = run(cachedFires);
		cachedFires.unchangedFireMask = 0;
		const auto evaluated = run(cachedFires);
		for (size_t i = 0; i < reused.size(); ++i)
			Require(reused[i] == evaluated[i], "Fire reuse changed local melt integration");
		Require(reused[2][0] > 0 && reused[5][0] > 0, "Fire reuse comparison omitted wet footprints");
		data.enabled = 0;
		values = run(data);
		for (const auto& value : values)
			Require(value[0] == 0 && value[1] == 0, "Disabled melt retained wetness");

		constants.SetMember("SharedData::snowCoverSettings", "WeatherFadeStart", 200.0f);
		constants.SetMember("SharedData::snowCoverSettings", "WeatherFadeEnd", 1000.0f);
		constants.SetMember("SharedData::snowCoverSettings", "TimeSnowing", 1.0f);
		const auto weatherRange = run(data);
		for (const auto& value : weatherRange)
			Require(value[3] > 0 && value[3] < 1, "Weather range comparison missed the distance fade");
		constants.SetMember("SharedData::snowCoverSettings", "TimeSnowing", 0.0f);
		const auto seasonalOnly = run(data);
		const auto weatherReference = static_cast<uint32_t>(CSX::Diagnostics::PerformanceCosts::Point::SnowWeatherRange);
		for (size_t i = 0; i < seasonalOnly.size(); ++i)
			Require(seasonalOnly[i][3] == ((referenceFlags & weatherReference) ? weatherRange[i][3] : 0),
				"Seasonal-only snow did not select the requested weather-distance path");
		data.enabled = 1;
		const auto historicalWeather = run(data);
		for (size_t i = 0; i < weatherRange.size(); ++i)
			Require(historicalWeather[i][3] == weatherRange[i][3], "Stopped snowfall changed the historical weather footprint");
		data.enabled = 0;
		constants.SetMember("SharedData::snowCoverSettings", "TimeSnowing", -1.0f);
		const auto negativeWeather = run(data);
		for (size_t i = 0; i < weatherRange.size(); ++i)
			Require(negativeWeather[i][3] == weatherRange[i][3], "Negative accumulation lost its weather footprint");

		data = {};
		data.enabled = 1;
		data.count = 2;
		data.lightDryingHours = data.heavyDryingHours = { 24, 24, 24, 24 };
		data.samples[0].state = { 1, 500, 0, 1 };
		data.samples[1].state = { 0, 500, 0, 0 };
		setDryingAges(data);
		constants.SetMember("SharedData::snowCoverSettings", "TimeSnowing", 0.0f);
		constants.SetMember("SharedData::snowCoverSettings", "FireMeltCount", 0u);
		const auto stoppedWeatherMelt = run(data);
		Require(stoppedWeatherMelt[2][0] > 0 && stoppedWeatherMelt[2][1] > 0,
			"Stopped snowfall lost its historical weather snowmelt footprint");
		data.samples[0].state = { -1, 1000, -0.2f, 1 };
		data.samples[1].state = { -2, 500, -1, 0 };
		constants.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", -1.0f);
		constants.SetMember("SharedData::snowCoverSettings", "TimeSnowing", -2.0f);
		setDryingAges(data);
		const auto negativeBound = run(data);
		Require(negativeBound[5][0] > 0 && negativeBound[5][1] > 0,
			"Conservative bounds rejected melting with negative weather and altitude");
		constants.SetMember("SharedData::snowCoverSettings", "SeasonalAltitude", 0.0f);
		constants.SetMember("SharedData::snowCoverSettings", "TimeSnowing", 0.0f);

		std::mt19937 random(11921);
		std::uniform_real_distribution<float> unit(0, 1);
		for (uint32_t repeat = 0; repeat < 128; ++repeat) {
			data = {};
			data.enabled = 1;
			data.count = 2 + repeat % 31;
			for (uint32_t material = 0; material < 4; ++material) {
				data.lightDryingHours[material] = 1 + 11 * unit(random);
				data.heavyDryingHours[material] = data.lightDryingHours[material] + 12 * unit(random);
			}
			for (uint32_t i = 0; i < data.count; ++i) {
				auto& randomSample = data.samples[i];
				randomSample.state = { 4 * unit(random) - 2, 1000 * unit(random), 3 * unit(random) - 1,
					24.0f * static_cast<float>(data.count - i - 1) / static_cast<float>(data.count - 1) };
				randomSample.fireCount = (repeat + i) % 9;
				randomSample.fireStrength = unit(random);
				randomSample.fireRadiusScale = 1 + 3 * unit(random);
				for (uint32_t fire = 0; fire < randomSample.fireCount; ++fire)
					randomSample.fires[fire] = { 100 * unit(random), 30 * unit(random), 0, 6 + 50 * unit(random) };
			}
			setDryingAges(data);
			run(data);
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
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				for (bool alpha : { false, true })
					for (bool grassLighting : { false, true })
						if (grassLighting || !pbr)
							CheckGrass(vr, pbr, alpha, grassLighting);
		using CSX::Diagnostics::PerformanceCosts::Point;
		const auto weatherReference = static_cast<uint32_t>(Point::SnowWeatherRange);
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				for (uint32_t referenceFlags : { 0u, weatherReference })
					for (bool sharedFire : { false, true })
						CheckMelt(device.Get(), context.Get(), vr, pbr, referenceFlags, sharedFire);
		std::cout << "PASS: local snowmelt, material drying, fire footprints and finite retirement\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
