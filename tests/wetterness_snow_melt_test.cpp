#define NOMINMAX
#include "../package/Shaders/Common/SnowExclusions.hlsli"
#include "Features/Wetterness/SnowMeltHistory.h"
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Results = std::array<std::array<float, 4>, 8>;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void CheckMelt(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool pbr, bool areas = false, bool snowOnly = false)
	{
		PackageIncludes includes("package/Shaders", "features/Wetterness/Shaders", "features/Snow Cover/Shaders");
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" } };
		if (!snowOnly)
			defines.push_back({ "WETTERNESS", "1" });
		if (vr)
			defines.push_back({ "VR", "1" });
		if (pbr)
			defines.push_back({ "TRUE_PBR", "1" });
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> bytecode, errors;
		const auto result = D3DCompileFromFile(L"tests/wetterness_snow_melt.hlsl", defines.data(), &includes,
			areas ? "area_main" : "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SnowMeltTest::CS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		if (areas && snowOnly) {
			D3D11_SHADER_DESC compiled{};
			Check(reflection->GetDesc(&compiled));
			Require(compiled.TempRegisterCount <= 16 && compiled.InstructionCount <= 250,
				"Area mask regression: repeated polygon evaluation expanded shader registers or instructions");
		}
		D3D11ShaderTest::ConstantBuffer constants(device, reflection.Get(), "SharedData::FeatureData");
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
		const auto run = [&](const WetternessSnowMelt::PerFrame& data) {
			context->ClearState();
			if (!snowOnly)
				constants.SetVariable("SharedData::snowMeltSettings", data);
			constants.Bind(context);
			auto* target = uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			Results values;
			std::memcpy(values.data(), mapped.pData, sizeof(values));
			context->Unmap(staging.Get(), 0);
			for (const auto& value : values)
				Require(std::isfinite(value[0]) && std::isfinite(value[1]) && (areas || value[2] == 0), "Melt shader produced invalid or unbounded drying");
			return values;
		};
		if (areas) {
			SnowExclusions::MaskData exclusions{};
			exclusions.count = 1;
			auto& area = exclusions.areas[0];
			area.minX = area.minY = -10;
			area.maxX = area.maxY = 10;
			area.count = 6;
			const std::array<SnowExclusions::Vertex, 6> corners{ { { -10, -10 }, { 10, -10 }, { 10, 5 }, { 5, 5 }, { 5, 10 }, { -10, 10 } } };
			std::copy(corners.begin(), corners.end(), area.vertices);
			constants.SetMember("SharedData::snowCoverSettings", "snowExclusions", exclusions);
			constants.SetMember("SharedData::snowCoverSettings", "minAngle", 0.0f);
			constants.SetMember("SharedData::snowCoverSettings", "maxAngle", 1.0f);
			constants.SetMember("SharedData::snowCoverSettings", "TimeSnowing", 1.0f);
			constants.SetMember("SharedData::snowCoverSettings", "WeatherFadeStart", 10000.0f);
			constants.SetMember("SharedData::snowCoverSettings", "WeatherFadeEnd", 20000.0f);
			const std::array<bool, 8> excluded{ true, true, false, true, false, true, false, true };
			const auto values = run({});
			for (size_t i = 0; i < values.size(); ++i)
				for (size_t channel = 0; channel < 3; ++channel)
					Require(std::abs(values[i][channel] - (excluded[i] ? 0.0f : 1.0f)) < 0.001f, "Surface/foliage area masks disagree with closed concave boundaries");
			const auto firstArea = area;
			exclusions.count = SnowExclusions::MaxAreas;
			exclusions.areas[SnowExclusions::MaxAreas - 1] = firstArea;
			area = {};
			constants.SetMember("SharedData::snowCoverSettings", "snowExclusions", exclusions);
			const auto last = run({});
			Require(last == values, "Later area slots must preserve CPU/shader array stride");
			auto& full = exclusions.areas[SnowExclusions::MaxAreas - 1];
			full.count = SnowExclusions::MaxVertices;
			const std::array<SnowExclusions::Vertex, 16> rectangle{ { { -10, -10 }, { -5, -10 }, { 0, -10 }, { 5, -10 },
				{ 10, -10 }, { 10, -5 }, { 10, 0 }, { 10, 5 }, { 10, 10 }, { 5, 10 }, { 0, 10 }, { -5, 10 },
				{ -10, 10 }, { -10, 5 }, { -10, 0 }, { -10, -5 } } };
			std::copy(rectangle.begin(), rectangle.end(), full.vertices);
			constants.SetMember("SharedData::snowCoverSettings", "snowExclusions", exclusions);
			const auto maximum = run({});
			for (size_t i = 0; i < maximum.size(); ++i)
				Require(maximum[i][0] == (i == 2 ? 1.0f : 0.0f), "Maximum vertex count must preserve closed boundaries");
			full.count = 4;
			const std::array<SnowExclusions::Vertex, 4> diamond{ { { -10, 0 }, { 0, -7 }, { 10, 0 }, { 0, 9 } } };
			std::copy(diamond.begin(), diamond.end(), full.vertices);
			const std::array<std::array<float, 2>, 8> positions{ { { 0, 0 }, { 10, 0 }, { 20, 0 }, { -5, -5 }, { 10, 10 }, { 5, 5 }, { 6, 6 }, { 2, 8 } } };
			for (int orientation = 0; orientation < 2; ++orientation) {
				constants.SetMember("SharedData::snowCoverSettings", "snowExclusions", exclusions);
				const auto sloped = run({});
				for (size_t i = 0; i < sloped.size(); ++i)
					Require(sloped[i][0] == (SnowExclusions::ContainsAny(exclusions, positions[i][0], positions[i][1]) ? 0.0f : 1.0f),
						"CPU/shader containment must agree on sloping edges in either winding order");
				std::reverse(full.vertices, full.vertices + full.count);
			}
			exclusions = {};
			constants.SetMember("SharedData::snowCoverSettings", "snowExclusions", exclusions);
			for (const auto& value : run({}))
				Require(value[0] == 1 && value[1] == 1 && value[2] == 1, "Empty area configuration must retain native snow coverage");
			if (snowOnly)
				return;
			exclusions.count = 1;
			exclusions.areas[0] = firstArea;
			constants.SetMember("SharedData::snowCoverSettings", "snowExclusions", exclusions);
			constants.SetMember("SharedData::snowCoverSettings", "TimeSnowing", 0.0f);
			WetternessSnowMelt::PerFrame melt;
			melt.enabled = 1;
			melt.count = 2;
			melt.lightDryingHours = melt.heavyDryingHours = { 3, 3, 3, 3 };
			melt.samples[0].state = { 0, 500, 1, 1 };
			melt.samples[1].state = { 0, 500, 0, 0 };
			const auto wet = run(melt);
			for (size_t i = 0; i < wet.size(); ++i)
				Require(excluded[i] ? wet[i][3] == 0 : wet[i][3] > 0, "Excluded ground must not create phantom snowmelt wetness");
			return;
		}
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
		Require(values[2][0] > 0.1f && values[5][0] == 0, "Fire melt escaped the actual cleared snow footprint");
		data.enabled = 0;
		values = run(data);
		for (const auto& value : values)
			Require(value[0] == 0 && value[1] == 0, "Disabled melt retained wetness");
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
			for (bool pbr : { false, true }) {
				for (bool areas : { false, true })
					CheckMelt(device.Get(), context.Get(), vr, pbr, areas);
				CheckMelt(device.Get(), context.Get(), vr, pbr, true, true);
			}
		std::cout << "PASS: snow area masks with/without Wetterness, local snowmelt, material drying, fire footprints and finite retirement\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
