// Standalone Windows WARP test of the actual production HLSL; no Skyrim or NGX.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace
{
	constexpr UINT width = 29, height = 23, left = 5, top = 3, sizeX = 13, sizeY = 11;
	struct Pixel { float r, g, b, a; };
	struct Constants
	{
		UINT x = left, y = top, w = sizeX, h = sizeY;
		UINT mode = 1, transfer = 1, codec = 0, radius = 2;
		float white = 1, detail = 0.65f, appearance = 0, stops = 1;
		float maximumR = 65024, maximumG = 65024, maximumB = 64512, padding = 0;
	};
	static_assert(sizeof(Constants) == 64);
	int checks = 0;
	void Require(bool condition, const char* message)
	{
		++checks;
		if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
	}
	void Check(HRESULT result, const char* message) { Require(SUCCEEDED(result), message); }
	bool Inside(UINT x, UINT y) { return x >= left && x < left + sizeX && y >= top && y < top + sizeY; }
	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
	};
	Texture Make(ID3D11Device* device, DXGI_FORMAT format, const std::vector<Pixel>* pixels = nullptr)
	{
		Texture texture;
		D3D11_TEXTURE2D_DESC description{};
		description.Width = width; description.Height = height;
		description.MipLevels = 1; description.ArraySize = 1;
		description.Format = format; description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		D3D11_SUBRESOURCE_DATA data{};
		if (pixels) { data.pSysMem = pixels->data(); data.SysMemPitch = width * sizeof(Pixel); }
		Check(device->CreateTexture2D(&description, pixels ? &data : nullptr, &texture.resource), "texture creation");
		Check(device->CreateShaderResourceView(texture.resource.Get(), nullptr, &texture.srv), "SRV creation");
		Check(device->CreateUnorderedAccessView(texture.resource.Get(), nullptr, &texture.uav), "UAV creation");
		return texture;
	}
	ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const std::filesystem::path& path, const char* entry)
	{
		ComPtr<ID3DBlob> code, errors;
		const HRESULT result = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			entry, "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, &code, &errors);
		if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), static_cast<std::streamsize>(errors->GetBufferSize()));
		Check(result, "production HLSL compilation");
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader), "compute shader creation");
		return shader;
	}
	void Dispatch(ID3D11DeviceContext* context, ID3D11ComputeShader* shader, ID3D11Buffer* buffer,
		const Constants& constants, const Texture& baseline, const Texture* prepared, const Texture* model, const Texture& output)
	{
		context->UpdateSubresource(buffer, 0, nullptr, &constants, 0, 0);
		ID3D11ShaderResourceView* sources[]{ baseline.srv.Get(), prepared ? prepared->srv.Get() : nullptr, model ? model->srv.Get() : nullptr };
		ID3D11UnorderedAccessView* destination = output.uav.Get();
		context->CSSetShader(shader, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &buffer);
		context->CSSetShaderResources(0, 3, sources);
		context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
		context->Dispatch((sizeX + 7) / 8, (sizeY + 7) / 8, 1);
		ID3D11ShaderResourceView* nullSources[3]{};
		ID3D11UnorderedAccessView* nullDestination = nullptr;
		context->CSSetShaderResources(0, 3, nullSources);
		context->CSSetUnorderedAccessViews(0, 1, &nullDestination, nullptr);
	}
	std::vector<Pixel> Read(ID3D11Device* device, ID3D11DeviceContext* context, const Texture& texture)
	{
		D3D11_TEXTURE2D_DESC description{};
		texture.resource->GetDesc(&description);
		description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> staging;
		Check(device->CreateTexture2D(&description, nullptr, &staging), "staging allocation");
		context->CopyResource(staging.Get(), texture.resource.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "test-only readback");
		std::vector<Pixel> pixels(width * height);
		for (UINT y = 0; y < height; ++y) {
			const auto* row = reinterpret_cast<const Pixel*>(static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * y);
			std::copy_n(row, width, pixels.data() + width * y);
		}
		context->Unmap(staging.Get(), 0);
		return pixels;
	}
	void Compare(const std::vector<Pixel>& actual, const std::vector<Pixel>& expected, float tolerance)
	{
		for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
			const auto& a = actual[y * width + x];
			if (!Inside(x, y)) {
				Require(a.r == -12345 && a.g == -12345 && a.b == -12345 && a.a == -12345, "ROI exterior remains untouched");
				continue;
			}
			const auto& e = expected[y * width + x];
			Require(std::isfinite(a.r) && std::isfinite(a.g) && std::isfinite(a.b), "finite result");
			Require(std::abs(a.r - e.r) <= tolerance && std::abs(a.g - e.g) <= tolerance && std::abs(a.b - e.b) <= tolerance, "colour reference");
			Require(a.a == e.a, "source alpha exact");
		}
	}
}
int main(int argc, char** argv)
{
	Require(argc == 2, "supply production shader filename");
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context), "WARP initialization");
	const auto prepareShader = Compile(device.Get(), std::filesystem::path(argv[1]), "Prepare");
	const auto resolveShader = Compile(device.Get(), std::filesystem::path(argv[1]), "Resolve");
	ComPtr<ID3D11Buffer> buffer;
	D3D11_BUFFER_DESC bufferDescription{};
	bufferDescription.ByteWidth = sizeof(Constants); bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	Check(device->CreateBuffer(&bufferDescription, nullptr, &buffer), "constant buffer");
	std::vector<Pixel> source(width * height);
	for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
		const float v = 0.00001f + static_cast<float>((x + y) % 17) * 0.123f;
		source[y * width + x] = { v, v * 0.25f, v * 0.7f, 0.37f };
	}
	auto baseline = Make(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, &source);
	auto result = Make(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT);
	const float sentinel[4]{ -12345, -12345, -12345, -12345 };
	const float poison[4]{ 500, 200, 0, 0 };
	for (DXGI_FORMAT format : { DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT }) {
		auto prepared = Make(device.Get(), format);
		auto model = Make(device.Get(), format);
		for (UINT codec : { 0u, 1u, 2u }) for (UINT mode : { 1u, 2u }) {
			Constants constants; constants.codec = codec; constants.mode = mode;
			context->ClearUnorderedAccessViewFloat(result.uav.Get(), sentinel);
			context->ClearUnorderedAccessViewFloat(model.uav.Get(), poison);
			Dispatch(context.Get(), prepareShader.Get(), buffer.Get(), constants, baseline, nullptr, nullptr, prepared);
			const D3D11_BOX box{ left, top, 0, left + sizeX, top + sizeY, 1 };
			context->CopySubresourceRegion(model.resource.Get(), 0, left, top, 0, prepared.resource.Get(), 0, &box);
			Dispatch(context.Get(), resolveShader.Get(), buffer.Get(), constants, baseline, &prepared, &model, result);
			Compare(Read(device.Get(), context.Get(), result), source, 0.0f);
		}
	}
	// Broad exposure/chroma shift is not detail; poison outside the ROI must
	// never participate in the low-frequency estimate at a region boundary.
	std::fill(source.begin(), source.end(), Pixel{ 0.18f, 0.09f, 0.04f, 0.6f });
	baseline = Make(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, &source);
	auto prepared = Make(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, &source);
	std::vector<Pixel> changed(width * height, Pixel{ 500, 0, 200, 0 });
	for (UINT y = top; y < top + sizeY; ++y) for (UINT x = left; x < left + sizeX; ++x)
		changed[y * width + x] = { 0.5f, 0.2f, 0.17f, 1 };
	auto model = Make(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, &changed);
	Constants constants; constants.mode = 2;
	for (UINT radius : { 1u, 2u, 4u }) {
		constants.radius = radius;
		context->ClearUnorderedAccessViewFloat(result.uav.Get(), sentinel);
		Dispatch(context.Get(), resolveShader.Get(), buffer.Get(), constants, baseline, &prepared, &model, result);
		Compare(Read(device.Get(), context.Get(), result), source, 0.00001f);
	}
	constants.detail = 0; constants.appearance = 0;
	context->ClearUnorderedAccessViewFloat(result.uav.Get(), sentinel);
	Dispatch(context.Get(), resolveShader.Get(), buffer.Get(), constants, baseline, &prepared, &model, result);
	Compare(Read(device.Get(), context.Get(), result), source, 0.0f);
	constants.mode = 1;
	context->ClearUnorderedAccessViewFloat(model.uav.Get(), poison);
	const float nan[4]{ std::numeric_limits<float>::quiet_NaN(), 0, 0, 1 };
	context->ClearUnorderedAccessViewFloat(model.uav.Get(), nan);
	context->ClearUnorderedAccessViewFloat(result.uav.Get(), sentinel);
	Dispatch(context.Get(), resolveShader.Get(), buffer.Get(), constants, baseline, &prepared, &model, result);
	Compare(Read(device.Get(), context.Get(), result), source, 0.0f);
	std::cout << checks << " WARP checks passed; NGX/D3D12/stereo runtime is not exercised\n";
}
