#include "Api/MainThreadDispatchState.h"
#include "SKSE/Impl/PCH.h"
#include "Utils/RendererContextAccess.h"

#include <chrono>
#include <functional>
#include <future>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

using json = nlohmann::json;
using namespace std::chrono_literals;
constexpr auto kMainThreadTimeout = 100ms;

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
#include "neural_renderer_ownership_under_test.h"

void Require(bool condition)
{
	if (!condition)
		throw std::runtime_error("NR renderer ownership regression");
}

auto Queue(std::function<json()> run)
{
	auto result = std::async(std::launch::async, [run = std::move(run)] { return RunWithRendererOwnership(run); });
	const auto deadline = std::chrono::steady_clock::now() + 2s;
	while (!g_rendererCommandPending.load(std::memory_order_acquire)) {
		Require(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
	return result;
}

json AtFrame(std::function<json()> run)
{
	auto result = Queue(std::move(run));
	ProcessRendererCommand();
	Require(result.wait_for(2s) == std::future_status::ready);
	return result.get();
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
		const auto result = AtFrame(mutate);
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
		auto result = Queue(mutate);
		{
			const Util::RendererOwnership frame(&renderer.lock, true);
			// A nonowner boundary leaves the same request pending, without mutation.
			std::jthread worker([] { ProcessRendererCommand(); });
			worker.join();
			Require(mutations == 0 && result.wait_for(0ms) == std::future_status::timeout);
			// The renderer's own frame boundary can claim recursive ownership.
			ProcessRendererCommand();
		}
		Require(result.get().at("ok") && mutations == 1);
	}
	const auto result = AtFrame([&]() -> json {
		bool acquired = false;
		std::jthread rendering([&] {
			const Util::RendererOwnership draw(&renderer.lock);
			acquired = static_cast<bool>(draw);
		});
		rendering.join();
		Require(!acquired);
		// Another command cannot replace an admitted command or deadlock recursively.
		const auto nested = RunWithRendererOwnership(mutate);
		Require(nested.at("errorCode") == "renderer_command_busy" && !nested.at("mutationApplied"));
		return mutate();
	});
	Require(result.at("ok") && mutations == 2);
	const auto failure = AtFrame([]() -> json { throw std::runtime_error("failure"); });
	Require(failure.at("errorCode") == "renderer_command_failed" && failure.at("mutationApplied").is_null());
	Require(AtFrame(mutate).at("ok") && mutations == 3);
	// Missing frames cancel the queued command, including a retained boundary reference.
	const auto timeout = RunWithRendererOwnership(mutate);
	Require(timeout.at("errorCode") == "renderer_command_timeout" && !timeout.at("mutationApplied"));
	ProcessRendererCommand();
	Require(mutations == 3 && !g_rendererCommandPending.load());
	{
		auto pending = Queue(mutate);
		std::shared_ptr<RendererCommand> late;
		{
			std::lock_guard lock(g_rendererCommandMutex);
			late = g_rendererCommand;
		}
		Require(pending.get().at("errorCode") == "renderer_command_timeout");
		Require(!late->completion.TryBegin() && mutations == 3);
	}
	{
		auto pending = Queue(mutate);
		Require(RunWithRendererOwnership(mutate).at("errorCode") == "renderer_command_busy");
		ProcessRendererCommand();
		Require(pending.get().at("ok") && mutations == 4);
	}
	// Admission wins the deadline: retain the exact result even if execution takes longer.
	const auto slow = AtFrame([&]() -> json {
		std::this_thread::sleep_for(2 * kMainThreadTimeout);
		return mutate();
	});
	Require(slow.at("ok") && mutations == 5 && !g_rendererCommandPending.load());
}
