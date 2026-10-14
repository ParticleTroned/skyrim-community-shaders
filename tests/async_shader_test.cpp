#include <spdlog/sinks/sink.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace logger = spdlog;
#include "Utils/AsyncShader.h"

namespace
{
	std::atomic_size_t allocations{};
	std::atomic_bool failAllocation{};
}

void* operator new(std::size_t size)
{
	if (failAllocation.load())
		throw std::bad_alloc{};
	++allocations;
	if (auto* memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
	using Shader = Util::AsyncShader<ID3D11ComputeShader>;
	using State = Util::ShaderPreparation::State;
	using Define = std::pair<const char*, const char*>;
	constexpr wchar_t path[] = L"Data\\Shaders\\AsyncShaderTest.hlsl";
	constexpr char name[] = "AsyncShaderTest::Compute";
	std::atomic_uint64_t compilationGeneration{ 1 }, sourceGeneration{ 1 };
	std::atomic_bool deviceCurrent{ true };
	std::atomic_uint currentChecks{}, liveShaders{};
	unsigned requestCalls{}, errorLogs{};
	bool nullRequest{}, throwRequest{}, throwUnknownRequest{}, rejectInterface{}, invalidateOnQuery{};
	std::shared_ptr<Util::ShaderPreparation> lastOperation;
	std::wstring lastPath;
	std::string lastTarget, lastEntry, lastName;
	struct Macro
	{
		std::string name;
		std::optional<std::string> definition;
	};
	std::vector<Macro> lastMacros;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class ErrorSink final : public spdlog::sinks::sink
	{
	public:
		void log(const spdlog::details::log_msg& message) override
		{
			if (message.level == spdlog::level::err)
				++errorLogs;
		}
		void flush() override {}
		void set_pattern(const std::string&) override {}
		void set_formatter(std::unique_ptr<spdlog::formatter>) override {}
	};

	class TestShader final : public ID3D11ComputeShader
	{
	public:
		TestShader() { ++liveShaders; }
		~TestShader() { --liveShaders; }
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override
		{
			if (!result)
				return E_POINTER;
			*result = nullptr;
			if (rejectInterface || (id != __uuidof(IUnknown) && id != __uuidof(ID3D11DeviceChild) && id != __uuidof(ID3D11ComputeShader)))
				return E_NOINTERFACE;
			if (invalidateOnQuery)
				++sourceGeneration;
			*result = static_cast<ID3D11ComputeShader*>(this);
			AddRef();
			return S_OK;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const auto remaining = --references;
			if (!remaining)
				delete this;
			return remaining;
		}
		void STDMETHODCALLTYPE GetDevice(ID3D11Device** device) override { *device = nullptr; }
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }

	private:
		std::atomic_ulong references{ 1 };
	};

	void Prepare(Shader& shader)
	{
		shader.Prepare(path, { { "TEST", nullptr } }, "cs_5_0", "main", name);
	}

	void PublishReady(const std::shared_ptr<Util::ShaderPreparation>& operation)
	{
		operation->shader.attach(new TestShader);
		operation->Finish(State::Ready);
	}

	void CheckPreparationAndResidentPaths()
	{
		Shader shader;
		const auto callsBefore = requestCalls;
		Prepare(shader);
		Require(shader.IsPending() && !shader.IsReady() && !shader.HasFailed() && !shader.get(), "Requesting preparation must publish pending without exposing a shader");
		const auto pendingAllocations = allocations.load();
		for (unsigned draw = 0; draw < 10000; ++draw) {
			Prepare(shader);
			Require(!shader.get() && !shader.Poll() && shader.IsPending(), "Pending observation must neither wait nor expose incomplete work");
		}
		Require(requestCalls == callsBefore + 1 && allocations.load() == pendingAllocations, "Repeated pending requests must neither allocate nor enqueue duplicate work");
		PublishReady(lastOperation);
		Require(!shader.get() && shader.IsPending(), "A completed producer must remain hidden until owner adoption");
		auto* resident = shader.Poll();
		Require(resident && shader.get() == resident && shader.IsReady() && static_cast<bool>(shader), "Owner polling must adopt the completed object");
		const auto warmedAllocations = allocations.load();
		const auto checksBefore = currentChecks.load();
		++compilationGeneration;
		for (unsigned draw = 0; draw < 10000; ++draw) {
			Prepare(shader);
			Require(shader.get() == resident && shader.Wait() == resident && shader.IsReady(), "Resident shaders must survive generation changes until explicit invalidation");
		}
		Require(requestCalls == callsBefore + 1 && allocations.load() == warmedAllocations && currentChecks == checksBefore, "Resident observation must allocate and enqueue nothing without rechecking compilation generations");
		Require(shader.IsCurrent(), "Resident identity must ignore engine-only compilation generation changes");
		++sourceGeneration;
		Require(!shader.IsCurrent() && shader.get() == resident, "Boundary identity validation must see source changes without mutating resident draw access");
		lastOperation.reset();
		resident->AddRef();
		shader.Reset();
		Require(!shader.get() && !shader.IsReady() && !shader.IsPending() && !shader.HasFailed() && liveShaders == 1, "Reset must release only this owner's reference and clear its state");
		resident->Release();
	}

	void CheckBoundaryWait()
	{
		Shader shader;
		Prepare(shader);
		auto operation = lastOperation;
		std::atomic_bool waitEntered{}, waitReturned{};
		ID3D11ComputeShader* result = nullptr;
		std::jthread owner([&] {
			waitEntered.store(true, std::memory_order_release);
			waitEntered.notify_one();
			result = shader.Wait();
			waitReturned.store(true, std::memory_order_release);
		});
		while (!waitEntered.load(std::memory_order_acquire))
			waitEntered.wait(false, std::memory_order_acquire);
		Require(shader.IsPending() && !waitReturned.load(std::memory_order_acquire), "A boundary wait must not return while its producer remains pending");
		PublishReady(operation);
		owner.join();
		Require(result && shader.IsReady() && waitReturned.load(), "Terminal publication must wake the boundary owner and adopt the shader");
	}

	void CheckSupersededRequests()
	{
		Shader shader;
		Prepare(shader);
		auto superseded = lastOperation;
		const auto callsBefore = requestCalls;
		shader.Reset();
		Prepare(shader);
		auto replacement = lastOperation;
		PublishReady(superseded);
		Require(!shader.Poll() && shader.IsPending() && requestCalls == callsBefore + 1, "Superseded completion must not replace the current request");
		PublishReady(replacement);
		Require(shader.Wait() == replacement->shader.get(), "Only the replacement request may publish the resident shader");
	}

	void CheckStalePublication()
	{
		for (const State terminal : { State::Ready, State::Failed, State::Stale }) {
			Shader shader;
			Prepare(shader);
			if (terminal == State::Ready)
				PublishReady(lastOperation);
			else
				lastOperation->Finish(terminal);
			++sourceGeneration;
			const auto callsBefore = requestCalls;
			Require(!shader.Wait() && shader.GetStatus() == Shader::Status::Empty, "Obsolete success and failure must be rejected without latching failure");
			Require(requestCalls == callsBefore, "Stale adoption must not enqueue from observation");
			Prepare(shader);
			Require(shader.IsPending() && requestCalls == callsBefore + 1, "A new preparation boundary must be able to replace stale work");
			lastOperation->Finish(State::Stale);
		}
		{
			Shader shader;
			Prepare(shader);
			PublishReady(lastOperation);
			deviceCurrent.store(false);
			Require(!shader.Poll() && shader.GetStatus() == Shader::Status::Empty, "A device that is no longer current must not publish its object");
			deviceCurrent.store(true);
		}
		{
			Shader shader;
			Prepare(shader);
			PublishReady(lastOperation);
			invalidateOnQuery = true;
			Require(!shader.Poll() && shader.GetStatus() == Shader::Status::Empty, "Invalidation during interface acquisition must reject adoption");
			invalidateOnQuery = false;
		}
	}

	void CheckFailureLatches()
	{
		Shader shader;
		Prepare(shader);
		lastOperation->Finish(State::Failed);
		Require(!shader.Wait() && shader.HasFailed(), "Current producer failure must latch at the boundary");
		const auto callsBefore = requestCalls;
		const auto failedAllocations = allocations.load();
		for (unsigned draw = 0; draw < 10000; ++draw) {
			Prepare(shader);
			Require(!shader.get() && !shader.Poll() && !shader.Wait() && shader.HasFailed(), "Failed observation must retain its latch without another producer");
		}
		Require(requestCalls == callsBefore && allocations.load() == failedAllocations, "Failure-latched observation must not allocate or enqueue");
		shader.Reset();
		Prepare(shader);
		PublishReady(lastOperation);
		Require(shader.Wait(), "Reset must admit recovery from a genuine failure");
		shader.Fail();
		Require(!shader.get() && shader.HasFailed(), "An exhausted preparation boundary must discard its result and latch failure");
		Prepare(shader);
		Require(shader.HasFailed() && requestCalls == callsBefore + 1, "An explicit boundary failure must prevent another request until Reset");

		Shader incompatible;
		Prepare(incompatible);
		PublishReady(lastOperation);
		rejectInterface = true;
		const auto errorsBefore = errorLogs;
		Require(!incompatible.Poll() && incompatible.HasFailed(), "An incompatible producer object must be rejected and latched");
		rejectInterface = false;
		Require(errorLogs == errorsBefore + 1, "Incompatible shader objects must report their failure once");
	}

	void CheckArgumentsAndRequestExceptions()
	{
		Shader shader;
		const std::vector<Define> defines{ { "NULL", nullptr }, { "EMPTY", "" }, { "VALUE", "42" } };
		shader.Prepare(path, defines, "cs_4_0", "CustomMain", name);
		Require(lastPath == path && lastTarget == "cs_4_0" && lastEntry == "CustomMain" && lastName == name, "Cold preparation must forward the exact source, profile, entry and name");
		Require(lastMacros.size() == 3 && !lastMacros[0].definition && lastMacros[1].definition == "" && lastMacros[2].definition == "42", "Cold preparation must preserve ordered null, empty and valued macro definitions");
		lastOperation->Finish(State::Stale);
		shader.Reset();
		shader.Prepare(path, {}, "cs_5_0");
		Require(lastMacros.empty() && lastEntry == "main" && lastName.empty(), "Default entry and unnamed empty-macro preparation must remain supported");
		lastOperation->Finish(State::Stale);

		for (unsigned failure = 0; failure < 3; ++failure) {
			Shader failed;
			nullRequest = failure == 0;
			throwRequest = failure == 1;
			throwUnknownRequest = failure == 2;
			const auto callsBefore = requestCalls;
			Prepare(failed);
			nullRequest = throwRequest = throwUnknownRequest = false;
			Require(failed.HasFailed() && !failed.Wait(), "Unqueued and exceptional requests must latch failure");
			Prepare(failed);
			Require(requestCalls == callsBefore + 1, "Request exceptions must not repeat without Reset");
		}
		Shader allocationFailure;
		const auto callsBefore = requestCalls;
		failAllocation.store(true);
		Prepare(allocationFailure);
		failAllocation.store(false);
		Require(allocationFailure.HasFailed() && requestCalls == callsBefore, "Cold macro allocation failure must latch before request submission");
		Prepare(allocationFailure);
		Require(requestCalls == callsBefore, "Allocation failure must not repeat at later boundaries without Reset");
	}
}

