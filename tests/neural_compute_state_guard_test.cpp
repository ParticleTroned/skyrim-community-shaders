#include "Features/Upscaling/NeuralRendering/ComputeStateGuard.h"
#include "d3d_resource_naming.h"

#include <array>
#include <cstdio>
#include <stdexcept>

namespace
{
	using Microsoft::WRL::ComPtr;
	using NeuralRendering::Color::ComputeStateGuard;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		std::array<ComPtr<ID3D11UnorderedAccessView>, 5> uavs;
		std::array<ComPtr<ID3D11ShaderResourceView>, 2> srvs;
		ComPtr<ID3D11Buffer> constantBuffer;
		ComPtr<ID3D11Buffer> temporaryBuffer;
		ComPtr<ID3D11Predicate> predicate;

		Fixture()
		{
			const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
			Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
						&requested, 1, D3D11_SDK_VERSION, &device, nullptr, &context)),
				"Create WARP device");
			for (UINT i = 0; i < uavs.size(); ++i) {
				auto texture = Texture(D3D11_BIND_UNORDERED_ACCESS, i);
				Require(SUCCEEDED(device->CreateUnorderedAccessView(texture.Get(), nullptr, &uavs[i])), "Create UAV");
				Util::SetResourceName(uavs[i].Get(), "ComputeStateGuardTest::Output%u UAV", i);
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				auto texture = Texture(D3D11_BIND_SHADER_RESOURCE, i);
				Require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &srvs[i])), "Create SRV");
				Util::SetResourceName(srvs[i].Get(), "ComputeStateGuardTest::Input%u SRV", i);
			}
			D3D11_BUFFER_DESC bufferDesc{};
			bufferDesc.ByteWidth = 16;
			bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			Require(SUCCEEDED(device->CreateBuffer(&bufferDesc, nullptr, &constantBuffer)), "Create original constant buffer");
			Util::SetResourceName(constantBuffer.Get(), "ComputeStateGuardTest::OriginalConstants");
			Require(SUCCEEDED(device->CreateBuffer(&bufferDesc, nullptr, &temporaryBuffer)), "Create temporary constant buffer");
			Util::SetResourceName(temporaryBuffer.Get(), "ComputeStateGuardTest::TemporaryConstants");
			const D3D11_QUERY_DESC predicateDesc{ D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
			Require(SUCCEEDED(device->CreatePredicate(&predicateDesc, &predicate)), "Create predicate");
			Util::SetResourceName(predicate.Get(), "ComputeStateGuardTest::Predicate");
		}

		ComPtr<ID3D11Texture2D> Texture(UINT bindFlags, UINT index)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = 4;
			desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.BindFlags = bindFlags;
			ComPtr<ID3D11Texture2D> texture;
			Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)), "Create texture");
			Util::SetResourceName(texture.Get(), "ComputeStateGuardTest::%s%u",
				bindFlags == D3D11_BIND_UNORDERED_ACCESS ? "Output" : "Input", index);
			return texture;
		}

		void Bind(BOOL predicateValue)
		{
			for (UINT i = 0; i < uavs.size(); ++i) {
				auto* view = uavs[i].Get();
				context->CSSetUnorderedAccessViews(i, 1, &view, nullptr);
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				auto* view = srvs[i].Get();
				context->CSSetShaderResources(i, 1, &view);
			}
			auto* buffer = constantBuffer.Get();
			context->CSSetConstantBuffers(0, 1, &buffer);
			context->CSSetShader(nullptr, nullptr, 0);
			context->SetPredication(predicate.Get(), predicateValue);
		}

		void CheckViews(UINT clearedUavs, bool clearedSrvs)
		{
			for (UINT i = 0; i < uavs.size(); ++i) {
				ComPtr<ID3D11UnorderedAccessView> actual;
				context->CSGetUnorderedAccessViews(i, 1, &actual);
				Require(actual.Get() == (i < clearedUavs ? nullptr : uavs[i].Get()), "UAV identity/coverage");
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				ComPtr<ID3D11ShaderResourceView> actual;
				context->CSGetShaderResources(i, 1, &actual);
				Require(actual.Get() == (clearedSrvs ? nullptr : srvs[i].Get()), "SRV identity/coverage");
			}
		}

		void CheckState(ID3D11Buffer* expectedBuffer, ID3D11Predicate* expectedPredicate, BOOL expectedValue)
		{
			ComPtr<ID3D11Buffer> actualBuffer;
			context->CSGetConstantBuffers(0, 1, &actualBuffer);
			Require(actualBuffer.Get() == expectedBuffer, "Constant buffer identity");
			ComPtr<ID3D11Predicate> actualPredicate;
			BOOL actualValue = !expectedValue;
			context->GetPredication(&actualPredicate, &actualValue);
			Require(actualPredicate.Get() == expectedPredicate && actualValue == expectedValue, "Predicate identity/value");
			ComPtr<ID3D11ComputeShader> shader;
			UINT classCount = 0;
			context->CSGetShader(&shader, nullptr, &classCount);
			Require(!shader && classCount == 0, "Null shader and class state");
		}
	};

	template <class Guard, UINT UAVCount>
	void CheckGuard(BOOL predicateValue)
	{
		Fixture fixture;
		fixture.Bind(predicateValue);
		fixture.CheckViews(0, false);
		fixture.CheckState(fixture.constantBuffer.Get(), fixture.predicate.Get(), predicateValue);
		{
			Guard guard(fixture.context.Get());
			fixture.CheckViews(UAVCount, true);
			fixture.CheckState(fixture.constantBuffer.Get(), nullptr, FALSE);
			fixture.Bind(!predicateValue);
			auto* temporary = fixture.temporaryBuffer.Get();
			fixture.context->CSSetConstantBuffers(0, 1, &temporary);
			guard.Unbind();
			fixture.CheckViews(UAVCount, true);
			fixture.CheckState(temporary, fixture.predicate.Get(), !predicateValue);
			// Different bindings ensure scope exit has to restore the captured views.
			for (UINT i = 0; i < UAVCount; ++i) {
				auto* view = UAVCount == 1 ? nullptr : fixture.uavs[UAVCount - i - 1].Get();
				fixture.context->CSSetUnorderedAccessViews(i, 1, &view, nullptr);
			}
			auto* srv = fixture.srvs[1].Get();
			fixture.context->CSSetShaderResources(0, 1, &srv);
		}
		fixture.CheckViews(0, false);
		fixture.CheckState(fixture.constantBuffer.Get(), fixture.predicate.Get(), predicateValue);
	}
}

int main()
{
	try {
		CheckGuard<ComputeStateGuard<2, 4>, 4>(TRUE);
		CheckGuard<ComputeStateGuard<2>, 1>(FALSE);
		std::puts("ComputeStateGuard: four-UAV and default one-UAV WARP checks passed");
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "ComputeStateGuard: %s\n", error.what());
		return 1;
	}
}
