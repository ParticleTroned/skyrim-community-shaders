#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
namespace logger = spdlog;
#include "../src/Utils/NvidiaBindingWorkaround.cpp"

using namespace Util::NvidiaBindingWorkaround;

namespace
{
	std::vector<std::byte> manager(0x120), context(0x22000), oldObject(0x100), newObject(0x100);
	std::uintptr_t oldWrapper{}, newWrapper{};
	ContextLayout fixtureLayout{ 0xc880, 0xc888, 0x21e48, 0x21e50 };
	int firstCalls{}, secondCalls{};
	std::int32_t firstResult{}, secondResult{};
	std::uint64_t forwardedRequest{};
	std::function<void()> afterFirst, afterSecond;
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	std::uintptr_t Ptr(auto& value) { return reinterpret_cast<std::uintptr_t>(value.data()); }
	template <class T>
	void Put(std::uintptr_t pointer, std::size_t offset, T value)
	{
		std::memcpy(reinterpret_cast<void*>(pointer + offset), &value, sizeof(value));
	}
	std::int32_t First(std::uintptr_t m, std::uintptr_t c, std::uint64_t request)
	{
		++firstCalls;
		forwardedRequest = request;
		Require(m == Ptr(manager) && c == Ptr(context), "native arguments changed");
		if (request > Policy::OldManagedCapacity && request <= Policy::ReplacementBytes && firstResult >= 0) {
			Put(m, 0, reinterpret_cast<std::uintptr_t>(&newWrapper));
			Put(m, 0x100, Policy::ReplacementBytes);
			Put(c, fixtureLayout.firstCache, Policy::ReplacementBytes);
		}
		if (afterFirst)
			afterFirst();
		SetLastError(79);
		return firstResult;
	}
	std::int32_t Second(std::uintptr_t m, std::uintptr_t c, std::uint64_t request)
	{
		++secondCalls;
		Require(m == Ptr(manager) && c == Ptr(context) && request == forwardedRequest, "refresh arguments changed");
		Require(inside, "refresh escaped native call scope");
		if (secondResult >= 0)
			Put(c, fixtureLayout.secondCache, Policy::ReplacementBytes);
		if (afterSecond)
			afterSecond();
		SetLastError(87);
		return secondResult;
	}
	void Objects()
	{
		for (auto* value : { &manager, &context, &oldObject, &newObject })
			std::fill(value->begin(), value->end(), std::byte{});
		oldWrapper = Ptr(oldObject);
		newWrapper = Ptr(newObject);
		Put(Ptr(manager), 0, reinterpret_cast<std::uintptr_t>(&oldWrapper));
		Put(Ptr(manager), 0x100, Policy::OldManagedCapacity);
		Put(Ptr(oldObject), 0x40, Policy::OldAllocationBytes);
		Put(Ptr(oldObject), 0x48, std::uint64_t{ 0x100000000 });
		Put(Ptr(oldObject), 0x90, std::uint32_t{ 11 });
		Put(Ptr(newObject), 0x40, Policy::ReplacementBytes);
		Put(Ptr(newObject), 0x48, std::uint64_t{ 0x120000000 });
		Put(Ptr(newObject), 0x90, std::uint32_t{ 12 });
		Put(Ptr(context), fixtureLayout.firstCache, Policy::OldManagedCapacity);
		Put(Ptr(context), fixtureLayout.secondCache, Policy::OldManagedCapacity);
		Put(Ptr(context), fixtureLayout.primaryManager, Ptr(manager));
		Put(Ptr(context), fixtureLayout.secondManager, Ptr(manager));
	}
	void Reset(const DriverProfile& profile = DriverProfiles.back())
	{
		fixtureLayout = profile.version == "616.92" ? ContextLayout{ 0xc850, 0xc858, 0x21e28, 0x21e30 } : ContextLayout{ 0xc880, 0xc888, 0x21e48, 0x21e50 };
		Objects();
		auto& state = Get();
		state.profile = &profile;
		state.first = First;
		state.second = Second;
		state.initialized = state.installed = true;
		state.mode = Refresh;
		state.reason = "armed";
		state.failure = nullptr;
		state.lastSkipped = "none";
		state.applied = state.candidates = state.rejected = 0;
		state.skipped = 0;
		state.loggingFailures = state.installationError = 0;
		firstCalls = secondCalls = firstResult = secondResult = 0;
		afterFirst = afterSecond = {};
		inside = false;
	}
	std::int32_t Run(std::uint64_t request = Policy::ReplacementBytes)
	{
		return Grow(Ptr(manager), Ptr(context), request);
	}
	void CheckCodeVerification()
	{
		const auto path = std::filesystem::temp_directory_path() /
		                  (std::wstring(L"csx-binding-code-") + std::to_wstring(GetCurrentProcessId()) + L".tmp");
		winrt::file_handle file(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
		Require(static_cast<bool>(file), "could not create owned code fixture");
		std::array<std::byte, 32> disk{};
		for (std::size_t i = 0; i < disk.size(); ++i) disk[i] = static_cast<std::byte>(i);
		DWORD written{};
		Require(WriteFile(file.get(), disk.data(), static_cast<DWORD>(disk.size()), &written, nullptr) && written == disk.size(), "code fixture write failed");
		IMAGE_NT_HEADERS64 image{};
		image.OptionalHeader.SizeOfImage = 64;
		std::array<IMAGE_SECTION_HEADER, 1> sections{};
		sections[0].Characteristics = IMAGE_SCN_MEM_EXECUTE;
		sections[0].VirtualAddress = 16;
		sections[0].PointerToRawData = 8;
		sections[0].SizeOfRawData = 16;
		std::array code{ CodeRange{ 16, 8 } };
		Require(ReadExpectedCode(file.get(), image, sections, code[0]), "valid code rejected");
		Require(std::memcmp(code[0].expected.data(), disk.data() + 8, 8) == 0, "incorrect RVA to raw translation");
		sections[0].Characteristics = 0;
		Require(!ReadExpectedCode(file.get(), image, sections, code[0]), "nonexecutable section accepted");
		sections[0].Characteristics = IMAGE_SCN_MEM_EXECUTE;
		CodeRange invalid{ 16, 513 };
		Require(!ReadExpectedCode(file.get(), image, sections, invalid), "oversized code accepted");
		invalid = CodeRange{ 60, 8 };
		Require(!ReadExpectedCode(file.get(), image, sections, invalid), "image overrun accepted");
		invalid = CodeRange{ 28, 8 };
		Require(!ReadExpectedCode(file.get(), image, sections, invalid), "section overrun accepted");
		sections[0].PointerToRawData = 30;
		Require(!ReadExpectedCode(file.get(), image, sections, code[0]), "short disk read accepted");
		file.close();
		std::array<std::byte, 64> live{};
		std::copy_n(disk.data() + 8, 8, code[0].expected.data());
		std::copy_n(code[0].expected.data(), 8, live.data() + 16);
		Require(MatchesCode(Ptr(live), code), "memory verification depended on closed file");
		live[18] = std::byte{ 255 };
		Require(!MatchesCode(Ptr(live), code), "modified native code accepted");
		Require(!MatchesCode(UINTPTR_MAX - 8, code), "overflowing module address accepted");
		Require(!MatchesCode(Ptr(live), {}), "empty verification accepted");
	}
	void CheckProfiles()
	{
		Require(!FindDriverProfile({}) && !FindDriverProfile("621.24"), "non-hash identity accepted");
		for (const auto& profile : DriverProfiles) {
			Require(FindDriverProfile(profile.sha256) == &profile, "exact driver pin not selected");
			auto altered = std::string(profile.sha256);
			altered.back() = altered.back() == '0' ? '1' : '0';
			Require(!FindDriverProfile(altered), "modified driver binary accepted");
			Reset(profile);
			Require(Run() == 0 && secondCalls == 1 && Status().applied == 1, "profile used incorrect context layout");
			Require(GetLastError() == 79, "profile refresh changed native last error");
			Reset(profile);
			Put(Ptr(context), fixtureLayout.secondManager, std::uintptr_t{ 1 });
			Run();
			Require(secondCalls == 0 && std::string_view(Status().lastSkipped) == "target_second_manager_mismatch", "profile accepted unrelated second manager");
			Reset(profile);
			afterFirst = [] { Put(Ptr(context), fixtureLayout.secondCache, Policy::ReplacementBytes); };
			Run();
			Require(secondCalls == 0 && Status().enabled, "profile duplicated a native refresh");
		}
	}
	void CheckRefreshRecords()
	{
		Reset();
		std::ostringstream output;
		auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
		auto previous = spdlog::default_logger();
		spdlog::set_default_logger(std::make_shared<spdlog::logger>("binding-test", sink));
		spdlog::set_level(spdlog::level::info);
		Require(Select(false), "could not select logging control");
		Run();
		Require(output.str().empty(), "control emitted an info record");
		Reset();
		Put(Ptr(context), fixtureLayout.secondCache, std::uint64_t{});
		Run();
		Require(output.str().empty(), "skipped refresh emitted an info record");
		Reset();
		constexpr int refreshes = 35;
		for (int i = 0; i < refreshes; ++i) {
			Objects();
			Run();
		}
		Require(!Status().recordsLimited && Status().enabled && Status().applied == refreshes, "logging disabled subsequent refresh");
		Objects();
		firstResult = -1;
		Require(Run() == -1 && !Status().enabled, "terminal rejection changed forwarding");
		const auto captured = output.str();
		spdlog::set_default_logger(std::move(previous));
		spdlog::set_level(spdlog::level::off);
		Require(std::count(captured.begin(), captured.end(), '\n') == refreshes, "each refresh did not emit exactly one info record");
		Require(captured.find("event=refreshed driver=621.24") != std::string::npos, "record omitted selected driver");
		for (int i = 1; i <= refreshes; ++i)
			Require(captured.find("refreshCount=" + std::to_string(i) + " ") != std::string::npos, "record omitted cumulative refresh count");
		Require(captured.find("request=" + std::to_string(Policy::ReplacementBytes)) != std::string::npos &&
					captured.find("secondBefore=" + std::to_string(Policy::OldManagedCapacity)) != std::string::npos,
			"record omitted requested growth or prior cache state");
		Require(captured.find("oldBytes=" + std::to_string(Policy::OldAllocationBytes)) != std::string::npos,
			"record confused allocation bytes with managed capacity");
	}
	void CheckLoggingFailure()
	{
		class ThrowingSink final : public spdlog::sinks::base_sink<std::mutex>
		{
			void sink_it_(const spdlog::details::log_msg&) override { throw std::runtime_error("injected logging failure"); }
			void flush_() override {}
		};
		Reset();
		auto previous = spdlog::default_logger();
		auto failing = std::make_shared<spdlog::logger>("binding-failure-test", std::make_shared<ThrowingSink>());
		failing->set_level(spdlog::level::info);
		failing->set_error_handler([](const std::string& message) { throw std::runtime_error(message); });
		spdlog::set_default_logger(failing);
		const auto first = Run();
		Objects();
		const auto second = Run();
		const auto error = GetLastError();
		spdlog::set_default_logger(std::move(previous));
		Require(first == 0 && second == 0 && error == 79, "logging failure changed native results");
		Require(Status().enabled && Status().applied == 2 && Status().loggingFailures == 2,
			"logging failure disabled later corrections or was lost");
	}

}

int main()
try {
	spdlog::set_level(spdlog::level::off);
	CheckCodeVerification();
	CheckProfiles();
	CheckRefreshRecords();
	CheckLoggingFailure();
	Reset();
	Get().initialized = Get().installed = false;
	Disable("early_shutdown");
	Require(!Status().enabled && !Select(false) && !(Get().mode.load() & Stopped), "pre-install lifecycle was poisoned or reported enabled");
	Require(!GetModuleHandleW(L"nvwgf2umx.dll"), "driver unexpectedly loaded in isolated fixture");
	Initialize();
	Require(!Status().installed && !Status().enabled && !Select(true) && std::string_view(Status().reason) == "driver_verification_or_install_failed" &&
				Status().installationError == ERROR_MOD_NOT_FOUND,
		"missing driver was accepted or mislabeled");
	Reset();
	Require(Run() == 0 && firstCalls == 1 && secondCalls == 1 && Status().applied == 1, "eligible refresh failed");
	Require(GetLastError() == 79 && !inside, "native error/reentrancy state was not restored");
	Require(!Select(false), "mode changed after replacement");
	Run();
	Require(secondCalls == 1, "already-grown manager refreshed again");
	Objects();
	Run();
	Require(secondCalls == 2 && Status().applied == 2, "global once flag blocked another replacement");
	for (std::uint64_t i = 0; i < 32; ++i) {
		Objects();
		Run();
	}
	Require(!Status().recordsLimited && Status().enabled && Status().applied == 34, "logging changed intervention");
	Reset();
	firstResult = 7;
	Require(Run(Policy::OldManagedCapacity + 1) == 7, "original positive result was changed");
	Require(secondCalls == 1 && forwardedRequest == Policy::OldManagedCapacity + 1, "growth request was rounded or skipped");
	Require(GetLastError() == 79, "refresh changed native last error");

	Reset();
	Require(Select(false), "control selection failed");
	Run();
	Require(firstCalls == 1 && secondCalls == 0 && Status().candidates == 1 && Status().locked, "control altered native growth");

	Reset();
	Run(Policy::OldManagedCapacity);
	Require(firstCalls == 1 && secondCalls == 0 && !Status().locked, "small request entered workaround");
	Reset();
	Run(Policy::ReplacementBytes + 1);
	Require(firstCalls == 1 && secondCalls == 0 && !Status().locked, "unsupported growth entered workaround");

	Reset();
	Put(Ptr(context), 0xc888, std::uint64_t{});
	Run();
	Require(secondCalls == 0 && !Status().locked, "inactive second path was initialized");
	Require(Status().skipped == 1 && std::string_view(Status().lastSkipped) == "target_second_cache_mismatch", "missing inactive-path rejection");
	Reset();
	Put(Ptr(context), 0x21e50, std::uintptr_t{ 1 });
	Run();
	Require(secondCalls == 0 && !Status().locked, "unrelated manager was refreshed");
	Reset();
	Put(Ptr(oldObject), 0x90, std::uint32_t{});
	Run();
	Require(secondCalls == 0, "missing allocation identity was accepted");

	Reset();
	firstResult = -1;
	Require(Run() == -1 && secondCalls == 0 && !Status().enabled, "failed original call was replaced");
	Reset();
	secondResult = -2;
	Require(Run() == 0 && secondCalls == 1 && Status().applied == 0 && !Status().enabled, "failed refresh overwrote original result");
	Require(std::string_view(Status().reason) == "native_refresh_failed", "missing native failure reason");

	Reset();
	afterFirst = [] { Put(Ptr(context), 0xc888, Policy::ReplacementBytes); };
	Run();
	Require(secondCalls == 0 && Status().enabled && std::string_view(Status().reason) == "already_current", "native refresh was duplicated");
	Reset();
	afterFirst = [] { Put(Ptr(newObject), 0x48, std::uint64_t{ 0x100000000 }); };
	Run();
	Require(secondCalls == 0 && !Status().enabled, "unchanged backing address was accepted");
	Require(std::string_view(Status().reason) == "address_not_replaced", "missing address rejection");
	Reset();
	afterFirst = [] { Put(Ptr(context), 0x21e50, std::uintptr_t{ 1 }); };
	Run();
	Require(secondCalls == 0 && std::string_view(Status().reason) == "after_second_manager_mismatch", "replacement manager mismatch was accepted");
	Reset();
	afterFirst = [] { Put(Ptr(context), 0xc880, Policy::OldManagedCapacity); };
	Run();
	Require(secondCalls == 0 && std::string_view(Status().reason) == "first_cache_not_current", "stale first cache was accepted");
	Reset();
	afterSecond = [] { Put(Ptr(context), 0xc888, Policy::OldManagedCapacity); };
	Run();
	Require(Status().applied == 0 && std::string_view(Status().reason) == "second_cache_not_updated", "missing refresh was reported applied");
	Reset();
	afterSecond = [] { Put(Ptr(newObject), 0x90, std::uint32_t{ 13 }); };
	Run();
	Require(Status().applied == 0 && !Status().enabled, "allocation change during refresh went unnoticed");
	Require(std::string_view(Status().reason) == "handle_changed", "missing identity rejection");

	Reset();
	afterFirst = [] { oldWrapper = 0; std::fill(oldObject.begin(), oldObject.end(), std::byte{}); };
	Run();
	Require(Status().applied == 1, "retired old object was read after growth");
	Reset();
	afterFirst = [] { Disable("shutdown"); };
	Run();
	Require(secondCalls == 0 && std::string_view(Status().reason) == "shutdown", "shutdown was ignored or overwritten");
	Disable("later_failure");
	Require(std::string_view(Status().reason) == "shutdown", "first failure was overwritten");
	Reset();
	afterFirst = [] { std::thread stop([] { Disable("concurrent_stop"); }); stop.join(); };
	Run();
	Require(secondCalls == 0 && std::string_view(Status().reason) == "concurrent_stop", "cross-thread stop before refresh was ignored");

	Reset();
	bool nested = false;
	afterFirst = [&] { if (!nested) { nested = true; Run(); } };
	Run();
	Require(firstCalls == 2 && secondCalls == 1 && !inside, "reentrant native call was reinstrumented");

	Reader invalid;
	(void)invalid.At<std::uint64_t>(1);
	Require(!invalid.valid, "unreadable state was accepted");
	std::cout << "NvidiaBindingWorkaround: code verification, lifecycle, identity, native forwarding, guards, repeats, control and reentrancy passed\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
