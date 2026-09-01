#pragma once

#include "Utils/D3D.h"
#include "Utils/Format.h"

#include <vector>
#include <winrt/base.h>

namespace Util
{
	/**
	 * @brief Synchronous, compile-once cache for one standalone shader.
	 *
	 * A failed compile remains latched until Reset() so a broken shader is not
	 * retried every frame. Callers must check Get() before binding or dispatching.
	 */
	template <typename ShaderT>
	class LazyShader
	{
	public:
		/**
		 * @brief Returns the cached shader, compiling it on first use.
		 *
		 * @param a_name Optional RenderDoc resource name applied after compilation.
		 */
		ShaderT* Get(
			const wchar_t* a_path,
			const std::vector<std::pair<const char*, const char*>>& a_defines,
			const char* a_target,
			const char* a_entry = "main",
			const char* a_name = nullptr,
			ShaderCompileTiming* a_timing = nullptr)
		{
			if (!shader && !failed) {
				logger::debug("Compiling {}", Util::WStringToString(a_path));
				shader.attach(static_cast<ShaderT*>(
					Util::CompileShader(
						a_path,
						a_defines,
						a_target,
						a_entry,
						a_timing)));
				failed = !shader;
				if (shader && a_name)
					Util::SetResourceName(shader.get(), a_name);
			}
			return shader.get();
		}

		/** @brief Drops the cached shader and failure latch. */
		void Reset()
		{
			shader = nullptr;
			failed = false;
		}

		/** @brief Returns the cached shader without triggering compilation. */
		ShaderT* get() const { return shader.get(); }
		/** @brief Returns whether first-use compilation failed and remains latched. */
		bool HasFailed() const { return failed; }

		/** @brief Returns whether a shader is currently cached. */
		explicit operator bool() const { return shader != nullptr; }

	private:
		winrt::com_ptr<ShaderT> shader;
		bool failed = false;
	};
}
