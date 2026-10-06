#include "Features/Upscaling/NeuralRendering/FramebufferTransaction.h"
#include "Features/Upscaling/NeuralRendering/MainDepthPresentation.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <d3dcompiler.h>
#include <filesystem>
#include <vector>

using Microsoft::WRL::ComPtr;
static void Require(bool value)
{
	if (!value)
		std::abort();
}
static void Check(HRESULT hr) { Require(SUCCEEDED(hr)); }

static void VerifyPostUpscaleHiddenArea(ID3D11Device* device, ID3D11DeviceContext* context, const char* shaderPath)
{
	constexpr unsigned eyeWidth = 32, height = 24, width = eyeWidth * 2;
	ComPtr<ID3DBlob> bytecode, errors;
	Check(D3DCompileFromFile(std::filesystem::path(shaderPath).c_str(), nullptr, nullptr,
		"main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS,
		0, &bytecode, &errors));
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader));
	std::array<float, width * height> depths{};
	std::array<unsigned, width * height> colors{};
	colors.fill(0x80402010u);
	for (unsigned y = 0; y < height; ++y)
		for (unsigned x = 0; x < width; ++x)
			depths[y * width + x] = (x < 4 || x >= width - 4) ? 0.0f : 0.5f;
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.Format = DXGI_FORMAT_R32_FLOAT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA initial{ depths.data(), width * sizeof(float), 0 };
	ComPtr<ID3D11Texture2D> depth, color, staging;
	Check(device->CreateTexture2D(&desc, &initial, &depth));
	ComPtr<ID3D11ShaderResourceView> depthSRV;
	Check(device->CreateShaderResourceView(depth.Get(), nullptr, &depthSRV));
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	initial.pSysMem = colors.data();
	Check(device->CreateTexture2D(&desc, &initial, &color));
	ComPtr<ID3D11UnorderedAccessView> colorUAV;
	Check(device->CreateUnorderedAccessView(color.Get(), nullptr, &colorUAV));
	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	Check(device->CreateTexture2D(&desc, nullptr, &staging));
	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.ByteWidth = 8 * sizeof(unsigned);
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	ComPtr<ID3D11Buffer> constants;
	Check(device->CreateBuffer(&bufferDesc, nullptr, &constants));
	auto run = [&](unsigned depthEyeWidth, unsigned depthHeight) {
		context->UpdateSubresource(color.Get(), 0, nullptr, colors.data(), width * sizeof(unsigned), 0);
		context->CSSetShader(shader.Get(), nullptr, 0);
		auto* srv = depthSRV.Get();
		auto* uav = colorUAV.Get();
		auto* cb = constants.Get();
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetConstantBuffers(0, 1, &cb);
		for (unsigned eye = 0; eye < 2; ++eye) {
			const std::array<unsigned, 8> values{
				eye * depthEyeWidth, eye * eyeWidth, 0, 0,
				depthEyeWidth, depthHeight, eyeWidth, height
			};
			context->UpdateSubresource(cb, 0, nullptr, values.data(), 0, 0);
			context->Dispatch((eyeWidth + 7) / 8, (height + 7) / 8, 1);
		}
		uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CopyResource(staging.Get(), color.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		bool exact = true;
		for (unsigned y = 0; y < height; ++y) {
			const auto* row = reinterpret_cast<const unsigned*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
			for (unsigned x = 0; x < width; ++x) {
				const bool hidden = x < 6 || x >= width - 6;
				exact = exact && row[x] == (hidden ? 0u : colors[y * width + x]);
			}
		}
		context->Unmap(staging.Get(), 0);
		return exact;
	};
	const auto identity = reinterpret_cast<std::uintptr_t>(depth.Get());
	const NeuralRendering::MainDepthPresentationProof proof{ 7, identity };
	Require(proof.SupportsOutputLayout(eyeWidth / 2, height / 2, eyeWidth, height, 7, identity));
	Require(run(eyeWidth, height));
	Require(!run(eyeWidth / 2, height / 2));
	std::puts("Passed post-upscale hidden-area mapping; reduced-grid control detects wrong eye/depth sampling");
}

int main(int argc, char** argv)
{
	Require(argc == 2);
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
	VerifyPostUpscaleHiddenArea(device.Get(), context.Get(), argv[1]);
}
