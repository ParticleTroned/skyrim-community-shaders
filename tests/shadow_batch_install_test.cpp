#include "EngineFixes/VRShadowBatchPolicy.h"
#include "Utils/VirtualFunctionHook.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace Test
{
	enum class Runtime
	{
		SE,
		AE,
		VR
	};
	struct Attachment
	{
		void** original;
		void* native;
		void* replacement;
	};
	struct State
	{
		Runtime runtime{ Runtime::VR };
		std::uint32_t version{ 0x010400F0 };
		std::size_t versionReads{};
		std::vector<std::uintptr_t> addresses;
		std::array<std::array<std::uint8_t, 16>, 8> code{};
		std::array<std::array<std::uint8_t, 16>, 7> trampolines{};
		std::vector<Attachment> attachments;
		std::string calls;
		long beginError{};
		long updateError{};
		std::size_t failedAttachment{};
		long commitError{};
		bool transaction{};
		bool published{};
		std::size_t successMessages{};
	} state;

	void Check(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition)
			throw std::runtime_error("installer assertion at line " + std::to_string(a_location.line()));
	}
	struct StartupFailure : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};
	[[noreturn]] void UnexpectedOperation(std::size_t a_operation)
	{
		throw std::runtime_error("installer invoked renderer operation " + std::to_string(a_operation));
	}
}

namespace RE
{
	struct BSBatchRenderer
	{
		struct PassGroup
		{
			std::array<std::byte, 0x30> storage{};
		};
	};
	struct BSRenderPass
	{
		std::array<std::byte, 0x30> storage{};
		BSRenderPass* passGroupNext{};
	};
}

namespace SKSE
{
	inline constexpr std::uint32_t RUNTIME_VR_1_4_15 = 0x010400F0;
}
namespace REL
{
	struct Module
	{
		static bool IsVR() { return Test::state.runtime == Test::Runtime::VR; }
		static Module& get()
		{
			static Module instance;
			return instance;
		}
		std::uint32_t version() const
		{
			++Test::state.versionReads;
			return Test::state.version;
		}
	};
	struct Offset
	{
		std::uintptr_t rva;
		explicit Offset(std::uintptr_t a_rva) : rva(a_rva) {}
		std::uintptr_t address() const
		{
			Test::state.addresses.push_back(rva);
			for (std::size_t i = 0; i < VRShadowBatch::kHookSites.size(); ++i) {
				if (rva == VRShadowBatch::kHookSites[i].rva)
					return reinterpret_cast<std::uintptr_t>(Test::state.code[i].data());
			}
			Test::Check(rva == VRShadowBatch::kContiguousGroups.rva);
			return reinterpret_cast<std::uintptr_t>(Test::state.code.back().data());
		}
	};
}
namespace logger
{
	template <class... Args>
	void critical(const char*, Args&&...)
	{}
	void info(const char*) { ++Test::state.successMessages; }
}
namespace stl
{
	[[noreturn]] void report_and_fail(const char* a_message) { throw Test::StartupFailure(a_message); }
}

long DetourTransactionBegin()
{
	Test::state.calls += 'B';
	if (Test::state.beginError)
		return Test::state.beginError;
	Test::Check(!Test::state.transaction);
	Test::state.transaction = true;
	return NO_ERROR;
}
long DetourUpdateThread(HANDLE a_thread)
{
	Test::Check(Test::state.transaction && a_thread == GetCurrentThread());
	Test::state.calls += 'U';
	return Test::state.updateError;
}
long DetourAttach(void** a_original, void* a_replacement)
{
	Test::Check(Test::state.transaction);
	Test::state.calls += 'A';
	Test::state.attachments.push_back({ a_original, *a_original, a_replacement });
	return Test::state.attachments.size() == Test::state.failedAttachment ? ERROR_INVALID_BLOCK : NO_ERROR;
}
long DetourTransactionAbort()
{
	Test::Check(Test::state.transaction && !Test::state.published);
	Test::state.calls += 'X';
	Test::state.transaction = false;
	return NO_ERROR;
}
long DetourTransactionCommit()
{
	Test::Check(Test::state.transaction && Test::state.attachments.size() == 7);
	Test::state.calls += 'C';
	Test::state.transaction = false;
	if (Test::state.commitError)
		return Test::state.commitError;
	for (std::size_t i = 0; i < Test::state.attachments.size(); ++i)
		*Test::state.attachments[i].original = Test::state.trampolines[i].data();
	Test::state.published = true;
	return NO_ERROR;
}

namespace VRShadowBatch
{
	using Renderer = RE::BSBatchRenderer;
	using Pass = RE::BSRenderPass;
	using Register = void (*)(Renderer*, Pass*, std::uint32_t);
	using Reset = void (*)(Renderer*);
	using RenderRange = void (*)(Renderer*, std::uint32_t, std::uint32_t, std::uint32_t);
	using RenderStep = bool (*)(Renderer*, std::uint32_t*, std::uint32_t*, void*, std::uint32_t);
	Register registerSorted{}, registerUnsorted{};
	Reset clearPasses{}, clearMap{}, destroy{};
	RenderRange renderRange{};
	RenderStep renderStep{};

