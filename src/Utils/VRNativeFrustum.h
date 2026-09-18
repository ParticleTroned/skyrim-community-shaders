#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace VRNativeFrustum
{
	struct NativeHeader
	{
		std::uintptr_t planes = 0, operators = 0;
		std::uint32_t planeStorage = 0, operatorStorage = 0, planeCount = 0, operatorCount = 0, firstOp = 0;
		std::uint8_t prethreaded = 0;
		bool valid = false;
		bool operator==(const NativeHeader&) const = default;
	};
	struct NativeOperator
	{
		std::uint32_t opcode = 0, onTrue = 0, onFalse = 0;
	};
	static_assert(sizeof(NativeOperator) == 12);
	inline bool ArrayAddress(std::uintptr_t base, std::uint32_t index, std::size_t stride, std::uintptr_t& address) noexcept
	{
		if (!base || !stride || index > (std::numeric_limits<std::uintptr_t>::max() - base) / stride)
			return false;
		address = base + index * stride;
		return address <= std::numeric_limits<std::uintptr_t>::max() - stride;
	}
	/** Only read layouts verified in the live snapshot; unavailable metadata never changes admission. */
	template <class Read>
	NativeHeader ReadHeader(std::uintptr_t owner, Read&& read)
	{
		NativeHeader h;
		if (!owner || owner > std::numeric_limits<std::uintptr_t>::max() - 0xC8)
			return h;
		if (!read(owner, &h.planes, 8) || !read(owner + 0x18, &h.operators, 8) || !read(owner + 0x10, &h.planeStorage, 4) || !read(owner + 0x28, &h.operatorStorage, 4) || !read(owner + 0xB8, &h.planeCount, 4) || !read(owner + 0xBC, &h.operatorCount, 4) || !read(owner + 0xC0, &h.firstOp, 4) || !read(owner + 0xC5, &h.prethreaded, 1))
			return h;
		h.valid = h.planeCount <= h.planeStorage && h.operatorCount <= h.operatorStorage && h.planeStorage <= 1'000'000 && h.operatorStorage <= 1'000'000 && (!h.planeCount || h.planes) && (!h.operatorCount || (h.operators && h.firstOp < h.operatorCount));
		return h;
	}
	template <class Read>
	bool ReadOperator(const NativeHeader& h, std::uint32_t i, NativeOperator& op, Read&& read)
	{
		std::uintptr_t a;
		return h.valid && i < h.operatorCount && ArrayAddress(h.operators, i, sizeof(op), a) && read(a, &op, sizeof(op));
	}
	/** Native branch targets can name terminal slots beyond the constructed body. */
	template <class Read>
	bool ReadInstruction(const NativeHeader& h, std::uint32_t i, NativeOperator& op, Read&& read)
	{
		if (i < h.operatorCount)
			return ReadOperator(h, i, op, read);
		std::uintptr_t address;
		return h.valid && i < h.operatorStorage && ArrayAddress(h.operators, i, sizeof(op), address) &&
		       read(address, &op, sizeof(op)) && (op.opcode == 2 || op.opcode == 3);
	}
	template <class Read>
	bool ReadBound(std::uintptr_t object, std::array<std::uint32_t, 4>& bits, Read&& read)
	{
		return object && object <= std::numeric_limits<std::uintptr_t>::max() - 0xF4 && read(object + 0xE4, bits.data(), sizeof(bits));
	}
}
