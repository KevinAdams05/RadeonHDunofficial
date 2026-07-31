#!/usr/bin/env python3
"""Mechanically enforce the parts of docs/STYLE_GUIDE.md that a script can.

This is a *checker*, never a reformatter. The style guide says not to rewrite
working upstream code to match it (§ preamble), and this fork carries files
that came from Haiku — so rewriting them wholesale would create exactly the
unreviewable churn the guide warns about. Every finding here is reported for a
human to act on.

Only files this repository actually carries are checked. The overlay build
pulls the rest from a Haiku source tree, and those are not ours to police.

Adoption is via a baseline: pre-existing findings are recorded in
scripts/style-baseline.txt and reported as "known", so the gate can require
zero *new* findings from day one instead of waiting on a cleanup. Fix a
baselined finding and `--update-baseline` will drop it.

Invoke through python3: this repo keeps core.fileMode false, so nothing
under scripts/ carries an executable bit.

Usage:
  python3 scripts/style-check.py                  # honour the baseline
  python3 scripts/style-check.py --all            # show baselined too
  python3 scripts/style-check.py --changed[=REF]  # files changed vs REF
  python3 scripts/style-check.py --update-baseline
  python3 scripts/style-check.py --list-rules

Exit status is 0 when there are no new findings, 1 otherwise, so it can gate
a release directly.
"""

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(REPO, "scripts", "style-baseline.txt")

TAB_WIDTH = 4
MAX_COLUMNS = 80

SOURCE_EXT = (".c", ".cpp", ".h")
SCRIPT_EXT = (".py", ".sh")
PROSE_EXT = (".md", ".svg")
TEXT_EXT = SOURCE_EXT + SCRIPT_EXT + PROSE_EXT + (".rdef",)

# What each rule applies to. Scope matters more than it looks: the 80-column
# rule is § 1.3 of the *code* guide, and applying it to SVG (machine-emitted,
# one long path per line) or to markdown prose would bury the real findings
# under a thousand false ones. Trailing whitespace is likewise excluded from
# markdown, where two trailing spaces are a hard line break — meaningful, not
# sloppy.
#   "text"   — every tracked text file
#   "code"   — C/C++ plus the shell and Python we wrote
#   "source" — C/C++ only
RULES = {
    "eol-crlf":
        ("text", "Line endings must be LF, not CRLF (.gitattributes)"),
    "no-final-eol":
        ("text", "File does not end with a newline (§2)"),
    "line-too-long":
        ("code", f"Line exceeds {MAX_COLUMNS} columns at tab width {TAB_WIDTH}"
            " (§1.3)"),
    "trailing-space":
        ("code", "Trailing whitespace (§2)"),
    "tab-indent":
        ("source", "Indentation must use tabs, not spaces (§2)"),
    "if-zero":
        ("source", "#if 0 block — delete it, git has the history (§18)"),
    "raw-printf":
        ("source", "Raw printf/fprintf — use TRACE()/ERROR() (§18, §19)"),
    "nullptr":
        ("source", "Use NULL, not nullptr (§10)"),
    "true-false":
        ("source", "Use true/false, not TRUE/FALSE (§11)"),
    "non-ascii-str":
        ("source", "Non-ASCII character inside a string literal (§3)"),
    "trace-error-gate":
        ("source", "_sPrintf declared inside the TRACE #ifdef, but ERROR() is"
            " always on — the file will not build with tracing disabled (§19)"),
}


def describe(rule):
    return RULES[rule][1]


# Directories that hold build output or third-party material rather than
# our sources. Only consulted by the non-git fallback below.
PRUNE_DIRS = {".git", "build", "dist", "testdata", "__pycache__"}


def git(*arguments):
    """Run git, or return None if this is not a usable checkout.

    package.sh calls this script as a release gate, and a release may
    well be built from an unpacked tarball with no .git — so every git
    use has to degrade instead of raising.
    """
    try:
        result = subprocess.run(["git", "-C", REPO, *arguments],
            capture_output=True, text=True)
    except OSError:
        return None
    return result.stdout if result.returncode == 0 else None


