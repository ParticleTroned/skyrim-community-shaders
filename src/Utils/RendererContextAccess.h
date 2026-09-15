#pragma once

#include "RendererOwnership.h"
#include <d3d11.h>
#include <type_traits>

namespace Util
{
	/** @brief Bind shared-context access to its current native renderer owner. */
	template <class Renderer>
	inline CRITICAL_SECTION* GetRendererContextLock(Renderer* a_renderer, ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_renderer || !a_context)
			return nullptr;
		if (a_context != reinterpret_cast<ID3D11DeviceContext*>(a_renderer->GetRuntimeData().context))
			return nullptr;
		using Lock = std::remove_reference_t<decltype(a_renderer->GetLock())>;
		static_assert(sizeof(Lock) == sizeof(CRITICAL_SECTION));
		static_assert(alignof(Lock) == alignof(CRITICAL_SECTION));
		return reinterpret_cast<CRITICAL_SECTION*>(&a_renderer->GetLock());
	}
}
