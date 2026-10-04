#include "ProviderFloorContract.h"
#include "Utils/CryptoHash.h"

#include <iostream>
#include <limits>

namespace
{
	unsigned checks = 0;
	void Check(bool condition)
	{
		++checks;
		if (!condition)
			throw std::runtime_error("provider floor contract assertion failed");
	}
	template <class Function>
	void Reject(Function&& function)
	{
		try {
			function();
		} catch (const std::runtime_error&) {
			++checks;
			return;
		}
		throw std::runtime_error("provider floor contract admitted an invalid input");
	}
}

int main()
{
	using namespace NrReplay::ProviderFloor;
	try {
		ValidateIdentity(256, kProviderSha256, kCodeSha256);
		const std::string upperProvider = "8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206";
		const std::string upperCode = "0D0543585E6765A87886678EFBDB6462F02BE2A3B6C26C9454C0E9F304194DC5";
		ValidateIdentity(256, CanonicalSha256(upperProvider), CanonicalSha256(upperCode));
		Check(CanonicalSha256(upperProvider) == kProviderSha256);
		Check(CanonicalSha256(upperCode) == kCodeSha256);
		Check(CanonicalSha256(kProviderSha256) == CanonicalSha256(upperProvider));
		for (std::size_t digit = 0; digit < upperProvider.size(); ++digit) {
			auto changed = upperProvider;
			changed[digit] = changed[digit] == '0' ? '1' : '0';
			Reject([&] { ValidateIdentity(256, CanonicalSha256(changed), CanonicalSha256(upperCode)); });
			auto changedCode = upperCode;
			changedCode[digit] = changedCode[digit] == '0' ? '1' : '0';
			Reject([&] { ValidateIdentity(256, CanonicalSha256(upperProvider), CanonicalSha256(changedCode)); });
		}
		for (const auto invalid : { "", "0x" })
			Reject([&] { (void)CanonicalSha256(invalid); });
		Reject([&] { (void)CanonicalSha256(upperProvider + " "); });
		for (const char invalid : { 'g', 'G', ' ', ':', '\0', static_cast<char>(0xff) }) {
			auto changed = upperProvider;
			changed[31] = invalid;
			Reject([&] { (void)CanonicalSha256(changed); });
		}
		const auto knownHash = Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes("abc"));
		Check(CanonicalSha256(knownHash) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
		Check(knownHash == Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes("abc")));
		for (const auto floor : { 0u, 128u, 255u, 257u, 320u })
			Reject([&] { ValidateIdentity(floor, kProviderSha256, kCodeSha256); });
		Reject([] { ValidateIdentity(256, "unrecognized-provider", kCodeSha256); });
		Reject([] { ValidateIdentity(256, kProviderSha256, "already-patched-code"); });
		ValidateGuard(kPatchRva, kGuard);
		Check(std::ranges::equal(std::span(kGuard).subspan(kPatchRva - kGuardRva, kOriginal.size()), kOriginal));
		Check(kReplacement == std::array<std::uint8_t, 5>{ 0xb8, 0, 1, 0, 0 });
		for (std::size_t byte = 0; byte < kGuard.size(); ++byte) {
			auto changed = kGuard;
			changed[byte] ^= 1;
			Reject([&] { ValidateGuard(kPatchRva, changed); });
		}
		Reject([] { ValidateGuard(kPatchRva + 1, kGuard); });
		Reject([] { ValidateGuard(kPatchRva, std::span(kGuard).first(kGuard.size() - 1)); });
		ValidateMemory(kModuleBytes, kCodeRva, kCodeBytes, true, true, true, false);
		for (unsigned bit = 0; bit < 4; ++bit)
			Reject([&] { ValidateMemory(kModuleBytes, kPatchRva, 5, bit != 0, bit != 1, bit != 2, bit == 3); });
		Reject([] { ValidateMemory(kModuleBytes - 1, kPatchRva, 5, true, true, true, false); });
		Reject([] { ValidateMemory(kModuleBytes, kModuleBytes, 1, true, true, true, false); });
		Reject([] { ValidateMemory(kModuleBytes, kModuleBytes - 1, 2, true, true, true, false); });
		Reject([] { ValidateMemory(kModuleBytes, kPatchRva, 0, true, true, true, false); });
		Reject([] { ValidateMemory(kModuleBytes, kPatchRva, std::numeric_limits<std::size_t>::max(), true, true, true, false); });
		Check(PaddedShape(192, 256, 320) == Shape{ 320, 320 });
		Check(PaddedShape(192, 256, 256) == Shape{ 320, 256 });
		Check(PaddedShape(256, 192, 256) == Shape{ 320, 256 });
		Check(PaddedShape(512, 512, 256) == Shape{ 576, 512 });
		Check(PaddedShape(768, 800, 256) == Shape{ 768, 832 });
		Check(PaddedShape(768, 800, 320) == Shape{ 768, 832 });
		Check(PaddedShape(320, 448, 320) == PaddedShape(320, 448, 256));
		Reject([] { (void)PaddedShape(0, 256, 256); });
		Reject([] { (void)PaddedShape(256, 16385, 256); });
		Reject([] { (void)PaddedShape(256, 256, 128); });
		std::cout << checks << " provider floor contract checks passed; no GPU or module patch executed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
