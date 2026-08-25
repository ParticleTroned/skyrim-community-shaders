"""Download one repository asset through Python's TLS stack.

The optional digest is used by the CMake asset helper.  vcpkg validates its own
asset digest after this helper returns.  This wrapper is only selected by the
repository launcher in isolated Codex environments where the native Windows
downloader cannot acquire Schannel credentials.
"""

from __future__ import annotations

import hashlib
import http.client
import os
from pathlib import Path
import re
import shutil
import sys
import time
import urllib.error
import urllib.request


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_content_range(value: str, offset: int) -> tuple[int, int]:
    match = re.fullmatch(r"bytes (\d+)-(\d+)/(\d+)", value)
    if not match:
        raise OSError(f"invalid Content-Range header: {value!r}")

    start, end, total = (int(part) for part in match.groups())
    if start != offset or end < start or end >= total:
        raise OSError(
            f"invalid ranged response: requested byte {offset}, received {value}"
        )
    return end - start + 1, total


def download(
    url: str, destination: Path, expected_sha256: str | None = None
) -> None:
    if expected_sha256 is not None:
        expected_sha256 = expected_sha256.lower()
        if not re.fullmatch(r"[0-9a-f]{64}", expected_sha256):
            raise ValueError("expected SHA-256 must contain 64 hexadecimal digits")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.{os.getpid()}.part")

    try:
        for attempt in range(3):
            try:
                if (
                    expected_sha256 is not None
                    and temporary.exists()
                    and file_sha256(temporary) == expected_sha256
                ):
                    os.replace(temporary, destination)
                    return

                offset = temporary.stat().st_size if temporary.exists() else 0
                headers = {"User-Agent": "CSX-vcpkg-downloader/1"}
                if offset:
                    headers["Range"] = f"bytes={offset}-"
                request = urllib.request.Request(
                    url,
                    headers=headers,
                )
                with urllib.request.urlopen(request, timeout=60) as response:
                    if response.status not in (200, 206):
                        raise OSError(f"unexpected HTTP status {response.status}")

                    content_length = response.headers.get("Content-Length")
                    content_range = response.headers.get("Content-Range")
                    if response.status == 206:
                        if not content_range:
                            raise OSError(
                                "partial response has no Content-Range header"
                            )
                        range_length, expected_length = parse_content_range(
                            content_range, offset
                        )
                        if (
                            content_length is not None
                            and int(content_length) != range_length
                        ):
                            raise OSError(
                                "partial response length disagrees with "
                                f"Content-Range: {content_length} != {range_length}"
                            )
                        mode = "ab" if offset else "wb"
                    else:
                        offset = 0
                        expected_length = (
                            int(content_length)
                            if content_length is not None
                            else None
                        )
                        mode = "wb"

                    with temporary.open(mode) as output:
                        shutil.copyfileobj(response, output)
                received_length = temporary.stat().st_size
                if expected_length is not None and received_length != expected_length:
                    if received_length > expected_length:
                        temporary.unlink()
                    raise OSError(
                        "incomplete download: "
                        f"expected {expected_length} bytes, "
                        f"received {received_length}"
                    )
                if (
                    expected_sha256 is not None
                    and file_sha256(temporary) != expected_sha256
                ):
                    temporary.unlink()
                    raise OSError("downloaded asset SHA-256 does not match")
                os.replace(temporary, destination)
                return
            except (
                OSError,
                ValueError,
                http.client.HTTPException,
                urllib.error.URLError,
            ) as error:
                if isinstance(error, urllib.error.HTTPError) and error.code == 416:
                    temporary.unlink(missing_ok=True)
                if attempt == 2:
                    raise
                time.sleep(2**attempt)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(
            "usage: vcpkg-download.py <url> <destination> [sha256]",
            file=sys.stderr,
        )
        return 2

    expected_sha256 = sys.argv[3] if len(sys.argv) == 4 else None
    download(sys.argv[1], Path(sys.argv[2]), expected_sha256)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
