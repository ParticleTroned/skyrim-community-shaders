#include "Api/MainThreadDispatchPolicy.h"
#include "Utils/RendererContextAccess.h"

#include <chrono>
#include <functional>
#include <future>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

using json = nlohmann::json;
using namespace std::chrono_literals;
constexpr auto kMainThreadTimeout = 10ms;

struct Renderer
{
	struct Data
	{
		ID3D11DeviceContext* context = nullptr;
	} data;
	CRITICAL_SECTION lock;
	Renderer() { InitializeCriticalSection(&lock); }
	~Renderer() { DeleteCriticalSection(&lock); }
	Data& GetRuntimeData() { return data; }
	CRITICAL_SECTION& GetLock() { return lock; }
};

namespace globals
{
	namespace game
	{
		Renderer* renderer = nullptr;
	}
	namespace d3d
	{
		ID3D11DeviceContext* context = nullptr;
	}
}
namespace CSX::Api
{
	void EnterRuntimeMainThreadTask() {}
}
namespace SKSE
{
	struct Queue
	{
		bool deferred = false;
		std::function<void()> task;
		void AddTask(std::function<void()> run)
		{
			if (deferred)
				task = std::move(run);
			else
				run();
		}
	};
	Queue queue;
	Queue* GetTaskInterface() { return &queue; }
}

#include "neural_renderer_ownership_under_test.h"

void Require(bool condition)
{
	if (!condition)
		throw std::runtime_error("NR renderer ownership regression");
}

int main()
{
	Renderer renderer;
	// Identity-only fixtures never invoke a COM method.
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(&renderer);
	renderer.data.context = context;
	unsigned mutations = 0;
	const auto mutate = [&]() -> json {
		++mutations;
		return { { "ok", true }, { "value", mutations } };
	};
	const auto rejected = [&](const char* reason) {
		const auto before = mutations;
		const auto result = RunWithRendererOwnership(mutate);
		Require(!result.at("ok") && !result.at("mutationApplied"));
		Require(result.at("errorCode") == reason && mutations == before);
	};
	rejected("renderer_unavailable");
	globals::game::renderer = &renderer;
	rejected("renderer_unavailable");
	globals::d3d::context = context;
	renderer.data.context = nullptr;
	rejected("renderer_unavailable");
	renderer.data.context = context;
	{
		const Util::RendererOwnership frame(&renderer.lock, true);
		// Another SKSE worker must reject before touching state or teardown.
		std::jthread worker([&] { rejected("renderer_busy"); });
	}
	Require(mutations == 0);
	const auto result = RunWithRendererOwnership([&]() -> json {
		bool acquired = false;
		std::jthread rendering([&] {
			const Util::RendererOwnership draw(&renderer.lock);
			acquired = static_cast<bool>(draw);
		});
		rendering.join();
		Require(!acquired);
		// Native ownership is recursive when SKSE already runs on its owner.
		return RunWithRendererOwnership(mutate);
	});
	Require(result.at("ok") && mutations == 1);
	Require(RunWithRendererOwnership([]() -> json { throw std::runtime_error("failure"); }).contains("error"));
	std::jthread afterException([&] { Require(RunWithRendererOwnership(mutate).at("ok")); });
	afterException.join();
	Require(mutations == 2);
	SKSE::queue.deferred = true;
	const auto timeout = RunWithRendererOwnership(mutate);
	Require(timeout.at("errorCode") == "main_thread_timeout");
	SKSE::queue.task();
	Require(mutations == 2);
	SKSE::queue.task = {};
}
