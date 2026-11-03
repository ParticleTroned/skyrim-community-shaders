#pragma once

#include <cstdint>
#include <xbyak/xbyak.h>

namespace Util
{
	/** @brief Pass the native CPU source and write-only mapped destination to an Unmap observer. */
	struct VRFrameBufferUploadThunk : Xbyak::CodeGenerator
	{
		explicit VRFrameBufferUploadThunk(std::uintptr_t observer)
		{
			// The return address shifts Map.pData and the intact CPU source by eight bytes.
			// The fifth argument reuses the caller's expired MapFlags stack slot.
			mov(rax, ptr[rsp + 0x48]);
			lea(r9, ptr[rsp + 0x58]);
			test(rax, rax);
			cmovz(r9, rax);
			mov(ptr[rsp + 0x28], rax);
			mov(rax, observer);
			jmp(rax);
		}
	};
}
