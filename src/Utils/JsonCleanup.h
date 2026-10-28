#pragma once

#include <nlohmann/json.hpp>

namespace Util
{
	/** Empty containers before JSON destruction without allocating a traversal stack. */
	inline void ClearJsonWithoutAllocation(nlohmann::json& a_value) noexcept
	{
		while (a_value.is_structured() && !a_value.empty()) {
			auto* current = &a_value;
			nlohmann::json* parent = nullptr;
			while (current->is_structured() && !current->empty()) {
				parent = current;
				current = current->is_array() ? &current->get_ref<nlohmann::json::array_t&>().back() :
				                                &current->get_ref<nlohmann::json::object_t&>().begin()->second;
			}
			// Only scalar or empty-container destructors run while removing a leaf.
			if (parent->is_array())
				parent->get_ref<nlohmann::json::array_t&>().pop_back();
			else {
				auto& object = parent->get_ref<nlohmann::json::object_t&>();
				object.erase(object.begin());
			}
		}
		a_value.clear();
	}

	/** Keep an owned JSON value safe to destroy when allocation failure persists. */
	class JsonCleanupGuard
	{
	public:
		explicit JsonCleanupGuard(nlohmann::json& a_value) noexcept : value(a_value) {}
		~JsonCleanupGuard() noexcept { ClearJsonWithoutAllocation(value); }
		JsonCleanupGuard(const JsonCleanupGuard&) = delete;
		JsonCleanupGuard& operator=(const JsonCleanupGuard&) = delete;

	private:
		nlohmann::json& value;
	};
}
