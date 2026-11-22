#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <d3d11sdklayers.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Float4 = std::array<float, 4>;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct Texture
	{
		D3D11_TEXTURE2D_DESC desc{};
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		Texture(ID3D11Device* device, unsigned width, unsigned height, DXGI_FORMAT format, bool writable = true, bool staging = false)
		{
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = format;
			desc.BindFlags = staging ? 0 : D3D11_BIND_SHADER_RESOURCE | (writable ? D3D11_BIND_UNORDERED_ACCESS : 0);
			desc.Usage = staging ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
			desc.CPUAccessFlags = staging ? D3D11_CPU_ACCESS_READ : 0;
			Check(device->CreateTexture2D(&desc, nullptr, resource.GetAddressOf()));
			Util::SetResourceName(resource.Get(), "StereoShadowTest::Texture");
			if (!staging) {
				D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
				viewDesc.Format = format == DXGI_FORMAT_R24G8_TYPELESS ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : format;
				viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				viewDesc.Texture2D.MipLevels = 1;
				Check(device->CreateShaderResourceView(resource.Get(), &viewDesc, srv.GetAddressOf()));
				Util::SetResourceName(srv.Get(), "StereoShadowTest::Texture SRV");
			}
			if (writable && !staging) {
				Check(device->CreateUnorderedAccessView(resource.Get(), nullptr, uav.GetAddressOf()));
				Util::SetResourceName(uav.Get(), "StereoShadowTest::Texture UAV");
			}
		}
	};

	bool SelectSecondary(Texture* screenSpaceShadowsTexture, unsigned frameWidth, unsigned frameHeight, bool sparse)
	{
		struct
		{
			bool active;
		} foveatedState{ sparse };
#include "stereo_output_selection.h"
		return publishSecondary;
	}

	struct Includes : ID3DInclude
	{
		Util::CustomInclude common{ "package/Shaders" };
		Util::CustomInclude feature{ "features/Screen-Space Shadows/Shaders" };
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			return std::filesystem::exists(std::filesystem::path("package/Shaders") / name) ?
			           common.Open(type, name, parent, data, size) :
			           feature.Open(type, name, parent, data, size);
		}
		HRESULT Close(LPCVOID data) override { return common.Close(data); }
	};

	struct Fixture
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		std::unique_ptr<ConstantBuffer> frame, shared, sync;
		Fixture(ID3D11Device* device, bool reproject, bool terrain, bool reverse)
		{
			std::vector<D3D_SHADER_MACRO> macros{ { "VR", "1" }, { "FRAMEBUFFER", "1" }, { "COMPUTESHADER", "1" } };
			if (terrain)
				macros.push_back({ "TERRAIN_BLENDING", "1" });
			if (reverse)
				macros.push_back({ "REVERSE_Z", "1" });
			macros.push_back({ nullptr, nullptr });
			Includes includes;
			ComPtr<ID3DBlob> code, errors;
			const wchar_t* source = reproject ?
			                            L"features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/ShadowReprojectCS.hlsl" :
			                            L"features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/StereoSyncCS.hlsl";
			auto result = D3DCompileFromFile(source, macros.data(), &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(result);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "StereoShadowTest::Producer");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			frame = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			shared = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::SharedData");
			sync = std::make_unique<ConstantBuffer>(device, reflection.Get(), "StereoSyncCB");
			std::array<float, 32> identity{};
			for (unsigned eye = 0; eye < 2; ++eye)
				for (unsigned i = 0; i < 4; ++i) identity[eye * 16 + i * 5] = 1;
			for (const auto* name : { "FrameBuffer::CameraProjUnjitteredInverse", "FrameBuffer::CameraViewInverse",
					 "FrameBuffer::CameraViewProjUnjittered", "FrameBuffer::CameraViewProjInverse", "FrameBuffer::CameraViewProj" })
				frame->SetVariable(name, identity);
			for (const auto* name : { "FrameBuffer::DynamicResolutionParams1", "FrameBuffer::DynamicResolutionParams2" })
				frame->SetVariable(name, Float4{ 1, 1, 1, 1 });
			frame->SetVariable("FrameBuffer::CameraPosAdjust", std::array<float, 8>{ 0, 0, 0, 0, 0.03f, 0, 0, 0 });
		}

		std::vector<unsigned char> Run(ID3D11DeviceContext* context, Texture& raw, Texture& secondary, Texture& depth,
			Texture& readback, const std::vector<unsigned char>& source, unsigned width, unsigned height, float feather,
			bool sparse, bool candidate, bool firstPerson)
		{
			ID3D11ShaderResourceView* nullView = nullptr;
			context->PSSetShaderResources(45, 1, &nullView);
			context->UpdateSubresource(raw.resource.Get(), 0, nullptr, source.data(), raw.desc.Width, 0);
			const float poison[4]{ 0.13f, 0, 0, 0 };
			const float white[4]{ 1, 1, 1, 1 };
			context->ClearUnorderedAccessViewFloat(secondary.uav.Get(), poison);
			const bool secondaryOutput = candidate && SelectSecondary(&raw, width, height, sparse);
			Require(SelectSecondary(&raw, width, height, sparse) == (!sparse && width == raw.desc.Width && height == raw.desc.Height),
				"Unqualified sparse or padded output selected");
			std::vector<D3D11_BOX> bounds;
			if (sparse) {
				const unsigned half = width / 2;
				bounds = { { 2, 3, 0, half - 2, height - 2, 1 }, { half + 3, 1, 0, width - 1, height - 3, 1 } };
			} else {
				bounds = { { 0, 0, 0, width, height, 1 } };
			}
			if (sparse)
				context->ClearUnorderedAccessViewFloat(secondary.uav.Get(), white);
			if (!secondaryOutput) {
				if (!sparse)
					context->CopyResource(secondary.resource.Get(), raw.resource.Get());
				else
					for (const auto& box : bounds)
						context->CopySubresourceRegion(secondary.resource.Get(), 0, box.left, box.top, 0, raw.resource.Get(), 0, &box);
			}
			shared->SetVariable("SharedData::CameraData", Float4{ 1, 1, 0, firstPerson ? 5.0f : 100.0f });
			frame->Bind(context);
			shared->Bind(context);
			ID3D11ShaderResourceView* inputs[]{ depth.srv.Get(), secondaryOutput ? raw.srv.Get() : secondary.srv.Get() };
			ID3D11UnorderedAccessView* output = secondaryOutput ? secondary.uav.Get() : raw.uav.Get();
			context->CSSetShaderResources(0, 2, inputs);
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			for (unsigned eye = 0; eye < bounds.size(); ++eye) {
				const auto& box = bounds[eye];
				sync->SetVariable("FrameDim", std::array<float, 2>{ float(width), float(height) });
				sync->SetVariable("RcpFrameDim", std::array<float, 2>{ 1.0f / width, 1.0f / height });
				sync->SetVariable("DispatchBase", std::array<float, 2>{ float(box.left), float(box.top) });
				sync->SetVariable("DispatchExtent", std::array<float, 2>{ float(box.right - box.left), float(box.bottom - box.top) });
				sync->SetVariable("FoveatedData0", Float4{ 0.6f, feather, 1.2f, sparse ? 1.0f : 0.0f });
				sync->SetVariable("FoveatedCenterOffset", Float4{ eye ? 0.12f : -0.15f, eye ? -0.05f : 0.07f, 0, 0 });
				sync->Bind(context);
				context->Dispatch((box.right - box.left + 7) / 8, (box.bottom - box.top + 7) / 8, 1);
			}
			ID3D11ShaderResourceView* nullInputs[2]{};
			ID3D11UnorderedAccessView* nullOutput = nullptr;
			context->CSSetShaderResources(0, 2, nullInputs);
			context->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
			context->CSSetShader(nullptr, nullptr, 0);
			auto& selected = secondaryOutput ? secondary : raw;
			auto* view = selected.srv.Get();
			context->PSSetShaderResources(45, 1, &view);
			context->CopyResource(readback.resource.Get(), selected.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(readback.resource.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<unsigned char> pixels(raw.desc.Width * raw.desc.Height);
			for (unsigned y = 0; y < raw.desc.Height; ++y)
				std::memcpy(pixels.data() + y * raw.desc.Width, static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, raw.desc.Width);
			context->Unmap(readback.resource.Get(), 0);
			return pixels;
		}
	};
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		ComPtr<ID3D11InfoQueue> info;
		Check(device.As(&info));
		unsigned comparisons = 0;
		for (bool reproject : { false, true })
			for (bool terrain : { false, true })
				for (bool reverse : { false, true }) {
					Fixture fixture(device.Get(), reproject, terrain, reverse);
					for (unsigned allocationWidth : { 64u, 65u }) {
						constexpr unsigned allocationHeight = 37;
						Texture raw(device.Get(), allocationWidth, allocationHeight, DXGI_FORMAT_R8_UNORM);
						Texture secondary(device.Get(), allocationWidth, allocationHeight, DXGI_FORMAT_R8_UNORM);
						const bool floatDepth = terrain || reverse;
						Texture depth(device.Get(), allocationWidth, allocationHeight, floatDepth ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R24G8_TYPELESS, false);
						Texture readback(device.Get(), allocationWidth, allocationHeight, DXGI_FORMAT_R8_UNORM, false, true);
						std::vector<float> depths(allocationWidth * allocationHeight);
						std::vector<unsigned char> source(depths.size());
						for (unsigned i = 0; i < depths.size(); ++i) {
							depths[i] = i % 17 == 0 ? 0.0f : i % 19 == 0 ? 1.0f :
							                             i % 7 == 0      ? 0.8f :
							                                               0.5f;
							source[i] = static_cast<unsigned char>(20 + (i * 71 + (i % allocationWidth > allocationWidth / 2 ? 43 : 0)) % 230);
						}
						std::vector<std::uint32_t> unormDepth(depths.size());
						for (size_t i = 0; i < depths.size(); ++i)
							unormDepth[i] = static_cast<std::uint32_t>(std::lround(depths[i] * 16777215.0));
						const void* depthData = floatDepth ? static_cast<const void*>(depths.data()) : unormDepth.data();
						context->UpdateSubresource(depth.resource.Get(), 0, nullptr, depthData, allocationWidth * sizeof(float), 0);
						for (bool padded : { false, true })
							for (bool sparse : { false, true })
								for (float feather : { 0.0f, 1e-5f, 0.15f })
									for (bool firstPerson : { false, true }) {
										const unsigned width = allocationWidth - (padded ? 3 : 0), height = allocationHeight - (padded ? 4 : 0);
										auto baseline = fixture.Run(context.Get(), raw, secondary, depth, readback, source, width, height, feather, sparse, false, firstPerson);
										auto candidate = fixture.Run(context.Get(), raw, secondary, depth, readback, source, width, height, feather, sparse, true, firstPerson);
										Require(baseline == candidate, "Stereo output bytes differ");
										comparisons += static_cast<unsigned>(baseline.size());
									}
						context->ClearState();
					}
				}
		for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
			SIZE_T size = 0;
			Check(info->GetMessage(i, nullptr, &size));
			std::vector<std::byte> storage(size);
			auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
			Check(info->GetMessage(i, message, &size));
			if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
				std::cerr << message->pDescription << '\n';
				throw std::runtime_error("D3D debug-layer warning or error");
			}
		}
		std::cout << comparisons << " stereo R8 bytes matched; D3D debug layer clean.\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
