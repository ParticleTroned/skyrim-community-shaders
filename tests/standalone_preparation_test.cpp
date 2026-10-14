#include "ShaderCompilationSchedulingPolicy.h"
#include "Utils/ShaderCompiler.h"
#include "Utils/ShaderPreparation.h"

#include <BS_thread_pool.hpp>
#include <atomic>
#include <d3dcompiler.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace logger = spdlog;
namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	std::atomic_size_t entered{}, priorityCalls{}, startupPriorityCalls{}, gameplayPriorityCalls{};
	std::atomic_bool blockCompile{}, releaseCompile{}, nullCompile{}, standardException{}, unknownException{}, injectSourceInstability{}, changeSourceEpoch{}, changeSourceContent{}, failDigest{}, priorityAvailable{ true }, injectPhaseCompletionFailure{};
	winrt::com_ptr<ID3DBlob> bytecode;
}
namespace Util
{
	bool SetCurrentThreadShaderCompilationPriority(ShaderCompilationSchedulingPolicy::CompilationPhase phase) noexcept
	{
		++priorityCalls;
		if (phase == ShaderCompilationSchedulingPolicy::CompilationPhase::Startup)
			++startupPriorityCalls;
		else
			++gameplayPriorityCalls;
		return priorityAvailable.load();
	}
}
namespace SIE
{
	std::atomic_uint64_t g_shaderSourceGeneration{ 1 }, sourceVersion{ 1 };
	std::filesystem::path ShaderSourceRoot() { return "Data/Shaders"; }
	std::optional<Util::ContentHash::Hash128> GetShaderContentDigest(const std::filesystem::path&, const std::filesystem::path&)
	{
		if (failDigest)
			throw std::runtime_error("Injected digest failure");
		return Util::ContentHash::Hash128{ sourceVersion.load(), 0 };
	}
	class ShaderCache
	{
	public:
		struct Compilation
		{
			std::atomic_uint64_t generation{ 1 }, totalTasks{}, completedTasks{}, failedTasks{};
			void Clear()
			{
				++generation;
				totalTasks = completedTasks = failedTasks = 0;
			}
		} compilationSet;
		std::mutex standalonePreparationMutex;
		std::unique_ptr<BS::thread_pool<>> standalonePreparationPool;
		std::atomic_size_t standalonePreparations{};
		std::atomic_bool menuLoaded{}, startupCompilationComplete{};
		std::atomic_size_t startupTransitions{};
		bool acceptStandalonePreparations = true;
		std::stop_source ssource;
		std::jthread managementJthread;
		~ShaderCache() { Shutdown(); }
		void StopFileWatcher() {}
		bool IsTaskStale(std::optional<uint64_t> value) const
		{
			return value && *value != compilationSet.generation.load(std::memory_order_acquire);
		}
		bool IsStandalonePreparationCurrent(const Util::ShaderPreparation&, bool resident = false) const;
		std::shared_ptr<Util::ShaderPreparation> PrepareStandaloneShader(Util::OwnedStandaloneShaderRequest);
		bool IsCompiling();
		void TryCompleteStartupCompilationPhase()
		{
			if (ShaderCompilationSchedulingPolicy::ShouldCompleteStartupCompilation(menuLoaded.load(), IsCompiling()) &&
				!startupCompilationComplete.exchange(true)) {
				++startupTransitions;
				if (injectPhaseCompletionFailure.load())
					throw std::runtime_error("Injected phase completion diagnostic failure");
			}
		}
		void StopCompilation();
		void Shutdown();
	};
	// clang-format off
#include "standalone_preparation_under_test.h"
#include "standalone_compiling_under_test.h"
#include "standalone_stop_under_test.h"
#include "standalone_shutdown_under_test.h"
	// clang-format on
}
namespace
{
	using State = Util::ShaderPreparation::State;
	winrt::com_ptr<ID3D11Device> CreateDevice(UINT flags = 0)
	{
		winrt::com_ptr<ID3D11Device> device;
		const D3D_FEATURE_LEVEL levels[]{ D3D_FEATURE_LEVEL_11_0 };
		Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 1,
					D3D11_SDK_VERSION, device.put(), nullptr, nullptr)),
			"WARP device creation failed");
		return device;
	}
	Util::OwnedStandaloneShaderRequest Request(SIE::ShaderCache& cache, const winrt::com_ptr<ID3D11Device>& device)
	{
		Util::OwnedStandaloneShaderRequest request;
		request.source = L"QueuedPreparation.hlsl";
		request.profile = "cs_5_0";
		request.entryPoint = "main";
		request.resourceName = "StandalonePreparationTest::Compute";
		request.macros = { { "NULL", std::nullopt }, { "EMPTY", "" }, { "VALUE", "42" } };
		request.device = device;
		request.state.compilationGeneration = cache.compilationSet.generation.load();
		request.state.sourceGeneration = SIE::g_shaderSourceGeneration.load();
		return request;
	}
	State Wait(const std::shared_ptr<Util::ShaderPreparation>& operation)
	{
		while (operation->state.load(std::memory_order_acquire) == State::Pending)
			operation->state.wait(State::Pending, std::memory_order_acquire);
		return operation->state.load(std::memory_order_acquire);
	}
	void WaitEntered(size_t minimum)
	{
		for (auto observed = entered.load(std::memory_order_acquire); observed < minimum; observed = entered.load(std::memory_order_acquire))
			entered.wait(observed, std::memory_order_acquire);
	}
	void ReleaseWorkers()
	{
		releaseCompile.store(true, std::memory_order_release);
		releaseCompile.notify_all();
	}
	struct ReleaseOnExit
	{
		~ReleaseOnExit() { ReleaseWorkers(); }
	};
	void ResetSeams()
	{
		entered = 0;
		blockCompile = false;
		releaseCompile = false;
		nullCompile = standardException = unknownException = injectSourceInstability = changeSourceEpoch = changeSourceContent = failDigest = false;
		priorityAvailable = true;
		injectPhaseCompletionFailure = false;
	}

	void CheckSuccessAndCapturedOwnership()
	{
		ResetSeams();
		SIE::ShaderCache cache;
		auto device = CreateDevice();
		blockCompile = true;
		ReleaseOnExit release;
		auto request = Request(cache, device);
		request.state.shaderDefinesGeneration = 37;
		request.state.developerMode = true;
		request.state.partialPrecision = true;
		request.state.avoidFlowControl = true;
		auto operation = cache.PrepareStandaloneShader(std::move(request));
		WaitEntered(1);
		cache.compilationSet.totalTasks = cache.compilationSet.completedTasks = 1;
		cache.menuLoaded = true;
		cache.TryCompleteStartupCompilationPhase();
		Require(!cache.startupCompilationComplete, "DataLoaded must retain startup scheduling while standalone preparation is still active");
		device = nullptr;
		request.source = L"Mutated.hlsl";
		request.macros.clear();
		request.state.shaderDefinesGeneration = 99;
		request.state.developerMode = request.state.partialPrecision = request.state.avoidFlowControl = false;
		Require(operation->shaderDefinesGeneration == 37 && operation->developerMode && operation->partialPrecision && operation->avoidFlowControl,
			"Queued operation identity must retain the exact defines revision and compiler flags after its original request is changed");
		Require(operation->state.load() == State::Pending && operation->device && cache.IsCompiling(), "Queued work must retain captured device identity and count as compilation");
		ReleaseWorkers();
		Require(Wait(operation) == State::Ready && operation->shader, "The actual worker must compile the unchanged owned request");
		cache.standalonePreparationPool->wait();
		Require(cache.standalonePreparations == 0 && !cache.IsCompiling(), "Producer completion must release its active count before the pool drains");
		Require(cache.startupCompilationComplete && cache.startupTransitions == 1, "The last standalone producer must finish the startup phase after engine work has already drained");
		++cache.compilationSet.generation;
		Require(!cache.IsStandalonePreparationCurrent(*operation) && cache.IsStandalonePreparationCurrent(*operation, true), "Resident currentness must ignore engine-only compilation generation changes");
		++SIE::g_shaderSourceGeneration;
		Require(cache.IsStandalonePreparationCurrent(*operation, true), "An unrelated edit must preserve resident shader identity");
		failDigest = true;
		Require(!cache.IsStandalonePreparationCurrent(*operation, true), "Unverifiable source identity must fail closed without throwing");
		failDigest = false;
		++SIE::sourceVersion;
		++SIE::g_shaderSourceGeneration;
		Require(!cache.IsStandalonePreparationCurrent(*operation, true), "A changed source closure must reject the resident shader");
	}

	void CheckCompilationPriorityPhases()
	{
		ResetSeams();
		SIE::ShaderCache cache;
		auto device = CreateDevice();
		const auto startupBefore = startupPriorityCalls.load();
		auto startup = cache.PrepareStandaloneShader(Request(cache, device));
		Require(Wait(startup) == State::Ready, "Startup priority must preserve standalone producer progress");
		cache.standalonePreparationPool->wait();
		Require(startupPriorityCalls == startupBefore + 1, "Standalone startup work must select cooperative priority");
		cache.startupCompilationComplete = true;
		for (const bool available : { true, false }) {
			priorityAvailable = available;
			const auto gameplayBefore = gameplayPriorityCalls.load();
			auto gameplay = cache.PrepareStandaloneShader(Request(cache, device));
			Require(Wait(gameplay) == State::Ready, "A failed gameplay priority change must preserve standalone producer progress");
			cache.standalonePreparationPool->wait();
			Require(gameplayPriorityCalls == gameplayBefore + 1 && cache.standalonePreparations == 0,
				"Standalone gameplay work must apply gameplay priority and release admission on the reused pool");
		}
	}

	void CheckBoundedQueueCancellationAndShutdown()
	{
		ResetSeams();
		SIE::ShaderCache cache;
		auto device = CreateDevice();
		blockCompile = true;
		ReleaseOnExit release;
		std::vector<std::shared_ptr<Util::ShaderPreparation>> operations;
		for (unsigned index = 0; index < 2; ++index)
			operations.push_back(cache.PrepareStandaloneShader(Request(cache, device)));
		WaitEntered(2);
		for (unsigned index = 2; index < 16; ++index)
			operations.push_back(cache.PrepareStandaloneShader(Request(cache, device)));
		Require(cache.standalonePreparations == 16 && cache.standalonePreparationPool->get_tasks_queued() == 14, "The two-worker scheduler must enforce its complete 16-operation bound");
		auto rejected = cache.PrepareStandaloneShader(Request(cache, device));
		Require(Wait(rejected) == State::Stale && cache.standalonePreparations == 16, "An over-capacity request must terminate without increasing active work");
		std::atomic_bool queuedWaiterEntered{}, queuedWaiterDone{};
		std::jthread waiter([&] {
			queuedWaiterEntered = true;
			queuedWaiterEntered.notify_one();
			queuedWaiterDone = Wait(operations.back()) == State::Stale;
		});
		while (!queuedWaiterEntered.load())
			queuedWaiterEntered.wait(false);
		cache.StopCompilation();
		waiter.join();
		Require(queuedWaiterDone && cache.standalonePreparations == 2, "Purging a queued producer must publish stale, wake waiters and decrement its count");
		for (size_t index = 2; index < operations.size(); ++index)
			Require(Wait(operations[index]) == State::Stale, "Every purged queue entry must have a terminal operation state");
		Require(cache.IsCompiling(), "Running producers must remain accounted for after queued cancellation");
		auto stopped = cache.PrepareStandaloneShader(Request(cache, device));
		Require(Wait(stopped) == State::Stale, "Stopped compilation must close preparation intake");
		std::atomic_bool shutdownEntered{}, shutdownDone{};
		std::jthread shutdown([&] {
			shutdownEntered = true;
			shutdownEntered.notify_one();
			cache.Shutdown();
			shutdownDone = true;
		});
		while (!shutdownEntered.load())
			shutdownEntered.wait(false);
		Require(!shutdownDone.load() && cache.standalonePreparations == 2, "Shutdown must drain running producers before destroying their cache");
		ReleaseWorkers();
		shutdown.join();
		Require(shutdownDone && cache.standalonePreparations == 0 && !cache.IsCompiling(), "Shutdown must finish after all running producer counts are released");
		Require(Wait(operations[0]) == State::Stale && Wait(operations[1]) == State::Stale && !operations[0]->shader && !operations[1]->shader, "Stopped generation results must discard their objects before publication");
	}

	void CheckTerminalFailuresAndSourceRaces()
	{
		auto device = CreateDevice();
		for (unsigned condition = 0; condition < 8; ++condition) {
			ResetSeams();
			SIE::ShaderCache cache;
			nullCompile = condition == 0;
			standardException = condition == 1;
			unknownException = condition == 2;
			injectSourceInstability = condition == 3;
			changeSourceEpoch = condition == 4;
			changeSourceContent = condition == 7;
			priorityAvailable = condition != 5;
			cache.menuLoaded = injectPhaseCompletionFailure = condition == 6;
			auto operation = cache.PrepareStandaloneShader(Request(cache, device));
			const State expected = condition < 3 ? State::Failed : condition == 3 || condition == 7 ? State::Stale :
			                                                                                          State::Ready;
			Require(Wait(operation) == expected, "Worker failure, source instability and priority-warning states must remain distinct");
			Require(bool(operation->shader) == (expected == State::Ready), "Only a current successful producer may publish its object");
			cache.standalonePreparationPool->wait();
			Require(cache.standalonePreparations == 0, "Every terminal worker path must release active admission");
			if (condition == 6)
				Require(cache.startupCompilationComplete && cache.startupTransitions == 1, "A throwing phase diagnostic must retain the completed transition and drain its producer safely");
		}
		ResetSeams();
		SIE::ShaderCache cache;
		auto invalid = Request(cache, device);
		--invalid.state.sourceGeneration;
		auto operation = cache.PrepareStandaloneShader(std::move(invalid));
		Require(Wait(operation) == State::Ready && entered == 1, "An unrelated edit before dispatch must still compile current source");
		cache.standalonePreparationPool->wait();
		auto cancelled = Request(cache, device);
		--cancelled.state.compilationGeneration;
		auto stale = cache.PrepareStandaloneShader(std::move(cancelled));
		Require(Wait(stale) == State::Stale && entered == 1, "An explicitly cancelled request must stop before compilation");
		cache.standalonePreparationPool->wait();
	}

	void CheckDeviceThreadingRejection()
	{
		ResetSeams();
		SIE::ShaderCache cache;
		auto device = CreateDevice(D3D11_CREATE_DEVICE_SINGLETHREADED);
		auto operation = cache.PrepareStandaloneShader(Request(cache, device));
		Require(Wait(operation) == State::Failed && entered == 0 && !operation->shader, "A single-threaded device must fail before entering a compiler worker");
		cache.standalonePreparationPool->wait();
		Require(cache.standalonePreparations == 0, "Rejected device work must release admission");
	}
}

