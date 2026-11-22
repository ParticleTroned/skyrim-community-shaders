#pragma once

#include "FlameHolderShadow.h"
#include <utility>

namespace RE
{
	class BSShadowLight;
}

namespace LightLimitFixDetail::ShadowCulling
{
	/** Bind shadow rejection to the synchronous native light render; null scopes suspend it. */
	class DrawScope
	{
	public:
		explicit DrawScope(RE::BSShadowLight* light) noexcept : light(light), previous(std::exchange(current, this)) {}
		~DrawScope() { current = previous; }
		DrawScope(const DrawScope&) = delete;
		DrawScope& operator=(const DrawScope&) = delete;

		[[nodiscard]] static RE::BSShadowLight* Current() noexcept { return current ? current->light : nullptr; }
		/** Available only inside a native light scope; nested scopes restore the outer cache. */
		[[nodiscard]] static FlameHolderContext* FlameHolder() noexcept { return current ? &current->flameHolder : nullptr; }

	private:
		RE::BSShadowLight* light;
		DrawScope* previous;
		FlameHolderContext flameHolder;
		static inline thread_local DrawScope* current = nullptr;
	};
}
