#define NOMINMAX
#include "Utils/ComputeState.h"
#include "Utils/DeviceContextState.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#include <iostream>
#include <stdexcept>
#include <string>

// Resource naming is observed here; the production helper performs the debug-name write.
namespace Util
{
	std::string resourceName;
	void SetResourceName(ID3D11DeviceChild* resource, const char* format, ...)
	{
		if (!resource)
			throw std::runtime_error("Cannot name a null state");
		char buffer[256]{};
		va_list arguments;
		va_start(arguments, format);
		vsnprintf(buffer, sizeof(buffer), format, arguments);
		va_end(arguments);
		resourceName = buffer;
	}
}
static void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
static void Check(HRESULT result) { winrt::check_hresult(result); }
static void CheckComputeState(ID3D11Device* device, ID3D11DeviceContext1* context)
{
	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.ByteWidth = 65536;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	winrt::com_ptr<ID3D11Buffer> buffer;
	Check(device->CreateBuffer(&bufferDesc, nullptr, buffer.put()));
	Util::SetResourceName(buffer.get(), "Test::ComputeCB");
	std::array<ID3D11Buffer*, 4> buffers{ buffer.get(), nullptr, buffer.get(), nullptr };
	context->CSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
	const UINT windowFirst = 16, windowCount = 16;
	D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
	Check(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)));
	if (options.ConstantBufferOffsetting)
		context->CSSetConstantBuffers1(0, 1, buffers.data(), &windowFirst, &windowCount);
	if (options.ConstantBufferOffsetting)
		context->CSSetConstantBuffers1(12, 1, buffers.data(), &windowFirst, &windowCount);
	else
		context->CSSetConstantBuffers(12, 1, buffers.data());
	D3D11_TEXTURE2D_DESC description{};
	description.Width = description.Height = 16;
	description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
	description.Format = DXGI_FORMAT_R32_FLOAT;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	std::array<winrt::com_ptr<ID3D11Texture2D>, 2> textures;
	for (std::size_t index = 0; index < textures.size(); ++index) {
		Check(device->CreateTexture2D(&description, nullptr, textures[index].put()));
		Util::SetResourceName(textures[index].get(), "Test::ComputeTexture%zu", index);
	}
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	Check(device->CreateShaderResourceView(textures[0].get(), nullptr, srv.put()));
	Check(device->CreateUnorderedAccessView(textures[1].get(), nullptr, uav.put()));
	Util::SetResourceName(srv.get(), "Test::ComputeInput SRV");
	Util::SetResourceName(uav.get(), "Test::ComputeOutput UAV");
	auto* input = srv.get();
	auto* output = uav.get();
	context->CSSetShaderResources(2, 1, &input);
	context->CSSetUnorderedAccessViews(3, 1, &output, nullptr);
	static constexpr char code[] = "[numthreads(1,1,1)] void main() {}";
	winrt::com_ptr<ID3DBlob> bytecode;
	Check(D3DCompile(code, sizeof(code) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, bytecode.put(), nullptr));
	winrt::com_ptr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.put()));
	Util::SetResourceName(shader.get(), "Test::ComputeShader");
	context->CSSetShader(shader.get(), nullptr, 0);
	D3D11_QUERY_DESC predicateDesc{ D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
	winrt::com_ptr<ID3D11Predicate> predicate;
	Check(device->CreatePredicate(&predicateDesc, predicate.put()));
	Util::SetResourceName(predicate.get(), "Test::ComputePredicate");
	context->SetPredication(predicate.get(), TRUE);
	{
		D3DState::ComputeState restore(context, 4, true);
		winrt::com_ptr<ID3D11Predicate> current;
		BOOL value = TRUE;
		context->GetPredication(current.put(), &value);
		Require(!current, "compute scope suppresses native predication");
		context->CSSetShader(nullptr, nullptr, 0);
		std::array<ID3D11Buffer*, 4> emptyBuffers{};
		context->CSSetConstantBuffers(0, 4, emptyBuffers.data());
		context->CSSetConstantBuffers(12, 1, emptyBuffers.data());
		input = nullptr;
		output = nullptr;
		context->CSSetShaderResources(2, 1, &input);
		context->CSSetUnorderedAccessViews(3, 1, &output, nullptr);
	}
	std::array<ID3D11Buffer*, 4> restored{};
	std::array<UINT, 4> first{}, count{};
	context->CSGetConstantBuffers1(0, 4, restored.data(), first.data(), count.data());
	const bool buffersMatch = restored == buffers;
	for (auto* item : restored)
		if (item)
			item->Release();
	Require(buffersMatch, "compute scope restores full and null constant buffers");
	if (options.ConstantBufferOffsetting)
		Require(first[0] == windowFirst && count[0] == windowCount, "compute scope restores CB window");
	Require(first[2] == 0 && count[2] == D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT, "full CB retains unbounded binding");
	winrt::com_ptr<ID3D11Buffer> restoredFrame;
	UINT frameFirst = 0, frameCount = 0;
	context->CSGetConstantBuffers1(12, 1, restoredFrame.put(), &frameFirst, &frameCount);
	Require(restoredFrame == buffer, "compute scope restores the scene framebuffer binding");
	Require(frameFirst == (options.ConstantBufferOffsetting ? windowFirst : 0) &&
				frameCount == (options.ConstantBufferOffsetting ? windowCount : D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT),
		"compute scope restores the scene framebuffer window");
	winrt::com_ptr<ID3D11ShaderResourceView> restoredSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView> restoredUAV;
	winrt::com_ptr<ID3D11ComputeShader> restoredShader;
	winrt::com_ptr<ID3D11Predicate> restoredPredicate;
	BOOL predicateValue = FALSE;
	context->CSGetShaderResources(2, 1, restoredSRV.put());
	context->CSGetUnorderedAccessViews(3, 1, restoredUAV.put());
	context->CSGetShader(restoredShader.put(), nullptr, nullptr);
	context->GetPredication(restoredPredicate.put(), &predicateValue);
	Require(restoredSRV == srv && restoredUAV == uav && restoredShader == shader, "compute bindings restored");
	Require(restoredPredicate == predicate && predicateValue, "compute predication restored");
	context->ClearState();
}