	// Installation must publish addresses without invoking any renderer operation.
	void RegisterSorted(Renderer*, Pass*, std::uint32_t) { Test::UnexpectedOperation(0); }
	void RegisterUnsorted(Renderer*, Pass*, std::uint32_t) { Test::UnexpectedOperation(1); }
	template <Reset& Original>
	void Clear(Renderer*)
	{
		Test::UnexpectedOperation(&Original == &clearPasses ? 2 : 3);
	}
	void Destroy(Renderer*) { Test::UnexpectedOperation(6); }
	void RenderActiveRange(Renderer*, std::uint32_t, std::uint32_t, std::uint32_t) { Test::UnexpectedOperation(4); }
	bool RenderBatch(Renderer*, std::uint32_t*, std::uint32_t*, void*, std::uint32_t)
	{
		Test::UnexpectedOperation(5);
	}
}

#include "shadow_batch_install.h"

namespace Test
{
	auto OriginalSlots()
	{
		using namespace VRShadowBatch;
		return std::array{
			reinterpret_cast<void**>(&registerSorted), reinterpret_cast<void**>(&registerUnsorted),
			reinterpret_cast<void**>(&clearPasses), reinterpret_cast<void**>(&clearMap),
			reinterpret_cast<void**>(&renderRange), reinterpret_cast<void**>(&renderStep),
			reinterpret_cast<void**>(&destroy)
		};
	}
	auto Replacements()
	{
		using namespace VRShadowBatch;
		return std::array{
			reinterpret_cast<void*>(&RegisterSorted), reinterpret_cast<void*>(&RegisterUnsorted),
			reinterpret_cast<void*>(&Clear<clearPasses>), reinterpret_cast<void*>(&Clear<clearMap>),
			reinterpret_cast<void*>(&RenderActiveRange), reinterpret_cast<void*>(&RenderBatch),
			reinterpret_cast<void*>(&Destroy)
		};
	}
	void ResetFixture()
	{
		state = {};
		for (std::size_t i = 0; i < VRShadowBatch::kHookSites.size(); ++i)
			state.code[i] = VRShadowBatch::kHookSites[i].prefix;
		state.code.back() = VRShadowBatch::kContiguousGroups.prefix;
		for (auto slot : OriginalSlots())
			*slot = nullptr;
	}
	void ExpectFailure()
	{
		try {
			VRShadowBatch::Install();
		} catch (const StartupFailure&) {
			Check(!state.published && !state.transaction && state.successMessages == 0);
			return;
		}
		Check(false);
	}
	void CheckOriginals(bool a_published)
	{
		const auto slots = OriginalSlots();
		for (std::size_t i = 0; i < slots.size(); ++i)
			Check(*slots[i] == (a_published ? state.trampolines[i].data() : state.code[i].data()));
	}
	void CheckAttachments()
	{
		const auto slots = OriginalSlots();
		const auto replacements = Replacements();
		Check(state.attachments.size() <= slots.size());
		for (std::size_t i = 0; i < state.attachments.size(); ++i) {
			Check(state.attachments[i].original == slots[i]);
			Check(state.attachments[i].native == state.code[i].data());
			Check(state.attachments[i].replacement == replacements[i]);
		}
	}
	void RuntimeAndByteAdmission()
	{
		for (const auto runtime : { Runtime::SE, Runtime::AE }) {
			ResetFixture();
			state.runtime = runtime;
			VRShadowBatch::Install();
			Check(state.addresses.empty() && state.calls.empty() && state.versionReads == 0);
			Check(!state.published && state.successMessages == 0);
			for (auto slot : OriginalSlots())
				Check(*slot == nullptr);
		}
		ResetFixture();
		state.version = 0;
		ExpectFailure();
		Check(state.addresses.empty() && state.calls.empty() && state.versionReads == 1);
		for (std::size_t site = 0; site < state.code.size(); ++site) {
			for (std::size_t byte = 0; byte < state.code[site].size(); ++byte) {
				ResetFixture();
				state.code[site][byte] ^= 1;
				ExpectFailure();
				Check(state.calls.empty() && state.attachments.empty() && state.addresses.size() == site + 1);
				for (auto slot : OriginalSlots())
					Check(*slot == nullptr);
			}
		}
	}
	void SuccessAndFailures()
	{
		ResetFixture();
		VRShadowBatch::Install();
		Check(state.calls == "BUAAAAAAAC" && state.published && !state.transaction && state.successMessages == 1);
		Check(state.addresses.size() == 15 && state.versionReads == 1);
		CheckAttachments();
		CheckOriginals(true);

		ResetFixture();
		state.beginError = ERROR_INVALID_OPERATION;
		ExpectFailure();
		Check(state.calls == "B" && state.attachments.empty());
		CheckOriginals(false);

		ResetFixture();
		state.updateError = ERROR_ACCESS_DENIED;
		ExpectFailure();
		Check(state.calls == "BUX" && state.attachments.empty());
		CheckOriginals(false);

		for (std::size_t failure = 1; failure <= 7; ++failure) {
			ResetFixture();
			state.failedAttachment = failure;
			ExpectFailure();
			Check(state.calls == "BU" + std::string(failure, 'A') + "X");
			Check(state.attachments.size() == failure);
			CheckAttachments();
			CheckOriginals(false);
		}
		for (const auto error : { ERROR_INVALID_DATA, ERROR_INVALID_OPERATION }) {
			ResetFixture();
			state.commitError = error;
			ExpectFailure();
			Check(state.calls == "BUAAAAAAAC" && state.attachments.size() == 7);
			CheckAttachments();
			CheckOriginals(false);
		}
	}
}

int main()
{
	try {
		Test::RuntimeAndByteAdmission();
		Test::SuccessAndFailures();
		std::cout << "production shadow batch installer admission and transaction tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
