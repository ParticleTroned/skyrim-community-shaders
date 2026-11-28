#include "Features/LightLimitFix/LightCountDiagnostics.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using Diagnostics = LightLimitFixDetail::LightCountDiagnostics;

template <class T>
concept HasCaptureAPI = requires(T& value) { value.RequestCapture(0); };
#ifdef DEVBENCH_BRIDGE_ENABLED
static_assert(HasCaptureAPI<Diagnostics>);
#else
static_assert(!HasCaptureAPI<Diagnostics>);
#endif

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

void TestPolicy()
{
	Diagnostics diagnostic;
	unsigned visits = 0;
	auto collect = [&] { ++visits; return Diagnostics::Counts{}; };
	diagnostic.Collect(0, collect);
	Require(visits == 0, "Closed diagnostics must not visit lights");
	diagnostic.Request(10);
	diagnostic.Collect(10, collect);
	diagnostic.Collect(10, collect);
	Require(visits == 1, "One request permits one update");
	diagnostic.Request(10);
	diagnostic.Collect(11, collect);
	Require(visits == 2, "UI requests may be consumed in the next frame");
	diagnostic.Request(10);
	diagnostic.Collect(12, collect);
	Require(visits == 2, "Expired requests must not visit lights");
	diagnostic.Request(UINT32_MAX);
	diagnostic.Collect(0, collect);
	Require(visits == 3 && diagnostic.Read(0).available, "Requests survive frame wrap");
	Require(!diagnostic.Read(2).available, "UI samples expire across frame wrap");
	diagnostic.Collect(2, collect);
	Require(visits == 3, "Reading samples must not renew collection");
	diagnostic.Request(20);
	diagnostic.Request(19);
	diagnostic.Collect(21, collect);
	Require(visits == 4, "An older concurrent request must not overwrite newer demand");
	Require(diagnostic.Read(21).available && diagnostic.Read(21).counts.pointLights == 0, "Zero lights is a valid sample");

	Diagnostics::Counts counts;
	counts.RecordPoint(false, 1, 1, 1);
	counts.RecordPoint(true, 1, 0.5f, 0.25f);
	counts.RecordPoint(false, 10, 9.95f, 10);
	counts.RecordPoint(false, 10, 9.8f, 10);
	counts.RecordPoint(false, 0, 0, 0);
	counts.RecordPoint(false, std::numeric_limits<float>::quiet_NaN(), 1, 0);
	counts.RecordPoint(false, std::numeric_limits<float>::infinity(), 1, 0);
	counts.RecordPoint(false, -1, 1, 0);
	Require(counts.pointLights == 8 && counts.shadowCastingPointLights == 1 && counts.colouredPointLights == 2,
		"Colour classification must tolerate neutral RGB and reject invalid values");
	diagnostic.Request(22);
	bool propagated = false;
	try {
		diagnostic.Collect(22, []() -> Diagnostics::Counts { throw std::runtime_error("collector failed"); });
	} catch (const std::runtime_error&) {
		propagated = true;
	}
	Require(propagated && !diagnostic.Read(22).available, "Collector failures propagate and invalidate UI results");
}

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/ServiceFoundation.h"
using json = nlohmann::json;

struct State
{
	std::atomic_uint32_t frameCountAtomic{ 20 };
	std::uint32_t frameCount = 20;
	bool disabled = false;
	bool IsFeatureDisabled(const char*) const { return disabled; }
	void Frame(std::uint32_t value)
	{
		frameCount = value;
		frameCountAtomic.store(value);
	}
};
namespace globals
{
	inline State instance;
	inline State* state = &instance;
}
struct LightLimitFix
{
	Diagnostics lightCountDiagnostics;
	bool loaded = true;
	std::uint64_t RequestLightCountDiagnostics();
	json GetLightCountDiagnostics() const;
	enum class LightFlags : unsigned
	{
		OmniDirectional = 1,
		Spot = 2,
		Particle = 4,
		Shadow = 8
	};
	struct Flags
	{
		unsigned bits;
		bool any(LightFlags flag) const { return (bits & unsigned(flag)) != 0; }
		bool none(LightFlags a, LightFlags b) const { return (bits & (unsigned(a) | unsigned(b))) == 0; }
	};
	struct LightData
	{
		Flags lightFlags;
		struct
		{
			float x, y, z;
		} color;
	};
	void UpdateCounts(const std::vector<LightData>& lightsData)
	{
#	include "light_count_collection.h"
	}
};
#	include "light_count_accessors.h"
namespace globals::features
{
	inline LightLimitFix lightLimitFix;
}
#	include "light_count_api.h"

CSX::Api::ServiceFoundation& Foundation()
{
	static CSX::Api::ServiceFoundation service({ "csx.feature", 1, 1, 46 });
	return service;
}
template <class Task>
json OnMain(Task task)
{
	return task();
}
// clang-format off
#include "light_count_dispatch_start.h"
#include "light_count_dispatch_action.h"
    return Foundation().MakeError(args, "unsupported_action", "Unknown action");
}
// clang-format on
json Dispatch(json args = json::object())
{
	static unsigned ordinal = 0;
	args["action"] = args.value("action", std::string("light_diagnostics"));
	args["contractMajor"] = 1;
	args["clientId"] = "light-count-test";
	if (!args.contains("commandId"))
		args["commandId"] = std::to_string(++ordinal);
	return Foundation().Dispatch(args, BuildResult);
}

