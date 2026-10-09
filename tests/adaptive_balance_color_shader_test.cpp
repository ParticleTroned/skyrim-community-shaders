#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "Utils/ShaderInclude.h"
#include "adaptive_balance_test_settings.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Appearance = AdaptiveBalanceTest::Appearance;
	using Pixel = std::array<float, 4>;
	using Pixels = std::array<Pixel, 8>;
	constexpr Pixels samples{ Pixel{ 0, 0, 0, 1 }, Pixel{ 0.01f, 0.01f, 0.01f, 1 },
		Pixel{ 0.18f, 0.18f, 0.18f, 1 }, Pixel{ 0.6f, 0.6f, 0.6f, 1 },
		Pixel{ 0.4f, 0.2f, 0.1f, 1 }, Pixel{ 2, 4, 8, 1 },
		Pixel{ -0.1f, 0.2f, 0.3f, 1 }, Pixel{ 1e-8f, 1e-8f, 1e-8f, 1 } };

	constexpr Pixels fireColors = [] {
		auto colors = samples;
		colors[5] = { 100, 500, 1000, 1 };
		return colors;
	}();
	constexpr Pixels pointLightColors = fireColors;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	bool Close(float a, float b) { return std::abs(a - b) <= 2e-5f * std::max(1.0f, std::abs(b)); }

	ComPtr<ID3DBlob> Compile(const wchar_t* path, std::vector<D3D_SHADER_MACRO> defines, const char* target)
	{
		std::cerr << "Compiling " << std::filesystem::path(path).string() << "\n";
		defines.push_back({ nullptr, nullptr });
		Util::CustomInclude includes{ "package/Shaders" };
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(path, defines.data(), &includes, "main", target,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, code.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	enum class ShaderMode
	{
		Scene,
		PointLights,
		Appearance,
		Fire
	};

	struct Fixture
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11Texture2D> output, staging;
		ComPtr<ID3D11UnorderedAccessView> uav;
		std::unique_ptr<ConstantBuffer> feature, inputs, permutation;

		Fixture(ID3D11Device* device, bool vr, ShaderMode mode = ShaderMode::Scene, const std::vector<const char*>& extraDefines = {})
		{
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" } };
			if (vr)
				defines.push_back({ "VR", "1" });
			if (mode == ShaderMode::PointLights)
				defines.push_back({ "POINT_LIGHT_SATURATION_TEST", "1" });
			if (mode == ShaderMode::Appearance)
				defines.push_back({ "APPEARANCE_TEST", "1" });
			if (mode == ShaderMode::Fire)
				defines.push_back({ "FIRE_TEST", "1" });
			for (const auto* define : extraDefines)
				defines.push_back({ define, "1" });
			auto code = Compile(L"tests/adaptive_balance_color.hlsl", defines, "cs_5_0");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "AdaptiveColorTest::CS");
			feature = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
			feature->SetMember("SharedData::adaptiveBalanceSettings", "appearance", AdaptiveBalanceTest::NeutralAppearance());
			inputs = std::make_unique<ConstantBuffer>(device, reflection.Get(), "Samples");
			inputs->SetVariable("colors", mode == ShaderMode::Scene ? samples : fireColors);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightSaturation", 1.0f);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightCurve", 1.0f);
			for (const auto* name : { "fireIntensity", "fireSaturation", "fireCurve" })
				feature->SetMember("SharedData::adaptiveBalanceSettings", name, 1.0f);
			if (mode == ShaderMode::Fire || mode == ShaderMode::Appearance)
				permutation = std::make_unique<ConstantBuffer>(device, reflection.Get(), "Permutation::PerShader");
			if (mode == ShaderMode::PointLights || mode == ShaderMode::Appearance) {
				feature->SetMember("SharedData::linearLightingSettings", "lightGamma", 2.2f);
				for (auto [name, value] : { std::pair{ "pointLightMult", 1.3f }, std::pair{ "linearPointLightMult", 0.7f },
						 std::pair{ "spotlightMult", 0.8f }, std::pair{ "linearSpotlightMult", 0.9f },
						 std::pair{ "omnidirectionalBulbMult", 0.6f }, std::pair{ "linearOmnidirectionalBulbMult", 1.2f },
						 std::pair{ "directionalLightMult", 0.75f } })
					feature->SetMember("SharedData::adaptiveBalanceSettings", name, value);
			}
			auto* balance = feature->reflection->GetVariableByName("SharedData::adaptiveBalanceSettings");
			D3D11_SHADER_VARIABLE_DESC desc{};
			Check(balance->GetDesc(&desc));
			Require(desc.Size == 320, "Adaptive Balance buffer size changed");
			for (auto [name, offset] : { std::pair{ "contrast", 40u }, std::pair{ "saturation", 44u },
					 std::pair{ "cloudBrightness", 48u }, std::pair{ "cloudSaturation", 52u },
					 std::pair{ "fogIntensity", 56u }, std::pair{ "sunGlareIntensity", 60u },
					 std::pair{ "useAmbientEffectLighting", 64u }, std::pair{ "skyStaticTransparency", 68u },
					 std::pair{ "effectBrightness", 72u }, std::pair{ "skyStaticBrightness", 76u },
					 std::pair{ "pointLightSaturation", 80u }, std::pair{ "fireIntensity", 84u },
					 std::pair{ "fireSaturation", 88u }, std::pair{ "fireCurve", 92u }, std::pair{ "pointLightCurve", 96u }, std::pair{ "appearance", 112u } }) {
				D3D11_SHADER_TYPE_DESC member{};
				Check(balance->GetType()->GetMemberTypeByName(name)->GetDesc(&member));
				Require(member.Offset == offset, "Adaptive Balance buffer layout differs from C++");
			}
			auto* appearance = balance->GetType()->GetMemberTypeByName("appearance");
			for (auto [name, offset] : { std::pair{ "directionalSaturation", 0u }, std::pair{ "directionalCurve", 4u },
					 std::pair{ "ambientSaturation", 8u }, std::pair{ "fogBrightness", 12u }, std::pair{ "directionalTint", 16u },
					 std::pair{ "cloudOpacity", 28u }, std::pair{ "fogTint", 32u }, std::pair{ "starsIntensity", 44u },
					 std::pair{ "cloudTint", 48u }, std::pair{ "starsCurve", 60u }, std::pair{ "skyTopTint", 64u },
					 std::pair{ "skyTopIntensity", 76u }, std::pair{ "skyMiddleTint", 80u }, std::pair{ "skyMiddleIntensity", 92u },
					 std::pair{ "skyHorizonTint", 96u }, std::pair{ "skyHorizonIntensity", 108u }, std::pair{ "skyTopCurve", 112u },
					 std::pair{ "skyMiddleCurve", 116u }, std::pair{ "skyHorizonCurve", 120u }, std::pair{ "skyStaticCurve", 124u },
					 std::pair{ "skyStaticTint", 128u }, std::pair{ "lightSpriteIntensity", 140u }, std::pair{ "lightSpriteCurve", 144u },
					 std::pair{ "particleIntensity", 148u }, std::pair{ "particleDirectionalInfluence", 152u }, std::pair{ "particleAmbientInfluence", 156u },
					 std::pair{ "particlePointInfluence", 160u }, std::pair{ "cloudShadowStrength", 164u }, std::pair{ "godrayIntensity", 168u },
					 std::pair{ "godrayOpacity", 172u }, std::pair{ "godrayTint", 176u }, std::pair{ "godrayTintAmount", 188u },
					 std::pair{ "godraySaturation", 192u }, std::pair{ "padding", 196u } }) {
				D3D11_SHADER_TYPE_DESC member{};
				Check(appearance->GetMemberTypeByName(name)->GetDesc(&member));
				Require(member.Offset == offset, "Appearance buffer layout differs from C++");
			}
			auto* linear = feature->reflection->GetVariableByName("SharedData::linearLightingSettings");
			Check(linear->GetDesc(&desc));
			Require(desc.Size == 112, "Linear Lighting buffer size changed");
			D3D11_SHADER_TYPE_DESC gamma{};
			Check(linear->GetType()->GetMemberTypeByName("cloudGamma")->GetDesc(&gamma));
			Require(gamma.Offset == 104, "Cloud gamma buffer layout differs from C++");
			D3D11_TEXTURE2D_DESC texture{};
			texture.Width = 8;
			texture.Height = texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
			texture.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			texture.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture2D(&texture, nullptr, output.GetAddressOf()));
			Util::SetResourceName(output.Get(), "AdaptiveColorTest::Output");
			Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
			Util::SetResourceName(uav.Get(), "AdaptiveColorTest::Output UAV");
			texture.BindFlags = 0;
			texture.Usage = D3D11_USAGE_STAGING;
			texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&texture, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "AdaptiveColorTest::Readback");
		}

		Pixels Draw(ID3D11DeviceContext* context, bool linear, float contrast, float saturation)
		{
			feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint32_t(linear));
			feature->SetMember("SharedData::adaptiveBalanceSettings", "contrast", contrast);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "saturation", saturation);
			return Dispatch(context);
		}

		Pixels DrawLights(ID3D11DeviceContext* context, bool linearLighting, bool linearLight, uint32_t kind,
			bool preserveHDRIntensity, float saturation, bool directional = false, float curve = 1.0f, const Pixels& colors = pointLightColors)
		{
			feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint32_t(linearLighting));
			feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightSaturation", saturation);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightCurve", curve);
			inputs->SetVariable("colors", colors);
			inputs->SetVariable("lightParameters", std::array<uint32_t, 4>{ uint32_t(linearLight), kind, uint32_t(preserveHDRIntensity), uint32_t(directional) });
			return Dispatch(context);
		}

		Pixels DrawAppearance(ID3D11DeviceContext* context, bool linearLighting, uint32_t scene, uint32_t route,
			const Appearance& appearance = AdaptiveBalanceTest::NeutralAppearance(), Pixel controls = { 1, 1, 1, 1 },
			Pixel tint = { 1, 1, 1, 1 }, Pixel material = { 1, 1, 1, 1 }, bool isLinear = false, const Pixels& colors = pointLightColors)
		{
			feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint32_t(linearLighting));
			feature->SetMember("SharedData::adaptiveBalanceSettings", "ambientMult", 1.7f);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "appearance", appearance);
			inputs->SetVariable("colors", colors);
			inputs->SetVariable("appearanceParameters", std::array<uint32_t, 4>{ route, uint32_t(isLinear), 0, 0 });
			inputs->SetVariable("appearanceControls", controls);
			inputs->SetVariable("appearanceTint", tint);
			inputs->SetVariable("appearanceMaterial", material);
			permutation->SetVariable("Permutation::ExtraShaderDescriptor", scene);
			return Dispatch(context);
		}

		Pixels DrawFire(ID3D11DeviceContext* context, bool linearLighting, uint32_t paletteFlags, uint32_t sceneFlags,
			float intensity, float saturation, float curve, const Pixels& colors = fireColors)
		{
			feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint32_t(linearLighting));
			feature->SetMember("SharedData::adaptiveBalanceSettings", "fireIntensity", intensity);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "fireSaturation", saturation);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "fireCurve", curve);
			inputs->SetVariable("colors", colors);
			permutation->SetVariable("Permutation::PixelShaderDescriptor", paletteFlags);
			permutation->SetVariable("Permutation::ExtraShaderDescriptor", sceneFlags);
			return Dispatch(context);
		}

		Pixels Dispatch(ID3D11DeviceContext* context)
		{
			context->ClearState();
			feature->Bind(context);
			inputs->Bind(context);
			if (permutation)
				permutation->Bind(context);
			ID3D11UnorderedAccessView* target = uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			Pixels result;
			std::memcpy(result.data(), mapped.pData, sizeof(result));
			context->Unmap(staging.Get(), 0);
			return result;
		}
	};

	void Verify(const Pixels& result, float contrast, float saturation)
	{
		for (size_t i = 0; i < result.size(); ++i) {
			Require(result[i][3] == 1, "Neutral grading changed the original input");
			for (size_t c = 0; c < 3; ++c) {
				Require(std::isfinite(result[i][c]), "Non-finite graded color");
				if (contrast == 1 && saturation == 1)
					Require(Close(result[i][c], samples[i][c]), "Neutral output changed");
				else
					Require(samples[i][c] < 0 ? result[i][c] <= 0 : result[i][c] >= 0, "Grading changed an authored channel sign");
				if (saturation == 0)
					Require(Close(std::abs(result[i][c]), std::abs(result[i][0])), "Zero saturation is not monochrome after display encoding");
			}
		}
		Require(result[0][0] == 0, "Contrast lifted black");
		Require(Close(result[2][0], 0.18f), "Contrast moved middle gray");
		Require(contrast >= 1 ? result[1][0] <= 0.010001f : result[1][0] > 0.01f, "Incorrect shadow contrast");
		Require(contrast <= 1 ? result[3][0] <= 0.60001f : result[3][0] > 0.6f, "Incorrect highlight contrast");
		if (saturation == 1) {
			Require(Close(result[4][0] / result[4][1], 2), "Contrast shifted hue");
			Require(result[5][2] > 1, "HDR highlights clipped to SDR");
		}
	}

	float Chroma(const Pixel& color)
	{
		const auto [low, high] = std::minmax_element(color.begin(), color.begin() + 3);
		return (*high - *low) / *high;
	}

	void VerifyPointLights(const Pixels& neutral, const Pixels& gray, const Pixels& saturated)
	{
		for (size_t i = 0; i < samples.size(); ++i) {
			Require(neutral[i][3] == 1, "Neutral point-light controls changed the original lighting bits");
			for (size_t c = 0; c < 3; ++c) {
				Require(std::isfinite(neutral[i][c]) && std::isfinite(gray[i][c]) && std::isfinite(saturated[i][c]), "Non-finite point-light color");
				Require(gray[i][c] >= 0 && saturated[i][c] >= 0, "Saturation created negative light energy");
				Require(Close(gray[i][c], gray[i][0]), "Zero point-light saturation is not monochrome");
				if (i < 4 || i == 7) {
					Require(Close(gray[i][c], neutral[i][c]), "Desaturation changed a neutral point light");
					Require(Close(saturated[i][c], neutral[i][c]), "Saturation changed a neutral point light");
				}
			}
		}
		Require(gray[0][0] == 0 && saturated[0][0] == 0, "Point-light saturation lifted black");
		Require(Chroma(saturated[4]) > Chroma(neutral[4]), "Point-light saturation did not increase chroma");
		Require(gray[5][0] > 1 && saturated[5][2] > 1, "Point-light saturation clipped HDR intensity");
	}

	Pixels CurveInputs(float curve)
	{
		auto result = pointLightColors;
		for (auto& pixel : result)
			for (size_t c = 0; c < 3; ++c)
				pixel[c] = std::pow(std::max(pixel[c], 0.0f), curve);
		return result;
	}

	unsigned VerifyPointLightCurves(Fixture& fixture, ID3D11DeviceContext* context, bool vr, bool linearLighting,
		bool linearLight, uint32_t kind, bool preserveHDRIntensity)
	{
		unsigned checkedSamples = 0;
		for (float curve : { 0.1f, 0.5f, 2.0f, 4.0f }) {
			const auto authored = CurveInputs(curve);
			for (float saturation : { 0.0f, 1.0f, 2.0f }) {
				const auto curved = fixture.DrawLights(context, linearLighting, linearLight, kind, preserveHDRIntensity, saturation, false, curve);
				// Compare with authored RGB sent through the unchanged lighting path to verify operation order.
				const auto expected = fixture.DrawLights(context, linearLighting, linearLight, kind, preserveHDRIntensity, saturation, false, 1, authored);
				for (size_t i = 0; i < curved.size(); ++i) {
					for (size_t c = 0; c < 3; ++c) {
						if (!std::isfinite(curved[i][c]) || !Close(curved[i][c], std::min(expected[i][c], 65504.0f))) {
							std::cerr << "Point curve VR=" << vr << " LL=" << linearLighting << " linear=" << linearLight
									  << " kind=" << kind << " preserveHDR=" << preserveHDRIntensity << " curve=" << curve
									  << " saturation=" << saturation << " sample=" << i << " channel=" << c
									  << " expected=" << expected[i][c] << " actual=" << curved[i][c] << '\n';
							throw std::runtime_error("Point-light curve disagrees with independently curved authored RGB");
						}
						Require(curved[i][c] >= 0 && curved[i][c] <= 65504, "Point-light curve exceeded scene storage bounds");
						if (saturation == 0)
							Require(Close(curved[i][c], curved[i][0]), "Curved zero-saturation light is not monochrome");
						if (i == 0)
							Require(curved[i][c] == 0, "Point-light curve lifted black");
					}
				}
				if (saturation == 1)
					Require(curved[6][0] == 0, "Nonneutral point-light curve did not clamp a negative authored channel");
				Require(curved[5][2] > 1, "Point-light curve clipped HDR to SDR");
				checkedSamples += 2 * unsigned(pointLightColors.size());
			}
		}
		return checkedSamples;
	}

	unsigned VerifyLightModes(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		Fixture fixture(device, vr, ShaderMode::PointLights);
		Pixels extreme;
		extreme.fill({ 65504, 32752, 0, 1 });
		for (bool linear : { false, true }) {
			const auto result = fixture.DrawLights(context, linear, false, 0, false, 2, false, 4, extreme);
			std::cerr << "Extreme point LL=" << linear << " RGB=" << result[0][0] << ',' << result[0][1] << ',' << result[0][2] << '\n';
			Require(result[0][0] > 1, "Extreme point-light curve extinguished HDR lighting");
			for (const auto& pixel : result)
				for (size_t c = 0; c < 3; ++c)
					Require(std::isfinite(pixel[c]) && pixel[c] >= 0 && pixel[c] <= 65504, "Extreme point-light curve exceeded HDR bounds");
		}
		unsigned checkedSamples = 16;
		for (bool linearLighting : { false, true }) {
			for (bool linearLight : { false, true }) {
				const auto directional = fixture.DrawLights(context, linearLighting, linearLight, 0, false, 1, true);
				checkedSamples += unsigned(samples.size());
				for (float curve : { 0.1f, 0.5f, 1.0f, 2.0f, 4.0f }) {
					for (float saturation : { 0.0f, 2.0f }) {
						const auto adjusted = fixture.DrawLights(context, linearLighting, linearLight, 0, false, saturation, true, curve);
						Require(adjusted == directional, "Point-light controls changed directional lighting");
						checkedSamples += unsigned(samples.size());
					}
				}
				for (uint32_t kind : { 0u, 1u, 2u }) {
					for (bool preserveHDRIntensity : { false, true }) {
						const auto neutral = fixture.DrawLights(context, linearLighting, linearLight, kind, preserveHDRIntensity, 1);
						const auto gray = fixture.DrawLights(context, linearLighting, linearLight, kind, preserveHDRIntensity, 0);
						const auto saturated = fixture.DrawLights(context, linearLighting, linearLight, kind, preserveHDRIntensity, 2);
						VerifyPointLights(neutral, gray, saturated);
						checkedSamples += 3 * unsigned(samples.size());
						checkedSamples += VerifyPointLightCurves(fixture, context, vr, linearLighting, linearLight, kind, preserveHDRIntensity);
					}
				}
			}
		}
		return checkedSamples;
	}

	void CompareAppearance(const Pixels& actual, const Pixels& expected, const char* message)
	{
		for (size_t i = 0; i < actual.size(); ++i) {
			for (size_t c = 0; c < 3; ++c) {
				if (!std::isfinite(actual[i][c]) || !Close(actual[i][c], expected[i][c])) {
					std::cerr << message << " sample=" << i << " channel=" << c << " expected=" << expected[i][c]
							  << " actual=" << actual[i][c] << '\n';
					throw std::runtime_error(message);
				}
			}
		}
	}

	void RequireAppearanceIdentity(const Pixels& result)
	{
		for (const auto& pixel : result)
			Require(pixel[3] == 1, "Neutral or out-of-world appearance controls changed authored bits");
	}

	unsigned VerifyGenericAppearance(Fixture& fixture, ID3D11DeviceContext* context, bool linear, uint32_t scene)
	{
		const std::array controls{ Pixel{ 1, 1, 1, 1 }, Pixel{ 0, 4, 2, 1 }, Pixel{ 5, 0.1f, 1, 1 },
			Pixel{ 1, 0.5f, 0, 1 }, Pixel{ 1, 4, 2, 1 }, Pixel{ 0.5f, 1, 2, 1 }, Pixel{ 2, 2, 1, 1 }, Pixel{ 1, 1, 0, 1 } };
		unsigned checked = 0;
		for (const auto control : controls) {
			for (const auto tint : { Pixel{ 1, 1, 1, 1 }, Pixel{ 0, 1, 2, 1 } }) {
				const auto result = fixture.DrawAppearance(context, linear, scene, 0, AdaptiveBalanceTest::NeutralAppearance(), control, tint);
				if (scene == 0 || (control[0] == 1 && control[1] == 1 && control[2] == 1 && tint[0] == 1))
					RequireAppearanceIdentity(result);
				for (size_t i = 0; i < result.size(); ++i) {
					for (size_t c = 0; c < 3; ++c) {
						Require(std::isfinite(result[i][c]), "Non-finite appearance grading");
						if (scene != 0 && control[0] == 0)
							Require(result[i][c] == 0, "Zero appearance intensity left visible energy");
						if (scene != 0 && control[2] == 0 && tint[0] == 1)
							Require(Close(result[i][c], result[i][0]), "Zero appearance saturation is not monochrome");
						if (i == 0)
							Require(result[i][c] == 0, "Appearance grading lifted black");
					}
				}
				if (scene != 0 && control[1] != 1) {
					auto neutralCurve = control;
					neutralCurve[1] = 1;
					auto expected = fixture.DrawAppearance(context, linear, scene, 0, AdaptiveBalanceTest::NeutralAppearance(), neutralCurve, tint,
						{ 1, 1, 1, 1 }, false, CurveInputs(control[1]));
					for (auto& pixel : expected)
						for (size_t c = 0; c < 3; ++c)
							pixel[c] = std::clamp(pixel[c], -65504.0f, 65504.0f);
					CompareAppearance(result, expected, "Appearance curve must precede saturation, tint and intensity");
					checked += unsigned(result.size());
				}
				checked += unsigned(result.size());
			}
		}
		return checked;
	}

	unsigned VerifyDirectionalAppearance(Fixture& fixture, ID3D11DeviceContext* context, bool linear, uint32_t scene)
	{
		unsigned checked = 0;
		for (bool linearLight : { false, true }) {
			for (float curve : { 0.1f, 0.5f, 1.0f, 2.0f, 4.0f }) {
				for (float saturation : { 0.0f, 1.0f, 2.0f }) {
					auto appearance = AdaptiveBalanceTest::NeutralAppearance();
					appearance[0] = saturation;
					appearance[1] = curve;
					const auto result = fixture.DrawAppearance(context, linear, scene, 1, appearance, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, linearLight);
					if (scene == 0 || (curve == 1 && saturation == 1))
						RequireAppearanceIdentity(result);
					for (size_t i = 0; i < result.size(); ++i) {
						for (size_t c = 0; c < 3; ++c) {
							Require(std::isfinite(result[i][c]), "Non-finite directional appearance");
							if (scene != 0 && saturation == 0)
								Require(Close(result[i][c], result[i][0]), "Zero directional saturation is not monochrome");
							if (i == 0)
								Require(result[i][c] == 0, "Directional curve lifted black");
						}
					}
					Require(result[5][2] > 1, "Directional controls clipped HDR to SDR");
					if (scene != 0 && curve != 1) {
						appearance[1] = 1;
						auto expected = fixture.DrawAppearance(context, linear, scene, 1, appearance, { 1, 1, 1, 1 }, { 1, 1, 1, 1 },
							{ 1, 1, 1, 1 }, linearLight, CurveInputs(curve));
						for (auto& pixel : expected)
							for (size_t c = 0; c < 3; ++c)
								pixel[c] = std::clamp(pixel[c], -65504.0f, 65504.0f);
						CompareAppearance(result, expected, "Directional curve must precede light conversion and saturation");
						appearance[1] = curve;
						checked += unsigned(result.size());
					}
					appearance[4] = 0;
					appearance[6] = 2;
					const auto tinted = fixture.DrawAppearance(context, linear, scene, 1, appearance, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, linearLight);
					auto expectedTint = result;
					if (scene != 0)
						for (auto& pixel : expectedTint) {
							pixel[0] = 0;
							pixel[2] *= 2;
							for (size_t c = 0; c < 3; ++c)
								pixel[c] = std::clamp(pixel[c], -65504.0f, 65504.0f);
						}
					CompareAppearance(tinted, expectedTint, "Directional tint must follow conversion and saturation");
					if (saturation == 1) {
						const auto shadowed = fixture.DrawAppearance(context, linear, scene, 1, appearance, { 1, 1, 1, 0.25f }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, linearLight);
						auto expectedShadow = tinted;
						for (auto& pixel : expectedShadow)
							for (size_t c = 0; c < 3; ++c)
								pixel[c] *= 0.25f;
						CompareAppearance(shadowed, expectedShadow, "Directional curve reshaped shadow attenuation");
						checked += unsigned(result.size());
					}
					checked += 2 * unsigned(result.size());
				}
			}
		}
		return checked;
	}

	unsigned VerifyAmbientAppearance(Fixture& fixture, ID3D11DeviceContext* context, bool linear, uint32_t scene)
	{
		unsigned checked = 0;
		for (float saturation : { 0.0f, 1.0f, 2.0f }) {
			auto appearance = AdaptiveBalanceTest::NeutralAppearance();
			appearance[2] = saturation;
			const auto result = fixture.DrawAppearance(context, linear, scene, 2, appearance);
			auto linearInputs = pointLightColors;
			if (!linear || scene == 0)
				for (auto& pixel : linearInputs)
					for (size_t c = 0; c < 3; ++c)
						pixel[c] = std::pow(std::abs(pixel[c]), 1.6f);
			const auto linearResult = fixture.DrawAppearance(context, linear, scene, 3, appearance, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, false, linearInputs);
			if (scene == 0 || saturation == 1) {
				RequireAppearanceIdentity(result);
				RequireAppearanceIdentity(linearResult);
			}
			auto encoded = result;
			if (!linear || scene == 0)
				for (auto& pixel : encoded)
					for (size_t c = 0; c < 3; ++c)
						pixel[c] = std::pow(std::abs(pixel[c]), 1.6f);
			CompareAppearance(encoded, linearResult, "Ambient renderer and linear irradiance grading disagree");
			const Pixel albedo{ 0.6f, 0.3f, 0.15f, 1 };
			const auto diffuse = fixture.DrawAppearance(context, linear, scene, 2, appearance, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, albedo);
			auto expected = result;
			for (size_t i = 0; i < expected.size(); ++i)
				for (size_t c = 0; c < 3; ++c) {
					expected[i][c] *= albedo[c];
					if (scene != 0 && saturation == 0)
						Require(Close(result[i][c], result[i][0]), "Zero ambient saturation is not monochrome");
				}
			CompareAppearance(diffuse, expected, "Ambient saturation changed diffuse material color");
			checked += 3 * unsigned(result.size());
		}
		return checked;
	}

	unsigned VerifyFogAppearance(Fixture& fixture, ID3D11DeviceContext* context, bool linear, uint32_t scene)
	{
		unsigned checked = 0;
		const Pixel surface{ 0.4f, 0.2f, 0.1f, 1 };
		for (float intensity : { 0.0f, 1.0f, 5.0f }) {
			for (const auto tint : { Pixel{ 1, 1, 1, 1 }, Pixel{ 0, 1, 2, 1 } }) {
				auto appearance = AdaptiveBalanceTest::NeutralAppearance();
				appearance[3] = intensity;
				std::copy_n(tint.begin(), 3, appearance.begin() + 8);
				for (float alpha : { 0.0f, 0.25f, 1.0f }) {
					const auto result = fixture.DrawAppearance(context, linear, scene, 4, appearance, { 1, 1, 1, alpha }, { 1, 1, 1, 1 }, surface);
					if (scene == 0 || (intensity == 1 && tint[0] == 1))
						RequireAppearanceIdentity(result);
					auto expected = pointLightColors;
					for (auto& pixel : expected)
						for (size_t c = 0; c < 3; ++c) {
							const float fog = scene == 0 ? pixel[c] : pixel[c] * (tint[c] * intensity);
							pixel[c] = surface[c] * (1 - alpha) + fog * alpha;
						}
					CompareAppearance(result, expected, "Fog color grading changed opacity or surface contribution");
					checked += unsigned(result.size());
				}
			}
		}
		return checked;
	}

	unsigned VerifyShadowContinuity(Fixture& fixture, ID3D11DeviceContext* context, bool linear, uint32_t scene)
	{
		unsigned checked = 0;
		auto authored = samples;
		for (auto& pixel : authored)
			for (size_t c = 0; c < 3; ++c)
				pixel[c] = std::max(pixel[c], 0.0f);
		for (bool linearLight : { false, true }) {
			for (float shadow : { 0.0f, 0.25f, 1.0f }) {
				const auto neutral = fixture.DrawAppearance(context, linear, scene, 5, AdaptiveBalanceTest::NeutralAppearance(),
					{ 1, 1, 1, shadow }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, linearLight, authored);
				RequireAppearanceIdentity(neutral);
				for (float curve : { 0.999999f, 1.000001f }) {
					auto appearance = AdaptiveBalanceTest::NeutralAppearance();
					appearance[1] = curve;
					const auto result = fixture.DrawAppearance(context, linear, scene, 5, appearance,
						{ 1, 1, 1, shadow }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, linearLight, authored);
					CompareAppearance(result, neutral, "Directional curve changed shadow response near neutral");
					checked += unsigned(result.size());
				}
			}
		}
		fixture.feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightCurve", 4.0f);
		fixture.feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightSaturation", 0.0f);
		const auto point = fixture.DrawAppearance(context, linear, scene, 6);
		if (scene == 0)
			RequireAppearanceIdentity(point);
		fixture.feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightCurve", 1.0f);
		fixture.feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightSaturation", 1.0f);
		return checked + unsigned(point.size());
	}

	unsigned VerifyAppearanceModes(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		Fixture fixture(device, vr, ShaderMode::Appearance);
		unsigned checked = 0;
		for (bool linear : { false, true }) {
			for (uint32_t scene : { 0u, 1u, 2u }) {
				try {
					checked += VerifyGenericAppearance(fixture, context, linear, scene);
					checked += VerifyDirectionalAppearance(fixture, context, linear, scene);
					checked += VerifyAmbientAppearance(fixture, context, linear, scene);
					checked += VerifyFogAppearance(fixture, context, linear, scene);
					checked += VerifyShadowContinuity(fixture, context, linear, scene);
				} catch (...) {
					std::cerr << "Appearance VR=" << vr << " LL=" << linear << " scene=" << scene << '\n';
					throw;
				}
			}
		}
		return checked;
	}

	constexpr uint32_t grayscaleColor = 1u << 19;
	constexpr uint32_t grayscaleAlpha = 1u << 20;
	constexpr uint32_t bothPaletteFlags = grayscaleColor | grayscaleAlpha;

	bool IdenticalRGB(const Pixel& a, const Pixel& b)
	{
		for (size_t c = 0; c < 3; ++c)
			if (std::bit_cast<uint32_t>(a[c]) != std::bit_cast<uint32_t>(b[c]))
				return false;
		return true;
	}

	void ReportFirePixels(const Pixels& result)
	{
		for (size_t i = 0; i < fireColors.size(); ++i) {
			std::cerr << "Fire sample=" << i;
			for (size_t c = 0; c < 3; ++c)
				std::cerr << " channel=" << c << " expected=" << fireColors[i][c] << " actual=" << result[i][c]
						  << " bits=" << std::hex << std::bit_cast<uint32_t>(fireColors[i][c]) << '/' << std::bit_cast<uint32_t>(result[i][c]) << std::dec;
			std::cerr << " diagnostic=" << result[i][3] << '\n';
		}
	}

	void VerifyFireColors(const Pixels& result, bool linearLighting, float intensity, float saturation, float curve)
	{
		for (size_t i = 0; i < fireColors.size(); ++i) {
			if (intensity == 1 && saturation == 1 && curve == 1)
				Require(IdenticalRGB(result[i], fireColors[i]), "Neutral fire controls changed authored RGB bits");
			for (size_t c = 0; c < 3; ++c) {
				Require(std::isfinite(result[i][c]), "Non-finite fire color");
				Require(fireColors[i][c] < 0 ? result[i][c] <= 0 : result[i][c] >= 0, "Fire controls changed an authored channel sign");
				if (intensity == 0)
					Require(result[i][c] == 0, "Zero fire intensity left visible energy");
				if (saturation == 0)
					Require(Close(std::abs(result[i][c]), std::abs(result[i][0])), "Zero fire saturation is not monochrome");
				if (saturation == 1 && curve == 1) {
					const float expectedScale = linearLighting ? intensity : std::pow(intensity, 1.0f / 2.2f);
					Require(Close(result[i][c], fireColors[i][c] * expectedScale), "Fire intensity is not linear-light scaling");
				}
			}
		}
		Require(result[0][0] == 0 && result[0][1] == 0 && result[0][2] == 0, "Fire grading lifted black");
		if (intensity == 0)
			return;
		Require(result[5][2] > 1, "Fire controls clipped HDR to SDR");
		if (intensity == 1 && saturation == 1 && curve != 1) {
			Require(Close(result[4][0] / result[4][1], 2), "Fire curve shifted authored hue");
			Require(curve > 1 ? result[1][0] < fireColors[1][0] : result[1][0] > fireColors[1][0], "Fire curve moved dim edges in the wrong direction");
			Require(curve > 1 ? result[5][2] > fireColors[5][2] : result[5][2] < fireColors[5][2], "Fire curve moved HDR cores in the wrong direction");
		}
		if (intensity == 1 && saturation == 2 && curve == 1)
			Require(Chroma(result[4]) > Chroma(fireColors[4]), "Fire saturation did not increase chroma");
	}

	unsigned VerifyFireControls(Fixture& fixture, ID3D11DeviceContext* context)
	{
		Pixels extreme;
		extreme.fill({ 65504, 32752, 0, 1 });
		for (bool linear : { false, true }) {
			const auto result = fixture.DrawFire(context, linear, bothPaletteFlags, 1, 5, 2, 4, extreme);
			std::cerr << "Extreme fire LL=" << linear << " RGB=" << result[0][0] << ',' << result[0][1] << ',' << result[0][2] << '\n';
			Require(result[0][0] > 1, "Extreme fire curve extinguished HDR lighting");
			for (const auto& pixel : result)
				for (size_t c = 0; c < 3; ++c)
					Require(std::isfinite(pixel[c]) && pixel[c] >= 0 && pixel[c] <= 65504, "Extreme fire curve exceeded HDR bounds");
		}

		const std::array controls{
			std::array{ 1.0f, 1.0f, 1.0f }, std::array{ 0.0f, 1.0f, 1.0f },
			std::array{ 2.0f, 1.0f, 1.0f }, std::array{ 1.0f, 0.0f, 1.0f },
			std::array{ 1.0f, 2.0f, 1.0f }, std::array{ 1.0f, 1.0f, 0.5f },
			std::array{ 1.0f, 1.0f, 2.0f }, std::array{ 5.0f, 2.0f, 4.0f },
			std::array{ 5.0f, 0.0f, 0.25f }, std::array{ 0.0f, 2.0f, 4.0f }
		};
		unsigned checkedSamples = 0;
		for (bool linearLighting : { false, true }) {
			for (uint32_t scene : { 1u, 2u }) {
				for (auto [intensity, saturation, curve] : controls) {
					const auto result = fixture.DrawFire(context, linearLighting, bothPaletteFlags, scene, intensity, saturation, curve);
					try {
						VerifyFireColors(result, linearLighting, intensity, saturation, curve);
					} catch (...) {
						std::cerr << "Fire controls LL=" << linearLighting << " scene=" << scene << " intensity=" << intensity
								  << " saturation=" << saturation << " curve=" << curve << '\n';
						ReportFirePixels(result);
						throw;
					}
					checkedSamples += unsigned(fireColors.size());
				}
			}
		}
		return checkedSamples;
	}

	unsigned VerifyFireModes(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		enum class Detection
		{
			Never,
			Palette,
			Always
		};
		struct FireCase
		{
			std::vector<const char*> defines;
			Detection detection;
		};
		const std::array cases{
			FireCase{ { "ADDBLEND", "SOFT" }, Detection::Palette },
			FireCase{ { "ADDBLEND", "PARTICLES", "TEXCOORD_INDEX", "INDEXED_TEXTURE" }, Detection::Always },
			FireCase{ { "ADDBLEND", "SOFT", "PARTICLES", "TEXCOORD_INDEX", "INDEXED_TEXTURE" }, Detection::Palette },
			FireCase{ { "SOFT" }, Detection::Never },
			FireCase{ { "PARTICLES", "TEXCOORD_INDEX", "INDEXED_TEXTURE" }, Detection::Never },
			FireCase{ { "ADDBLEND", "TEXCOORD_INDEX", "INDEXED_TEXTURE" }, Detection::Never },
			FireCase{ { "ADDBLEND", "PARTICLES", "INDEXED_TEXTURE" }, Detection::Never },
			FireCase{ { "ADDBLEND", "PARTICLES", "TEXCOORD_INDEX" }, Detection::Never },
			FireCase{ { "ADDBLEND" }, Detection::Never },
			FireCase{ { "ADDBLEND", "SOFT", "MOTIONVECTORS_NORMALS" }, Detection::Never },
			FireCase{ { "ADDBLEND", "PARTICLES", "TEXCOORD_INDEX", "INDEXED_TEXTURE", "MOTIONVECTORS_NORMALS" }, Detection::Never },
			FireCase{ {}, Detection::Never }
		};
		unsigned checkedSamples = 0;
		for (size_t index = 0; index < cases.size(); ++index) {
			const auto& test = cases[index];
			Fixture fixture(device, vr, ShaderMode::Fire, test.defines);
			for (bool linearLighting : { false, true }) {
				for (uint32_t palette : { 0u, grayscaleColor, grayscaleAlpha, bothPaletteFlags }) {
					const bool fire = test.detection == Detection::Always || (test.detection == Detection::Palette && palette == bothPaletteFlags);
					for (uint32_t scene : { 0u, 1u, 2u }) {
						const auto result = fixture.DrawFire(context, linearLighting, palette, scene, 2, 0.5f, 2);
						try {
							for (size_t i = 0; i < fireColors.size(); ++i) {
								Require(result[i][3] == 2.0f + 8.0f * fire + 16.0f * (scene != 0), "Fire detector or scene scope disagrees with the truth table");
								if (!fire || scene == 0)
									Require(IdenticalRGB(result[i], fireColors[i]), "Fire controls changed a non-fire or out-of-world draw");
							}
							if (fire && scene != 0)
								Require(!IdenticalRGB(result[4], fireColors[4]), "Fire controls did not reach the matched scene draw");
						} catch (...) {
							std::cerr << "Fire case=" << index << " VR=" << vr << " LL=" << linearLighting
									  << " palette=" << palette << " scene=" << scene << '\n';
							ReportFirePixels(result);
							throw;
						}
						checkedSamples += unsigned(fireColors.size());
					}
				}
			}
			if (index < 2)
				checkedSamples += VerifyFireControls(fixture, context);
		}
		return checkedSamples;
	}

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;

		Texture(ID3D11Device* device, const char* name, UINT flags)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 2;
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = flags;
			if (!flags) {
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			}
			Check(device->CreateTexture2D(&desc, nullptr, resource.GetAddressOf()));
			Util::SetResourceName(resource.Get(), "AdaptiveColorTest::%s", name);
			if (flags & D3D11_BIND_SHADER_RESOURCE) {
				Check(device->CreateShaderResourceView(resource.Get(), nullptr, srv.GetAddressOf()));
				Util::SetResourceName(srv.Get(), "AdaptiveColorTest::%s SRV", name);
			}
			if (flags & D3D11_BIND_RENDER_TARGET) {
				Check(device->CreateRenderTargetView(resource.Get(), nullptr, rtv.GetAddressOf()));
				Util::SetResourceName(rtv.Get(), "AdaptiveColorTest::%s RTV", name);
			}
		}

		void Set(ID3D11DeviceContext* context, Pixel color)
		{
			const std::array pixels{ color, color };
			context->UpdateSubresource(resource.Get(), 0, nullptr, pixels.data(), UINT(sizeof(pixels)), 0);
		}
	};

	struct BlendFixture
	{
		ID3D11DeviceContext* context;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11SamplerState> sampler;
		std::unique_ptr<ConstantBuffer> geometry, frame, feature;
		Texture output, staging, scene, bloom, average;

		BlendFixture(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool adaptive, bool fade) :
			context(context), output(device, "BlendOutput", D3D11_BIND_RENDER_TARGET),
			staging(device, "BlendReadback", 0), scene(device, "Scene", D3D11_BIND_SHADER_RESOURCE),
			bloom(device, "Bloom", D3D11_BIND_SHADER_RESOURCE), average(device, "Average", D3D11_BIND_SHADER_RESOURCE)
		{
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "BLEND", "1" } };
			if (vr)
				defines.push_back({ "VR", "1" });
			if (adaptive)
				defines.push_back({ "ADAPTIVE_BALANCE", "1" });
			if (fade)
				defines.push_back({ "FADE", "1" });
			auto code = Compile(L"package/Shaders/ISHDR.hlsl", defines, "ps_5_0");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, pixel.GetAddressOf()));
			Util::SetResourceName(pixel.Get(), "AdaptiveColorTest::BlendPS");
			constexpr char vs[] = "struct V { float4 p:SV_POSITION; float2 uv:TEXCOORD0; }; V main(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }";
			ComPtr<ID3DBlob> vertexCode;
			Check(D3DCompile(vs, sizeof(vs) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertexCode.GetAddressOf(), nullptr));
			Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
			Util::SetResourceName(vertex.Get(), "AdaptiveColorTest::FullscreenVS");
			D3D11_SAMPLER_DESC desc{};
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&desc, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "AdaptiveColorTest::Sampler");
			feature = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
			feature->SetMember("SharedData::adaptiveBalanceSettings", "appearance", AdaptiveBalanceTest::NeutralAppearance());
			feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightSaturation", 1.0f);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightCurve", 1.0f);
			geometry = std::make_unique<ConstantBuffer>(device, reflection.Get(), "PerGeometry");
			frame = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			frame->SetVariable("FrameBuffer::FrameParams", Pixel{ 1, 0, 0, 0 });
			frame->SetVariable("FrameBuffer::DynamicResolutionParams1", Pixel{ 1, 1, 1, 1 });
			frame->SetVariable("FrameBuffer::DynamicResolutionParams2", Pixel{ 1, 1, 1, 1 });
		}

		Pixel Draw(bool linear, float contrast, float saturation, float authoredContrast = 1, Pixel fade = {}, bool dark = false, bool alternateTonemap = false)
		{
			context->ClearState();
			scene.Set(context, dark ? Pixel{ 0.05f, 0.04f, 0.03f, 1 } : Pixel{ 0.4f, 0.2f, 0.1f, 1 });
			bloom.Set(context, dark ? Pixel{} : Pixel{ 0.1f, 0.2f, 0.3f, 1 });
			average.Set(context, { 0.8f, 0.8f, 0, 0 });
			feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint32_t(linear));
			feature->SetMember("SharedData::adaptiveBalanceSettings", "contrast", contrast);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "saturation", saturation);
			feature->Bind(context, D3D11ShaderTest::Stage::Pixel);
			geometry->SetVariable("Param", Pixel{ 1, 1, float(alternateTonemap), 0 });
			geometry->SetVariable("Cinematic", Pixel{ 1, 0, authoredContrast, 1 });
			geometry->SetVariable("Fade", fade);
			geometry->Bind(context, D3D11ShaderTest::Stage::Pixel);
			frame->Bind(context, D3D11ShaderTest::Stage::Pixel);
			ID3D11ShaderResourceView* sources[]{ bloom.srv.Get(), scene.srv.Get(), average.srv.Get() };
			context->PSSetShaderResources(0, 3, sources);
			ID3D11SamplerState* samplers[]{ sampler.Get(), sampler.Get(), sampler.Get() };
			context->PSSetSamplers(0, 3, samplers);
			ID3D11RenderTargetView* target = output.rtv.Get();
			context->OMSetRenderTargets(1, &target, nullptr);
			D3D11_VIEWPORT viewport{ 0, 0, 2, 1, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			context->Draw(3, 0);
			context->ClearState();
			context->CopyResource(staging.resource.Get(), output.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.resource.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<Pixel, 2> result;
			std::memcpy(result.data(), mapped.pData, sizeof(result));
			context->Unmap(staging.resource.Get(), 0);
			for (size_t c = 0; c < 4; ++c) {
				Require(std::isfinite(result[0][c]), "Non-finite HDR blend output");
				Require(Close(result[0][c], result[1][c]), "HDR blend eyes disagree");
			}
			return result[0];
		}
	};

	void VerifyBlend(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool fade)
	{
		BlendFixture baseline(device, context, vr, false, fade);
		BlendFixture graded(device, context, vr, true, fade);
		for (bool linear : { false, true }) {
			for (bool dark : { false, true }) {
				for (bool alternateTonemap : { false, true }) {
					for (const Pixel fading : { Pixel{}, Pixel{ 0.1f, 0.3f, 0.2f, 0.4f }, Pixel{ 0.1f, 0.3f, 0.2f, 1 } }) {
						const auto original = baseline.Draw(linear, 1, 1, 2, fading, dark, alternateTonemap);
						const auto neutral = graded.Draw(linear, 1, 1, 2, fading, dark, alternateTonemap);
						const auto nearNeutral = graded.Draw(linear, 1.000001f, 1, 2, fading, dark, alternateTonemap);
						const auto gray = graded.Draw(linear, 1, 0, 2, fading, dark, alternateTonemap);
						for (size_t c = 0; c < 3; ++c) {
							Require(Close(neutral[c], original[c]), "Neutral settings changed the HDR blend");
							Require(Close(nearNeutral[c], original[c]), "Tiny contrast adjustment clipped authored shadows");
							if (fade && fading[3] == 1)
								Require(Close(gray[c], original[c]), "Scene saturation changed a full fade");
							if (!fade || fading[3] == 0)
								Require(Close(gray[c], gray[0]), "Composed scene and bloom did not become monochrome");
						}
					}
				}
			}
		}
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		unsigned lightSamples = 0;
		unsigned fireSamples = 0;
		unsigned appearanceSamples = 0;
		for (bool vr : { false, true }) {
			std::cerr << "Creating " << (vr ? "VR" : "SE/AE") << " fixture\n";
			Fixture fixture(device.Get(), vr);
			std::cerr << "Running color samples\n";
			for (float contrast : { 0.5f, 1.0f, 2.0f }) {
				for (float saturation : { 0.0f, 1.0f, 2.0f }) {
					const auto linear = fixture.Draw(context.Get(), true, contrast, saturation);
					const auto gamma = fixture.Draw(context.Get(), false, contrast, saturation);
					Verify(linear, contrast, saturation);
					Verify(gamma, contrast, saturation);
					for (size_t i = 0; i < samples.size(); ++i)
						for (size_t c = 0; c < 3; ++c)
							Require(Close(linear[i][c], gamma[i][c]), "Linear Lighting paths disagree");
				}
			}
			lightSamples += VerifyLightModes(device.Get(), context.Get(), vr);
			appearanceSamples += VerifyAppearanceModes(device.Get(), context.Get(), vr);
			fireSamples += VerifyFireModes(device.Get(), context.Get(), vr);
			for (bool fade : { false, true })
				VerifyBlend(device.Get(), context.Get(), vr, fade);
		}
		std::cout << "288 color samples, 384 HDR blend draws, and " << lightSamples
				  << " light samples, " << fireSamples << " fire samples, and " << appearanceSamples << " appearance samples passed on WARP (SE/AE and VR).\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
