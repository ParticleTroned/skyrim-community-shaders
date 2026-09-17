#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d11shader.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Features/Upscaling/NeuralRendering/CharacterCategoryFormat.h"
#include "Features/Upscaling/NeuralRendering/CharacterMaskReadback.h"

namespace
{
	using Microsoft::WRL::ComPtr;
	using Bounds = std::array<std::uint32_t, 4>;
	using Clock = std::chrono::steady_clock;
	using NeuralRendering::CharacterMaskReadbackStatus;
	static_assert(sizeof(Bounds) == 16);

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Check(HRESULT result, const char* operation)
	{
		Require(SUCCEEDED(result), std::string(operation) + " failed: " +
									   std::to_string(static_cast<unsigned long>(result)));
	}

	std::vector<Bounds> Reference(std::uint32_t width, std::uint32_t height,
		const std::vector<std::uint8_t>& pixels)
	{
		const auto tilesX = (width + 31u) / 32u;
		std::vector<Bounds> bounds(tilesX * ((height + 31u) / 32u));
		for (std::uint32_t y = 0; y < height; ++y) {
			for (std::uint32_t x = 0; x < width; ++x) {
				if (pixels[y * width + x] == 0)
					continue;
				auto& tile = bounds[(y / 32u) * tilesX + x / 32u];
				if (tile[2] == 0) {
					tile = { x, y, x + 1u, y + 1u };
				} else {
					tile = { std::min(tile[0], x), std::min(tile[1], y),
						std::max(tile[2], x + 1u), std::max(tile[3], y + 1u) };
				}
			}
		}
		return bounds;
	}

	class Harness
	{
		struct Sample
		{
			Bounds size{};
			std::vector<Bounds> expected;
			ComPtr<ID3D11Texture2D> mask;
			ComPtr<ID3D11ShaderResourceView> maskView;
			ComPtr<ID3D11Buffer> output, staging, constants;
			ComPtr<ID3D11UnorderedAccessView> outputView;
			ComPtr<ID3D11Query> query;
		};

		struct Texture
		{
			ComPtr<ID3D11Texture2D> resource;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11UnorderedAccessView> uav;
		};

		// Real queued GPU work, not a fake HRESULT or a CPU-only sleep in the
		// readback loop. The CPU can independently release this shared fence.
		struct QueueGate
		{
			ComPtr<ID3D12Fence> cpuFence;
			ComPtr<ID3D11Fence> gpuFence;
			~QueueGate()
			{
				if (cpuFence)
					cpuFence->Signal(1);
			}
		};

