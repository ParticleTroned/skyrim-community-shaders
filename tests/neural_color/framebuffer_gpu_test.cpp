#include "Features/Upscaling/NeuralRendering/FramebufferTransaction.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

using Microsoft::WRL::ComPtr;
static void Require(bool value)
{
	if (!value)
		std::abort();
}
static void Check(HRESULT hr) { Require(SUCCEEDED(hr)); }

int main()
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL level{};
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &device, &level, &context));
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = 8;
	desc.Height = 4;
	desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET;
	std::array<unsigned, 32> baseline{};
	baseline.fill(0x80402010u);
	D3D11_SUBRESOURCE_DATA initial{ baseline.data(), 8 * sizeof(unsigned), 0 };
	ComPtr<ID3D11Texture2D> destination, scratch, staging, wrongSize;
	ComPtr<ID3D11RenderTargetView> rtv;
	Check(device->CreateTexture2D(&desc, &initial, &destination));
	Check(device->CreateRenderTargetView(destination.Get(), nullptr, &rtv));
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	Check(device->CreateTexture2D(&desc, nullptr, &scratch));
	desc.Width = 4;
	Check(device->CreateTexture2D(&desc, nullptr, &wrongSize));
	desc.Width = 8;
	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	Check(device->CreateTexture2D(&desc, nullptr, &staging));
	auto read = [&]() {
		context->CopyResource(staging.Get(), destination.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::array<unsigned, 32> result{};
		for (unsigned y = 0; y < 4; ++y) {
			const auto* row = reinterpret_cast<const unsigned*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
			for (unsigned x = 0; x < 8; ++x) result[y * 8 + x] = row[x];
		}
		context->Unmap(staging.Get(), 0);
		return result;
	};
	auto* bound = rtv.Get();
	context->OMSetRenderTargets(1, &bound, nullptr);
	{
		NeuralRendering::FramebufferTransaction bad(context.Get(), destination.Get(), wrongSize.Get());
		Require(!bad.IsValid());
		Require(!bad.Commit(true));
	}
	std::array<unsigned, 16> left{}, right{};
	left.fill(0xFF0000FFu);
	right.fill(0xFF00FF00u);
	const D3D11_BOX leftBox{ 0, 0, 0, 4, 4, 1 }, rightBox{ 4, 0, 0, 8, 4, 1 };
	{
		NeuralRendering::FramebufferTransaction failed(context.Get(), destination.Get(), scratch.Get());
		Require(failed.IsValid());
		context->UpdateSubresource(scratch.Get(), 0, &leftBox, left.data(), 4 * sizeof(unsigned), 0);
		Require(!failed.Commit(false));
	}
	Require(read() == baseline);
	{
		NeuralRendering::FramebufferTransaction pair(context.Get(), destination.Get(), scratch.Get());
		Require(pair.IsValid());
		context->UpdateSubresource(scratch.Get(), 0, &leftBox, left.data(), 4 * sizeof(unsigned), 0);
		context->UpdateSubresource(scratch.Get(), 0, &rightBox, right.data(), 4 * sizeof(unsigned), 0);
		Require(pair.Commit(true));
	}
	const auto result = read();
	for (unsigned y = 0; y < 4; ++y)
		for (unsigned x = 0; x < 8; ++x)
			Require(result[y * 8 + x] == (x < 4 ? left[0] : right[0]));
	ComPtr<ID3D11RenderTargetView> restored;
	context->OMGetRenderTargets(1, &restored, nullptr);
	Require(restored.Get() == rtv.Get());
	// A post-UI transaction must copy the current destination again.
	context->UpdateSubresource(destination.Get(), 0, nullptr, baseline.data(), 8 * sizeof(unsigned), 0);
	{
		NeuralRendering::FramebufferTransaction postUi(context.Get(), destination.Get(), scratch.Get());
		Require(postUi.Commit(true));
	}
	Require(read() == baseline);
	std::puts("Passed non-UAV framebuffer stereo commit, failed-eye preservation, fresh copy and RTV restoration");
}
