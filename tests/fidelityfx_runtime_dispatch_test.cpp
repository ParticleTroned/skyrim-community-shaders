#include "Features/Upscaling/FidelityFXRuntime.h"

#include <cassert>
#include <iostream>

ffxFunctions ffxModule{};

namespace
{
	struct Descriptor
	{
		ffxApiHeader header{};
	};
	const ffxApiHeader* observedHeader = nullptr;
	ffxContext* observedContext = nullptr;
	const ffxAllocationCallbacks* observedAllocator = nullptr;
	int contextStorage = 0;

	ffxReturnCode_t Create(ffxContext* context, ffxCreateContextDescHeader* header, const ffxAllocationCallbacks* allocator)
	{
		observedContext = context;
		observedHeader = header;
		observedAllocator = allocator;
		*context = &contextStorage;
		return FFX_API_RETURN_OK;
	}

	ffxReturnCode_t Configure(ffxContext* context, const ffxConfigureDescHeader* header)
	{
		observedContext = context;
		observedHeader = header;
		return FFX_API_RETURN_ERROR_PARAMETER;
	}

	ffxReturnCode_t Dispatch(ffxContext* context, const ffxDispatchDescHeader* header)
	{
		observedContext = context;
		observedHeader = header;
		return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
	}
}

int main()
{
	Descriptor first, second;
	ffx::Context context = nullptr;
	using ffx::ReturnCode;
	assert(FidelityFXRuntime::CreateContext(context, nullptr, first) == ReturnCode::ErrorNoProvider);
	assert(FidelityFXRuntime::Configure(context, first) == ReturnCode::ErrorNoProvider);
	assert(FidelityFXRuntime::Dispatch(context, first) == ReturnCode::ErrorNoProvider);
	assert(context == nullptr && observedHeader == nullptr);

	ffxModule.CreateContext = Create;
	ffxModule.Configure = Configure;
	ffxModule.Dispatch = Dispatch;
	ffxAllocationCallbacks allocator{};
	first.header.pNext = &first.header;
	second.header.pNext = &second.header;
	assert(FidelityFXRuntime::CreateContext(context, &allocator, first, second) == ReturnCode::Ok);
	assert(context == &contextStorage && observedContext == &context && observedAllocator == &allocator);
	assert(observedHeader == &first.header && first.header.pNext == &second.header && second.header.pNext == nullptr);
	assert(FidelityFXRuntime::Configure(context, first) == ReturnCode::ErrorParameter);
	assert(observedContext == &context && observedHeader == &first.header && first.header.pNext == nullptr);
	assert(FidelityFXRuntime::Dispatch(context, second, first) == ReturnCode::ErrorRuntimeError);
	assert(observedContext == &context && observedHeader == &second.header && second.header.pNext == &first.header && first.header.pNext == nullptr);
	std::cout << "PASS: loaded provider dispatch, descriptor chains, allocator and failure propagation\n";
}
