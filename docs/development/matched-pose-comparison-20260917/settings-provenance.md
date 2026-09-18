# Settings and build audit

Same HMD position/orientation was confirmed by the user; pose equality was not independently measured. Active FOV+TAA is centre 0.30 / outer 0.70. The saved FOV-only centre 0.60 is inactive and is not an active-setting mismatch. SSGI is enabled with GI off and AO/IL interiors-only. Before/after snapshots are not continuous per-save settings evidence. All exposed fields and unavailable snapshots are retained in comparison.json.

| Run         | Phase  | Shared fields | Differences against Baseline R1 before                                            |
| ----------- | ------ | ------------: | --------------------------------------------------------------------------------- |
| Head R1     | before |           813 | none                                                                              |
| Head R1     | after  |           813 | none                                                                              |
| Head R2     | before |           813 | none                                                                              |
| Head R2     | after  |           813 | none                                                                              |
| Head R3     | before |           813 | none                                                                              |
| Head R3     | after  |           813 | none                                                                              |
| Baseline R1 | before |           813 | none                                                                              |
| Baseline R1 | after  |   unavailable | "A verified semantic outcome was required, but the response did not provide one." |
| Baseline R2 | before |           813 | none                                                                              |
| Baseline R2 | after  |           813 | none                                                                              |
| Baseline R3 | before |           813 | none                                                                              |
| Baseline R3 | after  |   unavailable | "A verified semantic outcome was required, but the response did not provide one." |

## Exact build identity

| Build    | Source                                   | Dirty digest                                                     | Build ID                                                         | DLL SHA-256                                                      | PDB SHA-256                                                      |
| -------- | ---------------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------- |
| Baseline | a1a11fe0d722fd6dbc18315a023701e0ec4a84de | None                                                             | ee97357e1005ab9fc04025938a1875e7af689c1c4409a0df74e756b6b2974ca6 | 9376CA0C6F228FCDE3F29AF45E271CC36FC3B847432C3CA01D5809F4743D9141 | 4A289D685B8E0F8258A4FCBF07CD8A7DB1E5A3F0E8D091EFC94EAE9A6339C017 |
| Head     | 6588831aabf472dff76340ed58aaf07075fccacd | 0a05e13e172fc466fc282a19879bea1f895a641f493829275aa9555db21848eb | b78946fd101bdb509c418e23b7e3cd1b2d8a27e2fdaf8c270be05a56c472c733 | C758BEEA6C304DEDB91D23F8F26868B23D380C8597EB2C016C8A593FD05F5CB6 | C251760FF11EE102740E66C745BA845DF3AC0085C11F476201B42224CAAB2892 |

Baseline diagnostic source a1a11fe0 retains renderer base 190c28a39a52c2bace475d1143a124c5893ac741. Tested head is the attributed dirty 6588831a build, not the current analysis branch HEAD. Vendor runtime versions differ; this is a whole-build comparison, not an isolated material/culling A/B. Baseline R1/R3 after snapshots failed; measurement completion, physical DLL verification and trace completion do not fill that settings gap.
