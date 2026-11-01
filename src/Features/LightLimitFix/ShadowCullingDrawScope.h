#pragma once

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
		explicit DrawScope(RE::BSShadowLight* light) noexcept : previous(std::exchange(current, light)) {}
		~DrawScope() { current = previous; }
		DrawScope(const DrawScope&) = delete;
		DrawScope& operator=(const DrawScope&) = delete;

		[[nodiscard]] static RE::BSShadowLight* Current() noexcept { return current; }

	private:
		RE::BSShadowLight* previous;
		static inline thread_local RE::BSShadowLight* current = nullptr;
	};
}
