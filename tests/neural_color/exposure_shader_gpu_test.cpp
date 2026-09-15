// Production exposure/prepare/reconstruct/measurement shaders on Windows WARP.
#include "../ShaderPackageIncludes.h"
#include "Features/Upscaling/NeuralRendering/ExposureCapture.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using namespace NeuralRendering::Color;
using Pixel = std::array<float, 4>;
static void Require(bool value)
{
	if (!value) {
		std::fprintf(stderr, "Exposure WARP assertion failed\n");
		std::abort();
	}
}
static void Check(HRESULT result) { Require(SUCCEEDED(result)); }
struct Texture
{
	ComPtr<ID3D11Texture2D> resource;
	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
};
static Texture Make(ID3D11Device* device, UINT size, Pixel value, DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT, UINT height = 0)
{
	Texture t;
	if (!height)
		height = size;
	std::vector<Pixel> pixels(size * height, value);
	D3D11_TEXTURE2D_DESC d{};
	d.Width = size;
	d.Height = height;
	d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
	d.Format = format;
	d.Usage = D3D11_USAGE_DEFAULT;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	D3D11_SUBRESOURCE_DATA initial{ pixels.data(), static_cast<UINT>(size * sizeof(Pixel)), 0 };
	Check(device->CreateTexture2D(&d, format == DXGI_FORMAT_R32G32B32A32_FLOAT ? &initial : nullptr, &t.resource));
	Check(device->CreateShaderResourceView(t.resource.Get(), nullptr, &t.srv));
	Check(device->CreateUnorderedAccessView(t.resource.Get(), nullptr, &t.uav));
	return t;
}
static ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* d, const std::filesystem::path& path)
{
	ComPtr<ID3DBlob> bytes, errors;
	PackageIncludes includes(path.parent_path().parent_path().parent_path());
	auto hr = D3DCompileFromFile(path.c_str(), nullptr, &includes, "main", "cs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS, 0, &bytes, &errors);
	if (errors)
		std::fprintf(stderr, "%.*s\n", static_cast<int>(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
	Check(hr);
	ComPtr<ID3D11ComputeShader> cs;
	Check(d->CreateComputeShader(bytes->GetBufferPointer(), bytes->GetBufferSize(), nullptr, &cs));
	return cs;
}
static void Dispatch(ID3D11DeviceContext* c, ID3D11ComputeShader* cs, std::array<ID3D11ShaderResourceView*, 5> views, ID3D11UnorderedAccessView* output)
{
	c->CSSetShader(cs, nullptr, 0);
	c->CSSetShaderResources(0, 5, views.data());
	c->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
	c->Dispatch(1, 1, 1);
	views = {};
	output = nullptr;
	c->CSSetShaderResources(0, 5, views.data());
	c->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
}
static Pixel Read(ID3D11Device* d, ID3D11DeviceContext* c, ID3D11Texture2D* source, UINT x = 0)
{
	D3D11_TEXTURE2D_DESC desc{};
	source->GetDesc(&desc);
	Require(x < desc.Width);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(d->CreateTexture2D(&desc, nullptr, &staging));
	c->CopyResource(staging.Get(), source);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	Check(c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
	Pixel p{};
	if (desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT)
		std::memcpy(p.data(), static_cast<const char*>(mapped.pData) + x * sizeof(p), sizeof(p));
	else {
		Require(desc.Format == DXGI_FORMAT_R11G11B10_FLOAT);
		unsigned packed;
		std::memcpy(&packed, mapped.pData, sizeof(packed));
		for (unsigned channel = 0; channel < 3; ++channel) {
			unsigned fractionBits = channel == 2 ? 5 : 6;
			unsigned bits = (packed >> (channel * 11)) & ((1u << (fractionBits + 5)) - 1);
			unsigned exponent = bits >> fractionBits, fraction = bits & ((1u << fractionBits) - 1);
			Require(exponent != 31);  // Actual typed UAV must not contain infinity/NaN.
			p[channel] = std::ldexp(static_cast<float>(fraction + (exponent ? 1u << fractionBits : 0u)),
				static_cast<int>(exponent ? exponent : 1) - 15 - static_cast<int>(fractionBits));
		}
		p[3] = 1;
	}
	c->Unmap(staging.Get(), 0);
	return p;
}
static void VerifyUnitSampling(ID3D11Device* d, ID3D11DeviceContext* c, ID3D11ShaderResourceView* source)
{
	constexpr char code[] = R"(
Texture2D<float4> Source : register(t0);
SamplerState Filtering : register(s0);
RWTexture2D<float4> Result : register(u0);
[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
    float2 uv = (float2(id.x % 8, id.x / 8) - 2.5) / 3.0;
    float2 raw = Source.SampleLevel(Filtering, uv, 0).xy;
    Result[uint2(id.x, 0)] = float4(raw, raw.y / raw.x, 1);
})";
	ComPtr<ID3DBlob> bytes, errors;
	Check(D3DCompile(code, sizeof(code) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS, 0, &bytes, &errors));
	ComPtr<ID3D11ComputeShader> shader;
	Check(d->CreateComputeShader(bytes->GetBufferPointer(), bytes->GetBufferSize(), nullptr, &shader));
	auto output = Make(d, 64, {}, DXGI_FORMAT_R32G32B32A32_FLOAT, 1);
	for (const auto filter : { D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_FILTER_ANISOTROPIC }) {
		for (const auto address : { D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_MIRROR }) {
			D3D11_SAMPLER_DESC desc{};
			desc.Filter = filter;
			desc.AddressU = desc.AddressV = desc.AddressW = address;
			desc.MaxAnisotropy = 4;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			ComPtr<ID3D11SamplerState> sampler;
			Check(d->CreateSamplerState(&desc, &sampler));
			auto* pointer = sampler.Get();
			c->CSSetSamplers(0, 1, &pointer);
			Dispatch(c, shader.Get(), { source }, output.uav.Get());
			for (UINT x = 0; x < 64; ++x) {
				const auto value = Read(d, c, output.resource.Get(), x);
				Require(value[0] > 0 && value[0] == value[1] && std::abs(value[2] - 1) < 1e-6f);
			}
		}
	}
}
int main(int argc, char** argv)
{
	Require(argc == 2);
	D3D11_SAMPLER_DESC sampler{};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	Require(SupportedExposureSampler(sampler));
	for (auto address : { D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_MIRROR_ONCE }) {
		sampler.AddressU = sampler.AddressV = address;
		Require(SupportedExposureSampler(sampler));
	}
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	Require(!SupportedExposureSampler(sampler));
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	Require(!SupportedExposureSampler(sampler));
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	Require(!SupportedExposureSampler(sampler));
	ComPtr<ID3D11Device> d;
	ComPtr<ID3D11DeviceContext> c;
	const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &d, nullptr, &c));
	const std::filesystem::path folder(argv[1]);
	auto capture = Compile(d.Get(), folder / "ColorExposureCS.hlsl");
	auto prepare = Compile(d.Get(), folder / "ColorPrepareCS.hlsl");
	auto reconstruct = Compile(d.Get(), folder / "ColorReconstructCS.hlsl");
	auto measure = Compile(d.Get(), folder / "ColorMeasureCS.hlsl");
	auto average = Make(d.Get(), 1, { 2, 0.5f, 0, 0 });
	auto exposure = Make(d.Get(), kExposureSnapshotPixels, {}, DXGI_FORMAT_R32G32B32A32_FLOAT, 1);
	Dispatch(c.Get(), capture.Get(), { average.srv.Get() }, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get()) == Pixel{ 2, 0.5f, 0.25f, 1 });
	auto quad = Make(d.Get(), 2, { 2, 0.5f, 0, 0 });
	Dispatch(c.Get(), capture.Get(), { quad.srv.Get() }, exposure.uav.Get());
	for (UINT i = 0; i < kExposureSnapshotPixels; ++i)
		Require(Read(d.Get(), c.Get(), exposure.resource.Get(), i) == Pixel{ 2, 0.5f, 0.25f, 1 });
	for (UINT changed = 0; changed < 4; ++changed) {
		for (const Pixel value : { Pixel{ 4, 1, 0, 0 }, Pixel{ 0, 1, 0, 0 },
				 Pixel{ 2, std::nextafter(0.5f, 1.0f), 0, 0 } }) {
			std::array<Pixel, 4> pixels;
			pixels.fill({ 2, 0.5f, 0, 0 });
			pixels[changed] = value;
			c->UpdateSubresource(quad.resource.Get(), 0, nullptr, pixels.data(), 2 * sizeof(Pixel), 0);
			Dispatch(c.Get(), capture.Get(), { quad.srv.Get() }, exposure.uav.Get());
			Require(Read(d.Get(), c.Get(), exposure.resource.Get())[3] == 3);
			const auto raw = Read(d.Get(), c.Get(), exposure.resource.Get(), 1 + changed);
			Require(raw[0] == value[0] && raw[1] == value[1]);
		}
	}
	for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
		std::array<Pixel, 4> pixels;
		pixels.fill({ 2, 0.5f, 0, 0 });
		pixels[3][0] = bad;
		c->UpdateSubresource(quad.resource.Get(), 0, nullptr, pixels.data(), 2 * sizeof(Pixel), 0);
		Dispatch(c.Get(), capture.Get(), { quad.srv.Get() }, exposure.uav.Get());
		Require(Read(d.Get(), c.Get(), exposure.resource.Get())[3] == 0);
	}
	const std::array<Pixel, 4> liveUnit{ { { 0.1044921875f, 0.1044921875f, 0, 0 }, { 0.10546875f, 0.10546875f, 0, 0 },
		{ 0.111328125f, 0.111328125f, 0, 0 }, { 0.1123046875f, 0.1123046875f, 0, 0 } } };
	c->UpdateSubresource(quad.resource.Get(), 0, nullptr, liveUnit.data(), 2 * sizeof(Pixel), 0);
	Dispatch(c.Get(), capture.Get(), { quad.srv.Get() }, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get()) == Pixel{ liveUnit[0][0], liveUnit[0][1], 1, 1 });
	VerifyUnitSampling(d.Get(), c.Get(), quad.srv.Get());
	for (UINT changed = 0; changed < 4; ++changed) {
		Require(Read(d.Get(), c.Get(), exposure.resource.Get(), 1 + changed) == Pixel{ liveUnit[changed][0], liveUnit[changed][1], 1, 1 });
		for (const Pixel bad : { Pixel{ 0, 0, 0, 0 }, Pixel{ -1, -1, 0, 0 },
				 Pixel{ liveUnit[changed][0], std::nextafter(liveUnit[changed][1], 1.0f), 0, 0 } }) {
			auto pixels = liveUnit;
			pixels[changed] = bad;
			c->UpdateSubresource(quad.resource.Get(), 0, nullptr, pixels.data(), 2 * sizeof(Pixel), 0);
			Dispatch(c.Get(), capture.Get(), { quad.srv.Get() }, exposure.uav.Get());
			Require(Read(d.Get(), c.Get(), exposure.resource.Get())[3] != 1);
		}
		c->UpdateSubresource(quad.resource.Get(), 0, nullptr, liveUnit.data(), 2 * sizeof(Pixel), 0);
		Dispatch(c.Get(), capture.Get(), { quad.srv.Get() }, exposure.uav.Get());
	}
	auto packedAverage = Make(d.Get(), 2, {}, DXGI_FORMAT_R11G11B10_FLOAT);
	const Pixel packedValue{ 2, 0.5f, 0, 0 };
	c->ClearUnorderedAccessViewFloat(packedAverage.uav.Get(), packedValue.data());
	Dispatch(c.Get(), capture.Get(), { packedAverage.srv.Get() }, exposure.uav.Get());
	for (UINT i = 0; i < kExposureSnapshotPixels; ++i)
		Require(Read(d.Get(), c.Get(), exposure.resource.Get(), i) == Pixel{ 2, 0.5f, 0.25f, 1 });
	auto oversized = Make(d.Get(), 4, { 2, 0.5f, 0, 0 });
	Dispatch(c.Get(), capture.Get(), { oversized.srv.Get() }, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get())[3] == 0);
	// Load coordinates are relative to the bound view, not the resource's mip 0.
	std::array<Pixel, 16> mip0;
	std::array<Pixel, 4> mip1;
	mip0.fill({ 8, 2, 0, 0 });
	mip1.fill({ 4, 0.5f, 0, 0 });
	const Pixel mip2{ 2, 0.5f, 0, 0 };
	const D3D11_SUBRESOURCE_DATA mipData[]{
		{ mip0.data(), 4 * sizeof(Pixel), 0 },
		{ mip1.data(), 2 * sizeof(Pixel), 0 },
		{ mip2.data(), sizeof(Pixel), 0 }
	};
	D3D11_TEXTURE2D_DESC mipDesc{};
	mipDesc.Width = mipDesc.Height = 4;
	mipDesc.MipLevels = 3;
	mipDesc.ArraySize = mipDesc.SampleDesc.Count = 1;
	mipDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	mipDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	ComPtr<ID3D11Texture2D> mipTexture;
	Check(d->CreateTexture2D(&mipDesc, mipData, &mipTexture));
	D3D11_SHADER_RESOURCE_VIEW_DESC mipView{};
	mipView.Format = mipDesc.Format;
	mipView.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	mipView.Texture2D = { 1, 1 };
	ComPtr<ID3D11ShaderResourceView> mipSrv;
	Check(d->CreateShaderResourceView(mipTexture.Get(), &mipView, &mipSrv));
	Dispatch(c.Get(), capture.Get(), { mipSrv.Get() }, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get()) == Pixel{ 4, 0.5f, 0.125f, 1 });
	mipSrv.Reset();
	mipView.Texture2D.MipLevels = 2;
	Check(d->CreateShaderResourceView(mipTexture.Get(), &mipView, &mipSrv));
	Dispatch(c.Get(), capture.Get(), { mipSrv.Get() }, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get())[3] == 0);
	Dispatch(c.Get(), capture.Get(), { average.srv.Get() }, exposure.uav.Get());
	const Pixel b{ 0.18f, 0.09f, 0.01f, 0.375f };
	auto baseline = Make(d.Get(), 8, b);
	auto prepared = Make(d.Get(), 8, {}), result = Make(d.Get(), 8, {}), neural = Make(d.Get(), 8, { 0.8f, 0.2f, 0.1f, 0 });
	struct Constants
	{
		unsigned x = 0, y = 0, w = 8, h = 8, mode = 1, domain = 1, transform = 1, flags = 12;
		float multiplier = 1, detail = 1, appearance = 0, stops = 1;
	} constants;
	static_assert(sizeof(Constants) == 48);
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = sizeof(Constants);
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	ComPtr<ID3D11Buffer> cb;
	Check(d->CreateBuffer(&desc, nullptr, &cb));
	auto* cbPointer = cb.Get();
	c->CSSetConstantBuffers(0, 1, &cbPointer);
	const auto update = [&]() { c->UpdateSubresource(cb.Get(), 0, nullptr, &constants, 0, 0); };
	update();
	Dispatch(c.Get(), prepare.Get(), { baseline.srv.Get(), nullptr, nullptr, exposure.srv.Get() }, prepared.uav.Get());
	Profile resolved;
	Require(ResolveExposureProfile({ Domain::Linear, Transform::LinearToSRGB, 1, ExposureSource::CapturedHDR }, EvaluateHDRExposure(2, 0.5f), resolved));
	RGB expected{};
	Require(Forward({ b[0], b[1], b[2] }, resolved, expected));
	auto p = Read(d.Get(), c.Get(), prepared.resource.Get());
	for (unsigned i = 0; i < 3; ++i) Require(std::abs(p[i] - expected[i]) < 1e-5f);
	c->UpdateSubresource(quad.resource.Get(), 0, nullptr, liveUnit.data(), 2 * sizeof(Pixel), 0);
	Dispatch(c.Get(), capture.Get(), { quad.srv.Get() }, exposure.uav.Get());
	Dispatch(c.Get(), prepare.Get(), { baseline.srv.Get(), nullptr, nullptr, exposure.srv.Get() }, prepared.uav.Get());
	Require(ResolveExposureProfile({ Domain::Linear, Transform::LinearToSRGB, 1, ExposureSource::CapturedHDRPrevious }, EvaluateHDRExposure(liveUnit[0][0], liveUnit[0][1]), resolved));
	Require(Forward({ b[0], b[1], b[2] }, resolved, expected));
	p = Read(d.Get(), c.Get(), prepared.resource.Get());
	for (unsigned i = 0; i < 3; ++i) Require(std::abs(p[i] - expected[i]) < 1e-5f);
	Dispatch(c.Get(), reconstruct.Get(), { baseline.srv.Get(), prepared.srv.Get(), prepared.srv.Get(), exposure.srv.Get() }, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	constants.flags |= 2;
	update();
	Dispatch(c.Get(), reconstruct.Get(), { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get(), exposure.srv.Get() }, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	const Pixel zero{ 0, 1, 0, 0 };
	std::array<Pixel, 4> spatial;
	spatial.fill({ 2, 0.5f, 0, 0 });
	spatial[3] = { 4, 1, 0, 0 };
	c->UpdateSubresource(quad.resource.Get(), 0, nullptr, spatial.data(), 2 * sizeof(Pixel), 0);
	Dispatch(c.Get(), capture.Get(), { quad.srv.Get() }, exposure.uav.Get());
	constants.flags = 12;
	update();
	Dispatch(c.Get(), prepare.Get(), { baseline.srv.Get(), nullptr, nullptr, exposure.srv.Get() }, prepared.uav.Get());
	Require(Read(d.Get(), c.Get(), prepared.resource.Get())[0] == 0);
	Dispatch(c.Get(), reconstruct.Get(), { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get(), exposure.srv.Get() }, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	c->UpdateSubresource(average.resource.Get(), 0, nullptr, zero.data(), sizeof(Pixel), 0);
	Dispatch(c.Get(), capture.Get(), { average.srv.Get() }, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get())[3] == 2);
	constants.flags = 12;
	update();
	Dispatch(c.Get(), prepare.Get(), { baseline.srv.Get(), nullptr, nullptr, exposure.srv.Get() }, prepared.uav.Get());
	Require(Read(d.Get(), c.Get(), prepared.resource.Get())[0] == 0);
	Dispatch(c.Get(), reconstruct.Get(), { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get(), exposure.srv.Get() }, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	D3D11_BUFFER_DESC statsDesc{};
	statsDesc.ByteWidth = 24 * sizeof(float);
	statsDesc.Usage = D3D11_USAGE_DEFAULT;
	statsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	statsDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	statsDesc.StructureByteStride = sizeof(Pixel);
	ComPtr<ID3D11Buffer> gpu, staging;
	Check(d->CreateBuffer(&statsDesc, nullptr, &gpu));
	D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
	uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uv.Buffer.NumElements = 6;
	ComPtr<ID3D11UnorderedAccessView> uav;
	Check(d->CreateUnorderedAccessView(gpu.Get(), &uv, &uav));
	statsDesc.Usage = D3D11_USAGE_STAGING;
	statsDesc.BindFlags = 0;
	statsDesc.MiscFlags = 0;
	statsDesc.StructureByteStride = 0;
	statsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	Check(d->CreateBuffer(&statsDesc, nullptr, &staging));
	const auto statistics = [&]() {
		Dispatch(c.Get(), measure.Get(), { baseline.srv.Get(), neural.srv.Get(), result.srv.Get(), exposure.srv.Get(), prepared.srv.Get() }, uav.Get());
		c->CopyResource(staging.Get(), gpu.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::array<float, 24> values;
		std::memcpy(values.data(), mapped.pData, sizeof(values));
		c->Unmap(staging.Get(), 0);
		return values;
	};
	auto v = statistics();
	Require(v[16] == 64 && v[17] == 64 && v[19] == 0 && v[23] == 2);
	// Same-sign negative source is not a measured unit ratio.
	const Pixel negative{ -1, -1, 0, 0 };
	c->UpdateSubresource(average.resource.Get(), 0, nullptr, negative.data(), sizeof(Pixel), 0);
	Dispatch(c.Get(), capture.Get(), { average.srv.Get() }, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get())[3] == 0);
	// Invalid PREPARED inverse must be counted even when neural inverse is valid.
	constants.flags = 0;
	constants.transform = 2;
	update();
	prepared = Make(d.Get(), 8, { 1, 1, 1, 1 });
	Dispatch(c.Get(), reconstruct.Get(), { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	v = statistics();
	Require(v[16] == 0 && v[17] == 64 && v[7] == 0);
	// A finite encoded model output can decode beyond packed/half range.
	constants.transform = 1;
	constants.multiplier = 1.0f / 256.0f;
	constants.flags = 0x100;
	update();
	Dispatch(c.Get(), prepare.Get(), { baseline.srv.Get() }, prepared.uav.Get());
	neural = Make(d.Get(), 8, { 12, 12, 12, 0 });
	auto packed = Make(d.Get(), 8, {}, DXGI_FORMAT_R11G11B10_FLOAT);
	Dispatch(c.Get(), reconstruct.Get(), { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, packed.uav.Get());
	p = Read(d.Get(), c.Get(), packed.resource.Get());
	for (unsigned i = 0; i < 3; ++i) Require(std::abs(p[i] - b[i]) < 0.002f);
	// The same decision also applies before FP16 stores; output remains baseline.
	constants.flags = 0x200;
	update();
	Dispatch(c.Get(), reconstruct.Get(), { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	v = statistics();
	Require(v[17] == 64);
	std::puts("Exposure/codec/storage/t4/A-B/measurement WARP checks passed; engine hooks and NGX are not exercised.");
	return 0;
}
