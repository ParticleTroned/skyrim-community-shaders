import sys

from .cli import main as cli_main


if __name__ == "__main__":
    if any(arg == "--gui" for arg in sys.argv[1:]):
        filtered_args = [arg for arg in sys.argv[1:] if arg != "--gui"]
        sys.argv = [sys.argv[0], *filtered_args]
        from .gui import main as gui_main

        raise SystemExit(gui_main())
    raise SystemExit(cli_main())
