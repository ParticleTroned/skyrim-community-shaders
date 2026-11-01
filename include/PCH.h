#pragma once

#include <shared_mutex>
#include <new>
#include <cstdio>
#include <exception>
#include <source_location>
#include <string_view>
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line);
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags,
	unsigned debugFlags, const char* file, int line);

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <xbyak/xbyak.h>

#include <detours/detours.h>

#ifdef NDEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Utils/VirtualFunctionHook.h"

#ifndef TRACY_SUPPORT
	#undef TRACY_ENABLE
#endif

using namespace std::literals;

namespace stl
{
	using namespace SKSE::stl;

	template <class T, std::size_t Size = 5>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = SKSE::GetTrampoline();
		if (Size == 6) {
			T::func = *(uintptr_t*)trampoline.write_call<6>(a_src, T::thunk);
		} else {
			T::func = trampoline.write_call<Size>(a_src, T::thunk);
		}
	}

	template <class F, size_t index, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[index] };
		T::func = vtbl.write_vfunc(T::size, T::thunk);
	}

	template <std::size_t idx, class T>
	void write_vfunc(REL::VariantID id)
	{
		REL::Relocation<std::uintptr_t> vtbl{ id };
		T::func = vtbl.write_vfunc(idx, T::thunk);
	}

	template <std::size_t idx, class T>
	void write_vfunc(REL::VariantOffset offset)
	{
		REL::Relocation<std::uintptr_t> vtbl{ offset };
		T::func = vtbl.write_vfunc(idx, T::thunk);
	}

	template <class T>
	void write_thunk_jmp(std::uintptr_t a_src)
	{
		auto& trampoline = SKSE::GetTrampoline();
		T::func = trampoline.write_branch<5>(a_src, T::thunk);
	}

	template <class F, class T>
	void write_vfunc()
	{
		write_vfunc<F, 0, T>();
	}

	template <class T>
	long detour_thunk(REL::RelocationID a_relId)
	{
		const auto address = a_relId.address();
		const auto result = Util::InstallDetour(address, reinterpret_cast<PVOID*>(&T::func), reinterpret_cast<PVOID>(T::thunk));
		if (result.error != NO_ERROR)
			SKSE::log::error("[Hooks] Detour attach failed at 0x{:X} (error {})", address, result.error);
		return result.error;
	}

	template <class T>
	void detour_thunk_ignore_func(REL::RelocationID a_relId)
	{
		const auto address = a_relId.address();
		const auto result = Util::InstallDetour(address, nullptr, reinterpret_cast<PVOID>(T::thunk));
		if (result.error != NO_ERROR)
			SKSE::log::error("[Hooks] Detour attach failed at 0x{:X} (error {})", address, result.error);
	}

	template <std::size_t idx, class T, class Interface>
	void detour_vfunc(Interface* target)
	{
		const auto result = Util::InstallVirtualFunctionHook(target, idx,
			reinterpret_cast<PVOID*>(&T::func), reinterpret_cast<PVOID>(T::thunk));
		if (result.error != NO_ERROR) {
			SKSE::log::critical("[Hooks] Failed to install graphics slot {} (error {}, Detours {})", idx, result.error, result.detourError);
			stl::report_and_fail("A required graphics hook could not be installed. See CSX.log.");
		}
		if (result.detourError != NO_ERROR)
			SKSE::log::warn("[Hooks] Detours failed for graphics slot {} (error {}); using {}", idx, result.detourError,
				result.method == Util::VirtualHookMethod::Clone ? "a retained vtable clone" : "an in-place vtable hook");
	}
}

namespace logger = SKSE::log;

namespace util
{
	using SKSE::stl::report_and_fail;
}

#include "Plugin.h"
#include <wrl/client.h>
#include <wrl/event.h>

#include <DirectXColors.h>
#include <DirectXMath.h>

