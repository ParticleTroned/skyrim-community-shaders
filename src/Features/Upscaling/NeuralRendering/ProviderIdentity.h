#pragma once

#include <cstddef>
#include <string_view>

namespace NeuralRendering
{
	enum class ProviderReleaseMatch
	{
		Unknown,
		PinnedReference,
		NoMatch
	};

	enum class ProviderInitialization
	{
		NotAttempted,
		Failed,
		Succeeded
	};

	/** Independent artifact and native-call evidence; neither grants GPU support. */
	struct ProviderIdentity
	{
		ProviderReleaseMatch releaseMatch = ProviderReleaseMatch::Unknown;
		ProviderInitialization initialization = ProviderInitialization::NotAttempted;
	};

	/** Build-pinned reference metadata, independent of the installed DLL's origin. */
	struct ProviderReference
	{
		std::string_view sha256;
		std::string_view package;
		std::string_view source;
	};

	/** Compare complete SHA-256 values without treating a mismatch as modification. */
	[[nodiscard]] constexpr ProviderReleaseMatch ClassifyProviderRelease(std::string_view hash, std::string_view reference)
	{
		if (hash.size() != 64 || reference.size() != 64)
			return ProviderReleaseMatch::Unknown;
		const auto hexDigit = [](char c) constexpr {
			if (c >= '0' && c <= '9')
				return c - '0';
			if (c >= 'a' && c <= 'f')
				return c - 'a' + 10;
			if (c >= 'A' && c <= 'F')
				return c - 'A' + 10;
			return -1;
		};
		bool matches = true;
		for (std::size_t i = 0; i < hash.size(); ++i) {
			const auto actual = hexDigit(hash[i]);
			const auto expected = hexDigit(reference[i]);
			if (actual < 0 || expected < 0)
				return ProviderReleaseMatch::Unknown;
			matches &= actual == expected;
		}
		return matches ? ProviderReleaseMatch::PinnedReference : ProviderReleaseMatch::NoMatch;
	}

	[[nodiscard]] constexpr const char* ToString(ProviderReleaseMatch match)
	{
		switch (match) {
		case ProviderReleaseMatch::PinnedReference:
			return "pinned_reference_match";
		case ProviderReleaseMatch::NoMatch:
			return "no_reference_match";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] constexpr const char* ToString(ProviderInitialization initialization)
	{
		switch (initialization) {
		case ProviderInitialization::Succeeded:
			return "legacy_initialized";
		case ProviderInitialization::Failed:
			return "legacy_initialization_failed";
		default:
			return "untested";
		}
	}
}
