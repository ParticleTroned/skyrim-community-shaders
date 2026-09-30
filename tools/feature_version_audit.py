import os
import subprocess
import re
import json
import configparser
from pathlib import Path
import datetime
import sys
import argparse
import importlib

# =====================
# Path Resolution for Project Root
# =====================
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent

# =====================
# Configuration Constants
# =====================
DEFAULT_PR_BASE_REF = "origin/main-VR"
DEFAULT_FEATURES_DIR = PROJECT_ROOT / "features"
DEFAULT_FEATURE_HEADERS_DIR = PROJECT_ROOT / "src/Features"
DEFAULT_NEXUS_BASE_URL = "https://www.nexusmods.com/skyrimspecialedition/mods/"
DEFAULT_SHADER_TYPES = (".ini", ".hlsl", ".hlsli")

# Regex patterns for feature metadata extraction (all DRY, only here)
RE_MOD_ID = re.compile(r'MOD_ID\s*=\s*"([^"]+)"')
RE_FEATURE_MOD_LINK_DIRECT = re.compile(r'GetFeatureModLink\s*\([^)]*\)\s*\{\s*return\s*"(https?://[^"]+)";\s*\}')
RE_FEATURE_MOD_LINK_NEXUS = re.compile(r'GetFeatureModLink\s*\([^)]*\)\s*\{\s*return\s*MakeNexusModURL\(MOD_ID\);')
RE_FEATURE_SUMMARY_DIRECT = re.compile(r'GetFeatureSummary\s*\([^)]*\)\s*(?:override)?\s*\{\s*return \{\s*"([^"]+)"\s*,\s*\{([^}]*)\}', re.DOTALL)
RE_FEATURE_SUMMARY_MULTILINE = re.compile(r'GetFeatureSummary\s*\([^)]*\)\s*(?:override)?\s*\{\s*return \{\s*((?:"[^"]*"\s*)+),\s*\{([^}]*)\}', re.DOTALL)
RE_FEATURE_SUMMARY_CPP = re.compile(r'GetFeatureSummary\s*\([^)]*\)\s*\{[^}]*?std::string description\s*=\s*"([^"]+)";\s*std::vector<std::string> keyFeatures\s*=\s*\{([^}]*)\}', re.DOTALL)
RE_FEATURE_SUMMARY_CPP_MULTILINE = re.compile(r'GetFeatureSummary\s*\([^)]*\)\s*\{[^}]*?std::string description\s*=\s*((?:"[^"]*"\s*)+);\s*std::vector<std::string> keyFeatures\s*=\s*\{([^}]*)\}', re.DOTALL)
RE_FEATURE_SUMMARY_PAIR = re.compile(r'GetFeatureSummary\s*\([^)]*\).*?return\s+std::make_pair\(\s*((?:"(?:\\.|[^"\\])*"\s*)+|\w+)\s*,\s*std::vector<std::string>\s*\{([^}]*)\}', re.DOTALL)
RE_FEATURE_DESCRIPTION_DIRECT = re.compile(r'GetFeatureDescription\s*\([^)]*\)\s*\{\s*return\s*"([^"]+)";\s*\}')
RE_IS_CORE = re.compile(r"IsCore\s*\(.*\)\s*const\s*override\s*\{\s*return true;\s*\}")
RE_VERSION = re.compile(r"(?i)version\s*=\s*([0-9]+)-([0-9]+)-([0-9]+)")
RE_BUMP_SUGGESTION = re.compile(r"- \*\*(.+?)\.ini\*\*: bump to `([\d-]+)`.*\[link\]\([^)]+\) ?(?:\(([^)]+)\))?")

# Commit type regexes
RE_COMMIT_FEAT = re.compile(r"^feat(\(|:|\s)", re.IGNORECASE)
RE_COMMIT_FIX = re.compile(r"^fix(\(|:|\s)", re.IGNORECASE)
RE_COMMIT_REFACTOR = re.compile(r"^refactor(\(|:|\s)", re.IGNORECASE)
RE_COMMIT_PERF = re.compile(r"^perf(\(|:|\s)", re.IGNORECASE)
RE_COMMIT_BREAKING = re.compile(r"!\s*:|BREAKING CHANGE:", re.IGNORECASE)
RE_COMMIT_NONFUNCTIONAL = re.compile(r"^(chore|docs|style|ci|test|build|refactor|perf)(\(|:|\s)", re.IGNORECASE)

# =====================
# End Configuration
# =====================

# Global state (set in main)
RELEASE_TAG = None
FEATURES_DIR = DEFAULT_FEATURES_DIR
SHADER_TYPES = DEFAULT_SHADER_TYPES
HEAD_REF = "HEAD"

def extract_regex(pattern, content, group=1):
    m = pattern.search(content)
    return m.group(group) if m else None

def extract_multiline_strings(multiline):
    return [d.replace(r'\n', ' ').replace(r'\t', ' ').replace(r'\"', '"').replace("\n", " ").strip()
            for d in re.findall(r'"((?:\\.|[^"\\])*)"', multiline) if d.strip()]

def normalize_feature_key(name):
    return ''.join(str(name or '').lower().replace('-', ' ').split())

def load_nexus_metadata_file(path):
    try:
        raw = json.loads(Path(path).read_text(encoding='utf-8'))
    except Exception as e:
        raise RuntimeError(f"Failed to load Nexus metadata file {path}: {e}")

    if isinstance(raw, dict):
        if 'mods' in raw and isinstance(raw['mods'], list):
            raw = raw['mods']
        elif 'data' in raw and isinstance(raw['data'], list):
            raw = raw['data']
        elif 'features' in raw and isinstance(raw['features'], list):
            raw = raw['features']
        else:
            candidates = []
            for key, value in raw.items():
                if isinstance(value, dict):
                    value = dict(value)
                    if 'name' not in value:
                        value['name'] = key
                    candidates.append(value)
            if candidates:
                raw = candidates

    if not isinstance(raw, list):
        raise RuntimeError(f"Expected JSON list or object containing a feature list in {path}")

    nexus_metadata = {}
    for item in raw:
        if not isinstance(item, dict):
            continue
        name = item.get('name') or item.get('mod_name') or item.get('modFilename') or item.get('mod_filename') or item.get('file_name')
        if not name:
            continue
        key = normalize_feature_key(name)
        mod_id = item.get('nexus_mod_id') or item.get('mod_id') or item.get('modId') or item.get('id')
        if isinstance(mod_id, int):
            mod_id = str(mod_id)
        mod_filename = item.get('mod_filename') or item.get('modFilename') or item.get('file_name') or item.get('fileName')
        mod_link = item.get('mod_link') or item.get('modLink') or item.get('url') or item.get('link')
        description = item.get('description') or item.get('summary') or item.get('details') or ""
        key_features = item.get('key_features') or item.get('keyFeatures') or item.get('features') or []
        if isinstance(key_features, str):
            key_features = [k.strip() for k in key_features.split(',') if k.strip()]
        elif not isinstance(key_features, list):
            key_features = []
        artifact_pattern = item.get('artifact_pattern') or item.get('artifactPattern')
        short_name = item.get('short_name') or item.get('shortName')

        nexus_metadata[key] = {
            'name': name,
            'mod_id': mod_id,
            'mod_filename': mod_filename,
            'mod_link': mod_link,
            'description': description,
            'key_features': key_features,
            'artifact_pattern': artifact_pattern,
            'short_name': short_name,
        }
    return nexus_metadata

