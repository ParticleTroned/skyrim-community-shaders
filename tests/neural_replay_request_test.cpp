#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

using json = nlohmann::json;
namespace globals
{
	struct State
	{
		bool developer = false;
		bool IsDeveloperMode() const { return developer; }
	};
	State instance;
	State* state = &instance;
}
namespace NeuralRendering::Color
{
	struct Configuration
	{
		struct Experiments
		{
			bool captureFrameEvidence = true, transportBypass = false, applyModelEdit = true;
		} experiments;
	};
	struct Registry
	{
		Configuration config;
		static Registry& Instance()
		{
			static Registry value;
			return value;
		}
		Configuration Snapshot() const { return config; }
	};
}
namespace NeuralRendering::Replay
{
	unsigned calls = 0;
	json Status() { return { { "ok", true } }; }
	json Cancel(std::string_view id) { return { { "ok", id == "owned" } }; }
	json Request(unsigned frames, unsigned timeout, const std::filesystem::path&)
	{
		++calls;
		return { { "ok", true }, { "frames", frames }, { "timeoutMs", timeout } };
	}
}
namespace Util::PathHelpers
{
	std::filesystem::path GetLogPath() { return "fixture/CommunityShaders.log"; }
}
template <class F>
json RunOnMainThread(F callback)
{
	return callback();
}
#include "neural_replay_request_under_test.h"

int main()
{
	const auto check = [](bool ok) { if (!ok) throw std::runtime_error("native replay request contract failed"); };
	using namespace NeuralRendering;
	const auto rejected = [&](const json& request) {
		const auto before = Replay::calls;
		check(!BuildNeuralReplayResult(request).at("ok").get<bool>());
		check(Replay::calls == before);
	};
	rejected(nullptr);
	rejected(json::array());
	rejected({ { "action", false } });
	rejected({ { "action", "capture" } });
	globals::instance.developer = true;
	for (const json invalid : { json(false), json(-1), json(0), json(1.5), json("1"), json(UINT64_MAX) }) {
		rejected({ { "action", "capture" }, { "frames", invalid } });
		rejected({ { "action", "capture" }, { "timeoutMs", invalid } });
	}
	rejected({ { "action", "capture" }, { "frames", 33 } });
	rejected({ { "action", "capture" }, { "timeoutMs", 30001 } });
	rejected({ { "action", "capture" }, { "path", "untrusted" } });
	rejected({ { "action", "status" }, { "frames", 1 } });
	rejected({ { "action", "cancel" } });
	rejected({ { "action", "cancel" }, { "requestId", 3 } });
	rejected({ { "action", "cancel" }, { "requestId", "other" } });
	rejected({ { "action", "unknown" } });
	auto& config = Color::Registry::Instance().config;
	config.experiments.captureFrameEvidence = false;
	rejected({ { "action", "capture" } });
	config = {};
	config.experiments.transportBypass = true;
	rejected({ { "action", "capture" } });
	config = {};
	config.experiments.applyModelEdit = false;
	rejected({ { "action", "capture" } });
	config = {};
	check(BuildNeuralReplayResult({ { "action", "capture" } }) == json({ { "ok", true }, { "frames", 1 }, { "timeoutMs", 30000 } }));
	check(BuildNeuralReplayResult({ { "action", "capture" }, { "frames", 32 }, { "timeoutMs", 1 } }).at("frames") == 32);
	globals::state = nullptr;
	check(BuildNeuralReplayResult({ { "action", "status" } }).at("ok").get<bool>());
	check(BuildNeuralReplayResult({ { "action", "cancel" }, { "requestId", "owned" } }).at("ok").get<bool>());
	rejected({ { "action", "capture" } });
	std::cout << "Native replay request validation passed\n";
}
