#define NOMINMAX
#include <Windows.h>
#include <xbyak/xbyak.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Features/LightLimitFix/VRHookPolicy.h"

namespace
{
	std::vector<std::uint8_t> image(0x1A00000);
	std::vector<std::string> errors;
	bool vrRuntime = true;
	int runtimeVersion = 1415;
	std::size_t assertions = 0;
	void Require(bool condition)
	{
		++assertions;
		if (!condition) {
			throw std::runtime_error(std::format("Scene guard assertion {} failed", assertions));
		}
	}
}

namespace logger
{
	template <class... Args>
	void error(std::format_string<Args...> format, Args&&... args)
	{
		errors.push_back(std::format(format, std::forward<Args>(args)...));
	}
	template <class... Args>
	void info(const char*, Args&&...)
	{}
}
namespace REL
{
	struct Version
	{
		int value;
		bool operator==(const Version&) const = default;
		std::string string() const { return std::to_string(value); }
	};
	struct Segment
	{
		enum
		{
			rdata
		};
		std::uintptr_t address() const { return reinterpret_cast<std::uintptr_t>(image.data()) + 0x157F000; }
		std::size_t size() const { return image.size() - 0x157F000; }
	};
	struct Module
	{
		static bool IsVR() { return vrRuntime; }
		static Module get() { return {}; }
		Version version() const { return { runtimeVersion }; }
		std::uintptr_t base() const { return reinterpret_cast<std::uintptr_t>(image.data()); }
		Segment segment(int) const { return {}; }
	};
	constexpr std::uint8_t NOP = 0x90;
	void safe_fill(std::uintptr_t address, std::uint8_t value, std::size_t size)
	{
		std::memset(reinterpret_cast<void*>(address), value, size);
	}
}
namespace SKSE
{
	constexpr REL::Version RUNTIME_VR_1_4_15{ 1415 };
	struct Write
	{
		std::uintptr_t rva;
		std::size_t size;
		std::uint8_t opcode;
	};
	struct Trampoline
	{
		std::vector<Write> writes;
		std::size_t allocations = 0;
		void* allocate(const Xbyak::CodeGenerator& code)
		{
			Require(code.getSize() > 0);
			++allocations;
			return const_cast<std::uint8_t*>(code.getCode());
		}
		template <std::size_t N>
		void write_branch(std::uintptr_t address, std::uintptr_t)
		{
			writes.push_back({ address - REL::Module::get().base(), N, 0xE9 });
			std::memset(reinterpret_cast<void*>(address), 0xE9, N);
		}
		template <std::size_t N>
		void write_call(std::uintptr_t address, std::uintptr_t)
		{
			writes.push_back({ address - REL::Module::get().base(), N, 0xE8 });
			std::memset(reinterpret_cast<void*>(address), 0xE8, N);
		}
	} trampoline;
	Trampoline& GetTrampoline() { return trampoline; }
}
struct LightLimitFix
{
	struct Hooks
	{
		static void InstallVRSceneGraphCullingObjectGuard();
		static void InstallVRShadowMapCameraGuard();
	};
};

#include "fixtures/vr_scene_guard_sites.h"
#include "vr_scene_guards_under_test.h"

namespace
{
	void Reset()
	{
		std::fill(image.begin(), image.end(), 0);
		for (const auto& site : capturedSites) {
			for (std::size_t i = 0; i < site.hex.size(); i += 2) {
				image[site.rva + i / 2] = static_cast<std::uint8_t>(std::stoul(std::string(site.hex.substr(i, 2)), nullptr, 16));
			}
		}
		const auto executable = reinterpret_cast<std::uintptr_t>(&Reset);
		for (auto rva : kVRShadowCameraVtableRVAs) {
			std::memcpy(image.data() + rva, &executable, sizeof(executable));
		}
		vrRuntime = true;
		runtimeVersion = 1415;
		SKSE::trampoline.writes.clear();
		SKSE::trampoline.allocations = 0;
		errors.clear();
	}

