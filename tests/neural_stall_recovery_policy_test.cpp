#include "Features/Upscaling/NeuralRendering/StallRecoveryPolicy.h"
#include <stdexcept>

void Require(bool condition)
{
	if (!condition)
		throw std::runtime_error("stall recovery policy regression");
}
int main()
{
	using namespace NeuralRendering;
	StallRecoveryPolicy policy;
	LifetimeFenceSnapshot fences{ .issued = 10, .completed = 9, .contexts = { 10 }, .initialized = true, .completedKnown = true };
	Require(!policy.ShouldPoll(100000));
	Require(policy.Arm(1000) && !policy.Arm(1001));
	Require(!policy.ShouldPoll(1499) && policy.ShouldPoll(1500));
	Require(policy.Observe(11000, fences) == StallRecoveryDecision::Waiting);
	fences.completed = 10;
	fences.contexts[0] = 11;
	Require(policy.Observe(11500, fences) == StallRecoveryDecision::Waiting);
	fences.contexts[0] = 10;
	Require(policy.Observe(11501, fences) == StallRecoveryDecision::Recover);
	fences.completedKnown = false;
	Require(policy.Observe(11502, fences) == StallRecoveryDecision::Unsafe);
	fences.completedKnown = true;
	fences.recording = true;
	Require(policy.Observe(11503, fences) == StallRecoveryDecision::Unsafe);
	fences.recording = false;
	fences.deviceRemoved = true;
	Require(policy.Observe(11504, fences) == StallRecoveryDecision::Unsafe);
	fences.deviceRemoved = false;
	fences.completed = 0;
	Require(policy.Observe(60999, fences) == StallRecoveryDecision::Waiting);
	Require(policy.Observe(61000, fences) == StallRecoveryDecision::Expired);
	Require(policy.Observe(999, fences) == StallRecoveryDecision::Unsafe);
	policy.BeginAttempt();
	Require(!policy.pending && policy.attempts == 1 && policy.Arm(2000));
	Require(policy.CooldownMs() == 20000);
	policy.BeginAttempt();
	Require(policy.Arm(3000) && policy.CooldownMs() == 30000);
	policy.BeginAttempt();
	Require(!policy.Arm(4000) && policy.attempts == StallRecoveryPolicy::kMaximumAttempts);
	Require(policy.CooldownMs() == 0);
	policy.Cancel();
	Require(!policy.pending && policy.attempts == StallRecoveryPolicy::kMaximumAttempts && !policy.Arm(5000));

	StallRecoveryPolicy completedEarly;
	fences.completed = fences.issued;
	Require(completedEarly.Arm(1000));
	Require(completedEarly.Observe(1500, fences) == StallRecoveryDecision::Waiting);
	Require(completedEarly.Observe(10999, fences) == StallRecoveryDecision::Waiting);
	Require(completedEarly.Observe(11000, fences) == StallRecoveryDecision::Recover);
	completedEarly.Cancel();
	Require(!completedEarly.ShouldPoll(20000) && completedEarly.attempts == 0);
	Require(completedEarly.Observe(20000, fences) == StallRecoveryDecision::Unsafe);

	StallRecoveryPolicy unavailable;
	Require(unavailable.Arm(1000));
	fences.initialized = false;
	Require(unavailable.Observe(11000, fences) == StallRecoveryDecision::Unsafe);
}
