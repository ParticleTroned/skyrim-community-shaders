#define NOMINMAX
#include "Features/VR/MenuOverlayGeometry.h"
#include "Features/VR/MenuOverlayShader.h"
#include "d3d11_shader_test.h"

#include <d3dcompiler.h>
#include <iostream>
#include <memory>

namespace
{
	using D3D11ShaderTest::Check;
	using namespace MenuOverlayGeometry;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	constexpr UINT Width = 128, Height = 96, MenuSize = 64;
	constexpr double Near = 0.1, Far = 1000.0, HalfWidth = 1.4, HalfHeight = 0.8;
	constexpr Pixel Scene{ .1f, .2f, .3f, .7f };

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	std::array<Vertex, 4> Quad(double angle, double depth, double roll = 0, double projectionOffset = 0)
	{
		std::array<Vertex, 4> result{};
		const std::array<std::array<double, 2>, 4> uv{ { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } } };
		for (std::size_t i = 0; i < result.size(); ++i) {
			const double x = (uv[i][0] * 2 - 1) * HalfWidth;
			const double y = (1 - uv[i][1] * 2) * HalfHeight;
			const double w = std::sin(angle) * x + std::cos(angle) * depth;
			const double eyeX = std::cos(angle) * x - std::sin(angle) * depth;
			result[i] = { std::cos(roll) * eyeX - std::sin(roll) * y + projectionOffset * w,
				std::sin(roll) * eyeX + std::cos(roll) * y,
				Far / (Far - Near) * (w - Near), w, uv[i][0], uv[i][1] };
		}
		return result;
	}

	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Texture2D> menu, output, staging;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		ComPtr<ID3D11SamplerState> sampler;
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		std::unique_ptr<D3D11ShaderTest::ConstantBuffer> constants;

		Fixture()
		{
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
				device.GetAddressOf(), nullptr, context.GetAddressOf()));
			ComPtr<ID3DBlob> code, errors;
			const auto compiled = D3DCompile(ShaderSource, sizeof(ShaderSource) - 1, "VRMenuOverlay", nullptr, nullptr,
				"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, code.GetAddressOf(), errors.GetAddressOf());
			if (FAILED(compiled) && errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiled);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "MenuOverlayTest::Shader");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			constants = std::make_unique<D3D11ShaderTest::ConstantBuffer>(device.Get(), reflection.Get(), "OverlayCompositeCB");
			Require(constants->bytes.size() == sizeof(Constants) &&
						constants->Offset("TargetSize") == offsetof(Constants, targetSize) &&
						constants->Offset("DispatchOrigin") == offsetof(Constants, dispatchOrigin) &&
						constants->Offset("DispatchSize") == offsetof(Constants, dispatchSize) &&
						constants->Offset("VertexCount") == offsetof(Constants, vertexCount) &&
						constants->Offset("PolygonPixels") == offsetof(Constants, polygonPixels) &&
						constants->Offset("PolygonUV") == offsetof(Constants, polygonUV) &&
						constants->Offset("MenuMipLevel") == offsetof(Constants, menuMipLevel),
				"CPU and shader constant layouts must agree");
			for (const char* name : { "PolygonPixels", "PolygonUV" }) {
				D3D11_SHADER_TYPE_DESC array{};
				Check(constants->reflection->GetVariableByName(name)->GetType()->GetDesc(&array));
				Require(array.Elements == MaxVertices, "CPU and shader polygon capacities must agree");
			}
			std::array<std::vector<Pixel>, 3> pixels;
			std::array<D3D11_SUBRESOURCE_DATA, 3> initial{};
			for (UINT mip = 0; mip < pixels.size(); ++mip) {
				const UINT size = MenuSize >> mip;
				pixels[mip].resize(size * size);
				for (UINT y = 0; y < size; ++y)
					for (UINT x = 0; x < size; ++x)
						pixels[mip][y * size + x] = { (x + .5f) / size, (y + .5f) / size, .75f - .25f * mip, .6f };
				initial[mip] = { pixels[mip].data(), size * sizeof(Pixel), 0 };
			}
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = MenuSize;
			desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.MipLevels = 3;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			Check(device->CreateTexture2D(&desc, initial.data(), menu.GetAddressOf()));
			Util::SetResourceName(menu.Get(), "MenuOverlayTest::Menu");
			Check(device->CreateShaderResourceView(menu.Get(), nullptr, srv.GetAddressOf()));
			Util::SetResourceName(srv.Get(), "MenuOverlayTest::Menu SRV");
			desc.MipLevels = 1;
			desc.Width = Width;
			desc.Height = Height;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture2D(&desc, nullptr, output.GetAddressOf()));
			Util::SetResourceName(output.Get(), "MenuOverlayTest::Output");
			Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
			Util::SetResourceName(uav.Get(), "MenuOverlayTest::Output UAV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "MenuOverlayTest::Readback");
			D3D11_SAMPLER_DESC sampling{};
			sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampling.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&sampling, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "MenuOverlayTest::Sampler");
		}

		void Run(double angle, double depth, const Viewport& viewport, double roll = 0, double projectionOffset = 0, float mipLevel = 0)
		{
			Constants cb{};
			float pixelsPerUVArea;
			const bool visible = Project(Quad(angle, depth, roll, projectionOffset), viewport, Width, Height, cb, pixelsPerUVArea);
			context->ClearUnorderedAccessViewFloat(uav.Get(), Scene.data());
			if (visible) {
				Require(cb.vertexCount >= 3 && cb.vertexCount <= MaxVertices && pixelsPerUVArea > 0, "finite clipped polygon");
				Require(cb.dispatchOrigin[0] + .5 >= viewport.x && cb.dispatchOrigin[0] + cb.dispatchSize[0] - .5 < viewport.x + viewport.width,
					"dispatch must remain within its stereo eye");
				cb.menuMipLevel = mipLevel;
				std::memcpy(constants->bytes.data(), &cb, sizeof(cb));
				constants->Bind(context.Get());
				context->CSSetShader(shader.Get(), nullptr, 0);
				context->CSSetShaderResources(0, 1, srv.GetAddressOf());
				context->CSSetSamplers(0, 1, sampler.GetAddressOf());
				context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
				context->Dispatch((cb.dispatchSize[0] + 7) / 8, (cb.dispatchSize[1] + 7) / 8, 1);
				context->ClearState();
			}
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			bool matches = true;
			for (UINT y = 0; y < Height; ++y) {
				const auto* row = reinterpret_cast<const Pixel*>(static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch);
				for (UINT x = 0; x < Width; ++x) {
					Pixel expected = Scene;
					const double projectedX = ((x + .5 - viewport.x) / viewport.width) * 2 - 1 - projectionOffset;
					const double projectedY = 1 - ((y + .5 - viewport.y) / viewport.height) * 2;
					const double rayX = std::cos(roll) * projectedX + std::sin(roll) * projectedY;
					const double rayY = -std::sin(roll) * projectedX + std::cos(roll) * projectedY;
					// Intersect an eye ray with the original world-space plane, independently of polygon clipping.
					const double t = depth / (std::cos(angle) - std::sin(angle) * rayX);
					const double u = ((std::cos(angle) * rayX + std::sin(angle)) * t / HalfWidth + 1) * .5;
					const double v = (1 - rayY * t / HalfHeight) * .5;
					if (std::abs(u) < 1e-6 || std::abs(u - 1) < 1e-6 || std::abs(v) < 1e-6 || std::abs(v - 1) < 1e-6 ||
						std::abs(t - Near) < 1e-6 || std::abs(t - Far) < 1e-6)
						continue;
					if (x + .5 >= viewport.x && x + .5 < viewport.x + viewport.width &&
						y + .5 >= viewport.y && y + .5 < viewport.y + viewport.height &&
						t >= Near && t <= Far && u >= 0 && u <= 1 && v >= 0 && v <= 1) {
						const auto sample = [mipLevel](double uv) {
							const auto lower = unsigned(std::floor(mipLevel));
							const auto upper = std::min(lower + 1, 2u);
							const double lo = .5 / (MenuSize >> lower), hi = .5 / (MenuSize >> upper);
							return float(std::lerp(std::clamp(uv, lo, 1 - lo), std::clamp(uv, hi, 1 - hi), double(mipLevel - lower)));
						};
						expected[0] = Scene[0] * .4f + sample(u) * .6f;
						expected[1] = Scene[1] * .4f + sample(v) * .6f;
						expected[2] = Scene[2] * .4f + (.75f - .25f * mipLevel) * .6f;
					}
					for (std::size_t channel = 0; channel < 4; ++channel)
						matches &= std::isfinite(row[x][channel]) && std::abs(row[x][channel] - expected[channel]) < 2e-4f;
				}
			}
			context->Unmap(staging.Get(), 0);
			if (!matches)
				std::cerr << "angle=" << angle << " depth=" << depth << " viewport=" << viewport.x << '\n';
			Require(matches, "compositor coverage, perspective UVs and alpha must match eye-ray intersections");
		}
	};
}

