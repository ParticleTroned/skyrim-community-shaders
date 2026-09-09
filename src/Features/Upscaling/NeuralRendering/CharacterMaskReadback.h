#pragma once

#include <d3d11.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <span>

namespace NeuralRendering
{
	// This is a current-frame GPU readiness deadline, not a bound on the tiny
	// staging copy's execution time. Both eye copies must be queued before the
	// caller flushes ONCE and shares this absolute deadline across their reads.
	inline constexpr auto kCharacterMaskReadbackBudget = std::chrono::milliseconds(50);

	enum class CharacterMaskReadbackStatus
	{
		Ready,
		Timeout,
		QueryFailed,
		MapUnavailable,
	};

	struct CharacterMaskReadbackResult
	{
		CharacterMaskReadbackStatus status = CharacterMaskReadbackStatus::QueryFailed;
		HRESULT result = E_INVALIDARG;
		double waitMs = 0.0;

		[[nodiscard]] bool Ready() const noexcept { return status == CharacterMaskReadbackStatus::Ready; }
		[[nodiscard]] const char* Reason() const noexcept
		{
			switch (status) {
			case CharacterMaskReadbackStatus::Ready:
				return "current_copy_ready";
			case CharacterMaskReadbackStatus::Timeout:
				return "current_copy_timeout";
			case CharacterMaskReadbackStatus::QueryFailed:
				return "query_failed";
			case CharacterMaskReadbackStatus::MapUnavailable:
				return "current_map_unavailable";
			}
			return "query_failed";
		}
	};

	/**
	 * Consume one already submitted current-mask copy. Never flush, block in Map,
	 * or read an older copy as current coverage. The owner must validate the copy's
	 * source/content identity. Destination is unchanged unless the complete read
	 * succeeds. A ready second eye may complete with a nonblocking probe even when
	 * the first eye used the shared deadline. Driver calls are not preemptible.
	 */
	[[nodiscard]] inline CharacterMaskReadbackResult ReadCharacterMaskBounds(
		ID3D11DeviceContext* a_context, ID3D11Query* a_query,
		ID3D11Buffer* a_staging, std::span<std::byte> a_destination,
		std::chrono::steady_clock::time_point a_deadline) noexcept
	{
		using Clock = std::chrono::steady_clock;
		const auto start = Clock::now();
		const auto finish = [&](CharacterMaskReadbackStatus a_status, HRESULT a_result) {
			return CharacterMaskReadbackResult{
				a_status, a_result,
				std::chrono::duration<double, std::milli>(Clock::now() - start).count()
			};
		};
		if (!a_context || !a_query || !a_staging || a_destination.empty() ||
			a_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
			return finish(CharacterMaskReadbackStatus::QueryFailed, E_INVALIDARG);
		D3D11_BUFFER_DESC desc{};
		a_staging->GetDesc(&desc);
		if (desc.Usage != D3D11_USAGE_STAGING || !(desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) ||
			a_destination.size() != desc.ByteWidth)
			return finish(CharacterMaskReadbackStatus::MapUnavailable, E_INVALIDARG);

		bool ready = false;
		for (unsigned polls = 0;; ++polls) {
			if (!ready) {
				BOOL completed = FALSE;
				const auto result = a_context->GetData(a_query, &completed, sizeof(completed), D3D11_ASYNC_GETDATA_DONOTFLUSH);
				if (FAILED(result))
					return finish(CharacterMaskReadbackStatus::QueryFailed, result);
				ready = result == S_OK && completed;
			}
			if (ready) {
				D3D11_MAPPED_SUBRESOURCE mapped{};
				const auto result = a_context->Map(a_staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
				if (SUCCEEDED(result)) {
					if (!mapped.pData) {
						a_context->Unmap(a_staging, 0);
						return finish(CharacterMaskReadbackStatus::MapUnavailable, E_POINTER);
					}
					std::memcpy(a_destination.data(), mapped.pData, a_destination.size());
					a_context->Unmap(a_staging, 0);
					return finish(CharacterMaskReadbackStatus::Ready, S_OK);
				}
				if (result != DXGI_ERROR_WAS_STILL_DRAWING || Clock::now() >= a_deadline)
					return finish(CharacterMaskReadbackStatus::MapUnavailable, result);
			} else if (Clock::now() >= a_deadline) {
				return finish(CharacterMaskReadbackStatus::Timeout, HRESULT_FROM_WIN32(ERROR_TIMEOUT));
			}
			// Spin briefly for an immediately ready copy, then yield the CPU while
			// the GPU finishes queued world/NR work. Never busy-spin for 50 ms.
			if (polls < 32)
				YieldProcessor();
			else
				Sleep(1);
		}
	}
}
