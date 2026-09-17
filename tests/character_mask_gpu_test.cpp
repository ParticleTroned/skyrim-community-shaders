#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Features/Upscaling/NeuralRendering/CharacterCategoryFormat.h"
#include "ShaderPackageIncludes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
	};
	static_assert(sizeof(MaskConstants) == 480);

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
		std::uint32_t padding = 0;
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
		explicit Harness(const std::filesystem::path& shaderDirectory)
		{
			D3D_FEATURE_LEVEL actual{};
			constexpr D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
					  &requested, 1, D3D11_SDK_VERSION, &device_, &actual, &context_),
				"Create WARP device");
			auto maskBlob = Compile(shaderDirectory / "DLSS5CharacterMaskCS.hlsl");
			CheckMaskLayout(maskBlob.Get());
			Check(device_->CreateComputeShader(maskBlob->GetBufferPointer(),
					  maskBlob->GetBufferSize(), nullptr, &maskShader_),
				"Create mask shader");
			auto captureBlob = Compile(shaderDirectory / "DLSS5CharacterCaptureCS.hlsl");
			Check(device_->CreateComputeShader(captureBlob->GetBufferPointer(),
					  captureBlob->GetBufferSize(), nullptr, &captureShader_),
				"Create capture shader");
			PackageIncludes includes(shaderDirectory);
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
			D3D11_SHADER_VARIABLE_DESC boundsDesc{};
			Check(blendBuffer->GetVariableByName("CharacterMaskBounds")->GetDesc(&boundsDesc),
				"Reflect character mask bounds");
			Require(boundsDesc.StartOffset == offsetof(BlendConstants, characterMaskBounds),
				"BlendConstants bounds offset differs");
			Check(device_->CreateComputeShader(blendBlob->GetBufferPointer(),
					  blendBlob->GetBufferSize(), nullptr, &blendShader_),
				"Create blend shader");
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
		ComPtr<ID3DBlob> Compile(const std::filesystem::path& path,
			ID3DInclude* includes = D3D_COMPILE_STANDARD_FILE_INCLUDE)
		{
			ComPtr<ID3DBlob> shader, errors;
			const auto result = D3DCompileFromFile(path.c_str(), nullptr,
				includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, &shader, &errors);
			if (FAILED(result) && errors)
				throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
			Check(result, "Compile production shader");
			return shader;
		}

		void CheckMaskLayout(ID3DBlob* blob)
		{
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(),
					  __uuidof(ID3D11ShaderReflection), &reflection),
				"Reflect mask shader");
			auto* buffer = reflection->GetConstantBufferByName("CharacterMaskCB");
			D3D11_SHADER_BUFFER_DESC description{};
			Check(buffer->GetDesc(&description), "Reflect mask constants");
			Require(description.Size == sizeof(MaskConstants), "MaskConstants shader ABI size changed");
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
		}

		template <typename T>
		Texture MakeTexture(std::uint32_t width, std::uint32_t height,
			DXGI_FORMAT format, std::uint32_t bindFlags, const std::vector<T>& values)
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
			if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
				Check(device_->CreateShaderResourceView(result.resource.Get(), nullptr, &result.srv), "Create SRV");
			if (bindFlags & D3D11_BIND_UNORDERED_ACCESS)
				Check(device_->CreateUnorderedAccessView(result.resource.Get(), nullptr, &result.uav), "Create UAV");
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
		ComPtr<ID3D11ComputeShader> maskShader_, captureShader_, blendShader_;
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
					const auto result = gpu.Blend(constants, width, height, model,
						std::vector<Color>(width * height, baseline), mask,
						std::vector<Color>(width * 2 * height, baseline));
					for (std::uint32_t y = 0; y < height; ++y) {
						for (std::uint32_t x = 0; x < width * 2; ++x) {
							const bool selected = x / width == eye && (!character || x % width < 2 || x % width >= 6);
							for (std::uint32_t channel = 0; channel < 4; ++channel) {
								const float strength = selected ? (character ? mask[y * width + x % width] / 255.0f : 1.0f) : 0.0f;
								const float expected = finalLdr && channel == 3 ? baseline[channel] :
								                                                  baseline[channel] + strength * (neural[channel] - baseline[channel]);
								Require(std::isfinite(result[y * width * 2 + x][channel]) &&
											std::abs(result[y * width * 2 + x][channel] - expected) < 0.0002f,
									"Full-image corners, opposite eye, or reduced ROI baseline changed");
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

int wmain(int argc, wchar_t** argv)
{
	try {
		Require(argc == 2 || (argc == 3 && std::wstring(argv[2]) == L"--final-ldr-only"),
			"Expected production shader directory and optional --final-ldr-only");
		Harness gpu(argv[1]);
		FullImageAndReducedSelection(gpu);
		if (argc == 3) {
			FinalLdrColorModes(gpu);
			return 0;
		}
		gpu.CapturePreservesOutsideAndBothChannels();
		gpu.VertexAoPreservesOriginalPrecision();
		gpu.CompositeRespectsCurrentMaskBounds();
		SelectionAndCoverage(gpu);
		VisibilityAndDistance(gpu);
		CurrentDepthAllowsLargerAllocation(gpu);
		CropsDirtyRegionsAndStereo(gpu);
		std::cout << "Production character capture/mask HLSL passed WARP synthetic tests\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