	void TestInstallation()
	{
		Reset();
		LightLimitFix::Hooks::InstallVRSceneGraphCullingObjectGuard();
		LightLimitFix::Hooks::InstallVRShadowMapCameraGuard();
		Require(errors.empty());
		const auto& writes = SKSE::trampoline.writes;
		Require(writes.size() == 3);
		Require(writes[0].rva == 0xCBFC60 && writes[0].size == 5);
		Require(writes[1].rva == 0x134C370 && writes[1].size == 6);
		Require(writes[2].rva == 0x134C613 && writes[2].size == 5 && writes[2].opcode == 0xE8);
		Require(image[0x134C618] == REL::NOP && image[0x134C619] == REL::NOP);
		Require(image[0x134C61A] == 0x0F);

		Reset();
		// Engine Fixes 7.7.1 owns this five-byte interior branch, outside our prologue.
		constexpr std::array<std::uint8_t, 5> foreignHook{ 0xE9, 0x11, 0x22, 0x33, 0x44 };
		std::copy(foreignHook.begin(), foreignHook.end(), image.begin() + 0xCBFD24);
		LightLimitFix::Hooks::InstallVRSceneGraphCullingObjectGuard();
		Require(errors.empty() && SKSE::trampoline.writes.size() == 1);
		Require(std::equal(foreignHook.begin(), foreignHook.end(), image.begin() + 0xCBFD24));
	}

	void TestRejectedSites()
	{
		for (const auto& site : capturedSites) {
			if (site.rva != 0xCBFC60 && site.rva != 0x134C370 && site.rva != 0x134C5F9 && site.rva != 0x134C99E) {
				continue;
			}
			for (std::size_t byte = 0; byte < site.hex.size() / 2; ++byte) {
				Reset();
				image[site.rva + byte] ^= 0x80;
				if (site.rva == 0xCBFC60) {
					LightLimitFix::Hooks::InstallVRSceneGraphCullingObjectGuard();
				} else {
					LightLimitFix::Hooks::InstallVRShadowMapCameraGuard();
				}
				Require(SKSE::trampoline.writes.empty() && SKSE::trampoline.allocations == 0);
				Require(errors.size() == 2 && errors[0].find("expected") != std::string::npos && errors[0].find("observed") != std::string::npos);
			}
		}
		Reset();
		image[0xCBFC60] = 0xE9;
		LightLimitFix::Hooks::InstallVRSceneGraphCullingObjectGuard();
		Require(SKSE::trampoline.writes.empty());
		Require(errors[0].find("CBFC60, byte +0: expected 48, observed E9") != std::string::npos);
		Reset();
		std::memset(image.data() + kVRShadowCameraVtableRVAs.back(), 0, sizeof(std::uintptr_t));
		LightLimitFix::Hooks::InstallVRShadowMapCameraGuard();
		Require(SKSE::trampoline.writes.empty() && SKSE::trampoline.allocations == 0);
		Require(errors.size() == 1 && errors[0].find("invalid camera vtable") != std::string::npos);
		constexpr std::uint8_t instruction[]{ 0xC3 };
		auto* unreadable = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
		Require(unreadable != nullptr);
		const bool matched = MatchesInstructions(reinterpret_cast<std::uintptr_t>(unreadable), instruction);
		const bool freed = VirtualFree(unreadable, 0, MEM_RELEASE) != 0;
		Require(!matched && freed && errors.back().find("not readable") != std::string::npos);
	}

	void TestRuntimeScope()
	{
		for (int version : { 1597, 1640, 1416 }) {
			Reset();
			runtimeVersion = version;
			vrRuntime = version == 1416;
			LightLimitFix::Hooks::InstallVRSceneGraphCullingObjectGuard();
			LightLimitFix::Hooks::InstallVRShadowMapCameraGuard();
			Require(SKSE::trampoline.writes.empty() && SKSE::trampoline.allocations == 0);
		}
	}

