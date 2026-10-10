#include <DirectXMath.h>
#define NOMINMAX
#include "Features/Upscaling/FoveatedMaskCalibration.h"
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
	bool IsHatch(const Pixel& pixel, UINT x, UINT y)
	{
		const float width = std::max(6.0f, std::min(kWidth, kHeight) / 100.0f);
		const bool stripe = static_cast<unsigned>(std::floor((x + y) / width)) % 2 == 0;
		const Pixel tint = stripe ? Pixel{ .65f, .5f, .05f, 0 } : Pixel{ .08f, .06f, .02f, 0 };
		for (unsigned channel = 0; channel < 3; ++channel)
			if (std::abs(pixel[channel] - (kScene[channel] * .2f + tint[channel] * .8f)) > 2e-5f)
				return false;
		return std::abs(pixel[3] - kScene[3]) < 1e-5f;
	}

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

	struct ComputeShader
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;

		ComputeShader(ID3D11Device* device, const wchar_t* path, bool vr, const char* resourceName)
		{
			ShaderIncludes includes;
			const D3D_SHADER_MACRO defines[]{ { "VR", "1" }, { nullptr, nullptr } };
			ComPtr<ID3DBlob> code, errors;
			const auto result = D3DCompileFromFile(path, vr ? defines : defines + 1, &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, code.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(result);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "%s", resourceName);
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		}
	};

	void CheckCalibratedBlend(Fixture& f, bool vr)
	{
		using namespace FoveatedMaskCalibration;
		ComputeShader program(f.device.Get(), L"features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl", vr,
			"FovCalibrationTest::Blend CS");
		ConstantBuffer cb(f.device.Get(), program.reflection.Get(), "FoveatedCenterBlendCB");
		cb.SetVariable("InvOutputDim", Pair{ 1.0f / kWidth, 1.0f / kHeight });
		cb.SetVariable("InvSourceDim", Pair{ 1.0f / kWidth, 1.0f / kHeight });
		cb.SetVariable("DispatchDim", Pair{ kWidth, kHeight });
		std::vector<Pixel> scene(kWidth * kHeight, kScene);
		f.context->UpdateSubresource(f.input.Get(), 0, nullptr, scene.data(), kWidth * sizeof(Pixel), 0);
		for (const Projection projection : { Projection{ 1, 0, 0, 0, 1, 0, 0, 0, 1 },
				 Projection{ .93, .04, .12, -.02, 1.03, .015, .08, .01, 1 } }) {
			for (const float percent : { 70.0f, 100.0f, 130.0f }) {
				for (const float feather : { 0.0001f, 0.05f, 0.1f }) {
					const Reference reference{ .version = 1,
						.outer = { .scale = 0.6f, .horizontalScale = 1.2f, .centers = { .4f, .48f, .6f, .52f } },
						.leftToRight = projection,
						.feather = feather };
					const auto solution = Solve(reference, percent);
					const auto targets = BuildTargets(reference, percent);
					Require(solution && targets && !solution->fullImage, "Calibration regression must exercise a bounded fit, not full-image fallback");
					const auto& g = solution->geometry;
					cb.SetVariable("CenterScale", g.scale);
					cb.SetVariable("CenterFeather", feather);
					cb.SetVariable("CenterHorizontalScale", g.horizontalScale);
					cb.SetVariable("FullImage", solution->fullImage ? 1u : 0u);
					for (unsigned eye = 0; eye < 2; ++eye) {
						cb.SetVariable("CenterOffset", Pair{ g.centers[eye * 2] - .5f, g.centers[eye * 2 + 1] - .5f });
						const auto& target = (*targets)[eye];
						std::vector<size_t> required;
						for (UINT y = 0; y < kHeight; ++y) {
							for (UINT x = 0; x < kWidth; ++x) {
								const Point p{ (x + .5) / kWidth, (y + .5) / kHeight };
								bool inside = true;
								for (size_t i = 0; i < target.size(); ++i) {
									const auto a = target[i], b = target[(i + 1) % target.size()];
									inside &= (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) >= 0;
								}
								if (inside)
									required.push_back(y * kWidth + x);
							}
						}
						Require(required.size() > kWidth * kHeight / 10, "Calibration fixture must sample the filled centre and outer contours");
						for (float falloff : { .5f, 1.0f, 2.0f }) {
							cb.SetVariable("BlendFalloff", falloff);
							for (const Pixel background : { Pixel{ .9f, .1f, .8f, .2f }, Pixel{ .0f, .9f, .1f, .9f } }) {
								f.context->ClearUnorderedAccessViewFloat(f.uav.Get(), background.data());
								const auto pixels = f.Draw(program.shader.Get(), cb);
								for (const auto index : required)
									for (unsigned channel = 0; channel < 4; ++channel)
										Require(std::abs(pixels[index][channel] - kScene[channel]) < 1e-5f,
											"Calibrated coverage must not flicker when untreated background changes under the production blend");
							}
						}
					}
				}
			}
		}
	}

	void CheckPreview(Fixture& f, bool vr)
	{
		ComputeShader program(f.device.Get(), L"features/Upscaling/Shaders/Upscaling/FoveatedPeripheryCS.hlsl", vr,
			"FovOverlayTest::CS");
		ConstantBuffer cb(f.device.Get(), program.reflection.Get(), "FoveatedPeripheryCB");
		Require(cb.bytes.size() == 192 && cb.Offset("Preview") == 96 && cb.Offset("PreviewClipToOtherEye") == 112 && cb.Offset("PreviewArea") == 176,
			"CPU/HLSL preview constant buffer layout mismatch");
		cb.SetVariable("OutputDim", Pair{ kWidth, kHeight });
		cb.SetVariable("InvOutputDim", Pair{ 1.0f / kWidth, 1.0f / kHeight });
		cb.SetVariable("InvSourceDim", Pair{ 1.0f / kWidth, 1.0f / kHeight });
		cb.SetVariable("SourceScale", Pair{ 0.5f, 0.5f });
		cb.SetVariable("DispatchDim", Pair{ kWidth, kHeight });
		const std::array<float, 16> identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		const auto setProjection = [&](const std::array<float, 16>& values) {
			DirectX::XMFLOAT4X4 matrix;
			std::memcpy(&matrix, values.data(), sizeof(matrix));
			cb.SetVariable("PreviewClipToOtherEye", values);
			cb.SetVariable("PreviewArea", Pixel{ 0, FoveatedMaskCalibration::Inverse(FoveatedMaskCalibration::FromClipProjection(matrix)).has_value() ? 1.0f : 0.0f, 0, 0 });
		};
		setProjection(identity);
		cb.SetVariable("Tuning0", Pixel{ 0.5f, 0.05f, 1, 0.8f });
		for (const auto& p : f.Draw(program.shader.Get(), cb))
			for (size_t c = 0; c < p.size(); ++c)
				Require(std::abs(p[c] - kScene[c]) < 1e-5f, "Ordinary sampling must remain unchanged and avoid unused allocation pixels");

		unsigned seenOverlap = 0, seenTaa = 0, seenInk = 0, seenOutline = 0;
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
					cb.SetVariable("PreviewArea", Pixel{ coverage.savedPercent, 1, 0, 0 });
					const auto pixels = f.Draw(program.shader.Get(), cb);
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
								Require(IsHatch(p, x, y), "Every gap must retain static 80-percent hatching and scene context, without readout occlusion");
								++uncovered;
								continue;
							}
							Require(std::abs(p[3] - kScene[3]) < 1e-5f, "Covered preview must preserve scene alpha");
							if (std::abs(u - (full ? 0.5f : 0.5f + ox)) < 0.065f && std::abs(v - (full ? 0.5f : 0.5f + oy)) < 0.04f) {
								seenInk += p[0] > .8f && p[1] > .8f && p[2] > .8f;
								continue;
							}
							if ((std::abs(p[0] - .015f) < 1e-5f && std::abs(p[1] - .015f) < 1e-5f && std::abs(p[2] - .015f) < 1e-5f) ||
								(std::abs(p[0] - (eye ? .1f : 1.0f)) < 1e-5f && std::abs(p[1] - (eye ? 1.0f : .1f)) < 1e-5f && std::abs(p[2] - (eye ? .1f : 1.0f)) < 1e-5f)) {
								Require(!full, "Full coverage must not draw an internal mask edge");
								++seenOutline;
								continue;
							}
							const bool other = full || FoveatedCommon::MaskDistanceUV(u, v, geometry[0], geometry[2], ox, oy) <= support;
							const Pixel tint = !center ? Pixel{ .04f, .04f, .04f, 0 } : other ? Pixel{ 1, 1, 1, 0 } :
							                                                        eye       ? Pixel{ .1f, 1, .1f, 0 } :
							                                                                    Pixel{ 1, .1f, 1, 0 };
							for (unsigned c = 0; c < 3; ++c)
								Require(std::abs(p[c] - (kScene[c] * .82f + tint[c] * .18f)) < 2e-5f, "Preview must keep scene detail under the correct zone tint");
							seenTaa += !center;
							seenOverlap += center && other;
						}
					}
					Require(std::abs(100.0f * uncovered / (kWidth * kHeight) - coverage.savedPercent) < .12f,
						"Reported area must match rendered masks including feather overflow and clipping");
				}
			}
		}
		Require(seenOverlap && seenTaa && seenInk && seenOutline, "Fixtures must exercise overlap, TAA and readable lettering");

		// Partially overlapping masks must not let binocular fusion hide either uncovered edge.
		for (unsigned eye = 0; eye < 2; ++eye) {
			for (bool taa : { false, true }) {
				cb.SetVariable("Tuning0", Pixel{ .3f, .05f, 1, .65f });
				cb.SetVariable("CenterAndMask", Pixel{ -.2f, 0, 1, taa ? 1.0f : 0.0f });
				cb.SetVariable("Preview", Pixel{ .2f, 0, float(eye), 0 });
				const auto pixels = f.Draw(program.shader.Get(), cb);
				Require(IsHatch(pixels[(kHeight / 2) * kWidth + kWidth / 4], kWidth / 4, kHeight / 2),
					"A covered own-eye region with a peer gap must be hatched in both modes");
				if (!taa) {
					const auto& edge = pixels[(kHeight / 2) * kWidth + static_cast<UINT>(kWidth * .1f + 1)];
					Require(std::abs(edge[0] - (eye ? .1f : 1.0f)) < 1e-5f &&
								std::abs(edge[1] - (eye ? 1.0f : .1f)) < 1e-5f,
						"The outer outline must remain visible across a peer gap during manual setup");
				}
			}
			auto monocular = identity;
			monocular[3] = 4;
			setProjection(monocular);
			const auto pixels = f.Draw(program.shader.Get(), cb);
			const auto& p = pixels[(kHeight / 2) * kWidth + kWidth / 4 - 16];
			const Pixel tint = eye ? Pixel{ .1f, 1, .1f, 0 } : Pixel{ 1, .1f, 1, 0 };
			for (unsigned c = 0; c < 3; ++c)
				Require(std::abs(p[c] - (kScene[c] * .82f + tint[c] * .18f)) < 2e-5f,
					"Monocular edges must retain own-eye tint and coverage");
			setProjection(identity);
		}
		cb.SetVariable("Tuning0", Pixel{ .5f, .05f, 1, .8f });
		cb.SetVariable("CenterAndMask", Pixel{ 0, 0, 1, 0 });
		cb.SetVariable("Preview", Pixel{ 0, 0, 0, 0 });
		auto asymmetric = identity;
		asymmetric[0] = 1.2f;
		asymmetric[3] = 0.6f;
		asymmetric[12] = 0.2f;
		setProjection(asymmetric);
		const auto shifted = f.Draw(program.shader.Get(), cb);
		for (UINT x : { 180u, 350u }) {
			const float clipX = (x + .5f) / kWidth * 2 - 1;
			const float peerU = (1.2f * clipX + .6f) / (1 + .2f * clipX) * .5f + .5f;
			const bool peerCovered = FoveatedCommon::MaskDistanceUV(peerU, .5f, .5f, 1, 0, 0) <= 1.2f;
			Require(IsHatch(shifted[(kHeight / 2) * kWidth + x], x, kHeight / 2) == !peerCovered,
				"Shared-view coverage must use homogeneous peer projection, not equal eye UVs");
		}
		auto singular = identity;
		singular[0] = singular[5] = 0;
		setProjection(singular);
		const auto collapsed = f.Draw(program.shader.Get(), cb);
		Require(IsHatch(collapsed[(kHeight / 2) * kWidth + kWidth / 2], kWidth / 2, kHeight / 2),
			"A finite singular projection must not certify shared coverage");
		setProjection(std::array<float, 16>{});
		const auto unknown = f.Draw(program.shader.Get(), cb);
		for (UINT y = 0; y < kHeight; ++y)
			for (UINT x = 0; x < kWidth; ++x)
				Require(IsHatch(unknown[y * kWidth + x], x, y), "An uninitialized projection must not certify coverage");
		for (float peerW : { -1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN() }) {
			auto invalid = identity;
			invalid[15] = peerW;
			setProjection(invalid);
			const auto pixels = f.Draw(program.shader.Get(), cb);
			const auto& p = pixels[(kHeight / 2) * kWidth + kWidth / 2 + 60];
			if (!std::isfinite(peerW) || peerW == 0.0f) {
				Require(IsHatch(p, kWidth / 2 + 60, kHeight / 2), "Unknown stereo mapping must not falsely certify shared coverage");
				continue;
			}
			Require(std::abs(p[0] - .344f) < 1e-5f && std::abs(p[1] - .346f) < 1e-5f && IsHatch(pixels.front(), 0, 0),
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
		CheckCalibratedBlend(fixture, false);
		CheckCalibratedBlend(fixture, true);
		std::cout << "PASS: production VR/flat overlay, tints, own-eye gaps, readout, feather area, full coverage, source bounds and calibrated blend stability\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
