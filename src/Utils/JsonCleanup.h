#pragma once

#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

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

	/** Copy a JSON tree without exposing partial containers to allocating destruction. */
	inline void CopyJsonInto(nlohmann::json& a_destination, const nlohmann::json& a_source)
	{
		using json = nlohmann::json;
		json copy;
		const JsonCleanupGuard cleanup(copy);
		struct Pending
		{
			const json* source;
			json* destination;
		};
		std::vector<Pending> pending{ { &a_source, &copy } };
		while (!pending.empty()) {
			const auto [source, destination] = pending.back();
			pending.pop_back();
			if (source->is_object()) {
				*destination = json::object();
				for (auto it = source->begin(); it != source->end(); ++it)
					pending.push_back({ &it.value(), &(*destination)[it.key()] });
			} else if (source->is_array()) {
				*destination = json::array();
				auto& array = destination->get_ref<json::array_t&>();
				array.resize(source->size());
				for (std::size_t i = 0; i < source->size(); ++i)
					pending.push_back({ &(*source)[i], &array[i] });
			} else {
				*destination = *source;
			}
		}
		a_destination.swap(copy);
	}

	/** Append only a complete guarded copy; allocation failure leaves the input owned. */
	inline void AppendJsonCopy(nlohmann::json& a_array, const nlohmann::json& a_source)
	{
		nlohmann::json copy;
		const JsonCleanupGuard cleanup(copy);
		CopyJsonInto(copy, a_source);
		a_array.push_back(std::move(copy));
	}

	/** Build an object incrementally before replacing its destination by swap. */
	template <class Populate>
	void PopulateJsonObject(nlohmann::json& a_destination, Populate&& a_populate)
	{
		nlohmann::json object;
		const JsonCleanupGuard cleanup(object);
		object = nlohmann::json::object();
		a_populate(object);
		a_destination.swap(object);
	}
}
