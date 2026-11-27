#pragma once

#include "Presets/LookScenes.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace LookSceneAllocationTests
{
	thread_local int largeAllocationsUntilFailure = -1;
}

void* operator new(std::size_t bytes)
{
	if (bytes >= 4096 && LookSceneAllocationTests::largeAllocationsUntilFailure >= 0) {
		if (LookSceneAllocationTests::largeAllocationsUntilFailure-- == 0) {
			LookSceneAllocationTests::largeAllocationsUntilFailure = -1;
			throw std::bad_alloc();
		}
	}
	if (auto* allocation = std::malloc(bytes == 0 ? 1 : bytes))
		return allocation;
	throw std::bad_alloc();
}
void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }

namespace LookSceneJsonTests
{
	using Presets::Json;
	namespace Reference
	{
		void OverlayScene(Json& destination, const Json& patch)
		{
			auto flat = destination.flatten();
			flat.erase("");
			const auto updates = patch.flatten();
			for (const auto& [path, value] : updates.items())
				if (!path.empty() && !value.is_null())
					flat[path] = value;
			destination = flat.unflatten();
		}

		Json BlendSceneLayers(const Json& base, const Json& first, const Json& second, float weight)
		{
			const auto one = first.flatten(), two = second.flatten(), fallback = base.flatten();
			std::set<std::string> paths;
			for (const auto* values : { &one, &two })
				for (const auto& [path, value] : values->items())
					if (!path.empty() && !value.is_null())
						paths.insert(path);
			Json result = Json::object();
			weight = std::clamp(weight, 0.f, 1.f);
			for (const auto& path : paths) {
				if (!fallback.contains(path))
					continue;
				const auto& a = one.contains(path) ? one[path] : fallback[path];
				const auto& b = two.contains(path) ? two[path] : fallback[path];
				if (a.is_number() && b.is_number() && (a.is_number_float() || b.is_number_float()))
					result[path] = std::lerp(a.get<double>(), b.get<double>(), static_cast<double>(weight));
				else
					result[path] = weight < .5f ? a : b;
			}
			return result.unflatten();
		}
	}
	bool Exact(const Json& a, const Json& b)
	{
		if (a.type() != b.type())
			return false;
		if (a.is_discarded())
			return true;
		if (a.is_number_float()) {
			const auto x = a.get<double>(), y = b.get<double>();
			return (std::isnan(x) && std::isnan(y)) || std::bit_cast<uint64_t>(x) == std::bit_cast<uint64_t>(y);
		}
		if (a.is_structured()) {
			if (a.size() != b.size())
				return false;
			for (auto i = a.begin(), j = b.begin(); i != a.end(); ++i, ++j) {
				if (a.is_object() && i.key() != j.key())
					return false;
				if (!Exact(*i, *j))
					return false;
			}
			return true;
		}
		return a == b;
	}
	struct Outcome
	{
		Json value, after;
		std::string error;
		int exceptionId = 0;
	};
	template <class Fn>
	Outcome Invoke(Fn&& fn, Json input = Json{})
	{
		Outcome result;
		result.after = std::move(input);
		try {
			result.value = fn(result.after);
		} catch (const Json::exception& e) {
			result.error = e.what();
			result.exceptionId = e.id;
		} catch (const std::exception& e) {
			result.error = e.what();
			result.exceptionId = -1;
		}
		return result;
	}
	bool Same(const Outcome& a, const Outcome& b)
	{
		return a.error == b.error && a.exceptionId == b.exceptionId && Exact(a.value, b.value) && Exact(a.after, b.after);
	}
	size_t checks = 0, exceptionCases = 0;
	void Check(const Outcome& ref, const Outcome& got, const Json& details)
	{
		++checks;
		exceptionCases += ref.exceptionId != 0;
		if (!Same(ref, got)) {
			Json report = { { "details", details }, { "baseline_value", ref.value }, { "candidate_value", got.value }, { "baseline_after", ref.after }, { "candidate_after", got.after }, { "baseline_error", ref.error }, { "candidate_error", got.error } };
			std::cerr << report.dump(2) << '\n';
			throw std::runtime_error("differential mismatch");
		}
	}

