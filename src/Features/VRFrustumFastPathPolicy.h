#pragma once
#include "Utils/VRNativeFrustum.h"
#include <bit>
#include <immintrin.h>

#ifdef _MSC_VER
#	pragma float_control(precise, on, push)
#	pragma fp_contract(off)
#endif

namespace VRFrustumFastPath
{
	inline constexpr std::uint32_t StepLimit = 128;
	struct Result
	{
		bool handled = false, accepted = false;
		std::uint32_t tests = 0;
	};
	inline bool NormalOrZero(std::uint32_t bits) noexcept
	{
		const auto magnitude = bits & 0x7FFFFFFFu;
		return magnitude == 0 || (magnitude >= 0x00800000u && magnitude < 0x7F800000u);
	}
	/** Match the native scalar multiply/add order without FMA or reassociation. */
	inline float Distance(const std::array<std::uint32_t, 4>& plane, const std::array<std::uint32_t, 4>& bound) noexcept
	{
		const auto scalar = [](std::uint32_t bits) { return _mm_set_ss(std::bit_cast<float>(bits)); };
		const auto x = _mm_mul_ss(scalar(plane[0]), scalar(bound[0]));
		const auto y = _mm_mul_ss(scalar(plane[1]), scalar(bound[1]));
		const auto z = _mm_mul_ss(scalar(plane[2]), scalar(bound[2]));
		return _mm_cvtss_f32(_mm_sub_ss(_mm_add_ss(_mm_add_ss(y, x), z), scalar(plane[3])));
	}
	/** Resolve only complete, side-effect-free first-plane paths; otherwise defer to native. */
	template <class Read, class Observe>
	Result Evaluate(std::uintptr_t owner, std::uintptr_t object, Read&& read, Observe&& observe)
	{
		using namespace VRNativeFrustum;
		// Speculation must not raise unmasked exceptions or change rounding behavior.
		if ((_mm_getcsr() & 0x7F80u) != 0x1F80u)
			return {};
		std::array<std::uint32_t, 4> bound;
		if (!ReadBound(object, bound, read) || bound[3] < 0x00800000u || bound[3] >= 0x7F800000u)
			return {};
		for (auto bits : bound)
			if (!NormalOrZero(bits))
				return {};
		const auto header = ReadHeader(owner, read);
		if (!header.valid || header.prethreaded != 1 || !header.operatorCount)
			return {};
		auto cursor = header.firstOp;
		for (std::uint32_t tests = 0; tests <= StepLimit; ++tests) {
			NativeOperator op;
			if (!ReadInstruction(header, cursor, op, read))
				return {};
			if (op.opcode == 2 || op.opcode == 3) {
				std::array<std::uint32_t, 4> endBound;
				if (!tests || ReadHeader(owner, read) != header || !ReadBound(object, endBound, read) || endBound != bound)
					return {};
				return { true, op.opcode == 2, tests };
			}
			NativeOperator operand;
			std::uintptr_t address;
			if (tests == StepLimit || op.opcode != 8 || !ReadOperator(header, cursor + 1, operand, read) ||
				operand.opcode >= header.planeCount || !ArrayAddress(header.planes, operand.opcode, 0x70, address))
				return {};
			std::uint32_t mask;
			if (!read(address + 0x60, &mask, sizeof(mask)) || !mask || (mask & ~63u))
				return {};
			std::array<std::uint32_t, 4> plane;
			if (!read(address + 16 * std::countr_zero(mask), plane.data(), sizeof(plane)))
				return {};
			for (auto bits : plane)
				if (!NormalOrZero(bits))
					return {};
			const float distance = Distance(plane, bound);
			if (!NormalOrZero(std::bit_cast<std::uint32_t>(distance)) || !(distance < std::bit_cast<float>(bound[3])))
				return {};
			observe(address + 0x60, mask);
			cursor = op.onTrue;
		}
		return {};
	}
}
#ifdef _MSC_VER
#	pragma float_control(pop)
#endif
