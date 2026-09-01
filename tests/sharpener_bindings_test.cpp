#include "Features/Upscaling/SharpenerBindings.h"

#include <wrl/client.h>

#include <iostream>
#include <stdexcept>

namespace
{
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	void Check(HRESULT result)
	{
		if (FAILED(result))
			throw std::runtime_error("D3D11 WARP operation failed");
	}

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;

		explicit Texture(ID3D11Device* device)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = 8;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture2D(&desc, nullptr, resource.GetAddressOf()));
			Check(device->CreateShaderResourceView(resource.Get(), nullptr, srv.GetAddressOf()));
			Check(device->CreateUnorderedAccessView(resource.Get(), nullptr, uav.GetAddressOf()));
		}
	};

	void VerifyBindings(ID3D11DeviceContext* context, const Texture& color, const Texture& motion,
		const Texture& output, const Texture& untouched, ID3D11UnorderedAccessView* previousOutput, bool adaptive)
	{
		context->ClearState();
		ID3D11UnorderedAccessView* priorOutputs[] = { previousOutput, untouched.uav.Get() };
		context->CSSetUnorderedAccessViews(0, 2, priorOutputs, nullptr);
		UpscalingSharpener::BindComputeViews(context, color.srv.Get(), output.uav.Get(), adaptive ? motion.srv.Get() : nullptr);

		ComPtr<ID3D11ShaderResourceView> boundColor, boundMotion;
		ComPtr<ID3D11UnorderedAccessView> boundOutput, boundUntouched;
		context->CSGetShaderResources(0, 1, boundColor.GetAddressOf());
		context->CSGetShaderResources(1, 1, boundMotion.GetAddressOf());
		context->CSGetUnorderedAccessViews(0, 1, boundOutput.GetAddressOf());
		context->CSGetUnorderedAccessViews(1, 1, boundUntouched.GetAddressOf());
		if (boundColor.Get() != color.srv.Get() ||
			boundMotion.Get() != (adaptive ? motion.srv.Get() : nullptr) ||
			boundOutput.Get() != output.uav.Get() || boundUntouched.Get() != untouched.uav.Get()) {
			throw std::runtime_error("Sharpener bindings were suppressed or an unowned UAV slot changed");
		}
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		const Texture color(device.Get()), motion(device.Get()), output(device.Get()), untouched(device.Get());
		for (auto* prior : { color.uav.Get(), motion.uav.Get(), output.uav.Get(), static_cast<ID3D11UnorderedAccessView*>(nullptr) }) {
			VerifyBindings(context.Get(), color, motion, output, untouched, prior, true);
			VerifyBindings(context.Get(), color, motion, output, untouched, prior, false);
		}
		context->ClearState();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
