#include "Utils/VRLoadingMenuClear.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
	using namespace Util::VRLoadingMenuClear;

	void Require(bool a_value)
	{
		if (!a_value)
			std::abort();
	}

	struct Renderer
	{
		CRITICAL_SECTION lock{};
		Renderer() { InitializeCriticalSection(&lock); }
		~Renderer() { DeleteCriticalSection(&lock); }
	};

	// These instruction bytes are retained independently from the loading-message
	// and renderer-UI windows in the captured Skyrim VR 1.4.15 image.
	constexpr std::array<std::uint8_t, 5> kRecordedMessageCall{ 0xE8, 0x90, 0x54, 0xA6, 0x00 };
	constexpr std::array<std::uint8_t, 5> kRecordedRetryCall{ 0xE8, 0x7E, 0x32, 0x00, 0x00 };

	std::uintptr_t RelativeCallTarget(std::uintptr_t a_site, const std::array<std::uint8_t, 5>& a_instruction)
	{
		Require(a_instruction[0] == 0xE8);
		std::int32_t displacement{};
		std::memcpy(&displacement, a_instruction.data() + 1, sizeof(displacement));
		return a_site + a_instruction.size() + displacement;
	}

	void TestNativeAdmission()
	{
		Require(kMessageCallRva == 0x8C22FB && kRenderRetryCallRva == 0x132450D);
		Require(kNativeClearRva == 0x1327790);
		Require(RelativeCallTarget(kMessageCallRva, kRecordedMessageCall) == kNativeClearRva);
		Require(RelativeCallTarget(kRenderRetryCallRva, kRecordedRetryCall) == kNativeClearRva);
		Require(!ValidateAdmission(true, true, kRecordedMessageCall, kRecordedRetryCall));
		Require(ValidateAdmission(false, false, {}, {}) == Installation::NotApplicable);
		Require(ValidateAdmission(false, true, kRecordedMessageCall, kRecordedRetryCall) == Installation::NotApplicable);
		Require(ValidateAdmission(true, false, {}, {}) == Installation::UnsupportedRuntime);
		Require(ValidateAdmission(true, false, kRecordedMessageCall, kRecordedRetryCall) == Installation::UnsupportedRuntime);

		for (std::size_t length = 0; length < kRecordedMessageCall.size(); ++length) {
			Require(ValidateAdmission(true, true, std::span(kRecordedMessageCall).first(length), kRecordedRetryCall) == Installation::UnexpectedMessageCall);
			Require(ValidateAdmission(true, true, kRecordedMessageCall, std::span(kRecordedRetryCall).first(length)) == Installation::UnexpectedRenderRetry);
		}
		for (std::size_t index = 0; index < kRecordedMessageCall.size(); ++index) {
			auto changedMessage = kRecordedMessageCall;
			auto changedRetry = kRecordedRetryCall;
			changedMessage[index] ^= 1;
			changedRetry[index] ^= 1;
			Require(ValidateAdmission(true, true, changedMessage, kRecordedRetryCall) == Installation::UnexpectedMessageCall);
			Require(ValidateAdmission(true, true, kRecordedMessageCall, changedRetry) == Installation::UnexpectedRenderRetry);
		}
		const std::array<std::uint8_t, 6> oversizedMessage{ 0xE8, 0x90, 0x54, 0xA6, 0x00, 0x90 };
		const std::array<std::uint8_t, 6> oversizedRetry{ 0xE8, 0x7E, 0x32, 0x00, 0x00, 0x90 };
		Require(ValidateAdmission(true, true, oversizedMessage, kRecordedRetryCall) == Installation::UnexpectedMessageCall);
		Require(ValidateAdmission(true, true, kRecordedMessageCall, oversizedRetry) == Installation::UnexpectedRenderRetry);
	}

	void TestInstallationReadiness()
	{
		for (const auto state : { Installation::NotInstalled, Installation::UnsupportedRuntime,
				 Installation::UnexpectedMessageCall, Installation::UnexpectedRenderRetry }) {
			const auto status = DescribeInstallation(state);
			Require(status.applicable && !status.ready && !status.installed);
		}
		Require(std::string_view(DescribeInstallation(Installation::UnexpectedMessageCall).state) == "unexpected_message_call");
		Require(std::string_view(DescribeInstallation(Installation::UnexpectedRenderRetry).state) == "unexpected_render_retry");
		const auto installed = DescribeInstallation(Installation::Installed);
		Require(installed.applicable && installed.ready && installed.installed);
		const auto nonVR = DescribeInstallation(Installation::NotApplicable);
		Require(!nonVR.applicable && nonVR.ready && !nonVR.installed);
	}
}

int main()
{
	TestNativeAdmission();
	TestInstallationReadiness();
	Renderer renderer;
	std::atomic_bool pending{ true };
	std::atomic_uint clears{};
	const auto clear = [&]() {
		if (pending.exchange(false))
			++clears;
	};
	Require(TryExecute(nullptr, clear) == Result::MissingRenderer);
	Require(pending && clears == 0);

	EnterCriticalSection(&renderer.lock);
	std::thread loadingMessage([&]() {
		Require(TryExecute(&renderer.lock, clear) == Result::Deferred);
		Require(pending && clears == 0);
	});
	loadingMessage.join();
	// The render owner can service the original flags without deadlocking on
	// a recursive acquisition or waiting for a blocked loading-message thread.
	Require(TryExecute(&renderer.lock, [&]() {
		Require(TryExecute(&renderer.lock, clear) == Result::Executed);
	}) == Result::Executed);
	Require(!pending && clears == 1);
	LeaveCriticalSection(&renderer.lock);

	std::thread nextMessage([&]() {
		pending = true;
		Require(TryExecute(&renderer.lock, clear) == Result::Executed);
	});
	nextMessage.join();
	Require(!pending && clears == 2);

	bool propagated = false;
	try {
		(void)TryExecute(&renderer.lock, []() { throw std::runtime_error("native clear fixture"); });
	} catch (const std::runtime_error&) {
		propagated = true;
	}
	Require(propagated);
	std::thread afterException([&]() {
		pending = true;
		Require(TryExecute(&renderer.lock, clear) == Result::Executed);
	});
	afterException.join();
	Require(!pending && clears == 3);
	std::cout << "Loading-menu clear: native call admission, runtime rejection, readiness, "
				 "contention retains pending work, native-owner retry, recursion, missing renderer and exception release passed\n";
}
