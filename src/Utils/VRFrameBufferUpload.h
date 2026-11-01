#pragma once

#include <cstdint>
#include <xbyak/xbyak.h>

namespace Util
{
	/** @brief Pass the validated mapped GPU destination to an Unmap observer. */
	struct VRFrameBufferUploadThunk : Xbyak::CodeGenerator
	{
		explicit VRFrameBufferUploadThunk(std::uintptr_t observer)
		{
			// The caller stores Map.pData at rsp+0x40. Our return address adds
			// eight bytes; a failed Map supplies a null destination.
			mov(rax, ptr[rsp + 0x48]);
			mov(r9, rax);
			test(rax, rax);
			cmovz(r9, rax);
			mov(rax, observer);
			jmp(rax);
		}
	};
}
