#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace Util::Units
{
#include "angle_wrap_under_test.h"
}

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	DWORD nextWaitResult = WAIT_OBJECT_0;
	DWORD lastTimeout = INFINITE;
	unsigned waitCalls = 0;
	unsigned errorReads = 0;
	unsigned sleepCalls = 0;
	int64_t clockTicks = 0;

	DWORD FakeWait(HANDLE, DWORD timeout)
	{
		++waitCalls;
		lastTimeout = timeout;
		return nextWaitResult;
	}
	DWORD FakeLastError()
	{
		++errorReads;
		return ERROR_DEVICE_NOT_CONNECTED;
	}
	BOOL FakePerformanceCounter(LARGE_INTEGER* counter)
	{
		counter->QuadPart = ++clockTicks;
		return TRUE;
	}
}

namespace logger
{
	unsigned warnings = 0;
	template <class... Args>
	void warn(const char*, Args&&...)
	{
		++warnings;
	}
}

struct Upscaling
{
	bool d3d12SwapChainActive = false;
	HANDLE frameLatencyHandle = reinterpret_cast<HANDLE>(std::uintptr_t(1));
	struct
	{
		bool frameLimitMode = false;
	} settings;
	double refreshRate = 60.0;
	LARGE_INTEGER qpf = [] { LARGE_INTEGER value{}; value.QuadPart = 1000; return value; }();
	bool generation = false;
	int64_t frameLimitDeadline = 0;
	int64_t frameLimitInterval = 0;

	HANDLE GetFrameLatencyWaitableObject() const { return frameLatencyHandle; }
	bool ShouldUseFrameGenerationThisFrame() const { return generation; }
	void TimerSleepQPC(int64_t target, int64_t)
	{
		++sleepCalls;
		clockTicks = target;
	}
	void FrameLimiter();
};

#define WaitForSingleObject FakeWait
#define GetLastError FakeLastError
#define QueryPerformanceCounter FakePerformanceCounter
#include "frame_limiter_under_test.h"
#undef QueryPerformanceCounter
#undef GetLastError
#undef WaitForSingleObject

namespace
{
	void TestAngles()
	{
		struct Case
		{
			float input;
			float unsignedResult;
			float signedResult;
		};
		constexpr Case cases[] = {
			{ 0, 0, 0 }, { 1, 1, 1 }, { -1, 359, -1 }, { 180, 180, 180 }, { -180, 180, -180 },
			{ 181, 181, -179 }, { -181, 179, 179 }, { 360, 0, 0 }, { -360, 0, 0 },
			{ 720, 0, 0 }, { -720, 0, 0 }, { 100000, 280, -80 }, { -100000, 80, 80 },
			{ -1e-9f, 0, -1e-9f }
		};
		for (const auto& value : cases) {
			Require(Util::Units::NormalizeDegrees0To360(value.input) == value.unsignedResult, "unsigned angle mismatch");
			Require(Util::Units::NormalizeDegreesToSignedRange(value.input) == value.signedResult, "signed angle mismatch");
		}
		for (float value : { 1e10f, -1e10f, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() }) {
			const auto unsignedResult = Util::Units::NormalizeDegrees0To360(value);
			const auto signedResult = Util::Units::NormalizeDegreesToSignedRange(value);
			Require(std::isfinite(unsignedResult) && unsignedResult >= 0 && unsignedResult < 360, "large unsigned angle out of range");
			Require(std::isfinite(signedResult) && signedResult >= -180 && signedResult <= 180, "large signed angle out of range");
		}
		for (float value : { std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			Require(Util::Units::NormalizeDegrees0To360(value) == 0, "nonfinite unsigned angle");
			Require(Util::Units::NormalizeDegreesToSignedRange(value) == 0, "nonfinite signed angle");
		}
	}

	void TestFrameWait()
	{
		Upscaling upscaling;
		upscaling.FrameLimiter();
		Require(waitCalls == 0 && logger::warnings == 0, "inactive swapchain must not wait");
		upscaling.d3d12SwapChainActive = true;
		upscaling.FrameLimiter();
		Require(waitCalls == 1 && lastTimeout == 1000 && logger::warnings == 0, "signalled wait must remain bounded and quiet");
		nextWaitResult = WAIT_TIMEOUT;
		for (unsigned i = 0; i < 8; ++i)
			upscaling.FrameLimiter();
		Require(waitCalls == 9 && lastTimeout == 1000 && logger::warnings == 1 && errorReads == 0, "timeouts must continue without flooding warnings");
		nextWaitResult = WAIT_FAILED;
		upscaling.FrameLimiter();
		Require(waitCalls == 10 && errorReads == 1 && logger::warnings == 1, "failed wait must read the error and retain bounded diagnostics");
		upscaling.frameLatencyHandle = nullptr;
		upscaling.FrameLimiter();
		upscaling.frameLatencyHandle = INVALID_HANDLE_VALUE;
		upscaling.FrameLimiter();
		Require(waitCalls == 10 && errorReads == 1, "missing handles must skip the Win32 wait");
		upscaling.frameLatencyHandle = reinterpret_cast<HANDLE>(std::uintptr_t(1));
		nextWaitResult = WAIT_OBJECT_0;
		upscaling.settings.frameLimitMode = true;
		upscaling.FrameLimiter();
		Require(waitCalls == 11 && sleepCalls == 1 && logger::warnings == 1, "recovered wait must retain configured pacing");
		const auto previousTicks = clockTicks;
		upscaling.generation = true;
		nextWaitResult = WAIT_TIMEOUT;
		upscaling.FrameLimiter();
		Require(waitCalls == 12 && sleepCalls == 2 && clockTicks - previousTicks > 30, "timeout must retain frame-generation pacing fallback");
		const auto deadline = upscaling.frameLimitDeadline;
		clockTicks = deadline + 2;
		upscaling.FrameLimiter();
		Require(upscaling.frameLimitDeadline == deadline + upscaling.frameLimitInterval, "small overshoots must retain cadence");
		clockTicks += 1000;
		upscaling.FrameLimiter();
		Require(upscaling.frameLimitDeadline > clockTicks, "long hitches must rebase the deadline");
		upscaling.settings.frameLimitMode = false;
		upscaling.FrameLimiter();
		Require(upscaling.frameLimitDeadline == 0 && upscaling.frameLimitInterval == 0, "disabled pacing must forget the deadline");
		upscaling.settings.frameLimitMode = true;
		for (double invalid : { 0.0, -60.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
			upscaling.refreshRate = invalid;
			const auto previousSleeps = sleepCalls;
			upscaling.FrameLimiter();
			Require(sleepCalls == previousSleeps && upscaling.frameLimitDeadline == 0, "invalid timing must not sleep");
		}
	}
}

int main()
{
	try {
		TestAngles();
		TestFrameWait();
		std::cout << "Hang guard tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
