#include "VRTaskQueueFix.h"

#include "EngineFixes/VRTaskQueuePolicy.h"
#include <Psapi.h>
#include <array>
#include <cstring>
#include <mutex>
#include <span>

namespace
{
	constexpr std::uintptr_t kPumpRVA = 0x220B0;
	constexpr std::uintptr_t kCallbackRVA = 0x2214B;
	constexpr std::uintptr_t kQueueLockRVA = 0x161348;
	constexpr std::uintptr_t kConsumerLockReferenceRVA = 0x220CA;
	constexpr std::uintptr_t kProducerLockReferenceRVA = 0x22199;
	constexpr std::uintptr_t kExitLockReferenceRVA = 0x22172;
	constexpr std::array<std::uint8_t, 17> kCallbackCode{
		0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x10, 0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x08
	};
	constexpr std::array<std::uint8_t, 26> kPumpCode{
		0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x05, 0x5D, 0xBA, 0x13, 0x00, 0x48,
		0x05, 0x00, 0xA1, 0x5C, 0x00, 0x48, 0x89, 0x05, 0x88, 0x17, 0x14, 0x00, 0xFF, 0xD0
	};
	constexpr std::array<std::uint8_t, 13> kConsumerLockCode{ 0x48, 0x8D, 0x0D, 0x77, 0xF2, 0x13, 0x00, 0xFF, 0x15, 0x91, 0x3F, 0x0C, 0x00 };
	constexpr std::array<std::uint8_t, 13> kProducerLockCode{ 0x48, 0x8D, 0x0D, 0xA8, 0xF1, 0x13, 0x00, 0xFF, 0x15, 0xC2, 0x3E, 0x0C, 0x00 };
	constexpr std::array<std::uint8_t, 18> kExitLockCode{
		0x48, 0x8D, 0x0D, 0xCF, 0xF1, 0x13, 0x00, 0x48, 0x83, 0xC4, 0x48, 0x48, 0xFF, 0x25, 0x64, 0x3F, 0x0C, 0x00
	};
	constexpr std::array<std::uint8_t, 10> kResumeCode{ 0x4C, 0x8B, 0x05, 0xD5, 0xF1, 0x13, 0x00, 0x4D, 0x85, 0xC0 };

	struct QueueLock
	{
		CRITICAL_SECTION* section = nullptr;
		void lock() const { EnterCriticalSection(section); }
		void unlock() const { LeaveCriticalSection(section); }
		bool try_lock() const { return TryEnterCriticalSection(section) != FALSE; }
	} queueLock;
	bool installed = false;

	void RunTask(SKSE::Impl::TaskDelegate* a_task)
	{
		VRTaskQueue::RunOutsideLock(queueLock, *a_task);
	}

	bool Matches(std::uintptr_t a_base, std::uintptr_t a_rva, std::span<const std::uint8_t> a_code)
	{
		std::array<std::uint8_t, 32> actual{};
		SIZE_T read = 0;
		return a_code.size() <= actual.size() &&
		       ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(a_base + a_rva), actual.data(), a_code.size(), &read) &&
		       read == a_code.size() && std::memcmp(actual.data(), a_code.data(), a_code.size()) == 0;
	}
}

bool VRTaskQueueFix::TryInstall()
{
	if (!REL::Module::IsVR() || REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15)
		return false;
	if (installed)
		return true;
	const auto module = GetModuleHandleW(L"sksevr_1_4_15.dll");
	MODULEINFO info{};
	if (!module || !GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) ||
		info.SizeOfImage < kQueueLockRVA + sizeof(CRITICAL_SECTION)) {
		logger::warn("[Engine Fixes] SKSE VR task queue image unavailable; retaining its task pump");
		return false;
	}
	const auto base = reinterpret_cast<std::uintptr_t>(module);
	if (!Matches(base, kPumpRVA, kPumpCode) || !Matches(base, kCallbackRVA, kCallbackCode) ||
		!Matches(base, kConsumerLockReferenceRVA, kConsumerLockCode) || !Matches(base, kProducerLockReferenceRVA, kProducerLockCode) ||
		!Matches(base, kExitLockReferenceRVA, kExitLockCode) || !Matches(base, kCallbackRVA + kCallbackCode.size(), kResumeCode)) {
		logger::warn("[Engine Fixes] Unsupported or modified SKSE VR task queue; no callback hook installed");
		return false;
	}

	QueueLock candidate{ reinterpret_cast<CRITICAL_SECTION*>(base + kQueueLockRVA) };
	const std::unique_lock initializationLock{ candidate, std::try_to_lock };
	if (!initializationLock.owns_lock() || candidate.section->RecursionCount != 1) {
		logger::warn("[Engine Fixes] SKSE VR task queue is active; no callback patch installed");
		return false;
	}
	MEMORY_BASIC_INFORMATION originalPage{};
	if (VirtualQuery(reinterpret_cast<const void*>(base + kCallbackRVA), &originalPage, sizeof(originalPage)) != sizeof(originalPage)) {
		logger::warn("[Engine Fixes] SKSE VR task callback page unavailable; no patch installed");
		return false;
	}
	queueLock = candidate;
	VRTaskQueue::CallbackPatch code(reinterpret_cast<std::uintptr_t>(RunTask));
	code.ready();
	if (code.getSize() != kCallbackCode.size() ||
		!REL::safe_write(base + kCallbackRVA, code.getCode(), code.getSize(), kCallbackCode)) {
		logger::error("[Engine Fixes] SKSE VR task callback patch was rejected");
		return false;
	}
	const std::span appliedCode{ code.getCode(), code.getSize() };
	MEMORY_BASIC_INFORMATION appliedPage{};
	if (!Matches(base, kCallbackRVA, appliedCode)) {
		if (Matches(base, kCallbackRVA, kCallbackCode)) {
			logger::error("[Engine Fixes] SKSE VR task callback write did not apply");
			return false;
		}
		stl::report_and_fail("The SKSE VR task callback patch is inconsistent. See CSX.log.");
	}
	if (VirtualQuery(reinterpret_cast<const void*>(base + kCallbackRVA), &appliedPage, sizeof(appliedPage)) != sizeof(appliedPage) ||
		appliedPage.Protect != originalPage.Protect ||
		!FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(base + kCallbackRVA), code.getSize())) {
		stl::report_and_fail("The SKSE VR task callback patch could not be finalized safely. See CSX.log.");
	}
	installed = true;
	return true;
}
