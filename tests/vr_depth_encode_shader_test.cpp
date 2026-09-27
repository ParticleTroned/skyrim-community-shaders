#define NOMINMAX
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <winrt/base.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = winrt::com_ptr<T>;
	constexpr UINT eyeWidth = 13, sourceWidth = 2 * eyeWidth, sourceHeight = 11;
	constexpr UINT targetWidth = 16, targetHeight = 13;

	struct Texture2D
	{
		D3D11_TEXTURE2D_DESC desc{};
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
	};

#include "depth_encode_scope_exit.h"

#include "depth_encode_identity.h"
#include "depth_encode_texture_desc.h"
#include "depth_encode_validation.h"

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct ShaderIncludes : ID3DInclude
	{
		Util::CustomInclude package{ "package/Shaders" };
		Util::CustomInclude feature{ "features/Upscaling/Shaders" };
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			return std::string_view(name).starts_with("Upscaling/") ?
			           feature.Open(type, name, parent, data, size) :
			           package.Open(type, name, parent, data, size);
		}
		HRESULT Close(LPCVOID data) override { return package.Close(data); }
	};

	Texture2D MakeTexture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format,
		UINT bindFlags, const char* name, const D3D11_SUBRESOURCE_DATA* initial = nullptr)
	{
		Texture2D result;
		auto& desc = result.desc;
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = format;
		desc.BindFlags = bindFlags;
		Check(device->CreateTexture2D(&desc, initial, result.resource.put()));
		Util::SetResourceName(result.resource.get(), "DepthEncodeTest::%s", name);
		if (bindFlags & D3D11_BIND_SHADER_RESOURCE) {
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = format == DXGI_FORMAT_R24G8_TYPELESS ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : format;
			view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipLevels = 1;
			Check(device->CreateShaderResourceView(result.resource.get(), &view, result.srv.put()));
			Util::SetResourceName(result.srv.get(), "DepthEncodeTest::%s SRV", name);
		}
		if (bindFlags & D3D11_BIND_UNORDERED_ACCESS) {
			Check(device->CreateUnorderedAccessView(result.resource.get(), nullptr, result.uav.put()));
			Util::SetResourceName(result.uav.get(), "DepthEncodeTest::%s UAV", name);
		}
		return result;
	}

	ComPtr<ID3DBlob> Compile(const char* method, bool vr, bool depthOutput)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { method, "1" }, { "COMPUTESHADER", "1" }, { "DX11", "1" }, { "WINPC", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (depthOutput)
			defines.push_back({ "DEPTH_OUTPUT", "1" });
		defines.push_back({ nullptr, nullptr });
		ShaderIncludes includes;
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(L"features/Upscaling/Shaders/Upscaling/EncodeTexturesCS.hlsl",
			defines.data(), &includes, "main", "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, code.put(), errors.put());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	void CheckGuards(const Texture2D& source, Texture2D& output)
	{
		Require(IsVRDepthEncodeSourceValid(source.srv.get(), source.resource.get(), sourceWidth, sourceHeight), "Valid engine depth rejected");
		Require(!IsVRDepthEncodeSourceValid(source.srv.get(), output.resource.get(), sourceWidth, sourceHeight), "Wrong source identity accepted");
		Require(!IsVRDepthEncodeSourceValid(nullptr, source.resource.get(), 1, 1), "Missing SRV accepted");
		Require(!IsVRDepthEncodeSourceValid(source.srv.get(), nullptr, 1, 1), "Missing resource accepted");
		for (auto size : { std::array<UINT, 2>{ 0, 1 }, { 1, 0 }, { sourceWidth + 1, 1 }, { 1, sourceHeight + 1 }, { std::numeric_limits<UINT>::max(), std::numeric_limits<UINT>::max() } })
			Require(!IsVRDepthEncodeSourceValid(source.srv.get(), source.resource.get(), size[0], size[1]), "Invalid depth bounds accepted");
		Require(IsVRDepthEncodeTargetValid(&output, targetWidth, targetHeight), "Valid typed target rejected");
		Require(!IsVRDepthEncodeTargetValid(&output, targetWidth + 1, targetHeight), "Small target accepted");
		Require(!IsVRDepthEncodeTargetValid(&output, targetWidth, targetHeight + 1), "Short target accepted");
		Require(!IsVRDepthEncodeTargetValid(&output, 0, 1), "Empty target region accepted");
		Require(!IsVRDepthEncodeTargetValid(nullptr, 1, 1), "Missing target accepted");
		const auto desc = output.desc;
		output.desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		Require(!IsVRDepthEncodeTargetValid(&output, 1, 1), "Depth-stencil copy target accepted");
		output.desc = desc;
		output.desc.ArraySize = 2;
		Require(!IsVRDepthEncodeTargetValid(&output, 1, 1), "Array target accepted");
		output.desc = desc;
		output.desc.SampleDesc.Count = 2;
		Require(!IsVRDepthEncodeTargetValid(&output, 1, 1), "Multisampled target accepted");
		output.desc = desc;
		auto savedUAV = std::move(output.uav);
		Require(!IsVRDepthEncodeTargetValid(&output, 1, 1), "Missing UAV accepted");
		output.uav = std::move(savedUAV);
	}

	void VerifyShader(ID3D11Device* device, ID3D11DeviceContext* context, const char* method, bool vr)
	{
		std::array<uint32_t, sourceWidth * sourceHeight> nativeDepth{};
		for (UINT y = 0; y < sourceHeight; ++y)
			for (UINT x = 0; x < sourceWidth; ++x)
				nativeDepth[y * sourceWidth + x] = (x * 593117u + y * 31957u) | ((x * 7u & 0xffu) << 24);
		nativeDepth[0] = 0;
		nativeDepth.back() = 0xffffffffu;
		const D3D11_SUBRESOURCE_DATA initial{ nativeDepth.data(), sourceWidth * sizeof(uint32_t), 0 };
		auto source = MakeTexture(device, sourceWidth, sourceHeight, DXGI_FORMAT_R24G8_TYPELESS,
			D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, "NativeDepth", &initial);
		std::array<Texture2D, 3> inputs;
		std::array<Texture2D, 4> outputs;
		const std::array<std::array<float, 4>, sourceWidth * sourceHeight> guideData{};
		const D3D11_SUBRESOURCE_DATA guideInitial{ guideData.data(), sourceWidth * sizeof(guideData[0]), 0 };
		for (UINT i = 0; i < inputs.size(); ++i)
			inputs[i] = MakeTexture(device, sourceWidth, sourceHeight, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE, "GuideInput", &guideInitial);
		for (UINT i = 0; i < outputs.size(); ++i)
			outputs[i] = MakeTexture(device, targetWidth, targetHeight, i == 2 ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32_FLOAT,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, "GuideOutput");
		CheckGuards(source, outputs[3]);

		auto code = Compile(method, vr, true);
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.put()));
		Util::SetResourceName(shader.get(), "DepthEncodeTest::%s CS", method);
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.put())));
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		Check(reflection->GetResourceBindingDescByName("DepthOutput", &binding));
		Require(binding.BindPoint == 3, "Depth output must use the existing u3 slot");
		ConstantBuffer constants(device, reflection.get(), "UpscalingData");
		std::vector<std::unique_ptr<ConstantBuffer>> sharedBuffers;
		D3D11_SHADER_DESC reflected{};
		Check(reflection->GetDesc(&reflected));
		for (UINT i = 0; i < reflected.ConstantBuffers; ++i) {
			D3D11_SHADER_BUFFER_DESC buffer{};
			Check(reflection->GetConstantBufferByIndex(i)->GetDesc(&buffer));
			if (buffer.Type == D3D_CT_CBUFFER && std::string_view(buffer.Name) != "UpscalingData")
				sharedBuffers.push_back(std::make_unique<ConstantBuffer>(device, reflection.get(), buffer.Name));
		}
		D3D11_TEXTURE2D_DESC stagingDesc = outputs[3].desc;
		stagingDesc.BindFlags = 0;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> staging;
		Check(device->CreateTexture2D(&stagingDesc, nullptr, staging.put()));
		Util::SetResourceName(staging.get(), "DepthEncodeTest::Readback");

		for (UINT eye : { 0u, 1u }) {
			for (UINT mode : { 0u, 1u, 2u }) {
				context->ClearState();
				auto restoreState = ScopeExit([&] { context->ClearState(); });
				const UINT x = mode ? 3u : 0u, y = mode ? 2u : 0u;
				const UINT width = mode ? 7u : eyeWidth, height = mode ? 5u : sourceHeight;
				const UINT outX = mode == 1 ? x : 0u, outY = mode == 1 ? y : 0u;
				constants.SetVariable("DispatchDim", std::array<float, 2>{ float(width), float(height) });
				constants.SetVariable("TrueSamplingDim", std::array<float, 2>{ float(sourceWidth), float(sourceHeight) });
				constants.SetVariable("InvTrueSamplingDim", std::array<float, 2>{ 1.0f / sourceWidth, 1.0f / sourceHeight });
				constants.SetVariable("SourceOffset", std::array<float, 2>{ float(eye * eyeWidth + x), float(y) });
				constants.SetVariable("OutputOffset", std::array<float, 2>{ float(outX), float(outY) });
				constants.SetVariable("SourceSamplingXBounds", std::array<float, 2>{ float(eye * eyeWidth), float((eye + 1) * eyeWidth) });
				constants.Bind(context);
				for (auto& buffer : sharedBuffers)
					buffer->Bind(context);
				const float sentinel[4]{ -1, -1, -1, -1 };
				context->ClearUnorderedAccessViewFloat(outputs[3].uav.get(), sentinel);
				ID3D11ShaderResourceView* srvs[]{ inputs[0].srv.get(), inputs[1].srv.get(), inputs[2].srv.get(), source.srv.get() };
				ID3D11UnorderedAccessView* uavs[]{ outputs[0].uav.get(), outputs[1].uav.get(), outputs[2].uav.get(), outputs[3].uav.get() };
				context->CSSetShaderResources(0, 4, srvs);
				context->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);
				context->CSSetShader(shader.get(), nullptr, 0);
				context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
				context->ClearState();
				context->CopyResource(staging.get(), outputs[3].resource.get());
				D3D11_MAPPED_SUBRESOURCE mapped{};
				Check(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
				auto unmap = ScopeExit([&] { context->Unmap(staging.get(), 0); });
				for (UINT row = 0; row < targetHeight; ++row) {
					const auto* values = reinterpret_cast<const float*>(static_cast<const std::byte*>(mapped.pData) + row * mapped.RowPitch);
					for (UINT col = 0; col < targetWidth; ++col) {
						float expected = -1.0f;
						if (col >= outX && col < outX + width && row >= outY && row < outY + height) {
							const auto packed = nativeDepth[(y + row - outY) * sourceWidth + eye * eyeWidth + x + col - outX];
							expected = float(packed & 0xffffffu) / float(0xffffffu);
						}
						Require(std::isfinite(values[col]) && std::abs(values[col] - expected) < 2e-7f,
							"Depth conversion changed native depth, sampled the wrong eye/crop, or wrote outside the dispatch");
					}
				}
			}
		}
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		VerifyShader(device.get(), context.get(), "DLSS", true);
		VerifyShader(device.get(), context.get(), "FSR", true);
		VerifyShader(device.get(), context.get(), "FSR", false);
		auto flatDLSS = Compile("DLSS", false, false);
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(flatDLSS->GetBufferPointer(), flatDLSS->GetBufferSize(), IID_PPV_ARGS(reflection.put())));
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		Require(FAILED(reflection->GetResourceBindingDescByName("DepthOutput", &binding)), "Flat DLSS gained a depth UAV requirement");
		std::cout << "18 typed-depth GPU cases and production validation guards passed.\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
