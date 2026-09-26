#!/usr/bin/env python3
"""Validate the manifest fixture and its required provenance with Draft 2020-12."""

import copy
import json
import pathlib
import sys

from jsonschema import Draft202012Validator
from jsonschema.exceptions import ValidationError


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
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
