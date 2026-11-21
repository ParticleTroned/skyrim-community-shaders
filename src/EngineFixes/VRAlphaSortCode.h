#pragma once

#include <cstdint>
#include <xbyak/xbyak.h>

namespace VRAlphaSort
{
	enum class KeySite
	{
		Group,
		Peek,
		Consume
	};

	/** Preserve each native site's register ABI while replacing only its depth arithmetic. */
	class KeyCode final : public Xbyak::CodeGenerator
	{
	public:
		KeyCode(KeySite site, std::uintptr_t helper, std::uintptr_t continuation, std::uintptr_t groupCount) : CodeGenerator(512)
		{
			pushfq();
			for (auto reg : { rax, rcx, rdx, r8, r9, r10, r11 })
				push(reg);
			// Peek is a leaf entered with an unadjusted return address on the stack.
			const int stack = site == KeySite::Peek ? 0x98 : 0x90;
			sub(rsp, stack);
			for (int i = 0; i < 6; ++i)
				movdqu(ptr[rsp + 0x20 + i * 16], Xbyak::Xmm(i));
			if (site == KeySite::Group) {
				mov(rcx, rsi);
				mov(rdx, rdi);
				movzx(r8d, r15b);
			} else {
				lea(rcx, ptr[rax + 0xE4]);
				if (site == KeySite::Consume)
					mov(rdx, r15);
				movaps(xmm2, site == KeySite::Peek ? xmm3 : xmm8);
			}
			mov(rax, helper);
			call(rax);
			movss(ptr[rsp + 0x80], xmm0);
			for (int i = 0; i < 6; ++i)
				movdqu(Xbyak::Xmm(i), ptr[rsp + 0x20 + i * 16]);
			movss(site == KeySite::Consume ? xmm6 : xmm2, ptr[rsp + 0x80]);
			add(rsp, stack);
			for (auto reg : { r11, r10, r9, r8, rdx, rcx, rax })
				pop(reg);
			popfq();
			if (site == KeySite::Peek) {
				mov(rax, groupCount);
				mov(eax, dword[rax]);
			}
			jmp(ptr[rip]);
			dq(continuation);
			readyRE();
		}
	};
}
