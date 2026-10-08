#pragma once

#include <array>
#include <cstdint>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <mutex>
#include <utility>
#include <wrl/client.h>

namespace Util
{
	/** Shared device-local observations; clients retain their own recovery policies. */
	class GpuMemoryBudget
	{
	public:
		enum class Owner : std::uint8_t
		{
			NeuralRendering,
			RenderScale,
			Streaming,
			Count
		};
		static constexpr std::uint64_t MiB = 1024ull * 1024;
		struct Snapshot
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO local{};
			LUID adapter{};
			std::uint64_t generation = 0;
			std::uint64_t sequence = 0;
			std::uint64_t sampledAtMs = 0;
			std::uint64_t pendingBytes = 0;
			bool priorityWork = false;
			HRESULT result = E_PENDING;
			[[nodiscard]] bool Fresh(std::uint64_t now, std::uint64_t age = 500) const noexcept
			{
				return SUCCEEDED(result) && local.Budget && now >= sampledAtMs && now - sampledAtMs <= age;
			}
		};
		/** Holds the entire new allocation, including overlap with its old resource. */
		class Reservation
		{
		public:
			Reservation() = default;
			~Reservation() { Reset(); }
			Reservation(const Reservation&) = delete;
			Reservation& operator=(const Reservation&) = delete;
			Reservation(Reservation&& other) noexcept { *this = std::move(other); }
			Reservation& operator=(Reservation&& other) noexcept;
			void Reset() noexcept;
			explicit operator bool() const noexcept { return service != nullptr; }

		private:
			friend class GpuMemoryBudget;
			GpuMemoryBudget* service = nullptr;
			Owner owner = Owner::Streaming;
			std::uint64_t generation = 0;
			std::uint64_t bytes = 0;
		};

		static GpuMemoryBudget& Get();
		/** Query the adapter belonging to this device, never an independently selected GPU. */
		Snapshot Sample(ID3D11Device* device, std::uint64_t maxAgeMs = 250, bool force = false);
		/** Register replacement overlap after a client's own admission policy succeeds. */
		Reservation Reserve(Owner owner, ID3D11Device* device, std::uint64_t bytes);
		/** Atomically admit a bounded streaming replacement with a fresh budget observation. */
		Reservation TryReserveStreaming(ID3D11Device* device, std::uint64_t bytes, bool refill, bool required = false);
		void SetPriorityWork(Owner owner, bool pending);
		[[nodiscard]] std::uint64_t PendingBytes(Owner excluding) const;
		static constexpr std::uint64_t Add(std::uint64_t left, std::uint64_t right) noexcept
		{
			return right > UINT64_MAX - left ? UINT64_MAX : left + right;
		}
		/** Test peak usage without credit for a future retirement. */
		static constexpr bool StreamingFits(std::uint64_t budget, std::uint64_t usage, std::uint64_t outstanding,
			std::uint64_t bytes, bool refill, bool required, bool priorityWork) noexcept
		{
			if (!budget || !bytes || (refill && priorityWork))
				return false;
			const auto projected = Add(Add(usage, outstanding), bytes);
			const auto reserve = (refill ? (required ? 1024 : 1536) : 128) * MiB;
			const auto limit = budget / 100 * (refill ? (required ? 82 : 70) : 100);
			return projected < limit && projected < budget && budget - projected > reserve;
		}

	private:
		Snapshot SampleLocked(ID3D11Device* device, std::uint64_t maxAgeMs, bool force);
		Reservation ReserveLocked(Owner owner, std::uint64_t bytes);
		void Release(Owner owner, std::uint64_t generation, std::uint64_t bytes) noexcept;
		mutable std::mutex mutex;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
		Snapshot snapshot;
		std::array<std::uint64_t, static_cast<std::size_t>(Owner::Count)> pending{};
		std::array<bool, static_cast<std::size_t>(Owner::Count)> priority{};
	};
}
