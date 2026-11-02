#pragma once

#include <FidelityFX/api/include/ffx_api.hpp>
#include <FidelityFX/api/include/ffx_api_loader.h>

extern ffxFunctions ffxModule;

namespace FidelityFXRuntime
{
	/** Creates a context through the loaded provider, retaining its descriptor chain. */
	template <class... Descriptors>
	ffx::ReturnCode CreateContext(ffx::Context& context, ffxAllocationCallbacks* allocator, Descriptors&... descriptors)
	{
		if (!ffxModule.CreateContext)
			return ffx::ReturnCode::ErrorNoProvider;
		return static_cast<ffx::ReturnCode>(ffxModule.CreateContext(&context, ffx::LinkHeaders(descriptors.header...), allocator));
	}

	/** Configures a context through the loaded provider or reports its absence. */
	template <class... Descriptors>
	ffx::ReturnCode Configure(ffx::Context& context, Descriptors&... descriptors)
	{
		if (!ffxModule.Configure)
			return ffx::ReturnCode::ErrorNoProvider;
		return static_cast<ffx::ReturnCode>(ffxModule.Configure(&context, ffx::LinkHeaders(descriptors.header...)));
	}

	/** Dispatches a descriptor chain through the loaded provider or reports its absence. */
	template <class... Descriptors>
	ffx::ReturnCode Dispatch(ffx::Context& context, Descriptors&... descriptors)
	{
		if (!ffxModule.Dispatch)
			return ffx::ReturnCode::ErrorNoProvider;
		return static_cast<ffx::ReturnCode>(ffxModule.Dispatch(&context, ffx::LinkHeaders(descriptors.header...)));
	}
}
