#!/usr/bin/env python3
"""Validate screenshot requests and manifest provenance with Draft 2020-12."""

import copy
import json
import pathlib
import sys

from jsonschema import Draft202012Validator
from jsonschema.exceptions import ValidationError


def check_framing_requests(root: pathlib.Path) -> None:
    schema = json.loads(
        (root / "docs/development/schemas/screenshot-request-v1.schema.json").read_text(
            encoding="utf-8"
        )
    )
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)
    request = json.loads(
        (root / "tests/data/screenshot-api/capture-request-v1.json").read_text(
            encoding="utf-8"
        )
    )
    for output in (
        {"view": "framed_left"},
        {"view": "framed_combined", "resolution": "native"},
        {"view": "framed_right", "width": 2560, "height": 1440},
        {"view": "framed_right", "resolution": "4k", "width": 3840, "height": 2160},
        {"view": "left_eye"},
    ):
        request["capture"]["outputs"] = [output]
        validator.validate(request)
    for output in (
        {"view": "framed_left", "resolution": "native", "width": 3840},
        {"view": "framed_right", "width": 3840, "height": 1440},
        {"view": "framed_right", "resolution": "4k", "width": 2560},
        {"view": "left_eye", "resolution": "4k"},
    ):
        request["capture"]["outputs"] = [output]
        if validator.is_valid(request):
            raise RuntimeError(f"invalid framing request accepted: {output}")


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    check_framing_requests(root)
    schema = json.loads(
        (root / "docs/development/schemas/screenshot-sequence-manifest-v1.schema.json").read_text(
            encoding="utf-8"
        )
    )
    fixture = json.loads(
        (root / "tests/data/screenshot-api/sequence-manifest-v1.json").read_text(
            encoding="utf-8"
        )
    )

    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)
    validator.validate(fixture)

    missing_actual = copy.deepcopy(fixture)
    del missing_actual["children"][0]["artifacts"][0]["actual"]
    try:
        validator.validate(missing_actual)
    except ValidationError:
        pass
    else:
        raise RuntimeError("artifact without actual provenance unexpectedly satisfied the public schema")

    non_positive_dimensions = copy.deepcopy(fixture)
    non_positive_dimensions["children"][0]["artifacts"][0]["actual"]["width"] = 0
    try:
        validator.validate(non_positive_dimensions)
    except ValidationError:
        return 0
    raise RuntimeError("artifact with a non-positive width unexpectedly satisfied the public schema")


if __name__ == "__main__":
    sys.exit(main())