	public:
		explicit Harness(const std::filesystem::path& shaderDirectory)
		{
			constexpr D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
			D3D_FEATURE_LEVEL actual{};
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
					  &requested, 1, D3D11_SDK_VERSION, &device_, &actual, &context_),
				"Create WARP device");
			ComPtr<ID3DBlob> code, errors;
			const auto path = shaderDirectory / "DLSS5CharacterMaskBoundsCS.hlsl";
			const auto result = D3DCompileFromFile(path.c_str(), nullptr,
				D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
					D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, &code, &errors);
			if (FAILED(result) && errors)
				throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
			Check(result, "Compile production mask-bounds shader");
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
					  __uuidof(ID3D11ShaderReflection), &reflection),
				"Reflect mask-bounds shader");
			UINT threadsX{}, threadsY{}, threadsZ{};
			reflection->GetThreadGroupSize(&threadsX, &threadsY, &threadsZ);
			Require(threadsX == 8 && threadsY == 8 && threadsZ == 1, "Mask-bounds thread layout");
			auto* cb = reflection->GetConstantBufferByName("CharacterMaskBoundsCB");
			D3D11_SHADER_BUFFER_DESC cbDesc{};
			Check(cb->GetDesc(&cbDesc), "Reflect mask-bounds constants");
			Require(cbDesc.Size == sizeof(Bounds), "Mask-bounds constant-buffer size");
			D3D11_SHADER_VARIABLE_DESC sizeDesc{};
			Check(cb->GetVariableByName("Size")->GetDesc(&sizeDesc), "Reflect Size");
			Require(sizeDesc.StartOffset == 0 && sizeDesc.Size == sizeof(Bounds), "Mask-bounds Size ABI");
			for (const auto& binding : std::array{
					 std::pair{ "CharacterSelectionMask", D3D_SIT_TEXTURE },
					 std::pair{ "TileBounds", D3D_SIT_UAV_RWSTRUCTURED },
					 std::pair{ "CharacterMaskBoundsCB", D3D_SIT_CBUFFER } }) {
				D3D11_SHADER_INPUT_BIND_DESC desc{};
				Check(reflection->GetResourceBindingDescByName(binding.first, &desc), "Reflect resource binding");
				Require(desc.BindPoint == 0 && desc.BindCount == 1 && desc.Type == binding.second,
					std::string("Mask-bounds binding: ") + binding.first);
			}
			Check(device_->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(),
					  nullptr, &shader_),
				"Create mask-bounds shader");
		}

		Sample CreateSample(const std::string& name, std::uint32_t width, std::uint32_t height,
			const std::vector<std::uint8_t>& pixels)
		{
			Require(width && height && pixels.size() == width * height, name + ": input dimensions");
			Sample sample;
			sample.size = { width, height, (width + 31u) / 32u, 0u };
			sample.expected = Reference(width, height, pixels);
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = width;
			textureDesc.Height = height;
			textureDesc.MipLevels = textureDesc.ArraySize = textureDesc.SampleDesc.Count = 1;
			textureDesc.Format = DXGI_FORMAT_R8_UNORM;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			const D3D11_SUBRESOURCE_DATA maskData{ pixels.data(), width, 0 };
			Check(device_->CreateTexture2D(&textureDesc, &maskData, &sample.mask), "Create R8 mask");
			Check(device_->CreateShaderResourceView(sample.mask.Get(), nullptr, &sample.maskView), "Create R8 mask SRV");
			D3D11_BUFFER_DESC bufferDesc{};
			bufferDesc.ByteWidth = static_cast<UINT>(sample.expected.size() * sizeof(Bounds));
			bufferDesc.Usage = D3D11_USAGE_DEFAULT;
			bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			bufferDesc.StructureByteStride = sizeof(Bounds);
			// Poison every record: all tiles, even empty ones, must overwrite it.
			const std::vector<Bounds> poison(sample.expected.size(), Bounds{ 999u, 888u, 777u, 666u });
			const D3D11_SUBRESOURCE_DATA boundsData{ poison.data(), 0, 0 };
			Check(device_->CreateBuffer(&bufferDesc, &boundsData, &sample.output), "Create tile bounds");
			D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc{};
			viewDesc.Format = DXGI_FORMAT_UNKNOWN;
			viewDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			viewDesc.Buffer.NumElements = static_cast<UINT>(sample.expected.size());
			Check(device_->CreateUnorderedAccessView(sample.output.Get(), &viewDesc, &sample.outputView), "Create tile-bounds UAV");
			bufferDesc.Usage = D3D11_USAGE_STAGING;
			bufferDesc.BindFlags = bufferDesc.MiscFlags = bufferDesc.StructureByteStride = 0;
			bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device_->CreateBuffer(&bufferDesc, nullptr, &sample.staging), "Create bounds readback");
			bufferDesc = {};
			bufferDesc.ByteWidth = sizeof(Bounds);
			bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
			bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			const D3D11_SUBRESOURCE_DATA constantData{ sample.size.data(), 0, 0 };
			Check(device_->CreateBuffer(&bufferDesc, &constantData, &sample.constants), "Create bounds constants");
			const D3D11_QUERY_DESC queryDesc{ D3D11_QUERY_EVENT, 0 };
			Check(device_->CreateQuery(&queryDesc, &sample.query), "Create bounds completion event");
			return sample;
		}

		void Queue(const Sample& sample)
		{
			// Match production: resolve left its mask bound for writing. Unbind
			// the old outputs BEFORE requesting that same texture as an input.
			std::array<ID3D11UnorderedAccessView*, 2> nullUavs{};
			context_->CSSetUnorderedAccessViews(0, 2, nullUavs.data(), nullptr);
			context_->CSSetShader(shader_.Get(), nullptr, 0);
			context_->CSSetConstantBuffers(0, 1, sample.constants.GetAddressOf());
			context_->CSSetShaderResources(0, 1, sample.maskView.GetAddressOf());
			context_->CSSetUnorderedAccessViews(0, 1, sample.outputView.GetAddressOf(), nullptr);
			ComPtr<ID3D11ShaderResourceView> boundMask;
			context_->CSGetShaderResources(0, 1, &boundMask);
			Require(boundMask.Get() == sample.maskView.Get(), "Bounds input was silently nulled by an output hazard");
			context_->Dispatch(sample.size[2], (sample.size[1] + 31u) / 32u, 1);
			context_->CSSetUnorderedAccessViews(0, 2, nullUavs.data(), nullptr);
			context_->CopyResource(sample.staging.Get(), sample.output.Get());
			context_->End(sample.query.Get());
		}

		void EarlyCategoryBoundsCases(const std::filesystem::path& shaderDirectory)
		{
			ComPtr<ID3DBlob> code, errors;
			const D3D_SHADER_MACRO defines[]{ { "EARLY_CATEGORY_BOUNDS", "1" }, { nullptr, nullptr } };
			const auto compiled = D3DCompileFromFile((shaderDirectory / "DLSS5CharacterMaskBoundsCS.hlsl").c_str(), defines,
				D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, &code, &errors);
			if (FAILED(compiled) && errors)
				throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
			Check(compiled, "Compile early category bounds");
			ComPtr<ID3D11ComputeShader> shader;
			Check(device_->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader), "Create early bounds shader");
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)), "Reflect early bounds");
			D3D11_SHADER_BUFFER_DESC reflected{};
			Check(reflection->GetConstantBufferByName("CharacterMaskBoundsCB")->GetDesc(&reflected), "Reflect early bounds CB");
			Require(reflected.Size == 48u, "Early bounds constants must match production ABI");
			constexpr std::uint32_t width = 63, height = 49;
			const std::array<Bounds, 2> valid{ Bounds{ 5, 7, 50, 35 }, Bounds{ 0, 0, width, height } };
			const std::array<std::uint16_t, 6> codes{ 0, 21845, 43690, 65535, 257, 32768 };
			for (const auto eyeCount : { 1u, 2u }) {
				std::vector<std::array<std::uint16_t, 2>> pixels(width * height * eyeCount);
				for (std::uint32_t y = 0; y < height; ++y)
					for (std::uint32_t x = 0; x < width * eyeCount; ++x)
						pixels[y * width * eyeCount + x] = { 12345, codes[(x * 3u + y * 5u) % codes.size()] };
				auto source = MakeTexture(width * eyeCount, height, NeuralRendering::kCharacterCategoryFormat, pixels);
				for (std::uint32_t selection = 0; selection < 8; ++selection) {
					auto sample = CreateSample("early active-view bounds", width, height * eyeCount, std::vector<std::uint8_t>(width * height * eyeCount));
					std::vector<Bounds> expected;
					for (std::uint32_t eye = 0; eye < eyeCount; ++eye) {
						std::vector<std::uint8_t> mask(width * height);
						for (std::uint32_t y = 0; y < height; ++y)
							for (std::uint32_t x = 0; x < width; ++x) {
								const auto codeIndex = ((x + eye * width) * 3u + y * 5u) % codes.size();
								const bool inside = x >= valid[eye][0] && y >= valid[eye][1] &&
								                    x < valid[eye][0] + valid[eye][2] && y < valid[eye][1] + valid[eye][3];
								mask[y * width + x] = inside && codeIndex >= 1 && codeIndex <= 3 && (selection & (1u << (codeIndex - 1u))) ? 1 : 0;
							}
						const auto eyeBounds = Reference(width, height, mask);
						expected.insert(expected.end(), eyeBounds.begin(), eyeBounds.end());
					}
					Require(expected.size() == sample.expected.size(), "Early active-view staging extent");
					const std::array<Bounds, 3> constants{ Bounds{ width, height, (width + 31u) / 32u, selection << 1u }, valid[0], valid[1] };
					D3D11_BUFFER_DESC desc{};
					desc.ByteWidth = sizeof(constants);
					desc.Usage = D3D11_USAGE_IMMUTABLE;
					desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
					const D3D11_SUBRESOURCE_DATA data{ constants.data(), 0, 0 };
					ComPtr<ID3D11Buffer> cb;
					Check(device_->CreateBuffer(&desc, &data, &cb), "Create early active-view CB");
					context_->CSSetShader(shader.Get(), nullptr, 0);
					context_->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
					context_->CSSetShaderResources(0, 1, source.srv.GetAddressOf());
					context_->CSSetUnorderedAccessViews(0, 1, sample.outputView.GetAddressOf(), nullptr);
					context_->Dispatch((width + 31u) / 32u, (height + 31u) / 32u, eyeCount);
					ID3D11UnorderedAccessView* empty = nullptr;
					context_->CSSetUnorderedAccessViews(0, 1, &empty, nullptr);
					context_->CopyResource(sample.staging.Get(), sample.output.Get());
					context_->End(sample.query.Get());
					context_->Flush();
					std::vector<Bounds> actual(expected.size());
					Require(Read(sample, actual, Clock::now() + std::chrono::seconds(2)).Ready(), "Early category readback");
					Require(actual == expected, "Early category bounds must honor mono and stereo eye strides, category toggles, exclusions and capture validity");
					Require(NeuralRendering::PollCharacterMaskBounds(context_.Get(), sample.query.Get(), sample.staging.Get(),
								std::as_writable_bytes(std::span(actual)))
								.Ready(),
						"Completed early result must support nonblocking readback");
					++cases_;
				}
			}
		}

		void ConnectedPipelineCases(const std::filesystem::path& shaderDirectory)
		{
			// Reflected below against the production shader. Do not silently run
			// a test-only constants layout that differs from the shipped shader.
			struct alignas(16) MaskConstants
			{
				Bounds outputAndSourceSize{}, sourceCrop{}, options{};
				std::array<float, 4> featherOptions{}, visibilityOptions{}, depthLinearization{};
				std::array<float, 16> cameraProjInverse{};
				std::array<float, 4> jitter{}, categoryStrengths{};
				std::array<std::array<float, 4>, 16> eligibilityRectangles{};
				Bounds dispatchRegion{}, authoredRegion{};
			};
			static_assert(sizeof(MaskConstants) == 480);
			const auto compile = [&](const wchar_t* name) {
				ComPtr<ID3DBlob> code, errors;
				const auto path = shaderDirectory / name;
				const auto result = D3DCompileFromFile(path.c_str(), nullptr,
					D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
					D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
						D3DCOMPILE_OPTIMIZATION_LEVEL3,
					0, &code, &errors);
				if (FAILED(result) && errors)
					throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
				Check(result, "Compile connected production shader");
				return code;
			};
			const auto captureCode = compile(L"DLSS5CharacterCaptureCS.hlsl");
			const auto resolveCode = compile(L"DLSS5CharacterMaskCS.hlsl");
			const auto depthCode = compile(L"DLSS5DepthRegionCS.hlsl");
			ComPtr<ID3D11ComputeShader> captureShader, resolveShader, depthShader;
			Check(device_->CreateComputeShader(captureCode->GetBufferPointer(), captureCode->GetBufferSize(),
					  nullptr, &captureShader),
				"Create connected capture shader");
			Check(device_->CreateComputeShader(resolveCode->GetBufferPointer(), resolveCode->GetBufferSize(),
					  nullptr, &resolveShader),
				"Create connected mask shader");
			Check(device_->CreateComputeShader(depthCode->GetBufferPointer(), depthCode->GetBufferSize(),
					  nullptr, &depthShader),
				"Create connected raw-depth shader");
			ComPtr<ID3D11ShaderReflection> depthReflection;
			Check(D3DReflect(depthCode->GetBufferPointer(), depthCode->GetBufferSize(),
					  IID_PPV_ARGS(&depthReflection)),
				"Reflect connected raw-depth shader");
			auto* depthCb = depthReflection->GetConstantBufferByName("DepthRegionCB");
			D3D11_SHADER_BUFFER_DESC depthCbDesc{};
			Check(depthCb->GetDesc(&depthCbDesc), "Reflect depth-region constants");
			Require(depthCbDesc.Size == sizeof(Bounds) * 2, "Depth-region constants size");
			for (const auto& field : std::array{ std::pair{ "Offsets", 0u }, std::pair{ "Extent", 16u } }) {
				D3D11_SHADER_VARIABLE_DESC desc{};
				Check(depthCb->GetVariableByName(field.first)->GetDesc(&desc), "Reflect depth-region field");
				Require(desc.StartOffset == field.second, "Depth-region constants offset");
			}
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(resolveCode->GetBufferPointer(), resolveCode->GetBufferSize(),
					  IID_PPV_ARGS(&reflection)),
				"Reflect connected mask shader");
			auto* reflectedCb = reflection->GetConstantBufferByName("CharacterMaskCB");
			D3D11_SHADER_BUFFER_DESC reflectedDesc{};
			Check(reflectedCb->GetDesc(&reflectedDesc), "Reflect connected mask constants");
			Require(reflectedDesc.Size == sizeof(MaskConstants), "Connected mask constants size");
			for (const auto& field : std::array{
					 std::pair{ "OutputAndSourceSize", offsetof(MaskConstants, outputAndSourceSize) },
					 std::pair{ "SourceCrop", offsetof(MaskConstants, sourceCrop) },
					 std::pair{ "Options", offsetof(MaskConstants, options) },
					 std::pair{ "FeatherOptions", offsetof(MaskConstants, featherOptions) },
					 std::pair{ "VisibilityOptions", offsetof(MaskConstants, visibilityOptions) },
					 std::pair{ "DepthLinearization", offsetof(MaskConstants, depthLinearization) },
					 std::pair{ "CameraProjInverse", offsetof(MaskConstants, cameraProjInverse) },
					 std::pair{ "Jitter", offsetof(MaskConstants, jitter) },
					 std::pair{ "CategoryStrengths", offsetof(MaskConstants, categoryStrengths) },
					 std::pair{ "EligibilityRectangles", offsetof(MaskConstants, eligibilityRectangles) },
					 std::pair{ "DispatchRegion", offsetof(MaskConstants, dispatchRegion) },
					 std::pair{ "AuthoredRegion", offsetof(MaskConstants, authoredRegion) } }) {
				D3D11_SHADER_VARIABLE_DESC desc{};
				Check(reflectedCb->GetVariableByName(field.first)->GetDesc(&desc), "Reflect connected constants field");
				Require(desc.StartOffset == field.second, std::string("Connected constants offset: ") + field.first);
			}
			constexpr UINT inputWidth = 80, inputHeight = 64, packedWidth = inputWidth * 2;
			constexpr UINT outputWidth = 128, outputHeight = 96;
			constexpr Bounds crop{ 8, 8, 64, 48 };
			using Tuple = std::array<std::uint16_t, 2>;
			std::vector<Tuple> sourcePixels(packedWidth * inputHeight);
			const auto categoryAt = [&](int x, int y, unsigned eye) -> std::uint16_t {
				x -= static_cast<int>(eye * 2);
				if (x >= 20 && x < 28 && y >= 20 && y < 28)
					return 21845;  // face, encoded 1/3
				if (x >= 52 && x < 60 && y >= 36 && y < 44)
					return y < 40 ? 43690 : 65535;  // skin and hair
				return 0;
			};
			for (unsigned eye = 0; eye < 2; ++eye)
				for (UINT y = 0; y < inputHeight; ++y)
					for (UINT x = 0; x < inputWidth; ++x)
						sourcePixels[y * packedWidth + eye * inputWidth + x] = { 32123, categoryAt(x, y, eye) };
			auto categories = MakeTexture(packedWidth, inputHeight, NeuralRendering::kCharacterCategoryFormat, sourcePixels);
			// Real engine-style depth-stencil source, not an ordinary R32 texture.
			// Cropped CopySubresourceRegion from this resource is unsupported.
			Texture depth;
			D3D11_TEXTURE2D_DESC depthDesc{};
			depthDesc.Width = packedWidth;
			depthDesc.Height = inputHeight;
			depthDesc.MipLevels = depthDesc.ArraySize = depthDesc.SampleDesc.Count = 1;
			depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
			depthDesc.Usage = D3D11_USAGE_DEFAULT;
			depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
			std::vector<std::uint32_t> depthPixels(packedWidth * inputHeight);
			for (UINT y = 0; y < inputHeight; ++y) {
				for (UINT x = 0; x < packedWidth; ++x) {
					const float raw = 0.98f + static_cast<float>(x / inputWidth) * 0.003f +
					                  static_cast<float>(x % inputWidth) * 0.00001f + static_cast<float>(y) * 0.000005f;
					depthPixels[y * packedWidth + x] = static_cast<std::uint32_t>(std::lround(raw * 16777215.0)) | (29u << 24u);
				}
			}
			const D3D11_SUBRESOURCE_DATA depthData{ depthPixels.data(), packedWidth * sizeof(std::uint32_t), 0 };
			Check(device_->CreateTexture2D(&depthDesc, &depthData, &depth.resource), "Create actual depth-stencil source");
			D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
			depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			depthSrvDesc.Texture2D.MipLevels = 1;
			Check(device_->CreateShaderResourceView(depth.resource.Get(), &depthSrvDesc, &depth.srv), "Create D24 depth SRV");
			D3D11_DEPTH_STENCIL_VIEW_DESC depthDsvDesc{};
			depthDsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthDsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			ComPtr<ID3D11DepthStencilView> depthDsv;
			Check(device_->CreateDepthStencilView(depth.resource.Get(), &depthDsvDesc, &depthDsv), "Create D24 depth-stencil view");
			// Poison uncaptured texels: AuthoredRegion must forbid using them.
			auto frozenCategories = MakeTexture(packedWidth, inputHeight, NeuralRendering::kCharacterCategoryFormat,
				std::vector<Tuple>(packedWidth * inputHeight, Tuple{ 65535, 65535 }));
			auto frozenDepth = MakeTexture(packedWidth, inputHeight, DXGI_FORMAT_R32_FLOAT,
				std::vector<float>(packedWidth * inputHeight, 0.0f));
			auto currentDepth = MakeTexture(crop[2], crop[3], DXGI_FORMAT_R32_FLOAT,
				std::vector<float>(crop[2] * crop[3], 0.0f));
			auto intermediateDepth = MakeTexture(inputWidth, inputHeight, DXGI_FORMAT_R32_FLOAT,
				std::vector<float>(inputWidth * inputHeight, -7.0f));
			D3D11_TEXTURE2D_DESC readbackDesc{};
			currentDepth.resource->GetDesc(&readbackDesc);
			readbackDesc.Usage = D3D11_USAGE_STAGING;
			readbackDesc.BindFlags = 0;
			readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			std::array<ComPtr<ID3D11Texture2D>, 2> depthReadbacks;
			for (auto& readback : depthReadbacks)
				Check(device_->CreateTexture2D(&readbackDesc, nullptr, &readback), "Create copied raw-depth readback");
			std::array<Sample, 2> eyes{
				CreateSample("connected left", outputWidth, outputHeight, std::vector<std::uint8_t>(outputWidth * outputHeight)),
				CreateSample("connected right", outputWidth, outputHeight, std::vector<std::uint8_t>(outputWidth * outputHeight))
			};
			std::array<ComPtr<ID3D11UnorderedAccessView>, 2> maskUavs;
			for (unsigned eye = 0; eye < 2; ++eye)
				Check(device_->CreateUnorderedAccessView(eyes[eye].mask.Get(), nullptr, &maskUavs[eye]), "Create connected mask UAV");
			const auto makeConstants = [&](UINT bytes) {
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = bytes;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				ComPtr<ID3D11Buffer> constants;
				Check(device_->CreateBuffer(&desc, nullptr, &constants), "Create mutable connected constants");
				return constants;
			};
			auto captureConstants = makeConstants(sizeof(Bounds));
			auto maskConstants = makeConstants(sizeof(MaskConstants));
			auto depthConstants = makeConstants(sizeof(Bounds) * 2);
			std::array<ID3D11UnorderedAccessView*, 2> nullUavs{};
			std::array<ID3D11ShaderResourceView*, 3> nullSrvs{};
			for (unsigned frame = 0; frame < 4; ++frame) {
				if (frame == 3) {
					for (auto& pixel : sourcePixels) pixel[1] = 0;
					context_->UpdateSubresource(categories.resource.Get(), 0, nullptr, sourcePixels.data(), packedWidth * sizeof(Tuple), 0);
				}
				// A zero/near-plane current guide must reject the positive authored
				// surface, reproducing the live visibility counters. Restoring the
				// correct guide restores coverage without changing actor categories.
				context_->CSSetShaderResources(0, 3, nullSrvs.data());
				context_->CSSetUnorderedAccessViews(0, 2, nullUavs.data(), nullptr);
				context_->CSSetShader(captureShader.Get(), nullptr, 0);
				context_->CSSetConstantBuffers(0, 1, captureConstants.GetAddressOf());
				std::array<ID3D11ShaderResourceView*, 2> captureSrvs{ categories.srv.Get(), depth.srv.Get() };
				std::array<ID3D11UnorderedAccessView*, 2> captureUavs{ frozenCategories.uav.Get(), frozenDepth.uav.Get() };
				context_->CSSetShaderResources(0, 2, captureSrvs.data());
				context_->CSSetUnorderedAccessViews(0, 2, captureUavs.data(), nullptr);
				for (unsigned eye = 0; eye < 2; ++eye) {
					const Bounds region{ eye * inputWidth + crop[0], crop[1], crop[2], crop[3] };
					context_->UpdateSubresource(captureConstants.Get(), 0, nullptr, region.data(), 0, 0);
					context_->Dispatch((crop[2] + 7) / 8, (crop[3] + 7) / 8, 1);
				}
				// Unbind only overwritten slots, not ClearState: exercise real
				// UAV->SRV transitions between all three production shaders.
				for (unsigned eye = 0; eye < 2; ++eye) {
					context_->CSSetShaderResources(0, 3, nullSrvs.data());
					context_->CSSetUnorderedAccessViews(0, 2, nullUavs.data(), nullptr);
					const std::array<Bounds, 2> depthRegion{
						Bounds{ eye * inputWidth + crop[0], crop[1], crop[0], crop[1] },
						Bounds{ crop[2], crop[3], 0, 0 }
					};
					context_->UpdateSubresource(depthConstants.Get(), 0, nullptr, depthRegion.data(), 0, 0);
					context_->CSSetShader(depthShader.Get(), nullptr, 0);
					context_->CSSetConstantBuffers(0, 1, depthConstants.GetAddressOf());
					context_->CSSetShaderResources(0, 1, depth.srv.GetAddressOf());
					context_->CSSetUnorderedAccessViews(0, 1, intermediateDepth.uav.GetAddressOf(), nullptr);
					context_->Dispatch((crop[2] + 7) / 8, (crop[3] + 7) / 8, 1);
					// Submit encoding retains input-local offsets in the full-eye
					// intermediate; foveated extraction then legally crops R32 to 0,0.
					context_->CSSetUnorderedAccessViews(0, 2, nullUavs.data(), nullptr);
					const D3D11_BOX sourceBox{ crop[0], crop[1], 0,
						crop[0] + crop[2], crop[1] + crop[3], 1 };
					context_->CopySubresourceRegion(currentDepth.resource.Get(), 0, 0, 0, 0,
						intermediateDepth.resource.Get(), 0, &sourceBox);
					if (frame == 1) {
						const std::array<float, 4> zero{};
						context_->ClearUnorderedAccessViewFloat(currentDepth.uav.Get(), zero.data());
					}
					MaskConstants constants{};
					constants.outputAndSourceSize = { outputWidth, outputHeight, inputWidth, inputHeight };
					constants.sourceCrop = { eye * inputWidth, crop[0], crop[1], crop[2] };
					constants.options = { crop[3], 1, 0, 0 };
					constants.visibilityOptions = { 0.001f, 1.0f, 0.0f, 0.0f };
					constants.depthLinearization = { 100000, 5, 99995, 500000 };
					constants.jitter = { 0.25f, -0.375f, 0, 0 };
					constants.categoryStrengths = { 1, 1, 1, 0 };
					constants.eligibilityRectangles[0] = { 0, 0, outputWidth, outputHeight };
					constants.dispatchRegion = { 0, 0, outputWidth, outputHeight };
					constants.authoredRegion = crop;
					context_->UpdateSubresource(maskConstants.Get(), 0, nullptr, &constants, 0, 0);
					context_->CSSetUnorderedAccessViews(0, 2, nullUavs.data(), nullptr);
					context_->CSSetShader(resolveShader.Get(), nullptr, 0);
					context_->CSSetConstantBuffers(0, 1, maskConstants.GetAddressOf());
					std::array<ID3D11ShaderResourceView*, 3> resolveSrvs{
						frozenCategories.srv.Get(), frozenDepth.srv.Get(), currentDepth.srv.Get()
					};
					context_->CSSetShaderResources(0, 3, resolveSrvs.data());
					context_->CSSetUnorderedAccessViews(0, 1, maskUavs[eye].GetAddressOf(), nullptr);
					context_->Dispatch((outputWidth + 7) / 8, (outputHeight + 7) / 8, 1);
					context_->CopyResource(depthReadbacks[eye].Get(), currentDepth.resource.Get());
					Queue(eyes[eye]);  // Deliberately inherit the mask's output binding.
					std::vector<std::uint8_t> expectedMask(outputWidth * outputHeight);
					for (UINT y = 0; y < outputHeight; ++y) {
						for (UINT x = 0; x < outputWidth; ++x) {
							const float sx = (x + 0.5f) * crop[2] / outputWidth - 0.5f - constants.jitter[0];
							const float sy = (y + 0.5f) * crop[3] / outputHeight - 0.5f - constants.jitter[1];
							const int bx = static_cast<int>(std::floor(sx)), by = static_cast<int>(std::floor(sy));
							float coverage = 0;
							for (int dy = 0; dy < 2; ++dy)
								for (int dx = 0; dx < 2; ++dx) {
									const auto ix = std::clamp(bx + dx, 0, static_cast<int>(crop[2]) - 1) + crop[0];
									const auto iy = std::clamp(by + dy, 0, static_cast<int>(crop[3]) - 1) + crop[1];
									if ((frame == 0 || frame == 2) && categoryAt(ix, iy, eye) != 0)
										coverage += (dx ? sx - bx : 1 - (sx - bx)) * (dy ? sy - by : 1 - (sy - by));
								}
							expectedMask[y * outputWidth + x] = coverage > 0.5f / 255.0f ? 255 : 0;
						}
					}
					eyes[eye].expected = Reference(outputWidth, outputHeight, expectedMask);
				}
				context_->Flush();
				const auto deadline = Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget;
				for (unsigned eye = 0; eye < 2; ++eye) {
					std::vector<Bounds> actual(eyes[eye].expected.size());
					Require(Read(eyes[eye], actual, deadline).Ready(), "Connected pipeline readback must complete");
					Require(actual == eyes[eye].expected, "Connected stereo capture/resolve/bounds differs from exact coverage");
					// Bounds' completed event covers this earlier texture copy too.
					D3D11_MAPPED_SUBRESOURCE mapped{};
					Check(context_->Map(depthReadbacks[eye].Get(), 0, D3D11_MAP_READ,
							  D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped),
						"Map copied D24 raw depth without waiting");
					bool rawValuesMatch = true;
					for (UINT y = 0; y < crop[3]; ++y) {
						const auto* row = reinterpret_cast<const float*>(static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch);
						for (UINT x = 0; x < crop[2]; ++x) {
							const auto sourcePixel = (y + crop[1]) * packedWidth + eye * inputWidth + x + crop[0];
							const float expectedDepth = frame == 1 ? 0.0f :
							                                         static_cast<float>(depthPixels[sourcePixel] & 0x00ffffffu) / 16777215.0f;
							rawValuesMatch = rawValuesMatch && std::abs(row[x] - expectedDepth) <= 1.0e-7f;
						}
					}
					context_->Unmap(depthReadbacks[eye].Get(), 0);
					Require(rawValuesMatch, "Depth-stencil extraction must preserve numeric raw depth");
					++cases_;
				}
			}
		}

		auto Read(const Sample& sample, std::vector<Bounds>& destination, Clock::time_point deadline,
			NeuralRendering::CharacterMaskReadbackCompletion completion = {})
		{
			return NeuralRendering::ReadCharacterMaskBounds(context_.Get(), sample.query.Get(),
				sample.staging.Get(), std::as_writable_bytes(std::span(destination)), deadline, completion);
		}

		void Case(const std::string& name, std::uint32_t width, std::uint32_t height,
			const std::vector<std::uint8_t>& pixels)
		{
			const auto sample = CreateSample(name, width, height, pixels);
			const auto dispatchAndRead = [&]() {
				Queue(sample);
				context_->Flush();
				std::vector<Bounds> actual(sample.expected.size());
				const auto result = Read(sample, actual, Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget);
				Require(result.Ready(), name + ": production readback failed: " + result.Reason());
				return actual;
			};
			Require(dispatchAndRead() == sample.expected, name + ": GPU bounds differ from exact CPU R8 bounds");
			++cases_;
			// Reuse the populated output without clearing it. An empty next mask
			// must produce only zero records, not any previous-frame bounds.
			const std::vector<std::uint8_t> empty(pixels.size());
			context_->UpdateSubresource(sample.mask.Get(), 0, nullptr, empty.data(), width, 0);
			Require(dispatchAndRead() == std::vector<Bounds>(sample.expected.size()), name + ": stale bounds after empty mask");
			++cases_;
		}

		void ReadbackCases()
		{
			ComPtr<IDXGIDevice> dxgiDevice;
			ComPtr<IDXGIAdapter> adapter;
			Check(device_.As(&dxgiDevice), "Query WARP DXGI device");
			Check(dxgiDevice->GetAdapter(&adapter), "Get WARP adapter");
			ComPtr<ID3D12Device> fenceDevice;
			Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
					  IID_PPV_ARGS(&fenceDevice)),
				"Create shared-fence WARP device");
			ComPtr<ID3D11Device5> device5;
			ComPtr<ID3D11DeviceContext4> context4;
			Check(device_.As(&device5), "Query fence-capable D3D11 device");
			Check(context_.As(&context4), "Query fence-capable immediate context");
			const auto gateQueue = [&](QueueGate& gate) {
				Check(fenceDevice->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
						  IID_PPV_ARGS(&gate.cpuFence)),
					"Create queued-work fence");
				HANDLE shared = nullptr;
				Check(fenceDevice->CreateSharedHandle(gate.cpuFence.Get(), nullptr,
						  GENERIC_ALL, nullptr, &shared),
					"Share queued-work fence");
				const auto opened = device5->OpenSharedFence(shared, IID_PPV_ARGS(&gate.gpuFence));
				CloseHandle(shared);
				Check(opened, "Open queued-work fence on D3D11");
				Check(context4->Wait(gate.gpuFence.Get(), 1), "Queue GPU work gate");
			};

			std::vector<std::uint8_t> leftPixels(65 * 49), rightPixels(65 * 49);
			leftPixels[3 * 65 + 4] = 1;
			leftPixels[38 * 65 + 61] = 255;
			rightPixels[14 * 65 + 11] = 255;
			rightPixels[48 * 65 + 64] = 1;
			const auto left = CreateSample("queued left eye", 65, 49, leftPixels);
			const auto right = CreateSample("queued right eye", 65, 49, rightPixels);
			{
				ComPtr<ID3D11Fence> completionFence;
				Check(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&completionFence)),
					"Create stereo readback completion fence");
				Queue(left);
				context_->Flush();
				std::vector<Bounds> actualLeft(left.expected.size()), actualRight(right.expected.size());
				Require(Read(left, actualLeft, Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget).Ready(),
					"Left copy must finish before the delayed right copy is queued");
				QueueGate gate;
				gateQueue(gate);
				Queue(right);
				context_->Flush();
				std::vector<Bounds> untouched(right.expected.size(), Bounds{ 81, 82, 83, 84 });
				const auto beforePoll = untouched;
				const auto pendingPoll = NeuralRendering::PollCharacterMaskBounds(context_.Get(), right.query.Get(), right.staging.Get(),
					std::as_writable_bytes(std::span(untouched)));
				Require(pendingPoll.status == CharacterMaskReadbackStatus::Pending && untouched == beforePoll,
					"Nonblocking pending result must preserve destination while GPU is gated");
				Require(pendingPoll.waitMs < 20.0, "Nonblocking poll must not consume the synchronous 50ms budget");
				Check(context4->Signal(completionFence.Get(), 1), "Signal completion after both eye copies");
				context_->Flush();
				const NeuralRendering::CharacterMaskReadbackCompletion completion{ completionFence.Get(), 1 };
				const std::vector<Bounds> sentinel(left.expected.size(), Bounds{ 91, 92, 93, 94 });
				actualLeft = sentinel;
				Require(Read(left, actualLeft, Clock::now() + std::chrono::milliseconds(2), completion).status ==
								CharacterMaskReadbackStatus::Timeout &&
							actualLeft == sentinel,
					"A ready left query must not admit a staging Map while the stereo fence is pending");
				Check(gate.cpuFence->Signal(1), "Release delayed right-eye copy");
				const auto deadline = Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget;
				Require(Read(left, actualLeft, deadline, completion).Ready() &&
							Read(right, actualRight, deadline, completion).Ready(),
					"One fence must admit both current eye copies");
				Require(actualLeft == left.expected && actualRight == right.expected, "Fenced stereo bounds must be exact");
				Require(Read(right, actualRight, Clock::now() - std::chrono::milliseconds(1), completion).Ready(),
					"A completed stereo fence remains readable after the deadline");
				actualLeft = sentinel;
				Require(Read(left, actualLeft, deadline, { completionFence.Get(), 0 }).status ==
								CharacterMaskReadbackStatus::FenceFailed &&
							actualLeft == sentinel,
					"Fence value zero must not qualify a current copy");
				Require(Read(left, actualLeft, deadline, { nullptr, 0, E_FAIL }).status ==
								CharacterMaskReadbackStatus::FenceFailed &&
							actualLeft == sentinel,
					"A failed signal must not fall through to a ready per-eye query");
				{
					QueueGate nextGate;
					gateQueue(nextGate);
					const std::vector<std::uint8_t> empty(leftPixels.size());
					context_->UpdateSubresource(left.mask.Get(), 0, nullptr, empty.data(), 65, 0);
					Queue(left);
					Queue(right);
					Check(context4->Signal(completionFence.Get(), 2), "Signal next stereo copy epoch");
					context_->Flush();
					const NeuralRendering::CharacterMaskReadbackCompletion next{ completionFence.Get(), 2 };
					Require(Read(left, actualLeft, Clock::now() + std::chrono::milliseconds(2), next).status ==
									CharacterMaskReadbackStatus::Timeout &&
								actualLeft == sentinel,
						"The previous completed signal must not admit a newer copy");
					Check(nextGate.cpuFence->Signal(1), "Release next stereo copy epoch");
					Require(Read(left, actualLeft, Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget, next).Ready() &&
								actualLeft == std::vector<Bounds>(left.expected.size()),
						"Only the fresh fenced copy may prove an empty mask");
				}
				context_->UpdateSubresource(left.mask.Get(), 0, nullptr, leftPixels.data(), 65, 0);
				cases_ += 8;
			}
			{
				QueueGate gate;
				gateQueue(gate);
				Queue(left);
				Queue(right);
				context_->Flush();  // Both eyes are submitted before the only flush.
				std::vector<Bounds> actualLeft(left.expected.size()), actualRight(right.expected.size());
				// The gate is still closed, so this deterministically reproduces
				// the old timeout without relying on worker-thread scheduling.
				Require(Read(left, actualLeft, Clock::now() + std::chrono::milliseconds(2)).status ==
							CharacterMaskReadbackStatus::Timeout,
					"Old 2ms deadline must reject queued work");
				std::atomic<HRESULT> signalResult{ E_PENDING };
				std::jthread signal([&] {
					std::this_thread::sleep_for(std::chrono::milliseconds(8));
					signalResult = gate.cpuFence->Signal(1);
				});
				const auto deadline = Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget;
				const auto leftResult = Read(left, actualLeft, deadline);
				const auto rightResult = Read(right, actualRight, deadline);
				signal.join();
				Check(signalResult.load(), "Release delayed GPU work from CPU thread");
				Require(leftResult.Ready() && rightResult.Ready(), "Two queued eyes must share one readiness deadline");
				Require(actualLeft == left.expected && actualRight == right.expected, "Queued eye bounds must be exact");
				cases_ += 2;

				// A previous eye can exhaust the shared deadline. A second eye that
				// is already complete must still get one nonblocking readiness probe.
				std::fill(actualRight.begin(), actualRight.end(), Bounds{});
				Require(Read(right, actualRight, Clock::now() - std::chrono::milliseconds(1)).Ready() &&
							actualRight == right.expected,
					"Already-ready second eye survives an expired shared deadline");
				++cases_;
			}

			{
				QueueGate gate;
				gateQueue(gate);
				Queue(left);
				context_->Flush();
				const std::vector<Bounds> sentinel(left.expected.size(), Bounds{ 91, 92, 93, 94 });
				auto actual = sentinel;
				const auto result = Read(left, actual, Clock::now() + std::chrono::milliseconds(2));
				Require(result.status == CharacterMaskReadbackStatus::Timeout && actual == sentinel,
					"Pending copy must time out without changing destination");
				++cases_;
				Check(gate.cpuFence->Signal(1), "Release timed-out old copy");
				// The owner retires the old marker WITHOUT consuming its data as
				// current coverage, then queues this frame's changed (empty) mask.
				const auto retireDeadline = Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget;
				BOOL complete = FALSE;
				while (!complete && Clock::now() < retireDeadline) {
					Check(context_->GetData(left.query.Get(), &complete, sizeof(complete),
							  D3D11_ASYNC_GETDATA_DONOTFLUSH),
						"Retire old copy marker");
					if (!complete)
						std::this_thread::yield();
				}
				Require(complete && actual == sentinel, "Retirement must not publish stale bounds");
				const std::vector<std::uint8_t> empty(leftPixels.size());
				context_->UpdateSubresource(left.mask.Get(), 0, nullptr, empty.data(), 65, 0);
				Queue(left);
				context_->Flush();
				Require(Read(left, actual, Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget).Ready() &&
							actual == std::vector<Bounds>(left.expected.size()),
					"Only the new copy supplies current empty bounds");
				++cases_;

				std::vector<Bounds> tooSmall(left.expected.size() - 1, Bounds{ 91, 92, 93, 94 });
				const auto before = tooSmall;
				const auto invalid = Read(left, tooSmall, Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget);
				Require(invalid.status == CharacterMaskReadbackStatus::MapUnavailable &&
							invalid.result == E_INVALIDARG && tooSmall == before,
					"Reject incorrect destination size unchanged");
				++cases_;
			}
		}

		std::uint32_t Cases() const { return cases_; }

	private:
		template <class Pixel>
		Texture MakeTexture(UINT width, UINT height, DXGI_FORMAT format, const std::vector<Pixel>& pixels)
		{
			Require(pixels.size() == static_cast<std::size_t>(width) * height, "Connected texture dimensions");
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = format;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			const D3D11_SUBRESOURCE_DATA data{ pixels.data(), static_cast<UINT>(width * sizeof(Pixel)), 0 };
			Texture result;
			Check(device_->CreateTexture2D(&desc, &data, &result.resource), "Create connected texture");
			Check(device_->CreateShaderResourceView(result.resource.Get(), nullptr, &result.srv), "Create connected SRV");
			Check(device_->CreateUnorderedAccessView(result.resource.Get(), nullptr, &result.uav), "Create connected UAV");
			return result;
		}

		ComPtr<ID3D11Device> device_;
		ComPtr<ID3D11DeviceContext> context_;
		ComPtr<ID3D11ComputeShader> shader_;
		std::uint32_t cases_ = 0;
	};
}

