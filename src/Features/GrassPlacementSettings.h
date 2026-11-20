#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace GrassPlacement
{
	inline constexpr float kMaximumOffset = 32.0f;
	inline constexpr size_t kMaximumModels = 128;

	/** Placement in game units; model corrections add to the shared offset. */
	struct Settings
	{
		float verticalOffset = 0;
		std::map<std::string, float, std::less<>> modelOffsets;
		bool operator==(const Settings&) const = default;
	};

	inline float Clamp(float value)
	{
		return std::isfinite(value) ? std::clamp(value, -kMaximumOffset, kMaximumOffset) : 0;
	}

	/** Canonical mesh-relative identity; rejects absolute paths and traversal. */
	inline std::string ModelKey(std::string_view path)
	{
		if (path.empty() || path.size() > 260)
			return {};
		std::string result(path);
		for (auto& c : result) {
			if (static_cast<unsigned char>(c) < 32 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
				return {};
			if (c == '/')
				c = '\\';
			if (c >= 'A' && c <= 'Z')
				c += 'a' - 'A';
		}
		if (result.starts_with("meshes\\"))
			result.erase(0, 7);
		if (result.empty() || result.front() == '\\' || !result.ends_with(".nif"))
			return {};
		for (size_t start = 0; start < result.size();) {
			const auto end = result.find('\\', start);
			const auto part = result.substr(start, end == std::string::npos ? end : end - start);
			if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
				return {};
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		return result;
	}

	inline bool Active(const Settings& value)
	{
		return value.verticalOffset != 0 || std::any_of(value.modelOffsets.begin(), value.modelOffsets.end(),
												[](const auto& entry) { return entry.second != 0; });
	}

	struct History
	{
		uint32_t frame = UINT32_MAX;
		float current = 0, previous = 0;
		/** Advance once per rendered frame; discard temporal history after a gap. */
		void Update(uint32_t nextFrame, float target)
		{
			if (frame == nextFrame)
				return;
			target = Clamp(target);
			previous = frame != UINT32_MAX && nextFrame - frame == 1 ? current : target;
			current = target;
			frame = nextFrame;
		}
	};

	inline void to_json(nlohmann::json& json, const Settings& value)
	{
		json = { { "verticalOffset", value.verticalOffset }, { "modelOffsets", value.modelOffsets } };
	}

	/** Invalid saved entries are neutral; interactive mutations validate before replacing settings. */
	inline void from_json(const nlohmann::json& json, Settings& value)
	{
		value = {};
		if (!json.is_object())
			return;
		if (const auto offset = json.find("verticalOffset"); offset != json.end() && offset->is_number())
			value.verticalOffset = Clamp(offset->get<float>());
		if (const auto models = json.find("modelOffsets"); models != json.end() && models->is_object())
			for (const auto& [path, offset] : models->items()) {
				const auto key = ModelKey(path);
				if (!key.empty() && offset.is_number() && value.modelOffsets.size() < kMaximumModels)
					value.modelOffsets[key] = Clamp(offset.get<float>());
			}
	}

	inline bool Validate(const nlohmann::json& patch, std::string& error)
	{
		if (!patch.is_object() || patch.empty()) {
			error = "grassPlacement must be a nonempty object";
			return false;
		}
		const auto validOffset = [](const auto& value) {
			return value.is_number() && std::isfinite(value.template get<double>()) &&
			       std::abs(value.template get<double>()) <= kMaximumOffset;
		};
		for (const auto& [key, value] : patch.items()) {
			if (key == "verticalOffset" && validOffset(value))
				continue;
			if (key == "modelOffsets" && value.is_object() && value.size() <= kMaximumModels) {
				std::map<std::string, bool> seen;
				bool valid = true;
				for (const auto& [path, offset] : value.items()) {
					const auto canonical = ModelKey(path);
					valid &= !canonical.empty() && validOffset(offset) && seen.emplace(canonical, true).second;
				}
				if (valid)
					continue;
			}
			error = "Expected offsets in [-32,32] and at most 128 unique mesh-relative .nif paths";
			return false;
		}
		return true;
	}
}
