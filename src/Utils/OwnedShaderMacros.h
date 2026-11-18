#pragma once

#include <d3dcommon.h>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Util
{
	/** Owns macro strings across queued stages, preserving null definitions. */
	struct OwnedShaderMacros
	{
		std::vector<std::pair<std::string, std::optional<std::string>>> values;

		void Capture(std::span<const D3D_SHADER_MACRO> macros)
		{
			values.clear();
			for (const auto& macro : macros) {
				if (!macro.Name)
					break;
				values.emplace_back(macro.Name, macro.Definition ? std::optional<std::string>(macro.Definition) : std::nullopt);
			}
		}

		std::vector<D3D_SHADER_MACRO> View() const
		{
			std::vector<D3D_SHADER_MACRO> macros;
			macros.reserve(values.size() + 1);
			for (const auto& [name, definition] : values)
				macros.push_back({ name.c_str(), definition ? definition->c_str() : nullptr });
			macros.push_back({ nullptr, nullptr });
			return macros;
		}
	};
}
