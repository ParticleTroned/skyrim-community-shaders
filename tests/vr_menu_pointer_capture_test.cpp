#include "Features/Upscaling/VRMenuPointerOverlay.h"
#include "d3d_resource_naming.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message ? message : "missing diagnostic");
	}
	winrt::com_ptr<ID3DBlob> Compile(const std::string& source, const char* profile, const char* name = nullptr,
		const D3D_SHADER_MACRO* defines = nullptr)
	{
		winrt::com_ptr<ID3DBlob> code, errors;
		const auto result = D3DCompile(source.data(), source.size(), name, defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"main", profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, code.put(), errors.put());
		if (FAILED(result) && errors)
			throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
		winrt::check_hresult(result);
		return code;
	}
	winrt::com_ptr<ID3DBlob> CompileProduction(const char* file, const char* profile, const D3D_SHADER_MACRO* defines = nullptr)
	{
		const auto root = std::filesystem::path(__FILE__).parent_path().parent_path() / "features/Upscaling/Shaders";
		std::ifstream input(root / "Upscaling" / file);
		Require(input.good(), "production shader is missing");
		const std::string source{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
		return Compile(source, profile, (root / "pointer-test.hlsl").string().c_str(), defines);
	}
	UINT ColourRegister(ID3DBlob* code, bool input)
	{
		winrt::com_ptr<ID3D11ShaderReflection> reflection;
		winrt::check_hresult(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), __uuidof(ID3D11ShaderReflection), reflection.put_void()));
		D3D11_SHADER_DESC desc{};
		winrt::check_hresult(reflection->GetDesc(&desc));
		for (UINT i = 0; i < (input ? desc.InputParameters : desc.OutputParameters); ++i) {
			D3D11_SIGNATURE_PARAMETER_DESC parameter{};
			winrt::check_hresult(input ? reflection->GetInputParameterDesc(i, &parameter) : reflection->GetOutputParameterDesc(i, &parameter));
			if (std::strcmp(parameter.SemanticName, "COLOR") == 0 && parameter.SemanticIndex == 0)
				return parameter.Register;
		}
		throw std::runtime_error("compiled shader lost COLOR0");
	}
	struct Texture
	{
		winrt::com_ptr<ID3D11Texture2D> texture;
		winrt::com_ptr<ID3D11RenderTargetView> rtv;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
	};
	struct Pixel
	{
		uint8_t r, g, b, a;
		bool operator==(const Pixel&) const = default;
	};
	// Compare observable values without keeping extra references alive across the draw.
	struct PipelineSnapshot
	{
		std::vector<uint8_t> bytes;
		template <class T>
		void Value(const T& value)
		{
			const auto* data = reinterpret_cast<const uint8_t*>(&value);
			bytes.insert(bytes.end(), data, data + sizeof(value));
		}
		template <class T>
		void Object(T* value)
		{
			Value(value);
			if (value)
				value->Release();
		}
		explicit PipelineSnapshot(ID3D11DeviceContext* context)
		{
			ID3D11RenderTargetView* targets[8]{};
			ID3D11DepthStencilView* dsv = nullptr;
			context->OMGetRenderTargets(8, targets, &dsv);
			for (auto* target : targets)
				Object(target);
			Object(dsv);
			ID3D11BlendState* blend = nullptr;
			FLOAT factors[4]{};
			UINT sampleMask = 0;
			context->OMGetBlendState(&blend, factors, &sampleMask);
			Object(blend);
			Value(factors);
			Value(sampleMask);
			ID3D11DepthStencilState* depth = nullptr;
			UINT stencilRef = 0;
			context->OMGetDepthStencilState(&depth, &stencilRef);
			Object(depth);
			Value(stencilRef);
			ID3D11RasterizerState* raster = nullptr;
			context->RSGetState(&raster);
			Object(raster);
			UINT viewportCount = 16, scissorCount = 16;
			D3D11_VIEWPORT viewports[16]{};
			D3D11_RECT scissors[16]{};
			context->RSGetViewports(&viewportCount, viewports);
			context->RSGetScissorRects(&scissorCount, scissors);
			Value(viewportCount);
			Value(viewports);
			Value(scissorCount);
			Value(scissors);
			ID3D11VertexShader* vs = nullptr;
			ID3D11PixelShader* ps = nullptr;
			context->VSGetShader(&vs, nullptr, nullptr);
			ID3D11ClassInstance* classes[D3D11_SHADER_MAX_INTERFACES]{};
			UINT classCount = D3D11_SHADER_MAX_INTERFACES;
			context->PSGetShader(&ps, classes, &classCount);
			Object(vs);
			Object(ps);
			Value(classCount);
			for (auto* instance : classes)
				Object(instance);
			ID3D11InputLayout* layout = nullptr;
			context->IAGetInputLayout(&layout);
			Object(layout);
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			context->IAGetPrimitiveTopology(&topology);
			Value(topology);
			ID3D11Buffer* vertex[2]{};
			ID3D11Buffer* index = nullptr;
			UINT stride[2]{}, vertexOffset[2]{}, indexOffset = 0;
			DXGI_FORMAT format{};
			context->IAGetVertexBuffers(0, 2, vertex, stride, vertexOffset);
			context->IAGetIndexBuffer(&index, &format, &indexOffset);
			for (auto* buffer : vertex)
				Object(buffer);
			Object(index);
			Value(stride);
			Value(vertexOffset);
			Value(format);
			Value(indexOffset);
			using Getter = void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT, UINT, ID3D11ShaderResourceView**);
			for (auto getter : std::array<Getter, 6>{ &ID3D11DeviceContext::VSGetShaderResources, &ID3D11DeviceContext::HSGetShaderResources,
					 &ID3D11DeviceContext::DSGetShaderResources, &ID3D11DeviceContext::GSGetShaderResources,
					 &ID3D11DeviceContext::PSGetShaderResources, &ID3D11DeviceContext::CSGetShaderResources }) {
				ID3D11ShaderResourceView* views[128]{};
				(context->*getter)(0, 128, views);
				for (auto* view : views)
					Object(view);
			}
			ID3D11Buffer* buffers[14]{};
			context->VSGetConstantBuffers(0, 14, buffers);
			for (auto* buffer : buffers)
				Object(buffer);
			context->PSGetConstantBuffers(0, 14, buffers);
			for (auto* buffer : buffers)
				Object(buffer);
			ID3D11SamplerState* samplers[16]{};
			context->PSGetSamplers(0, 16, samplers);
			for (auto* sampler : samplers)
				Object(sampler);
			ID3D11UnorderedAccessView* unordered[8]{};
			context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 8, unordered);
			for (auto* view : unordered)
				Object(view);
			context->CSGetUnorderedAccessViews(0, 8, unordered);
			for (auto* view : unordered)
				Object(view);
			ID3D11Buffer* streams[4]{};
			context->SOGetTargets(4, streams);
			for (auto* buffer : streams)
				Object(buffer);
			ID3D11Predicate* predicate = nullptr;
			BOOL predicateValue = FALSE;
			context->GetPredication(&predicate, &predicateValue);
			Object(predicate);
			Value(predicateValue);
		}
	};
	struct Fixture
	{
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::com_ptr<ID3D11InfoQueue> messages;
		winrt::com_ptr<ID3D11VertexShader> nativeVS, compositeVS;
		winrt::com_ptr<ID3D11PixelShader> nativePS, capturePS, compositePS;
		winrt::com_ptr<ID3D11InputLayout> layout;
		winrt::com_ptr<ID3D11Buffer> vertices, instances, indices, nudgeCB, compositeCB;
		winrt::com_ptr<ID3D11Texture2D> depthTexture;
		winrt::com_ptr<ID3D11DepthStencilView> depthView;
		winrt::com_ptr<ID3D11DepthStencilState> nativeDepth, disabledDepth;
		winrt::com_ptr<ID3D11BlendState> nativeBlend, compositeBlend;
		winrt::com_ptr<ID3D11RasterizerState> raster;
		winrt::com_ptr<ID3D11SamplerState> sampler;
		std::array<Texture, 8> nativeTargets;
		Texture unrelated, finalEye;
		VRMenuPointerOverlay overlay;
		uint32_t frame = 1, generation = 7;
		VRMenuPointerOverlay::DrawArguments draw{ 12, 2, 3, 3, 0, true };
		const char* reason = nullptr;
		static constexpr Pixel left{ 191, 64, 128, 255 }, right{ 223, 64, 128, 255 }, clear{};
		static constexpr FLOAT nativeClear[4]{ 0.02f, 0.04f, 0.06f, 0.08f };
		Texture MakeTexture(UINT width, UINT height)
		{
			Texture result;
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, result.texture.put()));
			winrt::check_hresult(device->CreateRenderTargetView(result.texture.get(), nullptr, result.rtv.put()));
			winrt::check_hresult(device->CreateShaderResourceView(result.texture.get(), nullptr, result.srv.put()));
			Util::SetResourceName(result.texture.get(), "VRMenuPointerTest::Texture");
			Util::SetResourceName(result.rtv.get(), "VRMenuPointerTest::RTV");
			Util::SetResourceName(result.srv.get(), "VRMenuPointerTest::SRV");
			return result;
		}
		winrt::com_ptr<ID3D11Buffer> Buffer(UINT flags, UINT size, const void* contents = nullptr)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = size;
			desc.BindFlags = flags;
			D3D11_SUBRESOURCE_DATA initial{ contents, 0, 0 };
			winrt::com_ptr<ID3D11Buffer> result;
			winrt::check_hresult(device->CreateBuffer(&desc, contents ? &initial : nullptr, result.put()));
			Util::SetResourceName(result.get(), "VRMenuPointerTest::Buffer");
			return result;
		}
		Fixture()
		{
			const D3D_FEATURE_LEVEL feature = D3D_FEATURE_LEVEL_11_0;
			auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG,
				&feature, 1, D3D11_SDK_VERSION, device.put(), nullptr, context.put());
			if (result == DXGI_ERROR_SDK_COMPONENT_MISSING) {
				std::cout << "D3D11 debug layer unavailable; WARP pixel and reflection checks remain enabled\n";
				result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &feature, 1,
					D3D11_SDK_VERSION, device.put(), nullptr, context.put());
			}
			winrt::check_hresult(result);
			messages = device.try_as<ID3D11InfoQueue>();
			for (auto& target : nativeTargets)
				target = MakeTexture(32, 32);
			unrelated = MakeTexture(32, 32);
			finalEye = MakeTexture(32, 64);
			D3D11_TEXTURE2D_DESC depthDesc{};
			depthDesc.Width = depthDesc.Height = 32;
			depthDesc.MipLevels = depthDesc.ArraySize = depthDesc.SampleDesc.Count = 1;
			depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			winrt::check_hresult(device->CreateTexture2D(&depthDesc, nullptr, depthTexture.put()));
			winrt::check_hresult(device->CreateDepthStencilView(depthTexture.get(), nullptr, depthView.put()));
			Util::SetResourceName(depthTexture.get(), "VRMenuPointerTest::DepthTexture");
			Util::SetResourceName(depthView.get(), "VRMenuPointerTest::DepthView");
			D3D11_DEPTH_STENCIL_DESC ds{};
			ds.DepthEnable = TRUE;
			ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			winrt::check_hresult(device->CreateDepthStencilState(&ds, nativeDepth.put()));
			ds.DepthEnable = FALSE;
			winrt::check_hresult(device->CreateDepthStencilState(&ds, disabledDepth.put()));
			Util::SetResourceName(nativeDepth.get(), "VRMenuPointerTest::NativeDepth");
			Util::SetResourceName(disabledDepth.get(), "VRMenuPointerTest::DisabledDepth");
			D3D11_BLEND_DESC blendDesc{};
			blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
			winrt::check_hresult(device->CreateBlendState(&blendDesc, nativeBlend.put()));
			blendDesc.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD,
				D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
			winrt::check_hresult(device->CreateBlendState(&blendDesc, compositeBlend.put()));
			Util::SetResourceName(nativeBlend.get(), "VRMenuPointerTest::NativeBlend");
			Util::SetResourceName(compositeBlend.get(), "VRMenuPointerTest::CompositeBlend");
			D3D11_RASTERIZER_DESC rasterDesc{};
			rasterDesc.FillMode = D3D11_FILL_SOLID;
			rasterDesc.CullMode = D3D11_CULL_NONE;
			rasterDesc.DepthClipEnable = rasterDesc.ScissorEnable = TRUE;
			winrt::check_hresult(device->CreateRasterizerState(&rasterDesc, raster.put()));
			Util::SetResourceName(raster.get(), "VRMenuPointerTest::Rasterizer");
			auto vertexCode = Compile(R"(
cbuffer Nudge : register(b0) { float4 nudge; };
struct In { float3 position:POSITION0; float4 colour:COLOR0; float4 eye:INSTANCE0; };
struct Out { float4 position:SV_POSITION0; float4 uv:TEXCOORD0; float4 world:POSITION1; float4 colour:COLOR0; };
Out main(In input) {
    Out o;
    o.position=float4(input.position.xy+nudge.xy+float2(input.eye.x,0),input.position.z,1);
    o.uv=float4(input.position.xy,0,1);
    o.world=float4(-1,-1,-1,0);
    o.colour=input.colour+float4(input.eye.y,0,0,0);
    return o;
})",
				"vs_5_0");
			winrt::check_hresult(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, nativeVS.put()));
			Util::SetResourceName(nativeVS.get(), "VRMenuPointerTest::NativeVS");
			D3D11_INPUT_ELEMENT_DESC elements[]{ { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 } };
			winrt::check_hresult(device->CreateInputLayout(elements, 3, vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), layout.put()));
			Util::SetResourceName(layout.get(), "VRMenuPointerTest::InputLayout");
			auto captureCode = CompileProduction("VRMenuPointerOverlayPS.hlsl", "ps_5_0");
			Require(ColourRegister(vertexCode.get(), false) == 3 && ColourRegister(captureCode.get(), true) == 3,
				"overlay PS COLOR0 register does not match the Effect VS output signature");
			winrt::check_hresult(device->CreatePixelShader(captureCode->GetBufferPointer(), captureCode->GetBufferSize(), nullptr, capturePS.put()));
			Util::SetResourceName(capturePS.get(), "VRMenuPointerTest::OverlayPS");
			auto code = Compile(R"(
struct In { float4 position:SV_POSITION0; float4 uv:TEXCOORD0; float4 world:POSITION1; float4 colour:COLOR0; };
struct Out { float4 targets[8]:SV_Target0; };
Out main(In input) { Out o; for(uint i=0;i<8;i++)o.targets[i]=input.colour; return o; }
)",
				"ps_5_0");
			winrt::check_hresult(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, nativePS.put()));
			Util::SetResourceName(nativePS.get(), "VRMenuPointerTest::NativePS");
			const D3D_SHADER_MACRO overlayDefines[]{ { "POINTER_OVERLAY", "1" }, { nullptr, nullptr } };
			code = CompileProduction("VRMenuLayerCompositePS.hlsl", "ps_5_0", overlayDefines);
			winrt::check_hresult(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, compositePS.put()));
			Util::SetResourceName(compositePS.get(), "VRMenuPointerTest::CompositePS");
			const D3D_SHADER_MACRO vertexDefines[]{ { "VSHADER", "1" }, { nullptr, nullptr } };
			code = CompileProduction("UpscaleVS.hlsl", "vs_5_0", vertexDefines);
			winrt::check_hresult(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, compositeVS.put()));
			Util::SetResourceName(compositeVS.get(), "VRMenuPointerTest::CompositeVS");
			struct Vertex
			{
				float x, y, z, r, g, b, a;
			};
			const Vertex data[]{ {}, {}, {}, { -0.25f, -0.5f, 0.7f, 0.75f, 0.25f, 0.5f, 0 },
				{ -0.25f, 0.5f, 0.7f, 0.75f, 0.25f, 0.5f, 0 }, { 0.25f, 0.5f, 0.7f, 0.75f, 0.25f, 0.5f, 0 },
				{ 0.25f, -0.5f, 0.7f, 0.75f, 0.25f, 0.5f, 0 } };
			const UINT indexData[]{ 0, 0, 0, 0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3 };
			vertices = Buffer(D3D11_BIND_VERTEX_BUFFER, sizeof(data), data);
			const float instanceData[]{ -0.5f, 0, 0, 0, 0.5f, 0.125f, 0, 0 };
			instances = Buffer(D3D11_BIND_VERTEX_BUFFER, sizeof(instanceData), instanceData);
			indices = Buffer(D3D11_BIND_INDEX_BUFFER, sizeof(indexData), indexData);
			nudgeCB = Buffer(D3D11_BIND_CONSTANT_BUFFER, 16);
			compositeCB = Buffer(D3D11_BIND_CONSTANT_BUFFER, 16);
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			winrt::check_hresult(device->CreateSamplerState(&samplerDesc, sampler.put()));
			Util::SetResourceName(sampler.get(), "VRMenuPointerTest::Sampler");
		}
		void Bind()
		{
			context->ClearState();
			std::array<ID3D11RenderTargetView*, 8> targets{};
			for (size_t i = 0; i < targets.size(); ++i) {
				targets[i] = nativeTargets[i].rtv.get();
				context->ClearRenderTargetView(targets[i], nativeClear);
			}
			context->ClearDepthStencilView(depthView.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.2f, 37);
			context->OMSetRenderTargets(8, targets.data(), depthView.get());
			context->OMSetDepthStencilState(nativeDepth.get(), 23);
			const FLOAT factors[]{ 0.1f, 0.2f, 0.3f, 0.4f };
			context->OMSetBlendState(nativeBlend.get(), factors, 0xA5A5A5A5);
			context->RSSetState(raster.get());
			const D3D11_VIEWPORT viewport{ 0, 0, 32, 32, 0.1f, 0.9f };
			context->RSSetViewports(1, &viewport);
			const D3D11_RECT scissor{ 2, 2, 30, 30 };
			context->RSSetScissorRects(1, &scissor);
			context->VSSetShader(nativeVS.get(), nullptr, 0);
			context->PSSetShader(nativePS.get(), nullptr, 0);
			context->IASetInputLayout(layout.get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->IASetIndexBuffer(indices.get(), DXGI_FORMAT_R32_UINT, 0);
			ID3D11Buffer* vertex[]{ vertices.get(), instances.get() };
			const UINT stride[]{ 28, 16 }, offsets[]{ 0, 0 };
			context->IASetVertexBuffers(0, 2, vertex, stride, offsets);
			auto* cb = nudgeCB.get();
			context->VSSetConstantBuffers(0, 1, &cb);
			context->PSSetConstantBuffers(13, 1, &cb);
			const float zero[4]{};
			context->UpdateSubresource(nudgeCB.get(), 0, nullptr, zero, 0, 0);
			auto* view = unrelated.srv.get();
			context->VSSetShaderResources(127, 1, &view);
			context->PSSetShaderResources(127, 1, &view);
			auto* nativeSampler = sampler.get();
			context->PSSetSamplers(15, 1, &nativeSampler);
			if (messages)
				messages->ClearStoredMessages();
		}
		bool Capture(float x = 0, float y = 0, uint32_t displayWidth = 64)
		{
			const PipelineSnapshot before(context.get());
			const bool result = overlay.Capture(context.get(), capturePS.get(), draw, 32, 32, displayWidth, 64,
				frame, generation, x, y, &reason);
			Require(before.bytes == PipelineSnapshot(context.get()).bytes, "capture changed native pipeline state");
			return result;
		}
		std::vector<Pixel> Read(ID3D11Texture2D* texture)
		{
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			desc.BindFlags = desc.MiscFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			winrt::com_ptr<ID3D11Texture2D> staging;
			winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, staging.put()));
			Util::SetResourceName(staging.get(), "VRMenuPointerTest::Readback");
			context->CopyResource(staging.get(), texture);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			winrt::check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<Pixel> result(static_cast<size_t>(desc.Width) * desc.Height);
			for (UINT y = 0; y < desc.Height; ++y)
				std::memcpy(result.data() + static_cast<size_t>(y) * desc.Width,
					static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, desc.Width * sizeof(Pixel));
			context->Unmap(staging.get(), 0);
			return result;
		}
		std::vector<Pixel> ReadLayer()
		{
			auto* view = overlay.GetLayer(frame, generation);
			Require(view != nullptr, "fresh overlay is missing");
			winrt::com_ptr<ID3D11Resource> resource;
			view->GetResource(resource.put());
			return Read(resource.as<ID3D11Texture2D>().get());
		}
		void CheckDebug()
		{
			if (!messages)
				return;
			for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
				SIZE_T length = 0;
				winrt::check_hresult(messages->GetMessage(i, nullptr, &length));
				std::vector<uint8_t> storage(length);
				auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
				winrt::check_hresult(messages->GetMessage(i, message, &length));
				if (message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR)
					throw std::runtime_error(std::string("D3D11 debug error: ") + message->pDescription);
			}
			messages->ClearStoredMessages();
		}
		void TestPixelsAndState()
		{
			Bind();
			const auto nativeBefore = Read(nativeTargets[0].texture.get());
			const auto depthBefore = Read(depthTexture.get());
			context->DrawIndexedInstanced(draw.indexCount, draw.instanceCount, draw.startIndex, draw.baseVertex, draw.startInstance);
			Require(Read(nativeTargets[0].texture.get()) == nativeBefore, "native fixture pointer was not depth-occluded");
			Require(Capture(), reason);
			const auto pixels = ReadLayer();
			Require(pixels[32 * 64 + 16] == left && pixels[32 * 64 + 48] == right, "opaque stereo pointer colours were not captured");
			Require(pixels[32 * 64 + 2] == clear && pixels[4 * 64 + 16] == clear, "overlay background was not transparent");
			for (const auto& target : nativeTargets)
				Require(Read(target.texture.get()) == nativeBefore, "overlay capture changed a native MRT");
			Require(Read(depthTexture.get()) == depthBefore, "overlay capture changed native depth or stencil");
			context->ClearDepthStencilView(depthView.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
			context->DrawIndexedInstanced(draw.indexCount, draw.instanceCount, draw.startIndex, draw.baseVertex, draw.startInstance);
			for (const auto& target : nativeTargets)
				Require(Read(target.texture.get())[16 * 32 + 8] == Pixel{ left.r, left.g, left.b, 20 }, "native draw did not retain its RGB-only MRT outputs");
			CheckDebug();
			TestComposition();
		}
		void TestComposition()
		{
			auto* source = overlay.GetLayer(frame, generation);
			context->ClearState();
			auto* target = finalEye.rtv.get();
			context->OMSetRenderTargets(1, &target, nullptr);
			context->OMSetDepthStencilState(disabledDepth.get(), 0);
			context->OMSetBlendState(compositeBlend.get(), nullptr, UINT_MAX);
			context->VSSetShader(compositeVS.get(), nullptr, 0);
			context->PSSetShader(compositePS.get(), nullptr, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->PSSetShaderResources(1, 1, &source);
			auto* sample = sampler.get();
			context->PSSetSamplers(0, 1, &sample);
			auto* cb = compositeCB.get();
			context->PSSetConstantBuffers(0, 1, &cb);
			const D3D11_VIEWPORT viewport{ 0, 0, 32, 64, 0, 1 };
			context->RSSetViewports(1, &viewport);
			constexpr FLOAT menu[4]{ 0.1f, 0.2f, 0.8f, 1.0f };
			auto menuLayer = MakeTexture(64, 64);
			context->ClearRenderTargetView(menuLayer.rtv.get(), menu);
			auto* menuSource = menuLayer.srv.get();
			context->PSSetShaderResources(0, 1, &menuSource);
			for (UINT eye = 0; eye < 2; ++eye) {
				const float mapping[4]{ 0.5f, 1.0f, eye * 0.5f, 0.0f };
				context->UpdateSubresource(compositeCB.get(), 0, nullptr, mapping, 0, 0);
				context->ClearRenderTargetView(target, nativeClear);
				context->Draw(3, 0);
				const auto pixels = Read(finalEye.texture.get());
				Require(pixels[32 * 32 + 16] == (eye ? right : left), "pointer did not cover the opaque menu in its correct eye");
				Require(pixels[32 * 32 + 2] == Pixel{ 26, 51, 204, 255 }, "transparent pointer pixels changed the menu");
			}
			constexpr FLOAT translucentMenu[4]{ 0.2f, 0.1f, 0.0f, 0.5f };
			context->ClearRenderTargetView(menuLayer.rtv.get(), translucentMenu);
			for (UINT eye = 0; eye < 2; ++eye) {
				const float mapping[4]{ 0.5f, 1.0f, eye * 0.5f, 0.0f };
				context->UpdateSubresource(compositeCB.get(), 0, nullptr, mapping, 0, 0);
				context->ClearRenderTargetView(target, menu);
				context->Draw(3, 0);
				const auto pixels = Read(finalEye.texture.get());
				Require(pixels[32 * 32 + 16] == (eye ? right : left), "fused pointer lost coverage over translucent menu");
				const auto background = pixels[32 * 32 + 2];
				Require(std::abs(int(background.r) - 64) <= 1 && std::abs(int(background.g) - 51) <= 1 &&
							std::abs(int(background.b) - 102) <= 1 && background.a == 255,
					"fused menu changed premultiplied alpha blending");
			}
			auto seamSource = MakeTexture(64, 64);
			auto seamTarget = MakeTexture(128, 64);
			std::vector<Pixel> stereo(64 * 64);
			for (size_t i = 0; i < stereo.size(); ++i)
				stereo[i] = i % 64 < 32 ? left : right;
			context->UpdateSubresource(seamSource.texture.get(), 0, nullptr, stereo.data(), 64 * sizeof(Pixel), 0);
			source = seamSource.srv.get();
			target = seamTarget.rtv.get();
			context->PSSetShaderResources(1, 1, &source);
			context->OMSetRenderTargets(1, &target, nullptr);
			const D3D11_VIEWPORT wideViewport{ 0, 0, 128, 64, 0, 1 };
			context->RSSetViewports(1, &wideViewport);
			for (UINT eye = 0; eye < 2; ++eye) {
				const float mapping[4]{ 0.5f, 1.0f, eye * 0.5f, 0.0f };
				context->UpdateSubresource(compositeCB.get(), 0, nullptr, mapping, 0, 0);
				context->ClearRenderTargetView(target, menu);
				context->Draw(3, 0);
				const auto pixels = Read(seamTarget.texture.get());
				Require(pixels[32 * 128] == (eye ? right : left) && pixels[32 * 128 + 127] == (eye ? right : left),
					"pointer compositor sampled across the stereo seam");
			}
			context->ClearState();
			auto* original = seamSource.srv.get();
			context->PSSetShaderResources(1, 1, &original);
			auto checkBinding = [&](ID3D11ShaderResourceView* expected) {
				winrt::com_ptr<ID3D11ShaderResourceView> actual;
				context->PSGetShaderResources(1, 1, actual.put());
				Require(actual.get() == expected, "scoped pointer slot was not restored");
			};
			try {
				VRMenuPointerOverlay::CompositeBinding binding(context.get(), overlay.GetLayer(frame, generation));
				checkBinding(overlay.GetLayer(frame, generation));
				throw std::runtime_error("test unwind");
			} catch (const std::runtime_error& error) {
				Require(std::strcmp(error.what(), "test unwind") == 0, error.what());
				checkBinding(original);
			}
			{
				VRMenuPointerOverlay::CompositeBinding binding(context.get(), nullptr);
				checkBinding(original);
			}
			context->ClearState();
			CheckDebug();
		}
		void TestScalingAndDrawArguments()
		{
			Bind();
			++frame;
			Require(Capture(2, -2), reason);
			auto pixels = ReadLayer();
			Require(pixels[32 * 64 + 9] == clear && pixels[32 * 64 + 10] == left && pixels[32 * 64 + 25] == left && pixels[32 * 64 + 26] == clear,
				"positive viewport offset was not applied at display resolution");
			Require(pixels[13 * 64 + 16] == clear && pixels[14 * 64 + 16] == left && pixels[45 * 64 + 16] == left && pixels[46 * 64 + 16] == clear,
				"negative viewport offset was not applied at display resolution");
			const D3D11_RECT clipped{ 6, 10, 30, 30 };
			context->RSSetScissorRects(1, &clipped);
			++frame;
			Require(Capture(2, -2), reason);
			pixels = ReadLayer();
			Require(pixels[32 * 64 + 13] == clear && pixels[32 * 64 + 14] == left && pixels[17 * 64 + 16] == clear && pixels[18 * 64 + 16] == left,
				"native scissor was not transformed with the capture viewport");
			Bind();
			++frame;
			draw.instanced = false;
			draw.instanceCount = 1;
			Require(Capture(), reason);
			pixels = ReadLayer();
			Require(pixels[32 * 64 + 16] == left && pixels[32 * 64 + 48] == clear, "DrawIndexed arguments were not retained");
			++frame;
			draw.instanced = true;
			draw.startInstance = 1;
			Require(Capture(), reason);
			pixels = ReadLayer();
			Require(pixels[32 * 64 + 16] == clear && pixels[32 * 64 + 48] == right, "DrawIndexedInstanced start instance was lost");
			draw = { 12, 2, 3, 3, 0, true };
			CheckDebug();
		}
		void TestFreshness()
		{
			Bind();
			++frame;
			Require(Capture(-4), reason);
			Require(Capture(4), reason);
			auto pixels = ReadLayer();
			Require(pixels[32 * 64 + 5] == left && pixels[32 * 64 + 27] == left, "same-frame pointer captures did not accumulate");
			Require(!overlay.GetLayer(frame + 1, generation) && !overlay.GetLayer(frame, generation + 1), "stale frame or generation was exposed");
			++frame;
			Require(Capture(), reason);
			pixels = ReadLayer();
			Require(pixels[32 * 64 + 5] == clear && pixels[32 * 64 + 27] == clear, "new-frame capture retained old pointer pixels");
			Require(!Capture(0, 0, 128) && std::strcmp(reason, "ui-pointer-overlay-frame-layout-changed") == 0, "same-frame resource layout change was accepted");
			Require(!overlay.GetLayer(frame, generation), "failed capture left partial frame content visible");
			Require(!Capture() && std::strcmp(reason, "ui-pointer-overlay-frame-invalid") == 0, "failed frame could be revived by a later draw");
			++generation;
			Require(Capture(), reason);
			Require(!overlay.GetLayer(frame, generation - 1), "reallocated generation remained visible");
			overlay.Invalidate(frame, generation);
			Require(!overlay.GetLayer(frame, generation) && !Capture(), "external invalidation did not poison the frame");
			overlay.Reset();
			Require(!overlay.GetLayer(frame, generation), "reset retained a fresh layer");
			Require(Capture(), reason);
			CheckDebug();
		}
		void Rejected(const char* expected)
		{
			++frame;
			Require(!Capture(), "unsafe pipeline was captured");
			Require(reason && std::strcmp(reason, expected) == 0, "capture rejection reason changed");
			Require(!overlay.GetLayer(frame, generation), "rejected capture exposed a layer");
		}
		void TestRejectedPipelines()
		{
			Bind();
			context->VSSetShader(nullptr, nullptr, 0);
			Rejected("ui-pointer-overlay-unsupported-pipeline");
			Bind();
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
			Rejected("ui-pointer-overlay-unsupported-pipeline");
			Bind();
			const D3D11_VIEWPORT viewports[]{ { 0, 0, 16, 32, 0, 1 }, { 16, 0, 16, 32, 0, 1 } };
			context->RSSetViewports(2, viewports);
			Rejected("ui-pointer-overlay-unsupported-viewports");
			Bind();
			const D3D11_QUERY_DESC predicateDesc{ D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
			winrt::com_ptr<ID3D11Predicate> predicate;
			winrt::check_hresult(device->CreatePredicate(&predicateDesc, predicate.put()));
			Util::SetResourceName(predicate.get(), "VRMenuPointerTest::Predicate");
			context->Begin(predicate.get());
			context->End(predicate.get());
			context->SetPredication(predicate.get(), FALSE);
			Rejected("ui-pointer-overlay-draw-side-effects");
			Bind();
			auto stream = Buffer(D3D11_BIND_STREAM_OUTPUT, 256);
			auto* output = stream.get();
			UINT offset = 16;
			context->SOSetTargets(1, &output, &offset);
			Rejected("ui-pointer-overlay-draw-side-effects");
			Bind();
			auto storage = Buffer(D3D11_BIND_UNORDERED_ACCESS, 64);
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = DXGI_FORMAT_R32_UINT;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.NumElements = 16;
			winrt::com_ptr<ID3D11UnorderedAccessView> uav;
			winrt::check_hresult(device->CreateUnorderedAccessView(storage.get(), &uavDesc, uav.put()));
			Util::SetResourceName(uav.get(), "VRMenuPointerTest::UAV");
			auto* unordered = uav.get();
			auto* target = nativeTargets[0].rtv.get();
			context->OMSetRenderTargetsAndUnorderedAccessViews(1, &target, depthView.get(), 7, 1, &unordered, nullptr);
			Rejected("ui-pointer-overlay-draw-side-effects");
			Bind();
			context->CSSetUnorderedAccessViews(7, 1, &unordered, nullptr);
			++frame;
			Require(Capture(), "unrelated compute UAV incorrectly prevented graphics-only capture");
			Bind();
			++frame;
			Require(!Capture(std::numeric_limits<float>::quiet_NaN()) && std::strcmp(reason, "ui-pointer-overlay-invalid-dimensions") == 0,
				"nonfinite capture offset was accepted");
			Bind();
			draw.indexCount = 65537;
			Rejected("ui-pointer-overlay-invalid-draw");
			draw = { 12, 2, 3, 3, 0, true };
			Bind();
			++frame;
			Require(Capture(), reason);
			CheckDebug();
		}
	};
}

int main()
try {
	Fixture fixture;
	fixture.TestPixelsAndState();
	fixture.TestScalingAndDrawArguments();
	fixture.TestFreshness();
	fixture.TestRejectedPipelines();
	std::cout << "WARP overlay: actual shader linkage, 8 MRT preservation, native depth independence, opaque stereo pixels, menu composition, offsets, draw arguments, freshness and unsafe-pipeline rejection passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
