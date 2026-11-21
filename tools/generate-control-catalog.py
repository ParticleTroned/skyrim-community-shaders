"""Generate read-only search metadata from C++ UI declarations, without executing UI."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path


TOKEN = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|[LuU])?R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"'
    r'|(?:u8|[LuU])?"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    r'|[A-Za-z_]\w*|::|->|[^\s]'
)
IDENTIFIER = re.compile(r"^[A-Za-z_]\w*$")
CONTROL = re.compile(
    r"^(?:Slider(?:Float[234]?|Int[234]?|Scalar|Angle)(?:OnRelease)?|"
    r"Drag(?:Float[234]?|Int[234]?|Scalar)|Checkbox(?:Flags)?|UIntCheckbox|"
    r"ColorEdit[34]|Input(?:Text(?:WithHint|Multiline)?|Float|Int|Double)|"
    r"Combo|ComboBox|BeginCombo|RadioButton|Button|SmallButton|ButtonWithFlash|"
    r"ChoiceSetting|NavigationChoice|ChoiceCards|CollapsingHeader|TreeNode(?:Ex)?|SectionHeading)$"
)
NON_FUNCTION = {"if", "for", "while", "switch", "catch", "sizeof", "alignof", "decltype", "requires"}


def literal(tokens: list[str]) -> str | None:
    """Accept only literal strings; arbitrary expressions are never evaluated."""
    if not tokens or not all(re.fullmatch(r'(?:u8|[LuU])?"(?:\\.|[^"\\])*"', t) for t in tokens):
        return None
    try:
        return "".join(ast.literal_eval(t[t.index('"'):]) for t in tokens)
    except (ValueError, SyntaxError):
        return None


def evaluate_value(tokens, environment):
    text = literal(tokens)
    if text is not None:
        return text
    joined = "".join(tokens)
    if joined in environment:
        return environment[joined]
    if joined in ("true", "false"):
        return joined == "true"
    if joined.isdecimal():
        return int(joined)
    if "?" in tokens and ":" in tokens:
        question, colon = tokens.index("?"), tokens.index(":")
        condition = evaluate_value(tokens[:question], environment)
        if isinstance(condition, bool):
            return evaluate_value(tokens[question + 1:colon] if condition else tokens[colon + 1:], environment)
    return None


@dataclass
class Function:
    source: "Source"
    name: str
    params: list[str]
    start: int
    end: int

    @property
    def owner(self):
        return self.name.rpartition("::")[0]


class Source:
    def __init__(self, path: str, text: str):
        self.path = path
        self.tokens = [m.group() for m in TOKEN.finditer(text) if not m.group().startswith(("//", "/*"))]
        self.pairs = {}
        stack = []
        for i, token in enumerate(self.tokens):
            if token in ("(", "[", "{"):
                stack.append(i)
            elif token in (")", "]", "}") and stack:
                opening = stack[-1]
                if self.tokens[opening] == {")": "(", "]": "[", "}": "{"}[token]:
                    stack.pop()
                    self.pairs[opening] = i
                    self.pairs[i] = opening
        self.scopes = []
        for i, token in enumerate(self.tokens[:-2]):
            if token not in ("namespace", "struct", "class") or not IDENTIFIER.fullmatch(self.tokens[i + 1]):
                continue
            j = i + 2
            while j < len(self.tokens) and self.tokens[j] not in ("{", ";", "(", "=", ",", ">"):
                j += 1
            if j in self.pairs and self.tokens[j] == "{":
                name = self.tokens[i + 1]
                if token == "namespace" and all(t == "::" or IDENTIFIER.fullmatch(t) for t in self.tokens[i + 1:j]):
                    name = "".join(self.tokens[i + 1:j])
                self.scopes.append((j, self.pairs[j], name))
        self.functions = self.find_functions()
        self.constants = {}
        for i in range(1, len(self.tokens) - 2):
            if self.tokens[i] == "=" and IDENTIFIER.fullmatch(self.tokens[i - 1]):
                if any(f.start <= i < f.end for f in self.functions):
                    continue
                end = next((j for j in range(i + 1, len(self.tokens)) if self.tokens[j] == ";"), i)
                value = literal(self.tokens[i + 1:end])
                if value is not None:
                    self.constants[self.tokens[i - 1]] = value

    def name_before(self, i):
        end = i
        if i < 0 or not IDENTIFIER.fullmatch(self.tokens[i]):
            return ""
        while i >= 2 and self.tokens[i - 1] == "::" and IDENTIFIER.fullmatch(self.tokens[i - 2]):
            i -= 2
        return "".join(self.tokens[i:end + 1])

    def arguments(self, opening):
        end = self.pairs[opening]
        result, start, i = [], opening + 1, opening + 1
        while i < end:
            if self.tokens[i] == ",":
                result.append(self.tokens[start:i])
                start = i + 1
            if self.tokens[i] in ("(", "[", "{") and i in self.pairs:
                i = self.pairs[i]
            i += 1
        if start < end:
            result.append(self.tokens[start:end])
        return result

    def find_functions(self):
        result = []
        for i, token in enumerate(self.tokens):
            if token != "(" or i not in self.pairs:
                continue
            name = self.name_before(i - 1)
            lambda_name = ""
            if i and self.tokens[i - 1] == "]" and i - 1 in self.pairs:
                capture = self.pairs[i - 1]
                if capture >= 2 and self.tokens[capture - 1] == "=":
                    lambda_name = self.tokens[capture - 2]
                    name = lambda_name
            if not name or name in NON_FUNCTION:
                continue
            j = self.pairs[i] + 1
            while j < len(self.tokens) and self.tokens[j] in ("const", "override", "final", "noexcept"):
                j += 1
            if j not in self.pairs or self.tokens[j] != "{":
                continue
            # Calls in conditions end at another ')' rather than a function body.
            if i >= 2 and self.tokens[i - 2] in (".", "->"):
                continue
            prefix = "::".join(s[2] for s in self.scopes if s[0] < i < s[1])
            qualified = f"{prefix}::{name}" if prefix else name
            if lambda_name:
                parent = next((f for f in reversed(result) if f.start < i < f.end), None)
                if parent:
                    qualified = parent.name + "::" + name
            params = []
            for arg in self.arguments(i):
                arg = arg[:arg.index("=")] if "=" in arg else arg
                names = [t for t in arg if IDENTIFIER.fullmatch(t)]
                params.append(names[-1] if names else "")
            result.append(Function(self, qualified, params, j + 1, self.pairs[j]))
        return result

    def calls(self, function):
        nested = [(f.start - 1, f.end) for f in self.functions if function.start < f.start < f.end < function.end]
        for i in range(function.start, function.end):
            if self.tokens[i] != "(" or i not in self.pairs or any(a <= i <= b for a, b in nested):
                continue
            name = self.name_before(i - 1)
            if name and name not in NON_FUNCTION:
                yield i, name, self.arguments(i)

    def section_at(self, function, position, inherited):
        section = inherited
        for i in range(function.start, position):
            if self.tokens[i:i + 3] != [".", "Is", "("] or i + 2 not in self.pairs:
                continue
            args = self.arguments(i + 2)
            label = literal(args[0]) if args else None
            end = self.pairs[i + 2] + 1
            if end < len(self.tokens) and self.tokens[end] == ")":
                end += 1
            if end < len(self.tokens) and self.tokens[end] == "{" and end in self.pairs:
                limit = self.pairs[end]
            else:
                limit = next((j for j in range(end, function.end) if self.tokens[j] == ";"), end)
            if label and end <= position <= limit:
                section = label
        return section

    def ranges_at(self, function, position):
        for i in range(function.start, position):
            if self.tokens[i:i + 2] != ["for", "("] or i + 1 not in self.pairs:
                continue
            end = self.pairs[i + 1]
            if ":" not in self.tokens[i + 2:end]:
                continue
            colon = self.tokens.index(":", i + 2, end)
            begin = end + 1
            finish = self.pairs.get(begin) if self.tokens[begin] == "{" else next((j for j in range(begin, function.end) if self.tokens[j] == ";"), begin)
            if finish and begin <= position <= finish:
                yield self.tokens[colon - 1], "".join(self.tokens[colon + 1:end])

    def navigation_at(self, function, position, inherited, environment, calls):
        """Follow only explicitly UI-only selectors, never feature values."""
        t = self.tokens
        for call, name, args in calls:
            if name.rsplit("::", 1)[-1] != "NavigationChoice" or len(args) < 3:
                continue
            selector = literal(args[0])
            variable = "".join(args[1]).lstrip("&")
            array = "".join(args[2])
            choices = []
            for i in range(function.start, call):
                if t[i] == array and t[i + 1:i + 5] == ["[", "]", "=", "{"]:
                    choices = [literal(a) for a in self.arguments(i + 4)]
            for i in range(call, position):
                if t[i:i + 4] != ["switch", "(", variable, ")"] or t[i + 4] != "{":
                    continue
                finish = self.pairs.get(i + 4, i)
                if not i < position < finish:
                    continue
                selected = None
                j = i + 5
                while j < position:
                    if t[j] == "case" and t[j + 1].isdigit() and t[j + 2] == ":":
                        selected = int(t[j + 1])
                    if t[j] in ("{", "(", "[") and j in self.pairs and self.pairs[j] < position:
                        j = self.pairs[j]
                    j += 1
                limit = evaluate_value(args[3], environment) if len(args) > 3 else None
                if isinstance(limit, int) and selected is not None and selected >= limit:
                    return None
                if selector and selected is not None and selected < len(choices) and choices[selected]:
                    return selector, choices[selected]
        return inherited


def collect_tables(sources):
    schemas = {}
    for source in sources:
        t = source.tokens
        for start, end, name in source.scopes:
            fields, begin, i = [], start + 1, start + 1
            while i < end:
                if t[i] == ";":
                    declaration = t[begin:i]
                    if "=" in declaration:
                        declaration = declaration[:declaration.index("=")]
                    names = [x for x in declaration if IDENTIFIER.fullmatch(x)]
                    fields.append(names[-1] if names else "")
                    begin = i + 1
                if t[i] in ("(", "{", "[") and i in source.pairs:
                    i = source.pairs[i]
                i += 1
            if any(x in fields for x in ("label", "title", "name")):
                schemas.setdefault(name, []).append(fields)
    tables = {}
    for source in sources:
        t = source.tokens
        for opening in range(2, len(t)):
            if t[opening] != "{" or opening not in source.pairs:
                continue
            name_at = opening - 1 if t[opening - 1] != "=" else opening - 2
            if not IDENTIFIER.fullmatch(t[name_at]):
                continue
            name = t[name_at]
            # Only aggregate initializers contain typed record rows.
            rows = []
            i = opening + 1
            while i < source.pairs[opening]:
                if t[i] == "{" and i in source.pairs:
                    kind = t[i - 1]
                    candidates = schemas.get(kind, [])
                    args = source.arguments(i)
                    for schema in candidates:
                        if len(schema) == len(args):
                            row = {key: value for key, arg in zip(schema, args) if (value := literal(arg)) is not None}
                            if any(key in row for key in ("label", "title", "name")):
                                rows.append(row)
                                break
                    i = source.pairs[i]
                i += 1
            if rows:
                prefix = "::".join(s[2] for s in source.scopes if s[0] < opening < s[1])
                tables[(source.path, (prefix + "::" if prefix else "") + name)] = rows
    return tables


def collect(sources: list[Source]):
    functions = [f for s in sources for f in s.functions]
    by_name = {}
    for function in functions:
        by_name.setdefault(function.name, []).append(function)
    features = {}
    tables = collect_tables(sources)
    for f in functions:
        if f.name.endswith("::GetShortName"):
            body = f.source.tokens[f.start:f.end]
            if "return" in body:
                start = body.index("return") + 1
                expression = body[start:body.index(";", start)]
                value = literal(expression)
                if value is None:
                    constants = [f.source.constants[t] for t in expression if t in f.source.constants]
                    value = constants[0] if len(constants) == 1 else None
                if value:
                    features[f.owner] = value

    def resolve(caller, name):
        for candidate in (f"{caller.name}::{name}", f"{caller.owner}::{name}", name):
            if candidate in by_name:
                return by_name[candidate]
        local = [f for f in functions if f.source is caller.source and f.name.rsplit("::", 1)[-1] == name]
        return local

    entries = {}
    visited = set()

    def walk(function, feature, page, section, navigation, environment):
        key = (function.source.path, function.start, feature, page, section, navigation, tuple(sorted(environment.items())))
        if key in visited:
            return
        visited.add(key)
        source, tokens = function.source, function.source.tokens
        calls = list(source.calls(function))
        for position, name, args in calls:
            if not args:
                args = []
            active_section = source.section_at(function, position, section)
            active_navigation = source.navigation_at(function, position, navigation, environment, calls)
            if active_navigation is None:
                continue
            environments = [environment]
            for variable, collection in source.ranges_at(function, position):
                candidates = [rows for (path, name), rows in tables.items()
                              if name == collection or (path == source.path and name.rsplit("::", 1)[-1] == collection)
                              or name == function.owner + "::" + collection]
                if len(candidates) == 1:
                    environments = [dict(env, **{variable + "." + key: value for key, value in row.items()})
                                    for env in environments for row in candidates[0]]
            for env in environments:
                visit_call(function, feature, page, active_section, active_navigation, env, name, args)

    def visit_call(function, feature, page, active_section, navigation, environment, name, args):
        source = function.source
        leaf = name.rsplit("::", 1)[-1]
        values = [evaluate_value(a, source.constants | environment) for a in args]
        if CONTROL.fullmatch(leaf) and values:
            raw = values[0]
            if isinstance(raw, str) and raw:
                label = raw.split("##", 1)[0].strip()
                if label and len(label) <= 160 and "\n" not in label and "%" not in label:
                    group = environment.get("field.group", "")
                    section = environment.get("field.section", active_section)
                    identity = (feature, page, section, *navigation, group, raw)
                    entries[identity] = dict(feature=feature, page=page, section=section, label=label, raw=raw,
                                             selector=navigation[0], choice=navigation[1], group=group)
            return
        if name.startswith(("ImGui::", "std::", "logger::")):
            return
        for target in resolve(function, name):
            # Rendering metadata never follows helpers into engine/gameplay implementations.
            if target.source.path.startswith(("src/Features/", "src/Menu/")) or target.owner in ("Menu",):
                env = environment.copy() if target.name.startswith(function.name + "::") else {}
                env.update({p: v for p, v in zip(target.params, values) if v is not None})
                walk(target, feature, page, active_section, navigation, env)

    for f in functions:
        if f.name.endswith("::DrawSettings") and f.owner in features:
            walk(f, features[f.owner], features[f.owner], "", ("", ""), {})
    for name, feature, section in (
        ("Menu::DrawGeneralSettings", "General", ""),
        ("Menu::DrawAdvancedSettings", "Advanced", ""),
        ("HomePageRenderer::RenderHomePage", "Home", ""),
        ("PerformanceTuningRenderer::Render", "PerformanceTuning", ""),
        ("ProfilingRenderer::RenderStatistics", "Profiling", "timings"),
    ):
        for f in by_name.get(name, []):
            walk(f, feature, feature, section, ("", ""), {})
    result = []
    for identity, entry in sorted(entries.items()):
        entry["id"] = hashlib.sha256("\0".join(identity).encode()).hexdigest()[:24]
        result.append(entry)
    return result


def generate(root: Path):
    sources = [Source(p.relative_to(root).as_posix(), p.read_text(encoding="utf-8-sig"))
               for p in sorted((root / "src").rglob("*")) if p.suffix in (".cpp", ".h", ".hpp")]
    return collect(sources)


def write_changed(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8", newline="\n")


def cpp_string(text):
    """Use bounded octal escapes so UTF-8 cannot form invalid C++ UCNs."""
    return '"' + "".join(chr(byte) if 32 <= byte < 127 and chr(byte) not in '\\"'
                         else f"\\{byte:03o}" for byte in text.encode("utf-8")) + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    entries = generate(args.source_dir)
    if not entries:
        raise SystemExit("No UI controls found; refusing an empty search catalog")
    fields = ("id", "feature", "page", "section", "label", "raw", "selector", "choice", "group")
    rows = ["    { " + ", ".join(cpp_string(e[k]) for k in fields) + " }," for e in entries]
    write_changed(args.output, '#include "Menu/ControlCatalog.h"\n#include <array>\n\n'
                  'namespace MenuUI::ControlCatalog\n{\n'
                  f'static constexpr std::array<Entry, {len(rows)}> entries = {{{{\n' + "\n".join(rows) +
                  '\n}};\nstd::span<const Entry> Entries() { return entries; }\n}\n')
    if args.json:
        write_changed(args.json, json.dumps(entries, indent=2) + "\n")
    print(f"Indexed {len(entries)} controls across {len({e['feature'] for e in entries})} pages")


if __name__ == "__main__":
    main()
