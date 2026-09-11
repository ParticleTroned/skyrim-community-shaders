# PR73 pending-drain polling

An immutable public-API settings transition can encounter a healthy pending
GPU drain after memory relief has invalidated shared presentation resources.
The old retry path waited six frames before polling again because faster
polling also required permission to skip the later settling guard.

Polling admission is now independent of presentation admission. A proven
pending drain before provider release can be checked on the next frame, even
when memory relief requires the ordinary settling guard. Such retries retain
their Backend classification and never gain readiness-only promotion credit.
Repeated pending results remain eligible for polling on consecutive frames.

The existing six-frame settling requirement, three coherent stereo frames,
next-cycle promotion, exact request ownership, resource retirement, memory
admission, mutation, device-loss, quarantine and recovery protections remain.
Unidentified reset/retirement waits retain their existing cadence. SE and AE
behavior is unchanged because both call sites are inside the VR relatch path.

The focused policy tests cover repeated Backend pending results, memory-relief
exclusion from promotion, default rejection, stale ownership, actual mutation,
failures, quarantine, recovery and counter saturation. The adjacent stretch
accounting policy also passes under MSVC C++23 with /W4 /WX /permissive-.

The existing measurements in
[the canonical ledger](vr-render-scale-comparison-ledger.csv) and
[PR73 versus PR66 report](pr73-readiness-nvidia-comparison-20260911.md)
predate this implementation. They remain unchanged and are not evidence of
its performance. Actual savings depend on when each GPU drain becomes ready;
a shorter polling interval does not waive the fence or guarantee a five-frame
improvement. Runtime validation must also check retry and cleanup churn.
