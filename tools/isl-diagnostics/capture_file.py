"""Decode native joint recordings without a game, controller, or third-party package."""

import json

FORMAT = "csx-isl-capture-v1"


def read_capture(source):
    """Retain complete rows from an interrupted file; report every structural failure."""
    header = json.loads(source.readline())
    if header.get("format") != FORMAT:
        raise ValueError("unsupported capture format")
    raw, side = dict(header["raw"], records=[]), dict(header["isl"], records=[])
    columns = header["sideColumns"]
    if len(set(columns)) != len(columns) or not {"ordinal", "metric", "caller"} <= set(columns):
        raise ValueError("invalid sidecar columns")
    failures, footer = [], None
    for number, line in enumerate(source, 2):
        try:
            row = json.loads(line)
            if footer is not None:
                raise ValueError("data after completion footer")
            if isinstance(row, dict):
                if row.get("complete") is not True:
                    raise ValueError("invalid completion footer")
                footer = row
            elif isinstance(row, list) and len(row) == 2 and row[0] == 0:
                raw["records"].append(row[1])
            elif isinstance(row, list) and row[0] == 1 and len(row) == len(columns) + 1:
                value = dict(zip(columns, row[1:]))
                metric = value["metric"]
                if type(metric) is not int or not 0 <= metric < len(header["metrics"]):
                    raise ValueError("invalid metric index")
                value["metric"] = header["metrics"][metric]
                caller = value["caller"]
                value["caller"] = header["callers"][str(caller)] if caller else None
                side["records"].append(value)
            else:
                raise ValueError("invalid row type or column count")
        except (ValueError, KeyError, TypeError, IndexError) as error:
            failures.append(f"line {number}: {error}")
    if footer is None:
        failures.append("completion footer missing; recovered rows only")
    for name, document in (("raw", raw), ("isl", side)):
        total = len(document["records"])
        if total != document["total"] or footer and footer.get(name + "Total") != total:
            failures.append(f"{name}: count mismatch ({total} recovered, {document['total']} declared)")
    return raw, side, {"complete": not failures, "limitations": failures}


def pages(document):
    """Adapt a metadata-once export to the existing validators without duplicating rows."""
    rows = document["records"]
    header = {k: v for k, v in document.items() if k not in ("records", "offset", "nextOffset")}
    return [header | {"records": rows[offset:offset + 256], "offset": offset,
                      "nextOffset": offset + 256 if offset + 256 < document["total"] else None}
            for offset in range(0, max(1, len(rows)), 256)]
