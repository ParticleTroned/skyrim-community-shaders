"""Structural checks for the DXBC programs stored in engine shader caches."""

from __future__ import annotations

import struct


SHADER_STAGES = {".pso": 0, ".vso": 1, ".cso": 5}


def validate_dxbc(data: bytes, extension: str) -> None:
    """Reject malformed containers or a stage mismatch; do not validate instructions."""
    expected_stage = SHADER_STAGES.get(extension.lower())
    if expected_stage is None:
        raise ValueError(f"unsupported shader extension: {extension}")
    if len(data) < 32 or data[:4] != b"DXBC":
        raise ValueError("invalid DXBC header")
    version, size, count = struct.unpack_from("<III", data, 20)
    if version != 1 or size != len(data) or not 0 < count <= (size - 32) // 4:
        raise ValueError("invalid DXBC header size, version or chunk count")

    table_end = 32 + count * 4
    spans = []
    shader_found = False
    for index in range(count):
        offset = struct.unpack_from("<I", data, 32 + index * 4)[0]
        if not table_end <= offset <= size - 8:
            raise ValueError("DXBC chunk header is outside the payload")
        kind, length = struct.unpack_from("<4sI", data, offset)
        end = offset + 8 + length
        if end > size:
            raise ValueError("truncated DXBC chunk")
        spans.append((offset, end))
        if kind in (b"SHDR", b"SHEX"):
            if shader_found:
                raise ValueError("multiple DXBC shader programs")
            if length < 8 or length % 4:
                raise ValueError("invalid DXBC shader program size")
            token, word_count = struct.unpack_from("<II", data, offset + 8)
            if word_count * 4 != length:
                raise ValueError("DXBC shader program length disagrees with its chunk")
            if token >> 16 != expected_stage:
                raise ValueError(f"DXBC shader stage does not match {extension}")
            shader_found = True

    previous_end = table_end
    for start, end in sorted(spans):
        if start < previous_end:
            raise ValueError("overlapping DXBC chunks")
        previous_end = end
    if not shader_found:
        raise ValueError("missing DXBC shader program")
