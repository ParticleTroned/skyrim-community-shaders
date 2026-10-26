#define NOMINMAX
#include "Features/AdaptiveBalanceGodraySettings.h"
#include "Features/VolumetricLightingTuning.h"
#include "Utils/ShaderInclude.h"
#include "adaptive_balance_test_settings.h"
#include "d3d11_shader_test.h"

#include <d3d11.h>
#include <d3d11shader.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using uint = uint32_t;
	struct alignas(16) float4
	{
		float x, y, z, w;
	};
#include "shared_data_under_test.h"
#include "volumetric_lighting_runtime_fixture.h"

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	using D3D11ShaderTest::Check;
	using Buffer = D3D11ShaderTest::ConstantBuffer;

	ComPtr<ID3DBlob> CompileCompositeShader(bool vr, bool volumetric, bool lensFlare, bool vertex)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { vertex ? "VSHADER" : "PSHADER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (volumetric)
			defines.push_back({ "VOLUMETRIC_LIGHTING", "1" });
		if (lensFlare)
			defines.push_back({ "LENS_FLARE", "1" });
		defines.push_back({ nullptr, nullptr });
		Util::CustomInclude includes("package/Shaders");
		ComPtr<ID3DBlob> bytecode, errors;
		const auto compiled = D3DCompileFromFile(L"package/Shaders/ISCompositeLensFlareVolumetricLighting.hlsl",
			defines.data(), &includes, "main", vertex ? "vs_5_0" : "ps_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(compiled);
		return bytecode;
	}

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;

		Texture(ID3D11Device* device, const char* name, UINT flags, const float4* pixels = nullptr, UINT width = 2, UINT height = 1)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = flags;
			if (!flags) {
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			}
			D3D11_SUBRESOURCE_DATA initial{ pixels, width * sizeof(float4), 0 };
			Check(device->CreateTexture2D(&desc, pixels ? &initial : nullptr, resource.GetAddressOf()));
			Util::SetResourceName(resource.Get(), "VolumetricTest::%s", name);
			if (flags & D3D11_BIND_SHADER_RESOURCE) {
				Check(device->CreateShaderResourceView(resource.Get(), nullptr, srv.GetAddressOf()));
				Util::SetResourceName(srv.Get(), "VolumetricTest::%s SRV", name);
			}
			if (flags & D3D11_BIND_RENDER_TARGET) {
				Check(device->CreateRenderTargetView(resource.Get(), nullptr, rtv.GetAddressOf()));
				Util::SetResourceName(rtv.Get(), "VolumetricTest::%s RTV", name);
			}
		}
	};

	struct Fixture
	{
		ID3D11DeviceContext* context;
		bool volumetric, lensFlare, stereo;
		bool depthAvailable = true;
		UINT width, height;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11SamplerState> sampler;
		std::unique_ptr<Buffer> shared, geometry, frame, feature;
		Texture output, staging, shafts, flare, depth;
		static constexpr float4 shaftPixels[2]{ { 0.5f, 0, 0, 0 }, { 0.5f, 0, 0, 0 } };
		static constexpr float4 flarePixels[2]{ { 0.1f, 0.2f, 0.3f, 0 }, { 0.1f, 0.2f, 0.3f, 0 } };

		Fixture(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool vl, bool lf, UINT width = 128, UINT height = 96,
			D3D11_FILTER filter = D3D11_FILTER_MIN_MAG_MIP_POINT) :
			context(context), volumetric(vl), lensFlare(lf), stereo(vr),
			width(width), height(height),
			output(device, "Output", D3D11_BIND_RENDER_TARGET, nullptr, width, height), staging(device, "Readback", 0, nullptr, width, height),
			shafts(device, "Shafts", D3D11_BIND_SHADER_RESOURCE, width == 2 && height == 1 ? shaftPixels : nullptr, width, height),
			flare(device, "Flare", D3D11_BIND_SHADER_RESOURCE, nullptr, width, height),
			depth(device, "Depth", D3D11_BIND_SHADER_RESOURCE, nullptr, width, height)
		{
			std::vector<float4> initial(width * height, { 0.5f, 0, 0, 0 });
			context->UpdateSubresource(depth.resource.Get(), 0, nullptr, initial.data(), width * sizeof(float4), 0);
			std::fill(initial.begin(), initial.end(), flarePixels[0]);
			context->UpdateSubresource(flare.resource.Get(), 0, nullptr, initial.data(), width * sizeof(float4), 0);
			std::fill(initial.begin(), initial.end(), float4{});
			const UINT eyeWidth = vr ? width / 2 : width;
			for (UINT y = 0; y < height; ++y)
				for (UINT eye = 0; eye < (vr ? 2u : 1u); ++eye)
					initial[y * width + eye * eyeWidth + eyeWidth / 2] = shaftPixels[0];
			context->UpdateSubresource(shafts.resource.Get(), 0, nullptr, initial.data(), width * sizeof(float4), 0);
			auto bytecode = CompileCompositeShader(vr, vl, lf, false);
			Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, pixel.GetAddressOf()));
			Util::SetResourceName(pixel.Get(), "VolumetricTest::CompositePS");
			constexpr char vs[] = "struct V { float4 p:SV_POSITION; float2 uv:TEXCOORD0; }; V main(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }";
			ComPtr<ID3DBlob> vertexCode;
			Check(D3DCompile(vs, sizeof(vs) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertexCode.GetAddressOf(), nullptr));
			Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
			Util::SetResourceName(vertex.Get(), "VolumetricTest::FullscreenVS");
			D3D11_SAMPLER_DESC desc{};
			desc.Filter = filter;
			desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&desc, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "VolumetricTest::Sampler");
			feature = std::make_unique<Buffer>(device, reflection.Get(), "SharedData::FeatureData");
			if (vl) {
				shared = std::make_unique<Buffer>(device, reflection.Get(), "SharedData::SharedData");
				Require(shared->bytes.size() == sizeof(SharedDataCB), "SharedDataCB size mismatch");
				Require(shared->Offset("SharedData::VolumetricLightingSaturation") == offsetof(SharedDataCB, VolumetricLightingSaturation), "Saturation offset mismatch");
				Require(shared->Offset("SharedData::VolumetricLightingOpacity") == offsetof(SharedDataCB, VolumetricLightingOpacity), "Opacity offset mismatch");
				Require(shared->Offset("SharedData::VolumetricLightingReserved") == offsetof(SharedDataCB, VolumetricLightingReserved), "Reserved volumetric slot offset mismatch");
				Require(shared->Offset("SharedData::VolumetricLightingClarity") == offsetof(SharedDataCB, VolumetricLightingClarity), "Clarity offset mismatch");
				Require(shared->Offset("SharedData::VolumetricLightingCustomColor") == offsetof(SharedDataCB, VolumetricLightingCustomColor), "Custom color offset mismatch");
				geometry = std::make_unique<Buffer>(device, reflection.Get(), "PerGeometry");
				frame = std::make_unique<Buffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
				frame->SetVariable("FrameBuffer::DynamicResolutionParams1", float4{ 1, 1, 1, 1 });
				frame->SetVariable("FrameBuffer::DynamicResolutionParams2", float4{ 1, 1, 1, 1 });
			}
		}

		float4 Draw(float4 authored, float saturation = 1, float4 custom = {}, float opacity = 1, bool linear = false, float intensity = 1, float gamma = 1, float clarity = 0)
		{
			SharedDataCB data{};
			data.VolumetricLightingClarity.x = clarity;
			data.VolumetricLightingClarity.y = intensity;
			data.VolumetricLightingSaturation = saturation;
			data.VolumetricLightingOpacity = opacity;
			data.VolumetricLightingCustomColor = custom;
			return Draw(authored, data, linear, gamma);
		}

		float4 Draw(float4 authored, const SharedDataCB& data, bool linear = false, float gamma = 1)
		{
			const auto result = DrawPixels(authored, data, linear, gamma);
			const UINT first = (height / 2) * width + width / 4;
			const UINT second = first + width / 2;
			if (stereo)
				Require(std::abs(result[first].x - result[second].x) < 1e-5f && std::abs(result[first].y - result[second].y) < 1e-5f && std::abs(result[first].z - result[second].z) < 1e-5f, "Stereo eyes differ");
			// The mono fixture has its shaft at the center; stereo has one per half.
			return result[(height / 2) * width + (stereo ? width / 4 : width / 2)];
		}

		std::vector<float4> DrawPixels(float4 authored, const SharedDataCB& data, bool linear = false, float gamma = 1)
		{
			context->ClearState();
			feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint(linear));
			feature->SetMember("SharedData::linearLightingSettings", "vlGamma", gamma);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "appearance", AdaptiveBalanceTest::NeutralAppearance());
			feature->Bind(context, D3D11ShaderTest::Stage::Pixel);
			if (volumetric) {
				auto upload = data;
				upload.CameraData = { 1000, 1, 999, 1000 };
				std::memcpy(shared->bytes.data(), &upload, sizeof(upload));
				shared->Bind(context, D3D11ShaderTest::Stage::Pixel);
				geometry->SetVariable("VolumetricLightingColor", authored);
				geometry->Bind(context, D3D11ShaderTest::Stage::Pixel);
				frame->Bind(context, D3D11ShaderTest::Stage::Pixel);
			}
			ID3D11ShaderResourceView* sources[]{ shafts.srv.Get(), flare.srv.Get() };
			context->PSSetShaderResources(0, 2, sources);
			ID3D11ShaderResourceView* depthSource = depthAvailable ? depth.srv.Get() : nullptr;
			context->PSSetShaderResources(17, 1, &depthSource);
			ID3D11SamplerState* samplers[]{ sampler.Get(), sampler.Get() };
			context->PSSetSamplers(0, 2, samplers);
			ID3D11RenderTargetView* target = output.rtv.Get();
			context->OMSetRenderTargets(1, &target, nullptr);
			D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			context->Draw(3, 0);
			context->ClearState();
			context->CopyResource(staging.resource.Get(), output.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.resource.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<float4> result(width * height);
			for (UINT row = 0; row < height; ++row)
				std::memcpy(result.data() + row * width, static_cast<const std::byte*>(mapped.pData) + row * mapped.RowPitch, width * sizeof(float4));
			context->Unmap(staging.resource.Get(), 0);
			return result;
		}
	};

	void Expect(float4 actual, float4 expected, const char* message)
	{
		if (!std::isfinite(actual.x) || !std::isfinite(actual.y) || !std::isfinite(actual.z) ||
			std::abs(actual.x - expected.x) >= 2e-4f || std::abs(actual.y - expected.y) >= 2e-4f || std::abs(actual.z - expected.z) >= 2e-4f) {
			std::cerr << message << ": actual " << actual.x << ',' << actual.y << ',' << actual.z
					  << " expected " << expected.x << ',' << expected.y << ',' << expected.z << '\n';
			throw std::runtime_error(message);
		}
	}

	void VerifyClarity(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		struct Case
		{
			UINT width, height;
			float scale;
		};
		for (const auto& config : { Case{ 128, 96, 1 }, Case{ 128, 96, 0.5f }, Case{ 129, 97, 1 } }) {
			const UINT width = vr ? config.width * 2 - (config.width % 2) : config.width;
			Fixture fixture(device, context, vr, true, false, width, config.height);
			const UINT activeWidth = static_cast<UINT>(width * config.scale);
			const UINT activeHeight = static_cast<UINT>(config.height * config.scale);
			const UINT eyeWidth = vr ? activeWidth / 2 : activeWidth;
			const UINT eyeCount = vr ? 2 : 1;
			fixture.frame->SetVariable("FrameBuffer::DynamicResolutionParams1", float4{ config.scale, config.scale, config.scale, config.scale });
			fixture.frame->SetVariable("FrameBuffer::DynamicResolutionParams2", float4{ 1 / config.scale, 1 / config.scale, config.scale, config.scale });
			SharedDataCB data{};
			data.VolumetricLightingReserved = data.VolumetricLightingOpacity = data.VolumetricLightingSaturation = 1;
			data.VolumetricLightingClarity.y = 1;
			std::vector<float4> pixels(width * config.height);
			const auto upload = [&](bool beams) {
				std::fill(pixels.begin(), pixels.end(), float4{});
				for (UINT y = 0; y < activeHeight; ++y) {
					for (UINT x = 0; x < activeWidth; ++x) {
						const UINT eye = vr && x >= eyeWidth ? 1 : 0;
						const float haze = eye == 0 ? 0.2f : 0.65f;
						const UINT beamColumn = eye * eyeWidth + eyeWidth / 2;
						pixels[y * width + x].x = haze + (beams && x == beamColumn ? 0.6f : 0.0f);
					}
				}
				context->UpdateSubresource(fixture.shafts.resource.Get(), 0, nullptr, pixels.data(), width * sizeof(float4), 0);
			};
			const auto sample = [&](const std::vector<float4>& output, UINT eye, UINT sourceColumn) {
				const UINT x = std::min(width - 1, static_cast<UINT>((eye * eyeWidth + sourceColumn + 0.5f) / config.scale));
				return output[(config.height / 2) * width + x];
			};
			for (float gamma : { 0.5f, 1.0f, 2.0f }) {
				upload(false);
				data.VolumetricLightingClarity.x = 1;
				const auto clear = fixture.DrawPixels({ 1, 1, 1, 0 }, data, true, gamma);
				for (const auto& pixel : clear)
					Expect(pixel, {}, "Uniform haze survived clarity or sampled another eye/inactive pixels");

				for (float clarity : { 0.0f, 0.5f }) {
					data.VolumetricLightingClarity.x = clarity;
					const auto output = fixture.DrawPixels({ 1, 1, 1, 0 }, data, true, gamma);
					for (UINT eye = 0; eye < eyeCount; ++eye) {
						const float expected = std::pow(eye == 0 ? 0.2f : 0.65f, gamma) * (1 - clarity);
						Expect(sample(output, eye, eyeWidth / 4), { expected, expected, expected, 0 }, "Clarity did not interpolate the processed haze floor");
					}
				}

				upload(true);
				data.VolumetricLightingClarity.x = 1;
				const auto beams = fixture.DrawPixels({ 1, 1, 1, 0 }, data, true, gamma);
				for (UINT eye = 0; eye < eyeCount; ++eye) {
					const float haze = eye == 0 ? 0.2f : 0.65f;
					const float expected = std::pow(haze + 0.6f, gamma) - std::pow(haze, gamma);
					Expect(sample(beams, eye, eyeWidth / 2), { expected, expected, expected, 0 }, "Clarity lost bright shaft detail");
					Expect(sample(beams, eye, eyeWidth / 4), {}, "Clarity retained broad haze beside a shaft");
				}
				for (const auto& pixel : beams)
					Require(std::isfinite(pixel.x) && pixel.x >= 0, "Clarity created negative or nonfinite lighting");
			}

			data.VolumetricLightingClarity.y = 0;
			for (const auto& pixel : fixture.DrawPixels({ 1, 1, 1, 0 }, data))
				Expect(pixel, {}, "Clarity overrode zero godray intensity");
			data.VolumetricLightingClarity.y = 1;
			for (float invalid : { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
				data.VolumetricLightingClarity.x = invalid;
				const auto output = fixture.DrawPixels({ 1, 1, 1, 0 }, data);
				Expect(sample(output, 0, eyeWidth / 4), { 0.2f, 0.2f, 0.2f, 0 }, "Invalid shader clarity did not preserve the original image");
			}
		}
	}

	void VerifyClarityFiltering(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		const UINT width = vr ? 258 : 129;
		constexpr UINT height = 97;
		constexpr float scaleX = 0.725f, scaleY = 0.61f;
		const UINT activeWidth = static_cast<UINT>(width * scaleX);
		const UINT activeHeight = static_cast<UINT>(height * scaleY);
		for (auto filter : { D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_MIP_LINEAR }) {
			Fixture fixture(device, context, vr, true, false, width, height, filter);
			fixture.frame->SetVariable("FrameBuffer::DynamicResolutionParams1", float4{ scaleX, scaleY, scaleX, scaleY });
			fixture.frame->SetVariable("FrameBuffer::DynamicResolutionParams2", float4{ 1 / scaleX, 1 / scaleY, scaleX, scaleX });
			std::vector<float4> pixels(width * height, { 10, 0, 0, 0 });
			for (UINT y = 0; y < activeHeight; ++y)
				for (UINT x = 0; x < activeWidth; ++x)
					pixels[y * width + x].x = vr && x >= activeWidth / 2 ? 0.65f : 0.2f;
			context->UpdateSubresource(fixture.shafts.resource.Get(), 0, nullptr, pixels.data(), width * sizeof(float4), 0);
			SharedDataCB data{};
			data.VolumetricLightingReserved = data.VolumetricLightingSaturation = 1;
			data.VolumetricLightingClarity.y = 1;
			for (float opacity : { 0.0f, 0.5f, 1.0f, 2.0f }) {
				data.VolumetricLightingOpacity = opacity;
				for (float clarity : { 0.0f, 0.5f, 1.0f }) {
					data.VolumetricLightingClarity.x = clarity;
					const auto output = fixture.DrawPixels({ 1, 1, 1, 0 }, data);
					for (UINT y = 0; y < height; ++y) {
						for (UINT x = 0; x < width; ++x) {
							const float haze = vr && (x + 0.5f) / width >= 0.5f ? 0.65f : 0.2f;
							const float expected = haze * (1 - clarity);
							if (std::abs(output[y * width + x].x - expected) >= 2e-4f)
								std::cerr << "filter=" << filter << " x=" << x << " y=" << y << " opacity=" << opacity << " clarity=" << clarity << '\n';
							Expect(output[y * width + x], { expected, expected, expected, 0 }, "Filtering sampled outside the active eye or changed clarity/opacity composition");
						}
					}
				}
			}
		}
	}

	void VerifySeparation(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		const UINT width = vr ? 256 : 128;
		constexpr UINT height = 96;
		const UINT eyeWidth = vr ? width / 2 : width;
		Fixture fixture(device, context, vr, true, false, width, height);
		std::vector<float4> pixels(width * height, { 0.2f, 0, 0, 0 });
		for (UINT y = 0; y < height; ++y)
			for (UINT eye = 0; eye < (vr ? 2u : 1u); ++eye)
				pixels[y * width + eye * eyeWidth + eyeWidth / 2].x = 0.8f;
		context->UpdateSubresource(fixture.shafts.resource.Get(), 0, nullptr, pixels.data(), width * sizeof(float4), 0);
		SharedDataCB data{};
		for (float gamma : { 0.5f, 1.0f, 2.0f }) {
			const float haze = std::pow(0.2f, gamma);
			const float ray = std::pow(0.8f, gamma) - haze;
			for (float clarity : { 0.0f, 0.46f, 1.0f }) {
				data.VolumetricLightingClarity.x = clarity;
				for (float legacyBrightness : { 0.0f, 1.0f, 5.0f, std::numeric_limits<float>::quiet_NaN() }) {
					data.VolumetricLightingReserved = legacyBrightness;
					for (float intensity : { 0.0f, 1.0f, 3.0f }) {
						data.VolumetricLightingClarity.y = intensity;
						for (float opacity : { 0.0f, 1.0f, 2.0f }) {
							data.VolumetricLightingOpacity = opacity;
							data.VolumetricLightingSaturation = 4;
							data.VolumetricLightingCustomColor = { 0, 1, 0, 1 };
							const auto output = fixture.DrawPixels({ 1, 1, 1, 0 }, data, true, gamma);
							const float h = haze * (1 - clarity);
							const float r = VolumetricLightingTuning::ApplyOpacityCurve(ray, opacity) * intensity;
							for (UINT eye = 0; eye < (vr ? 2u : 1u); ++eye) {
								const UINT row = height / 2 * width + eye * eyeWidth;
								Expect(output[row + eyeWidth / 4], { h, h, h, 0 }, "Godray controls changed broad haze");
								Expect(output[row + eyeWidth / 2], { h, h + r, h, 0 }, "Clarity and godray controls were not independent");
							}
						}
					}
				}
			}
		}
		const float4 authored{ 2, 0.6f, 0.2f, 0 };
		data.VolumetricLightingClarity.y = 2;
		data.VolumetricLightingOpacity = 1.7f;
		for (float saturation : { 0.0f, 1.0f, 4.0f }) {
			data.VolumetricLightingSaturation = saturation;
			for (float contribution : { 0.0f, 0.5f }) {
				data.VolumetricLightingCustomColor = { 0, 1, 0, contribution };
				data.VolumetricLightingClarity.x = 1;
				const auto shaftsOnly = fixture.DrawPixels(authored, data);
				for (float clarity : { 0.0f, 0.46f }) {
					data.VolumetricLightingClarity.x = clarity;
					const auto withHaze = fixture.DrawPixels(authored, data);
					const float haze = 0.2f * (1 - clarity);
					for (size_t i = 0; i < pixels.size(); ++i) {
						const float4 difference{ withHaze[i].x - shaftsOnly[i].x, withHaze[i].y - shaftsOnly[i].y, withHaze[i].z - shaftsOnly[i].z, 0 };
						Expect(difference, { authored.x * haze, authored.y * haze, authored.z * haze, 0 }, "Saturation or partial tint changed the clarity contribution");
					}
				}
			}
		}
		fixture.depthAvailable = false;
		const auto fallback = fixture.DrawPixels({ 1, 1, 1, 0 }, data);
		for (size_t i = 0; i < pixels.size(); ++i)
			Expect(fallback[i], { pixels[i].x, pixels[i].x, pixels[i].x, 0 }, "Missing depth did not preserve original lighting");
		fixture.depthAvailable = true;
		std::vector<float4> invalidDepth(width * height, { std::numeric_limits<float>::quiet_NaN(), 0, 0, 0 });
		context->UpdateSubresource(fixture.depth.resource.Get(), 0, nullptr, invalidDepth.data(), width * sizeof(float4), 0);
		const auto invalid = fixture.DrawPixels({ 1, 1, 1, 0 }, data);
		for (size_t i = 0; i < pixels.size(); ++i)
			Expect(invalid[i], { pixels[i].x, pixels[i].x, pixels[i].x, 0 }, "Invalid depth did not preserve original lighting");
	}

	void VerifySilhouettes(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		const UINT width = vr ? 258 : 129;
		constexpr UINT height = 97;
		for (float scale : { 1.0f, 0.725f }) {
			const UINT activeWidth = static_cast<UINT>(width * scale), activeHeight = static_cast<UINT>(height * scale);
			const UINT eyeWidth = vr ? activeWidth / 2 : activeWidth;
			Fixture fixture(device, context, vr, true, false, width, height, D3D11_FILTER_MIN_MAG_MIP_LINEAR);
			fixture.frame->SetVariable("FrameBuffer::DynamicResolutionParams1", float4{ scale, scale, scale, scale });
			fixture.frame->SetVariable("FrameBuffer::DynamicResolutionParams2", float4{ 1 / scale, 1 / scale, scale, scale });
			SharedDataCB data{};
			data.VolumetricLightingReserved = 5;
			data.VolumetricLightingClarity = { 1, 3, 0, 0 };
			data.VolumetricLightingOpacity = data.VolumetricLightingSaturation = 1;
			for (int offset : { -7, -2, 0, 3, 8 }) {
				std::vector<float4> light(width * height, { 10, 0, 0, 0 }), depth(width * height);
				for (UINT y = 0; y < activeHeight; ++y) {
					for (UINT x = 0; x < activeWidth; ++x) {
						const UINT eye = vr && x >= eyeWidth ? 1 : 0;
						const int localX = static_cast<int>(x - eye * eyeWidth);
						const bool hand = std::abs(localX - static_cast<int>(eyeWidth / 2) - offset) <= 2 && y > activeHeight / 4;
						light[y * width + x].x = hand ? 0.001f : (eye ? 0.8f : 0.3f);
						depth[y * width + x].x = hand ? 0.1f : 0.99f;
					}
				}
				context->UpdateSubresource(fixture.shafts.resource.Get(), 0, nullptr, light.data(), width * sizeof(float4), 0);
				context->UpdateSubresource(fixture.depth.resource.Get(), 0, nullptr, depth.data(), width * sizeof(float4), 0);
				for (const auto& pixel : fixture.DrawPixels({ 1, 1, 1, 0 }, data))
					Expect(pixel, {}, "A moving foreground silhouette created a bright haze halo");
			}
			std::vector<float4> ramp(width * height), depth(width * height, { 0.9f, 0, 0, 0 });
			for (UINT y = 0; y < activeHeight; ++y)
				for (UINT x = 0; x < activeWidth; ++x) {
					const UINT eye = vr && x >= eyeWidth ? 1 : 0;
					ramp[y * width + x].x = 0.1f + 0.5f * float(x - eye * eyeWidth) / eyeWidth;
				}
			context->UpdateSubresource(fixture.shafts.resource.Get(), 0, nullptr, ramp.data(), width * sizeof(float4), 0);
			context->UpdateSubresource(fixture.depth.resource.Get(), 0, nullptr, depth.data(), width * sizeof(float4), 0);
			for (const auto& pixel : fixture.DrawPixels({ 1, 1, 1, 0 }, data))
				Expect(pixel, {}, "A smooth fog gradient became shaft detail");
		}
	}

	void Verify(Fixture& fixture)
	{
		const float4 authored{ 2.0f, 0.6f, 0.2f, 0 };
		const float4 lens = fixture.lensFlare ? Fixture::flarePixels[0] : float4{};
		const auto addLens = [&](float4 color) { return float4{ color.x + lens.x, color.y + lens.y, color.z + lens.z, 0 }; };
		if (!fixture.volumetric) {
			for (float intensity : { 0.0f, 0.5f, 1.0f, 2.0f, 3.0f }) {
				Expect(fixture.Draw(authored, 0, { 0, 1, 0, 1 }, 0, false, intensity), lens, "Godray tuning affected lens flare");
				Expect(fixture.Draw(authored, 0, { 0, 1, 0, 1 }, 0, false, intensity, 1, 1), lens, "Clarity affected lens flare without volumetric lighting");
			}
			return;
		}
		Expect(fixture.Draw(authored), addLens({ 1, 0.3f, 0.1f, 0 }), "Neutral settings changed weather color");
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 0 }), addLens({ 1, 0.3f, 0.1f, 0 }), "Zero contribution changed weather color");
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 1 }), addLens({ 0, 0.5f, 0, 0 }), "Custom color did not reach final composite");
		Expect(fixture.Draw({ 65504, 65504, 65504, 0 }, 1, { 0.001f, 0.002f, 0.003f, 1 }),
			addLens({ 0.0005f, 0.001f, 0.0015f, 0 }), "Full custom replacement lost dark channels against HDR weather");
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 0.5f }), addLens({ 0.5f, 0.4f, 0.05f, 0 }), "Partial contribution failed");
		const float grey = (2.0f * 0.2125f + 0.6f * 0.7154f + 0.2f * 0.0721f) * 0.5f;
		Expect(fixture.Draw(authored, 0), addLens({ grey, grey, grey, 0 }), "Saturation zero did not desaturate shafts");
		for (float saturation : { 2.0f, 4.0f }) {
			const auto result = fixture.Draw(authored, saturation);
			const float red = result.x - lens.x, green = result.y - lens.y, blue = result.z - lens.z;
			Require(red > 1.0f && green < 0.3f && blue < 0.1f, "HDR saturation remained neutral");
			Require(std::abs(red * 0.2125f + green * 0.7154f + blue * 0.0721f - grey) < 2e-4f, "Saturation changed brightness");
		}
		for (const auto hdr : { float4{ 60000, 30000, 10000, 0 }, float4{ 65504, 65504, 65504, 0 } }) {
			const auto result = fixture.Draw(hdr, 4);
			const float red = result.x - lens.x, green = result.y - lens.y, blue = result.z - lens.z;
			const float expectedLuminance = (hdr.x * 0.2125f + hdr.y * 0.7154f + hdr.z * 0.0721f) * 0.5f;
			Require(std::abs(red * 0.2125f + green * 0.7154f + blue * 0.0721f - expectedLuminance) < expectedLuminance * 1e-5f, "HDR range limiting changed saturation brightness");
			Require(red <= 32752 && green <= 32752 && blue <= 32752, "Saturation exceeded half-float color range");
		}
		Expect(fixture.Draw(authored, 0, { 0, 1, 0, 1 }), addLens({ 0, 0.5f, 0, 0 }), "Saturation altered full custom replacement");
		Expect(fixture.Draw(authored, 4, { 0, 1, 0, 1 }, 0), lens, "Zero opacity retained shafts or removed flare");
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 1 }, 2), addLens({ 0, 0.75f, 0, 0 }), "Opacity curve changed");
		const float nan = std::numeric_limits<float>::quiet_NaN();
		Expect(fixture.Draw(authored, nan, { 0, 1, 0, nan }), addLens({ 1, 0.3f, 0.1f, 0 }), "Invalid tuning did not fall back to neutral");
		Expect(fixture.Draw(authored, 1, {}, nan), addLens({ 1, 0.3f, 0.1f, 0 }), "Invalid opacity did not fall back to neutral");
		Expect(fixture.Draw({ nan, -1, 0.2f, 0 }), addLens({ 0, 0, 0.1f, 0 }), "Neutral tuning retained invalid authored color");
		Expect(fixture.Draw({}, 4), lens, "Black authored color became nonfinite");
		Expect(fixture.Draw({ 65504, 65504, 65504, 0 }, 1, {}, 2, false, 3),
			{ 65504, 65504, 65504, 0 }, "Extreme shaft settings exceeded the HDR output range");
		const float4 linearLens = fixture.lensFlare ? float4{ std::pow(0.1f, 1.6f), std::pow(0.2f, 1.6f), std::pow(0.3f, 1.6f), 0 } : float4{};
		Expect(fixture.Draw(authored, 1, { 0, 1, 0, 1 }, 1, true), { linearLens.x, linearLens.y + 0.5f, linearLens.z, 0 }, "Linear lighting lost custom color or changed flare conversion");

		for (bool linear : { false, true }) {
			for (float intensity : { 0.0f, 0.5f, 1.0f, 2.0f, 3.0f }) {
				const float power = (linear ? 0.25f : 0.5f) * intensity;
				const float4 expectedLens = linear ? linearLens : lens;
				Expect(fixture.Draw(authored, 1, {}, 1, linear, intensity, 2),
					{ authored.x * power + expectedLens.x, authored.y * power + expectedLens.y, authored.z * power + expectedLens.z, 0 },
					"Godray intensity did not scale after gamma or changed lens flare");
			}
		}

		auto& feature = Runtime::globals::features::volumetricLighting;
		auto& state = Runtime::globals::replacementState;
		feature.settings = {};
		auto& balance = Runtime::globals::features::adaptiveBrightness;
		balance = {};
		feature.loaded = true;
		state = {};
		Runtime::globals::state = &state;
		Runtime::LocationContext::interior = false;
		Runtime::LocationContext::sun = false;
		const auto expectRuntime = [&](float4 expected, const char* message, bool inWorld = true) {
			feature.runtimeSettings = feature.settings;
			feature.initialised = true;
			feature.inInterior = Runtime::LocationContext::interior;
			feature.runtimeEnabled = feature.inInterior ? feature.settings.InteriorEnabled && Runtime::LocationContext::sun : feature.settings.ExteriorEnabled;
			Expect(fixture.Draw(authored, Runtime::Upload(inWorld)), addLens(expected), message);
		};
		const float4 neutral{ 1, 0.3f, 0.1f, 0 };
		balance.settings.godrays.exterior.ShaftIntensity = 2.0f;
		expectRuntime({ 2, 0.6f, 0.2f, 0 }, "Existing godray intensity did not reach composite");
		balance.volumetricClarity = 1.0f;
		expectRuntime({ 2, 0.6f, 0.2f, 0 }, "Clarity changed isolated shafts through runtime upload");
		balance.enabled = false;
		expectRuntime(neutral, "Disabled Adaptive Balance retained godray intensity");
		balance.enabled = true;
		expectRuntime(neutral, "Godray tuning affected a non-world pass", false);
		balance = {};
		feature.settings = {};

		balance.settings.godrays.exterior.Saturation = 0;
		expectRuntime({ grey, grey, grey, 0 }, "Exterior saturation did not reach composite");
		balance.settings.godrays.exterior = { .Opacity = 2, .CustomColorContribution = 1, .CustomColorRed = 0, .CustomColorBlue = 0 };
		expectRuntime({ 0, 0.75f, 0, 0 }, "Exterior color and opacity did not reach composite");
		const auto authoredProfile = balance.settings.godrays.exterior;
		balance.effective.appearance = { .godrayIntensity = 2.0f, .godrayOpacity = 0.5f, .godrayTint = { 0.0f, 0.0f, 1.0f }, .godrayTintAmount = 0.5f };
		expectRuntime({ 0, 0.5f, 0.5f, 0 }, "Adaptive Balance did not overlay standalone godray colour, intensity and opacity");
		Require(feature.GetRuntimeGodrayProfile().ShaftIntensity == 2.0f, "Adaptive Balance shaft intensity did not compose");
		Require(balance.settings.godrays.exterior == authoredProfile, "Adaptive Balance changed authored godray settings");
		balance.enabled = false;
		expectRuntime(neutral, "Disabled Adaptive Balance retained context godray tuning");
		balance.enabled = true;
		feature.settings.ExteriorEnabled = false;
		expectRuntime(neutral, "Adaptive Balance re-enabled disabled godray tuning");
		feature.settings.ExteriorEnabled = true;
		expectRuntime(neutral, "Adaptive Balance godray appearance affected a non-world pass", false);
		balance = {};
		balance.settings.godrays.interior = { .CustomColorContribution = 1, .CustomColorGreen = 0, .CustomColorBlue = 0 };
		Runtime::LocationContext::interior = true;
		expectRuntime(neutral, "Sunless interior retained exterior tuning");
		Runtime::LocationContext::sun = true;
		expectRuntime({ 0.5f, 0, 0, 0 }, "Sunlit interior did not select its own profile");
		feature.settings.InteriorEnabled = false;
		expectRuntime(neutral, "Disabled interior retained tuning");
		Runtime::LocationContext::interior = false;
		feature.settings.ExteriorEnabled = false;
		expectRuntime(neutral, "Disabled exterior retained tuning");
		feature.settings.ExteriorEnabled = true;
		feature.loaded = false;
		expectRuntime(neutral, "Unloaded feature retained tuning");
		feature.loaded = true;
		state.enablePShaders = false;
		expectRuntime(neutral, "Disabled pixel replacement retained tuning");
		state.enablePShaders = true;
		state.enabledClasses[0] = false;
		expectRuntime(neutral, "Disabled ImageSpace class retained tuning");
		state.enabledClasses[0] = true;
		Runtime::globals::state = nullptr;
		expectRuntime(neutral, "Missing renderer state retained tuning");
		Runtime::globals::state = &state;
		expectRuntime(neutral, "Non-world pass retained tuning", false);
		balance.settings.godrays.exterior = { .CustomColorContribution = 1, .CustomColorRed = 0, .CustomColorBlue = 0 };
		balance.settings.godrays.exterior.Opacity = nan;
		balance.settings.godrays.exterior.CustomColorGreen = nan;
		expectRuntime({ 0, 0.5f, 0, 0 }, "Profile sanitization did not reach composite");
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		for (bool vr : { false, true }) {
			VerifyClarity(device.Get(), context.Get(), vr);
			VerifyClarityFiltering(device.Get(), context.Get(), vr);
			VerifySeparation(device.Get(), context.Get(), vr);
			VerifySilhouettes(device.Get(), context.Get(), vr);
			std::cout << (vr ? "VR" : "SE/AE") << " separation: independent controls, moving silhouettes, fog gradients, gamma, dynamic bounds and depth fallback passed\n";
			for (const auto mode : { std::array{ true, false }, std::array{ false, true }, std::array{ true, true } }) {
				CompileCompositeShader(vr, mode[0], mode[1], true);
				Fixture fixture(device.Get(), context.Get(), vr, mode[0], mode[1]);
				Verify(fixture);
				std::cout << (vr ? "VR" : "SE/AE") << " volumetric=" << mode[0] << " flare=" << mode[1] << " passed\n";
			}
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
