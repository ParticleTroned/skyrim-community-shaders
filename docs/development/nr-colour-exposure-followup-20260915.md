# NR colour exposure follow-up, 2026-09-15

## Live retest of 9b9d88467

The null-driver Dragonsreach run used source
`9b9d884679e229f9d06b5298677e65417c59a1b3`, producer Build ID
`0dc2f7ec936fec28b27e8c1753923c11d1fe21264341249082278d72ed7c1064`,
and DLL SHA-256
`577f3d706a3d40af2b42e1b7204f37513c04679135634feda5f2dc8f5529c965`.
The adjacent manifest, enabled physical AIO provider, loose providers,
Overwrite and unmanaged Data were checked once. This was a functional NR
retest, not an AIO validation campaign or render-scale qualification.

Raw evidence is retained locally at
`build/validation/nr-live-20260915/retest-9b9d88467-20260915T114021Z`.
`summary.json`, `live-results.json`, request/response journals, both
submission-eye images and the activity recording preserve the results.

-   Twelve insertion changes resumed NR at the next observed engine frame.
    Eleven have a contiguous transition-frame receipt; one starts immediately
    after the change. The recorded pairs were 18407→18408, 18428→18429,
    18449→18450, 18470→18471, 18491→18492, 18512→18513,
    18533→18534, 18554→18555, 18575→18576, 18596→18597,
    18617→18618 and 18638→18639. No full backend reset or later fallback
    was observed. This establishes engine-frame recovery, not HMD refresh time.
-   Twenty face-only/all-category changes passed, ten at each insertion.
    No renderer or character-preparation failures were reported.
-   Early identity transport had 31 fresh complete groups, including ten
    four-region groups. Late transport had 48 fresh four-region groups.
    Both passed the production offline assessor with zero measured change,
    zero maximum error and zero invalid samples.
-   Each eye ended with 22,641 readback attempts, 22,640 completed successes
    and zero fallbacks; the last request remained in flight at that sample.
-   Left/right submission captures at frame 21045, cycle 14354 showed the
    seated and standing characters. This does not verify physical display.
-   A redundant insertion setter returned `nr_configure_noop` and aborted the
    first transport scenario before measurement. The corrected scenario
    omitted that setter and passed. The failed receipt remains preserved.

The first activity trace recorded 685,319 ms, with no unrecorded tail.
SHA-256:
`eff0068ff4ea33ad2697ab0960481784576539c1151da7a2382ea93f96f04ca3`.

## Exposure result and subsequent implementation

Capture reached the live 2x2 R11G11B10_FLOAT view with linear/clamp sampling.
Its four average/target pairs were:
`(.1044921875,.1044921875)`, `(.10546875,.10546875)`,
`(.111328125,.111328125)`, `(.1123046875,.1123046875)`.
The old scalar test rejected the differing raw pairs even though every pair
had equal positive channels.

Late insertion did obtain exact current-frame GPU snapshots during the
insertion sequence. Later, observation became intermittent: capture count
stayed at 5,279 across the correction samples, with last binding frame 29566.
Early samples at 30047–30080 and late samples at 30103–30134 lacked matching
exposure. Both retained baseline and reported invalid codec samples; zero
image change was not accepted as successful correction. No binding rejection
or readback drop explained the observation gaps.

Reloading CSXTest01 did not restore regular capture: count 5,290 and last
binding 49791 remained unchanged across the post-load samples at
50477–50601. The follow-up activity trace is
`exposure-followup-activity.json`, SHA-256
`990783990a9eeafb1de63df4d17195d0df5dbf1ee49286ff9aef9329963ffd25`.
Its deliberately bounded five-minute recording reached its limit and was
finalized 37,660 ms later. That tail is not recorded evidence. Recording is
verified idle; the 30-minute recording test was not run.

The follow-up source change:

1. Establishes exact HDR producer scope through the existing ImageSpace
   Render/Dispatch wrappers, even with frame annotations disabled.
   Captures cover all seven D3D11 draw forms. Scope and draw counters expose
   which boundary was actually reached; the precise missing old callback
   is not proven by the old DLL.
2. Admits a measured unit ratio when every texel has finite positive
   `x == y`. Supported filtering preserves equal channels. Raw texels
   remain available; other spatial fields, zeros and nonfinite values
   cannot acquire scalar validity through this exception.
3. Keeps `captured_hdr` strict. Adds explicit `captured_hdr_previous` for
   an early invocation before current-frame HDR: only the immediately
   preceding source frame is eligible, with its real producer stamp and
   `exposureAgeFrames`. This is a latency experiment, not a claim to know
   a future tonemap value. Missing, ambiguous, older or cross-epoch samples
   retain baseline. Both eyes/regions reuse one immutable choice.
4. Adds the source to the UI, DevBench schema and optional automatic
   assessment candidates (`--include-captured-previous`).

## Validation at implementation commit

Passed before commit: 40 runner fixtures, 15 assessment fixtures, six
source contracts and all six inventory entries via:

```text
python tests/neural_color/runner_test.py
python tests/neural_color/assessment_test.py
python tests/neural_color/source_contract_test.py
python tools/nr-color/verify_assets.py
```

The WARP regression includes the observed 2x2 values, individual invalid
texels, filtered sampling and preparation/reconstruction. Lifecycle tests
cover strict versus previous-frame selection, age, epoch, ambiguity and
stereo latching. Windows compilation and those executable tests must run
after this implementation is committed; their exact results belong to the
new build receipt under `build/validation/nr-live-20260915/exposure-followup`.

The running DLL predates this follow-up. Its replacement still needs live
capture-frequency, strict late-exposure and explicit early-history tests.
Correct NVIDIA NR colour treatment, adaptation quality during transitions,
flat-runtime behaviour, physical HMD presentation and performance remain
separate qualification work.
