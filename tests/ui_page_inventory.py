"""Extract production page declarations for native navigation coverage."""

import argparse
import json
import re
from pathlib import Path

TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}()[\],]', re.S)
PAGE = re.compile(r'(?:MenuUI::)?SettingsPage\s+page\(\s*("[^"\n]+")\s*,\s*\{')
STRING = re.compile(r'"(?:\\.|[^"\\])*"')


def closing(source, start):
    """Match delimiters without interpreting braces in strings or comments."""
    stack = []
    pairs = {"}": "{", ")": "(", "]": "["}
    for token in TOKEN.finditer(source, start):
        value = token.group()
        if value in "{([":
            stack.append(value)
        elif value in "})]":
            if not stack or stack.pop() != pairs[value]:
                raise ValueError(f"unbalanced declaration at {start}")
            if not stack:
                return token.end()
    raise ValueError(f"unclosed declaration at {start}")


def fields(source):
    result, start, depth = [], 0, 0
    for token in TOKEN.finditer(source):
        value = token.group()
        if value in "{([":
            depth += 1
        elif value in "})]":
            depth -= 1
        elif value == "," and depth == 0:
            result.append(source[start:token.start()].strip())
            start = token.end()
    if source[start:].strip():
        result.append(source[start:].strip())
    return result


def inventory(root):
    result = []
    files = sorted(root.glob("src/**/*.cpp"))
    for path in files:
        source = path.read_text(encoding="utf-8")
        for match in PAGE.finditer(source):
            start = match.end() - 1
            declaration = source[start + 1:closing(source, start) - 1]
            sections = []
            for entry in fields(declaration):
                if not entry.startswith("{"):
                    raise ValueError(f"nonliteral section in {path}")
                parts = fields(entry[1:-1])
                if len(parts) < 3 or any(not STRING.fullmatch(p) for p in parts[:3]):
                    raise ValueError(f"missing ID, title or description in {path}")
                sections.append(parts)
            ids = [s[0] for s in sections]
            if len(ids) != len(set(ids)) or any(i in ('"overview"', '"performance"', '"profiling"') for i in ids):
                raise ValueError(f"duplicate or reserved section ID in {path}")
            result.append({"id": match[1], "file": path.relative_to(root).as_posix(), "sections": sections})
    ids = [page["id"] for page in result]
    if not result or len(ids) != len(set(ids)):
        raise ValueError("missing or duplicate production page IDs")
    for path in files:
        source = path.read_text(encoding="utf-8")
        for match in re.finditer(r'SettingsPage::(?:Select|Navigate)\(("[^"\n]+"),\s*("[^"\n]+")\)', source):
            page = next((p for p in result if p["id"] == match[1]), None)
            if not page or match[2] not in ['"overview"', '"performance"', '"profiling"', *[s[0] for s in page["sections"]]]:
                raise ValueError(f"unknown navigation destination {match.group()} in {path}")
    return result


def write_header(pages, path):
    lines = ["// Generated from production section metadata; feature control bodies are tested separately.",
             "struct UiReviewRoute { const char* page; const char* section; bool conditional; };",
             "constexpr UiReviewRoute uiReviewRoutes[] = {"]
    for page in pages:
        for section in page["sections"]:
            conditional = len(section) > 4 and section[4] != "true"
            lines.append("    { " + page["id"] + ", " + section[0] + ", " + str(conditional).lower() + " },")
    lines += ["};", "void DrawUiReviewPage(std::string_view id, bool conditional) {"]
    for page in pages:
        lines += ["    if (id == " + page["id"] + ") {", "        MenuUI::SettingsPage page(" + page["id"] + ", {"]
        for section in page["sections"]:
            # Runtime gates are varied by the fixture; all layout text is copied verbatim.
            parts = section.copy()
            for i in range(3, len(parts)):
                if i == 4:
                    parts[i] = "true" if parts[i] == "true" else "conditional"
                elif i in (5, 9):
                    parts[i] = "true" if parts[i] == "true" else "false" if parts[i] == "false" else "conditional"
                elif not STRING.fullmatch(parts[i]) and parts[i] != "nullptr":
                    parts[i] = '""' if i == 3 else "nullptr"
            lines.append("            { " + ", ".join(parts) + " },")
        lines += ["        });", "        return;", "    }"]
    lines += ['    throw std::runtime_error("unknown review page");', "}"]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    pages = inventory(args.root)
    if args.header:
        write_header(pages, args.header)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(pages, indent=2) + "\n", encoding="utf-8")
    print(f"Validated {len(pages)} production pages and {sum(len(p['sections']) for p in pages)} section routes")


if __name__ == "__main__":
    main()
