#define NOMINMAX
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	using Motion = std::array<float, 2>;
	using Size = std::array<UINT, 2>;
	constexpr Pixel sentinel{ -11.0f, -11.0f, -11.0f, -11.0f };

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Near(float actual, float expected, const char* message)
	{
		if (!std::isfinite(actual) || std::abs(actual - expected) > 2e-5f) {
			std::cerr << message << ": expected " << expected << ", observed " << actual << '\n';
			throw std::runtime_error(message);
		}
	}

	struct Region
	{
		Size offset{}, size{};
		bool Contains(UINT x, UINT y) const
		{
			return x >= offset[0] && y >= offset[1] && x - offset[0] < size[0] && y - offset[1] < size[1];
		}
	};

	struct Texture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		Size size;

		template <class T>
		Texture(ID3D11Device* device, Size dimensions, DXGI_FORMAT format, const std::vector<T>& data) :
			size(dimensions)
		{
			Require(data.size() == size[0] * size[1], "Texture fixture dimensions disagree");
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = size[0];
			desc.Height = size[1];
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = format;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			const D3D11_SUBRESOURCE_DATA initial{ data.data(), size[0] * UINT(sizeof(T)), 0 };
			Check(device->CreateTexture2D(&desc, &initial, &texture));
			Util::SetResourceName(texture.Get(), "NRModelResolutionTest::Texture");
			Check(device->CreateShaderResourceView(texture.Get(), nullptr, &srv));
			Util::SetResourceName(srv.Get(), "NRModelResolutionTest::Texture SRV");
			Check(device->CreateUnorderedAccessView(texture.Get(), nullptr, &uav));
			Util::SetResourceName(uav.Get(), "NRModelResolutionTest::Texture UAV");
		}

		template <class T>
		std::vector<T> Read(ID3D11Device* device, ID3D11DeviceContext* context) const
		{
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Texture2D> staging;
			Check(device->CreateTexture2D(&desc, nullptr, &staging));
			Util::SetResourceName(staging.Get(), "NRModelResolutionTest::Readback");
			context->CopyResource(staging.Get(), texture.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<T> pixels(size[0] * size[1]);
			for (UINT y = 0; y < size[1]; ++y) {
				const auto* row = reinterpret_cast<const T*>(static_cast<const std::byte*>(mapped.pData) + mapped.RowPitch * y);
				std::copy_n(row, size[0], pixels.begin() + size[0] * y);
			}
			context->Unmap(staging.Get(), 0);
			return pixels;
		}
	};

	struct Shader
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;

		Shader(ID3D11Device* device, const wchar_t* path, bool vr)
		{
			PackageIncludes includes("package/Shaders", "features/Neural Rendering/Shaders");
			const D3D_SHADER_MACRO defines[]{ { "VR", "1" }, { nullptr, nullptr } };
			ComPtr<ID3DBlob> code, errors;
			const auto result = D3DCompileFromFile(path, vr ? defines : defines + 1, &includes,
				"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, &code, &errors);
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(result);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "NRModelResolutionTest::CS");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		}
	};

	void Configure(ConstantBuffer& constants, Size source, Size model, Region sourceRegion, Region modelRegion, bool hasMask, Motion motionScale)
	{
		Require(constants.bytes.size() == 64, "Model resolution cbuffer layout changed");
		constants.SetVariable("SourceSize", source);
		constants.SetVariable("ModelSize", model);
		constants.SetVariable("SourceRegionOffset", sourceRegion.offset);
		constants.SetVariable("SourceRegionSize", sourceRegion.size);
		constants.SetVariable("ModelRegionOffset", modelRegion.offset);
		constants.SetVariable("ModelRegionSize", modelRegion.size);
		constants.SetVariable("HasMask", UINT(hasMask));
		constants.SetVariable("OutputFormat", 0u);
		constants.SetVariable("MotionVectorScale", motionScale);
	}

	void Dispatch(ID3D11DeviceContext* context, Shader& shader, ConstantBuffer& constants, Size extent,
		std::span<ID3D11ShaderResourceView* const> inputs, std::span<ID3D11UnorderedAccessView* const> outputs)
	{
		constants.Bind(context);
		context->CSSetShader(shader.shader.Get(), nullptr, 0);
		context->CSSetShaderResources(0, UINT(inputs.size()), inputs.data());
		context->CSSetUnorderedAccessViews(0, UINT(outputs.size()), outputs.data(), nullptr);
		context->Dispatch((extent[0] + 7) / 8, (extent[1] + 7) / 8, 1);
		context->ClearState();
	}

	void CheckPrepare(ID3D11Device* device, ID3D11DeviceContext* context, Shader& shader, bool maskEnabled)
	{
		constexpr Size source{ 14, 10 }, model{ 7, 5 };
		constexpr Region sourceRegion{ { 2, 2 }, { 10, 6 } }, modelRegion{ { 1, 1 }, { 5, 3 } };
		constexpr Motion motionScale{ 2.0f, 3.0f };
		std::vector<Pixel> color(source[0] * source[1], Pixel{ 10, 10, 10, 10 });
		std::vector<float> depth(color.size(), 0.0f), mask(color.size(), 1.0f);
		std::vector<Motion> motion(color.size(), Motion{ 10, 10 });
		for (UINT y = 2; y < 8; ++y) {
			for (UINT x = 2; x < 12; ++x) {
				const auto index = y * source[0] + x;
				color[index] = { x * 0.02f, y * 0.03f, ((x + y) & 1u) ? 0.8f : 0.2f, 0.375f };
				depth[index] = 0.8f - 0.2f * float((x & 1u) + 2 * (y & 1u));
				motion[index] = { x * 0.01f, y * 0.02f };
				mask[index] = (x & 1u) && (y & 1u) ? 0.75f : 0.25f;
			}
		}
		Texture input(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, color);
		Texture inputDepth(device, source, DXGI_FORMAT_R32_FLOAT, depth);
		Texture inputMotion(device, source, DXGI_FORMAT_R32G32_FLOAT, motion);
		Texture inputMask(device, source, DXGI_FORMAT_R32_FLOAT, mask);
		Texture proxy(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(model[0] * model[1], sentinel));
		Texture proxyDepth(device, model, DXGI_FORMAT_R32_FLOAT, std::vector(model[0] * model[1], -11.0f));
		Texture proxyMotion(device, model, DXGI_FORMAT_R32G32_FLOAT, std::vector(model[0] * model[1], Motion{ -11, -11 }));
		Texture proxyMask(device, model, DXGI_FORMAT_R32_FLOAT, std::vector(model[0] * model[1], -11.0f));
		ConstantBuffer constants(device, shader.reflection.Get(), "ModelResolutionConstants");
		Configure(constants, source, model, sourceRegion, modelRegion, maskEnabled, motionScale);
		const std::array inputs{ input.srv.Get(), inputDepth.srv.Get(), inputMotion.srv.Get(), maskEnabled ? inputMask.srv.Get() : nullptr };
		const std::array outputs{ proxy.uav.Get(), proxyDepth.uav.Get(), proxyMotion.uav.Get(), proxyMask.uav.Get() };
		Dispatch(context, shader, constants, modelRegion.size, inputs, outputs);
		const auto colors = proxy.Read<Pixel>(device, context);
		const auto depths = proxyDepth.Read<float>(device, context);
		const auto motions = proxyMotion.Read<Motion>(device, context);
		const auto masks = proxyMask.Read<float>(device, context);
		for (UINT y = 0; y < model[1]; ++y) {
			for (UINT x = 0; x < model[0]; ++x) {
				const auto index = y * model[0] + x;
				if (!modelRegion.Contains(x, y)) {
					Require(colors[index] == sentinel && depths[index] == -11 && motions[index] == Motion{ -11, -11 } && masks[index] == -11,
						"Preparation wrote outside its model ROI");
					continue;
				}
				Near(colors[index][0], (2 * x + 0.5f) * 0.02f, "Downsample changed the area-weighted red mean");
				Near(colors[index][1], (2 * y + 0.5f) * 0.03f, "Downsample changed the area-weighted green mean");
				Near(colors[index][2], 0.5f, "Downsample aliased alternating fine detail");
				Near(depths[index], 0.2f, "Downsample did not retain the nearest surface");
				Near(motions[index][0], (2 * x + 1) * 0.01f * motionScale[0], "Depth and horizontal motion came from different surfaces");
				Near(motions[index][1], (2 * y + 1) * 0.02f * motionScale[1], "Depth and vertical motion came from different surfaces");
				Near(masks[index], maskEnabled ? 0.75f : -11.0f, "Protection mask was diluted or an absent mask was enabled");
			}
		}
	}

	void CheckNonIntegerPrepare(ID3D11Device* device, ID3D11DeviceContext* context, Shader& shader, bool fullScale, bool clipped = false)
	{
		constexpr Size source{ 13, 9 };
		const Size model = fullScale ? source : Size{ 7, 5 };
		const Region sourceRegion = clipped ? Region{ { 3, 2 }, { 7, 5 } } : Region{ {}, source };
		const Region modelRegion = clipped ? Region{ { 1, 1 }, { 5, 3 } } : Region{ {}, model };
		std::vector<Pixel> color(source[0] * source[1]);
		Pixel sourceMean{};
		for (UINT y = 0; y < source[1]; ++y) {
			for (UINT x = 0; x < source[0]; ++x) {
				auto& pixel = color[y * source[0] + x];
				pixel = clipped ? (sourceRegion.Contains(x, y) ? Pixel{ 0.2f, 0.3f, 0.4f, 0.375f } : Pixel{ 10, 10, 10, 10 }) :
				                  Pixel{ x * 0.03f, y * 0.05f, ((x + y) & 1u) ? 0.8f : 0.2f, 0.375f };
				for (UINT channel = 0; channel < 3; ++channel)
					sourceMean[channel] += pixel[channel] / float(color.size());
			}
		}
		Texture input(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, color);
		Texture depth(device, source, DXGI_FORMAT_R32_FLOAT, std::vector(color.size(), 0.5f));
		Texture motion(device, source, DXGI_FORMAT_R32G32_FLOAT, std::vector(color.size(), Motion{ 0.1f, -0.2f }));
		Texture proxy(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(model[0] * model[1], sentinel));
		Texture proxyDepth(device, model, DXGI_FORMAT_R32_FLOAT, std::vector(model[0] * model[1], -11.0f));
		Texture proxyMotion(device, model, DXGI_FORMAT_R32G32_FLOAT, std::vector(model[0] * model[1], Motion{ -11, -11 }));
		Texture proxyMask(device, model, DXGI_FORMAT_R32_FLOAT, std::vector(model[0] * model[1], -11.0f));
		ConstantBuffer constants(device, shader.reflection.Get(), "ModelResolutionConstants");
		Configure(constants, source, model, sourceRegion, modelRegion, false, { 1, 1 });
		const std::array inputs{ input.srv.Get(), depth.srv.Get(), motion.srv.Get(), static_cast<ID3D11ShaderResourceView*>(nullptr) };
		const std::array outputs{ proxy.uav.Get(), proxyDepth.uav.Get(), proxyMotion.uav.Get(), proxyMask.uav.Get() };
		Dispatch(context, shader, constants, modelRegion.size, inputs, outputs);
		const auto pixels = proxy.Read<Pixel>(device, context);
		Pixel modelMean{};
		for (std::size_t index = 0; index < pixels.size(); ++index) {
			if (clipped) {
				if (modelRegion.Contains(UINT(index % model[0]), UINT(index / model[0]))) {
					for (UINT channel = 0; channel < 3; ++channel)
						Near(pixels[index][channel], 0.2f + 0.1f * channel, "Outward ROI mapping admitted color from outside the source region");
				} else {
					Require(pixels[index] == sentinel, "Clipped reduction wrote outside the outward model ROI");
				}
				continue;
			}
			for (UINT channel = 0; channel < 3; ++channel) {
				modelMean[channel] += pixels[index][channel] / float(pixels.size());
				if (fullScale)
					Near(pixels[index][channel], color[index][channel], "100% preparation changed source pixels");
			}
		}
		for (UINT channel = 0; !clipped && channel < 3; ++channel)
			Near(modelMean[channel], sourceMean[channel], "Odd-size area reduction did not conserve mean color");
	}

	void CheckReconstruct(ID3D11Device* device, ID3D11DeviceContext* context, Shader& shader, unsigned condition)
	{
		constexpr Size source{ 14, 10 }, model{ 7, 5 };
		constexpr Region sourceRegion{ { 2, 2 }, { 10, 6 } }, modelRegion{ { 1, 1 }, { 5, 3 } };
		std::vector<Pixel> color(source[0] * source[1]);
		for (UINT y = 0; y < source[1]; ++y)
			for (UINT x = 0; x < source[0]; ++x)
				color[y * source[0] + x] = { ((x + y) & 1u) ? 0.65f : 0.15f, 0.25f, 0.3f, 0.1f + 0.01f * x };
		std::vector<Pixel> baseline(model[0] * model[1], Pixel{ 20, 20, 20, 1 });
		std::vector<Pixel> neural(model[0] * model[1], Pixel{ 50, 50, 50, 1 });
		const float residual = condition == 1 ? 0.125f : 0.0f;
		for (UINT y = 1; y < 4; ++y) {
			for (UINT x = 1; x < 6; ++x) {
				const auto index = y * model[0] + x;
				baseline[index] = { 0.1f * x, 0.1f * y, 0.25f, 0.9f };
				neural[index] = { baseline[index][0] + residual, baseline[index][1] + residual, baseline[index][2] + residual, 0.0f };
				if (condition == 2)
					neural[index][0] = std::numeric_limits<float>::quiet_NaN();
				if (condition == 3)
					neural[index][1] = std::numeric_limits<float>::infinity();
				if (condition >= 4)
					neural[index][0] = condition == 4 ? 70000.0f : condition == 5 ? -1.0f :
					                                                                2.0f;
			}
		}
		Texture input(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, color);
		Texture proxy(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, baseline);
		Texture outputNeural(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, neural);
		Texture result(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(color.size(), sentinel));
		ConstantBuffer constants(device, shader.reflection.Get(), "ModelResolutionConstants");
		Configure(constants, source, model, sourceRegion, modelRegion, false, { 1, 1 });
		constants.SetVariable("OutputFormat", condition >= 4 ? condition - 3 : 0u);
		const std::array inputs{ input.srv.Get(), proxy.srv.Get(), outputNeural.srv.Get() };
		const std::array outputs{ result.uav.Get() };
		Dispatch(context, shader, constants, sourceRegion.size, inputs, outputs);
		const auto pixels = result.Read<Pixel>(device, context);
		for (UINT y = 0; y < source[1]; ++y) {
			for (UINT x = 0; x < source[0]; ++x) {
				const auto index = y * source[0] + x;
				if (!sourceRegion.Contains(x, y)) {
					Require(pixels[index] == sentinel, "Reconstruction wrote outside its source ROI");
					continue;
				}
				for (UINT channel = 0; channel < 3; ++channel)
					Near(pixels[index][channel], color[index][channel] + residual, "Residual reconstruction lost base detail, crossed ROI, or failed finite fallback");
				Near(pixels[index][3], color[index][3], "Residual reconstruction changed full-resolution alpha");
			}
		}
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
		for (bool vr : { false, true }) {
			Shader prepare(device.Get(), L"features/Neural Rendering/Shaders/Upscaling/NeuralRendering/ModelResolutionPrepareCS.hlsl", vr);
			Shader reconstruct(device.Get(), L"features/Neural Rendering/Shaders/Upscaling/NeuralRendering/ModelResolutionReconstructCS.hlsl", vr);
			for (bool mask : { false, true })
				CheckPrepare(device.Get(), context.Get(), prepare, mask);
			for (bool fullScale : { false, true })
				CheckNonIntegerPrepare(device.Get(), context.Get(), prepare, fullScale);
			CheckNonIntegerPrepare(device.Get(), context.Get(), prepare, false, true);
			for (unsigned condition = 0; condition < 7; ++condition)
				CheckReconstruct(device.Get(), context.Get(), reconstruct, condition);
		}
		std::cout << "PASS: NR production shaders on WARP: area reduction, guide/mask ownership, odd sizes, identity, residual detail, alpha, ROI isolation and finite fallback (flat/VR)\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}