int wmain(int argc, wchar_t** argv)
{
	try {
		Require(argc == 2, "Expected production shader directory");
		Harness gpu(argv[1]);
		gpu.ReadbackCases();
		gpu.EarlyCategoryBoundsCases(argv[1]);
		gpu.ConnectedPipelineCases(argv[1]);
		gpu.Case("minimum positive R8 pixel", 1, 1, { 1 });
		std::vector<std::uint8_t> tiny(33 * 17);
		tiny.back() = 1;
		gpu.Case("tiny pixel in clipped edge tile", 33, 17, tiny);
		std::vector<std::uint8_t> clusters(83 * 67);
		for (std::uint32_t y = 3; y < 8; ++y)
			for (std::uint32_t x = 2; x < 5; ++x)
				clusters[y * 83 + x] = 255;
		for (std::uint32_t y = 37; y < 66; ++y)
			for (std::uint32_t x = 61; x < 81; ++x)
				clusters[y * 83 + x] = 1;
		gpu.Case("two separated clusters", 83, 67, clusters);
		std::vector<std::uint8_t> hair(67 * 99);
		for (std::uint32_t y = 0; y < 99; ++y)
			hair[y * 67 + y % 67] = 1;
		gpu.Case("one-pixel faint hair crossing tiles", 67, 99, hair);
		gpu.Case("all zero", 83, 67, std::vector<std::uint8_t>(83 * 67));
		gpu.Case("full mask with clipped tiles", 83, 67, std::vector<std::uint8_t>(83 * 67, 255));
		gpu.Case("full exact tile grid", 64, 64, std::vector<std::uint8_t>(64 * 64, 1));
		std::vector<std::uint8_t> levels(256);
		for (std::uint32_t value = 0; value < levels.size(); ++value)
			levels[value] = static_cast<std::uint8_t>(value);
		gpu.Case("all R8 values", 256, 1, levels);
		std::mt19937 random(0xB01D512u);
		for (std::uint32_t test = 0; test < 128; ++test) {
			const std::uint32_t width = 1u + random() % 131u;
			const std::uint32_t height = 1u + random() % 101u;
			std::vector<std::uint8_t> pixels(width * height);
			const auto sparseDivisor = test % 2u ? 101u : 4u;
			for (auto& pixel : pixels)
				if (random() % sparseDivisor == 0u)
					pixel = static_cast<std::uint8_t>(1u + random() % 255u);
			gpu.Case("random " + std::to_string(test), width, height, pixels);
		}
		std::cout << "Production character mask-bounds HLSL passed " << gpu.Cases() << " WARP cases\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
