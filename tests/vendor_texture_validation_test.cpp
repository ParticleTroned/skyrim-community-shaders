#define NOMINMAX
#include "d3d11_shader_test.h"

#include <winrt/base.h>

#include <cstdint>
#include <format>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace
{
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = winrt::com_ptr<T>;

	struct Texture2D
	{
		D3D11_TEXTURE2D_DESC desc{};
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
	};

	namespace globals::d3d
	{
		ID3D11Device* device = nullptr;
	}

#include "vendor_texture_scope_exit.h"

#include "vendor_texture_identity.h"
#include "vendor_texture_resource.h"
#include "vendor_texture_validation.h"

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	ComPtr<ID3D11Device> MakeDevice()
	{
		ComPtr<ID3D11Device> device;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, nullptr));
		return device;
	}

	Texture2D MakeTexture(ID3D11Device* device, UINT mips = 1)
	{
		Texture2D texture;
		auto& desc = texture.desc;
		desc.Width = 16;
		desc.Height = 8;
		desc.MipLevels = mips;
		desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		Check(device->CreateTexture2D(&desc, nullptr, texture.resource.put()));
		Util::SetResourceName(texture.resource.get(), "VendorTextureTest::Texture");
		Check(device->CreateShaderResourceView(texture.resource.get(), nullptr, texture.srv.put()));
		Util::SetResourceName(texture.srv.get(), "VendorTextureTest::Texture.SRV");
		Check(device->CreateUnorderedAccessView(texture.resource.get(), nullptr, texture.uav.put()));
		Util::SetResourceName(texture.uav.get(), "VendorTextureTest::Texture.UAV");
		return texture;
	}

	void ExpectFailure(const Texture2D* texture, const D3D11_TEXTURE2D_DESC& expected, std::string_view reason)
	{
		std::string failure = "stale failure";
		Require(!IsCommonVendorTextureCompatible(texture, expected, &failure), "Invalid vendor texture was accepted");
		Require(failure.find(reason) != std::string::npos, "Texture failure reason did not identify the failed check");
		Require(!IsCommonVendorTextureCompatible(texture, expected), "Diagnostics changed texture acceptance");
	}

	void CheckResources(ID3D11Device* device)
	{
		const auto valid = MakeTexture(device);
		const auto expected = valid.desc;
		std::string reason;
		Require(IsCommonVendorTextureCompatible(&valid, expected, &reason) && reason.empty(),
			"Valid vendor resource was rejected");
		ExpectFailure(nullptr, expected, "missing texture wrapper");
		auto broken = valid;
		broken.resource = nullptr;
		ExpectFailure(&broken, expected, "missing texture resource");
		broken = valid;
		broken.srv = nullptr;
		ExpectFailure(&broken, expected, "missing SRV");
		broken = valid;
		broken.uav = nullptr;
		ExpectFailure(&broken, expected, "missing UAV");
		globals::d3d::device = nullptr;
		ExpectFailure(&valid, expected, "missing current D3D11 device");
		globals::d3d::device = device;

		const auto otherDevice = MakeDevice();
		const auto foreign = MakeTexture(otherDevice.get());
		ExpectFailure(&foreign, expected, "different D3D11 device");
		const auto otherResource = MakeTexture(device);
		broken = valid;
		broken.srv = otherResource.srv;
		ExpectFailure(&broken, expected, "SRV references a different resource");
		broken = valid;
		broken.uav = otherResource.uav;
		ExpectFailure(&broken, expected, "UAV references a different resource");

		auto wrongDesc = expected;
		++wrongDesc.Width;
		ExpectFailure(&valid, wrongDesc, "resource descriptor mismatch");
		broken = valid;
		++broken.desc.Height;
		ExpectFailure(&broken, expected, "wrapper descriptor mismatch");
		Require(!IsCommonVendorTextureCompatible(&valid, wrongDesc, &reason) &&
					reason.find("expected [17x8") != std::string::npos &&
					reason.find("actual [16x8") != std::string::npos,
			"Descriptor diagnostics must retain expected and actual dimensions");
		Require(IsCommonVendorTextureCompatible(&valid, expected, &reason) && reason.empty(),
			"Successful retry must clear the previous failure reason");
	}

	void CheckViews(ID3D11Device* device)
	{
		auto texture = MakeTexture(device, 2);
		const auto expected = texture.desc;
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = expected.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 1;
		srvDesc.Texture2D.MipLevels = 1;
		texture.srv = nullptr;
		Check(device->CreateShaderResourceView(texture.resource.get(), &srvDesc, texture.srv.put()));
		Util::SetResourceName(texture.srv.get(), "VendorTextureTest::Mip.SRV");
		ExpectFailure(&texture, expected, "firstMip=1");
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = std::numeric_limits<UINT>::max();
		texture.srv = nullptr;
		Check(device->CreateShaderResourceView(texture.resource.get(), &srvDesc, texture.srv.put()));
		Util::SetResourceName(texture.srv.get(), "VendorTextureTest::AllMips.SRV");
		Require(IsCommonVendorTextureCompatible(&texture, expected), "All-mip view was rejected");

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = expected.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 1;
		texture.uav = nullptr;
		Check(device->CreateUnorderedAccessView(texture.resource.get(), &uavDesc, texture.uav.put()));
		Util::SetResourceName(texture.uav.get(), "VendorTextureTest::Mip.UAV");
		ExpectFailure(&texture, expected, "mip=1");

		auto arraySRV = MakeTexture(device);
		srvDesc = {};
		srvDesc.Format = arraySRV.desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.MipLevels = 1;
		srvDesc.Texture2DArray.ArraySize = 1;
		arraySRV.srv = nullptr;
		Check(device->CreateShaderResourceView(arraySRV.resource.get(), &srvDesc, arraySRV.srv.put()));
		Util::SetResourceName(arraySRV.srv.get(), "VendorTextureTest::Array.SRV");
		ExpectFailure(&arraySRV, arraySRV.desc, "SRV dimension=");
		std::string reason;
		Require(!IsCommonVendorTextureCompatible(&arraySRV, arraySRV.desc, &reason) &&
					reason.find("Texture2D required") != std::string::npos &&
					reason.find("firstMip=") == std::string::npos,
			"Non-2D SRV diagnostics must not inspect the Texture2D union");

		auto arrayUAV = MakeTexture(device);
		uavDesc = {};
		uavDesc.Format = arrayUAV.desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.ArraySize = 1;
		arrayUAV.uav = nullptr;
		Check(device->CreateUnorderedAccessView(arrayUAV.resource.get(), &uavDesc, arrayUAV.uav.put()));
		Util::SetResourceName(arrayUAV.uav.get(), "VendorTextureTest::Array.UAV");
		ExpectFailure(&arrayUAV, arrayUAV.desc, "UAV dimension=");
		Require(!IsCommonVendorTextureCompatible(&arrayUAV, arrayUAV.desc, &reason) &&
					reason.find("Texture2D required") != std::string::npos &&
					reason.find("mip=") == std::string::npos,
			"Non-2D UAV diagnostics must not inspect the Texture2D union");
	}
}

int main()
{
	try {
		const auto device = MakeDevice();
		globals::d3d::device = device.get();
		const auto restoreDevice = ScopeExit([]() { globals::d3d::device = nullptr; });
		CheckResources(device.get());
		CheckViews(device.get());
		std::cout << "Vendor texture identity, descriptors and view diagnostics passed on D3D11 WARP\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
