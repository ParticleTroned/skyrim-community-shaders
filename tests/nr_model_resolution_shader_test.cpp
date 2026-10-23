#include "Features/Upscaling/NeuralRendering/ModelResolution.h"
#include "GpuPass.h"
#include "ShaderPackageIncludes.h"
#include "Utils/ShaderCompiler.h"
#include "d3d11_shader_test.h"
#include "nr_model_resolution_accounting_under_test.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

template <class T>
concept HasPackedCropDiagnosticField = requires(T value) { value.packedCropPlanning; };
#ifdef DEVBENCH_BRIDGE_ENABLED
static_assert(HasPackedCropDiagnosticField<NeuralRendering::RendererApplyArgs>);
#else
static_assert(!HasPackedCropDiagnosticField<NeuralRendering::RendererApplyArgs>);
#endif

namespace AdapterCompiler
{
	ID3D11Device* device = nullptr;
	unsigned calls = 0;
	bool vr = false;
	bool failPacking = false;
	unsigned packingAttempts = 0;
}

// Substitute only engine compilation/profiling; the adapter and GPU work are production code.
ID3D11DeviceChild* Util::CompileShader(const wchar_t* path,
	const std::vector<std::pair<const char*, const char*>>& options, const char* target, const char* entry, ShaderCompileTiming*)
{
	++AdapterCompiler::calls;
	if (std::filesystem::path(path).filename() == L"ModelResolutionCoverageCS.hlsl") {
		++AdapterCompiler::packingAttempts;
		if (AdapterCompiler::failPacking)
			return nullptr;
	}
	const auto shaderPath = std::filesystem::path("features/Neural Rendering/Shaders/Upscaling/NeuralRendering") /
	                        std::filesystem::path(path).filename();
	PackageIncludes includes("package/Shaders", "features/Neural Rendering/Shaders");
	std::vector<D3D_SHADER_MACRO> defines;
	for (const auto& [name, value] : options)
		defines.push_back({ name, value });
	if (AdapterCompiler::vr)
		defines.push_back({ "VR", "1" });
	defines.push_back({ nullptr, nullptr });
	Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
	const auto result = D3DCompileFromFile(shaderPath.c_str(), defines.data(), &includes,
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

		Shader(ID3D11Device* device, const wchar_t* path, bool vr, const char* featureRoot = "features/Neural Rendering/Shaders")
		{
			PackageIncludes includes("package/Shaders", featureRoot);
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
		Require(constants.bytes.size() == 352, "Model resolution cbuffer layout changed");
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
		constants.SetVariable("PackedCount", 0u);
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
	void CheckSharedAdapter(ID3D11Device* device, ID3D11DeviceContext* context, bool guidesOnly)
	{
		using namespace NeuralRendering;
		const auto sharing = guidesOnly ? ModelResolution::Sharing::Guides : ModelResolution::Sharing::All;
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
				Require(ModelResolution::CanUseSharedTargets(args, false), "Raw A/B/C inputs must accept shared preparation");
				Require(!ModelResolution::CanUseSharedTargets(args, true), "Compact recovery must retain staged inputs");
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
					if (guidesOnly) {
						target.color = {};
						target.output = {};
					}
				}
				const auto retained = (2u * source[0] * source[1] + (guidesOnly ? 4u * count : 0u)) * sizeof(Pixel);
				Require(shared.AdditionalBytes(args, sharing) == retained, "Shared aliases must not enter private allocation admission");
				auto bad = targets;
				bad[1].motion.srv = bad[1].depth.srv;
				Require(!shared.Prepare(args, invalid, result, bad) && result == E_INVALIDARG && invalid.count == 0,
					"Mismatched right-eye view must reject the whole batch before preparation");
				const auto untouched = Texture(targets[0].depth, model).Read<float>(device, context);
				Require(std::ranges::all_of(untouched, [](const auto& value) { return value == -1.0f; }),
					"Invalid right-eye target dispatched left-eye preparation");
				for (bool selectionSource : { false, true }) {
					auto aliased = args;
					if (selectionSource)
						aliased[1].actorSelection = targets[0].depth.srv;
					else
						aliased[1].controlMask = targets[0].depth.resource;
					Require(!shared.Prepare(aliased, invalid, result, targets) && invalid.count == 0,
						"An actor source aliased a mutable shared model target");
					Require(Texture(targets[0].depth, model).Read<float>(device, context) == untouched,
						"Aliased actor input dispatched the other eye before rejection");
				}
				bad = targets;
				bad[1].color = guidesOnly ? own(Texture(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(count, sentinel))) : Color::Texture{};
				Require(!shared.Prepare(args, invalid, result, bad), "Mixed colour ownership must reject both eyes");
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
				Require(Texture(targets[0].depth, model).Read<float>(device, context) == untouched,
					"Rejected batch changed a valid eye before preflight completed");
				ModelResolution::Batch mono;
				const auto monoArgs = std::span(args.data(), 1);
				Require(shared.Prepare(monoArgs, mono, result, std::span(targets.data(), 1)) && mono.count == 1,
					"Direct mono preparation failed");
				Require(Texture(targets[1].depth, model).Read<float>(device, context) == untouched,
					"Mono preparation touched the unused eye");
				Require(shared.Prepare(args, candidate, result, targets), "Direct shared-target preparation failed");
				Require(shared.AdditionalBytes(args, sharing) == 0 && shared.RetainedBytes(2).value() + shared.RetainedBytes(3).value() == retained,
					"Shared-target cache retains duplicate model-sized allocations");
				const auto previousMotion = candidate.targets[0].motion.resource;
				const auto previousReconstruction = candidate.resources[0];
				targets[0].motion = own(Texture(device, model, DXGI_FORMAT_R32G32_FLOAT, std::vector(count, Motion{ -1, -1 })));
				Require(shared.Prepare(args, candidate, result, targets) && candidate.arguments[0].motionVectors != previousMotion.Get() &&
							candidate.resources[0] == previousReconstruction && shared.AdditionalBytes(args, sharing) == 0,
					"Replaced shared views must bind without retaining stale targets or reallocating reconstruction");
				const auto rect = baseline.arguments[0].computeSubrect;
				for (std::size_t eye = 0; eye < args.size(); ++eye) {
					Require(candidate.arguments[eye].depthGuide == targets[eye].depth.resource.Get() &&
								candidate.arguments[eye].motionVectors == targets[eye].motion.resource.Get() &&
								(guidesOnly ? candidate.targets[eye].color.resource && candidate.targets[eye].output.resource :
											  candidate.arguments[eye].colorInput == targets[eye].color.resource.Get() && candidate.arguments[eye].colorOutput == targets[eye].output.resource.Get()),
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
				const auto retainedBeforeReload = shared.RetainedBytes(args[0].featureSlot);
				shared.InvalidateShaders();
				Require(shared.RetainedBytes(args[0].featureSlot) == retainedBeforeReload,
					"Shader invalidation retired compatible model work");
				Require(shared.Reconstruct(args, candidate, result), "Retained batch lost shader/constants across invalidation");
				Require(leftOutput.Read<Pixel>(device, context) == clear && rightOutput.Read<Pixel>(device, context) == clear,
					"Shader invalidation or reconstruction committed a partial pair");
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
				Texture actorMask(device, source, DXGI_FORMAT_R8_UNORM, std::vector<std::uint8_t>(color.size(), 128));
				for (bool providerBlending : { false, true }) {
					auto actor = args;
					for (auto& eye : actor) {
						eye.characterVisualIsolation = true;
						if (providerBlending) {
							eye.controlMask = actorMask.texture;
							eye.actorSelection = actorMask.srv;
							eye.controlMaskWidth = source[0];
							eye.controlMaskHeight = source[1];
							eye.actorSelectionSupport = eye.computeSubrect;
							eye.providerBlending = true;
						}
					}
					Require(ModelResolution::CanUseSharedTargets(actor, false), "Raw actors lost shared model preparation");
					const auto expectedBytes = retained + (providerBlending ? 2u * model[0] * model[1] : 0u);
					Require(shared.AdditionalBytes(actor, sharing) == expectedBytes,
						"Shared actor accounting lost the private positive mask or counted aliased model textures");
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
				Require(ModelResolution::CanUseSharedTargets(args, false) &&
							shared.Prepare(args, direct, result, std::span(batch.targets.data(), 1)),
					"Shared A/B preparation rejected proportional guide targets");
				const std::array guideTargets{ ModelResolution::Targets{ {}, batch.targets[0].depth, batch.targets[0].motion, {} } };
				Require(shared.Prepare(args, direct, result, guideTargets) &&
							direct.arguments[0].depthGuide == batch.arguments[0].depthGuide &&
							direct.arguments[0].colorInput != batch.arguments[0].colorInput &&
							shared.RetainedBytes(input.featureSlot) == expectedBytes - std::uint64_t(guideExtent.width) * guideExtent.height * (sizeof(float) + sizeof(Motion)),
					"Guide sharing lost independent guide dimensions, private colour or replacement accounting");
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
		Require(ModelResolution::CanUseSharedTargets(maskedArgs, false) && masked.Prepare(maskedArgs, first, result),
			"Scaled temporal actors lost shared eligibility or private selection preparation");
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
					Require(ModelResolution::Required(input) && ModelResolution::CanUseSharedTargets(args, false), "Independent central area lost the raw shared path at 100% model resolution");
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

	Pixel PackedColorReference(const std::vector<Pixel>& source, Size size, const NeuralRendering::PackedCropLayout& layout, unsigned tileIndex, unsigned x, unsigned y)
	{
		if (tileIndex == layout.count)
			return {};
		const auto& tile = layout.tiles[tileIndex];
		const double mx = x - tile.packed.baseX + tile.model.baseX, my = y - tile.packed.baseY + tile.model.baseY;
		const double left = std::max(double(tile.source.baseX), mx * size[0] / layout.unpackedExtent.width);
		const double top = std::max(double(tile.source.baseY), my * size[1] / layout.unpackedExtent.height);
		const double right = std::min(double(tile.source.baseX + tile.source.width), (mx + 1) * size[0] / layout.unpackedExtent.width);
		const double bottom = std::min(double(tile.source.baseY + tile.source.height), (my + 1) * size[1] / layout.unpackedExtent.height);
		std::array<double, 4> sum{};
		for (unsigned sy = unsigned(std::floor(top)); sy < unsigned(std::ceil(bottom)); ++sy)
			for (unsigned sx = unsigned(std::floor(left)); sx < unsigned(std::ceil(right)); ++sx) {
				const double weight = (std::min(right, double(sx + 1)) - std::max(left, double(sx))) *
				                      (std::min(bottom, double(sy + 1)) - std::max(top, double(sy)));
				for (unsigned channel = 0; channel < 4; ++channel)
					sum[channel] += source[sy * size[0] + sx][channel] * weight;
			}
		Pixel result{};
		for (unsigned channel = 0; channel < 4; ++channel)
			result[channel] = float(sum[channel] / ((right - left) * (bottom - top)));
		return result;
	}

	void CheckPackedAdapter(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		using namespace NeuralRendering;
		RendererApplyArgs central;
		central.colorWidth = central.outputWidth = central.guideWidth = 2048;
		central.colorHeight = central.outputHeight = central.guideHeight = 1024;
		central.viewportCrop = UpscalingDLSS::ViewportCrop::Identity(2048, 1024, 2048, 1024);
		central.computeSubrect = { 0, 0, 2048, 1024 };
		central.renderingMode = RenderingMode::ReducedResolution;
		central.reset = central.characterVisualIsolation = true;
		central.packedCrops = { { ComputeSubrect{ 32, 100, 800, 800 }, ComputeSubrect{ 1216, 100, 800, 800 } }, 2 };
		RendererApplyArgs projectedCentral;
		Require(ModelResolution::Project(central, projectedCentral) && projectedCentral.modelResolutionHistory->packedCrops.count,
			"Sparse baseline fixture must admit packing");
		central.centralArea.percent = 25;
		central.centralArea.finalOutput = { 2048, 1024 };
		Require(ModelResolution::Project(central, projectedCentral) && !projectedCentral.modelResolutionHistory->packedCrops.count,
			"Atlas admission ignored the cheaper existing central-area crop");
#ifdef DEVBENCH_BRIDGE_ENABLED
		PackedCropAdmissionDiagnostics admission;
		Require(ModelResolution::Project(central, projectedCentral, &admission) &&
					admission.reason == PackedCropReason::InsufficientAreaSaving && admission.stage == PackedCropStage::AreaAdmission &&
					admission.candidateAreaAvailable && admission.modelAreasAvailable &&
					admission.candidateModelPixels * 4u > admission.effectiveBaselineModelPixels * 3u,
			"Central-area atlas rejection discarded the actual candidate/baseline areas");
		central.centralArea = {};
		admission = {};
		Require(ModelResolution::Project(central, projectedCentral, &admission) &&
					admission.reason == PackedCropReason::Admitted && admission.stage == PackedCropStage::Admitted,
			"Valid projected atlas lost its final admission reason");
		central.colorWidth -= 1;
		admission = {};
		Require(!ModelResolution::Project(central, projectedCentral, &admission) &&
					admission.reason == PackedCropReason::InvalidProjection && admission.stage == PackedCropStage::ModelProjection,
			"Invalid projection retained a misleading admitted result");
		auto provider = central;
		provider.colorWidth = provider.outputWidth;
		provider.guideWidth -= 1;
		provider.renderingMode = RenderingMode::FullResolution;
		provider.featureUpscaling = true;
		provider.viewportCrop = UpscalingDLSS::ViewportCrop::Identity(2047, 1024, 2048, 1024);
		provider.packedCrops = { { ComputeSubrect{ 0, 0, 64, 64 }, ComputeSubrect{ 1984, 0, 64, 64 } }, 2 };
		admission = {};
		Require(ModelResolution::Project(provider, projectedCentral, &admission) &&
					projectedCentral.modelResolutionHistory->packedCrops.count && projectedCentral.featureUpscaling &&
					projectedCentral.guideWidth == 2047 && projectedCentral.outputWidth == 2048 &&
					admission.reason == PackedCropReason::Admitted && admission.candidateAreaAvailable,
			"Atlas changed near-unity native creation geometry or feature type");
#endif
		constexpr Size source{ 1025, 519 };
		constexpr ComputeSubrect owned{ 8, 8, 1009, 503 };
		const PackedCropSources crops{ { ComputeSubrect{ 32, 40, 320, 320 }, ComputeSubrect{ 677, 151, 320, 320 } }, 2 };
		const auto contains = [](ComputeSubrect rect, float x, float y) {
			return x >= rect.baseX && y >= rect.baseY && x < rect.baseX + rect.width && y < rect.baseY + rect.height;
		};
		for (auto mode : { RenderingMode::FullResolution, RenderingMode::ReducedResolution }) {
			const Size guides = mode == RenderingMode::FullResolution ? Size{ 513, 260 } : source;
			Texture depth(device, guides, DXGI_FORMAT_R32_FLOAT, std::vector(guides[0] * guides[1], 0.5f));
			Texture motion(device, guides, DXGI_FORMAT_R32G32_FLOAT, std::vector(guides[0] * guides[1], Motion{ 0.001f, -0.002f }));
			Texture mask(device, source, DXGI_FORMAT_R8_UNORM, std::vector<std::uint8_t>(source[0] * source[1], 255));
			std::vector<std::uint8_t> selected(source[0] * source[1]);
			selected[200 * source[0] + 192] = selected[311 * source[0] + 837] =
				selected[(40 + kPackedCropSupportPixels) * source[0] + 32 + kPackedCropSupportPixels] = 255;
			Texture selection(device, source, DXGI_FORMAT_R8_UNORM, selected);
			for (unsigned percent : { 30u, 75u, 100u })
				for (auto sharing : { ModelResolution::Sharing::Private, ModelResolution::Sharing::Guides, ModelResolution::Sharing::All }) {
					const bool sharedTargets = sharing != ModelResolution::Sharing::Private;
					const bool sharedColor = sharing == ModelResolution::Sharing::All;
					std::array<std::vector<Pixel>, 2> colors;
					for (unsigned eye = 0; eye < 2; ++eye) {
						colors[eye].resize(source[0] * source[1]);
						for (unsigned y = 0; y < source[1]; ++y)
							for (unsigned x = 0; x < source[0]; ++x) {
								float value = 0.2f + eye * 0.1f;
								for (unsigned tile = 0; tile < crops.count; ++tile)
									if (contains(crops.regions[tile], float(x), float(y)))
										value += (tile + 1) * 0.1f;
								colors[eye][y * source[0] + x] = { value + x / 1024.0f, value + y / 512.0f, value + ((x + y) % 2) / 16.0f, 0.375f + eye * 0.25f };
							}
					}
					std::array inputs{ Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, colors[0]), Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, colors[1]) };
					std::array outputs{ Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(colors[0].size(), sentinel)), Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(colors[0].size(), sentinel)) };
					std::array<RendererApplyArgs, 2> args{};
					for (unsigned eye = 0; eye < 2; ++eye) {
						auto& input = args[eye];
						input.device = device;
						input.context = context;
						input.colorInput = inputs[eye].texture.Get();
						input.colorOutput = outputs[eye].texture.Get();
						input.depthGuide = depth.texture.Get();
						input.depthGuideSRV = depth.srv.Get();
						input.motionVectors = motion.texture.Get();
						input.guideWidth = guides[0];
						input.guideHeight = guides[1];
						input.colorWidth = input.outputWidth = source[0];
						input.colorHeight = input.outputHeight = source[1];
						input.viewportCrop = { .fullInput = { guides[0] * 2, guides[1] * 2 }, .input = { 16, 20, 16 + guides[0], 20 + guides[1] }, .fullOutput = { source[0] * 2, source[1] * 2 }, .output = { 16, 20, 16 + source[0], 20 + source[1] } };
						input.computeSubrect = owned;
						input.roi = BuildRoiDescriptor(owned, owned, { source[0], source[1] }, true);
						input.featureSlot = eye;
						input.renderingMode = mode;
						input.modelResolutionPercent = percent;
						input.featureUpscaling = mode == RenderingMode::FullResolution;
						input.reset = mode == RenderingMode::ReducedResolution;
						input.characterVisualIsolation = input.providerBlending = true;
						input.packedCrops = crops;
						input.packedCropsEnabled = true;
						input.controlMask = mask.texture;
						input.controlMaskWidth = source[0];
						input.controlMaskHeight = source[1];
						input.actorSelection = selection.srv;
						input.actorSelectionSupport = owned;
						Require(ModelResolution::Required(input), "Packed 100% input bypassed atlas preparation");
					}
					ModelResolution adapter;
					for (unsigned frame = 1; frame <= kPackedCropAdmissionFrames; ++frame) {
						for (auto& eye : args) eye.frameId = frame;
						adapter.PlanPackedCrops(args);
						Require(args[0].packedCropCapacity && args[0].packedCropCapacity->IsValid() == (frame == kPackedCropAdmissionFrames),
							"Production atlas planning skipped the admission window");
					}

					ModelResolution::Batch batch;
					HRESULT result = S_OK;
					std::array<ModelResolution::Targets, 2> targets{};
					if (sharedTargets) {
						Require(ModelResolution::CanUseSharedTargets(args, false), "Packed actors lost raw shared eligibility");
						const auto own = [](const Texture& texture) { return Color::Texture{ texture.texture, texture.srv, texture.uav }; };
						for (unsigned eye = 0; eye < 2; ++eye) {
							RendererApplyArgs projected;
							Require(ModelResolution::Project(args[eye], projected), "Shared packed target geometry failed");
							const Size size{ projected.outputWidth, projected.outputHeight }, guideSize{ projected.guideWidth, projected.guideHeight };
							targets[eye] = { own(Texture(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(size[0] * size[1], sentinel))),
								own(Texture(device, guideSize, DXGI_FORMAT_R32_FLOAT, std::vector(guideSize[0] * guideSize[1], -1.0f))),
								own(Texture(device, guideSize, DXGI_FORMAT_R32G32_FLOAT, std::vector(guideSize[0] * guideSize[1], Motion{ -1, -1 }))),
								own(Texture(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(size[0] * size[1], sentinel))) };
						}
					}
					if (!sharedColor)
						for (auto& target : targets) {
							target.color = {};
							target.output = {};
						}

					const auto prepare = [&](ModelResolution::Batch& prepared) {
						return adapter.Prepare(args, prepared, result, sharedTargets ? std::span<const ModelResolution::Targets>(targets) : std::span<const ModelResolution::Targets>{});
					};
					const auto packedArgs = args;
					if (mode == RenderingMode::ReducedResolution && percent == 100 && !sharedTargets) {
						ModelResolution degraded;
						AdapterCompiler::failPacking = true;
						const auto attempts = AdapterCompiler::packingAttempts;
						Require(degraded.PreparePacking(args, result) && !degraded.PackingAvailable() && SUCCEEDED(result),
							"Optional packing shader failure disabled ordinary NR");
						ModelResolution::Batch fallback;
						Require(degraded.Prepare(args, fallback, result) && !fallback.arguments[0].modelResolutionHistory->packedCrops.count,
							"Unavailable packing shader did not use single-region preparation");
						Require(degraded.PreparePacking(args, result) && AdapterCompiler::packingAttempts == attempts + 1,
							"Failed optional shader was recompiled every frame");
						AdapterCompiler::failPacking = false;
						degraded.InvalidateShaders();
						Require(degraded.PreparePacking(args, result) && degraded.PackingAvailable(),
							"Explicit shader invalidation did not rearm packing");
					}
					for (auto& input : args) input.packedCrops = {};
					ModelResolution::Batch single;
					const auto bytes = adapter.AdditionalBytes(args, sharing);
					Require(bytes && *bytes && prepare(single), "Initial single-region preparation failed");
					const auto compileCount = AdapterCompiler::calls;
					args = packedArgs;
					if (sharedColor)
						for (auto& target : targets) context->ClearUnorderedAccessViewFloat(target.color.uav.Get(), sentinel.data());
					Require(adapter.AdditionalBytes(args, sharing) == 0 && prepare(batch) && AdapterCompiler::calls == compileCount,
						"First atlas admission allocated or compiled on a moving-actor frame");
					for (unsigned eye = 0; eye < 2; ++eye)
						Require(batch.targets[eye].color.resource == single.targets[eye].color.resource &&
									batch.arguments[eye].viewportCrop == single.arguments[eye].viewportCrop,
							"First atlas admission replaced single-region capacity");
					Require(adapter.AdditionalBytes(args, sharing) == 0 && adapter.RetainedBytes(0).value() + adapter.RetainedBytes(1).value() == *bytes,
						"Packed allocation reuse or replacement-peak accounting changed");
					if (sharedTargets) {
						std::uint64_t expectedBytes = 2u * std::uint64_t(source[0]) * source[1] * sizeof(Pixel);
						for (std::size_t eye = 0; eye < batch.count; ++eye)
							expectedBytes += std::uint64_t(batch.arguments[eye].outputWidth) * batch.arguments[eye].outputHeight * (1u + (sharedColor ? 0u : 2u * sizeof(Pixel)));
						Require(*bytes == expectedBytes, "Shared packed accounting duplicated native model targets or omitted positive masks");
					}
					Require(batch.arguments[0].colorInput != batch.arguments[1].colorInput, "Eye atlases alias");
					for (unsigned eye = 0; eye < 2; ++eye) {
						const auto& projected = batch.arguments[eye];
						const auto& layout = projected.modelResolutionHistory->packedCrops;
						Require(layout.count == 2 && !ModelResolution::Required(projected) && projected.reset == args[eye].reset && projected.featureUpscaling == args[eye].featureUpscaling,
							"Packing lost route semantics or recursively requested another atlas");
						const Size size{ projected.outputWidth, projected.outputHeight }, guideSize{ projected.guideWidth, projected.guideHeight };
						auto pixels = Texture(batch.targets[eye].color, size).Read<Pixel>(device, context);
						Color::Texture maskView;
						Check(projected.controlMask.As(&maskView.resource));
						const auto maskPixels = Texture(maskView, size).Read<std::uint8_t>(device, context);
						if (sharedTargets) {
							Require(projected.depthGuide == targets[eye].depth.resource.Get() && projected.motionVectors == targets[eye].motion.resource.Get() &&
										(!sharedColor || (projected.colorInput == targets[eye].color.resource.Get() && projected.colorOutput == targets[eye].output.resource.Get())),
								"Shared actor model targets were replaced");
							Require(projected.controlMask != args[eye].controlMask && projected.controlMask.Get() != targets[eye].output.resource.Get() && projected.actorSelection,
								"Positive actor selection no longer has independent private ownership");
							ComPtr<ID3D11Resource> selectedResource;
							projected.actorSelection->GetResource(&selectedResource);
							Require(selectedResource.Get() == projected.controlMask.Get(), "Projected actor selection lost its positive mask view");
						}
						for (unsigned y = 0; y < size[1]; ++y)
							for (unsigned x = 0; x < size[0]; ++x) {
								if (!contains(projected.computeSubrect, float(x), float(y))) {
									if (sharedColor)
										Require(pixels[y * size[0] + x] == sentinel, "Atlas touched unused backing pixels");
									continue;
								}
								unsigned tile = 0;
								while (tile < layout.count && !contains(layout.tiles[tile].packed, float(x), float(y))) ++tile;
								const auto index = y * size[0] + x;
								const auto expected = PackedColorReference(colors[eye], source, layout, tile, x, y);
								for (unsigned channel = 0; channel < 4; ++channel)
									Near(pixels[index][channel], expected[channel], "Packing shifted source coordinates, filter phase or context bounds");
								Require(maskPixels[index] == (tile < layout.count ? 255 : 0), "Atlas padding retained actor mask coverage");
								for (unsigned channel = 0; channel < 3; ++channel)
									pixels[index][channel] += tile < layout.count ? 0.05f * (tile + eye + 1) : 100.0f;
							}
						context->UpdateSubresource(projected.colorOutput, 0, nullptr, pixels.data(), size[0] * sizeof(Pixel), 0);
						const auto sampledDepth = Texture(batch.targets[eye].depth, guideSize).Read<float>(device, context);
						const auto sampledMotion = Texture(batch.targets[eye].motion, guideSize).Read<Motion>(device, context);
						const auto validGuides = MapComputeSubrect(projected.computeSubrect, size[0], size[1], guideSize[0], guideSize[1]);
						const Motion normal{ 0.002f * layout.unpackedExtent.width / size[0], -0.004f * layout.unpackedExtent.height / size[1] };
						for (unsigned y = 0; y < guideSize[1]; ++y)
							for (unsigned x = 0; x < guideSize[0]; ++x) {
								if (!contains(validGuides, float(x), float(y)))
									continue;
								const float px = (x + 0.5f) * size[0] / guideSize[0], py = (y + 0.5f) * size[1] / guideSize[1];
								unsigned tile = 0;
								while (tile < layout.count && !contains(layout.tiles[tile].packed, px, py)) ++tile;
								const auto index = y * guideSize[0] + x;
								Near(sampledDepth[index], tile < layout.count ? 0.5f : 1.0f, "Packed depth crossed a tile boundary");
								Motion expected{};
								if (tile < layout.count) {
									const auto rect = layout.tiles[tile].packed;
									const float dx = std::abs(normal[0]) * size[0] + 1, dy = std::abs(normal[1]) * size[1] + 1;
									expected = !contains(rect, px - dx, py - dy) || !contains(rect, px + dx, py + dy) ? Motion{ 2, 2 } : normal;
								}
								Near(sampledMotion[index][0], expected[0], "Packed X motion normalization or temporal boundary protection failed");
								Near(sampledMotion[index][1], expected[1], "Packed Y motion normalization or temporal boundary protection failed");
							}
					}
					Require(adapter.Reconstruct(args, batch, result), "Packed reconstruction failed");
					for (const auto& output : outputs)
						Require(std::ranges::all_of(output.Read<Pixel>(device, context), [](auto pixel) { return pixel == sentinel; }), "Eye output was visible before stereo commit");
					adapter.Commit(args, batch);
					for (unsigned eye = 0; eye < 2; ++eye) {
						const auto pixels = outputs[eye].Read<Pixel>(device, context);
						for (unsigned y = 0; y < source[1]; ++y)
							for (unsigned x = 0; x < source[0]; ++x) {
								const auto index = y * source[0] + x;
								auto expected = contains(owned, float(x), float(y)) ? colors[eye][index] : sentinel;
								for (unsigned tile = 0; tile < crops.count; ++tile)
									if (contains(crops.regions[tile], float(x), float(y)))
										for (unsigned channel = 0; channel < 3; ++channel) expected[channel] += 0.05f * (tile + eye + 1);
								for (unsigned channel = 0; channel < 4; ++channel)
									Near(pixels[index][channel], expected[channel], "Packed reconstruction mixed tiles/eyes, lost alpha or changed a gap");
							}
					}
					Require(batch.packedCoverage != nullptr, "Packed execution omitted current-mask validation");
					for (const auto missing : { std::pair{ 520u, 260u }, std::pair{ 32u + kPackedCropSupportPixels - 1u, 40u + kPackedCropSupportPixels } }) {
						auto changed = selected;
						changed[missing.second * source[0] + missing.first] = 255;
						Texture changedSelection(device, source, DXGI_FORMAT_R8_UNORM, changed);
						args[1].actorSelection = changedSelection.srv;
						ModelResolution::Batch guarded;
						Require(prepare(guarded), "Changed current coverage could not be guarded");
						for (unsigned eye = 0; eye < 2; ++eye) {
							const auto& projected = guarded.arguments[eye];
							const Size size{ projected.outputWidth, projected.outputHeight };
							auto neural = Texture(guarded.targets[eye].color, size).Read<Pixel>(device, context);
							for (auto& pixel : neural)
								for (unsigned channel = 0; channel < 3; ++channel) pixel[channel] += 0.1f;
							context->UpdateSubresource(projected.colorOutput, 0, nullptr, neural.data(), size[0] * sizeof(Pixel), 0);
						}
						Require(adapter.Reconstruct(args, guarded, result), "Coverage fallback reconstruction failed");
						adapter.Commit(args, guarded);
						for (unsigned eye = 0; eye < 2; ++eye) {
							const auto pixels = outputs[eye].Read<Pixel>(device, context);
							for (unsigned y = owned.baseY; y < owned.baseY + owned.height; ++y)
								for (unsigned x = owned.baseX; x < owned.baseX + owned.width; ++x)
									Require(pixels[y * source[0] + x] == colors[eye][y * source[0] + x], "A right-eye support/context miss committed partial NR in either eye");
						}
						args[1].actorSelection = selection.srv;
					}
					ModelResolution::Batch recovered;
					Require(prepare(recovered), "Coverage guard did not rearm for a complete current selection");
					for (unsigned eye = 0; eye < 2; ++eye) {
						const auto& projected = recovered.arguments[eye];
						const Size size{ projected.outputWidth, projected.outputHeight };
						auto neural = Texture(recovered.targets[eye].color, size).Read<Pixel>(device, context);
						for (auto& pixel : neural)
							for (unsigned channel = 0; channel < 3; ++channel) pixel[channel] += 0.1f;
						context->UpdateSubresource(projected.colorOutput, 0, nullptr, neural.data(), size[0] * sizeof(Pixel), 0);
					}
					Require(adapter.Reconstruct(args, recovered, result), "Recovered coverage reconstruction failed");
					adapter.Commit(args, recovered);
					for (unsigned eye = 0; eye < 2; ++eye) {
						const auto pixels = outputs[eye].Read<Pixel>(device, context);
						Near(pixels[200 * source[0] + 192][0], colors[eye][200 * source[0] + 192][0] + 0.1f, "A previous coverage miss permanently suppressed NR");
					}
					const auto transitionArgs = args;
					for (unsigned step = 0; step < 20; ++step) {
						args = transitionArgs;
						for (unsigned eye = 0; eye < 2; ++eye) {
							if (step % 4 == 0 || (step % 4 == 1 && eye == 1))
								args[eye].packedCrops = {};
							if (step % 4 == 3)
								args[eye].packedCropCapacity->width += kPackedCropAlignment;
							Require(ModelResolution::Required(args[eye]) == (percent != 100 || args[eye].packedCrops.count != 0),
								"Unpacked identity-scale fallback performed unnecessary model preparation");
						}
						ModelResolution::Batch transition;
						Require(adapter.AdditionalBytes(args, sharing) == 0 && prepare(transition) && AdapterCompiler::calls == compileCount,
							"Atlas/single/resize transition allocated or compiled");
						for (unsigned eye = 0; eye < 2; ++eye) {
							const auto& current = transition.arguments[eye];
							const auto& previous = batch.arguments[eye];
							Require(transition.targets[eye].color.resource == batch.targets[eye].color.resource &&
										transition.targets[eye].depth.resource == batch.targets[eye].depth.resource &&
										transition.targets[eye].motion.resource == batch.targets[eye].motion.resource &&
										transition.targets[eye].output.resource == batch.targets[eye].output.resource &&
										current.controlMask == previous.controlMask && current.viewportCrop == previous.viewportCrop &&
										current.featureUpscaling == previous.featureUpscaling,
								"Moving actors replaced a model/guide/mask allocation or native creation contract");
							Require(bool(current.modelResolutionHistory->packedCrops.count) == bool(args[eye].packedCrops.count),
								"Transition stress did not exercise its requested atlas/single path");
							if (step % 4 != 2 && !(step % 4 == 1 && eye == 0))
								Require(current.modelResolutionHistory != previous.modelResolutionHistory,
									"Layout transition preserved incompatible temporal identity");
						}
						adapter.ReleaseUnscaledSlots(args);
						Require(adapter.AdditionalBytes(args, sharing) == 0, "Fallback retired resident preparation");
					}
					args = transitionArgs;
					const auto originalSources = args[0].packedCrops;
					for (unsigned step = 0; step < 8; ++step) {
						for (auto& eye : args) ++eye.frameId;
						args[0].packedCrops.regions[0].width = crops.regions[0].width - (step % 2 ? 0u : 8u);
						args[0].packedCrops.regions[1].height = crops.regions[1].height - (step % 2 ? 0u : 8u);
						adapter.PlanPackedCrops(args);
						ModelResolution::Batch moving;
						Require(adapter.AdditionalBytes(args, sharing) == 0 && prepare(moving) &&
									moving.targets[0].color.resource == batch.targets[0].color.resource &&
									moving.targets[1].color.resource == batch.targets[1].color.resource,
							"Changing actor extents within capacity rebuilt model textures");
						Require(moving.arguments[0].outputWidth == batch.arguments[0].outputWidth && moving.arguments[0].outputHeight == batch.arguments[0].outputHeight &&
									moving.arguments[0].guideWidth == batch.arguments[0].guideWidth && moving.arguments[0].guideHeight == batch.arguments[0].guideHeight,
							"Retained atlas changed native model dimensions");
						if (step % 2 == 0)
							Require(moving.arguments[0].modelResolutionHistory != batch.arguments[0].modelResolutionHistory,
								"Changed mappings lost history reset while reusing capacity");
					}
					args[0].packedCrops = originalSources;
					const auto identity = batch.arguments[0].modelResolutionHistory;
					if (percent == 100) {
						const auto original = args[0];
						const auto allocation = batch.targets[0].color.resource;
						args[0].packedCrops.regions[0].baseX += 8;
						ModelResolution::Batch moved;
						Require(prepare(moved) && moved.targets[0].color.resource == allocation &&
									moved.arguments[0].modelResolutionHistory != identity,
							"Changed source coordinates must invalidate history without rebuilding equal atlas capacity");
						args[0] = original;
						args[0].packedCrops = {};
						Require(!ModelResolution::Required(args[0]) && ModelResolution::Required(args[1]) && prepare(moved) &&
									!moved.arguments[0].modelResolutionHistory->packedCrops.count && moved.arguments[1].modelResolutionHistory->packedCrops.count,
							"A one-eye packing fallback must retain a complete stereo transaction");
						adapter.ReleaseUnscaledSlots(args);
						Require(adapter.AdditionalBytes(args, sharing) == 0, "An unpacked stereo peer reallocated every frame while packing was active");
						args[0] = original;
						args[0].characterVisualIsolation = false;
						Require(!ModelResolution::Required(args[0]), "Ordinary-scene input was packed without actor-only coverage");
						args[0] = original;
						if (mode == RenderingMode::FullResolution) {
							args[0].guideWidth = source[0] - 1;
							args[0].guideHeight = source[1];
							args[0].viewportCrop = UpscalingDLSS::ViewportCrop::Identity(source[0] - 1, source[1], source[0], source[1]);
							RendererApplyArgs nearUnity;
							Require(ModelResolution::Project(args[0], nearUnity) && nearUnity.featureUpscaling &&
										nearUnity.guideWidth == source[0] - 1 && nearUnity.outputWidth == source[0],
								"Packed projection changed near-unity provider type");
							args[0] = original;
						}
					}
					std::swap(args[0].packedCrops.regions[0], args[0].packedCrops.regions[1]);
					RendererApplyArgs reordered;
					Require(ModelResolution::Project(args[0], reordered) && reordered.modelResolutionHistory != identity,
						"Tile order was omitted from temporal identity");
				}
		}
	}

	void CheckPackedPlanning()
	{
		using namespace NeuralRendering;
		for (auto mode : { RenderingMode::FullResolution, RenderingMode::ReducedResolution }) {
			RendererApplyArgs input;
			input.colorWidth = input.outputWidth = 2048;
			input.colorHeight = input.outputHeight = 1024;
			input.guideWidth = mode == RenderingMode::FullResolution ? 1024 : 2048;
			input.guideHeight = mode == RenderingMode::FullResolution ? 512 : 1024;
			input.featureUpscaling = mode == RenderingMode::FullResolution;
			input.reset = mode == RenderingMode::ReducedResolution;
			input.viewportCrop = UpscalingDLSS::ViewportCrop::Identity(input.guideWidth, input.guideHeight, input.outputWidth, input.outputHeight);
			input.computeSubrect = { 0, 0, 2048, 1024 };
			input.renderingMode = mode;
			input.characterVisualIsolation = true;
			input.packedCrops = { { ComputeSubrect{ 32, 32, 137, 185 }, ComputeSubrect{ 1500, 200, 137, 185 } }, 2 };
			ModelResolution planner;
#ifdef DEVBENCH_BRIDGE_ENABLED
			planner.CropDiagnostics().Configure(true, true);
#endif
			const auto plan = [&] { planner.PlanPackedCrops(std::span(&input, 1)); };
			RendererApplyArgs proxy;
			for (input.frameId = 1; input.frameId <= kPackedCropAdmissionFrames; ++input.frameId) plan();
			Require(ModelResolution::Project(input, proxy) && proxy.modelResolutionHistory->packedCrops.count == 2,
				"Settled production planning did not admit packing");
			const auto capacity = input.packedCropCapacity;
			Require(input.packedCropPlan && input.packedCropPlan->layout == proxy.modelResolutionHistory->packedCrops,
				"Projection did not use the frozen admission plan");
			const auto projectedHistory = proxy.modelResolutionHistory;
			RendererApplyArgs changed = input;
			changed.packedCrops.regions[0].baseX += 8;
			Require(ModelResolution::Project(changed, proxy) && proxy.modelResolutionHistory != projectedHistory,
				"Changed source mappings reused a stale frozen plan");
			planner.Reset();
			plan();
			Require(input.packedCropCapacity == capacity, "GPU retirement erased the CPU capacity policy");
			const auto crops = input.packedCrops;
#ifdef DEVBENCH_BRIDGE_ENABLED
			Require(planner.CropDiagnostics().Total() == 2, "Admission/reset lost the initial or active transition");
			input.packedCropPlanning.reason = PackedCropReason::CropDisabled;
#endif
			input.packedCrops = {};
			++input.frameId;
			plan();
			Require(!ModelResolution::Required(input), "Disabled packing retained model preparation at 100 percent");
			input.packedCrops = crops;
			for (unsigned i = 1; i <= kPackedCropRecoveryFrames; ++i) {
				++input.frameId;
				plan();
				Require(input.packedCropCapacity->IsValid() == (i == kPackedCropRecoveryFrames), "Empty selection bypassed recovery hysteresis");
			}
			++input.frameId;
			input.modelResolutionPercent = 75;
			planner.PlanPackedCrops(std::span(&input, 1), true);
#ifdef DEVBENCH_BRIDGE_ENABLED
			Require(planner.CropDiagnostics().Total() == 4 && !planner.CropDiagnostics().FirstRejection(),
				"Disabled or speculative planning created false rejection evidence");
#endif
			input.modelResolutionPercent = 100;
			plan();
			Require(input.packedCropCapacity == capacity && ModelResolution::Project(input, proxy) && proxy.modelResolutionHistory->packedCrops.count,
				"Rejected speculative resolution destroyed the working atlas capacity");
			++input.frameId;
			input.modelResolutionPercent = 75;
			plan();
			Require(input.packedCropCapacity && !input.packedCropCapacity->IsValid() && ModelResolution::Project(input, proxy) &&
						proxy.outputWidth == 1536 && proxy.outputHeight == 768 && !proxy.modelResolutionHistory->packedCrops.count,
				"A pressure resolution change reused an incompatible atlas plan");
			const auto first = input.packedCropCapacity;
			plan();
			Require(input.packedCropCapacity == first, "Same-frame retry advanced capacity admission");
			for (unsigned i = 1; i < kPackedCropAdmissionFrames; ++i) {
				++input.frameId;
				plan();
			}
			Require(ModelResolution::Project(input, proxy) && proxy.modelResolutionHistory->packedCrops.count == 2 &&
						proxy.modelResolutionHistory->percent == 75,
				"New-resolution capacity never became usable");
#ifdef DEVBENCH_BRIDGE_ENABLED
			Require(planner.CropDiagnostics().Total() == 6 && !planner.CropDiagnostics().FirstRejection(),
				"Resolution changes were misclassified as atlas rejection");
			const auto previousCapacity = input.packedCropCapacity;
			input.sourceWorldFrame = 987;
			input.generation = 123;
			input.executionContext.sourceTransactionId = 456;
			input.packedCrops.regions[0] = { 0, 0, 1000, 900 };
			++input.frameId;
			plan();
			const auto& firstRejection = planner.CropDiagnostics().FirstRejection();
			Require(firstRejection && firstRejection->retainedRejection && firstRejection->previous &&
						firstRejection->previous->sources == crops && firstRejection->current.sources == input.packedCrops &&
						firstRejection->previous->state.capacity == *previousCapacity &&
						firstRejection->retainedRejection->capacity.capacity == *previousCapacity &&
						firstRejection->current.reason == firstRejection->retainedRejection->reason &&
						firstRejection->current.reason != PackedCropReason::CapacitySettling &&
						firstRejection->current.sourceTransaction == 456 &&
						planner.CropDiagnostics().Pending(input.featureSlot, input.frameId, 987, 123, 456) == firstRejection->sequence,
				"Planner lost the first retained-capacity failure or its before/after ownership");
			planner.Reset();
			Require(planner.CropDiagnostics().FirstRejection().has_value(), "Resource reset erased first rejection evidence");
#endif
		}
	}

	using HalfPixel = std::array<std::uint16_t, 4>;
	using UnormPixel = std::array<std::uint8_t, 4>;

	using PackedPixel = std::uint32_t;

	template <class Stored>
	std::vector<Stored> StorePixels(const std::vector<Pixel>& pixels, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN)
	{
		std::vector<Stored> stored(pixels.size());
		for (std::size_t i = 0; i < pixels.size(); ++i) {
			if constexpr (std::is_same_v<Stored, PackedPixel>) {
				if (format == DXGI_FORMAT_R11G11B10_FLOAT) {
					DirectX::PackedVector::XMFLOAT3PK packed;
					DirectX::PackedVector::XMStoreFloat3PK(&packed, DirectX::XMVectorSet(pixels[i][0], pixels[i][1], pixels[i][2], 0));
					stored[i] = packed.v;
				} else {
					stored[i] = 0;
					for (unsigned channel = 0; channel < 4; ++channel) {
						const float maximum = channel == 3 ? 3.0f : 1023.0f;
						stored[i] |= UINT(std::nearbyint(std::clamp(pixels[i][channel], 0.0f, 1.0f) * maximum)) << (10u * channel);
					}
				}
			} else {
				for (unsigned channel = 0; channel < 4; ++channel) {
					if constexpr (std::is_same_v<Stored, HalfPixel>)
						stored[i][channel] = format == DXGI_FORMAT_R16G16B16A16_UNORM ?
						                         static_cast<std::uint16_t>(std::nearbyint(std::clamp(pixels[i][channel], 0.0f, 1.0f) * 65535.0f)) :
						                         DirectX::PackedVector::XMConvertFloatToHalf(pixels[i][channel]);
					else if constexpr (std::is_same_v<Stored, UnormPixel>)
						stored[i][channel] = static_cast<std::uint8_t>(std::nearbyint(std::clamp(pixels[i][channel], 0.0f, 1.0f) * 255.0f));
					else
						stored[i][channel] = pixels[i][channel];
				}
			}
		}
		return stored;
	}

	template <class Stored>
	std::vector<Pixel> LoadPixels(const std::vector<Stored>& stored, DXGI_FORMAT format)
	{
		std::vector<Pixel> pixels(stored.size());
		for (std::size_t i = 0; i < stored.size(); ++i) {
			if constexpr (std::is_same_v<Stored, PackedPixel>) {
				if (format == DXGI_FORMAT_R11G11B10_FLOAT) {
					DirectX::PackedVector::XMFLOAT3PK packed(stored[i]);
					DirectX::XMFLOAT3 decoded;
					DirectX::XMStoreFloat3(&decoded, DirectX::PackedVector::XMLoadFloat3PK(&packed));
					pixels[i] = { decoded.x, decoded.y, decoded.z, 1.0f };
				} else {
					for (unsigned channel = 0; channel < 4; ++channel) {
						const UINT maximum = channel == 3 ? 3u : 1023u;
						pixels[i][channel] = float((stored[i] >> (10u * channel)) & maximum) / float(maximum);
					}
				}
			} else {
				for (unsigned channel = 0; channel < 4; ++channel) {
					if constexpr (std::is_same_v<Stored, HalfPixel>)
						pixels[i][channel] = format == DXGI_FORMAT_R16G16B16A16_UNORM ?
						                         stored[i][channel] / 65535.0f :
						                         DirectX::PackedVector::XMConvertHalfToFloat(stored[i][channel]);
					else if constexpr (std::is_same_v<Stored, UnormPixel>)
						pixels[i][channel] = stored[i][channel] / 255.0f;
					else
						pixels[i][channel] = stored[i][channel];
				}
			}
		}
		return pixels;
	}

	void LegacySelect(ID3D11Device* device, ID3D11DeviceContext* context, Shader& shader,
		Size source, const Texture& neural, const Texture& baseline, const Texture* mask,
		NeuralRendering::ComputeSubrect support, bool provider, const Texture& destination)
	{
		ConstantBuffer constants(device, shader.reflection.Get(), "FoveatedCenterBlendCB");
		constants.SetVariable("InvOutputDim", Motion{ 1.0f / source[0], 1.0f / source[1] });
		constants.SetVariable("DispatchDim", Motion{ float(source[0]), float(source[1]) });
		constants.SetVariable("InvSourceDim", Motion{ 1.0f / source[0], 1.0f / source[1] });
		constants.SetVariable("FullImage", 1u);
		constants.SetVariable("CharacterSelectionMode", mask ? (provider ? 2u : 1u) : 0u);
		constants.SetVariable("CharacterMaskBounds", Pixel{ float(support.baseX) / source[0], float(support.baseY) / source[1],
														 float(support.baseX + support.width) / source[0], float(support.baseY + support.height) / source[1] });
		ComPtr<ID3D11SamplerState> sampler;
		D3D11_SAMPLER_DESC sampling{};
		sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampling.MaxLOD = D3D11_FLOAT32_MAX;
		Check(device->CreateSamplerState(&sampling, &sampler));
		Util::SetResourceName(sampler.Get(), "NRModelResolutionTest::LegacySelection Sampler");
		auto* rawSampler = sampler.Get();
		context->CSSetSamplers(0, 1, &rawSampler);
		const std::array inputs{ neural.srv.Get(), baseline.srv.Get(), mask ? mask->srv.Get() : nullptr };
		const std::array outputs{ destination.uav.Get() };
		Dispatch(context, shader, constants, source, inputs, outputs);
	}

	template <class Stored>
	void SetNativeResidual(ID3D11Device* device, ID3D11DeviceContext* context,
		const NeuralRendering::ModelResolution::Batch& batch, bool invalidNative = false)
	{
		for (std::size_t eye = 0; eye < batch.count; ++eye) {
			const auto& projected = batch.arguments[eye];
			const Size model{ projected.outputWidth, projected.outputHeight };
			D3D11_TEXTURE2D_DESC desc{};
			batch.targets[eye].color.resource->GetDesc(&desc);
			auto pixels = desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ?
			                  Texture(batch.targets[eye].color, model).Read<Pixel>(device, context) :
			                  LoadPixels(Texture(batch.targets[eye].color, model).Read<Stored>(device, context), desc.Format);
			batch.targets[eye].output.resource->GetDesc(&desc);
			for (std::size_t i = 0; i < pixels.size(); ++i) {
				pixels[i][0] += 0.07313f + float(eye) * 0.0167f;
				pixels[i][1] += 0.10937f + float(i % 3) * 0.0011f;
				pixels[i][2] -= 0.06731f;
				pixels[i][3] = 0.987f;
				if (invalidNative && i % 7 < 2)
					pixels[i][0] = i % 7 == 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
			}
			const auto stored = StorePixels<Stored>(pixels, desc.Format);
			context->UpdateSubresource(projected.colorOutput, 0, nullptr, stored.data(), model[0] * sizeof(Stored), 0);
		}
	}

	template <class Stored>
	void CheckSelectedFormat(ID3D11Device* device, ID3D11DeviceContext* context,
		Shader& selectShader, Shader& storageShader, NeuralRendering::ModelResolution& legacy, NeuralRendering::ModelResolution& fused,
		DXGI_FORMAT format, bool masked, bool provider, bool central, bool floatSource = false, bool invalidNative = false)
	{
		using namespace NeuralRendering;
		legacy.ReleaseSlot(0);
		fused.ReleaseSlot(0);
		constexpr Size source{ 31, 23 };
		constexpr ComputeSubrect owned{ 5, 3, 21, 15 }, support{ 7, 5, 13, 11 };
		std::vector<Pixel> colors(source[0] * source[1]);
		std::vector<std::uint8_t> weights(colors.size());
		for (UINT y = 0; y < source[1]; ++y)
			for (UINT x = 0; x < source[0]; ++x) {
				const auto i = y * source[0] + x;
				colors[i] = { 0.137f + float(x % 7) * 0.013f, 0.239f + float(y % 5) * 0.009f,
					0.381f + float((x + y) % 3) * 0.017f, 0.193f + float(x % 11) * 0.011f };
				constexpr std::array<std::uint8_t, 5> fractions{ 0, 17, 96, 160, 255 };
				weights[i] = fractions[(x + 2 * y) % fractions.size()];
			}
		const auto storedSource = StorePixels<Stored>(colors, format);
		const auto input = floatSource ? Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, colors) :
		                                 Texture(device, source, format, storedSource);
		Texture depth(device, source, DXGI_FORMAT_R32_FLOAT, std::vector(colors.size(), 0.5f));
		Texture motion(device, source, DXGI_FORMAT_R32G32_FLOAT, std::vector(colors.size(), Motion{ 0.01f, -0.02f }));
		Texture mask(device, source, DXGI_FORMAT_R8_UNORM, weights);
		Texture legacyNeural(device, source, format, storedSource);
		Texture legacySelected(device, source, format, storedSource);
		Texture fusedSelected(device, source, format, StorePixels<Stored>(std::vector(colors.size(), Pixel{ 0.91f, 0.89f, 0.87f, 0.85f }), format));
		Texture typedBaseline(device, source, format, storedSource);
		ConstantBuffer storage(device, storageShader.reflection.Get(), "ModelResolutionConstants");
		Configure(storage, source, source, { {}, source }, { {}, source }, false, { 1, 1 });
		storage.SetVariable("OutputFormat", format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1u :
											format == DXGI_FORMAT_R11G11B10_FLOAT    ? 2u :
											format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 0u :
																					   3u);
		const std::array storageInputs{ input.srv.Get(), input.srv.Get(), input.srv.Get() };
		const std::array storageOutputs{ typedBaseline.uav.Get() };
		Dispatch(context, storageShader, storage, source, storageInputs, storageOutputs);
		const auto baseline = typedBaseline.Read<Stored>(device, context);
		context->CopyResource(legacyNeural.texture.Get(), typedBaseline.texture.Get());
		context->CopyResource(legacySelected.texture.Get(), typedBaseline.texture.Get());

		RendererApplyArgs arg;
		arg.device = device;
		arg.context = context;
		arg.colorInput = input.texture.Get();
		arg.depthGuide = depth.texture.Get();
		arg.depthGuideSRV = depth.srv.Get();
		arg.motionVectors = motion.texture.Get();
		arg.colorOutput = legacyNeural.texture.Get();
		arg.colorWidth = arg.outputWidth = arg.guideWidth = source[0];
		arg.colorHeight = arg.outputHeight = arg.guideHeight = source[1];
		arg.viewportCrop = UpscalingDLSS::ViewportCrop::Identity(source[0], source[1], source[0], source[1]);
		arg.computeSubrect = owned;
		arg.modelResolutionPercent = 55;
		arg.renderingMode = RenderingMode::ReducedResolution;
		arg.reset = true;
		if (central)
			arg.centralArea = { 50, 4, 1.1f, { 0.03f, -0.02f }, { 62, 46 } };
		if (masked) {
			arg.actorSelection = mask.srv;
			arg.controlMask = mask.texture;
			arg.controlMaskWidth = source[0];
			arg.controlMaskHeight = source[1];
			arg.actorSelectionSupport = support;
			arg.providerBlending = provider;
			arg.characterVisualIsolation = true;
			arg.tuning.useAutoMask = false;
		}
		ModelResolution::Batch legacyBatch, fusedBatch;
		HRESULT result = S_OK;
		const auto legacyArgs = std::span(&arg, 1);
		const auto legacyBytes = legacy.AdditionalBytes(legacyArgs);
		Require(legacyBytes && legacy.Prepare(legacyArgs, legacyBatch, result), "Legacy selected-output reference preparation failed");
		SetNativeResidual<Stored>(device, context, legacyBatch, invalidNative);
		Require(legacy.Reconstruct(legacyArgs, legacyBatch, result), "Legacy selected-output reference reconstruction failed");
		legacy.Commit(legacyArgs, legacyBatch);
		LegacySelect(device, context, selectShader, source, legacyNeural, input, masked ? &mask : nullptr, support, provider, legacySelected);

		arg.colorOutput = fusedSelected.texture.Get();
		arg.modelOutputSelection = ModelOutputSelection{ fusedSelected.uav, masked ? mask.srv : ComPtr<ID3D11ShaderResourceView>{}, support, provider };
		const auto fusedArgs = std::span(&arg, 1);
		const auto selectedBytes = fused.AdditionalBytes(fusedArgs);
		Require(selectedBytes && *legacyBytes == *selectedBytes + std::uint64_t(source[0]) * source[1] * sizeof(Stored),
			"Selected-output allocation admission retained a redundant full-source reconstruction texture");
		Require(fused.Prepare(fusedArgs, fusedBatch, result), "Fused selected-output preparation failed");
		Require(!fusedBatch.arguments[0].modelOutputSelection, "Source-grid selection escaped into the native model projection");
		Require(fused.RetainedBytes(0) == selectedBytes && fused.AdditionalBytes(fusedArgs) == 0,
			"Selected-output retained bytes disagree with admission or reallocate unchanged resources");
		const auto beforeReconstruction = fusedSelected.Read<Stored>(device, context);
		for (unsigned mutation = 0; mutation < (masked ? 3u : 2u); ++mutation) {
			auto changedPolicy = arg;
			if (mutation == 0)
				++changedPolicy.modelOutputSelection->maskSupport.baseX;
			else if (mutation == 1)
				changedPolicy.modelOutputSelection->providerBlending = !provider;
			else
				changedPolicy.modelOutputSelection->mask.Reset();
			Require(!fused.Reconstruct(std::span(&changedPolicy, 1), fusedBatch, result) && result == E_INVALIDARG,
				"Selection policy mutation reached reconstruction after batch admission");
			Require(fusedSelected.Read<Stored>(device, context) == beforeReconstruction,
				"Rejected selection policy mutation dispatched into the selected destination");
		}
		SetNativeResidual<Stored>(device, context, fusedBatch, invalidNative);
		Require(fused.Reconstruct(fusedArgs, fusedBatch, result), "Fused selected-output reconstruction failed");
		const auto selected = fusedSelected.Read<Stored>(device, context);
		fused.Commit(fusedArgs, fusedBatch);
		Require(fusedSelected.Read<Stored>(device, context) == selected, "Selected-output commit copied a redundant or stale reconstruction");
		const auto reference = legacySelected.Read<Stored>(device, context);
		bool changed = false;
		for (std::size_t i = 0; i < selected.size(); ++i) {
			if constexpr (std::is_same_v<Stored, Pixel>) {
				for (unsigned channel = 0; channel < 4; ++channel)
					Near(selected[i][channel], reference[i][channel], "Fused selection changed legacy reconstruction or mask blending");
			} else {
				if (selected[i] != reference[i]) {
					std::cerr << "Selected typed-store mismatch at " << i << " format " << format << " mask " << masked
							  << " provider " << provider << " central " << central << " floatSource " << floatSource << '\n';
					const auto decodedSelected = LoadPixels(std::vector<Stored>{ selected[i] }, format).front();
					const auto decodedReference = LoadPixels(std::vector<Stored>{ reference[i] }, format).front();
					const auto decodedInput = LoadPixels(std::vector<Stored>{ storedSource[i] }, format).front();
					for (unsigned channel = 0; channel < 4; ++channel)
						std::cerr << "channel " << channel << " selected " << decodedSelected[channel] << " reference " << decodedReference[channel]
								  << " source " << decodedInput[channel] << '\n';
					if constexpr (std::is_same_v<Stored, PackedPixel>)
						std::cerr << "packed selected 0x" << std::hex << selected[i] << " reference 0x" << reference[i]
								  << " source 0x" << storedSource[i] << std::dec << '\n';
					const auto reconstructed = LoadPixels(legacyNeural.Read<Stored>(device, context), format);
					for (int oy = -1; oy <= 1; ++oy)
						for (int ox = -1; ox <= 1; ++ox) {
							const int px = int(i % source[0]) + ox, py = int(i / source[0]) + oy;
							if (px < 0 || py < 0 || px >= int(source[0]) || py >= int(source[1]))
								continue;
							const auto neighbor = std::size_t(py) * source[0] + std::size_t(px);
							std::cerr << "legacy texel " << px << "," << py;
							for (auto channel : reconstructed[neighbor]) std::cerr << " " << channel;
							std::cerr << '\n';
						}
					Require(false, "Fused selection changed intermediate typed-store rounding before blending");
				}
			}
			changed = changed || selected[i] != baseline[i];
			const UINT x = UINT(i % source[0]), y = UINT(i / source[0]);
			if (x < owned.baseX || y < owned.baseY || x >= owned.baseX + owned.width || y >= owned.baseY + owned.height)
				if (selected[i] != baseline[i]) {
					std::cerr << "Selected baseline mismatch at " << i << " format " << format << " mask " << masked
							  << " provider " << provider << " central " << central << " floatSource " << floatSource << " invalidNative " << invalidNative << '\n';
					Require(false, "Fused selection left stale pixels outside the native source ROI");
				}
		}
		Require(changed, "Selected-output comparison never applied an NR residual");

		const auto compilations = AdapterCompiler::calls;
		ModelResolution rejected;
		ModelResolution::Batch invalidBatch;
		for (const auto alias : { input.uav, depth.uav, mask.uav }) {
			auto invalid = arg;
			invalid.colorOutput = alias == input.uav ? input.texture.Get() : alias == depth.uav ? depth.texture.Get() :
			                                                                                      mask.texture.Get();
			invalid.modelOutputSelection->destination = alias;
			const auto invalidArgs = std::span(&invalid, 1);
			Require(!rejected.AdditionalBytes(invalidArgs) && !rejected.Prepare(invalidArgs, invalidBatch, result) && result == E_INVALIDARG,
				"Aliased selected-output destination reached allocation or dispatch");
		}
		{
			auto invalid = arg;
			invalid.modelOutputSelection->destination = legacySelected.uav;
			Require(!rejected.AdditionalBytes(std::span(&invalid, 1)) && !rejected.Prepare(std::span(&invalid, 1), invalidBatch, result),
				"Selection destination disagreed with the caller output resource");
		}
		if (masked) {
			auto invalid = arg;
			invalid.modelOutputSelection->mask.Reset();
			Require(!rejected.AdditionalBytes(std::span(&invalid, 1)) && !rejected.Prepare(std::span(&invalid, 1), invalidBatch, result),
				"Character-isolated output was accepted without its selection mask");
			invalid = arg;
			invalid.modelOutputSelection->providerBlending = !provider;
			Require(!rejected.AdditionalBytes(std::span(&invalid, 1)) && !rejected.Prepare(std::span(&invalid, 1), invalidBatch, result),
				"Native provider blending disagreed with selected-output provider policy");
			invalid = arg;
			invalid.modelOutputSelection->maskSupport = { source[0], 0, 1, 1 };
			Require(!rejected.AdditionalBytes(std::span(&invalid, 1)) && !rejected.Prepare(std::span(&invalid, 1), invalidBatch, result),
				"Out-of-bounds selection support reached dispatch");
		}
		Require(AdapterCompiler::calls == compilations && rejected.RetainedBytes(0) == 0,
			"Invalid selected-output preflight compiled shaders or retained allocations");
	}

	void CheckSelectedPackedCoverage(ID3D11Device* device, ID3D11DeviceContext* context, Shader& selection)
	{
		using namespace NeuralRendering;
		constexpr Size source{ 512, 256 };
		constexpr ComputeSubrect owned{ 0, 0, source[0], source[1] };
		const PackedCropSources crops{ { ComputeSubrect{ 16, 32, 96, 96 }, ComputeSubrect{ 368, 120, 96, 96 } }, 2 };
		const std::vector<Pixel> leftColors(source[0] * source[1], Pixel{ 0.2f, 0.3f, 0.4f, 0.375f });
		const std::vector<Pixel> rightColors(source[0] * source[1], Pixel{ 0.4f, 0.1f, 0.3f, 0.625f });
		Texture leftInput(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, leftColors);
		Texture rightInput(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, rightColors);
		Texture depth(device, source, DXGI_FORMAT_R32_FLOAT, std::vector(leftColors.size(), 0.5f));
		Texture motion(device, source, DXGI_FORMAT_R32G32_FLOAT, std::vector(leftColors.size(), Motion{}));
		std::vector<std::uint8_t> weights(leftColors.size());
		weights[80 * source[0] + 64] = 96;
		weights[168 * source[0] + 416] = 180;
		Texture mask(device, source, DXGI_FORMAT_R8_UNORM, weights);
		auto missingWeights = weights;
		missingWeights[128 * source[0] + 256] = 128;
		Texture missingMask(device, source, DXGI_FORMAT_R8_UNORM, missingWeights);
		for (const int missingEye : { -1, 0, 1 }) {
			std::array legacyOutputs{
				Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, leftColors),
				Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, rightColors)
			};
			std::array legacySelected{
				Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, leftColors),
				Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, rightColors)
			};
			std::array selected{
				Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(leftColors.size(), sentinel)),
				Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(leftColors.size(), sentinel))
			};
			RendererApplyArgs arg;
			arg.device = device;
			arg.context = context;
			arg.colorInput = leftInput.texture.Get();
			arg.depthGuide = depth.texture.Get();
			arg.depthGuideSRV = depth.srv.Get();
			arg.motionVectors = motion.texture.Get();
			arg.colorOutput = legacyOutputs[0].texture.Get();
			arg.colorWidth = arg.outputWidth = arg.guideWidth = source[0];
			arg.colorHeight = arg.outputHeight = arg.guideHeight = source[1];
			arg.viewportCrop = UpscalingDLSS::ViewportCrop::Identity(source[0], source[1], source[0], source[1]);
			arg.computeSubrect = owned;
			arg.modelResolutionPercent = 75;
			arg.renderingMode = RenderingMode::ReducedResolution;
			arg.reset = true;
			arg.characterVisualIsolation = true;
			arg.actorSelectionSupport = owned;
			arg.controlMaskWidth = source[0];
			arg.controlMaskHeight = source[1];
			arg.tuning.useAutoMask = false;
			arg.packedCrops = crops;
			arg.packedCropsEnabled = true;
			std::array args{ arg, arg };
			args[1].featureSlot = 1;
			args[1].colorInput = rightInput.texture.Get();
			args[1].colorOutput = legacyOutputs[1].texture.Get();
			for (unsigned eye = 0; eye < 2; ++eye) {
				const auto& current = int(eye) == missingEye ? missingMask : mask;
				args[eye].actorSelection = current.srv;
				args[eye].controlMask = current.texture;
			}
			ModelResolution legacy, fused;
			ModelResolution::Batch legacyBatch, fusedBatch;
			HRESULT result = S_OK;
			Require(legacy.Prepare(args, legacyBatch, result), "Packed legacy selection preparation failed");
			Require(legacyBatch.packedCoverage && legacyBatch.arguments[0].modelResolutionHistory->packedCrops.count == 2 &&
						legacyBatch.arguments[1].modelResolutionHistory->packedCrops.count == 2,
				"Packed selection comparison did not evaluate two packed eyes");
			SetNativeResidual<Pixel>(device, context, legacyBatch);
			Require(legacy.Reconstruct(args, legacyBatch, result), "Packed legacy selection reconstruction failed");
			legacy.Commit(args, legacyBatch);
			for (unsigned eye = 0; eye < 2; ++eye)
				LegacySelect(device, context, selection, source, legacyOutputs[eye], eye == 0 ? leftInput : rightInput,
					int(eye) == missingEye ? &missingMask : &mask, owned, false, legacySelected[eye]);
			for (unsigned eye = 0; eye < 2; ++eye) {
				args[eye].colorOutput = selected[eye].texture.Get();
				args[eye].modelOutputSelection = ModelOutputSelection{ selected[eye].uav, args[eye].actorSelection, owned, false };
			}
			{
				auto aliased = args;
				aliased[1].colorOutput = aliased[0].colorOutput;
				aliased[1].modelOutputSelection->destination = aliased[0].modelOutputSelection->destination;
				ModelResolution rejected;
				ModelResolution::Batch invalid;
				const auto compiled = AdapterCompiler::calls;
				Require(!rejected.AdditionalBytes(aliased) && !rejected.Prepare(aliased, invalid, result) && AdapterCompiler::calls == compiled,
					"Stereo selected outputs aliased before pair preflight");
				aliased = args;
				aliased[1].modelOutputSelection->mask = selected[0].srv;
				Require(!rejected.AdditionalBytes(aliased) && !rejected.Prepare(aliased, invalid, result) && AdapterCompiler::calls == compiled,
					"One selected destination aliased its stereo peer's source selection");
			}
			Require(fused.Prepare(args, fusedBatch, result), "Packed fused selection preparation failed");
			SetNativeResidual<Pixel>(device, context, fusedBatch);
			Require(fused.Reconstruct(args, fusedBatch, result), "Packed fused selection reconstruction failed");
			fused.Commit(args, fusedBatch);
			for (unsigned eye = 0; eye < 2; ++eye) {
				const auto observed = selected[eye].Read<Pixel>(device, context);
				const auto reference = legacySelected[eye].Read<Pixel>(device, context);
				const auto& baseline = eye == 0 ? leftColors : rightColors;
				for (std::size_t i = 0; i < observed.size(); ++i)
					for (unsigned channel = 0; channel < 4; ++channel) {
						Near(observed[i][channel], reference[i][channel], "Packed fused selection disagreed with legacy mask reconstruction");
						if (missingEye >= 0)
							Near(observed[i][channel], baseline[i][channel], "A packed support miss leaked partial NR into one selected eye");
					}
				if (missingEye < 0)
					Require(observed[80 * source[0] + 64] != baseline[80 * source[0] + 64], "Valid packed selection never applied NR");
			}
			if (missingEye < 0) {
				const auto compiled = AdapterCompiler::calls;
				for (unsigned step = 0; step < 8; ++step) {
					for (auto& eye : args) eye.packedCrops = step % 2 ? crops : PackedCropSources{};
					ModelResolution::Batch switched;
					Require(fused.AdditionalBytes(args) == 0 && fused.Prepare(args, switched, result) && AdapterCompiler::calls == compiled,
						"Fused atlas/single selection compiled or reallocated on transition");
					SetNativeResidual<Pixel>(device, context, switched);
					Require(fused.Reconstruct(args, switched, result), "Fused transition reconstruction failed");
					fused.Commit(args, switched);
					for (unsigned eye = 0; eye < 2; ++eye) {
						Require(switched.targets[eye].color.resource == fusedBatch.targets[eye].color.resource,
							"Fused transition replaced native input storage");
						const auto pixels = selected[eye].Read<Pixel>(device, context);
						const auto reference = legacySelected[eye].Read<Pixel>(device, context);
						for (std::size_t i = 0; i < pixels.size(); ++i)
							for (unsigned channel = 0; channel < 4; ++channel)
								Near(pixels[i][channel], reference[i][channel], "Fused transition changed selection or read stale atlas pixels");
					}
				}
			}
		}
	}

	void CheckSelectedAdapter(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		Shader selection(device, L"features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl", vr, "features/Upscaling/Shaders");
		Shader storage(device, L"features/Neural Rendering/Shaders/Upscaling/NeuralRendering/ModelResolutionReconstructCS.hlsl", vr);
		NeuralRendering::ModelResolution legacy, fused;
		CheckSelectedPackedCoverage(device, context, selection);
		CheckSelectedFormat<Pixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R32G32B32A32_FLOAT, true, false, true, false, true);
		CheckSelectedFormat<HalfPixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R16G16B16A16_FLOAT, true, false, true, false, true);
		CheckSelectedFormat<HalfPixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R16G16B16A16_FLOAT, true, false, true, true);
		CheckSelectedFormat<UnormPixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R8G8B8A8_UNORM, true, false, true, true);
		for (bool masked : { false, true })
			for (bool central : { false, true })
				for (bool provider : { false, true }) {
					if (!masked && provider)
						continue;
					CheckSelectedFormat<Pixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R32G32B32A32_FLOAT, masked, provider, central);
					CheckSelectedFormat<HalfPixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R16G16B16A16_FLOAT, masked, provider, central);
					CheckSelectedFormat<UnormPixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R8G8B8A8_UNORM, masked, provider, central);
					CheckSelectedFormat<HalfPixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R16G16B16A16_UNORM, masked, provider, central);
					CheckSelectedFormat<PackedPixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R10G10B10A2_UNORM, masked, provider, central);
					CheckSelectedFormat<PackedPixel>(device, context, selection, storage, legacy, fused, DXGI_FORMAT_R11G11B10_FLOAT, masked, provider, central);
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
		CheckPackedPlanning();
		constexpr NeuralRendering::ComputeSubrect tall{ 0, 0, 640, 448 }, wide{ 0, 0, 1600, 384 };
		constexpr auto tallCapacity = NeuralRendering::Color::Work::AllocationExtent(tall);
		constexpr auto grownCapacity = NeuralRendering::Color::Work::AllocationExtent(wide, tallCapacity);
		static_assert(grownCapacity[0] == 1600 && grownCapacity[1] == 448);
		static_assert(NeuralRendering::Color::Work::AllocationExtent(tall, grownCapacity) == grownCapacity);
		static_assert(NeuralRendering::Color::Work::AllocationExtent(wide)[1] == 384);
		for (bool vr : { false, true }) {
			AdapterCompiler::vr = vr;
			CheckAdapter(device.Get(), context.Get());
			CheckPackedAdapter(device.Get(), context.Get());
			CheckCentralArea(device.Get(), context.Get());
			CheckUpscaledGuides(device.Get(), context.Get());
			CheckSharedAdapter(device.Get(), context.Get(), false);
			CheckSharedAdapter(device.Get(), context.Get(), true);
			CheckSelectedAdapter(device.Get(), context.Get(), vr);
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
		std::cout << "PASS: NR production adapter and shaders on WARP: shared/staged pixel equivalence, fused/legacy selection, all carrier rounding, mixed precision, provider masks, central feather, packed stereo coverage, alias preflight, allocation reuse, stereo commit, area reduction, guide/mask ownership, odd sizes, identity, residual detail, alpha, ROI isolation and finite fallback (flat/VR)\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