def walked_files():
    """Every text file under the repo, for use without git."""
    found = []
    for root, directories, names in os.walk(REPO):
        directories[:] = [d for d in directories if d not in PRUNE_DIRS]
        for name in names:
            if name.endswith(TEXT_EXT):
                full = os.path.join(root, name)
                found.append(os.path.relpath(full, REPO))
    return sorted(found)


def tracked_files():
    """Tracked files plus new ones not yet committed.

    Untracked-but-not-ignored files are included deliberately: a brand
    new source file is exactly the case where a style check earns its
    keep, and leaving it out would mean the release gate never saw it
    until after it was committed.
    """
    out = git("ls-files")
    if out is None:
        print("style-check: not a git checkout — walking the filesystem",
            file=sys.stderr)
        return walked_files()
    names = set(out.splitlines())
    new = git("ls-files", "--others", "--exclude-standard")
    if new is not None:
        names |= set(new.splitlines())
    return [f for f in sorted(names) if f.endswith(TEXT_EXT)]


def changed_files(ref):
    worktree = git("diff", "--name-only", ref)
    staged = git("diff", "--name-only", "--cached")
    if worktree is None or staged is None:
        print(f"style-check: cannot diff against '{ref}' — checking "
            "everything instead", file=sys.stderr)
        return tracked_files()
    names = set(worktree.splitlines()) | set(staged.splitlines())
    return [f for f in sorted(names) if f.endswith(TEXT_EXT)]


