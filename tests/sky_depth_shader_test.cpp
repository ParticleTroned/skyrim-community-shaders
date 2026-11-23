#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Matrix = std::array<float, 16>;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct Technique
	{
		std::vector<const char*> defines;
		float units;
		bool moonEligible;
	};

	ComPtr<ID3DBlob> Compile(const Technique& technique, bool vr, bool reverse, const char* stage,
		std::span<const char* const> features = {}, const char* sourceOverride = nullptr)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { std::strcmp(stage, "ps_5_0") == 0 ? "PSHADER" : "VSHADER", "1" } };
		for (const auto* define : technique.defines)
			defines.push_back({ define, "1" });
		for (const auto* feature : features)
			defines.push_back({ feature, "1" });
		if (vr)
			defines.push_back({ "VR", "1" });
		if (reverse)
			defines.push_back({ "REVERSE_Z", "1" });
		defines.push_back({ nullptr, nullptr });
		auto includes = PackageIncludes::AllFeatures(".");
		const char* source = R"(
#define main SkyMain
#include "Sky.hlsl"
#undef main
cbuffer TestInput : register(b0) { uint Eye; };
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(1, 1, 1)] void main() {
    VS_INPUT input = (VS_INPUT)0;
    input.Position = float4(0, 0, 1, 1);
#if defined(VR)
    input.InstanceID = Eye;
#endif
    Result[0] = SkyMain(input).Position;
}
)";
		if (sourceOverride)
			source = sourceOverride;
		ComPtr<ID3DBlob> code, errors;
		const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		const HRESULT result = sourceOverride || std::strcmp(stage, "cs_5_0") == 0 ?
		                           D3DCompile(source, std::strlen(source), "sky-depth-test", defines.data(), &includes, "main", stage, flags, 0, &code, &errors) :
		                           D3DCompileFromFile(L"package/Shaders/Sky.hlsl", defines.data(), &includes, "main", stage, flags, 0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	unsigned Run(ID3D11Device* device, ID3D11DeviceContext* context, const Technique& technique, bool vr, bool reverse)
	{
		for (const auto& features : { std::vector<const char*>{}, { "CLOUD_SHADOWS" },
				 { "CLOUD_SHADOWS", "DEFERRED" }, { "CLOUD_SHADOWS", "DEFERRED", "CHARACTER_CATEGORY" } }) {
			Compile(technique, vr, reverse, "vs_5_0", features);
			auto pixel = Compile(technique, vr, reverse, "ps_5_0", features);
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(pixel->GetBufferPointer(), pixel->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			D3D11_SHADER_DESC desc{};
			Check(reflection->GetDesc(&desc));
			bool writesDepth = false, clouds = false;
			for (UINT i = 0; i < desc.OutputParameters; ++i) {
				D3D11_SIGNATURE_PARAMETER_DESC output{};
				Check(reflection->GetOutputParameterDesc(i, &output));
				writesDepth |= output.SystemValueType == D3D_NAME_DEPTH;
			}
			for (const auto* define : technique.defines)
				clouds |= std::strcmp(define, "CLOUDS") == 0;
			Require(writesDepth == (reverse && clouds), "Only reverse-Z clouds should write pixel depth");
		}
		auto code = Compile(technique, vr, reverse, "cs_5_0");
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
		Util::SetResourceName(shader.Get(), "SkyDepthTest::CS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		auto optionalBuffer = [&](const char* name) -> std::unique_ptr<ConstantBuffer> {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDescByName(name, &binding)))
				return {};
			return std::make_unique<ConstantBuffer>(device, reflection.Get(), name);
		};
		ConstantBuffer geometry(device, reflection.Get(), "PerGeometry");
		auto frame = optionalBuffer("FrameBuffer::PerFrame");
		auto permutation = optionalBuffer("Permutation::PerShader");
		auto input = optionalBuffer("TestInput");
		auto stereo = optionalBuffer("VRValues");
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = desc.StructureByteStride = 4 * sizeof(float);
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		ComPtr<ID3D11Buffer> output, readback;
		Check(device->CreateBuffer(&desc, nullptr, &output));
		Util::SetResourceName(output.Get(), "SkyDepthTest::Output");
		ComPtr<ID3D11UnorderedAccessView> uav;
		Check(device->CreateUnorderedAccessView(output.Get(), nullptr, &uav));
		Util::SetResourceName(uav.Get(), "SkyDepthTest::Output UAV");
		desc.BindFlags = desc.MiscFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateBuffer(&desc, nullptr, &readback));
		Util::SetResourceName(readback.Get(), "SkyDepthTest::Readback");
		unsigned checked = 0;
		for (uint32_t reversedEyes : { 0u, 1u, 2u, 3u })
			for (bool moon : { false, true })
				for (float clipW : { -64.0f, -0.5f, 0.0f, 0.5f, 1.0f, 64.0f })
					for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye) {
						context->ClearState();
						std::array<Matrix, 2> world{}, projection{};
						for (uint32_t i = 0; i < 2; ++i) {
							world[i][0] = world[i][5] = world[i][10] = 1;
							world[i][15] = clipW;
							projection[i][10] = (reversedEyes & (1u << i)) ? -1.0f : 1.0f;
							projection[i][14] = 1;
						}
						if (vr)
							geometry.SetVariable("WorldViewProj", world);
						else
							geometry.SetVariable("WorldViewProj", world[0]);
						geometry.Bind(context);
						if (frame) {
							if (vr)
								frame->SetVariable("FrameBuffer::CameraProj", projection);
							else
								frame->SetVariable("FrameBuffer::CameraProj", projection[0]);
							frame->Bind(context);
						}
						if (permutation) {
							permutation->SetVariable("Permutation::ExtraShaderDescriptor", 0xFFFFFFFFu);
							permutation->SetVariable("Permutation::ExtraFeatureDescriptor", moon ? 1u << 11 : 0u);
							permutation->Bind(context);
						}
						if (input) {
							input->SetVariable("Eye", eye);
							input->Bind(context);
						}
						if (stereo) {
							stereo->SetVariable("StereoEnabled", 1.0f);
							stereo->Bind(context);
						}
						auto* view = uav.Get();
						context->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
						context->CSSetShader(shader.Get(), nullptr, 0);
						context->Dispatch(1, 1, 1);
						context->ClearState();
						context->CopyResource(readback.Get(), output.Get());
						D3D11_MAPPED_SUBRESOURCE mapped{};
						Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
						std::array<float, 4> position;
						std::memcpy(position.data(), mapped.pData, sizeof(position));
						context->Unmap(readback.Get(), 0);
						const float units = technique.units != 0 ? technique.units : (technique.moonEligible && moon ? 9.0f : 0.0f);
						const float expected = reverse && (reversedEyes & (1u << eye)) ? units / 16777216.0f : 1.0f;
						Require(position[3] == clipW && position[2] == expected * clipW,
							"Production sky depth changed standard projection, layer order, eye selection or flag ownership");
						++checked;
					}
		return checked;
	}

	struct CloudRasterFixture
	{
		static constexpr UINT size = 32;
		enum class Placement
		{
			Front,
			Crossing,
			Behind
		};
		ID3D11DeviceContext* context;
		bool vr;
		bool reverseFeature;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11Texture2D> depth, readback;
		ComPtr<ID3D11Texture2D> coverage, coverageReadback;
		ComPtr<ID3D11RenderTargetView> coverageView;
		ComPtr<ID3D11DepthStencilView> depthView;
		std::array<ComPtr<ID3D11RasterizerState>, 2> rasterStates;
		std::array<ComPtr<ID3D11DepthStencilState>, 3> depthStates;
		std::unique_ptr<ConstantBuffer> geometry, frame, input, stereo;

		CloudRasterFixture(ID3D11Device* device, ID3D11DeviceContext* context, const Technique& technique, bool vr, bool reverseFeature) :
			context(context), vr(vr), reverseFeature(reverseFeature)
		{
			const char* source = R"(
#define main SkyMain
#include "Sky.hlsl"
#undef main
cbuffer TestInput : register(b0) { float4 Vertices[3]; uint Eye; };
VS_OUTPUT main(uint vertex : SV_VertexID) {
    VS_INPUT input = (VS_INPUT)0;
    input.Position = Vertices[vertex];
#if defined(VR)
    input.InstanceID = Eye;
#endif
    return SkyMain(input);
}
)";
			const char* pixelSource = R"(
#define main SkyMain
#include "Sky.hlsl"
#undef main
PS_OUTPUT main(PS_INPUT input) {
    PS_OUTPUT output = SkyMain(input);
    output.Color = 1;
    return output;
}
)";
			auto vertexCode = Compile(technique, vr, reverseFeature, "vs_5_0", {}, source);
			auto pixelCode = Compile(technique, vr, reverseFeature, "ps_5_0", {}, pixelSource);
			Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertex));
			Util::SetResourceName(vertex.Get(), "SkyDepthTest::CloudVS");
			Check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixel));
			Util::SetResourceName(pixel.Get(), "SkyDepthTest::CloudPS");
			Check(D3DReflect(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			geometry = std::make_unique<ConstantBuffer>(device, reflection.Get(), "PerGeometry");
			if (reverseFeature)
				frame = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			input = std::make_unique<ConstantBuffer>(device, reflection.Get(), "TestInput");
			if (vr) {
				stereo = std::make_unique<ConstantBuffer>(device, reflection.Get(), "VRValues");
				stereo->SetVariable("StereoEnabled", 1.0f);
				stereo->SetVariable("EyeOffsetScale", std::array{ -0.5f, 0.5f });
				stereo->SetVariable("EyeClipEdge", std::array<float, 8>{ -1, 0, 0, 1, 1, 0, 0, 1 });
			}
			std::array<Matrix, 2> world{};
			for (auto& matrix : world) {
				matrix[0] = matrix[5] = matrix[10] = 1;
				matrix[14] = 1;
			}
			if (vr)
				geometry->SetVariable("WorldViewProj", world);
			else
				geometry->SetVariable("WorldViewProj", world[0]);

			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = size;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32_TYPELESS;
			desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			Check(device->CreateTexture2D(&desc, nullptr, &depth));
			Util::SetResourceName(depth.Get(), "SkyDepthTest::Depth");
			D3D11_DEPTH_STENCIL_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_D32_FLOAT;
			view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			Check(device->CreateDepthStencilView(depth.Get(), &view, &depthView));
			Util::SetResourceName(depthView.Get(), "SkyDepthTest::Depth DSV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, &readback));
			Util::SetResourceName(readback.Get(), "SkyDepthTest::DepthReadback");
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			Check(device->CreateTexture2D(&desc, nullptr, &coverageReadback));
			Util::SetResourceName(coverageReadback.Get(), "SkyDepthTest::CoverageReadback");
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.CPUAccessFlags = 0;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			Check(device->CreateTexture2D(&desc, nullptr, &coverage));
			Util::SetResourceName(coverage.Get(), "SkyDepthTest::Coverage");
			Check(device->CreateRenderTargetView(coverage.Get(), nullptr, &coverageView));
			Util::SetResourceName(coverageView.Get(), "SkyDepthTest::Coverage RTV");
			D3D11_RASTERIZER_DESC rasterDesc{};
			rasterDesc.FillMode = D3D11_FILL_SOLID;
			rasterDesc.CullMode = D3D11_CULL_NONE;
			rasterDesc.DepthClipEnable = true;
			for (UINT i = 0; i < rasterStates.size(); ++i) {
				rasterDesc.DepthBias = i ? 9 : -9;
				Check(device->CreateRasterizerState(&rasterDesc, &rasterStates[i]));
				Util::SetResourceName(rasterStates[i].Get(), "SkyDepthTest::Rasterizer%u", i);
			}
			const std::array comparisons{ D3D11_COMPARISON_ALWAYS, D3D11_COMPARISON_LESS_EQUAL, D3D11_COMPARISON_GREATER_EQUAL };
			for (UINT i = 0; i < comparisons.size(); ++i) {
				D3D11_DEPTH_STENCIL_DESC depthDesc{};
				depthDesc.DepthEnable = true;
				depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
				depthDesc.DepthFunc = comparisons[i];
				Check(device->CreateDepthStencilState(&depthDesc, &depthStates[i]));
				Util::SetResourceName(depthStates[i].Get(), "SkyDepthTest::DepthState%u", i);
			}
		}

		std::array<float, size * size> Read(ID3D11Texture2D* source, ID3D11Texture2D* staging)
		{
			context->CopyResource(staging, source);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
			std::array<float, size * size> pixels{};
			for (UINT y = 0; y < size; ++y)
				std::memcpy(pixels.data() + y * size, static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch, size * sizeof(float));
			context->Unmap(staging, 0);
			return pixels;
		}

		void Draw(uint32_t eye, uint32_t reversedEyes, Placement placement, bool foreground)
		{
			context->ClearState();
			const bool reverse = reverseFeature && (reversedEyes & (1u << eye)) != 0;
			std::array<Matrix, 2> projections{};
			for (UINT i = 0; i < 2; ++i) {
				projections[i][10] = (reversedEyes & (1u << i)) ? -1.0f : 1.0f;
				projections[i][14] = 1;
			}
			if (frame) {
				if (vr)
					frame->SetVariable("FrameBuffer::CameraProj", projections);
				else
					frame->SetVariable("FrameBuffer::CameraProj", projections[0]);
			}
			// WorldViewProj maps input z to clip w, so the third vertex can sit behind the camera.
			const float lowerW = placement == Placement::Behind ? -1.0f : 1.0f;
			const float upperW = placement == Placement::Front ? 1.0f : -0.5f;
			input->SetVariable("Vertices", std::array<float, 12>{ -2, -1, lowerW, 1, 2, -1, lowerW, 1, 0, 1, upperW, 1 });
			if (vr)
				input->SetVariable("Eye", eye);
			for (auto* constants : { geometry.get(), frame.get(), input.get(), stereo.get() }) {
				if (!constants)
					continue;
				constants->Bind(context);
				auto* buffer = constants->buffer.Get();
				context->VSSetConstantBuffers(constants->slot, 1, &buffer);
			}
			if (frame)
				frame->Bind(context, D3D11ShaderTest::Stage::Pixel);
			// A restricted depth range also checks standard-projection SV_Position depth passthrough.
			D3D11_VIEWPORT viewport{ 0, 0, float(size), float(size), 0, 0.75f };
			context->RSSetViewports(1, &viewport);
			context->RSSetState(rasterStates[reverse ? 1 : 0].Get());
			auto* target = coverageView.Get();
			context->OMSetRenderTargets(1, &target, depthView.Get());
			context->OMSetDepthStencilState(depthStates[foreground ? (reverse ? 2 : 1) : 0].Get(), 0);
			context->ClearDepthStencilView(depthView.Get(), D3D11_CLEAR_DEPTH, 0.5f, 0);
			const float clear[4]{};
			context->ClearRenderTargetView(coverageView.Get(), clear);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			context->Draw(3, 0);
			context->ClearState();
			const auto pixels = Read(depth.Get(), readback.Get());
			const auto coveragePixels = Read(coverage.Get(), coverageReadback.Get());
			const float expected = reverse ? 9.0f / 16777216.0f : viewport.MaxDepth - 9.0f / 16777216.0f;
			const bool invisible = foreground || placement == Placement::Behind;
			unsigned covered = 0;
			for (size_t i = 0; i < pixels.size(); ++i) {
				const float value = pixels[i];
				if (coveragePixels[i] == 0) {
					Require(value == 0.5f, "Uncovered cloud pixels modified depth");
					continue;
				}
				Require(coveragePixels[i] == 1 && !invisible, "Occluded or behind-camera clouds wrote scene colour");
				Require(!vr || (i % size < size / 2) == (eye == 0), "Cloud draw crossed into the other eye");
				if (!std::isfinite(value) || value != expected)
					std::cerr << "Cloud depth: " << std::hexfloat << value << " expected " << expected << std::defaultfloat
							  << "; VR=" << vr << " eye=" << eye << " reversedEyes=" << reversedEyes << " placement=" << int(placement) << '\n';
				Require(std::isfinite(value) && value == expected, "Rasterized cloud depth lost sky ordering or selected the wrong eye");
				++covered;
			}
			Require(invisible || covered > 16, "Cloud clipping test did not rasterize sufficient pixels");
		}
	};

	unsigned CheckCloudRasterization(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		unsigned checked = 0;
		for (const auto& defines : { std::vector<const char*>{ "TEX", "CLOUDS" },
				 { "TEX", "CLOUDS", "TEXLERP" }, { "TEX", "CLOUDS", "TEXFADE" } })
			for (bool vr : { false, true })
				for (bool reverseFeature : { false, true }) {
					CloudRasterFixture fixture(device, context, Technique{ defines, 0, false }, vr, reverseFeature);
					for (uint32_t reversedEyes : { 0u, 1u, 2u, 3u })
						for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye)
							for (auto placement : { CloudRasterFixture::Placement::Front, CloudRasterFixture::Placement::Crossing, CloudRasterFixture::Placement::Behind })
								for (bool foreground : { true, false }) {
									fixture.Draw(eye, reversedEyes, placement, foreground);
									++checked;
								}
				}
		return checked;
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, nullptr, &context));
		const std::array techniques{
			Technique{ { "TEX", "MOONMASK" }, 3, true }, Technique{ { "TEX", "CLOUDS" }, 0, false },
			Technique{ { "TEX", "CLOUDS", "TEXLERP" }, 0, false }, Technique{ { "TEX" }, 0, true },
			Technique{ { "TEX", "CLOUDS", "TEXFADE" }, 0, false }, Technique{ { "TEX", "DITHER" }, 0, true },
			Technique{ { "DITHER" }, 0, false }, Technique{ { "HORIZFADE" }, 0, false }, Technique{ { "OCCLUSION" }, 0, false }
		};
		unsigned checked = 0;
		for (bool vr : { false, true })
			for (bool reverse : { false, true })
				for (const auto& technique : techniques)
					checked += Run(device.Get(), context.Get(), technique, vr, reverse);
		const auto rasterized = CheckCloudRasterization(device.Get(), context.Get());
		std::cout << checked << " production sky depth cases, 288 VS/PS permutations and " << rasterized
				  << " cloud rasterization cases passed on WARP (SE/AE and VR)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