namespace DX
{
	/** A COM failure with bounded diagnostic text and its original HRESULT. */
	class com_exception : public std::exception
	{
	public:
		explicit com_exception(HRESULT hr, const std::source_location& where = std::source_location::current()) noexcept :
			result(hr)
		{
			std::string_view file = where.file_name();
			if (const auto separator = file.find_last_of("\\/"); separator != std::string_view::npos)
				file.remove_prefix(separator + 1);
			const int written = std::snprintf(message, sizeof(message), "HRESULT 0x%08X at %.*s:%u",
				static_cast<unsigned int>(result), static_cast<int>(file.size()), file.data(), static_cast<unsigned int>(where.line()));
			if (written < 0 || static_cast<std::size_t>(written) >= sizeof(message)) {
				std::snprintf(message, sizeof(message), "HRESULT 0x%08X at line %u",
					static_cast<unsigned int>(result), static_cast<unsigned int>(where.line()));
				return;
			}

			// Descriptive lookup must not replace the HRESULT when memory is exhausted.
			wchar_t systemMessage[kSystemMessageCapacity]{};
			DWORD length = ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
				static_cast<DWORD>(result), 0, systemMessage, static_cast<DWORD>(kSystemMessageCapacity), nullptr);
			while (length && (systemMessage[length - 1] == L'\r' || systemMessage[length - 1] == L'\n' ||
							  systemMessage[length - 1] == L' ' || systemMessage[length - 1] == L'\t'))
				--length;
			const auto prefixLength = static_cast<std::size_t>(written);
			if (!length || prefixLength + 3 >= sizeof(message))
				return;
			message[prefixLength] = ':';
			message[prefixLength + 1] = ' ';
			const int converted = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, systemMessage, static_cast<int>(length),
				message + prefixLength + 2, static_cast<int>(sizeof(message) - prefixLength - 3), nullptr, nullptr);
			message[converted > 0 ? prefixLength + 2 + static_cast<std::size_t>(converted) : prefixLength] = '\0';
		}

		const char* what() const noexcept override { return message; }

		/** Returns the unmodified failure code for recovery decisions. */
		HRESULT Error() const noexcept { return result; }

	private:
		static constexpr std::size_t kMessageCapacity = 1024;
		static constexpr std::size_t kSystemMessageCapacity = 256;

		HRESULT result;
		char message[kMessageCapacity]{};
	};

	namespace detail
	{
		// Keep exception storage and diagnostic formatting out of successful call paths.
		[[noreturn]] __declspec(noinline) inline void ThrowComFailure(HRESULT hr, const std::source_location& where)
		{
			throw com_exception(hr, where);
		}
	}

	/** Throws a COM exception naming the caller when its HRESULT indicates failure. */
	inline void ThrowIfFailed(HRESULT hr, const std::source_location& where = std::source_location::current())
	{
		if (FAILED(hr)) {
			detail::ThrowComFailure(hr, where);
		}
	}
}

#include <ClibUtil/distribution.hpp>
#include <ClibUtil/editorID.hpp>
#include <ClibUtil/numeric.hpp>
#include <ClibUtil/rng.hpp>
#include <ClibUtil/simpleINI.hpp>

#include <imgui.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <magic_enum/magic_enum.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/bitset.h>
#include <EASTL/bonus/fixed_ring_buffer.h>
#include <EASTL/fixed_list.h>
#include <EASTL/fixed_slist.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/functional.h>
#include <EASTL/hash_set.h>
#include <EASTL/map.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/set.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/tuple.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <ankerl/unordered_dense.h>
template <>
struct ankerl::unordered_dense::hash<std::string>
{
	using is_transparent = void;  // enable heterogeneous overloads
	using is_avalanching = void;  // mark class as high quality avalanching hash

	[[nodiscard]] auto operator()(std::string_view str) const noexcept -> uint64_t
	{
		return ankerl::unordered_dense::hash<std::string_view>{}(str);
	}
};

#include "SimpleMath.h"

using float2 = DirectX::SimpleMath::Vector2;
using float3 = DirectX::SimpleMath::Vector3;
using float4 = DirectX::SimpleMath::Vector4;
using float4x4 = DirectX::SimpleMath::Matrix;
using uint = uint32_t;

#include "Profiler.h"
#include "Globals.h"
#include "Util.h"
#include "Feature.h"
#include "Buffer.h"
#include "EngineFix.h"
