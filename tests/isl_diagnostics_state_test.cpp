#include "Diagnostics/ISLDiagnosticsState.h"
#include "Diagnostics/ReflexMeasurementState.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace CSX::Diagnostics::ISL;
void Check(bool value, const char* reason)
{
	if (!value) {
		std::fprintf(stderr, "%s\n", reason);
		std::exit(1);
	}
}
int main()
{
	for (unsigned l = 0; l < 4; ++l)
		for (unsigned r = 0; r < 4; ++r)
			for (unsigned p = 0; p < 4; ++p)
				for (unsigned o = 0; o < 4; ++o) {
					Policy policy{ Luminance(l), Renderer(r), Particle(p), Observation(o), true, 1234567 };
					Check(Policy::Unpack(policy.Pack()) == policy, "packed policy lost a field");
					const bool valid = l <= 2 && r <= 2 && p <= 2 && o <= 3 && (r != 2 || l == 1);
					Check(policy.Valid(true, true) == valid, "illegal combined policy accepted");
					Check(!policy.Valid(false, false), "editor bypass admitted dirty editor");
				}
	Policy normal, direct{ Luminance::Native, Renderer::VanillaDirect, Particle::Normal, Observation::Counters, false, 9 };
	std::atomic<uint64_t> publication{ normal.Pack() };
	std::atomic_bool stop{};
	std::thread reader([&] { while (!stop) { const auto p = Policy::Unpack(publication.load()); Check(p == normal || p == direct, "torn policy"); } });
	for (unsigned n = 0; n < 100000; ++n) publication.store(n % 2 ? normal.Pack() : direct.Pack());
	stop = true;
	reader.join();
	PruneDelay delay;
	for (auto value : { 0u, 241u }) {
		bool rejected = false;
		try {
			delay.Arm(value);
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		Check(rejected, "prune range not validated");
	}
	delay.Arm(120);
	Check(!delay.Defer(200, 99999) && delay.armed && !delay.consumed, "arm consumed before admission");
	delay.Activate(1000, 1000);
	Check(delay.Defer(240, 1100) && delay.due == 240 && delay.target == 360, "first due sweep not bound");
	Check(delay.Defer(359, 1200) && delay.target == 360, "deferral rearmed");
	Check(!delay.Defer(360, 1300) && delay.executed == 360 && !delay.active, "due work not released");
	delay.Arm(240);
	delay.Activate(2000, 1000);
	Check(delay.Defer(500, 2100), "second arm failed");
	Check(!delay.Defer(501, 7000) && delay.executed == 501, "five-second expiry ignored");
	delay.Arm(10);
	delay.Activate(8000, 1000);
	delay.Cancel();
	Check(!delay.Defer(600, 8100) && delay.cancelled, "cancel did not preserve normal prune opportunity");
	using CSX::Diagnostics::ReflexMeasurement::PreferenceLease;
	using CSX::Diagnostics::ReflexMeasurement::Preferences;
	PreferenceLease lease;
	Preferences original{};
	lease.Acquire(original);
	Check(lease.ReleaseUnchanged(original, 1) && !lease.active && lease.unchangedReleased && !lease.acceptedRestored,
		"unchanged unavailable backend manufactured native acceptance");
	lease.Acquire(original);
	auto changed = original;
	changed.enabled = true;
	Check(lease.Write(original, changed) && !lease.ReleaseUnchanged(changed, 2), "written pacing bypassed native restore");
	Check(lease.Restore(changed, 3) && lease.active, "changed pacing did not require accepted restoration");
	lease.Confirm(4, 7, true, true);
	Check(!lease.active && lease.acceptedRestored, "accepted restoration did not release lease");
	lease.Acquire(original);
	Check(lease.ReleaseUnchanged(changed, 5) && lease.conflict && !lease.requestedRestored, "outside edits were lost");
}
