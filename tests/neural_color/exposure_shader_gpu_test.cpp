// Production exposure/prepare/reconstruct/measurement shaders on Windows WARP.
#include "Features/Upscaling/NeuralRendering/ExposurePolicy.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace NeuralRendering::Color;
using Pixel = std::array<float, 4>;
static void Require(bool value) { if (!value) { std::fprintf(stderr, "Exposure WARP assertion failed\n"); std::abort(); } }
static void Check(HRESULT result) { Require(SUCCEEDED(result)); }
struct Texture { ComPtr<ID3D11Texture2D> resource; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; };
static Texture Make(ID3D11Device* device, UINT size, Pixel value, DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT)
{
	Texture t; std::vector<Pixel> pixels(size * size, value);
	D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = size; d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
	d.Format = format; d.Usage = D3D11_USAGE_DEFAULT;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	D3D11_SUBRESOURCE_DATA initial{ pixels.data(), static_cast<UINT>(size * sizeof(Pixel)), 0 };
	Check(device->CreateTexture2D(&d, format == DXGI_FORMAT_R32G32B32A32_FLOAT ? &initial : nullptr, &t.resource));
	Check(device->CreateShaderResourceView(t.resource.Get(), nullptr, &t.srv));
	Check(device->CreateUnorderedAccessView(t.resource.Get(), nullptr, &t.uav)); return t;
}
static ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* d, const std::filesystem::path& path)
{
	ComPtr<ID3DBlob> bytes, errors;
	auto hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS, 0, &bytes, &errors);
	if (errors) std::fprintf(stderr, "%.*s\n", static_cast<int>(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
	Check(hr); ComPtr<ID3D11ComputeShader> cs; Check(d->CreateComputeShader(bytes->GetBufferPointer(), bytes->GetBufferSize(), nullptr, &cs)); return cs;
}
static void Dispatch(ID3D11DeviceContext* c, ID3D11ComputeShader* cs, std::array<ID3D11ShaderResourceView*, 5> views, ID3D11UnorderedAccessView* output)
{
	c->CSSetShader(cs, nullptr, 0); c->CSSetShaderResources(0, 5, views.data()); c->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
	c->Dispatch(1, 1, 1); views = {}; output = nullptr;
	c->CSSetShaderResources(0, 5, views.data()); c->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
}
static Pixel Read(ID3D11Device* d, ID3D11DeviceContext* c, ID3D11Texture2D* source)
{
	D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc); desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging; Check(d->CreateTexture2D(&desc, nullptr, &staging)); c->CopyResource(staging.Get(), source);
	D3D11_MAPPED_SUBRESOURCE mapped{}; Check(c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
	Pixel p{};
	if (desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT) std::memcpy(p.data(), mapped.pData, sizeof(p));
	else {
		Require(desc.Format == DXGI_FORMAT_R11G11B10_FLOAT);
		unsigned packed; std::memcpy(&packed, mapped.pData, sizeof(packed));
		for (unsigned channel = 0; channel < 3; ++channel) {
			unsigned fractionBits = channel == 2 ? 5 : 6;
			unsigned bits = (packed >> (channel * 11)) & ((1u << (fractionBits + 5)) - 1);
			unsigned exponent = bits >> fractionBits, fraction = bits & ((1u << fractionBits) - 1);
			Require(exponent != 31); // Actual typed UAV must not contain infinity/NaN.
			p[channel] = std::ldexp(static_cast<float>(fraction + (exponent ? 1u << fractionBits : 0u)),
				static_cast<int>(exponent ? exponent : 1) - 15 - static_cast<int>(fractionBits));
		}
		p[3] = 1;
	}
	c->Unmap(staging.Get(), 0); return p;
}
int main(int argc, char** argv)
{
	Require(argc == 2); ComPtr<ID3D11Device> d; ComPtr<ID3D11DeviceContext> c;
	const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &d, nullptr, &c));
	const std::filesystem::path folder(argv[1]);
	auto capture = Compile(d.Get(), folder / "ColorExposureCS.hlsl");
	auto prepare = Compile(d.Get(), folder / "ColorPrepareCS.hlsl");
	auto reconstruct = Compile(d.Get(), folder / "ColorReconstructCS.hlsl");
	auto measure = Compile(d.Get(), folder / "ColorMeasureCS.hlsl");
	auto average = Make(d.Get(), 1, {2, 0.5f, 0, 0}), exposure = Make(d.Get(), 1, {});
	Dispatch(c.Get(), capture.Get(), {average.srv.Get()}, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get()) == Pixel{2, 0.5f, 0.25f, 1});
	const Pixel b{0.18f, 0.09f, 0.01f, 0.375f}; auto baseline = Make(d.Get(), 8, b);
	auto prepared = Make(d.Get(), 8, {}), result = Make(d.Get(), 8, {}), neural = Make(d.Get(), 8, {0.8f, 0.2f, 0.1f, 0});
	struct Constants { unsigned x=0,y=0,w=8,h=8,mode=1,domain=1,transform=1,flags=12; float multiplier=1,detail=1,appearance=0,stops=1; } constants;
	static_assert(sizeof(Constants) == 48);
	D3D11_BUFFER_DESC desc{}; desc.ByteWidth = sizeof(Constants); desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	ComPtr<ID3D11Buffer> cb; Check(d->CreateBuffer(&desc, nullptr, &cb)); auto* cbPointer = cb.Get(); c->CSSetConstantBuffers(0, 1, &cbPointer);
	const auto update = [&]() { c->UpdateSubresource(cb.Get(), 0, nullptr, &constants, 0, 0); };
	update();
	Dispatch(c.Get(), prepare.Get(), {baseline.srv.Get(), nullptr, nullptr, exposure.srv.Get()}, prepared.uav.Get());
	Profile resolved; Require(ResolveExposureProfile({Domain::Linear, Transform::LinearToSRGB, 1, ExposureSource::CapturedHDR}, EvaluateHDRExposure(2, 0.5f), resolved));
	RGB expected{}; Require(Forward({b[0],b[1],b[2]}, resolved, expected)); auto p = Read(d.Get(), c.Get(), prepared.resource.Get());
	for (unsigned i=0; i<3; ++i) Require(std::abs(p[i]-expected[i]) < 1e-5f);
	Dispatch(c.Get(), reconstruct.Get(), {baseline.srv.Get(), prepared.srv.Get(), prepared.srv.Get(), exposure.srv.Get()}, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	constants.flags |= 2; update();
	Dispatch(c.Get(), reconstruct.Get(), {baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get(), exposure.srv.Get()}, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	const Pixel zero{0,1,0,0}; c->UpdateSubresource(average.resource.Get(), 0, nullptr, zero.data(), sizeof(Pixel), 0);
	Dispatch(c.Get(), capture.Get(), {average.srv.Get()}, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get())[3] == 2);
	constants.flags = 12; update();
	Dispatch(c.Get(), prepare.Get(), {baseline.srv.Get(), nullptr, nullptr, exposure.srv.Get()}, prepared.uav.Get());
	Require(Read(d.Get(), c.Get(), prepared.resource.Get())[0] == 0);
	Dispatch(c.Get(), reconstruct.Get(), {baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get(), exposure.srv.Get()}, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	D3D11_BUFFER_DESC statsDesc{}; statsDesc.ByteWidth = 24 * sizeof(float); statsDesc.Usage = D3D11_USAGE_DEFAULT;
	statsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS; statsDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; statsDesc.StructureByteStride = sizeof(Pixel);
	ComPtr<ID3D11Buffer> gpu, staging; Check(d->CreateBuffer(&statsDesc, nullptr, &gpu));
	D3D11_UNORDERED_ACCESS_VIEW_DESC uv{}; uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; uv.Buffer.NumElements = 6;
	ComPtr<ID3D11UnorderedAccessView> uav; Check(d->CreateUnorderedAccessView(gpu.Get(), &uv, &uav));
	statsDesc.Usage = D3D11_USAGE_STAGING; statsDesc.BindFlags=0; statsDesc.MiscFlags=0; statsDesc.StructureByteStride=0; statsDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
	Check(d->CreateBuffer(&statsDesc, nullptr, &staging));
	const auto statistics = [&]() {
		Dispatch(c.Get(), measure.Get(), {baseline.srv.Get(), neural.srv.Get(), result.srv.Get(), exposure.srv.Get(), prepared.srv.Get()}, uav.Get());
		c->CopyResource(staging.Get(), gpu.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{}; Check(c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::array<float,24> values; std::memcpy(values.data(), mapped.pData, sizeof(values));
		c->Unmap(staging.Get(), 0); return values;
	};
	auto v = statistics(); Require(v[16] == 64 && v[17] == 64 && v[19] == 0 && v[23] == 2);
	// Same-sign negative source is not a measured unit ratio.
	const Pixel negative{-1,-1,0,0}; c->UpdateSubresource(average.resource.Get(), 0, nullptr, negative.data(), sizeof(Pixel), 0);
	Dispatch(c.Get(), capture.Get(), {average.srv.Get()}, exposure.uav.Get());
	Require(Read(d.Get(), c.Get(), exposure.resource.Get())[3] == 0);
	// Invalid PREPARED inverse must be counted even when neural inverse is valid.
	constants.flags=0; constants.transform=2; update();
	prepared = Make(d.Get(), 8, {1,1,1,1});
	Dispatch(c.Get(), reconstruct.Get(), {baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get()}, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	v = statistics(); Require(v[16] == 0 && v[17] == 64 && v[7] == 0);
	// A finite encoded model output can decode beyond packed/half range.
	constants.transform=1; constants.multiplier=1.0f/256.0f; constants.flags=0x100; update();
	Dispatch(c.Get(), prepare.Get(), {baseline.srv.Get()}, prepared.uav.Get());
	neural = Make(d.Get(), 8, {12,12,12,0});
	auto packed = Make(d.Get(), 8, {}, DXGI_FORMAT_R11G11B10_FLOAT);
	Dispatch(c.Get(), reconstruct.Get(), {baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get()}, packed.uav.Get());
	p = Read(d.Get(), c.Get(), packed.resource.Get());
	for (unsigned i=0; i<3; ++i) Require(std::abs(p[i]-b[i]) < 0.002f);
	// The same decision also applies before FP16 stores; output remains baseline.
	constants.flags=0x200; update();
	Dispatch(c.Get(), reconstruct.Get(), {baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get()}, result.uav.Get());
	Require(Read(d.Get(), c.Get(), result.resource.Get()) == b);
	v = statistics(); Require(v[17] == 64);
	std::puts("Exposure/codec/storage/t4/A-B/measurement WARP checks passed; engine hooks and NGX are not exercised.");
	return 0;
}
