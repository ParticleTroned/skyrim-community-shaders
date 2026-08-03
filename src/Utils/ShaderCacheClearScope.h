#pragma once

namespace Util
{
	/** @brief Which shaders a clear-cache action removes. */
	enum class ShaderCacheClearScope
	{
		Full,
		ActiveOnly
	};

	/** @brief Resolves the configured default, inverted by the desktop Shift modifier. */
	constexpr ShaderCacheClearScope ResolveShaderCacheClearScope(bool a_smartDefault, bool a_shiftDown)
	{
		const bool useSmart = a_smartDefault != a_shiftDown;
		return useSmart ? ShaderCacheClearScope::ActiveOnly : ShaderCacheClearScope::Full;
	}

	static_assert(ResolveShaderCacheClearScope(false, false) == ShaderCacheClearScope::Full);
	static_assert(ResolveShaderCacheClearScope(false, true) == ShaderCacheClearScope::ActiveOnly);
	static_assert(ResolveShaderCacheClearScope(true, false) == ShaderCacheClearScope::ActiveOnly);
	static_assert(ResolveShaderCacheClearScope(true, true) == ShaderCacheClearScope::Full);
}
