"""Download one vcpkg asset through Python's TLS stack.

vcpkg validates the asset digest after this helper returns.  This wrapper is
only selected by the repository launcher in isolated Codex environments where
the native Windows downloader cannot acquire Schannel credentials.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import sys
import time
import urllib.error
import urllib.request


def download(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.{os.getpid()}.part")

    try:
        for attempt in range(3):
            try:
                offset = temporary.stat().st_size if temporary.exists() else 0
                headers = {"User-Agent": "CSX-vcpkg-downloader/1"}
                if offset:
                    headers["Range"] = f"bytes={offset}-"
                request = urllib.request.Request(
                    url,
                    headers=headers,
                )
                with urllib.request.urlopen(request, timeout=60) as response:
                    content_length = response.headers.get("Content-Length")
                    content_range = response.headers.get("Content-Range")
                    if offset and response.status == 206:
                        if not content_range:
                            raise OSError(
                                "resumed response has no Content-Range header"
                            )
                        returned_range, total_length = content_range.split("/", 1)
                        returned_start = int(
                            returned_range.split(" ", 1)[1].split("-", 1)[0]
                        )
                        if returned_start != offset:
                            raise OSError(
                                "invalid resumed response: "
                                f"requested byte {offset}, received {content_range}"
                            )
                        expected_length = int(total_length)
                        mode = "ab"
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
                    raise OSError(
                        "incomplete download: "
                        f"expected {expected_length} bytes, "
                        f"received {received_length}"
                    )
                os.replace(temporary, destination)
                return
            except (OSError, ValueError, urllib.error.URLError):
                if attempt == 2:
                    raise
                time.sleep(2**attempt)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: vcpkg-download.py <url> <destination>", file=sys.stderr)
        return 2

    download(sys.argv[1], Path(sys.argv[2]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
