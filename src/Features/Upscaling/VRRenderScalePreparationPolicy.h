#pragma once

#include <cstdint>

namespace VRRenderScalePreparationPolicy
{
	struct Key
	{
		bool vrRuntime = false;
		uint64_t requestID = 0;
		uint64_t transitionEpoch = 0;
		uint64_t optionsGeneration = 0;
		uint64_t shaderDefinesGeneration = 0;
		uintptr_t deviceIdentity = 0;
		uint32_t method = 0;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 0;
		uint32_t renderEyeWidth = 0;
		uint32_t renderEyeHeight = 0;
		uint32_t displayEyeWidth = 0;
		uint32_t displayEyeHeight = 0;
		bool renderScaleEnabled = false;
		bool fsr4RuntimeEnabled = false;

		constexpr bool operator==(const Key&) const = default;

		[[nodiscard]] constexpr bool Valid() const noexcept
		{
			return vrRuntime &&
			       requestID != 0 &&
			       transitionEpoch != 0 &&
			       optionsGeneration != 0 &&
			       deviceIdentity != 0 &&
			       renderEyeWidth != 0 &&
			       renderEyeHeight != 0 &&
			       displayEyeWidth != 0 &&
			       displayEyeHeight != 0;
		}
	};

	enum class CompletionDisposition : uint8_t
	{
		Publish,
		NotApplicable,
		Cancelled,
		Superseded,
		DeviceChanged,
		ShaderFailure,
		ProviderFailure
	};

	struct CompletionFacts
	{
		Key candidate{};
		Key latest{};
		uintptr_t currentDeviceIdentity = 0;
		bool cancelled = false;
		bool shadersReady = false;
		bool providerReady = false;
	};

	[[nodiscard]] constexpr bool ShouldAttempt(
		uint64_t a_requestID,
		uint64_t a_attemptedRequestID) noexcept
	{
		return a_requestID != 0 && a_requestID != a_attemptedRequestID;
	}

	[[nodiscard]] constexpr CompletionDisposition SelectCompletionDisposition(
		const CompletionFacts& a_facts) noexcept
	{
		if (!a_facts.candidate.vrRuntime)
			return CompletionDisposition::NotApplicable;
		if (a_facts.cancelled)
			return CompletionDisposition::Cancelled;
		if (!a_facts.candidate.Valid())
			return CompletionDisposition::Superseded;
		if (a_facts.currentDeviceIdentity == 0 ||
			a_facts.currentDeviceIdentity != a_facts.candidate.deviceIdentity) {
			return CompletionDisposition::DeviceChanged;
		}
		if (a_facts.candidate != a_facts.latest)
			return CompletionDisposition::Superseded;
		if (!a_facts.shadersReady)
			return CompletionDisposition::ShaderFailure;
		if (!a_facts.providerReady)
			return CompletionDisposition::ProviderFailure;
		return CompletionDisposition::Publish;
	}

	[[nodiscard]] constexpr bool CanUsePreparedResult(
		const Key& a_prepared,
		const Key& a_current,
		uintptr_t a_currentDeviceIdentity) noexcept
	{
		return a_prepared.Valid() &&
		       a_prepared == a_current &&
		       a_currentDeviceIdentity == a_prepared.deviceIdentity;
	}

	[[nodiscard]] constexpr bool RetainsProtectedFallback(
		CompletionDisposition a_disposition) noexcept
	{
		return a_disposition != CompletionDisposition::Publish &&
		       a_disposition != CompletionDisposition::NotApplicable;
	}
}