	void TestEntryGuardExecution()
	{
		Reset();
		int continued = 0;
		struct Continuation : Xbyak::CodeGenerator
		{
			Continuation(int* count, bool camera)
			{
				mov(rax, reinterpret_cast<std::uintptr_t>(count));
				inc(dword[rax]);
				if (camera) {
					pop(r12);
					pop(rbp);
				}
				ret();
				ready();
			}
		} sceneContinuation(&continued, false), cameraContinuation(&continued, true);
		std::array<std::uintptr_t, 0x190 / 8> camera{};
		std::array<std::uintptr_t, 0x48 / 8> descriptor{};
		std::array<std::uintptr_t, kVRShadowCameraVtableRVAs.size()> vtables{};
		for (std::size_t i = 0; i < vtables.size(); ++i) {
			vtables[i] = REL::Module::get().base() + kVRShadowCameraVtableRVAs[i];
		}
		camera[0] = vtables[0];
		camera[0x180 / 8] = reinterpret_cast<std::uintptr_t>(image.data());
		descriptor[0x40 / 8] = reinterpret_cast<std::uintptr_t>(camera.data());
		VRSceneGraphCullingObjectGuard scene(reinterpret_cast<std::uintptr_t>(sceneContinuation.getCode()));
		scene.ready();
		auto sceneCall = scene.getCode<void (*)(void*, void*)>();
		sceneCall(nullptr, nullptr);
		sceneCall(nullptr, reinterpret_cast<void*>(8));
		sceneCall(nullptr, reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(camera.data()) + 1));
		Require(continued == 0);
		sceneCall(nullptr, camera.data());
		Require(continued == 1);
		camera[0] = 0;
		sceneCall(nullptr, camera.data());
		Require(continued == 1);
		camera[0] = vtables[0];