void TestCapture()
{
	using Status = Diagnostics::CaptureState;
	Diagnostics diagnostic;
	auto id = diagnostic.RequestCapture(10);
	diagnostic.Collect(11, [] { return Diagnostics::Counts{ 3, 2, 1 }; });
	Require(diagnostic.ReadCapture(100).state == Status::Complete && diagnostic.ReadCapture(100).id == id,
		"Asynchronous queries must retain completed results");
	diagnostic.Request(100);
	diagnostic.Collect(100, [] { return Diagnostics::Counts{}; });
	Require(diagnostic.ReadCapture(100).sample.counts.pointLights == 3, "UI samples must not overwrite API captures");
	id = diagnostic.RequestCapture(101);
	diagnostic.Collect(101, [&] {
		id = diagnostic.RequestCapture(101);
		return Diagnostics::Counts{ 5, 4, 3 };
	});
	Require(diagnostic.ReadCapture(101).id == id && diagnostic.ReadCapture(101).state == Status::Pending,
		"An older in-flight capture cannot satisfy a superseding request");
	diagnostic.Collect(102, [] { return Diagnostics::Counts{}; });
	Require(diagnostic.ReadCapture(102).state == Status::Complete, "A superseding request receives its own sample");
	diagnostic.RequestCapture(103);
	Require(diagnostic.ReadCapture(105).state == Status::Expired, "Missed updates must report expired, not zero lights");
	diagnostic.RequestCapture(106);
	try {
		diagnostic.Collect(106, []() -> Diagnostics::Counts { throw std::runtime_error("failure"); });
	} catch (const std::runtime_error&) {}
	Require(diagnostic.ReadCapture(106).state == Status::Failed, "Collection failure must be explicit");
}

void TestApi()
{
	auto& feature = globals::features::lightLimitFix;
	Require(Dispatch().at("result").at("lightDiagnostics").at("sample").is_null(), "Missing samples are not zero lights");
	const std::vector<LightLimitFix::LightData> lights{
		{ { 1 }, { 1, 1, 1 } }, { { 1 | 8 }, { 1, 0.5f, 0.25f } },
		{ { 2 | 8 }, { 1, 0, 0 } }, { { 1 | 4 }, { 1, 0, 0 } }, { { 0 }, { 1, 0, 0 } }
	};
	feature.UpdateCounts(lights);
	Require(!feature.lightCountDiagnostics.Read(20).available, "Queries must not start production light iteration");
	const json capture{ { "lightDiagnostics", { { "capture", true } } } };
	auto response = Dispatch(capture).at("result").at("lightDiagnostics");
	Require(response.at("status") == "pending" && response.at("requestedCaptureId") == response.at("captureId"), "API requests are correlated");
	feature.UpdateCounts(lights);
	response = Dispatch().at("result").at("lightDiagnostics");
	Require(response.at("sample").at("pointLights") == 2 && response.at("sample").at("shadowCastingPointLights") == 1 &&
				response.at("sample").at("colouredPointLights") == 1,
		"Production collection excludes spotlight, particle and non-point entries");
	globals::state->Frame(120);
	response = Dispatch().at("result").at("lightDiagnostics");
	Require(response.at("status") == "complete" && response.at("sample").at("ageFrames") == 100, "Slow API polling retains a labelled historical sample");
	Require(Dispatch({ { "lightDiagnostics", { { "capture", false } } } }).at("ok") == true, "capture=false only reads");
	feature.UpdateCounts(lights);
	Require(!feature.lightCountDiagnostics.Read(120).available, "Reading retained captures never restarts collection");
	for (const auto& patch : { json(true), json::object(), json{ { "capture", 1 } }, json{ { "capture", true }, { "extra", 1 } } })
		Require(Dispatch({ { "lightDiagnostics", patch } }).at("ok") == false, "Invalid payload rejected before capture");
	Require(Dispatch({ { "unknown", true } }).at("ok") == false, "Unknown arguments rejected");
	Require(Dispatch({ { "action", "point_shadow_correction" }, { "lightDiagnostics", { { "capture", true } } } }).at("ok") == false,
		"Capture fields cannot be routed through another action");
	globals::state->disabled = true;
	Require(Dispatch(capture).at("ok") == false, "Disabled LLF cannot start capture");
	globals::state->disabled = false;
	feature.loaded = false;
	Require(Dispatch(capture).at("ok") == false, "Unloaded LLF cannot start capture");
	feature.loaded = true;
	auto replay = capture;
	replay["commandId"] = "replayed-capture";
	const auto first = Dispatch(replay).at("result").at("lightDiagnostics").at("requestedCaptureId");
	feature.UpdateCounts(lights);
	Dispatch(replay);
	Require(feature.lightCountDiagnostics.ReadCapture(120).id == first.get<std::uint64_t>() &&
				feature.lightCountDiagnostics.ReadCapture(120).state == Diagnostics::CaptureState::Complete,
		"Replay must not create another capture");
	json spec{ { "description", "Features." } };
	AddLightDiagnosticsSchema(spec);
	Require(spec["inputSchema"]["properties"]["action"]["enum"].at(0) == "light_diagnostics", "Schema advertises the action");
	Require(spec["inputSchema"]["properties"]["lightDiagnostics"]["additionalProperties"] == false, "Schema rejects unknown capture fields");
	Require(spec["outputSchema"]["properties"]["result"]["properties"]["lightDiagnostics"]["properties"]["sample"]["properties"].size() == 5,
		"Schema describes sample age and every count");
}
#endif

int main()
try {
	TestPolicy();
#ifdef DEVBENCH_BRIDGE_ENABLED
	TestCapture();
	TestApi();
#endif
	std::cout << "Light count diagnostics contracts passed\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
