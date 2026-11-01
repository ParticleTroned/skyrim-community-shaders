"""Synthetic containers for structural checks, not executable shader fixtures."""

import struct


def make_dxbc(stage: int = 0, marker: bytes = b"", kind: bytes = b"SHEX") -> bytes:
    program = struct.pack("<II", stage << 16 | 0x50, 2)
    return make_container([(kind, program), (b"PRIV", marker)])


def make_container(chunks: list[tuple[bytes, bytes]]) -> bytes:
    offset = 32 + len(chunks) * 4
    offsets = []
    parts = []
    for kind, payload in chunks:
        offsets.append(offset)
        part = struct.pack("<4sI", kind, len(payload)) + payload
        parts.append(part)
        offset += len(part)
    return (
        b"DXBC" + bytes(16) + struct.pack("<III", 1, offset, len(chunks))
        + b"".join(struct.pack("<I", value) for value in offsets)
        + b"".join(parts)
    )