def visual_width(line):
    width = 0
    for char in line:
        if char == "\t":
            width = (width // TAB_WIDTH + 1) * TAB_WIDTH
        else:
            width += 1
    return width


def string_literals(line):
    """Double-quoted literals on a line, escapes respected."""
    return re.findall(r'"(?:[^"\\]|\\.)*"', line)


def check_file(path, findings):
    full = os.path.join(REPO, path)
    try:
        raw = open(full, "rb").read()
    except OSError as error:
        print(f"style-check: cannot read {path}: {error}", file=sys.stderr)
        return

    is_source = path.endswith(SOURCE_EXT)
    is_code = is_source or path.endswith(SCRIPT_EXT) or path.endswith(".rdef")

    def add(rule, line_number, detail=""):
        scope = RULES[rule][0]
        if scope == "source" and not is_source:
            return
        if scope == "code" and not is_code:
            return
        findings.append((path, rule, line_number, detail))

    if b"\r\n" in raw:
        add("eol-crlf", 0)

    if len(raw) > 0 and not raw.endswith(b"\n"):
        add("no-final-eol", 0)

    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        return

    if not is_code:
        return

    for number, line in enumerate(text.split("\n"), 1):
        line = line.rstrip("\r")

        if line != line.rstrip():
            add("trailing-space", number)

        if visual_width(line) > MAX_COLUMNS:
            add("line-too-long", number, f"{visual_width(line)} columns")

        if not is_source:
            continue

        for literal in string_literals(line):
            if any(ord(char) > 127 for char in literal):
                add("non-ascii-str", number)
                break

        # Leading spaces used as indentation. Block-comment continuation
        # (" *") and empty lines are legitimate; so is a single space before
        # a '*' in a doxygen block.
        if re.match(r"^ +\S", line) and not re.match(r"^ +\*", line):
            add("tab-indent", number)

        if re.match(r"^\s*#if\s+0\b", line):
            add("if-zero", number)

        # Bare printf/fprintf. The macro definitions themselves are the one
        # legitimate use, and vendored files that define their own are caught
        # by review rather than here.
        if re.search(r"\b(printf|fprintf)\s*\(", line) \
                and not re.search(r"#\s*define|_sPrintf|dprintf", line):
            add("raw-printf", number)

        if re.search(r"\bnullptr\b", line):
            add("nullptr", number)

        if re.search(r"\b(TRUE|FALSE)\b", line):
            add("true-false", number)

    if is_source:
        check_trace_gate(text, add)


def check_trace_gate(text, add):
    """ERROR() is always on, so its backend must be declared unconditionally.

    Found the hard way: several files declare `extern "C" void _sPrintf(...)`
    inside `#ifdef TRACE_<MODULE>`, which builds fine normally and fails to
    compile the moment tracing is switched off — a configuration the style
    guide (§18) requires to keep working.
    """
    if "_sPrintf" not in text or "define ERROR(" not in text:
        return

    lines = text.split("\n")
    depth_at = {}
    depth = 0
    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        if re.match(r"#\s*if(n?def)?\b", stripped):
            depth += 1
        elif re.match(r"#\s*endif\b", stripped):
            depth = max(0, depth - 1)
        depth_at[number] = depth

    declaration = None
    for number, line in enumerate(lines, 1):
        if re.search(r'extern\s+"C".*_sPrintf', line):
            declaration = number
            break
    if declaration is None:
        return

    # Is ERROR() defined outside any conditional while the declaration is
    # inside one?
    error_outside = any(
        re.match(r"#\s*define\s+ERROR\(", line.strip())
            and depth_at[number] == 0
        for number, line in enumerate(lines, 1))

    if error_outside and depth_at[declaration] > 0:
        add("trace-error-gate", declaration)


def load_baseline():
    if not os.path.exists(BASELINE):
        return set()
    known = set()
    for line in open(BASELINE):
        line = line.strip()
        if line and not line.startswith("#"):
            known.add(line)
    return known


def key_of(finding):
    path, rule, line_number, _ = finding
    return f"{path}\t{rule}\t{line_number}"


def main():
    parser = argparse.ArgumentParser(add_help=True,
        description="Check this repo against docs/STYLE_GUIDE.md.")
    parser.add_argument("--all", action="store_true",
        help="report baselined findings too")
    parser.add_argument("--changed", nargs="?", const="HEAD", metavar="REF",
        help="only check files changed against REF (default HEAD)")
    parser.add_argument("--update-baseline", action="store_true",
        help="rewrite the baseline from the current findings")
    parser.add_argument("--list-rules", action="store_true")
    args = parser.parse_args()

    if args.list_rules:
        for rule, (scope, description) in sorted(RULES.items()):
            print(f"{rule:18s} [{scope:6s}] {description}")
        return 0

    files = changed_files(args.changed) if args.changed else tracked_files()
    if not files:
        print("style-check: nothing to check")
        return 0

    findings = []
    for path in files:
        check_file(path, findings)
    findings.sort(key=lambda f: (f[0], f[2], f[1]))

    baseline = set() if (args.all or args.update_baseline) else load_baseline()

    if args.update_baseline:
        with open(BASELINE, "w") as handle:
            handle.write("# Pre-existing style findings, recorded so the "
                "release gate can require zero *new* findings.\n")
            handle.write("# Regenerate with scripts/style-check.py "
                "--update-baseline. Shrinking this file is always welcome.\n")
            handle.write("# Format: <path>\\t<rule>\\t<line>\n")
            for finding in findings:
                handle.write(key_of(finding) + "\n")
        print(f"style-check: baseline written with {len(findings)} finding(s)")
        return 0

    new = [f for f in findings if key_of(f) not in baseline]
    known = len(findings) - len(new)

    for path, rule, line_number, detail in new:
        where = f"{path}:{line_number}" if line_number else path
        suffix = f" ({detail})" if detail else ""
        print(f"{where}: {rule}: {describe(rule)}{suffix}")

    print()
    print(f"style-check: {len(files)} file(s), {len(new)} new finding(s)"
        + (f", {known} baselined" if known else ""))

    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
