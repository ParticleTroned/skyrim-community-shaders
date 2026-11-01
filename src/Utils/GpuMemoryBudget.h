#pragma once

#include "TextureMemoryPolicy.h"
#include <algorithm>
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
			std::uint64_t recoveryDemandBytes = 0;
			bool priorityWork = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
			std::uint64_t physicalBudgetBytes = 0;
			bool pressureSimulated = false;
#endif
			HRESULT result = E_PENDING;
			[[nodiscard]] bool Fresh(std::uint64_t now, std::uint64_t age = 500) const noexcept
			{
				return SUCCEEDED(result) && local.Budget && now >= sampledAtMs && now - sampledAtMs <= age;
			}
		};
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Fixed lower budget for a bounded streaming test; real allocations remain authoritative. */
		struct StreamingPressureSimulation
		{
			static constexpr std::uint32_t MaximumDurationMilliseconds = TextureMemoryPolicy::MaximumPressureSimulationMilliseconds;
			static constexpr std::uint32_t MaximumHeadroomMiB = TextureMemoryPolicy::MaximumPressureSimulationHeadroomMiB;
			std::uint32_t headroomMiB = 0;
			std::uint64_t budgetBytes = 0, startedAtMs = 0, expiresAtMs = 0, generation = 0;
			[[nodiscard]] bool Active(std::uint64_t now, std::uint64_t deviceGeneration) const noexcept
			{
				return expiresAtMs && now >= startedAtMs && now < expiresAtMs && generation == deviceGeneration;
			}
			bool Configure(const Snapshot& physical, std::uint64_t now, std::uint32_t durationMilliseconds, std::uint32_t freeMiB) noexcept
			{
				if (durationMilliseconds > MaximumDurationMilliseconds || freeMiB > MaximumHeadroomMiB ||
					(!durationMilliseconds && freeMiB))
					return false;
				if (!durationMilliseconds) {
					*this = {};
					return true;
				}
				if (!physical.Fresh(now) || !physical.generation || now > UINT64_MAX - durationMilliseconds)
					return false;
				*this = { freeMiB, std::min(physical.local.Budget, Add(Add(physical.local.CurrentUsage, physical.pendingBytes), freeMiB * MiB)),
					now, now + durationMilliseconds, physical.generation };
				return true;
			}
			[[nodiscard]] Snapshot Apply(Snapshot physical, std::uint64_t now) const noexcept
			{
				physical.physicalBudgetBytes = physical.local.Budget;
				physical.pressureSimulated = Active(now, physical.generation) && physical.Fresh(now);
				if (physical.pressureSimulated)
					physical.local.Budget = std::min(physical.local.Budget, budgetBytes);
				return physical;
			}
		};
		/** Configure or explicitly clear a session-only test; a start requires fresh physical telemetry. */
		bool ConfigureStreamingPressure(ID3D11Device* device, std::uint32_t durationMilliseconds, std::uint32_t freeMiB = 0);
		/** Copy the bounded simulation state without querying or changing the renderer. */
		StreamingPressureSimulation GetStreamingPressure() const;
#endif
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
			/** Logical pending allocation allowance owned by this ticket. */
			[[nodiscard]] std::uint64_t Bytes() const noexcept { return service ? bytes : 0; }

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
		/** Streaming-only budget observation; developer tests cannot increase physical headroom. */
		Snapshot SampleStreaming(ID3D11Device* device, std::uint64_t maxAgeMs = 250, bool force = false);
		/** Register replacement overlap after a client's own admission policy succeeds. */
		Reservation Reserve(Owner owner, ID3D11Device* device, std::uint64_t bytes);
		/** Atomically admit a bounded streaming replacement with a fresh budget observation. */
		Reservation TryReserveStreaming(ID3D11Device* device, std::uint64_t bytes, bool refill, bool required = false,
			std::uint64_t reserveBytes = TextureMemoryPolicy::DefaultReserveMiB * MiB);
		/** Publish active priority and soft future demand together without touching allocation tickets. */
		void SetOwnerWork(Owner owner, bool priorityWork, std::uint64_t recoveryDemandBytes);
		void SetPriorityWork(Owner owner, bool pending);
		/** Soft future allocations constrain optional refill, never required texture detail. */
		void SetRecoveryDemand(Owner owner, std::uint64_t bytes);
		[[nodiscard]] std::uint64_t PendingBytes(Owner excluding) const;
		static constexpr std::uint64_t Add(std::uint64_t left, std::uint64_t right) noexcept
		{
			return right > UINT64_MAX - left ? UINT64_MAX : left + right;
		}
		/** Test peak usage without credit for a future retirement. */
		static constexpr bool StreamingFits(std::uint64_t budget, std::uint64_t usage, std::uint64_t outstanding,
			std::uint64_t bytes, bool refill, bool required, bool priorityWork, std::uint64_t recoveryDemandBytes = 0,
			std::uint64_t reserveBytes = TextureMemoryPolicy::DefaultReserveMiB * MiB) noexcept
		{
			if (!budget || !bytes || (refill && priorityWork))
				return false;
			const auto projected = Add(Add(Add(usage, outstanding), bytes), refill && !required ? recoveryDemandBytes : 0);
			const auto reserve = refill ? Add(reserveBytes, required ? 0 : TextureMemoryPolicy::RefillGapBytes) : TextureMemoryPolicy::ReductionOverlapBytes;
			return projected < budget && budget - projected > reserve;
		}

	private:
		Snapshot SampleLocked(ID3D11Device* device, std::uint64_t maxAgeMs, bool force);
		Reservation ReserveLocked(Owner owner, std::uint64_t bytes);
		void Release(Owner owner, std::uint64_t generation, std::uint64_t bytes) noexcept;
		void SetOwnerWorkLocked(Owner owner, bool priorityWork, std::uint64_t recoveryDemandBytes) noexcept;
		mutable std::mutex mutex;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
		Snapshot snapshot;
#ifdef DEVBENCH_BRIDGE_ENABLED
		StreamingPressureSimulation streamingPressure;
#endif
		std::array<std::uint64_t, static_cast<std::size_t>(Owner::Count)> pending{};
		std::array<bool, static_cast<std::size_t>(Owner::Count)> priority{};
		std::array<std::uint64_t, static_cast<std::size_t>(Owner::Count)> recoveryDemand{};
	};
}
