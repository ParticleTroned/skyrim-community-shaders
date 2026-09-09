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

		// Real queued GPU work, not a fake HRESULT or a CPU-only sleep in the
		// readback loop. The CPU can independently release this shared fence.
		struct QueueGate
		{
			ComPtr<ID3D12Fence> cpuFence;
			ComPtr<ID3D11Fence> gpuFence;
			~QueueGate() { if (cpuFence) cpuFence->Signal(1); }
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
				__uuidof(ID3D11ShaderReflection), &reflection), "Reflect mask-bounds shader");
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
				nullptr, &shader_), "Create mask-bounds shader");
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

		auto Read(const Sample& sample, std::vector<Bounds>& destination, Clock::time_point deadline)
		{
			return NeuralRendering::ReadCharacterMaskBounds(context_.Get(), sample.query.Get(),
				sample.staging.Get(), std::as_writable_bytes(std::span(destination)), deadline);
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
				IID_PPV_ARGS(&fenceDevice)), "Create shared-fence WARP device");
			ComPtr<ID3D11Device5> device5;
			ComPtr<ID3D11DeviceContext4> context4;
			Check(device_.As(&device5), "Query fence-capable D3D11 device");
			Check(context_.As(&context4), "Query fence-capable immediate context");
			const auto gateQueue = [&](QueueGate& gate) {
				Check(fenceDevice->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
					IID_PPV_ARGS(&gate.cpuFence)), "Create queued-work fence");
				HANDLE shared = nullptr;
				Check(fenceDevice->CreateSharedHandle(gate.cpuFence.Get(), nullptr,
					GENERIC_ALL, nullptr, &shared), "Share queued-work fence");
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
				QueueGate gate;
				gateQueue(gate);
				Queue(left);
				Queue(right);
				context_->Flush();  // Both eyes are submitted before the only flush.
				std::vector<Bounds> actualLeft(left.expected.size()), actualRight(right.expected.size());
				// The gate is still closed, so this deterministically reproduces
				// the old timeout without relying on worker-thread scheduling.
				Require(Read(left, actualLeft, Clock::now() + std::chrono::milliseconds(2)).status ==
					CharacterMaskReadbackStatus::Timeout, "Old 2ms deadline must reject queued work");
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
					actualRight == right.expected, "Already-ready second eye survives an expired shared deadline");
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
						D3D11_ASYNC_GETDATA_DONOTFLUSH), "Retire old copy marker");
					if (!complete) std::this_thread::yield();
				}
				Require(complete && actual == sentinel, "Retirement must not publish stale bounds");
				const std::vector<std::uint8_t> empty(leftPixels.size());
				context_->UpdateSubresource(left.mask.Get(), 0, nullptr, empty.data(), 65, 0);
				Queue(left);
				context_->Flush();
				Require(Read(left, actual, Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget).Ready() &&
					actual == std::vector<Bounds>(left.expected.size()), "Only the new copy supplies current empty bounds");
				++cases_;

				std::vector<Bounds> tooSmall(left.expected.size() - 1, Bounds{ 91, 92, 93, 94 });
				const auto before = tooSmall;
				const auto invalid = Read(left, tooSmall, Clock::now() + NeuralRendering::kCharacterMaskReadbackBudget);
				Require(invalid.status == CharacterMaskReadbackStatus::MapUnavailable &&
					invalid.result == E_INVALIDARG && tooSmall == before, "Reject incorrect destination size unchanged");
				++cases_;
			}
		}

		std::uint32_t Cases() const { return cases_; }

	private:
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
