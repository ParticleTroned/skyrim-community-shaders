#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace logger = spdlog;
#include "Utils/LazyShader.h"
#include "d3d_resource_naming.h"

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
	using Define = std::pair<const char*, const char*>;
	unsigned compileCalls{}, liveShaders{};
	bool failCompile{}, throwCompile{}, throwUnknownCompile{};
	unsigned loggedErrors{}, compilerErrors{}, allocationErrors{}, unknownErrors{};
	std::span<const Define> expectedDefines;
	Util::ShaderCompileTiming* expectedTiming{};
	constexpr wchar_t path[] = L"Data\\Shaders\\LazyShaderTest.hlsl";
	constexpr char resourceName[] = "LazyShaderTest::Compute";
	std::wstring_view expectedPath = path;
	std::string_view expectedTarget = "cs_5_0";
	std::string_view expectedEntry = "main";

	class ErrorSink final : public spdlog::sinks::sink
	{
	public:
		void log(const spdlog::details::log_msg& message) override
		{
			if (message.level != spdlog::level::err)
				return;
			++loggedErrors;
			const std::string_view text(message.payload.data(), message.payload.size());
			compilerErrors += text.find("Injected compiler exception") != std::string_view::npos;
			allocationErrors += text.find(std::bad_alloc{}.what()) != std::string_view::npos;
			unknownErrors += text.find("unknown exception") != std::string_view::npos;
		}
		void flush() override {}
		void set_pattern(const std::string&) override {}
		void set_formatter(std::unique_ptr<spdlog::formatter>) override {}
	};

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	bool Equal(const char* left, const char* right)
	{
		return left && right ? std::string_view(left) == right : left == right;
	}

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
			if (id != __uuidof(IUnknown) && id != __uuidof(ID3D11DeviceChild) && id != __uuidof(ID3D11ComputeShader))
				return E_NOINTERFACE;
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
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT size, const void* data) override
		{
			if (size > debugName.size())
				return E_INVALIDARG;
			std::memcpy(debugName.data(), data, size);
			debugNameSize = size;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }

		std::string_view Name() const { return { debugName.data(), debugNameSize }; }

	private:
		ULONG references = 1;
		std::array<char, 128> debugName{};
		UINT debugNameSize{};
	};

	ID3D11ComputeShader* Request(Util::LazyShader<ID3D11ComputeShader>& shader, const char* macro, Util::ShaderCompileTiming* timing = nullptr)
	{
		return shader.Get(path, { { macro, nullptr } }, "cs_5_0", "main", resourceName, timing);
	}

	void CheckCachedRequests()
	{
		constexpr std::array<const char*, 4> macros{
			"DOWNSAMPLE_SHADOW_MIP0", "DOWNSAMPLE_SHADOW_MIP1", "BLUR_HORIZONTAL", "BLUR_VERTICAL"
		};
		std::array<Util::LazyShader<ID3D11ComputeShader>, 4> shaders;
		std::array<ID3D11ComputeShader*, 4> cached{};
		Util::ShaderCompileTiming timing{};
		expectedTiming = &timing;
		const auto callsBefore = compileCalls;
		for (size_t i = 0; i < shaders.size(); ++i) {
			const std::array<Define, 1> defines{ { { macros[i], nullptr } } };
			expectedDefines = defines;
			cached[i] = Request(shaders[i], macros[i], &timing);
			Require(cached[i] && shaders[i] && shaders[i].get() == cached[i], "Successful compilation must populate the cache");
			Require(static_cast<TestShader*>(cached[i])->Name() == resourceName, "Compilation must retain resource naming");
		}
		Require(compileCalls == callsBefore + 4 && timing.bytecodeCompilationQpcTicks == 4 * 13 && timing.d3dObjectCreationQpcTicks == 4 * 17,
			"Each shader must compile and record timing exactly once");
		expectedDefines = {};
		const auto allocationsBefore = allocations.load();
		for (unsigned draw = 0; draw < 10000; ++draw)
			for (size_t i = 0; i < shaders.size(); ++i)
				Require(Request(shaders[i], macros[i], &timing) == cached[i], "Cached draws must retain shader identity");
		Require(allocations.load() == allocationsBefore, "Four warmed fixed-macro requests per draw must allocate zero times");
		Require(compileCalls == callsBefore + 4 && timing.bytecodeCompilationQpcTicks == 4 * 13 && timing.d3dObjectCreationQpcTicks == 4 * 17,
			"Cached draws must neither compile nor change timings");
		cached[0]->AddRef();
		shaders[0].Reset();
		Require(!shaders[0] && !shaders[0].get() && liveShaders == 4, "Reset must release only the cache's reference");
		cached[0]->Release();
		Require(liveShaders == 3, "The last reference must destroy the shader");
	}

	void CheckFailureAndReset()
	{
		Util::LazyShader<ID3D11ComputeShader> shader;
		const std::array<Define, 1> defines{ { { "BLUR_HORIZONTAL", nullptr } } };
		expectedDefines = defines;
		expectedTiming = nullptr;
		failCompile = true;
		const auto callsBefore = compileCalls;
		Require(!Request(shader, "BLUR_HORIZONTAL") && !shader && !shader.get(), "Failed compilation must return no shader");
		const auto allocationsBefore = allocations.load();
		failCompile = false;
		for (unsigned draw = 0; draw < 10000; ++draw)
			Require(!Request(shader, "BLUR_HORIZONTAL"), "Failure must remain latched even if the compiler recovers");
		Require(compileCalls == callsBefore + 1 && allocations.load() == allocationsBefore,
			"Failure-latched draws must neither retry nor allocate");
		shader.Reset();
		Require(Request(shader, "BLUR_HORIZONTAL") && compileCalls == callsBefore + 2, "Reset must permit recovery after failure");
		shader.Reset();
		Require(Request(shader, "BLUR_HORIZONTAL") && compileCalls == callsBefore + 3, "Reset must recompile a previously successful shader");
	}

	void CheckArgumentForms()
	{
		Util::LazyShader<ID3D11ComputeShader> shader;
		const std::vector<Define> defines{ { "FIRST", nullptr }, { "SECOND", "" }, { "THIRD", "42" } };
		expectedDefines = defines;
		expectedTiming = nullptr;
		Require(shader.Get(path, { { "FIRST", nullptr }, { "SECOND", "" }, { "THIRD", "42" } }, "cs_5_0", "main", resourceName),
			"Fixed macros must preserve order, null, empty and valued definitions");
		shader.Reset();
		Require(shader.Get(path, defines, "cs_5_0", "main", resourceName), "Dynamic vector callers must retain their existing behavior");
		const auto allocationsBefore = allocations.load();
		Require(shader.Get(path, defines, "cs_5_0") == shader.get(), "Dynamic callers must use the same cache");
		Require(allocations.load() == allocationsBefore, "Cached vector requests must not copy the vector");
		Require(shader.Get(path, std::vector<Define>(defines), "cs_5_0") == shader.get(), "Temporary vectors must use the same cache");
		Require(allocations.load() > allocationsBefore, "The allocation counter must detect caller-side temporary vectors");
		shader.Reset();
		expectedDefines = {};
		Require(shader.Get(path, {}, "cs_5_0"), "Empty macros and default entry must remain supported");
		shader.Reset();
		constexpr wchar_t alternatePath[] = L"Data\\Shaders\\AlternateLazyShaderTest.hlsl";
		expectedPath = alternatePath;
		expectedTarget = "cs_4_0";
		expectedEntry = "AlternateMain";
		Util::ShaderCompileTiming timing{};
		expectedTiming = &timing;
		auto* custom = shader.Get(alternatePath, {}, "cs_4_0", "AlternateMain", nullptr, &timing);
		Require(custom && static_cast<TestShader*>(custom)->Name().empty(), "An unnamed custom request must retain its optional name");
		Require(timing.bytecodeCompilationQpcTicks == 13 && timing.d3dObjectCreationQpcTicks == 17,
			"Custom requests must retain their timing destination");
		expectedPath = path;
		expectedTarget = "cs_5_0";
		expectedEntry = "main";
		expectedTiming = nullptr;
	}

	void CheckExceptions()
	{
		const std::array<Define, 1> defines{ { { "TEST", nullptr } } };
		const std::vector<Define> dynamicDefines(defines.begin(), defines.end());
		expectedDefines = defines;
		expectedTiming = nullptr;
		Util::LazyShader<ID3D11ComputeShader> allocationFailure;
		const auto callsBeforeAllocation = compileCalls;
		const auto errorsBeforeAllocation = loggedErrors;
		const auto allocationErrorsBefore = allocationErrors;
		failAllocation = true;
		const auto* failedAllocation = Request(allocationFailure, "TEST");
		failAllocation = false;
		Require(!failedAllocation && !allocationFailure && compileCalls == callsBeforeAllocation,
			"Cold macro allocation failure must return no shader without reaching the compiler");
		Require(loggedErrors == errorsBeforeAllocation + 1 && allocationErrors == allocationErrorsBefore + 1,
			"Cold allocation failure must report the actual exception once");
		const auto allocationsBefore = allocations.load();
		for (unsigned draw = 0; draw < 10000; ++draw) {
			Require(!Request(allocationFailure, "TEST"), "Allocation failure must stay latched for fixed macros");
			Require(!allocationFailure.Get(path, dynamicDefines, "cs_5_0"), "Allocation failure must stay latched for dynamic macros");
		}
		Require(compileCalls == callsBeforeAllocation && allocations.load() == allocationsBefore && loggedErrors == errorsBeforeAllocation + 1,
			"Allocation failure must not retry, allocate or log again during drawing");
		allocationFailure.Reset();
		Require(Request(allocationFailure, "TEST") && compileCalls == callsBeforeAllocation + 1,
			"Reset must permit recovery after allocation failure");

		for (const bool dynamicRequest : { false, true }) {
			std::array<Util::LazyShader<ID3D11ComputeShader>, 2> shaders;
			const auto callsBefore = compileCalls;
			const auto errorsBefore = loggedErrors;
			const auto compilerErrorsBefore = compilerErrors;
			throwCompile = true;
			for (auto& shader : shaders) {
				const auto* failed = dynamicRequest ? shader.Get(path, dynamicDefines, "cs_5_0") : Request(shader, "TEST");
				Require(!failed && !shader, "Compiler exceptions must return null and allow sibling preparation to continue");
			}
			throwCompile = false;
			Require(compileCalls == callsBefore + 2 && loggedErrors == errorsBefore + 2 && compilerErrors == compilerErrorsBefore + 2,
				"Each sibling must attempt compilation once and report its actual exception");
			const auto allocationsBeforeRetry = allocations.load();
			for (unsigned draw = 0; draw < 10000; ++draw) {
				for (auto& shader : shaders) {
					Require(!Request(shader, "TEST"), "A throwing compile must stay latched for fixed macros");
					Require(!shader.Get(path, dynamicDefines, "cs_5_0"), "A throwing compile must stay latched for dynamic macros");
				}
			}
			Require(compileCalls == callsBefore + 2 && allocations.load() == allocationsBeforeRetry && loggedErrors == errorsBefore + 2,
				"A throwing compile must not retry, allocate or log again during drawing");
			shaders[0].Reset();
			Require(Request(shaders[0], "TEST") && compileCalls == callsBefore + 3 && !Request(shaders[1], "TEST"),
				"Reset must recover only the requested shader after a throwing compile");
		}

		Util::LazyShader<ID3D11ComputeShader> unknownFailure;
		const auto unknownErrorsBefore = unknownErrors;
		throwUnknownCompile = true;
		Require(!Request(unknownFailure, "TEST"), "Non-standard compiler exceptions must return null");
		throwUnknownCompile = false;
		Require(unknownErrors == unknownErrorsBefore + 1 && !Request(unknownFailure, "TEST"),
			"Non-standard compiler exceptions must be reported and latched");
		unknownFailure.Reset();
		Require(Request(unknownFailure, "TEST"), "Reset must recover after a non-standard compiler exception");
	}

}

