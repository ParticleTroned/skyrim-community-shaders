#include "Features/VRFrustumFastPathPolicy.h"
#include "Features/VRFrustumFastPathSignatures.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <immintrin.h>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <vector>

namespace
{
	using VRNativeFrustum::NativeOperator;
	void Check(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	struct Fixture
	{
		std::array<std::byte, 0xC8> owner{};
		std::array<std::byte, 0xF4> object{};
		std::vector<NativeOperator> ops;
		std::vector<std::array<std::uint32_t, 28>> planes;
		template <class T>
		void Put(std::size_t offset, T value)
		{
			std::memcpy(owner.data() + offset, &value, sizeof(value));
		}
		void Bind(std::uint32_t body)
		{
			Put(0, reinterpret_cast<std::uintptr_t>(planes.data()));
			Put(0x18, reinterpret_cast<std::uintptr_t>(ops.data()));
			Put(0x10, static_cast<std::uint32_t>(planes.size()));
			Put(0x28, static_cast<std::uint32_t>(ops.size()));
			Put(0xB8, static_cast<std::uint32_t>(planes.size()));
			Put(0xBC, body);
			Put(0xC0, 0u);
			Put(0xC5, std::uint8_t{ 1 });
		}
		explicit Fixture(std::uint32_t n = 1) : ops(n * 2 + 3), planes(n)
		{
			const auto body = n * 2;
			for (std::uint32_t i = 0; i < n; ++i) {
				ops[2 * i] = { 8, i + 1 == n ? body + 1 : 2 * (i + 1), body + 2 };
				ops[2 * i + 1].opcode = i;
				for (unsigned p = 0; p < 6; ++p) {
					planes[i][4 * p] = std::bit_cast<std::uint32_t>(1.0f);
					planes[i][4 * p + 3] = std::bit_cast<std::uint32_t>(10.0f);
				}
				planes[i][24] = 63;
			}
			ops[body] = { 1, body + 1, body + 2 };
			ops[body + 1].opcode = 2;
			ops[body + 2].opcode = 3;
			Bound({ 0, 0, 0, std::bit_cast<std::uint32_t>(1.0f) });
			Bind(body);
		}
		void Bound(std::array<std::uint32_t, 4> bits) { std::memcpy(object.data() + 0xE4, bits.data(), 16); }
		bool Read(std::uintptr_t address, void* output, std::size_t size) const
		{
			const auto within = [&](const void* data, std::size_t bytes) {
				const auto begin = reinterpret_cast<std::uintptr_t>(data);
				return address >= begin && address - begin <= bytes && size <= bytes - (address - begin);
			};
			if (!within(owner.data(), owner.size()) && !within(object.data(), object.size()) &&
				!within(ops.data(), ops.size() * sizeof(ops[0])) && !within(planes.data(), planes.size() * sizeof(planes[0])))
				return false;
			std::memcpy(output, reinterpret_cast<void*>(address), size);
			return true;
		}
		VRFrustumFastPath::Result Candidate()
		{
			return VRFrustumFastPath::Evaluate(reinterpret_cast<std::uintptr_t>(owner.data()), reinterpret_cast<std::uintptr_t>(object.data()), [this](auto a, auto p, auto n) { return Read(a, p, n); }, [](auto, auto) {});
		}
	};

	// Execute the retained native instructions as the independent differential oracle.
	class NativeOracle
	{
	public:
		NativeOracle()
		{
			using namespace VRFrustumFastPath::NativeCode;
			bytes = static_cast<std::byte*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			Check(bytes != nullptr, "oracle allocation failed");
			std::memcpy(bytes, Compound.data(), Compound.size());
			std::memcpy(bytes + 0x200, Intersect.data(), Intersect.size());
			std::memcpy(bytes + 0x300, NotFullyInside.data(), NotFullyInside.size());
			const std::uint32_t sign = 0x80000000;
			std::memcpy(bytes + 0x400, &sign, sizeof(sign));
			Rel32(0x7B, 0x300);
			Rel32(0x9A, 0x200);
			Rel32(0x212, 0x400);
			Rel32(0x312, 0x400);
			DWORD old = 0;
			if (!VirtualProtect(bytes, 4096, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), bytes, 4096)) {
				VirtualFree(bytes, 0, MEM_RELEASE);
				throw std::runtime_error("oracle executable protection failed");
			}
		}
		~NativeOracle() { VirtualFree(bytes, 0, MEM_RELEASE); }
		bool Run(Fixture& f) const { return reinterpret_cast<bool (*)(void*, void*)>(bytes)(f.owner.data(), f.object.data()); }

	private:
		void Rel32(std::size_t operand, std::size_t target)
		{
			const auto displacement = static_cast<std::int32_t>(target) - static_cast<std::int32_t>(operand + 4);
			std::memcpy(bytes + operand, &displacement, 4);
		}
		std::byte* bytes;
	};
	std::uint32_t Compare(Fixture& f, const NativeOracle& native)
	{
		const auto before = f.planes;
		const auto candidate = f.Candidate();
		Check(f.planes == before, "candidate mutated native masks or planes");
		const bool expected = native.Run(f);
		if (candidate.handled) {
			Check(expected == candidate.accepted, "candidate/native visibility differs");
			Check(f.planes == before, "handled path concealed native mask writes");
		}
		return candidate.handled;
	}
	void TestPaths(const NativeOracle& native)
	{
		Fixture longPath(67);
		Check(longPath.Candidate().handled && longPath.Candidate().tests == 67, "long first-plane path was not fused");
		Compare(longPath, native);
		Fixture reject;
		reject.ops[0].onTrue = 4;
		Check(reject.Candidate().handled && !reject.Candidate().accepted, "true sphere was confused with object acceptance");
		Compare(reject, native);
		for (std::uint32_t mask = 1; mask < 64; ++mask) {
			Fixture f;
			f.planes[0][24] = mask;
			Check(f.Candidate().handled, "nonzero first-active-plane mask not handled");
			Compare(f, native);
		}
		Fixture fallback(3);
		for (unsigned p = 0; p < 6; ++p) fallback.planes[2][4 * p + 3] = std::bit_cast<std::uint32_t>(-10.0f);
		Check(!fallback.Candidate().handled, "mask-changing tail was accepted");
		Compare(fallback, native);
		Check(fallback.planes[2][24] == 0, "native fallback did not preserve mask effects");
		Fixture emptyMask;
		emptyMask.planes[0][24] = 0;
		Check(!emptyMask.Candidate().handled, "zero mask should use native");
		Compare(emptyMask, native);
		Fixture opcode7;
		opcode7.ops[0].opcode = 7;
		Check(!opcode7.Candidate().handled, "opcode7 should use native");
		Compare(opcode7, native);
		Fixture cycle;
		cycle.ops[0].onTrue = 0;
		Check(!cycle.Candidate().handled, "cyclic path did not fall back");
		Fixture operand;
		operand.ops[1].opcode = 99;
		Check(!operand.Candidate().handled, "out-of-range plane accepted");
		Fixture terminal;
		terminal.ops[0].onTrue = 999;
		Check(!terminal.Candidate().handled, "out-of-storage terminal accepted");
		Fixture unprepared;
		unprepared.Put(0xC5, std::uint8_t{ 0 });
		Check(!unprepared.Candidate().handled, "unprepared program accepted");
		Fixture unsupported;
		unsupported.ops[0].opcode = 9;
		Check(!unsupported.Candidate().handled, "unknown opcode accepted");
		Fixture changed;
		unsigned headerReads = 0;
		const auto owner = reinterpret_cast<std::uintptr_t>(changed.owner.data());
		auto result = VRFrustumFastPath::Evaluate(owner, reinterpret_cast<std::uintptr_t>(changed.object.data()), [&](auto a, auto p, auto n) { if(a==owner && ++headerReads==2) changed.Put(0xC0,1u); return changed.Read(a,p,n); }, [](auto, auto) {});
		Check(!result.handled, "changing header accepted");
		Fixture moved;
		result = VRFrustumFastPath::Evaluate(reinterpret_cast<std::uintptr_t>(moved.owner.data()), reinterpret_cast<std::uintptr_t>(moved.object.data()), [&](auto a, auto p, auto n) { return moved.Read(a, p, n); }, [&](auto, auto) { moved.Bound({ 0, 0, 0, std::bit_cast<std::uint32_t>(2.0f) }); });
		Check(!result.handled, "changing bound accepted");
		Fixture unreadable;
		const auto unreadablePlane = reinterpret_cast<std::uintptr_t>(unreadable.planes.data());
		result = VRFrustumFastPath::Evaluate(reinterpret_cast<std::uintptr_t>(unreadable.owner.data()), reinterpret_cast<std::uintptr_t>(unreadable.object.data()), [&](auto a, auto p, auto n) { return a != unreadablePlane && unreadable.Read(a, p, n); }, [](auto, auto) {});
		Check(!result.handled, "failed plane read accepted");
		Fixture highMask;
		highMask.planes[0][24] = 64;
		Check(!highMask.Candidate().handled, "unsupported mask bits accepted");
		Fixture limit(VRFrustumFastPath::StepLimit);
		Check(limit.Candidate().handled, "bounded path limit rejected");
		Compare(limit, native);
		Fixture overLimit(VRFrustumFastPath::StepLimit + 1);
		Check(!overLimit.Candidate().handled, "oversized path accepted");
		Compare(overLimit, native);
	}
	void TestNumbers(const NativeOracle& native)
	{
		for (auto radius : { 0u, 0x80000000u, 0xBF800000u, 1u, 0x7F800000u, 0x7FC00000u }) {
			Fixture f;
			f.Bound({ 0, 0, 0, radius });
			Check(!f.Candidate().handled, "unsupported radius admitted");
		}
		for (auto bad : { 1u, 0x7F800000u, 0x7FC00000u }) {
			Fixture f;
			f.planes[0][0] = bad;
			Check(!f.Candidate().handled, "non-normal coefficient admitted");
		}
		for (auto offset : { -1.0f, std::nextafter(-1.0f, 0.0f), std::nextafter(-1.0f, -2.0f), 1.0f }) {
			Fixture f;
			f.planes[0][3] = std::bit_cast<std::uint32_t>(offset);
			Compare(f, native);
		}
		std::mt19937 rng(0xDA54B0);
		std::uniform_real_distribution<float> values(-100.0f, 100.0f);
		unsigned handled = 0;
		for (unsigned test = 0; test < 12000; ++test) {
			Fixture f(1 + test % 8);
			f.Bound({ std::bit_cast<std::uint32_t>(values(rng)), std::bit_cast<std::uint32_t>(values(rng)), std::bit_cast<std::uint32_t>(values(rng)), std::bit_cast<std::uint32_t>(0.1f + std::abs(values(rng))) });
			for (auto& p : f.planes) {
				for (unsigned i = 0; i < 24; ++i) p[i] = std::bit_cast<std::uint32_t>(values(rng));
				p[24] = 1 + rng() % 63;
			}
			handled += Compare(f, native);
		}
		Check(handled > 100, "random cases did not exercise fast admission");
		const auto original = _mm_getcsr();
		_mm_setcsr((original & ~0x6000u) | 0x2000u);
		Fixture f;
		Check(!f.Candidate().handled, "non-default rounding admitted");
		_mm_setcsr((original & ~0x3Fu) & ~0x80u);
		Check(!f.Candidate().handled, "unmasked floating-point exceptions admitted");
		for (auto denormalMode : { 0u, 0x40u, 0x8000u, 0x8040u }) {
			_mm_setcsr((original & ~0xE040u) | denormalMode);
			Check(f.Candidate().handled, "valid DAZ/FTZ context rejected");
			Compare(f, native);
			for (unsigned i = 0; i < 4096; ++i) {
				Fixture wide;
				const auto bits = [&]() { return static_cast<std::uint32_t>(rng()); };
				wide.Bound({ bits(), bits(), bits(), bits() & 0x7FFFFFFFu });
				for (auto& word : wide.planes[0]) word = bits();
				wide.planes[0][24] = 1 + rng() % 63;
				Compare(wide, native);
			}
		}
		_mm_setcsr(original);
	}
	void TestCaptured(const NativeOracle& native)
	{
		std::ifstream stream(FRUSTUM_CASES);
		const auto data = nlohmann::json::parse(stream);
		unsigned handled = 0, total = 0;
		for (const auto& item : data["cases"]) {
			Fixture f(item["header"]["planeCount"].get<std::uint32_t>());
			const auto body = item["header"]["operatorCount"].get<std::uint32_t>();
			f.ops.assign(body + 3, {});
			for (const auto& slot : item["operators"]) {
				const auto words = slot["rawWords"].get<std::array<std::uint32_t, 3>>();
				f.ops.at(slot["index"].get<std::size_t>()) = { words[0], words[1], words[2] };
			}
			f.ops[body] = { 1, body + 1, body + 2 };
			f.ops[body + 1].opcode = 2;
			f.ops[body + 2].opcode = 3;
			for (const auto& plane : item["planes"])
				f.planes.at(plane["index"].get<std::size_t>()) = plane["rawUint32WordsBeforeFirstTest"].get<std::array<std::uint32_t, 28>>();
			f.Bind(body);
			f.Put(0xC0, item["header"]["firstOperator"].get<std::uint32_t>());
			f.Bound(item["bound"].get<std::array<std::uint32_t, 4>>());
			const auto before = f.planes;
			handled += Compare(f, native);
			f.planes = before;
			Check(native.Run(f) == item["accepted"].get<bool>(), "retained native result not reproduced");
			++total;
		}
		Check(total == 36 && handled > 0, "captured paths not exercised");
		std::printf("Captured native paths: %u/%u eligible; all results/masks match.\n", handled, total);
	}
}
int main()
{
	try {
		NativeOracle native;
		TestPaths(native);
		TestNumbers(native);
		TestCaptured(native);
	} catch (const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
	return 0;
}
