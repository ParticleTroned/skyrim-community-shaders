#define NOMINMAX
#include "Features/WetternessLightingPolicy.h"
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
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

	struct SurfaceCase
	{
		const char* name;
		float wetness, rain, filmRain, puddles, shore, waterHeight;
		bool preserve = true, drops = false, enabled = true;
		float filmPercent = 70;
	};

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
		ComPtr<ID3D11Texture2D> precipitation;
		ComPtr<ID3D11ShaderResourceView> precipitationView;
		ComPtr<ID3D11SamplerState> precipitationSampler;
		UINT precipitationSlot = 0, samplerSlot = 0;
		std::unique_ptr<ConstantBuffer> feature, inputs;

		Fixture(ID3D11Device* device, bool vr, bool pbr, const char* entry = "main", bool reference = false, const char* material = nullptr,
			const std::filesystem::path& sourceRoot = ".")
		{
			const bool surfaceChannels = std::strcmp(entry, "CheckSurfaceChannels") == 0;
			PackageIncludes includes(sourceRoot / "package/Shaders", sourceRoot / "features/Wetterness/Shaders", sourceRoot / "features/Skylighting/Shaders");
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "WETTERNESS", "1" } };
			if (surfaceChannels)
				defines.push_back({ "SKYLIGHTING", "1" });
			if (reference)
				defines.push_back({ "WETTERNESS_REFERENCE", "1" });
			if (vr)
				defines.push_back({ "VR", "1" });
			if (pbr)
				defines.push_back({ "TRUE_PBR", "1" });
			if (material) {
				defines.push_back({ material, "1" });
				defines.push_back({ "SKINNED", "1" });
			}
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
			if (std::strcmp(entry, "main") == 0 || std::strcmp(entry, "CheckRainFilmSurface") == 0 || surfaceChannels) {
				feature = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
				if (std::strcmp(entry, "main") == 0 || surfaceChannels)
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
			if (surfaceChannels) {
				D3D11_TEXTURE2D_DESC texture{};
				texture.Width = texture.Height = texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
				texture.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				Check(device->CreateTexture2D(&texture, nullptr, precipitation.GetAddressOf()));
				Util::SetResourceName(precipitation.Get(), "WetternessTest::Precipitation");
				Check(device->CreateShaderResourceView(precipitation.Get(), nullptr, precipitationView.GetAddressOf()));
				Util::SetResourceName(precipitationView.Get(), "WetternessTest::Precipitation SRV");
				D3D11_SAMPLER_DESC sampler{};
				sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
				sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
				sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
				sampler.MaxAnisotropy = 1;
				sampler.MaxLOD = D3D11_FLOAT32_MAX;
				Check(device->CreateSamplerState(&sampler, precipitationSampler.GetAddressOf()));
				Util::SetResourceName(precipitationSampler.Get(), "WetternessTest::Precipitation Sampler");
				D3D11_SHADER_INPUT_BIND_DESC binding{};
				Check(reflection->GetResourceBindingDescByName("Wetterness::TexPrecipOcclusion", &binding));
				precipitationSlot = binding.BindPoint;
				Check(reflection->GetResourceBindingDescByName("SampColorSampler", &binding));
				samplerSlot = binding.BindPoint;
			}
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
			return Dispatch(context);
		}

		Samples RunSurface(ID3D11DeviceContext* context, bool preserve, float percent, float reduction = 2.5f,
			float wetness = 1.0f, float rain = 1.0f, float puddles = 0.0f, float shore = 0.0f)
		{
			SetSurface(preserve, percent, reduction, wetness, rain, puddles, shore);
			return Dispatch(context);
		}

		void SetSurface(bool preserve, float percent, float reduction, float wetness, float rain, float puddles, float shore)
		{
			auto set = [&](const char* name, auto value) { feature->SetMember("SharedData::wetternessSettings", name, value); };
			set("EnableWetnessEffects", 1u);
			set("MaxRainWetness", 1.0f);
			set("SkinWetness", 1.0f);
			set("MaxPuddleWetness", 6.0f);
			set("PuddleMaxAngle", 0.75f);
			set("PuddleMinWetness", 0.25f);
			set("PuddleWetness", puddles);
			set("PuddleMaskMode", 0u);
			set("Wetness", wetness);
			set("Raining", rain);
			set("PackedPostRainControl", (preserve ? 1u << 30 : 0u) | (rain > 0 ? 1023u << 20 : 0u));
			set("PackedRainReflectionControl", 1u << 31);
			set("WetnessDistanceFadeRangePacked", std::bit_cast<unsigned>(10000.0f));
			set("RainContactWetnessScale", percent * 0.025f);
			set("WetFilmSpecularFloorScale", 2.0f);
			set("WetHighlightReduction", reduction);
			set("StoneDryingMultiplier", 24.0f);
			set("DirtDryingMultiplier", 24.0f);
			set("GrassDryingMultiplier", 24.0f);
			set("PostRainPuddleWaterStrength", 2.5f);
			set("MaxShoreWetness", shore);
			set("ShoreRange", 2000);
		}

		Samples RunChannels(ID3D11DeviceContext* context, const SurfaceCase& sample, float rainDarkening, float shoreDarkening, bool blocked)
		{
			SetSurface(sample.preserve, sample.filmPercent, 2.5f, sample.wetness, sample.rain, sample.puddles, sample.shore);
			auto set = [&](const char* name, auto value) { feature->SetMember("SharedData::wetternessSettings", name, value); };
			set("EnableWetnessEffects", unsigned(sample.enabled));
			set("WetDarkeningStrength", rainDarkening);
			set("ShorePersistentDarkeningStrength", shoreDarkening);
			set("StoneDryingMultiplier", 3.0f);
			set("DirtDryingMultiplier", 24.0f);
			set("GrassDryingMultiplier", 12.0f);
			set("PackedPostRainControl", (sample.preserve ? 1u << 30 : 0u) | (unsigned(sample.filmRain * 1023) << 20));
			set("PackedRainReflectionControl", (1u << 30) | (1u << 31));
			std::array<float, 16> projection{};
			projection[11] = 0.5f;
			projection[15] = 1.0f;
			set("OcclusionViewProj", projection);
			set("EnableRaindropFx", unsigned(sample.drops));
			set("RaindropFxRange", 1000.0f);
			set("EnableRipples", 1u);
			set("RaindropChance", 1.0f);
			set("RaindropGridSizeRcp", 0.05f);
			set("RaindropIntervalRcp", 1.0f);
			set("RippleLifetimeRcp", 1.0f);
			set("RippleRadius", 0.6f);
			set("RippleBreadth", 0.4f);
			set("RippleStrength", 2.0f);
			set("Time", 5.25f);
			inputs->SetVariable("surfaceWaterHeight", sample.waterHeight);
			const Pixel depth{ blocked ? 0.25f : 0.75f, 0, 0, 0 };
			context->UpdateSubresource(precipitation.Get(), 0, nullptr, depth.data(), sizeof(Pixel), 0);
			return Dispatch(context, 8);
		}

		Samples Dispatch(ID3D11DeviceContext* context, UINT groups = 16)
		{
			context->ClearState();
			if (feature) {
				feature->Bind(context);
				if (inputs)
					inputs->Bind(context);
			}
			if (precipitationView) {
				ID3D11ShaderResourceView* view = precipitationView.Get();
				ID3D11SamplerState* sampler = precipitationSampler.Get();
				context->CSSetShaderResources(precipitationSlot, 1, &view);
				context->CSSetSamplers(samplerSlot, 1, &sampler);
			}
			ID3D11UnorderedAccessView* target = uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(groups, 1, 1);
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

	void CheckSurfaceChannels(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool pbr,
		const char* material, const std::filesystem::path& baselineRoot)
	{
		Fixture fixture(device, vr, pbr, "CheckSurfaceChannels", false, material);
		std::unique_ptr<Fixture> baseline;
		if (!baselineRoot.empty())
			baseline = std::make_unique<Fixture>(device, vr, pbr, "CheckSurfaceChannels", false, material, baselineRoot);
		const std::array cases{
			SurfaceCase{ "rain", 1, 1, 1, 0, 0, 0, false },
			SurfaceCase{ "weak rain", 0.6f, 0.03f, 0, 0, 0, 0, true, true },
			SurfaceCase{ "drying", 0.6f, 0, 0, 0, 0, 0 },
			SurfaceCase{ "drying puddles", 0.6f, 0, 0, 0.45f, 0, 0 },
			SurfaceCase{ "wet shore", 1, 1, 1, 0, 1, 98 },
			SurfaceCase{ "dry shore", 0, 0, 0, 0, 1, 98, true, false, false },
			SurfaceCase{ "underwater", 1, 1, 1, 1, 1, 101 },
			SurfaceCase{ "dry", 0, 0, 0, 0, 0, 0 },
			SurfaceCase{ "zero film", 1, 1, 1, 0, 0, 0, false, false, true, 0 }
		};
		const std::array<std::array<float, 3>, 4> colors{ { { 0.6f, 0.6f, 0.6f }, { 0.45f, 0.15f, 0.07f }, { 0.1f, 0.45f, 0.08f }, { 0.72f, 0.62f, 0.42f } } };
		bool rainDarkened = false, shoreDarkened = false, openRainBeads = false, puddleExposureChanged = false;
		float maximumError = 0;
		for (const auto& sample : cases) {
			Samples exposed{};
			for (bool blocked : { false, true }) {
				Samples neutral{};
				for (unsigned channel = 0; channel < 3; ++channel) {
					const float rainDarkening = channel == 1 ? 1.0f : 0.0f;
					const float shoreDarkening = channel == 2 ? 1.0f : 0.0f;
					const auto actual = fixture.RunChannels(context, sample, rainDarkening, shoreDarkening, blocked);
					if (channel == 0)
						neutral = actual;
					Samples reference{};
					if (baseline)
						reference = baseline->RunChannels(context, sample, rainDarkening, shoreDarkening, blocked);
					for (size_t i = 0; i < actual.size(); ++i) {
						for (size_t c = 0; c < 4; ++c) {
							Require(std::isfinite(actual[i][c]), "Surface channel produced nonfinite output");
							if (baseline) {
								maximumError = std::max(maximumError, std::abs(actual[i][c] - reference[i][c]));
								if (!std::isfinite(reference[i][c]) || !Close(actual[i][c], reference[i][c])) {
									std::cerr << "Surface baseline mismatch: case=" << sample.name << " VR=" << vr << " PBR=" << pbr
											  << " material=" << (material ? material : "surface") << " blocked=" << blocked << " channel=" << channel
											  << " output=" << i << ':' << c << " actual=" << actual[i][c] << " baseline=" << reference[i][c] << '\n';
									throw std::runtime_error("Surface changed against the independently compiled source baseline");
								}
							}
							if (i % 6 == 0 && c < 3) {
								const float original = colors[(i / 6) % colors.size()][c];
								if (channel == 0)
									Require(Close(actual[i][c], original), "Zero darkening controls changed base color");
								else if (actual[i][c] < original - 1e-5f) {
									rainDarkened |= channel == 1;
									shoreDarkened |= channel == 2;
								}
							} else {
								Require(Close(actual[i][c], neutral[i][c]), "Darkening controls changed another wetness channel");
							}
							if (i < actual.size() / 2)
								Require(actual[i][c] == actual[i + actual.size() / 2][c], "Wetness surface differs between matching eye inputs");
						}
					}
				}
				if (!blocked)
					exposed = neutral;
				for (size_t i = 0; i < 512; ++i) {
					if (sample.drops) {
						if (blocked)
							Require(neutral[i * 6 + 3][3] == 0, "Occluded weak-rain beads reached the surface");
						else
							openRainBeads |= neutral[i * 6 + 3][3] > 0;
					}
					if (blocked && sample.puddles > 0)
						puddleExposureChanged |= neutral[i * 6 + 1][2] != exposed[i * 6 + 1][2];
				}
			}
		}
		Require(rainDarkened && (material || shoreDarkened), "Darkening comparison did not exercise active controls");
		Require(openRainBeads, "Weak-rain fixture did not produce exposed beads");
		if (!material)
			Require(puddleExposureChanged, "Puddle fixture did not exercise precipitation masking");
		std::cout << "PASS: wetness surface channels VR=" << vr << " PBR=" << pbr << " material=" << (material ? material : "surface")
				  << " sourceBaseline=" << bool(baseline) << " maximumError=" << maximumError << '\n';
	}

	void CheckSurfaceChannelMatrix(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& baselineRoot)
	{
		for (bool vr : { false, true }) {
			for (bool pbr : { false, true })
				CheckSurfaceChannels(device, context, vr, pbr, nullptr, baselineRoot);
			for (const char* material : { "SKIN", "HAIR" })
				CheckSurfaceChannels(device, context, vr, false, material, baselineRoot);
		}
	}

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

	void CheckRainFilmSurface(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool pbr)
	{
		Fixture fixture(device, vr, pbr, "CheckRainFilmSurface");
		Samples previous{};
		for (float percent : { 0.0f, 10.0f, 25.0f, 50.0f, 70.0f, 100.0f }) {
			const auto lowReduction = fixture.RunSurface(context, true, percent, 0.25f);
			const auto highReduction = fixture.RunSurface(context, true, percent, 10.0f);
			for (size_t i = 0; i < 1024; ++i) {
				const auto& actual = highReduction[i * 3];
				const auto& facing = highReduction[(i / 32 * 32) * 3];
				for (size_t c = 0; c < 4; ++c) {
					Require(std::isfinite(actual[c]) && Close(actual[c], facing[c]), "Rain-film surface changed with viewing angle");
					Require(Close(actual[c], lowReduction[i * 3][c]), "Highlight reduction changed coating coverage or roughness");
				}
				Require(actual[1] >= previous[i * 3][1] && actual[2] >= previous[i * 3][2], "Rain-film slider must increase coverage and darkening monotonically");
				const bool sheltered = (i / 32) % 4 >= 2;
				if (percent == 0 || sheltered)
					Require(actual[1] == 0 && actual[2] == 0, "Disabled or sheltered rain film remained visible");
				else
					Require(actual[1] > 0 && actual[0] >= 0.18f && actual[0] <= 0.4f, "Exposed ground lost its bounded wet coating");
			}
			previous = highReduction;
		}
		const auto legacy = fixture.RunSurface(context, false, 70.0f);
		Require(legacy[0][0] > legacy[31 * 3][0], "Legacy comparison no longer exercises grazing roughness");
		const auto puddlesOff = fixture.RunSurface(context, true, 0, 2.5f, 1, 1, 1);
		const auto puddlesOn = fixture.RunSurface(context, true, 100, 2.5f, 1, 1, 1);
		for (size_t i = 0; i < 32 * 3; ++i)
			for (size_t c = 0; c < 4; ++c)
				Require(Close(puddlesOff[i][c], puddlesOn[i][c]), "Rain-film slider changed deep puddles");
		Require(puddlesOn[32 * 3][1] > 0, "Puddle slope restriction removed rain film on sloping ground");
		const auto shoreOnly = fixture.RunSurface(context, true, 0, 10, 1, 1, 0, 1);
		const auto traceRain = fixture.RunSurface(context, true, 1e-5f, 10, 1, 1, 0, 1);
		for (size_t i = 0; i < shoreOnly.size(); ++i)
			for (size_t c = 0; c < 4; ++c)
				Require(Close(shoreOnly[i][c], traceRain[i][c]), "Trace rain caused a discontinuous change to shoreline wetness");
		float previousDrying = 1;
		for (float wetness : { 1.0f, 0.7f, 0.3f, 0.0f }) {
			const auto drying = fixture.RunSurface(context, true, 100, 2.5f, wetness, 0);
			Require(drying[0][1] <= previousDrying, "Rain film intensified while drying");
			previousDrying = drying[0][1];
		}
		Require(previousDrying == 0, "Rain film persisted after full drying");
		std::cout << "PASS: rain-film surface, view invariance, slider, shelter, puddles and drying VR=" << vr << " PBR=" << pbr << '\n';
	}

	void CheckRainFilmHighlights(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool pbr)
	{
		Fixture fixture(device, vr, pbr, "CheckRainFilmHighlights");
		const auto samples = fixture.Run(context, 0, 0, 0, 0);
		unsigned reduced = 0;
		float previousReflectance = 0;
		for (size_t i = 0; i < 1024; ++i) {
			const auto& actual = samples[i * 3];
			const auto& reference = samples[i * 3 + 1];
			const auto& indirect = samples[i * 3 + 2];
			for (size_t c = 0; c < 3; ++c)
				Require(std::isfinite(actual[c]) && actual[c] >= 0 && actual[c] <= reference[c] + 1e-5f,
					"Rain-film highlight compression increased or invalidated a highlight");
			Require(actual[3] == 0 && indirect[0] < 1e-5f, "Peak compression changed diffuse lighting or light attenuation");
			Require(Close(actual[1], actual[0] * 0.25f) && Close(actual[2], actual[0] * 0.0625f), "Rain-film highlight hue changed");
			Require(std::isfinite(indirect[1]) && indirect[1] >= previousReflectance && indirect[1] <= 1.0f &&
						Close(indirect[2], indirect[1] * 0.25f) && Close(indirect[3], indirect[1] * 0.0625f),
				"Wet reflection compression lost its bound, ordering or hue");
			previousReflectance = indirect[1];
			reduced += actual[0] < reference[0] * 0.9f;
		}
		Require(reduced > 0, "Rain-film fixture did not exercise highlight peaks");
		std::cout << "PASS: rain-film direct and indirect highlight compression VR=" << vr << " PBR=" << pbr << '\n';
	}

	void CheckRainFilmReflectivity(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool pbr)
	{
		Fixture fixture(device, vr, pbr, "CheckRainFilmReflectivity");
		const auto samples = fixture.Run(context, 0, 0, 0, 0);
		for (size_t i = 0; i < 1024; ++i) {
			const auto& reflected = samples[i * 3];
			for (size_t c = 0; c < 3; ++c) {
				Require(std::isfinite(reflected[c]) && Close(reflected[c], reflected[3]),
					"Indirect film reflectivity disagrees with the direct wetness strength");
				Require(Close(samples[i * 3 + 1][c] + reflected[c], 1) && Close(samples[i * 3 + 2][c] + reflected[c], 1),
					"Wet reflection weights lost energy balance");
			}
			if (i % 32 == 0)
				Require(reflected[0] == 0, "Dry coating retained a reflection");
		}
		std::cout << "PASS: rain-film direct/indirect reflectivity agreement VR=" << vr << " PBR=" << pbr << '\n';
	}

	void CheckCharacterScope(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		for (const char* material : { "SKIN", "HAIR" }) {
			Fixture fixture(device, vr, false, "CheckRainFilmSurface", false, material);
			const auto legacy = fixture.RunSurface(context, false, 70);
			const auto corrected = fixture.RunSurface(context, true, 70);
			Require(legacy[0][1] > 0, "Character scope fixture did not produce wet skin or hair");
			for (size_t i = 0; i < legacy.size(); ++i)
				for (size_t c = 0; c < 4; ++c)
					Require(legacy[i][c] == corrected[i][c], "Rain-film toggle changed skin or hair shading");
		}
		std::cout << "PASS: rain-film toggle preserves skin and hair VR=" << vr << '\n';
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
			Material{ "LANDSCAPE", true, false }, Material{ "MODELSPACENORMALS", true, true }, Material{ "VC", true, false },
			Material{ "SKIN", false, true }, Material{ "HAIR", false, true }
		};
		for (bool vr : { false, true }) {
			for (bool deferred : { false, true }) {
				for (bool cubemaps : { false, true }) {
					for (const auto& material : materials) {
						PackageIncludes includes("package/Shaders", "features/Wetterness/Shaders", "features/Dynamic Cubemaps/Shaders");
						std::vector<D3D_SHADER_MACRO> defines{
							{ "PSHADER", "1" }, { "WETTERNESS", "1" }, { material.define, "1" }
						};
						if (vr)
							defines.push_back({ "VR", "1" });
						if (deferred)
							defines.push_back({ "DEFERRED", "1" });
						if (cubemaps)
							defines.push_back({ "DYNAMIC_CUBEMAPS", "1" });
						if (material.pbr)
							defines.push_back({ "TRUE_PBR", "1" });
						if (material.skinned)
							defines.push_back({ "SKINNED", "1" });
						defines.push_back({ nullptr, nullptr });
						std::cout << "Compile wetness pixel shader VR=" << vr << " deferred=" << deferred << " PBR=" << material.pbr
								  << " material=" << material.define << " skinned=" << material.skinned << " cubemaps=" << cubemaps << std::endl;
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
}

int main(int argc, char** argv)
{
	try {
		std::filesystem::path baselineRoot;
		bool surfaceChannelsOnly = false;
		for (int i = 1; i < argc; ++i) {
			if (std::strcmp(argv[i], "--baseline-root") == 0 && i + 1 < argc && baselineRoot.empty()) {
				baselineRoot = argv[++i];
				Require(!baselineRoot.empty(), "Baseline source root must not be empty");
			} else if (std::strcmp(argv[i], "--surface-channels-only") == 0 && !surfaceChannelsOnly)
				surfaceChannelsOnly = true;
			else
				throw std::runtime_error("Usage: wetterness_highlights_test [--baseline-root <source-root>] [--surface-channels-only]");
		}
		if (!baselineRoot.empty()) {
			Require(std::filesystem::is_regular_file(baselineRoot / "package/Shaders/Lighting.hlsl") &&
						std::filesystem::is_regular_file(baselineRoot / "features/Wetterness/Shaders/Wetterness/Wetterness.hlsli") &&
						std::filesystem::is_regular_file(baselineRoot / "features/Skylighting/Shaders/Skylighting/Skylighting.hlsli"),
				"Baseline root must contain its complete package/Shaders and feature include trees");
		}
		CheckNightPolicy();
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		CheckSurfaceChannelMatrix(device.Get(), context.Get(), baselineRoot);
		if (surfaceChannelsOnly)
			return 0;
		CheckDistribution(device.Get(), context.Get());
		CheckPuddleSlopes(device.Get(), context.Get());
		for (bool vr : { false, true }) {
			CheckCharacterScope(device.Get(), context.Get(), vr);
			for (bool pbr : { false, true }) {
				CheckRainFilmSurface(device.Get(), context.Get(), vr, pbr);
				CheckRainFilmHighlights(device.Get(), context.Get(), vr, pbr);
				CheckRainFilmReflectivity(device.Get(), context.Get(), vr, pbr);
			}
		}
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
