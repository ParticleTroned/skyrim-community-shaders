"""Verify the portable PR96 evidence export using only the standard library."""

from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify():
    root = Path(__file__).resolve().parent
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    entries = manifest["files"]
    names = [entry["path"] for entry in entries]
    require(len(names) == len(set(names)), "Duplicate manifest paths")
    journal_total = 0
    native_events = {}

    for entry in entries:
        path = (root / entry["path"]).resolve()
        require(path.is_relative_to(root), "Manifest path escapes bundle")
        require(path.is_file(), f"Missing file: {entry['path']}")
        raw = path.read_bytes()
        require(len(raw) == entry["exportBytes"], f"Size mismatch: {entry['path']}")
        require(
            hashlib.sha256(raw).hexdigest() == entry["exportSha256"],
            f"Hash mismatch: {entry['path']}",
        )
        if not entry["path"].endswith("events.jsonl.gz"):
            if path.suffix == ".json":
                json.loads(raw)
            continue

        summary = json.loads(
            (path.parent / "events.summary.json").read_text(encoding="utf-8")
        )
        decoded_hash = hashlib.sha256()
        decoded_bytes = 0
        counts = Counter()
        count = 0
        with gzip.open(path, "rb") as stream:
            for line in stream:
                decoded_hash.update(line)
                decoded_bytes += len(line)
                event = json.loads(line)
                count += 1
                require(event["sequence"] == count, f"Journal gap: {entry['path']}")
                counts[event["event"]] += 1
                if path.parent.name == "pid-22880" and event["sequence"] in (
                    274537, 274583, 274626
                ):
                    native_events[event["sequence"]] = event
        require(count == entry["records"] == summary["retained"], "Journal count mismatch")
        require(counts == summary["eventCounts"], "Journal event totals mismatch")
        require(decoded_bytes == entry["decodedBytes"], "Decoded size mismatch")
        require(decoded_hash.hexdigest() == entry["decodedSha256"], "Decoded hash mismatch")
        journal_total += count

    begin, destroyed, end = (native_events[key] for key in (274537, 274583, 274626))
    require(
        (begin["event"], destroyed["event"], end["event"])
        == ("light_render_begin", "light_destroy_end", "light_render_end"),
        "Unexpected overlap event types",
    )
    require(begin["object"] == destroyed["object"] == end["object"], "Object mismatch")
    require(begin["generation"] == destroyed["generation"] == end["generation"] == 28,
            "Generation mismatch")
    require(begin["tick"] < destroyed["tick"] < end["tick"], "Invalid lifetime ordering")
    require(begin["thread"] == end["thread"] != destroyed["thread"], "Thread mismatch")
    summary = json.loads((root / "pid-22880/events.summary.json").read_text(encoding="utf-8"))
    overlap_ms = 1000 * (end["tick"] - destroyed["tick"]) / summary["frequency"]
    require(abs(overlap_ms - 0.6308) < 1e-9, "Lifetime overlap differs from report")
    print(f"PASS: {len(entries)} evidence files; {journal_total:,} journal records")
    print(f"PASS: generation 28 destroyed {overlap_ms:.4f} ms before render returned")


if __name__ == "__main__":
    verify()
