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

#ifdef DEVBENCH_BRIDGE_ENABLED
// Standalone shader tests keep game-thread diagnostics inert.
namespace CSX::Diagnostics::Stutters
{
	Scope::Scope(std::string_view, Boundary) noexcept {}
	Scope::~Scope() noexcept = default;
}
#endif

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
		Require(constants.bytes.size() == 112, "Model resolution cbuffer layout changed");
		constants.SetVariable("SourceSize", source);
		constants.SetVariable("ModelSize", model);
		constants.SetVariable("SourceRegionOffset", sourceRegion.offset);
		constants.SetVariable("SourceRegionSize", sourceRegion.size);
		constants.SetVariable("ModelRegionOffset", modelRegion.offset);
		constants.SetVariable("ModelRegionSize", modelRegion.size);
		constants.SetVariable("HasMask", UINT(hasMask));
		constants.SetVariable("OutputFormat", 0u);
		constants.SetVariable("MotionVectorScale", motionScale);
		constants.SetVariable("CentralActive", 0u);
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
		for (auto mode : { RenderingMode::FullResolution, RenderingMode::Foveated, RenderingMode::ReducedResolution }) {
			input.renderingMode = mode;
			input.reset = mode == RenderingMode::ReducedResolution;
			for (unsigned percent : { 90u, 80u, 50u, 33u, 30u }) {
				input.modelResolutionPercent = percent;
				std::array args{ input, input };
				args[1].featureSlot = 3;
				args[1].colorInput = rightInput.texture.Get();
				args[1].colorOutput = rightOutput.texture.Get();
				Require(ModelResolution::CanUseSharedTargets(args, false, false), "Raw A/B/C inputs must accept shared preparation");
				Require(!ModelResolution::CanUseSharedTargets(args, true, false) && !ModelResolution::CanUseSharedTargets(args, false, true),
					"Color processing and compact recovery must retain staged inputs");
				ModelResolution staged, shared;
				RendererApplyArgs projected;
				Require(ModelResolution::Project(args[0], projected) && projected.reset == input.reset &&
							projected.modelResolutionHistory == ModelResolutionHistory{ input.viewportCrop, percent, input.computeSubrect },
					"Projection changed temporal policy or lost the original crop and scale");
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
	}

	void CheckUpscaledGuides(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		using namespace NeuralRendering;
		constexpr Size source{ 17, 11 }, guides{ 9, 6 };
		const std::vector<Pixel> colors(source[0] * source[1], Pixel{ 0.25f, 0.5f, 0.75f, 0.375f });
		std::vector<float> depths(guides[0] * guides[1]);
		std::vector<Motion> velocities(depths.size());
		for (std::size_t i = 0; i < depths.size(); ++i) {
			depths[i] = float((i * 7) % 53) / 53.0f;
			velocities[i] = { float(i) * 0.01f, -float(i) * 0.02f };
		}
		Texture color(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, colors);
		Texture depth(device, guides, DXGI_FORMAT_R32_FLOAT, depths);
		Texture motion(device, guides, DXGI_FORMAT_R32G32_FLOAT, velocities);
		Texture output(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(colors.size(), sentinel));
		RendererApplyArgs input;
		input.device = device;
		input.context = context;
		input.colorInput = color.texture.Get();
		input.depthGuide = depth.texture.Get();
		input.depthGuideSRV = depth.srv.Get();
		input.motionVectors = motion.texture.Get();
		input.colorOutput = output.texture.Get();
		input.colorWidth = input.outputWidth = source[0];
		input.colorHeight = input.outputHeight = source[1];
		input.guideWidth = guides[0];
		input.guideHeight = guides[1];
		input.viewportCrop = { .fullInput = { 20, 14 }, .input = { 3, 2, 12, 8 }, .fullOutput = { 40, 28 }, .output = { 6, 4, 23, 15 } };
		input.computeSubrect = { 3, 2, 11, 7 };
		input.featureUpscaling = true;
		for (auto mode : { RenderingMode::FullResolution, RenderingMode::Foveated }) {
			input.renderingMode = mode;
			for (unsigned percent : { 30u, 50u, 90u }) {
				input.modelResolutionPercent = percent;
				const auto args = std::span(&input, 1);
				ModelResolution adapter;
				ModelResolution::Batch batch;
				HRESULT result = S_OK;
				const auto colorExtent = BuildModelResolutionExtent(source[0], source[1], percent);
				const auto guideExtent = BuildModelResolutionExtent(guides[0], guides[1], percent);
				const auto expectedBytes = std::uint64_t(source[0]) * source[1] * sizeof(Pixel) +
				                           std::uint64_t(colorExtent.width) * colorExtent.height * 2 * sizeof(Pixel) +
				                           std::uint64_t(guideExtent.width) * guideExtent.height * (sizeof(float) + sizeof(Motion));
				Require(adapter.AdditionalBytes(args) == expectedBytes, "A/B admission did not account for the reduced guide grid");
				Require(adapter.Prepare(args, batch, result), "Final-scene NR rejected lower-resolution guides");
				ModelResolution shared;
				ModelResolution::Batch direct;
				Require(ModelResolution::CanUseSharedTargets(args, false, false) &&
							shared.Prepare(args, direct, result, std::span(batch.targets.data(), 1)),
					"Shared A/B preparation rejected proportional guide targets");
				const auto& projected = batch.arguments[0];
				Require(!projected.reset && projected.featureUpscaling &&
							projected.modelResolutionHistory == ModelResolutionHistory{ input.viewportCrop, percent, input.computeSubrect },
					"Scaled final-scene inputs lost temporal history or original sampling identity");
				const Size model{ projected.outputWidth, projected.outputHeight };
				const Size modelGuides{ projected.guideWidth, projected.guideHeight };
				Require(modelGuides[0] == (guides[0] * percent + 99) / 100 && modelGuides[1] == (guides[1] * percent + 99) / 100,
					"A/B guides must scale from their original dimensions");
				const auto sampledDepth = Texture(batch.targets[0].depth, modelGuides).Read<float>(device, context);
				const auto sampledMotion = Texture(batch.targets[0].motion, modelGuides).Read<Motion>(device, context);
				const auto rect = MapComputeSubrect(projected.computeSubrect, model[0], model[1], modelGuides[0], modelGuides[1]);
				const auto guideSource = MapComputeSubrect(input.computeSubrect, source[0], source[1], guides[0], guides[1]);
				for (UINT y = rect.baseY; y < rect.baseY + rect.height; ++y) {
					for (UINT x = rect.baseX; x < rect.baseX + rect.width; ++x) {
						const float lowX = std::max(float(x) * guides[0] / modelGuides[0], float(guideSource.baseX));
						const float lowY = std::max(float(y) * guides[1] / modelGuides[1], float(guideSource.baseY));
						const float highX = std::min(float(x + 1) * guides[0] / modelGuides[0], float(guideSource.baseX + guideSource.width));
						const float highY = std::min(float(y + 1) * guides[1] / modelGuides[1], float(guideSource.baseY + guideSource.height));
						std::size_t nearest = 0;
						float expectedDepth = 1.0f;
						for (UINT gy = UINT(std::floor(lowY)); gy < std::min(UINT(std::ceil(highY)), guides[1]); ++gy)
							for (UINT gx = UINT(std::floor(lowX)); gx < std::min(UINT(std::ceil(highX)), guides[0]); ++gx) {
								const auto i = gy * guides[0] + gx;
								if (depths[i] < expectedDepth) {
									expectedDepth = depths[i];
									nearest = i;
								}
							}
						const auto i = y * modelGuides[0] + x;
						Near(sampledDepth[i], expectedDepth, "Scaled NR read depth in colour coordinates");
						Near(sampledMotion[i][0], velocities[nearest][0] * (20.0f / 9.0f), "Scaled guide X motion lost its crop basis");
						Near(sampledMotion[i][1], velocities[nearest][1] * (14.0f / 6.0f), "Scaled guide Y motion lost its crop basis");
					}
				}
				const auto previousColor = batch.targets[0].color.resource;
				const auto history = projected.modelResolutionHistory;
				++input.viewportCrop.input.left;
				++input.viewportCrop.input.right;
				Require(adapter.Prepare(args, batch, result) && batch.targets[0].color.resource == previousColor &&
							batch.arguments[0].modelResolutionHistory != history,
					"Crop movement lost history invalidation or rebuilt unchanged capacity");
				--input.viewportCrop.input.left;
				--input.viewportCrop.input.right;
				context->CopyResource(batch.arguments[0].colorOutput, batch.arguments[0].colorInput);
				Require(adapter.Reconstruct(args, batch, result), "Final-scene residual reconstruction failed");
				adapter.Commit(args, batch);
				const auto pixels = output.Read<Pixel>(device, context);
				for (UINT y = 0; y < source[1]; ++y)
					for (UINT x = 0; x < source[0]; ++x) {
						const auto i = y * source[0] + x;
						Require(pixels[i] == (x >= 3 && x < 14 && y >= 2 && y < 9 ? colors[i] : sentinel),
							"Scaled A/B reconstruction changed source detail, alpha or pixels outside the crop");
					}
			}
		}
		input.modelResolutionPercent = 30;
		input.computeSubrect = { 3, 2, 11, 7 };
		RendererApplyArgs projected, moved;
		Require(ModelResolution::Project(input, projected), "Final-scene projection failed");
		++input.computeSubrect.baseX;
		--input.computeSubrect.width;
		Require(ModelResolution::Project(input, moved) && moved.computeSubrect == projected.computeSubrect &&
					moved.modelResolutionHistory != projected.modelResolutionHistory,
			"Source-region changes inside one model rectangle must invalidate temporal history");
		Texture selection(device, source, DXGI_FORMAT_R8_UNORM, std::vector<std::uint8_t>(colors.size(), 255));
		Texture replacementSelection(device, source, DXGI_FORMAT_R8_UNORM, std::vector<std::uint8_t>(colors.size(), 255));
		input.controlMask = selection.texture;
		input.actorSelection = selection.srv;
		input.controlMaskWidth = source[0];
		input.controlMaskHeight = source[1];
		input.providerBlending = input.characterVisualIsolation = true;
		input.actorSelectionSupport = input.computeSubrect;
		input.roi = BuildRoiDescriptor(input.computeSubrect, input.computeSubrect, { source[0], source[1] }, true);
		ModelResolution masked;
		ModelResolution::Batch first, second;
		HRESULT result = S_OK;
		const auto maskedArgs = std::span(&input, 1);
		Require(!ModelResolution::CanUseSharedTargets(maskedArgs, false, false) && masked.Prepare(maskedArgs, first, result),
			"Scaled temporal actor inputs must retain the staged mask path");
		input.controlMask = replacementSelection.texture;
		input.actorSelection = replacementSelection.srv;
		Require(masked.Prepare(maskedArgs, second, result) && first.arguments[0].controlMask == second.arguments[0].controlMask &&
					first.arguments[0].modelResolutionHistory != second.arguments[0].modelResolutionHistory,
			"Replacing the source mask must invalidate history even when the model mask is reused");
		Require(second.arguments[0].actorSelection && second.arguments[0].providerBlending &&
					second.arguments[0].actorSelectionSupport == second.arguments[0].computeSubrect &&
					second.arguments[0].roi->temporalEnvelope == second.arguments[0].computeSubrect,
			"Scaled temporal actor preparation lost selection, provider blending or ROI roles");
		input.controlMask.Reset();
		input.actorSelection.Reset();
		input.providerBlending = input.characterVisualIsolation = false;
		input.controlMaskWidth = input.controlMaskHeight = 0;
		input.roi.reset();
		input.computeSubrect = { 8, 1, 1, 1 };
		input.modelResolutionPercent = 90;
		ModelResolution narrow;
		ModelResolution::Batch narrowBatch;
		Require(narrow.Prepare(std::span(&input, 1), narrowBatch, result), "Narrow A/B preparation failed");
		const auto& narrowArgs = narrowBatch.arguments[0];
		const auto narrowGuides = MapComputeSubrect(narrowArgs.computeSubrect, narrowArgs.outputWidth, narrowArgs.outputHeight,
			narrowArgs.guideWidth, narrowArgs.guideHeight);
		Require(narrowGuides.width > narrowArgs.computeSubrect.width, "Narrow guide dispatch fixture lost outward rounding");
		const auto narrowDepth = Texture(narrowBatch.targets[0].depth, { narrowArgs.guideWidth, narrowArgs.guideHeight }).Read<float>(device, context);
		for (UINT y = narrowGuides.baseY; y < narrowGuides.baseY + narrowGuides.height; ++y)
			for (UINT x = narrowGuides.baseX; x < narrowGuides.baseX + narrowGuides.width; ++x)
				Near(narrowDepth[y * narrowArgs.guideWidth + x], x == 4 && y < 2 ? depths[y * guides[0] + x] : 1.0f,
					"Outward guide coverage exceeded the colour dispatch or retained stale padding");
		input.renderingMode = RenderingMode::ReducedResolution;
		Require(!ModelResolution::Project(input, projected), "C admitted temporal lower-guide inputs");
		input.renderingMode.reset();
		Require(!ModelResolution::Project(input, projected), "Missing route admitted scaled model inputs");
	}

	void CheckCentralArea(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		using namespace NeuralRendering;
		constexpr Size source{ 321, 241 };
		const Pixel baseline{ 0.2f, 0.3f, 0.4f, 0.375f };
		const std::vector<Pixel> colors(source[0] * source[1], baseline);
		Texture color(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, colors);
		Texture depth(device, source, DXGI_FORMAT_R32_FLOAT, std::vector(colors.size(), 0.5f));
		Texture motion(device, source, DXGI_FORMAT_R32G32_FLOAT, std::vector(colors.size(), Motion{ 0.01f, -0.02f }));
		Texture left(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, colors);
		Texture right(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, colors);
		Texture mask(device, source, DXGI_FORMAT_R8_UNORM, std::vector<std::uint8_t>(colors.size(), 255));
		RendererApplyArgs input;
		input.device = device;
		input.context = context;
		input.colorInput = color.texture.Get();
		input.depthGuide = depth.texture.Get();
		input.depthGuideSRV = depth.srv.Get();
		input.motionVectors = motion.texture.Get();
		input.colorOutput = left.texture.Get();
		input.colorWidth = input.guideWidth = input.outputWidth = source[0];
		input.colorHeight = input.guideHeight = input.outputHeight = source[1];
		input.viewportCrop = { { 401, 301 }, { 39, 21, 360, 262 }, { 401, 301 }, { 39, 21, 360, 262 } };
		input.computeSubrect = { 0, 0, source[0], source[1] };
		input.centralArea = { 25, 64, 1.2f, {}, { 401, 301 } };
		ModelResolution adapter;
		for (const auto mode : { RenderingMode::FullResolution, RenderingMode::Foveated, RenderingMode::ReducedResolution }) {
			input.renderingMode = mode;
			input.reset = mode == RenderingMode::ReducedResolution;
			input.centralArea.finalOutput = mode == RenderingMode::ReducedResolution ? UpscalingDLSS::Extent{ 1604, 1204 } : UpscalingDLSS::Extent{ 401, 301 };
			for (const unsigned percent : { 100u, 80u, 30u }) {
				input.modelResolutionPercent = percent;
				for (const unsigned feather : { 0u, 64u, 256u }) {
					input.centralArea.featherPixels = feather;
					std::array args{ input, input };
					args[0].centralArea.offset = { -0.08f, 0.03f };
					args[1].centralArea.offset = { 0.09f, -0.02f };
					args[1].featureSlot = 1;
					args[1].colorOutput = right.texture.Get();
					HRESULT result = S_OK;
					ModelResolution::Batch batch;
					Require(ModelResolution::Required(input) && ModelResolution::CanUseSharedTargets(args, false, false), "Independent central area lost the raw shared path at 100% model resolution");
					Require(adapter.Prepare(args, batch, result), "Central area preparation failed");
					const Size model{ batch.arguments[0].outputWidth, batch.arguments[0].outputHeight };
					for (unsigned eye = 0; eye < 2; ++eye) {
						const auto& native = batch.arguments[eye];
						Require(!native.centralArea.Active() && native.modelResolutionHistory->centralArea == args[eye].centralArea,
							"Projected central mask lost temporal identity or recursively reapplied the feather");
						if (feather == 0 && percent == 100)
							Require(native.computeSubrect.Area() < std::uint64_t(model[0]) * model[1], "Central area did not reduce native work");
						const float nan = std::numeric_limits<float>::quiet_NaN();
						std::vector<Pixel> evaluated(model[0] * model[1], Pixel{ nan, nan, nan, nan });
						const auto r = native.computeSubrect;
						for (unsigned y = r.baseY; y < r.baseY + r.height; ++y)
							for (unsigned x = r.baseX; x < r.baseX + r.width; ++x)
								evaluated[y * model[0] + x] = { baseline[0] + 0.1f, baseline[1] + 0.1f, baseline[2] + 0.1f, 1.0f };
						context->UpdateSubresource(native.colorOutput, 0, nullptr, evaluated.data(), model[0] * sizeof(Pixel), 0);
					}
					Require(adapter.Reconstruct(args, batch, result), "Central area private reconstruction failed");
					adapter.Commit(args, batch);
					for (unsigned eye = 0; eye < 2; ++eye) {
						const auto values = (eye ? right : left).Read<Pixel>(device, context);
						const auto& area = args[eye].centralArea;
						bool sawFull = false, sawOutside = false, sawFeather = false;
						for (unsigned y = 0; y < source[1]; ++y)
							for (unsigned x = 0; x < source[0]; ++x) {
								const float uvX = (float(x) + 39.5f) / 401.0f, uvY = (float(y) + 21.5f) / 301.0f;
								const float distance = FoveatedCommon::MaskDistanceUV(uvX, uvY, area.Scale(), area.horizontalScale, area.offset[0], area.offset[1]);
								const float radiusX = area.Scale() * area.horizontalScale * 0.5f, radiusY = area.Scale() * 0.5f;
								const float nx = std::abs((uvX - std::clamp(0.5f + area.offset[0], 0.0f, 1.0f)) / radiusX);
								const float ny = std::abs((uvY - std::clamp(0.5f + area.offset[1], 0.0f, 1.0f)) / radiusY);
								const float gx = nx * nx * nx / (radiusX * area.finalOutput.width), gy = ny * ny * ny / (radiusY * area.finalOutput.height);
								const float edgeDistance = distance > 1 ? (distance - 1) * distance * distance * distance / std::hypot(gx, gy) : 0;
								const float t = feather > 0 ? std::clamp(edgeDistance / feather, 0.0f, 1.0f) : (distance <= 1 ? 0.0f : 1.0f);
								const float weight = 1.0f - t * t * (3.0f - 2.0f * t);
								sawFull |= weight == 1;
								sawOutside |= weight == 0;
								sawFeather |= weight > 0 && weight < 1;
								const auto& pixel = values[y * source[0] + x];
								for (unsigned c = 0; c < 3; ++c) Near(pixel[c], baseline[c] + 0.1f * weight, "Central mask moved, lost its FOV shape, feathered twice or read unevaluated texels");
								Near(pixel[3], baseline[3], "Central feather changed source alpha");
							}
						Require(sawFull && (feather == 256 || sawOutside) && (feather == 0 || sawFeather), "Central mask fixture did not exercise the core, boundary and feather");
					}
					auto changed = args[0];
					changed.centralArea.offset[0] += 0.001f;
					RendererApplyArgs projected;
					Require(ModelResolution::Project(changed, projected) && projected.modelResolutionHistory != batch.arguments[0].modelResolutionHistory,
						"Centre movement reused history just because model allocations fit");
					changed = args[0];
					changed.centralArea.featherPixels = feather == 256 ? 255 : feather + 1;
					Require(ModelResolution::Project(changed, projected) && projected.modelResolutionHistory != batch.arguments[0].modelResolutionHistory,
						"Feather changes reused incompatible history");
				}
			}
		}
		input.modelResolutionPercent = 80;
		input.centralArea = { 25, 0, 1.0f, {}, { 401, 301 } };
		input.characterVisualIsolation = true;
		input.controlMask = mask.texture;
		input.actorSelection = mask.srv;
		input.controlMaskWidth = source[0];
		input.controlMaskHeight = source[1];
		const ComputeSubrect actor{ 0, 0, 8, 8 };
		input.roi = BuildRoiDescriptor(actor, input.computeSubrect, { source[0], source[1] }, true);
		input.actorSelectionSupport = actor;
		RendererApplyArgs projected;
		Require(ModelResolution::Project(input, projected) && projected.roi->samplingSupport &&
					GetRoiDescriptorViolation(*projected.roi, projected.computeSubrect, { projected.outputWidth, projected.outputHeight }).empty(),
			"A disjoint actor selection lost its prepared nonempty ROI contract");
		input.centralArea.percent = 100;
		input.modelResolutionPercent = 100;
		Require(!ModelResolution::Required(input), "100% central area and model scale must retain the original route");
		input.centralArea.featherPixels = 257;
		Require(ModelResolution::Required(input) && !ModelResolution::Project(input, projected), "Invalid feather was allowed past the renderer boundary");
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
			CheckCentralArea(device.Get(), context.Get());
			CheckUpscaledGuides(device.Get(), context.Get());
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
