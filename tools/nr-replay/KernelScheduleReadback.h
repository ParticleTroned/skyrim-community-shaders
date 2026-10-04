#pragma once

#include <d3d12.h>
#include <nlohmann/json.hpp>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace NrReplay
{
	/** Retains independent full-output snapshots from bounded schedules in one command list. */
	class KernelScheduleReadback
	{
		using Json = nlohmann::json;
		template <class T>
		using ComPtr = Microsoft::WRL::ComPtr<T>;

	public:
		static constexpr unsigned kOutputs = 4, kMaximumRepeats = 4;
		static constexpr UINT kWidth = 1008, kHeight = 1120, kRowBytes = kWidth * 4;
		static constexpr std::uint64_t kMaximumAllocationBytes = 256ull * 1024 * 1024;

		struct Input
		{
			ID3D12Resource* output = nullptr;
			UINT width = 0, height = 0, rowBytes = 0;
			std::array<std::span<const std::uint8_t>, 2> sentinel;
		};

		/** Copies sentinel bytes during construction; no GPU commands are submitted. */
		KernelScheduleReadback(ID3D12Device* device, std::span<const Input> inputs, unsigned repeats) : state_(std::make_unique<State>())
		{
			Require(device && inputs.size() == kOutputs && repeats >= 2 && repeats <= kMaximumRepeats,
				"schedule readback requires a device, four outputs and two to four repeats");
			state_->device = device;
			state_->repeats = repeats;
			AdmitInputs(inputs);
			const auto buffer = BufferDescription(state_->packedBytes);
			const auto allocation = device->GetResourceAllocationInfo(0, 1, &buffer);
			Require(allocation.SizeInBytes && allocation.SizeInBytes <= kMaximumAllocationBytes / (repeats + 2),
				"schedule readback exceeds allocation budget");
			state_->allocationBytes = allocation.SizeInBytes * (repeats + 2);
			for (unsigned pattern = 0; pattern < 2; ++pattern) {
				CreateBuffer(state_->uploads[pattern], buffer, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
					L"NRReplay::ScheduleSentinel" + std::to_wstring(pattern));
				FillUpload(pattern, inputs);
			}
			for (unsigned repeat = 0; repeat < repeats; ++repeat)
				CreateBuffer(state_->readbacks[repeat], buffer, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
					L"NRReplay::ScheduleReadback" + std::to_wstring(repeat));
		}

		KernelScheduleReadback(const KernelScheduleReadback&) = delete;
		KernelScheduleReadback& operator=(const KernelScheduleReadback&) = delete;
		~KernelScheduleReadback() noexcept
		{
			if (state_ && state_->recorded && !state_->idle) {
				std::fputs("schedule readback owners retained until process exit: GPU idle not proven\n", stderr);
				state_.release();
			}
		}

		/** Outputs must enter and leave this callback in UNORDERED_ACCESS state. */
		void RecordBefore(ID3D12GraphicsCommandList* list, unsigned repeat)
		{
			Require(!state_->idle && repeat < state_->repeats && repeat == state_->beforeCount && repeat == state_->afterCount,
				"schedule sentinel callback is out of order");
			AdmitList(list);
			state_->recorded = true;
			Transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
			for (unsigned slot = 0; slot < kOutputs; ++slot) {
				const auto source = BufferLocation(state_->uploads[repeat & 1].Get(), slot);
				const auto target = OutputLocation(slot);
				list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
			}
			Transition(list, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			++state_->beforeCount;
		}

		/** Records the full output copies after the caller has ended its GPU timestamp. */
		void RecordAfter(ID3D12GraphicsCommandList* list, unsigned repeat)
		{
			Require(!state_->idle && repeat < state_->repeats && state_->beforeCount == repeat + 1 && state_->afterCount == repeat,
				"schedule snapshot callback is out of order");
			AdmitList(list);
			Transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
			for (unsigned slot = 0; slot < kOutputs; ++slot) {
				const auto source = OutputLocation(slot);
				const auto target = BufferLocation(state_->readbacks[repeat].Get(), slot);
				list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
			}
			Transition(list, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			++state_->afterCount;
		}

		/** Caller must first prove completion of this exact list through its fence. */
		void ConfirmIdle()
		{
			Require(state_->recorded && state_->beforeCount == state_->repeats && state_->afterCount == state_->repeats,
				"schedule readback cannot confirm an incomplete recording");
			Check(state_->device->GetDeviceRemovedReason(), "schedule readback device is unhealthy");
			state_->idle = true;
		}

		/** Returns row-tight RGBA8 bytes only after the caller's explicit idle proof. */
		std::vector<std::uint8_t> Read(unsigned repeat, unsigned slot)
		{
			Require(state_->idle && repeat < state_->repeats && slot < kOutputs,
				"schedule readback requires proven idle and valid indices");
			std::vector<std::uint8_t> result(static_cast<std::size_t>(kRowBytes) * kHeight);
			const auto& layout = state_->layouts[slot];
			const D3D12_RANGE range{ static_cast<SIZE_T>(layout.Offset),
				static_cast<SIZE_T>(layout.Offset + static_cast<std::uint64_t>(layout.Footprint.RowPitch) * kHeight) };
			void* mapped = nullptr;
			Check(state_->readbacks[repeat]->Map(0, &range, &mapped), "schedule readback map failed");
			if (!mapped) {
				const D3D12_RANGE noWrite{};
				state_->readbacks[repeat]->Unmap(0, &noWrite);
				Require(false, "schedule readback map returned a null address");
			}
			for (UINT row = 0; row < kHeight; ++row)
				std::memcpy(result.data() + static_cast<std::size_t>(row) * kRowBytes,
					static_cast<const std::uint8_t*>(mapped) + layout.Offset + static_cast<std::size_t>(row) * layout.Footprint.RowPitch, kRowBytes);
			const D3D12_RANGE noWrite{};
			state_->readbacks[repeat]->Unmap(0, &noWrite);
			state_->read[repeat][slot] = true;
			return result;
		}

		[[nodiscard]] Json Receipt() const
		{
			return { { "requested", true }, { "scope", "full_rgba8_output_per_same_list_schedule_repeat" },
				{ "repeatCount", state_->repeats }, { "outputCount", kOutputs }, { "width", kWidth }, { "height", kHeight },
				{ "rowBytes", kRowBytes }, { "packedBufferBytes", state_->packedBytes }, { "allocationBytes", state_->allocationBytes },
				{ "allocationLimitBytes", kMaximumAllocationBytes }, { "sentinelPattern", "repeat_index_modulo_2" },
				{ "recordedBefore", state_->beforeCount }, { "recordedAfter", state_->afterCount }, { "gpuIdleProven", state_->idle },
				{ "retainedUntilProcessExit", state_->recorded && !state_->idle }, { "readOutputs", state_->read },
				{ "failed", state_->failure != nullptr }, { "reason", state_->failure ? state_->failure : "" }, { "lastHresult", state_->lastResult } };
		}

	private:
		struct State
		{
			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12GraphicsCommandList> list;
			std::array<ComPtr<ID3D12Resource>, kOutputs> outputs;
			std::array<ComPtr<ID3D12Resource>, 2> uploads;
			std::array<ComPtr<ID3D12Resource>, kMaximumRepeats> readbacks;
			std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kOutputs> layouts{};
			std::array<std::array<bool, kOutputs>, kMaximumRepeats> read{};
			std::uint64_t packedBytes = 0, allocationBytes = 0;
			unsigned repeats = 0, beforeCount = 0, afterCount = 0;
			bool recorded = false, idle = false;
			const char* failure = nullptr;
			HRESULT lastResult = S_OK;
		};
		std::unique_ptr<State> state_;

		void Require(bool value, const char* reason)
		{
			if (!value) {
				state_->failure = reason;
				throw std::runtime_error(reason);
			}
		}
		void Check(HRESULT result, const char* reason)
		{
			if (FAILED(result)) {
				state_->lastResult = result;
				Require(false, reason);
			}
		}
		void AdmitInputs(std::span<const Input> inputs)
		{
			for (unsigned slot = 0; slot < kOutputs; ++slot) {
				const auto& input = inputs[slot];
				Require(input.output && input.width == kWidth && input.height == kHeight && input.rowBytes == kRowBytes,
					"schedule readback output dimensions differ from qualified geometry");
				for (const auto pattern : input.sentinel)
					Require(pattern.size() == static_cast<std::size_t>(kRowBytes) * kHeight && pattern.data(), "schedule sentinel size differs from full RGBA8 output");
				for (unsigned prior = 0; prior < slot; ++prior)
					Require(state_->outputs[prior].Get() != input.output, "schedule outputs must be distinct resources");
				ComPtr<ID3D12Device> owner;
				Check(input.output->GetDevice(IID_PPV_ARGS(&owner)), "schedule output device query failed");
				Require(owner.Get() == state_->device.Get(), "schedule output belongs to another device");
				const auto desc = input.output->GetDesc();
				Require(desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.Width == kWidth && desc.Height == kHeight &&
							desc.DepthOrArraySize == 1 && desc.MipLevels == 1 && desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && desc.SampleDesc.Count == 1 &&
							desc.SampleDesc.Quality == 0 && (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0,
					"schedule output resource is not the qualified single-subresource RGBA8 UAV");
				UINT rows = 0;
				UINT64 rowBytes = 0, totalBytes = 0;
				auto& layout = state_->layouts[slot];
				state_->device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rows, &rowBytes, &totalBytes);
				Require(rows == kHeight && rowBytes == kRowBytes && layout.Offset == 0 && layout.Footprint.Format == desc.Format &&
							layout.Footprint.Width == kWidth && layout.Footprint.Height == kHeight && layout.Footprint.Depth == 1 &&
							layout.Footprint.RowPitch >= kRowBytes && layout.Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0,
					"schedule output copy footprint is invalid");
				const auto bytes = static_cast<std::uint64_t>(layout.Footprint.RowPitch) * kHeight;
				Require(totalBytes >= static_cast<std::uint64_t>(kRowBytes) * kHeight && totalBytes <= bytes && bytes <= kMaximumAllocationBytes,
					"schedule output copy footprint exceeds budget");
				layout.Offset = (state_->packedBytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~(std::uint64_t(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) - 1);
				Require(layout.Offset <= kMaximumAllocationBytes - bytes, "schedule packed output buffer exceeds budget");
				state_->packedBytes = layout.Offset + bytes;
				state_->outputs[slot] = input.output;
			}
		}
		static D3D12_RESOURCE_DESC BufferDescription(std::uint64_t bytes)
		{
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = bytes;
			desc.Height = desc.SampleDesc.Count = 1;
			desc.DepthOrArraySize = desc.MipLevels = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			return desc;
		}
		void CreateBuffer(ComPtr<ID3D12Resource>& output, const D3D12_RESOURCE_DESC& desc, D3D12_HEAP_TYPE heapType,
			D3D12_RESOURCE_STATES initialState, const std::wstring& name)
		{
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = heapType;
			heap.CreationNodeMask = heap.VisibleNodeMask = 1;
			Check(state_->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&output)),
				"schedule staging allocation failed");
			Require(output != nullptr, "schedule staging allocation returned null");
			Check(output->SetName(name.c_str()), "schedule staging resource naming failed");
		}
		void FillUpload(unsigned pattern, std::span<const Input> inputs)
		{
			const D3D12_RANGE noRead{};
			void* mapped = nullptr;
			Check(state_->uploads[pattern]->Map(0, &noRead, &mapped), "schedule upload map failed");
			if (!mapped) {
				state_->uploads[pattern]->Unmap(0, nullptr);
				Require(false, "schedule upload map returned a null address");
			}
			std::memset(mapped, 0, static_cast<std::size_t>(state_->packedBytes));
			for (unsigned slot = 0; slot < kOutputs; ++slot)
				for (UINT row = 0; row < kHeight; ++row)
					std::memcpy(static_cast<std::uint8_t*>(mapped) + state_->layouts[slot].Offset + static_cast<std::size_t>(row) * state_->layouts[slot].Footprint.RowPitch,
						inputs[slot].sentinel[pattern].data() + static_cast<std::size_t>(row) * kRowBytes, kRowBytes);
			const D3D12_RANGE written{ 0, static_cast<SIZE_T>(state_->packedBytes) };
			state_->uploads[pattern]->Unmap(0, &written);
		}
		void AdmitList(ID3D12GraphicsCommandList* list)
		{
			Require(list && (!state_->list || state_->list.Get() == list), "schedule callbacks require one command list");
			if (state_->list)
				return;
			ComPtr<ID3D12Device> owner;
			Check(list->GetDevice(IID_PPV_ARGS(&owner)), "schedule command list device query failed");
			Require(owner.Get() == state_->device.Get() && list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT,
				"schedule callbacks require the matching direct command list");
			state_->list = list;
		}
		void Transition(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const
		{
			std::array<D3D12_RESOURCE_BARRIER, kOutputs> barriers{};
			for (unsigned slot = 0; slot < kOutputs; ++slot) {
				barriers[slot].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barriers[slot].Transition = { state_->outputs[slot].Get(), 0, before, after };
			}
			list->ResourceBarrier(kOutputs, barriers.data());
		}
		D3D12_TEXTURE_COPY_LOCATION OutputLocation(unsigned slot) const
		{
			D3D12_TEXTURE_COPY_LOCATION location{};
			location.pResource = state_->outputs[slot].Get();
			location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			return location;
		}
		D3D12_TEXTURE_COPY_LOCATION BufferLocation(ID3D12Resource* buffer, unsigned slot) const
		{
			D3D12_TEXTURE_COPY_LOCATION location{};
			location.pResource = buffer;
			location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			location.PlacedFootprint = state_->layouts[slot];
			return location;
		}
	};
}
