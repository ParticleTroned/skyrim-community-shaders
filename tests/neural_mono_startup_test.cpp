#include "Features/Upscaling/NeuralRendering/NativeMemoryPolicy.h"
#include <stdexcept>
#include <utility>

using namespace NeuralRendering;
namespace Util
{
	struct GpuMemoryBudget
	{
		enum class Owner
		{
			NeuralRendering
		};
		struct Reservation
		{
			std::uint64_t bytes = 0;
			explicit operator bool() const { return bytes != 0; }
			void Reset() { bytes = 0; }
			std::uint64_t Bytes() const { return bytes; }
		};
		static GpuMemoryBudget& Get()
		{
			static GpuMemoryBudget value;
			return value;
		}
		Reservation Reserve(Owner, int*, std::uint64_t bytes) { return { bytes }; }
	};
}
std::uint64_t clockMs = 10;
MemoryBudgetSample sample{ 16 * 1024 * NativeMemoryPolicy::kMiB, 1000, true, 0, false, clockMs };
std::uint64_t GetTickCount64() { return clockMs; }
MemoryBudgetSample MemorySample(int*, bool force = false)
{
	if (force && sample.valid)
		sample.sampledAtMs = ++clockMs;
	return sample;
}
struct Startup
{
	struct Device
	{
		int value = 0;
		int* Get() { return &value; }
		explicit operator bool() const { return true; }
	} device;
	struct Interop
	{
		LifetimeFenceSnapshot fences{};
		LifetimeFenceSnapshot Snapshot() const { return fences; }
	} interop;
	NativeMemoryPolicy nativeMemory;
	NativeStartupObservation nativeStartup;
	Util::GpuMemoryBudget::Reservation nativeReservation;
	MemoryRecoveryPolicy memory;
	std::uint64_t nativeObservations = 0;
	bool retiring = false;
	const char* status = nullptr;
	void Status(const char* value) { status = value; }
#include "neural_mono_startup_under_test.h"
};
static void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
int main()
{
	const NativeMemoryKey key{ .creation = { { 320, 192 }, { 320, 192 } }, .color = { 320, 192 } };
	Startup state;
	Require(state.ReserveNative(key, 100, clockMs, false), "prewarm reservation failed");
	Require(state.nativeStartup.valid, "fresh query time was compared against an earlier render timestamp");
	state.interop.fences = { .fence = 1, .issued = 1, .completed = 0, .initialized = true, .completedKnown = true };
	state.RecordNativeSubmission();
	state.PollNativeStartup(clockMs);
	Require(state.nativeStartup.active && state.nativeReservation.Bytes() == 100, "pending prewarm released native allowance");
	state.interop.fences.completed = 1;
	sample.usageBytes = 1300;
	state.SampleNativeStartup();
	Require(state.nativeStartup.valid && state.nativeStartup.peak == 1300, "startup sample used a timestamp preceding its forced query");
	state.PollNativeStartup(clockMs);
	Require(state.nativeReservation.Bytes() == 100 && state.nativeObservations == 0, "creation-only prewarm consumed first-evaluation allowance");
	Require(state.ReserveNative(key, 0, clockMs, true), "matching evaluation could not adopt prewarm reservation");
	state.interop.fences.issued = 2;
	state.RecordNativeSubmission();
	state.PollNativeStartup(clockMs);
	Require(state.nativeStartup.active, "first evaluation completed before its own fence");
	state.interop.fences.completed = 2;
	state.interop.fences.fence = 2;
	state.PollNativeStartup(clockMs);
	Require(state.nativeStartup.active, "another fence identity completed this native startup");
	state.interop.fences.fence = 1;
	sample.usageBytes = 1500;
	state.PollNativeStartup(clockMs);
	Require(!state.nativeStartup.active && state.nativeReservation.Bytes() == 0 && state.nativeObservations == 1, "completed fresh evaluation retained a duplicate reservation");
	Require(state.nativeMemory.Project(key)->observedProcessGrowth == 500, "native estimate lost creation plus evaluation growth");
	Require(state.ReserveNative(key, 100, clockMs, true), "second reservation failed");
	state.interop.fences.issued = state.interop.fences.completed = 3;
	state.RecordNativeSubmission();
	sample.valid = false;
	clockMs += NativeMemoryPolicy::kMaximumObservationMs + 1;
	state.PollNativeStartup(clockMs);
	Require(state.retiring && state.nativeReservation.Bytes() == 100 && state.nativeObservations == 1,
		"missing fresh memory telemetry silently released ownership or blocked forever without retirement");
}
