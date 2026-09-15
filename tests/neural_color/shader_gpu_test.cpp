// Standalone Windows WARP tests. No Skyrim, NVIDIA DLL or plugin build needed.
#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace NeuralRendering::Color;
struct Pixel
{
	float r, g, b, a;
};
struct Constants
{
	unsigned x = 5, y = 7, width = 17, height = 19;
	unsigned mode = 1, domain = 1, transform = 0, bypass = 0;
	float exposure = 1, detail = 1, appearance = 0, maximumStops = 1;
};
static_assert(sizeof(Constants) == 48);
static unsigned checks = 0;
static void Require(bool value, const char* message)
{
	++checks;
	if (!value) {
		std::fprintf(stderr, "%s (check %u)\n", message, checks);
		std::abort();
	}
}
static void Check(HRESULT result) { Require(SUCCEEDED(result), "D3D operation failed"); }
struct Texture
{
	ComPtr<ID3D11Texture2D> texture;
	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
};
static Texture MakeTexture(ID3D11Device* device, const std::vector<Pixel>& pixels)
{
	Texture value;
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = 64;
	desc.Height = 64;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	D3D11_SUBRESOURCE_DATA data{ pixels.data(), 64u * sizeof(Pixel), 0 };
	Check(device->CreateTexture2D(&desc, &data, &value.texture));
	Check(device->CreateShaderResourceView(value.texture.Get(), nullptr, &value.srv));
	Check(device->CreateUnorderedAccessView(value.texture.Get(), nullptr, &value.uav));
	return value;
}
static ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const std::filesystem::path& path)
{
	ComPtr<ID3DBlob> bytecode, errors;
	const auto result = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS, 0, &bytecode, &errors);
	if (errors)
		std::fprintf(stderr, "%.*s\n", static_cast<int>(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
	Check(result);
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader));
	return shader;
}
static void Dispatch(ID3D11DeviceContext* context, ID3D11ComputeShader* shader, ID3D11Buffer* cb,
	const Constants& constants, const std::array<ID3D11ShaderResourceView*, 3>& sources, ID3D11UnorderedAccessView* output)
{
	context->UpdateSubresource(cb, 0, nullptr, &constants, 0, 0);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShader(shader, nullptr, 0);
	context->CSSetShaderResources(0, 3, sources.data());
	context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
	context->Dispatch((constants.width + 7) / 8, (constants.height + 7) / 8, 1);
	ID3D11ShaderResourceView* empty[3]{};
	ID3D11UnorderedAccessView* emptyOutput = nullptr;
	context->CSSetShaderResources(0, 3, empty);
	context->CSSetUnorderedAccessViews(0, 1, &emptyOutput, nullptr);
}
static std::vector<Pixel> Read(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture)
{
	D3D11_TEXTURE2D_DESC desc{};
	texture->GetDesc(&desc);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&desc, nullptr, &staging));
	context->CopyResource(staging.Get(), texture);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	// Blocking readback is deliberate in this standalone TEST, never the runtime.
	Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
	std::vector<Pixel> result(64 * 64);
	for (unsigned y = 0; y < 64; ++y) {
		const auto* row = reinterpret_cast<const Pixel*>(static_cast<const char*>(mapped.pData) + mapped.RowPitch * y);
		std::copy(row, row + 64, result.begin() + y * 64);
	}
	context->Unmap(staging.Get(), 0);
	return result;
}
int main(int argc, char** argv)
{
	Require(argc == 2, "provide NR colour shader directory");
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	const D3D_FEATURE_LEVEL levels[]{ D3D_FEATURE_LEVEL_11_0 };
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
		D3D11_SDK_VERSION, &device, nullptr, &context));
	const std::filesystem::path directory(argv[1]);
	auto prepare = Compile(device.Get(), directory / "ColorPrepareCS.hlsl");
	auto reconstruct = Compile(device.Get(), directory / "ColorReconstructCS.hlsl");
	const auto measure = Compile(device.Get(), directory / "ColorMeasureCS.hlsl");
	Require(measure != nullptr, "measurement shader compilation");
	ComPtr<ID3D11Buffer> cb;
	D3D11_BUFFER_DESC cbDesc{};
	cbDesc.ByteWidth = sizeof(Constants);
	cbDesc.Usage = D3D11_USAGE_DEFAULT;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	Check(device->CreateBuffer(&cbDesc, nullptr, &cb));
	std::vector<Pixel> original(4096), sentinel(4096, Pixel{ -7.0f, -7.0f, -7.0f, -7.0f });
	for (unsigned y = 0; y < 64; ++y)
		for (unsigned x = 0; x < 64; ++x) {
			float value = 0.001f + static_cast<float>(x + y) / 256.0f;
			original[y * 64 + x] = { value, value * 0.5f, value * 0.25f, 0.375f };
		}
	original[0] = { 0, 0, 0, 0.25f };
	auto baseline = MakeTexture(device.Get(), original);
	Constants constants;
	for (unsigned transform = 0; transform < 3; ++transform) {
		constants.transform = transform;
		for (unsigned insertion = 0; insertion < 2; ++insertion) {
			// Second position represents another isolated physical ROI; no image
			// context or pixel reads may leak from its arbitrary backing memory.
			constants.x = insertion ? 30u : 5u;
			constants.y = insertion ? 40u : 7u;
			auto prepared = MakeTexture(device.Get(), sentinel);
			auto result = MakeTexture(device.Get(), sentinel);
			Dispatch(context.Get(), prepare.Get(), cb.Get(), constants, { baseline.srv.Get(), nullptr, nullptr }, prepared.uav.Get());
			for (unsigned mode = 1; mode <= 2; ++mode) {
				constants.mode = mode;
				Dispatch(context.Get(), reconstruct.Get(), cb.Get(), constants,
					{ baseline.srv.Get(), prepared.srv.Get(), prepared.srv.Get() }, result.uav.Get());
				const auto values = Read(device.Get(), context.Get(), result.texture.Get());
				for (unsigned y = 0; y < 64; ++y)
					for (unsigned x = 0; x < 64; ++x) {
						const auto& expected = x < constants.width && y < constants.height ? original[y * 64 + x] : sentinel[y * 64 + x];
						const auto& value = values[y * 64 + x];
						Require(std::abs(value.r - expected.r) < 0.00002f, "identity/ROI red");
						Require(std::abs(value.g - expected.g) < 0.00002f, "identity/ROI green");
						Require(std::abs(value.b - expected.b) < 0.00002f, "identity/ROI blue");
						Require(value.a == expected.a, "alpha preservation");
					}
			}
		}
	}
	constants = {};
	constants.mode = 2;
	auto prepared = MakeTexture(device.Get(), sentinel);
	Dispatch(context.Get(), prepare.Get(), cb.Get(), constants, { baseline.srv.Get(), nullptr, nullptr }, prepared.uav.Get());
	auto doubled = sentinel;
	for (unsigned y = 0; y < constants.height; ++y)
		for (unsigned x = 0; x < constants.width; ++x) {
			auto value = original[y * 64 + x];
			value.r *= 2;
			value.g *= 2;
			value.b *= 2;
			value.a = 0.0f;
			doubled[(y + constants.y) * 64 + x + constants.x] = value;
		}
	auto neural = MakeTexture(device.Get(), doubled);
	for (float appearance : { 0.0f, 1.0f }) {
		constants.appearance = appearance;
		auto result = MakeTexture(device.Get(), sentinel);
		Dispatch(context.Get(), reconstruct.Get(), cb.Get(), constants,
			{ baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, result.uav.Get());
		const auto values = Read(device.Get(), context.Get(), result.texture.Get());
		for (unsigned y = 0; y < constants.height; ++y)
			for (unsigned x = 0; x < constants.width; ++x) {
				const auto b = original[y * 64 + x], v = values[y * 64 + x];
				const float gain = appearance == 0.0f ? 1.0f : 2.0f;
				Require(std::abs(v.r - b.r * gain) < 0.00003f, "uniform lighting/appearance endpoint");
				Require(v.a == b.a, "neural alpha must not replace baseline alpha");
			}
	}
	std::printf("Passed %u WARP shader checks; D3D12/NGX transport is not exercised by this test.\n", checks);
}