def find_feature_dir(feature_name):
    """Find the feature directory matching the given name, with fuzzy matching support."""
    base_dir = FEATURES_DIR / feature_name
    if base_dir.exists():
        return base_dir

    # Fuzzy search: try removing spaces/hyphens and matching case-insensitively
    normalized_name = normalize_feature_key(feature_name)
    for entry in FEATURES_DIR.iterdir():
        if entry.is_dir() and normalize_feature_key(entry.name) == normalized_name:
            return entry

    return None

def get_feature_ini(feature_path):
    # Support both direct paths and feature names (strings)
    if isinstance(feature_path, str):
        feature_path = find_feature_dir(feature_path)
        if not feature_path:
            return None

    ini_dir = feature_path / "Shaders" / "Features"
    if ini_dir.exists():
        for f in ini_dir.glob("*.ini"):
            return f
    return None

def get_feature_mod_id_from_ini(feature_dir):
    ini_path = get_feature_ini(feature_dir)
    if not ini_path:
        return None
    try:
        content = ini_path.read_text(encoding='utf-8')
    except Exception:
        return None
    m = RE_MOD_ID.search(content)
    return m.group(1) if m else None

def get_feature_ini_metadata(feature_dir_or_ini_path):
    # Accept both directory and direct INI path
    if isinstance(feature_dir_or_ini_path, (str, Path)):
        path = Path(feature_dir_or_ini_path)
        if path.suffix == '.ini':
            ini_path = path
        else:
            ini_path = get_feature_ini(feature_dir_or_ini_path)
    else:
        ini_path = get_feature_ini(feature_dir_or_ini_path)

    if not ini_path:
        return {}
    parser = configparser.ConfigParser()
    try:
        parser.read(ini_path, encoding='utf-8')
    except Exception:
        return {}

    sections = [s for s in parser.sections() if s.lower() in {'info', 'nexus'}]
    if not sections:
        sections = ['Info'] if parser.has_section('Info') else []

    metadata = {'auto_upload': False, 'audit_version': True}
    for section in sections:
        if not parser.has_section(section):
            continue
        section_items = dict(parser.items(section))
        # ConfigParser converts keys to lowercase, so look for both variants
        auto_upload_str = section_items.get('autoupload') or section_items.get('auto_upload')
        if auto_upload_str is not None:
            metadata['auto_upload'] = str(auto_upload_str).strip().lower() not in ('false', '0', 'no', 'off', '')

        # Activation-only features keep all code in the core mod; their toggle
        # .ini opts out of version auditing/bumping with `AuditVersion = false`.
        # Auditing is on by default, so a blank value must NOT exclude the ini:
        # only explicit falsy values opt out (unlike auto_upload, which is opt-in
        # and treats blank as off).
        audit_version_str = section_items.get('auditversion') or section_items.get('audit_version')
        if audit_version_str is not None:
            metadata['audit_version'] = str(audit_version_str).strip().lower() not in ('false', '0', 'no', 'off')

        section_metadata = {
            'mod_id': section_items.get('nexusmodid') or section_items.get('nexus_mod_id') or section_items.get('mod_id'),
            'mod_filename': section_items.get('nexusfilename') or section_items.get('nexus_filename') or section_items.get('nexusmodfilename') or section_items.get('nexus_mod_filename') or section_items.get('mod_filename') or section_items.get('modname') or section_items.get('name'),
            'mod_link': section_items.get('nexusmodlink') or section_items.get('nexus_mod_link') or section_items.get('mod_link') or section_items.get('link'),
            'description': section_items.get('nexusdescription') or section_items.get('nexus_description') or section_items.get('description'),
            'artifact_pattern': section_items.get('nexusartifactpattern') or section_items.get('nexus_artifact_pattern') or section_items.get('artifact_pattern'),
            'short_name': section_items.get('shortname') or section_items.get('short_name') or section_items.get('nexusshortname') or section_items.get('nexus_short_name'),
        }
        metadata.update({k: v for k, v in section_metadata.items() if v is not None and v != ""})
        key_features = section_items.get('nexuskeyfeatures') or section_items.get('nexus_key_features') or section_items.get('key_features') or section_items.get('keyfeatures')
        if key_features:
            parsed_kf = [k.strip() for k in re.split(r'[;,]', key_features) if k.strip()]
            existing_kf = metadata.get('key_features', [])
            for kf in parsed_kf:
                if kf not in existing_kf:
                    existing_kf.append(kf)
            metadata['key_features'] = existing_kf

    return {k: v for k, v in metadata.items() if v is not None and v != ""}

def read_current_ini_content(ini_path):
    """Read the current (HEAD) feature .ini text. In ref mode read it from HEAD_REF;
    if that read fails, fall back to the checked-out workspace file rather than the
    caller's miss-default. A failed ref read must not be mistaken for "no version /
    release stage"; that misread proposes a spurious version bump on a pre-release or
    space-named feature whose ref blob the runner failed to fetch. Returns None only
    when neither source is readable."""
    if HEAD_REF != "HEAD":
        rel_path = os.path.relpath(ini_path, PROJECT_ROOT).replace("\\", "/")
        try:
            return subprocess.check_output(
                ["git", "show", f"{HEAD_REF}:{rel_path}"], stderr=subprocess.DEVNULL
            ).decode("utf-8")
        except Exception:
            pass  # fall back to the checked-out workspace file
    try:
        with open(ini_path, "r", encoding="utf-8") as f:
            return f.read()
    except Exception:
        return None

def get_version_from_ini(ini_path, content=None):
    if content is None:
        content = read_current_ini_content(ini_path)
        if content is None:
            return None
    m = RE_VERSION.search(content)
    if m:
        return tuple(map(int, m.groups()))
    return None

