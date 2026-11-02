# Water dev rebase — 2026-11-02

Old upstream: `d64dc05b60d38774f43797b9d4e6c1f357b8b1f8`. New upstream: `34b4b82881d2507ebba26a958f471a7e5f8a7742`.

All original water and repair commits have been replayed with their dependency/merge topology intact. Every non-upstream-overlapping source file is byte-identical to its old branch snapshot; the only overlap is the translation catalog, whose key/value changes from both sides are preserved.

| Branch | Before | Rebased source |
|---|---|---|
| `work/os-water-04-flowmap-20261101` | `1dcf4adc6e` | `44fa6cf8a3` |
| `work/os-water-02-refraction-20261101` | `e3e44eaaa9` | `3793e3b8b2` |
| `work/os-water-06-cache-20261101` | `99de91ced5` | `0aa22e28b2` |
| `work/os-water-03-child-worldspace-20261101` | `88b92dd7d9` | `1f1b71cffe` |
| `work/os-water-05-lifetime-20261101` | `dfc440cd21` | `273bd3a65e` |
| `work/os-water-07-flowmap-lock-20261101` | `3202b55301` | `7ffc2dced0` |
| `work/os-water-08-parallax-20261101` | `87621e7f8a` | `b994fcf16a` |
| `work/os-water-09-history-20261101` | `76b2b5c420` | `26910655c9` |
| `work/os-water-10a-reflection-source-20261101` | `9e08766a02` | `5fc5264e37` |
| `work/os-water-10b-ssr-20261101` | `76ce4e459d` | `e6287269ce` |
| `work/os-water-10c-reflection-height-20261101` | `47b79a46cc` | `155d0bd12a` |
| `work/os-water-11a-light-eligibility-20261101` | `4005635b03` | `83aa665f2f` |
| `work/os-water-11b-contact-candidates-20261101` | `0a1f389f04` | `fdaa8ee877` |
| `work/os-water-shallow-base-20261101` | `02b839baa6` | `00d221df83` |
| `work/os-water-01-shallow-20261101` | `cd7370c1b8` | `5f12378f91` |
| `work/os-water-base-20261101` | `d64dc05b60` | `34b4b82881` |
| `build/os-dev-water-integration-20261101` | `362160e22b` | `d3e4618c9b` |

Backups: `backup/os-water-pre-dev-34b4b82881-20261102/`. No main-VR/main-SE update or upstream PR is part of publication. Previous successful AIO packages are not packages of this rebased source. New Windows CI and in-game verification remain required.
