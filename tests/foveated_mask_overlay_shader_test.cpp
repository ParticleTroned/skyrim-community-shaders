#define NOMINMAX
#include "Features/Upscaling/FoveatedMaskVisualization.h"
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	using Pair = std::array<float, 2>;
	constexpr UINT kWidth = 512, kHeight = 384;
	constexpr Pixel kScene{ 0.2f, 0.4f, 0.6f, 0.7f };
	constexpr Pixel kYellow{ .55f, .5f, 0, 1 };

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct ShaderIncludes : ID3DInclude
	{
		Util::CustomInclude package{ "package/Shaders" };
		Util::CustomInclude feature{ "features/Upscaling/Shaders" };
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			return std::string_view(name).starts_with("Upscaling/") ?
			           feature.Open(type, name, parent, data, size) :
			           package.Open(type, name, parent, data, size);
		}
		HRESULT Close(LPCVOID data) override { return package.Close(data); }
	};

	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Texture2D> input, output, staging;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		ComPtr<ID3D11SamplerState> sampler;

		Fixture()
		{
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
				device.GetAddressOf(), nullptr, context.GetAddressOf()));
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = kWidth;
			desc.Height = kHeight;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			std::vector<Pixel> scene(kWidth * kHeight, Pixel{ 8, 0, 8, 0 });
			for (UINT y = 0; y < kHeight / 2; ++y)
				for (UINT x = 0; x < kWidth / 2; ++x)
					scene[y * kWidth + x] = kScene;
			D3D11_SUBRESOURCE_DATA data{ scene.data(), kWidth * sizeof(Pixel), 0 };
			Check(device->CreateTexture2D(&desc, &data, input.GetAddressOf()));
			Util::SetResourceName(input.Get(), "FovOverlayTest::Scene");
			Check(device->CreateShaderResourceView(input.Get(), nullptr, srv.GetAddressOf()));
			Util::SetResourceName(srv.Get(), "FovOverlayTest::Scene SRV");
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture2D(&desc, nullptr, output.GetAddressOf()));
			Util::SetResourceName(output.Get(), "FovOverlayTest::Output");
			Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
			Util::SetResourceName(uav.Get(), "FovOverlayTest::Output UAV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "FovOverlayTest::Readback");
			D3D11_SAMPLER_DESC sd{};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&sd, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "FovOverlayTest::Sampler");
		}

		std::vector<Pixel> Draw(ID3D11ComputeShader* shader, ConstantBuffer& cb)
		{
			context->ClearState();
			cb.Bind(context.Get());
			context->CSSetShader(shader, nullptr, 0);
			ID3D11ShaderResourceView* source = srv.Get();
			ID3D11UnorderedAccessView* target = uav.Get();
			ID3D11SamplerState* sample = sampler.Get();
			context->CSSetShaderResources(0, 1, &source);
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetSamplers(0, 1, &sample);
			context->Dispatch(kWidth / 8, kHeight / 8, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<Pixel> result(kWidth * kHeight);
			for (UINT y = 0; y < kHeight; ++y)
				std::memcpy(result.data() + y * kWidth, static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch, kWidth * sizeof(Pixel));
			context->Unmap(staging.Get(), 0);
			return result;
		}
	};

	void CheckPreview(Fixture& f, bool vr)
	{
		ShaderIncludes includes;
		const D3D_SHADER_MACRO defines[]{ { "VR", "1" }, { nullptr, nullptr } };
		ComPtr<ID3DBlob> code, errors;
		auto result = D3DCompileFromFile(L"features/Upscaling/Shaders/Upscaling/FoveatedPeripheryCS.hlsl",
			vr ? defines : defines + 1, &includes, "main", "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, code.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		ComPtr<ID3D11ComputeShader> shader;
		Check(f.device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "FovOverlayTest::CS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer cb(f.device.Get(), reflection.Get(), "FoveatedPeripheryCB");
		Require(cb.bytes.size() == 192 && cb.Offset("Preview") == 96 && cb.Offset("PreviewClipToOtherEye") == 112 && cb.Offset("PreviewArea") == 176,
			"CPU/HLSL preview constant buffer layout mismatch");
		cb.SetVariable("OutputDim", Pair{ kWidth, kHeight });
		cb.SetVariable("InvOutputDim", Pair{ 1.0f / kWidth, 1.0f / kHeight });
		cb.SetVariable("InvSourceDim", Pair{ 1.0f / kWidth, 1.0f / kHeight });
		cb.SetVariable("SourceScale", Pair{ 0.5f, 0.5f });
		cb.SetVariable("DispatchDim", Pair{ kWidth, kHeight });
		const std::array<float, 16> identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		cb.SetVariable("PreviewClipToOtherEye", identity);
		cb.SetVariable("Tuning0", Pixel{ 0.5f, 0.05f, 1, 0.8f });
		for (const auto& p : f.Draw(shader.Get(), cb))
			for (size_t c = 0; c < p.size(); ++c)
				Require(std::abs(p[c] - kScene[c]) < 1e-5f, "Ordinary sampling must remain unchanged and avoid unused allocation pixels");

		unsigned seenOverlap = 0, seenTaa = 0, seenInk = 0;
		for (const auto geometry : { Pixel{ 0.25f, 0.0f, 1, 0.8f }, Pixel{ 0.55f, 0.05f, 1.5f, 0.9f },
				 Pixel{ 0.9f, 0.1f, 2, 1 }, Pixel{ 0.998f, 0.1f, 1, 1 }, Pixel{ 0.999f, 0.05f, 1, 1 }, Pixel{ 1, 0.1f, 2, 1 } }) {
			for (bool taa : { false, true }) {
				for (unsigned eye = 0; eye < 2; ++eye) {
					const float ox = eye ? 0.15f : -0.15f, oy = 0.12f;
					const auto coverage = FoveatedMaskVisualization::MeasureCoverage(geometry[0], geometry[1], geometry[2], ox, oy, taa, geometry[3]);
					Require(coverage.savedPercent >= 0 && coverage.savedPercent <= 100 && std::abs(coverage.totalPercent + coverage.savedPercent - 100) < 1e-4f,
						"Coverage must be clipped and counted once");
					const bool full = !FoveatedCommon::IsActiveCoverage(geometry[0]);
					if (full)
						Require(coverage.savedPercent == 0, "Full CSX FOV must save zero area");
					cb.SetVariable("Tuning0", geometry);
					cb.SetVariable("CenterAndMask", Pixel{ ox, oy, 1, taa ? 1.0f : 0.0f });
					cb.SetVariable("Preview", Pixel{ ox, oy, float(eye), full ? 1.0f : 0.0f });
					cb.SetVariable("PreviewArea", Pixel{ coverage.savedPercent, 0, 0, 0 });
					const auto pixels = f.Draw(shader.Get(), cb);
					unsigned uncovered = 0;
					for (UINT y = 0; y < kHeight; ++y) {
						for (UINT x = 0; x < kWidth; ++x) {
							const auto& p = pixels[y * kWidth + x];
							const float u = (x + 0.5f) / kWidth, v = (y + 0.5f) / kHeight;
							const float support = 1 + 2 * std::max(geometry[1], 1e-4f) / geometry[0];
							const bool center = full || FoveatedCommon::MaskDistanceUV(u, v, geometry[0], geometry[2], ox, oy) <= support;
							const float outer = std::min(1.0f, std::max(geometry[3], geometry[0] + 2 * std::max(geometry[1], 1e-4f)));
							const bool inTaa = taa && FoveatedCommon::MaskDistanceUV(u, v, outer, geometry[2], ox, oy) <= 1;
							if (!center && !inTaa) {
								Require(p == kYellow, "Every own-eye gap must stay opaque yellow, regardless of peer mask or readout");
								++uncovered;
								continue;
							}
							Require(std::abs(p[3] - kScene[3]) < 1e-5f, "Covered preview must preserve scene alpha");
							if (std::abs(u - (full ? 0.5f : 0.5f + ox)) < 0.065f && std::abs(v - (full ? 0.5f : 0.5f + oy)) < 0.04f) {
								seenInk += p[0] > .8f && p[1] > .8f && p[2] > .8f;
								continue;
							}
							const bool other = full || FoveatedCommon::MaskDistanceUV(u, v, geometry[0], geometry[2], ox, oy) <= support;
							const Pixel tint = !center ? Pixel{ .04f, .04f, .04f, 0 } : other ? Pixel{ 1, 1, 1, 0 } :
							                                                        eye       ? Pixel{ .1f, 1, .1f, 0 } :
							                                                                    Pixel{ 1, .1f, 1, 0 };
							for (unsigned c = 0; c < 3; ++c)
								Require(std::abs(p[c] - (kScene[c] * .65f + tint[c] * .35f)) < 2e-5f, "Preview must keep scene detail under the correct zone tint");
							seenTaa += !center;
							seenOverlap += center && other;
						}
					}
					Require(std::abs(100.0f * uncovered / (kWidth * kHeight) - coverage.savedPercent) < .12f,
						"Reported area must match rendered masks including feather overflow and clipping");
				}
			}
		}
		Require(seenOverlap && seenTaa && seenInk, "Fixtures must exercise overlap, TAA and readable lettering");

		// Partially overlapping masks must not let binocular fusion hide either uncovered edge.
		for (unsigned eye = 0; eye < 2; ++eye) {
			for (bool taa : { false, true }) {
				cb.SetVariable("Tuning0", Pixel{ .3f, .05f, 1, .65f });
				cb.SetVariable("CenterAndMask", Pixel{ -.2f, 0, 1, taa ? 1.0f : 0.0f });
				cb.SetVariable("Preview", Pixel{ .2f, 0, float(eye), 0 });
				const auto pixels = f.Draw(shader.Get(), cb);
				Require(pixels[(kHeight / 2) * kWidth + kWidth / 4] == kYellow,
					"A covered own-eye region with a peer gap must be yellow in both modes");
			}
			auto monocular = identity;
			monocular[3] = 4;
			cb.SetVariable("PreviewClipToOtherEye", monocular);
			const auto pixels = f.Draw(shader.Get(), cb);
			const auto& p = pixels[(kHeight / 2) * kWidth + kWidth / 4 - 16];
			const Pixel tint = eye ? Pixel{ .1f, 1, .1f, 0 } : Pixel{ 1, .1f, 1, 0 };
			for (unsigned c = 0; c < 3; ++c)
				Require(std::abs(p[c] - (kScene[c] * .65f + tint[c] * .35f)) < 2e-5f,
					"Monocular edges must retain own-eye tint and coverage");
			cb.SetVariable("PreviewClipToOtherEye", identity);
		}
		cb.SetVariable("Tuning0", Pixel{ .5f, .05f, 1, .8f });
		cb.SetVariable("CenterAndMask", Pixel{ 0, 0, 1, 0 });
		cb.SetVariable("Preview", Pixel{ 0, 0, 0, 0 });
		auto asymmetric = identity;
		asymmetric[0] = 1.2f;
		asymmetric[3] = 0.6f;
		asymmetric[12] = 0.2f;
		cb.SetVariable("PreviewClipToOtherEye", asymmetric);
		const auto shifted = f.Draw(shader.Get(), cb);
		for (UINT x : { 180u, 350u }) {
			const float clipX = (x + .5f) / kWidth * 2 - 1;
			const float peerU = (1.2f * clipX + .6f) / (1 + .2f * clipX) * .5f + .5f;
			const bool peerCovered = FoveatedCommon::MaskDistanceUV(peerU, .5f, .5f, 1, 0, 0) <= 1.2f;
			Require((shifted[(kHeight / 2) * kWidth + x] == kYellow) == !peerCovered,
				"Shared-view coverage must use homogeneous peer projection, not equal eye UVs");
		}
		cb.SetVariable("PreviewClipToOtherEye", std::array<float, 16>{});
		for (const auto& pixel : f.Draw(shader.Get(), cb))
			Require(pixel == kYellow, "An uninitialized projection must not certify coverage");
		for (float peerW : { -1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN() }) {
			auto invalid = identity;
			invalid[15] = peerW;
			cb.SetVariable("PreviewClipToOtherEye", invalid);
			const auto pixels = f.Draw(shader.Get(), cb);
			const auto& p = pixels[(kHeight / 2) * kWidth + kWidth / 2 + 60];
			if (!std::isfinite(peerW)) {
				Require(p == kYellow, "Unknown stereo mapping must not falsely certify shared coverage");
				continue;
			}
			Require(std::abs(p[0] - .48f) < 1e-5f && std::abs(p[1] - .295f) < 1e-5f && pixels.front() == kYellow,
				"Invalid peer projection must disable overlap without changing this eye's coverage");
		}
	}
}

int main()
{
	try {
		Fixture fixture;
		CheckPreview(fixture, false);
		CheckPreview(fixture, true);
		std::cout << "PASS: production VR/flat overlay, tints, own-eye gaps, readout, feather area, full coverage and source bounds\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
