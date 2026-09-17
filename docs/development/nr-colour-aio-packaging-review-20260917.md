# NR test AIO packaging preservation

The first reviewed NR AIO build exposed a second destructive packaging
hook: with individual archives disabled, AIO packaging still removed the
entire source checkout's `dist` directory after linking the DLL. Disabling
`ZIP_TO_DIST` alone therefore did not preserve earlier artifacts.

Both directory-wide `dist` removals are removed. Individual packaging still
refreshes its private `zip` staging folder, and the existing directory
creation and timestamped archive rules remain. This is a build-only
change; NR shader bytes, runtime settings and package selection are
unchanged. Explicitly invoking a target with the same generated filename
can still replace that one output; this change prevents deletion of
unrelated archives and receipts.

The first attempt removed seven older individual archives and two AIO
receipts. Both receipts were restored byte-for-byte from preserved
evidence. The seven archives were reconstructed from retained inputs;
all 351 payload file occurrences match the previous AIO's retained size
and SHA-256 records. Their container hashes differ from the originals:
they are recovered packages, not byte-identical archive restorations.
The recovery keeps this distinction explicit. The prior AIO archive was
already absent before the build; its extracted evidence remains intact.

The unchanged shader's correction and adversarial test results are
recorded in the [rounding review](nr-colour-packed-rounding-review-20260917.md).
Packaging evidence, original hashes, recovery results and the subsequent
archive verification are retained locally under
`build/validation/nr-packed-rounding-aio-20260917`.
