#include <SKSE/Impl/PCH.h>

#include <REX/W32/Bridge.h>

#include "d3d11_shader_test.h"
#include <iostream>

namespace
{
	using D3D11ShaderTest::Check;
	using Microsoft::WRL::ComPtr;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void CheckDepthBridge()
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));

		D3D11_TEXTURE2D_DESC description{};
		description.Width = description.Height = 4;
		description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
		description.Format = DXGI_FORMAT_R32_TYPELESS;
		description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		ComPtr<ID3D11Texture2D> texture;
		Check(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf()));
		Util::SetResourceName(texture.Get(), "CommonLibBridgeTest::Depth");

		auto* const engineTexture = REX::W32::AsW32(texture.Get());
		Require(REX::W32::AsReal(engineTexture) == texture.Get(), "Texture identity changed");
		REX::W32::D3D11_TEXTURE2D_DESC engineDescription{};
		engineTexture->GetDesc(&engineDescription);
		Require(std::memcmp(&description, &engineDescription, sizeof(description)) == 0,
			"Engine texture descriptor differs from the SDK descriptor");
		const auto references = texture->AddRef();
		Require(engineTexture->AddRef() == references + 1, "Engine AddRef used a different object");
		Require(engineTexture->Release() == references, "Engine Release lost ownership");
		Require(texture->Release() == references - 1, "Bridge conversion changed ownership");

		D3D11_DEPTH_STENCIL_VIEW_DESC depthDescription{};
		depthDescription.Format = DXGI_FORMAT_D32_FLOAT;
		depthDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		REX::W32::ID3D11DepthStencilView* engineDepth = nullptr;
		Check(device->CreateDepthStencilView(texture.Get(), &depthDescription, REX::W32::AsReal(&engineDepth)));
		ComPtr<ID3D11DepthStencilView> depth;
		depth.Attach(REX::W32::AsReal(engineDepth));
		Util::SetResourceName(depth.Get(), "CommonLibBridgeTest::Depth DSV");
		REX::W32::D3D11_DEPTH_STENCIL_VIEW_DESC engineDepthDescription{};
		engineDepth->GetDesc(&engineDepthDescription);
		Require(std::memcmp(&depthDescription, &engineDepthDescription, sizeof(depthDescription)) == 0,
			"Engine depth view descriptor differs from the SDK descriptor");
		REX::W32::ID3D11Resource* engineResource = nullptr;
		engineDepth->GetResource(&engineResource);
		ComPtr<ID3D11Resource> viewedResource;
		viewedResource.Attach(REX::W32::AsReal(engineResource));
		Require(viewedResource.Get() == texture.Get(), "Depth view references a different resource");

		REX::W32::AsW32(context.Get())->ClearDepthStencilView(engineDepth, D3D11_CLEAR_DEPTH, 0.375f, 0);
		description.Usage = D3D11_USAGE_STAGING;
		description.BindFlags = 0;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> readback;
		Check(device->CreateTexture2D(&description, nullptr, readback.GetAddressOf()));
		Util::SetResourceName(readback.Get(), "CommonLibBridgeTest::Readback");
		context->CopyResource(readback.Get(), texture.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		float depthValue{};
		std::memcpy(&depthValue, mapped.pData, sizeof(depthValue));
		context->Unmap(readback.Get(), 0);
		Require(depthValue == 0.375f, "Engine context dispatch did not clear the SDK depth resource");
	}
}

static_assert(sizeof(REX::W32::D3D11_TEXTURE2D_DESC) == sizeof(D3D11_TEXTURE2D_DESC));
static_assert(sizeof(REX::W32::D3D11_DEPTH_STENCIL_VIEW_DESC) == sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC));
static_assert(sizeof(REX::W32::D3D11_VIEWPORT) == sizeof(D3D11_VIEWPORT));
static_assert(offsetof(REX::W32::D3D11_VIEWPORT, minDepth) == offsetof(D3D11_VIEWPORT, MinDepth));
static_assert(offsetof(REX::W32::D3D11_VIEWPORT, maxDepth) == offsetof(D3D11_VIEWPORT, MaxDepth));

int main()
{
	try {
		CheckDepthBridge();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