		VRShadowMapCameraGuard entry(vtables, reinterpret_cast<std::uintptr_t>(cameraContinuation.getCode()));
		entry.ready();
		auto cameraCall = entry.getCode<void (*)(void*, void*)>();
		cameraCall(nullptr, nullptr);
		Require(continued == 1);
		cameraCall(nullptr, descriptor.data());
		Require(continued == 2);
		camera[0x180 / 8] = 0;
		cameraCall(nullptr, descriptor.data());
		camera[0x180 / 8] = reinterpret_cast<std::uintptr_t>(image.data());
		camera[0] = reinterpret_cast<std::uintptr_t>(image.data());
		cameraCall(nullptr, descriptor.data());
		Require(continued == 2);
	}

	void TestNativeLateExit()
	{
		Reset();
		auto appendCaptured = [](Xbyak::CodeGenerator& code, std::uintptr_t rva) {
			const auto site = std::find_if(std::begin(capturedSites), std::end(capturedSites),
				[rva](const auto& captured) { return captured.rva == rva; });
			Require(site != std::end(capturedSites));
			code.db(image.data() + rva, site->hex.size() / 2);
		};
		int continued = 0;
		std::array<std::uintptr_t, kVRShadowCameraVtableRVAs.size()> vtables{};
		for (std::size_t i = 0; i < vtables.size(); ++i) {
			vtables[i] = REL::Module::get().base() + kVRShadowCameraVtableRVAs[i];
		}
		std::array<std::uintptr_t, 0x190 / 8> camera{};
		Xbyak::CodeGenerator epilogue;
		appendCaptured(epilogue, 0x134C99E);
		epilogue.ready();
		VRShadowMapCameraLateUseGuard late(vtables, reinterpret_cast<std::uintptr_t>(epilogue.getCode()));
		late.ready();
		Xbyak::CodeGenerator nativeFrame;
		appendCaptured(nativeFrame, 0x134C370);
		appendCaptured(nativeFrame, 0x134C38A);
		nativeFrame.mov(nativeFrame.rdi, nativeFrame.rcx);
		for (int reg = 6; reg <= 12; ++reg) {
			nativeFrame.pxor(Xbyak::Xmm(reg), Xbyak::Xmm(reg));
		}
		nativeFrame.mov(nativeFrame.rax, reinterpret_cast<std::uintptr_t>(late.getCode()));
		nativeFrame.call(nativeFrame.rax);
		nativeFrame.mov(nativeFrame.rax, reinterpret_cast<std::uintptr_t>(&continued));
		nativeFrame.inc(nativeFrame.dword[nativeFrame.rax]);
		nativeFrame.mov(nativeFrame.rax, reinterpret_cast<std::uintptr_t>(epilogue.getCode()));
		nativeFrame.jmp(nativeFrame.rax);
		nativeFrame.ready();

		using Vectors = std::array<std::array<std::uint64_t, 2>, 10>;
		Vectors expectedVectors{}, observedVectors{};
		for (std::size_t i = 0; i < expectedVectors.size(); ++i) {
			expectedVectors[i] = { 0xAABBCCDD00000000ULL + i, 0x1122334400000000ULL + i };
		}
		std::array<std::uint64_t, 8> expectedGeneral{}, observedGeneral{};
		for (std::size_t i = 0; i < expectedGeneral.size(); ++i) {
			expectedGeneral[i] = 0x9988776600000000ULL + i;
		}
		std::array<std::uintptr_t, 2> stackPointers{};
		Xbyak::CodeGenerator probe;
		const std::array<Xbyak::Reg64, 8> nonvolatile{ probe.rbx, probe.rbp, probe.rsi, probe.rdi, probe.r12, probe.r13, probe.r14, probe.r15 };
		for (const auto& reg : nonvolatile) {
			probe.push(reg);
		}
		probe.sub(probe.rsp, 0xC8);
		probe.mov(probe.rax, reinterpret_cast<std::uintptr_t>(expectedVectors.data()));
		for (int i = 0; i < 10; ++i) {
			probe.movdqu(probe.ptr[probe.rsp + 0x20 + i * 16], Xbyak::Xmm(i + 6));
			probe.movdqu(Xbyak::Xmm(i + 6), probe.ptr[probe.rax + i * 16]);
		}
		for (std::size_t i = 0; i < nonvolatile.size(); ++i) {
			probe.mov(nonvolatile[i], expectedGeneral[i]);
		}
		probe.mov(probe.rax, reinterpret_cast<std::uintptr_t>(stackPointers.data()));
		probe.mov(probe.qword[probe.rax], probe.rsp);
		probe.mov(probe.rax, reinterpret_cast<std::uintptr_t>(nativeFrame.getCode()));
		probe.call(probe.rax);
		probe.mov(probe.rax, reinterpret_cast<std::uintptr_t>(stackPointers.data()));
		probe.mov(probe.qword[probe.rax + 8], probe.rsp);
		probe.mov(probe.rax, reinterpret_cast<std::uintptr_t>(observedGeneral.data()));
		for (std::size_t i = 0; i < nonvolatile.size(); ++i) {
			probe.mov(probe.qword[probe.rax + i * 8], nonvolatile[i]);
		}
		probe.mov(probe.rax, reinterpret_cast<std::uintptr_t>(observedVectors.data()));
		for (int i = 0; i < 10; ++i) {
			probe.movdqu(probe.ptr[probe.rax + i * 16], Xbyak::Xmm(i + 6));
			probe.movdqu(Xbyak::Xmm(i + 6), probe.ptr[probe.rsp + 0x20 + i * 16]);
		}
		probe.add(probe.rsp, 0xC8);
		for (auto reg = nonvolatile.rbegin(); reg != nonvolatile.rend(); ++reg) {
			probe.pop(*reg);
		}
		probe.ret();
		probe.ready();
		auto call = probe.getCode<void (*)(void*)>();
		auto verify = [&](void* object, bool valid) {
			const int before = continued;
			call(object);
			Require(continued == before + (valid ? 1 : 0));
			Require(stackPointers[0] == stackPointers[1]);
			Require(observedGeneral == expectedGeneral);
			Require(observedVectors == expectedVectors);
		};
		verify(nullptr, false);
		camera[0] = reinterpret_cast<std::uintptr_t>(image.data());
		verify(camera.data(), false);
		for (auto vtable : vtables) {
			camera[0] = vtable;
			camera[0x180 / 8] = 0;
			verify(camera.data(), false);
			camera[0x180 / 8] = reinterpret_cast<std::uintptr_t>(image.data());
			verify(camera.data(), true);
		}

		// Incorrect REX.R bits restore XMM14/XMM15 instead of XMM6/XMM7.
		epilogue.rewrite(0x14, 0x45, 1);
		epilogue.rewrite(0x19, 0x45, 1);
		call(nullptr);
		Require(stackPointers[0] == stackPointers[1] && observedGeneral == expectedGeneral);
		Require(observedVectors[0] != expectedVectors[0] && observedVectors[1] != expectedVectors[1]);
		Require(observedVectors[8] == expectedVectors[0] && observedVectors[9] == expectedVectors[1]);
	}
}

int main()
{
	try {
		TestInstallation();
		TestRejectedSites();
		TestRuntimeScope();
		TestEntryGuardExecution();
		TestNativeLateExit();
		std::cout << "VR scene guard installation: " << assertions << " assertions passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
