#pragma once

#include <d3d12.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace NrReplay
{
	/** Bounded per-sample command evidence; observer failures must suppress proxy forwarding. */
	class KernelCommandCapture
	{
		using Json = nlohmann::json;

	public:
		static constexpr std::size_t kMaximumCommands = 65536;
		static constexpr std::size_t kMaximumDetails = 8192;
		static constexpr std::size_t kMaximumBarriers = 32768;
		static constexpr std::size_t kMaximumHeaps = 4096;
		static constexpr std::size_t kMaximumEntries = 32768;
		static constexpr UINT kMaximumBarriersPerCommand = 4096;
		static constexpr UINT kMaximumHeapsPerCommand = 2;

		explicit KernelCommandCapture(Json& sample, bool captureResourceExtents = false) : sample_(sample), captureResourceExtents_(captureResourceExtents)
		{
			sample_["kernelCommandDetails"] = Json::array();
			sample_["kernelCommandCapture"] = { { "requested", true }, { "status", "recording" }, { "failed", false },
				{ "resourceExtentsRequested", captureResourceExtents_ },
				{ "timingQualified", false }, { "commands", 0 }, { "details", 0 }, { "barriers", 0 }, { "descriptorHeaps", 0 },
				{ "limits", { { "commands", kMaximumCommands }, { "details", kMaximumDetails }, { "barriers", kMaximumBarriers },
								{ "descriptorHeaps", kMaximumHeaps }, { "combinedEntries", kMaximumEntries },
								{ "barriersPerCommand", kMaximumBarriersPerCommand }, { "heapsPerCommand", kMaximumHeapsPerCommand } } } };
		}
		KernelCommandCapture(const KernelCommandCapture&) = delete;
		KernelCommandCapture& operator=(const KernelCommandCapture&) = delete;
		~KernelCommandCapture() noexcept
		{
			if (!finished_ && !failed_)
				RecordFailure("command capture ended before explicit completion");
		}

		void SetEvaluation(unsigned eye, unsigned region) noexcept
		{
			eye_ = eye;
			region_ = region;
		}
		void BeforeCommand()
		{
			Observe([&] {
				Require(commandOrdinal_ < kMaximumCommands, "kernel command capture exceeds command budget");
				++commandOrdinal_;
				sample_["kernelCommandCapture"]["commands"] = commandOrdinal_;
			});
		}
		void Barriers(UINT count, const D3D12_RESOURCE_BARRIER* values)
		{
			Observe([&] {
				Admit(count, values, kMaximumBarriersPerCommand, barriers_, kMaximumBarriers);
				Json records = Json::array();
				for (UINT i = 0; i < count; ++i) {
					const auto& barrier = values[i];
					Json record = { { "type", barrier.Type }, { "flags", barrier.Flags } };
					switch (barrier.Type) {
					case D3D12_RESOURCE_BARRIER_TYPE_UAV:
						record["resource"] = Pointer(barrier.UAV.pResource);
						CaptureResource(record, "resourceInfo", barrier.UAV.pResource);
						break;
					case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
						record.update({ { "resource", Pointer(barrier.Transition.pResource) }, { "subresource", barrier.Transition.Subresource },
							{ "before", barrier.Transition.StateBefore }, { "after", barrier.Transition.StateAfter } });
						CaptureResource(record, "resourceInfo", barrier.Transition.pResource);
						break;
					case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
						record.update({ { "beforeResource", Pointer(barrier.Aliasing.pResourceBefore) }, { "afterResource", Pointer(barrier.Aliasing.pResourceAfter) } });
						CaptureResource(record, "beforeResourceInfo", barrier.Aliasing.pResourceBefore);
						CaptureResource(record, "afterResourceInfo", barrier.Aliasing.pResourceAfter);
						break;
					default:
						throw std::runtime_error("unrecognized captured barrier type");
					}
					records.push_back(std::move(record));
				}
				Append("ResourceBarrier", "barriers", std::move(records));
				barriers_ += count;
				PublishCounts();
			});
		}
		void Heaps(UINT count, ID3D12DescriptorHeap* const* values)
		{
			Observe([&] {
				Admit(count, values, kMaximumHeapsPerCommand, heaps_, kMaximumHeaps);
				Json records = Json::array();
				for (UINT i = 0; i < count; ++i) {
					Require(values[i] != nullptr, "null descriptor heap in capture");
					const auto desc = values[i]->GetDesc();
					records.push_back({ { "heap", Pointer(values[i]) }, { "type", desc.Type }, { "count", desc.NumDescriptors },
						{ "flags", desc.Flags }, { "nodeMask", desc.NodeMask } });
				}
				Append("SetDescriptorHeaps", "heaps", std::move(records));
				heaps_ += count;
				PublishCounts();
			});
		}
		/** Marks command observation complete, independently of subsequent GPU/output validation. */
		void Complete()
		{
			Observe([&] { sample_["kernelCommandCapture"]["status"] = "command_recording_complete"; });
			finished_ = true;
		}
		/** Retains the first observer or proxy rejection without throwing through the COM boundary. */
		void RecordFailure(std::string_view reason) noexcept
		{
			if (failed_)
				return;
			failed_ = true;
			try {
				auto& receipt = sample_["kernelCommandCapture"];
				receipt["failed"] = true;
				receipt["status"] = "failed";
				receipt["failure"] = { { "reason", std::string(reason.substr(0, 512)) }, { "commandOrdinal", commandOrdinal_ },
					{ "eye", eye_ }, { "region", region_ } };
			} catch (...) {
				// The proxy separately latches failure if evidence allocation is exhausted.
			}
		}

	private:
		Json& sample_;
		bool captureResourceExtents_ = false;
		unsigned eye_ = 0, region_ = 0;
		std::size_t commandOrdinal_ = 0, details_ = 0, barriers_ = 0, heaps_ = 0;
		bool finished_ = false, failed_ = false;
		static std::uintptr_t Pointer(const void* value) noexcept { return reinterpret_cast<std::uintptr_t>(value); }
		static void Require(bool condition, const char* reason)
		{
			if (!condition)
				throw std::runtime_error(reason);
		}
		void CaptureResource(Json& record, const char* field, ID3D12Resource* resource) const
		{
			if (!captureResourceExtents_)
				return;
			record[field] = nullptr;
			if (!resource)
				return;
			const auto desc = resource->GetDesc();
			Json info = { { "resource", Pointer(resource) }, { "allocationAliasingProven", false },
				{ "scope", "resource_identity_and_buffer_virtual_range_only" },
				{ "description", { { "dimension", desc.Dimension }, { "alignment", desc.Alignment }, { "width", desc.Width },
									 { "height", desc.Height }, { "depthOrArraySize", desc.DepthOrArraySize }, { "mipLevels", desc.MipLevels },
									 { "format", desc.Format }, { "sampleCount", desc.SampleDesc.Count }, { "sampleQuality", desc.SampleDesc.Quality },
									 { "layout", desc.Layout }, { "flags", desc.Flags } } },
				{ "gpuVirtualAddress", nullptr }, { "bufferRangeEndExclusive", nullptr }, { "bufferRangeAvailable", false } };
			if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
				const auto address = resource->GetGPUVirtualAddress();
				info["gpuVirtualAddress"] = address;
				if (address && desc.Width && desc.Width <= std::numeric_limits<UINT64>::max() - address) {
					info["bufferRangeEndExclusive"] = address + desc.Width;
					info["bufferRangeAvailable"] = true;
				} else {
					info["bufferRangeUnavailableReason"] = "zero_address_or_width_or_range_overflow";
				}
			} else {
				info["bufferRangeUnavailableReason"] = "resource_is_not_a_buffer";
			}
			D3D12_HEAP_PROPERTIES properties{};
			D3D12_HEAP_FLAGS flags{};
			const auto result = resource->GetHeapProperties(&properties, &flags);
			info["heapProperties"] = { { "hresult", static_cast<std::int32_t>(result) }, { "available", SUCCEEDED(result) },
				{ "properties", nullptr }, { "flags", nullptr } };
			if (SUCCEEDED(result)) {
				info["heapProperties"]["properties"] = { { "type", properties.Type }, { "cpuPageProperty", properties.CPUPageProperty },
					{ "memoryPoolPreference", properties.MemoryPoolPreference }, { "creationNodeMask", properties.CreationNodeMask },
					{ "visibleNodeMask", properties.VisibleNodeMask } };
				info["heapProperties"]["flags"] = flags;
			} else {
				info["heapProperties"]["unavailableReason"] = "GetHeapProperties_failed";
			}
			record[field] = std::move(info);
		}
		template <class Function>
		void Observe(Function&& function)
		{
			try {
				Require(!failed_ && !finished_, "kernel command capture is no longer recording");
				function();
			} catch (const std::exception& error) {
				RecordFailure(error.what());
				throw;
			} catch (...) {
				RecordFailure("unknown kernel command capture failure");
				throw;
			}
		}
		void Admit(UINT count, const void* values, UINT perCommand, std::size_t captured, std::size_t maximum) const
		{
			Require(commandOrdinal_ != 0, "kernel command detail lacks command boundary");
			Require(count <= perCommand && (!count || values), "kernel command capture argument exceeds bounds");
			Require(details_ < kMaximumDetails && count <= maximum - captured &&
						count <= kMaximumEntries - barriers_ - heaps_,
				"kernel command capture exceeds aggregate budget");
		}
		void Append(const char* command, const char* field, Json records)
		{
			sample_["kernelCommandDetails"].push_back({ { "commandOrdinal", commandOrdinal_ }, { "eye", eye_ },
				{ "region", region_ }, { "command", command }, { field, std::move(records) } });
			++details_;
		}
		void PublishCounts()
		{
			auto& receipt = sample_["kernelCommandCapture"];
			receipt["details"] = details_;
			receipt["barriers"] = barriers_;
			receipt["descriptorHeaps"] = heaps_;
		}
	};
}
