#include "Utils/SkyShaderBindings.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <d3dcompiler.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using Microsoft::WRL::ComPtr;
	constexpr UINT width = 162, height = 81;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	ComPtr<ID3DBlob> Compile(bool vr, bool reverse, bool vertex = false)
	{
		const char* source = R"(
#include "Common/SunGlare.hlsli"
cbuffer TestInput : register(b0) { float4 SunPosition; uint Eye; };
RWStructuredBuffer<float> Result : register(u0);
[numthreads(1, 1, 1)]
void main() { Result[0] = SunGlare::GetVisibility(SunPosition, Eye); }
)";
		const char* vertexSource = R"(
#define main SkyMain
#include "Sky.hlsl"
#undef main
float4 main(uint instance : SV_InstanceID) : COLOR0 {
    VS_INPUT input = (VS_INPUT)0;
    input.Color = 1;
#if defined(VR)
    input.InstanceID = instance;
#endif
    return SkyMain(input).Color;
}
)";
		if (vertex)
			source = vertexSource;
		std::vector<D3D_SHADER_MACRO> defines{ { vertex ? "VSHADER" : "CSHADER", "1" }, { "TEX", "1" }, { "DITHER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (reverse)
			defines.push_back({ "REVERSE_Z", "1" });
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompile(source, std::strlen(source), "sun-glare-test",
			defines.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", vertex ? "vs_5_0" : "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Texture2D> depth;
		ComPtr<ID3D11ShaderResourceView> depthView;
		ComPtr<ID3D11Buffer> result, staging;
		ComPtr<ID3D11UnorderedAccessView> resultView;

		Fixture()
		{
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
				nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
			D3D11_TEXTURE2D_DESC texture{};
			texture.Width = width;
			texture.Height = height;
			texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
			texture.Format = DXGI_FORMAT_R32_TYPELESS;
			texture.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
			Check(device->CreateTexture2D(&texture, nullptr, &depth));
			Util::SetResourceName(depth.Get(), "SunGlareTest::Depth");
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_R32_FLOAT;
			view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipLevels = 1;
			Check(device->CreateShaderResourceView(depth.Get(), &view, &depthView));
			Util::SetResourceName(depthView.Get(), "SunGlareTest::Depth SRV");
			D3D11_BUFFER_DESC buffer{};
			buffer.ByteWidth = buffer.StructureByteStride = sizeof(float);
			buffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			buffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			Check(device->CreateBuffer(&buffer, nullptr, &result));
			Util::SetResourceName(result.Get(), "SunGlareTest::Result");
			Check(device->CreateUnorderedAccessView(result.Get(), nullptr, &resultView));
			Util::SetResourceName(resultView.Get(), "SunGlareTest::Result UAV");
			buffer.Usage = D3D11_USAGE_STAGING;
			buffer.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			buffer.BindFlags = buffer.MiscFlags = 0;
			Check(device->CreateBuffer(&buffer, nullptr, &staging));
			Util::SetResourceName(staging.Get(), "SunGlareTest::Readback");
		}

		float Dispatch(ID3D11ShaderResourceView* view)
		{
			context->CSSetShaderResources(17, 1, &view);
			auto* uav = resultView.Get();
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->Dispatch(1, 1, 1);
			context->CopyResource(staging.Get(), result.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			const float value = *static_cast<float*>(mapped.pData);
			context->Unmap(staging.Get(), 0);
			return value;
		}

		void CheckSkyVertex(bool vr, bool reverse, bool stereo)
		{
			context->ClearState();
			auto code = Compile(vr, reverse, true);
			ComPtr<ID3D11VertexShader> shader;
			Check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "SunGlareTest::SkyVertex");
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			ConstantBuffer frame(device.Get(), reflection.Get(), "FrameBuffer::PerFrame");
			frame.SetVariable("FrameBuffer::DynamicResolutionParams1", std::array<float, 4>{ 1, 1, 1, 1 });
			frame.SetVariable("FrameBuffer::DynamicResolutionParams2", std::array<float, 4>{ 1, 1, 1, 1 });
			frame.Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
			ConstantBuffer geometry(device.Get(), reflection.Get(), "PerGeometry");
			std::array<std::array<float, 16>, 2> matrices{};
			for (UINT eye = 0; eye < 2; ++eye) {
				matrices[eye][0] = matrices[eye][5] = matrices[eye][10] = matrices[eye][15] = 1;
				matrices[eye][3] = eye == 0 ? -0.5f : 0.5f;
			}
			if (vr)
				geometry.SetVariable("WorldViewProj", matrices);
			else
				geometry.SetVariable("WorldViewProj", matrices[0]);
			geometry.SetVariable("BlendColor", std::array<float, 12>{ 1, 1, 1, 0.75f });
			geometry.Bind(context.Get());
			auto* constants = geometry.buffer.Get();
			context->VSSetConstantBuffers(2, 1, &constants);
			std::optional<ConstantBuffer> vrValues;
			if (vr) {
				vrValues.emplace(device.Get(), reflection.Get(), "VRValues");
				vrValues->SetVariable("StereoEnabled", stereo ? 1.0f : 0.0f);
				vrValues->Bind(context.Get());
				constants = vrValues->buffer.Get();
				context->VSSetConstantBuffers(13, 1, &constants);
			}
			const char* streamSource = R"(
struct Vertex { float4 color : COLOR0; };
[maxvertexcount(1)] void main(point Vertex input[1], inout PointStream<Vertex> output) { output.Append(input[0]); }
)";
			ComPtr<ID3DBlob> streamCode;
			Check(D3DCompile(streamSource, std::strlen(streamSource), nullptr, nullptr, nullptr, "main", "gs_5_0", 0, 0, &streamCode, nullptr));
			D3D11_SO_DECLARATION_ENTRY entry{ 0, "COLOR", 0, 0, 4, 0 };
			UINT stride = 4 * sizeof(float);
			ComPtr<ID3D11GeometryShader> streamShader;
			Check(device->CreateGeometryShaderWithStreamOutput(streamCode->GetBufferPointer(), streamCode->GetBufferSize(),
				&entry, 1, &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &streamShader));
			Util::SetResourceName(streamShader.Get(), "SunGlareTest::Stream");
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = 2 * stride;
			desc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
			ComPtr<ID3D11Buffer> output, readback;
			Check(device->CreateBuffer(&desc, nullptr, &output));
			Util::SetResourceName(output.Get(), "SunGlareTest::Vertices");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateBuffer(&desc, nullptr, &readback));
			Util::SetResourceName(readback.Get(), "SunGlareTest::VerticesReadback");
			std::array<float, width * height> data;
			for (UINT y = 0; y < height; ++y)
				for (UINT x = 0; x < width; ++x) {
					const UINT eyeWidth = stereo ? width / 2 : width;
					data[y * width + x] = x % eyeWidth < eyeWidth / 2 ? 0.5f : (reverse ? 0.0f : 1.0f);
				}
			context->UpdateSubresource(depth.Get(), 0, nullptr, data.data(), width * sizeof(float), 0);
			context->VSSetShader(shader.Get(), nullptr, 0);
			context->GSSetShader(streamShader.Get(), nullptr, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
			UINT offset = 0;
			auto* stream = output.Get();
			context->SOSetTargets(1, &stream, &offset);
			{
				SkyShader::ScopedBindings bindings(context.Get(), depthView.Get());
				context->DrawInstanced(1, stereo ? 2 : 1, 0, 0);
			}
			context->SOSetTargets(0, nullptr, nullptr);
			context->CopyResource(readback.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			const auto* values = static_cast<const float*>(mapped.pData);
			const bool correct = values[3] == 0 && (!stereo || values[7] == 0.75f);
			context->Unmap(readback.Get(), 0);
			Require(correct, "Production Sky must project each eye and fade its existing alpha output");
			context->ClearState();
		}

		void CheckBindings()
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = 1024;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			ComPtr<ID3D11Buffer> previous, frame;
			Check(device->CreateBuffer(&desc, nullptr, &previous));
			Check(device->CreateBuffer(&desc, nullptr, &frame));
			Util::SetResourceName(previous.Get(), "SunGlareTest::PreviousFrame");
			Util::SetResourceName(frame.Get(), "SunGlareTest::Frame");
			ComPtr<ID3D11DeviceContext1> extended;
			Check(context.As(&extended));
			auto* previousRaw = previous.Get();
			auto* frameRaw = frame.Get();
			UINT first = 16, count = 16;
			extended->VSSetConstantBuffers1(12, 1, &previousRaw, &first, &count);
			extended->PSSetConstantBuffers1(12, 1, &frameRaw, &first, &count);
			auto* oldView = depthView.Get();
			context->VSSetShaderResources(17, 1, &oldView);
			{
				SkyShader::ScopedBindings bindings(context.Get(), nullptr);
				ComPtr<ID3D11ShaderResourceView> bound;
				context->VSGetShaderResources(17, 1, &bound);
				Require(!bound, "Unavailable scene depth must be unbound");
				ComPtr<ID3D11Buffer> boundFrame;
				UINT boundFirst = 0, boundCount = 0;
				extended->VSGetConstantBuffers1(12, 1, &boundFrame, &boundFirst, &boundCount);
				Require(boundFrame.Get() == frame.Get() && boundFirst == first && boundCount == count,
					"VS must receive the active scene frame constants and range");
			}
			ComPtr<ID3D11ShaderResourceView> restored;
			context->VSGetShaderResources(17, 1, &restored);
			ComPtr<ID3D11Buffer> restoredFrame;
			UINT restoredFirst = 0, restoredCount = 0;
			extended->VSGetConstantBuffers1(12, 1, &restoredFrame, &restoredFirst, &restoredCount);
			Require(restored.Get() == oldView && restoredFrame.Get() == previous.Get() &&
						restoredFirst == first && restoredCount == count,
				"Glare must restore the SRV and exact CB range");
			extended->VSSetConstantBuffers1(4, 1, &previousRaw, &first, &count);
			{
				SkyShader::ScopedBindings bindings(context.Get(), nullptr, frameRaw, false);
				ComPtr<ID3D11ShaderResourceView> boundDepth;
				context->VSGetShaderResources(17, 1, &boundDepth);
				Require(boundDepth.Get() == oldView, "Sky ordering must leave sun-glare depth bindings untouched");
				ComPtr<ID3D11Buffer> boundPermutation;
				UINT boundFirst = 0, boundCount = 0;
				extended->VSGetConstantBuffers1(4, 1, &boundPermutation, &boundFirst, &boundCount);
				Require(boundPermutation.Get() == frameRaw && boundFirst == 0 && boundCount >= desc.ByteWidth / 16,
					"Sky vertices must receive the whole permutation buffer");
				ComPtr<ID3D11Buffer> boundFrame;
				extended->VSGetConstantBuffers1(12, 1, &boundFrame, &boundFirst, &boundCount);
				Require(boundFrame.Get() == frameRaw && boundFirst == first && boundCount == count,
					"Sky ordering must use the active projection constants and range");
			}
			ComPtr<ID3D11Buffer> restoredPermutation;
			extended->VSGetConstantBuffers1(4, 1, &restoredPermutation, &restoredFirst, &restoredCount);
			Require(restoredPermutation.Get() == previousRaw && restoredFirst == first && restoredCount == count,
				"Sky ordering must restore the previous permutation slot and exact CB range");
			restoredFrame.Reset();
			extended->VSGetConstantBuffers1(12, 1, &restoredFrame, &restoredFirst, &restoredCount);
			Require(restoredFrame.Get() == previousRaw && restoredFirst == first && restoredCount == count,
				"Sky ordering must restore the previous projection constants and range");
			context->ClearState();
			{
				SkyShader::ScopedBindings bindings(context.Get(), nullptr, frameRaw, false);
			}
			restoredPermutation.Reset();
			context->VSGetConstantBuffers(4, 1, &restoredPermutation);
			Require(!restoredPermutation, "Sky ordering must restore an originally empty permutation slot");

			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
			dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
			dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			ComPtr<ID3D11DepthStencilView> dsv;
			Check(device->CreateDepthStencilView(depth.Get(), &dsvDesc, &dsv));
			Util::SetResourceName(dsv.Get(), "SunGlareTest::Depth DSV");
			context->ClearState();
			context->PSSetConstantBuffers(12, 1, &frameRaw);
			context->OMSetRenderTargets(0, nullptr, dsv.Get());
			{
				SkyShader::ScopedBindings bindings(context.Get(), depthView.Get());
				ComPtr<ID3D11ShaderResourceView> bound;
				context->VSGetShaderResources(17, 1, &bound);
				Require(!bound, "A depth attachment must never also be bound as a visibility SRV");
			}
			context->ClearState();
			{
				SkyShader::ScopedBindings bindings(context.Get(), depthView.Get());
				ComPtr<ID3D11ShaderResourceView> bound;
				context->VSGetShaderResources(17, 1, &bound);
				Require(!bound, "Missing frame constants must disable depth filtering");
			}
			dsv.Reset();
			dsvDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH;
			Check(device->CreateDepthStencilView(depth.Get(), &dsvDesc, &dsv));
			Util::SetResourceName(dsv.Get(), "SunGlareTest::ReadOnlyDepth DSV");
			context->PSSetConstantBuffers(12, 1, &frameRaw);
			context->OMSetRenderTargets(0, nullptr, dsv.Get());
			{
				SkyShader::ScopedBindings bindings(context.Get(), depthView.Get());
				ComPtr<ID3D11ShaderResourceView> bound;
				context->VSGetShaderResources(17, 1, &bound);
				Require(bound.Get() == depthView.Get(), "Read-only depth must remain available for visibility");
			}
			context->ClearState();
		}
	};
}

