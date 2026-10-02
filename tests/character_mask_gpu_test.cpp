#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Features/Upscaling/NeuralRendering/CharacterCategoryFormat.h"
#include "Features/Upscaling/NeuralRendering/CharacterMaskWorkPolicy.h"
#include "ShaderPackageIncludes.h"
#include "d3d_resource_naming.h"
#include "neural_color/ShaderConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using Microsoft::WRL::ComPtr;

	void Require(bool condition, const std::string& description)
	{
		if (!condition)
			throw std::runtime_error(description);
	}

	void Check(HRESULT result, const char* operation)
	{
		Require(SUCCEEDED(result), std::string(operation) + " failed: " +
									   std::to_string(static_cast<unsigned long>(result)));
	}

	struct Tuple
	{
		std::uint16_t inverseAo = 0;
		std::uint16_t category = 0;
		Tuple() = default;
		Tuple(std::uint8_t inverseAoByte, std::uint8_t categoryByte) :
			inverseAo(static_cast<std::uint16_t>(inverseAoByte * 257u)),
			category(static_cast<std::uint16_t>(categoryByte * 257u))
		{}
		bool operator==(const Tuple&) const = default;
	};
	static_assert(sizeof(Tuple) == 4);

	// Asserted against reflection of the production HLSL, not a test shader.
	struct alignas(16) MaskConstants
	{
		std::uint32_t outputAndSourceSize[4]{};
		std::uint32_t sourceCrop[4]{};
		std::uint32_t options[4]{};
		float featherOptions[4]{};
		float visibilityOptions[4]{};
		float depthLinearization[4]{};
		float cameraProjInverse[16]{};
		float jitter[4]{};
		float categoryStrengths[4]{};
		float eligibilityRectangles[16][4]{};
		std::uint32_t dispatchRegion[4]{};
		std::uint32_t authoredRegion[4]{};
		std::uint32_t supportGrid[4]{};
	};
	static_assert(sizeof(MaskConstants) == 496);

	struct alignas(16) BlendConstants
	{
		float invOutputDim[2]{};
		float centerScale = 1.0f;
		float centerFeather = 0.001f;
		float centerOffset[2]{};
		float outputOffset[2]{};
		float dispatchDim[2]{};
		float sourceOffset[2]{};
		float invSourceDim[2]{};
		float centerHorizontalScale = 2.0f;
		std::uint32_t targetOffsetX = 0;
		std::uint32_t characterSelectionMode = 1;
		std::uint32_t finalLdrColorMode = 0;
		std::uint32_t fullImage = 0;
		float blendFalloff = 1.0f;
		float characterMaskBounds[4]{};
	};
	static_assert(sizeof(BlendConstants) == 96);
	using Color = std::array<float, 4>;

	MaskConstants Defaults(std::uint32_t width, std::uint32_t height)
	{
		MaskConstants result{};
		result.outputAndSourceSize[0] = result.outputAndSourceSize[2] = width;
		result.outputAndSourceSize[1] = result.outputAndSourceSize[3] = height;
		result.sourceCrop[3] = width;
		result.options[0] = height;
		result.options[1] = 1;
		result.featherOptions[2] = 1.0f;
		result.visibilityOptions[0] = 0.001f;
		result.depthLinearization[0] = 100.0f;
		result.depthLinearization[1] = 1.0f;
		result.depthLinearization[2] = 99.0f;
		result.depthLinearization[3] = 100.0f;
		for (int diagonal = 0; diagonal < 4; ++diagonal)
			result.cameraProjInverse[diagonal * 5] = 1.0f;
		std::fill_n(result.categoryStrengths, 3, 1.0f);
		result.eligibilityRectangles[0][2] = static_cast<float>(width);
		result.eligibilityRectangles[0][3] = static_cast<float>(height);
		result.dispatchRegion[2] = result.authoredRegion[2] = width;
		result.dispatchRegion[3] = result.authoredRegion[3] = height;
		return result;
	}

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
	};

	struct MaskResult
	{
		std::vector<std::uint8_t> pixels;
		std::array<std::uint32_t, 9> counters{};
	};

	class Harness
	{
	public:
		explicit Harness(const std::filesystem::path& shaderDirectory, bool benchmark = false) : benchmark_(benchmark)
		{
			D3D_FEATURE_LEVEL actual{};
			constexpr D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
			Check(D3D11CreateDevice(nullptr, benchmark ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0,
					  &requested, 1, D3D11_SDK_VERSION, &device_, &actual, &context_),
				"Create WARP device");
			auto maskBlob = Compile(shaderDirectory / "DLSS5CharacterMaskCS.hlsl");
			CheckMaskLayout(maskBlob.Get());
			Check(device_->CreateComputeShader(maskBlob->GetBufferPointer(),
					  maskBlob->GetBufferSize(), nullptr, &maskShader_),
				"Create mask shader");
			const D3D_SHADER_MACRO supportDefines[]{ { "GPU_CHARACTER_SUPPORT", "1" }, { nullptr, nullptr } };
			auto supportBlob = Compile(shaderDirectory / "DLSS5CharacterMaskCS.hlsl", D3D_COMPILE_STANDARD_FILE_INCLUDE, supportDefines);
			CheckMaskLayout(supportBlob.Get(), true);
			Check(device_->CreateComputeShader(supportBlob->GetBufferPointer(), supportBlob->GetBufferSize(), nullptr, &supportShader_), "Create sparse mask shader");
			Util::SetResourceName(supportShader_.Get(), "CharacterMaskTest::SparseMask");
			const D3D_SHADER_MACRO boundsDefines[]{ { "EARLY_CATEGORY_BOUNDS", "1" }, { nullptr, nullptr } };
			auto boundsBlob = Compile(shaderDirectory / "DLSS5CharacterMaskBoundsCS.hlsl", D3D_COMPILE_STANDARD_FILE_INCLUDE, boundsDefines);
			Check(device_->CreateComputeShader(boundsBlob->GetBufferPointer(), boundsBlob->GetBufferSize(), nullptr, &boundsShader_), "Create support shader");
			Util::SetResourceName(boundsShader_.Get(), "CharacterMaskTest::SupportBounds");
			auto captureBlob = Compile(shaderDirectory / "DLSS5CharacterCaptureCS.hlsl");
			Check(device_->CreateComputeShader(captureBlob->GetBufferPointer(),
					  captureBlob->GetBufferSize(), nullptr, &captureShader_),
				"Create capture shader");
			PackageIncludes includes(shaderDirectory,
				shaderDirectory.parent_path().parent_path() / "features/Upscaling/Shaders");
			const auto blendPath = shaderDirectory.parent_path().parent_path() /
			                       "features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl";
			auto blendBlob = Compile(blendPath, &includes);
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(blendBlob->GetBufferPointer(), blendBlob->GetBufferSize(),
					  __uuidof(ID3D11ShaderReflection), &reflection),
				"Reflect blend shader");
			auto* blendBuffer = reflection->GetConstantBufferByName("FoveatedCenterBlendCB");
			D3D11_SHADER_BUFFER_DESC blendDesc{};
			Check(blendBuffer->GetDesc(&blendDesc), "Reflect blend constants");
			Require(blendDesc.Size == sizeof(BlendConstants), "BlendConstants shader ABI size changed");
			D3D11_SHADER_VARIABLE_DESC colorModeDesc{};
			Check(blendBuffer->GetVariableByName("FinalLdrColorMode")->GetDesc(&colorModeDesc),
				"Reflect Final LDR color mode");
			Require(colorModeDesc.StartOffset == offsetof(BlendConstants, finalLdrColorMode),
				"BlendConstants Final LDR color-mode offset differs");
			D3D11_SHADER_VARIABLE_DESC fullImageDesc{};
			Check(blendBuffer->GetVariableByName("FullImage")->GetDesc(&fullImageDesc), "Reflect full-image route");
			Require(fullImageDesc.StartOffset == offsetof(BlendConstants, fullImage), "BlendConstants full-image offset differs");
			D3D11_SHADER_VARIABLE_DESC falloffDesc{};
			Check(blendBuffer->GetVariableByName("BlendFalloff")->GetDesc(&falloffDesc), "Reflect blend falloff");
			Require(falloffDesc.StartOffset == offsetof(BlendConstants, blendFalloff), "BlendConstants falloff offset differs");
			D3D11_SHADER_VARIABLE_DESC boundsDesc{};
			Check(blendBuffer->GetVariableByName("CharacterMaskBounds")->GetDesc(&boundsDesc),
				"Reflect character mask bounds");
			Require(boundsDesc.StartOffset == offsetof(BlendConstants, characterMaskBounds),
				"BlendConstants bounds offset differs");
			Check(device_->CreateComputeShader(blendBlob->GetBufferPointer(),
					  blendBlob->GetBufferSize(), nullptr, &blendShader_),
				"Create blend shader");
			auto depthBlob = Compile(blendPath.parent_path() / "NeuralRendering/CopyDepthGuideCS.hlsl");
			Check(device_->CreateComputeShader(depthBlob->GetBufferPointer(), depthBlob->GetBufferSize(), nullptr,
					  &depthShader_),
				"Create depth guide shader");
			Util::SetResourceName(depthShader_.Get(), "CharacterMaskTest::CopyDepthGuide");
			const auto colorRoot = shaderDirectory.parent_path().parent_path() / "features/Neural Rendering/Shaders";
			PackageIncludes colorIncludes(shaderDirectory, colorRoot);
			for (const auto& [name, shader] : std::array{
					 std::pair{ "ColorPrepareCS.hlsl", std::addressof(prepareShader_) },
					 std::pair{ "ColorReconstructCS.hlsl", std::addressof(reconstructShader_) } }) {
				auto blob = Compile(colorRoot / "Upscaling/NeuralRendering" / name, &colorIncludes);
				Check(device_->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
						  shader->ReleaseAndGetAddressOf()),
					"Create color shader");
				Util::SetResourceName(shader->Get(), "CharacterMaskTest::%s", name);
			}
		}

		MaskResult Mask(const MaskConstants& constants, std::uint32_t packedWidth,
			const std::vector<Tuple>& tuples,
			std::vector<float> authoredDepth = {}, std::vector<float> currentDepth = {},
			std::uint8_t initialOutput = 0,
			std::uint32_t currentWidth = 0, std::uint32_t currentHeight = 0)
		{
			const auto sourceHeight = constants.outputAndSourceSize[3];
			const auto outputWidth = constants.outputAndSourceSize[0];
			const auto outputHeight = constants.outputAndSourceSize[1];
			currentWidth = currentWidth ? currentWidth : constants.sourceCrop[3];
			currentHeight = currentHeight ? currentHeight : constants.options[0];
			Require(tuples.size() == packedWidth * sourceHeight, "Tuple input dimensions");
			if (authoredDepth.empty())
				authoredDepth.assign(tuples.size(), 0.5f);
			if (currentDepth.empty())
				currentDepth.assign(currentWidth * currentHeight, 0.5f);
			Require(authoredDepth.size() == tuples.size(), "Authored depth dimensions");
			Require(currentDepth.size() == currentWidth * currentHeight,
				"Current depth dimensions");
			auto category = MakeTexture(packedWidth, sourceHeight, NeuralRendering::kCharacterCategoryFormat,
				D3D11_BIND_SHADER_RESOURCE, tuples);
			auto authored = MakeTexture(packedWidth, sourceHeight, DXGI_FORMAT_R32_FLOAT,
				D3D11_BIND_SHADER_RESOURCE, authoredDepth);
			auto current = MakeTexture(currentWidth, currentHeight,
				DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, currentDepth);
			auto output = MakeTexture(outputWidth, outputHeight, DXGI_FORMAT_R8_UNORM,
				D3D11_BIND_UNORDERED_ACCESS,
				std::vector<std::uint8_t>(outputWidth * outputHeight, initialOutput));
			D3D11_BUFFER_DESC counterDesc{};
			counterDesc.ByteWidth = 9 * sizeof(std::uint32_t);
			counterDesc.Usage = D3D11_USAGE_DEFAULT;
			counterDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			counterDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			const std::array<std::uint32_t, 9> zero{};
			const D3D11_SUBRESOURCE_DATA counterData{ zero.data(), 0, 0 };
			ComPtr<ID3D11Buffer> counters;
			Check(device_->CreateBuffer(&counterDesc, &counterData, &counters), "Create counters");
			D3D11_UNORDERED_ACCESS_VIEW_DESC counterView{};
			counterView.Format = DXGI_FORMAT_R32_TYPELESS;
			counterView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			counterView.Buffer.NumElements = 9;
			counterView.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			ComPtr<ID3D11UnorderedAccessView> counterUav;
			Check(device_->CreateUnorderedAccessView(counters.Get(), &counterView, &counterUav),
				"Create counter UAV");
			auto constantBuffer = Constants(constants);
			std::array<ID3D11ShaderResourceView*, 3> srvs{ category.srv.Get(), authored.srv.Get(), current.srv.Get() };
			std::array<ID3D11UnorderedAccessView*, 2> uavs{ output.uav.Get(), counterUav.Get() };
			context_->CSSetShader(maskShader_.Get(), nullptr, 0);
			context_->CSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
			context_->CSSetShaderResources(0, 3, srvs.data());
			context_->CSSetUnorderedAccessViews(0, 2, uavs.data(), nullptr);
			context_->Dispatch((constants.dispatchRegion[2] + 7) / 8,
				(constants.dispatchRegion[3] + 7) / 8, 1);
			context_->ClearState();
			MaskResult result{ Read<std::uint8_t>(output.resource.Get()) };
			counterDesc.Usage = D3D11_USAGE_STAGING;
			counterDesc.BindFlags = counterDesc.MiscFlags = 0;
			counterDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Buffer> staging;
			Check(device_->CreateBuffer(&counterDesc, nullptr, &staging), "Create counter staging");
			context_->CopyResource(staging.Get(), counters.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read counters");
			std::memcpy(result.counters.data(), mapped.pData, sizeof(result.counters));
			context_->Unmap(staging.Get(), 0);
			CheckSparseMask(constants, packedWidth, tuples, category, authored, current, output, result);
			return result;
		}

		void CapturePreservesOutsideAndBothChannels()
		{
			constexpr std::uint32_t width = 16, height = 4;
			std::vector<Tuple> tuples(width * height);
			std::vector<float> depth(width * height);
			for (std::size_t index = 0; index < tuples.size(); ++index) {
				tuples[index] = { 0,
					static_cast<std::uint8_t>((index % 4) * 85) };
				tuples[index].inverseAo = static_cast<std::uint16_t>(index * 997u);
				depth[index] = static_cast<float>(index) / 128.0f;
			}
			auto sourceTuple = MakeTexture(width, height, NeuralRendering::kCharacterCategoryFormat,
				D3D11_BIND_SHADER_RESOURCE, tuples);
			auto sourceDepth = MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT,
				D3D11_BIND_SHADER_RESOURCE, depth);
			const Tuple sentinel{ 17, 19 };
			auto frozenTuple = MakeTexture(width, height, NeuralRendering::kCharacterCategoryFormat,
				D3D11_BIND_UNORDERED_ACCESS, std::vector<Tuple>(width * height, sentinel));
			auto frozenDepth = MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT,
				D3D11_BIND_UNORDERED_ACCESS, std::vector<float>(width * height, -1.0f));
			// Intentionally odd extent in the second eye exercises rounded dispatch.
			const std::array<std::uint32_t, 4> region{ 10, 1, 3, 2 };
			auto cb = Constants(region);
			std::array<ID3D11ShaderResourceView*, 2> srvs{ sourceTuple.srv.Get(), sourceDepth.srv.Get() };
			std::array<ID3D11UnorderedAccessView*, 2> uavs{ frozenTuple.uav.Get(), frozenDepth.uav.Get() };
			context_->CSSetShader(captureShader_.Get(), nullptr, 0);
			context_->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
			context_->CSSetShaderResources(0, 2, srvs.data());
			context_->CSSetUnorderedAccessViews(0, 2, uavs.data(), nullptr);
			context_->Dispatch(1, 1, 1);
			context_->ClearState();
			const auto actualTuple = Read<Tuple>(frozenTuple.resource.Get());
			const auto actualDepth = Read<float>(frozenDepth.resource.Get());
			for (std::uint32_t y = 0; y < height; ++y) {
				for (std::uint32_t x = 0; x < width; ++x) {
					const auto index = y * width + x;
					const bool inside = x >= 10 && x < 13 && y >= 1 && y < 3;
					Require(actualTuple[index] == (inside ? tuples[index] : sentinel),
						"Capture tuple differs or writes outside region");
					Require(actualDepth[index] == (inside ? depth[index] : -1.0f),
						"Capture raw depth differs or writes outside region");
				}
			}
		}

		void VertexAoPreservesOriginalPrecision()
		{
			auto original = MakeTexture(1, 1, DXGI_FORMAT_R16_UNORM,
				D3D11_BIND_RENDER_TARGET, std::vector<std::uint16_t>(1));
			auto categories = MakeTexture(1, 1, NeuralRendering::kCharacterCategoryFormat,
				D3D11_BIND_RENDER_TARGET, std::vector<Tuple>(1));
			ComPtr<ID3D11RenderTargetView> originalRtv, categoriesRtv;
			Check(device_->CreateRenderTargetView(original.resource.Get(), nullptr, &originalRtv),
				"Create original vertex AO RTV");
			Check(device_->CreateRenderTargetView(categories.resource.Get(), nullptr, &categoriesRtv),
				"Create character category RTV");
			// Linear Lighting stores gamma-corrected vertex AO, including values
			// much smaller than an 8-bit linear attachment can preserve.
			for (unsigned vertexByte = 0; vertexByte <= 255; ++vertexByte) {
				const float vertexAo = std::pow(vertexByte / 255.0f, 2.2f);
				const float encoded[]{ 1.0f - vertexAo, 1.0f / 3.0f, 0.0f, 1.0f };
				context_->ClearRenderTargetView(originalRtv.Get(), encoded);
				context_->ClearRenderTargetView(categoriesRtv.Get(), encoded);
				const auto expected = Read<std::uint16_t>(original.resource.Get()).front();
				const auto actual = Read<Tuple>(categories.resource.Get()).front();
				Require(actual.inverseAo == expected,
					"Character attachment changes original vertex AO precision at vertex byte " +
						std::to_string(vertexByte));
				Require(actual.category == 21845u,
					"Character category lane changes with vertex AO");
			}
		}

		void DepthCapacityInvariance()
		{
			const std::array<std::uint32_t, 4> rect{ 3, 5, 13, 9 };
			for (const auto extent : { std::array{ 19u, 17u }, std::array{ 37u, 29u } }) {
				const auto width = extent[0], height = extent[1];
				for (const float poison : { 12345.0f, std::numeric_limits<float>::quiet_NaN() }) {
					std::vector<float> input(width * height, poison), unused(width * height, -1.0f);
					for (unsigned y = 0; y < rect[3]; ++y)
						for (unsigned x = 0; x < rect[2]; ++x)
							input[(rect[1] + y) * width + rect[0] + x] = 0.125f + static_cast<float>(x + y) / 64.0f;
					auto source = MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT,
						D3D11_BIND_SHADER_RESOURCE, input, "DepthValidSource");
					auto output = MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT,
						D3D11_BIND_UNORDERED_ACCESS, unused, "DepthValidOutput");
					auto cb = Constants(rect);
					context_->CSSetShader(depthShader_.Get(), nullptr, 0);
					context_->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
					context_->CSSetShaderResources(0, 1, source.srv.GetAddressOf());
					context_->CSSetUnorderedAccessViews(0, 1, output.uav.GetAddressOf(), nullptr);
					context_->Dispatch((rect[2] + 7) / 8, (rect[3] + 7) / 8, 1);
					context_->ClearState();
					const auto actual = Read<float>(output.resource.Get());
					auto direct = MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT,
						D3D11_BIND_UNORDERED_ACCESS, unused, "DepthTypedCopyOutput");
					const D3D11_BOX box{ rect[0], rect[1], 0, rect[0] + rect[2], rect[1] + rect[3], 1 };
					context_->CopySubresourceRegion(direct.resource.Get(), 0, rect[0], rect[1], 0,
						source.resource.Get(), 0, &box);
					const auto copied = Read<float>(direct.resource.Get());
					Require(std::memcmp(copied.data(), actual.data(), actual.size() * sizeof(float)) == 0,
						"Typed depth copy differs from identity shader or overwrites spare capacity");
					for (unsigned y = 0; y < height; ++y)
						for (unsigned x = 0; x < width; ++x) {
							const bool valid = x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] && y < rect[1] + rect[3];
							Require(actual[y * width + x] == (valid ? input[y * width + x] : -1.0f),
								"Depth copy depends on unused backing pixels or writes outside its valid domain");
						}
				}
			}
		}

		std::vector<Color> Reconstruct(const NeuralColorTest::Constants& constants,
			unsigned width, unsigned height, Color baseline, float poison, bool shrink = false, bool editEdges = false)
		{
			const std::vector<Color> unused(width * height, Color{ poison, poison, poison, poison });
			auto base = MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT,
				D3D11_BIND_SHADER_RESOURCE, std::vector<Color>(width * height, baseline), "ColorBaseline");
			auto prepared = MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, unused, "ColorPrepared");
			auto neural = MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT,
				D3D11_BIND_SHADER_RESOURCE, unused, "ColorNeural");
			auto result = MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT,
				D3D11_BIND_UNORDERED_ACCESS, unused, "ColorReconstructed");
			auto cb = Constants(constants);
			const auto run = [&](const NeuralColorTest::Constants& current) {
				context_->UpdateSubresource(cb.Get(), 0, nullptr, &current, 0, 0);
				const auto dispatch = [&](ID3D11ComputeShader* shader, std::array<ID3D11ShaderResourceView*, 3> inputs,
										  ID3D11UnorderedAccessView* output) {
					context_->CSSetShader(shader, nullptr, 0);
					context_->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
					context_->CSSetShaderResources(0, 3, inputs.data());
					context_->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
					context_->Dispatch((current.width + 7) / 8, (current.height + 7) / 8, 1);
					context_->ClearState();
				};
				dispatch(prepareShader_.Get(), { base.srv.Get(), nullptr, nullptr }, prepared.uav.Get());
				auto native = Read<Color>(prepared.resource.Get());
				for (unsigned y = 0; y < current.height; ++y)
					for (unsigned x = 0; x < current.width; ++x) {
						if ((x == 8 && y == 9) || (editEdges && (x == 0 || y == 0 || x + 1 == current.width || y + 1 == current.height))) {
							auto& edited = native[(current.y + y) * width + current.x + x];
							for (unsigned channel = 0; channel < 3; ++channel)
								edited[channel] *= 1.5f;
							edited[3] = 0.99f;
						}
					}
				const D3D11_BOX valid{ current.x, current.y, 0, current.x + current.width, current.y + current.height, 1 };
				context_->UpdateSubresource(neural.resource.Get(), 0, &valid, native.data() + current.y * width + current.x,
					width * static_cast<UINT>(sizeof(Color)), 0);
				dispatch(reconstructShader_.Get(), { base.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, result.uav.Get());
			};
			if (shrink) {
				auto previous = constants;
				previous.x = previous.y = 0;
				previous.width = width;
				previous.height = height;
				run(previous);
			}
			// Keep previous prepared/native/output texels while narrowing the source's valid domain.
			auto source = unused;
			for (unsigned y = 0; y < constants.height; ++y)
				std::fill_n(source.begin() + y * width, constants.width, baseline);
			context_->UpdateSubresource(base.resource.Get(), 0, nullptr, source.data(), width * static_cast<UINT>(sizeof(Color)), 0);
			const auto before = Read<Color>(result.resource.Get());
			run(constants);
			auto actual = Read<Color>(result.resource.Get());
			for (unsigned y = 0; y < height; ++y)
				for (unsigned x = 0; x < width; ++x)
					if (x >= constants.width || y >= constants.height)
						for (unsigned channel = 0; channel < 4; ++channel) {
							const auto expected = before[y * width + x][channel];
							Require(std::isnan(expected) ? std::isnan(actual[y * width + x][channel]) : actual[y * width + x][channel] == expected,
								"Reconstruction wrote outside the current valid rectangle");
						}
			return actual;
		}

		std::vector<Color> Blend(const BlendConstants& constants,
			std::uint32_t width, std::uint32_t height,
			const std::vector<Color>& neuralPixels, const std::vector<Color>& baselinePixels,
			const std::vector<std::uint8_t>& maskPixels, const std::vector<Color>& initialPixels,
			bool unormOutput = false)
		{
			auto nr = MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT,
				D3D11_BIND_SHADER_RESOURCE, neuralPixels);
			auto base = MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT,
				D3D11_BIND_SHADER_RESOURCE, baselinePixels);
			auto mask = MakeTexture(width, height, DXGI_FORMAT_R8_UNORM,
				D3D11_BIND_SHADER_RESOURCE, maskPixels);
			Texture output;
			if (unormOutput) {
				std::vector<std::array<std::uint8_t, 4>> packed(initialPixels.size());
				for (std::size_t pixel = 0; pixel < packed.size(); ++pixel) {
					for (std::size_t channel = 0; channel < 4; ++channel)
						packed[pixel][channel] = static_cast<std::uint8_t>(
							std::lround(std::clamp(initialPixels[pixel][channel], 0.0f, 1.0f) * 255.0f));
				}
				output = MakeTexture(width * 2, height, DXGI_FORMAT_R8G8B8A8_UNORM,
					D3D11_BIND_UNORDERED_ACCESS, packed);
			} else {
				output = MakeTexture(width * 2, height, DXGI_FORMAT_R32G32B32A32_FLOAT,
					D3D11_BIND_UNORDERED_ACCESS, initialPixels);
			}
			auto cb = Constants(constants);
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			ComPtr<ID3D11SamplerState> sampler;
			Check(device_->CreateSamplerState(&samplerDesc, &sampler), "Create blend sampler");
			Util::SetResourceName(sampler.Get(), "CharacterMaskTest::BlendSampler");
			std::array<ID3D11ShaderResourceView*, 3> srvs{ nr.srv.Get(), base.srv.Get(), mask.srv.Get() };
			context_->CSSetShader(blendShader_.Get(), nullptr, 0);
			context_->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
			context_->CSSetSamplers(0, 1, sampler.GetAddressOf());
			context_->CSSetShaderResources(0, 3, srvs.data());
			context_->CSSetUnorderedAccessViews(0, 1, output.uav.GetAddressOf(), nullptr);
			context_->Dispatch((static_cast<UINT>(constants.dispatchDim[0]) + 7u) / 8u,
				(static_cast<UINT>(constants.dispatchDim[1]) + 7u) / 8u, 1);
			context_->ClearState();
			if (!unormOutput)
				return Read<Color>(output.resource.Get());
			const auto packed = Read<std::array<std::uint8_t, 4>>(output.resource.Get());
			std::vector<Color> result(packed.size());
			for (std::size_t pixel = 0; pixel < packed.size(); ++pixel) {
				for (std::size_t channel = 0; channel < 4; ++channel)
					result[pixel][channel] = packed[pixel][channel] / 255.0f;
			}
			return result;
		}

		void CompositeRespectsCurrentMaskBounds()
		{
			constexpr std::uint32_t width = 8, height = 4;
			const Color baseline{ 0.1f, 0.2f, 0.3f, 1.0f };
			const Color neural{ 0.7f, 0.6f, 0.5f, 1.0f };
			const Color untouched{ -2.0f, -2.0f, -2.0f, -2.0f };
			const auto run = [&](const BlendConstants& constants,
								 const std::vector<Color>& neuralPixels, const std::vector<std::uint8_t>& maskPixels) {
				return Blend(constants, width, height, neuralPixels,
					std::vector<Color>(width * height, baseline), maskPixels,
					std::vector<Color>(width * 2 * height, untouched));
			};
			BlendConstants constants{};
			constants.invOutputDim[0] = constants.invSourceDim[0] = 1.0f / width;
			constants.invOutputDim[1] = constants.invSourceDim[1] = 1.0f / height;
			constants.dispatchDim[0] = width;
			constants.dispatchDim[1] = height;
			constants.targetOffsetX = width;
			std::copy_n(std::array{ 0.25f, 0.25f, 0.75f, 0.75f }.begin(), 4,
				constants.characterMaskBounds);
			std::vector<Color> nrPixels(width * height, Color{ 1.0e20f, 1.0e20f, 1.0e20f, 1.0f });
			std::vector<std::uint8_t> maskPixels(width * height, 255);
			for (std::uint32_t y = 1; y < 3; ++y) {
				for (std::uint32_t x = 2; x < 6; ++x) {
					maskPixels[y * width + x] = std::array<std::uint8_t, 4>{ 0, 64, 128, 255 }[x - 2];
					nrPixels[y * width + x] = neural;
				}
			}
			const auto verify = [&](const std::vector<Color>& pixels, bool useMask, bool useFullNR) {
				for (std::uint32_t y = 0; y < height; ++y) {
					for (std::uint32_t x = 0; x < width * 2; ++x) {
						Color expected = untouched;
						if (x >= width) {
							expected = baseline;
							const auto eyeX = x - width;
							const bool supported = eyeX >= 2 && eyeX < 6 && y >= 1 && y < 3;
							const float weight = useFullNR            ? 1.0f :
							                     useMask && supported ? maskPixels[y * width + eyeX] / 255.0f :
							                                            0.0f;
							for (std::size_t channel = 0; channel < 4; ++channel)
								expected[channel] += weight * (neural[channel] - baseline[channel]);
						}
						for (std::size_t channel = 0; channel < 4; ++channel) {
							Require(std::isfinite(pixels[y * width * 2 + x][channel]) &&
										std::abs(pixels[y * width * 2 + x][channel] - expected[channel]) < 0.0001f,
								"Composite sampled poisoned NR/mask, lost precise strength, or touched other eye");
						}
					}
				}
			};
			verify(run(constants, nrPixels, maskPixels), true, false);
			// Proven-empty bounds must ignore even a stale all-one mask and NaN NR.
			std::fill_n(constants.characterMaskBounds, 4, 0.0f);
			const auto nan = std::numeric_limits<float>::quiet_NaN();
			nrPixels.assign(width * height, Color{ nan, nan, nan, nan });
			verify(run(constants, nrPixels, std::vector<std::uint8_t>(width * height, 255)), false, false);
			// Unknown/full bounds still respect exact zero mask before NR sampling.
			constants.characterMaskBounds[2] = constants.characterMaskBounds[3] = 1.0f;
			verify(run(constants, nrPixels, std::vector<std::uint8_t>(width * height, 0)), false, false);
			// Two independently produced regions leave the central gap undefined.
			// Guard pixels belong to each region; no positive mask may sample the gap.
			const Color otherRegion{ 0.2f, 0.8f, 0.4f, 1.0f };
			nrPixels.assign(width * height, Color{ nan, nan, nan, nan });
			maskPixels.assign(width * height, 0);
			for (std::uint32_t y = 0; y < height; ++y) {
				for (std::uint32_t x = 0; x < width; ++x) {
					if (x < 3)
						nrPixels[y * width + x] = neural;
					else if (x >= 5)
						nrPixels[y * width + x] = otherRegion;
					if (y >= 1 && y < 3 && (x == 1 || x == 6))
						maskPixels[y * width + x] = x == 1 ? 64 : 192;
				}
			}
			const auto separated = run(constants, nrPixels, maskPixels);
			for (std::uint32_t y = 0; y < height; ++y) {
				for (std::uint32_t x = 0; x < width * 2; ++x) {
					Color expected = untouched;
					if (x >= width) {
						const auto eyeX = x - width;
						expected = baseline;
						const float weight = maskPixels[y * width + eyeX] / 255.0f;
						const auto& selected = eyeX < 3 ? neural : otherRegion;
						for (std::size_t channel = 0; channel < 4; ++channel)
							expected[channel] += weight * (selected[channel] - baseline[channel]);
					}
					for (std::size_t channel = 0; channel < 4; ++channel) {
						Require(std::isfinite(separated[y * width * 2 + x][channel]) &&
									std::abs(separated[y * width * 2 + x][channel] - expected[channel]) < 0.0001f,
							"Independent ROI composite leaked poisoned gap, mixed regions, or modified peer eye");
					}
				}
			}
			// Disabling character-only mode retains the normal full-NR route.
			constants.characterSelectionMode = 0;
			verify(run(constants, std::vector<Color>(width * height, neural), maskPixels), false, true);
		}

	private:
		bool benchmark_ = false;
		double TimeGpu(const std::function<void()>& run)
		{
			ComPtr<ID3D11Query> begin, end, disjoint;
			D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP, 0 };
			Check(device_->CreateQuery(&desc, &begin), "Create start timestamp");
			Check(device_->CreateQuery(&desc, &end), "Create end timestamp");
			desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
			Check(device_->CreateQuery(&desc, &disjoint), "Create clock query");
			Util::SetResourceName(begin.Get(), "CharacterMaskTest::Start");
			Util::SetResourceName(end.Get(), "CharacterMaskTest::End");
			Util::SetResourceName(disjoint.Get(), "CharacterMaskTest::Clock");
			context_->Begin(disjoint.Get());
			context_->End(begin.Get());
			for (unsigned i = 0; i < 16; ++i)
				run();
			context_->End(end.Get());
			context_->End(disjoint.Get());
			context_->Flush();
			const auto read = [&](ID3D11Query* query, void* value, UINT size) {
				const auto deadline = GetTickCount64() + 5000;
				HRESULT result;
				while ((result = context_->GetData(query, value, size, D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE && GetTickCount64() < deadline)
					SwitchToThread();
				Require(result == S_OK, "GPU benchmark deadline or query failure");
			};
			UINT64 first{}, last{};
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
			read(disjoint.Get(), &clock, sizeof(clock));
			read(begin.Get(), &first, sizeof(first));
			read(end.Get(), &last, sizeof(last));
			Require(!clock.Disjoint && clock.Frequency && last > first, "Invalid GPU benchmark clock");
			return static_cast<double>(last - first) * 1000.0 / static_cast<double>(clock.Frequency) / 16.0;
		}

		void CheckSparseMask(MaskConstants constants, std::uint32_t packedWidth,
			const std::vector<Tuple>& tuples, const Texture& category, const Texture& authored,
			const Texture& current, const Texture& output, const MaskResult& reference)
		{
			const auto width = constants.outputAndSourceSize[0], height = constants.outputAndSourceSize[1];
			if (constants.dispatchRegion[0] || constants.dispatchRegion[1] ||
				constants.dispatchRegion[2] != width || constants.dispatchRegion[3] != height ||
				(constants.featherOptions[1] != 0.0f && constants.featherOptions[1] != 5.0f))
				return;
			const auto sourceWidth = constants.outputAndSourceSize[2], sourceHeight = constants.outputAndSourceSize[3];
			const auto columns = (sourceWidth + 31u) / 32u, rows = (sourceHeight + 31u) / 32u;
			const auto eyes = packedWidth / sourceWidth;
			Require(eyes >= 1 && eyes <= 2 && packedWidth == eyes * sourceWidth, "Sparse source layout");
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = columns * rows * eyes * 16u;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = 16u;
			ComPtr<ID3D11Buffer> bounds;
			ComPtr<ID3D11ShaderResourceView> boundsSrv;
			ComPtr<ID3D11UnorderedAccessView> boundsUav;
			Check(device_->CreateBuffer(&desc, nullptr, &bounds), "Create sparse support buffer");
			Check(device_->CreateShaderResourceView(bounds.Get(), nullptr, &boundsSrv), "Create sparse support SRV");
			Check(device_->CreateUnorderedAccessView(bounds.Get(), nullptr, &boundsUav), "Create sparse support UAV");
			Util::SetResourceName(bounds.Get(), "CharacterMaskTest::CurrentBounds");
			Util::SetResourceName(boundsSrv.Get(), "CharacterMaskTest::CurrentBounds SRV");
			Util::SetResourceName(boundsUav.Get(), "CharacterMaskTest::CurrentBounds UAV");
			std::array<std::array<std::uint32_t, 4>, 3> boundsConstants{};
			std::uint32_t categories = 0;
			for (std::uint32_t c = 0; c < 3; ++c)
				if (constants.categoryStrengths[c] > 0)
					categories |= 1u << (c + 1u);
			boundsConstants[0] = { sourceWidth, sourceHeight, columns, categories };
			for (auto eye = 0u; eye < eyes; ++eye)
				std::copy_n(constants.authoredRegion, 4, boundsConstants[eye + 1u].begin());
			auto boundsCb = Constants(boundsConstants);
			const auto tileWidth = (width + 7u) / 8u, tileHeight = (height + 7u) / 8u;
			auto dirty = MakeTexture(tileWidth, tileHeight, DXGI_FORMAT_R32_UINT,
				D3D11_BIND_UNORDERED_ACCESS, std::vector<std::uint32_t>(tileWidth * tileHeight, 1u), "DirtyTiles");
			constants.featherOptions[2] = 0;
			constants.supportGrid[0] = columns;
			constants.supportGrid[1] = rows;
			constants.supportGrid[2] = constants.sourceCrop[0] / sourceWidth * columns * rows;
			constants.supportGrid[3] = 1;
			auto cb = Constants(constants);
			auto referenceConstants = constants;
			referenceConstants.supportGrid[3] = 0;
			auto referenceCb = Constants(referenceConstants);
			const auto run = [&](bool support = true) {
				context_->CSSetShader(boundsShader_.Get(), nullptr, 0);
				context_->CSSetConstantBuffers(0, 1, boundsCb.GetAddressOf());
				context_->CSSetShaderResources(0, 1, category.srv.GetAddressOf());
				context_->CSSetUnorderedAccessViews(0, 1, boundsUav.GetAddressOf(), nullptr);
				context_->Dispatch(columns, rows, eyes);
				context_->ClearState();
				const std::array<ID3D11ShaderResourceView*, 4> srvs{ category.srv.Get(), authored.srv.Get(), current.srv.Get(), boundsSrv.Get() };
				const std::array<ID3D11UnorderedAccessView*, 3> uavs{ output.uav.Get(), nullptr, dirty.uav.Get() };
				context_->CSSetShader(support ? supportShader_.Get() : maskShader_.Get(), nullptr, 0);
				context_->CSSetConstantBuffers(0, 1, support ? cb.GetAddressOf() : referenceCb.GetAddressOf());
				context_->CSSetShaderResources(0, 4, srvs.data());
				context_->CSSetUnorderedAccessViews(0, 3, uavs.data(), nullptr);
				context_->Dispatch(tileWidth, tileHeight, 1);
				context_->ClearState();
			};
			const std::vector<std::uint8_t> poison(width * height, 231u);
			context_->UpdateSubresource(output.resource.Get(), 0, nullptr, poison.data(), width, 0);
			run();
			Require(Read<std::uint8_t>(output.resource.Get()) == reference.pixels, "Sparse mask differs from reference");
			if (!benchmark_) {
				std::vector<Tuple> moved(tuples.size());
				for (unsigned y = 0; y < sourceHeight; ++y)
					for (unsigned x = 0; x < packedWidth; ++x)
						if (x % sourceWidth >= 17u)
							moved[y * packedWidth + x] = tuples[y * packedWidth + x - 17u];
				context_->UpdateSubresource(category.resource.Get(), 0, nullptr, moved.data(), packedWidth * sizeof(Tuple), 0);
				run(false);
				const auto movedReference = Read<std::uint8_t>(output.resource.Get());
				context_->UpdateSubresource(output.resource.Get(), 0, nullptr, reference.pixels.data(), width, 0);
				run();
				Require(Read<std::uint8_t>(output.resource.Get()) == movedReference, "Moving sparse support left stale pixels or lost new coverage");
				context_->UpdateSubresource(category.resource.Get(), 0, nullptr, tuples.data(), packedWidth * sizeof(Tuple), 0);
				run();
				Require(Read<std::uint8_t>(output.resource.Get()) == reference.pixels, "Returning sparse support differs from reference");
			}
			if (benchmark_) {
				for (unsigned repeat = 0; repeat < 12; ++repeat) {
					const bool supportFirst = repeat % 2u != 0;
					const double first = TimeGpu([&]() { run(supportFirst); });
					const double second = TimeGpu([&]() { run(!supportFirst); });
					std::cout << "mask_bounds_ms," << width << ',' << height << ',' << constants.options[2] << ',' << repeat
							  << ',' << (supportFirst ? second : first) << ',' << (supportFirst ? first : second) << '\n';
				}
			}
			const std::vector<Tuple> empty(tuples.size());
			context_->UpdateSubresource(category.resource.Get(), 0, nullptr, empty.data(), packedWidth * sizeof(Tuple), 0);
			run();
			Require(Read<std::uint8_t>(output.resource.Get()) == std::vector<std::uint8_t>(width * height), "Departed sparse support was not cleared");
			Require(Read<std::uint32_t>(dirty.resource.Get()) == std::vector<std::uint32_t>(tileWidth * tileHeight), "Empty sparse tiles remain dirty");
			run();
			Require(Read<std::uint8_t>(output.resource.Get()) == std::vector<std::uint8_t>(width * height), "Repeated empty support changed selection");
			context_->UpdateSubresource(category.resource.Get(), 0, nullptr, tuples.data(), packedWidth * sizeof(Tuple), 0);
			run();
			Require(Read<std::uint8_t>(output.resource.Get()) == reference.pixels, "Sparse re-entry differs from reference");
		}

		ComPtr<ID3DBlob> Compile(const std::filesystem::path& path,
			ID3DInclude* includes = D3D_COMPILE_STANDARD_FILE_INCLUDE, const D3D_SHADER_MACRO* defines = nullptr)
		{
			ComPtr<ID3DBlob> shader, errors;
			const auto result = D3DCompileFromFile(path.c_str(), defines,
				includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, &shader, &errors);
			if (FAILED(result) && errors)
				throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
			Check(result, "Compile production shader");
			return shader;
		}

		void CheckMaskLayout(ID3DBlob* blob, bool support = false)
		{
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(),
					  __uuidof(ID3D11ShaderReflection), &reflection),
				"Reflect mask shader");
			auto* buffer = reflection->GetConstantBufferByName("CharacterMaskCB");
			D3D11_SHADER_BUFFER_DESC description{};
			Check(buffer->GetDesc(&description), "Reflect mask constants");
			Require(description.Size == (support ? sizeof(MaskConstants) : offsetof(MaskConstants, supportGrid)), "MaskConstants shader ABI size changed");
			const std::array members{
				std::pair{ "CameraProjInverse", offsetof(MaskConstants, cameraProjInverse) },
				std::pair{ "CategoryStrengths", offsetof(MaskConstants, categoryStrengths) },
				std::pair{ "DispatchRegion", offsetof(MaskConstants, dispatchRegion) },
				std::pair{ "AuthoredRegion", offsetof(MaskConstants, authoredRegion) }
			};
			for (const auto& [name, offset] : members) {
				D3D11_SHADER_VARIABLE_DESC member{};
				Check(buffer->GetVariableByName(name)->GetDesc(&member), "Reflect mask member");
				Require(member.StartOffset == offset, std::string("MaskConstants offset differs: ") + name);
			}
			if (support) {
				D3D11_SHADER_VARIABLE_DESC member{};
				Check(buffer->GetVariableByName("SupportGrid")->GetDesc(&member), "Reflect support grid");
				Require(member.StartOffset == offsetof(MaskConstants, supportGrid), "SupportGrid CPU/HLSL offset differs");
			}
		}

		template <typename T>
		Texture MakeTexture(std::uint32_t width, std::uint32_t height,
			DXGI_FORMAT format, std::uint32_t bindFlags, const std::vector<T>& values, const char* name = "Texture")
		{
			Require(values.size() == width * height, "Texture initial data dimensions");
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = bindFlags;
			const D3D11_SUBRESOURCE_DATA initial{ values.data(), width * static_cast<UINT>(sizeof(T)), 0 };
			Texture result;
			Check(device_->CreateTexture2D(&desc, &initial, &result.resource), "Create texture");
			Util::SetResourceName(result.resource.Get(), "CharacterMaskTest::%s", name);
			if (bindFlags & D3D11_BIND_SHADER_RESOURCE) {
				Check(device_->CreateShaderResourceView(result.resource.Get(), nullptr, &result.srv), "Create SRV");
				Util::SetResourceName(result.srv.Get(), "CharacterMaskTest::%s SRV", name);
			}
			if (bindFlags & D3D11_BIND_UNORDERED_ACCESS) {
				Check(device_->CreateUnorderedAccessView(result.resource.Get(), nullptr, &result.uav), "Create UAV");
				Util::SetResourceName(result.uav.Get(), "CharacterMaskTest::%s UAV", name);
			}
			return result;
		}

		template <typename T>
		ComPtr<ID3D11Buffer> Constants(const T& constants)
		{
			static_assert(sizeof(T) % 16 == 0);
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(T);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_SUBRESOURCE_DATA initial{ &constants, 0, 0 };
			ComPtr<ID3D11Buffer> buffer;
			Check(device_->CreateBuffer(&desc, &initial, &buffer), "Create constant buffer");
			Util::SetResourceName(buffer.Get(), "CharacterMaskTest::Constants");
			return buffer;
		}

		template <typename T>
		std::vector<T> Read(ID3D11Texture2D* texture)
		{
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = desc.MiscFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Texture2D> staging;
			Check(device_->CreateTexture2D(&desc, nullptr, &staging), "Create texture staging");
			Util::SetResourceName(staging.Get(), "CharacterMaskTest::Readback");
			context_->CopyResource(staging.Get(), texture);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read texture");
			std::vector<T> result(desc.Width * desc.Height);
			for (std::uint32_t y = 0; y < desc.Height; ++y)
				std::memcpy(result.data() + y * desc.Width,
					static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch,
					desc.Width * sizeof(T));
			context_->Unmap(staging.Get(), 0);
			return result;
		}

		ComPtr<ID3D11Device> device_;
		ComPtr<ID3D11DeviceContext> context_;
		ComPtr<ID3D11ComputeShader> supportShader_, maskShader_, boundsShader_, captureShader_, blendShader_, prepareShader_, reconstructShader_, depthShader_;
	};

	void FullImageAndReducedSelection(Harness& gpu)
	{
		constexpr std::uint32_t width = 9, height = 5;
		const Color baseline{ 0.1f, 0.2f, 0.3f, 0.8f };
		const Color neural{ 0.7f, 0.6f, 0.5f, 0.3f };
		const auto nan = std::numeric_limits<float>::quiet_NaN();
		for (std::uint32_t eye = 0; eye < 2; ++eye) {
			for (std::uint32_t character = 0; character < 2; ++character) {
				for (std::uint32_t finalLdr = 0; finalLdr < 2; ++finalLdr) {
					std::vector<Color> model(width * height, neural);
					std::vector<std::uint8_t> mask(width * height, 255);
					for (std::uint32_t y = 0; y < height; ++y) {
						for (std::uint32_t x = 0; x < width; ++x) {
							const bool produced = x < 2 || x >= 6;
							if (character && produced)
								mask[y * width + x] = std::array<std::uint8_t, 4>{ 255, 64, 128, 0 }[y % 4];
							if (character && (!produced || mask[y * width + x] == 0)) {
								model[y * width + x] = { nan, nan, nan, nan };
								mask[y * width + x] = 0;
							}
						}
					}
					BlendConstants constants{};
					constants.invOutputDim[0] = constants.invSourceDim[0] = 1.0f / width;
					constants.invOutputDim[1] = constants.invSourceDim[1] = 1.0f / height;
					constants.dispatchDim[0] = width;
					constants.dispatchDim[1] = height;
					constants.centerScale = 0.15f;
					constants.centerHorizontalScale = 1.0f;
					constants.targetOffsetX = eye * width;
					if (eye == 0 && character == 0 && finalLdr == 0) {
						constants.characterSelectionMode = 0;
						const auto restricted = gpu.Blend(constants, width, height, model,
							std::vector<Color>(width * height, baseline), mask,
							std::vector<Color>(width * 2 * height, baseline));
						Require(restricted.front() == baseline, "FOV restriction control must preserve the corner");
					}
					constants.fullImage = 1;
					constants.finalLdrColorMode = finalLdr;
					constants.characterSelectionMode = character;
					constants.characterMaskBounds[2] = constants.characterMaskBounds[3] = 1.0f;
					std::vector<Color> destination(width * 2 * height, baseline);
					if (!finalLdr) {
						for (std::uint32_t y = 0; y < height; ++y)
							for (std::uint32_t x = 0; x < width; ++x)
								destination[y * width * 2 + eye * width + x] = { nan, nan, nan, nan };
					}
					const auto result = gpu.Blend(constants, width, height, model,
						std::vector<Color>(width * height, baseline), mask, destination);
					for (std::uint32_t y = 0; y < height; ++y) {
						for (std::uint32_t x = 0; x < width * 2; ++x) {
							const bool selected = x / width == eye && (!character || x % width < 2 || x % width >= 6);
							for (std::uint32_t channel = 0; channel < 4; ++channel) {
								const float strength = selected ? (character ? mask[y * width + x % width] / 255.0f : 1.0f) : 0.0f;
								const float expected = finalLdr && channel == 3 ? baseline[channel] :
								                                                  baseline[channel] + strength * (neural[channel] - baseline[channel]);
								Require(std::isfinite(result[y * width * 2 + x][channel]) &&
											std::abs(result[y * width * 2 + x][channel] - expected) < 0.0002f,
									"Full-image blend mismatch: eye=" + std::to_string(eye) +
										" character=" + std::to_string(character) + " finalLdr=" + std::to_string(finalLdr) +
										" x=" + std::to_string(x) + " y=" + std::to_string(y) + " channel=" + std::to_string(channel) +
										" mask=" + std::to_string(mask[y * width + x % width]) +
										" actual=" + std::to_string(result[y * width * 2 + x][channel]) + " expected=" + std::to_string(expected));
							}
						}
					}
				}
			}
		}
		std::cout << "Full-image and reduced-selection HLSL passed 8 WARP cases\n";
	}

	void FinalLdrColorModes(Harness& gpu)
	{
		constexpr std::uint32_t width = 8, height = 8;
		const auto nan = std::numeric_limits<float>::quiet_NaN();
		const auto infinity = std::numeric_limits<float>::infinity();
		std::vector<Color> original(width * 2 * height), baseline(width * height);
		std::vector<std::uint8_t> mask(width * height);
		for (std::uint32_t y = 0; y < height; ++y) {
			for (std::uint32_t x = 0; x < width * 2; ++x)
				original[y * width * 2 + x] = Color{
					(40.0f + x) / 255.0f, (70.0f + y) / 255.0f, 100.0f / 255.0f, (50.0f + x + y) / 255.0f
				};
			for (std::uint32_t x = 0; x < width; ++x) {
				baseline[y * width + x] = Color{ 0.1f + x * 0.01f, 0.2f + y * 0.01f, 0.3f, 0.8f };
				mask[y * width + x] = std::array<std::uint8_t, 4>{ 0, 64, 128, 255 }[x % 4];
			}
		}
		std::size_t checkedCases = 0, featherPixels = 0, zeroWeightPixels = 0;
		// Mode 2 also runs against FLOAT: hardware UNORM saturation cannot hide
		// a shader regression that clamps after, rather than before, the lerps.
		for (const auto [mode, unorm] : std::array{
				 std::pair{ 0u, false }, std::pair{ 1u, false },
				 std::pair{ 2u, false }, std::pair{ 2u, true } }) {
			for (std::uint32_t eye = 0; eye < 2; ++eye) {
				for (std::uint32_t character = 0; character < 2; ++character) {
					for (std::uint32_t cropped = 0; cropped < 2; ++cropped) {
						// Non-finite RGB falls back to the original target, not the
						// character baseline. Non-finite model alpha is always ignored
						// by the late modes when its RGB is otherwise finite.
						for (std::uint32_t poison = 0; poison < (mode ? 4u : 1u); ++poison) {
							std::vector<Color> neural(width * height);
							for (std::uint32_t y = 0; y < height; ++y) {
								for (std::uint32_t x = 0; x < width; ++x) {
									auto& color = neural[y * width + x];
									color = Color{ 4.0f + x * 0.1f, -2.0f - y * 0.1f,
										0.1f + x * 0.05f + y * 0.01f, mode ? nan : 0.9f };
									if (poison)
										color[poison - 1] = std::array{ nan, infinity, -infinity }[poison - 1];
								}
							}
							BlendConstants constants{};
							constants.invOutputDim[0] = constants.invSourceDim[0] = 1.0f / width;
							constants.invOutputDim[1] = constants.invSourceDim[1] = 1.0f / height;
							constants.centerScale = 0.5f;
							constants.centerHorizontalScale = 1.0f;
							constants.centerFeather = 0.125f;
							constants.targetOffsetX = eye * width;
							constants.characterSelectionMode = character;
							constants.finalLdrColorMode = mode;
							constants.characterMaskBounds[2] = constants.characterMaskBounds[3] = 1.0f;
							constants.outputOffset[0] = cropped ? 2.0f : 0.0f;
							constants.outputOffset[1] = cropped ? 1.0f : 0.0f;
							constants.sourceOffset[0] = cropped ? 1.0f : 0.0f;
							constants.sourceOffset[1] = cropped ? 2.0f : 0.0f;
							constants.dispatchDim[0] = constants.dispatchDim[1] = cropped ? 4.0f : 8.0f;
							const auto actual = gpu.Blend(constants, width, height, neural,
								baseline, mask, original, unorm);
							for (std::uint32_t y = 0; y < height; ++y) {
								for (std::uint32_t x = 0; x < width * 2; ++x) {
									const auto index = y * width * 2 + x;
									Color expected = original[index];
									const int localX = static_cast<int>(x) - static_cast<int>(constants.targetOffsetX + constants.outputOffset[0]);
									const int localY = static_cast<int>(y) - static_cast<int>(constants.outputOffset[1]);
									if (localX >= 0 && localY >= 0 && localX < constants.dispatchDim[0] && localY < constants.dispatchDim[1]) {
										const float dx = std::abs(((x - constants.targetOffsetX + 0.5f) / width - 0.5f) / 0.25f);
										const float dy = std::abs(((y + 0.5f) / height - 0.5f) / 0.25f);
										const float distance = std::sqrt(std::sqrt(dx * dx * dx * dx + dy * dy * dy * dy));
										const float t = std::clamp((distance - 1.0f) / 0.5f, 0.0f, 1.0f);
										const float fovWeight = 1.0f - t * t * (3.0f - 2.0f * t);
										zeroWeightPixels += fovWeight == 0.0f;
										featherPixels += fovWeight > 0.0f && fovWeight < 1.0f;
										if (fovWeight > 0.0f) {
											const auto sourceIndex = (localY + static_cast<std::uint32_t>(constants.sourceOffset[1])) * width +
											                         localX + static_cast<std::uint32_t>(constants.sourceOffset[0]);
											Color selected = poison ? original[index] : neural[sourceIndex];
											if (mode == 2) {
												for (std::size_t channel = 0; channel < 3; ++channel)
													selected[channel] = std::clamp(selected[channel], 0.0f, 1.0f);
											}
											const float strength = character ? mask[sourceIndex] / 255.0f : 1.0f;
											for (std::size_t channel = 0; channel < (mode ? 3u : 4u); ++channel) {
												const float center = character ? baseline[sourceIndex][channel] +
												                                     strength * (selected[channel] - baseline[sourceIndex][channel]) :
												                                 selected[channel];
												expected[channel] += fovWeight * (center - expected[channel]);
											}
										}
									}
									for (std::size_t channel = 0; channel < 4; ++channel) {
										Require(std::isfinite(actual[index][channel]) &&
													std::abs(actual[index][channel] - expected[channel]) < (unorm ? 1.1f / 255.0f : 0.0002f),
											"Final LDR mismatch: mode=" + std::to_string(mode) + " unorm=" + std::to_string(unorm) +
												" eye=" + std::to_string(eye) + " character=" + std::to_string(character) +
												" cropped=" + std::to_string(cropped) + " poison=" + std::to_string(poison) +
												" x=" + std::to_string(x) + " y=" + std::to_string(y) + " channel=" + std::to_string(channel));
									}
								}
							}
							++checkedCases;
						}
					}
				}
			}
		}
		Require(checkedCases == 104 && featherPixels != 0 && zeroWeightPixels != 0,
			"Final LDR case matrix omitted required coverage");
		std::cout << "Production Final LDR blend HLSL passed " << checkedCases << " WARP cases\n";
	}

	void ExpectByte(std::uint8_t actual, int expected, const char* message)
	{
		Require(std::abs(static_cast<int>(actual) - expected) <= 1,
			std::string(message) + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
	}

	void ReconstructionCapacityInvariance(Harness& gpu)
	{
		const Color baseline{ 0.25f, 0.125f, 0.0625f, 0.375f };
		NeuralColorTest::Constants color;
		color.mode = 2;
		color.transform = 1;
		color.exposure = 1.25f;
		color.detail = 1.3f;
		color.appearance = 0.2f;
		color.lightingPreservation = 0.35f;
		std::vector<Color> reference;
		for (const auto capacity : { std::array{ 25u, 29u }, std::array{ 47u, 43u }, std::array{ 73u, 57u } }) {
			const auto width = capacity[0], height = capacity[1];
			for (const auto origin : { std::array{ 1u, 1u }, std::array{ 5u, 7u } }) {
				color.x = origin[0];
				color.y = origin[1];
				for (const float poison : { 12345.0f, std::numeric_limits<float>::quiet_NaN() }) {
					for (bool shrink : { false, true }) {
						const auto output = gpu.Reconstruct(color, width, height, baseline, poison, shrink, true);
						std::vector<Color> valid;
						for (unsigned y = 0; y < height; ++y)
							for (unsigned x = 0; x < width; ++x) {
								const auto& pixel = output[y * width + x];
								if (x < color.width && y < color.height) {
									for (float value : pixel)
										Require(std::isfinite(value), "Backing padding contaminated reconstructed valid pixels");
									Require(pixel[3] == baseline[3], "Backing capacity changed reconstructed alpha");
									valid.push_back(pixel);
								}
							}
						if (reference.empty()) {
							reference = valid;
							Require(reference[9 * color.width + 8][0] > baseline[0], "Capacity fixture must contain a neural edit");
						} else {
							Require(valid == reference, "Selected output depends on origin, spare storage or previous valid extent");
						}
					}
				}
			}
		}
	}

	void ReconstructionThenExactSelection(Harness& gpu)
	{
		constexpr unsigned width = 37, height = 29;
		const Color baseline{ 0.25f, 0.125f, 0.0625f, 0.375f };
		NeuralColorTest::Constants color;
		color.mode = 2;
		color.transform = 1;
		color.exposure = 1.25f;
		color.detail = 1.3f;
		color.appearance = 0.2f;
		for (const float lighting : { 0.0f, 0.35f, 1.0f }) {
			color.lightingPreservation = lighting;
			for (const float poison : { 12345.0f, std::numeric_limits<float>::quiet_NaN() }) {
				const auto reconstructed = gpu.Reconstruct(color, width, height, baseline, poison);
				Require(reconstructed[9 * width + 8][0] > baseline[0], "Fixture must retain a nonzero neural edit");
				if (lighting > 0)
					Require(reconstructed[9 * width + 9][0] < baseline[0],
						"Unedited center must receive a neighborhood residual before final selection");
				std::vector<Color> committed(width * height, Color{ poison, poison, poison, poison });
				for (unsigned y = 0; y < color.height; ++y)
					for (unsigned x = 0; x < color.width; ++x)
						committed[(color.y + y) * width + color.x + x] = reconstructed[y * width + x];
				for (unsigned eye = 0; eye < 2; ++eye) {
					for (bool subjectPresent : { true, false, true }) {
						std::vector<std::uint8_t> mask(width * height);
						const auto selected = (color.y + 9) * width + color.x + 8;
						mask[selected] = subjectPresent ? 255 : 0;
						BlendConstants blend;
						blend.fullImage = 1;
						blend.invOutputDim[0] = blend.invSourceDim[0] = 1.0f / width;
						blend.invOutputDim[1] = blend.invSourceDim[1] = 1.0f / height;
						// Dispatch beyond the owned rectangle as the full character composite does.
						blend.dispatchDim[0] = width;
						blend.dispatchDim[1] = height;
						blend.targetOffsetX = eye * width;
						blend.characterMaskBounds[2] = blend.characterMaskBounds[3] = 1;
						blend.finalLdrColorMode = 1;
						const std::vector<Color> initial(width * height * 2, baseline);
						const auto output = gpu.Blend(blend, width, height, committed,
							std::vector<Color>(width * height, baseline), mask, initial);
						for (unsigned y = 0; y < height; ++y)
							for (unsigned x = 0; x < width * 2; ++x) {
								const bool edited = subjectPresent && x == eye * width + color.x + 8 && y == color.y + 9;
								const auto& actual = output[y * width * 2 + x];
								Require(actual[3] == baseline[3], "Post-filter selection changed baseline alpha");
								if (edited) {
									for (unsigned channel = 0; channel < 3; ++channel)
										Require(std::abs(actual[channel] - reconstructed[9 * width + 8][channel]) < 1e-6f,
											"Post-filter selection lost the selected neural edit");
								} else {
									Require(actual == baseline, "Post-filter selection changed outside RGB/alpha or the other eye");
								}
							}
					}
				}
			}
		}
	}

	void SelectionAndCoverage(Harness& gpu)
	{
		auto constants = Defaults(4, 1);
		constants.categoryStrengths[0] = 0.2f;
		constants.categoryStrengths[1] = 0.4f;
		constants.categoryStrengths[2] = 0.8f;
		const auto strengths = gpu.Mask(constants, 4, { { 255, 85 }, { 0, 170 }, { 127, 255 }, { 255, 127 } });
		for (std::uint32_t index = 0; index < 4; ++index)
			ExpectByte(strengths.pixels[index], std::array{ 51, 102, 204, 0 }[index], "Category strength");
		Require(strengths.counters[0] == 3, "Nonzero mask coverage count");

		constants = Defaults(2, 1);
		constants.outputAndSourceSize[0] = constants.dispatchRegion[2] = 4;
		constants.eligibilityRectangles[0][2] = 4.0f;
		const auto coverage = gpu.Mask(constants, 2, { { 0, 85 }, { 0, 0 } });
		for (std::uint32_t index = 0; index < 4; ++index)
			ExpectByte(coverage.pixels[index], std::array{ 255, 191, 64, 0 }[index], "Subpixel reconstructed coverage");
		constants.categoryStrengths[0] = constants.categoryStrengths[2] = 0.0f;
		const auto noInventedSkin = gpu.Mask(constants, 2, { { 0, 85 }, { 0, 255 } });
		Require(std::ranges::all_of(noInventedSkin.pixels, [](auto value) { return value == 0; }),
			"Interpolated face/hair codes must never invent skin coverage");

		constants = Defaults(3, 1);
		constants.jitter[0] = 0.25f;
		constants.options[2] = constants.options[3] = 1;
		const auto rejected = gpu.Mask(constants, 3, { { 0, 85 }, { 0, 1 }, { 0, 0 } });
		Require(rejected.pixels[1] == 0, "Rejected-actor marker must block neighbor interpolation and feathering");

		// A strong adjacent category cannot override the user's weaker/disabled
		// center category, even with both subpixel reconstruction and feathering.
		constants.categoryStrengths[1] = 0.2f;
		const auto weakSkin = gpu.Mask(constants, 3, { { 0, 85 }, { 0, 170 }, { 0, 170 } });
		ExpectByte(weakSkin.pixels[1], 51, "Weak skin center must cap adjacent face contribution");
		constants.categoryStrengths[1] = 0.0f;
		const auto disabledSkin = gpu.Mask(constants, 3, { { 0, 85 }, { 0, 170 }, { 0, 170 } });
		Require(disabledSkin.pixels[1] == 0, "Disabled skin must not receive enabled-face feather");

		// A supported center remains usable beside an excluded actor; exclusion
		// must block coverage on that actor, not erase the selected foreground.
		constants = Defaults(3, 1);
		constants.jitter[0] = -0.25f;
		const auto besideRejected = gpu.Mask(constants, 3, { { 0, 0 }, { 0, 85 }, { 0, 1 } });
		ExpectByte(besideRejected.pixels[1], 191, "Selected center beside excluded actor");
		Require(besideRejected.pixels[2] == 0, "Excluded neighbor remains zero");

		// Sweep across the nearest-category boundary: coverage must remain a
		// continuous tent function, not a binary nearest-neighbor disco pattern.
		for (int step = -18; step <= 18; ++step) {
			constants.jitter[0] = static_cast<float>(step) * 0.05f;
			const auto animated = gpu.Mask(constants, 3, { { 0, 0 }, { 0, 85 }, { 0, 0 } });
			ExpectByte(animated.pixels[1], static_cast<int>(std::lround(255.0f * (1.0f - std::abs(constants.jitter[0])))), "Subpixel jitter continuity");
		}
	}

	void MaskSamplingMatchesOutputGrid(Harness& gpu)
	{
		constexpr std::uint32_t width = 9, height = 5, faceX = 4, faceY = 2;
		std::vector<Tuple> categories(width * height);
		categories[faceY * width + faceX] = { 0, 85 };
		for (const auto& phase : { std::array{ -0.499f, 0.33f }, std::array{ 0.501f, -0.25f } }) {
			for (bool outputIsJittered : { false, true }) {
				auto constants = Defaults(width, height);
				const auto samplingJitter = NeuralRendering::ResolveCharacterMaskSamplingJitter(
					outputIsJittered, phase[0], phase[1]);
				std::copy(samplingJitter.begin(), samplingJitter.end(), constants.jitter);
				const auto result = gpu.Mask(constants, width, categories);
				for (std::uint32_t y = 0; y < height; ++y) {
					for (std::uint32_t x = 0; x < width; ++x) {
						const float dx = static_cast<float>(x) - faceX - (outputIsJittered ? 0.0f : phase[0]);
						const float dy = static_cast<float>(y) - faceY - (outputIsJittered ? 0.0f : phase[1]);
						const float coverage = std::max(0.0f, 1.0f - std::abs(dx)) *
						                       std::max(0.0f, 1.0f - std::abs(dy));
						ExpectByte(result.pixels[y * width + x], static_cast<int>(std::lround(255.0f * coverage)),
							outputIsJittered ? "Pre-DLSS mask must exactly match source category texels" :
											   "Post-DLSS mask must reconstruct unjittered coverage");
					}
				}
			}
		}
	}

	void FractionalFeatherCoverage(Harness& gpu)
	{
		constexpr std::uint32_t width = 17, height = 17, faceX = 5, faceY = 5;
		std::vector<Tuple> categories(width * height);
		categories[faceY * width + faceX] = { 0, 85 };
		const std::array phases{
			std::array{ 0.0f, 0.0f }, std::array{ 0.499f, 0.0f }, std::array{ 0.501f, 0.0f },
			std::array{ 0.0f, 0.499f }, std::array{ 0.0f, 0.501f },
			std::array{ 0.499f, 0.499f }, std::array{ 0.501f, 0.501f },
			std::array{ -0.51f, 0.25f }, std::array{ 0.51f, -0.25f }
		};
		for (const auto& phase : phases) {
			auto constants = Defaults(width, height);
			std::copy(phase.begin(), phase.end(), constants.jitter);
			const auto unfeathered = gpu.Mask(constants, width, categories);
			constants.options[3] = 1;
			const auto zeroRadius = gpu.Mask(constants, width, categories);
			Require(zeroRadius.pixels == unfeathered.pixels,
				"Zero-radius feather must preserve current-frame coverage");
			for (std::uint32_t radius : { 1u, 4u }) {
				constants.options[2] = radius;
				const auto result = gpu.Mask(constants, width, categories);
				for (std::uint32_t y = 0; y < height; ++y) {
					for (std::uint32_t x = 0; x < width; ++x) {
						const float dx = static_cast<float>(x) - phase[0] - faceX;
						const float dy = static_cast<float>(y) - phase[1] - faceY;
						const float coverage = std::max(0.0f, 1.0f - std::abs(dx)) *
						                       std::max(0.0f, 1.0f - std::abs(dy));
						const float feather = std::max(0.0f,
							1.0f - std::hypot(dx, dy) / static_cast<float>(radius + 1));
						ExpectByte(result.pixels[y * width + x],
							static_cast<int>(std::lround(255.0f * std::max(coverage, feather))),
							"Feather must remain continuous across source-texel jitter phases");
					}
				}
			}
			constants.options[3] = 0;
			Require(gpu.Mask(constants, width, categories).pixels == unfeathered.pixels,
				"Disabled feather must preserve current-frame coverage regardless of radius");
		}
		auto constants = Defaults(width, height);
		constants.options[2] = 4;
		constants.options[3] = 1;
		constants.jitter[0] = 0.501f;
		const auto interior = gpu.Mask(constants, width, std::vector<Tuple>(width * height, { 0, 85 }));
		Require(std::ranges::all_of(interior.pixels, [](auto value) { return value == 255; }),
			"Feather ceiling optimization must preserve fully covered interiors");
		std::vector<float> depths(width * height, 0.2f);
		depths[faceY * width + faceX] = 0.9f;
		const auto depthEdge = gpu.Mask(constants, width, categories, depths, depths);
		Require(depthEdge.pixels[faceY * width + faceX + 2] == 0,
			"Continuous feather must not cross a rejected depth boundary");
		constants.visibilityOptions[2] = constants.visibilityOptions[3] = 0.1f;
		const auto distant = gpu.Mask(constants, width, categories, depths, depths);
		Require(std::ranges::all_of(distant.pixels, [](auto value) { return value == 0; }),
			"Continuous feather must not restore distance-culled category samples");
	}

	void VisibilityAndDistance(Harness& gpu)
	{
		auto constants = Defaults(1, 1);
		constants.visibilityOptions[1] = 1.0f;
		const std::vector<Tuple> face{ { 0, 85 } };
		Require(gpu.Mask(constants, 1, face, { 0.9f }, { 0.5f }).pixels[0] == 0,
			"Closer current geometry must occlude authored face");
		ExpectByte(gpu.Mask(constants, 1, face, { 0.9f }, { 0.99f }).pixels[0], 255,
			"Farther current depth must not erase visible authored face");

		auto edge = Defaults(2, 1);
		edge.outputAndSourceSize[0] = edge.dispatchRegion[2] = 4;
		edge.eligibilityRectangles[0][2] = 4.0f;
		edge.visibilityOptions[1] = 1.0f;
		const auto foregroundEdge = gpu.Mask(edge, 2, { { 0, 85 }, { 0, 0 } },
			{ 0.9f, 0.2f }, { 0.9f, 0.2f });
		Require(foregroundEdge.pixels[2] == 0,
			"Background face sample must not bleed onto a nearer center surface");
		ExpectByte(foregroundEdge.pixels[1], 191,
			"Visible face coverage survives beside foreground occluder");

		edge.visibilityOptions[1] = 0.0f;
		edge.cameraProjInverse[0] = edge.cameraProjInverse[5] = 0.0f;
		edge.cameraProjInverse[10] = 20.0f;
		edge.visibilityOptions[2] = 10.0f;
		edge.visibilityOptions[3] = 1.0f;
		const auto distantBackground = gpu.Mask(edge, 2, { { 0, 85 }, { 0, 0 } },
			{ 0.25f, 0.75f });
		constexpr std::array expectedCoverage{ 255, 191, 64, 0 };
		for (std::size_t index = 0; index < expectedCoverage.size(); ++index)
			ExpectByte(distantBackground.pixels[index], expectedCoverage[index],
				"Background distance must not clip reconstructed coverage of a nearby character");

		edge.visibilityOptions[1] = 1.0f;
		edge.visibilityOptions[2] = 0.0f;
		const auto rawDepth = [](float distance) { return (100.0f - 100.0f / distance) / 99.0f; };
		const std::vector<float> authoredEdgeDepth{ rawDepth(5.0f), rawDepth(15.0f) };
		const auto behindCharacter = gpu.Mask(edge, 2, { { 0, 85 }, { 0, 0 } },
			authoredEdgeDepth, { rawDepth(5.0f), rawDepth(10.0f) });
		ExpectByte(behindCharacter.pixels[2], 64,
			"Changed background behind the character must preserve reconstructed coverage");
		const auto inFrontOfCharacter = gpu.Mask(edge, 2, { { 0, 85 }, { 0, 0 } },
			authoredEdgeDepth, { rawDepth(5.0f), rawDepth(2.0f) });
		Require(inFrontOfCharacter.pixels[2] == 0,
			"Changed background in front of the character must occlude reconstructed coverage");

		constants.visibilityOptions[1] = 0.0f;
		constants.cameraProjInverse[0] = constants.cameraProjInverse[5] = 0.0f;
		constants.cameraProjInverse[10] = 20.0f;
		constants.visibilityOptions[2] = 8.0f;
		constants.visibilityOptions[3] = 1.0f;
		Require(gpu.Mask(constants, 1, face, { 0.5f }).pixels[0] == 0, "Distance cutoff");
		constants.visibilityOptions[2] = 10.5f;
		ExpectByte(gpu.Mask(constants, 1, face, { 0.5f }).pixels[0], 128, "Distance fade");
		constants.visibilityOptions[2] = 0.0f;
		ExpectByte(gpu.Mask(constants, 1, face, { 0.5f }).pixels[0], 255, "Zero disables distance culling");
	}

	void CurrentDepthAllowsLargerAllocation(Harness& gpu)
	{
		constexpr std::uint32_t activeWidth = 5, activeHeight = 3;
		constexpr std::uint32_t allocationWidth = 9, allocationHeight = 7;
		auto constants = Defaults(activeWidth, activeHeight);
		constants.visibilityOptions[1] = 1.0f;
		std::vector<Tuple> categories(activeWidth * activeHeight, { 0, 85 });
		std::vector<float> authored(categories.size(), 0.9f);
		std::vector<float> activeDepth(categories.size(), 0.99f);
		activeDepth[activeWidth + 2] = 0.5f;
		std::vector<float> allocation(allocationWidth * allocationHeight, 0.0f);
		for (std::uint32_t y = 0; y < activeHeight; ++y)
			std::copy_n(activeDepth.begin() + y * activeWidth, activeWidth,
				allocation.begin() + y * allocationWidth);
		const auto exact = gpu.Mask(constants, activeWidth, categories, authored, activeDepth);
		const auto larger = gpu.Mask(constants, activeWidth, categories, authored, allocation,
			0, allocationWidth, allocationHeight);
		Require(exact.pixels == larger.pixels && exact.counters == larger.counters,
			"Display-size current depth allocation changed active-input visibility");
		Require(exact.pixels[0] > 0 && exact.pixels[activeWidth + 2] == 0,
			"Depth allocation test must include visible and occluded characters");
	}

	void CropsDirtyRegionsAndStereo(Harness& gpu)
	{
		auto constants = Defaults(8, 4);
		constants.authoredRegion[0] = 2;
		constants.authoredRegion[1] = 1;
		constants.authoredRegion[2] = 3;
		constants.authoredRegion[3] = 2;
		const auto bounded = gpu.Mask(constants, 8, std::vector<Tuple>(32, { 0, 85 }));
		for (std::uint32_t y = 0; y < 4; ++y) {
			for (std::uint32_t x = 0; x < 8; ++x) {
				const bool valid = x >= 2 && x < 5 && y >= 1 && y < 3;
				ExpectByte(bounded.pixels[y * 8 + x], valid ? 255 : 0, "Poisoned stale capture outside valid rectangle");
			}
		}

		constants = Defaults(8, 4);
		constants.dispatchRegion[0] = 1;
		constants.dispatchRegion[1] = 1;
		constants.dispatchRegion[2] = 6;
		constants.dispatchRegion[3] = 2;
		constants.eligibilityRectangles[0][0] = 5.0f;
		constants.eligibilityRectangles[0][1] = 1.0f;
		constants.eligibilityRectangles[0][2] = 7.0f;
		constants.eligibilityRectangles[0][3] = 3.0f;
		const auto dirty = gpu.Mask(constants, 8, std::vector<Tuple>(32, { 0, 85 }), {}, {}, 77);
		for (std::uint32_t y = 0; y < 4; ++y) {
			for (std::uint32_t x = 0; x < 8; ++x) {
				const bool dispatched = x >= 1 && x < 7 && y >= 1 && y < 3;
				const bool eligible = dispatched && x >= 5;
				ExpectByte(dirty.pixels[y * 8 + x], eligible ? 255 : dispatched ? 0 :
																				  77,
					"Dirty dispatch must clear old region without touching outside");
			}
		}

		constants = Defaults(4, 2);
		constants.sourceCrop[0] = 4;
		constants.categoryStrengths[0] = 0.25f;
		constants.categoryStrengths[1] = 0.75f;
		std::vector<Tuple> stereo(16);
		for (std::uint32_t y = 0; y < 2; ++y) {
			for (std::uint32_t x = 0; x < 8; ++x)
				stereo[y * 8 + x] = { 0, static_cast<std::uint8_t>(x < 4 ? 85 : 170) };
		}
		const auto right = gpu.Mask(constants, 8, stereo);
		for (auto value : right.pixels)
			ExpectByte(value, 191, "Right eye uses packed source offset");
		constants.sourceCrop[0] = 0;
		const auto left = gpu.Mask(constants, 8, stereo);
		for (auto value : left.pixels)
			ExpectByte(value, 64, "Left eye uses independent source");
	}
}

