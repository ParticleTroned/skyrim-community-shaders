#define NOMINMAX
#include "Utils/DeviceContextState.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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

static void TestIsolationFallback()
{
	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
	winrt::com_ptr<ID3D11DeviceContext1> context1;
	winrt::com_ptr<ID3DDeviceContextState> state;
	Require(Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, state, "Test::BeforeHooks"), "create state before hooks");
	Util::RegisterDeviceContextStateForHooks(context.get());
	Util::DisableDeviceContextStateIsolation(context.get());
	Util::RegisterDeviceContextStateForHooks(context.get());
	auto* previous = state.get();
	Require(!Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, state, "Invalid") && state.get() == previous,
		"latched fallback rejects later state creation without replacing existing resources");
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
	const Util::ScopedDeviceContextState guard(context1.get(), state.get());
	Require(!static_cast<bool>(guard), "latched fallback rejects previously created isolated states");
	D3D11_PRIMITIVE_TOPOLOGY topology{};
	context->IAGetPrimitiveTopology(&topology);
	Require(topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, "rejected isolation preserves context state");
}
int main()
{
	try {
		Util::RegisterDeviceContextStateForHooks(nullptr);
		TestIsolationFallback();
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
		Util::RegisterDeviceContextStateForHooks(context.get());
		verify();
		Util::resourceName = "NoRepeatedCreation";
		Util::RegisterDeviceContextStateForHooks(context.get());
		Require(Util::resourceName == "NoRepeatedCreation", "context registration does not allocate native states");
		verify();
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
		std::cout << "D3D11 WARP full-state restoration passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
