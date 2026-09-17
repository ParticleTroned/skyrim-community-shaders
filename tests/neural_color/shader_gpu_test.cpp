// Standalone Windows shader tests: WARP by default, optional --hardware adapter.
#include "../ShaderPackageIncludes.h"
#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <limits>
#include <string_view>
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
static Texture MakeTexture(ID3D11Device* device, const std::vector<Pixel>& pixels,
	DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT)
{
	Texture value;
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = 64;
	desc.Height = 64;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	D3D11_SUBRESOURCE_DATA data{ pixels.data(), 64u * sizeof(Pixel), 0 };
	Check(device->CreateTexture2D(&desc, format == DXGI_FORMAT_R32G32B32A32_FLOAT ? &data : nullptr, &value.texture));
	Check(device->CreateShaderResourceView(value.texture.Get(), nullptr, &value.srv));
	Check(device->CreateUnorderedAccessView(value.texture.Get(), nullptr, &value.uav));
	return value;
}
static ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const std::filesystem::path& path,
	const std::filesystem::path& shaderRoot = {})
{
	ComPtr<ID3DBlob> bytecode, errors;
	PackageIncludes includes(shaderRoot.empty() ? path.parent_path().parent_path().parent_path() : shaderRoot);
	const auto result = D3DCompileFromFile(path.c_str(), nullptr, &includes,
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
static void CheckFineDetail(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* prepare, ID3D11ComputeShader* reconstruct, ID3D11Buffer* cb, unsigned domain)
{
	// Alternating detail must survive regardless of orientation or pixel parity.
	const auto toWorking = [domain](float value) { return domain == 2 ? Decode(value) : value; };
	const auto fromWorking = [domain](float value) { return domain == 2 ? Encode(value) : value; };
	const Pixel source{ fromWorking(0.25f), fromWorking(0.125f), fromWorking(0.0625f), 0.375f };
	const std::vector<Pixel> original(4096, source), sentinel(4096, Pixel{ -7, -7, -7, -7 });
	auto baseline = MakeTexture(device, original);
	const std::array strengths{ 0.0f, 1.0f, 2.0f, 2.0f, 2.0f };
	const std::array limits{ 1.0f, 1.0f, 1.0f, 0.125f, 0.0f };
	for (unsigned pattern = 0; pattern < 3; ++pattern) {
		const auto isBright = [pattern](unsigned x, unsigned y) {
			const unsigned column = pattern == 1 ? 0 : x;
			const unsigned row = pattern == 0 ? 0 : y;
			return ((column + row) & 1u) == 0;
		};
		Constants constants;
		constants.mode = 2;
		constants.domain = domain;
		auto prepared = MakeTexture(device, sentinel);
		Dispatch(context, prepare, cb, constants, { baseline.srv.Get(), nullptr, nullptr }, prepared.uav.Get());
		auto input = sentinel;
		for (unsigned y = 0; y < constants.height; ++y)
			for (unsigned x = 0; x < constants.width; ++x) {
				const float gain = std::exp2(isBright(x, y) ? 0.25f : -0.25f);
				input[(y + constants.y) * 64 + x + constants.x] = { fromWorking(0.25f * gain), fromWorking(0.125f * gain), fromWorking(0.0625f * gain), 0.0f };
			}
		auto neural = MakeTexture(device, input);
		std::array<std::vector<Pixel>, strengths.size()> outputs;
		for (unsigned variant = 0; variant < outputs.size(); ++variant) {
			constants.detail = strengths[variant];
			constants.maximumStops = limits[variant];
			auto result = MakeTexture(device, sentinel);
			Dispatch(context, reconstruct, cb, constants,
				{ baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, result.uav.Get());
			outputs[variant] = Read(device, context, result.texture.Get());
		}
		const auto center = 8 * 64 + 8;
		std::printf("Fine detail domain %u pattern %u: strength 0/1/2 = %.6f / %.6f / %.6f stops\n", domain, pattern,
			std::log2(toWorking(outputs[0][center].r) / 0.25f), std::log2(toWorking(outputs[1][center].r) / 0.25f),
			std::log2(toWorking(outputs[2][center].r) / 0.25f));
		std::fflush(stdout);
		for (unsigned y = 0; y < 64; ++y)
			for (unsigned x = 0; x < 64; ++x) {
				const auto index = y * 64 + x;
				const bool inside = x < constants.width && y < constants.height;
				for (unsigned variant = 0; variant < outputs.size(); ++variant) {
					const auto value = outputs[variant][index];
					if (!inside) {
						Require(value.r == -7 && value.g == -7 && value.b == -7 && value.a == -7,
							"detail must not write outside the physical ROI");
						continue;
					}
					Require(value.a == source.a, "detail must preserve source alpha");
					const float r = toWorking(value.r), g = toWorking(value.g), b = toWorking(value.b);
					Require(std::abs(r - 2 * g) < 0.00002f &&
								std::abs(r - 4 * b) < 0.00002f,
						"detail must preserve source chroma");
					Require(std::abs(std::log2(r / 0.25f)) <= limits[variant] + 0.00002f,
						"detail must obey the configured stop bound");
				}
				if (!inside)
					continue;
				Require(outputs[0][index].r == source.r && outputs[0][index].g == source.g &&
							outputs[0][index].b == source.b,
					"zero strength must preserve source exactly");
				if (x == 0 || y == 0 || x + 1 == constants.width || y + 1 == constants.height)
					Require(std::abs(outputs[2][index].r - source.r) < 0.00002f, "physical ROI border must preserve source");
				if (x < 4 || y < 4 || x + 4 >= constants.width || y + 4 >= constants.height)
					continue;
				const float sign = isBright(x, y) ? 1.0f : -1.0f;
				const float stops1 = std::log2(toWorking(outputs[1][index].r) / 0.25f);
				const float stops2 = std::log2(toWorking(outputs[2][index].r) / 0.25f);
				Require(sign * stops1 > 0.05f, "alternating neural detail must not disappear");
				Require(sign * stops2 > sign * stops1 + 0.04f,
					"increasing detail strength must increase the retained fine detail");
				Require(std::abs(std::log2(toWorking(outputs[3][index].r) / 0.25f) - sign * 0.125f) < 0.00002f,
					"fine detail above the stop limit must saturate at that limit");
			}
	}
}
static std::vector<float> PackedValues(unsigned fractionBits)
{
	std::vector<float> values;
	for (unsigned exponent = 0; exponent < 31; ++exponent)
		for (unsigned fraction = 0; fraction < (1u << fractionBits); ++fraction)
			values.push_back(std::ldexp(static_cast<float>(fraction + (exponent ? 1u << fractionBits : 0)),
				static_cast<int>(exponent ? exponent : 1) - 15 - static_cast<int>(fractionBits)));
	return values;
}
static float NearestPacked(float value, const std::vector<float>& values)
{
	// Enumerating representable neighbours keeps the oracle independent of shader bit rounding.
	const auto upper = std::lower_bound(values.begin(), values.end(), value);
	if (upper == values.begin())
		return *upper;
	if (upper == values.end())
		return values.back();
	const auto index = static_cast<std::size_t>(upper - values.begin());
	const double below = static_cast<double>(value) - values[index - 1];
	const double above = static_cast<double>(*upper) - value;
	return below < above || (below == above && ((index - 1) & 1u) == 0) ? values[index - 1] : *upper;
}
static Constants FullTextureConstants()
{
	Constants constants;
	constants.x = constants.y = 0;
	constants.width = constants.height = 64;
	constants.mode = 0;
	return constants;
}
static std::vector<Pixel> ReadStored(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* copy, ID3D11Buffer* cb, const Texture& texture)
{
	D3D11_TEXTURE2D_DESC desc{};
	texture.texture->GetDesc(&desc);
	if (desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT)
		return Read(device, context, texture.texture.Get());
	auto decoded = MakeTexture(device, std::vector<Pixel>(4096));
	Dispatch(context, copy, cb, FullTextureConstants(), { texture.srv.Get() }, decoded.uav.Get());
	return Read(device, context, decoded.texture.Get());
}
static Texture StorePixels(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* copy, ID3D11Buffer* cb, const std::vector<Pixel>& pixels)
{
	auto input = MakeTexture(device, pixels);
	auto packed = MakeTexture(device, {}, DXGI_FORMAT_R11G11B10_FLOAT);
	Dispatch(context, copy, cb, FullTextureConstants(), { input.srv.Get() }, packed.uav.Get());
	return packed;
}
static std::vector<float> PackedRoundingCases(const std::vector<float>& values)
{
	auto cases = values;
	cases.push_back(std::numeric_limits<float>::denorm_min());
	for (std::size_t i = 1; i < values.size(); ++i) {
		const float midpoint = static_cast<float>((static_cast<double>(values[i - 1]) + values[i]) * 0.5);
		cases.push_back(std::nextafter(midpoint, values[i - 1]));
		cases.push_back(midpoint);
		cases.push_back(std::nextafter(midpoint, values[i]));
	}
	return cases;
}
static void CheckPackedRounding(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* rounding, ID3D11Buffer* cb)
{
	const auto redValues = PackedValues(6), blueValues = PackedValues(5);
	const auto redCases = PackedRoundingCases(redValues), blueCases = PackedRoundingCases(blueValues);
	for (std::size_t offset = 0; offset < redCases.size(); offset += 4096) {
		std::vector<Pixel> inputs(4096);
		for (std::size_t i = 0; i < inputs.size(); ++i)
			inputs[i] = { redCases[(offset + i) % redCases.size()],
				redCases[(redCases.size() - 1 - ((offset + i) % redCases.size()))],
				blueCases[(offset + i) % blueCases.size()], 0.375f };
		auto input = MakeTexture(device, inputs);
		for (unsigned storage : { 0u, 0x100u, 0x200u, 0x300u }) {
			auto constants = FullTextureConstants();
			constants.mode = 1;
			constants.bypass = storage;
			for (bool packed : { false, true }) {
				if (packed && storage != 0x100)
					continue;
				auto result = MakeTexture(device, std::vector<Pixel>(4096),
					packed ? DXGI_FORMAT_R11G11B10_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT);
				Dispatch(context, rounding, cb, constants, { input.srv.Get() }, result.uav.Get());
				const auto actual = ReadStored(device, context, rounding, cb, result);
				for (std::size_t i = 0; i < inputs.size(); ++i) {
					const auto& v = actual[i];
					const auto& s = inputs[i];
					const Pixel expected = storage == 0x100 ?
					                           Pixel{ NearestPacked(s.r, redValues), NearestPacked(s.g, redValues), NearestPacked(s.b, blueValues), 0.375f } :
					                           s;
					Require(v.r == expected.r && v.g == expected.g && v.b == expected.b,
						"packed rounding must match nearest-even across finite values, boundaries and subnormals");
					if (!packed)
						Require(v.a == expected.a, "rounding preserves alpha in formats with alpha");
				}
			}
		}
	}
}
static void CheckPackedDetail(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* reconstruct, ID3D11ComputeShader* copy, ID3D11Buffer* cb)
{
	const auto redValues = PackedValues(6), blueValues = PackedValues(5);
	for (bool ramp : { false, true }) {
		std::vector<Pixel> source(4096);
		for (unsigned y = 0; y < 64; ++y)
			for (unsigned x = 0; x < 64; ++x)
				source[y * 64 + x] = ramp ?
				                         Pixel{ (64.f + x % 32) / 512.f, (64.f + y % 32) / 1024.f, (32.f + (x + y) % 16) / 1024.f, 1 } :
				                         Pixel{ 0.15625f, 0.078125f, 0.0390625f, 1 };
		auto baseline = StorePixels(device, context, copy, cb, source);
		for (unsigned domain = 0; domain < 3; ++domain)
			for (std::string_view edit : { "identity", "uniform_half", "tiny_checker", "checker", "hidden", "zero", "limit_zero", "appearance", "mixed_appearance", "managed", "raw", "transport" }) {
				auto neuralPixels = source;
				for (unsigned y = 0; y < 64; ++y)
					for (unsigned x = 0; x < 64; ++x) {
						const float amplitude = edit == "tiny_checker" ? 0.002f : 0.25f;
						const float stops = edit == "identity" ? 0.f : edit == "uniform_half" ? -1.f :
						                                                                        ((x + y) % 2 ? amplitude : -amplitude);
						auto& v = neuralPixels[y * 64 + x];
						v.r *= std::exp2(stops);
						v.g *= std::exp2(stops);
						v.b *= std::exp2(stops);
					}
				auto neural = StorePixels(device, context, copy, cb, neuralPixels);
				auto constants = FullTextureConstants();
				constants.mode = edit == "managed" ? 1u : edit == "raw" ? 0u :
				                                                          2u;
				constants.domain = domain;
				constants.detail = edit == "zero" ? 0.f : 1.f;
				constants.appearance = edit == "appearance" ? 1.f : edit == "mixed_appearance" ? 0.5f :
				                                                                                 0.f;
				constants.maximumStops = edit == "limit_zero" ? 0.f : 1.f;
				const unsigned flags = edit == "hidden" ? 2u : edit == "transport" ? 1u :
				                                                                     0u;
				std::array<std::vector<Pixel>, 2> outputs;
				for (unsigned packed = 0; packed < 2; ++packed) {
					constants.bypass = flags | (packed ? 0x100u : 0u);
					auto result = MakeTexture(device, std::vector<Pixel>(4096),
						packed ? DXGI_FORMAT_R11G11B10_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT);
					Dispatch(context, reconstruct, cb, constants,
						{ baseline.srv.Get(), neural.srv.Get(), baseline.srv.Get() }, result.uav.Get());
					outputs[packed] = ReadStored(device, context, copy, cb, result);
				}
				const bool endpoint = edit == "appearance" || edit == "managed" || edit == "raw" || edit == "transport";
				const auto candidate = endpoint ? ReadStored(device, context, copy, cb, neural) : source;
				double signedLuma = 0;
				unsigned positive = 0, negative = 0;
				for (unsigned i = 0; i < 4096; ++i) {
					const auto& f = outputs[0][i];
					const auto& actual = outputs[1][i];
					const auto expected = endpoint ? candidate[i] :
					                                 Pixel{ NearestPacked(f.r, redValues), NearestPacked(f.g, redValues), NearestPacked(f.b, blueValues), 1 };
					Require(actual.r == expected.r && actual.g == expected.g && actual.b == expected.b,
						"production Preserve Source store must match nearest packed FP32 result and retain other endpoints");
					if (edit == "identity" || edit == "hidden" || edit == "zero" || edit == "limit_zero")
						Require(actual.r == source[i].r && actual.g == source[i].g && actual.b == source[i].b,
							"packed identity, hidden and zero endpoints must remain exact");
					const unsigned x = i % 64, y = i / 64;
					if (x >= 4 && x < 60 && y >= 4 && y < 60) {
						const double delta = 0.2126 * (actual.r - source[i].r) + 0.7152 * (actual.g - source[i].g) + 0.0722 * (actual.b - source[i].b);
						signedLuma += delta;
						positive += delta > 0;
						negative += delta < 0;
					}
				}
				if (domain == 0 && (edit == "tiny_checker" || edit == "uniform_half")) {
					Require(signedLuma == 0 && positive == 0 && negative == 0,
						"sub-precision detail and neutral residual must not create packed darkening");
					std::printf("Packed %s %.*s: mean luma delta %.9f\n", ramp ? "ramp" : "flat",
						static_cast<int>(edit.size()), edit.data(), signedLuma / 3136);
				}
				if (edit == "checker")
					Require(positive > 0 && negative > 0, "packed alternating detail must retain both signs");
			}
	}
}

static void CheckPackedInvalidCandidates(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* reconstruct, ID3D11ComputeShader* copy, ID3D11Buffer* cb)
{
	const Pixel source{ 0.15625f, 0.078125f, 0.0390625f, 1 };
	auto baseline = StorePixels(device, context, copy, cb, std::vector<Pixel>(4096, source));
	for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.f, 65504.f, 70000.f }) {
		auto neural = MakeTexture(device, std::vector<Pixel>(4096, Pixel{ invalid, invalid, invalid, 1 }));
		auto result = MakeTexture(device, {}, DXGI_FORMAT_R11G11B10_FLOAT);
		auto constants = FullTextureConstants();
		constants.mode = 2;
		constants.bypass = 0x100;
		Dispatch(context, reconstruct, cb, constants,
			{ baseline.srv.Get(), neural.srv.Get(), baseline.srv.Get() }, result.uav.Get());
		for (const auto& value : ReadStored(device, context, copy, cb, result))
			Require(value.r == source.r && value.g == source.g && value.b == source.b,
				"invalid packed candidates must retain source before storage rounding");
	}
}
int main(int argc, char** argv)
{
	Require(argc == 2 || ((argc == 3 || argc == 4) && std::string_view(argv[2]) == "--hardware"), "provide NR colour shader directory and optional --hardware [adapter index]");
	const bool hardware = argc >= 3;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	const D3D_FEATURE_LEVEL levels[]{ D3D_FEATURE_LEVEL_11_0 };
	Check(D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
		D3D11_SDK_VERSION, &device, nullptr, &context));
	ComPtr<IDXGIDevice> dxgiDevice;
	ComPtr<IDXGIAdapter> adapter;
	DXGI_ADAPTER_DESC adapterDesc{};
	Check(device.As(&dxgiDevice));
	Check(dxgiDevice->GetAdapter(&adapter));
	if (argc == 4) {
		unsigned index = 0;
		const std::string_view text(argv[3]);
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), index);
		Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "hardware adapter index must be unsigned");
		ComPtr<IDXGIFactory> factory;
		Check(adapter->GetParent(IID_PPV_ARGS(&factory)));
		adapter.Reset();
		dxgiDevice.Reset();
		context.Reset();
		device.Reset();
		Check(factory->EnumAdapters(index, &adapter));
		Check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 1,
			D3D11_SDK_VERSION, &device, nullptr, &context));
	}
	Check(adapter->GetDesc(&adapterDesc));
	std::printf("Shader test adapter: %ls (vendor %04x, device %04x)\n", adapterDesc.Description, adapterDesc.VendorId, adapterDesc.DeviceId);
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
	for (unsigned domain = 0; domain < 3; ++domain)
		CheckFineDetail(device.Get(), context.Get(), prepare.Get(), reconstruct.Get(), cb.Get(), domain);
	const auto rounding = Compile(device.Get(), std::filesystem::path(__FILE__).parent_path() / "packed_rounding_test.hlsl",
		directory.parent_path().parent_path());
	CheckPackedRounding(device.Get(), context.Get(), rounding.Get(), cb.Get());
	CheckPackedDetail(device.Get(), context.Get(), reconstruct.Get(), rounding.Get(), cb.Get());
	CheckPackedInvalidCandidates(device.Get(), context.Get(), reconstruct.Get(), rounding.Get(), cb.Get());
	std::printf("Passed %u %s shader checks; D3D12/NGX transport is not exercised by this test.\n", checks, hardware ? "hardware" : "WARP");
}
