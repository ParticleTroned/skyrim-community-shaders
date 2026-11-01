#pragma once

#include <ankerl/unordered_dense.h>
#include <d3dcommon.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Util
{
	/** @brief Bounded diagnostic-key reuse with exact input validation; callers provide synchronization. */
	class ShaderTrackingKeyCache
	{
	public:
		static constexpr std::size_t kMaxEntries = 4096;

		/** @brief Return a key for these inputs; the reference remains valid until the next cache mutation. */
		template <class BuildKey>
		const std::string& GetOrCreate(std::uint64_t a_taskId, std::string_view a_source,
			std::span<const D3D_SHADER_MACRO> a_macros, BuildKey&& a_buildKey)
		{
			std::size_t count = 0;
			while (count < a_macros.size() && a_macros[count].Name)
				++count;
			a_macros = a_macros.first(count);

			auto found = entries.find(a_taskId);
			if (found != entries.end() && found->second.Matches(a_source, a_macros))
				return found->second.key;

			Entry replacement{ std::string(a_source), {}, std::forward<BuildKey>(a_buildKey)() };
			replacement.macros.reserve(count);
			for (const auto& macro : a_macros)
				replacement.macros.emplace_back(macro.Name, macro.Definition ? macro.Definition : "");
			if (found != entries.end()) {
				found->second = std::move(replacement);
				return found->second.key;
			}

			// Retire one oldest admission instead of rebuilding every key at the capacity limit.
			if (entries.size() == kMaxEntries)
				entries.erase(admissionOrder[nextAdmission]);
			const auto inserted = entries.emplace(a_taskId, std::move(replacement)).first;
			admissionOrder[nextAdmission] = a_taskId;
			nextAdmission = (nextAdmission + 1) % kMaxEntries;
			return inserted->second.key;
		}

		/** @brief Discard retained identities when the shader-define generation changes. */
		void Clear()
		{
			entries.clear();
			nextAdmission = 0;
		}

	private:
		struct Entry
		{
			std::string source;
			std::vector<std::pair<std::string, std::string>> macros;
			std::string key;

			bool Matches(std::string_view a_source, std::span<const D3D_SHADER_MACRO> a_macros) const
			{
				if (source != a_source || macros.size() != a_macros.size())
					return false;
				for (std::size_t i = 0; i < macros.size(); ++i) {
					if (macros[i].first != a_macros[i].Name ||
						macros[i].second != (a_macros[i].Definition ? a_macros[i].Definition : ""))
						return false;
				}
				return true;
			}
		};

		ankerl::unordered_dense::map<std::uint64_t, Entry> entries;
		std::array<std::uint64_t, kMaxEntries> admissionOrder{};
		std::size_t nextAdmission = 0;
	};
}
