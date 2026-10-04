#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace CSX::MeshBlendingPolicy
{
	[[nodiscard]] constexpr char LowerAscii(char a_character) noexcept
	{
		return a_character >= 'A' && a_character <= 'Z' ?
		           static_cast<char>(a_character - 'A' + 'a') :
		           a_character;
	}

	[[nodiscard]] inline std::string NormalizePath(
		std::string_view a_path,
		bool a_isModelPath)
	{
		std::string normalized;
		normalized.reserve(a_path.size() + (a_isModelPath ? 7u : 0u));
		bool previousSlash = false;
		for (char value : a_path) {
			const char character = LowerAscii(value == '\\' ? '/' : value);
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

	struct RuleIdentity
	{
		std::string model;
		std::string nodeIdentity;

		bool operator==(const RuleIdentity&) const = default;
	};

	struct NodePathPart
	{
		std::string_view name;
		std::uint32_t parentIndex;
	};

	/** Retain ordinary rule syntax; ambiguous raw names have no node selector. */
	[[nodiscard]] inline std::string BuildCanonicalNodePath(std::span<const NodePathPart> a_path)
	{
		std::string result;
		result.reserve(a_path.size() * 24u);
		for (std::size_t index = 0u; index < a_path.size(); ++index) {
			const auto& part = a_path[index];
			if (part.name.find_first_of("/\\#[]*?") != std::string_view::npos ||
				part.name == "." || part.name == "..")
				return {};
			if (index != 0u)
				result.push_back('/');
			result.append(part.name.empty() ? "#" : part.name);
			if (index != 0u) {
				result.push_back('[');
				std::array<char, 16> number{};
				const auto conversion = std::to_chars(number.data(), number.data() + number.size(), part.parentIndex);
				if (conversion.ec != std::errc{})
					return {};
				result.append(number.data(), conversion.ptr);
				result.push_back(']');
			}
		}
		return NormalizePath(result, false);
	}

	/** Frame complete case-folded names and indices without treating names as syntax. */
	[[nodiscard]] inline std::string BuildNodeCacheIdentity(std::span<const NodePathPart> a_path)
	{
		if (a_path.empty())
			return {};
		std::string result = "node-v1:";
		const auto appendNumber = [&](std::uint64_t value) {
			std::array<char, 24> number{};
			const auto conversion = std::to_chars(number.data(), number.data() + number.size(), value);
			if (conversion.ec != std::errc{})
				return false;
			result.append(number.data(), conversion.ptr);
			result.push_back(':');
			return true;
		};
		if (!appendNumber(a_path.size()))
			return {};
		for (std::size_t index = 0u; index < a_path.size(); ++index) {
			const auto& part = a_path[index];
			if (!appendNumber(part.name.size()))
				return {};
			for (const char character : part.name)
				result.push_back(LowerAscii(character));
			if (!appendNumber(index == 0u ? 0u : part.parentIndex))
				return {};
		}
		return result;
	}

	/** An unavailable node selector cannot satisfy even a wildcard node rule. */
	[[nodiscard]] constexpr bool CanMatchNodeSelector(
		std::string_view a_rule, std::string_view a_current) noexcept
	{
		return a_rule.empty() || !a_current.empty();
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
		bool a_automaticReceiverIsCurrentAndSafe,
		bool a_ruleIdentityIsCurrent) noexcept
	{
		if (a_classification == CachedClassification::kRejected)
			return true;
		if (a_rootHasAnimation || !a_ruleIdentityIsCurrent)
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
