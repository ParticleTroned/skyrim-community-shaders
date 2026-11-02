#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "native_d3d11_test.h"
#include "sss_skin_color_fixture.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	constexpr std::array<Pixel, 8> colors{ Pixel{ 0, 0, 0, 0.125f }, { 0.02f, 0.02f, 0.02f, 0.25f },
		{ 0.12f, 0.012f, 0.003f, 0.375f }, { 0.03f, 0.008f, 0.004f, 0.5f },
		{ 0.001f, 0.004f, 0.03f, 0.625f }, { 0.7f, 0.4f, 0.2f, 0.75f },
		{ 8, 1, 0.1f, 0.875f }, { 1e-8f, 2e-9f, 3e-10f, 1 } };

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	double Linear(float value, bool linear) { return linear ? value : std::pow(std::max(0.0f, value), 1.6); }
	double Luminance(const Pixel& value, bool linear)
	{
		return 0.2125 * Linear(value[0], linear) + 0.7154 * Linear(value[1], linear) + 0.0721 * Linear(value[2], linear);
	}
	bool Close(double value, double expected)
	{
		return std::abs(value - expected) < 3e-5 * std::max(std::abs(expected), 1e-14);
	}

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;

		Texture(ID3D11Device* device, UINT height, UINT flags, const char* name, const Pixel* initial = nullptr)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 8;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = flags;
			if (!flags) {
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			}
			D3D11_SUBRESOURCE_DATA data{ initial, UINT(8 * sizeof(Pixel)), 0 };
			Check(device->CreateTexture2D(&desc, initial ? &data : nullptr, resource.GetAddressOf()));
			D3D11ShaderTest::Name(resource.Get(), (std::string("SSSSkinTest::") + name).c_str());
			if (flags & D3D11_BIND_SHADER_RESOURCE) {
				Check(device->CreateShaderResourceView(resource.Get(), nullptr, srv.GetAddressOf()));
				D3D11ShaderTest::Name(srv.Get(), (std::string("SSSSkinTest::") + name + " SRV").c_str());
			}
			if (flags & D3D11_BIND_UNORDERED_ACCESS) {
				Check(device->CreateUnorderedAccessView(resource.Get(), nullptr, uav.GetAddressOf()));
				D3D11ShaderTest::Name(uav.Get(), (std::string("SSSSkinTest::") + name + " UAV").c_str());
			}
		}
	};

	struct Fixture
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		Texture output, staging;
		UINT height;

		Fixture(ID3D11Device* device, bool full = false, bool reverse = false) :
			output(device, full ? 4 : 1, D3D11_BIND_UNORDERED_ACCESS, "Output"),
			staging(device, full ? 4 : 1, 0, "Readback"), height(full ? 4 : 1)
		{
			D3D11ShaderTest::ShaderIncludes includes;
			std::vector<D3D_SHADER_MACRO> defines{ { "COMPUTESHADER", "1" }, { "BURLEY", "1" } };
			if (reverse)
				defines.push_back({ "REVERSE_Z", "1" });
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> code, errors;
			constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
			const auto compiled = full ?
			                          D3DCompileFromFile((includes.root / "SubsurfaceScattering/SeparableSSSCS.hlsl").c_str(),
										  defines.data(), &includes, "main", "cs_5_0", flags, 0, code.GetAddressOf(), errors.GetAddressOf()) :
			                          D3DCompile(kSSSSkinColorFixture, std::strlen(kSSSSkinColorFixture), "sss_skin_color_fixture.hlsl",
										  defines.data(), &includes, "main", "cs_5_0", flags, 0, code.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiled);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
			D3D11ShaderTest::Name(shader.Get(), "SSSSkinTest::CS");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		}

		std::vector<Pixel> Draw(ID3D11DeviceContext* context, std::span<ConstantBuffer*> buffers, std::span<ID3D11ShaderResourceView*> textures = {})
		{
			context->ClearState();
			if (height > 1 && !textures.empty()) {
				ComPtr<ID3D11Resource> source;
				textures.front()->GetResource(source.GetAddressOf());
				context->CopyResource(output.resource.Get(), source.Get());
			}
			for (auto* buffer : buffers)
				buffer->Bind(context);
			if (!textures.empty())
				context->CSSetShaderResources(0, UINT(textures.size()), textures.data());
			auto* target = output.uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.resource.Get(), output.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.resource.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<Pixel> result(8 * height);
			for (UINT y = 0; y < height; ++y)
				std::memcpy(result.data() + y * 8, static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch, 8 * sizeof(Pixel));
			context->Unmap(staging.resource.Get(), 0);
			return result;
		}
	};

	void CheckControls(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		Fixture fixture(device);
		ConstantBuffer feature(device, fixture.reflection.Get(), "SharedData::FeatureData");
		ConstantBuffer inputs(device, fixture.reflection.Get(), "Samples");
		std::array buffers{ &feature, &inputs };
		auto originals = colors;
		for (auto& color : originals)
			for (unsigned c = 0; c < 3; ++c)
				color[c] *= c == 0 ? 0.75f : 0.5f;
		inputs.SetVariable("originals", originals);
		inputs.SetVariable("scattered", colors);
		for (bool linear : { false, true }) {
			feature.SetMember("SharedData::linearLightingSettings", "enableLinearLighting", unsigned(linear));
			for (float intensity : { 0.0f, 1.0f, 2.0f })
				for (float brightness : { 0.0f, 0.5f, 1.0f, 2.0f })
					for (float baseSat : { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f })
						for (float finalSat : { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f }) {
							inputs.SetVariable("controls", Pixel{ intensity, brightness, baseSat, finalSat });
							std::vector<Pixel> full;
							for (float mask : { 1.0f, 0.25f, 0.000001f, 0.0f }) {
								inputs.SetVariable("mask", mask);
								const auto result = fixture.Draw(context, buffers);
								if (mask == 1)
									full = result;
								for (unsigned i = 0; i < colors.size(); ++i) {
									Pixel neutral = originals[i];
									for (unsigned c = 0; c < 3; ++c)
										neutral[c] = std::max(originals[i][c] + (colors[i][c] * brightness - originals[i][c]) * intensity, 0.0f);
									Require(Close(Luminance(result[i], linear), Luminance(neutral, linear)), "Saturation changed linear-light brightness");
									Require(result[i][3] == originals[i][3], "Skin tuning changed alpha");
									for (unsigned c = 0; c < 3; ++c) {
										Require(std::isfinite(result[i][c]) && result[i][c] >= 0, "Skin tuning generated invalid RGB");
										const auto baseline = Linear(neutral[c], linear);
										const auto expected = baseline + (Linear(full[i][c], linear) - baseline) * mask;
										Require(Close(Linear(result[i][c], linear), expected), "Saturation did not fade continuously with the mask");
										if (baseSat == 1 && finalSat == 1)
											Require(Close(result[i][c], neutral[c]), "Neutral saturation changed brightness or intensity controls");
										if (mask == 1 && (finalSat == 0 || (baseSat == 0 && intensity == 1)))
											Require(Close(result[i][c], result[i][0]), "Desaturation no longer removes chroma");
									}
								}
							}
						}
			inputs.SetVariable("mask", 1.0f);
			for (const Pixel controls : { Pixel{ 1, 1, 2, 1 }, Pixel{ 1, 1, 1, 2 } }) {
				inputs.SetVariable("controls", controls);
				const auto result = fixture.Draw(context, buffers);
				Require(result[5][0] / result[5][1] > colors[5][0] / colors[5][1], "A saturation slider lost its stronger-colour response");
				Require(result[6][0] > 1, "Saturation clipped HDR to SDR");
			}
		}
	}

	void CheckExtremeColors(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		Fixture fixture(device);
		ConstantBuffer feature(device, fixture.reflection.Get(), "SharedData::FeatureData");
		ConstantBuffer inputs(device, fixture.reflection.Get(), "Samples");
		std::array buffers{ &feature, &inputs };
		constexpr std::array<Pixel, 8> originals{ Pixel{ 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 0.5f, 0.5f, 0.5f, 1 },
			{ 60000, 20000, 20000, 1 }, { 60000, 60000, 60000, 1 }, { 65504, 1, 65504, 1 }, { 65504, 65504, 65504, 1 } };
		constexpr std::array<Pixel, 8> scattered{ Pixel{ 0, 0.1f, 0, 1 }, { 0.1f, 0, 0, 1 }, { 0.1f, 0.2f, 0, 1 }, { 0.8f, 0, 0, 1 },
			{ 60000, 20000, 20000, 1 }, { 65504, 40000, 10000, 1 }, { 1, 65504, 1, 1 }, { 65504, 65504, 65504, 1 } };
		inputs.SetVariable("originals", originals);
		inputs.SetVariable("scattered", scattered);
		for (bool linear : { false, true }) {
			feature.SetMember("SharedData::linearLightingSettings", "enableLinearLighting", unsigned(linear));
			for (const Pixel controls : { Pixel{ 2, 1, 1, 0 }, { 1, 1, 2, 1 }, { 2, 2, 0, 0 }, { 2, 2, 2, 2 }, { 2, 2, 1, 1 } }) {
				inputs.SetVariable("controls", controls);
				for (float mask : { 1.0f, 0.25f, 0.0f }) {
					inputs.SetVariable("mask", mask);
					const auto result = fixture.Draw(context, buffers);
					for (unsigned i = 0; i < originals.size(); ++i) {
						Pixel neutral{};
						for (unsigned c = 0; c < 3; ++c)
							neutral[c] = std::clamp(originals[i][c] + (scattered[i][c] * controls[1] - originals[i][c]) * controls[0], 0.0f, 65504.0f);
						for (unsigned c = 0; c < 3; ++c) {
							Require(std::isfinite(result[i][c]) && result[i][c] >= 0 && result[i][c] <= 65504, "Skin tuning exceeded finite half-float storage");
							if (mask == 1 && controls[3] == 0)
								Require(Close(result[i][c], result[i][0]), "Negative intensity intermediates defeated zero SSS saturation");
						}
						Require(Close(Luminance(result[i], linear), Luminance(neutral, linear)), "Skin gamut fitting changed neutral luminance");
					}
				}
			}
			// Cross the point where intensity extrapolation clips every adjusted channel.
			inputs.SetVariable("mask", 1.0f);
			std::vector<Pixel> previous;
			for (unsigned step = 0; step <= 200; ++step) {
				inputs.SetVariable("controls", Pixel{ 2, 1, float(step) * 0.001f, 1 });
				const auto result = fixture.Draw(context, buffers);
				if (!previous.empty())
					for (unsigned c = 0; c < 3; ++c)
						Require(std::abs(result[3][c] - previous[3][c]) < 0.01f, "Near-black normalization introduced a colour discontinuity");
				previous = result;
			}
		}
	}

	void CheckBurley(ID3D11Device* device, ID3D11DeviceContext* context, bool featureEnabled, bool reversed)
	{
		Fixture fixture(device, true, featureEnabled);
		ConstantBuffer feature(device, fixture.reflection.Get(), "SharedData::FeatureData");
		ConstantBuffer shared(device, fixture.reflection.Get(), "SharedData::SharedData");
		ConstantBuffer frame(device, fixture.reflection.Get(), "FrameBuffer::PerFrame");
		ConstantBuffer sss(device, fixture.reflection.Get(), "PerFrameSSS");
		std::array buffers{ &feature, &shared, &frame, &sss };
		shared.SetVariable("SharedData::BufferDim", Pixel{ 8, 4, 0.125f, 0.25f });
		shared.SetVariable("SharedData::CameraData", Pixel{ 1, 1, 0, 100 });
		constexpr std::array<float, 16> identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		frame.SetVariable("FrameBuffer::CameraViewInverse", identity);
		if (featureEnabled) {
			std::array<float, 16> projection{};
			projection[0] = projection[5] = projection[14] = 1.0f;
			projection[10] = reversed ? -0.001f : 1.001f;
			projection[11] = reversed ? 1.001f : -1.001f;
			frame.SetVariable("FrameBuffer::CameraProj", projection);
		}
		sss.SetVariable("SSSS_FOVY", 90.0f);
		sss.SetVariable("BurleySamples", 16u);
		sss.SetVariable("MeanFreePathBase", Pixel{ 0.56f, 0.56f, 0.56f, 2.67f });
		sss.SetVariable("MeanFreePathHuman", Pixel{ 1, 0.37f, 0.3f, 2.67f });
		std::array<Pixel, 32> source{}, masks{}, depth{}, albedo{}, normals{};
		constexpr std::array maskValues{ 0.0f, 0.001f, 0.25f, 1.0f };
		for (unsigned i = 0; i < source.size(); ++i) {
			source[i] = colors[i % 8];
			masks[i] = { maskValues[i / 8], float(i % 3), 0, 0 };
			depth[i] = { 0.5f, 0, 0, 0 };
			albedo[i] = { 0.5f, 0.4f, 0.3f, 0.5f };
			normals[i] = { 0.5f, 0.5f, 0, 0 };
		}
		std::array textures{ Texture(device, 4, D3D11_BIND_SHADER_RESOURCE, "Color", source.data()),
			Texture(device, 4, D3D11_BIND_SHADER_RESOURCE, "Depth", depth.data()),
			Texture(device, 4, D3D11_BIND_SHADER_RESOURCE, "Mask", masks.data()),
			Texture(device, 4, D3D11_BIND_SHADER_RESOURCE, "Albedo", albedo.data()),
			Texture(device, 4, D3D11_BIND_SHADER_RESOURCE, "Normal", normals.data()) };
		std::array<ID3D11ShaderResourceView*, 5> views;
		for (unsigned i = 0; i < views.size(); ++i)
			views[i] = textures[i].srv.Get();
		for (bool linear : { false, true }) {
			feature.SetMember("SharedData::linearLightingSettings", "enableLinearLighting", unsigned(linear));
			for (const char* sex : { "Male", "Female" })
				sss.SetVariable((std::string("Human") + sex + "Controls").c_str(), Pixel{ 1, 1, 1, 1 });
			const auto neutral = fixture.Draw(context, buffers, views);
			for (const char* sex : { "Male", "Female" })
				for (unsigned component : { 1u, 3u })
					for (float saturation : { 0.0f, 0.5f, 1.5f, 2.0f }) {
						const auto name = std::string("Human") + sex + "Controls";
						Pixel controls{ 1, 1, 1, 1 };
						controls[component] = saturation;
						sss.SetVariable(name.c_str(), controls);
						const auto result = fixture.Draw(context, buffers, views);
						unsigned changed = 0;
						for (unsigned i = 0; i < result.size(); ++i) {
							Require(Close(Luminance(result[i], linear), Luminance(neutral[i], linear)), "Burley saturation changed lighting energy");
							Require(result[i][3] == source[i][3], "Burley changed source alpha");
							const auto selected = std::string_view(sex) == "Male" ? 1.0f : 2.0f;
							if (masks[i][0] == 0 || masks[i][1] != selected)
								Require(result[i] == neutral[i], "Skin tuning escaped its mask or sex classification");
							else
								changed += result[i] != neutral[i];
						}
						Require(changed > 0, "Burley did not apply the selected saturation control");
						sss.SetVariable(name.c_str(), Pixel{ 1, 1, 1, 1 });
					}
		}
	}
}

TEST_CASE("SE skin saturation preserves lighting energy and native routing", "[sss][d3d11][warp]")
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
		device.GetAddressOf(), nullptr, context.GetAddressOf()));
	CheckExtremeColors(device.Get(), context.Get());
	CheckControls(device.Get(), context.Get());
	CheckBurley(device.Get(), context.Get(), false, false);
	CheckBurley(device.Get(), context.Get(), true, false);
	CheckBurley(device.Get(), context.Get(), true, true);
}
