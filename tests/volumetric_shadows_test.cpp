#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <d3dcompiler.h>
#include <iostream>
#include <limits>
#include <winrt/base.h>

#include "volumetric_shadows_fixture.h"

namespace
{
	using D3D11ShaderTest::Check;
	using Microsoft::WRL::ComPtr;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const char* permutation, bool general = false)
	{
		std::array<D3D_SHADER_MACRO, 3> defines{ { { permutation, "1" }, { nullptr, nullptr }, { nullptr, nullptr } } };
		if (general)
			defines[1] = { "DOWNSAMPLE_SHADOW_GENERAL", "1" };
		const wchar_t* path = permutation[0] == 'B' ?
		                          L"features/Volumetric Shadows/Shaders/VolumetricShadows/BlurShadowCS.hlsl" :
		                          L"features/Volumetric Shadows/Shaders/VolumetricShadows/DownsampleShadowCS.hlsl";
		ComPtr<ID3DBlob> bytecode, errors;
		const auto result = D3DCompileFromFile(path, defines.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "VolumetricShadowsTest::%s%s", permutation, general ? "General" : "Grouped");
		return shader;
	}

	uint8_t Depth(UINT x, UINT y, UINT cascade)
	{
		return static_cast<uint8_t>((x / 13 * 17 + y / 11 * 31 + cascade * 47) % 256);
	}
	struct DepthTexture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		DepthTexture(ID3D11Device* device, UINT width, UINT height, bool auxiliary = false, UINT slices = 2)
		{
			std::vector<uint8_t> pixels(size_t(width) * height * slices);
			std::vector<D3D11_SUBRESOURCE_DATA> data(slices);
			for (UINT slice = 0; slice < slices; ++slice) {
				auto* plane = pixels.data() + size_t(width) * height * slice;
				for (UINT y = 0; y < height; ++y)
					for (UINT x = 0; x < width; ++x)
						plane[size_t(y) * width + x] = auxiliary ? (slice == 0 ? 255 : 170) : Depth(x, y, slice);
				data[slice] = { plane, width, 0 };
			}
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.SampleDesc.Count = 1;
			desc.ArraySize = slices;
			desc.Format = DXGI_FORMAT_R8_UNORM;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			Check(device->CreateTexture2D(&desc, data.data(), texture.GetAddressOf()));
			Util::SetResourceName(texture.Get(), "VolumetricShadowsTest::%sDepth", auxiliary ? "Auxiliary" : "Source");
			Check(device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf()));
			Util::SetResourceName(srv.Get(), "VolumetricShadowsTest::%sDepth SRV", auxiliary ? "Auxiliary" : "Source");
		}
	};

	std::vector<std::array<uint16_t, 2>> ReadMoments(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, UINT mip)
	{
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		desc.Width = std::max(1u, desc.Width >> mip);
		desc.Height = std::max(1u, desc.Height >> mip);
		desc.MipLevels = desc.ArraySize = 1;
		desc.BindFlags = desc.MiscFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> staging;
		Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
		Util::SetResourceName(staging.Get(), "VolumetricShadowsTest::Readback");
		context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture, mip, nullptr);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::vector<std::array<uint16_t, 2>> result(size_t(desc.Width) * desc.Height);
		for (UINT y = 0; y < desc.Height; ++y)
			std::memcpy(result.data() + size_t(y) * desc.Width, static_cast<const std::byte*>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(desc.Width) * sizeof(result[0]));
		context->Unmap(staging.Get(), 0);
		return result;
	}

	void CheckReduction(ID3D11Device* device, ID3D11DeviceContext* context, VolumetricShadows& feature,
		const std::array<ComPtr<ID3D11ComputeShader>, 6>& shaders, UINT width, UINT height)
	{
		DepthTexture source(device, width, height), auxiliary(device, 32, 32, true);
		D3D11_TEXTURE2D_DESC desc{};
		Require(TryGetShadowTextureDescription(source.srv.Get(), desc), "Valid source rejected");
		Require(feature.EnsureShadowCopyTextures(), "Moment-buffer allocation failed");
		ID3D11ShaderResourceView* srvs[]{ source.srv.Get(), auxiliary.srv.Get() };
		context->CSSetShaderResources(0, 2, srvs);
		context->CSSetSamplers(0, 1, &feature.linearSampler);
		for (UINT mip = 0; mip < 2; ++mip) {
			const UINT size = kShadowCopySize >> mip, cascade = 1 - mip;
			const bool grouped = UsesGroupedDownsample(desc, size);
			auto* output = mip == 0 ? feature.shadowCopyMip0UAV : feature.shadowCopyMip1UAV;
			auto* shader = shaders[mip + (grouped ? 0 : 2)].Get();
			const std::array<float, 4> poison{ 0, 0, 0, 0 };
			context->ClearUnorderedAccessViewFloat(output, poison.data());
			context->CSSetShader(shader, nullptr, 0);
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->Dispatch(grouped ? (width + 15) / 16 : (size + 7) / 8,
				grouped ? (height + 15) / 16 : (size + 7) / 8, 1);
			ClearComputeUAV(context);
			const auto pixels = ReadMoments(device, context, feature.shadowCopyTexture, mip);
			for (UINT y = 0; y < size; ++y)
				for (UINT x = 0; x < size; ++x) {
					const UINT x0 = std::min(x * width / size, width - 1), y0 = std::min(y * height / size, height - 1);
					const UINT x1 = std::min(std::max(x0 + 1, (x + 1) * width / size), width);
					const UINT y1 = std::min(std::max(y0 + 1, (y + 1) * height / size), height);
					double mean = 0, squared = 0;
					for (UINT sy = y0; sy < y1; ++sy)
						for (UINT sx = x0; sx < x1; ++sx) {
							const double depth = double(std::min<unsigned>(Depth(sx, sy, cascade), cascade == 0 ? 255 : 170)) / 255;
							mean += depth;
							squared += depth * depth;
						}
					const double count = double(x1 - x0) * (y1 - y0);
					const auto& pixel = pixels[size_t(y) * size + x];
					if (std::abs(double(pixel[0]) / 65535 - mean / count) > 2.0 / 65535 ||
						std::abs(double(pixel[1]) / 65535 - squared / count) > 2.0 / 65535) {
						std::cerr << width << 'x' << height << " mip " << mip << " pixel " << x << ',' << y << '\n';
						throw std::runtime_error("GPU moments differ from complete CPU box average");
					}
				}
		}
		ClearComputeSRVs(context, 0, 2);
		std::cout << "Complete moments checked: " << width << 'x' << height << " both cascades\n";
	}

	void CheckFallback(ID3D11Device* device, ID3D11DeviceContext* context, VolumetricShadows& feature)
	{
		ComPtr<ID3D11ShaderResourceView> bound;
		context->PSGetShaderResources(VolumetricShadows::kSharedShadowMapShaderSlot, 1, bound.GetAddressOf());
		Require(bound.Get() == feature.fallbackShadowSRV, "Failed preparation did not bind typed fallback");
		Require(!feature.shadowCopyValid, "Failed preparation published valid moments");
		D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
		bound->GetDesc(&desc);
		Require(desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && desc.Format == DXGI_FORMAT_R16G16_UNORM && desc.Texture2D.MipLevels == 2,
			"Fallback differs from the VSM resource contract");
		for (UINT mip = 0; mip < 2; ++mip)
			for (const auto& pixel : ReadMoments(device, context, feature.fallbackShadowTexture, mip))
				Require(pixel[0] == 65535 && pixel[1] == 65535, "Fallback moments are not fully lit");
	}

	void CheckDrawBindings(ID3D11Device* device, ID3D11DeviceContext* context, VolumetricShadows& feature)
	{
		using Shader = RE::BSShader::Type;
		DepthTexture foreign(device, 32, 32);
		auto* foreignSRV = foreign.srv.Get();
		const auto requireBinding = [&](ID3D11ShaderResourceView* expected) {
			ComPtr<ID3D11ShaderResourceView> bound;
			context->PSGetShaderResources(VolumetricShadows::kSharedShadowMapShaderSlot, 1, bound.GetAddressOf());
			Require(bound.Get() == expected, "Draw binding differs from the current consumer contract");
		};
		const std::array nonconsumers{ Shader::Grass, Shader::Sky, Shader::BloodSplatter, Shader::Utility, Shader::DistantTree };
		const std::array consumers{ Shader::Water, Shader::Lighting, Shader::Effect, Shader::Particle,
			Shader::ImageSpace, Shader::None, Shader::Total, static_cast<Shader>(255) };

		const auto allocationCalls = globals::d3d::device->allocationCalls;
		const auto directionalChecks = globals::stateStorage.directionalChecks;
		for (const auto shader : nonconsumers) {
			context->PSSetShaderResources(VolumetricShadows::kSharedShadowMapShaderSlot, 1, &foreignSRV);
			feature.SetShaderResources(context, shader);
			requireBinding(foreignSRV);
		}
		Require(globals::d3d::device->allocationCalls == allocationCalls &&
					globals::stateStorage.directionalChecks == directionalChecks,
			"Nonconsuming draws entered directional/resource setup");

		for (const auto shader : consumers) {
			feature.settings.Enabled = true;
			globals::stateStorage.directional = true;
			context->PSSetShaderResources(VolumetricShadows::kSharedShadowMapShaderSlot, 1, &foreignSRV);
			feature.SetShaderResources(context, shader);
			requireBinding(feature.fallbackShadowSRV);
			context->ClearState();
			feature.SetShaderResources(context, shader);
			requireBinding(feature.fallbackShadowSRV);

			feature.settings.Enabled = false;
			feature.SetShaderResources(context, Shader::Utility);
			requireBinding(feature.fallbackShadowSRV);
			feature.SetShaderResources(context, shader);
			requireBinding(nullptr);
			feature.settings.Enabled = true;
			feature.SetShaderResources(context, shader);
			requireBinding(feature.fallbackShadowSRV);
			globals::stateStorage.directional = false;
			feature.SetShaderResources(context, shader);
			requireBinding(nullptr);
			globals::stateStorage.directional = true;
			feature.SetShaderResources(context, shader);
			requireBinding(feature.fallbackShadowSRV);
			globals::state = nullptr;
			feature.SetShaderResources(context, shader);
			requireBinding(nullptr);
			globals::state = &globals::stateStorage;
		}
		feature.SetShaderResources(nullptr, Shader::Lighting);
		context->ClearState();
		std::cout << "Consumer admission, context clobber, same-frame toggles and unknown-shader fallback passed.\n";
	}

	void CheckPreparation(ID3D11Device* device, ID3D11DeviceContext* context, VolumetricShadows& feature,
		const std::array<ComPtr<ID3D11ComputeShader>, 6>& shaders)
	{
		DepthTexture source(device, 512, 512), auxiliary(device, 32, 32, true), invalid(device, 32, 32, true, 1);
		auto* input = source.srv.Get();
		context->PSSetShaderResources(4, 1, &input);
		globals::game::rendererStorage.data.depthStencils.storage[0].depthSRV = auxiliary.srv.Get();
		std::array<Util::LazyShader<ID3D11ComputeShader>*, 6> slots{
			&feature.downsampleShadowMip0CS, &feature.downsampleShadowMip1CS,
			&feature.downsampleShadowGeneralMip0CS, &feature.downsampleShadowGeneralMip1CS,
			&feature.blurShadowHorizontalCS, &feature.blurShadowVerticalCS
		};
		for (size_t i = 0; i < slots.size(); ++i)
			slots[i]->value = shaders[i].Get();
		context->CSSetShader(shaders[0].Get(), nullptr, 0);
		ID3D11ShaderResourceView* savedSRVs[]{ source.srv.Get(), auxiliary.srv.Get() };
		context->CSSetShaderResources(0, 2, savedSRVs);
		ID3D11UnorderedAccessView* savedUAV = feature.shadowBlurTempMip1UAV;
		context->CSSetUnorderedAccessViews(0, 1, &savedUAV, nullptr);
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		ComPtr<ID3D11SamplerState> savedSampler;
		Check(device->CreateSamplerState(&samplerDesc, savedSampler.GetAddressOf()));
		Util::SetResourceName(savedSampler.Get(), "VolumetricShadowsTest::PreviousSampler");
		ID3D11SamplerState* sample = savedSampler.Get();
		context->CSSetSamplers(0, 1, &sample);
		feature.CopyShadowLightData();
		Require(feature.shadowCopyValid, "Successful preparation failed");
		ComPtr<ID3D11ComputeShader> returnedShader;
		ComPtr<ID3D11SamplerState> returnedSampler;
		ComPtr<ID3D11UnorderedAccessView> returnedUAV;
		std::array<ComPtr<ID3D11ShaderResourceView>, 2> returnedSRVs;
		context->CSGetShader(returnedShader.GetAddressOf(), nullptr, nullptr);
		context->CSGetSamplers(0, 1, returnedSampler.GetAddressOf());
		context->CSGetUnorderedAccessViews(0, 1, returnedUAV.GetAddressOf());
		for (UINT slot = 0; slot < returnedSRVs.size(); ++slot)
			context->CSGetShaderResources(slot, 1, returnedSRVs[slot].GetAddressOf());
		Require(returnedShader.Get() == shaders[0].Get() && returnedSampler.Get() == savedSampler.Get() &&
					returnedUAV.Get() == savedUAV && returnedSRVs[0].Get() == savedSRVs[0] && returnedSRVs[1].Get() == savedSRVs[1],
			"Shadow preparation did not restore compute bindings");
		ComPtr<ID3D11ShaderResourceView> bound;
		context->PSGetShaderResources(18, 1, bound.GetAddressOf());
		Require(bound.Get() == feature.shadowCopySRV, "Successful preparation did not publish VSM");
		ID3D11UnorderedAccessView* outputHazard = feature.shadowCopyMip0UAV;
		context->CSSetUnorderedAccessViews(0, 1, &outputHazard, nullptr);
		bound.Reset();
		context->PSGetShaderResources(18, 1, bound.GetAddressOf());
		Require(!bound, "Writable shadow resource did not unbind the previous read view");
		ClearComputeUAV(context);
		feature.SetShaderResources(context, RE::BSShader::Type::Lighting);
		context->PSGetShaderResources(18, 1, bound.ReleaseAndGetAddressOf());
		Require(bound.Get() == feature.shadowCopySRV, "Consumer did not restore a hazard-invalidated shadow binding");
		std::array<std::vector<std::array<uint16_t, 2>>, 2> expectedMoments;
		for (UINT mip = 0; mip < 2; ++mip)
			expectedMoments[mip] = ReadMoments(device, context, feature.shadowCopyTexture, mip);
		D3D11_QUERY_DESC predicateDesc{ D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
		ComPtr<ID3D11Predicate> predicate;
		Check(device->CreatePredicate(&predicateDesc, predicate.GetAddressOf()));
		Util::SetResourceName(predicate.Get(), "VolumetricShadowsTest::PreviousPredicate");
		context->Begin(predicate.Get());
		context->End(predicate.Get());
		const std::array<float, 4> poison{};
		context->ClearUnorderedAccessViewFloat(feature.shadowCopyMip0UAV, poison.data());
		context->ClearUnorderedAccessViewFloat(feature.shadowCopyMip1UAV, poison.data());
		context->SetPredication(predicate.Get(), FALSE);
		feature.CopyShadowLightData();
		ComPtr<ID3D11Predicate> returnedPredicate;
		BOOL predicateValue = TRUE;
		context->GetPredication(returnedPredicate.GetAddressOf(), &predicateValue);
		Require(returnedPredicate.Get() == predicate.Get() && predicateValue == FALSE, "Preparation did not restore predication");
		context->SetPredication(nullptr, FALSE);
		for (UINT mip = 0; mip < 2; ++mip)
			Require(ReadMoments(device, context, feature.shadowCopyTexture, mip) == expectedMoments[mip],
				"Inherited predication suppressed shadow preparation");
		globals::d3d::context = nullptr;
		feature.CopyShadowLightData();
		Require(!feature.shadowCopyValid, "Missing context retained stale valid moments");
		globals::d3d::context = context;
		feature.SetShaderResources(context, RE::BSShader::Type::Lighting);
		CheckFallback(device, context, feature);
		for (size_t missing : { 1u, 2u, 4u, 5u }) {
			slots[missing]->value = nullptr;
			feature.CopyShadowLightData();
			CheckFallback(device, context, feature);
			slots[missing]->value = shaders[missing].Get();
		}
		input = nullptr;
		context->PSSetShaderResources(4, 1, &input);
		feature.CopyShadowLightData();
		CheckFallback(device, context, feature);
		input = invalid.srv.Get();
		context->PSSetShaderResources(4, 1, &input);
		feature.CopyShadowLightData();
		CheckFallback(device, context, feature);
		input = source.srv.Get();
		context->PSSetShaderResources(4, 1, &input);
		for (auto* view : { static_cast<ID3D11ShaderResourceView*>(nullptr), invalid.srv.Get() }) {
			globals::game::rendererStorage.data.depthStencils.storage[0].depthSRV = view;
			feature.CopyShadowLightData();
			CheckFallback(device, context, feature);
		}
		globals::game::renderer = nullptr;
		feature.CopyShadowLightData();
		CheckFallback(device, context, feature);
		globals::game::renderer = &globals::game::rendererStorage;
		globals::game::rendererStorage.data.depthStencils.storage[0].depthSRV = auxiliary.srv.Get();
		feature.CopyShadowLightData();
		Require(feature.shadowCopyValid, "Preparation did not recover after failure");
		globals::state = nullptr;
		feature.CopyShadowLightData();
		bound.Reset();
		context->PSGetShaderResources(18, 1, bound.GetAddressOf());
		Require(!bound && !feature.shadowCopyValid, "Missing state retained a valid shadow binding");
		globals::state = &globals::stateStorage;
		feature.settings.Enabled = false;
		feature.CopyShadowLightData();
		bound.Reset();
		context->PSGetShaderResources(18, 1, bound.GetAddressOf());
		Require(!bound && !feature.shadowCopyValid, "Disabled feature retained shadow binding");
		std::cout << "Preparation failure, typed fallback, recovery and disable cases passed.\n";
	}
	void CheckAllocationFailures(ID3D11Device* device, ID3D11DeviceContext* context, VolumetricShadows& feature,
		const std::array<ComPtr<ID3D11ComputeShader>, 6>& shaders)
	{
		context->ClearState();
		feature.settings.Enabled = true;
		DepthTexture source(device, 1024, 1024), auxiliary(device, 32, 32, true);
		auto* input = source.srv.Get();
		context->PSSetShaderResources(4, 1, &input);
		globals::game::rendererStorage.data.depthStencils.storage[0].depthSRV = auxiliary.srv.Get();
		std::array<Util::LazyShader<ID3D11ComputeShader>*, 6> slots{
			&feature.downsampleShadowMip0CS, &feature.downsampleShadowMip1CS,
			&feature.downsampleShadowGeneralMip0CS, &feature.downsampleShadowGeneralMip1CS,
			&feature.blurShadowHorizontalCS, &feature.blurShadowVerticalCS
		};
		auto* fallback = feature.fallbackShadowSRV;
		for (unsigned failure = 1; failure <= 11; ++failure) {
			feature.ReleaseShadowCopyTextures();
			globals::d3d::device->failAfter = failure;
			const auto previousErrors = logger::allocationErrors;
			feature.CopyShadowLightData();
			CheckFallback(device, context, feature);
			Require(feature.shadowCopyAllocationFailed && !feature.shadowCopyTexture && !feature.shadowBlurTempTexture &&
						!feature.shadowCopySRV && !feature.shadowCopyMip0SRV && !feature.shadowCopyMip1SRV &&
						!feature.shadowCopyMip0UAV && !feature.shadowCopyMip1UAV && !feature.shadowBlurTempMip0SRV &&
						!feature.shadowBlurTempMip1SRV && !feature.shadowBlurTempMip0UAV && !feature.shadowBlurTempMip1UAV &&
						feature.shadowCopyWidth == 0 && feature.shadowCopyHeight == 0 && feature.fallbackShadowSRV == fallback,
				"Allocation failure did not discard every partial resource and preserve the fallback");
			Require(logger::allocationErrors == previousErrors + 1, "Allocation failure was not reported once");
			const auto allocations = globals::d3d::device->allocationCalls;
			feature.CopyShadowLightData();
			Require(globals::d3d::device->allocationCalls == allocations && logger::allocationErrors == previousErrors + 1,
				"Failed allocation retried during normal rendering");
			feature.ClearShaderCache();
			for (size_t i = 0; i < slots.size(); ++i)
				slots[i]->value = shaders[i].Get();
			feature.CopyShadowLightData();
			Require(feature.shadowCopyValid && !feature.shadowCopyAllocationFailed, "Explicit reset did not recover allocation");
		}
		SafeRelease(feature.shadowCopyMip1SRV);
		feature.CopyShadowLightData();
		Require(feature.shadowCopyValid && feature.shadowCopyMip1SRV, "Partial resource set did not recover completely");
		std::cout << "All 11 allocation failures, rollback, failure latch, explicit recovery and partial resources passed.\n";
	}

}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		ShadowTestDevice testDevice{ device.Get() };
		globals::d3d::device = &testDevice;
		globals::d3d::context = context.Get();
		const std::array<ComPtr<ID3D11ComputeShader>, 6> shaders{
			Compile(device.Get(), "DOWNSAMPLE_SHADOW_MIP0"), Compile(device.Get(), "DOWNSAMPLE_SHADOW_MIP1"),
			Compile(device.Get(), "DOWNSAMPLE_SHADOW_MIP0", true), Compile(device.Get(), "DOWNSAMPLE_SHADOW_MIP1", true),
			Compile(device.Get(), "BLUR_HORIZONTAL"), Compile(device.Get(), "BLUR_VERTICAL")
		};
		VolumetricShadows feature;
		feature.SetupResources();
		CheckDrawBindings(device.Get(), context.Get(), feature);
		for (auto size : { 256u, 512u, 1024u, 2048u, 4096u, 8192u })
			CheckReduction(device.Get(), context.Get(), feature, shaders, size, size);
		CheckReduction(device.Get(), context.Get(), feature, shaders, 513, 769);
		CheckReduction(device.Get(), context.Get(), feature, shaders, 16384, 513);
		CheckReduction(device.Get(), context.Get(), feature, shaders, 1, 1);
		CheckPreparation(device.Get(), context.Get(), feature, shaders);
		CheckAllocationFailures(device.Get(), context.Get(), feature, shaders);
		context->ClearState();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
