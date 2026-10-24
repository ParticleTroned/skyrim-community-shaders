#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CSX::MeshBlendingPolicy
{
	inline constexpr std::uint32_t EnableMask = 1u << 23;

	/** Clear the Mesh-owned bit while preserving other shader descriptors. */
	inline constexpr void ClearDrawDescriptor(std::uint32_t& shader) noexcept
	{
		shader &= ~EnableMask;
	}

	[[nodiscard]] constexpr char LowerAscii(char a_character) noexcept
	{
		return a_character >= 'A' && a_character <= 'Z' ?
		           static_cast<char>(a_character - 'A' + 'a') :
		           a_character;
	}

	namespace detail
	{
		[[nodiscard]] constexpr bool IsSlash(char a_character) noexcept
		{
			return a_character == '/' || a_character == '\\';
		}

		[[nodiscard]] constexpr bool EqualsFolded(std::string_view a_value, std::string_view a_lowercase) noexcept
		{
			if (a_value.size() != a_lowercase.size())
				return false;
			for (std::size_t index = 0u; index < a_value.size(); ++index) {
				if (LowerAscii(a_value[index]) != a_lowercase[index])
					return false;
			}
			return true;
		}

		[[nodiscard]] constexpr bool HasRoot(std::string_view a_value, std::string_view a_root) noexcept
		{
			return a_value.size() > a_root.size() && IsSlash(a_value[a_root.size()]) &&
			       EqualsFolded(a_value.substr(0u, a_root.size()), a_root);
		}

		template <class Emit>
		bool VisitNormalizedPath(std::string_view a_path, std::string_view a_root, Emit&& a_emit)
		{
			while (a_path.size() >= 2u && a_path.front() == '.' && IsSlash(a_path[1u])) {
				a_path.remove_prefix(2u);
				while (!a_path.empty() && IsSlash(a_path.front()))
					a_path.remove_prefix(1u);
			}
			while (!a_path.empty() && IsSlash(a_path.front()))
				a_path.remove_prefix(1u);
			while (!a_path.empty() && IsSlash(a_path.back()))
				a_path.remove_suffix(1u);
			if (!a_root.empty()) {
				if (EqualsFolded(a_path, "data") || EqualsFolded(a_path, a_root))
					return true;
				if (HasRoot(a_path, "data")) {
					a_path.remove_prefix(5u);
					while (!a_path.empty() && IsSlash(a_path.front()))
						a_path.remove_prefix(1u);
				}
				if (EqualsFolded(a_path, a_root))
					return true;
				if (!a_path.empty() && !HasRoot(a_path, a_root)) {
					for (const char character : a_root) {
						if (!a_emit(character))
							return false;
					}
					if (!a_emit('/'))
						return false;
				}
			}
			bool previousSlash = false;
			for (const char value : a_path) {
				const bool slash = IsSlash(value);
				if ((!slash || !previousSlash) && !a_emit(slash ? '/' : LowerAscii(value)))
					return false;
				previousSlash = slash;
			}
			return true;
		}
	}

	[[nodiscard]] inline std::string NormalizePath(std::string_view a_path, bool a_isModelPath)
	{
		std::string normalized;
		normalized.reserve(a_path.size() + (a_isModelPath ? 7u : 0u));
		detail::VisitNormalizedPath(a_path, a_isModelPath ? "meshes" : "", [&](char a_character) {
			normalized.push_back(a_character);
			return true;
		});
		return normalized;
	}

	/** Compares the complete normalized model identity without allocating. */
	[[nodiscard]] inline bool MatchesNormalizedModelPath(std::string_view a_current, std::string_view a_normalized)
	{
		std::size_t offset = 0u;
		return detail::VisitNormalizedPath(a_current, "meshes", [&](char a_character) {
			return offset < a_normalized.size() && a_normalized[offset++] == a_character;
		}) && offset == a_normalized.size();
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

	template <class Emit>
	bool VisitNodeCacheIdentity(std::span<const NodePathPart> a_path, Emit&& a_emit)
	{
		if (a_path.empty())
			return false;
		for (const char character : std::string_view("node-v1:")) {
			if (!a_emit(character))
				return false;
		}
		const auto appendNumber = [&](std::uint64_t value) {
			std::array<char, 24> number{};
			const auto conversion = std::to_chars(number.data(), number.data() + number.size(), value);
			if (conversion.ec != std::errc{})
				return false;
			for (auto* character = number.data(); character != conversion.ptr; ++character) {
				if (!a_emit(*character))
					return false;
			}
			return a_emit(':');
		};
		if (!appendNumber(a_path.size()))
			return false;
		for (std::size_t index = 0u; index < a_path.size(); ++index) {
			const auto& part = a_path[index];
			if (!appendNumber(part.name.size()))
				return false;
			for (const char character : part.name) {
				if (!a_emit(LowerAscii(character)))
					return false;
			}
			if (!appendNumber(index == 0u ? 0u : part.parentIndex))
				return false;
		}
		return true;
	}

	/** Frame complete case-folded names and indices without treating names as syntax. */
	[[nodiscard]] inline std::string BuildNodeCacheIdentity(std::span<const NodePathPart> a_path)
	{
		std::string result;
		if (!VisitNodeCacheIdentity(a_path, [&](char a_character) {
				result.push_back(a_character);
				return true;
			}))
			return {};
		return result;
	}

	/** Current names and indices are compared as bytes, never by pointer or hash. */
	[[nodiscard]] inline bool MatchesRuleIdentity(
		std::string_view a_currentModel,
		std::span<const NodePathPart> a_currentNodes,
		const RuleIdentity& a_cached)
	{
		if (!MatchesNormalizedModelPath(a_currentModel, a_cached.model))
			return false;
		std::size_t offset = 0u;
		return VisitNodeCacheIdentity(a_currentNodes, [&](char a_character) {
			return offset < a_cached.nodeIdentity.size() && a_cached.nodeIdentity[offset++] == a_character;
		}) && offset == a_cached.nodeIdentity.size();
	}

	/** An unavailable node selector cannot satisfy even a wildcard node rule. */
	[[nodiscard]] constexpr bool CanMatchNodeSelector(
		std::string_view a_rule, std::string_view a_current) noexcept
	{
		return a_rule.empty() || !a_current.empty();
	}

	[[nodiscard]] inline bool WildcardMatch(std::string_view a_pattern, std::string_view a_value)
	{
		std::size_t patternIndex = 0u;
		std::size_t valueIndex = 0u;
		std::size_t starIndex = std::string_view::npos;
		std::size_t retryValueIndex = 0u;
		while (valueIndex < a_value.size()) {
			if (patternIndex < a_pattern.size() &&
				(a_pattern[patternIndex] == '?' || a_pattern[patternIndex] == a_value[valueIndex])) {
				++patternIndex;
				++valueIndex;
			} else if (patternIndex < a_pattern.size() && a_pattern[patternIndex] == '*') {
				starIndex = patternIndex++;
				retryValueIndex = valueIndex;
			} else if (starIndex != std::string_view::npos) {
				patternIndex = starIndex + 1u;
				valueIndex = ++retryValueIndex;
			} else {
				return false;
			}
		}
		while (patternIndex < a_pattern.size() && a_pattern[patternIndex] == '*')
			++patternIndex;
		return patternIndex == a_pattern.size();
	}

	struct ExactRuleLess
	{
		using is_transparent = void;

		template <class Left, class Right>
		bool operator()(const Left& a_left, const Right& a_right) const
		{
			return a_left.first != a_right.first ? a_left.first < a_right.first : a_left.second < a_right.second;
		}
	};
	using ExactRuleSet = std::set<std::pair<std::string, std::string>, ExactRuleLess>;

	struct CompiledRule
	{
		std::string model;
		std::string nodePath;
		bool modelHasWildcard = false;
		bool nodeHasWildcard = false;
	};

	struct CompiledRuleSet
	{
		std::vector<CompiledRule> flexible;
		ExactRuleSet exact;

		void Add(std::string_view a_model, std::string_view a_nodePath)
		{
			CompiledRule rule{ NormalizePath(a_model, true), NormalizePath(a_nodePath, false) };
			if ((!a_model.empty() && rule.model.empty()) || (!a_nodePath.empty() && rule.nodePath.empty()) ||
				(rule.model.empty() && rule.nodePath.empty()))
				return;
			rule.modelHasWildcard = HasWildcard(rule.model);
			rule.nodeHasWildcard = HasWildcard(rule.nodePath);
			if (!rule.model.empty() && !rule.nodePath.empty() && !rule.modelHasWildcard && !rule.nodeHasWildcard)
				exact.emplace(std::move(rule.model), std::move(rule.nodePath));
			else
				flexible.push_back(std::move(rule));
		}

		[[nodiscard]] std::size_t Size() const noexcept { return flexible.size() + exact.size(); }

		[[nodiscard]] bool Matches(std::string_view a_model, std::string_view a_nodePath) const
		{
			if (!a_model.empty() && !a_nodePath.empty() && exact.contains(std::pair{ a_model, a_nodePath }))
				return true;
			for (const auto& rule : flexible) {
				if (!CanMatchNodeSelector(rule.nodePath, a_nodePath))
					continue;
				const bool modelMatches = rule.model.empty() ||
				                          (rule.modelHasWildcard ? WildcardMatch(rule.model, a_model) : rule.model == a_model);
				const bool nodeMatches = rule.nodePath.empty() ||
				                         (rule.nodeHasWildcard ? WildcardMatch(rule.nodePath, a_nodePath) : rule.nodePath == a_nodePath);
				if (modelMatches && nodeMatches)
					return true;
			}
			return false;
		}
	};

	enum class RuleMatch : std::uint8_t
	{
		kNone,
		kDeny,
		kAuthored,
		kDetected
	};

	/** Generated exact observations retain the receiver checks used to discover them. */
	struct CompiledRules
	{
		CompiledRuleSet authored;
		CompiledRuleSet denied;
		ExactRuleSet detected;
		std::set<std::string, std::less<>> exactModels;
		bool flexibleNeedsNodePath = false;

		void AddDetected(std::string_view a_model, std::string_view a_nodePath)
		{
			auto model = NormalizePath(a_model, true);
			auto node = NormalizePath(a_nodePath, false);
			if (!model.empty() && !node.empty() && !HasWildcard(model) && !HasWildcard(node))
				detected.emplace(std::move(model), std::move(node));
		}

		void BuildNodeIndex()
		{
			exactModels.clear();
			flexibleNeedsNodePath = false;
			for (const auto* rules : { &authored, &denied }) {
				for (const auto& [model, node] : rules->exact) {
					(void)node;
					exactModels.insert(model);
				}
				for (const auto& rule : rules->flexible)
					flexibleNeedsNodePath |= !rule.nodePath.empty();
			}
			for (const auto& [model, node] : detected) {
				(void)node;
				exactModels.insert(model);
			}
		}

		[[nodiscard]] bool HasAllowRules() const noexcept { return authored.Size() != 0u || !detected.empty(); }
		[[nodiscard]] bool HasAnyRules() const noexcept { return HasAllowRules() || denied.Size() != 0u; }
		[[nodiscard]] bool NeedsNodePath(std::string_view a_model) const
		{
			return flexibleNeedsNodePath || exactModels.contains(a_model);
		}
		[[nodiscard]] RuleMatch Match(std::string_view a_model, std::string_view a_nodePath) const
		{
			if (denied.Matches(a_model, a_nodePath))
				return RuleMatch::kDeny;
			if (authored.Matches(a_model, a_nodePath))
				return RuleMatch::kAuthored;
			if (!a_model.empty() && !a_nodePath.empty() && detected.contains(std::pair{ a_model, a_nodePath }))
				return RuleMatch::kDetected;
			return RuleMatch::kNone;
		}
	};

	enum class CachedClassification : std::uint8_t
	{
		kRejected,
		kAllowedByRule,
		kDetectedByRule,
		kAutomatic,
	};

	[[nodiscard]] constexpr bool RequiresReceiverValidation(CachedClassification a_classification) noexcept
	{
		return a_classification == CachedClassification::kAutomatic ||
		       a_classification == CachedClassification::kDetectedByRule;
	}

	[[nodiscard]] constexpr bool CanReuseCacheHit(
		CachedClassification a_classification,
		bool a_rootHasAnimation,
		bool a_receiverIsCurrentAndSafe,
		bool a_ruleIdentityIsCurrent) noexcept
	{
		if (a_classification == CachedClassification::kRejected)
			return true;
		if (a_rootHasAnimation || !a_ruleIdentityIsCurrent)
			return false;
		return !RequiresReceiverValidation(a_classification) || a_receiverIsCurrentAndSafe;
	}

	/** Bound inherited ownership lookup even when parent links contain a cycle. */
	template <class Object, class OwnUserData, class LinkedParent>
	[[nodiscard]] auto ResolveBoundedOwner(
		Object* a_object,
		std::size_t a_maximumDepth,
		OwnUserData&& a_ownUserData,
		LinkedParent&& a_linkedParent) -> decltype(a_ownUserData(a_object))
	{
		for (std::size_t depth = 0u; a_object && depth < a_maximumDepth; ++depth) {
			if (auto* owner = a_ownUserData(a_object))
				return owner;
			if (depth + 1u == a_maximumDepth)
				return nullptr;
			a_object = a_linkedParent(a_object);
		}
		return nullptr;
	}

	/** Only eligible live root children are visited; the cached address is never dereferenced. */
	template <class Object, class ResolveChild, class IsEligible>
	[[nodiscard]] Object* ResolveLiveRoute(
		Object* a_root,
		std::span<const std::uint32_t> a_indices,
		const Object* a_expected,
		ResolveChild&& a_resolveChild,
		IsEligible&& a_isEligible)
	{
		auto* current = a_root;
		for (const auto index : a_indices) {
			if (!current || !a_isEligible(current))
				return nullptr;
			current = a_resolveChild(current, index);
		}
		return current && a_isEligible(current) && current == a_expected ? current : nullptr;
	}

	template <class Object, class ResolveChild>
	[[nodiscard]] Object* ResolveLiveRoute(
		Object* a_root,
		std::span<const std::uint32_t> a_indices,
		const Object* a_expected,
		ResolveChild&& a_resolveChild)
	{
		return ResolveLiveRoute(a_root, a_indices, a_expected,
			std::forward<ResolveChild>(a_resolveChild), [](const Object*) { return true; });
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

}
