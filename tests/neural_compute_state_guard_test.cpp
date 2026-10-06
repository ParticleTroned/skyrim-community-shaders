#include "Features/Upscaling/NeuralRendering/ComputeStateGuard.h"
#include "d3d_resource_naming.h"

#include <array>
#include <cstdio>
#include <d3dcompiler.h>
#include <stdexcept>

namespace
{
	using Microsoft::WRL::ComPtr;
	using NeuralRendering::ComputeStateGuard;
	using NeuralRendering::ComputeStatePolicy;

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
		std::array<ComPtr<ID3D11ShaderResourceView>, 5> srvs;
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ComputeShader> temporaryShader;
		ComPtr<ID3D11Buffer> constantBuffer;
		ComPtr<ID3D11Buffer> temporaryBuffer;
		ComPtr<ID3D11Predicate> predicate;

		Fixture()
		{
			const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
			Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
						&requested, 1, D3D11_SDK_VERSION, &device, nullptr, &context)),
				"Create WARP device");
			constexpr char code[] = "[numthreads(1,1,1)] void main() {}";
			ComPtr<ID3DBlob> bytecode;
			Require(SUCCEEDED(D3DCompile(code, sizeof(code), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &bytecode, nullptr)), "Compile shader");
			Require(SUCCEEDED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)), "Create original shader");
			Require(SUCCEEDED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &temporaryShader)), "Create temporary shader");
			Util::SetResourceName(shader.Get(), "ComputeStateGuardTest::OriginalShader");
			Util::SetResourceName(temporaryShader.Get(), "ComputeStateGuardTest::TemporaryShader");
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
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->SetPredication(predicate.Get(), predicateValue);
		}

		void CheckViews(UINT clearedUavs, UINT clearedSrvs)
		{
			for (UINT i = 0; i < uavs.size(); ++i) {
				ComPtr<ID3D11UnorderedAccessView> actual;
				context->CSGetUnorderedAccessViews(i, 1, &actual);
				Require(actual.Get() == (i < clearedUavs ? nullptr : uavs[i].Get()), "UAV identity/coverage");
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				ComPtr<ID3D11ShaderResourceView> actual;
				context->CSGetShaderResources(i, 1, &actual);
				Require(actual.Get() == (i < clearedSrvs ? nullptr : srvs[i].Get()), "SRV identity/coverage");
			}
		}

		void CheckState(ID3D11Buffer* expectedBuffer, ID3D11Predicate* expectedPredicate, BOOL expectedValue, bool temporaryShaderExpected = false)
		{
			ComPtr<ID3D11Buffer> actualBuffer;
			context->CSGetConstantBuffers(0, 1, &actualBuffer);
			Require(actualBuffer.Get() == expectedBuffer, "Constant buffer identity");
			ComPtr<ID3D11Predicate> actualPredicate;
			BOOL actualValue = !expectedValue;
			context->GetPredication(&actualPredicate, &actualValue);
			Require(actualPredicate.Get() == expectedPredicate && actualValue == expectedValue, "Predicate identity/value");
			ComPtr<ID3D11ComputeShader> actualShader;
			UINT classCount = 0;
			context->CSGetShader(&actualShader, nullptr, &classCount);
			Require(actualShader.Get() == (temporaryShaderExpected ? temporaryShader.Get() : shader.Get()) && classCount == 0, "Shader identity and class state");
		}
	};

	template <UINT SRVCount, UINT UAVCount, ComputeStatePolicy Policy = ComputeStatePolicy::ClearBindingsAndPredication>
	void CheckGuard(BOOL predicateValue)
	{
		using Guard = ComputeStateGuard<SRVCount, UAVCount, Policy>;
		constexpr bool isolated = Policy == ComputeStatePolicy::ClearBindingsAndPredication;
		Fixture fixture;
		fixture.Bind(predicateValue);
		fixture.CheckViews(0, 0);
		fixture.CheckState(fixture.constantBuffer.Get(), fixture.predicate.Get(), predicateValue);
		{
			Guard guard(fixture.context.Get());
			Require(guard.Captured(), "Non-null context captured");
			fixture.CheckViews(isolated ? UAVCount : 0, isolated ? SRVCount : 0);
			fixture.CheckState(fixture.constantBuffer.Get(), isolated ? nullptr : fixture.predicate.Get(), isolated ? FALSE : predicateValue);
			fixture.Bind(!predicateValue);
			auto* temporary = fixture.temporaryBuffer.Get();
			fixture.context->CSSetConstantBuffers(0, 1, &temporary);
			fixture.context->CSSetShader(fixture.temporaryShader.Get(), nullptr, 0);
			guard.Unbind();
			fixture.CheckViews(UAVCount, SRVCount);
			fixture.CheckState(temporary, fixture.predicate.Get(), !predicateValue, true);
			// Different bindings ensure scope exit has to restore the captured views.
			for (UINT i = 0; i < UAVCount; ++i) {
				auto* view = UAVCount == 1 ? nullptr : fixture.uavs[UAVCount - i - 1].Get();
				fixture.context->CSSetUnorderedAccessViews(i, 1, &view, nullptr);
			}
			for (UINT i = 0; i < SRVCount; ++i) {
				auto* srv = fixture.srvs[SRVCount - i].Get();
				fixture.context->CSSetShaderResources(i, 1, &srv);
			}
		}
		fixture.CheckViews(0, 0);
		fixture.CheckState(fixture.constantBuffer.Get(), fixture.predicate.Get(), isolated ? predicateValue : !predicateValue);
		Guard missingContext(nullptr);
		Require(!missingContext.Captured(), "Null context is not captured");
		missingContext.Unbind();
	}
}

int main()
{
	try {
		CheckGuard<2, 4>(TRUE);
		CheckGuard<2, 1>(FALSE);
		CheckGuard<1, 1, ComputeStatePolicy::PreserveBindings>(TRUE);
		CheckGuard<4, 3, ComputeStatePolicy::PreserveBindings>(FALSE);
		std::puts("ComputeStateGuard: isolated colour and preserving renderer/actor WARP checks passed");
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "ComputeStateGuard: %s\n", error.what());
		return 1;
	}
}
