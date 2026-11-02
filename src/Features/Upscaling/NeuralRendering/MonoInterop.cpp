#include "MonoInterop.h"
#include "Utils/ComIdentity.h"
#include "Utils/ResourceName.h"
#include "Globals.h"
#include "Profiler.h"
#include <array>
#include <cmath>
#include <cstring>
#include <directx/d3dx12.h>

#include <dxgi1_2.h>
#include <string>
#include <utility>

namespace NeuralRendering
{
	using Microsoft::WRL::ComPtr;

	void MonoSharedTexture::Abandon() noexcept
	{
		(void)texture.Detach();
		(void)srv.Detach();
		(void)uav.Detach();
		(void)shared.Detach();
	}

	bool MonoInterop::Check(HRESULT result) noexcept
	{
		if (SUCCEEDED(result))
			return true;
		error_ = result;
		failed_ = true;
		return false;
	}

	bool MonoInterop::Matches(ID3D11Device* device) const noexcept
	{
		return device11_ && Util::SameIdentity(device11_.Get(), device);
	}

	bool MonoInterop::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		if (Initialized())
			return Matches(device) && !failed_;
		if (failed_ || !device || !context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
			return false;
		ComPtr<ID3D11Device> contextDevice;
		context->GetDevice(&contextDevice);
		if (!Util::SameIdentity(device, contextDevice.Get()))
			return false;
		ComPtr<IDXGIDevice> dxgiDevice;
		ComPtr<IDXGIAdapter> adapter;
		if (!Check(device->QueryInterface(IID_PPV_ARGS(&device11_))) ||
			!Check(context->QueryInterface(IID_PPV_ARGS(&context11_))) ||
			!Check(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
			!Check(dxgiDevice->GetAdapter(&adapter)))
			return false;
		DXGI_ADAPTER_DESC adapterDescription{};
		if (!Check(adapter->GetDesc(&adapterDescription)) || adapterDescription.VendorId != 0x10de)
			return false;
		if (!Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12_))))
			return false;
		D3D12_COMMAND_QUEUE_DESC queueDescription{};
		queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (!Check(device12_->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue_))) ||
			!Check(device12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_))) ||
			!Check(device12_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&commands_))) ||
			!Check(commands_->Close()) ||
			!Check(device12_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12_))))
			return false;
		queue_->SetName(L"NeuralRendering::MonoQueue");
		allocator_->SetName(L"NeuralRendering::MonoAllocator");
		commands_->SetName(L"NeuralRendering::MonoCommands");
		fence12_->SetName(L"NeuralRendering::MonoFence");
		HANDLE handle = nullptr;
		if (!Check(device12_->CreateSharedHandle(fence12_.Get(), nullptr, GENERIC_ALL, nullptr, &handle)))
			return false;
		const auto result = device11_->OpenSharedFence(handle, IID_PPV_ARGS(&fence11_));
		CloseHandle(handle);
		if (!Check(result))
			return false;
		Util::SetResourceName(fence11_.Get(), "NeuralRendering::MonoFence11");
		return true;
	}

	HRESULT MonoInterop::CreateTexture(std::uint32_t width, std::uint32_t height, DXGI_FORMAT format,
		MonoSharedTexture& texture, const char* name)
	{
		if (!device11_ || !device12_ || failed_ || !width || !height || width > 16384 || height > 16384)
			return E_INVALIDARG;
		D3D11_TEXTURE2D_DESC description{};
		description.Width = width;
		description.Height = height;
		description.MipLevels = description.ArraySize = 1;
		description.Format = format;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		description.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		MonoSharedTexture candidate;
		auto result = device11_->CreateTexture2D(&description, nullptr, &candidate.texture);
		if (FAILED(result))
			return result;
		Util::SetResourceName(candidate.texture.Get(), "%s", name);
		if (FAILED(result = device11_->CreateShaderResourceView(candidate.texture.Get(), nullptr, &candidate.srv)))
			return result;
		Util::SetResourceName(candidate.srv.Get(), "%s SRV", name);
		if (FAILED(result = device11_->CreateUnorderedAccessView(candidate.texture.Get(), nullptr, &candidate.uav)))
			return result;
		Util::SetResourceName(candidate.uav.Get(), "%s UAV", name);
		ComPtr<IDXGIResource1> resource;
		if (FAILED(result = candidate.texture.As(&resource)))
			return result;
		HANDLE handle = nullptr;
		if (FAILED(result = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle)))
			return result;
		result = device12_->OpenSharedHandle(handle, IID_PPV_ARGS(&candidate.shared));
		CloseHandle(handle);
		if (FAILED(result))
			return result;
		const auto opened = candidate.shared->GetDesc();
		if (opened.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || opened.Width != width || opened.Height != height ||
			opened.DepthOrArraySize != 1 || opened.MipLevels != 1 || opened.Format != format || opened.SampleDesc.Count != 1 ||
			(opened.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
			return E_INVALIDARG;
		const std::string label(name);
		candidate.shared->SetName(std::wstring(label.begin(), label.end()).c_str());
		texture = std::move(candidate);
		return S_OK;
	}

	ID3D12GraphicsCommandList* MonoInterop::Begin()
	{
		if (!Initialized() || failed_ || recording_ || retirement_)
			return nullptr;
		const auto completed = fence12_->GetCompletedValue();
		if (completed == UINT64_MAX) {
			(void)Check(DXGI_ERROR_DEVICE_REMOVED);
			return nullptr;
		}
		if (completed < lastSubmission_)
			return nullptr;
		if (value_ >= UINT64_MAX - 2 || !Check(allocator_->Reset()) || !Check(commands_->Reset(allocator_.Get(), nullptr)))
			return nullptr;
		recording_ = true;
		if (!Check(context11_->Signal(fence11_.Get(), ++value_)))
			return nullptr;
		context11_->Flush();
		if (!Check(queue_->Wait(fence12_.Get(), value_)))
			return nullptr;
		return commands_.Get();
	}

	bool MonoInterop::Submit()
	{
		if (!recording_ || failed_ || !Check(commands_->Close()))
			return false;
		recording_ = false;
		ID3D12CommandList* list = commands_.Get();
		queue_->ExecuteCommandLists(1, &list);
		if (!Check(queue_->Signal(fence12_.Get(), ++value_)))
			return false;
		lastSubmission_ = value_;
		if (timestampRecorded_) { timestampFence_ = value_; timestampRecorded_ = false; }
		pendingSinceMs_ = GetTickCount64();
		return Check(context11_->Wait(fence11_.Get(), value_));
	}

	bool MonoInterop::PollRetired()
	{
		if (!device12_)
			return !failed_;
		if (failed_ || recording_)
			return false;
		if (!retirement_) {
			if (value_ == UINT64_MAX || !Check(context11_->Signal(fence11_.Get(), ++value_)))
				return false;
			retirement_ = value_;
			pendingSinceMs_ = GetTickCount64();
			context11_->Flush();
		}
		const auto completed = fence12_->GetCompletedValue();
		if (completed == UINT64_MAX)
			return Check(DXGI_ERROR_DEVICE_REMOVED);
		return completed >= retirement_;
	}

	void MonoInterop::ReleaseRetired()
	{
		if (!PollRetired())
			return;
		retirement_ = 0;
	}

	LifetimeFenceSnapshot MonoInterop::Snapshot() const noexcept
	{
		LifetimeFenceSnapshot snapshot{};
		snapshot.device = reinterpret_cast<std::uintptr_t>(device12_.Get());
		snapshot.queue = reinterpret_cast<std::uintptr_t>(queue_.Get());
		snapshot.fence = reinterpret_cast<std::uintptr_t>(fence12_.Get());
		snapshot.device11 = reinterpret_cast<std::uintptr_t>(device11_.Get());
		snapshot.context11 = reinterpret_cast<std::uintptr_t>(context11_.Get());
		snapshot.initialized = device12_ && queue_ && fence12_ && fence11_;
		snapshot.recording = recording_;
		snapshot.issued = value_;
		snapshot.contexts[0] = lastSubmission_;
		if (fence12_) {
			snapshot.completed = fence12_->GetCompletedValue();
			snapshot.completedKnown = snapshot.completed != UINT64_MAX;
		}
		snapshot.deviceRemoved = snapshot.initialized && (!snapshot.completedKnown ||
			(device12_ && FAILED(device12_->GetDeviceRemovedReason())) || (device11_ && FAILED(device11_->GetDeviceRemovedReason())));
		return snapshot;
	}

	bool MonoInterop::Stalled(std::uint64_t now) const noexcept
	{
		if (!pendingSinceMs_ || now < pendingSinceMs_ || now - pendingSinceMs_ < 1000 || failed_ || recording_)
			return false;
		const auto observation = Snapshot();
		return observation.initialized && observation.completedKnown && observation.completed < observation.issued;
	}

	void MonoInterop::PollGpuTiming()
	{
		if (!timestampFence_ || !fence12_ || !timestampReadback_) return;
		const auto complete = fence12_->GetCompletedValue();
		if (complete == UINT64_MAX || complete < timestampFence_) return;
		const D3D12_RANGE read{ 0, 2 * sizeof(std::uint64_t) }, written{ 0, 0 };
		void* mapped = nullptr;
		if (SUCCEEDED(timestampReadback_->Map(0, &read, &mapped)) && mapped) {
			std::array<std::uint64_t, 2> values{};
			std::memcpy(values.data(), mapped, sizeof(values));
			timestampReadback_->Unmap(0, &written);
			if (timestampFrequency_ && values[1] >= values[0] && globals::profiler) {
				const auto milliseconds = double(values[1] - values[0]) * 1000.0 / double(timestampFrequency_);
				if (std::isfinite(milliseconds) && milliseconds >= 0 && milliseconds < 1000000.0)
					(void)globals::profiler->PublishExternalGpuTiming(timestampEpoch_, "NeuralRendering::Inference", timestampFrame_, float(milliseconds));
			}
		} else timestampUnavailable_ = true;
		timestampFence_ = 0;
	}

	void MonoInterop::BeginGpuTiming(std::uint64_t captureEpoch, std::uint32_t frame)
	{
		PollGpuTiming();
		if (!recording_ || !captureEpoch || timestampUnavailable_ || timestampFence_ || timestampActive_) return;
		if (!timestamps_ || !timestampReadback_) {
			D3D12_QUERY_HEAP_DESC description{ D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0 };
			const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
			const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(2 * sizeof(std::uint64_t));
			ComPtr<ID3D12QueryHeap> queries;
			ComPtr<ID3D12Resource> readback;
			if (FAILED(queue_->GetTimestampFrequency(&timestampFrequency_)) || !timestampFrequency_ ||
				FAILED(device12_->CreateQueryHeap(&description, IID_PPV_ARGS(&queries))) ||
				FAILED(device12_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
					D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
				timestampUnavailable_ = true; return;
			}
			queries->SetName(L"NeuralRendering::InferenceTimestamps");
			readback->SetName(L"NeuralRendering::InferenceTimestampReadback");
			timestamps_ = std::move(queries); timestampReadback_ = std::move(readback);
		}
		timestampEpoch_ = captureEpoch; timestampFrame_ = frame; timestampActive_ = true;
		commands_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
	}

	void MonoInterop::EndGpuTiming()
	{
		if (!timestampActive_) return;
		commands_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
		commands_->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timestampReadback_.Get(), 0);
		timestampActive_ = false; timestampRecorded_ = true;
	}

	void MonoInterop::Abandon() noexcept
	{
		failed_ = true;
		(void)timestamps_.Detach(); (void)timestampReadback_.Detach();
		(void)commands_.Detach();
		(void)allocator_.Detach();
		(void)queue_.Detach();
		(void)fence12_.Detach();
		(void)fence11_.Detach();
		(void)device12_.Detach();
		(void)context11_.Detach();
		(void)device11_.Detach();
	}
}