int main()
{
	try {
		std::filesystem::current_path("package/Shaders");
		Fixture fixture;
		fixture.CheckBindings();
		unsigned checks = 0;
		for (unsigned mode : { 0u, 1u, 2u })
			for (bool reverse : { false, true }) {
				const bool vr = mode != 0, stereo = mode == 2;
				auto code = Compile(vr, reverse);
				ComPtr<ID3D11ShaderReflection> reflection;
				Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
				ComPtr<ID3D11ComputeShader> shader;
				Check(fixture.device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
				Util::SetResourceName(shader.Get(), "SunGlareTest::Visibility");
				fixture.context->CSSetShader(shader.Get(), nullptr, 0);
				ConstantBuffer frame(fixture.device.Get(), reflection.Get(), "FrameBuffer::PerFrame");
				ConstantBuffer input(fixture.device.Get(), reflection.Get(), "TestInput");
				std::optional<ConstantBuffer> vrValues;
				if (vr) {
					vrValues.emplace(fixture.device.Get(), reflection.Get(), "VRValues");
					vrValues->SetVariable("StereoEnabled", stereo ? 1.0f : 0.0f);
					vrValues->Bind(fixture.context.Get());
				}
				for (float scale : { 1.0f, 0.5f, 0.725f }) {
					frame.SetVariable("FrameBuffer::DynamicResolutionParams1", std::array<float, 4>{ scale, scale, scale, scale });
					frame.SetVariable("FrameBuffer::DynamicResolutionParams2", std::array<float, 4>{ 1 / scale, 1 / scale, scale, scale });
					frame.Bind(fixture.context.Get());
					for (bool occluded : { false, true }) {
						std::array<float, width * height> data;
						data.fill(0.5f);
						const UINT activeWidth = UINT(width * scale), activeHeight = UINT(height * scale);
						for (UINT y = 0; y < activeHeight; ++y)
							for (UINT x = 0; x < activeWidth; ++x) {
								const bool blocked = occluded != (stereo && x >= activeWidth / 2);
								data[y * width + x] = blocked ? 0.5f : (reverse ? 0.0f : 1.0f);
							}
						fixture.context->UpdateSubresource(fixture.depth.Get(), 0, nullptr, data.data(), width * sizeof(float), 0);
						for (UINT eye = 0; eye < (stereo ? 2u : 1u); ++eye) {
							input.SetVariable("Eye", eye);
							for (float x : { -0.95f, 0.0f, 0.95f }) {
								input.SetVariable("SunPosition", std::array<float, 4>{ x, 0, 1, 1 });
								input.Bind(fixture.context.Get());
								const float expected = (occluded != (eye == 1)) ? 0.0f : 1.0f;
								Require(std::abs(fixture.Dispatch(fixture.depthView.Get()) - expected) < 1e-5f,
									"Visibility must use only the selected eye's active depth region");
								++checks;
							}
						}
					}
					for (auto position : { std::array<float, 4>{ 3, 0, 1, 1 }, std::array<float, 4>{ 0, 0, 1, -1 } }) {
						input.SetVariable("SunPosition", position);
						input.Bind(fixture.context.Get());
						Require(fixture.Dispatch(fixture.depthView.Get()) == 1, "Offscreen/behind-camera sun must retain native visibility");
						++checks;
					}
					input.SetVariable("SunPosition", std::array<float, 4>{ 0, 0, 1, 1 });
					input.SetVariable("Eye", 0u);
					input.Bind(fixture.context.Get());
					Require(fixture.Dispatch(nullptr) == 1, "Missing depth must retain native visibility");
					++checks;
					std::array<float, width * height> partial;
					partial.fill(reverse ? 0.0f : 1.0f);
					for (UINT y = 0; y < UINT(height * scale) / 2; ++y)
						for (UINT x = 0; x < UINT(width * scale); ++x)
							partial[y * width + x] = 0.5f;
					fixture.context->UpdateSubresource(fixture.depth.Get(), 0, nullptr, partial.data(), width * sizeof(float), 0);
					const float faded = fixture.Dispatch(fixture.depthView.Get());
					Require(faded > 0 && faded < 1, "Partial sky coverage must fade rather than switch glare");
					++checks;
				}
				for (float scale : { 0.0f, -0.5f, 1.5f, std::numeric_limits<float>::quiet_NaN() }) {
					frame.SetVariable("FrameBuffer::DynamicResolutionParams1", std::array<float, 4>{ scale, scale, scale, scale });
					frame.Bind(fixture.context.Get());
					Require(fixture.Dispatch(fixture.depthView.Get()) == 1, "Invalid render regions must preserve native visibility");
					++checks;
				}
				fixture.CheckSkyVertex(vr, reverse, stereo);
			}
		std::cout << checks << " WARP visibility cases, 6 production Sky vertex draws and scoped binding restoration passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