	void CheckAllocationFailure()
	{
		const Json original{ { "a", "old" }, { "empty", Json::array() }, { "z", 1 } };
		const Json patch{ { "a", std::string(8192, 'a') }, { "z", std::string(8192, 'z') } };
		Json expected = original;
		Reference::OverlayScene(expected, patch);
		for (int allocation = 0; allocation < 32; ++allocation) {
			auto destination = original;
			LookSceneAllocationTests::largeAllocationsUntilFailure = allocation;
			bool failed = false;
			try {
				Presets::OverlayScene(destination, patch);
			} catch (const std::bad_alloc&) {
				failed = true;
			}
			LookSceneAllocationTests::largeAllocationsUntilFailure = -1;
			if (!Exact(destination, failed ? original : expected))
				throw std::runtime_error("overlay string allocation failure partially modified its destination");
			if (!failed)
				return;
		}
		throw std::runtime_error("overlay allocation sweep never reached success");
	}

	void Run()
	{
		CheckAllocationFailure();
		std::vector<Json> values{ Json(Json::value_t::discarded), Json::binary({ 0, 1, 255 }),
			{ { "a", Json(Json::value_t::discarded) } }, { { "a", Json::binary({ 0, 255 }) } },
			{ { "a", std::numeric_limits<double>::quiet_NaN() } }, nullptr, 0, 1u, 1.0, false, "x", Json::object(), Json::array(), Json::array({ nullptr, 1.0, Json::object(), Json::array() }),
			{ { "a", 1.0 } }, { { "a", nullptr } }, { { "a", Json::object() } }, { { "a", Json::array() } }, { { "a", { { "b", 2.0 } } } },
			{ { "a", Json::array({ 1.0, 2.0, 3.0 }) } }, { { "0", 1.0 } }, { { "0", 1.0 }, { "x", 2.0 } }, { { "/", 1.0 }, { "0", 2.0 } },
			{ { "a/b", { { "~", 3.0 }, { "", 1.0 } } } }, { { "a", { { "0", 4.0 }, { "1", 5.0 } } } }, { { "a", { { "01", 4.0 } } } },
			{ { "a", { { "", 4.0 } } } }, { { "a", { { "-", 4.0 } } } }, { { "a", Json::array({ 1.0 }) } }, { { "a", 2 } }, { { "a", false } }, { { "a", "s" } },
			{ { "a", Json::array({ nullptr, 2.0 }) } }, { { "a", { { "b", 1.0 }, { "empty", Json::array() } } } } };

		const std::array weights{ -1.f, 0.f, .25f, .499f, .5f, .75f, 1.f, 2.f, std::numeric_limits<float>::quiet_NaN() };
		for (const auto& base : values) {
			for (const auto& patch : values) {
				const Json details = { { "base", base }, { "patch", patch } };
				const auto overlay = Invoke([&](Json& value) { Reference::OverlayScene(value, patch); return value; }, base);
				Check(overlay, Invoke([&](Json& value) { Presets::OverlayScene(value, patch); return value; }, base), details);
				for (const auto weight : weights) {
					const auto blend = Invoke([&](Json&) { return Reference::BlendSceneLayers(base, patch, Json::object(), weight); });
					Check(blend, Invoke([&](Json&) { return Presets::BlendSceneLayers(base, patch, Json::object(), weight); }), details);
				}
			}
			const auto alias = Invoke([](Json& value) { Reference::OverlayScene(value, value); return value; }, base);
			Check(alias, Invoke([](Json& value) { Presets::OverlayScene(value, value); return value; }, base), { { "alias", base } });
		}
	}
}
