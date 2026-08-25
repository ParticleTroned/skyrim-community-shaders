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
                request = urllib.request.Request(
                    url,
                    headers={"User-Agent": "CSX-vcpkg-downloader/1"},
                )
                with urllib.request.urlopen(request, timeout=60) as response:
                    with temporary.open("wb") as output:
                        shutil.copyfileobj(response, output)
                os.replace(temporary, destination)
                return
            except (OSError, urllib.error.URLError):
                temporary.unlink(missing_ok=True)
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