std::shared_ptr<Util::ShaderPreparation> Util::RequestShaderPreparation(const wchar_t* shaderPath,
	const std::vector<Define>& defines, const char* target, const char* entry, const char* resourceName)
{
	++requestCalls;
	if (throwRequest)
		throw std::runtime_error("Injected request exception");
	if (throwUnknownRequest)
		throw 1;
	if (nullRequest)
		return nullptr;
	lastPath = shaderPath;
	lastTarget = target;
	lastEntry = entry;
	lastName = resourceName ? resourceName : "";
	lastMacros.clear();
	for (const auto& [macro, value] : defines)
		lastMacros.push_back({ macro, value ? std::optional<std::string>{ value } : std::nullopt });
	lastOperation = std::make_shared<ShaderPreparation>();
	lastOperation->compilationGeneration = compilationGeneration.load();
	lastOperation->sourceGeneration = sourceGeneration.load();
	return lastOperation;
}

bool Util::IsShaderPreparationCurrent(const ShaderPreparation& operation, bool resident)
{
	++currentChecks;
	return deviceCurrent.load() && (resident || operation.compilationGeneration == compilationGeneration.load()) && operation.sourceGeneration == sourceGeneration.load();
}

int main()
{
	try {
		logger::set_default_logger(std::make_shared<spdlog::logger>("async_shader_test", std::make_shared<ErrorSink>()));
		for (const auto check : { CheckPreparationAndResidentPaths, CheckBoundaryWait, CheckSupersededRequests,
				 CheckStalePublication, CheckFailureLatches, CheckArgumentsAndRequestExceptions }) {
			check();
			lastOperation.reset();
			Require(liveShaders == 0, "Completed and superseded requests must release every shader");
		}
		std::cout << "Async shader: boundary waits, stale publication, superseded ownership, failure latches and zero warmed allocations passed\n";
		return 0;
	} catch (const std::exception& error) {
		failAllocation.store(false);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