winrt::com_ptr<ID3D11DeviceChild> Util::CompilePreparedShader(const OwnedStandaloneShaderRequest& request,
	SIE::ShaderCache&, ShaderCompileTiming*, bool* sourceUnstable, std::optional<ContentHash::Hash128>* acceptedSourceDigest)
{
	Require(request.source == L"QueuedPreparation.hlsl" && request.profile == "cs_5_0" && request.entryPoint == "main" &&
				request.macros.size() == 3 && request.macros[0].name == "NULL" && !request.macros[0].definition &&
				request.macros[1].name == "EMPTY" && request.macros[1].definition == "" &&
				request.macros[2].name == "VALUE" && request.macros[2].definition == "42",
		"Worker received altered captured compiler inputs");
	entered.fetch_add(1, std::memory_order_release);
	entered.notify_all();
	while (blockCompile.load() && !releaseCompile.load(std::memory_order_acquire))
		releaseCompile.wait(false, std::memory_order_acquire);
	if (standardException.load())
		throw std::runtime_error("Injected compiler exception");
	if (unknownException.load())
		throw 1;
	if (nullCompile.load())
		return nullptr;
	if (injectSourceInstability.load() && sourceUnstable)
		*sourceUnstable = true;
	if (acceptedSourceDigest)
		*acceptedSourceDigest = ContentHash::Hash128{ SIE::sourceVersion.load(), 0 };
	if (changeSourceContent.load())
		++SIE::sourceVersion;
	if (changeSourceEpoch.load() || changeSourceContent.load())
		++SIE::g_shaderSourceGeneration;
	winrt::com_ptr<ID3D11ComputeShader> shader;
	Require(SUCCEEDED(request.device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.put())), "WARP compute shader creation failed");
	winrt::com_ptr<ID3D11DeviceChild> result;
	result.attach(shader.detach());
	return result;
}

int main()
{
	try {
		logger::set_default_logger(std::make_shared<spdlog::logger>("standalone_preparation_test", std::make_shared<spdlog::sinks::null_sink_mt>()));
		constexpr char source[] = "[numthreads(1,1,1)] void main() {}";
		Require(SUCCEEDED(D3DCompile(source, sizeof(source) - 1, "StandalonePreparationTest", nullptr, nullptr,
					"main", "cs_5_0", 0, 0, bytecode.put(), nullptr)),
			"Compute test bytecode compilation failed");
		CheckSuccessAndCapturedOwnership();
		CheckCompilationPriorityPhases();
		CheckBoundedQueueCancellationAndShutdown();
		CheckTerminalFailuresAndSourceRaces();
		CheckDeviceThreadingRejection();
		Require(priorityCalls > 0, "Actual worker preparation must apply the compilation phase priority");
		std::cout << "Standalone preparation: owned capture, two-worker/16-operation bound, purge notification, startup counts, shutdown, device threading and source races passed\n";
		return 0;
	} catch (const std::exception& error) {
		ReleaseWorkers();
		std::cerr << error.what() << '\n';
		return 1;
	}
}
