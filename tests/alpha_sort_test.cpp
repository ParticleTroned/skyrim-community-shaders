#include "EngineFixes/AlphaSortPolicy.h"
#include "EngineFixes/VRAlphaSortCode.h"
#include <array>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <source_location>
#include <stdexcept>
#include <vector>

namespace
{
	bool failArrayAllocation = false;
	std::size_t arrayAllocations = 0;
}

void* operator new[](std::size_t size)
{
	if (failArrayAllocation)
		throw std::bad_alloc{};
	++arrayAllocations;
	if (auto* memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
	void Check(bool value, const std::source_location& location = std::source_location::current())
	{
		if (!value)
			throw std::runtime_error("alpha sort check at line " + std::to_string(location.line()));
	}
	struct Pass
	{
		Pass* passGroupNext{};
		AlphaSort::Position centre{};
		float radius{};
		std::size_t ordinal{};
	};

	void CheckPolicy()
	{
		using namespace AlphaSort;
		const Position origin{ 16046.195f, -8945.370f, -4524.980f };
		const Position puff{ 41638.699f, -26081.252f, 6552.832f };
		const Position broad{ 36641.836f, -40280.773f, 11265.123f };
		Check(DistanceKey(broad, 27522.049f, origin, 0) > DistanceKey(puff, 7149.146f, origin, 0));
		Check(DistanceKey({ 3, 4, 0 }, 0, { 0, 0, 0 }, 0) == 25);
		Check(DistanceKey({ 3, 4, 0 }, 8, { 0, 0, 0 }, 1) == -9);
		Check(DistanceKey({ 3, 4, 0 }, 2, { 0, 0, 0 }, 1) == 9);
		Check(DistanceKey({ 3, 4, 0 }, 0, { 3, 4, 0 }, 0) == 0);
		const float huge = std::numeric_limits<float>::max();
		Check(Util::IsFinite(DistanceKey({ huge, huge, huge }, 0, { -huge, 0, 0 }, 0)));
		Check(DistanceKey({ 0, 0, 0 }, huge, { 0, 0, 0 }, 1) > -huge);
		Check(DistanceKey({ std::numeric_limits<float>::quiet_NaN(), 0, 0 }, 0, { 0, 0, 0 }, 0) == 0);
		Check(DistanceKey({ 1, 0, 0 }, -1, { 0, 0, 0 }, 0) == 0);
		for (const auto bits : { 0x7F800000u, 0xFF800000u, 0x7FC00001u, 0xFFC00001u }) {
			volatile std::uint32_t runtimeBits = bits;
			const auto invalid = std::bit_cast<float>(std::uint32_t(runtimeBits));
			Check(!IsFinite({ invalid, 0, 0 }));
			Check(!IsFinite({ 0, invalid, 0 }));
			Check(!IsFinite({ 0, 0, invalid }));
			Check(DistanceKey({ invalid, 0, 0 }, 1, {}, 0) == 0);
			Check(DistanceKey({ 1, 0, 0 }, 1, { invalid, 0, 0 }, 0) == 0);
			Check(DistanceKey({ 1, 0, 0 }, invalid, {}, 0) == 0);
			Check(DistanceKey({ 1, 0, 0 }, 1, {}, invalid) == 0);
		}

		std::mt19937 random(41);
		for (const std::size_t count : { 0u, 1u, 2u, 3u, 17u, 31u, 32u, 33u, 64u, 786u, 25000u, 65536u, 100000u }) {
			std::vector<Pass> passes(count);
			std::vector<Pass*> expected;
			for (std::size_t i = 0; i < count; ++i) {
				passes[i] = { i + 1 < count ? &passes[i + 1] : nullptr,
					{ static_cast<float>(random() % 100), 0, 0 }, static_cast<float>(random() % 100), i };
				expected.push_back(&passes[i]);
			}
			const auto key = [](const Pass* pass) { return DistanceKey(pass->centre, pass->radius, { 0, 0, 0 }, 1); };
			std::stable_sort(expected.begin(), expected.end(), [&](const Pass* a, const Pass* b) { return key(a) > key(b); });
			auto* head = count ? passes.data() : nullptr;
			Sort(head, key);
			for (auto* pass : expected) {
				Check(head == pass);
				head = head->passGroupNext;
			}
			Check(head == nullptr);
		}
	}

	void CheckFloatOrdering()
	{
		std::mt19937 random(74);
		for (const std::size_t count : { 2u, 17u, 31u, 32u, 33u, 64u, 10000u, 65536u, 65537u }) {
			std::vector<Pass> passes(count);
			std::vector<Pass*> expected;
			for (std::size_t i = 0; i < count; ++i) {
				const auto bits = std::uint32_t(random());
				const float value = std::bit_cast<float>(bits);
				// Deliberately interleave equal signed zeros with the full finite exponent range.
				const float depth = i % 3 == 0 || !Util::IsFinite(value) ? (i % 2 ? -0.0f : 0.0f) : value;
				passes[i] = { i + 1 < count ? &passes[i + 1] : nullptr, {}, depth, i };
				expected.push_back(&passes[i]);
			}
			std::stable_sort(expected.begin(), expected.end(), [](auto* a, auto* b) { return a->radius > b->radius; });
			auto* head = passes.data();
			std::size_t evaluations = 0;
			AlphaSort::Sort(head, [&](auto* pass) { ++evaluations; return pass->radius; });
			Check(evaluations == (count <= AlphaSort::detail::kMaximumCachedPasses ? count : 4 * count));
			for (auto* pass : expected) {
				Check(head == pass);
				head = head->passGroupNext;
			}
			Check(head == nullptr);
		}
	}

	void CheckScratchFailureAndReuse()
	{
		std::array<Pass, 128> passes{};
		const auto reset = [&](float direction) {
			for (std::size_t i = 0; i < passes.size(); ++i)
				passes[i] = { i + 1 < passes.size() ? &passes[i + 1] : nullptr, {}, direction * float(i), i };
		};
		const auto verify = [&](Pass* head, bool descending) {
			for (std::size_t i = 0; i < passes.size(); ++i) {
				Check(head == &passes[descending ? passes.size() - 1 - i : i]);
				head = head->passGroupNext;
			}
			Check(!head);
		};
		std::size_t evaluations = 0;
		const auto key = [&](const Pass* pass) { ++evaluations; return pass->radius; };
		reset(1);
		auto* head = passes.data();
		failArrayAllocation = true;
		AlphaSort::Sort(head, key);
		failArrayAllocation = false;
		Check(evaluations == 4 * passes.size());
		verify(head, true);
		reset(1);
		head = passes.data();
		evaluations = 0;
		AlphaSort::Sort(head, key);
		Check(evaluations == passes.size());
		verify(head, true);
		const auto allocations = arrayAllocations;
		reset(-1);
		head = passes.data();
		evaluations = 0;
		failArrayAllocation = true;
		AlphaSort::Sort(head, key);
		failArrayAllocation = false;
		Check(evaluations == passes.size() && arrayAllocations == allocations);
		verify(head, false);

		std::array<Pass, 257> larger{};
		const auto resetLarger = [&] {
			for (std::size_t i = 0; i < larger.size(); ++i)
				larger[i] = { i + 1 < larger.size() ? &larger[i + 1] : nullptr, {}, float(i), i };
		};
		const auto verifyLarger = [&](Pass* sorted) {
			for (std::size_t i = larger.size(); i > 0; --i) {
				Check(sorted == &larger[i - 1]);
				sorted = sorted->passGroupNext;
			}
			Check(!sorted);
		};
		resetLarger();
		auto* largerHead = larger.data();
		evaluations = 0;
		failArrayAllocation = true;
		AlphaSort::Sort(largerHead, key);
		failArrayAllocation = false;
		Check(evaluations == 4 * larger.size() && arrayAllocations == allocations);
		verifyLarger(largerHead);
		reset(1);
		head = passes.data();
		evaluations = 0;
		failArrayAllocation = true;
		AlphaSort::Sort(head, key);
		failArrayAllocation = false;
		Check(evaluations == passes.size() && arrayAllocations == allocations);
		verify(head, true);

		reset(1);
		head = passes.data();
		std::array<Pass, 64> nested{};
		for (std::size_t i = 0; i < nested.size(); ++i)
			nested[i] = { i + 1 < nested.size() ? &nested[i + 1] : nullptr, {}, float(i), i };
		bool invoked = false;
		AlphaSort::Sort(head, [&](const Pass* pass) {
			if (!invoked) {
				invoked = true;
				auto* inner = nested.data();
				AlphaSort::Sort(inner, [](const Pass* value) { return value->radius; });
				for (std::size_t i = nested.size(); i > 0; --i) {
					Check(inner == &nested[i - 1]);
					inner = inner->passGroupNext;
				}
				Check(!inner);
			}
			return pass->radius;
		});
		verify(head, true);

		reset(1);
		head = passes.data();
		resetLarger();
		invoked = false;
		AlphaSort::Sort(head, [&](const Pass* pass) {
			if (!invoked) {
				invoked = true;
				auto* inner = larger.data();
				AlphaSort::Sort(inner, key);
				verifyLarger(inner);
			}
			return pass->radius;
		});
		verify(head, true);
		const auto grownAllocations = arrayAllocations;
		resetLarger();
		largerHead = larger.data();
		evaluations = 0;
		failArrayAllocation = true;
		AlphaSort::Sort(largerHead, key);
		failArrayAllocation = false;
		Check(evaluations == larger.size() && arrayAllocations == grownAllocations);
		verifyLarger(largerHead);
	}

	struct Invocation
	{
		std::uintptr_t stub{};
		std::array<std::uintptr_t, 7> registers{};
		std::array<std::array<std::uint32_t, 4>, 6> vectors{};
		float key{};
		std::uint64_t flags{};
		std::uintptr_t bound{}, cameraOrDirection{}, closest{}, alignment{};
	};

	class ClobberHelper : public Xbyak::CodeGenerator
	{
	public:
		ClobberHelper(Invocation& state, VRAlphaSort::KeySite site) : CodeGenerator(512)
		{
			mov(r11, reinterpret_cast<std::uintptr_t>(&state));
			mov(ptr[r11 + offsetof(Invocation, bound)], rcx);
			mov(ptr[r11 + offsetof(Invocation, cameraOrDirection)], rdx);
			if (site == VRAlphaSort::KeySite::Group)
				mov(ptr[r11 + offsetof(Invocation, closest)], r8);
			else
				movss(ptr[r11 + offsetof(Invocation, closest)], xmm2);
			mov(rax, rsp);
			and_(rax, 15);
			mov(ptr[r11 + offsetof(Invocation, alignment)], rax);
			for (auto reg : { rax, rcx, rdx, r8, r9, r10, r11 })
				mov(reg, 0xBADBAD);
			for (int i = 0; i < 6; ++i)
				pcmpeqd(Xbyak::Xmm(i), Xbyak::Xmm(i));
			mov(eax, std::bit_cast<std::uint32_t>(42.25f));
			movd(xmm0, eax);
			clc();
			ret();
			readyRE();
		}
	};

	class Invoke : public Xbyak::CodeGenerator
	{
	public:
		std::uintptr_t continuation{};
		Invoke(Invocation& state, VRAlphaSort::KeySite site) : CodeGenerator(2048)
		{
			for (auto reg : { rsi, rdi, r15, r12 })
				push(reg);
			sub(rsp, 0x38);
			movdqu(ptr[rsp + 0x10], xmm6);
			movdqu(ptr[rsp + 0x20], xmm8);
			mov(r12, reinterpret_cast<std::uintptr_t>(&state));
			for (int i = 0; i < 6; ++i) {
				mov(eax, 0x3F800000u + static_cast<std::uint32_t>(i));
				movd(Xbyak::Xmm(i), eax);
				pshufd(Xbyak::Xmm(i), Xbyak::Xmm(i), 0);
			}
			mov(eax, std::bit_cast<std::uint32_t>(3.0f));
			movd(xmm8, eax);
			if (site == VRAlphaSort::KeySite::Peek)
				movss(xmm3, xmm8);
			mov(rsi, 0x1000);
			mov(rdi, 0x2000);
			mov(r15, site == VRAlphaSort::KeySite::Group ? 1 : 0x3000);
			mov(rax, 0x1000 - 0xE4);
			mov(rcx, 0xCC);
			mov(rdx, 0x3000);
			mov(r8, 0x88);
			mov(r9, 0x99);
			mov(r10, 0xAA);
			mov(r11, 0xBB);
			if (site == VRAlphaSort::KeySite::Peek)
				sub(rsp, 8);
			stc();
			jmp(ptr[r12]);
			continuation = reinterpret_cast<std::uintptr_t>(getCurr());
			int index = 0;
			for (auto reg : { rax, rcx, rdx, r8, r9, r10, r11 })
				mov(ptr[r12 + offsetof(Invocation, registers) + index++ * 8], reg);
			for (int i = 0; i < 6; ++i)
				movdqu(ptr[r12 + offsetof(Invocation, vectors) + i * 16], Xbyak::Xmm(i));
			movss(ptr[r12 + offsetof(Invocation, key)], site == VRAlphaSort::KeySite::Consume ? xmm6 : xmm2);
			pushfq();
			pop(rax);
			mov(ptr[r12 + offsetof(Invocation, flags)], rax);
			if (site == VRAlphaSort::KeySite::Peek)
				add(rsp, 8);
			movdqu(xmm6, ptr[rsp + 0x10]);
			movdqu(xmm8, ptr[rsp + 0x20]);
			add(rsp, 0x38);
			for (auto reg : { r12, r15, rdi, rsi })
				pop(reg);
			ret();
			readyRE();
		}
	};

	void CheckNativeABI()
	{
		using namespace VRAlphaSort;
		for (const auto site : { KeySite::Group, KeySite::Peek, KeySite::Consume }) {
			Invocation state;
			std::uint32_t count = 17;
			ClobberHelper helper(state, site);
			Invoke invoke(state, site);
			KeyCode code(site, reinterpret_cast<std::uintptr_t>(helper.getCode()), invoke.continuation, reinterpret_cast<std::uintptr_t>(&count));
			state.stub = reinterpret_cast<std::uintptr_t>(code.getCode());
			invoke.getCode<void (*)()>()();
			Check(state.bound == 0x1000);
			Check(state.cameraOrDirection == (site == KeySite::Group ? 0x2000u : 0x3000u));
			Check(state.closest == (site == KeySite::Group ? 1u : std::bit_cast<std::uint32_t>(3.0f)));
			Check(state.alignment == 8);
			Check(state.key == 42.25f);
			Check(state.flags & 1);
			const std::array<std::uintptr_t, 7> expected{ site == KeySite::Peek ? 17u : 0x1000u - 0xE4u, 0xCC, 0x3000, 0x88, 0x99, 0xAA, 0xBB };
			Check(state.registers == expected);
			for (std::size_t i = 0; i < state.vectors.size(); ++i) {
				if (i == 2 && site != KeySite::Consume)
					continue;
				for (std::size_t lane = 0; lane < 4; ++lane) {
					const auto value = i == 3 && lane == 0 && site == KeySite::Peek ? std::bit_cast<std::uint32_t>(3.0f) : 0x3F800000u + static_cast<std::uint32_t>(i);
					Check(state.vectors[i][lane] == value);
				}
			}
		}
	}

	// Reproduce the native stable linked merge sort as the CPU benchmark reference.
	template <class Key>
	void ReferenceMergeSort(Pass*& head, Key key)
	{
		for (std::size_t width = 1;; width *= 2) {
			auto* left = head;
			Pass** tail = &head;
			std::size_t merges = 0;
			while (left) {
				++merges;
				auto* right = left;
				std::size_t leftCount = 0;
				for (; leftCount < width && right; ++leftCount)
					right = right->passGroupNext;
				std::size_t rightCount = width;
				while (leftCount || (rightCount && right)) {
					Pass* next;
					if (!leftCount || (rightCount && right && key(right) > key(left))) {
						next = right;
						right = right->passGroupNext;
						--rightCount;
					} else {
						next = left;
						left = left->passGroupNext;
						--leftCount;
					}
					*tail = next;
					tail = &next->passGroupNext;
				}
				left = right;
			}
			*tail = nullptr;
			if (merges <= 1)
				return;
		}
	}

	void Benchmark()
	{
		std::mt19937 random(82);
		for (const std::size_t count : { 2u, 17u, 64u, 786u, 25000u }) {
			std::vector<Pass> passes(count);
			for (auto& pass : passes)
				pass.centre = { float(random() % 80000), float(random() % 80000), float(random() % 20000) };
			for (const bool radial : { false, true }) {
				std::vector<double> samples;
				for (int iteration = 0; iteration < 24; ++iteration) {
					for (std::size_t i = 0; i < count; ++i)
						passes[i].passGroupNext = i + 1 < count ? &passes[i + 1] : nullptr;
					auto* head = passes.data();
					const auto start = std::chrono::steady_clock::now();
					if (radial)
						AlphaSort::Sort(head, [](const Pass* p) { return AlphaSort::DistanceKey(p->centre, 0, { 16000, -9000, -4500 }, 0); });
					else
						ReferenceMergeSort(head, [](const Pass* p) { return p->centre.x * 0.8f - p->centre.y * 0.6f; });
					samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
					Check(head != nullptr);
				}
				std::sort(samples.begin(), samples.end());
				std::cout << count << (radial ? " distance" : " projected") << " median sort ms: " << samples[samples.size() / 2] << '\n';
			}
		}
	}

	struct NativeGeometry
	{
		std::array<std::byte, 0xE4> prefix{};
		AlphaSort::Position centre{ 40000, -20000, 12000 };
		float radius{ 7000 };
	};
	thread_local std::optional<AlphaSort::Position> benchmarkOrigin{ { 16000, -9000, -4500 } };
	float BenchmarkKey(const float* bound, const AlphaSort::Position* direction, float closest) noexcept
	{
		if (benchmarkOrigin)
			return AlphaSort::DistanceKey({ bound[0], bound[1], bound[2] }, bound[3], *benchmarkOrigin, closest);
		return bound[1] * direction->y + bound[0] * direction->x + bound[2] * direction->z - bound[3] * closest;
	}

	void BenchmarkKeyBridge()
	{
		using namespace Xbyak::util;
		std::uint32_t count = 17;
		Xbyak::CodeGenerator continuation;
		continuation.movaps(xmm0, xmm2);
		continuation.ret();
		continuation.readyRE();
		VRAlphaSort::KeyCode bridge(VRAlphaSort::KeySite::Peek, reinterpret_cast<std::uintptr_t>(&BenchmarkKey),
			reinterpret_cast<std::uintptr_t>(continuation.getCode()), reinterpret_cast<std::uintptr_t>(&count));
		NativeGeometry geometry;
		AlphaSort::Position direction{ 0.8f, -0.6f, 0 };
		for (const bool radial : { false, true }) {
			Xbyak::CodeGenerator code;
			code.mov(rax, rcx);
			code.movaps(xmm3, xmm2);
			if (radial) {
				code.jmp(code.ptr[code.rip]);
				code.dq(reinterpret_cast<std::uintptr_t>(bridge.getCode()));
			} else {
				code.movss(xmm0, code.dword[rax + 0xE4]);
				code.mulss(xmm0, code.dword[rdx]);
				code.movss(xmm2, code.dword[rax + 0xE8]);
				code.mulss(xmm2, code.dword[rdx + 4]);
				code.movss(xmm1, code.dword[rax + 0xEC]);
				code.mulss(xmm1, code.dword[rdx + 8]);
				code.mulss(xmm3, code.dword[rax + 0xF0]);
				code.addss(xmm0, xmm2);
				code.addss(xmm0, xmm1);
				code.subss(xmm0, xmm3);
				code.ret();
			}
			code.readyRE();
			const auto function = code.getCode<float (*)(NativeGeometry*, AlphaSort::Position*, float)>();
			std::vector<double> samples;
			for (int iteration = 0; iteration < 12; ++iteration) {
				float sum = 0;
				const auto start = std::chrono::steady_clock::now();
				for (int call = 0; call < 100000; ++call)
					sum += function(&geometry, &direction, 0);
				samples.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 100000.0);
				Check(sum > 0);
			}
			std::sort(samples.begin(), samples.end());
			std::cout << (radial ? "distance bridge" : "projected arithmetic") << " median key ns: " << samples[samples.size() / 2] << '\n';
		}
	}
}

int main(int argc, char**)
{
	try {
		CheckScratchFailureAndReuse();
		CheckPolicy();
		CheckFloatOrdering();
		CheckNativeABI();
		if (argc > 1) {
			Benchmark();
			BenchmarkKeyBridge();
		}
		std::cout << "Alpha ordering and native key ABI checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