int main()
{
	try {
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		winrt::com_ptr<ID3D11DeviceContext1> context1;
		winrt::com_ptr<ID3DDeviceContextState> isolated;
		Require(Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, isolated, "Test::Isolated"), "create isolated state");
		Require(Util::resourceName == "Test::Isolated", "isolated state receives resource name");
		auto* originalIsolated = isolated.get();
		Require(!Util::CreateIsolatedDeviceContextState(nullptr, context.get(), context1, isolated, "Invalid") && isolated.get() == originalIsolated, "failed initialization retains valid state");
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = 64;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		winrt::com_ptr<ID3D11Buffer> buffer;
		Check(device->CreateBuffer(&bufferDesc, nullptr, buffer.put()));
		Util::SetResourceName(buffer.get(), "Test::ContextConstantBuffer");
		auto* cb = buffer.get();
		context->VSSetConstantBuffers(3, 1, &cb);
		context->PSSetConstantBuffers(4, 1, &cb);
		context->GSSetConstantBuffers(5, 1, &cb);
		context->CSSetConstantBuffers(6, 1, &cb);
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = 32;
		textureDesc.Height = 32;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		std::array<winrt::com_ptr<ID3D11Texture2D>, 2> textures;
		std::array<winrt::com_ptr<ID3D11RenderTargetView>, 2> views;
		std::array<ID3D11RenderTargetView*, 2> targets;
		for (std::size_t i = 0; i < 2; ++i) {
			Check(device->CreateTexture2D(&textureDesc, nullptr, textures[i].put()));
			Util::SetResourceName(textures[i].get(), "Test::ContextTarget%zu", i);
			Check(device->CreateRenderTargetView(textures[i].get(), nullptr, views[i].put()));
			Util::SetResourceName(views[i].get(), "Test::ContextTarget%zu RTV", i);
			targets[i] = views[i].get();
		}
		context->OMSetRenderTargets(2, targets.data(), nullptr);
		const D3D11_VIEWPORT viewports[] = { { 0, 0, 32, 32, 0, 1 }, { 2, 3, 20, 21, .1f, .8f } };
		const D3D11_RECT scissors[] = { { 0, 0, 31, 30 }, { 2, 3, 18, 19 } };
		context->RSSetViewports(2, viewports);
		context->RSSetScissorRects(2, scissors);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		const float blendFactor[] = { .1f, .2f, .3f, .4f };
		context->OMSetBlendState(nullptr, blendFactor, 0x12345678);
		context->OMSetDepthStencilState(nullptr, 37);
		D3D11_QUERY_DESC predicateDesc{ D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
		winrt::com_ptr<ID3D11Predicate> predicate;
		Check(device->CreatePredicate(&predicateDesc, predicate.put()));
		Util::SetResourceName(predicate.get(), "Test::ContextPredicate");
		context->SetPredication(predicate.get(), TRUE);
		auto verify = [&] {
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			context->IAGetPrimitiveTopology(&topology);
			Require(topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, "restore topology");
			D3D11_VIEWPORT restoredViewports[2]{};
			UINT count = 2;
			context->RSGetViewports(&count, restoredViewports);
			Require(count == 2 && !std::memcmp(viewports, restoredViewports, sizeof(viewports)), "restore all viewports");
			D3D11_RECT restoredScissors[2]{};
			count = 2;
			context->RSGetScissorRects(&count, restoredScissors);
			Require(count == 2 && !std::memcmp(scissors, restoredScissors, sizeof(scissors)), "restore all scissors");
			ID3D11RenderTargetView* restoredTargets[2]{};
			context->OMGetRenderTargets(2, restoredTargets, nullptr);
			bool same = restoredTargets[0] == targets[0] && restoredTargets[1] == targets[1];
			for (auto* target : restoredTargets)
				if (target)
					target->Release();
			Require(same, "restore multiple render targets");
			for (int stage = 0; stage < 4; ++stage) {
				winrt::com_ptr<ID3D11Buffer> restored;
				if (stage == 0)
					context->VSGetConstantBuffers(3, 1, restored.put());
				if (stage == 1)
					context->PSGetConstantBuffers(4, 1, restored.put());
				if (stage == 2)
					context->GSGetConstantBuffers(5, 1, restored.put());
				if (stage == 3)
					context->CSGetConstantBuffers(6, 1, restored.put());
				Require(restored.get() == buffer.get(), "restore shader-stage bindings");
			}
			float restoredBlend[4]{};
			UINT mask = 0;
			context->OMGetBlendState(nullptr, restoredBlend, &mask);
			Require(mask == 0x12345678 && !std::memcmp(restoredBlend, blendFactor, sizeof(blendFactor)), "restore blend values");
			UINT stencil = 0;
			context->OMGetDepthStencilState(nullptr, &stencil);
			Require(stencil == 37, "restore stencil reference");
			winrt::com_ptr<ID3D11Predicate> restoredPredicate;
			BOOL value = FALSE;
			context->GetPredication(restoredPredicate.put(), &value);
			Require(restoredPredicate.get() == predicate.get() && value, "restore predication");
		};
		for (int iteration = 0; iteration < 2; ++iteration) {
			{
				Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
				Require(static_cast<bool>(guard), "state guard activates");
				context->ClearState();
				context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				if (iteration == 1) {
					guard.Restore();
					verify();
					guard.Restore();
				}
			}
			verify();
		}
		try {
			Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
			context->ClearState();
			throw std::runtime_error("render aborted");
		} catch (const std::runtime_error&) {}
		verify();
		context->ClearState();
		CheckComputeState(device.get(), context1.get());
		std::cout << "D3D11 WARP full-state restoration passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
