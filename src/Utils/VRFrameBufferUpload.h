#pragma once

#include <cstdint>
#include <xbyak/xbyak.h>

namespace Util
{
	/** @brief Pass the validated engine upload's CPU source to an Unmap observer. */
	struct VRFrameBufferUploadThunk : Xbyak::CodeGenerator
	{
		explicit VRFrameBufferUploadThunk(std::uintptr_t observer)
		{
			// The caller stores its Map result at rsp+0x40 and upload bytes at rsp+0x50.
			// Our return address adds eight bytes; a failed Map supplies no source.
			mov(rax, ptr[rsp + 0x48]);
			lea(r9, ptr[rsp + 0x58]);
			test(rax, rax);
			cmovz(r9, rax);
			mov(rax, observer);
			jmp(rax);
		}
	};
}
