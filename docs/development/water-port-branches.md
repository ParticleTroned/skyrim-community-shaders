# Fork-only water port work branches

Baseline: `d64dc05b60d38774f43797b9d4e6c1f357b8b1f8`. Donor: `f140c878138fe2783a6a4ad40420a3a2a996ccd0`.

No upstream PRs. These are preparation branches, not merge-approved changes.

| Work branch | Commit | Type | Required parent |
|---|---|---|---|
| `work/os-water-04-flowmap-20261101` | `1074a4b0ab` | BUG / CORRECTION | pinned baseline |
| `work/os-water-02-refraction-20261101` | `101d67d123` | BUG / CORRECTION | pinned baseline |
| `work/os-water-06-cache-20261101` | `05eee84c96` | BUG / CORRECTION | 04-flowmap |
| `work/os-water-03-child-worldspace-20261101` | `1a1b26f1e7` | CORRECTION | pinned baseline |
| `work/os-water-05-lifetime-20261101` | `dfc440cd21` | CORRECTION | pinned baseline |
| `work/os-water-07-flowmap-lock-20261101` | `3202b55301` | CORRECTION | pinned baseline |
| `work/os-water-08-parallax-20261101` | `b48143a395` | CORRECTION / FEATURE | pinned baseline |
| `work/os-water-09-history-20261101` | `889a2ba61d` | CORRECTION | pinned baseline |
| `work/os-water-10a-reflection-source-20261101` | `1d93527072` | BUG | pinned baseline |
| `work/os-water-10b-ssr-20261101` | `6b6436cba0` | CORRECTION | 10a-reflection-source |
| `work/os-water-10c-reflection-height-20261101` | `add49f697a` | FEATURE | 10a-reflection-source |
| `work/os-water-11a-light-eligibility-20261101` | `4005635b03` | BUG | pinned baseline |
| `work/os-water-11b-contact-candidates-20261101` | `a1d130be86` | CORRECTION | 11a-light-eligibility |
| `work/os-water-shallow-base-20261101` | `59a05576f0` | INTEGRATION | 04-flowmap, 02-refraction |
| `work/os-water-01-shallow-20261101` | `74099182b4` | FEATURE | shallow-base |

## Scope and test controls

Group 1: set Unified Water / Shallow Surface Strength above 0 to test; default 0 preserves the existing appearance.
Group 8: enable Foveated Water Parallax with an active VR foveation profile. Disabled/unavailable masks retain full detail. No path may exceed 64 steps.
Group 10c: Water Reflection Strength -1 preserves current output. Nonnegative values enable height-faded strength.
Group 11b: Water Contact Candidates 0 retains the existing loop. Values 1-8 enable the experimental shared per-cluster candidate pass; shadow-map contributions remain untouched.

## Dependencies

04 -> 06. Both 04 and 02 -> shallow-base -> 01. 10a -> 10b and 10c. 11a -> 11b. Other work branches start independently from the pinned baseline.
The integration commit has the work-branch tips as parents and contains their verified combined source tree; it is not an upstream PR.

## Deliberately excluded or narrowed

Source 1 (an extra scene-depth copy) remains deferred until a skipped-deferred failure is demonstrated. The depth debug toggle is unchanged.
Group 3 is a transition-test candidate, not a reproduced upstream bug. Plane refresh is restricted to generated horizontal tiles.
Group 5 adds appended-object guards and duplicate occurrences of the current node; it does not add broad global cleanup scans.
Group 6 keeps startup synchronous. Runtime cache publication runs on the game thread and failed/incomplete results retain the previous runtime snapshot.
Group 7 keeps immediate-context execution and saving/readback protected. Only source loading/conversion and deferred recording move outside that scope. Profile generation-time contention.
Group 11b adapts candidate budgeting to existing upstream grid padding rather than importing CSX global buffers. Its extra compute cost must be measured; no performance gain is claimed. Separate VR contact-shadow detail gating is not imported.
No 256-step ceiling, blanket Shadow/PortalStrict exclusions, NR runtime DLL, version bump or release tag is added.

## Validation required before upstream submission

Run Windows plugin compilation, full/release-parity SE/VR shader compilation and shader/C++ unit tests. Then test in-game: corrupt/missing flowmap; failed cache generation and restart retry; child-worldspace and interior/map transitions; shoreline/deep-water output; both VR eyes and render scales; planar/cubemap/SSR permutations; eligible and ineligible engine lights plus particles; candidate budgets 0 and 1-8.
Check the D3D debug layer and paired GPU timings. Defaults being unchanged does not replace these tests.
