#include "EngineFixes/LightListSearchPolicy.h"
#include "Utils/PointerSearch.h"

#include <Windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
	void Require(bool a_condition, const std::source_location& a_location = std::source_location::current())
	{
		if (!a_condition)
			throw std::runtime_error("Light list search check failed at line " + std::to_string(a_location.line()));
	}

	void CheckAdmission()
	{
		using namespace LightListSearch::Policy;
		for (const auto fence : { std::numeric_limits<std::int32_t>::min(), -2, 0, 1, 16 })
			Require(!ShouldSearch(fence, 32));
		Require(!ShouldSearch(-1, 0));
		Require(!ShouldSearch(-1, MinimumVectorCount - 1));
		Require(ShouldSearch(-1, MinimumVectorCount));
	}

	void CheckCodeGuard()
	{
		using namespace LightListSearch::Policy;
		std::array<std::uint8_t, NativeCodeSize> bytes{};
		const auto put = [&](std::size_t offset, std::string_view hex) {
			Require(hex.size() % 2 == 0 && offset + hex.size() / 2 <= bytes.size());
			for (std::size_t i = 0; i < hex.size(); i += 2)
				bytes[offset++] = static_cast<std::uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16));
		};
		put(0, "488954241041564883ec5048c7442430feffffff48895c2460488974247048897c2478488bfa488bd9448b511833c04183faff0f84ae000000");
		put(0xE7, "4183c8ff448b49104585c9741e4183f8ff755c488b1148393cc2440f44c0ffc0413bc172e84183f8ff7544");
		put(0x156, "488b5c2460488b742470488b7c24784883c450415ec3");
		Require(MatchesNativeCode(bytes));
		Require(!MatchesNativeCode(std::span(bytes).first(bytes.size() - 1)));
		Require(!MatchesNativeCode({}));
		for (std::size_t i = 0; i < bytes.size(); ++i) {
			if (i >= 57 && !(i >= 0xE7 && i < 0x112) && i < 0x156)
				continue;
			bytes[i] ^= 1;
			Require(!MatchesNativeCode(bytes));
			bytes[i] ^= 1;
		}
	}

	void CheckSearchBounds()
	{
		using Util::PointerSearch::ContainsAvx2;
		Require(!ContainsAvx2(nullptr, 0, nullptr));
		SYSTEM_INFO info{};
		GetSystemInfo(&info);
		const auto pageSize = static_cast<std::size_t>(info.dwPageSize);
		auto* memory = static_cast<std::byte*>(VirtualAlloc(nullptr, pageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		Require(memory != nullptr);
		const auto release = [](std::byte* p) { VirtualFree(p, 0, MEM_RELEASE); };
		const std::unique_ptr<std::byte, decltype(release)> storage(memory, release);
		DWORD oldProtection = 0;
		Require(VirtualProtect(memory + pageSize, pageSize, PAGE_NOACCESS, &oldProtection) != FALSE);
		std::array<int, 68> values{};
		for (std::size_t count = 0; count < values.size(); ++count) {
			auto* data = memory + pageSize - count * sizeof(void*);
			for (std::size_t i = 0; i < count; ++i) {
				const void* pointer = &values[i];
				std::memcpy(data + i * sizeof(void*), &pointer, sizeof(pointer));
			}
			Require(!ContainsAvx2(data, count, &values.back()));
			for (std::size_t i = 0; i < count; ++i)
				Require(ContainsAvx2(data, count, &values[i]));
			if (count != 0) {
				const void* nullValue = nullptr;
				std::memcpy(data + (count - 1) * sizeof(void*), &nullValue, sizeof(nullValue));
				Require(ContainsAvx2(data, count, nullptr));
			}
		}
	}
}

int main()
{
	try {
		CheckAdmission();
		CheckCodeGuard();
		if (Util::PointerSearch::HasAvx2())
			CheckSearchBounds();
		else
			std::cout << "AVX2 search cases skipped: CPU/OS support unavailable\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
