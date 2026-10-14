#pragma once

#include "Utils/D3D.h"
#include "Utils/Format.h"

#include <exception>
#include <initializer_list>
#include <utility>
#include <vector>
#include <winrt/base.h>

namespace Util
{
	/**
	 * @brief Synchronous, compile-once cache for one standalone shader.
	 *
	 * Preparation failures are logged and remain latched until Reset(). Get() returns
	 * null on failure; callers must check it before binding or dispatching.
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
			return GetOrCompile(a_path, [&]() -> const auto& { return a_defines; }, a_target, a_entry, a_name, a_timing);
		}

		/** @brief Accepts fixed macros without allocating on a cached or failed request. */
		ShaderT* Get(
			const wchar_t* a_path,
			std::initializer_list<std::pair<const char*, const char*>> a_defines,
			const char* a_target,
			const char* a_entry = "main",
			const char* a_name = nullptr,
			ShaderCompileTiming* a_timing = nullptr)
		{
			return GetOrCompile(a_path, [&] { return std::vector<std::pair<const char*, const char*>>(a_defines); }, a_target, a_entry, a_name, a_timing);
		}

		/** @brief Drops the cached shader and failure latch. */
		void Reset()
		{
			shader = nullptr;
			failed = false;
		}

		/** @brief Returns the cached shader without triggering compilation. */
		ShaderT* get() const { return shader.get(); }

		/** @brief Returns whether a shader is currently cached. */
		explicit operator bool() const { return shader != nullptr; }

	private:
		template <class GetDefines>
		ShaderT* GetOrCompile(const wchar_t* a_path, GetDefines&& a_getDefines,
			const char* a_target, const char* a_entry, const char* a_name, ShaderCompileTiming* a_timing)
		{
			if (shader || failed)
				return shader.get();
			failed = true;
			try {
				const auto& defines = a_getDefines();
				logger::debug("Compiling {}", Util::WStringToString(a_path));
				winrt::com_ptr<ShaderT> prepared;
				prepared.attach(static_cast<ShaderT*>(Util::CompileShader(a_path, defines, a_target, a_entry, a_timing)));
				if (prepared && a_name)
					Util::SetResourceName(prepared.get(), a_name);
				shader = std::move(prepared);
				failed = !shader;
			} catch (const std::exception& error) {
				logger::error("Failed to prepare standalone shader {}: {}", a_name ? a_name : a_entry, error.what());
			} catch (...) {
				logger::error("Failed to prepare standalone shader {}: unknown exception", a_name ? a_name : a_entry);
			}
			return shader.get();
		}

		winrt::com_ptr<ShaderT> shader;
		bool failed = false;
	};
}