def get_prior_version(ini_path, base_ref):
    # Always use path relative to project root for git show
    rel_path = os.path.relpath(ini_path, PROJECT_ROOT).replace("\\", "/")
    try:
        content = subprocess.check_output(
            ["git", "show", f"{base_ref}:{rel_path}"], stderr=subprocess.DEVNULL
        ).decode("utf-8")
        return get_version_from_ini(ini_path, content)
    except Exception:
        return None

def get_changed_files(feature_path, base_ref, file_types=None):
    if file_types is None:
        file_types = SHADER_TYPES
    rel_path = str(feature_path).replace("\\", "/")
    try:
        output = subprocess.check_output(
            ["git", "diff", "--name-status", f"{base_ref}...{HEAD_REF}", "--", rel_path],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8")
        changes = []
        for line in output.splitlines():
            status, file = line.split(maxsplit=1)
            ext = os.path.splitext(file)[1].lower()
            if ext in file_types:
                changes.append((status, file))
        return changes
    except Exception:
        return []

def is_version_relevant(file_path, ini_path):
    """Return whether a packaged shader file participates in feature versioning.

    Shader source and include changes affect the shader cache. Of the packaged
    INIs, only the feature's canonical Shaders/Features INI keys cache
    invalidation; auxiliary configuration INIs must not trigger a version bump.
    """
    path = Path(file_path)
    if path.suffix.lower() != ".ini":
        return True
    if not ini_path:
        return False

    if not path.is_absolute():
        path = PROJECT_ROOT / path
    canonical_ini = Path(ini_path)
    if not canonical_ini.is_absolute():
        canonical_ini = PROJECT_ROOT / canonical_ini
    return path.resolve() == canonical_ini.resolve()

def get_commits_for_file(file_path, base_ref):
    try:
        output = subprocess.check_output(
            ["git", "log", f"{base_ref}..{HEAD_REF}", "--pretty=%B%x1e", "--", file_path],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8")
        return [msg.strip() for msg in output.split("\x1e") if msg.strip()]
    except Exception:
        return []

def get_bump_commit(file_path, base_ref):
    try:
        output = subprocess.check_output(
            ["git", "log", f"{base_ref}..{HEAD_REF}", "--pretty=%H%x1f%B%x1e", "--", file_path],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8")
        for entry in output.split("\x1e"):
            if not entry.strip():
                continue
            parts = entry.strip().split("\x1f", 1)
            if len(parts) < 2:
                continue
            commit_hash, msg = parts
            if RE_COMMIT_FEAT.match(msg) or RE_COMMIT_FIX.match(msg) or RE_COMMIT_BREAKING.search(msg):
                return commit_hash.strip()
    except Exception:
        pass
    return None

def get_latest_commit(file_paths, base_ref):
    try:
        output = subprocess.check_output(
            ["git", "log", f"{base_ref}..{HEAD_REF}", "-1", "--pretty=%H", "--", *sorted(file_paths)],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8").strip()
        return output or None
    except Exception:
        pass
    return None

def get_commit_author(commit_hash):
    try:
        output = subprocess.check_output(
            ["git", "show", "-s", "--format=%an", commit_hash],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8").strip()
        return output
    except Exception:
        return None

def get_feature_changelog(feature_dir, feature_info, base_ref):
    """Return bullet-point changelog of user-facing commits since base_ref.

    Includes feat, fix, perf, and breaking-change commits only.
    Deduplicates by subject line and preserves commit order (newest first).
    """
    if not base_ref:
        return ""
    paths = [str(feature_dir).replace("\\", "/")]
    try:
        # Use ASCII unit-separator (0x1f) between subject and body so BREAKING
        # CHANGE footers are detected even when the subject lacks the '!' marker.
        # Null bytes are not used because Windows subprocess rejects them.
        raw = subprocess.check_output(
            ["git", "log", f"{base_ref}..{HEAD_REF}", "--pretty=format:%s\x1f%b\x1f", "--"] + paths,
            cwd=str(PROJECT_ROOT),
            stderr=subprocess.DEVNULL,
        ).decode("utf-8", errors="replace")
    except Exception:
        return ""
    parts = raw.split("\x1f")
    seen = set()
    lines = []
    for subject, body in zip(parts[::2], parts[1::2]):
        subject = subject.strip()
        if not subject or subject in seen:
            continue
        seen.add(subject)
        full = subject + "\n" + body
        if (RE_COMMIT_FEAT.match(subject) or RE_COMMIT_FIX.match(subject) or
                RE_COMMIT_PERF.match(subject) or RE_COMMIT_BREAKING.search(full)):
            lines.append(f"- {subject}")
    return "\n".join(lines)

def apply_version_bump(ini_path, proposed_ver_str):
    try:
        with open(ini_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Replace version = X-X-X preserving original key casing
        new_content = re.sub(
            r"(?i)(version\s*=\s*)[0-9]+-[0-9]+-[0-9]+",
            lambda m: f"{m.group(1)}{proposed_ver_str}",
            content,
            count=1,
        )

        if new_content != content:
            with open(ini_path, "w", encoding="utf-8") as f:
                f.write(new_content)
            return True
    except Exception as e:
        print(f"Error applying bump to {ini_path}: {e}", file=sys.stderr)
    return False

def parse_feature_metadata_file(path, mod_id=None, is_core=False):
    mod_link = ""
    description = ""
    key_features = []
    with open(path, encoding="utf-8") as f:
        content = f.read()
        # modId
        if not mod_id:
            mod_id = extract_regex(RE_MOD_ID, content)
        # GetFeatureModLink
        mod_link = extract_regex(RE_FEATURE_MOD_LINK_DIRECT, content) or mod_link
        if RE_FEATURE_MOD_LINK_NEXUS.search(content) and mod_id:
            mod_link = DEFAULT_NEXUS_BASE_URL + mod_id
        if not mod_link and not is_core and mod_id:
            mod_link = DEFAULT_NEXUS_BASE_URL + mod_id
        # GetFeatureSummary
        m = RE_FEATURE_SUMMARY_DIRECT.search(content)
        if m:
            description = m.group(1).replace("\n", " ").strip()
            key_features = extract_multiline_strings(m.group(2))
        m = RE_FEATURE_SUMMARY_MULTILINE.search(content)
        if m:
            description = " ".join(extract_multiline_strings(m.group(1)))
            key_features = extract_multiline_strings(m.group(2))
        m = RE_FEATURE_SUMMARY_CPP.search(content)
        if m:
            description = m.group(1).replace("\n", " ").strip()
            key_features = extract_multiline_strings(m.group(2))
        m = RE_FEATURE_SUMMARY_CPP_MULTILINE.search(content)
        if m:
            description = " ".join(extract_multiline_strings(m.group(1)))
            key_features = extract_multiline_strings(m.group(2))
        m = RE_FEATURE_SUMMARY_PAIR.search(content)
        if m:
            expression = m.group(1).strip()
            if re.fullmatch(r'\w+', expression):
                # Only the unconditional initializer is common to every runtime.
                initializer = re.search(r'\bstd::string\s+' + re.escape(expression) + r'\s*=\s*((?:"(?:\\.|[^"\\])*"\s*)+);', m.group(0))
                expression = initializer.group(1) if initializer else ''
            description = description or " ".join(extract_multiline_strings(expression))
            key_features = key_features or extract_multiline_strings(m.group(2))
        desc_direct = extract_regex(RE_FEATURE_DESCRIPTION_DIRECT, content)
        if desc_direct and not description:
            description = desc_direct.strip()
    return {
        "mod_id": mod_id,
        "mod_link": mod_link,
        "description": description,
        "key_features": key_features
    }

def extract_feature_metadata(feature_headers_dir, nexus_metadata=None):
    """Describe CSX's shipped AIO modules, independently of split-mod metadata."""
    builder = importlib.import_module("build-shader-cache")
    distribution = builder.derive_distribution_profile(PROJECT_ROOT)
    packages = builder.packaged_feature_directories(PROJECT_ROOT)
    headers = {}
    for header in sorted(feature_headers_dir.glob("*.h")) + [PROJECT_ROOT / "src/TruePBR.h"]:
        if header.is_file():
            content = header.read_text(encoding="utf-8")
            short_name = builder.feature_short_name_from_header(content)
            if short_name:
                headers[short_name] = header
    result = []
    for short_name, package in sorted(packages.items()):
        if package in distribution.excluded_packages:
            continue
        header = headers.get(short_name)
        # IBL's historical INI filename predates its runtime short name.
        if header is None and short_name == "ImageBasedLighting":
            header = headers.get("IBL")
        metadata = {"description": "", "key_features": []}
        display_name = package
        if header:
            content = header.read_text(encoding="utf-8")
            display_name = (extract_regex(re.compile(r'GetName\s*\([^)]*\)[^{]*\{\s*return\s+"([^"\n]+)"'), content)
                            or extract_regex(re.compile(r'kFeatureName\s*=\s*"([^"\n]+)"'), content)
                            or package)
            for source in (header, header.with_suffix(".cpp")):
                if source.is_file():
                    parsed = parse_feature_metadata_file(source, is_core=True)
                    for key in ("description", "key_features"):
                        metadata[key] = metadata[key] or parsed[key]
        if short_name == "Screenshot" and not metadata["description"]:
            metadata["description"] = "Desktop and headset image capture, with selectable eye views and image sequences."
        ini_metadata = get_feature_ini_metadata(FEATURES_DIR / package)
        for key in ("description", "key_features"):
            metadata[key] = metadata[key] or ini_metadata.get(key, metadata[key])
        result.append({
            "name": package, "display_name": display_name, "short_name": short_name,
            "is_core": True, "mod_id": None, "mod_link": "",
            "description": metadata["description"], "key_features": metadata["key_features"],
            "artifact_pattern": "CSX_AIO-*.7z", "mod_filename": "Community Shaders Expanded (CSX)",
        })
    return result

def get_latest_release_tag(ref="HEAD"):
    try:
        output = subprocess.check_output(
            ["git", "tag", "--merged", ref, "--list", "csx*", "CSX*"],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8")
        tags = [t.strip() for t in output.splitlines() if re.fullmatch(r"(?i:csx)\d+\.\d+(?:\.\d+)?", t.strip())]
        if not tags:
            return None
        # Sort tags by version
        def tag_key(tag):
            return tuple(map(int, tag[3:].split('.'))) + ((0,) if tag.count('.') == 1 else ())
        tags.sort(key=tag_key, reverse=True)
        return tags[0]
    except Exception:
        return None

def detect_pr_base():
    # 1. Use GITHUB_BASE_REF if set (GitHub Actions)
    env_base_ref = os.environ.get("GITHUB_BASE_REF")
    if env_base_ref:
        print(f"Detected PR base from GITHUB_BASE_REF: origin/{env_base_ref}", file=sys.stderr)
        return f"origin/{env_base_ref}"
    # 2. Fallback
    print(f"Falling back to {DEFAULT_PR_BASE_REF} for PR base.", file=sys.stderr)
    return DEFAULT_PR_BASE_REF

def propose_new_version(prior_version, commits):
    if not prior_version:
        return None
    major, minor, patch = prior_version
    if not commits:
        return None

    is_minor = any(RE_COMMIT_FEAT.match(c) or RE_COMMIT_BREAKING.search(c) for c in commits)
    is_patch = any(RE_COMMIT_FIX.match(c) for c in commits)
    is_nonfunctional_only = all(RE_COMMIT_NONFUNCTIONAL.match(c) for c in commits)

    if is_minor:
        return (major, minor + 1, 0)
    elif is_patch:
        return (major, minor, patch + 1)
    elif is_nonfunctional_only:
        return None
    else:
        return (major, minor, patch + 1)

def analyze_features(FEATURES_DIR, feature_meta_map, base_ref, only_changed=False, release_ref=None):
    bump_suggestions = []
    new_features = []
    new_code_features = []  # Features with new code added (not entirely new)
    actionable = False
    feature_actions = {}
    feature_analysis = []
    # version_ref: base for version comparison (last release tag in PR mode; otherwise same as base_ref)
    version_ref = release_ref if release_ref else base_ref
    # If only_changed, build a set of changed feature names
    changed_features = set()
    if only_changed:
        # Only packaged feature content should affect feature-version auditing.
        target_dirs = [str(FEATURES_DIR)]
        cmd = ["git", "diff", "--name-status", f"{base_ref}...{HEAD_REF}", "--"] + target_dirs
        try:
            all_changes = subprocess.check_output(cmd, stderr=subprocess.DEVNULL).decode("utf-8").splitlines()
        except Exception as e:
            print(f"Error running git diff: {e}", file=sys.stderr)
            all_changes = []

        for line in all_changes:
            parts = line.split(maxsplit=1)
            if len(parts) != 2:
                continue
            status, file = parts
            file_parts = Path(file).parts

            # Case 1: features/[Feature Name]/...
            if FEATURES_DIR.name in file_parts:
                try:
                    idx = file_parts.index(FEATURES_DIR.name)
                    feature_name = file_parts[idx+1]
                    changed_features.add(feature_name)
                    continue
                except (ValueError, IndexError):
                    pass

    # Always use GetShortName() for feature key normalization if available
    def get_feature_key(feature_dir, feature_meta_map):
        # Try to use GetShortName from metadata if present
        meta = feature_meta_map.get(''.join(feature_dir.name.lower().split()))
        if meta and 'short_name' in meta and meta['short_name']:
            return ''.join(meta['short_name'].lower().split())
        # Fallback to directory name
        return ''.join(feature_dir.name.lower().split())

    distribution = importlib.import_module("build-shader-cache").derive_distribution_profile(PROJECT_ROOT)
    for feature_dir in FEATURES_DIR.iterdir():
        if not feature_dir.is_dir() or feature_dir.name in distribution.excluded_packages:
            continue
        feature_key = get_feature_key(feature_dir, feature_meta_map)

        # membership check
        normalized_name = ''.join(feature_dir.name.lower().split())
        if only_changed and feature_dir.name not in changed_features and feature_key not in changed_features and normalized_name not in changed_features:
            continue

        meta = feature_meta_map.get(feature_key)
        ini_path = get_feature_ini(feature_dir)
        # Activation-only features (e.g. Terrain Helper) opt out of version
        # auditing via `AuditVersion = false` in their .ini: all their code lives
        # in the core mod, so bumping the toggle .ini on every edit is meaningless
        # churn. Skip them entirely from bump suggestions and PR-check failures.
        if ini_path and not get_feature_ini_metadata(ini_path).get('audit_version', True):
            continue
        # Use last release tag (version_ref) as the baseline for version proposals so that
        # multiple PRs between releases don't accumulate spurious bumps.
        prior_ver = get_prior_version(ini_path, version_ref) if ini_path else None
        # PR-scoped prior version: used for new-feature detection and as the effective
        # current version when the PR branch is behind base_ref (non-rebased PRs).
        pr_prior_ver = get_prior_version(ini_path, base_ref) if (ini_path and version_ref != base_ref) else prior_ver
        new_ver = get_version_from_ini(ini_path) if ini_path else None

        # PR-scoped changes: used for change-type display and new-feature detection.
        # Only packaged files under features/ participate in feature versioning.
        changes = get_changed_files(feature_dir, base_ref)
        changes = [change for change in set(changes) if is_version_relevant(change[1], ini_path)]

        # Release-scoped changes: all changes since last release, used to propose the correct
        # version so that a bump already applied by a prior PR satisfies this check.
        release_changes = get_changed_files(feature_dir, version_ref)
        release_changes = [change for change in set(release_changes) if is_version_relevant(change[1], ini_path)]

        change_types = set(os.path.splitext(f)[1].lower() for _, f in changes)
        all_commits = []
        bump_commit = None
        bump_author = None
        for status, f in release_changes:
            all_commits.extend(get_commits_for_file(f, version_ref))
        for status, f in changes:
            if not bump_commit:
                bump_commit = get_bump_commit(f, base_ref)
                if bump_commit:
                    bump_author = get_commit_author(bump_commit)
        if not bump_commit and changes:
            any_commit = get_latest_commit([f for _, f in changes], base_ref)
            if any_commit:
                bump_commit = any_commit
                bump_author = get_commit_author(any_commit)

        proposed_ver = propose_new_version(prior_ver, all_commits) if ini_path else None
        # Use max(new_ver, pr_prior_ver) as the effective current version so that a version
        # bump already on base_ref (e.g. from a parallel PR) satisfies the check even when
        # the PR branch has not been rebased.
        effective_new_ver = max(new_ver, pr_prior_ver) if (new_ver and pr_prior_ver) else (new_ver or pr_prior_ver)
        needs_bump = (proposed_ver is not None and effective_new_ver is not None and proposed_ver > effective_new_ver)
        proposed_ver_str = "-".join(map(str, proposed_ver)) if proposed_ver else "-"
        prior_ver_str = "-".join(map(str, prior_ver)) if prior_ver else "-"
        new_ver_str = "-".join(map(str, new_ver)) if new_ver else "-"
        note = ""
        is_attention = False

        # Detect new feature (all files added, ini present). A complete, valid
        # ini needs no human action -- report it for visibility but don't
        # block the release on it (only a genuinely missing ini, below, does).
        if changes and all(s == "A" for s, _ in changes):
            if ini_path:
                note = f"New feature (with ini v{new_ver_str})"
                new_features.append((feature_dir.name, new_ver_str, bump_commit))
            else:
                note = "New feature (missing ini!)"
                new_features.append((feature_dir.name, "-", bump_commit))
                is_attention = True
        # Detect new ini added — use pr_prior_ver (base_ref baseline) so features added
        # earlier in the release cycle are not re-reported as new in subsequent PRs.
        # Same reasoning as above: a valid new ini is not an action item. elif: a
        # brand-new feature (caught above) always has pr_prior_ver is None too --
        # without elif this double-appends to new_features and clobbers note.
        elif ini_path and pr_prior_ver is None and new_ver is not None:
            note = f"New ini added (v{new_ver_str})"
            new_features.append((feature_dir.name, new_ver_str, bump_commit))
        # Detect files added but ini missing
        if not ini_path and any(s == "A" for s, _ in changes):
            note = "Files added, ini missing!"
            new_features.append((feature_dir.name, "-", bump_commit))
            is_attention = True

        # Detect new code added to existing feature (not entirely new since last release)
        # Feature existed before version_ref (has non-"A" release_changes) AND has new files
        if (not (release_changes and all(s == "A" for s, _ in release_changes)) and  # Not brand new since last release
            release_changes and any(s == "A" for s, _ in release_changes)):  # Has new files added
            # Find author of the commit that added the file (first commit touching it since version_ref)
            for status, f in release_changes:
                if status == "A":
                    # Get the commit that added this file (not the latest commit)
                    try:
                        add_commits = subprocess.check_output(
                            ["git", "log", "--follow", "--diff-filter=A", "--format=%H", f"{version_ref}..{HEAD_REF}", "--", f],
                            stderr=subprocess.DEVNULL
                        ).decode("utf-8").splitlines()
                        add_commit = add_commits[-1].strip() if add_commits else None
                        if add_commit:
                            new_code_author = get_commit_author(add_commit)
                            if new_code_author:
                                new_code_features.append((feature_dir.name, new_code_author))
                                break
                    except Exception:
                        pass

        if needs_bump:
            is_attention = True
        if is_attention:
            actionable = True

        commit_link = ""
        if bump_commit:
            author_str = f" ({bump_author})" if bump_author else ""
            commit_link = f"[link](https://github.com/ParticleTroned/skyrim-community-shaders/commit/{bump_commit}){author_str}"

        def bold(val):
            return f"**{val}**" if is_attention and val != '' and val != '-' else val

        feature_analysis.append({
            'name': feature_dir.name,
            'prior_ver_str': prior_ver_str,
            'proposed_ver_str': proposed_ver_str,
            'needs_bump': needs_bump,
            'change_types': ', '.join(sorted(change_types)),
            'note': note,
            'commit_link': commit_link,
            'is_attention': is_attention,
            'ini_path': str(ini_path) if ini_path else None,
            'author': bump_author
        })
        if needs_bump:
            bump_suggestions.append(f"- **{os.path.basename(ini_path)}**: bump to `{proposed_ver_str}` {commit_link}")
        if is_attention:
            feat_act = feature_actions.setdefault(feature_dir.name, {"actions": [], "author": bump_author})
            if note:
                feat_act["actions"].append(note)
            if needs_bump:
                feat_act["actions"].append(f"Needs version bump to {proposed_ver_str}")

    return feature_analysis, bump_suggestions, new_features, new_code_features, actionable, feature_actions

def print_actionable_suggestions(feature_actions):
    if feature_actions:
        print("## Actionable Suggestions\n")
        for fname, info in sorted(feature_actions.items()):
            author = f" ({info['author']})" if info.get('author') else ""
            print(f"- **{fname}**{author}: " + "; ".join(info["actions"]))
    else:
        print("No actionable suggestions for changed features.")

def format_author_stats(author_stats):
    lines = []
    if author_stats:
        lines.append("\n### Author Stats\n")
        lines.append("| Author | Changed Features | New Features | Version Bumps | Metadata Issues | Total Actionable |")
        lines.append("|--------|------------------|--------------|---------------|-----------------|------------------|")
        for author, stat in sorted(author_stats.items(), key=lambda x: (-(x[1].get('total_changed',0)), x[0])):
            total_actionable = stat.get('new',0) + stat.get('bump',0) + stat.get('meta',0)
            lines.append(f"| {author} | {stat.get('total_changed',0)} | {stat.get('new',0)} | {stat.get('bump',0)} | {stat.get('meta',0)} | {total_actionable} |")
    return lines

def format_actionable_lines(feature_actions):
    lines = []
    if feature_actions:
        lines.append("\n## Actionable Suggestions\n")
        for fname, info in sorted(feature_actions.items()):
            author = f" ({info['author']})" if info['author'] else ""
            lines.append(f"- **{fname}**{author}: " + "; ".join(info["actions"]))
    return lines

def format_feature_table(feature_analysis):
    lines = []
    lines.append("| Feature | Prior Ver | Proposed Ver | Needs Bump | Change Types | Note | Commit |")
    lines.append("|---------|-----------|--------------|------------|--------------|------|--------|")
    def bold(val, is_attention):
        return f"**{val}**" if is_attention and val != '' and val != '-' else val
    for fa in sorted(feature_analysis, key=lambda item: item['name'].casefold()):
        lines.append(f"| {bold(fa['name'], fa['is_attention'])} | {bold(fa['prior_ver_str'], fa['is_attention'])} | {bold(fa['proposed_ver_str'], fa['is_attention'])} | {bold(str(fa['needs_bump']), fa['is_attention'])} | {bold(fa['change_types'], fa['is_attention'])} | {bold(fa['note'], fa['is_attention'])} | {fa['commit_link']} |")
    return lines

def format_new_features_table(new_features, feature_meta_map, get_commit_author, normalize_name):
    lines = []
    if new_features:
        lines.append(f"### New Features Added ({len(set((n[0], n[1], n[2]) for n in new_features))})\n")
        lines.append("| Feature | INI Version | CSX distribution | Commit |")
        lines.append("|---------|-------------|-------|--------|")
        seen = set()
        for name, ver, commit in new_features:
            key = (name, ver, commit)
            if key in seen:
                continue
            seen.add(key)
            meta = feature_meta_map.get(normalize_name(name))
            missing = not meta
            def boldmeta(val, missing=missing):
                return f"**{val}**" if missing and val != '' and val != '-' else val
            distribution = "Core AIO" if meta else "**Missing package metadata**"
            author = get_commit_author(commit) if commit else None
            author_str = f" ({author})" if author else ""
            commit_link = f"[link](https://github.com/ParticleTroned/skyrim-community-shaders/commit/{commit}){author_str}" if commit else ""
            lines.append(f"| {boldmeta(name)} | {boldmeta(ver)} | {distribution} | {commit_link} |")
    return lines

def format_new_code_table(new_code_features):
    lines = []
    if new_code_features:
        lines.append(f"### New Code Added to Existing Features ({len(new_code_features)})\n")
        lines.append("| Feature | Author |")
        lines.append("|---------|--------|")
        for name, author in new_code_features:
            lines.append(f"| {name} | {author} |")
    return lines

def format_metadata_summary(feature_metadata):
    lines = ["\n## CSX core features\n",
             "All listed features and built-in systems are included in the CSX AIO, sorted alphabetically. Separate feature downloads are not required.\n",
             "| CSX feature | Included in core AIO | Description | Key features |",
             "| --- | --- | --- | --- |"]
    def cell(value):
        return str(value).replace(r'\n', ' ').replace("|", "\\|").replace("\n", " ").replace("\r", " ")
    built_in_systems = [
        {"name": "Shader-cache management",
         "description": "Built-in compatibility-checked shader packs, background compilation and runtime-specific prebuilt caches.",
         "key_features": ["Standard and Horizon Fix Water variants", "Uses the CSX core version"]},
    ]
    for info in sorted([*feature_metadata, *built_in_systems],
                       key=lambda item: item.get('display_name', item['name']).casefold()):
        lines.append(f"| {cell(info.get('display_name', info['name']))} | Yes | "
                     f"{cell(info['description'])} | {cell(', '.join(info['key_features']))} |")
    lines.extend([
        "\n### Built-in CSX systems\n",
        "The module inventory also includes Adaptive Balance. Its Lighting, Bloom and Water profiles are core CSX features.",
        "Performance Tuning is built into the menu: feature-cost controls, CPU/GPU comparisons and saving/restoring tuned defaults. It is separate from Performance Overlay and does not require DevBench.",
        "Shader-cache management, rendering controls and integration APIs are also built in; they follow the CSX release version instead of a separate feature-module version.",
        "Hardware/runtime support and optional companion-plugin dependencies still apply; bundled does not mean enabled by default.",
    ])
    return lines, []


def build_nexus_upload_matrix(feature_metadata, core_mod_id, core_filename, core_artifact_pattern,
                              base_ref=None, core_file_group_id=""):
    """Plan one CSX AIO upload; never inherit destinations from feature INIs."""
    if core_artifact_pattern != "CSX_AIO-*.7z":
        raise ValueError("CSX releases must select the complete CSX_AIO-*.7z package")
    for name, value in (("CSX Nexus mod ID", core_mod_id), ("CSX Nexus file group ID", core_file_group_id)):
        if value and not re.fullmatch(r"[1-9][0-9]*", value):
            raise ValueError(f"{name} must be a positive decimal identifier")
    if core_mod_id == "86492":
        raise ValueError("86492 is the upstream Community Shaders mod, not a CSX upload destination")
    return [{
        "name": "core", "artifact_pattern": core_artifact_pattern,
        "artifact_name": "nexus-upload-core", "nexus_mod_id": core_mod_id,
        "file_group_id": core_file_group_id, "mod_filename": core_filename,
        "file_description": "Complete CSX AIO with all bundled features and the shader-cache FOMOD.",
        "auto_upload": bool(core_mod_id), "changelog": "",
    }]


def build_feature_actions(bump_suggestions, metadata_issues, new_features, get_commit_author, normalize_name):
    consolidated = {}
    display_name_map = {}

    for suggestion in bump_suggestions:
        m = RE_BUMP_SUGGESTION.match(suggestion)
        if m:
            fname, ver, author = m.group(1), m.group(2), m.group(3)
            norm = normalize_name(fname)
            display_name_map.setdefault(norm, fname)
            if ' ' in fname and ' ' not in display_name_map[norm]:
                display_name_map[norm] = fname
            if norm not in consolidated:
                consolidated[norm] = {"actions": [], "author": author}
            consolidated[norm]["actions"].append(f"Bump INI version to `{ver}`")
            if author and not consolidated[norm]["author"]:
                consolidated[norm]["author"] = author

    for name, fields in metadata_issues:
        norm = normalize_name(name)
        display_name_map.setdefault(norm, name)
        if ' ' in name and ' ' not in display_name_map[norm]:
            display_name_map[norm] = name
        if norm not in consolidated:
            consolidated[norm] = {"actions": [], "author": None}
        consolidated[norm]["actions"].append(f"Update INI: add {', '.join(fields)}")

    # Add new features with authors
    for feat_name, _, commit in new_features:
        norm = normalize_name(feat_name)
        display_name_map.setdefault(norm, feat_name)
        if ' ' in feat_name and ' ' not in display_name_map[norm]:
            display_name_map[norm] = feat_name
        author = get_commit_author(commit) if commit else None
        if norm not in consolidated:
            consolidated[norm] = {"actions": [], "author": author}
        if "New feature" not in "".join(consolidated[norm]["actions"]):
            consolidated[norm]["actions"].append("New feature")
        if author and not consolidated[norm]["author"]:
            consolidated[norm]["author"] = author

    feature_actions = {}
    for norm, info in consolidated.items():
        feature_actions[display_name_map[norm]] = info

    # Also assign authors to existing entries if not already set
    new_features_map = {normalize_name(n): (commit, get_commit_author(commit) if commit else None) for n, _, commit in new_features}
    for disp, info in feature_actions.items():
        if not info["author"]:
            norm = normalize_name(disp)
            if norm in new_features_map:
                commit, author = new_features_map[norm]
                if author:
                    info["author"] = author
    return feature_actions

def build_author_stats(feature_analysis, feature_actions):
    # Count all features per author from feature_analysis
    author_stats = {}
    for fa in feature_analysis:
        author = fa.get('author')
        if not author:
            continue
        if author not in author_stats:
            author_stats[author] = {'new': 0, 'bump': 0, 'meta': 0, 'total_changed': 0}
        # Count each feature once per author
        author_stats[author]['total_changed'] += 1

    # Then, count actionable items for authors who have them (from feature_actions)
    for fname, info in feature_actions.items():
        author = info["author"]
        if not author:
            continue
        if author not in author_stats:
            author_stats[author] = {'new': 0, 'bump': 0, 'meta': 0, 'total_changed': 0}
        for action in info["actions"]:
            if action.startswith("Bump INI version"):
                author_stats[author]['bump'] += 1
            elif action.startswith("Update INI"):
                author_stats[author]['meta'] += 1
            elif "new feature" in action.lower() or "new ini" in action.lower():
                author_stats[author]['new'] += 1

    return author_stats

def generate_audit_report(
    base_ref,
    base_date_iso,
    base_date_human,
    now,
    feature_analysis,
    new_features,
    new_code_features,
    feature_meta_map,
    get_commit_author,
    normalize_name,
    feature_metadata,
    bump_suggestions
):
    lines = []
    lines.append("# CSX Feature Audit\n")
    lines.append(f"_Compared to base:_ `{base_ref}`  ")
    if base_date_iso and base_date_human:
        lines.append(f"_Base commit date:_ `{base_date_iso}` ({base_date_human})  ")
    lines.append(f"_Generated:_ `{now}`\n")
    metadata_lines, metadata_issues = format_metadata_summary(feature_metadata)
    lines.extend(metadata_lines)
    lines.append("\n## Component version audit\n")
    lines.extend(format_feature_table(feature_analysis))
    lines.append("\n## Critical Information Summary\n")
    lines.extend(format_new_features_table(new_features, feature_meta_map, get_commit_author, normalize_name))
    lines.extend(format_new_code_table(new_code_features))
    feature_actions = build_feature_actions(bump_suggestions, metadata_issues, new_features, get_commit_author, normalize_name)
    author_stats = build_author_stats(feature_analysis, feature_actions)
    author_stats_lines = format_author_stats(author_stats)
    actionable_lines = format_actionable_lines(feature_actions)
    output = "\n".join(lines)
    output += "\n" + "\n".join(author_stats_lines)
    output += "\n" + "\n".join(actionable_lines)
    return output

def main():
    parser = argparse.ArgumentParser(description="Feature version audit for Community Shaders Expanded (CSX).")
    parser.add_argument('--output', type=str, help='Output markdown filename')
    parser.add_argument('--ci', action='store_true', help='Exit 1 if actionable items found (alias for --fail-on-actionable)')
    parser.add_argument('--base', type=str, default=None, help='Base tag/branch/commit to compare against')
    parser.add_argument('--head', type=str, default=None, help='Head commit/SHA to compare against (defaults to HEAD). Use to compare against a specific revision without checking it out.')
    parser.add_argument('--fail-on-actionable', action='store_true', help='Exit 1 if actionable items found (alias for --ci)')
    parser.add_argument('--pr-check', action='store_true', help='Only show actionable items for changes since base')
    parser.add_argument('--apply-bumps', action='store_true', help='Automatically apply suggested version bumps')
    parser.add_argument('--export-nexus-matrix', action='store_true', help='Export a JSON Nexus upload matrix and exit')
    parser.add_argument('--matrix-output', type=str, default='nexus-matrix.json', help='Output filename for Nexus matrix JSON')
    parser.add_argument('--nexus-metadata-file', type=str, help='Legacy option; rejected because CSX does not publish split feature mods')
    parser.add_argument('--all-features', action='store_true', help='Compatibility option; CSX always exports one complete AIO')
    parser.add_argument('--core-mod-id', type=str, default='', help='Explicit CSX Nexus mod ID for the generated upload matrix')
    parser.add_argument('--core-filename', type=str, default='Community Shaders Expanded (CSX)', help='Core Nexus filename for the generated upload matrix')
    parser.add_argument('--core-artifact-pattern', type=str, default='CSX_AIO-*.7z', help='Core artifact pattern for the generated upload matrix')
    parser.add_argument('--core-file-group-id', default='', help='Explicit CSX Nexus file group ID')
    args = parser.parse_args()

    if args.nexus_metadata_file:
        parser.error('CSX uses an explicit AIO destination, not per-feature Nexus metadata')
    if args.export_nexus_matrix:
        try:
            matrix = build_nexus_upload_matrix(
                [], args.core_mod_id, args.core_filename, args.core_artifact_pattern,
                core_file_group_id=args.core_file_group_id,
            )
        except ValueError as error:
            parser.error(str(error))
        Path(args.matrix_output).write_text(json.dumps(matrix, indent=2), encoding='utf-8')
        print(f'Wrote CSX AIO Nexus matrix to {args.matrix_output}')
        return

    global HEAD_REF
    if args.head:
        HEAD_REF = args.head

    if args.base:
        base_ref = args.base
    else:
        detected_base = detect_pr_base() if args.pr_check else get_latest_release_tag(HEAD_REF)
        if detected_base:
            base_ref = detected_base
        else:
            print("No valid base ref found.", file=sys.stderr)
            sys.exit(1)

    # In PR check mode, determine the last release tag so version proposals are anchored
    # to the release baseline rather than to the tip of the target branch.  This prevents
    # multiple PRs between releases from each requiring an additional version bump.
    # Use --merged base_ref so only tags reachable from the target branch are considered;
    # hotfix tags on unrelated branches cannot skew the baseline.
    release_ref = None
    if args.pr_check:
        release_ref = get_latest_release_tag(base_ref)
        if release_ref:
            print(f"Using release tag for version baseline: {release_ref}", file=sys.stderr)
        else:
            print("No release tag found; falling back to base_ref for version baseline.", file=sys.stderr)

    base_date_iso = None
    base_date_human = None
    try:
        base_date_iso = subprocess.check_output([
            "git", "log", "-1", "--format=%cI", base_ref
        ], stderr=subprocess.DEVNULL).decode("utf-8").strip()
        base_date_human = datetime.datetime.fromisoformat(base_date_iso.replace('Z', '+00:00')).strftime('%A, %B %d, %Y %I:%M %p')
    except Exception:
        pass

    print(f"Using base ref: {base_ref}", file=sys.stderr)
    if base_date_iso:
        print(f"Base commit date: {base_date_iso} ({base_date_human})", file=sys.stderr)

    feature_metadata = extract_feature_metadata(DEFAULT_FEATURE_HEADERS_DIR)
    def normalize_name(name): return normalize_feature_key(name)
    feature_meta_map = {normalize_name(f['name']): f for f in feature_metadata}

    feature_analysis, bump_suggestions, new_features, new_code_features, actionable, feature_actions = analyze_features(
        FEATURES_DIR, feature_meta_map, base_ref, only_changed=args.pr_check, release_ref=release_ref)

    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    date_tag = datetime.datetime.now().strftime('%Y-%m-%d')
    output_file = args.output if args.output else (None if args.pr_check else f"feature-version-audit-{date_tag}.md")

    if args.apply_bumps:
        applied_count = 0
        for fa in feature_analysis:
            if fa['needs_bump'] and fa['ini_path']:
                if apply_version_bump(fa['ini_path'], fa['proposed_ver_str']):
                    print(f"Applied bump to {fa['name']}: {fa['prior_ver_str']} -> {fa['proposed_ver_str']}", file=sys.stderr)
                    applied_count += 1

                    fa['needs_bump'] = False

        print(f"\nSuccessfully applied {applied_count} version bumps." if applied_count > 0 else "\nNo version bumps applied.", file=sys.stderr)

        # Remove stale bump actions from feature_actions
        for fname in list(feature_actions.keys()):
            info = feature_actions[fname]
            info["actions"] = [a for a in info["actions"] if not a.startswith("Needs version bump")]
            if not info["actions"]:
                del feature_actions[fname]

        # Filter bump_suggestions for the full-report path
        bumped_ini_names = {
            os.path.basename(fa['ini_path'])
            for fa in feature_analysis
            if not fa['needs_bump'] and fa.get('ini_path')
        }
        bump_suggestions = [
            s for s in bump_suggestions
            if not any(f"**{name}**" in s for name in bumped_ini_names)
        ]

        # Recompute actionable after applying bumps
        actionable = any(fa.get('needs_bump') or "missing" in fa.get('note', '').lower() for fa in feature_analysis)
        if new_features:
            actionable = True

    if args.pr_check:
        print_actionable_suggestions(feature_actions)
    else:
        output = generate_audit_report(base_ref, base_date_iso, base_date_human, now,
                                      feature_analysis, new_features, new_code_features, feature_meta_map,
                                      get_commit_author, normalize_name, feature_metadata, bump_suggestions)
        if output_file:
            with open(output_file, "w", encoding="utf-8") as f: f.write(output)
        else:
            print(output)

    if actionable and (args.ci or args.fail_on_actionable):
        sys.exit(1)
    sys.exit(0)

if __name__ == "__main__":
    main()