void GpuSupportCoverageCases(Harness& gpu, bool benchmark)
{
	const unsigned sourceWidth = benchmark ? 1008u : 257u, sourceHeight = benchmark ? 1120u : 137u;
	unsigned cases = 0;
	for (unsigned density = 0; density < 2; ++density) {
		for (unsigned eyes = 1; eyes <= (benchmark ? 1u : 2u); ++eyes) {
			const unsigned packedWidth = sourceWidth * eyes;
			std::vector<Tuple> tuples(packedWidth * sourceHeight);
			for (unsigned y = 0; y < sourceHeight; ++y)
				for (unsigned x = 0; x < packedWidth; ++x) {
					const unsigned local = x % sourceWidth;
					const bool selected = density || local == 31u || local == 32u ||
					                      (local > sourceWidth / 2 && local < sourceWidth / 2 + 20u && y > 10u && y < 70u);
					if (selected && (local + y) % 7u != 0u)
						tuples[y * packedWidth + x] = Tuple{ 37, static_cast<std::uint8_t>((1u + (local + y) % 3u) * 85u) };
				}
			for (unsigned radius : { 0u, 1u, 4u }) {
				if (benchmark && radius == 1u)
					continue;
				for (unsigned eye = 0; eye < eyes; ++eye) {
					for (float phase : { -0.49f, 0.0f, 0.49f }) {
						if (benchmark && phase != 0.0f)
							continue;
						auto constants = Defaults(benchmark ? sourceWidth : 391u, benchmark ? sourceHeight : 211u);
						constants.outputAndSourceSize[2] = sourceWidth;
						constants.outputAndSourceSize[3] = sourceHeight;
						constants.sourceCrop[0] = eye * sourceWidth;
						constants.sourceCrop[1] = 3;
						constants.sourceCrop[2] = 5;
						constants.sourceCrop[3] = sourceWidth - 6u;
						constants.options[0] = sourceHeight - 10u;
						constants.options[2] = radius;
						constants.options[3] = radius ? 1u : 0u;
						constants.authoredRegion[2] = sourceWidth;
						constants.authoredRegion[3] = sourceHeight;
						constants.jitter[0] = phase;
						constants.jitter[1] = -phase;
						constants.categoryStrengths[1] = 0.0f;
						constants.categoryStrengths[2] = 0.65f;
						if (benchmark)
							std::cout << "fixture," << density << ',' << radius << '\n';
						(void)gpu.Mask(constants, packedWidth, tuples);
						++cases;
					}
				}
			}
		}
	}
	std::cout << "Sparse/reference coverage, departure and re-entry cases: " << cases << '\n';
}

