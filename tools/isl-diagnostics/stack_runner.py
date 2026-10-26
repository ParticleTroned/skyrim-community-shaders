"""Collect six bounded ISL engine windows with owned Windows CPU stack/wait tracing."""

import argparse
import json
from pathlib import Path
import sys

import runner as isl
from stack_wait import OwnedTrace, control


class StackCampaign(isl.Campaign):
    external_trace = True

    def __init__(self, args, config):
        super().__init__(args, config)
        self.trace = OwnedTrace(self.root, args.recorder_directory, args.wpr_path)

    def phase(self, phase):
        if not self.trace.requested:
            self.trace.start()
        self.trace.ensure_active()
        super().phase(phase)
        self.trace.ensure_active()

    def run(self):
        result = 1
        trace = {"state": "not_started", "filesSaved": False}
        try:
            result = super().run()
        finally:
            try:
                trace = self.trace.finish()
            except (Exception, KeyboardInterrupt) as error:
                trace = {"state": "finalization_unconfirmed", "filesSaved": False,
                         "error": f"{type(error).__name__}: {error}",
                         "stopRequested": self.trace.stop_requested,
                         "recovery": "preserve the owned worker and output; do not rerun; " +
                         ("stop was requested; inspect the eventual trace-result.json" if self.trace.stop_requested else
                          "stop request was not confirmed; the worker also watches owner exit and its recording deadline")}
            summary = {"engineExitCode": result, "trace": trace,
                       "complete": result == 0 and trace["filesSaved"], "analysis": "pending_offline"}
            self.save("stack-campaign.json", summary)
            (self.root / "STACK-RESULT.txt").write_text(
                ("ALL FILES SAVED" if summary["complete"] else "INCOMPLETE / PRESERVE ALL FILES") + "\n" +
                json.dumps(summary, indent=2) + "\n", encoding="utf-8")
            if summary["complete"]:
                print("ALL FILES SAVED: 6/6 engine windows and Windows trace. You can close Skyrim and this terminal.", flush=True)
            else:
                print("INCOMPLETE: preserve the entire output and console errors. Do not rerun automatically.", flush=True)
                print("Check STACK-RESULT.txt and stack-wait/trace-result.json for finalization status.", flush=True)
            print(f"Send the ENTIRE output folder, including stack-wait: {self.root}", flush=True)
        return int(not summary["complete"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("controller", "runtime", "recorder-directory"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--output")
    parser.add_argument("--wpr-path")
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    try:
        recorder = control("preflight", args.recorder_directory, args.wpr_path)
        args.wpr_path = recorder["path"]
        if args.check_only:
            print("PREREQUISITES OK: elevated PowerShell and supported Windows recorder. No recording or game changes made.")
            return 0
        if not args.output:
            raise ValueError("an output directory is required")
        args.plan, args.disable_neural_rendering, args.compare_neural_rendering = "stacks", True, False
        config = json.loads((Path(__file__).with_name("core.json")).read_text(encoding="utf-8-sig"))
        print("Six 10-second windows: ISL ON / OFF / ON, then ON / OFF / ON. NR is left OFF.", flush=True)
        print("Actual engine recording: 60 seconds total. Setup, settling and file saving add time.", flush=True)
        return StackCampaign(args, config).run()
    except (Exception, KeyboardInterrupt) as error:
        print(f"STACK TEST FAILED: {type(error).__name__}: {error}", file=sys.stderr, flush=True)
        print("Preserve any output already written. No automatic retry.", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
