#pragma once

#include "../../Utils/D3D.h"

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
			context->Flush();
		}
		BOOL completed = FALSE;
		const HRESULT result = context->GetData(query.get(), &completed, sizeof(completed), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (result == S_OK && completed) {
			complete = true;
			return Result::Ready;
		}
		return result == S_FALSE || result == S_OK ? Result::Pending : Result::Failed;
	}

	void Reset() noexcept
	{
		query = nullptr;
		context = nullptr;
		complete = false;
	}

private:
	winrt::com_ptr<ID3D11Query> query;
	winrt::com_ptr<ID3D11DeviceContext> context;
	bool complete = false;
};
