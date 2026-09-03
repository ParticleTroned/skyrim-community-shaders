#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace CSX::MeshBlendingPolicy
{
	[[nodiscard]] inline std::string NormalizePath(
		std::string_view a_path,
		bool a_isModelPath)
	{
		std::string normalized;
		normalized.reserve(a_path.size() + (a_isModelPath ? 7u : 0u));
		bool previousSlash = false;
		for (char value : a_path) {
			char character = value == '\\' ? '/' : value;
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
			if (character == '/') {
				if (previousSlash) {
					continue;
				}
				previousSlash = true;
			} else {
				previousSlash = false;
			}
			normalized.push_back(character);
		}

		while (normalized.starts_with("./")) {
			normalized.erase(0u, 2u);
		}
		while (!normalized.empty() && normalized.front() == '/') {
			normalized.erase(normalized.begin());
		}
		while (!normalized.empty() && normalized.back() == '/') {
			normalized.pop_back();
		}

		if (a_isModelPath) {
			if (normalized == "data" || normalized == "meshes") {
				return {};
			}
			if (normalized.starts_with("data/")) {
				normalized.erase(0u, 5u);
			}
			if (normalized == "meshes") {
				return {};
			}
			if (!normalized.empty() && !normalized.starts_with("meshes/")) {
				normalized.insert(0u, "meshes/");
			}
		}
		return normalized;
	}

	[[nodiscard]] inline std::string NormalizeTexturePath(std::string_view a_path)
	{
		auto normalized = NormalizePath(a_path, false);
		if (normalized == "data" || normalized == "textures") {
			return {};
		}
		if (normalized.starts_with("data/")) {
			normalized.erase(0u, 5u);
		}
		if (normalized == "textures") {
			return {};
		}
		if (!normalized.empty() && !normalized.starts_with("textures/")) {
			normalized.insert(0u, "textures/");
		}
		return normalized;
	}

	[[nodiscard]] constexpr bool HasWildcard(std::string_view a_value) noexcept
	{
		return a_value.find_first_of("*?") != std::string_view::npos;
	}

	enum class CachedClassification : std::uint8_t
	{
		kRejected,
		kAllowedByRule,
		kAutomatic,
	};

	[[nodiscard]] constexpr bool CanReuseCacheHit(
		CachedClassification a_classification,
		bool a_rootHasAnimation,
		bool a_automaticReceiverIsCurrentAndSafe) noexcept
	{
		if (a_classification == CachedClassification::kRejected)
			return true;
		if (a_rootHasAnimation)
			return false;
		return a_classification != CachedClassification::kAutomatic ||
		       a_automaticReceiverIsCurrentAndSafe;
	}

	[[nodiscard]] constexpr std::string_view TrimAsciiSpaces(std::string_view a_value) noexcept
	{
		const auto first = a_value.find_first_not_of(' ');
		if (first == std::string_view::npos)
			return {};
		const auto lastNonSpace = a_value.find_last_not_of(' ');
		return a_value.substr(first, lastNonSpace - first + 1);
	}

	[[nodiscard]] constexpr bool HasLandscapeSelector(
		std::string_view a_form,
		std::string_view a_editorID,
		std::string_view a_diffuse) noexcept
	{
		return !TrimAsciiSpaces(a_form).empty() ||
		       !TrimAsciiSpaces(a_editorID).empty() ||
		       !TrimAsciiSpaces(a_diffuse).empty();
	}

	struct CanonicalOverrideSelectors
	{
		std::string model;
		std::string nodePath;
		bool modelWasSupplied = false;
		bool nodePathWasSupplied = false;

		[[nodiscard]] bool ModelCollapsed() const noexcept
		{
			return modelWasSupplied && model.empty();
		}

		[[nodiscard]] bool NodePathCollapsed() const noexcept
		{
			return nodePathWasSupplied && nodePath.empty();
		}

		[[nodiscard]] bool HasSelector() const noexcept
		{
			return !model.empty() || !nodePath.empty();
		}

		[[nodiscard]] bool IsExactPair() const noexcept
		{
			return !model.empty() && !nodePath.empty() &&
			       !HasWildcard(model) && !HasWildcard(nodePath);
		}
	};

	[[nodiscard]] inline CanonicalOverrideSelectors CanonicalizeOverrideSelectors(
		std::string_view a_model,
		std::string_view a_nodePath)
	{
		return {
			.model = NormalizePath(a_model, true),
			.nodePath = NormalizePath(a_nodePath, false),
			.modelWasSupplied = !a_model.empty(),
			.nodePathWasSupplied = !a_nodePath.empty(),
		};
	}

	struct CanonicalLandscapeSelectors
	{
		std::string form;
		std::string editorID;
		std::string diffuse;
		bool diffuseWasSupplied = false;

		[[nodiscard]] bool DiffuseCollapsed() const noexcept
		{
			return diffuseWasSupplied && diffuse.empty();
		}

		[[nodiscard]] bool HasSelector() const noexcept
		{
			return !form.empty() || !editorID.empty() || !diffuse.empty();
		}
	};

	[[nodiscard]] inline CanonicalLandscapeSelectors CanonicalizeLandscapeSelectors(
		std::string_view a_form,
		std::string_view a_editorID,
		std::string_view a_diffuse)
	{
		const auto trimmedDiffuse = TrimAsciiSpaces(a_diffuse);
		return {
			.form = std::string(TrimAsciiSpaces(a_form)),
			.editorID = std::string(TrimAsciiSpaces(a_editorID)),
			.diffuse = NormalizeTexturePath(trimmedDiffuse),
			.diffuseWasSupplied = !trimmedDiffuse.empty(),
		};
	}
}
