from __future__ import annotations

import sys
from pathlib import Path

if __package__ is None or __package__ == "":
    repo_root = Path(__file__).resolve().parents[2]
    if str(repo_root) not in sys.path:
        sys.path.insert(0, str(repo_root))
    from tools.lp_resolver.gui import main
else:
    from .gui import main


if __name__ == "__main__":
    raise SystemExit(main())