int main()
{
	try {
		Fixture fixture;
		unsigned conditions = 0;
		for (const Viewport viewport : { Viewport{ 0, 0, Width, Height }, Viewport{ 0, 0, Width / 2, Height }, Viewport{ Width / 2, 0, Width / 2, Height }, Viewport{ 13.25f, 7.75f, 91.5f, 77.5f }, Viewport{ 12.25f, 9.25f, .5f, .5f } }) {
			for (double depth : { .05, .5, 2.0, 1001.0 }) {
				for (int degrees = -180; degrees <= 180; degrees += 5) {
					fixture.Run(degrees * 3.141592653589793 / 180, depth, viewport);
					++conditions;
				}
			}
		}
		for (int degrees = -180; degrees <= 180; degrees += 3) {
			for (double roll : { .41, 1.57079632679 }) {
				for (float mip : { 0.0f, 1.25f, 2.0f }) {
					fixture.Run(degrees * 3.141592653589793 / 180, .5, { 13.25f, 7.75f, 91.5f, 77.5f }, roll, .22, mip);
					++conditions;
				}
			}
		}
		Constants cb;
		float area;
		const Viewport full{ 0, 0, Width, Height };
		const auto rejected = [&](const auto& quad, const Viewport& viewport, UINT width = Width, UINT height = Height) {
			cb.vertexCount = 42;
			area = 42;
			Require(!Project(quad, viewport, width, height, cb, area) && cb.vertexCount == 0 &&
						cb.dispatchSize[0] == 0 && cb.dispatchSize[1] == 0 && area == 0,
				"rejection must clear all dispatch outputs");
		};
		for (double value : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
				 -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::max() }) {
			for (auto member : { &Vertex::x, &Vertex::y, &Vertex::z, &Vertex::w, &Vertex::u, &Vertex::v }) {
				auto invalid = Quad(0, 2);
				invalid[0].*member = value;
				rejected(invalid, full);
			}
		}
		for (float value : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max() }) {
			for (auto member : { &Viewport::x, &Viewport::y, &Viewport::width, &Viewport::height }) {
				Viewport invalid = full;
				invalid.*member = value;
				rejected(Quad(0, 2), invalid);
			}
		}
		rejected(Quad(0, 2), full, 0, Height);
		rejected(Quad(0, 2), full, MaxTextureExtent + 1, Height);
		rejected(Quad(0, 2), { 0, 0, 0, Height });
		rejected(Quad(0, 2), { 200, 0, Width, Height });
		auto invalid = Quad(0, 2);
		invalid.fill(invalid[0]);
		rejected(invalid, full);
		invalid = Quad(0, 2);
		invalid[0].u = -1;
		rejected(invalid, full);
		for (double depth : { .5, 2.0 }) {
			Require(Project(Quad(0, depth), full, Width, Height, cb, area), "front-facing menu must project");
			const double expectedArea = Width * Height * HalfWidth * HalfHeight / (depth * depth);
			Require(std::abs(area / expectedArea - 1) < 1e-5, "clipping must retain the UV-to-pixel ratio used for mip selection");
		}
		std::cout << conditions << " WARP menu conditions matched independent ray intersections; invalid inputs rejected.\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
