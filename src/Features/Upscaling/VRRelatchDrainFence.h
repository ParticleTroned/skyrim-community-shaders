#pragma once

#include "../../Utils/D3D.h"
#include "VRRenderScaleRetryTelemetry.h"

#include <d3d11.h>
#include <winrt/base.h>

/** One retained event query; observing completion never releases provider resources. */
class VRRelatchDrainFence
{
public:
	enum class Result
	{
		Ready,
		Pending,
		Failed
	};

	[[nodiscard]] Result Poll(ID3D11DeviceContext* a_context, const char* a_name)
	{
		if (!a_context || (context && context.get() != a_context))
			return Result::Failed;
		if (complete)
			return Result::Ready;
		if (!query) {
			winrt::com_ptr<ID3D11Device> device;
			a_context->GetDevice(device.put());
			if (!device)
				return Result::Failed;
			const D3D11_QUERY_DESC desc{ D3D11_QUERY_EVENT, 0 };
			if (FAILED(device->CreateQuery(&desc, query.put())))
				return Result::Failed;
			Util::SetResourceName(query.get(), "%s", a_name);
			context.copy_from(a_context);
			context->End(query.get());
#ifdef DEVBENCH_BRIDGE_ENABLED
			observation.observed = true;
			observation.issueQpc = ObserveQpc();
			observation.deviceIdentity = reinterpret_cast<uintptr_t>(device.get());
			observation.contextIdentity = reinterpret_cast<uintptr_t>(context.get());
			observation.fenceIdentity = reinterpret_cast<uintptr_t>(query.get());
#endif
			context->Flush();
		}
		BOOL completed = FALSE;
		const HRESULT result = context->GetData(query.get(), &completed, sizeof(completed), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (result == S_OK && completed) {
			complete = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
			observation.readyQpc = ObserveQpc();
			observation.result = VRRenderScaleRetryTelemetry::FenceResult::Ready;
#endif
			return Result::Ready;
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		observation.result = result == S_FALSE || result == S_OK ?
		                         VRRenderScaleRetryTelemetry::FenceResult::Pending :
		                         VRRenderScaleRetryTelemetry::FenceResult::Failed;
#endif
		return result == S_FALSE || result == S_OK ? Result::Pending : Result::Failed;
	}

	void Reset() noexcept
	{
		query = nullptr;
		context = nullptr;
		complete = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		observation = {};
#endif
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Copies timestamps from prior operations without touching the device or query. */
	[[nodiscard]] VRRenderScaleRetryTelemetry::DrainFenceObservation GetObservation() const noexcept { return observation; }
#endif

private:
	winrt::com_ptr<ID3D11Query> query;
	winrt::com_ptr<ID3D11DeviceContext> context;
	bool complete = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
	static uint64_t ObserveQpc() noexcept
	{
		LARGE_INTEGER tick{};
		return QueryPerformanceCounter(&tick) ? static_cast<uint64_t>(tick.QuadPart) : 0;
	}
	VRRenderScaleRetryTelemetry::DrainFenceObservation observation{};
#endif
};
