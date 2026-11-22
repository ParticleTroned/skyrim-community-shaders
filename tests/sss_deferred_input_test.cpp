#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "../src/Features/SubsurfaceScattering/DeferredInput.h"
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cstdint>
#include <d3d11sdklayers.h>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <utility>

// Count uploads while retaining the real D3D resource, allocation and copy paths.
struct UploadContext
{
	ID3D11DeviceContext* context;
	unsigned uploads = 0;
	HRESULT Map(ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped)
	{
		return context->Map(resource, subresource, type, flags, mapped);
	}
	void Unmap(ID3D11Resource* resource, UINT subresource) { context->Unmap(resource, subresource); }
	void UpdateSubresource(ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT pitch, UINT depthPitch)
	{
		++uploads;
		context->UpdateSubresource(resource, subresource, box, data, pitch, depthPitch);
	}
	void CSSetConstantBuffers(UINT slot, UINT count, ID3D11Buffer* const* buffers) { context->CSSetConstantBuffers(slot, count, buffers); }
};

namespace globals::d3d
{
	ID3D11Device* device;
	UploadContext* context;
}
namespace DX
{
	bool failAllocationCheck = false;
	void ThrowIfFailed(HRESULT result)
	{
		if (std::exchange(failAllocationCheck, false))
			throw std::runtime_error("Injected allocation failure");
		D3D11ShaderTest::Check(result);
	}
}
namespace logger
{
	unsigned failures = 0;
	void warn(const char*, const char*) { ++failures; }
}
#include "../src/Buffer.h"
#include "sss_deferred_upload_under_test.h"

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using Microsoft::WRL::ComPtr;
	using Pixel = std::array<float, 4>;
	constexpr UINT width = 30, height = 14;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	void CheckControlUpload(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		UploadContext uploads{ context };
		globals::d3d::device = device;
		globals::d3d::context = &uploads;
		Deferred deferred;
		deferred.SetupDiffuseInputBuffer();
		Require(bool(deferred.diffuseInputCB), "Diffuse control setup failed");
		D3D11_BUFFER_DESC desc{};
		deferred.diffuseInputCB->CB()->GetDesc(&desc);
		Require(desc.ByteWidth == sizeof(SSS::DeferredInput::Control), "Control allocation overreads its upload payload");

		SYSTEM_INFO info{};
		GetSystemInfo(&info);
		auto* memory = static_cast<std::byte*>(VirtualAlloc(nullptr, SIZE_T(info.dwPageSize) * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		Require(memory != nullptr, "Control guard allocation failed");
		const auto release = [](std::byte* p) { VirtualFree(p, 0, MEM_RELEASE); };
		const std::unique_ptr<std::byte, decltype(release)> storage(memory, release);
		DWORD oldProtection = 0;
		Require(VirtualProtect(memory + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &oldProtection) != FALSE, "Control guard protection failed");
		auto* control = new (memory + info.dwPageSize - sizeof(SSS::DeferredInput::Control)) SSS::DeferredInput::Control{ width, height, 1, 0 };

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Buffer> staging;
		Check(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()));
		Util::SetResourceName(staging.Get(), "SSSDeferredTest::ControlReadback");
		const auto verify = [&] {
			ComPtr<ID3D11Buffer> bound;
			context->CSGetConstantBuffers(0, 1, bound.GetAddressOf());
			Require(bound.Get() == deferred.diffuseInputCB->CB(), "Intervening pass left stale control binding");
			context->CopyResource(staging.Get(), bound.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			const auto actual = *static_cast<const SSS::DeferredInput::Control*>(mapped.pData);
			context->Unmap(staging.Get(), 0);
			Require(actual == *control, "GPU control contents are stale");
		};
		deferred.BindDiffuseInput(*control);
		verify();
		Require(uploads.uploads == 1, "First control upload missing");
		for (unsigned i = 0; i < 8; ++i) {
			// Cubemap compute work replaces b0 between invocations.
			context->ClearState();
			deferred.BindDiffuseInput(*control);
			verify();
		}
		Require(uploads.uploads == 1, "Unchanged direct input uploaded repeatedly");
		*control = {};
		deferred.BindDiffuseInput(*control);
		verify();
		for (unsigned i = 0; i < 8; ++i)
			deferred.BindDiffuseInput(*control);
		Require(uploads.uploads == 2, "Inactive/capture fallback input uploaded repeatedly");
		*control = { width - 3, height - 2, 1, 0 };
		deferred.BindDiffuseInput(*control);
		verify();
		Require(uploads.uploads == 3, "Changed extent was not uploaded");
		deferred.SetupDiffuseInputBuffer();
		deferred.BindDiffuseInput(*control);
		verify();
		Require(uploads.uploads == 4, "Resource recreation retained stale upload state");
		DX::failAllocationCheck = true;
		deferred.SetupDiffuseInputBuffer();
		Require(!deferred.diffuseInputCB && !deferred.uploadedDiffuseInput && logger::failures == 1, "Failed allocation retained stale state or swallowed failure");
		deferred.BindDiffuseInput({});
		ComPtr<ID3D11Buffer> bound;
		context->CSGetConstantBuffers(0, 1, bound.GetAddressOf());
		Require(!bound && uploads.uploads == 4, "Unavailable control retained a stale binding");
		context->ClearState();
		globals::d3d::context = nullptr;
		globals::d3d::device = nullptr;
	}

	struct Shader
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;

		Shader(ID3D11Device* device, const wchar_t* file, std::vector<D3D_SHADER_MACRO> defines)
		{
			defines.push_back({ nullptr, nullptr });
			auto includes = PackageIncludes::AllFeatures(".");
			ComPtr<ID3DBlob> code, errors;
			const auto result = D3DCompileFromFile(file, defines.data(), &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, code.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(result);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "SSSDeferredTest::CS");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		}
	};

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		D3D11_TEXTURE2D_DESC desc{};

		Texture(ID3D11Device* device, DXGI_FORMAT format, UINT w = width, UINT h = height, const Pixel* data = nullptr,
			DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN, UINT mipLevels = 1)
		{
			desc.Width = w;
			desc.Height = h;
			desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.MipLevels = mipLevels;
			desc.Format = format;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			D3D11_SUBRESOURCE_DATA initial{ data, w * sizeof(Pixel), 0 };
			Check(device->CreateTexture2D(&desc, data ? &initial : nullptr, resource.GetAddressOf()));
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = viewFormat == DXGI_FORMAT_UNKNOWN ? format : viewFormat;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = mipLevels;
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = srvDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			Check(device->CreateShaderResourceView(resource.Get(), &srvDesc, srv.GetAddressOf()));
			Check(device->CreateUnorderedAccessView(resource.Get(), &uavDesc, uav.GetAddressOf()));
			Util::SetResourceName(resource.Get(), "SSSDeferredTest::Texture");
			Util::SetResourceName(srv.Get(), "SSSDeferredTest::Texture SRV");
			Util::SetResourceName(uav.Get(), "SSSDeferredTest::Texture UAV");
		}

		std::vector<std::byte> Read(ID3D11Device* device, ID3D11DeviceContext* context) const
		{
			auto stagingDesc = desc;
			stagingDesc.BindFlags = 0;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Texture2D> staging;
			Check(device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "SSSDeferredTest::Readback");
			context->CopyResource(staging.Get(), resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			const UINT pixelBytes = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 :
			                        desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 16 :
			                                                                        4;
			const UINT rowBytes = desc.Width * pixelBytes;
			std::vector<std::byte> result(rowBytes * desc.Height);
			for (UINT y = 0; y < desc.Height; ++y)
				std::memcpy(result.data() + y * rowBytes, static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch, rowBytes);
			context->Unmap(staging.Get(), 0);
			return result;
		}
	};

	void Clear(ID3D11DeviceContext* context, const Texture& texture, Pixel value)
	{
		context->ClearUnorderedAccessViewFloat(texture.uav.Get(), value.data());
	}

	SSS::DeferredInput Pending(ID3D11DeviceContext* context, const Texture& source, const Texture& target,
		UINT producedWidth = width, UINT producedHeight = height, bool region = false)
	{
		return { context, source.resource.Get(), source.srv.Get(), source.uav.Get(), target.resource.Get(), target.uav.Get(),
			producedWidth, producedHeight, region };
	}

	void CheckLifetime(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		Texture source(device, DXGI_FORMAT_R16G16B16A16_FLOAT), target(device, source.desc.Format), other(device, source.desc.Format);
		Clear(context, source, { 0.125f, 0.5f, 4, 0.75f });
		const auto expected = source.Read(device, context);
		Clear(context, target, {});
		{
			auto input = Pending(context, source, target);
			auto moved = std::move(input);
			input.Materialize();
			Require(target.Read(device, context) != expected, "Moved-from input copied");
			moved.Materialize();
			Require(target.Read(device, context) == expected, "Fallback failed to copy");
			Clear(context, source, {});
			moved.Materialize();
			Require(!moved.Prepare(target.uav.Get(), width, height).enabled, "Materialized input remained direct");
		}
		Require(target.Read(device, context) == expected, "Fallback copied twice or at destruction");
		{
			auto input = Pending(context, source, target);
			Require(input.Prepare(target.uav.Get(), width, height).enabled, "Complete input rejected");
			Require(target.Read(device, context) == expected, "Eligible input copied");
			input.Consume();
			input.Materialize();
		}
		Require(target.Read(device, context) == expected, "Consumed input overwrote composition");
		for (int reason = 0; reason != 4; ++reason) {
			Clear(context, target, { 1, 1, 1, 1 });
			auto input = Pending(context, source, target, reason == 0 ? width - 1 : width);
			auto control = input.Prepare(reason == 1 ? other.uav.Get() : reason == 2 ? nullptr :
																					   target.uav.Get(),
				reason == 3 ? width - 1 : width, height);
			Require(!control.enabled && !input.View(), "Unsafe copy footprint or output accepted");
			Require(target.Read(device, context) == source.Read(device, context), "Ineligible input lost original copy");
		}
		{
			Clear(context, target, { 1, 1, 1, 1 });
			auto input = Pending(context, source, target);
		}
		Require(target.Read(device, context) == source.Read(device, context), "Abandoned handoff lost copy");
		SSS::DeferredInput inactive;
		Require(!inactive.Prepare(target.uav.Get(), width, height).enabled && !inactive.View(), "Inactive handoff enabled direct input");
		// Retained COM objects survive replacement of the feature's texture wrapper.
		{
			auto replacement = std::make_unique<Texture>(device, source.desc.Format);
			Clear(context, *replacement, { 0.125f, 0.5f, 4, 0.75f });
			auto input = Pending(context, *replacement, target);
			replacement.reset();
			input.Materialize();
		}
		Require(target.Read(device, context) == expected, "Resource recreation invalidated the retained result");
	}

	void CheckViews(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		for (bool srgb : { false, true }) {
			const auto resourceFormat = srgb ? DXGI_FORMAT_R8G8B8A8_TYPELESS : DXGI_FORMAT_R32G32B32A32_TYPELESS;
			const auto viewFormat = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R32G32B32A32_FLOAT;
			Texture source(device, resourceFormat, width, height, nullptr, viewFormat), target(device, resourceFormat, width, height, nullptr, viewFormat);
			Clear(context, source, { 0.25f, 0.5f, 0.75f, 1 });
			D3D11_SHADER_RESOURCE_VIEW_DESC alternate{};
			source.srv->GetDesc(&alternate);
			alternate.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R32G32B32A32_UINT;
			ComPtr<ID3D11ShaderResourceView> view;
			Check(device->CreateShaderResourceView(source.resource.Get(), &alternate, view.GetAddressOf()));
			Util::SetResourceName(view.Get(), "SSSDeferredTest::Alternate SRV");
			SSS::DeferredInput input(context, source.resource.Get(), view.Get(), source.uav.Get(), target.resource.Get(), target.uav.Get(), width, height, false);
			Require(!input.Prepare(target.uav.Get(), width, height).enabled, "Incompatible typed interpretation accepted");
			Require(!input.View(), "Incompatible SRV survived materialization");
		}
		Texture mipSource(device, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, nullptr, DXGI_FORMAT_UNKNOWN, 2);
		Texture mipTarget(device, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, nullptr, DXGI_FORMAT_UNKNOWN, 2);
		auto input = Pending(context, mipSource, mipTarget);
		Require(!input.Prepare(mipTarget.uav.Get(), width, height).enabled, "Multi-subresource copy was suppressed");
	}

	struct Constants
	{
		ConstantBuffer shared, feature, frame;
		Constants(ID3D11Device* device, const Shader& shader, bool vr, bool linear) :
			shared(device, shader.reflection.Get(), "SharedData::SharedData"),
			feature(device, shader.reflection.Get(), "SharedData::FeatureData"),
			frame(device, shader.reflection.Get(), "FrameBuffer::PerFrame")
		{
			shared.SetVariable("SharedData::BufferDim", Pixel{ float(width), float(height), 1.0f / width, 1.0f / height });
			shared.SetVariable("SharedData::CameraData", Pixel{ 1, 1, 0, 100 });
			feature.SetMember("SharedData::linearLightingSettings", "enableLinearLighting", unsigned(linear));
			constexpr std::array<float, 16> identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
			for (const char* matrix : { "FrameBuffer::CameraViewInverse", "FrameBuffer::CameraViewProjInverse", "FrameBuffer::CameraViewProjUnjittered", "FrameBuffer::CameraPreviousViewProjUnjittered" }) {
				// The Burley and composite shaders retain different subsets of frame constants.
				D3D11_SHADER_VARIABLE_DESC variable{};
				if (FAILED(frame.reflection->GetVariableByName(matrix)->GetDesc(&variable)))
					continue;
				if (vr)
					frame.SetVariable(matrix, std::array{ identity, identity });
				else
					frame.SetVariable(matrix, identity);
			}
			frame.SetVariable("FrameBuffer::DynamicResolutionParams1", Pixel{ 1, 1, 1, 1 });
			frame.SetVariable("FrameBuffer::DynamicResolutionParams2", Pixel{ 1, 1, 1, 1 });
		}
		void Bind(ID3D11DeviceContext* context)
		{
			shared.Bind(context);
			feature.Bind(context);
			frame.Bind(context);
		}
	};

	void CheckComposition(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool reverse, bool linear, bool gi, DXGI_FORMAT format)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { "CSHADER", "1" }, { "FRAMEBUFFER", "1" }, { "TERRAIN_BLENDING", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (reverse)
			defines.push_back({ "REVERSE_Z", "1" });
		auto burleyDefines = defines;
		burleyDefines.push_back({ "BURLEY", "1" });
		if (gi)
			defines.push_back({ "SSGI", "1" });
		Shader burley(device, L"features/Subsurface Scattering/Shaders/SubsurfaceScattering/SeparableSSSCS.hlsl", burleyDefines);
		Shader composite(device, L"package/Shaders/DeferredCompositeCS.hlsl", defines);
		Constants burleyConstants(device, burley, vr, linear), compositeConstants(device, composite, vr, linear);
		for (const char* sex : { "Male", "Female" })
			for (const char* setting : { "Intensity", "Brightness", "Saturation", "BaseSaturation" })
				burleyConstants.shared.SetVariable((std::string("SharedData::SSSHuman") + sex + setting).c_str(), 1.0f);
		ConstantBuffer sss(device, burley.reflection.Get(), "PerFrameSSS");
		sss.SetVariable("SSSS_FOVY", 90.0f);
		sss.SetVariable("BurleySamples", 16u);
		sss.SetVariable("MeanFreePathBase", Pixel{ 0.56f, 0.56f, 0.56f, 2.67f });
		sss.SetVariable("MeanFreePathHuman", Pixel{ 1, 0.37f, 0.3f, 2.67f });
		ConstantBuffer control(device, composite.reflection.Get(), "DeferredDiffuseInput");
		Require(control.slot == 0 && control.bytes.size() == sizeof(SSS::DeferredInput::Control), "Composite control layout changed");
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		Check(composite.reflection->GetResourceBindingDescByName("DeferredDiffuseTexture", &binding));
		Require(binding.BindPoint == 16, "Diffuse SRV slot changed");

		std::array<Pixel, width * height> colors{}, depths{}, masks{};
		for (UINT i = 0; i < colors.size(); ++i) {
			const auto x = i % width;
			colors[i] = { 0.01f * (1 + x), x < width / 2 ? 0.07f : 0.31f, i % 5 == 0 ? 8.0f : 0.01f, float(i % 8) / 8 };
			depths[i] = { i % 13 == 0 ? (reverse ? 0.0f : 1.0f) : i % 17 == 0 ? -0.1f :
																				0.5f,
				0, 0, 0 };
			masks[i] = { float(i % 4) / 3, float(i % 3), 0.01f, 0 };
		}
		Texture color(device, DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, colors.data());
		Texture depth(device, DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, depths.data());
		Texture mask(device, DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, masks.data());
		Texture albedo(device, DXGI_FORMAT_R10G10B10A2_UNORM), normal(device, DXGI_FORMAT_R10G10B10A2_UNORM);
		Texture specular(device, DXGI_FORMAT_R11G11B10_FLOAT), masks2(device, DXGI_FORMAT_R8G8B8A8_UNORM);
		Texture giTexture(device, DXGI_FORMAT_R16G16B16A16_FLOAT);
		Clear(context, albedo, { 0.5f, 0.4f, 0.3f, 1 });
		Clear(context, normal, { 0.5f, 0.5f, 0.5f, 0 });
		Clear(context, specular, { 0.01f, 0.02f, 0.03f, 0 });
		Clear(context, masks2, {});
		Clear(context, giTexture, { 0.2f, 0, 0, 0 });

		for (int domain = 0; domain != 3; ++domain) {
			const bool cropped = domain == 1;
			const UINT tempWidth = cropped ? 17 : width, tempHeight = cropped ? 9 : height;
			const UINT groupsX = domain == 2 ? 2 : (tempWidth + 7) / 8, groupsY = (tempHeight + 7) / 8;
			const float ratioX = cropped ? 16.6f / width : domain == 2 ? 16.0f / width :
			                                                             1.0f;
			const float ratioY = cropped ? 8.6f / height : 1.0f;
			for (auto* constants : { &burleyConstants, &compositeConstants }) {
				constants->frame.SetVariable("FrameBuffer::DynamicResolutionParams1", Pixel{ ratioX, ratioY, ratioX, ratioY });
				constants->frame.SetVariable("FrameBuffer::DynamicResolutionParams2", Pixel{ 1 / ratioX, 1 / ratioY, ratioX, ratioX });
			}
			Texture temp(device, format, tempWidth, tempHeight);
			Clear(context, temp, { 1000, 2000, 3000, 0.25f });
			context->ClearState();
			burleyConstants.Bind(context);
			sss.Bind(context);
			ID3D11ShaderResourceView* inputs[]{ color.srv.Get(), depth.srv.Get(), mask.srv.Get(), albedo.srv.Get(), normal.srv.Get() };
			context->CSSetShaderResources(0, 5, inputs);
			auto* output = temp.uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->CSSetShader(burley.shader.Get(), nullptr, 0);
			context->Dispatch(groupsX, groupsY, 1);
			context->ClearState();

			std::array<std::vector<std::byte>, 3> expected;
			std::vector<std::byte> captureInput;
			for (int route = 0; route != 3; ++route) {
				Texture main(device, format), taa(device, DXGI_FORMAT_R16G16B16A16_FLOAT), motion(device, DXGI_FORMAT_R16G16_FLOAT);
				Clear(context, main, { 0.04f, 0.08f, 0.16f, 0.5f });
				Clear(context, taa, { 0.1f, 0.2f, 0.3f, 0.4f });
				Clear(context, motion, { 0.3f, -0.7f, 0, 0 });
				// Keep the baseline independent of the handoff's copy and rectangle logic.
				if (route == 0) {
					if (cropped) {
						const D3D11_BOX box{ 0, 0, 0, tempWidth, tempHeight, 1 };
						context->CopySubresourceRegion(main.resource.Get(), 0, 0, 0, 0, temp.resource.Get(), 0, &box);
					} else {
						context->CopyResource(main.resource.Get(), temp.resource.Get());
					}
				}
				auto input = route == 0 ? SSS::DeferredInput{} : Pending(context, temp, main, std::min(width, groupsX * 8), std::min(height, groupsY * 8), cropped);
				if (route == 2)
					input.Materialize();
				if (route == 0)
					captureInput = main.Read(device, context);
				if (route == 2)
					Require(main.Read(device, context) == captureInput, "Capture/detector MAIN input changed");
				auto data = input.Prepare(main.uav.Get(), std::min(width, groupsX * 8), std::min(height, groupsY * 8));
				Require(bool(data.enabled) == (route == 1 && domain != 2), "Unexpected direct eligibility");
				std::memcpy(control.bytes.data(), &data, sizeof(data));
				compositeConstants.Bind(context);
				control.Bind(context);
				ID3D11ShaderResourceView* srvs[17]{ specular.srv.Get(), albedo.srv.Get(), normal.srv.Get(), mask.srv.Get(), depth.srv.Get() };
				srvs[9] = masks2.srv.Get();
				for (UINT slot = 10; slot <= 13; ++slot)
					srvs[slot] = giTexture.srv.Get();
				srvs[16] = input.View();
				context->CSSetShaderResources(0, 17, srvs);
				ID3D11UnorderedAccessView* uavs[]{ main.uav.Get(), taa.uav.Get(), motion.uav.Get() };
				context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
				context->CSSetShader(composite.shader.Get(), nullptr, 0);
				context->Dispatch(groupsX, groupsY, 1);
				input.Consume();
				context->ClearState();
				input.Materialize();
				const std::array actual{ main.Read(device, context), taa.Read(device, context), motion.Read(device, context) };
				if (route == 0)
					expected = actual;
				else
					Require(actual == expected, "Direct/capture-fallback composite changed color, alpha, normal/TAA or motion");
			}
		}
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		ComPtr<ID3D11InfoQueue> messages;
		Check(device.As(&messages));
		CheckControlUpload(device.Get(), context.Get());
		CheckLifetime(device.Get(), context.Get());
		CheckViews(device.Get(), context.Get());
		for (bool vr : { false, true })
			for (bool reverse : { false, true })
				for (bool linear : { false, true })
					for (bool gi : { false, true })
						for (auto format : { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT })
							CheckComposition(device.Get(), context.Get(), vr, reverse, linear, gi, format);
		for (UINT64 i = 0; i < messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
			SIZE_T size = 0;
			Check(messages->GetMessage(i, nullptr, &size));
			std::vector<std::byte> storage(size);
			auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
			Check(messages->GetMessage(i, message, &size));
			if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
				throw std::runtime_error(message->pDescription);
		}
		std::cout << "PASS: production control upload bounds/cache/rebinding; 432 Burley/composite routes; exact color/alpha, normal/TAA and motion; copy lifecycle and D3D11 debug layer\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
