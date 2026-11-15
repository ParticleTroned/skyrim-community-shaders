#define NOMINMAX
#include "Features/WetternessLightingPolicy.h"
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	using Samples = std::array<Pixel, 3072>;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	bool Close(float a, float b)
	{
		return std::abs(a - b) <= 2e-5f * std::max(1.0f, std::abs(b));
	}

	void CheckNightPolicy()
	{
		using WetternessLightingPolicy::NightFactor;
		for (float hour : { 8.0f, 12.0f, 18.0f })
			Require(NightFactor(hour, 6, 8, 18, 20) == 0, "Daytime must bypass suppression");
		for (float hour : { -1.0f, 0.0f, 6.0f, 20.0f, 24.0f, 25.0f })
			Require(NightFactor(hour, 6, 8, 18, 20) == 1, "Night weight or midnight wrapping incorrect");
		Require(NightFactor(7, 6, 8, 18, 20) == 0.5f && NightFactor(19, 6, 8, 18, 20) == 0.5f, "Twilight must blend smoothly");
		Require(NightFactor(12, 10, 14, 16, 18) == 0.5f, "Climate timing must override fixed clock hours");
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		for (float bad : { nan, inf, -inf }) {
			Require(NightFactor(bad, 6, 8, 18, 20) == 0, "Invalid hour must preserve daytime behavior");
			Require(NightFactor(0, bad, 8, 18, 20) == 0 && NightFactor(0, 6, bad, 18, 20) == 0 &&
						NightFactor(0, 6, 8, bad, 20) == 0 && NightFactor(0, 6, 8, 18, bad) == 0,
				"Invalid climate must preserve daytime behavior");
		}
		Require(NightFactor(0, 8, 6, 18, 20) == 0 && NightFactor(0, 6, 8, 8, 20) == 0 &&
					NightFactor(0, 6, 8, 18, 25) == 0,
			"Malformed climate timing accepted");
		Require(NightFactor(0, 6, 6, 18, 18) == 1 && NightFactor(6, 6, 6, 18, 18) == 0 &&
					NightFactor(12, 6, 6, 18, 18) == 0 && NightFactor(18, 6, 6, 18, 18) == 1,
			"Instant climate transitions must remain valid without division by zero");
		float previous = 1;
		for (int i = 0; i <= 200; ++i) {
			const float phase = static_cast<float>(i) * 0.01f;
			const float dawn = NightFactor(6 + phase, 6, 8, 18, 20);
			Require(dawn <= previous && dawn >= 0 && dawn <= 1, "Dawn transition must be monotonic and bounded");
			Require(Close(dawn, 1 - NightFactor(18 + phase, 6, 8, 18, 20)), "Dawn and dusk transitions disagree");
			previous = dawn;
		}
	}

	struct Fixture
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11Buffer> output, staging;
		ComPtr<ID3D11UnorderedAccessView> uav;
		std::unique_ptr<ConstantBuffer> feature, inputs;

		Fixture(ID3D11Device* device, bool vr, bool pbr, const char* entry = "main", bool reference = false)
		{
			PackageIncludes includes("package/Shaders", "features/Wetterness/Shaders");
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "WETTERNESS", "1" } };
			if (reference)
				defines.push_back({ "WETTERNESS_REFERENCE", "1" });
			if (vr)
				defines.push_back({ "VR", "1" });
			if (pbr)
				defines.push_back({ "TRUE_PBR", "1" });
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> bytecode, errors;
			const auto compiled = D3DCompileFromFile(L"tests/wetterness_highlights.hlsl", defines.data(), &includes, entry, "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
				bytecode.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiled);
			std::cerr << "Compiled wet lighting fixture VR=" << vr << " PBR=" << pbr << "\n";
			Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "WetternessTest::CS");
			Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			if (std::strcmp(entry, "main") == 0) {
				feature = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
				inputs = std::make_unique<ConstantBuffer>(device, reflection.Get(), "TestInputs");
				auto* wetnessType = reflection->GetConstantBufferByName("SharedData::FeatureData")->GetVariableByName("SharedData::wetternessSettings")->GetType();
				D3D11_SHADER_TYPE_DESC night{};
				Check(wetnessType->GetMemberTypeByName("NightFactor")->GetDesc(&night));
				Require(night.Offset == 260, "Wetterness night field changed the CPU/GPU layout");
			}
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = static_cast<UINT>(sizeof(Samples));
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(Pixel);
			Check(device->CreateBuffer(&desc, nullptr, output.GetAddressOf()));
			Util::SetResourceName(output.Get(), "WetternessTest::Output");
			Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
			Util::SetResourceName(uav.Get(), "WetternessTest::Output UAV");
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = desc.MiscFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "WetternessTest::Readback");
		}

		Samples Run(ID3D11DeviceContext* context, float night, float luminance, float lightScale, unsigned mode)
		{
			if (feature) {
				feature->SetMember("SharedData::wetternessSettings", "NightFactor", night);
				feature->SetMember("SharedData::wetternessSettings", "EnableModernWetReflection", unsigned(mode != 1));
				feature->SetMember("SharedData::wetternessSettings", "EnableLegacyWetReflection", unsigned(mode != 0));
				feature->SetMember("SharedData::wetternessSettings", "EnableForwardReflectionBias", 1u);
				feature->SetMember("SharedData::wetternessSettings", "EnableVanillaReflectionCompensation", 1u);
				inputs->SetVariable("directionalLuminance", luminance);
				inputs->SetVariable("lightScale", lightScale);
			}
			context->ClearState();
			if (feature) {
				feature->Bind(context);
				inputs->Bind(context);
			}
			ID3D11UnorderedAccessView* target = uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(16, 1, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			Samples samples;
			std::memcpy(samples.data(), mapped.pData, sizeof(samples));
			context->Unmap(staging.Get(), 0);
			return samples;
		}
	};

	void CheckDistribution(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		Fixture fixture(device, false, false, "CheckDistribution");
		const auto samples = fixture.Run(context, 0, 0, 0, 0);
		double maximumRelativeError = 0;
		for (size_t i = 0; i < 1024; ++i) {
			const auto& distribution = samples[i * 3];
			const double roughness = distribution[1], cosine = distribution[2];
			const double a2 = std::pow(roughness, 4);
			const double denominator = (1 - cosine * cosine) + cosine * cosine * a2;
			const double expected = a2 / (3.14159265358979323846 * denominator * denominator);
			const double relativeError = std::abs(distribution[0] - expected) / expected;
			Require(std::isfinite(distribution[0]) && distribution[0] > 0 && relativeError < 5e-4,
				"GGX distribution lost accuracy near mirror alignment");
			maximumRelativeError = std::max(maximumRelativeError, relativeError);
			const auto& compression = samples[i * 3 + 1];
			const double response = compression[1], night = compression[2];
			const double excess = std::max(response - 0.25, 0.0);
			const double compressed = std::min(response, 0.25) + excess / (1 + excess / 0.75);
			const double expectedScale = (1 - night) + night * compressed / response;
			Require(std::isfinite(compression[0]) && compression[0] >= 0 && compression[0] <= 1 &&
						std::abs(compression[0] - expectedScale) < 2e-6,
				"Wet highlight compression changed its response curve");
			if (response <= 0.25 || night == 0)
				Require(compression[0] == 1, "Compression changed sub-knee or daytime response");
		}
		std::cout << "PASS: GGX and compression grid, samples=1024 maximumRelativeError=" << maximumRelativeError << '\n';
	}

	void CheckLighting(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool pbr)
	{
		Fixture fixture(device, vr, pbr);
		Fixture referenceFixture(device, vr, pbr, "main", true);
		unsigned reduced = 0, protectedFlat = 0, compared = 0, baselineNonfinite = 0;
		for (unsigned mode : { 0u, 1u, 2u }) {
			for (float night : { 0.0f, 0.000001f, 0.25f, 0.5f, 1.0f }) {
				for (float luminance : { 0.0f, 0.10f, 0.22f, 1.0f }) {
					const auto full = fixture.Run(context, night, luminance, 1, mode);
					const auto independentReference = referenceFixture.Run(context, night, luminance, 1, mode);
					const auto attenuated = fixture.Run(context, night, luminance, 0.125f, mode);
					const auto unlit = fixture.Run(context, night, luminance, 0, mode);
					for (size_t i = 0; i < 1024; ++i) {
						const auto& actual = full[i * 3];
						const auto& reference = full[i * 3 + 1];
						const auto& meta = full[i * 3 + 2];
						const bool protectedPoint = meta[1] > 0 && meta[3] == 1;
						Require(meta[0] == 0, "Highlight suppression changed underlying diffuse lighting");
						if (!protectedPoint) {
							// A separate shader prevents precision propagation from changing both sides of the comparison.
							const auto& baseline = independentReference[i * 3];
							for (size_t c = 0; c < 3; ++c) {
								Require((!std::isfinite(baseline[c]) && !std::isfinite(actual[c])) || actual[c] == baseline[c],
									"Unprotected lighting changed against the independently compiled baseline");
							}
							for (size_t c = 0; c < 4; ++c) {
								Require((!std::isfinite(reference[c]) && !std::isfinite(actual[c])) || actual[c] == reference[c],
									"Daytime, bright night, or directional wet lighting changed");
							}
						}
						if (!protectedPoint && !std::isfinite(reference[0])) {
							++baselineNonfinite;
							continue;
						}
						for (size_t c = 0; c < 3; ++c) {
							if (!std::isfinite(actual[c])) {
								std::cerr << "Nonfinite: vr=" << vr << " pbr=" << pbr << " night=" << night << " luminance=" << luminance << " sample=" << i << '\n';
								throw std::runtime_error("Protected wet highlight must remain finite");
							}
							Require(actual[c] >= 0 && Close(attenuated[i * 3][c], actual[c] * 0.125f), "Local-light attenuation or colour changed");
							Require(unlit[i * 3][c] == 0, "Unlit wetness emitted light");
						}
						Require(Close(actual[1], actual[0] * 0.25f) && Close(actual[2], actual[0] * 0.0625f), "Highlight compression changed fire colour");
						if (protectedPoint && meta[1] == 1) {
							Require(actual[3] >= 0.18f - 1e-6f, "Flat ripple escaped nighttime roughness protection");
							Require(actual[0] <= 0.5f * meta[2] + 1e-5f, "Night wet peak exceeded attenuated and shadowed light bound");
							protectedFlat += (i / 64) % 8 == 0;
							reduced += std::isfinite(reference[0]) && actual[0] < reference[0] * 0.75f;
						}
						++compared;
					}
				}
			}
		}
		Require(reduced > 0 && protectedFlat > 0, "Fixture did not exercise the reported sparkle regression");
		std::cout << "PASS VR=" << vr << " PBR=" << pbr << " samples=" << compared << " reduced=" << reduced << " flatProtected=" << protectedFlat << " preservedBaselineNonfinite=" << baselineNonfinite << '\n';
	}

	void CheckPuddleSlopes(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		Fixture fixture(device, false, false, "CheckPuddleSlopes");
		const auto samples = fixture.Run(context, 0, 0, 0, 0);
		float previous = 1;
		for (size_t i = 0; i < 1024; ++i) {
			const auto& sample = samples[i * 3];
			Require(std::isfinite(sample[0]) && sample[0] >= 0 && sample[0] <= previous,
				"Puddle slope coverage must decrease monotonically");
			if (sample[1] <= 3)
				Require(sample[0] == 1, "Nearly flat surfaces lost puddle eligibility");
			else if (sample[1] >= 8)
				Require(sample[0] == 0, "Standing puddles survived on a slope above 8 degrees");
			else
				Require(sample[0] > 0 && sample[0] < 1, "Puddle slope transition must fade smoothly");
			Require(sample[2] == 0 && sample[3] == 0, "Downward or degenerate normals admitted puddles");
			const auto& geometric = samples[i * 3 + 1];
			Require(Close(geometric[0], sample[0]) && Close(geometric[1], sample[0]),
				"Geometric puddle eligibility changed with distance or resolution");
			Require(geometric[2] == 0 && geometric[3] == 0, "Downward or degenerate geometry admitted puddles");
			Require(Close(samples[i * 3 + 2][0], sample[0]), "Geometric derivative orientation disagrees with the mesh slope");
			previous = sample[0];
		}
		std::cout << "PASS: puddle slope coverage, samples=1024\n";
	}

	void CheckWetnessPixelShaders()
	{
		struct Material
		{
			const char* define;
			bool pbr, skinned;
		};
		const std::array materials{
			Material{ "LANDSCAPE", false, false }, Material{ "MODELSPACENORMALS", false, false }, Material{ "ENVMAP", false, false },
			Material{ "LANDSCAPE", true, false }, Material{ "MODELSPACENORMALS", true, true }, Material{ "VC", true, false }
		};
		for (bool vr : { false, true }) {
			for (bool deferred : { false, true }) {
				for (const auto& material : materials) {
					PackageIncludes includes("package/Shaders", "features/Wetterness/Shaders");
					std::vector<D3D_SHADER_MACRO> defines{
						{ "PSHADER", "1" }, { "WETTERNESS", "1" }, { material.define, "1" }
					};
					if (vr)
						defines.push_back({ "VR", "1" });
					if (deferred)
						defines.push_back({ "DEFERRED", "1" });
					if (material.pbr)
						defines.push_back({ "TRUE_PBR", "1" });
					if (material.skinned)
						defines.push_back({ "SKINNED", "1" });
					defines.push_back({ nullptr, nullptr });
					std::cout << "Compile wetness pixel shader VR=" << vr << " deferred=" << deferred << " PBR=" << material.pbr
							  << " material=" << material.define << " skinned=" << material.skinned << std::endl;
					ComPtr<ID3DBlob> bytecode, errors;
					// Full material permutations use the production compiler flags, including warning reporting.
					const auto compiled = D3DCompileFromFile(L"package/Shaders/Lighting.hlsl", defines.data(), &includes, "main", "ps_5_0",
						D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
						0, bytecode.GetAddressOf(), errors.GetAddressOf());
					if (errors)
						std::cerr << static_cast<const char*>(errors->GetBufferPointer());
					Check(compiled);
					std::cout << "PASS\n";
				}
			}
		}
	}
}

int main()
{
	try {
		CheckNightPolicy();
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		CheckDistribution(device.Get(), context.Get());
		CheckPuddleSlopes(device.Get(), context.Get());
		CheckWetnessPixelShaders();
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				CheckLighting(device.Get(), context.Get(), vr, pbr);
		std::cout << "PASS: climate timing and local wet highlight GPU regression\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