std::string Util::WStringToString(const std::wstring& value)
{
	std::string result;
	for (const auto character : value)
		result.push_back(static_cast<char>(character));
	return result;
}

ID3D11DeviceChild* Util::CompileShader(const wchar_t* shaderPath,
	const std::vector<Define>& defines, const char* target, const char* entry, ShaderCompileTiming* timing)
{
	++compileCalls;
	Require(std::wstring_view(shaderPath) == expectedPath && std::string_view(target) == expectedTarget && std::string_view(entry) == expectedEntry,
		"The compiler must receive the original path, target and entry");
	Require(defines.size() == expectedDefines.size(), "The compiler must receive every macro");
	for (size_t i = 0; i < defines.size(); ++i)
		Require(Equal(defines[i].first, expectedDefines[i].first) && Equal(defines[i].second, expectedDefines[i].second),
			"The compiler must receive unchanged macro names and values");
	Require(timing == expectedTiming, "The compiler must receive the original timing destination");
	if (throwCompile)
		throw std::runtime_error("Injected compiler exception");
	if (throwUnknownCompile)
		throw 1;
	if (timing) {
		timing->bytecodeCompilationQpcTicks += 13;
		timing->d3dObjectCreationQpcTicks += 17;
	}
	return failCompile ? nullptr : new TestShader;
}

int main()
{
	try {
		logger::set_default_logger(std::make_shared<spdlog::logger>("lazy_shader_test", std::make_shared<ErrorSink>()));
		CheckCachedRequests();
		Require(liveShaders == 0, "Cache destruction must release every shader");
		CheckFailureAndReset();
		CheckArgumentForms();
		CheckExceptions();
		Require(liveShaders == 0, "Reset and exception paths must not leak shaders");
		std::cout << "Lazy shader: zero cached/failed allocations; macros, timing, naming, reset and ownership passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
