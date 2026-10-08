#include "Features/Upscaling/NeuralRendering/ModelResolution.h"
#include "GpuPass.h"
#include "ShaderPackageIncludes.h"
#include "Utils/ShaderCompiler.h"
#include "d3d11_shader_test.h"
#include "nr_model_resolution_accounting_under_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace AdapterCompiler
{
	ID3D11Device* device = nullptr;
	unsigned calls = 0;
	bool vr = false;
}

// Substitute only engine compilation/profiling; the adapter and GPU work are production code.
ID3D11DeviceChild* Util::CompileShader(const wchar_t* path,
	const std::vector<std::pair<const char*, const char*>>&, const char* target, const char* entry, ShaderCompileTiming*)
{
	++AdapterCompiler::calls;
	const auto shaderPath = std::filesystem::path("features/Neural Rendering/Shaders/Upscaling/NeuralRendering") /
	                        std::filesystem::path(path).filename();
	PackageIncludes includes("package/Shaders", "features/Neural Rendering/Shaders");
	const D3D_SHADER_MACRO defines[]{ { "VR", "1" }, { nullptr, nullptr } };
	Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
	const auto result = D3DCompileFromFile(shaderPath.c_str(), AdapterCompiler::vr ? defines : defines + 1, &includes,
		entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &errors);
	if (errors)
		std::cerr << static_cast<const char*>(errors->GetBufferPointer());
	D3D11ShaderTest::Check(result);
	ID3D11ComputeShader* shader = nullptr;
	D3D11ShaderTest::Check(AdapterCompiler::device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
	return shader;
}
ScopedGpuPass::ScopedGpuPass(std::string_view, const Util::PassTimingHandle&, bool) {}
ScopedGpuPass::~ScopedGpuPass() = default;

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

		Texture(const NeuralRendering::Color::Texture& source, Size dimensions) :
			texture(source.resource), srv(source.srv), uav(source.uav), size(dimensions) {}

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
	void CheckSharedAdapter(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		using namespace NeuralRendering;
		constexpr Size source{ 17, 11 };
		std::vector<Pixel> color(source[0] * source[1]), right(color.size());
		std::vector<float> depths(color.size());
		std::vector<Motion> velocities(color.size());
		for (std::size_t i = 0; i < color.size(); ++i) {
			color[i] = { float(i % 7) * 0.03f, float(i % 11) * 0.02f, float(i % 5) * 0.04f, 0.375f };
			right[i] = { color[i][2] + 0.1f, color[i][0] + 0.05f, color[i][1], 0.625f };
			depths[i] = float(i % 13) / 13.0f;
			velocities[i] = { float(i % 3) * 0.01f, -float(i % 7) * 0.02f };
		}
		Texture leftInput(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, color);
		Texture rightInput(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, right);
		Texture depth(device, source, DXGI_FORMAT_R32_FLOAT, depths);
		Texture motion(device, source, DXGI_FORMAT_R32G32_FLOAT, velocities);
		Texture leftOutput(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(color.size(), sentinel));
		Texture rightOutput(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(color.size(), sentinel));
		RendererApplyArgs input;
		input.device = device;
		input.context = context;
		input.colorInput = leftInput.texture.Get();
		input.depthGuide = depth.texture.Get();
		input.depthGuideSRV = depth.srv.Get();
		input.motionVectors = motion.texture.Get();
		input.colorOutput = leftOutput.texture.Get();
		input.colorWidth = input.guideWidth = input.outputWidth = source[0];
		input.colorHeight = input.guideHeight = input.outputHeight = source[1];
		input.viewportCrop = UpscalingDLSS::ViewportCrop::Identity(source[0], source[1], source[0], source[1]);
		input.computeSubrect = { 3, 2, 11, 7 };
		input.featureSlot = 2;
		input.renderingMode = RenderingMode::ReducedResolution;
		input.reset = true;
		for (unsigned percent : { 90u, 80u, 50u, 33u }) {
			input.modelResolutionPercent = percent;
			std::array args{ input, input };
			args[1].featureSlot = 3;
			args[1].colorInput = rightInput.texture.Get();
			args[1].colorOutput = rightOutput.texture.Get();
			Require(ModelResolution::CanUseSharedTargets(args, false, false), "Raw stateless C must accept shared preparation");
			Require(!ModelResolution::CanUseSharedTargets(args, true, false) && !ModelResolution::CanUseSharedTargets(args, false, true),
				"Color processing and compact recovery must retain staged inputs");
			ModelResolution staged, shared;
			ModelResolution::Batch baseline, candidate, invalid;
			HRESULT result = S_OK;
			Require(staged.Prepare(args, baseline, result), "Staged reference preparation failed");
			const Size model{ baseline.arguments[0].outputWidth, baseline.arguments[0].outputHeight };
			const auto count = model[0] * model[1];
			std::array<ModelResolution::Targets, 2> targets;
			const auto own = [](const Texture& texture) { return Color::Texture{ texture.texture, texture.srv, texture.uav }; };
			for (auto& target : targets) {
				target = { own(Texture(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(count, sentinel))),
					own(Texture(device, model, DXGI_FORMAT_R32_FLOAT, std::vector(count, -1.0f))),
					own(Texture(device, model, DXGI_FORMAT_R32G32_FLOAT, std::vector(count, Motion{ -1, -1 }))),
					own(Texture(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(count, sentinel))) };
			}
			const auto retained = 2u * source[0] * source[1] * sizeof(Pixel);
			Require(shared.AdditionalBytes(args, true) == retained, "Shared aliases must not enter private allocation admission");
			auto bad = targets;
			bad[1].color.srv = bad[1].depth.srv;
			Require(!shared.Prepare(args, invalid, result, bad) && result == E_INVALIDARG && invalid.count == 0,
				"Mismatched right-eye view must reject the whole batch before preparation");
			const auto untouched = Texture(targets[0].color, model).Read<Pixel>(device, context);
			Require(std::ranges::all_of(untouched, [](const auto& value) { return value == sentinel; }),
				"Invalid right-eye target dispatched left-eye preparation");
			bad = targets;
			bad[1] = bad[0];
			Require(!shared.Prepare(args, invalid, result, bad), "Aliased eyes must not share mutable model targets");
			Require(!shared.Prepare(args, invalid, result, std::span(targets.data(), 1)), "Incomplete target pair was accepted");
			ComPtr<ID3D11DeviceContext> deferred;
			Check(device->CreateDeferredContext(0, &deferred));
			for (unsigned contract = 0; contract < 5; ++contract) {
				auto malformed = args;
				if (contract == 0)
					malformed[1].featureSlot = malformed[0].featureSlot;
				else if (contract == 1)
					malformed[1].depthGuideSRV = nullptr;
				else if (contract == 2)
					malformed[1].depthGuideSRV = motion.srv.Get();
				else if (contract == 3)
					malformed[1].context = deferred.Get();
				else
					malformed[0].context = malformed[1].context = deferred.Get();
				Require(!shared.Prepare(malformed, invalid, result, targets) && result == E_INVALIDARG && invalid.count == 0,
					"Malformed shared-input batch reached preparation");
			}
			Require(Texture(targets[0].color, model).Read<Pixel>(device, context) == untouched,
				"Rejected batch changed a valid eye before preflight completed");
			ModelResolution::Batch mono;
			const auto monoArgs = std::span(args.data(), 1);
			Require(shared.Prepare(monoArgs, mono, result, std::span(targets.data(), 1)) && mono.count == 1,
				"Direct mono preparation failed");
			Require(Texture(targets[1].color, model).Read<Pixel>(device, context) == untouched,
				"Mono preparation touched the unused eye");
			Require(shared.Prepare(args, candidate, result, targets), "Direct shared-target preparation failed");
			Require(shared.AdditionalBytes(args, true) == 0 && shared.RetainedBytes(2).value() + shared.RetainedBytes(3).value() == retained,
				"Shared-target cache retains duplicate model-sized allocations");
			const auto previousColor = candidate.targets[0].color.resource;
			const auto previousReconstruction = candidate.resources[0];
			targets[0].color = own(Texture(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(count, sentinel)));
			Require(shared.Prepare(args, candidate, result, targets) && candidate.arguments[0].colorInput != previousColor.Get() &&
						candidate.resources[0] == previousReconstruction && shared.AdditionalBytes(args, true) == 0,
				"Replaced shared views must bind without retaining stale targets or reallocating reconstruction");
			const auto rect = baseline.arguments[0].computeSubrect;
			for (std::size_t eye = 0; eye < args.size(); ++eye) {
				Require(candidate.arguments[eye].colorInput == targets[eye].color.resource.Get() &&
							candidate.arguments[eye].colorOutput == targets[eye].output.resource.Get(),
					"Shared target identities were replaced");
				const auto oldColor = Texture(baseline.targets[eye].color, model).Read<Pixel>(device, context);
				const auto newColor = Texture(candidate.targets[eye].color, model).Read<Pixel>(device, context);
				const auto oldDepth = Texture(baseline.targets[eye].depth, model).Read<float>(device, context);
				const auto newDepth = Texture(candidate.targets[eye].depth, model).Read<float>(device, context);
				const auto oldMotion = Texture(baseline.targets[eye].motion, model).Read<Motion>(device, context);
				const auto newMotion = Texture(candidate.targets[eye].motion, model).Read<Motion>(device, context);
				std::vector<Pixel> neural(count, sentinel);
				for (UINT y = rect.baseY; y < rect.baseY + rect.height; ++y)
					for (UINT x = rect.baseX; x < rect.baseX + rect.width; ++x) {
						const auto i = y * model[0] + x;
						Require(oldColor[i] == newColor[i] && oldDepth[i] == newDepth[i] && oldMotion[i] == newMotion[i],
							"Direct preparation changed color, nearest depth or correlated motion");
						neural[i] = { newColor[i][0] + 0.025f, newColor[i][1] - 0.01f, newColor[i][2] + 0.005f, 1 };
					}
				context->UpdateSubresource(baseline.arguments[eye].colorOutput, 0, nullptr, neural.data(), static_cast<UINT>(model[0] * sizeof(Pixel)), 0);
				context->UpdateSubresource(candidate.arguments[eye].colorOutput, 0, nullptr, neural.data(), static_cast<UINT>(model[0] * sizeof(Pixel)), 0);
			}
			Require(staged.Reconstruct(args, baseline, result), "Staged reference reconstruction failed");
			staged.Commit(args, baseline);
			const auto expectedLeft = leftOutput.Read<Pixel>(device, context), expectedRight = rightOutput.Read<Pixel>(device, context);
			const std::vector clear(color.size(), sentinel);
			context->UpdateSubresource(leftOutput.texture.Get(), 0, nullptr, clear.data(), static_cast<UINT>(source[0] * sizeof(Pixel)), 0);
			context->UpdateSubresource(rightOutput.texture.Get(), 0, nullptr, clear.data(), static_cast<UINT>(source[0] * sizeof(Pixel)), 0);
			Require(shared.Reconstruct(monoArgs, mono, result), "Direct mono reconstruction failed");
			shared.Commit(monoArgs, mono);
			Require(leftOutput.Read<Pixel>(device, context) == expectedLeft && rightOutput.Read<Pixel>(device, context) == clear,
				"Direct mono output differs or publishes the unused eye");
			context->UpdateSubresource(leftOutput.texture.Get(), 0, nullptr, clear.data(), static_cast<UINT>(source[0] * sizeof(Pixel)), 0);
			shared.Reset();
			bad = {};
			invalid = {};
			targets = {};
			Require(shared.Reconstruct(args, candidate, result), "Retained shared targets failed across backend reset");
			Require(leftOutput.Read<Pixel>(device, context) == clear && rightOutput.Read<Pixel>(device, context) == clear,
				"Reconstruction published an eye before atomic pair commit");
			shared.Commit(args, candidate);
			Require(leftOutput.Read<Pixel>(device, context) == expectedLeft && rightOutput.Read<Pixel>(device, context) == expectedRight,
				"Shared transport changed residual color, alpha or owned ROI");
			for (unsigned policy = 0; policy < 4; ++policy) {
				auto masked = args;
				if (policy == 0)
					masked[1].actorSelection = motion.srv;
				else if (policy == 1)
					masked[1].controlMask = depth.texture;
				else if (policy == 2)
					masked[1].providerBlending = true;
				else
					masked[1].characterVisualIsolation = true;
				Require(!ModelResolution::CanUseSharedTargets(masked, false, false), "Actor and mask policies must retain staged semantics");
				Require(!shared.AdditionalBytes(masked, true), "Shared memory admission accepted an unsupported mask policy");
			}
		}
	}

	void CheckAdapter(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		using namespace NeuralRendering;
		constexpr Size source{ 13, 9 };
		const std::vector<Pixel> color(source[0] * source[1], Pixel{ 0.2f, 0.3f, 0.4f, 0.375f });
		const std::vector<Pixel> colorRight(color.size(), Pixel{ 0.6f, 0.15f, 0.1f, 0.625f });
		Texture input(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, color);
		Texture inputRight(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, colorRight);
		Texture depth(device, source, DXGI_FORMAT_R32_FLOAT, std::vector(color.size(), 0.5f));
		Texture motion(device, source, DXGI_FORMAT_R32G32_FLOAT, std::vector(color.size(), Motion{ 0.1f, -0.2f }));
		Texture outputLeft(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(color.size(), sentinel));
		Texture outputRight(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(color.size(), sentinel));
		RendererApplyArgs inputArgs;
		inputArgs.device = device;
		inputArgs.context = context;
		inputArgs.colorInput = input.texture.Get();
		inputArgs.depthGuide = depth.texture.Get();
		inputArgs.depthGuideSRV = depth.srv.Get();
		inputArgs.motionVectors = motion.texture.Get();
		inputArgs.colorOutput = outputLeft.texture.Get();
		inputArgs.colorWidth = inputArgs.guideWidth = inputArgs.outputWidth = source[0];
		inputArgs.colorHeight = inputArgs.guideHeight = inputArgs.outputHeight = source[1];
		inputArgs.viewportCrop = UpscalingDLSS::ViewportCrop::Identity(source[0], source[1], source[0], source[1]);
		inputArgs.computeSubrect = { 3, 2, 7, 5 };
		inputArgs.renderingMode = RenderingMode::ReducedResolution;
		inputArgs.modelResolutionPercent = 50;
		inputArgs.reset = true;
		std::array args{ inputArgs, inputArgs };
		args[1].featureSlot = 1;
		args[1].colorInput = inputRight.texture.Get();
		args[1].colorOutput = outputRight.texture.Get();
		ModelResolution adapter;
		ModelResolution::Batch batch;
		HRESULT result = S_OK;
		const auto estimate = adapter.AdditionalBytes(args);
		Require(estimate && *estimate > 0, "Proxy allocation must be admitted before preparation");
		Require(adapter.Prepare(args, batch, result), "Production proxy preparation failed");
		Require(adapter.AdditionalBytes(args) == 0, "Unchanged eye allocations must be reused");
		Require(adapter.RetainedBytes(0).value() + adapter.RetainedBytes(1).value() == *estimate,
			"Admission and retained-allocation accounting disagree");
		Require(batch.arguments[0].outputWidth == 7 && batch.arguments[0].outputHeight == 5,
			"Production adapter did not use the independent model grid");
		Require(batch.arguments[0].computeSubrect == ComputeSubrect{ 1, 1, 5, 3 },
			"Production adapter lost outward ROI coverage");
		for (const auto& eye : batch.arguments)
			context->CopyResource(eye.colorOutput, eye.colorInput);
		const auto unchanged = outputLeft.Read<Pixel>(device, context);
		Require(std::ranges::all_of(unchanged, [](const auto& pixel) { return pixel == sentinel; }),
			"Preparation exposed a private eye before pair reconstruction");
		const auto compilations = AdapterCompiler::calls;
		adapter.Reset();
		Require(adapter.RetainedBytes(0) == 0 && adapter.RetainedBytes(1) == 0, "Reset retained cached proxy allocations");
		Require(adapter.Reconstruct(args, batch, result), "An admitted batch must survive native backend reset");
		Require(AdapterCompiler::calls == compilations, "Backend reset unnecessarily recompiles an admitted batch's shaders");
		adapter.Commit(args, batch);
		for (const auto* output : { &outputLeft, &outputRight }) {
			const auto pixels = output->Read<Pixel>(device, context);
			for (UINT y = 0; y < source[1]; ++y)
				for (UINT x = 0; x < source[0]; ++x) {
					const bool owned = x >= 3 && x < 10 && y >= 2 && y < 7;
					const auto index = y * source[0] + x;
					for (UINT channel = 0; channel < 4; ++channel)
						Near(pixels[index][channel], owned ? (output == &outputLeft ? color : colorRight)[index][channel] : sentinel[channel],
							"Production commit changed identity colour, alpha, or pixels outside the owned ROI");
				}
		}
		ModelResolution::Batch incomplete;
		incomplete.count = args.size();
		Require(!adapter.Reconstruct(args, incomplete, result) && result == E_INVALIDARG,
			"An incomplete batch must fail before touching caller outputs");
		Texture mask(device, source, DXGI_FORMAT_R8_UNORM, std::vector<std::uint8_t>(color.size(), 255));
		for (auto& eye : args) {
			eye.controlMask = mask.texture;
			eye.controlMaskWidth = source[0];
			eye.controlMaskHeight = source[1];
			eye.tuning.useAutoMask = false;
		}
		Require(adapter.AdditionalBytes(args) == *estimate + 2u * 7u * 5u, "Mask allocations are missing from memory admission");
		Require(adapter.Prepare(args, batch, result), "Masked proxy recreation failed");
		Require(adapter.AdditionalBytes(args) == 0, "Masked proxy allocation was not retained");
		Require(adapter.RetainedBytes(0).value() + adapter.RetainedBytes(1).value() == *estimate + 2u * 7u * 5u,
			"Masked retained-memory accounting disagrees with admission");
		auto resized = args;
		resized[0].modelResolutionPercent = resized[1].modelResolutionPercent = 33;
		Require(adapter.AdditionalBytes(resized).value() > 0, "A changed model grid reused incompatible allocations");
		auto invalid = args;
		invalid[1].viewportCrop = {};
		Require(!adapter.AdditionalBytes(invalid) && !adapter.Prepare(invalid, incomplete, result),
			"Invalid right-eye geometry must fail before proxy allocation or dispatch");
		args[0].modelResolutionPercent = args[1].modelResolutionPercent = 100;
		adapter.ReleaseUnscaledSlots(args);
		Require(adapter.RetainedBytes(0) == 0 && adapter.RetainedBytes(1) == 0, "100% retained downscaling caches");
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
		AdapterCompiler::device = device.Get();
		for (bool vr : { false, true }) {
			AdapterCompiler::vr = vr;
			CheckAdapter(device.Get(), context.Get());
			CheckSharedAdapter(device.Get(), context.Get());
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
		std::cout << "PASS: NR production adapter and shaders on WARP: shared/staged pixel equivalence, target preflight, alias rejection, batch reset ownership, allocation reuse, stereo commit, area reduction, guide/mask ownership, odd sizes, identity, residual detail, alpha, ROI isolation and finite fallback (flat/VR)\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
