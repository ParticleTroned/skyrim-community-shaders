#include "GpuMemoryBudget.h"
#include "ComIdentity.h"
#include <algorithm>

namespace Util
{
	GpuMemoryBudget& GpuMemoryBudget::Get()
	{
		// Reservations can outlive feature singletons during DLL shutdown.
		static auto* value = new GpuMemoryBudget;
		return *value;
	}

	GpuMemoryBudget::Reservation& GpuMemoryBudget::Reservation::operator=(Reservation&& other) noexcept
	{
		if (this != &other) {
			Reset();
			service = std::exchange(other.service, nullptr);
			owner = other.owner;
			generation = other.generation;
			bytes = other.bytes;
		}
		return *this;
	}

	void GpuMemoryBudget::Reservation::Reset() noexcept
	{
		if (auto* previous = std::exchange(service, nullptr))
			previous->Release(owner, generation, bytes);
	}

	GpuMemoryBudget::Snapshot GpuMemoryBudget::Sample(ID3D11Device* source, std::uint64_t maxAgeMs, bool force)
	{
		std::scoped_lock lock(mutex);
		return SampleLocked(source, maxAgeMs, force);
	}

	GpuMemoryBudget::Snapshot GpuMemoryBudget::SampleLocked(ID3D11Device* source, std::uint64_t maxAgeMs, bool force)
	{
		const auto now = GetTickCount64();
		if (source != device.Get() && !SameIdentity(source, device.Get())) {
			device = source;
			adapter.Reset();
			const auto generation = snapshot.generation + 1;
			snapshot = {};
			snapshot.generation = generation;
			pending.fill(0);
			priority.fill(false);
			recoveryDemand.fill(0);
		}
		if (force || !snapshot.sequence || now < snapshot.sampledAtMs || now - snapshot.sampledAtMs >= maxAgeMs) {
			snapshot.local = {};
			snapshot.sampledAtMs = now;
			++snapshot.sequence;
			snapshot.result = source ? source->GetDeviceRemovedReason() : E_POINTER;
			if (SUCCEEDED(snapshot.result) && !adapter) {
				Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
				Microsoft::WRL::ComPtr<IDXGIAdapter> baseAdapter;
				snapshot.result = source->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
				if (SUCCEEDED(snapshot.result))
					snapshot.result = dxgiDevice->GetAdapter(&baseAdapter);
				if (SUCCEEDED(snapshot.result))
					snapshot.result = baseAdapter.As(&adapter);
				if (adapter) {
					DXGI_ADAPTER_DESC desc{};
					snapshot.result = adapter->GetDesc(&desc);
					snapshot.adapter = desc.AdapterLuid;
					if (FAILED(snapshot.result))
						adapter.Reset();
				}
			}
			if (SUCCEEDED(snapshot.result))
				snapshot.result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &snapshot.local);
		}
		snapshot.pendingBytes = 0;
		for (auto bytes : pending)
			snapshot.pendingBytes = Add(snapshot.pendingBytes, bytes);
		snapshot.recoveryDemandBytes = 0;
		for (auto bytes : recoveryDemand)
			snapshot.recoveryDemandBytes = Add(snapshot.recoveryDemandBytes, bytes);
		snapshot.priorityWork = priority[0] || priority[1];
		return snapshot;
	}

	GpuMemoryBudget::Reservation GpuMemoryBudget::ReserveLocked(Owner owner, std::uint64_t bytes)
	{
		Reservation result;
		auto& outstanding = pending[static_cast<std::size_t>(owner)];
		if (!bytes || bytes > UINT64_MAX - outstanding)
			return result;
		outstanding += bytes;
		result.service = this;
		result.owner = owner;
		result.generation = snapshot.generation;
		result.bytes = bytes;
		return result;
	}

	GpuMemoryBudget::Reservation GpuMemoryBudget::Reserve(Owner owner, ID3D11Device* source, std::uint64_t bytes)
	{
		std::scoped_lock lock(mutex);
		SampleLocked(source, 250, false);
		return ReserveLocked(owner, bytes);
	}

	GpuMemoryBudget::Reservation GpuMemoryBudget::TryReserveStreaming(ID3D11Device* source, std::uint64_t bytes, bool refill, bool required)
	{
		std::scoped_lock lock(mutex);
		const auto observation = SampleLocked(source, 0, true);
		if (!observation.Fresh(GetTickCount64()) || !StreamingFits(observation.local.Budget,
														observation.local.CurrentUsage, observation.pendingBytes, bytes, refill, required, observation.priorityWork, observation.recoveryDemandBytes))
			return {};
		return ReserveLocked(Owner::Streaming, bytes);
	}

	void GpuMemoryBudget::Release(Owner owner, std::uint64_t generation, std::uint64_t bytes) noexcept
	{
		std::scoped_lock lock(mutex);
		if (generation != snapshot.generation)
			return;
		auto& value = pending[static_cast<std::size_t>(owner)];
		value -= std::min(value, bytes);
	}

	void GpuMemoryBudget::SetOwnerWorkLocked(Owner owner, bool priorityWork, std::uint64_t recoveryDemandBytes) noexcept
	{
		const auto index = static_cast<std::size_t>(owner);
		priority[index] = priorityWork;
		recoveryDemand[index] = recoveryDemandBytes;
	}

	void GpuMemoryBudget::SetOwnerWork(Owner owner, bool priorityWork, std::uint64_t recoveryDemandBytes)
	{
		std::scoped_lock lock(mutex);
		SetOwnerWorkLocked(owner, priorityWork, recoveryDemandBytes);
	}

	void GpuMemoryBudget::SetPriorityWork(Owner owner, bool value)
	{
		std::scoped_lock lock(mutex);
		SetOwnerWorkLocked(owner, value, recoveryDemand[static_cast<std::size_t>(owner)]);
	}

	void GpuMemoryBudget::SetRecoveryDemand(Owner owner, std::uint64_t bytes)
	{
		std::scoped_lock lock(mutex);
		SetOwnerWorkLocked(owner, priority[static_cast<std::size_t>(owner)], bytes);
	}

	std::uint64_t GpuMemoryBudget::PendingBytes(Owner excluding) const
	{
		std::scoped_lock lock(mutex);
		std::uint64_t result = 0;
		for (std::size_t i = 0; i < pending.size(); ++i)
			if (i != static_cast<std::size_t>(excluding))
				result = Add(result, pending[i]);
		return result;
	}
}