void GpuSupportQuantizationCases(Harness& gpu)
{
	constexpr unsigned width = 64, height = 32;
	const float halfStep = 0.5f / 255.0f;
	for (float strength : { std::nextafter(halfStep, 0.0f), halfStep,
			 std::nextafter(halfStep, 1.0f), 1.0f / 255.0f }) {
		auto constants = Defaults(width, height);
		constants.categoryStrengths[0] = strength;
		std::vector<Tuple> tuples(width * height);
		for (unsigned y = 4; y < 20; ++y)
			for (unsigned x = 3; x < 16; ++x)
				tuples[y * width + x] = Tuple{ 37, 85 };
		(void)gpu.Mask(constants, width, tuples);
	}
	std::cout << "Sparse mask R8 half-step departure cases: 4\n";
}

int wmain(int argc, wchar_t** argv)
{
	try {
		const bool benchmark = argc == 3 && std::wstring(argv[2]) == L"--gpu-benchmark";
		Require(argc == 2 || (argc == 3 && (benchmark || std::wstring(argv[2]) == L"--final-ldr-only")),
			"Expected production shader directory and optional --final-ldr-only or --gpu-benchmark");
		Harness gpu(argv[1], benchmark);
		if (benchmark) {
			GpuSupportCoverageCases(gpu, true);
			return 0;
		}
		FullImageAndReducedSelection(gpu);
		gpu.DepthCapacityInvariance();
		ReconstructionCapacityInvariance(gpu);
		ReconstructionThenExactSelection(gpu);
		if (argc == 3) {
			FinalLdrColorModes(gpu);
			return 0;
		}
		gpu.CapturePreservesOutsideAndBothChannels();
		gpu.VertexAoPreservesOriginalPrecision();
		gpu.CompositeRespectsCurrentMaskBounds();
		SelectionAndCoverage(gpu);
		MaskSamplingMatchesOutputGrid(gpu);
		FractionalFeatherCoverage(gpu);
		VisibilityAndDistance(gpu);
		CurrentDepthAllowsLargerAllocation(gpu);
		CropsDirtyRegionsAndStereo(gpu);
		GpuSupportCoverageCases(gpu, false);
		GpuSupportQuantizationCases(gpu);
		std::cout << "Production character capture/mask HLSL passed WARP synthetic tests\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
