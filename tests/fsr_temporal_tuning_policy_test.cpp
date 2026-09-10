#include "Features/Upscaling/FSRTemporalTuningPolicy.h"

#include <array>
#include <iostream>
#include <limits>

namespace
{
	using namespace FSRTemporalTuningPolicy;
	constexpr std::uint64_t Version(unsigned major, unsigned minor, unsigned patch)
	{
		return (major << 22) | (minor << 12) | patch;
	}
	static_assert(SupportsProvider(Version(3, 1, 4)));
	static_assert(SupportsProvider((123ull << 32) | Version(3, 1, 5)));
	static_assert(!SupportsProvider(0));
	static_assert(!SupportsProvider(Version(3, 1, 3)));
	static_assert(!SupportsProvider(Version(3, 1, 6)));
	static_assert(!SupportsProvider(Version(3, 2, 4)));
	static_assert(!SupportsProvider(Version(4, 1, 4)));
	static_assert(!Settings{}.enabled);
	static_assert(Values(Settings{})[4] < 0.0f);
	static_assert(CanReuseContextProfile(3, 3, false));
	static_assert(CanReuseContextProfile(3, 4, true));
	static_assert(!CanReuseContextProfile(3, 4, false));

	int failures = 0;
	void Check(bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << message << '\n';
			++failures;
		}
	}
}

int main()
{
	Settings enabled{};
	enabled.enabled = true;
	Check(IsValid(enabled), "documented initial profile must be valid");
	constexpr std::array<float Settings::*, 5> fields{
		&Settings::velocityFactor, &Settings::reactivenessScale,
		&Settings::shadingChangeScale, &Settings::accumulationAddedPerFrame,
		&Settings::minimumDisocclusionAccumulation
	};
	constexpr std::array<float, 5> minima{ 0, 0, 0, 0, -1 };
	constexpr std::array<float, 5> maxima{ 1, 4, 4, 1, 1 };
	for (size_t i = 0; i < fields.size(); ++i) {
		for (const auto invalid : { std::numeric_limits<float>::quiet_NaN(),
				 std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
				 minima[i] - 0.01f, maxima[i] + 0.01f }) {
			auto candidate = enabled;
			candidate.*fields[i] = invalid;
			Check(!IsValid(candidate), "nonfinite and out-of-range inputs must fail atomically");
		}
		for (const auto boundary : { minima[i], maxima[i] }) {
			auto candidate = enabled;
			candidate.*fields[i] = boundary;
			Check(IsValid(candidate), "inclusive supported bounds must remain available");
		}
	}

	auto inactiveEdit = Settings{};
	inactiveEdit.velocityFactor = 0;
	Check(Equivalent({}, inactiveEdit), "editing disabled values must not rebuild contexts");
	Check(!Equivalent({}, enabled), "enabling must invalidate context configuration");
	Check(!Equivalent(enabled, {}), "disabling must recreate true vendor defaults");
	RejectedRequest rejected{ true, enabled, Version(3, 1, 4), 7 };
	Check(rejected.Matches(enabled, Version(3, 1, 4), 7), "same rejected request must not retry every frame");
	Check(!rejected.Matches(enabled, Version(3, 1, 5), 7), "replacement provider permits a fresh capability attempt");
	auto edited = enabled;
	edited.velocityFactor = 0.5f;
	Check(!rejected.Matches(edited, Version(3, 1, 4), 8), "explicit setting changes permit retry");
	Check(!rejected.Matches(enabled, Version(3, 1, 4), 9), "edits away and back before application must permit a fresh attempt");

	unsigned calls = 0;
	auto success = [&](unsigned, unsigned, float) {
		++calls;
		return ConfigureResult{};
	};
	Check(ConfigureFreshContexts({}, 2, success).success && calls == 0,
		"disabled default must not call the provider");
	Check(ConfigureFreshContexts(enabled, 2, success).success && calls == 10,
		"stereo configuration must apply every key to both contexts");
	calls = 0;
	Check(!ConfigureFreshContexts(enabled, 3, success).success && calls == 0,
		"invalid context counts must not reach the provider");

	// Reject each possible key, including the second eye after a complete first eye.
	for (unsigned failure = 0; failure < 10; ++failure) {
		for (const bool fault : { false, true }) {
			calls = 0;
			const auto result = ConfigureFreshContexts(enabled, 2,
				[&](unsigned context, unsigned key, float value) {
					Check(value == Values(enabled)[key], "public key must receive its matching field");
					Check(context * 5 + key == calls, "complete one fresh context before advancing");
					const bool fails = calls++ == failure;
					return ConfigureResult{ !fails, fails && fault, fails ? -7 : 0 };
				});
			Check(!result.success && result.faulted == fault && result.code == -7,
				"provider error/fault must prevent publication of the complete set");
			Check(result.context == failure / 5 && result.key == failure % 5 && calls == failure + 1,
				"failure must retain exact evidence and stop subsequent configure calls");
		}
	}
	return failures == 0 ? 0 : 1;
}
