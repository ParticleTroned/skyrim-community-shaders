#pragma once

#include "ContentHash.h"

#include <d3dcommon.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Util::ShaderDefines
{
	namespace detail
	{
		template <typename Range>
		auto Canonicalize(Range& a_defines)
		{
			const auto end = std::ranges::find_if(a_defines, [](const auto& define) { return !define.Name; });
			std::stable_sort(a_defines.begin(), end, [](const auto& left, const auto& right) {
				return std::strcmp(left.Name, right.Name) < 0;
			});
			// Conflicting values retain compiler order; identical neighbors are redundant.
			return std::unique(a_defines.begin(), end, [](const auto& left, const auto& right) {
				return std::strcmp(left.Name, right.Name) == 0 &&
				       std::strcmp(left.Definition ? left.Definition : "", right.Definition ? right.Definition : "") == 0;
			});
		}
		/** Encode boundaries so macro values cannot alias another macro list. */
		template <typename Range>
		std::string SerializeOwned(Range defines)
		{
			const auto end = Canonicalize(defines);
			std::string result;
			for (auto define = defines.begin(); define != end; ++define) {
				for (std::string_view text : { std::string_view(define->Name), std::string_view(define->Definition ? define->Definition : "") }) {
					result += std::to_string(text.size());
					result += ':';
					result += text;
				}
			}
			return result;
		}
	}

	/** Serialize diagnostic text without changing the compiler's captured input. */
	template <typename Range>
	inline std::string MergeDefinesString(Range a_defines, bool a_sort = false)
	{
		const auto end = a_sort ? detail::Canonicalize(a_defines) :
		                          std::ranges::find_if(a_defines, [](const auto& define) { return !define.Name; });
		std::string result;
		for (auto define = a_defines.begin(); define != end; ++define) {
			result += define->Name;
			if (define->Definition && !std::string_view(define->Definition).empty()) {
				result += '=';
				result += define->Definition;
			}
			result += ' ';
		}
		return result;
	}

	/** Return an unambiguous identity without mutating the compiler's macros. */
	template <size_t Size>
	inline std::string Serialize(const std::array<D3D_SHADER_MACRO, Size>& a_defines)
	{
		return detail::SerializeOwned(a_defines);
	}

	/** Return an unambiguous identity for a dynamically sized macro list. */
	inline std::string Serialize(std::span<const D3D_SHADER_MACRO> a_defines)
	{
		return detail::SerializeOwned(std::vector<D3D_SHADER_MACRO>(a_defines.begin(), a_defines.end()));
	}

	/** Bind cache validity to the actual stage, feature and descriptor macros. */
	inline ContentHash::Hash128 CompileStateDigest(
		const ContentHash::Hash128& a_globalDigest,
		std::span<const D3D_SHADER_MACRO> a_defines)
	{
		return ContentHash::CombineHashes(a_globalDigest, ContentHash::HashString(Serialize(a_defines)));
	}
}
