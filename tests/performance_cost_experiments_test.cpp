#ifdef NDEBUG
#	undef NDEBUG
#endif
#include "Diagnostics/PerformanceCostExperiments.h"
#include <atomic>
#include <cassert>
#include <memory>
#include <pystring/pystring.h>
#include <string>
#include <utility>
#include <vector>

namespace logger
{
	template <class... T>
	void warn(const char*, T&&...)
	{}
	template <class... T>
	void debug(const char*, T&&...)
	{}
}

struct State
{
	struct ShaderDefinesSnapshot
	{
		std::string canonicalText, authoredText;
		std::vector<std::pair<std::string, std::string>> defines;
	};
	std::atomic<std::shared_ptr<const ShaderDefinesSnapshot>> shaderDefinesSnapshot;
	std::atomic_uint64_t shaderDefinesGeneration{ 0 };
	void SetDefines(std::string a_defines);
};
#include "performance_cost_defines_under_test.h"

int main()
{
	using namespace CSX::Diagnostics::PerformanceCosts;
	State renderer;
	const std::string authored = "FOO=1;BAR";
	renderer.SetDefines("FOO=1;REVERSE_Z;CSX_PERF_REFERENCE_FLAGS=63;CSX_DEPTH_ORDER_TEST_USER;BAR");
	auto snapshot = renderer.shaderDefinesSnapshot.load();
	assert(snapshot->canonicalText == authored && snapshot->authoredText == authored && snapshot->defines.size() == 2);
#ifdef DEVBENCH_BRIDGE_ENABLED
	Selection state;
	assert(!state.Pending() && state.Active() == 0);
	const auto requested = ParseSettings(0, { { "snowMeltFireReuse", false }, { "reverseZBindings", false } });
	state.Request(requested);
	assert(state.Pending() && state.Active() == 0 && state.Requested() == 65);
	assert(state.Apply() && !state.Pending() && state.Active() == 65);
	assert(!state.Apply());
	state.Request(ParseSettings(state.Requested(), { { "snowMeltFireReuse", true } }));
	assert(state.Apply() && state.Active() == 64);
	state.Request(0);
	assert(state.Apply() && state.Active() == 0);
	for (const auto& invalid : { nlohmann::json{}, nlohmann::json::object(), nlohmann::json{ { "unknown", false } },
			 nlohmann::json{ { "snowMeltFireReuse", false }, { "grassTileFilter", 1 } } }) {
		bool rejected = false;
		try {
			state.Request(ParseSettings(state.Requested(), invalid));
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		assert(rejected && state.Requested() == 0 && state.Active() == 0);
	}
	bool rejected = false;
	try {
		state.Request(128);
	} catch (const std::invalid_argument&) {
		rejected = true;
	}
	assert(rejected && !state.Pending());
	const auto schema = SettingsSchema();
	for (const auto& entry : Entries) {
		assert(schema.at("properties").contains(entry.name));
		const auto bit = static_cast<uint32_t>(entry.point);
		assert(ParseSettings(0, { { entry.name, false } }) == bit);
		assert(ParseSettings(AllPoints, { { entry.name, true } }) == (AllPoints & ~bit));
	}
	selection.Request(CSX_PERF_POINT_LIGHT_COMPOSITION);
	selection.Apply();
	renderer.SetDefines(authored);
	snapshot = renderer.shaderDefinesSnapshot.load();
	assert(snapshot->canonicalText == authored + ";CSX_PERF_REFERENCE_FLAGS=16");
	assert(snapshot->authoredText == authored && snapshot->defines.back().first == "CSX_PERF_REFERENCE_FLAGS");
	selection.Request(CSX_PERF_REVERSE_Z_BINDINGS);
	selection.Apply();
	renderer.SetDefines(snapshot->authoredText);
	snapshot = renderer.shaderDefinesSnapshot.load();
	assert(snapshot->canonicalText == authored && snapshot->authoredText == authored && snapshot->defines.size() == 2);
	selection.Request(0);
	selection.Apply();
	renderer.SetDefines(authored);
	assert(renderer.shaderDefinesSnapshot.load()->canonicalText == authored);
#else
	static_assert(!ReferenceEnabled(Point::SnowMeltFireReuse));
	static_assert(!ReferenceEnabled(Point::GrassTileFilter));
	static_assert(!ReferenceEnabled(Point::FurContactReject));
	static_assert(!ReferenceEnabled(Point::SnowWeatherRange));
	static_assert(!ReferenceEnabled(Point::PointLightComposition));
	static_assert(!ReferenceEnabled(Point::NeuralPreparation));
	static_assert(!ReferenceEnabled(Point::ReverseZBindings));
#endif
}
