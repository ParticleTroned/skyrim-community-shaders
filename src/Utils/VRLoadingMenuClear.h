#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace Util::VRLoadingMenuClear
{
	enum class Installation
	{
		NotInstalled,
		NotApplicable,
		UnsupportedRuntime,
		UnexpectedMessageCall,
		UnexpectedRenderRetry,
		Installed
	};

	inline constexpr std::uintptr_t kMessageCallRva = 0x8C22FB;
	inline constexpr std::uintptr_t kRenderRetryCallRva = 0x132450D;
	inline constexpr std::uintptr_t kNativeClearRva = 0x1327790;
	inline constexpr std::array<std::uint8_t, 5> kMessageCall{ 0xE8, 0x90, 0x54, 0xA6, 0x00 };
	inline constexpr std::array<std::uint8_t, 5> kRenderRetryCall{ 0xE8, 0x7E, 0x32, 0x00, 0x00 };

	struct Status
	{
		const char* state = "not_installed";
		bool installed = false;
		bool applicable = true;
		bool ready = false;
		std::uint64_t attempted = 0;
		std::uint64_t executed = 0;
		std::uint64_t deferred = 0;
		std::uint64_t missingRenderer = 0;
	};

	/** @brief Return why installation must be skipped, or nullopt when both native calls match. */
	[[nodiscard]] constexpr std::optional<Installation> ValidateAdmission(
		bool a_isVR, bool a_supportedRuntime,
		std::span<const std::uint8_t> a_messageCall,
		std::span<const std::uint8_t> a_renderRetryCall) noexcept
	{
		if (!a_isVR)
			return Installation::NotApplicable;
		if (!a_supportedRuntime)
			return Installation::UnsupportedRuntime;
		if (a_messageCall.size() != kMessageCall.size() || !std::equal(a_messageCall.begin(), a_messageCall.end(), kMessageCall.begin()))
			return Installation::UnexpectedMessageCall;
		if (a_renderRetryCall.size() != kRenderRetryCall.size() || !std::equal(a_renderRetryCall.begin(), a_renderRetryCall.end(), kRenderRetryCall.begin()))
			return Installation::UnexpectedRenderRetry;
		return std::nullopt;
	}

	/** @brief Report readiness only after installation or when the runtime does not need this guard. */
	[[nodiscard]] constexpr Status DescribeInstallation(Installation a_installation) noexcept
	{
		switch (a_installation) {
		case Installation::NotApplicable:
			return { .state = "not_applicable", .applicable = false, .ready = true };
		case Installation::UnsupportedRuntime:
			return { .state = "unsupported_runtime" };
		case Installation::UnexpectedMessageCall:
			return { .state = "unexpected_message_call" };
		case Installation::UnexpectedRenderRetry:
			return { .state = "unexpected_render_retry" };
		case Installation::Installed:
			return { .state = "installed", .installed = true, .ready = true };
		case Installation::NotInstalled:
			return {};
		}
		return {};
	}

	enum class Result
	{
		Executed,
		Deferred,
		MissingRenderer
	};

	/** @brief Run pending clears only with renderer ownership; retain pending work when acquisition fails. */
	template <class Callback>
	[[nodiscard]] Result TryExecute(CRITICAL_SECTION* a_lock, Callback&& a_clear)
	{
		if (!a_lock)
			return Result::MissingRenderer;
		if (!TryEnterCriticalSection(a_lock))
			return Result::Deferred;

		struct ReleaseOwnership
		{
			CRITICAL_SECTION* lock;
			~ReleaseOwnership() { LeaveCriticalSection(lock); }
		} release{ a_lock };
		std::forward<Callback>(a_clear)();
		return Result::Executed;
	}

	/** @brief Guard the validated VR loading-message clear call without changing SE or AE. */
	void Install();

	/** @brief Read installation and admission counters without touching the renderer or D3D context. */
	[[nodiscard]] Status GetStatus() noexcept;
}
