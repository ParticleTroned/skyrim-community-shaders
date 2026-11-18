#pragma once

#include <cstdint>
#include <xbyak/xbyak.h>

namespace VRTaskQueue
{
	/** Reacquires the producer lock on normal return; the native pump cannot release it after an exception. */
	template <class Mutex, class Task>
	void RunOutsideLock(Mutex& a_mutex, Task& a_task)
	{
		a_mutex.unlock();
		a_task.Run();
		a_task.Dispose();
		a_mutex.lock();
	}

	/** Keeps the call inside the native pump's unwind range and existing shadow space. */
	class CallbackPatch : public Xbyak::CodeGenerator
	{
	public:
		explicit CallbackPatch(std::uintptr_t a_callback)
		{
			mov(rcx, rbx);
			mov(rax, a_callback);
			call(rax);
			nop(2);
		}
	};
}
